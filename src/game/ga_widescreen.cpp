// GGA: widescreen (2026-10-20), step (b): the 3D scene fills the window, the 2D (HUD, menus) stays in the 4:3 center.
// Measured: the default viewport already covers 320 x 240; the black frame inside the 4:3 picture is the scene scissor
// (x 16..304, y 8..198 / 232). RT64 (Expand) widens a scene and corrects its projection by itself when its scissor covers
// the whole frame width, so only the scissor is changed. A first try that widened the game's projections (4:3 hard-coded
// in $a3 before guPerspective 0x800D79E4 / guPerspectiveF 0x800CF98C) squeezed the picture into the 4:3 frame: dropped.
// Hooks in ga.toml.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include "recomp.h"
#include "recomp_ui.h"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"
#include "goemon_config.h"

static constexpr float original_aspect = 4.0f / 3.0f;

// Same rule as recomp_get_target_aspect_ratio of Goemon 1: window aspect in "Expand" mode, 4:3 otherwise.
static float target_aspect() {
    if (ultramodern::renderer::get_graphics_config().ar_option != ultramodern::renderer::AspectRatio::Expand) {
        return original_aspect;
    }
    int width = 0, height = 0;
    recompui::get_window_size(width, height);
    if (width <= 0 || height <= 0) {
        return original_aspect;
    }
    return std::max(static_cast<float>(width) / height, original_aspect);
}

static bool widescreen() {
    return target_aspect() > original_aspect + 0.01f;
}



// The "wall" between players (func_80188D4C, overlay 8, measured 2026-10-21 with a write trace on player 1's position): a
// step is refused when it moves a player away from another one beyond func_800CF2F0(camera) = the HEIGHT seen at the camera
// target (distance eye-target * tan(fov / 2) * 2), i.e. the width of a 4:3 view... minus nothing for the aspect. Hook right
// after that call (0x80188D8C, $f0 = the limit): in widescreen it grows with the view's width (x window aspect / (4/3)).
// func_80188F20 does the same test on the height (+0x54): unchanged, the view's height does not change.
// WALL_EXTRA (user's tests in 16:9, players' distance on a 2000 px wide window, the camera stays centered between them):
// x 1.33 alone -> ~250 px from the edges; x 1.25 more -> at the edges (cut); 1.15 -> a margin of ~150 px.
static constexpr float WALL_EXTRA = 1.15f;
extern "C" void ga_wall_limit(recomp_context* ctx) {
    if (widescreen()) {
        ctx->f0.fl *= target_aspect() / original_aspect * WALL_EXTRA;
    }
}

// Culling of the scenery to the view (func_800E0C44, file 6; found 2026-10-21 by counting the camera's readers): the game
// builds 5 planes from the camera's orientation (guAlignF) and the angle a = fov (camera + 0x30) * 0.8 (D_8010D4B4): top /
// bottom (0, -+cos a, sin a), sides (+-cos a, 0, sin a), far. The sides ignore the aspect, so in widescreen what is outside the
// 4:3 area was not drawn (waterfall, stones). Hook before the 3rd plane (0x800E0E1C, $f20 = cos a, $f22 = sin a, $f24 = -cos a,
// all rewritten before any later use): side angle a' = atan(tan a * aspect / (4/3)), the game's margin kept in proportion.
extern "C" void ga_cull_sides(recomp_context* ctx) {
    if (!widescreen()) {
        return;
    }
    const float a = std::atan2(ctx->f22.fl, ctx->f20.fl);
    if (a <= 0.0f || a >= 1.5f) { // tan undefined near 90 degrees: leave the game's planes
        return;
    }
    const float wide = std::atan(std::tan(a) * target_aspect() / original_aspect);
    ctx->f20.fl = std::cos(wide);
    ctx->f22.fl = std::sin(wide);
    ctx->f24.fl = -ctx->f20.fl;
}

// The level's objects (overlay 8, func_80196160, found 2026-10-22 with a host call stack trace of the object creations):
// every 8 frames func_80196428 creates the level objects whose distance to the camera target is under range[type] (table
// D_801B4508: 150 / 250 / 400 / 30000) and func_80196888 deletes those beyond range[type] * D_801B6548. Ranges made for a 4:3
// view: in widescreen objects at the screen's sides appeared late (the waterfall). Hooks right after the range is loaded
// (spawn 0x801966C4: $f8, delete 0x8019695C: $f10) scale it like the view's width.
extern "C" float ga_spawn_scale(void) {
    return widescreen() ? target_aspect() / original_aspect : 1.0f;
}

// Enemies wake up when a player comes near (overlay 8, func_8019BC40(object, d), read 2026-11-03): the nearest player is
// returned when |player x - object + 0x30| <= d (x along the level's path), else 0. Wake distances (60..120; world 3 lantern,
// module 84: 80 = the 4:3 edge for that camera) were made for a 4:3 view: in widescreen enemies stood still at the screen's
// sides. Distances >= 60 are scaled like the view's width; shorter ones (15..50, attacks) are kept.
// Modules 173 / 175 / 176 / 177 / 179 have their own copy: 3D distance to the nearest player < 70..100 (same rule).
extern "C" float ga_wake_distance_f(float d) {
    return d >= 60.0f ? d * ga_spawn_scale() : d;
}

extern "C" uint32_t ga_wake_distance(uint32_t bits) {
    float d;
    std::memcpy(&d, &bits, sizeof(d));
    d = ga_wake_distance_f(d);
    std::memcpy(&bits, &d, sizeof(d));
    return bits;
}

// Unlock characters option (General menu), read by the character select hook (src/game/ga_hud.c).
extern "C" int ga_unlock_characters(void) {
    return goemon64::get_unlock_characters() == goemon64::UnlockCharacters::On;
}

// Replay castles option (General menu), read by the world map hook in ga.toml (func_08003DC0).
extern "C" int ga_replay_castles(void) {
    return goemon64::get_replay_castles() == goemon64::ReplayCastles::On;
}

// Draw distance option (General menu): 1 or 2 (x4 tried 2026-10-24: no visible change from x2, dropped).
static float view_scale() {
    return goemon64::get_draw_distance() == goemon64::DrawDistance::X2 ? 2.0f : 1.0f;
}

// Scenery culling (func_800E0C44, every 8 frames, read 2026-10-24): the level's scenery is a 3D grid of cells; only the cells
// of a box (world axes, sizes cull + 0x34 / + 0x36 / + 0x38, centered cull + 0x3A ahead of the camera target) are tested,
// each against 5 planes (2 sides, top, bottom, far = camera + 0x38, the projection's far). ga_cull_sides only widened the side
// planes: the box stayed 4:3-sized, so walls, paths and rocks at the screen's sides appeared late (world 2). The box's x / z
// sizes are scaled like the view's width and by the draw distance; its y size and center are kept (y: memory, the cells are
// counted x * y * z). The far plane is NOT changed (user's choice, 2026-10-24): a far x 2 (here and in guPerspective) with the
// fog kept at its original distance (gSPFogPosition rewritten) showed the added scenery all white; without that fix the fog
// moved away. So x 2 only widens the sides, and the fog stays the game's.
extern "C" int32_t ga_cull_box(int32_t size) {
    return (int32_t)(size * ga_spawn_scale() * view_scale());
}

// Thin lines on the ground along the terrain pieces' joins (all resolutions, also 4:3 / native; not on the N64). Measured
// 2026-10-24 (F7 dump of the display list, Lost'n Road): the ground is ONE mesh drawn several times (sub-list 0x030004E0),
// each copy with its own matrix: scale 0x1999 / 65536 = 0.09999 (the game's 0.1, truncated by the fixed-point conversion),
// copies 324 units apart, mesh 3239 raw units long (z -1735..1504) -> 323.87 units: a 0.13 unit gap = 0.15 to 0.4 native
// pixel. The N64 hides it (screen coordinates snapped to 1/4 pixel, coverage antialiasing); RT64 draws it. Castle slope:
// mesh 654 raw units for 655 between copies. The gap is about 1 raw unit whatever the mesh's size.
// 1st fix (scale x 1.005 around the node's ORIGIN, func_800E16A8) opened new gaps (world 2, 2026-10-25: a cliff mesh whose
// origin is 1490 raw units away from its edge moved 0.45 unit; that join had no gap). So each mesh now grows around its
// own CENTER, by SEAM_RAW raw units on each side (at most SEAM_MAX of its size), when it is drawn: func_800D60BC (models
// with a loaded matrix: scenery, trees) copies the node's float matrix to $sp + 0x40 (rows of 4 floats, row vectors),
// translates it by the camera, then converts it (0x800D6154). Mesh = display list node + 0x54, segments 3.. = node + 0x58..
// (physical; the game adds 0x80000000).
static constexpr float SEAM_RAW = 2.0f;
static constexpr float SEAM_MAX = 0.02f;
static float rd_float(uint8_t* rdram, uint32_t addr) {
    int32_t bits = MEM_W(0, (int32_t)addr);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
static void wr_float(uint8_t* rdram, uint32_t addr, float f) {
    int32_t bits;
    std::memcpy(&bits, &f, sizeof(f));
    MEM_W(0, (int32_t)addr) = bits;
}
extern "C" void ga_seam_grow(uint8_t* rdram, uint32_t node, uint32_t mtx) {
    const uint32_t dl = (uint32_t)MEM_W(0x54, (int32_t)node);
    if (dl == 0) {
        return;
    }
    // Bounding box of the mesh's vertices (G_VTX), following its G_DL / G_ENDDL; segments 3..13 from the node.
    auto resolve = [&](uint32_t a) -> uint32_t {
        const uint32_t seg = (a >> 24) & 0xF;
        if (seg == 0) {
            return 0x80000000u | (a & 0x00FFFFFF);
        }
        if (seg < 3 || seg > 13) {
            return 0;
        }
        const uint32_t base = (uint32_t)MEM_W(0, (int32_t)(node + 0x58 + 4 * (seg - 3)));
        return base == 0 ? 0 : (0x80000000u | ((base + (a & 0x00FFFFFF)) & 0x007FFFFF));
    };
    float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
    uint32_t stack[10];
    int sp = 0;
    uint32_t pc = resolve(dl);
    for (int steps = 0; pc != 0 && steps < 4000; steps++) {
        const uint32_t w0 = (uint32_t)MEM_W(0, (int32_t)pc), w1 = (uint32_t)MEM_W(4, (int32_t)pc);
        const uint8_t op = w0 >> 24;
        if (op == 0xDF) {
            if (sp == 0) {
                break;
            }
            pc = stack[--sp];
            continue;
        }
        if (op == 0xDE) {
            if (((w0 >> 16) & 0xFF) == 0 && sp < 10) {
                stack[sp++] = pc + 8;
            }
            pc = resolve(w1);
            continue;
        }
        if (op == 0x01) { // G_VTX
            const uint32_t n = (w0 >> 12) & 0xFF, v = resolve(w1);
            for (uint32_t i = 0; v != 0 && i < n; i++) {
                const uint32_t xy = (uint32_t)MEM_W(0, (int32_t)(v + i * 16)), zf = (uint32_t)MEM_W(4, (int32_t)(v + i * 16));
                const float p[3] = { (float)(int16_t)(xy >> 16), (float)(int16_t)(xy & 0xFFFF), (float)(int16_t)(zf >> 16) };
                for (int k = 0; k < 3; k++) {
                    lo[k] = std::min(lo[k], p[k]);
                    hi[k] = std::max(hi[k], p[k]);
                }
            }
        }
        pc += 8;
    }
    if (lo[0] > hi[0]) {
        return;
    }
    // v' = c + (v - c) * k per axis: row r of the 3x3 x k[r]; translation += sum c[r] * (1 - k[r]) * row r.
    float t[3] = {};
    for (int r = 0; r < 3; r++) {
        const float half = (hi[r] - lo[r]) * 0.5f, c = (hi[r] + lo[r]) * 0.5f;
        const float k = half < 1.0f ? 1.0f : 1.0f + std::min(SEAM_RAW / half, SEAM_MAX);
        for (int col = 0; col < 3; col++) {
            const uint32_t a = mtx + (uint32_t)(r * 16 + col * 4);
            const float m = rd_float(rdram, a);
            t[col] += c * (1.0f - k) * m;
            wr_float(rdram, a, m * k);
        }
    }
    for (int col = 0; col < 3; col++) {
        const uint32_t a = mtx + (uint32_t)(48 + col * 4);
        wr_float(rdram, a, rd_float(rdram, a) + t[col]);
    }
}

// Window aspect for the corner HUD (ga_hud.c): 4/3 when not in widescreen.
extern "C" float ga_screen_aspect(void) {
    return widescreen() ? target_aspect() : original_aspect;
}

static void put(uint8_t* rdram, uint32_t addr, uint32_t w0, uint32_t w1) {
    MEM_W(0, (int32_t)addr) = (int32_t)w0;
    MEM_W(4, (int32_t)addr) = (int32_t)w1;
}

// Start of the frame display list sent to RT64 (func_800013F0 at 0x800015B8, just before the frame's full screen scissor;
// pointer D_800881EC; measured task lists 0x800500D8 / 600D8 / 700D8): RT64 extended commands on (gEXEnable, F3DEX2).
// A first try in the heap of func_800D05E0 did nothing: that heap is not the start of the list sent (extended commands
// were ignored until this fix, 2026-10-20). RT64 also disables the extended GBI at the end of each workload (each
// G_RDPFULLSYNC, rt64_state.cpp "Extended GBI must be enabled again"), so every use of an extended command here is preceded
// by its own gEXEnable.
#define GA_FRAME_GFX 0x800881ECu
extern "C" void ga_frame_start(uint8_t* rdram) {
    const uint32_t gfx = (uint32_t)MEM_W(0, (int32_t)GA_FRAME_GFX);
    put(rdram, gfx, 0xE0525464u, 0x10000064u);
    MEM_W(0, (int32_t)GA_FRAME_GFX) = (int32_t)(gfx + 8);
}

// End of the HUD display list (func_800DE210 after the jal func_800DDFC8, $v0 = HUD gfx end, hook at 0x800DE580): the HUD
// sets the scissor x 16..304 / y 8..232 (DL_BARS) and nothing restores it, so what is drawn after it (pause menu veil...)
// was cut to that area (measured with a display list trace, 2026-10-20). Full screen scissor again (4:3 too: the scene
// fills the screen in every mode) and the HUD's RT64 offset off.
extern "C" uint32_t ga_hud_end(uint8_t* rdram, uint32_t gfx) {
    put(rdram, gfx, 0xE0525464u, 0x10000064u);     // gEXEnable again (see ga_frame_start)
    put(rdram, gfx + 8, 0x64000006u, 0x00800800u); // HUD offset off (gEXSetRectAlign origins NONE, no offsets)
    put(rdram, gfx + 16, 0u, 0u);
    put(rdram, gfx + 24, 0xED000000u, 0x005003C0u);
    return gfx + 32;
}

// HUD position (4:3 and widescreen, every mode): the scene fills the screen down to y 208 when the HUD is shown
// (ga_scene_scissor), so the game's bar (y 198..230, the user's HD art through tools/hud_pack_2p.py) is moved down 10 px to
// cover 208..240: rectangles with a RT64 offset (gEXSetRectAlign, origins NONE so nothing is stretched), HUD scissor down to
// 240 (DL_BARS command 2), the 3D center bubble (y 214 from func_800D0D6C) at 224. The 3P / 4P top blocks (ga_hud.c) turn
// the offset off before they are drawn.
#define GA_DL_BARS_SCISSOR_W1 0x80108C64u /* DL_BARS command 2: ED040020 004C03A0 = x 16..304, y 8..232 */
static constexpr uint32_t hud_down_px = 10;

// func_800DE210, before the jal func_800DDFC8 (hook at 0x800DE578, $s1 = &gfx pointer D_8010FCE0).
extern "C" void gga_watchdog_tick(); // GGA TEMP (freeze of player 4, main.cpp)
extern "C" void ga_hud_begin(uint8_t* rdram, recomp_context* ctx) {
    gga_watchdog_tick();
    // Full screen (was x 16..304 / y 8..232): with the HUD's x 16..304, the row J1 bars + bubble frame + J2 bars covered the
    // scissor's whole width and RT64 stretched the last rect, the frame (oval in widescreen, user's capture 2026-10-23).
    MEM_W(0, (int32_t)(GA_DL_BARS_SCISSOR_W1 - 4)) = (int32_t)0xED000000;
    MEM_W(0, (int32_t)GA_DL_BARS_SCISSOR_W1) = 0x005003C0;
    const uint32_t ptr = (uint32_t)ctx->r17;
    const uint32_t gfx = (uint32_t)MEM_W(0, (int32_t)ptr);
    const uint32_t down = hud_down_px * 4;
    put(rdram, gfx, 0xE0525464u, 0x10000064u);                         // gEXEnable again (see ga_frame_start)
    put(rdram, gfx + 8, 0x64000006u, 0x00800800u);                     // gEXSetRectAlign, origins NONE
    put(rdram, gfx + 16, down, down);                                  // ulx 0 / uly +10 px, lrx 0 / lry +10 px
    MEM_W(0, (int32_t)ptr) = (int32_t)(gfx + 24);
}

// func_800DDB78 (center bubble), before the jal func_800D0D6C (hook at 0x800DDC58): its y ($a2, float 214) moved down with
// its bar.
extern "C" void ga_hud_bubble(uint8_t* rdram, recomp_context* ctx) {
    const float y = 214.f + hud_down_px;
    uint32_t bits;
    std::memcpy(&bits, &y, sizeof(bits));
    ctx->r6 = (gpr)(int32_t)bits;
}

// 2D tile backgrounds (mountains panorama): func_800EE568(gfx, layer) draws 16 x 16 tiles (E4, 1-cycle, dsdx 1.0) from
// x = 16 - (scroll mod 16), at most 18 / 19 columns, starting at map column $t3 = scroll / 16; layer + 0x3A = wraps
// horizontally, + 0x20 = map width, + 0x14 = fixed x origin (0 = scrolling). Measured with a display list trace
// (2026-10-20): the panorama is a grid of 16 x 16 rectangles, x 16..304, y 56..136.
// In widescreen a wrapping layer starts 5 columns earlier in the map and draws 28 / 29 columns (x 16..464 as drawn, i.e.
// -64..384 once moved by -80). Tried and dropped: RT64 extended commands (gEXSetRectAlign + gEXSetScissor): the wide scissor
// is merged into RT64's frame scissor, then nothing "covers the whole width" any more and the 3D scene is broken (tested).
// Used instead: RT64 stretches to the whole window a row of rectangles covering the frame width 0..320 (as the sky lines).
// So each tile is placed COMPRESSED into 0..320 (x' = 160 + (x - 80 - 160) * k, k = (4/3) / aspect, texture step / k) and
// cut to 0..320; RT64's stretch (1 / k) puts it back at its real size and place across the window.
static constexpr int pano_extra_columns = 5;
static bool pano_active = false;
static uint32_t pano_stream_next; // scene list position after our G_DL to the tiles
static uint32_t pano_heap_start;  // first command of the tiles list in the heap

// The layer callback writes into the scene display list (func_800D48F4: gfx = callback(gfx, node), 64 KB buffers): with
// 28 columns instead of 18 that list overflowed and the 3D after it was not drawn (tested). So the tiles go to the game's
// display list heap (D_8010FCE0, 128 KB per frame, used by the HUD) and the scene list only gets a G_DL to them.
#define GA_DL_HEAP      0x8010FCE0u /* current pointer */
#define GA_DL_HEAP_BASE 0x8010FCE8u /* buffer n = base + n * 0x20000, n = byte D_800881DE (func_800D05FC: room left) */
#define GA_DL_FRAME     0x800881DEu
static constexpr uint32_t pano_heap_needed = 0x6000; // 29 columns x 15 rows x 0x28 bytes, and margin

// Hook at 0x800EE840 (start column $t3 and start x $f0 computed, $a2 = layer, $s0 = gfx).
extern "C" void ga_pano_begin(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t layer = (uint32_t)ctx->r6;
    pano_active = false;
    if (!widescreen() || MEM_BU(0x3A, (int32_t)layer) == 0 || MEM_HU(0x14, (int32_t)layer) != 0) {
        return;
    }
    const uint32_t heap = (uint32_t)MEM_W(0, (int32_t)GA_DL_HEAP);
    const uint32_t heap_end = GA_DL_HEAP_BASE + ((uint32_t)MEM_BU(0, (int32_t)GA_DL_FRAME) << 17) + 0x1FFF8u;
    if (heap >= heap_end || heap_end - heap < pano_heap_needed) {
        return; // not enough room: the layer stays 4:3
    }
    const int width = MEM_HU(0x20, (int32_t)layer);
    int column = (int16_t)ctx->r11 - pano_extra_columns;
    while (width > 0 && column < 0) {
        column += width;
    }
    ctx->r11 = (gpr)(int32_t)column;
    const uint32_t stream = (uint32_t)ctx->r16;
    put(rdram, stream, 0xDE000000u, heap); // G_DL to the tiles
    pano_stream_next = stream + 8;
    // The compressed tiles step 1.33 texels per pixel: drawn at the native 320 x 240 (upscale2D = Original) a texel in three
    // was dropped or mixed with the transparent texels at the edges (dark dots on the mountains, green on the roofs, none in
    // 4:3; 2026-10-24). RT64 draws them at the output resolution instead (gEXForceUpscale2D, its own gEXEnable first).
    put(rdram, heap, 0xE0525464u, 0x10000064u); // gEXEnable
    put(rdram, heap + 8, 0x6400000Eu, 1u);      // gEXForceUpscale2D(1)
    // Drawn at full resolution, the game's bilinear filter really blends at each tile's edges: the next tile of the stacked
    // texture (lines every 16 px) and the transparent texels (dark / green outlines). Point sampling, as the native 1:1 look.
    put(rdram, heap + 16, 0xE3001201u, 0u);     // G_SETOTHERMODE_H text filter: point
    pano_heap_start = heap + 24;
    ctx->r16 = (gpr)(int32_t)(heap + 24);
    pano_active = true;
}

// Column limits of the tile loop (hooks at 0x800EF600: $v0 = min(width, 18); 0x800EF634: $a0 = min(width + 1, 19); $t0 =
// map width): 28 / 29 while a widened layer is drawn to the heap, the game's values otherwise.
extern "C" void ga_pano_limit(recomp_context* ctx, int partial) {
    if (!pano_active) {
        return;
    }
    const int width = (int)(int32_t)ctx->r8;
    if (partial) {
        ctx->r4 = (gpr)(int32_t)(width < 28 ? width + 1 : 29);
    } else {
        ctx->r2 = (gpr)(int32_t)(width < 28 ? width : 28);
    }
}

// Copy of the sub-list at dl (KSEG0, up to its G_ENDDL, 32 commands at most) to dst with any text filter setting made point,
// and the G_DL at call redirected to it. Returns the end of the copy. Other G_DL targets are left as they are.
static uint32_t pano_dl_copy(uint8_t* rdram, uint32_t dl, uint32_t dst, uint32_t call) {
    if (dl < 0x80000000u || dl >= 0x80800000u) {
        return dst;
    }
    uint32_t out = dst;
    for (int i = 0; i < 32; i++, dl += 8, out += 8) {
        const uint32_t w0 = (uint32_t)MEM_W(0, (int32_t)dl);
        put(rdram, out, w0, w0 == 0xE3001201u ? 0u : (uint32_t)MEM_W(4, (int32_t)dl));
        if ((w0 >> 24) == 0xDF) {
            MEM_W(4, (int32_t)call) = (int32_t)dst;
            return out + 8;
        }
    }
    return dst; // no G_ENDDL found: the call keeps the game's list
}

// Hook at 0x800EF75C (before the final G_ENDDL, $s0 = gfx in the heap): every tile rectangle of the list is compressed into
// 0..320 (see above), the list is ended and the scene list continues after our G_DL.
extern "C" void ga_pano_end(uint8_t* rdram, recomp_context* ctx) {
    if (!pano_active) {
        return;
    }
    pano_active = false;
    const uint32_t gfx = (uint32_t)ctx->r16;
    const float k = original_aspect / target_aspect();
    uint32_t tail = gfx + 32; // after our end commands: copies of the sub-lists called (pano_dl_copy)
    for (uint32_t a = pano_heap_start; a + 24 <= gfx; a += 8) {
        const uint32_t w0 = (uint32_t)MEM_W(0, (int32_t)a), op = w0 >> 24;
        if (w0 == 0xE3001201u) { // the game's own text filter setting: point too (see ga_pano_begin)
            MEM_W(4, (int32_t)a) = 0;
            continue;
        }
        if (w0 == 0xDE000000u) { // G_DL to the game's tile setup (D_8010C160: sets the filter back to bilinear): to a copy
            tail = pano_dl_copy(rdram, (uint32_t)MEM_W(4, (int32_t)a), tail, a);
            continue;
        }
        if (op != 0xE4 && op != 0xE5) {
            continue;
        }
        const uint32_t w1 = (uint32_t)MEM_W(4, (int32_t)a);
        const uint32_t e1 = (uint32_t)MEM_W(12, (int32_t)a), f1 = (uint32_t)MEM_W(20, (int32_t)a);
        const float ulx = ((w1 >> 12) & 0xFFF) / 4.0f - 80.0f, lrx = ((w0 >> 12) & 0xFFF) / 4.0f - 80.0f;
        const float dsdx = (int16_t)(f1 >> 16) / 1024.0f;
        float nulx = 160.0f + (ulx - 160.0f) * k, nlrx = 160.0f + (lrx - 160.0f) * k;
        float s = (int16_t)(e1 >> 16) / 32.0f;
        if (nlrx <= 0.0f || nulx >= 320.0f) { // fully outside the window: no-op (pipe syncs)
            put(rdram, a, 0xE7000000u, 0);
            put(rdram, a + 8, 0xE7000000u, 0);
            put(rdram, a + 16, 0xE7000000u, 0);
            a += 16;
            continue;
        }
        if (nulx < 0.0f) {
            s += -nulx * dsdx / k;
            nulx = 0.0f;
        }
        if (nlrx > 320.0f) {
            nlrx = 320.0f;
        }
        const uint32_t iulx = (uint32_t)(nulx * 4.0f + 0.5f), ilrx = (uint32_t)(nlrx * 4.0f + 0.5f);
        MEM_W(0, (int32_t)a) = (int32_t)((w0 & 0xFF000FFFu) | ((ilrx & 0xFFF) << 12));
        MEM_W(4, (int32_t)a) = (int32_t)((w1 & 0xFF000FFFu) | ((iulx & 0xFFF) << 12));
        MEM_W(12, (int32_t)a) = (int32_t)((e1 & 0xFFFFu) | ((uint32_t)((int)(s * 32.0f) & 0xFFFF) << 16));
        MEM_W(20, (int32_t)a) = (int32_t)((f1 & 0xFFFFu) | ((uint32_t)((int)(dsdx / k * 1024.0f) & 0xFFFF) << 16));
        a += 16;
    }
    put(rdram, gfx, 0xE0525464u, 0x10000064u);         // gEXEnable
    put(rdram, gfx + 8, 0x6400000Eu, 0u);              // gEXForceUpscale2D(0)
    put(rdram, gfx + 16, 0xE3001201u, 0x00002000u);    // text filter back to bilinear (G_TF_BILERP) for what follows
    put(rdram, gfx + 24, 0xDF000000u, 0u);             // end of the tiles list
    MEM_W(0, (int32_t)GA_DL_HEAP) = (int32_t)tail;
    ctx->r16 = (gpr)(int32_t)pano_stream_next;         // the function ends the scene list here and returns it
}

// func_80004880, scene setup (hook at 0x80004A4C): the scene scissor ED (just written, at $s0 - 8; region D_800881C0 + 0x14,
// x 16..304 / y 8..198 or 232) and the fill region ($a3, $a1, $t4, $a2 = x0, y0, x1, y1, used for the clears that follow)
// become the full screen, as the game's own "no region" case (0, 0, 319, 239). RT64 in Expand mode widens a scene whose
// scissor covers the whole frame width (and corrects its projection itself): rt64_projection_processor.cpp, coversWholeWidth.
extern "C" void ga_scene_scissor(uint8_t* rdram, recomp_context* ctx) {
    // Every mode, 4:3 and widescreen (black bands removed, user's request): the game's y range is 8..198 with its HUD,
    // 8..232 without (func_800DEC40). With the HUD: 0..208, the top of the bottom bar (the game's moved down 10 px in
    // 1P / 2P, ga_hud_begin; ours in 3P / 4P), which hides the scenery's bottom edge as the original band did; without:
    // full screen. RT64 only checks x for coversWholeWidth.
    const uint32_t cmd = (uint32_t)ctx->r16 - 8;
    const bool is_ed = ((uint32_t)MEM_W(0, (int32_t)cmd) >> 24) == 0xED;
    const bool hud = is_ed && ((uint32_t)MEM_W(4, (int32_t)cmd) & 0xFFFu) < 208u * 4u; // its lry: 198 with the HUD
    if (is_ed) {
        put(rdram, cmd, 0xED000000u, hud ? 0x00500340u : 0x005003C0u);
    }
    ctx->r7 = 0;
    ctx->r5 = 0;
    // x1 = 321, not 319: with no sky gradient (world 1 castle, 2026-11-03, RT64 inspector) the scene is cleared by one fill
    // rectangle to x1 - 1 (path 0x80005028), which RT64 reads as lrx 318.75 < 320: not the frame's width, so not stretched
    // (black bands in 16:9). 320.75 covers it; the scissor cuts the excess (and the gradient lines' x1 too).
    ctx->r12 = 0x141;
    ctx->r6 = hud ? 0xCF : 0xEF;
}

// Invincibility blink (func_8018B378, overlay 8; measured 2026-10-22): the game clears the "drawn" bit 0x2 of the player's
// scene node (object + 0x20C) when the frame counter D_800881E8 is odd, so the player is missing every other game frame and
// RT64 (which matches draw calls by combiner / render mode / triangle count, then their matrices) can never interpolate it.
// Instead the node stays drawn and, on those frames, its handler runs between an empty scissor (vertices processed, no pixel):
// the draw call and its matrices exist in every frame, the scissor is not part of RT64's match key. Same blink, at 15 Hz.
static constexpr uint32_t frame_counter = 0x800881E8u;
static constexpr int ghost_max = 8;
static uint32_t ghost_node[ghost_max];
static uint32_t ghost_frame[ghost_max];

static int ghost_find(uint32_t node) {
    for (int i = 0; i < ghost_max; i++) {
        if (ghost_node[i] == node) return i;
    }
    return -1;
}

// Entry of func_8018B378 ($a0 = player object): forget this frame's state of its node.
extern "C" void ga_blink_clear(uint8_t* rdram, recomp_context* ctx) {
    int i = ghost_find((uint32_t)MEM_W(0x20C, ctx->r4));
    if (i >= 0) ghost_node[i] = 0;
}

// Before the "counter & 1" test ($t0 = D_800881E8): an odd frame becomes a hidden-but-drawn one, the game then shows the node.
extern "C" void ga_blink_hide(uint8_t* rdram, recomp_context* ctx) {
    if (!(ctx->r8 & 1)) return;
    uint32_t node = (uint32_t)MEM_W(0x20C, ctx->r4);
    int i = ghost_find(node);
    if (i < 0) i = ghost_find(0);
    if (i < 0) return; // table full: the game hides it as before
    ghost_node[i] = node;
    ghost_frame[i] = (uint32_t)ctx->r8;
    ctx->r8 = 0;
}

// The update and the drawing may not see the same counter value; an older entry (object gone) is ignored.
static bool ghost_now(uint8_t* rdram, uint32_t node) {
    int i = node ? ghost_find(node) : -1;
    return i >= 0 && (uint32_t)MEM_W(0, (int32_t)frame_counter) - ghost_frame[i] <= 1;
}

// Interpolation tags (2026-10-25). Without them RT64 pairs the draws of two frames by a hash (combiner, render mode,
// triangle count) then by the closest matrices: the bones of a carried player's head, or two copies of the same scenery mesh
// whose scene nodes were handed out in another order, got paired with the wrong ones (jerks, textures sliding sideways; gone
// with the original refresh rate, user's test). Each scene node's handler call now runs inside its own matrix group:
// - scenery element: id from (cull data, element index), stable when func_800E16A8 rebuilds the nodes;
// - any other node: a new id each time the node comes out of its free list (func_800D4CEC), so a reused node never
//   interpolates from the object it held before (it snaps for one frame instead).
static std::unordered_map<uint32_t, uint32_t> tag_of_node;
static uint32_t tag_serial = 0;

static uint32_t tag_mix(uint32_t x) {
    x ^= x >> 16; x *= 0x85EBCA6Bu; x ^= x >> 13; x *= 0xC2B2AE35u; x ^= x >> 16;
    return (x == 0 || x == 0xFFFFFFFFu) ? 1 : x; // 0 = G_EX_ID_IGNORE, ~0 = G_EX_ID_AUTO
}

// func_800D4CEC before 0x800D4D50 ($v0 = node taken from the free list).
extern "C" void ga_tag_alloc(recomp_context* ctx) {
    tag_of_node[(uint32_t)ctx->r2] = tag_mix(++tag_serial * 0x9E3779B9u);
}

// func_800E16A8 at 0x800E1B90 ($s2 = scenery node, $s4 = cull data, $s7 = element index).
extern "C" void ga_tag_scenery(recomp_context* ctx) {
    tag_of_node[(uint32_t)ctx->r18] = tag_mix((uint32_t)ctx->r20 ^ ((uint32_t)ctx->r23 * 0x01000193u) ^ 0x5CE0E000u);
}

// gEXEnable + gEXMatrixGroupDecomposedNormal(id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW) as in Goemon 1 (anime.c):
// decomposed, position / rotation / scale / skew / perspective / tile interpolated, vertices and texcoords skipped, look-at
// auto, linear order (the n-th matrix of a node pairs with its n-th matrix of the previous frame).
static void tag_begin(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t node = (uint32_t)ctx->r5;
    auto it = tag_of_node.find(node);
    const uint32_t id = it != tag_of_node.end() ? it->second : tag_mix(node);
    const uint32_t gfx = (uint32_t)ctx->r4;
    put(rdram, gfx, 0xE0525464u, 0x10000064u);
    put(rdram, gfx + 8, 0x6400000Cu, id);
    put(rdram, gfx + 16, 0x02088AADu, 0);
    ctx->r4 += 24;
}

// gEXEnable + gEXPopMatrixGroup(G_MTX_MODELVIEW) after the handler ($v0 = gfx).
static void tag_end(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t gfx = (uint32_t)ctx->r2;
    put(rdram, gfx, 0xE0525464u, 0x10000064u);
    put(rdram, gfx + 8, 0x6400000Du, 1);
    ctx->r2 += 16;
}

// Before the handler call in func_800D48F4 ($a0 = gfx, $a1 = node): gEXEnable, gEXPushScissor, G_SETSCISSOR 0, 0, 0, 0.
// Each extended command needs its own gEXEnable (see ga_frame_start): without it the push / pop were ignored and the empty
// scissor cut the whole scene until the HUD (tested 2026-10-22).
extern "C" void ga_blink_begin(uint8_t* rdram, recomp_context* ctx) {
    tag_begin(rdram, ctx);
    if (!ghost_now(rdram, (uint32_t)ctx->r5)) return;
    put(rdram, (uint32_t)ctx->r4, 0xE0525464u, 0x10000064u);
    put(rdram, (uint32_t)ctx->r4 + 8, 0x64000017u, 0);
    put(rdram, (uint32_t)ctx->r4 + 16, 0xED000000u, 0);
    ctx->r4 += 24;
}

// After it ($v0 = new gfx, $a1 = node reloaded): gEXEnable, gEXPopScissor.
extern "C" void ga_blink_end(uint8_t* rdram, recomp_context* ctx) {
    if (ghost_now(rdram, (uint32_t)ctx->r5)) {
        put(rdram, (uint32_t)ctx->r2, 0xE0525464u, 0x10000064u);
        put(rdram, (uint32_t)ctx->r2 + 8, 0x64000018u, 0);
        ctx->r2 += 16;
    }
    tag_end(rdram, ctx);
}


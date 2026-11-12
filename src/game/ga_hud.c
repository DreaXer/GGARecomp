/*
 * GGA: HUD (2026-10-20, layout of 2026-10-23, user's choice). Bottom: the game's own 2P bar for J1 / J2 in every mode
 * (func_800DDFC8, its textures replaced by the user's HD art, tools/hud_pack_2p.py). 3P / 4P: J3 / J4 blocks at the top,
 * appended here after the game's bar.
 *
 * The bottom HUD is drawn by func_800DDFC8(gfx, state, a2) (file 6): a fixed display list for the orange bars, then one loop
 * "player 0..1" per element, each calling a primitive (gfx, player, record data) that returns the new gfx. Measured facts:
 * notes/REPRISE.md, "HUD — faits MESURÉS". J3 / J4 are drawn by the same primitives as J1 / J2 (their 10-byte record and
 * per-player HUD state swapped into slot 0 / 1 for the call, then swapped back), so no primitive reads past its 2-entry data;
 * the rectangles produced are then moved up to the top blocks (see "Top blocks" below).
 */
#include <stdio.h>
#include "recomp.h"

void func_800DCF2C_59814C(uint8_t* rdram, recomp_context* ctx); /* life / armor pips (gfx, j, life, armor) */
void func_800DD0B8_5982D8(uint8_t* rdram, recomp_context* ctx); /* face (gfx, j, character) */
void func_800DD83C_598A5C(uint8_t* rdram, recomp_context* ctx); /* weapon frame (gfx, state, j, character, [sp+0x10] level) */
void func_800DDB78_598D98(uint8_t* rdram, recomp_context* ctx); /* center bubble (gfx, a2, 0) */
void func_800DD218_598438(uint8_t* rdram, recomp_context* ctx); /* lives "x02" (gfx, j, lives - 1) */
void func_800DDCE8_598F08(uint8_t* rdram, recomp_context* ctx); /* money counter (gfx, j, state) */

int gga_port_has_controller(int port); /* src/game/input.cpp */
void func_800D8CF8_593F18(uint8_t* rdram, recomp_context* ctx); /* (port, button mask) -> hold frames (1 = pressed now) */
void func_800D8CA0_593EC0(uint8_t* rdram, recomp_context* ctx); /* (port, x): called by the join states with 1 / 0 */
void func_800DE99C_599BBC(uint8_t* rdram, recomp_context* ctx); /* (character, +1 / -1) -> next free character */
void func_800DE7FC_599A1C(uint8_t* rdram, recomp_context* ctx); /* (port, character): creates the player */
void func_80008B6C_976C(uint8_t* rdram, recomp_context* ctx);   /* (sound id) */

#define REC(p)      (0x80088282u + 10u * (uint32_t)(p)) /* player record: +0 character, +1 lives, +2 life, +3 armor, +6 weapon */
#define REC_SIZE    10
#define DL_BARS     0x80108C50u /* orange bars: cmd 0..3 setup, 4..14 J1 left, 15..25 J1 right, 26..47 J2, 48..55 pips texture */
#define ST_COUNTER  0x18u       /* HUD state: rolling money value, word per player */
#define ST_WEAPON   0x2Au       /* HUD state: weapon frame byte per player */

/*
 * Top blocks (3P / 4P, user's art hud_3p.png / hud_4p.png, 2026-10-23): J3 above the J1 half of the bottom bar (4:3 x 16..144),
 * J4 above the J2 half (176..304), against the top edge, in the 4:3 area like the bar (also in widescreen). hud_4p.png has the
 * layout of the game's J2 art: J4 is drawn as J2 (slot 1), J3 as J1 (slot 0), so the game lays each block out itself; its
 * rects are moved up by 198 (the bar's top in the game). The art: proxy textures (ga_hud_top.h, the art scaled down to two
 * 64 x 32 halves per block) written into the display list; the texture pack maps their hashes to the HD art.
 * Coordinates are in "wide units" (0..wide_w, 240 high; the 4:3 area starts at ox), drawn at their real size: hud_align()
 * sets RT64's rect alignment to origins CENTER with x relative to the window center (2026-11-08). Before, rect() compressed
 * them into 0..320 with invisible anchors at x 0 and 320 for RT64 to stretch each row back: each edge was rounded twice
 * (1/4 native px, then the output pixel at a non integer scale), so the digits' spacing and the boxes' lines changed with
 * every pixel of window width (user's captures), as the panorama's tiles did (ga_widescreen.cpp).
 * Tried before (2026-10-20 / 23, dropped by the user): 2 rows of one-line blocks at the bottom; a row over the whole width;
 * the game's J1 block in the 4 corners (J2 / J4 mirrored) with a wood bar between the bottom ones (tools/hud_wood.py, removed).
 */
static uint32_t rw(uint8_t* rdram, uint32_t a);
static void ww(uint8_t* rdram, uint32_t a, uint32_t v);
static uint32_t put(uint8_t* rdram, uint32_t g, uint32_t w0, uint32_t w1);

float ga_screen_aspect(void); /* src/game/ga_widescreen.cpp */
#define BLK_W 128.f
#define BLK_H 32.f
#define BAR_Y 198.f /* top of the game's bar */
static const float top_x[2] = { 16.f, 176.f }; /* J3 / J4 block left, 4:3 x (the bar's J1 / J2 halves) */
static float wide_w, ox; /* screen width and left of the 4:3 area, wide units */

static void wide_layout(void) {
    const float aspect = ga_screen_aspect();
    wide_w = 240.f * aspect;
    ox = (float)(int)(wide_w / 2.f) - 160.f;
}

/* 1-cycle texture rectangle (exclusive bottom right): screen rect, texel origin, texels per pixel. */
static uint32_t last_fc0 = 0xFCFFFFFFu, last_fc1 = 0xFFFCF279u; /* last combiner written by put() */

static uint32_t rect_raw(uint8_t* rdram, uint32_t g, int tile, int ulx, int uly, int lrx, int lry, float s, float t, float dsdx, float dtdy) {
    g = put(rdram, g, 0xE4000000u | ((uint32_t)(lrx & 0xFFF) << 12) | (uint32_t)(lry & 0xFFF),
            ((uint32_t)tile << 24) | ((uint32_t)(ulx & 0xFFF) << 12) | (uint32_t)(uly & 0xFFF));
    g = put(rdram, g, 0xE1000000u, ((uint32_t)((int)(s * 32.f) & 0xFFFF) << 16) | (uint32_t)((int)(t * 32.f) & 0xFFFF));
    return put(rdram, g, 0xF1000000u, ((uint32_t)((int)(dsdx * 1024.f) & 0xFFFF) << 16) | (uint32_t)((int)(dtdy * 1024.f) & 0xFFFF));
}

static uint32_t rect(uint8_t* rdram, uint32_t g, int tile, float x, float y, float w, float h, float s, float t, float dsdx, float dtdy) {
    int ulx, uly, lrx, lry;
    if (y < 0.f) { /* above the screen (the face of a top block starts at y -1): cut, a negative y would wrap off screen */
        t -= y * dtdy;
        h += y;
        y = 0.f;
        if (h <= 0.f) {
            return g;
        }
    }
    ulx = (int)(x * 4.f + 0.5f); uly = (int)(y * 4.f + 0.5f);
    lrx = (int)((x + w) * 4.f + 0.5f); lry = (int)((y + h) * 4.f + 0.5f);
    return rect_raw(rdram, g, tile, ulx, uly, lrx, lry, s, t, dsdx, dtdy);
}

/* RT64 rect alignment for the wide units (see the header): origins CENTER, x relative to the window center (RT64 adds 160,
   the left of the 4:3 area is ox), y moved down by down_q (1/4 px). RT64 keeps such rects at their real size (no stretch)
   and snaps them to the native pixel grid. gEXEnable first (ga_frame_start). */
#define HUD_DOWN_Q 40u /* the bar 10 px down (hud_down_px of ga_widescreen.cpp) */
static uint32_t hud_align(uint8_t* rdram, uint32_t g, uint32_t down_q) {
    const uint32_t dx = (uint32_t)(-(int)((ox + 160.f) * 4.f)) & 0xFFFFu;
    g = put(rdram, g, 0xE0525464u, 0x10000064u);
    g = put(rdram, g, 0x64000006u, 0x00200200u); /* gEXSetRectAlign, origins CENTER / CENTER */
    return put(rdram, g, (dx << 16) | down_q, (dx << 16) | down_q);
}

/* Loads a 64 x 32 RGBA16 texture as tile 0 (same commands as the game's bars, DL_BARS 4..11, other address). */
static uint32_t load_bars_tex(uint8_t* rdram, uint32_t g, uint32_t tex) {
    g = put(rdram, g, 0xE7000000u, 0);
    g = put(rdram, g, 0xFD100000u, tex);
    g = put(rdram, g, 0xF5100000u, 0x07080200u);
    g = put(rdram, g, 0xE6000000u, 0);
    g = put(rdram, g, 0xF3000000u, 0x077FF080u);
    g = put(rdram, g, 0xE7000000u, 0);
    g = put(rdram, g, 0xF5102000u, 0x00080200u);
    return put(rdram, g, 0xF2000000u, 0x000FC07Cu);
}

/* 1: the user's HD HUD art (texture pack build/gga_hud_2p.rtz, J3 / J4 proxy textures, element places measured on the art);
   0: the game's own HUD art for all 4 players, elements where the game puts them (user's choice 2026-11-03, for now). */
#define GGA_HD_HUD 0

#if GGA_HD_HUD
/* One top block half (proxy texture, see "Top blocks"): written into the display list (branched over), loaded, drawn. */
#include "ga_hud_top.h"
static uint32_t top_half(uint8_t* rdram, uint32_t g, int i, float x) {
    const uint32_t tex = g + 8;
    int k;
    g = put(rdram, g, 0xDE010000u, tex + sizeof(top_tex[0])); /* G_DL branch over the texels */
    for (k = 0; k < 64 * 32; k += 2) {
        ww(rdram, tex + 2u * k, ((uint32_t)top_tex[i][k] << 16) | top_tex[i][k + 1]);
    }
    g = load_bars_tex(rdram, tex + sizeof(top_tex[0]), tex);
    return rect(rdram, g, 0, x, 0.f, 64.f, BLK_H, 0.f, 0.f, 1.f, 1.f);
}
#endif

/*
 * Rewrites what a game primitive wrote at [start, end): copy-mode settings become 1-cycle ones (copy mode cannot scale for the
 * widescreen compression), the HUD display lists it calls are inlined for the same reason, and every texture rectangle is
 * moved by (dx, dy) in wide units (+ dx_low for the lower row, game y >= 210: the coin under the face), its center x
 * spread by k around xc (counter digits: the HD boxes are wider apart).
 * src_copy = whether the source rectangles use copy-mode conventions (inclusive bottom right, dsdx 4.0).
 */
#define MAX_CMDS 512
static uint32_t src_w0[MAX_CMDS], src_w1[MAX_CMDS];
static int src_copy;

static uint32_t rewrite(uint8_t* rdram, uint32_t start, uint32_t end, float dx, float dy, float xc, float k, float dx_low) {
    uint32_t a, g = start;
    int n = 0, i;
    for (a = start; a < end && n < MAX_CMDS; a += 8) { /* source, HUD display lists inlined */
        uint32_t w0 = rw(rdram, a), w1 = rw(rdram, a + 4);
        if ((w0 >> 24) == 0xDE && w1 >= 0x80108B00u && w1 < 0x80109200u) {
            uint32_t d;
            for (d = w1; (rw(rdram, d) >> 24) != 0xDF && n < MAX_CMDS; d += 8) {
                src_w0[n] = rw(rdram, d);
                src_w1[n++] = rw(rdram, d + 4);
            }
            continue;
        }
        src_w0[n] = w0;
        src_w1[n++] = w1;
    }
    g = put(rdram, g, 0xE3001201u, 0); /* point sampling, as copy mode */
    for (i = 0; i < n; i++) {
        uint32_t w0 = src_w0[i], w1 = src_w1[i], op = w0 >> 24;
        if (w0 == 0xE3000A01u) { /* cycle type */
            src_copy = (w1 & 0x00300000u) == 0x00200000u;
            g = put(rdram, g, w0, 0);
            continue;
        }
        if (w0 == 0xE200001Cu) { /* render mode: the one of the game's 1-cycle HUD texts */
            g = put(rdram, g, w0, 0x00504240u);
            continue;
        }
        if ((op == 0xE4 || op == 0xE5) && i + 2 < n) {
            const float lrx = ((w0 >> 12) & 0xFFF) / 4.f, lry = (w0 & 0xFFF) / 4.f;
            const float ulx = ((w1 >> 12) & 0xFFF) / 4.f, uly = (w1 & 0xFFF) / 4.f;
            const int tile = (int)((w1 >> 24) & 7);
            const float s = (int16_t)(src_w1[i + 1] >> 16) / 32.f, t = (int16_t)(src_w1[i + 1] & 0xFFFF) / 32.f;
            const float tx = (int16_t)(src_w1[i + 2] >> 16) / (src_copy ? 4096.f : 1024.f);
            const float ty = (int16_t)(src_w1[i + 2] & 0xFFFF) / 1024.f;
            const float w = lrx - ulx + (src_copy ? 1.f : 0.f), h = lry - uly + (src_copy ? 1.f : 0.f);
            i += 2;
            g = rect(rdram, g, tile, xc + (ulx + w / 2.f - xc) * k + dx + (uly >= 210.f ? dx_low : 0.f) - w / 2.f, uly + dy, w, h,
                     s, t, tx, ty);
            continue;
        }
        g = put(rdram, g, w0, w1);
    }
    return g;
}

static uint32_t st_counter[4]; /* players 2 / 3 per-player HUD state (players 0 / 1 use the game's own fields) */
static uint8_t st_weapon[4];   /* weapon frame byte: 1 "OK", 2 cross, character + 3 head, | 0x80 / 0 blink, as the game */
static uint32_t st_owner;      /* HUD state these values belong to (a new HUD object resets them) */

static uint32_t rw(uint8_t* rdram, uint32_t a) { return (uint32_t)MEM_W(0, (int32_t)a); }
static void ww(uint8_t* rdram, uint32_t a, uint32_t v) { MEM_W(0, (int32_t)a) = (int32_t)v; }
static uint8_t rb(uint8_t* rdram, uint32_t a) { return (uint8_t)MEM_B(0, (int32_t)a); }
static void wb(uint8_t* rdram, uint32_t a, uint8_t v) { MEM_B(0, (int32_t)a) = (int8_t)v; }

static int rec_valid(uint8_t* rdram, int p) { int8_t c = (int8_t)rb(rdram, REC(p)); return c >= 0 && c < 4; }

/* Last combiner written (put() keeps it for the anchors of rect()). */
static uint32_t put(uint8_t* rdram, uint32_t g, uint32_t w0, uint32_t w1) {
    if ((w0 >> 24) == 0xFC) {
        last_fc0 = w0;
        last_fc1 = w1;
    }
    ww(rdram, g, w0);
    ww(rdram, g + 4, w1);
    return g + 8;
}

/* A display list of the HUD copied inline (through put(): its combiner is tracked for the anchors of rect()). */
static uint32_t inline_dl(uint8_t* rdram, uint32_t g, uint32_t dl) {
    for (; (rw(rdram, dl) >> 24) != 0xDF; dl += 8) {
        g = put(rdram, g, rw(rdram, dl), rw(rdram, dl + 4));
    }
    return g;
}

static uint32_t copy_cmds(uint8_t* rdram, uint32_t g, uint32_t dl, int first, int last) {
    int i;
    for (i = first; i <= last; i++) {
        g = put(rdram, g, rw(rdram, dl + 8u * i), rw(rdram, dl + 8u * i + 4));
    }
    return g;
}

/* Player p's record and HUD state into slot j (swap_in) and back (swap_out). Players 2 / 3 keep their HUD state in
 * st_counter / st_weapon (slot j's are the game's fields at ST_COUNTER + 4 j / ST_WEAPON + j). */
static uint8_t save_rec[REC_SIZE];
static uint32_t save_counter;
static uint8_t save_weapon;

static void swap_in(uint8_t* rdram, uint32_t state, int p, int j) {
    int i;
    for (i = 0; i < REC_SIZE; i++) {
        save_rec[i] = rb(rdram, REC(j) + i);
        wb(rdram, REC(j) + i, rb(rdram, REC(p) + i));
    }
    save_counter = rw(rdram, state + ST_COUNTER + 4u * j);
    save_weapon = rb(rdram, state + ST_WEAPON + j);
    ww(rdram, state + ST_COUNTER + 4u * j, st_counter[p]);
    wb(rdram, state + ST_WEAPON + j, st_weapon[p]);
}

static void swap_out(uint8_t* rdram, uint32_t state, int p, int j) {
    int i;
    for (i = 0; i < REC_SIZE; i++) { /* the primitives may write the record (lives / money clamps) */
        wb(rdram, REC(p) + i, rb(rdram, REC(j) + i));
        wb(rdram, REC(j) + i, save_rec[i]);
    }
    st_counter[p] = rw(rdram, state + ST_COUNTER + 4u * j);
    st_weapon[p] = rb(rdram, state + ST_WEAPON + j);
    ww(rdram, state + ST_COUNTER + 4u * j, save_counter);
    wb(rdram, state + ST_WEAPON + j, save_weapon);
}

/* Copy of the context for a nested call. f_odd points INTO the context (odd float registers, FR = 0): rebased onto the copy,
 * or the callee's mtc1 $f11 etc. write the caller's registers while its doubles read stale halves (2026-10-23: J3 / J4
 * joining via callf got a garbage jump speed in func_8018AF8C, 0.5 in $f10:$f11; the jump height changed at each launch). */
static recomp_context ctx_copy(recomp_context* ctx) {
    recomp_context c = *ctx;
    c.f_odd = (uint32_t*)((uint8_t*)&c + ((uint8_t*)ctx->f_odd - (uint8_t*)ctx));
    return c;
}

/* Calls a primitive with (gfx, a1, a2, a3[, stack arg]) and returns the new gfx. */
static uint32_t call(uint8_t* rdram, recomp_context* ctx, void (*f)(uint8_t*, recomp_context*),
                     uint32_t gfx, int32_t a1, int32_t a2, int32_t a3, int32_t sarg) {
    recomp_context c = ctx_copy(ctx);
    c.r29 = ctx->r29 - 0x38; /* same frame size as func_800DDFC8 */
    MEM_W(0x10, c.r29) = sarg;
    c.r4 = (gpr)(int32_t)gfx;
    c.r5 = (gpr)a1;
    c.r6 = (gpr)a2;
    c.r7 = (gpr)a3;
    f(rdram, &c);
    return (uint32_t)c.r2;
}

/* Number of HUD slots: 4 when a 3rd controller is plugged in or player 3 is in the game (an absent J4 shows its cross), else 2. */
static int hud_players(uint8_t* rdram) {
    return gga_port_has_controller(2) || rec_valid(rdram, 2) ? 4 : 2;
}

/* A new HUD object (zone change...): players 2 / 3 counters start at their money, like the game does for players 0 / 1. */
static void hud_owner(uint8_t* rdram, uint32_t state) {
    int p;
    if (st_owner == state) {
        return;
    }
    st_owner = state;
    for (p = 2; p < 4; p++) {
        st_counter[p] = (uint32_t)((int16_t)MEM_H(0, (int32_t)(REC(p) + 4)) * 4);
        st_weapon[p] = 0;
    }
}

/*
 * The game's bar: its bubble frame (D_80108E18, copy-mode rect x 144..176 / y 198..230) came out stretched in
 * widescreen while the bars beside it did not (user's captures, 2026-10-23). RT64 stretches a rect that covers the scissor
 * width, alone or with its row, or whose texture is a tile copy (rt64_framebuffer_renderer.cpp, coversScissorWidth): only the
 * last fits (supposed: the bubble's sphere texture, drawn just before, is a framebuffer). So ga_hud4 draws the frame here (1-cycle; at its real size with hud_align, never stretched).
 */
static uint32_t one_cycle(uint8_t* rdram, uint32_t g) {
    g = put(rdram, g, 0xE7000000u, 0);
    g = put(rdram, g, 0xE3000A01u, 0);
    g = put(rdram, g, 0xE3001201u, 0);           /* point sampling, as copy mode */
    g = put(rdram, g, 0xE200001Cu, 0x00504240u);
    return put(rdram, g, 0xFCFFFFFFu, 0xFFFCF279u); /* texture color and alpha (as D_80108EC0) */
}

uint32_t ga_hud_frame(uint8_t* rdram, uint32_t g) {
    wide_layout();
    g = one_cycle(rdram, g);
    g = copy_cmds(rdram, g, 0x80108E18u, 1, 8);
    return rect(rdram, g, 0, ox + 144.f, BAR_Y, 32.f, 32.f, 0.f, 0.f, 1.f, 1.f);
}

/*
 * Element places on the user's HD art (measured 2026-10-23 on the art, in game pixels, against the original art the game lays
 * its elements out for; user's captures). Per player J1..J4: x shift of the pips (and the empty ones), of the face + coin,
 * of what the weapon frame shows (weapon, "OK", cross, head), of the lives text; the coin moves with the face plus coin_dx;
 * counter digits: each one to the center of its HD box (first box center original / HD, block coords, the HD boxes' spacing
 * over the original's 12, and box_dy: the HD boxes are higher, white inside measured y 16..28.75 original, J1 / J2 12.25..28.25,
 * J3 12.25..25.5, J4 15.5..25.25). User's tests: coin further left for J1..J3, J2 / J4 weapon further left.
 * Original boxes: J1 art 93 / 105 / 117, J2 art 60 / 72 / 84. HD: J1 (bar) 88.25 / 101.5 / 114.75, J2 (bar) 55.1 / 68.5 /
 * 82.25, J3 (hud_3p) 88 / 100.9 / 114, J4 (hud_4p) 63.5 / 76 / 88.9. The rest: the user's estimates, measured roughly.
 */
enum { E_PIPS, E_FACE, E_WEAPON, E_LIVES, E_COUNT };
#if !GGA_HD_HUD /* the game's art: no shift, digits in the game's boxes */
static const float elem_dx[4][E_COUNT] = { { 0.f } };
static const float box_orig[4] = { 93.f, 60.f, 93.f, 60.f }, box_hd[4] = { 93.f, 60.f, 93.f, 60.f };
static const float box_k[4] = { 1.f, 1.f, 1.f, 1.f };
static const float coin_dx[4] = { 0.f };
static const float box_dy[4] = { 0.f };
#else
static const float elem_dx[4][E_COUNT] = {
    { -3.f, -3.f, 0.f, 0.f },  /* J1 */
    { -3.f, -3.f, -3.f, 0.f }, /* J2 */
    { -3.f, -3.f, 0.f, 0.f },  /* J3 */
    { 3.f, 3.f, 0.f, 0.f },    /* J4 */
};
static const float box_orig[4] = { 93.f, 60.f, 93.f, 60.f }, box_hd[4] = { 88.25f, 54.1f, 87.f, 62.5f }; /* J2..J4 -1: user's test */
static const float box_k[4] = { 26.5f / 24.f, 27.15f / 24.f, 26.f / 24.f, 25.4f / 24.f };
static const float coin_dx[4] = { -2.f, -2.f, -2.f, 0.f };
static const float box_dy[4] = { -2.f, -1.5f, -1.3f, -1.5f }; /* J3 measured -3.3, +2 after the user's tests */
#endif
static const float blk_game_x[2] = { 16.f, 176.f }; /* the game's J1 / J2 block left */

/*
 * Hook at the entry of func_800DDFC8: the whole HUD, every mode (returns 1, new gfx in $v0; user's layout of 2026-10-23).
 * Bottom (RT64 offset +10 px from ga_hud_begin): the game's 2P bar background, its elements for J1 / J2 (drawn natively,
 * j = p) moved by elem_dx, the bubble and its frame. 3P / 4P top: J3 / J4 blocks, their elements drawn as J1 / J2 (record
 * and HUD state swapped into slot 0 / 1), moved up by 198. Before 2026-10-23 the game drew the bar itself (copy mode).
 */
static const uint32_t bar_tex[4] = { 0x03000000u, 0x03001000u, 0x03002800u, 0x03003800u };
static const float bar_x[4] = { 16.f, 80.f, 176.f, 240.f };
int ga_hud4(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t state = (uint32_t)ctx->r5;
    const int n = hud_players(rdram);
    uint32_t g = (uint32_t)ctx->r4, start;
    int p, top;
    hud_owner(rdram, state);
    wide_layout();

    /* the game's HUD settings (DL_BARS 0..3), full screen scissor, 1-cycle mode for everything; the bar background */
    g = hud_align(rdram, g, HUD_DOWN_Q);
    g = copy_cmds(rdram, g, DL_BARS, 0, 3);
    g = put(rdram, g, 0xED000000u, 0x005003C0u);
    g = one_cycle(rdram, g);
    for (p = 0; p < 4; p++) {
        g = load_bars_tex(rdram, g, bar_tex[p]);
        g = rect(rdram, g, 0, ox + bar_x[p], BAR_Y, 64.f, BLK_H, 0.f, 0.f, 1.f, 1.f);
    }

    /* the elements of player p: drawn by the game as player j = p & 1 (players 2 / 3 swapped into that slot), moved by
       (ox + elem_dx, dy): dy 0 for the bottom bar, -198 for the top blocks */
#define EACH(first, last, draw, e)                                          \
    for (p = (first); p <= (last); p++) {                                   \
        const int j = p & 1;                                                \
        if (p != j) swap_in(rdram, state, p, j);                            \
        start = g;                                                          \
        g = draw;                                                           \
        g = rewrite(rdram, start, g, ox + elem_dx[p][e], p < 2 ? 0.f : -BAR_Y, 0.f, 1.f, e == E_FACE ? coin_dx[p] : 0.f); \
        if (p != j) swap_out(rdram, state, p, j);                           \
    }
#define ELEMENTS(first, last)                                                                                         \
    g = copy_cmds(rdram, g, DL_BARS, 48, 55); /* pips / faces texture */                                              \
    src_copy = 1;                                                                                                     \
    EACH(first, last, call(rdram, ctx, func_800DCF2C_59814C, g, j, (int8_t)rb(rdram, REC(j) + 2), (int8_t)rb(rdram, REC(j) + 3), 0), E_PIPS) \
    EACH(first, last, call(rdram, ctx, func_800DD0B8_5982D8, g, j, (int8_t)rb(rdram, REC(j)), 0, 0), E_FACE) /* face, coin */ \
    EACH(first, last, call(rdram, ctx, func_800DD83C_598A5C, g, (int32_t)state, j, (int8_t)rb(rdram, REC(j)), (int8_t)rb(rdram, REC(j) + 6)), E_WEAPON)

#define TEXTS(first, last)                                                                                            \
    g = inline_dl(rdram, g, 0x80108E90u); /* texts: setup and font texture, as the game's bar */                     \
    g = put(rdram, g, 0xE7000000u, 0);                                                                                \
    g = put(rdram, g, 0xFD700000u, 0x03006480u);                                                                      \
    g = put(rdram, g, 0xF5700000u, 0x07080200u);                                                                      \
    g = put(rdram, g, 0xE6000000u, 0);                                                                                \
    g = put(rdram, g, 0xF3000000u, 0x0715F800u);                                                                      \
    g = put(rdram, g, 0xE7000000u, 0);                                                                                \
    g = put(rdram, g, 0xF5680200u, 0x00080200u);                                                                      \
    g = put(rdram, g, 0xF2000000u, 0x0001C15Cu);                                                                      \
    for (p = (first); p <= (last); p++) {                                                                             \
        const int j = p & 1;                                                                                          \
        int8_t lives;                                                                                                 \
        if (p != j) swap_in(rdram, state, p, j);                                                                      \
        lives = (int8_t)rb(rdram, REC(j) + 1);                                                                        \
        if (lives >= 101) {                                                                                           \
            wb(rdram, REC(j) + 1, 100);                                                                               \
            lives = 100;                                                                                              \
        }                                                                                                             \
        start = g;                                                                                                    \
        g = call(rdram, ctx, func_800DD218_598438, g, j, lives - 1, 0, 0);                                            \
        src_copy = 0; /* D_80108E90 above: 1-cycle */                                                                 \
        g = rewrite(rdram, start, g, ox + elem_dx[p][E_LIVES], p < 2 ? 0.f : -BAR_Y, 0.f, 1.f, 0.f);                 \
        if (p != j) swap_out(rdram, state, p, j);                                                                     \
    }                                                                                                                 \
    g = inline_dl(rdram, g, 0x80109030u);                                                                             \
    src_copy = 0;                                                                                                     \
    for (p = (first); p <= (last); p++) { /* counter digits, each to its HD box */                                    \
        const int j = p & 1;                                                                                          \
        const float xc = blk_game_x[j] + box_orig[p];                                                                 \
        if (p != j) swap_in(rdram, state, p, j);                                                                      \
        start = g;                                                                                                    \
        g = call(rdram, ctx, func_800DDCE8_598F08, g, j, (int32_t)state, 0, 0);                                       \
        g = rewrite(rdram, start, g, ox + box_hd[p] - box_orig[p], (p < 2 ? 0.f : -BAR_Y) + box_dy[p], xc, box_k[p], 0.f); \
        if (p != j) swap_out(rdram, state, p, j);                                                                     \
    }

    ELEMENTS(0, 1)
    /* the bubble (3D, orthographic) gets a scissor around itself (x 144..176, y 208..240): with a wider one RT64 gives it its
       wide viewport (oval in widescreen); then its frame (ga_hud_frame) */
    g = put(rdram, g, 0xED240340u, 0x002C03C0u);
    g = call(rdram, ctx, func_800DDB78_598D98, g, (int32_t)ctx->r6, 0, 0, 0);
    g = put(rdram, g, 0xED000000u, 0x005003C0u);
    g = ga_hud_frame(rdram, g);
    TEXTS(0, 1)

    if (n == 4) {
        /* RT64's HUD offset off (the top blocks stay against the top edge), the blocks (an absent J4 shows its cross) */
        g = hud_align(rdram, g, 0u);
        g = put(rdram, g, 0xED000000u, 0x005003C0u);
        g = one_cycle(rdram, g);
        for (top = 0; top < 2; top++) {
#if GGA_HD_HUD
            g = top_half(rdram, g, 2 * top, ox + top_x[top]);
            g = top_half(rdram, g, 2 * top + 1, ox + top_x[top] + 64.f);
#else /* J3 / J4 = the game's J1 / J2 block halves, at the top edge */
            g = load_bars_tex(rdram, g, bar_tex[2 * top]);
            g = rect(rdram, g, 0, ox + top_x[top], 0.f, 64.f, BLK_H, 0.f, 0.f, 1.f, 1.f);
            g = load_bars_tex(rdram, g, bar_tex[2 * top + 1]);
            g = rect(rdram, g, 0, ox + top_x[top] + 64.f, 0.f, 64.f, BLK_H, 0.f, 0.f, 1.f, 1.f);
#endif
        }
        ELEMENTS(2, 3)
        TEXTS(2, 3)
    }
#undef EACH
#undef ELEMENTS
#undef TEXTS

    ctx->r2 = (gpr)(int32_t)g;
    return 1;
}

/*
 * Item squares (measured, 2026-10-21): func_800DDF64 draws D_80108B78 (1-cycle setup) and D_80108BD8 (the two yellow frames,
 * texture 0x03004800 32 x 28, ALWAYS drawn: J1 at (20, 12), J2 at (264, 12)), then func_800DD9F0(gfx, j) for j = 0, 1: the
 * item icon (record + 8 = item 1..3, else nothing) 24 x 24 at (244 j + 24, 14), 4 / 2 inside the frame. Hook at its entry,
 * every mode (user's choice, 2026-10-23): one per HUD slot, under its weapon frame's side, 2 inside the bar's ends: J1 / J2
 * 3 above the bottom bar (y 208 once moved down), J3 / J4 3 under their top block.
 */
#define ITEM_DL_SETUP 0x80108B78u
#define ITEM_DL_FRAMES 0x80108BD8u /* commands 0..7: texture load; 8..13: the two frames */
void func_800DD9F0_598C10(uint8_t* rdram, recomp_context* ctx);
int ga_hud_items(uint8_t* rdram, recomp_context* ctx) {
    const int n = hud_players(rdram);
    uint32_t g = (uint32_t)ctx->r4;
    int p;
    wide_layout(); /* this runs before the HUD itself (func_800DE210, 0x800DE454) */
    g = hud_align(rdram, g, 0u);
    g = inline_dl(rdram, g, ITEM_DL_SETUP);
    g = put(rdram, g, 0xED000000u, 0x005003C0u); /* full screen scissor (the corners) */
    for (p = 0; p < n; p++) {
        const float fx = ox + ((p & 1) ? 304.f - 2.f - 32.f : 16.f + 2.f);
        const float fy = p < 2 ? 208.f - 3.f - 28.f : BLK_H + 3.f;
        uint32_t start;
        g = copy_cmds(rdram, g, ITEM_DL_FRAMES, 0, 7);
        g = rect(rdram, g, 0, fx, fy, 32.f, 28.f, 0.f, 0.f, 1.f, 1.f);
        start = g;
        g = call(rdram, ctx, func_800DD9F0_598C10, g, p, 0, 0, 0); /* reads player p's record (records are contiguous) */
        src_copy = 0;
        g = rewrite(rdram, start, g, fx + 4.f - (244.f * p + 24.f), fy + 2.f - 14.f, 0.f, 1.f, 0.f); /* the game's (244 p + 24, 14) */
    }
    g = put(rdram, g, 0xE0525464u, 0x10000064u); /* rect alignment back to NONE for what the game draws next */
    g = put(rdram, g, 0x64000006u, 0x00800800u);
    g = put(rdram, g, 0u, 0u);
    ctx->r2 = (gpr)(int32_t)g;
    return 1;
}

/*
 * Joining back after a game over, players 3 / 4, the way player 2 does (measured: the HUD update, message 0 of
 * func_800DE210, runs for each ABSENT player j < 2 the state function at state + 4*j: func_800DEA20 "waiting" then
 * func_800DEB28 "choosing"). C copies of those two functions for ports 2 / 3, because the game ones only know 2 players:
 * their per-player state has 2 entries and the life given to a player who has none comes from "the other one" (port ^ 1).
 * Here the life comes from the player who has the most. Added: no controller on the port -> blinking cross.
 * Removed on 2026-10-25 with the 3P / 4P "absolute" mode, put back the same day (user's choice) for dead players.
 */
#define JOIN_BLOCKED 0x80088349u /* set by the game when a player could not be created */
#define CHAR_COUNT   0x80088211u /* number of playable characters */
#define ST_FRAME     0x28u       /* HUD state: frame counter (blink: (frame & 0x1F) >= 21) */

static int st_choosing[4];
static int8_t st_char[4];

static int32_t callf(uint8_t* rdram, recomp_context* ctx, void (*f)(uint8_t*, recomp_context*), int32_t a0, int32_t a1) {
    recomp_context c = ctx_copy(ctx);
    c.r29 = ctx->r29 - 0x40;
    c.r4 = (gpr)a0;
    c.r5 = (gpr)a1;
    f(rdram, &c);
    return (int32_t)c.r2;
}

static int lives(uint8_t* rdram, int p) { return (int8_t)rb(rdram, REC(p) + 1); }

/* Present player with the most lives (gives one to a player who joins without any), -1 if none. */
static int donor(uint8_t* rdram, int self) {
    int p, best = -1;
    for (p = 0; p < 4; p++) {
        if (p != self && rec_valid(rdram, p) && lives(rdram, p) > 0 && (best < 0 || lives(rdram, p) > lives(rdram, best))) {
            best = p;
        }
    }
    return best;
}

/* func_800DE99C loops until it finds a character no living player uses: with every unlocked character taken (fewer than 4
 * unlocked, 4 players) it never returns. Same test as its loop (count clamped to 2 like it). */
static int free_char(uint8_t* rdram) {
    int c, p, n = rb(rdram, CHAR_COUNT) < 2 ? 2 : rb(rdram, CHAR_COUNT);
    for (c = 0; c < n; c++) {
        for (p = 0; p < 4 && !((int8_t)rb(rdram, REC(p)) == c && lives(rdram, p) > 0); p++) {
        }
        if (p == 4) {
            return 1;
        }
    }
    return 0;
}

static void join_port(uint8_t* rdram, recomp_context* ctx, uint32_t state, int p) {
    const int blink = (MEM_HU(0, (int32_t)(state + ST_FRAME)) & 0x1F) >= 21;
    const int d = donor(rdram, p);
    uint8_t b;
    if (!gga_port_has_controller(p)) {
        st_choosing[p] = 0;
        st_weapon[p] = blink ? 0 : 2;
        return;
    }
    if (!st_choosing[p]) { /* func_800DEA20 */
        if (rb(rdram, JOIN_BLOCKED) != 0 || d < 0 || lives(rdram, d) + lives(rdram, p) < 2 || !free_char(rdram)) {
            b = 2;
        } else if (callf(rdram, ctx, func_800D8CF8_593F18, p, 0x1000) == 1) { /* START */
            callf(rdram, ctx, func_800D8CA0_593EC0, p, 1);
            st_char[p] = (int8_t)callf(rdram, ctx, func_800DE99C_599BBC, (int32_t)rb(rdram, CHAR_COUNT) - 1, 1);
            st_choosing[p] = 1;
            return;
        } else {
            b = 1;
        }
        st_weapon[p] = blink ? 0 : b;
        return;
    }
    /* func_800DEB28 */
    if (rb(rdram, JOIN_BLOCKED) != 0 || !free_char(rdram)) {
        callf(rdram, ctx, func_800D8CA0_593EC0, p, 0);
        st_choosing[p] = 0;
        return;
    }
    if (callf(rdram, ctx, func_800D8CF8_593F18, p, 0x200) == 1) { /* left */
        st_char[p] = (int8_t)callf(rdram, ctx, func_800DE99C_599BBC, st_char[p], -1);
    }
    if (callf(rdram, ctx, func_800D8CF8_593F18, p, 0x100) == 1) { /* right */
        st_char[p] = (int8_t)callf(rdram, ctx, func_800DE99C_599BBC, st_char[p], 1);
    }
    st_weapon[p] = (uint8_t)(st_char[p] + 3) | (blink ? 0x80 : 0);
    if (callf(rdram, ctx, func_800D8CF8_593F18, p, 0x9000) == 1) { /* A or START */
        callf(rdram, ctx, func_800D8CA0_593EC0, p, 0);
        st_choosing[p] = 0;
        callf(rdram, ctx, func_800DE7FC_599A1C, p, st_char[p]); /* takes the life from donor() (ga_join_donor) */
    }
}

/* Hook in func_800DE210, start of the HUD update (message 0, $a0 = HUD object, state = object + 0x20). */
void ga_hud_join(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t state = (uint32_t)ctx->r4 + 0x20u;
    int p;
    if (hud_players(rdram) == 2) {
        return;
    }
    hud_owner(rdram, state);
    for (p = 2; p < 4; p++) {
        if (!rec_valid(rdram, p) || lives(rdram, p) <= 0) {
            join_port(rdram, ctx, state, p);
        }
    }
}

/* J1 / J2 (the game's func_800DEA20, before 0x800DEA78): "OK" when self + donor() lives >= 2 instead of self + port ^ 1;
 * cross when no character is free (func_800DE99C would never return). $s0 = port, $t4 = the sum tested (< 2: cross). */
void ga_join_lives(uint8_t* rdram, recomp_context* ctx) {
    const int p = (int)ctx->r16;
    const int d = donor(rdram, p);
    ctx->r12 = (gpr)(d < 0 || !free_char(rdram) ? 0 : lives(rdram, p) + lives(rdram, d));
}

/* func_800DE7FC (before 0x800DE930): the player who gives a life to a joining one without any is donor() instead of
 * port ^ 1. $v0 = its record - 0x7A (D_80088208 + 10 * port), [sp + 0x78] = joining port. */
void ga_join_donor(uint8_t* rdram, recomp_context* ctx) {
    const int d = donor(rdram, (int)MEM_W(0x78, ctx->r29));
    if (d >= 0) {
        ctx->r2 = (gpr)(int32_t)(REC(d) - 0x7Au);
    }
}

/*
 * 3P / 4P "absolute" mode (2026-10-25): the character select screen (func_080080E8, module of file 18) creates one cursor per
 * player, port 0 .. *(u32 *)(screen + 0) - 1, and the game already has the 3P / 4P cursors (green / magenta): only the number
 * of players was missing. It is now chosen in the title menu [1P][2P] / [3P][4P] below (the 2P -> 3P / 4P raise by
 * controller count, ga_select_players, was removed).
 *
 * Title menu, players screen (module of file 18, measured 2026-10-25): func_0800579C creates the buttons, type 0xA "1P" and
 * 0xB "2P" (x 112 / 208, y 184), into screen + 0x20 + 4 k (4 slots, all freed by every screen); func_08005550 updates it:
 * cursor screen + 8 (left / right of any controller), A / START -> screen + 0xA = cursor + 1 (the number of players),
 * screen + 0xC = 0, next function func_08005878; B -> back to the title (func_080049FC). Buttons (handler func_08007560,
 * object + 0x30 type, + 0x34 / + 0x38 x / y, + 0x40..0x48 label color): types >= 0xA draw a 22 x 12 label of the I4 image
 * of resource 0x10B (256 wide) at x 24 * (type - 10), y 111, where "3P" and "4P" already are (types 0xC / 0xD).
 * Grid: 1P 2P on the first row, 3P 4P under them; an option needs a controller on each of its ports (1P always).
 */
void func_08007C4C_666AEC(uint8_t* rdram, recomp_context* ctx); /* (screen, type, x, y) -> button */
void func_08007C98_666B38(uint8_t* rdram, recomp_context* ctx); /* (button, fade speed) */
void func_08007D14_666BB4(uint8_t* rdram, recomp_context* ctx); /* (button, selected) */
void func_080049FC_66389C(uint8_t* rdram, recomp_context* ctx); /* (a0, screen): back to the title screen */
#define MENU_NEXT 0x08005878u /* the game's next step after A (func_08005878) */
static const float menu_x[4] = { 112.f, 208.f, 112.f, 208.f };
static const float menu_y[4] = { 164.f, 164.f, 186.f, 186.f }; /* the game's single row: y 184 */

static int32_t fbits(float f) { union { float f; int32_t i; } u; u.f = f; return u.i; }

/* Option k (0 = 1P .. 3 = 4P): needs a controller on ports 1 .. k. */
static int menu_enabled(int k) {
    int p;
    for (p = 1; p <= k; p++) {
        if (!gga_port_has_controller(p)) {
            return 0;
        }
    }
    return 1;
}

/* Hook at the end of func_0800579C (before 0x08005854): $s4 = screen, [sp + 0x30] = a0. 3P / 4P added (1P / 2P moved up
 * by ga.toml: their y argument 184 -> 164). */
void ga_menu_buttons(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t screen = (uint32_t)ctx->r20;
    const int32_t a0 = MEM_W(0x30, ctx->r29);
    uint32_t btn;
    int k;
    for (k = 2; k < 4; k++) {
        btn = call(rdram, ctx, func_08007C4C_666AEC, (uint32_t)a0, 0xA + k, fbits(menu_x[k]), fbits(menu_y[k]), 0);
        ww(rdram, screen + 0x20u + 4u * k, btn);
        call(rdram, ctx, func_08007C98_666B38, btn, 8, 0, 0, 0);
    }
}

/* Pressed this frame, or repeated while held (the game's test: hold 1, or >= 21 and every 5 frames), on any controller. */
static uint32_t menu_pressed(uint8_t* rdram, recomp_context* ctx, uint32_t mask, int repeat) {
    int p;
    for (p = 0; p < 4; p++) {
        const int32_t v = (int32_t)call(rdram, ctx, func_800D8CF8_593F18, (uint32_t)p, (int32_t)mask, 0, 0, 0);
        if (v == 1 || (repeat && v >= 21 && v % 5 == 1)) {
            return mask;
        }
    }
    return 0;
}

/* Hook at the entry of func_08005550 (returns 1, $v0 = 0): C version on a 2 x 2 grid. $a0 = a0, $a1 = screen. */
int ga_menu_players(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t a0 = (uint32_t)ctx->r4, screen = (uint32_t)ctx->r5;
    const int old = (int8_t)rb(rdram, screen + 8);
    const uint32_t keys = menu_pressed(rdram, ctx, 0x200, 1) | menu_pressed(rdram, ctx, 0x100, 1) |
                          menu_pressed(rdram, ctx, 0x800, 1) | menu_pressed(rdram, ctx, 0x400, 1) |
                          menu_pressed(rdram, ctx, 0x9000, 0) | menu_pressed(rdram, ctx, 0x4000, 0);
    int cur = old >= 0 && old < 4 && menu_enabled(old) ? old : 0; /* a controller unplugged: back to 1P */
    int col = cur & 1, row = cur >> 1, k;
    if (keys & 0x200) col = 0; /* left */
    if (keys & 0x100) col = 1; /* right */
    if (keys & 0x800) row = 0; /* up */
    if (keys & 0x400) row = 1; /* down */
    if (menu_enabled(row * 2 + col)) {
        cur = row * 2 + col;
    }
    wb(rdram, screen + 8, (uint8_t)cur);
    if (cur != old) {
        call(rdram, ctx, func_80008B6C_976C, 0x119, 0, 0, 0, 0);
    }
    if (keys & 0x9000) { /* A / START */
        call(rdram, ctx, func_80008B6C_976C, 0x151, 0, 0, 0, 0);
        wb(rdram, screen + 0xA, (uint8_t)(cur + 1));
        MEM_H(0, (int32_t)(screen + 0xC)) = 0;
        ww(rdram, screen + 4, MENU_NEXT);
    }
    if (keys & 0x4000) { /* B */
        call(rdram, ctx, func_080049FC_66389C, a0, (int32_t)screen, 0, 0, 0);
        ctx->r2 = 0;
        return 1;
    }
    for (k = 0; k < 4; k++) {
        call(rdram, ctx, func_08007D14_666BB4, rw(rdram, screen + 0x20u + 4u * k), k == cur, 0, 0, 0);
    }
    ctx->r2 = 0;
    return 1;
}

/*
 * Character select (func_080080E8, every message, hook at its entry): each player needs a different character and a new game
 * has only Goemon / Ebisumaru (unlocked count D_80088211, part of the save). "Unlock Sasuke and Yae" option (General menu):
 * count set to 4. Then the number of players, one cursor each, is cut to the unlocked count: the players left out join later
 * with START (ga_hud_join) once a character is free. Measured (trace, 2026-10-25): $a1 = screen state, + 0x40 = frame counter
 * (1, 2, ...), cursors created at frame 0x24 for port 0 .. *(u32 *)(screen + 0) - 1 (NOT 0x80088208, which stays 0 here).
 */
int ga_unlock_characters(void); /* src/game/ga_widescreen.cpp */
#define SELECT_FRAME 0x40u
void ga_select_chars(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t screen = (uint32_t)ctx->r5;
    uint32_t chars;
    if (ga_unlock_characters() && rb(rdram, CHAR_COUNT) < 4) {
        wb(rdram, CHAR_COUNT, 4);
    }
    chars = rb(rdram, CHAR_COUNT) < 2 ? 2 : rb(rdram, CHAR_COUNT); /* func_800DE99C clamps it to 2 too */
    if ((uint16_t)MEM_H(0, (int32_t)(screen + SELECT_FRAME)) == 0x24 && rw(rdram, screen) > chars) {
        ww(rdram, screen, chars);
    }
}

/*
 * World map (module of file 16, measured 2026-10-25): func_08003DC0 starts a move: the player who pressed A ($s1) goes with
 * one partner only, (s1 + 1) & 1 at [sp + 0x80], each sent along the path by func_8018B570(object, path [node + 6],
 * &vector sp + 0x6C, [sp + 0x74] = side offset 5 - 10 p) and given its speed by func_8018B514(object, [sp + 0x68], 2.5 leader
 * / 1.8 partner); arrival code already loops on 4 players. Hook before 0x08004224 (after the partner's path, $s0 = node):
 * the other present players get the partner's orders.
 */
void func_8018B570_5EDFD0(uint8_t* rdram, recomp_context* ctx);
void func_8018B514_5EDF74(uint8_t* rdram, recomp_context* ctx);
#define PLAYER_OBJ(p) (0x800883A0u + 4u * (uint32_t)(p))
/* Same function, before 0x080041D8 (partner just stored at [sp + 0x80]): with J1 / J2 dead the partner (s1 + 1) & 1 has no
 * object and func_8018B570 crashed on it (2026-10-25, gga_crash.txt): take the first other present player instead. */
void ga_map_partner(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t sp = (uint32_t)ctx->r29;
    const int lead = (int)ctx->r17;
    int p;
    if (rw(rdram, PLAYER_OBJ(rw(rdram, sp + 0x80))) != 0) {
        return;
    }
    for (p = 0; p < 4; p++) {
        if (p != lead && rw(rdram, PLAYER_OBJ(p)) != 0) {
            ww(rdram, sp + 0x80, (uint32_t)p);
            return;
        }
    }
}

void ga_map_move(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t sp = (uint32_t)ctx->r29;
    const int lead = (int)ctx->r17, partner = (int)rw(rdram, sp + 0x80);
    const int32_t path = MEM_H(6, ctx->r16);
    int p;
    for (p = 0; p < 4; p++) {
        const uint32_t obj = rw(rdram, PLAYER_OBJ(p));
        if (p == lead || p == partner || obj == 0) {
            continue;
        }
        ww(rdram, sp + 0x74, (uint32_t)fbits(5.f - 10.f * p));
        call(rdram, ctx, func_8018B570_5EDFD0, obj, path, (int32_t)(sp + 0x6C), 0, 0);
        call(rdram, ctx, func_8018B514_5EDF74, obj, (int32_t)rw(rdram, sp + 0x68), 0x3FE66666, 0, 0);
    }
}

/* World map opening (func_080030BC message 2, hook before 0x08003128): the game calls func_800DB8C4 (game over) when the
 * records of J1 AND J2 are empty or without lives; with J3 or J4 alive, jump to the normal path (L_08003170) instead. */
int ga_map_alive(uint8_t* rdram) {
    int p;
    for (p = 2; p < 4; p++) {
        if (rec_valid(rdram, p) && lives(rdram, p) > 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * World map, entering a stage (func_08004418, measured 2026-10-25 with a call stack trace of the sounds): state $a1,
 * + 0x24 = step (1 = entering), + 0x26 = frame, + 0x20 != 0 = no voices, + 0x21 / + 0x22 = characters of the leader and of
 * its partner (0xFF = none, set by func_08003DC0). Jingle 0x162 at frame 1, leader's voice at 0xF, partner's at 0x15, next
 * step at 0x2D. Hook at its entry: the other present players' voices at 0x1B and 0x21 (same 6-frame spacing).
 */
static const uint16_t map_voice_id[4] = { 0x116, 0x115, 0x117, 0x118 }; /* character 0..3, as the game */
void ga_map_voice(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t st = (uint32_t)ctx->r5;
    const int frame = MEM_H(0x26, ctx->r5);
    int p, k = 0, slot;
    if (rb(rdram, st + 0x24) != 1 || rb(rdram, st + 0x20) != 0 || (frame != 0x1B && frame != 0x21)) {
        return;
    }
    slot = (frame - 0x1B) / 6;
    for (p = 0; p < 4; p++) {
        const int8_t c = (int8_t)rb(rdram, REC(p));
        if (rw(rdram, PLAYER_OBJ(p)) == 0 || c < 0 || c > 3 || c == (int8_t)rb(rdram, st + 0x21) ||
            c == (int8_t)rb(rdram, st + 0x22)) {
            continue;
        }
        if (k++ == slot) {
            call(rdram, ctx, func_80008B6C_976C, map_voice_id[c], 0, 0, 0, 0);
            return;
        }
    }
}

/*
 * World map, save anywhere (user's idea, 2026-11-11): START (does nothing on the map; pauses in stages, and this hook only
 * runs on the map) saves like the game's own save after the castle robot. func_08003DC0 = idle update of the map (A / B per
 * present player, read with func_800D8CF8(port, mask) == 1 = pressed now). func_800CFC64(0x80088208, slot) (file 6) = the
 * game's save of a slot (copies 0x130 bytes, tops up lives / coins in the copy, writes the Controller Pak), called with the
 * slot of the loaded game D_80088348 by the script command func_800EDFFC. That command is in script 0x51 (game file 333,
 * "Do you wish to record your journal thus far?", yes / no, then the save and its messages, Controller Pak errors included).
 * The map itself starts it on arrival after a world change (func_08003C40, frame 8: slot D_80088348 in 0..2 ->
 * func_800E78E8(0x51, 0, 0), then state + 0x18 = func_08003D78, which waits for the script (func_800E7A0C) and goes back
 * to func_08003DC0). START does the same (hook at the entry of func_08003DC0, $a1 = map state; returns 1 = skip the rest
 * of this frame). 0x80088210 = D_80088346 + 7 as the game (resume place, supposed: world map of the current world).
 */
void func_800E78E8_5A2B08(uint8_t* rdram, recomp_context* ctx); /* (script id, 0, 0) */

/*
 * Resume code 0x80088210 (in the save): loading a game (func_08008470, title module) reads it (lbu at 0x08008518, 0..0x10,
 * jump table) and sets fixed (world D_80088346, place D_80088347): villages / inns, or 7 + world = world map at place 0;
 * the map then opens at place D_80088347. Our map save writes RESUME_MAP + world * 16 + place (0x20..0x6F, unused by the
 * game); ga_resume_read (before 0x08008518) puts 7 + world back for the game's table and keeps the place, ga_resume_place
 * (before 0x080086F8, after the table) sets D_80088347 to it.
 */
#define RESUME_MAP 0x20
static int resume_place = -1;
void ga_resume_read(uint8_t* rdram) {
    const uint8_t b = rb(rdram, 0x80088210u);
    resume_place = -1;
    if (b >= RESUME_MAP && b < RESUME_MAP + 5 * 16) {
        resume_place = (b - RESUME_MAP) & 15;
        wb(rdram, 0x80088210u, (uint8_t)(7 + (b - RESUME_MAP) / 16));
    }
}
void ga_resume_place(uint8_t* rdram) {
    if (resume_place >= 0) {
        wb(rdram, 0x80088347u, (uint8_t)resume_place);
        resume_place = -1;
    }
}

/*
 * Journal screen: func_08001794(obj, x bits, y bits, code) (title module) stores x / y floats at obj + 0x14 / + 0x18 and the
 * name of resume code 1..11 at obj + 0x28 ("Lost'n Town:INN" .. "Home of Wiseman:Front", "Edo:Complete" ..), else none.
 * Hook at its entry: our code -> the map place's name (strings of the map, game file 333 + 0x10, in world order; the
 * place index matches for the inns (0,2) (1,2) (2,3) (3,3); world 4 order supposed), written at MAP_SAVE_NAMES.
 */
#define MAP_SAVE_NAMES 0x80D10200u /* 80 x 48 bytes, after the header's strings / dot (MAP_SAVE_RAM 0x80D10000) */
/* "World:Place" as the game's "Edo:Complete"; no world name in the game for world 4 (user's choice: Floating World) */
static const char* const world_name[5] = { "Edo", "Ryugu", "Mafu Island", "Underworld", "Floating World" };
static const char* const place_name[5][8] = {
    { "Home of the Wiseman", "Lost'n Road", "Lost'n Town", "Digadig Gold Mine", "Ringbell Pass", "Edo Checkpoint",
      "Edo Castle", 0 },
    { "Kappa Road", "Frog Mountain", "Otohime Town", "Naruto Road", "Ryugu Checkpoint", "Ryugu Castle", 0, 0 },
    { "Kokeshi Road", "Maneater Pass", "Tomb Pass", "Spook Village", "Pochan Lake", "Mafu Checkpoint", "Majin Castle", 0 },
    { "Kaiware Road", "Mokeke Forest", "Wonder Valley", "Tree Stump Village", "Underworld Checkpoint", "Underworld Castle",
      0, 0 },
    { "Creep Village", "Cloud Fortress", "Sky Garden", "Wind Castle Wall", "Floating World Town", "Floating Checkpoint",
      "Dream Castle", 0 },
};
int ga_journal_name(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t obj = (uint32_t)ctx->r4, code = (uint32_t)ctx->r7;
    const char* n;
    char buf[48];
    uint32_t s, i = 0;
    if (code < RESUME_MAP || code >= RESUME_MAP + 5 * 16 || (code & 15) >= 8 ||
        (n = place_name[(code - RESUME_MAP) / 16][code & 15]) == 0) {
        return 0;
    }
    snprintf(buf, sizeof buf, "%s:%s", world_name[(code - RESUME_MAP) / 16], n);
    s = MAP_SAVE_NAMES + 48u * (code - RESUME_MAP);
    do {
        wb(rdram, s + i, (uint8_t)buf[i]);
    } while (buf[i++] != '\0');
    ww(rdram, obj + 0x14, (uint32_t)ctx->r5);
    ww(rdram, obj + 0x18, (uint32_t)ctx->r6);
    ww(rdram, obj + 0x28, s);
    return 1;
}

int ga_map_save(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t st = (uint32_t)ctx->r5;
    const int slot = (int8_t)rb(rdram, 0x80088348u);
    int p;
    int ga_save_anywhere(void); /* option "Save Anywhere" (Enhanced menu), src/game/ga_widescreen.cpp */
    if (!ga_save_anywhere() || slot < 0 || slot > 2) { /* no slot (as the game: no save prompt) */
        return 0;
    }
    for (p = 0; p < 4; p++) {
        if (rw(rdram, PLAYER_OBJ(p)) != 0 && callf(rdram, ctx, func_800D8CF8_593F18, p, 0x1000) == 1) {
            fprintf(stderr, "[GGA SAVE] map save, slot %d, 0x80088210 = %d, D_80088346 = %d, D_80088347 = %d\n", slot,
                    (int8_t)rb(rdram, 0x80088210u), (int8_t)rb(rdram, 0x80088346u), (int8_t)rb(rdram, 0x80088347u));
            /* resume code (as func_08003C40 before the script: 7 + world = world map, at place 0; without it the save kept
               the previous one, 3 = Spook village, user's test 2026-11-11), here with the place: see ga_resume_* */
            const int world = (int8_t)rb(rdram, 0x80088346u), place = (int32_t)rw(rdram, 0x08007E50u); /* D_08007E50 */
            wb(rdram, 0x80088210u, (uint8_t)(world >= 0 && world <= 4 && place >= 0 && place <= 15
                                                 ? RESUME_MAP + world * 16 + place : world + 7));
            call(rdram, ctx, func_800E78E8_5A2B08, 0x51, 0, 0, 0, 0);
            MEM_H(0x26, (int32_t)st) = 0;
            ww(rdram, st + 0x18, 0x08003D78u); /* func_08003D78: wait for the script */
            return 1;
        }
    }
    return 0;
}

/*
 * World map header (top right, "Move / Enter stage", func_0800159C, read 2026-11-11): state $s2 = object + 0x20: +0 node,
 * +4 / +8 texts of lines 1 / 2, +0xC x, +0x10 y, +0x14 width, +0x18 height 0x19. Lines 11 px apart (text y + 2 / + 13),
 * dots = 10 x 10 pieces of a 20 x 10 RGBA16 texture (0x03003F80, resource 0x113: green s 0, blue s 10), copy-mode rects.
 * Third line "Save" with a red dot (user's choice: START is red on the N64 pad): its text created with the other two
 * (hook before 0x080016CC), placed and the frame made 11 px taller (before 0x08001778), freed (message 1, before
 * 0x08001780), its dot drawn before the end of the header's display list (before 0x08001BCC, $s1 = &gfx). The red dot =
 * the game's green dot with red and green swapped, written in free RDRAM (MAP_SAVE_RAM, after the object pool 0x80D00000).
 * shortcut: the frame's width stays the one of line 2 (longer than "Save" in EN / FR / DE), recompute if a label grows.
 */
void func_800EB828_5A6A48(uint8_t* rdram, recomp_context* ctx); /* (node, x, y, string) -> text object */
void func_800EAF20_5A6140(uint8_t* rdram, recomp_context* ctx); /* (text, 8, 10, 1, [sp + 0x10] 0) */
void func_800EB8E8_5A6B08(uint8_t* rdram, recomp_context* ctx); /* (text): frees it */
void func_800CDF44_589164(uint8_t* rdram, recomp_context* ctx); /* (segmented address, resource, 0) -> address */
#define MAP_SAVE_RAM  0x80D10000u            /* strings (3 x 16 bytes) */
#define MAP_SAVE_TEX  (MAP_SAVE_RAM + 0x40u) /* 20 x 10 RGBA16 = 400 bytes */
#define MAP_LINE      11
static const char* const map_save_label[3] = { "Save", "Sauvegarder", "Speichern" }; /* language byte 0x80088200 */
static uint32_t map_hdr_state, map_hdr_text;

void ga_map_hdr_create(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t st = (uint32_t)ctx->r18;
    uint32_t i, lang = rb(rdram, 0x80088200u), s;
    const char* l;
    int ga_save_anywhere(void);
    if (ctx->r2 == 0 || !ga_save_anywhere()) { /* line 2 failed: the game gives up; option Off: header as the game's */
        return;
    }
    if (lang > 2) {
        lang = 0;
    }
    s = MAP_SAVE_RAM + 16u * lang;
    l = map_save_label[lang];
    i = 0;
    do {
        wb(rdram, s + i, (uint8_t)l[i]);
    } while (l[i++] != '\0');
    map_hdr_state = st;
    map_hdr_text = call(rdram, ctx, func_800EB828_5A6A48, rw(rdram, st), (int32_t)rw(rdram, st + 0xC) + 15,
                        (int32_t)rw(rdram, st + 0x10) + 2 + 2 * MAP_LINE, (int32_t)s, 0);
    if (map_hdr_text != 0) {
        call(rdram, ctx, func_800EAF20_5A6140, map_hdr_text, 8, 10, 1, 0);
    }
}

void ga_map_hdr_layout(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t st = (uint32_t)ctx->r18;
    if (st != map_hdr_state || map_hdr_text == 0) {
        return;
    }
    ww(rdram, map_hdr_text + 0x14, (uint32_t)fbits((float)((int32_t)rw(rdram, st + 0xC) + 16))); /* as lines 1 / 2 */
    ww(rdram, st + 0x18, rw(rdram, st + 0x18) + MAP_LINE);
}

void ga_map_hdr_free(uint8_t* rdram, recomp_context* ctx) {
    if ((uint32_t)ctx->r18 != map_hdr_state) {
        return;
    }
    if (map_hdr_text != 0) {
        callf(rdram, ctx, func_800EB8E8_5A6B08, (int32_t)map_hdr_text, 0);
    }
    map_hdr_state = map_hdr_text = 0;
}

void ga_map_hdr_draw(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t st = (uint32_t)ctx->r18, gp = (uint32_t)ctx->r17;
    const uint32_t x = rw(rdram, st + 0xC), y = rw(rdram, st + 0x10) + 2 + 2 * MAP_LINE;
    uint32_t g = rw(rdram, gp), src, i;
    if (st != map_hdr_state || map_hdr_text == 0) {
        return;
    }
    src = call(rdram, ctx, func_800CDF44_589164, 0x03003F80u, 0x113, 0, 0, 0);
    for (i = 0; i < 20 * 10; i++) { /* green dot (s 0..9) -> red, both halves */
        const uint16_t c = (uint16_t)MEM_HU(0, (int32_t)(src + 2u * (i / 20u * 20u + i % 10u)));
        const uint16_t r = (uint16_t)((c & 0x003F) | ((c >> 6) & 0x1F) << 11 | ((c >> 11) & 0x1F) << 6); /* RGBA5551 */
        MEM_H(0, (int32_t)(MAP_SAVE_TEX + 2u * i)) = (int16_t)r;
    }
    /* same texture load as the game's (64C7F0.s), our address */
    g = put(rdram, g, 0xE7000000, 0);
    g = put(rdram, g, 0xFD100000, MAP_SAVE_TEX);
    g = put(rdram, g, 0xF5100000, 0x07080200);
    g = put(rdram, g, 0xE6000000, 0);
    g = put(rdram, g, 0xF3000000, 0x070C719A);
    g = put(rdram, g, 0xE7000000, 0);
    g = put(rdram, g, 0xF5100A00, 0x00080200);
    g = put(rdram, g, 0xF2000000, 0x0004C024);
    g = put(rdram, g, 0xE4000000 | ((x + 11) * 4 & 0xFFF) << 12 | ((y + 9) * 4 & 0xFFF),
            ((x + 2) * 4 & 0xFFF) << 12 | (y * 4 & 0xFFF));
    g = put(rdram, g, 0xE1000000, 0);
    g = put(rdram, g, 0xF1000000, 0x10000400);
    ww(rdram, gp, g);
}

/* Button handler func_08007560 (before 0x08007C20, after its update, [sp + 0x68] = object, [sp + 0x6E] = message): an
 * option without enough controllers gets a grey label. */
void ga_menu_gray(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t obj = (uint32_t)MEM_W(0x68, ctx->r29);
    const uint32_t type = rw(rdram, obj + 0x30);
    int i;
    if (MEM_H(0x6E, ctx->r29) != 0 || type < 0xB || type > 0xD || menu_enabled((int)type - 0xA)) {
        return;
    }
    for (i = 0; i < 3; i++) {
        ww(rdram, obj + 0x40u + 4u * i, 0x50);
    }
}

/*
 * Underwater zones in 3P / 4P (measured 2026-10-25, world 2 stage 1, zone 0x2A): func_800DBCAC (players of a zone) ORs its
 * zone flags $a0 ([sp + 0xC0]) into every player's flags (object + 0x150); 0x10000 = underwater. The player init then calls
 * func_8017C318: Sasuke (2) / Yae (3) get their swimming form, any other character loses 0x10000 and floats back up (state
 * 0xE6), which takes everybody out of the zone. Hook before 0x800DBD8C ($a0 = character of the record): such a player is
 * not created (goto L_800DBE1C = s1++ then the loop test, record untouched), so he comes back in the next zone.
 */
static int swim_skipped_pass; /* ports skipped by the running func_800DBCAC loop */
static int swim_skipped;      /* ports skipped in the last zone created */
static int swim_rescue;       /* next zone creation skips nobody */

/* Reset button (ga_reset.cpp): our per-game state back to its launch values. */
void ga_hud_reset_state(void) {
    int p;
    for (p = 0; p < 4; p++) {
        st_choosing[p] = 0;
        st_char[p] = 0;
    }
    st_owner = 0;
    resume_place = -1;
    map_hdr_state = map_hdr_text = 0;
    swim_skipped_pass = swim_skipped = swim_rescue = 0;
}
int ga_swim_skip(uint8_t* rdram, recomp_context* ctx) {
    const int8_t c = (int8_t)ctx->r4;
    if (swim_rescue || (MEM_W(0xC0, ctx->r29) & 0x10000) == 0 || c == 2 || c == 3) {
        return 0;
    }
    swim_skipped_pass |= 1 << (ctx->r17 & 3);
    return 1;
}

/* End of the func_800DBCAC loop (hook before 0x800DBE2C). */
void ga_swim_pass_end(void) {
    swim_skipped = swim_skipped_pass;
    swim_skipped_pass = 0;
    swim_rescue = 0;
}

/*
 * Death outcome func_8018B9D0 (measured 2026-10-25): func_8018C128 counts the players present; when the last present one
 * loses his last life (record + 1 < 2), it returns 2 = game over (func_800DB8C4). Underwater, the players skipped above are
 * not present, so the swimmers' death was a game over (tested). Hook before 0x8018BB00 (that case only): if a skipped player
 * still has lives, "replay the zone" (1, func_8018C030) instead, and the zone is created again with nobody skipped: the
 * non-swimmers float up like in the original game and take everybody back to the surface.
 */
int ga_swim_rescue(uint8_t* rdram) {
    int p;
    for (p = 0; p < 4; p++) {
        if ((swim_skipped >> p & 1) && rec_valid(rdram, p) && lives(rdram, p) > 0) {
            swim_rescue = 1;
            return 1;
        }
    }
    return 0;
}

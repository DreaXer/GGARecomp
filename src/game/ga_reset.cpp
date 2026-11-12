// GGA: Reset button of the in-game menu (2026-11-12): back to the first scene (boot logos), like the N64 reset button, without
// quitting. Fade to black + sound fade (black veil of the UI, src/ui/ui_state.cpp; volume factor in src/main/main.cpp), then the
// game's own scene switch, then fade back in on the logos.
//
// LU (582D30.s, 1000.s): the scene manager is the object func_800C7B10 (created at boot by func_80000F10, state byte obj + 0x20).
// States 5 / 6 / 7 = boot logos (object func_801738A0 of overlay 8), then title; state 8 = waits for a scene request in
// D_80088338 + 0x8C (entry), + 0x94 (module), + 0x82 (overlay), + 0x88 (old root), filled by the func_800DAxxx "go to" functions.
// func_800DB140 (no caller in the game) asks for the manager itself: state 8 stops the sounds and destroys the whole scene, then
// goes back to state 5 (logos). Two things it does not do, done here: reload overlay 8 (the logos are its code, overlay 9 / 10
// sits at 0x801738A0 in game), and the boot's game-data init (func_80005190, minus its last call func_800037A0, which resets the
// heap pointer and the file table and must not run again).
#include <atomic>
#include <chrono>
#include <cstdint>
#include "recomp.h"

extern "C" {
void func_800DB140_596360(uint8_t* rdram, recomp_context* ctx); // scene request: the manager itself (= logos)
void func_800047EC_53EC(uint8_t* rdram, recomp_context* ctx);   // overlay loader
void func_80002E04_3A04(uint8_t* rdram, recomp_context* ctx);   // end of a scene switch (flags, pak states)
void ga_hud_reset_state(void);                                  // ga_hud.c
}

namespace {
using clock = std::chrono::steady_clock;
constexpr int64_t FADE_MS = 700;
constexpr int64_t PAK_QUIET_MS = 1500; // Reset greyed out while the Controller Pak was written less than this ago

enum Phase { IDLE, FADE_OUT, SWITCH, FADE_IN };
std::atomic<int> phase{ IDLE };
std::atomic<int64_t> phase_start{ 0 };
std::atomic<int64_t> last_pak_write{ -PAK_QUIET_MS };

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count();
}

void set_phase(Phase p) {
    phase_start = now_ms();
    phase = p;
}

float progress() {
    const float t = float(now_ms() - phase_start) / float(FADE_MS);
    return t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
}

void call(uint8_t* rdram, recomp_context* ctx, void (*f)(uint8_t*, recomp_context*), int32_t a0) {
    recomp_context c = *ctx; // f_odd points into the context: rebase it (see ga_hud.c ctx_copy)
    c.f_odd = (uint32_t*)((uint8_t*)&c + ((uint8_t*)ctx->f_odd - (uint8_t*)ctx));
    c.r29 = ctx->r29 - 0x40;
    c.r4 = (gpr)a0;
    f(rdram, &c);
}

// func_80005190 without func_800037A0: game globals as at boot (language 0x80088200 is outside these blocks, kept).
void boot_data_init(uint8_t* rdram) {
    auto zero = [&](int32_t base, int32_t size) {
        for (int32_t i = 0; i < size; i++) {
            MEM_B(0, base + i) = 0;
        }
    };
    zero((int32_t)0x800881C0, 0x2C);
    zero((int32_t)0x80088338, 0xE4);
    zero((int32_t)0x80088208, 0x130);
    for (int32_t s = 0; s < 4; s++) { // the 4 player records' defaults, 10 bytes apart (0x80088282..)
        const int32_t r = (int32_t)(0x80088282 + s * 10);
        MEM_B(0, r) = (int8_t)-1;
        MEM_B(1, r) = 0x10;
        MEM_B(2, r) = 3;
        MEM_B(3, r) = 3;
        MEM_H(4, r) = 0x1F4;
    }
    MEM_B(0, (int32_t)0x80088211) = 4;
    MEM_H(0x06, (int32_t)0x800881C0) = 1;
    MEM_H(0x1C, (int32_t)0x800881C0) = 2;
    MEM_B(0x24, (int32_t)0x800881C0) = 1;
}
} // namespace

// --- UI thread (menu, veil, audio) ---

// ga_pak.cpp: a Controller Pak block was written.
extern "C" void gga_pak_written(void) {
    last_pak_write = now_ms();
}

bool gga_reset_available() {
    return phase == IDLE && now_ms() - last_pak_write >= PAK_QUIET_MS;
}

void gga_reset_request() {
    if (gga_reset_available()) {
        set_phase(FADE_OUT);
    }
}

// Opacity of the black veil, 0..1 (also ends the fade-in).
float gga_reset_black() {
    switch (phase.load()) {
    case FADE_OUT:
        return progress();
    case SWITCH:
        return 1.0f;
    case FADE_IN: {
        const float t = progress();
        if (t >= 1.0f) {
            phase = IDLE;
        }
        return 1.0f - t;
    }
    default:
        return 0.0f;
    }
}

// Audio factor (main.cpp queue_samples).
float gga_reset_volume() {
    switch (phase.load()) {
    case FADE_OUT:
        return 1.0f - progress();
    case SWITCH:
        return 0.0f;
    case FADE_IN:
        return progress();
    default:
        return 1.0f;
    }
}

// --- game thread (ga.toml hooks in func_800C7B10) ---

// Before state 8 (0x800C7D28), every frame of the game: once the screen is black, the game's own reset request.
extern "C" void ga_reset_poll(uint8_t* rdram, recomp_context* ctx) {
    if (phase == FADE_OUT && progress() >= 1.0f) {
        set_phase(SWITCH);
        call(rdram, ctx, func_800DB140_596360, 0);
    }
}

// Before state 5 (0x800C7C70, creates the logos object): after our request only. $sp + 0x50 = the manager object.
extern "C" void ga_reset_logos(uint8_t* rdram, recomp_context* ctx) {
    if (phase != SWITCH) {
        return;
    }
    const int32_t manager = MEM_W(0x50, ctx->r29);
    boot_data_init(rdram);
    MEM_W(0x4, (int32_t)0x80088338) = manager; // D_80088338 + 4 = manager, written once at its creation (message 2)
    call(rdram, ctx, func_800047EC_53EC, 8);   // overlay 8: the logos
    call(rdram, ctx, func_80002E04_3A04, 0);
    ga_hud_reset_state();
    set_phase(FADE_IN);
}

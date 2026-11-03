// Debug / quicksave hooks used by the UI and the input code of the skeleton. Nothing is implemented for Goemon's Great Adventure yet:
// no warp table (scene numbers are unknown), no quicksave. The UI indexes game_warps[0].scenes[0].entrances, so it must not be empty.
#include "goemon_debug.h"
#include "goemon_game.h"

std::vector<goemon64::AreaWarps> goemon64::game_warps = {
    { "(no warp table yet)", { { 0, "-", { "-" } } } },
};

void goemon64::do_warp(int, int, int) {}

void goemon64::set_time(uint8_t, uint8_t, uint8_t) {}

void goemon64::quicksave_save() {}

void goemon64::quicksave_load() {}

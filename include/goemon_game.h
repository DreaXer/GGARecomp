#ifndef __GOEMON_GAME_H__
#define __GOEMON_GAME_H__

#include <cstdint>
#include <span>
#include <vector>

// The namespace keeps the name of the Goemon64Recomp skeleton (fewer changes in the copied UI / config code).
namespace goemon64 {
    // Decompresses the Goemon's Great Adventure (US) ROM, see src/game/rom_decompression.cpp.
    std::vector<uint8_t> decompress_gga(std::span<const uint8_t> compressed_rom);
    // One file of a Nisitenma-Ichigo table (index = rommy index), decompressed; empty on error. Used on the EU ROM.
    std::vector<uint8_t> decompress_rom_file(std::span<const uint8_t> rom, size_t index);

    // Not implemented yet (empty, src/game/debug.cpp): called by the input code (hotkeys).
    void quicksave_save();
    void quicksave_load();
};

#endif

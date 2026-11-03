// Checks that src/game/rom_decompression.cpp gives exactly baserom.us.decompressed.z64 (output of tools/ga_rommy.py).
// Build + run: .\tools\test_rom_decompression.ps1
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "goemon_game.h"

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s compressed.z64 expected_decompressed.z64\n", argv[0]);
        return 2;
    }
    auto rom = read_file(argv[1]);
    auto expected = read_file(argv[2]);
    auto got = goemon64::decompress_gga(rom);
    std::printf("input %zu bytes, expected %zu, got %zu\n", rom.size(), expected.size(), got.size());
    if (got.empty()) {
        std::puts("FAIL: decompression returned nothing");
        return 1;
    }
    if (got != expected) {
        size_t n = std::min(got.size(), expected.size()), i = 0;
        while (i < n && got[i] == expected[i]) i++;
        std::printf("FAIL: first difference at 0x%zX\n", i);
        return 1;
    }
    std::puts("OK: identical");
    return 0;
}

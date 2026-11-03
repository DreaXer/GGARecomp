// Decompression of the Goemon's Great Adventure (US) ROM at launch. C++ port of tools/ga_rommy.py: the output must be
// byte for byte identical to baserom.us.decompressed.z64 (the ROM N64Recomp was run on). Test: tools/test_rom_decompression.cpp.
//
// The ROM has a Konami "Nisitenma-Ichigo" file table (see notes/ROM_NOTES.md). Per file (table bit 31 = compressed):
//   bit 31 = 0 : raw data
//   bit 31 = 1 : 4-byte header, then a zlib stream (header bit 31 = 1) or LZKN64 (header bit 31 = 0, top 4 bits = 0)
// The output keeps ROM[0 : first file], then every file decompressed back to back, a rewritten table (offsets only, bit 31 cleared),
// and ROM[end marker : end] as is.
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "miniz.h"

#include "goemon_game.h"

namespace {

constexpr std::array<char, 16> SIGNATURE{ 'N', 'i', 's', 'i', 't', 'e', 'n', 'm', 'a', '-', 'I', 'c', 'h', 'i', 'g', 'o' };
constexpr uint32_t COMPRESSED_FLAG = 0x80000000;
constexpr uint32_t OFFSET_MASK = 0x7FFFFFFF;
constexpr size_t EXPECTED_ROM_SIZE = 0x1000000;

uint32_t read_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

void write_be32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

// LZKN64 stream (the 4-byte header is already removed). Returns false on a malformed stream.
// Commands: 00-7F window copy (len (c>>2)+2, offset ((c&3)<<8)|next), 80-9F raw copy (c&0x1F), C0-DF fill with the next byte ((c&0x1F)+2),
// E0-FE zeros ((c&0x1F)+2), FF zeros (next+2). A0-BF is never used (treated as an error).
bool lzkn64_decompress(std::span<const uint8_t> in, std::vector<uint8_t>& out) {
    size_t i = 0;
    while (i < in.size()) {
        uint8_t c = in[i++];
        if (c < 0x80) {
            if (i >= in.size()) return false;
            size_t length = (c >> 2) + 2;
            size_t offset = ((c & 3) << 8) | in[i++];
            if (offset == 0 || offset > out.size()) return false;
            for (size_t k = 0; k < length; k++) {
                out.push_back(out[out.size() - offset]);
            }
        } else if (c < 0xA0) {
            size_t length = c & 0x1F;
            if (i + length > in.size()) return false;
            out.insert(out.end(), in.begin() + i, in.begin() + i + length);
            i += length;
        } else if (c < 0xC0) {
            return false;
        } else if (c < 0xE0) {
            if (i >= in.size()) return false;
            out.insert(out.end(), size_t(c & 0x1F) + 2, in[i++]);
        } else if (c < 0xFF) {
            out.insert(out.end(), size_t(c & 0x1F) + 2, uint8_t(0));
        } else {
            if (i >= in.size()) return false;
            out.insert(out.end(), size_t(in[i++]) + 2, uint8_t(0));
        }
    }
    return true;
}

// zlib stream. One trailing byte after the stream is normal in this ROM, so the input is not required to be fully consumed.
bool zlib_decompress(std::span<const uint8_t> in, std::vector<uint8_t>& out) {
    size_t out_size = 0;
    void* data = tinfl_decompress_mem_to_heap(in.data(), in.size(), &out_size, TINFL_FLAG_PARSE_ZLIB_HEADER);
    if (data == nullptr) return false;
    out.insert(out.end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + out_size);
    mz_free(data);
    return true;
}

// Offset of the first table entry and the entries up to the zero terminator (the last one is the end marker of the last
// file, not a file). Returns false if the table is missing or not sorted.
bool read_table(std::span<const uint8_t> rom, size_t& table_offset, std::vector<uint32_t>& entries) {
    auto sig = std::search(rom.begin(), rom.end(), SIGNATURE.begin(), SIGNATURE.end());
    if (sig == rom.end()) return false;
    table_offset = size_t(sig - rom.begin()) + SIGNATURE.size();
    for (size_t pos = table_offset; pos + 4 <= rom.size(); pos += 4) {
        uint32_t entry = read_be32(rom.data() + pos);
        if (entry == 0) break;
        entries.push_back(entry);
    }
    if (entries.size() < 2) return false;
    for (size_t i = 0; i < entries.size(); i++) {
        uint32_t offset = entries[i] & OFFSET_MASK;
        if (offset > rom.size() || (i > 0 && offset < (entries[i - 1] & OFFSET_MASK))) return false;
    }
    return true;
}

// Appends file i of the table (raw or decompressed) to out.
bool append_file(std::span<const uint8_t> rom, const std::vector<uint32_t>& entries, size_t i, std::vector<uint8_t>& out) {
    const size_t start = entries[i] & OFFSET_MASK;
    const size_t end = entries[i + 1] & OFFSET_MASK;
    if (start == end) return true; // empty file
    std::span<const uint8_t> file = rom.subspan(start, end - start);

    if (!(entries[i] & COMPRESSED_FLAG)) {
        out.insert(out.end(), file.begin(), file.end());
        return true;
    }

    if (file.size() < 4) return false;
    const uint32_t header = read_be32(file.data());
    if (header & COMPRESSED_FLAG) {
        return zlib_decompress(file.subspan(4), out);
    } else if ((header >> 28) == 0 && header >= 4 && header <= file.size()) {
        return lzkn64_decompress(file.subspan(4, header - 4), out); // header = compressed size including the header
    }
    return false; // multi-chunk LZKN64 variant: never seen in this ROM
}

} // namespace

std::vector<uint8_t> goemon64::decompress_rom_file(std::span<const uint8_t> rom, size_t index) {
    size_t table_offset;
    std::vector<uint32_t> entries;
    std::vector<uint8_t> out;
    if (!read_table(rom, table_offset, entries) || index + 1 >= entries.size() || !append_file(rom, entries, index, out)) {
        return {};
    }
    return out;
}

std::vector<uint8_t> goemon64::decompress_gga(std::span<const uint8_t> compressed_rom) {
    // The runtime already checked the hash; this only protects against this file being used for another game.
    if (compressed_rom.size() != EXPECTED_ROM_SIZE || std::memcmp(compressed_rom.data() + 0x3B, "NGME", 4) != 0) {
        assert(false);
        return {};
    }

    size_t table_offset;
    std::vector<uint32_t> entries;
    if (!read_table(compressed_rom, table_offset, entries)) return {};

    const size_t first_file = entries[0] & OFFSET_MASK;
    const size_t end_marker = entries.back() & OFFSET_MASK;
    if (table_offset + entries.size() * 4 > first_file) return {};

    std::vector<uint8_t> out(compressed_rom.begin(), compressed_rom.begin() + first_file);
    out.reserve(0x1A00000); // 27 059 246 bytes expected
    std::vector<uint32_t> new_offsets;
    new_offsets.reserve(entries.size());

    for (size_t i = 0; i + 1 < entries.size(); i++) {
        new_offsets.push_back(uint32_t(out.size()));
        if (!append_file(compressed_rom, entries, i, out)) return {};
    }
    new_offsets.push_back(uint32_t(out.size()));

    out.insert(out.end(), compressed_rom.begin() + end_marker, compressed_rom.end());
    for (size_t i = 0; i < new_offsets.size(); i++) {
        write_be32(out.data() + table_offset + i * 4, new_offsets[i]);
    }
    return out;
}

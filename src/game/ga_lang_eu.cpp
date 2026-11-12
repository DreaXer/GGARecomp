// GGA: French / German dialogues and accented glyphs taken from the player's European ROM (Mystical Ninja 2, NGMP).
// Language = D_80088200 (0 EN, 1 FR, 2 DE), chosen in the title menu (src/game/ga_lang.c). Facts: notes/TEXTES.md.
//
// - Font: the dialogue / ASCII font is a table of 8x12 I4 glyphs (48 bytes) in file 6: 96 glyphs in the US game, 132 in
//   the EU game (0x60..0x83 = accented letters, the codes the EU texts use). The EU table and its widths are copied to
//   unused RDRAM (8..16 MB, RT64 reads up to 16 MB) and the glyph / width reads of codes >= 0x60 are redirected there
//   (hooks of ga.toml).
// - Dialogues: game files 330..342 and 346..368 (rommy 329..341, 345..367) are messages + an event script. The EU file of
//   the language replaces the US file at the end of file_load, converted for the US code: script op 0x0B (call of a game
//   function) address - 0x15B0, op 0x09 (wait in frames) 50 Hz -> 60 Hz. The load table 0x80026B00 is enlarged so that the
//   memory the game reserves fits the longest version.
// Without the EU ROM (or with a different one) nothing changes.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "goemon_config.h"
#include "goemon_game.h"

namespace {

constexpr const char* EU_ROM_NAME = "goemon2.eu.z64"; // in the application folder, next to gga.us.z64 (not in saves/)
constexpr uint32_t LANG_ADDR = 0x80088200;        // D_80088200, written by the language screen of the title menu
constexpr uint32_t LOAD_TABLE = 0x80026B00;       // {start, end} per game file (idx - 1)

constexpr uint32_t US_GLYPHS = 0x80109880;  // D_80109880, 96 glyphs
constexpr uint32_t GLYPH_SIZE = 48;
constexpr uint32_t US_GLYPH_COUNT = 0x60;
constexpr uint32_t EU_GLYPH_COUNT = 0x84;
constexpr uint32_t EU_FILE6_VRAM = 0x800C8F70;
constexpr uint32_t EU_GLYPHS = 0x8010AE80;  // EU glyph table and widths, in EU file 6 (rommy 5)
constexpr uint32_t EU_WIDTHS = 0x8010C740;
constexpr uint32_t FONT_RAM = 0x80E00000;   // our copy: glyphs, then widths
constexpr uint32_t WIDTH_RAM = FONT_RAM + EU_GLYPH_COUNT * GLYPH_SIZE;
// Script start of each dialogue id (pointer 0x08xxxxxx into the file D_8010AED0[id]), read by func_800E7B34's caller
// (0x800E7428): US D_8010B500, ids 0..790; EU 3 blocks of 818 ids (EN, DE, FR; EU id = US id - 1, the US Japanese file
// of id 1 is gone). Rewritten for the language at each file load.
constexpr uint32_t US_SCRIPT_TABLE = 0x8010B500;
constexpr uint32_t US_TABLE_IDS = 791;
constexpr uint32_t EU_SCRIPT_TABLE = 0x8010DEF0;
constexpr uint32_t EU_TABLE_IDS = 818;

uint8_t* g_rdram;
bool g_tried, g_font_ok;
uint32_t g_load_idx;
std::map<uint32_t, std::vector<uint8_t>> g_dialogues[3]; // per language, key = game file index
std::vector<uint8_t> g_eu_font;                            // EU glyphs then widths, empty without EU ROM
std::vector<uint32_t> g_eu_table;                          // EU script table, 3 blocks
std::vector<uint8_t> g_eu_logo;                            // EU title logo, PIC chunk (see ga_rom_read_end)
uint32_t g_tables[3][US_TABLE_IDS];                        // script table per language (0 = the US one)
bool g_tables_ok;
uint8_t g_table_lang;                                      // language currently written in D_8010B500

// Stage title cards (text under the print, e.g. "The Stolen Summoning Machine"): 5 raw 4-bit textures, chosen by
// func_0800ABC0 (module of game file 18) with sizes written in its code. US: archive 762 ids 0x851E..0x8522. EU archive
// (rommy 899): 3 blocks EN / DE / FR (EU ids 0x8533.. / 0x8538.. / 0x853D..), sizes from the EU copy of that function.
struct CardTitle { uint32_t off, w, h; };
constexpr CardTitle CARD_TITLES[2][5] = {
    { { 0x75360, 256, 19 }, { 0x75CE0, 272, 20 }, { 0x76780, 208, 16 }, { 0x76E00, 272, 36 }, { 0x78120, 256, 20 } }, // FR
    { { 0x721F0, 256, 19 }, { 0x72B70, 272, 20 }, { 0x73610, 240, 16 }, { 0x73D90, 176, 38 }, { 0x74AA0, 224, 20 } }, // DE
};
constexpr uint32_t CARD_RAM = 0x80E10000; // free RDRAM after the font and the menu strings
std::vector<uint8_t> g_eu_cards[2][5];

// "STAGE CLEAR" letters (src/game/ga_lang_stc.c), per language (FR, DE): EU letter display lists (EU rommy 668 / 669) and
// their 32x32 PIC textures with their segment address. Textures = EU asset ids (table EU_ASSET_TABLE, offset in the archive):
// 3 own letters (ids 0x8161.. / 0x8164.., archive rommy 671) after the display lists, alphabet 0x8180..0x8189 (rommy 672).
constexpr size_t EU_ASSET_TABLE = 0x2F8C4; // EU twin of D_8002E49C (ROM 0x2F09C): word per asset id >= 0x8000
struct StcTex { uint32_t dest; std::vector<uint8_t> pic; };
std::vector<uint8_t> g_stc_dl[2];
std::vector<StcTex> g_stc_tex[2];

// "AREA CLEAR" of the castle robots (src/game/ga_lang_stc.c): EU file of the models (EU rommy 626 = US 491 + the DE / FR
// display lists 0x7640 / 0x7750) and the PIC texture of each language (FR: asset 0x80E5, DE: the EU English 0x80E4).
std::vector<uint8_t> g_area_file;
StcTex g_area_tex[2];

// "AREA CLEAR" letters of the robot fight (overlay of game file 10, src/game/ga_lang_stc.c): EU file of resource 0xF2
// (segment 3, EU rommy 847 = US 709 + the FR / DE letters) and its 29 assets 0x8458..0x8474 (EU rommy 848), each with its
// segment 3 address (EU table of (id, address) pairs at ROM 0x34054). Chunks that are not PIC are copied as is (func_800040D0).
std::vector<uint8_t> g_alc_file;
std::vector<StcTex> g_alc_tex;

uint8_t rd8(uint32_t addr) { return g_rdram[(addr ^ 3) - 0x80000000u]; }
void wr8(uint32_t addr, uint8_t v) { g_rdram[(addr ^ 3) - 0x80000000u] = v; }
uint32_t rd32(uint32_t addr) { return *reinterpret_cast<uint32_t*>(g_rdram + addr - 0x80000000u); }
void wr32(uint32_t addr, uint32_t v) { *reinterpret_cast<uint32_t*>(g_rdram + addr - 0x80000000u) = v; }
uint32_t be32(const std::vector<uint8_t>& b, size_t i) {
    return (uint32_t(b[i]) << 24) | (uint32_t(b[i + 1]) << 16) | (uint32_t(b[i + 2]) << 8) | b[i + 3];
}
void set_be32(std::vector<uint8_t>& b, size_t i, uint32_t v) {
    b[i] = uint8_t(v >> 24); b[i + 1] = uint8_t(v >> 16); b[i + 2] = uint8_t(v >> 8); b[i + 3] = uint8_t(v);
}

// EU dialogue file -> US code. Text words never look like 0x0B000000 (high halfword 0x0B00 is not a character code), so a
// scan of the aligned words finds the call ops.
// shortcut: the 50 Hz waits (op 0x09) are kept: op 0x09 also passes function arguments, and on the English files no rule
// separates the 29 waits from the identical values (a "x 1.2 everywhere" froze the game); EU timed boxes run ~17 % faster.
void convert_dialogue(std::vector<uint8_t>& f) {
    for (size_t i = 0; i + 8 <= f.size(); i += 4) {
        const uint32_t op = be32(f, i), v = be32(f, i + 4);
        if (op == 0x0B000000 && v >= 0x800EE000 && v < 0x800F0000) {
            set_be32(f, i + 4, v - 0x15B0); // the 41 functions used, all checked (notes/TEXTES.md)
        }
    }
}

void load_eu_rom() {
    // option "Use European Version" (General tab), read once at boot: Off = US game, as without the EU ROM
    if (goemon64::get_use_european_version() != goemon64::UseEuropeanVersion::On) {
        fprintf(stderr, "[GGA LANG] Use European Version is Off, %s not read\n", EU_ROM_NAME);
        return;
    }
    std::ifstream in(goemon64::get_app_folder_path() / EU_ROM_NAME, std::ios::binary);
    if (!in) {
        return;
    }
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (rom.size() != 0x1000000 || std::memcmp(rom.data() + 0x3B, "NGMP", 4) != 0) {
        fprintf(stderr, "[GGA LANG] %s is not the European ROM (z64), ignored\n", EU_ROM_NAME);
        return;
    }

    // font (installed by install_font once file 6 is in memory)
    const std::vector<uint8_t> file6 = goemon64::decompress_rom_file(rom, 5);
    const size_t glyphs = EU_GLYPHS - EU_FILE6_VRAM, widths = EU_WIDTHS - EU_FILE6_VRAM;
    if (file6.size() < widths + EU_GLYPH_COUNT) {
        fprintf(stderr, "[GGA LANG] EU font not found, %s ignored\n", EU_ROM_NAME);
        return;
    }
    g_eu_font.assign(file6.begin() + glyphs, file6.begin() + glyphs + EU_GLYPH_COUNT * GLYPH_SIZE);
    g_eu_font.insert(g_eu_font.end(), file6.begin() + widths, file6.begin() + widths + EU_GLYPH_COUNT);

    // script start table: EU blocks EN / DE / FR (checked against the US table by install_font, once file 6 is loaded)
    const size_t eu_tab = EU_SCRIPT_TABLE - EU_FILE6_VRAM;
    if (file6.size() < eu_tab + 3 * EU_TABLE_IDS * 4) {
        return;
    }
    for (uint32_t i = 0; i < 3 * EU_TABLE_IDS; i++) {
        g_eu_table.push_back(be32(file6, eu_tab + i * 4));
    }

    // title logo: PIC chunk of the EU archive (rommy 899), header "PIC\x1a" 0 then 0x10, width 0x010E, height 0x62
    const std::vector<uint8_t> archive = goemon64::decompress_rom_file(rom, 899);
    static const uint8_t logo_head[] = { 'P', 'I', 'C', 0x1A, 0, 0, 0, 0, 0x10, 0x01, 0x0E, 0x00, 0x62 };
    if (archive.size() >= 0x3A2D0 && std::memcmp(archive.data() + 0x35720, logo_head, sizeof(logo_head)) == 0) {
        g_eu_logo.assign(archive.begin() + 0x35720, archive.begin() + 0x3A2D0);
    }
    // stage title cards: raw 4-bit textures of the same archive, one block of 5 per language (see ga_lang_card)
    for (uint32_t lang = 1; lang <= 2; lang++) {
        for (uint32_t k = 0; k < 5; k++) {
            const CardTitle& t = CARD_TITLES[lang - 1][k];
            if (archive.size() >= t.off + t.w * t.h / 2) {
                g_eu_cards[lang - 1][k].assign(archive.begin() + t.off, archive.begin() + t.off + t.w * t.h / 2);
            }
        }
    }

    // "STAGE CLEAR" letters
    const std::vector<uint8_t> own = goemon64::decompress_rom_file(rom, 671), alphabet = goemon64::decompress_rom_file(rom, 672);
    auto pic = [&](uint32_t id, const std::vector<uint8_t>& archive) {
        const uint32_t off = be32(rom, EU_ASSET_TABLE + (id - 0x8000) * 4) & 0xFFFFFF;
        uint32_t end = be32(rom, EU_ASSET_TABLE + (id - 0x8000 + 1) * 4) & 0xFFFFFF;
        if (end <= off) {
            end = uint32_t(archive.size()); // last asset of the archive
        }
        std::vector<uint8_t> chunk;
        if (end <= archive.size() && end - off >= 4 && std::memcmp(archive.data() + off, "PIC\x1a", 4) == 0) {
            chunk.assign(archive.begin() + off, archive.begin() + end);
        }
        return chunk;
    };
    static const uint32_t stc_file[2] = { 668, 669 }, stc_own_id[2] = { 0x8161, 0x8164 };
    static const uint32_t stc_own_dest[2][3] = { { 0x03000B00, 0x03001300, 0x03001B00 }, { 0x03000700, 0x03000F00, 0x03001700 } };
    for (uint32_t l = 0; l < 2; l++) {
        g_stc_dl[l] = goemon64::decompress_rom_file(rom, stc_file[l]);
        for (uint32_t k = 0; k < 3; k++) {
            g_stc_tex[l].push_back({ stc_own_dest[l][k], pic(stc_own_id[l] + k, own) });
        }
        for (uint32_t k = 0; k < 10; k++) {
            g_stc_tex[l].push_back({ 0x04000000 + k * 0x800, pic(0x8180 + k, alphabet) });
        }
        for (const StcTex& t : g_stc_tex[l]) {
            if (t.pic.empty() || g_stc_dl[l].empty()) {
                fprintf(stderr, "[GGA LANG] EU STAGE CLEAR letters not found, English letters kept\n");
                g_stc_dl[l].clear();
                break;
            }
        }
    }

    // "AREA CLEAR"
    const std::vector<uint8_t> area_archive = goemon64::decompress_rom_file(rom, 635);
    g_area_tex[0] = { 0x0300E8D0, pic(0x80E5, area_archive) };
    g_area_tex[1] = { 0x0300E4B0, pic(0x80E4, area_archive) };
    g_area_file = goemon64::decompress_rom_file(rom, 626);
    if (g_area_file.size() != 0x8C90 || g_area_tex[0].pic.empty() || g_area_tex[1].pic.empty()) {
        fprintf(stderr, "[GGA LANG] EU AREA CLEAR not found, English kept\n");
        g_area_file.clear();
    }

    // "AREA CLEAR" letters
    const std::vector<uint8_t> alc_archive = goemon64::decompress_rom_file(rom, 848);
    g_alc_file = goemon64::decompress_rom_file(rom, 847);
    for (uint32_t k = 0; k < 29 && g_alc_file.size() == 0x1A20; k++) {
        const uint32_t id = be32(rom, 0x34054 + k * 8), dest = be32(rom, 0x34058 + k * 8);
        const uint32_t off = be32(rom, EU_ASSET_TABLE + (id - 0x8000) * 4) & 0xFFFFFF;
        uint32_t end = be32(rom, EU_ASSET_TABLE + (id - 0x8000 + 1) * 4) & 0xFFFFFF;
        if (end <= off) {
            end = uint32_t(alc_archive.size());
        }
        if (id != 0x8458 + k || (dest >> 24) != 3 || end > alc_archive.size()) {
            g_alc_tex.clear();
            break;
        }
        g_alc_tex.push_back({ dest, std::vector<uint8_t>(alc_archive.begin() + off, alc_archive.begin() + end) });
    }
    if (g_alc_tex.size() != 29) {
        fprintf(stderr, "[GGA LANG] EU AREA CLEAR letters not found, English kept\n");
        g_alc_file.clear();
    }

    // dialogues (all 39, so that every table entry matches its file): US rommy u = EU English u - 1, German + 68, French + 136
    for (uint32_t u = 329; u <= 367; u++) {
        const uint32_t idx = u + 1, entry = LOAD_TABLE + (idx - 1) * 8;
        uint32_t size = rd32(entry + 4) - rd32(entry);
        for (uint32_t lang = 1; lang <= 2; lang++) {
            std::vector<uint8_t> f = goemon64::decompress_rom_file(rom, u - 1 + (lang == 1 ? 136 : 68));
            if (f.empty()) {
                continue;
            }
            convert_dialogue(f);
            size = std::max<uint32_t>(size, uint32_t((f.size() + 15) & ~size_t(15)));
            g_dialogues[lang][idx] = std::move(f);
        }
        wr32(entry + 4, rd32(entry) + size); // memory reserved by the game for this file
    }
    fprintf(stderr, "[GGA LANG] European ROM loaded: font + %zu FR / %zu DE dialogue files\n", g_dialogues[1].size(), g_dialogues[2].size());
}

// Copies the EU font to FONT_RAM when the US font is in memory: its 96 glyphs must equal the first EU ones (proves the
// EU offsets, and that file 6 is loaded). Retried at each file load until then.
void install_font() {
    for (uint32_t i = 0; i < US_GLYPH_COUNT * GLYPH_SIZE; i++) {
        if (g_eu_font[i] != rd8(US_GLYPHS + i)) {
            return;
        }
    }
    for (uint32_t i = 0; i < g_eu_font.size(); i++) {
        wr8(FONT_RAM + i, g_eu_font[i]); // glyphs, then widths at WIDTH_RAM
    }
    g_font_ok = true;

    // script table: US id i = EU id i - 1 (i >= 2; id 1 = the US Japanese file, gone in the EU game)
    if (g_eu_table.size() != 3 * EU_TABLE_IDS) {
        return;
    }
    uint32_t same = 0;
    for (uint32_t i = 0; i < US_TABLE_IDS; i++) {
        g_tables[0][i] = g_tables[1][i] = g_tables[2][i] = rd32(US_SCRIPT_TABLE + i * 4);
        if (i >= 2) {
            same += g_eu_table[i - 1] == g_tables[0][i];
            g_tables[1][i] = g_eu_table[2 * EU_TABLE_IDS + i - 1]; // French block
            g_tables[2][i] = g_eu_table[EU_TABLE_IDS + i - 1];     // German block
        }
    }
    g_tables_ok = same >= 600; // measured: 663 of 789 equal (the others: EU English files shifted by a few bytes)
    if (!g_tables_ok) {
        fprintf(stderr, "[GGA LANG] EU script table does not match the US one (%u equal), dialogues not translated\n", same);
    }
}

// The 16 MB European ROM in z64 (big-endian) order: v64 (16-bit swapped) and n64 (32-bit swapped) dumps are put back in order.
// Returns false if it is not the European ROM (size, header code NGMP).
bool eu_rom_to_z64(std::vector<uint8_t>& rom) {
    if (rom.size() != 0x1000000) {
        return false;
    }
    if (rom[0] == 0x37 && rom[1] == 0x80) {
        for (size_t i = 0; i < rom.size(); i += 2) {
            std::swap(rom[i], rom[i + 1]);
        }
    } else if (rom[0] == 0x40 && rom[1] == 0x12) {
        for (size_t i = 0; i < rom.size(); i += 4) {
            std::swap(rom[i], rom[i + 3]);
            std::swap(rom[i + 1], rom[i + 2]);
        }
    }
    return rom[0] == 0x80 && rom[1] == 0x37 && std::memcmp(rom.data() + 0x3B, "NGMP", 4) == 0;
}

} // namespace

// "Use European Version" option (ui_config.cpp): is goemon2.eu.z64 in the application folder and the European ROM?
bool gga_eu_rom_present() {
    std::ifstream in(goemon64::get_app_folder_path() / EU_ROM_NAME, std::ios::binary | std::ios::ate);
    if (!in || in.tellg() != std::streamoff(0x1000000)) {
        return false;
    }
    char head[0x40] = {};
    in.seekg(0);
    in.read(head, sizeof(head));
    return std::memcmp(head + 0x3B, "NGMP", 4) == 0;
}

// Copies the player's European dump (any byte order) to the application folder as goemon2.eu.z64. Empty string = done,
// else the reason it was refused (shown to the player).
std::string gga_eu_rom_install(const std::filesystem::path& source) {
    std::ifstream in(source, std::ios::binary);
    if (!in) {
        return "Failed to open the file.";
    }
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (!eu_rom_to_z64(rom)) {
        return "This is not the European ROM of the game (Mystical Ninja 2 Starring Goemon, NGMP).";
    }
    const std::filesystem::path dest = goemon64::get_app_folder_path() / EU_ROM_NAME;
    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out.write(reinterpret_cast<const char*>(rom.data()), std::streamsize(rom.size()))) {
        return "Failed to copy the ROM to " + dest.string() + ".";
    }
    return {};
}

// Hook at the entry of file_load (through ga_file_load_begin): the first call (boot) reads the EU ROM.
extern "C" void ga_lang_load_begin(uint8_t* rdram, uint32_t idx) {
    g_rdram = rdram;
    g_load_idx = idx;
    if (!g_tried) {
        g_tried = true;
        load_eu_rom();
    }
}

// Hook at the end of file_load (0x80003FD8): dest = $s0, end = $a3 (dest + reserved size).
extern "C" void ga_lang_load_end(uint8_t* rdram, uint32_t dest, uint32_t end) {
    g_rdram = rdram;
    if (!g_font_ok && !g_eu_font.empty()) {
        install_font();
    }
    const uint8_t lang = rd8(LANG_ADDR);
    if (!g_tables_ok) {
        return; // without the matching script table, an EU dialogue file would run from wrong positions (freeze)
    }
    if (lang <= 2 && lang != g_table_lang) { // the table of the language goes with its dialogue files
        for (uint32_t i = 0; i < US_TABLE_IDS; i++) {
            wr32(US_SCRIPT_TABLE + i * 4, g_tables[lang][i]);
        }
        g_table_lang = lang;
    }
    if (lang < 1 || lang > 2 || dest < 0x80000000u || end <= dest) {
        return;
    }
    auto it = g_dialogues[lang].find(g_load_idx);
    if (it == g_dialogues[lang].end() || it->second.size() > end - dest) {
        return;
    }
    const std::vector<uint8_t>& f = it->second;
    for (uint32_t i = 0; i < end - dest; i++) {
        wr8(dest + i, i < f.size() ? f[i] : 0);
    }
}

// Title logo: the title reads its images as PIC chunks of the archive game file 762 (raw reads func_80001CF8(rom, dest, size),
// positions from the table of file 11) and decodes them into segment 3; the logo is the chunk at 0x35720 (0x9F60 bytes,
// PIC header 269 x 102). In the EU archive (EU rommy 899) the logo chunk is 0x35720..0x3A2D0 (270 x 98). With the EU ROM,
// the EU chunk is written over the US one right after its DMA, and ga.toml gives the drawing code the EU size / position.
namespace {
constexpr uint32_t LOGO_FILE = 762;       // game file index of the title archive
constexpr uint32_t LOGO_OFF = 0x35720, LOGO_US_SIZE = 0x9F60, LOGO_EU_END = 0x3A2D0;
constexpr uint32_t NISITENMA = 0x8002BB94; // file table in RAM: decompressed ROM offset of game file idx = entry idx - 1
uint32_t g_read_rom;
}

// Hook at the entry of func_80001CF8 (raw ROM read): remembers the ROM offset for ga_rom_read_end.
extern "C" void ga_rom_read_begin(uint32_t rom) {
    g_read_rom = rom;
}

// Hook after the DMA of func_80001CF8 (0x80001D28): dest / size saved by the game at $sp + 0x24 / 0x28.
extern "C" void ga_rom_read_end(uint8_t* rdram, uint32_t dest, uint32_t size) {
    g_rdram = rdram;
    if (!g_tables_ok || g_eu_logo.empty() || size != LOGO_US_SIZE ||
        g_read_rom != (rd32(NISITENMA + (LOGO_FILE - 1) * 4) & 0x7FFFFFFFu) + LOGO_OFF) {
        return;
    }
    for (uint32_t i = 0; i < size; i++) {
        wr8(dest + i, i < g_eu_logo.size() ? g_eu_logo[i] : 0);
    }
}

// Hook in func_0800ABC0 right after func_800CDF44 turned the title texture (segment 3) into an address ($v0); obj = $v1
// saved at $sp + 0x34: +0x10 title index, +0x1C / +0x20 width / height. In French / German: the EU title is copied to
// CARD_RAM and replaces the address and the size. Returns the address to use.
extern "C" uint32_t ga_lang_card(uint8_t* rdram, uint32_t addr, uint32_t obj) {
    g_rdram = rdram;
    const uint32_t lang = rd8(LANG_ADDR), k = rd32(obj + 0x10);
    if (!g_tables_ok || (lang != 1 && lang != 2) || k >= 5 || g_eu_cards[lang - 1][k].empty()) {
        return addr;
    }
    const std::vector<uint8_t>& tex = g_eu_cards[lang - 1][k];
    for (uint32_t i = 0; i < tex.size(); i++) {
        wr8(CARD_RAM + i, tex[i]);
    }
    wr32(obj + 0x1C, CARD_TITLES[lang - 1][k].w);
    wr32(obj + 0x20, CARD_TITLES[lang - 1][k].h);
    return CARD_RAM;
}

// EU "STAGE CLEAR" letters of lang (1 FR, 2 DE) for src/game/ga_lang_stc.c: display lists (size, 0 = none) and texture k
// (size, 0 = no more; dest = its segment address).
extern "C" uint32_t ga_stc_dl(uint32_t lang, const uint8_t** data) {
    if (lang < 1 || lang > 2 || g_stc_dl[lang - 1].empty()) {
        return 0;
    }
    *data = g_stc_dl[lang - 1].data();
    return uint32_t(g_stc_dl[lang - 1].size());
}

extern "C" uint32_t ga_stc_tex(uint32_t lang, uint32_t k, uint32_t* dest, const uint8_t** data) {
    if (lang < 1 || lang > 2 || k >= g_stc_tex[lang - 1].size()) {
        return 0;
    }
    *dest = g_stc_tex[lang - 1][k].dest;
    *data = g_stc_tex[lang - 1][k].pic.data();
    return uint32_t(g_stc_tex[lang - 1][k].pic.size());
}

// EU "AREA CLEAR" for src/game/ga_lang_stc.c: the models file (size, 0 = none) and the texture of lang (1 FR, 2 DE).
extern "C" uint32_t ga_area_file(const uint8_t** data) {
    *data = g_area_file.data();
    return uint32_t(g_area_file.size());
}

extern "C" uint32_t ga_area_tex(uint32_t lang, uint32_t* dest, const uint8_t** data) {
    if (lang < 1 || lang > 2 || g_area_file.empty()) {
        return 0;
    }
    *dest = g_area_tex[lang - 1].dest;
    *data = g_area_tex[lang - 1].pic.data();
    return uint32_t(g_area_tex[lang - 1].pic.size());
}

// EU "AREA CLEAR" letters for src/game/ga_lang_stc.c: the file of resource 0xF2 (size, 0 = none) and asset k (size,
// 0 = no more; dest = its segment 3 address; *pic = 1 if it is a PIC chunk to decode, 0 to copy as is).
extern "C" uint32_t ga_alc_file(const uint8_t** data) {
    *data = g_alc_file.data();
    return uint32_t(g_alc_file.size());
}

extern "C" uint32_t ga_alc_tex(uint32_t k, uint32_t* dest, const uint8_t** data, uint32_t* pic) {
    if (g_alc_file.empty() || k >= g_alc_tex.size()) {
        return 0;
    }
    const std::vector<uint8_t>& t = g_alc_tex[k].pic;
    *dest = g_alc_tex[k].dest;
    *data = t.data();
    *pic = t.size() >= 4 && std::memcmp(t.data(), "PIC\x1a", 4) == 0;
    return uint32_t(t.size());
}

// 1 when the EU ROM is loaded and checked (font + script table): the title menu then offers the language choice (ga.toml).
extern "C" int ga_lang_eu_ready(void) {
    return g_tables_ok ? 1 : 0;
}

// Glyph / width hooks (ga.toml): address of glyph code, or 0 to keep the game's value; width of code, or -1.
extern "C" uint32_t ga_font_glyph(uint8_t*, uint32_t code) {
    return (g_font_ok && code >= US_GLYPH_COUNT && code < EU_GLYPH_COUNT) ? FONT_RAM + code * GLYPH_SIZE : 0;
}

extern "C" int ga_font_width(uint8_t*, uint32_t code) {
    return (g_font_ok && code >= US_GLYPH_COUNT && code < EU_GLYPH_COUNT) ? rd8(WIDTH_RAM + code) : -1;
}

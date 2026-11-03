/*
 * GGA: language choice of the European version (English / Français / Deutsch) in the US game. See notes/TEXTES.md.
 *
 * The US title module (game file 18 = rommy 17) still has the EU menus (Option screen [Language][Sound], Language screen
 * writing the language to D_80088200, 0 = EN, 1 = FR, 2 = DE): ga.toml only reconnects them. Its buttons draw
 * table[item * 3 + language] (pointer table 0x0800BF30, 9 items: Start, Option, Stereo, Monaural, Language, Sound, English,
 * Français, Deutsch), but the US table holds "Start" / "START" / "start"...: it is rewritten with the EU labels each time
 * the module is mapped. The game's ASCII text (func_800EB1DC) already handles the EU accent prefixes ("<e" = é, "{c" = ç).
 */
#include "recomp.h"

#define TITLE_TABLE     0xBF30u      /* 27 string pointers (0x0800xxxx), item * 3 + language */
#define TITLE_STRINGS   0xD150u      /* the US strings of the table, 0xF0 bytes up to the jump table 0x0800D240 */
#define TITLE_STRINGS_N 0xF0u

/* EU labels (module 17 of the EU ROM, table 0x0800C900), per item: EN, FR, DE */
static const char* const title_labels[27] = {
    "Start",    "Commencez", "Start",
    "Option",   "Option",    "Optionen",
    "Stereo",   "St<er<eo",  "Stereo",
    "Monaural", "Mono",      "Mono",
    "Language", "Langue",    "Sprache",
    "Sound",    "Son",       "Sound",
    "English",  "English",   "English",
    "Fran{cais", "Fran{cais", "Fran{cais",
    "Deutsch",  "Deutsch",   "Deutsch",
};

/* Translated strings that do not fit in the modules: written to unused RDRAM after the EU font (src/game/ga_lang_eu.cpp,
   0x80E00000..0x80E01984). The game's text objects take such a KSEG0 pointer as is (only 0x08xxxxxx pointers are
   converted with the module base, func_800EB1DC). */
#define STRINGS_RAM 0x80E02000u
static uint32_t strings_next = STRINGS_RAM;

static uint32_t put_string(uint8_t* rdram, const char* s) {
    const uint32_t addr = strings_next;
    uint32_t k = 0;
    do {
        MEM_B(0, (int32_t)(strings_next++)) = (int8_t)s[k];
    } while (s[k++] != '\0');
    return addr;
}

/* World map (game file 16 = rommy 15): table[item * 3 + language] at 0x08007F78 (func_08001654 reads D_80088200), items
   Move, Enter stage; the US table holds "Move" / "MOVE" / "move". EU labels: module 15 of the EU ROM, table 0x08007F88. */
#define MAP_TABLE 0x7F78u
static const char* const map_labels[6] = {
    "Move",        "D<eplacer",       "Bewegung",
    "Enter stage", "Etape d'entr<ee", "Stufe betreten",
};
static uint32_t map_ptr[6];

void ga_lang_map_mapped(uint8_t* rdram, uint32_t base) {
    uint32_t i;
    /* only if the table is there: a raw-address mapping can show memory the game reused after the module (2026-10-25: these
       pointers landed in an object pool of the castle robot fight -> crash in func_800D4CEC) */
    for (i = 0; i < 6; i++) {
        const uint32_t w = (uint32_t)MEM_W(0, (int32_t)(base + MAP_TABLE + i * 4));
        if ((w & 0xFFFF0000u) != 0x08000000u && (map_ptr[i] == 0 || w != map_ptr[i])) {
            return;
        }
    }
    for (i = 0; i < 6; i++) {
        if (map_ptr[i] == 0) {
            map_ptr[i] = put_string(rdram, map_labels[i]);
        }
        MEM_W(0, (int32_t)(base + MAP_TABLE + i * 4)) = (int32_t)map_ptr[i];
    }
}

/* Journal (save) and player screens of the title module: 8 strings given in code (lui / addiu) to func_08001070 (6th argument,
   $sp + 0x14), the EU game takes them from a table of 3 (module 17 of the EU ROM, 0x0800C5C0). Hook at the entry of
   func_08001070: the US English string is replaced by the one of the language. */
static const uint32_t text_us[8] = {
    0x0800C4D0, 0x0800C524, 0x0800C534, 0x0800C53C, 0x0800C544, 0x0800C55C, 0x0800C56C, 0x0800C57C,
};
static const char* const text_labels[8][2] = { /* FR, DE */
    {"Depuis le d<ebut", "Vom Anfang"},
    {"Choisir le journal", "Tagebuch w~ahlen"},
    {"Lesquelles?", "Welche?"},
    {"O>u?", "Wohin?"},
    {"Choisir le joueur", "Spielfigur w~ahlen"},
    {"Copier le journal", "Tagebuch kopieren"},
    {"Effacer le journal", "Tagebuch l~oschen"},
    {"Annuler", "Abbrechen"},
};
static uint32_t text_ptr[8][2];

void ga_lang_title_text(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t arg = (uint32_t)ctx->r29 + 0x14;
    const uint32_t s = (uint32_t)MEM_W(0, (int32_t)arg);
    const int lang = MEM_B(0, (int32_t)0x80088200) - 1; /* D_80088200: 0 EN, 1 FR, 2 DE */
    uint32_t i;
    if (lang < 0 || lang > 1) {
        return;
    }
    for (i = 0; i < 8; i++) {
        if (s == text_us[i]) {
            if (text_ptr[i][lang] == 0) {
                text_ptr[i][lang] = put_string(rdram, text_labels[i][lang]);
            }
            MEM_W(0, (int32_t)arg) = (int32_t)text_ptr[i][lang];
            return;
        }
    }
}

/* Called by ga_module_mapped (src/game/ga_modules.c) when the title module is mapped at base (KSEG0 address). */
void ga_lang_title_mapped(uint8_t* rdram, uint32_t base) {
    int ga_lang_eu_ready(void); /* src/game/ga_lang_eu.cpp */
    uint32_t i, k, off = 0;
    if (!ga_lang_eu_ready()) {
        /* no EU ROM: US menu without language choice, so a language saved earlier (with the EU ROM) goes back to English */
        MEM_B(0, (int32_t)0x80088200) = 0;
    }
    /* only if the table is there (pointers to its strings, US or ours): see ga_lang_map_mapped */
    for (i = 0; i < 27; i++) {
        const uint32_t w = (uint32_t)MEM_W(0, (int32_t)(base + TITLE_TABLE + i * 4));
        if (w < 0x08000000u + TITLE_STRINGS || w >= 0x08000000u + TITLE_STRINGS + TITLE_STRINGS_N) {
            return;
        }
    }
    /* addresses as signed 32-bit values with offset 0: MEM_W(unsigned offset, int32_t reg) would not sign-extend */
    for (i = 0; i < 27; i++) {
        /* the 27 labels take 205 of the TITLE_STRINGS_N bytes */
        MEM_W(0, (int32_t)(base + TITLE_TABLE + i * 4)) = (int32_t)(0x08000000u + TITLE_STRINGS + off);
        for (k = 0; title_labels[i][k] != '\0'; k++) {
            MEM_B(0, (int32_t)(base + TITLE_STRINGS + off++)) = (int8_t)title_labels[i][k];
        }
        MEM_B(0, (int32_t)(base + TITLE_STRINGS + off++)) = 0;
    }
}

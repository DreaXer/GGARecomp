/*
 * GGA: "STAGE CLEAR" in French / German, as in the European version (letters taken from the player's EU ROM). See
 * notes/TEXTES.md, "STAGE CLEAR".
 *
 * Module "mesStageClear" (game file 108, US rommy 107, vram 0x08000000): a controller (func_08000000) spawns one 3D letter
 * object (func_08000470) every 5 frames; a letter flies in from (x0, y0) to (0, 3), spreads to its final x (x1), flashes,
 * then the controller makes pairs of letters spin. The US module is written for the 10 letters S,T,A,G,E,C,L,E,A,R; the EU
 * module has the same code with per-language tables: French 14 letters, German 12, on two lines (final y), with their own
 * letter display lists (segment 3) and an alphabet of textures (segment 4).
 *
 * What ga.toml changes in the US module (all languages):
 * - the letter tables (models, x0, y0, x1) are read from TAB (written here at each controller init, US values in English);
 * - the controller's 10 letter slots (work + 0x34..0x58) are replaced by 14 slots at SLOTS (one STAGE CLEAR at a time);
 *   its loops stop at the byte work + 0x34 = letter count * 4 (written here). The EU layout (counters moved to work + 0x6C)
 *   is NOT used: the US work area is smaller, the first try overwrote the next object (crash in func_800D40FC);
 * - hooks below: init, spawn period, segments of the letter models, final y, spin direction, flash, pairs of spinning
 *   letters (all languages, since the US code read its slots in the work area).
 * In French / German, segment 3 / 4 of the letters point to SEG3 / SEG4, filled at init with the EU display lists and the
 * EU textures, decoded by the game's own PIC decoder (func_80005820 / func_80005888, as func_800040D0 does).
 */
#include <stdio.h>
#include "recomp.h"

void func_80005820_6420(uint8_t* rdram, recomp_context* ctx); /* (pic, header): reads a PIC header */
void func_80005888_6488(uint8_t* rdram, recomp_context* ctx); /* (pic, dest, header): decodes it */
int ga_lang_eu_ready(void);
uint32_t ga_stc_dl(uint32_t lang, const uint8_t** data);
uint32_t ga_stc_tex(uint32_t lang, uint32_t k, uint32_t* dest, const uint8_t** data);

#define LANG_ADDR 0x80088200u /* D_80088200: 0 EN, 1 FR, 2 DE */
#define SEG3      0x80E20000u /* free RDRAM (after the font, strings and card title of ga_lang_eu.cpp / ga_lang.c) */
#define SEG4      0x80E24000u
#define TAB       0x80E2F000u /* models, x0, y0, x1: 16 words each (ga.toml patches: lui 0x80E3, offset 0xF000..) */
#define SLOTS     0x80E30034u /* letter object of each slot (ga.toml: $s1 = 0x80E30000, lw 0x34($s1)) */
#define PIC_SRC   0x80E31000u
#define PIC_HDR   0x80E3F000u
#define US_TABLES 0x08000C50u /* module tables: models, x0, y0, x1 (10 entries each, 0x28 bytes apart) */
#define MAX_LETTERS 14

typedef struct {
    uint32_t count;
    uint32_t model[MAX_LETTERS]; /* display list in segment 3 */
    float x0[MAX_LETTERS], y0[MAX_LETTERS], x1[MAX_LETTERS], y1[MAX_LETTERS];
    int8_t spin[MAX_LETTERS];    /* +1 / -1: direction of the final spin, 0: none */
    uint8_t pairs[5][7];         /* frames 0x28, 0x2D, 0x32, 0x37, 0x3C of the controller: slots that spin, 0xFF = end */
} StcLang;

/* US pairs (func_08000000, the US letters keep their tables). */
static const uint8_t us_pairs[5][7] = { { 0, 9, 0xFF }, { 1, 8, 0xFF }, { 2, 7, 0xFF }, { 3, 6, 0xFF }, { 4, 5, 0xFF } };

/* Values of the EU module (EU rommy 106, tables written by its first function, pairs and spins in its code). */
static const StcLang stc_langs[2] = {
    { 14, /* French */
      { 0x03000058, 0x03000158, 0x03000258, 0x03000358, 0x03000458, 0x03000558, 0x03000658, 0x03000858, 0x03000758,
        0x03000858, 0x03000958, 0x03000258, 0x03000A58, 0x03000858 },
      { -20.0f, -21.7f, -23.4f, -25.0f, -23.4f, -21.7f, -20.0f, 20.0f, 21.7f, 23.4f, 25.0f, 23.4f, 21.7f, 20.0f },
      { 18.0f, 13.0f, 8.0f, 3.0f, -2.0f, -7.0f, -12.0f, -12.0f, -7.0f, -2.0f, 3.0f, 8.0f, 13.0f, 18.0f },
      { -15.75f, -11.25f, -6.75f, -2.25f, 2.25f, 6.75f, 11.25f, 15.75f, -12.7f, -6.75f, -2.25f, 2.25f, 6.75f, 11.25f },
      { 5.6f, 5.6f, 5.6f, 5.6f, 5.6f, 5.6f, 5.6f, 5.6f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f },
      { 1, 1, 1, 1, -1, -1, -1, -1, 1, 1, 1, -1, -1, -1 },
      { { 0xFF }, { 0, 7, 0xFF }, { 1, 6, 8, 13, 0xFF }, { 2, 5, 9, 12, 0xFF }, { 3, 4, 10, 11, 0xFF } } },
    { 12, /* German */
      { 0x03000058, 0x03000158, 0x03000258, 0x03000158, 0x03000058, 0x03000358, 0x03000158, 0x03000158, 0x03000458,
        0x03000558, 0x03000158, 0x03000658 },
      { -20.0f, -22.5f, -25.0f, -25.0f, -22.5f, -20.0f, 20.0f, 22.5f, 25.0f, 25.0f, 22.5f, 20.0f },
      { 18.0f, 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -12.0f, -6.0f, 0.0f, 6.0f, 12.0f, 18.0f },
      { -9.0f, -4.5f, 0.0f, 4.5f, 9.0f, -13.5f, -9.0f, -4.5f, 0.0f, 4.5f, 9.0f, 13.5f },
      { 5.6f, 5.6f, 5.6f, 5.6f, 5.6f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f, 0.4f },
      { 1, 1, 0, -1, -1, 1, 1, 1, 0, -1, -1, -1 },
      { { 0xFF }, { 5, 11, 0xFF }, { 0, 4, 6, 10, 0xFF }, { 1, 3, 7, 9, 2, 8, 0xFF }, { 0xFF } } },
};

static uint32_t stc_lang; /* language of the letters on screen: 0 = the US letters */

static float getf(uint8_t* rdram, uint32_t addr) {
    union { uint32_t u; float f; } v;
    v.u = (uint32_t)MEM_W(0, (int32_t)addr);
    return v.f;
}

static void setf(uint8_t* rdram, uint32_t addr, float f) {
    union { uint32_t u; float f; } v;
    v.f = f;
    MEM_W(0, (int32_t)addr) = (int32_t)v.u;
}

static recomp_context ctx_copy(recomp_context* ctx) { /* see ga_hud.c: f_odd points into the context */
    recomp_context c = *ctx;
    c.f_odd = (uint32_t*)((uint8_t*)&c + ((uint8_t*)ctx->f_odd - (uint8_t*)ctx));
    return c;
}

static void copy_in(uint8_t* rdram, uint32_t dest, const uint8_t* data, uint32_t size) {
    uint32_t i;
    for (i = 0; i < size; i++) {
        MEM_B(0, (int32_t)(dest + i)) = (int8_t)data[i];
    }
}

/* Decodes a PIC chunk to dest with the game's decoder, as func_800040D0 does; returns the decoded size. */
static uint32_t pic_decode(uint8_t* rdram, recomp_context* ctx, const uint8_t* data, uint32_t size, uint32_t dest) {
    recomp_context c;
    copy_in(rdram, PIC_SRC, data, size);
    c = ctx_copy(ctx);
    c.r29 = ctx->r29 - 0x80;
    c.r4 = (gpr)(int32_t)PIC_SRC;
    c.r5 = (gpr)(int32_t)PIC_HDR;
    func_80005820_6420(rdram, &c);
    c = ctx_copy(ctx);
    c.r29 = ctx->r29 - 0x80;
    c.r4 = (gpr)(int32_t)PIC_SRC;
    c.r5 = (gpr)(int32_t)dest;
    c.r6 = (gpr)(int32_t)PIC_HDR;
    func_80005888_6488(rdram, &c);
    return (uint32_t)MEM_W(0xC, (int32_t)PIC_HDR); /* decoded size (func_800040D0 reads it there) */
}

/* Writes the EU letters of lang into SEG3 / SEG4. 0 if a texture does not decode to 32x32 RGBA16. */
static int stc_build(uint8_t* rdram, recomp_context* ctx, uint32_t lang) {
    const uint8_t* data;
    uint32_t size = ga_stc_dl(lang, &data), dest, k;
    if (size == 0) {
        return 0;
    }
    copy_in(rdram, SEG3, data, size);
    for (k = 0; (size = ga_stc_tex(lang, k, &dest, &data)) != 0; k++) {
        dest = ((dest >> 24) == 3 ? SEG3 : SEG4) + (dest & 0xFFFFFF);
        if (pic_decode(rdram, ctx, data, size, dest) != 0x800) {
            fprintf(stderr, "[GGA LANG] STAGE CLEAR: EU texture %u did not decode, English letters kept\n", k);
            return 0;
        }
    }
    return 1;
}

/* "AREA CLEAR" of the castle robots: modules of game files 37 / 41 (US rommy 36 / 40, EU 35 / 39) create the models
   0x03007420 (common) and 0x03007530 (text) of game file 492 (segment 3). The EU modules take 0x03007750 in French and
   0x03007640 in German, display lists of the EU file (EU rommy 626 = US 491 + 0x220 bytes) whose texture is decoded after
   the file. French / German: the EU file and the texture go to AREA_SEG3, the text model draws from there. */
#define AREA_SEG3 0x80E40000u /* 0x8C90 bytes of file, textures up to 0xF000 */
uint32_t ga_area_file(const uint8_t** data);
uint32_t ga_area_tex(uint32_t lang, uint32_t* dest, const uint8_t** data);

void ga_area_model(uint8_t* rdram, recomp_context* ctx, uint32_t model) {
    const uint32_t lang = (uint8_t)MEM_BU(0, (int32_t)LANG_ADDR);
    const uint8_t *file, *pic;
    uint32_t dest, mask, size, file_size;
    if (model == 0 || (lang != 1 && lang != 2) || !ga_lang_eu_ready() ||
        (size = ga_area_tex(lang, &dest, &pic)) == 0 || (file_size = ga_area_file(&file)) == 0) {
        return;
    }
    fprintf(stderr, "[GGA LANG] AREA CLEAR: EU text model (lang %u) for model %08X\n", lang, model);
    copy_in(rdram, AREA_SEG3, file, file_size);
    if (pic_decode(rdram, ctx, pic, size, AREA_SEG3 + (dest & 0xFFFFFF)) == 0) {
        fprintf(stderr, "[GGA LANG] AREA CLEAR: EU texture did not decode, English kept\n");
        return;
    }
    mask = ((uint32_t)MEM_W(0x44, (int32_t)model) & 0x80000000u) ? 0xFFFFFFFFu : 0x1FFFFFFFu;
    MEM_W(0x40, (int32_t)model) = lang == 1 ? 0x03007750 : 0x03007640;
    MEM_W(0x44, (int32_t)model) = (int32_t)(AREA_SEG3 & mask);
}

/* "AREA CLEAR" after a robot fight (overlay of game file 10, notes/TEXTES.md). US: the controller func_80189914 creates
   9 letters (func_801894DC(x, y, z, index 0..8) at frames 5, 15.. 85, sound 0x206 at 0x46.. 0x96); the letter object
   func_80188224 takes its model (resource 0xF2, segment 3) and start position from its index, and its animation block
   (fly to a target with func_8017B4A0, trail objects func_8018953C whose model also follows the index). The EU overlay has
   the same code with more indexes: French 0x0A..0x16, German 0x1E..0x2A (13 letters each, frames 5 + 8k, sounds 0x46 + 8k).
   French / German here: the controller's own spawns and sounds are skipped ($v0 = frame faked until the end test at
   0x80189D00) and done in C; each letter is created as US index 2 (template: its blocks call everything we change) and
   remembered with its EU index; hooks then give its model / trails the EU display list (EU file of resource 0xF2 and its
   textures in ALC_SEG3), its start position and its target. */
#define ALC_SEG3 0x80E50000u /* EU file 0x1A20 bytes + assets up to 0x0300ECA0 */
#define ALC_TEMPLATE 2
#define ALC_MAX 16
uint32_t ga_alc_file(const uint8_t** data);
uint32_t ga_alc_tex(uint32_t k, uint32_t* dest, const uint8_t** data, uint32_t* pic);
void func_801894DC_63033C(uint8_t* rdram, recomp_context* ctx); /* (f12 x, f14 y, a2 z, a3 index): creates a letter */
void func_80008B6C_976C(uint8_t* rdram, recomp_context* ctx);   /* (sound id) */

static uint32_t alc_lang;         /* language of the letters of the current fight, 0 = US letters */
static uint32_t alc_faked;        /* 1 while $v0 is faked in the controller */
static uint32_t alc_obj[ALC_MAX], alc_idx[ALC_MAX], alc_n;
static uint32_t alc_pending, alc_pending_trail; /* EU index of the letter / trail being created */

/* EU display list (segment 3) of each EU index: 0x0A..0x16 (French), 0x1E..0x2A (German) */
static const uint16_t alc_dl[2][13] = {
    { 0xAF0, 0xBC0, 0xBC0, 0xC90, 0xD60, 0xE30, 0xF00, 0xD60, 0xAF0, 0xFD0, 0x10A0, 0xBC0, 0xE30 },
    { 0x1170, 0x1240, 0x1310, 0x13E0, 0x1240, 0x14B0, 0x1650, 0x1580, 0x1720, 0x17F0, 0x18C0, 0x1240, 0x1990 },
};
/* target (x, y) of each EU index (z -50 as in the US code) */
static const int8_t alc_target[2][13][2] = {
    { { -20, 5 }, { -14, 5 }, { -8, 5 }, { -2, 5 }, { 4, 5 }, { 10, 5 }, { 16, 5 },
      { -14, -3 }, { -8, -3 }, { 4, -3 }, { 10, -3 }, { 16, -3 }, { 22, -3 } },
    { { -14, 5 }, { -8, 5 }, { -2, 5 }, { 4, 5 }, { 10, 5 }, { 16, 5 },
      { -17, -3 }, { -11, -3 }, { -5, -3 }, { 1, -3 }, { 7, -3 }, { 13, -3 }, { 19, -3 } },
};
/* start position of the k-th letter (same in US and EU), then (-3, -151) */
static const int16_t alc_start[6][2] = { { -97, 111 }, { -137, 5 }, { -97, -101 }, { 95, -93 }, { 138, 8 }, { 97, 96 } };

static uint32_t fbits(float f) {
    union { uint32_t u; float f; } v;
    v.f = f;
    return v.u;
}

static uint32_t alc_find(uint32_t obj) {
    uint32_t i;
    for (i = 0; i < alc_n; i++) {
        if (alc_obj[i] == obj) {
            return alc_idx[i];
        }
    }
    return 0;
}

/* Model of a letter / trail: EU display list of EU index e, segment 3 = ALC_SEG3. */
static void alc_model(uint8_t* rdram, uint32_t model, uint32_t e) {
    const uint32_t mask = ((uint32_t)MEM_W(0x44, (int32_t)model) & 0x80000000u) ? 0xFFFFFFFFu : 0x1FFFFFFFu;
    const uint32_t base = e >= 0x1E ? 0x1E : 0x0A;
    MEM_W(0x40, (int32_t)model) = (int32_t)(0x03000000u + alc_dl[base == 0x1E][e - base]);
    MEM_W(0x44, (int32_t)model) = (int32_t)(ALC_SEG3 & mask);
}

static int alc_build(uint8_t* rdram, recomp_context* ctx) {
    const uint8_t* data;
    uint32_t size = ga_alc_file(&data), dest, pic, k;
    if (size == 0) {
        return 0;
    }
    copy_in(rdram, ALC_SEG3, data, size);
    for (k = 0; (size = ga_alc_tex(k, &dest, &data, &pic)) != 0; k++) {
        dest = ALC_SEG3 + (dest & 0xFFFFFF);
        if (!pic) {
            copy_in(rdram, dest, data, size);
        } else if (pic_decode(rdram, ctx, data, size, dest) == 0) {
            fprintf(stderr, "[GGA LANG] AREA CLEAR: EU letter texture %u did not decode, English kept\n", k);
            return 0;
        }
    }
    return 1;
}

/* Controller func_80189914, letters state, before the first compare (0x80189A0C): $v0 = frame, $a1 = work. Returns the
   value for $v0 (faked in French / German so that the US spawns and sounds are skipped). */
uint32_t ga_alc_ctrl(uint8_t* rdram, recomp_context* ctx, uint32_t frame) {
    const uint32_t lang = (uint8_t)MEM_BU(0, (int32_t)LANG_ADDR);
    recomp_context c;
    alc_faked = 0;
    if (frame == 5) {
        alc_n = 0;
        alc_lang = ((lang == 1 || lang == 2) && ga_lang_eu_ready() && alc_build(rdram, ctx)) ? lang : 0;
    }
    if (alc_lang == 0) {
        return frame;
    }
    if (frame >= 5 && (frame - 5) % 8 == 0 && (frame - 5) / 8 < 13 && alc_n < ALC_MAX) {
        alc_pending = (alc_lang == 1 ? 0x0A : 0x1E) + (frame - 5) / 8;
        c = ctx_copy(ctx);
        c.r29 = ctx->r29 - 0x100;
        c.f12.u32l = 0;
        c.f14.u32l = 0;
        c.r6 = 0;
        c.r7 = ALC_TEMPLATE;
        func_801894DC_63033C(rdram, &c);
        alc_pending = 0;
    }
    if (frame >= 0x46 && frame <= 0x9E && (frame - 0x46) % 8 == 0) {
        c = ctx_copy(ctx);
        c.r29 = ctx->r29 - 0x100;
        c.r4 = 0x206;
        func_80008B6C_976C(rdram, &c);
    }
    alc_faked = 1;
    return 0x7FFF;
}

/* Before the end test of that state (0x80189D00): the real frame back in $v0. */
uint32_t ga_alc_ctrl_end(uint8_t* rdram, uint32_t v0, uint32_t work) {
    if (!alc_faked) {
        return v0;
    }
    alc_faked = 0;
    return (uint32_t)MEM_W(0x24, (int32_t)work);
}

/* End of the letter init (func_80188224, 0x8018871C): a letter created by ga_alc_ctrl gets its EU model and start. */
void ga_alc_letter_init(uint8_t* rdram, uint32_t obj, uint32_t work) {
    const uint32_t e = alc_pending, k = e - (e >= 0x1E ? 0x1E : 0x0A);
    const uint32_t model = (uint32_t)MEM_W(0, (int32_t)work);
    if (e == 0 || model == 0 || alc_n >= ALC_MAX) {
        return;
    }
    alc_obj[alc_n] = obj;
    alc_idx[alc_n++] = e;
    alc_model(rdram, model, e);
    setf(rdram, model + 0x14, k < 6 ? alc_start[k][0] : -3.0f);
    setf(rdram, model + 0x18, k < 6 ? alc_start[k][1] : -151.0f);
}

/* Template block (index 2), before the trail call: the trail takes the letter's EU index. */
void ga_alc_trail_pending(uint8_t* rdram, uint32_t obj) {
    (void)rdram;
    alc_pending_trail = alc_find(obj);
}

/* End of the trail init (func_8018953C, 0x80189738): EU model. */
void ga_alc_trail_init(uint8_t* rdram, uint32_t work) {
    const uint32_t model = (uint32_t)MEM_W(0, (int32_t)work);
    if (alc_pending_trail != 0 && model != 0) {
        alc_model(rdram, model, alc_pending_trail);
    }
    alc_pending_trail = 0;
}

/* Template block, before func_8017B4A0(obj, x, y, z, speed): EU target. */
void ga_alc_target(uint8_t* rdram, recomp_context* ctx, uint32_t obj) {
    const uint32_t e = alc_find(obj), base = e >= 0x1E ? 0x1E : 0x0A;
    (void)rdram;
    if (e != 0) {
        ctx->r5 = (gpr)(int32_t)fbits(alc_target[base == 0x1E][e - base][0]);
        ctx->r6 = (gpr)(int32_t)fbits(alc_target[base == 0x1E][e - base][1]);
    }
}

/* Controller init (func_08000000, message 2), work = obj + 0x20: language, letter tables, letter count. */
void ga_stc_init(uint8_t* rdram, recomp_context* ctx, uint32_t obj) {
    const uint32_t lang = (uint8_t)MEM_BU(0, (int32_t)LANG_ADDR);
    uint32_t i, count = 10;
    stc_lang = 0;
    if ((lang == 1 || lang == 2) && ga_lang_eu_ready() && stc_build(rdram, ctx, lang)) {
        stc_lang = lang;
    }
    if (stc_lang != 0) {
        const StcLang* l = &stc_langs[stc_lang - 1];
        count = l->count;
        for (i = 0; i < count; i++) {
            MEM_W(0, (int32_t)(TAB + i * 4)) = (int32_t)l->model[i];
            setf(rdram, TAB + 0x40 + i * 4, l->x0[i]);
            setf(rdram, TAB + 0x80 + i * 4, l->y0[i]);
            setf(rdram, TAB + 0xC0 + i * 4, l->x1[i]);
        }
    } else {
        for (i = 0; i < count; i++) {
            MEM_W(0, (int32_t)(TAB + i * 4)) = MEM_W(0, (int32_t)(US_TABLES + i * 4));
            MEM_W(0, (int32_t)(TAB + 0x40 + i * 4)) = MEM_W(0, (int32_t)(US_TABLES + 0x28 + i * 4));
            MEM_W(0, (int32_t)(TAB + 0x80 + i * 4)) = MEM_W(0, (int32_t)(US_TABLES + 0x50 + i * 4));
            MEM_W(0, (int32_t)(TAB + 0xC0 + i * 4)) = MEM_W(0, (int32_t)(US_TABLES + 0x78 + i * 4));
        }
    }
    for (i = 0; i < MAX_LETTERS; i++) {
        MEM_W(0, (int32_t)(SLOTS + i * 4)) = 0;
    }
    MEM_B(0, (int32_t)(obj + 0x20 + 0x34)) = (int8_t)(count * 4);
}

/* After func_800CDEE0(model + 0x44, 0x4A) (segment 3..6 addresses of a letter model): EU letters in French / German. */
void ga_stc_seg(uint8_t* rdram, uint32_t model) {
    const uint32_t mask = ((uint32_t)MEM_W(0x44, (int32_t)model) & 0x80000000u) ? 0xFFFFFFFFu : 0x1FFFFFFFu;
    if (stc_lang != 0) {
        MEM_W(0x44, (int32_t)model) = (int32_t)(SEG3 & mask);
        MEM_W(0x48, (int32_t)model) = (int32_t)(SEG4 & mask);
    }
}

/* Letter state 1 (spread to x1, 6 frames; work + 0x5E = frame): the EU letters also move from y 3 to their line. */
void ga_stc_spread(uint8_t* rdram, uint32_t work) {
    const uint32_t k = (uint8_t)MEM_BU(0x5C, (int32_t)work);
    float y1;
    if (stc_lang == 0 || k >= stc_langs[stc_lang - 1].count) {
        return;
    }
    y1 = stc_langs[stc_lang - 1].y1[k];
    if ((uint16_t)MEM_HU(0x5E, (int32_t)work) >= 6) {
        setf(rdram, work + 0x14, y1);
    } else {
        setf(rdram, work + 0x14, getf(rdram, work + 0x14) + (y1 - 3.0f) / 6.0f);
    }
}

/* Letter state 3 (spin, work + 0x28 += 0.2 if the result is 1, else -= 0.2; replaces "slti $at, index, 5"). */
uint32_t ga_stc_spin(uint8_t* rdram, uint32_t work, uint32_t k) {
    int s;
    if (stc_lang == 0 || k >= stc_langs[stc_lang - 1].count) {
        return k < 5 ? 1 : 0;
    }
    s = stc_langs[stc_lang - 1].spin[k];
    if (s == 0) { /* no spin: undo the + 0.2 the game will add (same double arithmetic) */
        setf(rdram, work + 0x28, (float)((double)getf(rdram, work + 0x28) - 0.2));
    }
    return s >= 0 ? 1 : 0;
}

/* End of the fly-in: the game sets work + 0x5B (flash) to 0xFF; the EU code skips it for its 14th French letter. */
uint32_t ga_stc_flash(uint8_t* rdram, uint32_t work, uint32_t v) {
    if (stc_lang == 1 && (uint8_t)MEM_BU(0x5C, (int32_t)work) == 13) {
        return (uint8_t)MEM_BU(0x5B, (int32_t)work);
    }
    return v;
}

/* Controller state 0: a letter is spawned when work + 0x5C (frame) % 5 == 1. French / German: every 4 frames (as the EU
   game, at 50 Hz), so that the 14th letter has landed (12 frames) before the controller spreads them at frame 0x4E. */
uint32_t ga_stc_spawn(uint8_t* rdram, uint32_t work, uint32_t r) {
    return stc_lang == 0 ? r : (uint16_t)MEM_HU(0x5C, (int32_t)work) % 4;
}

/* Controller state 3, frame % 100 = f: pairs of letters spin at f = 0x28..0x3C (US pairs or EU ones), the end at 0x46 is
   left to the US code; its own pair compares see a frame that matches nothing (they read the old slots). */
uint32_t ga_stc_pairs(uint8_t* rdram, uint32_t work, uint32_t f) {
    const uint8_t* s;
    (void)work;
    if (f == 0x46) {
        return f;
    }
    if (f >= 0x28 && f <= 0x3C && (f - 0x28) % 5 == 0) {
        for (s = stc_lang == 0 ? us_pairs[(f - 0x28) / 5] : stc_langs[stc_lang - 1].pairs[(f - 0x28) / 5]; *s != 0xFF; s++) {
            const uint32_t letter = (uint32_t)MEM_W(0, (int32_t)(SLOTS + *s * 4));
            if (letter != 0) {
                MEM_B(0x20, (int32_t)letter) = 3; /* func_08000BD8(letter, 3) */
            }
        }
    }
    return 0xFFFF;
}

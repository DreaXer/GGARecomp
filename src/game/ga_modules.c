/*
 * GGA: TLB emulation for the code modules (VRAM 0x08000000) and loading of the fixed overlays (0x801738A0).
 * Called from the [[patches.hook]] entries of ga.toml. Design and measured facts: notes/OVERLAYS.md, "TLB émulée".
 *
 * On the console a module is copied to the heap (4 KB aligned) and module_map (0x80004638) maps it at 0x08000000 with
 * osMapTLB, one 8 KB TLB entry per page, pages 0..n only (n = load size >> 13): the pages above n KEEP the previously
 * mapped module (measured 2026-10-19, loop 0x8000470C..0x80004770; the game relies on it: it calls 0x08006098 of module 21
 * while a smaller module is mapped). The game keeps module pointers in their 0x08xxxxxx form and converts them itself
 * (base + (ptr & 0xFFFFFF)), so nothing is relocated. In the recompilation:
 *   - gga_tlb_page[] / gga_tlb_owner[] = what each TLB page shows (address, module ROM), updated by the module_map hook;
 *   - the memory access macros of include/gga_recomp/recomp.h translate 0x08000000..0x0803DFFF through gga_tlb_page;
 *   - LOOKUP_FUNC of a 0x08xxxxxx address asks the module owning the page (gga_get_function, src/game/ga_tlb.cpp);
 *   - __osProbeTLB answers for the same table (the game's osVirtualToPhysical, renamed, is recompiled and calls it).
 */
#include <stdio.h>
#include "recomp.h"

void load_overlays(uint32_t rom, int32_t ram_addr, uint32_t size);  /* librecomp/overlays.hpp (extern "C") */

/* 1 = trace every module load / TLB mapping to stderr ([GGA LOAD] / [GGA MAP], very verbose; useful when a
   "Failed to find function at 0x08xxxxxx" or a [GGA TLB] error comes back). */
#define GGA_TRACE_MAP 0

#define GA_FILE_TABLE  0x8002BB94u /* Nisitenma-Ichigo entries, game file idx-1 (decompressed ROM offsets in rdram) */
#define GA_LOAD_TABLE  0x80026B00u /* {start, end} link addresses, game file idx-1 */
#define GA_FILE_COUNT  0xA10u      /* valid game file indices: 1..0xA10 (test sltiu 0xa11 in the loader) */

uint32_t gga_tlb_page[GGA_TLB_PAGES];  /* KSEG0 address shown by each 8 KB page of 0x08000000 */
uint32_t gga_tlb_owner[GGA_TLB_PAGES]; /* ROM offset (decompressed ROM) of the module owning the page, 0 = unknown */

/* Modules loaded in memory (KSEG0 address -> ROM), to name the module when the game maps a raw address.
   shortcut: 64 most recent loads, a module still mapped after 64 other module loads is not found by address. */
#define GA_LOADS 64
static uint32_t load_addr[GA_LOADS], load_rom[GA_LOADS], load_size[GA_LOADS], load_next;

static uint32_t rd32(uint8_t* rdram, uint32_t addr) { return (uint32_t)MEM_W(0, (int32_t)addr); }

/* ROM offset (decompressed ROM) and size of a game file, 0 if invalid or empty. */
static uint32_t file_rom(uint8_t* rdram, uint32_t idx, uint32_t* size) {
    uint32_t rom;
    *size = 0;
    if (idx == 0 || idx > GA_FILE_COUNT) {
        return 0;
    }
    rom = rd32(rdram, GA_FILE_TABLE + (idx - 1) * 4) & 0x7FFFFFFFu;
    *size = (rd32(rdram, GA_FILE_TABLE + idx * 4) & 0x7FFFFFFFu) - rom;
    return rom;
}

/* Hook at the entry of file_load(idx, dest) 0x80003E50. Code at a fixed address (file 6, overlays 8/9/10) is registered
   where it is loaded; modules (linked at 0x08000000) are only remembered: they become visible through module_map. */
void ga_file_load_begin(uint8_t* rdram, uint32_t idx, uint32_t dest) {
    void ga_lang_load_begin(uint8_t*, uint32_t); /* src/game/ga_lang_eu.cpp */
    uint32_t size, vram;
    uint32_t rom = file_rom(rdram, idx, &size);
    ga_lang_load_begin(rdram, idx);
    if (size == 0) {
        return;
    }
    vram = rd32(rdram, GA_LOAD_TABLE + (idx - 1) * 8);
    if (vram == GGA_TLB_VRAM) {
        load_addr[load_next] = dest | 0x80000000u;
        load_rom[load_next] = rom;
        load_size[load_next] = size;
        if (GGA_TRACE_MAP) {
            fprintf(stderr, "[GGA LOAD] file %u (rom 0x%X) at 0x%08X size 0x%X\n", idx, rom, dest | 0x80000000u, size);
        }
        load_next = (load_next + 1) % GA_LOADS;
    } else if (vram >= 0x80000000u) {
        load_overlays(rom, (int32_t)dest, size); /* no-op for data files (no recompiled section there) */
    }
}

/* Hook in module_map at 0x80004714 (start of the TLB loop, reached only when the game really maps): idx = game file index
   (0 when the game maps a raw address), phys = mapped address & 0x3FFFFFFF, last = last TLB entry written (0..0x1E). */
void ga_module_mapped(uint8_t* rdram, uint32_t idx, uint32_t phys, uint32_t last) {
    uint32_t size, i, k, owner = file_rom(rdram, idx, &size);
    const uint32_t base = phys | 0x80000000u;
    for (i = 0; i <= last && i < GGA_TLB_PAGES; i++) {
        const uint32_t page = base + i * 0x2000u;
        uint32_t rom = owner;
        if (idx == 0) {
            /* raw address: the page shows the memory, so its owner is the NEWEST load covering this page;
               if no load covers it, keep the previous owner when the page did not move */
            rom = (gga_tlb_page[i] == page) ? gga_tlb_owner[i] : 0;
            for (k = 1; k <= GA_LOADS; k++) {
                const uint32_t j = (load_next + GA_LOADS - k) % GA_LOADS;
                if (load_size[j] != 0 && page >= load_addr[j] && page < load_addr[j] + load_size[j]) {
                    rom = load_rom[j];
                    break;
                }
            }
            if (i == 0) {
                owner = rom;
            }
        }
        gga_tlb_page[i] = page;
        gga_tlb_owner[i] = rom;
    }
    /* diagnostic (visible with --log): the last lines before "Failed to find function at 0x08xxxxxx" name the mapping */
    if (GGA_TRACE_MAP) {
        fprintf(stderr, "[GGA MAP] file %u (rom 0x%X) at 0x%08X, pages 0..%u\n", idx, owner, base, last);
    }
    {
        void ga_lang_title_mapped(uint8_t*, uint32_t);
        void ga_lang_map_mapped(uint8_t*, uint32_t);
        uint32_t tsize;
        if (owner != 0 && owner == file_rom(rdram, 18, &tsize)) { /* game file 18 = title module */
            ga_lang_title_mapped(rdram, base);
        } else if (owner != 0 && owner == file_rom(rdram, 16, &tsize)) { /* game file 16 = world map module */
            ga_lang_map_mapped(rdram, base);
        }
    }
}

/* __osProbeTLB(vaddr) -> physical address, or -1 if not mapped (N64Recomp skips the real one: it uses cop0). */
void __osProbeTLB_recomp(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t off = (uint32_t)ctx->r4 - GGA_TLB_VRAM;
    (void)rdram;
    if (off < GGA_TLB_SIZE && gga_tlb_page[off >> 13] != 0) {
        ctx->r2 = (gpr)(int32_t)((gga_tlb_page[off >> 13] & 0x1FFFFFFFu) + (off & 0x1FFFu));
    } else {
        ctx->r2 = (gpr)(int32_t)-1;
    }
}




// GGA: LOOKUP_FUNC of the recompiled code (include/gga_recomp/recomp.h). A 0x08xxxxxx address is looked up in the module
// that owns its TLB page (tables kept by src/game/ga_modules.c, see notes/OVERLAYS.md, "TLB émulée"); anything else goes to
// the runtime's get_function as before.
#include <cstdio>
#include <cstdlib>
#include "recomp.h"
#include "librecomp/overlays.hpp"

extern "C" uint32_t gga_tlb_owner[31];

extern "C" recomp_func_t* gga_get_function(int32_t vram) {
    const uint32_t off = (uint32_t)vram - 0x08000000u;
    if (off >= 31u * 0x2000u) {
        return get_function(vram);
    }
    const uint32_t owner = gga_tlb_owner[off >> 13];
    recomp_func_t* func = recomp::overlays::get_func_by_section_rom_function_vram(owner, (uint32_t)vram);
    if (func == nullptr) {
        fprintf(stderr, "[GGA TLB] no function at 0x%08X in the module of ROM 0x%X (page %u)\n", (uint32_t)vram, owner, off >> 13);
        std::exit(EXIT_FAILURE);
    }
    return func;
}

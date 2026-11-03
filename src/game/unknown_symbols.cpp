// Libultra-internal functions that N64Recomp calls as <name>_recomp (the functions are named in config/symbol_addrs.txt) and that
// the runtime does not provide. Empty stubs, like unknown_symbols.cpp of Goemon64Recomp. Measured at link time (2026-10-19):
// Controller Pak (Pfs / Cont Ram), thread queue and Vi / Si / timer internals. Check at runtime that none of them is really needed.
#include "recomp.h"
#include "ultramodern/ultramodern.hpp"

// Controller Pak: __osContRamRead/Write and __osPfsGetStatus are emulated in ga_pak.cpp; the other Pfs internals are renamed *_ga
// (config/symbol_addrs.txt) so the game's own code runs (2026-10-20).
extern "C" void __osContAddressCrc_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void __osEnqueueThread_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void __osPopThread_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void __osSiGetAccess_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void __osSiRelAccess_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void __osTimerInterrupt_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void __osViGetCurrentContext_recomp(uint8_t* rdram, recomp_context* ctx) {}
extern "C" void __osViSwapContext_recomp(uint8_t* rdram, recomp_context* ctx) {}

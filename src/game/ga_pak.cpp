// GGA: Controller Pak (mempak) emulation at the hardware level (2026-10-20).
// The game's own libultra Pfs code is recompiled (osPfsInitPak, AllocateFile, ReadWriteFile..., and the internals renamed *_ga in
// config/symbol_addrs.txt); only the joybus accesses are replaced here: __osPfsGetStatus (is a pak plugged?) and the 32-byte block
// reads / writes __osContRamRead / __osContRamWrite. Each pak is a standard 32 KB mempak image (same layout as an emulator .mpk),
// stored in the runtime's save buffer at port * 0x8000 (saves/<game id>.bin, written to disk by the runtime's saving thread).
// shortcut: needs a save buffer of at least 0x8000 per enabled port (SaveType::AllowAll = 0x20000, src/main/main.cpp).
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include "recomp.h"
#include "ultramodern/ultra64.h"

void save_write(RDRAM_ARG PTR(void) rdram_address, uint32_t offset, uint32_t count);
void save_read(RDRAM_ARG PTR(void) rdram_address, uint32_t offset, uint32_t count);
void save_write_ptr(const void* in, uint32_t offset, uint32_t count);
extern "C" void gga_pak_written(void); // ga_reset.cpp: the Reset button is greyed out for a while after a write

// Ports with a Controller Pak. A port with a pak has no Rumble Pak (hook of func_80001F24 in ga.toml), like on hardware.
static constexpr bool pak_on_port[4] = { true, false, false, false };

static constexpr uint32_t PAK_SIZE = 0x8000;   // 32 KB, 1 bank
static constexpr uint32_t BLOCK = 32;
static constexpr int32_t PFS_ERR_NOPACK = 1;

static bool pak_checked[4] = {};

extern "C" int ga_pak_present(int port) {
    return port >= 0 && port < 4 && pak_on_port[port];
}

// Blank formatted pak: ID area (block 0 label, ID at blocks 1 / 3 / 4 / 6), inode table page 1 and its copy page 2 (all pages free),
// empty note table (pages 3-4).
static void pak_format(int port) {
    uint8_t img[0x500] = {};
    uint8_t id[32] = {
        0xFF, 0xFF, 0xFF, 0xFF,  0x05, 0x1A, 0x5F, 0x13,              // repaired, random
        0, 0, 0, 0, 0, 0, 0, 0,                                     // serial_mid
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,             // serial_low
        0x00, 0x01,  0x01,  0x00,                                   // deviceid (bit 0 set), banks, version
    };
    uint16_t sum = 0, isum = 0;                                     // __osIdCheckSum: 14 big-endian halfwords
    for (int i = 0; i < 28; i += 2) {
        uint16_t w = (uint16_t)((id[i] << 8) | id[i + 1]);
        sum += w;
        isum += (uint16_t)~w;
    }
    id[28] = sum >> 8; id[29] = sum & 0xFF; id[30] = isum >> 8; id[31] = isum & 0xFF;
    for (int blk : { 1, 3, 4, 6 }) {
        memcpy(&img[blk * BLOCK], id, sizeof(id));
    }
    // Inode entries 5..127 = 0x0003 (free); entry 0 low byte = 8-bit sum of the bytes of entries 5..127 (123 * 3 = 0x171 -> 0x71).
    for (int page = 0x100; page <= 0x200; page += 0x100) {
        for (int e = 5; e < 128; e++) {
            img[page + e * 2 + 1] = 0x03;
        }
        img[page + 1] = 0x71;
    }
    save_write_ptr(img, port * PAK_SIZE, sizeof(img));
}

// First access of the session: format the pak if its ID block is still blank (new save file).
static void pak_check(uint8_t* rdram, int port, gpr buffer) {
    if (pak_checked[port]) {
        return;
    }
    pak_checked[port] = true;
    save_read(rdram, (int32_t)buffer, port * PAK_SIZE + 1 * BLOCK, BLOCK);
    for (uint32_t i = 0; i < BLOCK; i++) {
        if (MEM_B(i, buffer) != 0) {
            return;
        }
    }
    pak_format(port);
}

// s32 __osPfsGetStatus(OSMesgQueue* mq, int channel)
extern "C" void __osPfsGetStatus_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = ga_pak_present((int)ctx->r5) ? 0 : PFS_ERR_NOPACK;
}

// s32 __osContRamRead(OSMesgQueue* mq, int channel, u16 address, u8* buffer): one 32-byte block.
extern "C" void __osContRamRead_recomp(uint8_t* rdram, recomp_context* ctx) {
    const int port = (int)ctx->r5;
    const uint32_t address = (uint32_t)ctx->r6 & 0xFFFF;
    const gpr buffer = ctx->r7;
    if (!ga_pak_present(port)) {
        ctx->r2 = PFS_ERR_NOPACK;
        return;
    }
    pak_check(rdram, port, buffer);
    if (address * BLOCK < PAK_SIZE) {
        save_read(rdram, (int32_t)buffer, port * PAK_SIZE + address * BLOCK, BLOCK);
    } else {
        for (uint32_t i = 0; i < BLOCK; i++) {   // above 32 KB (bank select / rumble area): nothing there
            MEM_B(i, buffer) = 0;
        }
    }
    ctx->r2 = 0;
}

// s32 __osContRamWrite(OSMesgQueue* mq, int channel, u16 address, u8* buffer, int force)
extern "C" void __osContRamWrite_recomp(uint8_t* rdram, recomp_context* ctx) {
    const int port = (int)ctx->r5;
    const uint32_t address = (uint32_t)ctx->r6 & 0xFFFF;
    if (!ga_pak_present(port)) {
        ctx->r2 = PFS_ERR_NOPACK;
        return;
    }
    if (address * BLOCK < PAK_SIZE) {   // writes above 32 KB (bank select of a 1-bank pak) are ignored, like on hardware
        save_write(rdram, (int32_t)ctx->r7, port * PAK_SIZE + address * BLOCK, BLOCK);
        gga_pak_written();
    }
    ctx->r2 = 0;
}

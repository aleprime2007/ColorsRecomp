#include "boot_env.h"

#include "disc.h"
#include "gxruntime/boot.h"
#include "ios.h"
#include "pi.h"

#include <stdio.h>
#include <string.h>

// Apploader-provided top of the MEM1 arena, just below the IPC/heap area.
#define BOOT_ARENA_HI 0x817FEC60u

void boot_env_setup(CPUState* cpu, const DolLayout* layout) {
    ios_init();
    boot_setup_os_globals(cpu, layout);

    // The IPL/apploader copies the disc's 32-byte boot header (the disc ID,
    // e.g. "SNCE8P") to 0x80000000. The DVD layer reads the game code, disc
    // number and region from there and treats an all-zero ID as an invalid
    // disc (showing the "disc error" screen).
    {
        const u8* id = disc_disk_id();
        for (u32 i = 0; i < 32u; i += 4u) {
            u32 w = ((u32)id[i] << 24) | ((u32)id[i + 1] << 16) |
                    ((u32)id[i + 2] << 8) | (u32)id[i + 3];
            mem_write32(cpu, 0x80000000u + i, w);
        }
    }
    // Keep the disc ID for the IOS /dev/di ReadDiskID service.
    ios_set_disc_id(disc_disk_id());

    // The GC/Wii apploader reserves the OS-globals block (0x80003100..0x80003140)
    // before jumping to the DOL, including the IOS IPC heap bounds that
    // IPCCltInit checks (start + 0x2000 <= end). We only load the DOL, so
    // emulate that reservation here.
    //
    // 0x80003100.. is the IOS/boot memory map (WiiBrew "Memory map"). These are
    // what the PPC-side loader and the game's memory manager read to discover
    // how much MEM2 exists and where the usable window is. Leaving them zero
    // (or writing pointers where sizes belong) makes the renderer's MEM2 pool
    // arithmetic underflow: it computes usable_end - usable_start, and with
    // usable_end == 0 that is a negative size, so the pool hands out blocks
    // that overlap live allocations.
    mem_write32(cpu, 0x80003100, 0x01800000u);  // physical MEM1 size (24 MiB)
    mem_write32(cpu, 0x80003104, 0x01800000u);  // simulated MEM1 size
    mem_write32(cpu, 0x80003118, 0x04000000u);  // physical MEM2 size (64 MiB)
    mem_write32(cpu, 0x8000311C, 0x04000000u);  // simulated MEM2 size
    mem_write32(cpu, 0x80003120, 0x93400000u);  // end of MEM2 addressable to PPC
    mem_write32(cpu, 0x80003124, 0x90000800u);  // usable MEM2 start
    mem_write32(cpu, 0x80003128, 0x933E0000u);  // usable MEM2 end
    mem_write32(cpu, 0x80003130, 0x933E0000u);  // IOS IPC heap start
    mem_write32(cpu, 0x80003134, 0x93400000u);  // IOS IPC heap end (0x20000)
    mem_write32(cpu, 0x80003138, 0x00000011u);  // Hollywood version

    // MEM1 boot-arena bounds. OSInit reads these apploader-provided fields
    // (0x8000310C/0x80003110) for the initial arena; the DSP/AX init then
    // shrinks the arena to reserve its stack. With 0x80003110 left at 0 the
    // audio thread switches to stack 0xFFFFFFF8 and crashes.
    mem_write32(cpu, 0x8000310C, mem_read32(cpu, 0x80000030u));  // arena lo
    mem_write32(cpu, 0x80003110, BOOT_ARENA_HI);                 // arena hi

    // Apploader emulation: the real apploader reads the FST off the disc and
    // places it at the top of MEM1, publishing its address/size at
    // 0x80000038/0x8000003C. The OS then shrinks ArenaHi to the FST start so
    // the arena never overwrites it, and the game's DVD layer resolves file
    // paths -> disc offsets from this in-memory FST. We skipped the apploader,
    // so do that step here (otherwise every disc file lookup fails and the
    // game shows its disc-error screen).
    {
        u32 fst_size = 0;
        const u8* fst = disc_fst(&fst_size);
        if (fst_size != 0u && fst != NULL) {
            u32 fst_addr = (BOOT_ARENA_HI - fst_size) & ~31u;
            for (u32 i = 0; i < fst_size; i += 4u) {
                u32 w = 0;
                for (u32 b = 0; b < 4u; b++) {
                    u32 idx = i + b;
                    w = (w << 8) | (idx < fst_size ? fst[idx] : 0u);
                }
                mem_write32(cpu, fst_addr + i, w);
            }
            mem_write32(cpu, 0x80000038u, fst_addr);
            mem_write32(cpu, 0x8000003Cu, fst_size);
            // Keep the arena below the FST image.
            mem_write32(cpu, 0x80003110u, fst_addr);
            fprintf(stderr, "[boot] FST image at 0x%08X (%u bytes)\n", fst_addr, fst_size);
        }
    }

    // Seed the PI/GX FIFO registers the way GXInit would, so the game's
    // "remaining FIFO space" arithmetic starts from a valid window instead of
    // subtracting a stack pointer from 0x80000000 (which underflowed).
    pi_fifo_init();
}

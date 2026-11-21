// Wii disc-interface / external-interface MMIO, translated onto GXRuntime's
// GameCube DI/EXI device models.
#include "dvd.h"

#include "gxruntime/di.h"
#include "gxruntime/exi.h"

#include "log.h"

#include <stdio.h>

#define WII_DI_BASE  0xCD006000u
#define WII_EXI_BASE 0xCD006800u

static DolDi g_di;
static DolExi g_exi;
static u32 g_di_reads;
static u32 g_di_writes;

void dvd_init(void) {
    dol_di_init(&g_di);
    dol_exi_init(&g_exi);
    // A disc is present, and the disc drive is attached to EXI channel 0.
    dol_di_set_disc_present(&g_di, true);
    dol_exi_set_device_present(&g_exi, 0, true, false);
}

bool dvd_mmio_contains(u32 ea) {
    if (ea >= WII_DI_BASE && ea < WII_DI_BASE + DOL_DI_REGISTER_BYTES)
        return true;
    if (ea >= WII_EXI_BASE && ea < WII_EXI_BASE + DOL_EXI_REGISTER_BYTES)
        return true;
    return false;
}

u64 dvd_mmio_read(u32 ea, u8 size) {
    if (ea >= WII_DI_BASE && ea < WII_DI_BASE + DOL_DI_REGISTER_BYTES) {
        g_di_reads++;
        return dol_di_mmio_read(&g_di, DOL_DI_BASE + (ea - WII_DI_BASE), size);
    }
    if (ea >= WII_EXI_BASE && ea < WII_EXI_BASE + DOL_EXI_REGISTER_BYTES)
        return dol_exi_mmio_read(&g_exi, DOL_EXI_BASE + (ea - WII_EXI_BASE), size);
    return 0;
}

void dvd_mmio_write(CPUState* cpu, u32 ea, u8 size, u64 value) {
    if (ea >= WII_DI_BASE && ea < WII_DI_BASE + DOL_DI_REGISTER_BYTES) {
        g_di_writes++;
        if (g_mmio_log)
            fprintf(stderr, "[di] write%u 0x%08X = 0x%llX pc=0x%08X\n",
                    size, ea, (unsigned long long)value, cpu->pc);
        dol_di_mmio_write(&g_di, cpu, DOL_DI_BASE + (ea - WII_DI_BASE), size, value);
        return;
    }
    if (ea >= WII_EXI_BASE && ea < WII_EXI_BASE + DOL_EXI_REGISTER_BYTES) {
        dol_exi_mmio_write(&g_exi, cpu, DOL_EXI_BASE + (ea - WII_EXI_BASE), size, value);
        return;
    }
}

void dvd_mmio_stats(u32* reads, u32* writes) {
    if (reads != NULL)
        *reads = g_di_reads;
    if (writes != NULL)
        *writes = g_di_writes;
}

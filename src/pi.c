#include "pi.h"

#include "log.h"

#include <stdio.h>

// 0x0C..0x28 in 4-byte steps.
static u32 g_pi_fifo[8];
static bool g_pi_fifo_init_done;

bool pi_fifo_contains(u32 addr) {
    return addr >= PI_FIFO_BASE_REG && addr < PI_FIFO_BASE_REG + PI_FIFO_SIZE;
}

u64 pi_fifo_read(u32 addr) {
    const u32 off = (addr - PI_FIFO_BASE_REG) & ~3u;
    if (off < sizeof(g_pi_fifo)) {
        if (g_mmio_log)
            fprintf(stderr, "[pi] read  0x%08X -> 0x%08X\n", addr, g_pi_fifo[off / 4u]);
        return g_pi_fifo[off / 4u];
    }
    return 0;
}

void pi_fifo_write(u32 addr, u64 value) {
    const u32 off = (addr - PI_FIFO_BASE_REG) & ~3u;
    if (off < sizeof(g_pi_fifo)) {
        if (g_mmio_log)
            fprintf(stderr, "[pi] write 0x%08X = 0x%08X\n", addr, (unsigned)value);
        g_pi_fifo[off / 4u] = (u32)value;
        // Writing the write pointer means the guest queued commands; the GP
        // consumes them immediately, so keep the guest-visible read pointer
        // equal to it (space always available).
        if (addr == PI_FIFO_WPTR_REG)
            g_pi_fifo[(PI_FIFO_WPTR_REG - PI_FIFO_BASE_REG) / 4u] = (u32)value;
        if (off == 0u && !g_pi_fifo_init_done) {
            g_pi_fifo_init_done = true;
            if (g_mmio_log)
                fprintf(stderr, "[pi] fifo base=0x%08X start=0x%08X end=0x%08X\n",
                        (unsigned)value, (unsigned)g_pi_fifo[1], (unsigned)g_pi_fifo[2]);
        }
    }
}

void pi_fifo_init(void) {
    g_pi_fifo[(0x00u) / 4u] = 0x80000000u;              // base
    g_pi_fifo[(0x04u) / 4u] = 0x80000000u;              // start
    g_pi_fifo[(0x08u) / 4u] = 0x80000000u + 0x200000u;  // end
    g_pi_fifo[(0x0Cu) / 4u] = 0x80000000u + 0x200000u;  // write pointer
}

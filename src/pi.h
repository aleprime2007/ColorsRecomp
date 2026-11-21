#ifndef COLORSRECOMP_PI_H
#define COLORSRECOMP_PI_H

#include "core/types.h"

#include <stdbool.h>

// PI / GX FIFO registers (0xCC00300C..0xCC003027).
//
// The GX command processor reads the FIFO base/start/end/wrap registers and the
// write pointer to compute free space. Without them (reads returning 0) the
// game's "remaining space" arithmetic underflows, which produced a runaway
// sample/display-list copy. We store what the guest programs and let the read
// pointer track the write pointer (the FIFO drains instantly), which is the
// "GP always catches up" model used by headless backends.
//
// NOTE: this block must be routed *before* interrupt_mmio_*() in the bus
// dispatch: GXRuntime's interrupt model claims 0xCC003000.. and answers
// 0xCC003014 with 0, which would shadow this model.
#define PI_FIFO_BASE_REG 0xCC00300Cu
#define PI_FIFO_END_REG 0xCC003014u
#define PI_FIFO_WPTR_REG 0xCC003018u
#define PI_FIFO_SIZE 0x1Cu

bool pi_fifo_contains(u32 addr);
u64  pi_fifo_read(u32 addr);
void pi_fifo_write(u32 addr, u64 value);

// Seed the registers the way GXInit would, so the guest's FIFO-space
// arithmetic starts from a valid window. Call once during boot setup.
void pi_fifo_init(void);

#endif

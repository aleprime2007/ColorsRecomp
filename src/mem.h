#ifndef COLORSRECOMP_MEM_H
#define COLORSRECOMP_MEM_H

#include "core/cpu.h"
#include "core/types.h"

#include <stdbool.h>

// Wii MEM2 (64 MiB at 0x90000000, uncached mirror 0xD0000000 — the
// WII_MEM2_* constants come from <core/cpu.h>) plus the small accessors the
// MMIO layer and the diagnostics use to peek at guest memory.

// Allocate the MEM2 backing store. Returns false on allocation failure.
bool mem2_init(void);
void mem2_shutdown(void);

// True once mem2_init() succeeded.
bool mem2_ready(void);

// Offset of `addr` inside MEM2, or (u32)-1 when it is not a MEM2 address.
u32 mem2_offset(u32 addr);

// Host pointer to the MEM2 backing store (NULL before init).
u8* mem2_host_pointer(u32 off);

u64 mem2_read(u32 off, u8 size);
void mem2_write(u32 off, u64 val, u8 size);

// Bounds-checked MEM1 access (offsets into cpu->ram).
u64 ram_read(CPUState* cpu, u32 off, u8 size);
void ram_write(CPUState* cpu, u32 off, u64 val, u8 size);

#endif

#ifndef COLORSRECOMP_DVD_H
#define COLORSRECOMP_DVD_H

#include "core/cpu.h"

// Wii disc-interface (DI) + external-interface (EXI) MMIO. The Wii exposes the
// DI at 0xCD006000 and EXI at 0xCD006800 (vs the GameCube 0xCC006000/0xCC006800
// that GXRuntime models); we translate the Wii addresses onto the GXRuntime
// device models and mark the disc/drive present.

void dvd_init(void);
bool dvd_mmio_contains(u32 ea);
u64  dvd_mmio_read(u32 ea, u8 size);
void dvd_mmio_write(CPUState* cpu, u32 ea, u8 size, u64 value);

// Low-level DI/EXI MMIO access counts (diagnostics).
void dvd_mmio_stats(u32* reads, u32* writes);

#endif

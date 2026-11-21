#ifndef COLORSRECOMP_MMIO_H
#define COLORSRECOMP_MMIO_H

#include "core/cpu.h"
#include "core/types.h"

// Host bus: routes external (non-MEM1) guest accesses to the device models and
// provides the instruction fallback for cache maintenance / unmodeled SPRs.
//
// Dispatch order matters. PI/GX FIFO registers must be routed *before* the
// GXRuntime interrupt model, which claims the whole 0xCC003000 block and would
// answer 0xCC003014 with zero, shadowing the FIFO model (that shadowing was the
// cause of the multi-round runaway memcpy).

// Set the environment SPR defaults (PVR).
void mmio_init(void);

// Install the bus callbacks on the CPU.
void mmio_attach(CPUState* cpu);

#endif

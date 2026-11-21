#ifndef COLORSRECOMP_AI_H
#define COLORSRECOMP_AI_H

#include "core/cpu.h"
#include "core/types.h"

#include <stdbool.h>

// Audio Interface (AI) registers at 0xCD006C00 (Wii AI block).
//
// The SDK's audio driver starts streaming by setting AI_CONTROL bit 0 and then
// waits for the sample counter (AI_SAMPLE_COUNT, 0x08) to advance. Without a
// model of that counter the driver spins forever. The counter is derived from
// the timebase at the streaming rate (48 kHz / 32 kHz from AI_CONTROL).
#define AI_BASE 0xCD006C00u
#define AI_SIZE 0x20u

bool ai_mmio_contains(u32 addr);
u64  ai_mmio_read(CPUState* cpu, u32 off, u8 size);
void ai_mmio_write(CPUState* cpu, u32 off, u8 size, u64 value);

#endif

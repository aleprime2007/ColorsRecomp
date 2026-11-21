#ifndef COLORSRECOMP_BOOT_ENV_H
#define COLORSRECOMP_BOOT_ENV_H

#include "core/cpu.h"
#include "gxruntime/loader.h"

// Reconstruct the console state the apploader/IPL leaves behind before jumping
// to the DOL entry point.
//
// This host loads only the DOL, so the pieces the real boot chain performs have
// to be emulated here:
//   - OS low-memory globals (memory size, clocks, console type, arena bounds)
//   - the 32-byte disc boot header (disc ID) at 0x80000000
//   - the apploader's FST load into the top of MEM1 and its 0x80000038/3C
//     publication (which also shrinks ArenaHi)
//   - the PI/GX FIFO register window GXInit would program
void boot_env_setup(CPUState* cpu, const DolLayout* layout);

#endif

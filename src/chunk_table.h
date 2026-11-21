#ifndef COLORSRECOMP_CHUNK_TABLE_H
#define COLORSRECOMP_CHUNK_TABLE_H

#include "core/cpu.h"
#include "core/types.h"

#include <stdbool.h>

// O(1) dispatch into the recompiled chunk functions.
//
// DolRecomp emits `dolrecomp_call()` as a linear chain of ~450 range tests.
// The chunks are contiguous 0x4000-byte windows, so we generate a
// direct-mapped table (tools/gen_chunk_table.py) and index it instead.
//
// `chunk_dispatch` behaves like `dolrecomp_call`: it runs the chunk containing
// `pc` (the chunk's internal switch selects the exact instruction) and returns
// false when no recompiled code covers the address.

bool chunk_contains(u32 pc);
bool chunk_dispatch(CPUState* cpu, u32 pc);

#endif

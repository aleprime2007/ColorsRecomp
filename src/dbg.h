#ifndef COLORSRECOMP_DBG_H
#define COLORSRECOMP_DBG_H

#include "core/cpu.h"
#include "core/types.h"

#include <stdbool.h>

// Host-side diagnostics for the recompiled guest: PC history, exception names,
// guest backchain walks, the per-block watch hooks used while chasing boot
// bugs, and the end-of-run state report.

// Read the COLORS_* environment flags.
void dbg_init(void);

// Record a dispatched pc in the ring buffer (oldest first when printed).
void dbg_pc_push(u32 pc);

// Per-block watch hooks, called after the pc translation and the history push,
// before the block is dispatched. `last_pc` is the previous block's entry pc.
void dbg_block_enter(CPUState* cpu, u32 pc, unsigned long long blocks, u32 last_pc);

// Called immediately after a block returns from the dispatcher (before the
// inter-block timing/interrupt work), so a write can be attributed either to
// the guest block or to the host-side inter-block work.
void dbg_post_block(CPUState* cpu, u32 pc);

// Print the ring buffer (no trailing newline).
void dbg_print_pc_history(void);

void dbg_dump_backchain(CPUState* cpu);

const char* dbg_exception_name(u32 exc);

// Print the final `[run]`/`[dbg]` state dump.
void dbg_report_final(CPUState* cpu, unsigned long long blocks, const char* stop_reason);

// Install the memory-write journal (COLORS_WATCH_WRITE=lo:hi).
void dbg_install_write_watch(CPUState* cpu);

#endif

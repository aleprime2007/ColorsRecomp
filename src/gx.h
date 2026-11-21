#ifndef COLORSRECOMP_GX_H
#define COLORSRECOMP_GX_H

#include "core/cpu.h"
#include "core/types.h"
#include "gxruntime/guest_memory.h"

#include <stdbool.h>

// GX command FIFO (0xCC008000, the GP write-gather pipe).
//
// The host feeds every FIFO write into GXRuntime's retail GX frontend (via
// the gx_frontend bridge, src/gx_frontend.h), which decodes the real
// command stream: CP array bases/strides, VCD/VAT vertex layouts, XF loads
// (matrices, viewport, projection, lights), BP registers, texture/TLUT
// resolves, display-list calls and draw calls. It emits packets; we tally
// them, which both validates the render path and is the seam a rasteriser
// plugs into later.

// Prepare the decoder (call once, after the CPU exists).
void gx_init(CPUState* cpu);

bool gx_fifo_contains(u32 addr);
u64  gx_fifo_read(u32 addr);
void gx_fifo_write(u32 addr, u64 value, u8 size);

// Drain the staged bytes through the decoder.
void gx_flush(void);

// Stats for the end-of-run report.
typedef struct {
    u32 fifo_writes;
    u32 draws;
    u32 state_packets;
    u32 resource_packets;
    u32 trace_events;
    u32 flush_failures;
    u32 pending_bytes;
    u32 resyncs;
    u32 skip_resyncs;
} GxStats;

void gx_get_stats(GxStats* out);

// Flush the software rasteriser (writes COLORS_FB_DUMP if set).
void gx_finalize(void);

// Resolve a guest address (CP array, texture, ...) to host memory.
bool gx_resolve_range(u32 address, u32 size, u32 resource,
                      DolGuestResolvedRange* out);

#endif

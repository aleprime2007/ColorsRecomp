#ifndef COLORSRECOMP_GX_FRONTEND_H
#define COLORSRECOMP_GX_FRONTEND_H

#include "core/types.h"
#include "gxruntime/aurora_recomp/retail_gx_frontend_c.h"

#include <stdbool.h>
#include <stddef.h>

// Host-owned bridge onto GXRuntime's retail GX frontend.
//
// GXRuntime ships a C API (retail_gx_frontend_c.h) but its flush path leaves
// packet drain mode disabled, which is only correct for offline FIFO replay:
// a live sink then re-walks the retained trace ring on every flush and the
// decoder eventually rejects the stream. The C++ frontend exposes
// set_packet_drain_enabled() for exactly this reason, and the decoder's
// last-error accessors live on the C++ class too.
//
// GXRuntime is consumed unmodified, so this file wraps the C++ frontend in the
// small C surface the host needs. Packets are handed back as the same plain
// data structs GXRuntime's C API uses, so the decoder call sites stay simple.
// (The per-resource width/height/TLUT and EFB-copy source rect that the C++
// sink struct carries are recovered from the packet's trace event — see
// raster_packet_resource.)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ColorsGxFrontend ColorsGxFrontend;

typedef bool (*ColorsGxPacketFn)(void* user,
                                 const DolAuroraRecompRenderPacket* packet);

ColorsGxFrontend* colors_gx_frontend_create(
    const DolGuestAddressResolver* resolver);
void colors_gx_frontend_destroy(ColorsGxFrontend* frontend);
void colors_gx_frontend_reset(ColorsGxFrontend* frontend,
                              const DolGuestAddressResolver* resolver);

bool colors_gx_frontend_set_vertex_layout(ColorsGxFrontend* frontend,
                                          u8 vtx_fmt, u32 vertex_size);
bool colors_gx_frontend_derive_vertex_layout(ColorsGxFrontend* frontend,
                                             u8 vtx_fmt);
bool colors_gx_frontend_set_cp_array(ColorsGxFrontend* frontend, u8 attr,
                                     u32 physical_base, u8 stride);
bool colors_gx_frontend_load_cp_reg(ColorsGxFrontend* frontend, u8 reg,
                                    u32 value);

bool colors_gx_frontend_write_fifo(ColorsGxFrontend* frontend,
                                   const void* bytes, size_t size);
// Decode the staged stream. On failure the offending bytes stay pending and the
// decoder's error is available from the accessors below.
bool colors_gx_frontend_flush(ColorsGxFrontend* frontend,
                              ColorsGxPacketFn submit, void* user);

size_t colors_gx_frontend_pending_fifo_size(const ColorsGxFrontend* frontend);
const DolGxRecompState* colors_gx_frontend_state(
    const ColorsGxFrontend* frontend);

const char* colors_gx_frontend_last_error(const ColorsGxFrontend* frontend);
size_t colors_gx_frontend_last_error_offset(const ColorsGxFrontend* frontend);
u32 colors_gx_frontend_last_error_opcode(const ColorsGxFrontend* frontend);

// Number of successful desync recoveries (a stream that failed to decode was
// resumed from a later byte offset). Diagnostic only.
u64 colors_gx_frontend_resync_count(const ColorsGxFrontend* frontend);

#ifdef __cplusplus
}
#endif

#endif /* COLORSRECOMP_GX_FRONTEND_H */

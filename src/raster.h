#ifndef COLORSRECOMP_RASTER_H
#define COLORSRECOMP_RASTER_H

#include "core/types.h"
#include "gxruntime/aurora_recomp/retail_gx_frontend_c.h"

#include <stdbool.h>

// Minimal host-side software rasteriser for decoded GX draw packets.
//
// The GX frontend hands us fully decoded draw calls (raw per-vertex bytes,
// position matrix, projection/viewport) plus the CP/XF/BP state and resolved
// textures. This module keeps a small mirror of the state a GX pixel pipe
// needs, decodes the vertices, transforms them with the guest's own matrices
// and rasterises triangles into an EFB-sized RGBA8 buffer. It is deliberately
// small: enough to turn the decoded stream into visible frames (vertex colour,
// one texture stage, depth test) while the full TEV/lighting path stays with
// the GPU backend.

typedef struct {
    u32 draws;
    u32 triangles;
    u32 pixels;
    u32 textured;
    u32 clears;
    u32 dumps;
} RasterStats;

void raster_init(void);
void raster_shutdown(void);

void raster_packet_state(const DolAuroraRecompRenderStatePacket* state);
void raster_packet_resource(const DolAuroraRecompRenderPacket* packet);
void raster_packet_draw(const DolAuroraRecompRenderDrawPacket* draw);

// Write the EFB as a binary PPM (P6). Returns false on I/O failure.
bool raster_dump_ppm(const char* path);

void raster_get_stats(RasterStats* out);

#endif

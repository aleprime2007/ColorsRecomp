#include "gx.h"

#include "gx_frontend.h"
#include "raster.h"
#include "window.h"

#include "gxruntime/aurora_recomp/retail_gx_frontend_c.h"
#include "gxruntime/guest_memory.h"
#include "mem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WII_GX_FIFO_BASE 0xCC008000u
#define WII_GX_FIFO_END 0xCC008800u

// Batch FIFO bytes before decoding. The frontend keeps partial commands in its
// own pending buffer, so this only controls how often we hand bytes over.
#define GX_STAGE_BYTES 256u

static ColorsGxFrontend* g_frontend;
static DolGuestAddressResolver g_resolver;
static CPUState* g_cpu;
static int g_decode;   // COLORS_GX_DECODE=1 enables the full decoder
static FILE* g_dump;   // COLORS_GX_DUMP=<path> records the raw FIFO stream

static u8 g_stage[GX_STAGE_BYTES];
static u32 g_stage_len;

// Shadow of the bytes the frontend has not consumed yet (capped). Used to
// report the command the decoder stalls on.
#define GX_SHADOW_BYTES 4096u
static u8 g_shadow[GX_SHADOW_BYTES];
static u32 g_shadow_len;
static bool g_shadow_reported;

static GxStats g_stats;

static u32 g_draws_logged;

// Guest address -> host pointer for CP arrays, textures and TLUTs.
static bool gx_resolve(void* user, u32 address, u32 size,
                       DolGuestAddressSpace space,
                       DolGuestResourceKind resource,
                       DolGuestResolvedRange* out) {
    (void)user;
    (void)space;
    (void)resource;
    if (out == NULL || g_cpu == NULL || size == 0u)
        return false;

    u8* data = NULL;
    u32 available = 0;

    if (address >= 0x80000000u && address < 0x80000000u + g_cpu->ram_size) {
        const u32 off = address - 0x80000000u;
        data = g_cpu->ram + off;
        available = g_cpu->ram_size - off;
    } else if (address >= 0xC0000000u && address < 0xC0000000u + g_cpu->ram_size) {
        const u32 off = address - 0xC0000000u;
        data = g_cpu->ram + off;
        available = g_cpu->ram_size - off;
    } else if (address < g_cpu->ram_size) {  // MEM1 physical
        data = g_cpu->ram + address;
        available = g_cpu->ram_size - address;
    } else if (address >= 0x10000000u && address < 0x10000000u + WII_MEM2_SIZE) {
        // MEM2 physical: GX addresses (EFB copies, textures, CP arrays) are
        // physical, and the game places MEM2 buffers at 0x10000000 + offset.
        const u32 off = address - 0x10000000u;
        data = mem2_host_pointer(off);
        available = WII_MEM2_SIZE - off;
    } else if (address >= WII_MEM2_BASE && address < WII_MEM2_BASE + WII_MEM2_SIZE) {
        const u32 off = address - WII_MEM2_BASE;
        data = mem2_host_pointer(off);
        available = WII_MEM2_SIZE - off;
    } else if (address >= WII_MEM2_UNCACHED &&
               address < WII_MEM2_UNCACHED + WII_MEM2_SIZE) {
        const u32 off = address - WII_MEM2_UNCACHED;
        data = mem2_host_pointer(off);
        available = WII_MEM2_SIZE - off;
    }
    if (data == NULL)
        return false;

    memset(out, 0, sizeof(*out));
    out->data = data;
    out->available = available;
    return true;
}

static bool gx_packet(void* user, const DolAuroraRecompRenderPacket* packet) {
    (void)user;
    if (packet == NULL)
        return true;
    switch (packet->kind) {
    case DOL_AURORA_RECOMP_PACKET_DRAW:
        g_stats.draws++;
        if (getenv("COLORS_MAT_DEBUG") != NULL && packet->draw.vertex_size == 3u) {
            static int n = 0;
            if (n++ < 3) {
                const DolGxRecompState* st =
                    colors_gx_frontend_state(g_frontend);
                fprintf(stderr,
                        "[mat] draw mi=%u mask=0x%X | state mask=0x%X m0=(%.3f,%.3f,%.3f) xf_mask=0x%llX\n",
                        packet->draw.current_pn_matrix,
                        packet->draw.position_matrix_valid_mask,
                        st != NULL ? st->position_matrix_word_mask[0] : 0u,
                        st != NULL ? st->position_matrices[0][0] : 0.f,
                        st != NULL ? st->position_matrices[0][1] : 0.f,
                        st != NULL ? st->position_matrices[0][2] : 0.f,
                        st != NULL ? (unsigned long long)st->xf_reg_mask : 0ull);
                if (st != NULL)
                    fprintf(stderr,
                            "[mat]   matrix_index_a=0x%08X pn_row=%u nz=%u "
                            "last_xf=0x%X/%u xf_window0=(%08X %08X %08X)\n",
                            st->matrix_index_a, st->matrix_index_a & 0x3Fu,
                            st->position_matrices[0][0] != 0.f,
                            st->last_xf_base, st->last_xf_count,
                            st->xf_regs[0], st->xf_regs[1], st->xf_regs[2]);
            }
        }
        if (getenv("COLORS_MAT_TRACE") != NULL && (g_stats.draws % 3000u) == 0u) {
            const DolGxRecompState* st =
                colors_gx_frontend_state(g_frontend);
            if (st != NULL)
                fprintf(stderr,
                        "[mattrace] draw=%u m0=(%.3f,%.3f,%.3f,%.3f) "
                        "mask=%X ptr=%llu\n",
                        g_stats.draws, st->position_matrices[0][0],
                        st->position_matrices[0][1], st->position_matrices[0][2],
                        st->position_matrices[0][3],
                        st->position_matrix_word_mask[0],
                        (unsigned long long)0);
        }
        raster_packet_draw(&packet->draw);
        if (g_draws_logged < 4u) {
            g_draws_logged++;
            fprintf(stderr,
                    "[gx] draw #%u primitive=%u vtx_fmt=%u verts=%u size=%u "
                    "pn_matrix=%u xform=0x%X\n",
                    g_stats.draws, packet->draw.primitive, packet->draw.vtx_fmt,
                    packet->draw.vertex_count, packet->draw.vertex_size,
                    packet->draw.current_pn_matrix, packet->draw.transform_flags);
        }
        break;
    case DOL_AURORA_RECOMP_PACKET_STATE:
        g_stats.state_packets++;
        raster_packet_state(&packet->state);
        break;
    case DOL_AURORA_RECOMP_PACKET_RESOURCE:
        g_stats.resource_packets++;
        raster_packet_resource(packet);
        break;
    case DOL_AURORA_RECOMP_PACKET_TRACE_EVENT:
        g_stats.trace_events++;
        break;
    default:
        break;
    }
    return true;
}

void gx_init(CPUState* cpu) {
    g_cpu = cpu;
    g_decode = getenv("COLORS_GX_DECODE") != NULL || window_active();
    {
        const char* dump_path = getenv("COLORS_GX_DUMP");
        if (dump_path != NULL && dump_path[0] != '\0')
            g_dump = fopen(dump_path, "wb");
    }
    memset(&g_stats, 0, sizeof(g_stats));
    g_stage_len = 0;
    g_draws_logged = 0;
    if (g_decode)
        raster_init();

    dol_guest_address_resolver_init_callback(&g_resolver, gx_resolve, NULL);
    if (g_frontend == NULL) {
        g_frontend = colors_gx_frontend_create(&g_resolver);
        if (g_frontend == NULL)
            fprintf(stderr, "[gx] warning: frontend create failed\n");
    } else {
        colors_gx_frontend_reset(g_frontend, &g_resolver);
    }
}

bool gx_fifo_contains(u32 addr) {
    return addr >= WII_GX_FIFO_BASE && addr < WII_GX_FIFO_END;
}

u64 gx_fifo_read(u32 addr) {
    (void)addr;
    return 0;  // the write-gather pipe is write-only
}

// Report the decoder's first hard failure once (state dump + stall bytes).
static void gx_report_stall(void) {
    if (g_shadow_reported)
        return;
    if (g_cpu != NULL) {
        const u32 gxdata = mem_read32(g_cpu, g_cpu->gpr[2] - 888u);
        if (gxdata >= 0x80000000u && gxdata < 0x94000000u) {
            fprintf(stderr, "[gx] __GXData=0x%08X r2=0x%08X\n", gxdata,
                    g_cpu->gpr[2]);
            for (u32 off = 0; off < 0x120u; off += 4u)
                fprintf(stderr, "[gx]   +0x%03X: %08X\n", off,
                        mem_read32(g_cpu, gxdata + off));
        }
    }
    const DolGxRecompState* st = colors_gx_frontend_state(g_frontend);
    const char* err = colors_gx_frontend_last_error(g_frontend);
    fprintf(stderr,
            "[gx] decode stall: %s (opcode 0x%02X offset %zu) pending=%u "
            "vcd_lo=0x%X vcd_hi=0x%X vat0=0x%X layout0=%u fifo=%u\n",
            err != NULL ? err : "(none)",
            colors_gx_frontend_last_error_opcode(g_frontend),
            colors_gx_frontend_last_error_offset(g_frontend),
            g_stats.pending_bytes,
            st != NULL ? st->vcd_lo : 0u, st != NULL ? st->vcd_hi : 0u,
            st != NULL ? st->vat_group0[0] : 0u,
            st != NULL ? st->vertex_layouts[0].vertex_size : 0u,
            st != NULL ? st->fifo.size : 0u);
    if (g_shadow_len >= 8u) {
        g_shadow_reported = true;
        fprintf(stderr, "[gx] first stall: pending=%u first bytes:",
                g_stats.pending_bytes);
        for (u32 i = 0; i < 32u && i < g_shadow_len; i++)
            fprintf(stderr, " %02X", g_shadow[i]);
        fprintf(stderr, "\n");
    }
}

void gx_flush(void) {
    if (!g_decode || g_frontend == NULL || g_stage_len == 0u)
        return;
    const bool wrote = colors_gx_frontend_write_fifo(g_frontend, g_stage,
                                                     g_stage_len);
    g_stage_len = 0;
    if (!wrote) {
        g_stats.flush_failures++;
        return;
    }

    if (colors_gx_frontend_flush(g_frontend, gx_packet, NULL)) {
        g_stats.pending_bytes = 0;
        g_stats.skip_resyncs = (u32)colors_gx_frontend_resync_count(g_frontend);
        g_shadow_len = 0;
        return;
    }

    // The bridge already rescans a failed stream byte-by-byte (see
    // gx_frontend.cpp), so reaching this point means even that could not find a
    // parseable continuation. Drop the unconsumed bytes and carry on: the
    // decoder keeps all of its register state, so rendering resumes once the
    // guest resubmits a display list.
    g_stats.flush_failures++;
    g_stats.pending_bytes =
        (u32)colors_gx_frontend_pending_fifo_size(g_frontend);
    gx_report_stall();
    g_stats.resyncs++;
    if (g_stats.resyncs <= 4u)
        fprintf(stderr, "[gx] stream unrecoverable, dropping %u pending byte(s)\n",
                g_stats.pending_bytes);
    // The bridge already discarded the bytes while preserving the decoder's
    // register state, so the next command decodes normally.
    g_stats.pending_bytes = 0;
    g_shadow_reported = false;
}

void gx_fifo_write(u32 addr, u64 value, u8 size) {
    (void)addr;
    g_stats.fifo_writes++;
    if (g_dump != NULL) {
        u8 raw[8];
        for (u32 i = 0; i < size; i++)
            raw[i] = (u8)(value >> (8u * (u32)(size - 1u - i)));
        fwrite(raw, 1, size, g_dump);
    }
    if (!g_decode)
        return;
    // The FIFO is big-endian: store the bytes in memory order. Flush before
    // storing when the stage is full so a write that straddles the boundary is
    // not truncated.
    for (u32 i = 0; i < size; i++) {
        if (g_stage_len == GX_STAGE_BYTES)
            gx_flush();
        const u8 byte = (u8)(value >> (8u * (u32)(size - 1u - i)));
        g_stage[g_stage_len++] = byte;
        if (g_shadow_len < GX_SHADOW_BYTES)
            g_shadow[g_shadow_len++] = byte;
    }
}

bool gx_resolve_range(u32 address, u32 size, u32 resource,
                      DolGuestResolvedRange* out) {
    return dol_guest_address_resolver_resolve(
        &g_resolver, address, size, DOL_GUEST_ADDRESS_AUTO,
        (DolGuestResourceKind)resource, out);
}

void gx_finalize(void) {
    RasterStats rs;
    if (!g_decode)
        return;
    raster_get_stats(&rs);
    fprintf(stderr,
            "[gx] raster: draws=%u tris=%u pixels=%u textured=%u clears=%u "
            "dumps=%u\n",
            rs.draws, rs.triangles, rs.pixels, rs.textured, rs.clears,
            rs.dumps);
    const char* path = getenv("COLORS_FB_DUMP");
    if (path != NULL && path[0] != '\0' && rs.dumps == 0u) {
        if (raster_dump_ppm(path))
            fprintf(stderr, "[gx] framebuffer written to %s\n", path);
        else
            fprintf(stderr, "[gx] framebuffer dump failed (%s)\n", path);
    }
    raster_shutdown();
}

void gx_get_stats(GxStats* out) {
    if (out != NULL)
        *out = g_stats;
}

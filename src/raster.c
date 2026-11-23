#include "raster.h"

#include "window.h"

#include "gx.h"

#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GX texture decode lives in gxcore (C++); these wrappers are in texdecode.cpp.
extern bool colors_decode_texture(u32 format, u32 width, u32 height,
                                  const u8* data, size_t size, u8* out,
                                  size_t out_size);
extern bool colors_decode_ci(u32 format, u32 width, u32 height, const u8* data,
                             size_t size, u32 tlut_format, u32 tlut_entries,
                             const u8* tlut_data, size_t tlut_size, u8* out,
                             size_t out_size);

// The guest renders to a 640x480 EFB here (the game sets that scissor; the
// framebuffer is allocated at the EFB's native size).
#define EFB_W 640u
#define EFB_H 480u

// ---------------------------------------------------------------------------
// State mirror (populated from the frontend's packets)
// ---------------------------------------------------------------------------

static u32 g_vcd_lo, g_vcd_hi;
static bool g_vcd_lo_valid, g_vcd_hi_valid;
static u32 g_vat[DOL_GX_RECOMP_VERTEX_FORMATS][3];
static u32 g_bp[256];
static bool g_bp_valid[256];
static u32 g_arr_base[16];
static u32 g_arr_stride[16];
static bool g_arr_base_valid[16];

typedef struct {
    bool valid;
    u32 address;
    u32 width, height, format;
    const u8* data;
    u32 data_size;
    u32 tlut_format, tlut_entries;
    const u8* tlut;
    u32 tlut_size;
    u8* rgba;  // decoded image, width*height*4
} Tex;

static Tex g_tex[DOL_GX_RECOMP_TEXTURE_SLOTS];

// Pixel pipeline: GX TEV combiner stages + ROP (blend / depth / scissor), all
// driven from BP registers. Enough of the real pipeline to reproduce retail
// multi-pass compositing (modulate, add, blend) instead of a flat modulate.
#define TEV_MAX_STAGES 16u

typedef struct {
    bool tex_enabled;
    u32 tex_map;
    u32 c_a, c_b, c_c, c_d;   // GXTevColorArg
    u32 a_a, a_b, a_c, a_d;   // GXTevAlphaArg
    u32 c_bias, c_op, c_scale;
    u32 a_bias, a_op, a_scale;
    u32 kc_sel, ka_sel;
} TevStage;

static TevStage g_tev[TEV_MAX_STAGES];
static u32 g_num_tev_stages = 1u;
static f32 g_kcolor[4][4];
static f32 g_tevreg[4][4];
static bool g_tev_valid;
static bool g_color_update = true;
static bool g_alpha_update = true;

// ROP: blend factor state from BP 0x41.
static u32 g_blend_dst = 1u, g_blend_src = 1u;
static bool g_blend_enable, g_logic_enable, g_blend_subtract;
static u32 g_depth_func = 3u;  // GX_LEQUAL
static bool g_depth_test = true, g_depth_update = true;

// Scissor rectangle in EFB coordinates (BP 0x20/0x21, +342 biased).
static bool g_scissor_valid;
static u32 g_scissor_x0, g_scissor_y0, g_scissor_x1, g_scissor_y1;

static void raster_bp(u32 reg, u32 value);

static u8 g_color[EFB_W * EFB_H * 4u];
static f32 g_depth[EFB_W * EFB_H];
static f32 g_clear[4] = {0.f, 0.f, 0.f, 1.f};

// EFB copies produce guest textures (mip chains, render-to-texture). Rather
// than re-encode the tiled GX memory layout, keep the copied images as RGBA8
// keyed by destination address and serve them when the game binds that
// address as a texture. That is exactly the data the GX would have sampled.
#define COPY_CACHE_SLOTS 24u

typedef struct {
    bool valid;
    u32 address;
    u32 width, height;
    u8* rgba;
} CopyImage;

static CopyImage g_copies[COPY_CACHE_SLOTS];
static u32 g_copy_next;

typedef struct {
    u32 fmt, prim, n, vs;
    f32 x0, y0, x1, y1;
    f32 col[4];
    u32 tex;
    u32 blend, colupd;
} DrawRec;

#define DRAW_RING 160u
static DrawRec g_draw_ring[DRAW_RING];
static u32 g_draw_ring_n;

static RasterStats g_stats;
static int g_ready;

// Hot-path switches cached once: getenv() must never sit inside the pixel loop.
static bool g_opt_no_depth;
static bool g_opt_no_blend;
static bool g_opt_white_tex;
static bool g_opt_tev_ras;
static bool g_opt_tev_force;
static bool g_opt_force_tex_pass;
static bool g_opt_tex_magenta;
static bool g_opt_uv_pixels;
static unsigned long long g_path_px[8];  // tex/ras/mod/generic/notev/lerp/texd/rasd
static bool g_opt_path_stats;
static unsigned long long g_combo_px[65536];
static unsigned long long g_stage_px[9];

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static u32 bits(u32 v, u32 shift, u32 size) {
    return (v >> shift) & ((1u << size) - 1u);
}

static f32 read_f32(const u8* p) {
    u32 v = read_be32(p);
    f32 f;
    memcpy(&f, &v, sizeof(f));
    return f;
}

static u32 comp_size(u32 format) {
    switch (format & 7u) {
    case 0: case 1: return 1u;  // u8, s8
    case 2: case 3: return 2u;  // u16, s16
    default: return 4u;         // f32
    }
}

static u32 color_size(u32 format) {
    switch (format & 7u) {
    case 0: return 2u;  // RGB565
    case 1: return 3u;  // RGB888
    case 2: return 4u;  // RGB888x
    case 3: return 2u;  // RGBA4
    case 4: return 3u;  // RGBA6
    default: return 4u; // RGBA8
    }
}

static f32 read_comp(const u8* p, u32 format) {
    switch (format & 7u) {
    case 0: return (f32)p[0];
    case 1: return (f32)(s8)p[0];
    case 2: return (f32)read_be16(p);
    case 3: return (f32)(s16)read_be16(p);
    default: return read_f32(p);
    }
}

static void read_color(const u8* p, u32 format, f32 out[4]) {
    switch (format & 7u) {
    case 0: {  // RGB565
        const u32 v = read_be16(p);
        out[0] = (f32)bits(v, 11, 5) / 31.f;
        out[1] = (f32)bits(v, 5, 6) / 63.f;
        out[2] = (f32)bits(v, 0, 5) / 31.f;
        out[3] = 1.f;
        break;
    }
    case 1:  // RGB888
    case 2:  // RGB888x
        out[0] = (f32)p[0] / 255.f;
        out[1] = (f32)p[1] / 255.f;
        out[2] = (f32)p[2] / 255.f;
        out[3] = 1.f;
        break;
    case 3: {  // RGBA4
        const u32 v = read_be16(p);
        out[0] = (f32)bits(v, 12, 4) / 15.f;
        out[1] = (f32)bits(v, 8, 4) / 15.f;
        out[2] = (f32)bits(v, 4, 4) / 15.f;
        out[3] = (f32)bits(v, 0, 4) / 15.f;
        break;
    }
    case 4: {  // RGBA6
        const u32 v = ((u32)p[0] << 16) | ((u32)p[1] << 8) | (u32)p[2];
        out[0] = (f32)bits(v, 18, 6) / 63.f;
        out[1] = (f32)bits(v, 12, 6) / 63.f;
        out[2] = (f32)bits(v, 6, 6) / 63.f;
        out[3] = (f32)bits(v, 0, 6) / 63.f;
        break;
    }
    default:  // RGBA8
        out[0] = (f32)p[0] / 255.f;
        out[1] = (f32)p[1] / 255.f;
        out[2] = (f32)p[2] / 255.f;
        out[3] = (f32)p[3] / 255.f;
        break;
    }
}

// ---------------------------------------------------------------------------
// Vertex layout
// ---------------------------------------------------------------------------

typedef struct {
    bool present;
    bool direct;
    u32 off;         // byte offset inside the vertex
    u32 index_size;  // 0 when direct, else 1/2
    u32 elems;       // position/tex components
    u32 fmt;         // component format (positions, normals, texcoords)
    u32 cformat;     // colour format (colours)
    u32 frac;        // texcoord fractional bits
    u32 esize;       // element size for indexed attributes
    u8 id;           // CP array id for indexed attributes
} Attr;

typedef struct {
    bool valid;
    u32 size;
    Attr pos, nrm, col0, col1;
    Attr tex[DOL_GX_RECOMP_TEXTURE_SLOTS];
} Layout;

static u32 popcount9(u32 v) {
    u32 n = 0;
    for (u32 i = 0; i < 9u; i++)
        n += (v >> i) & 1u;
    return n;
}

static void derive_attr_direct(Attr* a, u32 type, u32 elems, u32 fmt,
                               u32 esize, u32* off) {
    memset(a, 0, sizeof(*a));
    if (type == 0u)
        return;
    a->present = true;
    a->elems = elems;
    a->fmt = fmt;
    a->esize = esize;
    a->off = *off;
    if (type == 1u) {
        a->direct = true;
        *off += esize;
    } else {
        a->index_size = (type == 2u) ? 1u : 2u;
        *off += a->index_size;
    }
}

// Fallback layout for draws whose declared VCD/VAT does not describe the
// payload (retail titles mix formats inside one vertex-format index). Only the
// shapes this title actually emits are covered; the stride must match exactly.
static bool layout_from_size(u32 vertex_size, Layout* out) {
    memset(out, 0, sizeof(*out));
    Attr pos;
    memset(&pos, 0, sizeof(pos));
    pos.present = true;
    pos.direct = true;
    pos.off = 0;
    pos.elems = 3;
    pos.fmt = 4;   // f32
    pos.esize = 12;
    out->pos = pos;

    Attr col;
    memset(&col, 0, sizeof(col));
    Attr uv;
    memset(&uv, 0, sizeof(uv));

    if (vertex_size == 12u) {
        out->size = 12u;
        out->valid = true;
        return true;
    }
    if (vertex_size == 20u) {  // pos f32x3 + uv f32x2
        uv.present = true;
        uv.direct = true;
        uv.off = 12u;
        uv.elems = 2u;
        uv.fmt = 4u;
        uv.esize = 8u;
        out->tex[0] = uv;
        out->size = 20u;
        out->valid = true;
        return true;
    }
    if (vertex_size == 16u) {  // pos f32x3 + RGBA8
        col.present = true;
        col.direct = true;
        col.off = 12u;
        col.elems = 4u;
        col.cformat = 5u;  // RGBA8
        col.esize = 4u;
        out->col0 = col;
        out->size = 16u;
        out->valid = true;
        return true;
    }
    if (vertex_size == 24u) {  // pos f32x3 + RGBA8 + uv f32x2
        col.present = true;
        col.direct = true;
        col.off = 12u;
        col.elems = 4u;
        col.cformat = 5u;
        col.esize = 4u;
        out->col0 = col;
        uv.present = true;
        uv.direct = true;
        uv.off = 16u;
        uv.elems = 2u;
        uv.fmt = 4u;
        uv.esize = 8u;
        out->tex[0] = uv;
        out->size = 24u;
        out->valid = true;
        return true;
    }
    return false;
}

static void derive_layout(u32 fmt, bool force_color0, Layout* out) {
    memset(out, 0, sizeof(*out));
    if (!g_vcd_lo_valid && !g_vcd_hi_valid)
        return;

    u32 off = popcount9(g_vcd_lo);
    const u32 v0 = g_vat[fmt][0];
    const u32 v1 = g_vat[fmt][1];
    const u32 v2 = g_vat[fmt][2];

    // Position: VAT0[0]=XYZ, VAT0[1..3]=component format.
    derive_attr_direct(&out->pos, bits(g_vcd_lo, 9, 2),
                       bits(v0, 0, 1) ? 3u : 2u, bits(v0, 1, 3),
                       (bits(v0, 0, 1) ? 3u : 2u) * comp_size(bits(v0, 1, 3)),
                       &off);
    // Normal: always 3 components (NTB or XYZ).
    derive_attr_direct(&out->nrm, bits(g_vcd_lo, 11, 2), 3u,
                       bits(v0, 10, 3), 3u * comp_size(bits(v0, 10, 3)),
                       &off);
    // Colours: VAT0[13]=RGBA, VAT0[14..16]=format. Some retail streams write
    // a colour the VCD never enables; the caller retries with it forced on
    // when the draw's real stride is wider than the declared layout.
    derive_attr_direct(&out->col0,
                       force_color0 ? 1u : bits(g_vcd_lo, 13, 2),
                       bits(v0, 13, 1) ? 4u : 3u, bits(v0, 14, 3),
                       color_size(bits(v0, 14, 3)), &off);
    derive_attr_direct(&out->col1, bits(g_vcd_lo, 15, 2),
                       bits(v0, 17, 1) ? 4u : 3u, bits(v0, 18, 3),
                       color_size(bits(v0, 18, 3)), &off);

    // Texcoords 0..7 live in VAT groups 0/1/2; see Dolphin UVAT_group*.
    static const struct {
        u32 elem_bit, fmt_bit, frac_bit, group;
    } tv[8] = {
        {21u, 22u, 25u, 0u}, {0u, 1u, 4u, 1u}, {9u, 10u, 13u, 1u},
        {18u, 19u, 22u, 1u}, {27u, 28u, 0u, 1u}, {5u, 6u, 9u, 2u},
        {14u, 15u, 18u, 2u}, {24u, 25u, 27u, 2u},
    };
    // tex4's fractional bits spill into VAT group 2, as does group bookkeeping;
    // keep the split explicit rather than indexing the array above blindly.
    for (u32 i = 0; i < 8u; i++) {
        const u32 group = tv[i].group;
        const u32 word = group == 0u ? v0 : (group == 1u ? v1 : v2);
        const u32 elems = bits(word, tv[i].elem_bit, 1u) ? 2u : 1u;
        const u32 cfmt = bits(word, tv[i].fmt_bit, 3u);
        Attr a;
        memset(&a, 0, sizeof(a));
        const u32 type = bits(g_vcd_hi, i * 2u, 2u);
        if (type == 0u) {
            out->tex[i] = a;
            continue;
        }
        a.present = true;
        a.elems = elems;
        a.fmt = cfmt;
        // Texcoord 4 keeps its element/format bits in group 1 but its
        // fractional-bit count in group 2 (Dolphin UVAT_group2.Tex4Frac).
        a.frac = (i == 4u) ? bits(v2, 0, 5u)
                           : bits(word, tv[i].frac_bit, 5u);
        a.esize = elems * comp_size(cfmt);
        a.off = off;
        if (type == 1u) {
            a.direct = true;
            off += a.esize;
        } else {
            a.index_size = (type == 2u) ? 1u : 2u;
            off += a.index_size;
        }
        out->tex[i] = a;
    }

    out->valid = true;
    out->size = off;
}

// ---------------------------------------------------------------------------
// Vertex payload reads
// ---------------------------------------------------------------------------

// Pointer to this attribute's bytes for one vertex. Indexed arrays resolve
// through the guest memory map (same resolver the frontend uses).
static const u8* attr_ptr(const u8* vertex, const Attr* a, u8 attr_id) {
    if (!a->present)
        return NULL;
    if (a->direct)
        return vertex + a->off;
    const u32 idx = (a->index_size == 1u) ? vertex[a->off]
                                          : read_be16(vertex + a->off);
    if (!g_arr_base_valid[attr_id])
        return NULL;
    const u32 stride = g_arr_stride[attr_id];
    DolGuestResolvedRange range;
    if (!gx_resolve_range(g_arr_base[attr_id] + idx * stride, a->esize,
                          DOL_GUEST_RESOURCE_VERTEX_ARRAY, &range))
        return NULL;
    return (const u8*)range.data;
}

static u32 scissor_offset_x(void);
static u32 scissor_offset_y(void);

typedef struct {
    f32 sx, sy, sz, w, dz;
    f32 r, g, b, a;
    f32 u, v;
    bool has_uv;
    bool clip;  // outside the near plane
} Vtx;

static void transform_vertex(const DolAuroraRecompRenderDrawPacket* d,
                             const f32 pos[3], Vtx* out) {
    u32 mi = d->current_pn_matrix;
    if (mi >= DOL_GX_RECOMP_POSITION_MATRIX_COUNT)
        mi = 0;
    const f32* m = d->position_matrices[mi];
    const bool m_valid =
        (d->position_matrix_valid_mask & (1u << mi)) != 0u;

    f32 vx = pos[0], vy = pos[1], vz = pos[2];
    if (m_valid) {
        vx = m[0] * pos[0] + m[1] * pos[1] + m[2] * pos[2] + m[3];
        vy = m[4] * pos[0] + m[5] * pos[1] + m[6] * pos[2] + m[7];
        vz = m[8] * pos[0] + m[9] * pos[1] + m[10] * pos[2] + m[11];
    }

    const f32* pr = d->projection;
    f32 cx, cy, cz, cw;
    if (d->projection_type == 1u) {  // orthographic
        cx = pr[0] * vx + pr[1];
        cy = pr[2] * vy + pr[3];
        cz = pr[4] * vz + pr[5];
        cw = 1.f;
    } else {  // perspective (GC 6-param form)
        cx = pr[0] * vx + pr[1] * vz;
        cy = pr[2] * vy + pr[3] * vz;
        cz = pr[4] * vz + pr[5];
        cw = -vz;
    }
    // GX clip z lands in [-w, 0]; flip to reversed-Z [0, w] (near = 0).
    cz = -cz;

    out->w = cw;
    if (cw <= 0.000001f) {
        out->clip = true;
        out->sx = out->sy = out->sz = out->dz = 0.f;
        return;
    }
    const f32 invw = 1.f / cw;
    const f32 nx = cx * invw;
    const f32 ny = cy * invw;
    const f32 nz = cz * invw;

    // GX window coordinates carry a +342 scissor bias in the viewport origin
    // registers (Dolphin subtracts bpmem.scissorOffset); without it every quad
    // lands 342 px off. wd/ht are the halved scale values from XF 0x101A/B.
    const f32 wd = d->viewport[0];
    const f32 ht = d->viewport[1];
    const f32 offx = (f32)scissor_offset_x();
    const f32 offy = (f32)scissor_offset_y();
    out->sx = nx * wd + (d->viewport[3] - offx);
    out->sy = (f32)EFB_H - (d->viewport[4] - offy) + ht * ny;
    out->sz = nz;      // reversed Z in [0, 1]
    out->dz = nz;
    out->clip = false;
}

static u32 g_force_vs;   // COLORS_FORCE_VS_SIZE, diagnostic only

static void decode_vertex(const DolAuroraRecompRenderDrawPacket* d,
                          const Layout* l, u32 index, Vtx* out) {
    memset(out, 0, sizeof(*out));
    out->r = out->g = out->b = out->a = 1.f;
    out->has_uv = false;

    const u32 stride = g_force_vs != 0u ? g_force_vs : d->vertex_size;
    const u8* v = d->vertex_payload + (size_t)index * (size_t)stride;

    f32 pos[3] = {0.f, 0.f, 0.f};
    const u8* p = attr_ptr(v, &l->pos, 0u);
    if (p != NULL) {
        pos[0] = read_comp(p, l->pos.fmt);
        if (l->pos.elems >= 2u)
            pos[1] = read_comp(p + comp_size(l->pos.fmt), l->pos.fmt);
        if (l->pos.elems >= 3u)
            pos[2] = read_comp(p + 2u * comp_size(l->pos.fmt), l->pos.fmt);
    }

    const u8* c = attr_ptr(v, &l->col0, 2u);
    if (c == NULL)
        c = attr_ptr(v, &l->col1, 3u);
    if (c != NULL) {
        const Attr* ca = (attr_ptr(v, &l->col0, 2u) != NULL) ? &l->col0
                                                             : &l->col1;
        f32 rgba[4];
        read_color(c, ca->cformat, rgba);
        out->r = rgba[0];
        out->g = rgba[1];
        out->b = rgba[2];
        out->a = rgba[3];
    }

    const u8* t = attr_ptr(v, &l->tex[0], 4u);
    if (t != NULL) {
        const f32 scale = (l->tex[0].frac > 0u && l->tex[0].fmt != 4u)
                              ? 1.f / (f32)(1u << l->tex[0].frac)
                              : 1.f;
        out->u = read_comp(t, l->tex[0].fmt) * scale;
        if (l->tex[0].elems >= 2u)
            out->v = read_comp(t + comp_size(l->tex[0].fmt), l->tex[0].fmt) *
                     scale;
        out->has_uv = true;
    }

    transform_vertex(d, pos, out);
}

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

static bool tex_decode(Tex* t) {
    if (t->rgba != NULL)
        return true;
    if (!t->valid || t->width == 0u || t->height == 0u || t->data == NULL)
        return false;
    if (getenv("COLORS_TEXLOG") != NULL) {
        static u32 seen_addr[64], seen_fmt[64];
        static u32 seen_count;
        bool known = false;
        for (u32 i = 0; i < seen_count; i++)
            if (seen_addr[i] == t->address && seen_fmt[i] == t->format)
                known = true;
        if (!known && seen_count < 64u) {
            seen_addr[seen_count] = t->address;
            seen_fmt[seen_count] = t->format;
            seen_count++;
            fprintf(stderr,
                    "[texlog] addr=0x%08X fmt=%u %ux%u size=%zu tlut=%u/%u/%zu\n",
                    t->address, t->format, t->width, t->height,
                    (size_t)t->data_size, t->tlut_format, t->tlut_entries,
                    (size_t)t->tlut_size);
        }
    }
    const size_t need = (size_t)t->width * t->height * 4u;
    u8* rgba = (u8*)calloc(1, need);
    if (rgba == NULL)
        return false;
    bool ok;
    if (t->format == 8u || t->format == 9u || t->format == 10u) {
        ok = colors_decode_ci(t->format, t->width, t->height, t->data,
                              t->data_size, t->tlut_format, t->tlut_entries,
                              t->tlut, t->tlut_size, rgba, need);
    } else {
        ok = colors_decode_texture(t->format, t->width, t->height, t->data,
                                   t->data_size, rgba, need);
    }
    if (!ok) {
        free(rgba);
        return false;
    }
    t->rgba = rgba;
    {
        const char* one = getenv("COLORS_TEX_DUMP_ONE");
        if (one != NULL && one[0] != '\0' &&
            t->address == (u32)strtoul(one, NULL, 0)) {
            static u32 one_n = 0;
            char path[512];
            snprintf(path, sizeof(path), "/tmp/opencode/texone_%u.ppm", one_n++);
            FILE* f = fopen(path, "wb");
            if (f != NULL) {
                fprintf(f, "P6\n%u %u\n255\n", t->width, t->height);
                for (u32 i = 0; i < t->width * t->height; i++)
                    fwrite(rgba + i * 4u, 1, 3, f);
                fclose(f);
                fprintf(stderr, "[texone] 0x%08X fmt=%u %ux%u -> %s\n",
                        t->address, t->format, t->width, t->height, path);
            }
        }
    }
    {
        const char* dir = getenv("COLORS_TEX_DUMP");
        static u32 dumped = 0;
        if (dir != NULL && dir[0] != '\0' && dumped < 40u) {
            char path[512];
            snprintf(path, sizeof(path), "%s/tex%02u_%08X_%ux%u_f%u.ppm", dir,
                     dumped, t->address, t->width, t->height, t->format);
            FILE* f = fopen(path, "wb");
            if (f != NULL) {
                fprintf(f, "P6\n%u %u\n255\n", t->width, t->height);
                for (u32 i = 0; i < t->width * t->height; i++)
                    fwrite(rgba + i * 4u, 1, 3, f);
                fclose(f);
                dumped++;
            }
        }
    }
    return true;
}

// BP 0x59 (scissor offset) holds the half of the GX window-coordinate bias;
// Dolphin's viewport raw_x is `xOrig - scissorOffset * 2`, and the unset
// default offset is 171 (so the doubled bias is 342).
static u32 scissor_offset_x(void) {
    return 2u * (g_bp_valid[0x59u] ? (g_bp[0x59u] & 0x3FFu) : 171u);
}

static u32 scissor_offset_y(void) {
    return 2u * (g_bp_valid[0x59u] ? ((g_bp[0x59u] >> 10) & 0x3FFu) : 171u);
}

static f32 wrap_coord(f32 c, u32 mode) {
    // c is already in texel units; return 0..1 wrapped coordinate.
    switch (mode) {
    case 1u: {  // repeat
        f32 f = c - floorf(c);
        if (f < 0.f)
            f += 1.f;
        return f;
    }
    case 2u: {  // mirror
        f32 f = c;
        f32 period = 2.f;
        f = f - 2.f * floorf(f / period);
        if (f < 0.f)
            f += 2.f;
        return (f <= 1.f) ? f : (2.f - f);
    }
    default:  // clamp
        return c < 0.f ? 0.f : (c > 0.999999f ? 0.999999f : c);
    }
}

static inline bool tex_sample(u32 slot, f32 u, f32 v, f32 out[4]) {
    // Geometry/transform probe: ignore image content so coverage, placement and
    // rect sizes can be checked without depending on what the title uploaded.
    if (g_opt_white_tex) {
        out[0] = out[1] = out[2] = out[3] = 1.f;
        return true;
    }
    if (g_opt_tex_magenta) {
        out[0] = 1.f; out[1] = 0.f; out[2] = 1.f; out[3] = 1.f;
        return true;
    }
    if (slot >= DOL_GX_RECOMP_TEXTURE_SLOTS)
        return false;
    Tex* t = &g_tex[slot];
    if (t->rgba == NULL && !tex_decode(t)) {
        if (getenv("COLORS_TEX_FAIL") != NULL) {
            static u32 seen[16];
            static u32 n;
            const u32 key = (slot << 24) ^ t->address ^ (t->format << 8);
            bool dup = false;
            for (u32 i = 0; i < n; i++)
                if (seen[i] == key) dup = true;
            if (!dup && n < 16u) {
                seen[n++] = key;
                fprintf(stderr,
                        "[texfail] slot=%u valid=%d addr=0x%08X fmt=%u %ux%u "
                        "data=%p size=%zu tlut=%u/%u\n",
                        slot, (int)t->valid, t->address, t->format, t->width,
                        t->height, (const void*)t->data, t->data_size,
                        t->tlut_format, t->tlut_entries);
            }
        }
        return false;
    }

    if (g_opt_uv_pixels) {
        // Treat UVs above 1 as pixel coordinates for this texture.
        if (u > 1.f && u <= (f32)t->width)
            u /= (f32)t->width;
        if (v > 1.f && v <= (f32)t->height)
            v /= (f32)t->height;
    }
    const u32 mode = g_bp_valid[0xA0u] ? g_bp[0xA0u] : 0u;
    const f32 fw = (f32)t->width;
    const f32 fh = (f32)t->height;
    const f32 wu = wrap_coord(u, mode & 7u);
    const f32 wv = wrap_coord(v, (mode >> 3) & 7u);
    u32 x = (u32)(wu * fw);
    u32 y = (u32)(wv * fh);
    if (x >= t->width)
        x = t->width - 1u;
    if (y >= t->height)
        y = t->height - 1u;
    const u8* px = t->rgba + ((size_t)y * t->width + x) * 4u;
    // Multiply by 1/255 instead of dividing (4 divisions per shaded texel).
    const f32 inv255 = 1.f / 255.f;
    out[0] = (f32)px[0] * inv255;
    out[1] = (f32)px[1] * inv255;
    out[2] = (f32)px[2] * inv255;
    out[3] = (f32)px[3] * inv255;
    return true;
}

// Sampler for the fast pixel paths: the texture object, its dimensions and
// the wrap mode are hoisted out of the loop, so this avoids the per-pixel
// slot lookup / decode check / BP read.
static inline bool tex_sample_cached(const u8* rgba, u32 w, u32 h, u32 mode,
                                     f32 u, f32 v, f32 out[4]) {
    if (rgba == NULL || w == 0u || h == 0u)
        return false;
    const f32 wu = wrap_coord(u, mode & 7u);
    const f32 wv = wrap_coord(v, (mode >> 3) & 7u);
    u32 x = (u32)(wu * (f32)w);
    u32 y = (u32)(wv * (f32)h);
    if (x >= w) x = w - 1u;
    if (y >= h) y = h - 1u;
    const u8* px = rgba + ((size_t)y * w + x) * 4u;
    const f32 inv255 = 1.f / 255.f;
    out[0] = (f32)px[0] * inv255;
    out[1] = (f32)px[1] * inv255;
    out[2] = (f32)px[2] * inv255;
    out[3] = (f32)px[3] * inv255;
    return true;
}

// ---------------------------------------------------------------------------
// Rasterisation
// ---------------------------------------------------------------------------

static inline f32 edge(f32 ax, f32 ay, f32 bx, f32 by, f32 cx, f32 cy) {
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

static inline f32 tev_c_arg(u32 arg, u32 ch, const f32 prev[4], const f32 ras[4],
                     const f32 tex[4], const f32 konst[4]) {
    switch (arg & 0xFu) {
    case 0: return prev[ch];              // CPREV
    case 1: return prev[3];               // APREV
    case 2: return g_tevreg[0][ch];       // C0
    case 3: return g_tevreg[0][3];        // A0
    case 4: return g_tevreg[1][ch];       // C1
    case 5: return g_tevreg[1][3];        // A1
    case 6: return g_tevreg[2][ch];       // C2
    case 7: return g_tevreg[2][3];        // A2
    case 8: return tex[ch];               // TEXC
    case 9: return tex[3];                // TEXA
    case 10: return ras[ch];              // RASC
    case 11: return ras[3];               // RASA
    case 12: return 1.f;                  // ONE
    case 13: return 0.5f;                 // HALF
    case 14: return konst[ch];            // KONST
    default: return 0.f;                  // ZERO
    }
}

static inline f32 tev_a_arg(u32 arg, const f32 prev[4], const f32 ras[4],
                     const f32 tex[4], const f32 konst[4]) {
    switch (arg & 0x7u) {
    case 0: return prev[3];
    case 1: return g_tevreg[0][3];
    case 2: return g_tevreg[1][3];
    case 3: return g_tevreg[2][3];
    case 4: return tex[3];
    case 5: return ras[3];
    case 6: return konst[3];
    default: return 0.f;
    }
}

// GX TEV combiner. The hardware (and Dolphin's shader generator) evaluates
//     out = mix(a, b, c) + d        (ADD)
//     out = d - mix(a, b, c)        (SUB)
// where mix(a,b,c) = a*(1-c) + b*c. This is why GX_MODULATE
// (a=ZERO, b=TEXC, c=RASC, d=ZERO) yields TEXC*RASC. The earlier
// (a*b) + (c*d) form made every SDK combiner evaluate to zero. Bias is
// applied before the scale.
static f32 tev_combine(f32 a, f32 b, f32 c, f32 d, u32 op, u32 bias,
                       u32 scale) {
    const f32 mix = a * (1.f - c) + b * c;
    f32 t = (op & 1u) != 0u ? (d - mix) : (mix + d);
    if ((bias & 3u) == 1u)
        t += 0.5f;
    else if ((bias & 3u) == 2u)
        t -= 0.5f;
    switch (scale & 3u) {
    case 1u: t *= 2.f; break;
    case 2u: t *= 4.f; break;
    case 3u: t *= 0.5f; break;
    default: break;
    }
    return t;
}

static inline f32 tev_stage0_alpha(const TevStage* t0, const f32 ras[4],
                                   const f32 texel[4]) {
    const f32 prev[4] = {0.f, 0.f, 0.f, 0.f};
    f32 konst[4] = {1.f, 1.f, 1.f, 1.f};
    const bool need_k = t0->a_a == 6u || t0->a_b == 6u ||
                        t0->a_c == 6u || t0->a_d == 6u;
    if (need_k) {
        const u32 kc = t0->kc_sel & 0x1Fu;
        const u32 ka = t0->ka_sel & 0x1Fu;
        if (kc < 0x10u) {
            konst[0] = g_kcolor[kc / 4u][0];
            konst[1] = g_kcolor[kc / 4u][1];
            konst[2] = g_kcolor[kc / 4u][2];
        } else if (kc < 0x14u) {
            const f32* r = g_tevreg[kc - 0x10u];
            konst[0] = r[0]; konst[1] = r[1]; konst[2] = r[2];
        }
        if (ka < 0x4u)
            konst[3] = g_kcolor[ka][3];
        else if (ka < 0x8u)
            konst[3] = g_tevreg[ka - 0x4u][3];
    }
    const f32 a = tev_a_arg(t0->a_a, prev, ras, texel, konst);
    const f32 b = tev_a_arg(t0->a_b, prev, ras, texel, konst);
    const f32 c = tev_a_arg(t0->a_c, prev, ras, texel, konst);
    const f32 d = tev_a_arg(t0->a_d, prev, ras, texel, konst);
    f32 t = tev_combine(a, b, c, d, t0->a_op, t0->a_bias, t0->a_scale);
    if (t < 0.f) t = 0.f;
    else if (t > 1.f) t = 1.f;
    return t;
}

static void put_pixel(u32 x, u32 y, const f32 rgba[4]) {
    u8* dst = g_color + ((size_t)y * EFB_W + x) * 4u;
    dst[0] = (u8)(rgba[0] * 255.f + 0.5f);
    dst[1] = (u8)(rgba[1] * 255.f + 0.5f);
    dst[2] = (u8)(rgba[2] * 255.f + 0.5f);
    dst[3] = (u8)(rgba[3] * 255.f + 0.5f);
}

// GX blend factors (BP 0x41 src/dst factor fields).
static inline f32 blend_factor(u32 factor, const f32 src[4], const f32 dst[4]) {
    switch (factor & 7u) {
    case 0: return 0.f;                 // ZERO
    case 1: return 1.f;                 // ONE
    case 2: return src[0];              // SRCCLR (same channel)
    case 3: return 1.f - src[0];        // INVSRCCLR
    case 4: return src[3];              // SRCALPHA
    case 5: return 1.f - src[3];        // INVSRCALPHA
    case 6: return dst[3];              // DSTALPHA
    default: return 1.f - dst[3];       // INVDSTALPHA
    }
}

static void raster_tri(const Vtx* v0, const Vtx* v1, const Vtx* v2,
                       bool textured) {
    ++g_stats.triangles;
    u32 pixels = 0;  // local: g_stats is a global the inner loop would reload
    const f32 area = edge(v0->sx, v0->sy, v1->sx, v1->sy, v2->sx, v2->sy);
    if (fabsf(area) < 0.0001f)
        return;
    const f32 inv_area = 1.f / area;

    s32 minx = (s32)floorf(fminf(v0->sx, fminf(v1->sx, v2->sx)));
    s32 maxx = (s32)ceilf(fmaxf(v0->sx, fmaxf(v1->sx, v2->sx)));
    s32 miny = (s32)floorf(fminf(v0->sy, fminf(v1->sy, v2->sy)));
    s32 maxy = (s32)ceilf(fmaxf(v0->sy, fmaxf(v1->sy, v2->sy)));
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > (s32)EFB_W - 1) maxx = (s32)EFB_W - 1;
    if (maxy > (s32)EFB_H - 1) maxy = (s32)EFB_H - 1;
    if (g_scissor_valid) {
        if (minx < (s32)g_scissor_x0) minx = (s32)g_scissor_x0;
        if (miny < (s32)g_scissor_y0) miny = (s32)g_scissor_y0;
        if (maxx > (s32)g_scissor_x1) maxx = (s32)g_scissor_x1;
        if (maxy > (s32)g_scissor_y1) maxy = (s32)g_scissor_y1;
    }
    if (minx > maxx || miny > maxy)
        return;

    const f32 ia = 1.f / v0->w, ib = 1.f / v1->w, ic = 1.f / v2->w;
    const bool want_tex = textured || (g_tev_valid && g_tev[0].tex_enabled);

    // Fast paths for the two configurations the title uses for almost every
    // full-screen pass: "texture pass-through" (out = TEXC/TEXA) and
    // "raster pass-through" (out = RASC/RASA). They skip the generic combiner.
    bool fast_tex = false;
    bool fast_ras = false;
    bool fast_mod = false;
    bool fast_mod_tex = false;
    bool fast_lerp = false;
    bool fast_texd = false;
    bool fast_rasd = false;
    u32 fast_tex_slot = 0u;
    if (g_opt_force_tex_pass && textured) {
        fast_tex = true;
        fast_tex_slot = (g_tev_valid && g_tev[0].tex_map < 8u) ? g_tev[0].tex_map
                                                               : 0u;
    }
    if (g_tev_valid && g_num_tev_stages == 1u) {
        const TevStage* t0 = &g_tev[0];
        // Exact pass-throughs: (TEXC * ONE) + (ZERO * ZERO), and the same
        // with RASC. Bias/op/scale must be neutral.
        const bool c_plain = t0->c_bias == 0u && t0->c_scale == 0u &&
                             t0->c_op == 0u && t0->c_c == 15u && t0->c_d == 15u;
        const bool a_plain = t0->a_bias == 0u && t0->a_scale == 0u &&
                             t0->a_op == 0u && t0->a_c == 7u && t0->a_d == 7u;
        (void)0;
        if (c_plain && a_plain && t0->c_a == 12u && t0->c_b == 8u &&
            t0->a_a == 7u && t0->a_b == 4u && t0->tex_enabled &&
            t0->tex_map < 8u && g_tex[t0->tex_map].valid) {
            fast_tex = true;
            fast_tex_slot = t0->tex_map;
        } else if (c_plain && a_plain && t0->c_a == 12u && t0->c_b == 10u &&
                   t0->a_a == 7u && t0->a_b == 5u) {
            fast_ras = true;
        } else if (t0->c_a == 15u && t0->c_b == 8u && t0->c_c == 10u &&
                   t0->c_d == 15u) {
            // GX_MODULATE colour (alpha handled generically below). When the
            // stage's texture is disabled/invalid the sampler returns 1, so
            // the same branch degenerates to RASC.
            fast_mod = true;
            fast_mod_tex = t0->tex_enabled && t0->tex_map < 8u &&
                           g_tex[t0->tex_map].valid;
            fast_tex_slot = t0->tex_map;
        } else if (t0->c_bias == 0u && t0->c_scale == 0u && t0->c_op == 0u &&
                   ((t0->c_a == 8u && t0->c_b == 15u && t0->c_c == 15u &&
                     t0->c_d == 15u) ||
                    (t0->c_a == 15u && t0->c_b == 15u && t0->c_c == 15u &&
                     t0->c_d == 8u)) &&
                   t0->tex_enabled && t0->tex_map < 8u &&
                   g_tex[t0->tex_map].valid) {
            // Plain texture pass (TEXC through a or d).
            fast_texd = true;
            fast_tex_slot = t0->tex_map;
        } else if (t0->c_bias == 0u && t0->c_scale == 0u && t0->c_op == 0u &&
                   t0->c_a == 15u && t0->c_b == 15u && t0->c_c == 15u &&
                   t0->c_d == 10u) {
            // Plain raster pass (RASC through d).
            fast_rasd = true;
        } else if (t0->c_bias == 0u && t0->c_scale == 0u && t0->c_op == 0u &&
                   t0->c_a == 2u && t0->c_b == 4u && t0->c_c == 8u &&
                   t0->c_d == 15u && t0->a_bias == 0u &&
                   t0->a_scale == 0u && t0->a_op == 0u && t0->a_a == 7u &&
                   t0->a_b == 1u && t0->a_c == 4u && t0->a_d == 7u &&
                   t0->tex_enabled && t0->tex_map < 8u &&
                   g_tex[t0->tex_map].valid) {
            // Bloom composite: colour = mix(C0, C1, TEXC), alpha = A0*TEXA.
            fast_lerp = true;
            fast_tex_slot = t0->tex_map;
        }
    }

    // Incremental edge functions: each edge is linear in (x, y), so evaluate
    // it once per row and step by a constant per pixel instead of running the
    // full cross product (two multiplies + two subtracts) for every pixel.
    const f32 e0dx = -(v2->sy - v1->sy);
    const f32 e1dx = -(v0->sy - v2->sy);
    const u8* ft_rgba = NULL;
    u32 ft_w = 0u, ft_h = 0u, ft_mode = 0u;
    if ((fast_tex || fast_mod || fast_texd || fast_lerp) &&
        !g_opt_white_tex && !g_opt_tex_magenta &&
        fast_tex_slot < DOL_GX_RECOMP_TEXTURE_SLOTS) {
        Tex* ft = &g_tex[fast_tex_slot];
        if (ft->rgba == NULL)
            (void)tex_decode(ft);
        if (ft->rgba != NULL) {
            ft_rgba = ft->rgba;
            ft_w = ft->width;
            ft_h = ft->height;
            ft_mode = g_bp_valid[0xA0u] ? g_bp[0xA0u] : 0u;
        }
    }
    const bool ft_ok = ft_rgba != NULL;

    // Hoist the blend mode: ONE/ONE (additive) and SRCALPHA/INVSRCALPHA cover
    // most of the title/bloom passes and avoid the per-pixel factor switch.
    int blend_kind = 0;  // 0 none, 1 one/one, 2 srcA/invsrcA, 3 srcA/one, 4 generic
    if (g_blend_enable && !g_opt_no_blend) {
        const u32 bs = g_blend_src & 7u, bd = g_blend_dst & 7u;
        if (bs == 1u && bd == 1u) blend_kind = 1;
        else if (bs == 4u && bd == 5u) blend_kind = 2;
        else if (bs == 4u && bd == 1u) blend_kind = 3;
        else blend_kind = 4;
    }

    for (s32 y = miny; y <= maxy; y++) {
        const f32 py = (f32)y + 0.5f;
        const f32 px0 = (f32)minx + 0.5f;
        f32 e0 = edge(v1->sx, v1->sy, v2->sx, v2->sy, px0, py);
        f32 e1 = edge(v2->sx, v2->sy, v0->sx, v0->sy, px0, py);
        size_t di = (size_t)y * EFB_W + (u32)minx;
        for (s32 x = minx; x <= maxx; x++, di++) {
            const f32 w0 = e0 * inv_area;
            const f32 w1 = e1 * inv_area;
            e0 += e0dx;
            e1 += e1dx;
            const f32 w2 = 1.f - w0 - w1;
            if (w0 < 0.f || w1 < 0.f || w2 < 0.f)
                continue;

            const f32 z = w0 * v0->sz + w1 * v1->sz + w2 * v2->sz;
            if (!g_opt_no_depth && g_depth_test) {
                // Reversed-Z (near = 1). GX compare functions are expressed for
                // the console's [-w, 0] depth range, so the ordering inverts.
                bool pass;
                switch (g_depth_func & 7u) {
                case 0: pass = false; break;                 // NEVER
                case 1: pass = z > g_depth[di]; break;       // LESS
                case 2: pass = z == g_depth[di]; break;      // EQUAL
                case 3: pass = z >= g_depth[di]; break;      // LEQUAL
                case 4: pass = z < g_depth[di]; break;       // GREATER
                case 5: pass = z != g_depth[di]; break;      // NEQUAL
                case 6: pass = z <= g_depth[di]; break;      // GEQUAL
                default: pass = true; break;                 // ALWAYS
                }
                if (!pass)
                    continue;
            }

            f32 ras[4];
            ras[0] = w0 * v0->r + w1 * v1->r + w2 * v2->r;
            ras[1] = w0 * v0->g + w1 * v1->g + w2 * v2->g;
            ras[2] = w0 * v0->b + w1 * v1->b + w2 * v2->b;
            ras[3] = w0 * v0->a + w1 * v1->a + w2 * v2->a;

            f32 uv[2] = {0.f, 0.f};
            if (want_tex) {
                const f32 iw = w0 * ia + w1 * ib + w2 * ic;
                if (iw > 0.000001f) {
                    uv[0] = (w0 * v0->u * ia + w1 * v1->u * ib +
                             w2 * v2->u * ic) / iw;
                    uv[1] = (w0 * v0->v * ia + w1 * v1->v * ib +
                             w2 * v2->v * ic) / iw;
                }
            }

            f32 col[4];
            if (g_opt_path_stats) {
                g_path_px[fast_tex ? 0 : fast_ras ? 1 :
                          fast_mod ? 2 : fast_lerp ? 5 :
                          fast_texd ? 6 : fast_rasd ? 7 :
                          (!g_tev_valid ? 4 : 3)]++;
            }
            if (fast_tex) {
                if (!ft_ok ||
                    !tex_sample_cached(ft_rgba, ft_w, ft_h, ft_mode, uv[0],
                                       uv[1], col)) {
                    col[0] = col[1] = col[2] = col[3] = 1.f;
                }
            } else if (fast_ras) {
                col[0] = ras[0];
                col[1] = ras[1];
                col[2] = ras[2];
                col[3] = ras[3];
            } else if (fast_mod) {
                f32 texel[4] = {1.f, 1.f, 1.f, 1.f};
                if (fast_mod_tex && ft_ok &&
                    !tex_sample_cached(ft_rgba, ft_w, ft_h, ft_mode, uv[0],
                                       uv[1], texel)) {
                    texel[0] = texel[1] = texel[2] = texel[3] = 1.f;
                }
                {
                    const TevStage* t0 = &g_tev[0];
                    const f32 neg = t0->c_op ? -1.f : 1.f;
                    f32 bias = 0.f;
                    if ((t0->c_bias & 3u) == 1u) bias = 0.5f;
                    else if ((t0->c_bias & 3u) == 2u) bias = -0.5f;
                    f32 scale = 1.f;
                    switch (t0->c_scale & 3u) {
                    case 1u: scale = 2.f; break;
                    case 2u: scale = 4.f; break;
                    case 3u: scale = 0.5f; break;
                    default: break;
                    }
                    for (u32 ch = 0; ch < 3u; ch++) {
                        f32 t = texel[ch] * ras[ch] * neg;
                        t = (t + bias) * scale;
                        col[ch] = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
                    }
                }
                col[3] = tev_stage0_alpha(&g_tev[0], ras, texel);
            } else if (fast_texd) {
                f32 texel[4];
                if (!ft_ok ||
                    !tex_sample_cached(ft_rgba, ft_w, ft_h, ft_mode, uv[0],
                                       uv[1], texel)) {
                    texel[0] = texel[1] = texel[2] = texel[3] = 1.f;
                }
                col[0] = texel[0];
                col[1] = texel[1];
                col[2] = texel[2];
                col[3] = tev_stage0_alpha(&g_tev[0], ras, texel);
            } else if (fast_rasd) {
                f32 texel[4] = {1.f, 1.f, 1.f, 1.f};
                col[0] = ras[0];
                col[1] = ras[1];
                col[2] = ras[2];
                col[3] = tev_stage0_alpha(&g_tev[0], ras, texel);
            } else if (fast_lerp) {
                f32 texel[4];
                if (!ft_ok ||
                    !tex_sample_cached(ft_rgba, ft_w, ft_h, ft_mode, uv[0],
                                       uv[1], texel)) {
                    texel[0] = texel[1] = texel[2] = texel[3] = 1.f;
                }
                const f32* c0 = g_tevreg[0];
                const f32* c1 = g_tevreg[1];
                col[0] = c0[0] + (c1[0] - c0[0]) * texel[0];
                col[1] = c0[1] + (c1[1] - c0[1]) * texel[1];
                col[2] = c0[2] + (c1[2] - c0[2]) * texel[2];
                col[3] = c0[3] * texel[3];
            } else if (!g_tev_valid) {
                // No TEV configured yet: vertex colour modulated by tex0.
                col[0] = ras[0];
                col[1] = ras[1];
                col[2] = ras[2];
                col[3] = ras[3];
                if (textured && g_tex[0].valid) {
                    f32 texel[4];
                    if (tex_sample(0u, uv[0], uv[1], texel)) {
                        col[0] *= texel[0];
                        col[1] *= texel[1];
                        col[2] *= texel[2];
                        col[3] *= texel[3];
                    }
                }
            } else {
                f32 prev[4] = {0.f, 0.f, 0.f, 0.f};
                for (u32 st = 0; st < g_num_tev_stages && st < TEV_MAX_STAGES;
                     st++) {
                    const TevStage* ts = &g_tev[st];
                    f32 texel[4] = {1.f, 1.f, 1.f, 1.f};
                    if (ts->tex_enabled && ts->tex_map < 8u &&
                        g_tex[ts->tex_map].valid)
                        (void)tex_sample(ts->tex_map, uv[0], uv[1], texel);
                    const u32 kc = ts->kc_sel & 0x1Fu;
                    const u32 ka = ts->ka_sel & 0x1Fu;
                    f32 konst[4] = {1.f, 1.f, 1.f, 1.f};
                    const bool need_k =
                        ts->c_a == 14u || ts->c_b == 14u ||
                        ts->c_c == 14u || ts->c_d == 14u ||
                        ts->a_a == 6u || ts->a_b == 6u ||
                        ts->a_c == 6u || ts->a_d == 6u;
                    if (!need_k) {
                        /* no KONST selector: leave konst at 1.0 */
                    } else if (kc < 0x10u) {
                        konst[0] = g_kcolor[kc / 4u][0];
                        konst[1] = g_kcolor[kc / 4u][1];
                        konst[2] = g_kcolor[kc / 4u][2];
                    } else if (kc >= 0x10u && kc < 0x14u) {
                        const f32* r = g_tevreg[kc - 0x10u];
                        konst[0] = r[0];
                        konst[1] = r[1];
                        konst[2] = r[2];
                    }
                    if (ka < 0x4u)
                        konst[3] = g_kcolor[ka][3];
                    else if (ka < 0x8u)
                        konst[3] = g_tevreg[ka - 0x4u][3];
                    f32 out[4];
                    for (u32 ch = 0; ch < 3u; ch++) {
                        const f32 a =
                            tev_c_arg(ts->c_a, ch, prev, ras, texel, konst);
                        const f32 b =
                            tev_c_arg(ts->c_b, ch, prev, ras, texel, konst);
                        const f32 c =
                            tev_c_arg(ts->c_c, ch, prev, ras, texel, konst);
                        const f32 d =
                            tev_c_arg(ts->c_d, ch, prev, ras, texel, konst);
                        out[ch] = tev_combine(a, b, c, d, ts->c_op, ts->c_bias,
                                              ts->c_scale);
                    }
                    {
                        const f32 a = tev_a_arg(ts->a_a, prev, ras, texel, konst);
                        const f32 b = tev_a_arg(ts->a_b, prev, ras, texel, konst);
                        const f32 c = tev_a_arg(ts->a_c, prev, ras, texel, konst);
                        const f32 d = tev_a_arg(ts->a_d, prev, ras, texel, konst);
                        out[3] = tev_combine(a, b, c, d, ts->a_op, ts->a_bias,
                                             ts->a_scale);
                    }
                    for (u32 ch = 0; ch < 4u; ch++) {
                        if (out[ch] < 0.f)
                            out[ch] = 0.f;
                        else if (out[ch] > 1.f)
                            out[ch] = 1.f;
                    }
                    prev[0] = out[0];
                    prev[1] = out[1];
                    prev[2] = out[2];
                    prev[3] = out[3];
                }
                col[0] = prev[0];
                col[1] = prev[1];
                col[2] = prev[2];
                col[3] = prev[3];
            }

            // ROP: blend against the framebuffer (GX cmode0), then write the
            // channels the guest enabled.
            u8* dst = g_color + di * 4u;
            f32 final[4];
            if (blend_kind == 0) {
                final[0] = col[0];
                final[1] = col[1];
                final[2] = col[2];
                final[3] = col[3];
            } else {
                const f32 inv255 = 1.f / 255.f;
                f32 dstc[4] = {(f32)dst[0] * inv255, (f32)dst[1] * inv255,
                               (f32)dst[2] * inv255, (f32)dst[3] * inv255};
                if (blend_kind == 1) {          // ONE / ONE
                    for (u32 ch = 0; ch < 3u; ch++) {
                        f32 t = col[ch] + dstc[ch];
                        final[ch] = t > 1.f ? 1.f : t;
                    }
                } else if (blend_kind == 2) {   // SRCALPHA / INVSRCALPHA
                    const f32 sa = col[3], ia = 1.f - sa;
                    for (u32 ch = 0; ch < 3u; ch++) {
                        f32 t = col[ch] * sa + dstc[ch] * ia;
                        final[ch] = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
                    }
                } else if (blend_kind == 3) {   // SRCALPHA / ONE
                    const f32 sa = col[3];
                    for (u32 ch = 0; ch < 3u; ch++) {
                        f32 t = col[ch] * sa + dstc[ch];
                        final[ch] = t > 1.f ? 1.f : t;
                    }
                } else {
                    const f32 sf = blend_factor(g_blend_src, col, dstc);
                    const f32 df = blend_factor(g_blend_dst, col, dstc);
                    for (u32 ch = 0; ch < 3u; ch++) {
                        final[ch] = g_blend_subtract
                                        ? (dstc[ch] * df - col[ch] * sf)
                                        : (col[ch] * sf + dstc[ch] * df);
                        if (final[ch] < 0.f) final[ch] = 0.f;
                        if (final[ch] > 1.f) final[ch] = 1.f;
                    }
                }
                final[3] = col[3];
            }

            if (g_opt_tev_ras) {
                final[0] = ras[0];
                final[1] = ras[1];
                final[2] = ras[2];
                final[3] = ras[3];
            }
            if (g_opt_tev_force) {
                final[0] = final[1] = final[2] = 1.f;
                final[3] = 1.f;
            }
            if (g_depth_update)
                g_depth[di] = z;
            for (u32 ch = 0; ch < 4u; ch++) {
                if (ch == 3u && !g_alpha_update)
                    continue;
                if (ch < 3u && !g_color_update)
                    continue;
                dst[ch] = (u8)(final[ch] * 255.f + 0.5f);
            }
            ++pixels;
        }
    }
    g_stats.pixels += pixels;
    if (g_opt_path_stats && pixels != 0u) {
        if (g_tev_valid)
            g_combo_px[(u32)((g_tev[0].c_a << 12) | (g_tev[0].c_b << 8) |
                             (g_tev[0].c_c << 4) | g_tev[0].c_d)] += pixels;
        if (g_num_tev_stages < 9u)
            g_stage_px[g_num_tev_stages] += pixels;
    }
}

static double g_prof_tri;
static inline double prof_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
static void emit_tri(const Vtx* a, const Vtx* b, const Vtx* c, bool textured) {
    if (a->clip || b->clip || c->clip)
        return;
    if (getenv("COLORS_RASTER_PROF") != NULL) {
        const double t0 = prof_now();
        raster_tri(a, b, c, textured);
        g_prof_tri += prof_now() - t0;
        return;
    }
    raster_tri(a, b, c, textured);
}

static int g_seq;

void raster_packet_draw(const DolAuroraRecompRenderDrawPacket* d) {
    if (getenv("COLORS_SEQ") != NULL && g_seq < 400)
        fprintf(stderr, "[seq] draw fmt=%u prim=%02X n=%u tex=%08X\n",
                d->vtx_fmt, d->primitive & 0xF8u, d->vertex_count,
                g_tex[0].valid ? g_tex[0].address : 0u);
    {
        const char* dl = getenv("COLORS_DRAWLOG");
        if (dl != NULL) {
            const u32 lo = (u32)strtoul(dl, NULL, 0);
            const u32 hi = (u32)strtoul(strchr(dl, ':') ? strchr(dl, ':') + 1
                                                        : "0", NULL, 0);
            if (g_stats.clears >= lo && (hi == 0u || g_stats.clears < hi)) {
                static u32 dn = 0;
                if (dn++ < 600u)
                    fprintf(stderr,
                            "[drawlog] p=%u fmt=%u prim=%02X n=%u vs=%u tex=%08X "
                            "tfmt=%u %ux%u\n",
                            g_stats.clears, d->vtx_fmt, d->primitive & 0xF8u,
                            d->vertex_count, d->vertex_size,
                            g_tex[0].valid ? g_tex[0].address : 0u,
                            g_tex[0].valid ? g_tex[0].format : 0u,
                            g_tex[0].valid ? g_tex[0].width : 0u,
                            g_tex[0].valid ? g_tex[0].height : 0u);
            }
        }
        const char* ml = getenv("COLORS_MOVIE_TEXLOG");
        if (ml != NULL && g_tex[0].valid && g_tex[0].address >= 0x90000000u) {
            static u32 seen[32], n;
            bool known = false;
            for (u32 i = 0; i < n; i++)
                if (seen[i] == g_tex[0].address) known = true;
            if (!known && n < 32u) {
                seen[n++] = g_tex[0].address;
                fprintf(stderr,
                        "[movietex] addr=0x%08X fmt=%u %ux%u prim=%02X n=%u\n",
                        g_tex[0].address, g_tex[0].format, g_tex[0].width,
                        g_tex[0].height, d->primitive & 0xF8u,
                        d->vertex_count);
            }
        }
    }
    if (!g_ready || d == NULL || d->vertex_payload == NULL ||
        d->vertex_count == 0u || d->vertex_size == 0u)
        return;
    {
        const char* pre = getenv("COLORS_FB_PRE");
        static u32 pre_dumps = 0;
        if (pre != NULL && pre[0] != '\0' && d->vtx_fmt == 0u &&
            pre_dumps < 1u) {
            if (raster_dump_ppm(pre))
                pre_dumps++;
        }
    }

    if (getenv("COLORS_TEV_DUMP") != NULL) {
        static int tn = 0;
        const char* want = getenv("COLORS_TEV_FMT");
        const bool match = want == NULL || (u32)atoi(want) == d->vtx_fmt;
        if (tn < 8 && match && g_stats.draws > 1000000u) {
            ++tn;
            fprintf(stderr, "[tev] raw C0=%06X C1=%06X gen=%06X\n",
                    g_bp_valid[0xC0] ? g_bp[0xC0] : 0u,
                    g_bp_valid[0xC1] ? g_bp[0xC1] : 0u,
                    g_bp_valid[0x00] ? g_bp[0x00] : 0u);
            fprintf(stderr,
                    "[tev] draw tex=%08X vs=%u stages=%u tevvalid=%d "
                    "reg0=(%.2f,%.2f,%.2f,%.2f) reg1=(%.2f,%.2f,%.2f,%.2f) "
                    "reg2=(%.2f,%.2f,%.2f,%.2f)\n",
                    g_tex[0].valid ? g_tex[0].address : 0u, d->vertex_size,
                    g_num_tev_stages, (int)g_tev_valid, g_tevreg[0][0],
                    g_tevreg[0][1], g_tevreg[0][2], g_tevreg[0][3],
                    g_tevreg[1][0], g_tevreg[1][1], g_tevreg[1][2],
                    g_tevreg[1][3], g_tevreg[2][0], g_tevreg[2][1],
                    g_tevreg[2][2], g_tevreg[2][3]);
            for (u32 i = 0; i < g_num_tev_stages && i < TEV_MAX_STAGES; i++)
                fprintf(stderr,
                        "[tev]  s%u map=%u en=%d c=(%u,%u,%u,%u) op=%u bias=%u "
                        "sc=%u a=(%u,%u,%u,%u) aop=%u abias=%u asc=%u "
                        "kc=%u ka=%u\n",
                        i, g_tev[i].tex_map, (int)g_tev[i].tex_enabled,
                        g_tev[i].c_a, g_tev[i].c_b, g_tev[i].c_c,
                        g_tev[i].c_d, g_tev[i].c_op, g_tev[i].c_bias,
                        g_tev[i].c_scale, g_tev[i].a_a, g_tev[i].a_b,
                        g_tev[i].a_c, g_tev[i].a_d, g_tev[i].a_op,
                        g_tev[i].a_bias, g_tev[i].a_scale, g_tev[i].kc_sel,
                        g_tev[i].ka_sel);
        }
    }
    Layout l;
    derive_layout(d->vtx_fmt, false, &l);
    if (g_force_vs != 0u) {
        l.size = g_force_vs;
        if (l.tex[0].present && l.tex[0].off + l.tex[0].esize > g_force_vs)
            l.tex[0].present = false;
        if (l.col0.present && l.col0.off + l.col0.esize > g_force_vs)
            l.col0.present = false;
        if (l.pos.off + l.pos.esize > g_force_vs)
            l.pos.elems = (g_force_vs - l.pos.off) / 4u;
    }
    if (l.valid && d->vertex_size > l.size) {
        Layout forced;
        derive_layout(d->vtx_fmt, true, &forced);
        if (forced.valid && forced.size == d->vertex_size)
            l = forced;
    }
    if (!l.valid || l.size != d->vertex_size) {
        Layout guess;
        if (layout_from_size(d->vertex_size, &guess))
            l = guess;
    }
    if (!l.valid)
        return;

    uint32_t n = d->vertex_count;
    if (n > 4096u)
        n = 4096u;
    if ((size_t)n * d->vertex_size > d->vertex_payload_size)
        n = d->vertex_payload_size / d->vertex_size;
    if (n == 0u)
        return;

    Vtx* v = (Vtx*)malloc(sizeof(Vtx) * n);
    if (v == NULL)
        return;
    for (u32 i = 0; i < n; i++)
        decode_vertex(d, &l, i, &v[i]);

    const char* probe_tex_env = getenv("COLORS_PROBE_TEX");
    const bool probe_tex =
        probe_tex_env != NULL && g_tex[0].valid &&
        g_tex[0].address == (u32)strtoul(probe_tex_env, NULL, 0);
    if (probe_tex || (getenv("COLORS_PROBE_VS") != NULL &&
                      d->vertex_size == (u32)atoi(getenv("COLORS_PROBE_VS")))) {
        static u32 pn = 0;
        if (pn++ < 6u) {
            fprintf(stderr,
                    "[probe] fmt=%u prim=%02X n=%u vs=%u tex=0x%08X "
                    "proj=%u mi=%u mask=%03X vp=(%.1f,%.1f,%.1f,%.1f) "
                    "pos=(off%u e%u f%u) col=(p%u cf%u) uv=(p%u e%u f%u) "
                    "v0=(%.1f,%.1f) col0=(%.2f,%.2f,%.2f,%.2f) uv0=(%.2f,%.2f)\n",
                    d->vtx_fmt, d->primitive & 0xF8u, d->vertex_count,
                    d->vertex_size, g_tex[0].valid ? g_tex[0].address : 0u,
                    d->projection_type, d->current_pn_matrix,
                    d->position_matrix_valid_mask, d->viewport[0],
                    d->viewport[1], d->viewport[3], d->viewport[4],
                    l.pos.off, l.pos.elems, l.pos.fmt, (unsigned)l.col0.present,
                    l.col0.cformat, l.tex[0].off, l.tex[0].elems, l.tex[0].fmt,
                    v[0].sx, v[0].sy, v[0].r, v[0].g, v[0].b, v[0].a, v[0].u,
                    v[0].v);
            {
                const u8* vv = d->vertex_payload;
                const u32 idx = vv[0];
                DolGuestResolvedRange rr;
                if (gx_resolve_range(g_arr_base[0] + idx * g_arr_stride[0],
                                     l.pos.esize,
                                     DOL_GUEST_RESOURCE_VERTEX_ARRAY, &rr)) {
                    const u8* p2 = (const u8*)rr.data;
                    fprintf(stderr,
                            "[probe]   idx=%u elem=%d,%d,%d bytes=%02X %02X "
                            "%02X %02X %02X %02X\n",
                            idx, (s16)read_be16(p2), (s16)read_be16(p2 + 2),
                            (s16)read_be16(p2 + 4), p2[0], p2[1], p2[2], p2[3],
                            p2[4], p2[5]);
                } else {
                    fprintf(stderr, "[probe]   idx=%u RESOLVE FAILED\n", idx);
                }
            }
            {
                u32 nz = 0;
                for (u32 mi = 0; mi < DOL_GX_RECOMP_POSITION_MATRIX_COUNT; mi++)
                    for (u32 w = 0; w < DOL_GX_RECOMP_POSITION_MATRIX_WORDS; w++)
                        if (d->position_matrices[mi][w] != 0.f)
                            nz++;
                fprintf(stderr, "[probe]   matrices non-zero words=%u/120\n", nz);
            }
            {
                const f32* m = d->position_matrices[d->current_pn_matrix];
                const u8* vv = d->vertex_payload;
                DolGuestResolvedRange rr;
                if (gx_resolve_range(g_arr_base[0] + (u32)vv[0] * g_arr_stride[0],
                                     l.pos.esize,
                                     DOL_GUEST_RESOURCE_VERTEX_ARRAY, &rr)) {
                    const u8* p2 = (const u8*)rr.data;
                    const f32 x = (f32)(s16)read_be16(p2);
                    const f32 y = (f32)(s16)read_be16(p2 + 2);
                    const f32 z = (f32)(s16)read_be16(p2 + 4);
                    const f32 vz = m[8] * x + m[9] * y + m[10] * z + m[11];
                    fprintf(stderr,
                            "[probe]   m0=(%.3f,%.3f,%.3f,%.3f) m1=(%.3f,%.3f,%.3f,%.3f) m2=(%.3f,%.3f,%.3f,%.3f) vz=%.3f\n",
                            m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7],
                            m[8], m[9], m[10], m[11], vz);
                }
            }
            fprintf(stderr,
                    "[probe]   clip=%d w=%.3f projtype=%u vp=(%.1f,%.1f,%.1f,%.1f) "
                    "proj=(%.4f,%.4f,%.4f,%.4f,%.4f,%.4f) mi=%u mask=0x%X\n",
                    (int)v[0].clip, v[0].w, d->projection_type, d->viewport[0],
                    d->viewport[1], d->viewport[3], d->viewport[4],
                    d->projection[0], d->projection[1], d->projection[2],
                    d->projection[3], d->projection[4], d->projection[5],
                    d->current_pn_matrix, d->position_matrix_valid_mask);
            fprintf(stderr,
                    "[probe]   pos idxsz=%u base_valid=%d base=0x%08X stride=%u "
                    "raw0=%02X %02X %02X %02X %02X %02X\n",
                    l.pos.index_size, (int)g_arr_base_valid[0], g_arr_base[0],
                    g_arr_stride[0], d->vertex_payload[0],
                    d->vertex_payload_size > 1 ? d->vertex_payload[1] : 0,
                    d->vertex_payload_size > 2 ? d->vertex_payload[2] : 0,
                    d->vertex_payload_size > 3 ? d->vertex_payload[3] : 0,
                    d->vertex_payload_size > 4 ? d->vertex_payload[4] : 0,
                    d->vertex_payload_size > 5 ? d->vertex_payload[5] : 0);
        }
    }

    {
        const char* dbg = getenv("COLORS_RASTER_DEBUG");
        static int vdbg = 0;
        if (dbg != NULL && dbg[0] == 'd' && vdbg < 400) {
            ++vdbg;
            f32 x0 = v[0].sx, x1 = v[0].sx, y0 = v[0].sy, y1 = v[0].sy;
            for (u32 i = 1; i < n; i++) {
                if (v[i].sx < x0) x0 = v[i].sx;
                if (v[i].sx > x1) x1 = v[i].sx;
                if (v[i].sy < y0) y0 = v[i].sy;
                if (v[i].sy > y1) y1 = v[i].sy;
            }
            static int f0dbg2 = 0;
            if (d->vtx_fmt == 0u && f0dbg2 < 2) {
                ++f0dbg2;
                fprintf(stderr,
                        "[raster] F0 vcd=lo %08X hi %08X vat0=%08X %08X %08X "
                        "vs=%u lsize=%u pos(o%u e%u f%u) "
                        "col(o%u e%u f%u) tex0(o%u e%u f%u d%d) "
                        "proj=(%.5f,%.5f,%.5f,%.5f,%.5f,%.5f)\n",
                        g_vcd_lo, g_vcd_hi, g_vat[0][0], g_vat[0][1],
                        g_vat[0][2], d->vertex_size, l.size, l.pos.off,
                        l.pos.elems,
                        l.pos.fmt, l.col0.off, l.col0.elems, l.col0.cformat,
                        l.tex[0].off, l.tex[0].elems, l.tex[0].fmt,
                        (int)l.tex[0].direct, d->projection[0],
                        d->projection[1], d->projection[2], d->projection[3],
                        d->projection[4], d->projection[5]);
                for (u32 i = 0; i < n && i < 4u; i++) {
                    const u8* pp =
                        d->vertex_payload + (size_t)i * d->vertex_size;
                    fprintf(stderr,
                            "[raster] F0 v%u bytes=", i);
                    for (u32 bi = 0; bi < d->vertex_size && bi < 24u; bi++)
                        fprintf(stderr, "%02X", pp[bi]);
                    fprintf(stderr,
                            " f=(%.2f,%.2f,%.2f) screen=(%.1f,%.1f) uv=(%.3f,%.3f) hasuv=%d\n",
                            (double)*(const f32*)(const void*)(pp + 0),
                            (double)*(const f32*)(const void*)(pp + 4),
                            (double)*(const f32*)(const void*)(pp + 8),
                            v[i].sx, v[i].sy, (double)v[i].u, (double)v[i].v,
                            (int)v[i].has_uv);
                }
            }
            fprintf(stderr,
                    "[raster]   bbox=(%.1f,%.1f)-(%.1f,%.1f) n=%u prim=%02X "
                    "fmt=%u vs=%u v0=(%.1f,%.1f) v1=(%.1f,%.1f) v2=(%.1f,%.1f)\n",
                    x0, y0, x1, y1, n, d->primitive & 0xF8u, d->vtx_fmt,
                    d->vertex_size, v[0].sx, v[0].sy,
                    n > 1 ? v[1].sx : 0.f, n > 1 ? v[1].sy : 0.f,
                    n > 2 ? v[2].sx : 0.f, n > 2 ? v[2].sy : 0.f);
        }
    }

    {
        // Diagnostic: snapshot the EFB just before a full-screen quad paints
        // over it, so the content underneath a fade can be inspected.
        const char* pre = getenv("COLORS_FB_PRE_FULL");
        if (pre != NULL && pre[0] != '\0' && n >= 3u) {
            f32 x0 = v[0].sx, x1 = v[0].sx, y0 = v[0].sy, y1 = v[0].sy;
            for (u32 i = 1; i < n; i++) {
                if (v[i].sx < x0) x0 = v[i].sx;
                if (v[i].sx > x1) x1 = v[i].sx;
                if (v[i].sy < y0) y0 = v[i].sy;
                if (v[i].sy > y1) y1 = v[i].sy;
            }
            if (x0 <= 1.f && y0 <= 1.f && x1 >= (f32)EFB_W - 1.f &&
                y1 >= (f32)EFB_H - 1.f) {
                static u32 pn = 0;
                if (pn < 4u && raster_dump_ppm(pre))
                    ++pn;
            }
        }
    }

    const bool textured = l.tex[0].present && l.tex[0].elems >= 2u &&
                          g_tex[0].valid;
    if (textured)
        ++g_stats.textured;

    {
        const char* dbg = getenv("COLORS_RASTER_DEBUG");
        if (dbg != NULL && dbg[0] == 't' && g_tex[0].valid) {
            static int n = 0;
            if (n++ < 5) {
                f32 texel[4] = {0, 0, 0, 0};
                const bool got = tex_sample(0u, v[0].u, v[0].v, texel);
                fprintf(stderr,
                        "[texuse] fmt=%u prim=%02X n=%u vs=%u tex=0x%08X "
                        "%ux%u f%u uv0=(%.3f,%.3f) texel=(%.2f,%.2f,%.2f)\n",
                        d->vtx_fmt, d->primitive & 0xF8u, d->vertex_count,
                        d->vertex_size, g_tex[0].address, g_tex[0].width,
                        g_tex[0].height, g_tex[0].format, v[0].u, v[0].v,
                        texel[0], texel[1], texel[2]);
                (void)got;
            }
        }
        if (dbg != NULL && dbg[0] == 's')
            fprintf(stderr, "[sig] fmt=%u prim=%02X vs=%u n=%u\n", d->vtx_fmt,
                    d->primitive & 0xF8u, d->vertex_size, d->vertex_count);
        if (dbg != NULL && dbg[0] == 'd')
            fprintf(stderr,
                    "[raster] draw prim=0x%X fmt=%u n=%u vs=%u mi=%u mask=0x%X "
                    "vp=(%.1f,%.1f,%.1f,%.1f) proj=(%.4f,%.4f,%.4f,%.4f,%.4f,"
                    "%.4f)\n",
                    d->primitive, d->vtx_fmt, d->vertex_count, d->vertex_size,
                    d->current_pn_matrix, d->position_matrix_valid_mask,
                    d->viewport[0], d->viewport[1], d->viewport[3],
                    d->viewport[4], d->projection[0], d->projection[1],
                    d->projection[2], d->projection[3], d->projection[4],
                    d->projection[5]);
    }

    if (getenv("COLORS_BLOOM_DEBUG") != NULL && g_blend_enable && n >= 3u) {
        f32 x0 = v[0].sx, x1 = v[0].sx, y0 = v[0].sy, y1 = v[0].sy;
        for (u32 i = 1; i < n; i++) {
            if (v[i].sx < x0) x0 = v[i].sx;
            if (v[i].sx > x1) x1 = v[i].sx;
            if (v[i].sy < y0) y0 = v[i].sy;
            if (v[i].sy > y1) y1 = v[i].sy;
        }
        if (x1 - x0 >= (f32)EFB_W * 0.5f && y1 - y0 >= (f32)EFB_H * 0.5f) {
            static int bn = 0;
            if (bn++ < 6) {
                fprintf(stderr,
                        "[bloom] tevvalid=%d stages=%u src=%u dst=%u sub=%d "
                        "colupd=%d alupd=%d | t0 c=%u,%u,%u,%u a=%u,%u,%u,%u "
                        "op=%u/%u bias=%u/%u scale=%u/%u kc=%u/%u\n",
                        (int)g_tev_valid, g_num_tev_stages, g_blend_src,
                        g_blend_dst, (int)g_blend_subtract, (int)g_color_update,
                        (int)g_alpha_update, g_tev[0].c_a, g_tev[0].c_b,
                        g_tev[0].c_c, g_tev[0].c_d, g_tev[0].a_a, g_tev[0].a_b,
                        g_tev[0].a_c, g_tev[0].a_d, g_tev[0].c_op, g_tev[0].a_op,
                        g_tev[0].c_bias, g_tev[0].a_bias, g_tev[0].c_scale,
                        g_tev[0].a_scale, g_tev[0].kc_sel, g_tev[0].ka_sel);
                for (u32 st = 1; st < g_num_tev_stages && st < 3u; st++)
                    fprintf(stderr,
                            "[bloom] t%u c=%u,%u,%u,%u a=%u,%u,%u,%u op=%u/%u "
                            "bias=%u/%u scale=%u/%u kc=%u/%u\n",
                            st, g_tev[st].c_a, g_tev[st].c_b, g_tev[st].c_c,
                            g_tev[st].c_d, g_tev[st].a_a, g_tev[st].a_b,
                            g_tev[st].a_c, g_tev[st].a_d, g_tev[st].c_op,
                            g_tev[st].a_op, g_tev[st].c_bias, g_tev[st].a_bias,
                            g_tev[st].c_scale, g_tev[st].a_scale,
                            g_tev[st].kc_sel, g_tev[st].ka_sel);
            }
        }
    }

    {
        DrawRec* r = &g_draw_ring[g_draw_ring_n % DRAW_RING];
        ++g_draw_ring_n;
        r->fmt = d->vtx_fmt;
        r->prim = d->primitive & 0xF8u;
        r->n = n;
        r->vs = d->vertex_size;
        r->x0 = r->y0 = 1e9f;
        r->x1 = r->y1 = -1e9f;
        for (u32 i = 0; i < n; i++) {
            if (v[i].sx < r->x0) r->x0 = v[i].sx;
            if (v[i].sx > r->x1) r->x1 = v[i].sx;
            if (v[i].sy < r->y0) r->y0 = v[i].sy;
            if (v[i].sy > r->y1) r->y1 = v[i].sy;
        }
        r->col[0] = v[0].r;
        r->col[1] = v[0].g;
        r->col[2] = v[0].b;
        r->col[3] = v[0].a;
        r->tex = g_tex[0].valid ? g_tex[0].address : 0u;
        r->blend = g_blend_enable;
        r->colupd = g_color_update;
    }

    const u32 prim = d->primitive & 0xF8u;
    switch (prim) {
    case 0x80u:  // quads
        for (u32 i = 0; i + 3u < n; i += 4u) {
            emit_tri(&v[i], &v[i + 1u], &v[i + 2u], textured);
            emit_tri(&v[i], &v[i + 2u], &v[i + 3u], textured);
        }
        break;
    case 0x90u:  // triangles
        for (u32 i = 0; i + 2u < n; i += 3u)
            emit_tri(&v[i], &v[i + 1u], &v[i + 2u], textured);
        break;
    case 0x98u:  // triangle strip
        for (u32 i = 0; i + 2u < n; i++) {
            if ((i & 1u) == 0u)
                emit_tri(&v[i], &v[i + 1u], &v[i + 2u], textured);
            else
                emit_tri(&v[i + 1u], &v[i], &v[i + 2u], textured);
        }
        break;
    case 0xA0u:  // triangle fan
        for (u32 i = 1u; i + 1u < n; i++)
            emit_tri(&v[0], &v[i], &v[i + 1u], textured);
        break;
    default:
        break;
    }
    free(v);
    ++g_stats.draws;
}

// ---------------------------------------------------------------------------
// Packet entry points
// ---------------------------------------------------------------------------

void raster_packet_state(const DolAuroraRecompRenderStatePacket* s) {
    if (s == NULL)
        return;
    switch (s->kind) {
    case DOL_AURORA_RECOMP_STATE_CP_VCD:
        if (s->index == 0u) {
            g_vcd_lo = s->value;
            g_vcd_lo_valid = true;
        } else {
            g_vcd_hi = s->value;
            g_vcd_hi_valid = true;
        }
        break;
    case DOL_AURORA_RECOMP_STATE_CP_VAT:
        if (s->index < DOL_GX_RECOMP_VERTEX_FORMATS && s->aux0 < 3u)
            g_vat[s->index][s->aux0] = s->value;
        break;
    case DOL_AURORA_RECOMP_STATE_CP_ARRAY_BASE:
        if (s->index < 16u) {
            g_arr_base[s->index] = s->value;
            g_arr_base_valid[s->index] = true;
        }
        break;
    case DOL_AURORA_RECOMP_STATE_CP_ARRAY_STRIDE:
        if (s->index < 16u)
            g_arr_stride[s->index] = s->value;
        break;
    case DOL_AURORA_RECOMP_STATE_BP_REG:
        if (s->index < 256u) {
            g_bp[s->index] = s->value;
            g_bp_valid[s->index] = true;
            raster_bp(s->index, s->value);
            if (getenv("COLORS_CLEAR_LOG") != NULL)
                fprintf(stderr, "[clear] BP 0x%02X = 0x%06X\n", s->index,
                        s->value);
            if (s->index == 0x4Fu) {  // clear colour AR
                g_clear[0] = (f32)(s->value & 0xFFu) / 255.f;
                g_clear[3] = (f32)((s->value >> 8) & 0xFFu) / 255.f;
            } else if (s->index == 0x50u) {  // clear colour GB
                g_clear[1] = (f32)((s->value >> 8) & 0xFFu) / 255.f;
                g_clear[2] = (f32)(s->value & 0xFFu) / 255.f;
            }
        }
        break;
    default:
        break;
    }
}

static const CopyImage* copy_lookup(u32 address, u32 width, u32 height) {
    for (u32 i = 0; i < COPY_CACHE_SLOTS; i++) {
        const CopyImage* c = &g_copies[i];
        if (c->valid && c->address == address && c->width == width &&
            c->height == height) {
            if (getenv("COLORS_COPY_LOG") != NULL) {
                static u32 hit_seen[32], hit_n;
                bool dup = false;
                for (u32 k = 0; k < hit_n; k++)
                    if (hit_seen[k] == address) dup = true;
                if (!dup && hit_n < 32u) {
                    hit_seen[hit_n++] = address;
                    fprintf(stderr,
                            "[copyhit] addr=0x%08X %ux%u first_px=%02X%02X%02X%02X\n",
                            address, width, height, c->rgba[0], c->rgba[1],
                            c->rgba[2], c->rgba[3]);
                }
            }
            return c;
        }
    }
    if (getenv("COLORS_COPY_LOG") != NULL) {
        static u32 miss_seen[32], miss_n;
        bool dup = false;
        for (u32 k = 0; k < miss_n; k++)
            if (miss_seen[k] == address) dup = true;
        if (!dup && miss_n < 32u) {
            miss_seen[miss_n++] = address;
            fprintf(stderr, "[copymiss] addr=0x%08X %ux%u\n", address, width,
                    height);
        }
    }
    return NULL;
}

static void copy_store(u32 address, u32 width, u32 height, u32 src_x,
                       u32 src_y, bool flip) {
    if (width == 0u || height == 0u || width > EFB_W || height > EFB_H)
        return;
    u8* rgba = (u8*)malloc((size_t)width * height * 4u);
    if (rgba == NULL)
        return;
    for (u32 y = 0; y < height; y++) {
        const u32 sy = src_y + (flip ? (height - 1u - y) : y);
        for (u32 x = 0; x < width; x++) {
            const u32 sx = src_x + x;
            u8* dst = rgba + ((size_t)y * width + x) * 4u;
            if (sx >= EFB_W || sy >= EFB_H) {
                dst[0] = dst[1] = dst[2] = 0;
                dst[3] = 255;
                continue;
            }
            const u8* src = g_color + ((size_t)sy * EFB_W + sx) * 4u;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
        }
    }
    // Replace an existing image for this address, else take the next slot.
    CopyImage* slot = NULL;
    for (u32 i = 0; i < COPY_CACHE_SLOTS; i++) {
        if (g_copies[i].valid && g_copies[i].address == address) {
            slot = &g_copies[i];
            break;
        }
    }
    if (slot == NULL) {
        slot = &g_copies[g_copy_next];
        g_copy_next = (g_copy_next + 1u) % COPY_CACHE_SLOTS;
    }
    free(slot->rgba);
    slot->valid = true;
    slot->address = address;
    slot->width = width;
    slot->height = height;
    slot->rgba = rgba;
}

// Apply a BP register write to the pixel pipeline state.
static void raster_bp(u32 reg, u32 v) {
    if (reg >= 0xC0u && reg <= 0xDFu) {
        const u32 stage = (reg - 0xC0u) / 2u;
        if (stage >= TEV_MAX_STAGES)
            return;
        TevStage* t = &g_tev[stage];
        if ((reg & 1u) == 0u) {
            // Dolphin TevStageCombiner::ColorCombiner:
            // d[3:0] c[7:4] b[11:8] a[15:12], bias[17:16] op[18]
            // clamp[19] scale[21:20] dest[23:22].
            t->c_d = v & 0xFu;
            t->c_c = (v >> 4) & 0xFu;
            t->c_b = (v >> 8) & 0xFu;
            t->c_a = (v >> 12) & 0xFu;
            t->c_bias = (v >> 16) & 3u;
            t->c_op = (v >> 18) & 1u;
            t->c_scale = (v >> 20) & 3u;
            if (getenv("COLORS_TEV_NOSCALE") != NULL)
                t->c_scale = 0u;
            if (getenv("COLORS_TEV_X2") != NULL)
                t->c_scale = 1u;
            if (getenv("COLORS_TEV_NOBIAS") != NULL)
                t->c_bias = 0u;
        } else {
            // Dolphin TevStageCombiner::AlphaCombiner:
            // rswap[1:0] tswap[3:2] d[6:4] c[9:7] b[12:10] a[15:13], then
            // bias[17:16] op[18] clamp[19] scale[21:20] dest[23:22].
            t->a_d = (v >> 4) & 7u;
            t->a_c = (v >> 7) & 7u;
            t->a_b = (v >> 10) & 7u;
            t->a_a = (v >> 13) & 7u;
            t->a_bias = (v >> 16) & 3u;
            t->a_op = (v >> 18) & 1u;
            t->a_scale = (v >> 20) & 3u;
        }
        g_tev_valid = true;
        return;
    }
    if (reg >= 0x28u && reg <= 0x2Fu) {
        const u32 idx = reg - 0x28u;
        for (u32 half = 0; half < 2u; half++) {
            const u32 st = idx * 2u + half;
            if (st >= TEV_MAX_STAGES)
                break;
            const u32 shift = half * 12u;
            const u32 map = (v >> (shift + 0u)) & 7u;
            const bool enable = ((v >> (shift + 6u)) & 1u) != 0u;
            g_tev[st].tex_map = map;
            g_tev[st].tex_enabled = enable && map != 7u;
        }
        return;
    }
    if (reg >= 0xF6u && reg <= 0xFDu) {
        const u32 idx = reg - 0xF6u;
        const u32 st0 = idx * 2u;
        if (st0 < TEV_MAX_STAGES) {
            g_tev[st0].kc_sel = (v >> 4) & 0x1Fu;
            g_tev[st0].ka_sel = (v >> 9) & 0x1Fu;
        }
        if (st0 + 1u < TEV_MAX_STAGES) {
            g_tev[st0 + 1u].kc_sel = (v >> 14) & 0x1Fu;
            g_tev[st0 + 1u].ka_sel = (v >> 19) & 0x1Fu;
        }
        return;
    }
    if (reg >= 0xE0u && reg <= 0xE7u) {
        const u32 idx = (reg - 0xE0u) / 2u;
        const bool is_ra = (reg & 1u) == 0u;
        if (((v >> 23) & 1u) != 0u) {  // K colour register
            if (idx < 4u) {
                if (is_ra) {
                    g_kcolor[idx][0] = (f32)(v & 0xFFu) / 255.f;
                    g_kcolor[idx][3] = (f32)((v >> 12) & 0xFFu) / 255.f;
                } else {
                    g_kcolor[idx][2] = (f32)(v & 0xFFu) / 255.f;
                    g_kcolor[idx][1] = (f32)((v >> 12) & 0xFFu) / 255.f;
                }
            }
        } else if (idx < 3u) {  // TEV colour register C0..C2 (11-bit signed)
            const s32 r = (s32)(v << 21) >> 21;
            const s32 a = (s32)(v << 9) >> 21;
            if (is_ra) {
                g_tevreg[idx][0] = (f32)r / 1024.f;
                g_tevreg[idx][3] = (f32)a / 1024.f;
            } else {
                const s32 b = (s32)(v << 21) >> 21;
                const s32 g = (s32)(v << 9) >> 21;
                g_tevreg[idx][2] = (f32)b / 1024.f;
                g_tevreg[idx][1] = (f32)g / 1024.f;
            }
        }
        return;
    }
    switch (reg) {
    case 0x00:  // genMode
        g_num_tev_stages = ((v >> 10) & 0xFu) + 1u;
        if (g_num_tev_stages > TEV_MAX_STAGES)
            g_num_tev_stages = TEV_MAX_STAGES;
        break;
    case 0x20:  // scissor TL
    case 0x21: {  // scissor BR
        // Dolphin ScissorPos: y is bits 0-10, x is bits 12-22 (both +342).
        static u32 tl_x, tl_y, br_x, br_y;
        const u32 y = v & 0x7FFu;
        const u32 x = (v >> 12) & 0x7FFu;
        if (reg == 0x20u) {
            tl_x = x; tl_y = y;
        } else {
            br_x = x; br_y = y;
        }
        if (tl_x >= 342u && tl_y >= 342u && br_x >= 342u && br_y >= 342u) {
            g_scissor_x0 = tl_x - 342u;
            g_scissor_y0 = tl_y - 342u;
            g_scissor_x1 = br_x - 342u;
            g_scissor_y1 = br_y - 342u;
            g_scissor_valid = g_scissor_x0 <= g_scissor_x1 &&
                              g_scissor_y0 <= g_scissor_y1;
        }
        break;
    }
    case 0x40:  // zMode
        g_depth_test = (v & 1u) != 0u;
        g_depth_func = (v >> 1) & 7u;
        g_depth_update = ((v >> 4) & 1u) != 0u;
        break;
    case 0x41:  // blend / ROP
        g_blend_enable = (v & 1u) != 0u;
        g_logic_enable = ((v >> 1) & 1u) != 0u;
        g_color_update = ((v >> 3) & 1u) != 0u;
        g_alpha_update = ((v >> 4) & 1u) != 0u;
        g_blend_dst = (v >> 5) & 7u;
        g_blend_src = (v >> 8) & 7u;
        g_blend_subtract = ((v >> 11) & 1u) != 0u;
        break;
    default:
        break;
    }
}

static void free_texture(Tex* t) {
    free(t->rgba);
    memset(t, 0, sizeof(*t));
}

// GXRuntime's C render packet keeps the resource dimensions and the EFB-copy
// source rect in the trace event that produced the packet (the C++ sink struct
// carries them as fields, the C mirror only carries `event`). Decode them back
// here so the rasteriser sees the same values the packet used to expose:
//   TEXTURE           event.e = width,  event.f = height
//   COPY_DESTINATION  event.g = width << 16 | height
//                     event.e/f = source x/y, event.d = post-copy clear
void raster_packet_resource(const DolAuroraRecompRenderPacket* packet) {
    if (packet == NULL)
        return;
    const DolAuroraRecompRenderResourcePacket* r = &packet->resource;
    const DolGxRecompTraceEvent* ev = &packet->event;
    u32 res_w = 0;
    u32 res_h = 0;
    if (r->kind == DOL_AURORA_RECOMP_RESOURCE_TEXTURE) {
        res_w = ev->e;
        res_h = ev->f;
    } else if (r->kind == DOL_AURORA_RECOMP_RESOURCE_COPY_DESTINATION) {
        res_w = ev->g >> 16;
        res_h = ev->g & 0xFFFFu;
    }
    switch (r->kind) {
    case DOL_AURORA_RECOMP_RESOURCE_TEXTURE: {
        if (r->index >= DOL_GX_RECOMP_TEXTURE_SLOTS)
            return;
        {
            static int dbg = 0;
            if (getenv("COLORS_TEX_DEBUG") != NULL && dbg < 200000) {
                ++dbg;
                fprintf(stderr,
                        "[tex] slot=%u addr=0x%08X size=%u fmt=%u %ux%u "
                        "tlut=0x%08X/%u/%u\n",
                        r->index, r->address, r->size, r->format, res_w,
                        res_h, ev->tlut_address, ev->tlut_format,
                        ev->tlut_entries);
            }
        }
        Tex* t = &g_tex[r->index];
        DolGuestResolvedRange range;
        if (!gx_resolve_range(r->address, r->size,
                              DOL_GUEST_RESOURCE_TEXTURE, &range))
            return;
        // A texture written by an EFB copy: take the cached pixels.
        const CopyImage* copied = copy_lookup(r->address, res_w, res_h);
        if (copied != NULL) {
            free_texture(t);
            t->valid = true;
            t->address = r->address;
            t->width = res_w;
            t->height = res_h;
            t->format = r->format;
            t->data = (const u8*)range.data;
            t->data_size = range.available < r->size ? range.available : r->size;
            const size_t need = (size_t)res_w * res_h * 4u;
            t->rgba = (u8*)malloc(need);
            if (t->rgba != NULL)
                memcpy(t->rgba, copied->rgba, need);
            break;
        }
        // Reuse the decoded image when the binding did not change. MEM1-backed
        // textures are written by the CPU every frame (the Sofdec movie
        // decoder), so their decode must not be cached across binds.
        const bool volatile_mem1 = r->address < 0x01800000u;
        const bool same = t->valid && t->data == (const u8*)range.data &&
                          t->width == res_w && t->height == res_h &&
                          t->format == r->format;
        if (!same || (volatile_mem1 && t->rgba != NULL))
            free_texture(t);
        t->valid = true;
        t->address = r->address;
        t->width = res_w;
        t->height = res_h;
        t->format = r->format;
        t->data = (const u8*)range.data;
        t->data_size = range.available < r->size ? range.available : r->size;
        t->tlut_format = ev->tlut_format;
        t->tlut_entries = ev->tlut_entries;
        if (ev->tlut_address != 0u && ev->tlut_entries != 0u) {
            DolGuestResolvedRange pal;
            if (gx_resolve_range(ev->tlut_address, ev->tlut_entries * 2u,
                                 DOL_GUEST_RESOURCE_TLUT, &pal)) {
                t->tlut = (const u8*)pal.data;
                t->tlut_size = pal.available;
            }
        }
        break;
    }
    case DOL_AURORA_RECOMP_RESOURCE_COPY_DESTINATION:
        if (getenv("COLORS_SEQ") != NULL && g_seq < 400)
            fprintf(stderr, "[seq] copy addr=%08X %ux%u fmt=%u clear=%u\n",
                    r->address, res_w, res_h, r->format, ev->d);
        if (r->format != 15u && res_w != 0u && res_h != 0u &&
            r->size != 0u)
            copy_store(r->address, res_w, res_h, ev->e,
                       ev->f, true);
        {
            static int dbg = 0;
            if (getenv("COLORS_TEX_DEBUG") != NULL && dbg < 200000) {
                ++dbg;
                fprintf(stderr,
                        "[copy] addr=0x%08X size=%u fmt=%u %ux%u src=(%u,%u) "
                        "clear=%u\n",
                        r->address, r->size, r->format, res_w, res_h,
                        ev->e, ev->f, ev->d);
            }
        }
        // The EFB copy itself still needs a readback path; the post-copy clear
        // is cheap and keeps frames from accumulating.
        if (ev->d != 0u) {
            if (getenv("COLORS_FRAME_LOG") != NULL) {
                static u32 frames = 0;
                ++frames;
                if ((frames % 200u) == 0u) {
                    const u32 shown =
                        g_draw_ring_n < DRAW_RING ? g_draw_ring_n : DRAW_RING;
                    const u32 base = g_draw_ring_n - shown;
                    fprintf(stderr, "[frame] %u draws (last %u)\n",
                            g_draw_ring_n, shown);
                    for (u32 i = 0; i < shown; i++) {
                        const DrawRec* r = &g_draw_ring[(base + i) % DRAW_RING];
                        fprintf(stderr,
                                "[frame]  %2u fmt=%u prim=%02X n=%u vs=%u "
                                "bbox=(%.0f,%.0f)-(%.0f,%.0f) col=(%.2f,%.2f,"
                                "%.2f,%.2f) tex=%08X blend=%u colupd=%u\n",
                                i, r->fmt, r->prim, r->n, r->vs, r->x0, r->y0,
                                r->x1, r->y1, r->col[0], r->col[1], r->col[2],
                                r->col[3], r->tex, r->blend, r->colupd);
                    }
                }
                g_draw_ring_n = 0;
            }
            if (getenv("COLORS_BRIGHTNESS") != NULL) {
                static u32 bn = 0;
                if ((bn++ % 10u) == 0u) {
                    u64 sum = 0;
                    u32 mx = 0, nonblack = 0;
                    for (u32 i = 0; i < EFB_W * EFB_H; i++) {
                        const u32 lum = (g_color[i * 4u] + g_color[i * 4u + 1u] +
                                         g_color[i * 4u + 2u]) / 3u;
                        sum += lum;
                        if (lum > mx) mx = lum;
                        if (lum != 0u) nonblack++;
                    }
                    u32 dark = 0;
                    for (u32 i = 0; i < EFB_W * EFB_H; i++) {
                        const u32 lum = (g_color[i * 4u] + g_color[i * 4u + 1u] +
                                         g_color[i * 4u + 2u]) / 3u;
                        if (lum < 128u) dark++;
                    }
                    fprintf(stderr,
                            "[bright] frame=%u mean=%.1f max=%u nonblack=%u "
                            "dark=%u\n",
                            bn, (double)sum / (double)(EFB_W * EFB_H), mx,
                            nonblack, dark);
                }
            }
            if (getenv("COLORS_FB_STATS") != NULL) {
                u32 nonblack = 0;
                for (u32 i = 0; i < EFB_W * EFB_H; i++)
                    if (g_color[i * 4u] || g_color[i * 4u + 1u] ||
                        g_color[i * 4u + 2u])
                        nonblack++;
                fprintf(stderr, "[present] #%u draws=%u nonblack=%u\n",
                        g_stats.dumps, g_stats.draws, nonblack);
            }
            // Present the completed frame to the host window (no-op when the
            // emulator runs headless).
            window_present(g_color, EFB_W, EFB_H);
            (void)window_poll();
            const char* dump = getenv("COLORS_FB_DUMP");
            if (dump != NULL && dump[0] != '\0') {
                if (raster_dump_ppm(dump))
                    g_stats.dumps++;
            }
            const char* ring = getenv("COLORS_FB_RING");
            if (ring != NULL && ring[0] != '\0') {
                static u32 ring_n = 0;
                char path[512];
                snprintf(path, sizeof(path), "%s_%u.ppm", ring, ring_n % 8u);
                (void)raster_dump_ppm(path);
                ++ring_n;
            }
            const char* series = getenv("COLORS_FB_SERIES");
            if (series != NULL && series[0] != '\0' && g_stats.dumps < 40u) {
                char path[512];
                snprintf(path, sizeof(path), "%s_%02u.ppm", series,
                         g_stats.dumps);
                if (raster_dump_ppm(path))
                    g_stats.dumps++;
            }
            for (u32 i = 0; i < EFB_W * EFB_H; i++) {
                g_color[i * 4u + 0u] = (u8)(g_clear[0] * 255.f + 0.5f);
                g_color[i * 4u + 1u] = (u8)(g_clear[1] * 255.f + 0.5f);
                g_color[i * 4u + 2u] = (u8)(g_clear[2] * 255.f + 0.5f);
                g_color[i * 4u + 3u] = (u8)(g_clear[3] * 255.f + 0.5f);
                g_depth[i] = 0.f;
            }
            ++g_stats.clears;
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void raster_init(void) {
    if (g_ready)
        return;
    memset(g_color, 0, sizeof(g_color));
    for (u32 i = 0; i < EFB_W * EFB_H; i++)
        g_depth[i] = 0.f;
    memset(&g_stats, 0, sizeof(g_stats));
    g_opt_no_depth = getenv("COLORS_RASTER_NO_DEPTH") != NULL;
    g_opt_no_blend = getenv("COLORS_RASTER_NO_BLEND") != NULL;
    g_opt_white_tex = getenv("COLORS_RASTER_WHITE_TEX") != NULL;
    g_opt_tev_ras = getenv("COLORS_TEV_RAS") != NULL;
    g_opt_tev_force = getenv("COLORS_TEV_FORCE") != NULL;
    g_opt_force_tex_pass = getenv("COLORS_FORCE_TEX_PASS") != NULL;
    g_opt_tex_magenta = getenv("COLORS_TEX_MAGENTA") != NULL;
    g_opt_uv_pixels = getenv("COLORS_UV_PIXELS") != NULL;
    g_opt_path_stats = getenv("COLORS_PATH_STATS") != NULL;
    {
        const char* fv = getenv("COLORS_FORCE_VS_SIZE");
        if (fv != NULL)
            g_force_vs = (u32)strtoul(fv, NULL, 0);
    }
    g_ready = 1;
}

static void raster_dump_pipe(void) {
    if (getenv("COLORS_BP_DEBUG") == NULL)
        return;
    fprintf(stderr,
            "[pipe] stages=%u tev_valid=%d blend=%d src=%u dst=%u sub=%d "
            "colupd=%d alupd=%d depth_test=%d update=%d func=%u scissor=%d "
            "(%u,%u)-(%u,%u)\n",
            g_num_tev_stages, (int)g_tev_valid, (int)g_blend_enable,
            g_blend_src, g_blend_dst, (int)g_blend_subtract,
            (int)g_color_update, (int)g_alpha_update, (int)g_depth_test,
            (int)g_depth_update, g_depth_func, (int)g_scissor_valid,
            g_scissor_x0, g_scissor_y0, g_scissor_x1, g_scissor_y1);
    for (u32 i = 0; i < g_num_tev_stages && i < 4u; i++) {
        const TevStage* t = &g_tev[i];
        fprintf(stderr,
                "[pipe]  tev%u tex=%d map=%u c=%u,%u,%u,%u op=%u bias=%u "
                "scale=%u | a=%u,%u,%u,%u op=%u bias=%u scale=%u kc=%u ka=%u\n",
                i, (int)t->tex_enabled, t->tex_map, t->c_a, t->c_b, t->c_c,
                t->c_d, t->c_op, t->c_bias, t->c_scale, t->a_a, t->a_b, t->a_c,
                t->a_d, t->a_op, t->a_bias, t->a_scale, t->kc_sel, t->ka_sel);
    }
    for (u32 i = 0; i < 4u; i++)
        fprintf(stderr, "[pipe]  k%u=(%.2f,%.2f,%.2f,%.2f)\n", i,
                g_kcolor[i][0], g_kcolor[i][1], g_kcolor[i][2], g_kcolor[i][3]);
}

void raster_shutdown(void) {
    if (getenv("COLORS_RASTER_PROF") != NULL)
        fprintf(stderr, "[prof] raster_tri=%.3fs\n", g_prof_tri);
    if (g_opt_path_stats) {
        fprintf(stderr,
                "[paths] tex=%llu ras=%llu mod=%llu generic=%llu notev=%llu "
                "lerp=%llu texd=%llu rasd=%llu\n",
                g_path_px[0], g_path_px[1], g_path_px[2], g_path_px[3],
                g_path_px[4], g_path_px[5], g_path_px[6], g_path_px[7]);
        for (u32 rank = 0; rank < 12u; rank++) {
            u32 best = 0;
            unsigned long long bestn = 0;
            for (u32 i = 0; i < 65536u; i++)
                if (g_combo_px[i] > bestn) { bestn = g_combo_px[i]; best = i; }
            if (bestn == 0) break;
            fprintf(stderr, "[combo] a=%u b=%u c=%u d=%u px=%llu\n",
                    best >> 12, (best >> 8) & 0xF, (best >> 4) & 0xF, best & 0xF,
                    bestn);
            g_combo_px[best] = 0;
        }
        for (u32 i = 0; i < 9u; i++)
            if (g_stage_px[i])
                fprintf(stderr, "[stages] n=%u px=%llu\n", i, g_stage_px[i]);
    }
    raster_dump_pipe();
    for (u32 i = 0; i < DOL_GX_RECOMP_TEXTURE_SLOTS; i++)
        free_texture(&g_tex[i]);
    for (u32 i = 0; i < COPY_CACHE_SLOTS; i++) {
        free(g_copies[i].rgba);
        memset(&g_copies[i], 0, sizeof(g_copies[i]));
    }
    g_ready = 0;
}

bool raster_dump_ppm(const char* path) {
    if (!g_ready || path == NULL || path[0] == '\0')
        return false;
    FILE* f = fopen(path, "wb");
    if (f == NULL)
        return false;
    fprintf(f, "P6\n%u %u\n255\n", EFB_W, EFB_H);
    for (u32 i = 0; i < EFB_W * EFB_H; i++) {
        const u8 rgb[3] = {g_color[i * 4u + 0u], g_color[i * 4u + 1u],
                           g_color[i * 4u + 2u]};
        if (fwrite(rgb, 1, 3, f) != 3) {
            fclose(f);
            return false;
        }
    }
    fclose(f);
    return true;
}

void raster_get_stats(RasterStats* out) {
    if (out != NULL)
        *out = g_stats;
}

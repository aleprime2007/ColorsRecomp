// C++ bridge onto GXRuntime's retail GX frontend (see gx_frontend.h).
//
// GXRuntime itself is consumed unmodified. Two host-side gaps are closed here:
//
//  * Packet drain mode. GXRuntime's C flush leaves it off, which is only valid
//    for offline FIFO replay; a live sink then re-walks the retained trace ring
//    on every flush. set_packet_drain_enabled(true) is what the C++ frontend
//    exposes for live use.
//
//  * Desync recovery. Retail streams can write vertices wider than the declared
//    VCD/VAT, so the decoder consumes a draw with too small a stride and lands
//    inside the vertex payload; it then rejects the next "command". GXRuntime
//    keeps the rejected bytes and offers no way to drop a prefix, so this bridge
//    owns the pending byte stream: on a decode failure it restores the decoder
//    state captured at the failure point and retries from successive later byte
//    offsets until the stream parses again. That is the host-side equivalent of
//    the byte-wise resync the previous runtime performed internally.

#include "gx_frontend.h"

#include "gxruntime/aurora_recomp/retail_gx_frontend.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace {


namespace {

// ---------------------------------------------------------------------------
// Top-level GX FIFO command lengths (mirrors GXRuntime's parse_stream so the
// host can split the stream into complete commands).
// ---------------------------------------------------------------------------
bool is_draw_opcode(std::uint8_t cmd) {
    switch (cmd & 0xF8u) {
    case 0x80u: case 0x90u: case 0x98u: case 0xA0u:
    case 0xA8u: case 0xB0u: case 0xB8u:
        return true;
    default:
        return false;
    }
}

std::uint16_t be16(const std::uint8_t* p) {
    return (std::uint16_t)(((std::uint16_t)p[0] << 8) | p[1]);
}
std::uint32_t be32(const std::uint8_t* p) {
    return ((std::uint32_t)p[0] << 24) | ((std::uint32_t)p[1] << 16) |
           ((std::uint32_t)p[2] << 8) | p[3];
}

enum class PlanKind { kCommand, kDraw, kUnknown, kTruncated };

struct CommandPlan {
    PlanKind kind = PlanKind::kUnknown;
    std::size_t len = 0;
    std::uint8_t fmt = 0;
    std::uint32_t count = 0;
    std::uint32_t vsize = 0;   // chosen vertex stride for a draw
};

// Length of one command at `p`, using `vsize_for` to size draws.
CommandPlan plan_command(const std::uint8_t* p, std::size_t avail,
                         const DolGxRecompState& state,
                         const std::uint32_t* vsize_override) {
    CommandPlan plan;
    if (avail == 0u)
        return plan;
    const std::uint8_t cmd = p[0];
    if (cmd == 0x00u) { plan.kind = PlanKind::kCommand; plan.len = 1u; return plan; }
    if (cmd == 0x08u) { plan.kind = PlanKind::kCommand; plan.len = 6u; return plan; }
    if (cmd == 0x10u) {
        if (avail < 5u) { plan.kind = PlanKind::kTruncated; return plan; }
        const std::uint32_t header = be32(p + 1);
        const std::uint32_t count = ((header >> 16) & 0xFFFFu) + 1u;
        plan.kind = PlanKind::kCommand;
        plan.len = 5u + (std::size_t)count * 4u;
        return plan;
    }
    if (cmd == 0x20u || cmd == 0x28u || cmd == 0x30u || cmd == 0x38u) {
        plan.kind = PlanKind::kCommand; plan.len = 5u; return plan;
    }
    if (cmd == 0x40u) { plan.kind = PlanKind::kCommand; plan.len = 9u; return plan; }
    if (cmd == 0x48u) { plan.kind = PlanKind::kCommand; plan.len = 1u; return plan; }
    if (cmd == 0x61u) { plan.kind = PlanKind::kCommand; plan.len = 5u; return plan; }
    if (is_draw_opcode(cmd)) {
        if (avail < 3u) { plan.kind = PlanKind::kTruncated; return plan; }
        const std::uint8_t fmt = cmd & 0x7u;
        if (fmt >= DOL_GX_RECOMP_VERTEX_FORMATS)
            return plan;
        std::uint32_t vsize = 0;
        if (vsize_override != nullptr)
            vsize = *vsize_override;
        else if (state.vertex_layouts[fmt].valid)
            vsize = state.vertex_layouts[fmt].vertex_size;
        if (vsize == 0u)
            return plan;
        const std::uint32_t count = be16(p + 1);
        const std::size_t len = 3u + (std::size_t)count * vsize;
        plan.kind = PlanKind::kDraw;
        plan.len = len;
        plan.fmt = fmt;
        plan.count = count;
        plan.vsize = vsize;
        return plan;
    }
    return plan;
}

// Does the stream from `p` decode cleanly for a few commands with this draw
// stride? Used to recover retail streams that write wider vertices than the
// VCD/VAT declares (an attribute the title never enables but does emit).
bool looks_parseable(const std::uint8_t* p, std::size_t avail,
                     const DolGxRecompState& state,
                     const std::uint32_t* first_vsize) {
    std::size_t pos = 0;
    int commands = 0;
    bool first = true;
    while (commands < 4) {
        if (pos >= avail)
            return true;   // ran out of evidence, not a contradiction
        const CommandPlan plan =
            plan_command(p + pos, avail - pos, state,
                         (first && first_vsize != nullptr) ? first_vsize
                                                           : nullptr);
        first = false;
        if (plan.kind == PlanKind::kTruncated)
            return true;
        if (plan.kind == PlanKind::kUnknown)
            return false;
        const std::size_t next = pos + plan.len;
        if (next > avail)
            return true;
        pos = next;
        ++commands;
    }
    return true;
}

}  // namespace

class ColorsSink final : public gxruntime::aurora_recomp::AuroraRenderSink {
public:
    ColorsSink(ColorsGxPacketFn submit, void* user)
        : submit_(submit), user_(user) {}

    bool submit_packet(
        const gxruntime::aurora_recomp::RenderPacket& packet) override {
        if (submit_ == nullptr)
            return true;
        DolAuroraRecompRenderPacket c_packet{};
        c_packet.kind = static_cast<u32>(packet.kind);
        c_packet.sequence = packet.sequence;
        c_packet.event = packet.event;

        c_packet.stream.kind = static_cast<u32>(packet.stream.kind);
        c_packet.stream.address = packet.stream.address;
        c_packet.stream.size = packet.stream.size;
        c_packet.stream.address_space = packet.stream.address_space;
        c_packet.stream.total_size = packet.stream.total_size;

        c_packet.state.kind = static_cast<u32>(packet.state.kind);
        c_packet.state.index = packet.state.index;
        c_packet.state.value = packet.state.value;
        c_packet.state.aux0 = packet.state.aux0;
        c_packet.state.aux1 = packet.state.aux1;

        c_packet.resource.kind = static_cast<u32>(packet.resource.kind);
        c_packet.resource.index = packet.resource.index;
        c_packet.resource.address = packet.resource.address;
        c_packet.resource.size = packet.resource.size;
        c_packet.resource.format = packet.resource.format;
        c_packet.resource.count = packet.resource.count;
        c_packet.resource.vtx_fmt = packet.resource.vtx_fmt;
        c_packet.resource.vertex_offset = packet.resource.vertex_offset;
        c_packet.resource.index_size = packet.resource.index_size;
        c_packet.resource.element_size = packet.resource.element_size;

        c_packet.draw.primitive = packet.draw.primitive;
        c_packet.draw.vtx_fmt = packet.draw.vtx_fmt;
        c_packet.draw.vertex_count = packet.draw.vertex_count;
        c_packet.draw.vertex_size = packet.draw.vertex_size;
        c_packet.draw.vertex_payload = packet.draw.vertex_payload;
        c_packet.draw.vertex_payload_size = packet.draw.vertex_payload_size;
        c_packet.draw.transform_flags = packet.draw.transform_flags;
        c_packet.draw.current_pn_matrix = packet.draw.current_pn_matrix;
        c_packet.draw.payload_pn_matrix_mask =
            packet.draw.payload_pn_matrix_mask;
        c_packet.draw.position_matrix_valid_mask =
            packet.draw.position_matrix_valid_mask;
        c_packet.draw.projection_type = packet.draw.projection_type;
        std::memcpy(c_packet.draw.viewport, packet.draw.viewport,
                    sizeof(c_packet.draw.viewport));
        std::memcpy(c_packet.draw.projection, packet.draw.projection,
                    sizeof(c_packet.draw.projection));
        std::memcpy(c_packet.draw.position_matrices,
                    packet.draw.position_matrices,
                    sizeof(c_packet.draw.position_matrices));
        return submit_(user_, &c_packet);
    }

private:
    ColorsGxPacketFn submit_;
    void* user_;
};

// How far past the rejected byte a resync may search. A mis-strided draw can
// push the parser one vertex payload's worth of bytes into the stream
// (vertex_size delta * vertex_count), so allow a few hundred bytes.
constexpr size_t kResyncSearchBytes = 1024u;

}  // namespace

struct ColorsGxFrontend;

// Replace the decoder with a fresh one carrying the same register state, which
// clears its internal byte buffer without discarding the GX state.
static void drop_pending_keep_state(ColorsGxFrontend& frontend);

struct ColorsGxFrontend {
    gxruntime::aurora_recomp::RetailGxFrontend impl;
    DolGuestAddressResolver resolver{};
    bool has_resolver = false;
    // Bytes handed to the host bridge that the decoder has not accepted yet.
    std::vector<std::uint8_t> pending;
    u64 resyncs = 0;

    // Keep only what the decoder still holds (partial trailing command).
    void consume() {
        const size_t left = impl.pending_fifo_size();
        if (left >= pending.size())
            pending.clear();
        else
            pending.erase(pending.begin(),
                          pending.begin() +
                              static_cast<std::ptrdiff_t>(pending.size() - left));
    }
};

ColorsGxFrontend* colors_gx_frontend_create(
    const DolGuestAddressResolver* resolver) {
    auto* frontend = new (std::nothrow) ColorsGxFrontend;
    if (frontend == nullptr)
        return nullptr;
    if (resolver != nullptr) {
        frontend->resolver = *resolver;
        frontend->has_resolver = true;
    }
    frontend->impl.reset(resolver);
    frontend->impl.set_packet_drain_enabled(true);
    return frontend;
}

void colors_gx_frontend_destroy(ColorsGxFrontend* frontend) {
    delete frontend;
}

void colors_gx_frontend_reset(ColorsGxFrontend* frontend,
                              const DolGuestAddressResolver* resolver) {
    if (frontend == nullptr)
        return;
    if (resolver != nullptr) {
        frontend->resolver = *resolver;
        frontend->has_resolver = true;
    }
    frontend->pending.clear();
    frontend->impl.reset(frontend->has_resolver ? &frontend->resolver
                                                : nullptr);
    frontend->impl.set_packet_drain_enabled(true);
}

// Diagnostic: COLORS_FORCE_VS=<fmt>:<size>[,<fmt>:<size>...] pins a vertex
// stride, to test a layout derivation hypothesis without touching GXRuntime.
static void apply_forced_vs(ColorsGxFrontend* frontend) {
    static bool done = false;
    if (done || frontend == nullptr)
        return;
    const char* env = getenv("COLORS_FORCE_VS");
    if (env == nullptr)
        return;
    done = true;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", env);
    char* save = nullptr;
    for (char* tok = strtok_r(buf, ",", &save); tok != nullptr;
         tok = strtok_r(nullptr, ",", &save)) {
        unsigned fmt = 0, size = 0;
        if (sscanf(tok, "%u:%u", &fmt, &size) == 2 && fmt < 8u && size != 0u) {
            frontend->impl.set_vertex_layout((std::uint8_t)fmt,
                                             (std::uint32_t)size);
            fprintf(stderr, "[vs] forced fmt=%u size=%u\n", fmt, size);
        }
    }
}

bool colors_gx_frontend_set_vertex_layout(ColorsGxFrontend* frontend,
                                          u8 vtx_fmt, u32 vertex_size) {
    return frontend != nullptr &&
           frontend->impl.set_vertex_layout(vtx_fmt, vertex_size);
}

bool colors_gx_frontend_derive_vertex_layout(ColorsGxFrontend* frontend,
                                             u8 vtx_fmt) {
    return frontend != nullptr && frontend->impl.derive_vertex_layout(vtx_fmt);
}

bool colors_gx_frontend_set_cp_array(ColorsGxFrontend* frontend, u8 attr,
                                     u32 physical_base, u8 stride) {
    return frontend != nullptr &&
           frontend->impl.set_cp_array(attr, physical_base, stride);
}

bool colors_gx_frontend_load_cp_reg(ColorsGxFrontend* frontend, u8 reg,
                                    u32 value) {
    return frontend != nullptr && frontend->impl.load_cp_reg(reg, value);
}

bool colors_gx_frontend_write_fifo(ColorsGxFrontend* frontend,
                                   const void* bytes, size_t size) {
    if (frontend == nullptr || bytes == nullptr || size == 0u)
        return false;
    const auto* p = static_cast<const std::uint8_t*>(bytes);
    frontend->pending.insert(frontend->pending.end(), p, p + size);
    // Feed immediately; `pending` mirrors what the decoder still holds.
    return frontend->impl.write_fifo(
        std::span<const std::uint8_t>{p, size});
}

// Retail GX streams can write vertices wider than the VCD/VAT declares (an
// attribute the title never enables but does emit). When that happens the
// decoder consumes the draw with too small a stride, lands inside the payload
// and rejects the following "command". Walk the buffered commands and, for any
// draw whose declared stride does not continue cleanly, adopt the smallest
// wider stride that does - the same recovery the previous runtime performed.
static void recover_vertex_strides(ColorsGxFrontend& frontend) {
    std::size_t pos = 0;
    int guard = 0;
    int fixes = 0;
    while (pos < frontend.pending.size() && guard++ < 4096) {
        const DolGxRecompState& st = frontend.impl.state();
        const std::uint8_t* p = frontend.pending.data() + pos;
        const std::size_t avail = frontend.pending.size() - pos;
        CommandPlan plan = plan_command(p, avail, st, nullptr);
        if (plan.kind == PlanKind::kTruncated || plan.kind == PlanKind::kUnknown) {
            if (getenv("COLORS_STRIDE_DEBUG") != nullptr) {
                static int why = 0;
                if (why++ < 8)
                    fprintf(stderr,
                            "[stride] stop at pos=%zu/%zu kind=%d first=%02X "
                            "cmd=%02X\n",
                            pos, frontend.pending.size(), (int)plan.kind,
                            p[0], p[0]);
            }
            return;
        }
        if (plan.kind == PlanKind::kDraw && plan.count > 0u &&
            plan.vsize != 0u) {
            if (!looks_parseable(p, avail, st, nullptr)) {
                for (std::uint32_t extra = 1; extra <= 12u; ++extra) {
                    const std::uint32_t cand = plan.vsize + extra;
                    if (!looks_parseable(p, avail, st, &cand))
                        continue;
                    frontend.impl.state().vertex_layouts[plan.fmt].vertex_size =
                        cand;
                    ++fixes;
                    if (getenv("COLORS_STRIDE_DEBUG") != nullptr)
                        fprintf(stderr,
                                "[stride] fmt=%u count=%u %u -> %u bytes\n",
                                (unsigned)plan.fmt, (unsigned)plan.count,
                                (unsigned)plan.vsize, (unsigned)cand);
                    plan = plan_command(p, avail, frontend.impl.state(),
                                        nullptr);
                    break;
                }
            }
        }
        pos += plan.len;
    }
    (void)fixes;
}

static void drop_pending_keep_state(ColorsGxFrontend& frontend) {
    static DolGxRecompState keep;
    keep = frontend.impl.state();
    frontend.pending.clear();
    frontend.impl.reset(frontend.has_resolver ? &frontend.resolver : nullptr);
    frontend.impl.set_packet_drain_enabled(true);
    frontend.impl.state() = keep;
}

bool colors_gx_frontend_flush(ColorsGxFrontend* frontend,
                              ColorsGxPacketFn submit, void* user) {
    if (frontend == nullptr)
        return false;
    apply_forced_vs(frontend);
    ColorsSink sink(submit, user);
    if (getenv("COLORS_NO_STRIDE_RECOVERY") == nullptr)
        recover_vertex_strides(*frontend);
    if (frontend->impl.flush(&sink)) {
        frontend->consume();
        return true;
    }

    // Decode failed: the decoder state reflects every command parsed before the
    // rejection, so snapshot it and retry from later byte offsets.
    static int fail_log = -1;
    if (fail_log < 0)
        fail_log = getenv("COLORS_GX_FAIL_LOG") != nullptr ? 8 : 0;
    if (fail_log > 0) {
        --fail_log;
        fprintf(stderr,
                "[gxf] decode fail: %s opcode=0x%02X bad=%zu pending=%zu\n",
                frontend->impl.last_error() != nullptr
                    ? frontend->impl.last_error()
                    : "(null)",
                (unsigned)frontend->impl.last_error_opcode(),
                frontend->impl.last_error_offset(),
                frontend->pending.size());
    }
    static DolGxRecompState saved;
    saved = frontend->impl.state();
    const size_t bad = frontend->impl.last_error_offset();
    const size_t remaining = frontend->pending.size();
    if (bad >= remaining) {
        drop_pending_keep_state(*frontend);
        return false;
    }

    for (size_t skip = 1u; skip <= kResyncSearchBytes; ++skip) {
        const size_t from = bad + skip;
        if (from >= remaining)
            break;
        frontend->impl.reset(frontend->has_resolver ? &frontend->resolver
                                                    : nullptr);
        frontend->impl.set_packet_drain_enabled(true);
        frontend->impl.state() = saved;
        frontend->impl.write_fifo(std::span<const std::uint8_t>{
            frontend->pending.data() + from, remaining - from});
        if (frontend->impl.flush(&sink)) {
            frontend->pending.erase(
                frontend->pending.begin(),
                frontend->pending.begin() + static_cast<std::ptrdiff_t>(from));
            frontend->consume();
            ++frontend->resyncs;
            return true;
        }
    }

    // No offset in the buffered bytes parses yet. Everything the decoder did
    // parse is already reflected in its state (and its packets are still queued
    // in the trace ring), so keep that state, drop the parsed prefix, and keep
    // the unparsed tail: the next flush appends more bytes and retries. In
    // practice this resynchronises without losing a command. Only a
    // pathological growth is dropped outright.
    frontend->pending.erase(frontend->pending.begin(),
                            frontend->pending.begin() +
                                static_cast<std::ptrdiff_t>(bad));
    if (frontend->pending.size() > 65536u) {
        drop_pending_keep_state(*frontend);
        return false;
    }
    {
        static DolGxRecompState keep;
        keep = frontend->impl.state();
        frontend->impl.reset(frontend->has_resolver ? &frontend->resolver
                                                   : nullptr);
        frontend->impl.set_packet_drain_enabled(true);
        frontend->impl.state() = keep;
        if (!frontend->pending.empty())
            frontend->impl.write_fifo(std::span<const std::uint8_t>{
                frontend->pending.data(), frontend->pending.size()});
    }
    return true;
}

size_t colors_gx_frontend_pending_fifo_size(
    const ColorsGxFrontend* frontend) {
    return frontend != nullptr ? frontend->pending.size() : 0u;
}

const DolGxRecompState* colors_gx_frontend_state(
    const ColorsGxFrontend* frontend) {
    return frontend != nullptr ? &frontend->impl.state() : nullptr;
}

const char* colors_gx_frontend_last_error(const ColorsGxFrontend* frontend) {
    return frontend != nullptr ? frontend->impl.last_error() : nullptr;
}

size_t colors_gx_frontend_last_error_offset(
    const ColorsGxFrontend* frontend) {
    return frontend != nullptr ? frontend->impl.last_error_offset() : 0u;
}

u32 colors_gx_frontend_last_error_opcode(const ColorsGxFrontend* frontend) {
    return frontend != nullptr ? frontend->impl.last_error_opcode() : 0u;
}

u64 colors_gx_frontend_resync_count(const ColorsGxFrontend* frontend) {
    return frontend != nullptr ? frontend->resyncs : 0u;
}

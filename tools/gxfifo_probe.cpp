// Replay a raw GX FIFO capture through DolRuntime's retail GX frontend (the
// same C API the game host uses) and report where the decoder stalls.
//
// Usage: gxfifo_probe <fifo.bin>
// Capture a stream with: COLORS_GX_DUMP=/tmp/fifo.bin COLORS_CLEAR_FONTFLAG=1
//                        ./build/ColorsRecomp game/sys/main.dol --max-blocks N
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "gxruntime/aurora_recomp/retail_gx_frontend_c.h"

namespace {
// Flat fake address space: any guest address resolves into a zeroed buffer so
// the probe isolates decode logic from memory mapping.
std::vector<unsigned char> g_mem(128u * 1024u * 1024u);

bool fake_resolve(void*, u32 address, u32 size, DolGuestAddressSpace,
                  DolGuestResourceKind, DolGuestResolvedRange* out) {
  if (out == nullptr || size == 0u || size > g_mem.size())
    return false;
  std::memset(out, 0, sizeof(*out));
  out->data = g_mem.data();
  out->available = static_cast<u32>(g_mem.size());
  (void)address;
  return true;
}

bool on_packet(void*, const DolAuroraRecompRenderPacket*) { return true; }
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <fifo.bin> [vtx_fmt] [vertex_size]\n",
                 argv[0]);
    return 2;
  }
  FILE* f = std::fopen(argv[1], "rb");
  if (f == nullptr) {
    std::perror("open");
    return 1;
  }
  std::fseek(f, 0, SEEK_END);
  const long length = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<unsigned char> bytes(
      static_cast<std::size_t>(length > 0 ? length : 0));
  if (!bytes.empty())
    (void)std::fread(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);

  DolGuestAddressResolver resolver{};
  dol_guest_address_resolver_init_callback(&resolver, fake_resolve, nullptr);
  DolAuroraRecompRetailGxFrontend* fe = dol_aurora_recomp_frontend_create(&resolver);
  if (fe == nullptr) {
    std::fprintf(stderr, "frontend create failed\n");
    return 1;
  }

  // Optional layout override: gxfifo_probe <fifo> [vtx_fmt] [vertex_size]
  if (argc >= 4 && argv[2][0] >= '0' && argv[2][0] <= '9') {
    const unsigned fmt = static_cast<unsigned>(std::atoi(argv[2]));
    const unsigned size = static_cast<unsigned>(std::atoi(argv[3]));
    std::fprintf(stderr, "forcing vtx_fmt %u vertex_size %u\n", fmt, size);
    dol_aurora_recomp_frontend_set_vertex_layout(fe, static_cast<u8>(fmt),
                                                 size);
  }

  // Optional: feed up to `offset`, inject VCD CP writes, then continue.
  // usage: gxfifo_probe <fifo> vcd <offset> <lo> <hi>
  if (argc >= 6 && std::strcmp(argv[2], "vcd") == 0) {
    const std::size_t at = static_cast<std::size_t>(std::strtoul(argv[3], nullptr, 0));
    const u32 lo = static_cast<u32>(std::strtoul(argv[4], nullptr, 0));
    const u32 hi = static_cast<u32>(std::strtoul(argv[5], nullptr, 0));
    if (at > bytes.size())
      return 3;
    unsigned char inject[12] = {0x08, 0x50,
                                static_cast<unsigned char>(lo >> 24),
                                static_cast<unsigned char>(lo >> 16),
                                static_cast<unsigned char>(lo >> 8),
                                static_cast<unsigned char>(lo),
                                0x08, 0x60,
                                static_cast<unsigned char>(hi >> 24),
                                static_cast<unsigned char>(hi >> 16),
                                static_cast<unsigned char>(hi >> 8),
                                static_cast<unsigned char>(hi)};
    if (!dol_aurora_recomp_frontend_write_fifo(fe, bytes.data(), at) ||
        !dol_aurora_recomp_frontend_flush(fe, on_packet, nullptr)) {
      std::fprintf(stderr, "prefix %zu failed\n", at);
      return 1;
    }
    std::fprintf(stderr, "inject VCD lo=0x%X hi=0x%X at %zu\n", lo, hi, at);
    if (!dol_aurora_recomp_frontend_write_fifo(fe, inject, sizeof inject) ||
        !dol_aurora_recomp_frontend_flush(fe, on_packet, nullptr)) {
      std::fprintf(stderr, "injection flush failed\n");
      return 1;
    }
    if (const DolGxRecompState* st0 = dol_aurora_recomp_frontend_state(fe))
      std::fprintf(stderr, "  after inject: vcd_lo=0x%X vcd_hi=0x%X layout0=%u\n",
                   st0->vcd_lo, st0->vcd_hi, st0->vertex_layouts[0].vertex_size);
    if (
        !dol_aurora_recomp_frontend_write_fifo(fe, bytes.data() + at,
                                               bytes.size() - at) ||
        !dol_aurora_recomp_frontend_flush(fe, on_packet, nullptr)) {
      const DolGxRecompState* st = dol_aurora_recomp_frontend_state(fe);
      std::fprintf(stderr, "failed after injection (layout0=%u pending=%zu)\n",
                   st != nullptr ? st->vertex_layouts[0].vertex_size : 0u,
                   dol_aurora_recomp_frontend_pending_fifo_size(fe));
      return 1;
    }
    std::printf("ok: %zu bytes decoded with VCD injection\n", bytes.size());
    dol_aurora_recomp_frontend_destroy(fe);
    return 0;
  }

  // Optional: feed up to `offset`, override one vertex layout, then continue.
  // usage: gxfifo_probe <fifo> at <offset> <fmt> <size>
  if (argc >= 6 && std::strcmp(argv[2], "at") == 0) {
    const std::size_t at = static_cast<std::size_t>(std::strtoul(argv[3], nullptr, 0));
    const unsigned fmt = static_cast<unsigned>(std::atoi(argv[4]));
    const unsigned size = static_cast<unsigned>(std::atoi(argv[5]));
    if (at > bytes.size())
      return 3;
    if (!dol_aurora_recomp_frontend_write_fifo(fe, bytes.data(), at) ||
        !dol_aurora_recomp_frontend_flush(fe, on_packet, nullptr)) {
      std::fprintf(stderr, "prefix %zu failed\n", at);
      return 1;
    }
    std::fprintf(stderr, "override at %zu: fmt %u size %u\n", at, fmt, size);
    dol_aurora_recomp_frontend_set_vertex_layout(fe, static_cast<u8>(fmt), size);
    if (!dol_aurora_recomp_frontend_write_fifo(fe, bytes.data() + at,
                                               bytes.size() - at) ||
        !dol_aurora_recomp_frontend_flush(fe, on_packet, nullptr)) {
      {
        unsigned count = 0;
        const DolGxRecompTraceEvent* ev =
            dol_aurora_recomp_frontend_trace_events(fe, &count);
        std::fprintf(stderr, "rest failed: pending=%zu events=%u\n",
                     dol_aurora_recomp_frontend_pending_fifo_size(fe), count);
        const unsigned from = count > 6u ? count - 6u : 0u;
        for (unsigned i = from; i < count; i++)
          std::fprintf(stderr, "  ev[%u] kind=%u a=0x%X b=0x%X c=0x%X d=0x%X\n",
                       i, ev[i].kind, ev[i].a, ev[i].b, ev[i].c, ev[i].d);
      }
      return 1;
    }
    std::printf("ok: %zu bytes decoded with override\n", bytes.size());
    dol_aurora_recomp_frontend_destroy(fe);
    return 0;
  }

  // Feed the whole capture at once so the reported offset is absolute.
  if (dol_aurora_recomp_frontend_write_fifo(fe, bytes.data(), bytes.size()) &&
      dol_aurora_recomp_frontend_flush(fe, on_packet, nullptr)) {
    const DolGxRecompState* st = dol_aurora_recomp_frontend_state(fe);
    std::printf("ok: %zu bytes decoded (%u trace events)\n", bytes.size(),
                st != nullptr ? st->trace_count : 0u);
    dol_aurora_recomp_frontend_destroy(fe);
    return 0;
  }
  {
    const DolGxRecompState* st = dol_aurora_recomp_frontend_state(fe);
    unsigned count = 0;
    const DolGxRecompTraceEvent* ev =
        dol_aurora_recomp_frontend_trace_events(fe, &count);
    std::fprintf(stderr, "stall: pending=%zu events=%u",
                 dol_aurora_recomp_frontend_pending_fifo_size(fe), count);
    if (st != nullptr)
      std::fprintf(stderr, " vcd_lo=0x%X vcd_hi=0x%X", st->vcd_lo, st->vcd_hi);
    std::fprintf(stderr, "\n");
    const unsigned from = count > 4u ? count - 4u : 0u;
    for (unsigned i = from; i < count; i++)
      std::fprintf(stderr, "  ev[%u] kind=%u a=0x%X b=0x%X c=0x%X d=0x%X\n", i,
                   ev[i].kind, ev[i].a, ev[i].b, ev[i].c, ev[i].d);
  }
  dol_aurora_recomp_frontend_destroy(fe);
  return 1;

  // Feed in small chunks: on failure the pending region starts at the command
  // that could not be decoded.
  const std::size_t chunk = 32;
  std::size_t fed = 0;
  std::size_t last_ok = 0;
  bool failed = false;
  while (fed < bytes.size()) {
    const std::size_t n = std::min(chunk, bytes.size() - fed);
    if (!dol_aurora_recomp_frontend_write_fifo(fe, bytes.data() + fed, n)) {
      std::fprintf(stderr, "write_fifo rejected at %zu\n", fed);
      failed = true;
      break;
    }
    fed += n;
    if (!dol_aurora_recomp_frontend_flush(fe, on_packet, nullptr)) {
      failed = true;
      break;
    }
    last_ok = fed;
  }

  if (!failed && last_ok == bytes.size()) {
    const DolGxRecompState* st = dol_aurora_recomp_frontend_state(fe);
    std::printf("ok: %zu bytes decoded (%u trace events)\n", bytes.size(),
                st != nullptr ? st->trace_count : 0u);
    dol_aurora_recomp_frontend_destroy(fe);
    return 0;
  }

  std::fprintf(stderr, "failing command starts at ~%zu:", last_ok);
  for (std::size_t i = last_ok; i < last_ok + 16u && i < bytes.size(); i++)
    std::fprintf(stderr, " %02X", bytes[i]);
  std::fprintf(stderr, "\n  pending=%zu\n",
               dol_aurora_recomp_frontend_pending_fifo_size(fe));

  if (const DolGxRecompState* st = dol_aurora_recomp_frontend_state(fe)) {
    std::fprintf(stderr,
                 "  vcd_lo=0x%08X vcd_hi=0x%08X vat0=0x%08X vat1=0x%08X "
                 "vat2=0x%08X\n",
                 st->vcd_lo, st->vcd_hi, st->vat_group0[0], st->vat_group1[0],
                 st->vat_group2[0]);
    for (unsigned f = 0; f < DOL_GX_RECOMP_VERTEX_FORMATS; f++) {
      if (!st->vertex_layouts[f].valid)
        continue;
      std::fprintf(stderr, "  layout[%u] vertex_size=%u indexed_attrs=%u\n", f,
                   st->vertex_layouts[f].vertex_size,
                   st->vertex_layouts[f].indexed_attr_count);
    }
  }
  unsigned count = 0;
  const DolGxRecompTraceEvent* ev =
      dol_aurora_recomp_frontend_trace_events(fe, &count);
  const unsigned from = count > 48u ? count - 48u : 0u;
  for (unsigned i = from; i < count; i++)
    std::fprintf(stderr, "  ev[%u] kind=%u a=0x%X b=0x%X c=0x%X d=0x%X\n", i,
                 ev[i].kind, ev[i].a, ev[i].b, ev[i].c, ev[i].d);
  dol_aurora_recomp_frontend_destroy(fe);
  return 1;
}

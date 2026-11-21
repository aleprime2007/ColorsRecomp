// C wrapper around gxcore's headless GX texture decoder so the C rasteriser can
// turn guest texture memory into RGBA8 without duplicating the tiled-format
// math (which is the part that must be bit-exact).

#include "core/types.h"
#include "gxruntime/gxcore/texture_decode.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" {

// Decode a direct (non-CI) GX texture into `out` (width*height*4 bytes).
bool colors_decode_texture(u32 format, u32 width, u32 height,
                           const u8* data, std::size_t size, u8* out,
                           std::size_t out_size) {
    if (out == nullptr)
        return false;
    std::vector<std::uint8_t> rgba =
        gxruntime::gxcore::decode_texture(format, width, height, data, size);
    const std::size_t need =
        static_cast<std::size_t>(width) * height * 4u;
    if (rgba.size() != need || out_size < need)
        return false;
    std::copy(rgba.begin(), rgba.end(), out);
    return true;
}

// Decode a TLUT palette into `out` (entries*4 bytes).
bool colors_decode_tlut(u32 format, u32 entries, const u8* data,
                        std::size_t size, u8* out, std::size_t out_size) {
    if (out == nullptr)
        return false;
    std::vector<std::uint8_t> rgba =
        gxruntime::gxcore::decode_tlut(format, entries, data, size);
    const std::size_t need = static_cast<std::size_t>(entries) * 4u;
    if (rgba.size() != need || out_size < need)
        return false;
    std::copy(rgba.begin(), rgba.end(), out);
    return true;
}

// Decode a CI (palettised) GX texture through its TLUT into `out`.
bool colors_decode_ci(u32 format, u32 width, u32 height, const u8* data,
                      std::size_t size, u32 tlut_format, u32 tlut_entries,
                      const u8* tlut_data, std::size_t tlut_size, u8* out,
                      std::size_t out_size) {
    if (out == nullptr)
        return false;
    std::vector<std::uint8_t> rgba = gxruntime::gxcore::decode_ci(
        format, width, height, data, size, tlut_format, tlut_entries,
        tlut_data, tlut_size);
    const std::size_t need = static_cast<std::size_t>(width) * height * 4u;
    if (rgba.size() != need || out_size < need)
        return false;
    std::copy(rgba.begin(), rgba.end(), out);
    return true;
}

}  // extern "C"

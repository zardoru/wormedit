#pragma once

#include <wormedit/core/document.h>
#include <wormedit/visual/visual_asset.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace wormedit::visual {

struct image_decode_limits_t {
    std::size_t maximum_input_bytes{64U * 1024U * 1024U};
    std::uint32_t maximum_width{16'384};
    std::uint32_t maximum_height{16'384};
    std::size_t maximum_pixels{64U * 1024U * 1024U};
    std::size_t maximum_rgba_bytes{256U * 1024U * 1024U};
};

// Decodes one complete bounded source buffer to tightly packed RGBA8. The
// decoder owns no renderer or texture state and is reusable by live and
// offline consumers.
struct rgba_image_t {
    [[nodiscard]] static auto from(const resolved_visual_asset_t& source,
                                   std::span<const std::byte> encoded,
                                   image_decode_limits_t limits = {})
        -> result_t<rgba_image_t, error_t<failure_code_t>>;

    resource_id_t resource;
    asset_id_t asset;
    std::filesystem::path path;
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;
};

// File loading applies the input byte cap before allocation and delegates to
// the same memory decoder used by fixtures and future archive-backed assets.
[[nodiscard]] auto open_image_rgba(const resolved_visual_asset_t& source,
                                   image_decode_limits_t limits = {})
        -> result_t<rgba_image_t, error_t<failure_code_t>>;

} // namespace wormedit::visual

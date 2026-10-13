#pragma once

#include <wormedit/core/document.h>
#include <wormedit/visual/visual_asset.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit::visual {

enum class image_status_t : std::uint8_t {
    ready,
    unknown_resource,
    missing_resource,
    unknown_asset,
    empty_path,
    relative_path_without_base,
    unsupported_asset_kind,
    input_too_large,
    read_failed,
    decode_failed,
    dimension_limit_exceeded,
    pixel_limit_exceeded,
    byte_limit_exceeded,
    declared_geometry_mismatch,
};

struct image_decode_limits_t {
    std::size_t maximum_input_bytes{64U * 1024U * 1024U};
    std::uint32_t maximum_width{16'384};
    std::uint32_t maximum_height{16'384};
    std::size_t maximum_pixels{64U * 1024U * 1024U};
    std::size_t maximum_rgba_bytes{256U * 1024U * 1024U};
};

struct rgba_image_t {
    [[nodiscard]] static auto from(const resolved_visual_asset_t& source,
                                   std::span<const std::byte> encoded,
                                   image_decode_limits_t limits = {})
        -> result_t<rgba_image_t, error_t<image_status_t>>;

    resource_id_t resource;
    asset_id_t asset;
    std::filesystem::path path;
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;
};

using image_decode_outcome_t = result_t<rgba_image_t, error_t<image_status_t>>;

[[nodiscard]] auto open_image_rgba(const resolved_visual_asset_t& source,
                                   image_decode_limits_t limits = {}) -> image_decode_outcome_t;

// Compatibility boundary for existing sys/UI callers; migrate them before removing this adapter.
struct image_decode_result_t {
    [[nodiscard]] static auto from(image_decode_outcome_t&& outcome) -> image_decode_result_t;

    image_status_t status{image_status_t::decode_failed};
    std::optional<rgba_image_t> image;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == image_status_t::ready && image.has_value();
    }
};

// Decodes one complete bounded source buffer to tightly packed RGBA8. The
// decoder owns no renderer or texture state and is reusable by live and
// offline consumers.
[[nodiscard]] auto decode_image_rgba(const resolved_visual_asset_t& source,
                                     std::span<const std::byte> encoded,
                                     image_decode_limits_t limits = {}) -> image_decode_result_t;

// File loading applies the input byte cap before allocation and delegates to
// the same memory decoder used by fixtures and future archive-backed assets.
[[nodiscard]] auto load_image_rgba(const resolved_visual_asset_t& source,
                                   image_decode_limits_t limits = {}) -> image_decode_result_t;

} // namespace wormedit::visual

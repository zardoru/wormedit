#pragma once

#include <wormedit/core/document.h>

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

struct resolved_visual_asset_t {
    resource_id_t resource;
    asset_id_t asset;
    asset_kind_t kind{asset_kind_t::image};
    std::filesystem::path path;
    std::optional<std::uint32_t> declared_width;
    std::optional<std::uint32_t> declared_height;
};

struct visual_asset_resolution_result_t {
    image_status_t status{image_status_t::unknown_resource};
    std::optional<resolved_visual_asset_t> source;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == image_status_t::ready && source.has_value();
    }
};

struct rgba_image_t {
    resource_id_t resource;
    asset_id_t asset;
    std::filesystem::path path;
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;
};

struct image_decode_result_t {
    image_status_t status{image_status_t::decode_failed};
    std::optional<rgba_image_t> image;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == image_status_t::ready && image.has_value();
    }
};

// Resolves one resource through the immutable document inventory. Relative
// paths require an explicit project directory; current process state is never
// consulted implicitly.
[[nodiscard]] auto resolve_visual_asset(const song_document_t& document, resource_id_t resource,
                                        const std::filesystem::path& project_directory)
    -> visual_asset_resolution_result_t;

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

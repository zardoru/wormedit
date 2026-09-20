#pragma once

#include <wormedit/core/video_preview.h>
#include <wormedit/visual/visual_asset.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::visual {

enum class video_frame_status_t : std::uint8_t {
    ready,
    invalid_request,
    unsupported_asset_kind,
    open_failed,
    stream_info_failed,
    no_video_stream,
    decoder_open_failed,
    timestamp_out_of_range,
    seek_failed,
    decode_failed,
    timestamp_missing,
    dimension_limit_exceeded,
    pixel_limit_exceeded,
    byte_limit_exceeded,
    declared_geometry_mismatch,
};

struct video_frame_decode_limits_t {
    std::uint32_t maximum_width{16'384};
    std::uint32_t maximum_height{16'384};
    std::size_t maximum_pixels{64U * 1024U * 1024U};
    std::size_t maximum_rgba_bytes{256U * 1024U * 1024U};
    std::size_t maximum_packets{1'000'000};
};

struct rgba_video_frame_t {
    resource_id_t resource;
    asset_id_t asset;
    std::filesystem::path path;
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;
    std::int64_t presentation_timestamp{};
    std::int32_t time_base_numerator{1};
    std::int32_t time_base_denominator{1};
};

struct video_frame_decode_result_t {
    video_frame_status_t status{video_frame_status_t::decode_failed};
    std::optional<rgba_video_frame_t> frame;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == video_frame_status_t::ready && frame.has_value();
    }
};

// Opens, seeks, and decodes the frame presented at an exact player-preview
// media clock. FFmpeg state and pixel conversion stay behind this optional
// adapter; callers receive an immutable-friendly tightly packed RGBA8 payload.
[[nodiscard]] auto decode_video_frame_rgba(const resolved_visual_asset_t& source,
                                           const video_preview_clock_t& clock,
                                           video_frame_decode_limits_t limits = {})
    -> video_frame_decode_result_t;

} // namespace wormedit::visual

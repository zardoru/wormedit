#pragma once

#include <wormedit/core/video_preview.h>
#include <wormedit/visual/visual_asset.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
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
    cancelled,
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
    std::optional<std::int64_t> next_presentation_timestamp;
    std::int64_t stream_start_timestamp{};
    std::int32_t time_base_numerator{1};
    std::int32_t time_base_denominator{1};
};

class video_decoder_session_t;
using video_frame_decode_result_t = result_t<rgba_video_frame_t, error_t<video_frame_status_t>>;
using video_decoder_session_open_result_t =
    result_t<std::unique_ptr<video_decoder_session_t>, error_t<video_frame_status_t>>;

class video_decoder_session_t {
  public:
    struct impl_t;

    video_decoder_session_t(video_decoder_session_t&&) noexcept;
    auto operator=(video_decoder_session_t&&) noexcept -> video_decoder_session_t&;
    ~video_decoder_session_t();

    video_decoder_session_t(const video_decoder_session_t&) = delete;
    auto operator=(const video_decoder_session_t&) -> video_decoder_session_t& = delete;

    [[nodiscard]] auto source() const noexcept -> const resolved_visual_asset_t&;
    [[nodiscard]] auto decode(const video_preview_clock_t& clock,
                              video_frame_decode_limits_t limits = {}, std::stop_token stop = {})
        -> video_frame_decode_result_t;

  private:
    explicit video_decoder_session_t(std::unique_ptr<impl_t> impl);
    std::unique_ptr<impl_t> impl_;

    friend auto open_video_decoder_session(const resolved_visual_asset_t&, std::stop_token)
        -> video_decoder_session_open_result_t;
};

[[nodiscard]] auto open_video_decoder_session(const resolved_visual_asset_t& source,
                                              std::stop_token stop = {})
    -> video_decoder_session_open_result_t;

// A missing next timestamp denotes the final frame and an open-ended interval.
[[nodiscard]] auto is_video_frame_covering_clock(const rgba_video_frame_t& frame,
                                            const video_preview_clock_t& clock) noexcept -> bool;
// Compatibility for the coordinated sys/UI migration.
inline auto video_frame_covers_clock(const rgba_video_frame_t& frame,
                                            const video_preview_clock_t& clock) noexcept -> bool { return is_video_frame_covering_clock(frame, clock); }

// Opens, seeks, and decodes the frame presented at an exact player-preview
// media clock. FFmpeg state and pixel conversion stay behind this optional
// adapter; callers receive an immutable-friendly tightly packed RGBA8 payload.
[[nodiscard]] auto
decode_video_frame_rgba(const resolved_visual_asset_t& source, const video_preview_clock_t& clock,
                        video_frame_decode_limits_t limits = {}, std::stop_token stop = {})
    -> video_frame_decode_result_t;

} // namespace wormedit::visual

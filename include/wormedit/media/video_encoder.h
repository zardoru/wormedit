#pragma once

#include <wormedit/core/video_vocabulary.h>
#include <wormedit/audio/offline_mix.h>
#include <wormedit/core/storyboard_raster.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>

namespace wormedit::media {

enum class video_colour_matrix_t { bt601, bt709 };

struct video_encoder_options_t {
    static constexpr std::uint64_t audio_target_bit_rate{192'000};
    std::filesystem::path destination;
    video_profile_t profile{video_profile_t::mp4_h264_aac};
    std::uint32_t width{storyboard_raster_options_t{}.width};
    std::uint32_t height{storyboard_raster_options_t{}.height};
    storyboard_raster_clock_t clock;
    std::uint64_t frame_count{};
    // Absolute native preview/mixer frame. Both scene and PCM start here;
    // output timestamps are relative to this origin. Chart offset stays in
    // the existing preview schedule; it must not be applied a second time.
    std::uint64_t begin_audio_frame{};
    storyboard_conditional_layer_t conditional_layer{storyboard_conditional_layer_t::pass};
    std::uint32_t threads{4};
    std::uint64_t maximum_output_bytes{8ULL * 1024 * 1024 * 1024};
    // Explicit opt-in only; package builds keep GPL codecs disabled.
    bool allow_gpl{};
    bool replace_existing{true}; // Direct callers retain replacement by default; UI supplies explicit consent.
    [[nodiscard]] auto is_valid() const -> bool;
    [[nodiscard]] auto scene_time_at(std::uint64_t index) const
        -> result_t<storyboard_time_t, failure_code_t>;
    [[nodiscard]] auto input_audio_frames() const -> result_t<std::uint64_t, failure_code_t>;
    [[nodiscard]] auto colour_matrix() const -> video_colour_matrix_t;
    [[nodiscard]] auto h264_level() const -> int;
    [[nodiscard]] auto estimated_video_bit_rate() const -> std::uint64_t;
    auto operator==(const video_encoder_options_t&) const -> bool = default;
};

struct video_encoder_progress_t {
    std::uint64_t completed_frames{}, total_frames{};
    auto operator==(const video_encoder_progress_t&) const -> bool = default;
};
struct video_encoder_report_t {
    std::uint64_t video_frames{}, audio_frames{};
    std::uint32_t audio_sample_rate{};
    std::string video_encoder, audio_encoder, ffmpeg_version, ffmpeg_configuration;
    video_colour_matrix_t colour_matrix{video_colour_matrix_t::bt709};
    int h264_level{};
    auto operator==(const video_encoder_report_t&) const -> bool = default;
};

using video_frame_source_t = std::function<result_t<storyboard_raster_frame_t, error_t<failure_code_t>>(
    std::uint64_t index, storyboard_time_t time, storyboard_conditional_layer_t, std::stop_token)>;
// Call render_offline_interval_to through this producer. Its sink timestamps
// remain absolute preview frames and must cover exactly the requested interval.
using video_audio_source_t = std::function<result_t<void, error_t<failure_code_t>>(
    const audio::offline_mix_sink_t&, std::stop_token)>;
struct video_encoder_sources_t {
    video_frame_source_t frame;
    video_audio_source_t audio; // Empty selects video without audio.
};

// Synchronous work boundary for async_job_t (VID-3 owns scheduling). Each
// borrowed RGBA frame is converted/consumed before requesting the next one:
// a slow codec back-pressures VID-1's fixed ring, never queues RGBA copies.
// Progress is called at most once per completed input frame, then once after
// validation/publication. The final completed count means atomic success.
class video_encoder_t {
  public:
    // Build availability only. Probe each selected configuration before
    // offering it; linking a decoder does not imply an encoder/profile works.
    [[nodiscard]] static auto is_available() noexcept -> bool;
    [[nodiscard]] static auto encode(const video_encoder_options_t&, const video_encoder_sources_t&,
        std::stop_token stop = {}, std::function<void(video_encoder_progress_t)> progress = {})
        -> result_t<video_encoder_report_t, error_t<failure_code_t>>;
    // Actual short encode + mux + completed-file validation in an exclusively
    // owned same-directory temporary, removed without publication. This is the
    // configuration capability check, not merely a codec-name/open query.
    [[nodiscard]] static auto probe(video_encoder_options_t options, bool audio,
        std::stop_token stop = {}) -> result_t<video_encoder_report_t, error_t<failure_code_t>>;
};

} // namespace wormedit::media

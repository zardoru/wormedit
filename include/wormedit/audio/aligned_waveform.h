#pragma once

#include <wormedit/audio/waveform.h>
#include <wormedit/core/timing.h>

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace wormedit::audio {

inline constexpr std::size_t maximum_aligned_waveform_rows = 4'096;
inline constexpr std::size_t maximum_waveform_overview_rows = 512;

struct aligned_waveform_request_t {
    double viewport_scroll_pixels{};
    double viewport_height_pixels{};
    double pixels_per_standard_measure{};
    std::size_t maximum_rows{maximum_aligned_waveform_rows};
};

struct aligned_waveform_window_t {
    std::uint64_t begin_frame{};
    std::uint64_t end_frame{};
    double frames_per_pixel{};
};

[[nodiscard]] auto aligned_waveform_window(const waveform_t& waveform,
    const timing_index_t& timing, aligned_waveform_request_t request)
    -> std::optional<aligned_waveform_window_t>;

struct aligned_waveform_style_t {
    bool sample_points{};
    bool detail_level{};
    auto operator==(const aligned_waveform_style_t&) const -> bool = default;
};
struct aligned_waveform_detail_range_t {
    std::uint64_t begin_frame{};
    std::uint64_t end_frame{};
    [[nodiscard]] auto covers(aligned_waveform_window_t window) const noexcept -> bool;
    [[nodiscard]] auto needs_refill(aligned_waveform_window_t window, int direction) const noexcept -> bool;
    auto operator==(const aligned_waveform_detail_range_t&) const -> bool = default;
};
[[nodiscard]] auto padded_aligned_waveform_range(aligned_waveform_window_t window,
    int direction = 1) -> std::optional<aligned_waveform_detail_range_t>;
void update_aligned_waveform_style(aligned_waveform_style_t& style, double frames_per_pixel) noexcept;

struct aligned_waveform_row_t {
    double y_pixels{};
    std::array<float, output_channel_count> absolute_peak{};
    std::array<float, output_channel_count> rms{};
    bool sample_point{};
    std::array<float, output_channel_count> samples{};
};

enum class aligned_waveform_status_t {
    projected,
    invalid_request,
    invalid_timing,
    invalid_waveform,
    capacity_exceeded,
};

struct aligned_waveform_result_t {
    aligned_waveform_status_t status{aligned_waveform_status_t::invalid_request};
    std::vector<aligned_waveform_row_t> rows;
    // Composition diagnostics distinguish drawing mode from per-span data availability.
    std::uint32_t chosen_frames_per_bin{};
    std::size_t pcm_spans{};
    std::size_t detail_spans{};
    std::size_t overview_spans{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == aligned_waveform_status_t::projected;
    }
};

// Each pixel span uses PCM, then available detail bins, then overview bins.
// Drawing style is supplied independently of availability.
[[nodiscard]] auto project_composed_aligned_waveform(const waveform_t& overview,
    const waveform_t* detail, const timing_index_t& timing, aligned_waveform_request_t request,
    decoded_block_cache_t* blocks, aligned_waveform_style_t style) -> aligned_waveform_result_t;

struct waveform_overview_row_t {
    double audio_fraction{};
    float absolute_peak{};
};

struct waveform_overview_result_t {
    aligned_waveform_status_t status{aligned_waveform_status_t::invalid_request};
    double visible_begin_fraction{};
    double visible_end_fraction{};
    std::optional<double> beat_zero_fraction;
    std::vector<waveform_overview_row_t> rows;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == aligned_waveform_status_t::projected;
    }
};

[[nodiscard]] auto project_waveform_overview(
    const waveform_t& waveform, const timing_index_t& timing, aligned_waveform_request_t viewport,
    std::size_t maximum_rows = maximum_waveform_overview_rows) -> waveform_overview_result_t;

[[nodiscard]] auto project_aligned_waveform_samples(const waveform_t& waveform,
    const timing_index_t& timing, aligned_waveform_request_t request, decoded_block_cache_t& cache)
    -> aligned_waveform_result_t;

// Projects normalized source peaks through the exact chart timing inverse onto
// the shared vertical musical viewport. It reads only derived waveform data.
[[nodiscard]] auto project_aligned_waveform(const waveform_t& waveform,
                                            const timing_index_t& timing,
                                            aligned_waveform_request_t request)
    -> aligned_waveform_result_t;

} // namespace wormedit::audio

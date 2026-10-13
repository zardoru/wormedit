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
inline constexpr std::size_t maximum_waveform_range_rows = 1'024;

struct aligned_waveform_request_t {
    double viewport_scroll_pixels{};
    double viewport_height_pixels{};
    double pixels_per_standard_measure{};
    std::size_t maximum_rows{maximum_aligned_waveform_rows};
};

// Display-only clock: exact timing/editing queries remain on timing_index_t.
struct display_timing_boundary_t {
    double row{}, before_seconds{}, event_seconds{}, after_seconds{};
};
struct display_timing_segment_t {
    double begin_row{}, end_row{}, begin_seconds{}, seconds_per_row{};
};
struct display_row_table_t;

struct display_row_time_t {
    [[nodiscard]] static auto from(const display_row_table_t& table, std::int64_t world)
        -> display_row_time_t;

    std::int64_t world_pixel{};
    double begin_row{}, end_row{}, begin_seconds{}, end_seconds{};
    bool valid{};
};
struct display_row_table_identity_t {
    std::uint64_t project{}, chart{}, revision{};
    auto operator==(const display_row_table_identity_t&) const -> bool = default;
};
struct display_row_table_t {
    display_row_table_identity_t identity;
    const timing_index_t* timing_identity{};
    std::uint64_t fingerprint{}, divisions{}, total_rows{};
    timing_discontinuity_profile_t profile{};
    bool bounded_clock{};
    std::size_t clock_roundings{}, timing_row_count{}, timing_segment_count{};
    const void* timing_rows_identity{};
    const void* timing_segments_identity{};
    double zoom{}, scroll{}, height{};
    std::int64_t first_world_pixel{};
    std::vector<display_timing_boundary_t> boundaries;
    std::vector<display_timing_segment_t> segments;
    std::vector<display_row_time_t> rows;
    // Work for the last changed generation; identical same-frame prepares do
    // not change these counters or generation.
    std::size_t built_rows{}, reused_rows{};
    std::uint64_t generation{};
    std::uint64_t clock_generation{};
    bool valid{};
};
// Includes one padding row at each end, shared by all envelope rings. Scroll
// reuses overlapping world pixels; timing/identity/zoom/height changes rebuild.
[[nodiscard]] auto prepare_display_row_table(display_row_table_t& table,
    const timing_index_t& timing, aligned_waveform_request_t request,
    display_row_table_identity_t identity = {}) -> bool;
[[nodiscard]] auto display_seconds_at(const display_row_table_t& table, double row,
    timing_boundary_side_t side = timing_boundary_side_t::event) -> std::optional<double>;
// Complete analyzed pyramids read a bounded handful of closest-level bins.
// A valid base-only/partial pyramid falls back to aggregating available bins.
// Peak and RMS retain max reduction, rather than an average across a pixel.
[[nodiscard]] auto sample_display_waveform_span(const waveform_t& waveform,
    std::uint64_t begin, std::uint64_t end, std::size_t& sampled_bins)
    -> std::optional<waveform_column_t>;

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
    [[nodiscard]] auto is_covering_window(aligned_waveform_window_t window) const noexcept -> bool;
    // Compatibility for the coordinated sys/UI migration.
    inline auto covers(aligned_waveform_window_t window) const noexcept -> bool { return is_covering_window(window); }
    [[nodiscard]] auto is_refill_needed(aligned_waveform_window_t window, int direction) const noexcept -> bool;
    // Compatibility for the coordinated sys/UI migration.
    inline auto needs_refill(aligned_waveform_window_t window, int direction) const noexcept -> bool { return is_refill_needed(window, direction); }
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
    // Display work counters, independent of source length/zoom when pyramids are available.
    std::size_t sampled_bins{};
    std::size_t visited_slices{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == aligned_waveform_status_t::projected;
    }
};

// Each pixel span uses PCM, then available detail bins, then overview bins.
// Drawing style is supplied independently of availability.
[[nodiscard]] auto project_composed_aligned_waveform(const waveform_t& overview,
    const waveform_t* detail, const timing_index_t& timing, aligned_waveform_request_t request,
    decoded_block_cache_t* blocks, aligned_waveform_style_t style) -> aligned_waveform_result_t;

[[nodiscard]] auto project_composed_aligned_waveform(const waveform_t& overview,
    const waveform_t* detail, const display_row_table_t& table, aligned_waveform_request_t request,
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

// Projects the audio span [begin_fraction, end_fraction] of the whole source into at most
// `maximum_rows` rows, each placed by its absolute audio fraction. The visible fractions of the
// result echo the requested span; beat zero is not reported.
[[nodiscard]] auto project_waveform_range(const waveform_t& waveform, double begin_fraction,
                                          double end_fraction,
                                          std::size_t maximum_rows = maximum_waveform_range_rows)
    -> waveform_overview_result_t;

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

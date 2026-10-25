#pragma once

#include <wormedit/audio/waveform.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/export_cut_axis.h>
#include <wormedit/core/export_cut_frames.h>
#include <memory>
#include <wormedit/core/operation_state.h>
#include <wormedit/core/result.h>
#include <wormedit/core/keyed_cache.h>

#include <array>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <vector>
#include <tuple>
#include <variant>

namespace wormedit { struct sound_slice_result_t; }

namespace wormedit::audio {

struct channel_waveform_source_geometry_t;

inline constexpr std::size_t maximum_aligned_waveform_rows = 4'096;
inline constexpr std::size_t maximum_waveform_overview_rows = 512;
inline constexpr std::size_t maximum_waveform_range_rows = 1'024;

struct aligned_waveform_request_t {
    double viewport_scroll_pixels{};
    double viewport_height_pixels{};
    double pixels_per_standard_measure{};
    std::size_t maximum_rows{maximum_aligned_waveform_rows};
    std::shared_ptr<const export_cut_axis_t> cut_axis;
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
    [[nodiscard]] static auto from(const display_row_table_t& table, std::int64_t world, double zoom)
        -> display_row_time_t;

    std::int64_t world_pixel{};
    double begin_row{}, end_row{}, begin_seconds{}, end_seconds{};
    bool valid{};
};
struct display_row_table_identity_t {
    std::uint64_t project{}, chart{}, revision{};
    auto operator==(const display_row_table_identity_t&) const -> bool = default;
};
struct display_clock_source_key_t {
    display_row_table_identity_t identity;
    const timing_index_t* timing_identity{};
    std::uint64_t fingerprint{}, divisions{}, total_rows{};
    timing_discontinuity_profile_t profile{};
    bool bounded_clock{};
    std::size_t clock_roundings{}, timing_row_count{}, timing_segment_count{};
    const void* timing_rows_identity{};
    const void* timing_segments_identity{};
    auto operator==(const display_clock_source_key_t&) const -> bool = default;
    static auto from(const timing_index_t& timing, display_row_table_identity_t identity)
        -> display_clock_source_key_t {
        return {identity, &timing, timing.source_fingerprint, timing.divisions_per_standard_measure,
                timing.total_rows, timing.discontinuity_profile, timing.bounded_clock,
                timing.bounded_clock_roundings, timing.rows.size(), timing.segments.size(),
                timing.rows.data(), timing.segments.data()};
    }
};
struct display_row_table_t {
    std::shared_ptr<const export_cut_axis_t> cut_axis;
    std::shared_ptr<const export_cut_frame_timeline_t> cut_frames;
    struct clock_data_t {
        const timing_index_t* timing_identity{};
        std::uint64_t divisions{}, total_rows{};
        std::vector<display_timing_boundary_t> boundaries;
        std::vector<display_timing_segment_t> segments;
        std::uint64_t generation{};
    };
    // Packed extrapolation follows the base clock's generation, not its own build count.
    using clock_key_t = std::variant<display_clock_source_key_t, std::uint64_t>;
    keyed_cache_t<clock_key_t, clock_data_t> clock;
    struct row_data_t {
        std::int64_t first_world_pixel{};
        std::vector<display_row_time_t> rows;
        std::size_t built_rows{}, reused_rows{};
        std::uint64_t generation{};
    };
    keyed_cache_t<std::tuple<double, double, double>, row_data_t> viewport;
    // Invalid requests retain the last geometry key, as well as the converted clock.
    [[nodiscard]] auto scroll() const noexcept -> double {
        return viewport.key() ? std::get<0>(*viewport.key()) : 0.0;
    }
    [[nodiscard]] auto height() const noexcept -> double {
        return viewport.key() ? std::get<1>(*viewport.key()) : 0.0;
    }
    [[nodiscard]] auto zoom() const noexcept -> double {
        return viewport.key() ? std::get<2>(*viewport.key()) : 0.0;
    }
    // Includes one padding row at each end; scroll reuses overlapping world pixels.
    [[nodiscard]] auto prepare(const timing_index_t& timing, aligned_waveform_request_t request,
                                display_row_table_identity_t identity = {}) -> bool;
    [[nodiscard]] auto seconds_at(double row,
        timing_boundary_side_t side = timing_boundary_side_t::event) const -> std::optional<double>;
};
// Samples a pixel as retained source-clock spans, including cuts inside pauses
// whose boundaries occupy the same musical row. Cursor is a source sample frame.
[[nodiscard]] auto next_display_audio_span(const display_row_time_t& row,
    const export_cut_frame_timeline_t& frames, std::uint32_t sample_rate, std::uint64_t& cursor)
    -> std::optional<display_row_time_t>;

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
    [[nodiscard]] auto is_refill_needed(aligned_waveform_window_t window, int direction) const noexcept -> bool;
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
    auto operator==(const aligned_waveform_row_t&) const -> bool = default;
};

static_assert(std::is_trivially_copyable_v<operation_state_t<std::monostate>>);

struct aligned_waveform_view_result_t {
    [[nodiscard]] static auto from(const waveform_t& waveform, const timing_index_t& timing,
                                   aligned_waveform_request_t request)
        -> aligned_waveform_view_result_t;
    [[nodiscard]] static auto from(const waveform_t& waveform, const timing_index_t& timing,
                                   aligned_waveform_request_t request, decoded_block_cache_t& cache)
        -> aligned_waveform_view_result_t;
    [[nodiscard]] static auto from(const waveform_t& overview, const waveform_t* detail,
                                   const timing_index_t& timing, aligned_waveform_request_t request,
                                   decoded_block_cache_t* blocks, aligned_waveform_style_t style)
        -> aligned_waveform_view_result_t;
    [[nodiscard]] static auto from(const waveform_t& overview, const waveform_t* detail,
                                   const display_row_table_t& table, aligned_waveform_request_t request,
                                   decoded_block_cache_t* blocks, aligned_waveform_style_t style)
        -> aligned_waveform_view_result_t;
    operation_state_t<std::monostate> status{failure_code_t::invalid_request};
    std::vector<aligned_waveform_row_t> rows;
    // Composition diagnostics distinguish drawing mode from per-span data availability.
    std::uint32_t chosen_frames_per_bin{};
    std::size_t pcm_spans{};
    std::size_t detail_spans{};
    std::size_t overview_spans{};
    // Display work counters, independent of source length/zoom when pyramids are available.
    std::size_t sampled_bins{};
    std::size_t visited_slices{};
    auto operator==(const aligned_waveform_view_result_t&) const -> bool = default;

    // Display path: shared forward clock and bounded row/slice pyramid reads.
    // The timing_index_t overloads remain the exact inverse reference path.
    [[nodiscard]] static auto from(const waveform_t& waveform,
                                   const sound_channel_t& channel, const display_row_table_t& table,
                                   aligned_waveform_request_t request, const sound_slice_result_t& slices,
                                   channel_waveform_source_geometry_t source)
        -> aligned_waveform_view_result_t;

    // Projects one retained BMSON sound channel into the chart viewport. Source
    // peaks stay in source-frame space; the exact channel cuts select the frames
    // that may appear and timing maps them to musical rows.
    [[nodiscard]] static auto from(const waveform_t& waveform, const chart_document_t& chart,
                                   const sound_channel_t& channel, const timing_index_t& timing,
                                   aligned_waveform_request_t request,
                                   channel_waveform_source_geometry_t source)
        -> aligned_waveform_view_result_t;

    // Accepts a revision-bound slice derivation so interactive scrolling and zoom
    // need only reproject peaks, not rebuild all channel cuts.
    [[nodiscard]] static auto from(const waveform_t& waveform, const sound_channel_t& channel,
                                   const timing_index_t& timing, aligned_waveform_request_t request,
                                   const sound_slice_result_t& slices,
                                   channel_waveform_source_geometry_t source)
        -> aligned_waveform_view_result_t;
};

struct waveform_overview_row_t {
    double audio_fraction{};
    float absolute_peak{};
    auto operator==(const waveform_overview_row_t&) const -> bool = default;
};

struct waveform_overview_view_result_t {
    [[nodiscard]] static auto from(const waveform_t& waveform, const export_cut_frame_timeline_t& frames,
        double begin_fraction, double end_fraction, std::size_t maximum_rows = maximum_waveform_overview_rows)
        -> waveform_overview_view_result_t;
    [[nodiscard]] static auto from(const waveform_t& waveform, const timing_index_t& timing,
                                   aligned_waveform_request_t viewport,
                                   std::size_t maximum_rows = maximum_waveform_overview_rows)
        -> waveform_overview_view_result_t;
    [[nodiscard]] static auto from(const waveform_t& waveform, double begin_fraction,
                                   double end_fraction,
                                   std::size_t maximum_rows = maximum_waveform_range_rows)
        -> waveform_overview_view_result_t;
    operation_state_t<std::monostate> status{failure_code_t::invalid_request};
    double visible_begin_fraction{};
    double visible_end_fraction{};
    std::optional<double> beat_zero_fraction;
    std::vector<waveform_overview_row_t> rows;
    auto operator==(const waveform_overview_view_result_t&) const -> bool = default;
};

} // namespace wormedit::audio

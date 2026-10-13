#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/timing_discontinuity.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit {

enum class timing_error_code_t {
    invalid_document,
    missing_initial_bpm,
    invalid_bpm,
    invalid_pause,
    invalid_warp,
    invalid_scroll,
    invalid_speed,
    arithmetic_overflow,
    position_out_of_range,
    negative_sample_frame,
    capacity_exceeded,
    invalid_fit,
    locked_anchor_conflict,
};

using timing_diagnostic_t =
    object_diagnostic_t<timing_error_code_t, timing_error_code_t::invalid_document>;

enum class timing_boundary_side_t {
    // Arrival at the musical row, before a delay at that row.
    before_delays,
    // Gameplay/event time. Delays apply before this point; stops apply after it.
    event,
    // Departure from the row after both delays and stops.
    after_stops,
};

struct timing_row_state_t {
    std::uint64_t absolute_row{};
    signed_rational_t before_delays_seconds;
    signed_rational_t event_seconds;
    signed_rational_t after_stops_seconds;
    signed_rational_t effective_bpm;
    bool inside_warp{};
};

struct timing_segment_t {
    std::uint64_t begin_row{};
    std::uint64_t end_row{};
    signed_rational_t begin_seconds;
    signed_rational_t end_seconds;
    signed_rational_t effective_bpm;
    bool warped{};
};

struct timing_build_options_t {
    std::size_t maximum_boundaries{100'000};
    std::size_t maximum_timing_events{200'000};
    std::optional<timing_discontinuity_profile_t> discontinuity_profile;
    // Default-off: retry only exact-clock accumulation overflow with a bounded
    // derived clock. Invalid source intervals and timing conversions stay strict.
    bool allow_bounded_clock{};
    std::size_t maximum_bounded_clock_roundings{300'001};
};

struct timing_query_result_t {
    std::optional<signed_rational_t> seconds;
    bool inside_warp{};
    std::optional<timing_diagnostic_t> error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return seconds.has_value();
    }
};

enum class timing_warp_inverse_side_t {
    // A zero-time warp plateau resolves to the first row in the collapsed span.
    entry,
    // A zero-time warp plateau resolves to the row just after the collapsed span.
    exit,
};

struct timing_inverse_options_t {
    timing_warp_inverse_side_t warp_side{timing_warp_inverse_side_t::entry};
    // Preview-only callers may extrapolate rows before the chart's first
    // arrival time from the row-zero BPM. Ordinary editor queries stay strict.
    bool extrapolate_before_chart{};
};

struct timing_position_query_result_t {
    // Exact absolute row on ordinary clocks; bounded clocks return a nanorow
    // coordinate for interior segments. Fractional rows are retained either way.
    std::optional<signed_rational_t> absolute_row;
    bool inside_warp{};
    std::optional<timing_diagnostic_t> error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return absolute_row.has_value();
    }
};

// Tail behavior is a caller choice; editable cursors use the last stored row.
enum class chart_end_policy_t { reject, clamp_to_end, clamp_to_last_row, extrapolate_tail };

struct timing_index_t {
    std::uint64_t divisions_per_standard_measure{};
    std::uint64_t total_rows{};
    // Detects accidental reuse after timing/measure edits. UI publication still
    // uses the document generation guard because a hash is not a session token.
    std::uint64_t source_fingerprint{};
    timing_discontinuity_profile_t discontinuity_profile{
        timing_discontinuity_profile_t::canonical};
    // Preview/display callers may opt into a uniformly rounded derived clock
    // if exact rational accumulation alone exceeds representable bounds. The
    // clock uses 1e-12-second ticks. Accumulated boundary error is at most
    // roundings/(2e12) seconds; interior interpolation adds at most 0.5 ps.
    bool bounded_clock{};
    std::size_t bounded_clock_roundings{};
    std::vector<timing_row_state_t> rows;
    std::vector<timing_segment_t> segments;
    [[nodiscard]] auto seconds_at(std::uint64_t row,
        timing_boundary_side_t side = timing_boundary_side_t::event) const -> timing_query_result_t;
    [[nodiscard]] auto seconds_at(exact_chart_row_t row,
        timing_boundary_side_t side = timing_boundary_side_t::event) const -> timing_query_result_t;
    [[nodiscard]] auto seconds_at(const measure_index_t& measures, local_position_t position,
        timing_boundary_side_t side = timing_boundary_side_t::event) const -> timing_query_result_t;
    [[nodiscard]] auto row_at(signed_rational_t seconds,
        timing_inverse_options_t options = {}) const -> timing_position_query_result_t;
    [[nodiscard]] auto row_at(signed_rational_t seconds, chart_end_policy_t policy,
        timing_inverse_options_t options = {}) const -> timing_position_query_result_t;
    [[nodiscard]] auto seconds_at_approx(std::uint64_t row,
        chart_end_policy_t policy = chart_end_policy_t::reject,
        timing_boundary_side_t side = timing_boundary_side_t::event) const -> std::optional<double>;
    [[nodiscard]] auto row_at_approx(signed_rational_t seconds, chart_end_policy_t policy,
        timing_inverse_options_t options = {}) const -> std::optional<double>;
    [[nodiscard]] auto tail_seconds_per_row() const -> std::optional<double>;
    [[nodiscard]] auto row_at_frame(std::uint64_t frame, std::uint32_t sample_rate,
        chart_end_policy_t policy, timing_inverse_options_t options = {}) const -> std::optional<double>;
};
struct timing_build_result_t {
    std::optional<timing_index_t> index;
    std::vector<timing_diagnostic_t> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return index.has_value();
    }
};

enum class sample_rounding_t {
    floor,
    ceil,
    nearest_ties_to_even,
};

struct sample_frame_result_t {
    std::optional<std::uint64_t> frame;
    std::optional<timing_diagnostic_t> error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return frame.has_value();
    }
};

[[nodiscard]] auto build_timing_index(const chart_document_t& chart,
                                      timing_build_options_t options = {}) -> timing_build_result_t;

[[nodiscard]] auto timing_at(const timing_index_t& index, std::uint64_t absolute_row,
                             timing_boundary_side_t side = timing_boundary_side_t::event)
    -> timing_query_result_t;

[[nodiscard]] auto timing_at(const timing_index_t& index, exact_chart_row_t absolute_row,
                             timing_boundary_side_t side = timing_boundary_side_t::event)
    -> timing_query_result_t;

// Resolve a pointer position on the fixed chart waveform to its nearest
// source frame. Fractional rows are retained to one millionth of a row before
// exact timing and sample-frame rounding are applied.
[[nodiscard]] auto sample_frame_at_chart_y(const timing_index_t& index,
                                           double viewport_scroll_pixels,
                                           double pixels_per_standard_measure, double y_pixels,
                                           double viewport_height_pixels, std::uint32_t sample_rate)
    -> std::optional<std::uint64_t>;

// Show when a proposed musical row would sound against the original chart's
// fixed waveform/viewport. The row itself stays unchanged in the document.
[[nodiscard]] auto
proposed_timing_y_on_fixed_waveform(const timing_index_t& original, const timing_index_t& proposed,
                                    exact_chart_row_t chart_row, double viewport_scroll_pixels,
                                    double pixels_per_standard_measure) -> std::optional<double>;

[[nodiscard]] auto timing_at(const timing_index_t& index, const measure_index_t& measures,
                             local_position_t position,
                             timing_boundary_side_t side = timing_boundary_side_t::event)
    -> timing_query_result_t;

struct beat_interval_fit_result_t {
    std::optional<signed_rational_t> beats_per_minute;
    std::optional<timing_diagnostic_t> error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return beats_per_minute.has_value();
    }
};

// Fit the constant-BPM interval from the selected last BPM event through a
// later beat to an exact source frame. Fixed-duration pauses, BPM-scaled pauses,
// and warps are evaluated by the timing engine. Rejects a fit that would move
// a locked anchor; downstream compensation is a separate operation.
[[nodiscard]] auto fit_beat_interval(const chart_document_t& chart, object_id_t bpm_event,
                                     local_position_t beat, std::uint64_t target_sample_frame,
                                     std::uint32_t sample_rate) -> beat_interval_fit_result_t;

// On a bounded clock, input time rounds to the nearest picosecond tick and
// interior rows round to the nearest nanorow (<= 0.5e-9 row), in addition to
// the clock's accumulated error and <= 0.5 ps input rounding.
[[nodiscard]] auto musical_position_at(const timing_index_t& index, signed_rational_t seconds,
                                       timing_inverse_options_t options = {})
    -> timing_position_query_result_t;

// Scroll events set an instantaneous signed multiplier. Zero stops chart
// motion and negative values reverse it. Speed events set a nonnegative
// multiplier, optionally interpolated linearly over beats or elapsed chart
// seconds. Beat transitions advance through warped musical rows and pause
// during stops; second transitions advance during stops and pause inside
// warps. Event-side queries include events at their row, while before_delays
// queries see the state arriving at that row. Preview callers may explicitly
// enable negative-row extrapolation from the row-zero event-side state.
struct scroll_curve_segment_t {
    std::uint64_t begin_row{};
    std::uint64_t end_row{};
    signed_rational_t multiplier{1, 1};
    object_id_t source_event;
    std::size_t source_order{};
};

struct speed_curve_segment_t {
    std::uint64_t begin_row{};
    std::uint64_t end_row{};
    signed_rational_t begin_seconds;
    signed_rational_t initial_multiplier{1, 1};
    signed_rational_t target_multiplier{1, 1};
    rational_t transition_duration{};
    bool transition_is_seconds{};
    object_id_t source_event;
    std::size_t source_order{};
};

struct scroll_displacement_segment_t {
    signed_rational_t begin_row;
    signed_rational_t end_row;
    signed_rational_t cumulative_begin_beats;
    signed_rational_t scroll_multiplier{1, 1};
    signed_rational_t speed_begin_multiplier{1, 1};
    bool warped{};
};

struct scroll_speed_index_t {
    std::uint64_t divisions_per_standard_measure{};
    std::uint64_t total_rows{};
    std::uint64_t timing_source_fingerprint{};
    std::uint64_t source_fingerprint{};
    // Segments are ordered by start row, source order, source insertion order,
    // then stable object ID. Multiple events at one row intentionally create
    // zero-width segments; binary search selects the last one for event-side
    // evaluation and skips all of them for before_delays evaluation.
    std::vector<scroll_curve_segment_t> scroll_segments;
    std::vector<speed_curve_segment_t> speed_segments;
    // Integral of scroll * speed over quarter-beat distance from row zero.
    // Stops add no distance and warp spans are skipped. It is precompiled at
    // timing/event/transition boundaries, so arbitrary chart-play seeks need
    // one binary search and one exact local integral.
    std::vector<scroll_displacement_segment_t> displacement_segments;
    signed_rational_t total_displacement_beats{};
};

struct scroll_speed_build_options_t {
    std::size_t maximum_events{200'000};
    std::size_t maximum_timing_boundaries{100'000};
    std::size_t maximum_segments{200'001};
};

struct scroll_speed_build_result_t {
    std::optional<scroll_speed_index_t> index;
    std::vector<timing_diagnostic_t> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return index.has_value();
    }
};

struct scroll_speed_state_t {
    signed_rational_t scroll_multiplier{1, 1};
    signed_rational_t speed_multiplier{1, 1};
    signed_rational_t effective_multiplier{1, 1};
    signed_rational_t displacement_beats{};
    // Convenience value for renderers. It is derived only after exact checked
    // evaluation and is guaranteed finite on success.
    double render_multiplier{1.0};
    double render_displacement_beats{};
};

struct scroll_speed_query_result_t {
    std::optional<scroll_speed_state_t> state;
    std::optional<timing_diagnostic_t> error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return state.has_value();
    }
};

// Exact extent of one SPEEDS object's interpolation. Row coordinates are
// absolute and may be fractional; seconds preserve progress during stops,
// where a seconds-clock transition can advance without moving in row space.
// The end is clipped at a replacement speed event or chart end.
struct scroll_speed_transition_span_t {
    object_id_t source_event;
    signed_rational_t begin_row;
    signed_rational_t end_row;
    signed_rational_t begin_seconds;
    signed_rational_t end_seconds;
    bool transition_is_seconds{};
    bool clipped{};
};

struct scroll_speed_transition_span_result_t {
    std::optional<scroll_speed_transition_span_t> span;
    std::optional<timing_diagnostic_t> error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return span.has_value();
    }
};

struct scroll_speed_transition_spans_options_t {
    // Bulk callers must choose a finite upper bound. The default is large
    // enough for normal charts while keeping accidental projections bounded.
    std::size_t maximum_spans{100'000};
};

struct scroll_speed_transition_spans_result_t {
    // Successful spans are returned in compiled SPEEDS order. If an exact
    // evaluation fails, prior spans are retained and an error diagnostic is
    // included. A cap warning is reported separately through diagnostics.
    std::vector<scroll_speed_transition_span_t> spans;
    std::vector<timing_diagnostic_t> diagnostics;
    bool truncated{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return std::ranges::none_of(diagnostics, [](const auto& diagnostic) {
            return diagnostic.severity == diagnostic_severity_t::error;
        });
    }
};

[[nodiscard]] auto build_scroll_speed_index(const chart_document_t& chart,
                                            const timing_index_t& timing,
                                            scroll_speed_build_options_t options = {})
    -> scroll_speed_build_result_t;

// Recompute the stored source hash once when a chart revision is published or
// a cached evaluator is reused; this is O(scroll + speed events), so it should
// not be called in a per-frame render loop.
[[nodiscard]] auto is_scroll_speed_index_current(const scroll_speed_index_t& index,
                                              const chart_document_t& chart,
                                              const timing_index_t& timing) -> bool;


// The absolute row may be fractional, which lets playback and rendering query
// the same compiled curves between authored grid positions.
[[nodiscard]] auto scroll_speed_at(const scroll_speed_index_t& index, const timing_index_t& timing,
                                   signed_rational_t absolute_row,
                                   timing_boundary_side_t side = timing_boundary_side_t::event,
                                   bool extrapolate_before_chart = false)
    -> scroll_speed_query_result_t;

// Return the exact transition extent for a compiled SPEEDS object. Beat-clock
// endpoints are measured in musical rows; seconds-clock endpoints use the
// shared inverse timing evaluator, so stops and warps retain their evaluator
// semantics. A zero-duration transition has identical begin and end points.
[[nodiscard]] auto scroll_speed_transition_span(const scroll_speed_index_t& index,
                                                const timing_index_t& timing,
                                                object_id_t source_event)
    -> scroll_speed_transition_span_result_t;

// Return exact spans for the first maximum_spans compiled SPEEDS objects.
// The result preserves compiled order and reports both per-object errors and
// explicit truncation when the configured cap omits later spans.
[[nodiscard]] auto
scroll_speed_transition_spans(const scroll_speed_index_t& index, const timing_index_t& timing,
                              scroll_speed_transition_spans_options_t options = {})
    -> scroll_speed_transition_spans_result_t;

[[nodiscard]] auto
sample_frame_at(signed_rational_t seconds, std::uint32_t sample_rate,
                sample_rounding_t rounding = sample_rounding_t::nearest_ties_to_even)
    -> sample_frame_result_t;

enum class metronome_accent_t : std::uint8_t {
    regular,
    group,
    measure,
};

struct preview_audio_event_t {
    std::uint64_t sample_frame{};
    object_id_t object;
    resource_id_t resource;
    std::optional<lane_id_t> lane;
    std::size_t source_order{};
    std::size_t secondary_order{};
};

struct preview_visual_event_t {
    std::uint64_t sample_frame{};
    object_id_t object;
    resource_id_t resource;
    visual_event_kind_t kind{visual_event_kind_t::background};
    std::size_t draw_order{};
    std::size_t source_order{};

    auto operator==(const preview_visual_event_t&) const -> bool = default;
};

struct preview_metronome_event_t {
    std::uint64_t sample_frame{};
    measure_id_t measure;
    // Chart pulses are nonnegative and local to their measure. Count-in pulses
    // are negative ordinals relative to the first measure's downbeat.
    std::int64_t pulse{};
    metronome_accent_t accent{metronome_accent_t::regular};
};

struct preview_discontinuity_event_t {
    std::uint64_t sample_frame{};
    preview_discontinuity_kind_t kind{preview_discontinuity_kind_t::playable};
    object_id_t object;
    std::size_t source_order{};
    // Index in the corresponding immutable family queue. Timing objects use
    // their stable object ID and leave this at zero.
    std::size_t family_index{};

    auto operator==(const preview_discontinuity_event_t&) const -> bool = default;
};

struct preview_layer_tick_t {
    std::uint64_t sample_frame{};
    std::vector<layer_id_t> layers;
};

struct preview_schedule_t {
    std::uint32_t sample_rate{};
    timing_discontinuity_profile_t discontinuity_profile{
        timing_discontinuity_profile_t::canonical};
    std::vector<preview_audio_event_t> playable;
    std::vector<preview_audio_event_t> bgm;
    std::vector<preview_visual_event_t> bga;
    std::vector<preview_metronome_event_t> metronome;
    std::vector<preview_discontinuity_event_t> discontinuities;
    struct music_event_t {
        [[nodiscard]] static auto from(resource_id_t resource, std::uint64_t music_frame_count)
            -> std::optional<preview_schedule_t::music_event_t>;

        resource_id_t resource;
        std::uint64_t sample_frame{};
        std::uint64_t source_start_frame{};
        std::uint64_t frame_count{};
    };
    std::optional<music_event_t> music;
    std::vector<preview_layer_tick_t> layer_ticks;
};

[[nodiscard]] auto make_preview_music_event(resource_id_t resource,
                                            std::uint64_t music_frame_count)
    -> std::optional<preview_schedule_t::music_event_t>;
[[nodiscard]] auto preview_music_source_frame_at(
    const preview_schedule_t::music_event_t& music, std::uint64_t timeline_frame)
    -> std::optional<std::uint64_t>;

struct preview_schedule_options_t {
    std::uint32_t sample_rate{44'100};
    sample_rounding_t rounding{sample_rounding_t::nearest_ties_to_even};
    std::size_t maximum_playable_events{100'000};
    std::size_t maximum_bgm_events{100'000};
    std::size_t maximum_bga_events{100'000};
    std::size_t maximum_metronome_events{100'000};
    // Covers all four 100k presentation queues plus the 200k timing-event cap.
    std::size_t maximum_discontinuity_events{600'000};
    bool include_metronome{true};
    // Number of first-measure metronome pulses to schedule before row zero.
    // They use the row-zero BPM and meter, and are included only when their
    // exact audio time maps to a nonnegative sample frame.
    std::uint32_t count_in_pulses{};
    bool include_layer_ticks{};
    std::size_t maximum_layer_tick_events{200'000};
};

struct preview_schedule_result_t {
    std::optional<preview_schedule_t> schedule;
    std::vector<timing_diagnostic_t> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return schedule.has_value();
    }
};

[[nodiscard]] auto build_preview_schedule(const chart_document_t& chart,
                                          const timing_index_t& timing,
                                          preview_schedule_options_t options = {})
    -> preview_schedule_result_t;

struct preview_queue_head_t {
    std::size_t index{};
};

// Seek uses binary search. Realtime advancement only changes the small head
// handle and returns a view into the immutable contiguous queue.
template <typename event_t>
void seek_preview_queue(std::span<const event_t> events, preview_queue_head_t& head,
                        std::uint64_t sample_frame) {
    head.index = static_cast<std::size_t>(std::distance(
        events.begin(), std::lower_bound(events.begin(), events.end(), sample_frame,
                                         [](const event_t& event, std::uint64_t frame) {
                                             return event.sample_frame < frame;
                                         })));
}

template <typename event_t>
[[nodiscard]] auto advance_preview_queue(std::span<const event_t> events,
                                         preview_queue_head_t& head,
                                         std::uint64_t exclusive_end_frame)
    -> std::span<const event_t> {
    head.index = std::min(head.index, events.size());
    const auto begin = head.index;
    while (head.index < events.size() && events[head.index].sample_frame < exclusive_end_frame) {
        ++head.index;
    }
    return events.subspan(begin, head.index - begin);
}

} // namespace wormedit

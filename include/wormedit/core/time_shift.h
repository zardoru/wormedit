#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wormedit {

struct time_shift_limits_t {
    std::size_t maximum_selected_objects{100'000};
};

// Moves the named notes and BGM events by an exact signed number of chart rows
// (the chart's own division grid, counted across measure boundaries). Hold
// tails move with their heads.
struct shift_selection_action_t {
    chart_id_t chart;
    std::vector<object_id_t> notes;
    std::vector<object_id_t> bgm_events;
    std::int64_t rows{};
};

// Moves every beat-anchored object at or after `cursor`: notes whose head is at
// or after it (with their tails), BGM and BGA events, lane-layout segments,
// beat-anchored bookmarks and sections, and, only when `include_timing` is set,
// BPM, pause, warp, scroll, speed, kiai and timing-anchor events.
struct shift_from_cursor_action_t {
    chart_id_t chart;
    local_position_t cursor;
    std::int64_t rows{};
    bool include_timing{false};
};

struct time_shift_payload_t {
    std::size_t moved_objects{};
    // Audio-time-anchored bookmarks and sections that were left in place.
    std::size_t audio_anchored_skipped{};
};

using time_shift_result_t = command_result_with_t<time_shift_payload_t>;

// Both commands are all-or-nothing. A target before the first row or after the
// last materialized measure, a same-lane overlap, an invalid lane layout at the
// new position, or any object tied to retained BMS control flow or a retained
// BMSON sound-channel event rejects the whole command. A zero shift, or one
// that moves nothing, creates no history entry.
[[nodiscard]] auto apply_shift_selection(document_history_t& history,
                                         const shift_selection_action_t& action,
                                         time_shift_limits_t limits = {}) -> time_shift_result_t;

[[nodiscard]] auto apply_shift_from_cursor(document_history_t& history,
                                           const shift_from_cursor_action_t& action,
                                           time_shift_limits_t limits = {}) -> time_shift_result_t;

} // namespace wormedit

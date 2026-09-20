#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace wormedit {

struct bpm_edit_limits_t {
    std::size_t maximum_bpm_events{100'000};
    std::size_t maximum_chart_objects{1'000'000};
};

struct bpm_text_format_t {
    std::string decimal;
    bool decimal_exact{};
    std::string fraction;
};

enum class bpm_step_direction_t { decrement, increment };

// Parses positive decimal or fraction text without passing through binary
// floating point. Formatting emits an exact terminating decimal when it fits
// the configured digit cap; otherwise decimal is an explicitly approximate UI
// value while fraction remains the lossless representation.
[[nodiscard]] auto parse_bpm_text(std::string_view text) -> std::optional<signed_rational_t>;
[[nodiscard]] auto format_bpm_text(signed_rational_t bpm, std::size_t maximum_fraction_digits = 18)
    -> std::optional<bpm_text_format_t>;
[[nodiscard]] auto step_bpm_by_one(signed_rational_t bpm, bpm_step_direction_t direction)
    -> std::optional<signed_rational_t>;

struct insert_bpm_event_action_t {
    chart_id_t chart;
    local_position_t position;
    signed_rational_t beats_per_minute;
};

struct update_bpm_event_action_t {
    chart_id_t chart;
    object_id_t event;
    local_position_t position;
    signed_rational_t beats_per_minute;
};

struct delete_bpm_event_action_t {
    chart_id_t chart;
    object_id_t event;
};

using bpm_edit_action_t =
    std::variant<insert_bpm_event_action_t, update_bpm_event_action_t, delete_bpm_event_action_t>;

// Mutates one chart BPM event through shared document history. Local positions
// and exact positive rationals are preserved verbatim; same-row events remain
// legal and timing order uses source order before insertion order and stable ID.
// Insert assigns a fresh chart-wide object ID and an order after existing BPMs.
[[nodiscard]] auto apply_bpm_edit(document_history_t& history, std::uint64_t expected_revision,
                                  const bpm_edit_action_t& action, bpm_edit_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit

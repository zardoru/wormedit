#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/proposal.h>
#include <wormedit/core/result.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace wormedit {

struct bpm_edit_limits_t {
    std::size_t maximum_bpm_events{100'000};
    std::size_t maximum_chart_objects{default_maximum_chart_objects};
};

struct bpm_text_format_t {
    std::string decimal;
    bool decimal_exact{};
    std::string fraction;
};

enum class bpm_step_direction_t { decrement, increment };

// Parses positive decimal (including e/E exponents) or fraction text without passing through binary
// floating point. Formatting emits an exact terminating decimal when it fits
// the configured digit cap; otherwise decimal is an explicitly approximate UI
// value while fraction remains the lossless representation.
[[nodiscard]] auto parse_bpm_text(std::string_view text) -> std::optional<signed_rational_t>;
[[nodiscard]] auto format_bpm_text(signed_rational_t bpm, std::size_t maximum_fraction_digits = 18)
    -> std::optional<bpm_text_format_t>;
[[nodiscard]] auto step_bpm_by_one(signed_rational_t bpm, bpm_step_direction_t direction)
    -> std::optional<signed_rational_t>;

struct timing_bpm_value_t {
    signed_rational_t beats_per_minute{120, 1};

    [[nodiscard]] static auto with_defaults(chart_edit_value_default_policy_t policy)
        -> timing_bpm_value_t {
        timing_bpm_value_t value;
        if (policy == chart_edit_value_default_policy_t::command)
            value.beats_per_minute = {};
        return value;
    }
    [[nodiscard]] static auto from(const bpm_event_t& event) -> timing_bpm_value_t {
        return {.beats_per_minute = event.beats_per_minute};
    }
    auto operator==(const timing_bpm_value_t&) const -> bool = default;
};

using bpm_edit_value_t = positioned_edit_value_t<timing_bpm_value_t>;
using bpm_edit_action_t = chart_edit_t<bpm_edit_value_t>;

// Mutates one chart BPM event through shared document history. Local positions
// and exact positive rationals are preserved verbatim; same-row events remain
// legal and timing order uses source order before insertion order and stable ID.
// Insert assigns a fresh chart-wide object ID and an order after existing BPMs.
[[nodiscard]] auto apply_bpm_edit(document_history_t& history,
                                  const bpm_edit_action_t& action, bpm_edit_limits_t limits = {})
    -> document_command_result_t;

// Begins a fit anchored at a timing row. Updates the BPM event already at that
// row, or inserts one there when none exists (committed as one undoable edit).
struct beat_interval_drag_t {
    static constexpr proposal_capture_policy_t apply_policy{
        .stale_label = "Beat fit", .identity = document_identity_policy_t::revision_only};
    static constexpr proposal_capture_policy_t revision_policy{
        .stale_label = apply_policy.stale_label, .identity = apply_policy.identity,
        .stale_code = apply_policy.stale_code,
        .locked_code = diagnostic_code_t::unsupported_feature,
        .locked_message = "beat fitting is disabled while timing is locked"};
    [[nodiscard]] static auto validate_candidate(signed_rational_t target_bpm) -> std::optional<diagnostic_t>;
    [[nodiscard]] auto validate_apply(const chart_stamp_t&, bool timing_locked = false) const -> diagnostic_list_t;
    [[nodiscard]] auto apply(proposal_edit_scope_t<chart_stamp_t>&,
                             bool timing_locked = false) const
        -> document_command_result_t;

    [[nodiscard]] static auto propose(const song_document_t& document, chart_id_t chart,
                                  object_id_t bpm_event, local_position_t beat,
                                  std::uint32_t sample_rate, bool timing_locked = false)
        -> proposal_outcome_t<beat_interval_drag_t>;
    [[nodiscard]] static auto propose(const song_document_t& document, chart_id_t chart,
                                  local_position_t anchor, local_position_t beat,
                                  std::uint32_t sample_rate, bool timing_locked = false)
        -> proposal_outcome_t<beat_interval_drag_t>;
    [[nodiscard]] auto revise(const proposal_read_scope_t<chart_stamp_t>& scope,
                              std::uint64_t target_sample_frame) const
        -> proposal_outcome_t<beat_interval_drag_t>;
    [[nodiscard]] auto revise(const proposal_read_scope_t<chart_stamp_t>& scope,
                              signed_rational_t target_bpm) const
        -> proposal_outcome_t<beat_interval_drag_t>;

    // Existing BPM event being updated, or a placeholder id when insert_bpm is set.
    object_id_t bpm_event;
    // Anchor row: the existing BPM event's position, or where a new BPM event is inserted.
    local_position_t original_bpm_position;
    bool insert_bpm{};
    local_position_t beat;
    std::uint32_t sample_rate{};
    std::uint64_t original_sample_frame{};
    std::uint64_t proposed_sample_frame{};
    signed_rational_t original_bpm;
    signed_rational_t proposed_bpm;
    bool snap_active{};
    signed_rational_t snap_step{1, 1};

    auto operator==(const beat_interval_drag_t&) const -> bool = default;
private:
    [[nodiscard]] static auto validate_initial_input(bool timing_locked, std::uint32_t sample_rate)
        -> std::optional<diagnostic_t>;
    [[nodiscard]] static auto from(const song_document_t&, const chart_document_t&,
        const bpm_event_t* existing, local_position_t anchor, local_position_t beat, std::uint32_t sample_rate)
        -> proposal_outcome_t<beat_interval_drag_t>;
    [[nodiscard]] auto prepare_snapped_revision(const proposal_read_scope_t<chart_stamp_t>&,
        signed_rational_t target_bpm, proposal_t<beat_interval_drag_t>&& updated) const
        -> proposal_outcome_t<beat_interval_drag_t>;
};

static_assert(staged_proposal<beat_interval_drag_t,
    std::tuple<const song_document_t&, chart_id_t, object_id_t, local_position_t, std::uint32_t, bool>,
    std::tuple<const proposal_read_scope_t<chart_stamp_t>&, std::uint64_t>>);
static_assert(staged_proposal<beat_interval_drag_t,
    std::tuple<const song_document_t&, chart_id_t, local_position_t, local_position_t, std::uint32_t, bool>,
    std::tuple<const proposal_read_scope_t<chart_stamp_t>&, signed_rational_t>>);

using beat_interval_drag_outcome_t = proposal_outcome_t<beat_interval_drag_t>;

[[nodiscard]] auto quantize_bpm(signed_rational_t bpm, signed_rational_t step)
    -> std::optional<signed_rational_t>;
// Temporary: removed by T-637; the Sys native edit dispatcher still supplies existing transactions.
[[nodiscard]] auto apply_bpm_edit(document_command_transaction_t& transaction,
    const bpm_edit_action_t& action, bpm_edit_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit

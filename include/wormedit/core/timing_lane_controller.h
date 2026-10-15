#pragma once

#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/proposal.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/pause_warp_editor.h>
#include <wormedit/core/scroll_speed_editor.h>
#include <wormedit/core/timing_anchor_editor.h>

#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace wormedit {

// The caller starts this controller after the platform's double-click
// recognizer confirms either an empty timing-lane row or one stable existing
// marker. Insert positions are retained verbatim from the active snap;
// existing positions and values are loaded exactly from the document.
enum class timing_lane_kind_t {
    bpm,
    scroll,
    speed,
    stop,
    delay,
    warp,
    kiai,
    anchor,
};

struct timing_lane_event_ref_t {
    timing_lane_kind_t kind{timing_lane_kind_t::bpm};
    object_id_t event;
};

enum class timing_lane_initial_value_source_t {
    explicit_default,
    effective_at_position,
};

// A warp's start is the pseudo-note row; its end is required user input.

using timing_lane_value_t =
    std::variant<timing_bpm_value_t, timing_scroll_value_t, timing_speed_value_t,
                 timing_pause_value_t, timing_warp_value_t, timing_kiai_value_t,
                 timing_anchor_value_t>;

enum class timing_lane_pseudonote_status_t {
    inactive,
    editing,
    committed,
    cancelled,
    rejected,
};

// Editor data remains visible after a failed revision, independently of applicability.
struct timing_lane_pseudonote_state_t {
    timing_lane_pseudonote_status_t status{timing_lane_pseudonote_status_t::inactive};
    timing_lane_kind_t kind{timing_lane_kind_t::bpm};
    local_position_t position;
    timing_lane_value_t value{timing_bpm_value_t{}};
    std::optional<object_id_t> existing_event;
    timing_lane_initial_value_source_t initial_value_source{
        timing_lane_initial_value_source_t::explicit_default};
    auto operator==(const timing_lane_pseudonote_state_t&) const -> bool = default;

    void normalize_value();
    void set_value(timing_lane_value_t&& next);
};

struct timing_lane_pseudonote_preview_t;
class timing_lane_pseudonote_outcome_t;
using timing_lane_pseudonote_proposal_t = proposal_t<timing_lane_pseudonote_preview_t>;

struct timing_lane_pseudonote_preview_t {
    [[nodiscard]] static auto propose(const document_history_t& history, chart_id_t chart,
        timing_lane_pseudonote_state_t initial, bool timing_locked) -> timing_lane_pseudonote_outcome_t;
    [[nodiscard]] static auto propose(const document_history_t& history, chart_id_t chart,
        timing_lane_event_ref_t event, bool timing_locked) -> timing_lane_pseudonote_outcome_t;
    [[nodiscard]] static auto revise(timing_lane_pseudonote_outcome_t previous,
        const document_history_t& history, timing_lane_value_t value, bool timing_locked)
        -> timing_lane_pseudonote_outcome_t;
    [[nodiscard]] static auto revise(timing_lane_pseudonote_outcome_t previous,
        const document_history_t& history, local_position_t exact_snapped_position, bool timing_locked)
        -> timing_lane_pseudonote_outcome_t;

    timing_lane_pseudonote_state_t report;
    auto operator==(const timing_lane_pseudonote_preview_t&) const -> bool = default;
};

// This editor retains rejected display state beside its plain proposal outcome.
// Diagnostics remain an ordinary list; retained reports stay in their payload.
class timing_lane_pseudonote_outcome_t : public proposal_outcome_t<timing_lane_pseudonote_preview_t> {
    using outcome_type = proposal_outcome_t<timing_lane_pseudonote_preview_t>;

  public:
    timing_lane_pseudonote_outcome_t() = default;
    explicit timing_lane_pseudonote_outcome_t(timing_lane_pseudonote_proposal_t editor)
        : outcome_type{.value = std::move(editor), .diagnostics = {}} {}

    [[nodiscard]] auto editor() noexcept -> timing_lane_pseudonote_proposal_t* {
        return value ? &*value : rejected_ ? &*rejected_ : nullptr;
    }
    [[nodiscard]] auto editor() const noexcept -> const timing_lane_pseudonote_proposal_t* {
        return value ? &*value : rejected_ ? &*rejected_ : nullptr;
    }
    [[nodiscard]] auto report() noexcept -> timing_lane_pseudonote_state_t* {
        auto* captured = editor();
        return captured ? &captured->payload.report : nullptr;
    }
    [[nodiscard]] auto report() const noexcept -> const timing_lane_pseudonote_state_t* {
        const auto* captured = editor();
        return captured ? &captured->payload.report : nullptr;
    }
    [[nodiscard]] auto captured_stamp() const noexcept -> const chart_stamp_t& {
        const auto* captured = editor();
        static constexpr chart_stamp_t empty{};
        return captured ? captured->stamp : empty;
    }
    void complete(bool is_accepted) {
        if (is_accepted) {
            if (!value && rejected_) value = std::move(rejected_);
            rejected_.reset();
        } else if (value) {
            rejected_ = std::move(value);
            value.reset();
        }
    }


  private:
    std::optional<timing_lane_pseudonote_proposal_t> rejected_;
};

static_assert(std::is_nothrow_move_constructible_v<timing_lane_pseudonote_outcome_t>);
static_assert(std::is_nothrow_move_assignable_v<timing_lane_pseudonote_outcome_t>);
static_assert(!std::is_polymorphic_v<timing_lane_pseudonote_outcome_t>);

static_assert(staged_proposal<timing_lane_pseudonote_preview_t,
    std::tuple<const document_history_t&, chart_id_t, timing_lane_pseudonote_state_t, bool>,
    std::tuple<timing_lane_pseudonote_outcome_t, const document_history_t&, timing_lane_value_t, bool>,
    chart_stamp_t, diagnostic_list_t>);
static_assert(staged_proposal<timing_lane_pseudonote_preview_t,
    std::tuple<const document_history_t&, chart_id_t, timing_lane_event_ref_t, bool>,
    std::tuple<timing_lane_pseudonote_outcome_t, const document_history_t&, local_position_t, bool>,
    chart_stamp_t, diagnostic_list_t>);

// BPM, scroll/SV, speed, stop, delay, warp, kiai, and timing-anchor lanes are
// supported. Meter editing is outside this controller because the current
// typed editor command set has no meter-event insertion command. The caller
// supplies a value resolved either from an explicit default or the effective
// value at the cursor; this controller does not guess timing projections.
// BPM in force at an exact position: the latest valid BPM event at or before it
// (ties broken by source order), or nullopt when the chart has none or the
// measure index cannot be built. Used as the default for a newly inserted BPM.
[[nodiscard]] auto effective_bpm_at(const chart_document_t& chart, local_position_t position)
    -> std::optional<signed_rational_t>;

// Moves a bounded selection by the same signed number of chart rows. A staged
// history applies each typed update; publication is one atomic undoable command.
// The fixed effective initial BPM cannot be part of the selection.
[[nodiscard]] auto move_timing_lane_event_group(
    document_history_t& history, chart_id_t chart, std::span<const timing_lane_event_ref_t> events,
    std::int64_t row_delta, bool timing_locked)
    -> document_command_result_t;

// Confirmation validates the captured preview and calls the typed timing
// command API, using an update command for an existing event. No playable note
// is ever created by this controller.
[[nodiscard]] auto apply_timing_lane_pseudonote(document_history_t& history,
                                                  timing_lane_pseudonote_outcome_t& preview,
                                                  bool timing_locked) -> document_command_result_t;

// Deletes an existing marker through its typed editor command. The event kind,
// identity and lock are checked before history changes. Delayed callers validate their stamp.
[[nodiscard]] auto delete_existing_timing_lane_event(document_history_t& history,
                                                      chart_id_t chart, timing_lane_kind_t kind,
                                                      object_id_t event,
                                                      bool timing_locked)
    -> document_command_result_t;

// Deletes a bounded selection of timing markers as one atomic undoable command. The fixed
// initial BPM is skipped when other markers are selected; a lock rejects all.
// Delayed callers validate their captured stamp before this synchronous command.
[[nodiscard]] auto delete_timing_lane_event_group(
    document_history_t& history, chart_id_t chart, std::span<const timing_lane_event_ref_t> events, bool timing_locked) -> document_command_result_t;

// One speed event as a span: its begin row and the length of its transition. Either part may be
// left unchanged. Applies as one undoable update that keeps the event's identity and multiplier.
struct speed_span_edit_t {
    std::optional<local_position_t> position;
    // Transition length in the event's own clock (beats, or seconds when it is a seconds clock).
    std::optional<rational_t> transition;
};

[[nodiscard]] auto edit_speed_event_span(document_history_t& history, chart_id_t chart,
                                         object_id_t event, const speed_span_edit_t& edit,
                                         bool timing_locked) -> document_command_result_t;

void cancel_timing_lane_pseudonote(timing_lane_pseudonote_outcome_t& preview) noexcept;

} // namespace wormedit

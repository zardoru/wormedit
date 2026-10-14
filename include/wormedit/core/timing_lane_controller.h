#pragma once

#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/command.h>
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

struct timing_lane_pseudonote_preview_t {
    timing_lane_pseudonote_status_t status{timing_lane_pseudonote_status_t::inactive};
    chart_stamp_t stamp;
    timing_lane_kind_t kind{timing_lane_kind_t::bpm};
    local_position_t position;
    timing_lane_value_t value{timing_bpm_value_t{}};
    std::optional<object_id_t> existing_event;
    timing_lane_initial_value_source_t initial_value_source{
        timing_lane_initial_value_source_t::explicit_default};
    diagnostic_list_t diagnostics;

    void set_value(timing_lane_value_t&& next);

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return status == timing_lane_pseudonote_status_t::committed ||
               (status == timing_lane_pseudonote_status_t::editing && diagnostics.empty());
    }
};

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

[[nodiscard]] auto
begin_timing_lane_pseudonote(const document_history_t& history, chart_id_t chart,
                             timing_lane_kind_t kind, local_position_t exact_snapped_position,
                             timing_lane_value_t initial_value,
                             timing_lane_initial_value_source_t initial_value_source,
                             bool timing_locked) -> timing_lane_pseudonote_preview_t;

// Opens an existing typed timing event for property editing. The event's exact
// position and value are loaded from the document; the target ID is retained
// so confirmation updates that event in place rather than inserting another.
// A known object of another kind, an ambiguous ID, a missing event, or a lock
// rejects the preview without changing history.
[[nodiscard]] auto begin_existing_timing_lane_pseudonote(const document_history_t& history,
                                                         chart_id_t chart, timing_lane_kind_t kind,
                                                         object_id_t event, bool timing_locked)
    -> timing_lane_pseudonote_preview_t;

// Updates only the transient preview. Invalid typed values remain visible in
// the preview with diagnostics and cannot be confirmed. A lock or revision
// change cancels the preview immediately.
[[nodiscard]] auto revise_timing_lane_pseudonote(const document_history_t& history,
                                                 timing_lane_pseudonote_preview_t& preview,
                                                 timing_lane_value_t value, bool timing_locked)
    -> bool;

// Moves an open existing marker through the same transient revision-checked
// preview. The caller supplies an already-snapped exact position; confirmation
// remains one typed update command and preserves the marker's stable ID and
// source provenance. New pseudo-notes keep their insertion row fixed.
[[nodiscard]] auto revise_existing_timing_lane_pseudonote_position(
    const document_history_t& history, timing_lane_pseudonote_preview_t& preview,
    local_position_t exact_snapped_position, bool timing_locked) -> bool;

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
                                                  timing_lane_pseudonote_preview_t& preview,
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

void cancel_timing_lane_pseudonote(timing_lane_pseudonote_preview_t& preview) noexcept;

} // namespace wormedit

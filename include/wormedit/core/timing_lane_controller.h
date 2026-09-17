#pragma once

#include <wormedit/core/command.h>

#include <cstdint>
#include <optional>
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

enum class timing_lane_initial_value_source_t {
    explicit_default,
    effective_at_position,
};

struct timing_bpm_value_t {
    signed_rational_t beats_per_minute{120, 1};
};

struct timing_scroll_value_t {
    signed_rational_t multiplier{1, 1};
};

struct timing_speed_value_t {
    signed_rational_t multiplier{1, 1};
    rational_t transition_beats{};
    bool transition_is_seconds{};
};

struct timing_pause_value_t {
    signed_rational_t duration{};
    pause_unit_t source_unit{pause_unit_t::seconds};
};

// A warp's start is the pseudo-note row; its end is required user input.
struct timing_warp_value_t {
    local_position_t end;
    warp_provenance_t source_encoding{warp_provenance_t::native};
};

struct timing_kiai_value_t {
    bool enabled{true};
    std::uint32_t retained_effect_bits{};
};

struct timing_anchor_value_t {
    std::uint64_t source_sample_frame{};
    bool locked{};
};

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
    chart_id_t chart;
    timing_lane_kind_t kind{timing_lane_kind_t::bpm};
    local_position_t position;
    timing_lane_value_t value{timing_bpm_value_t{}};
    std::optional<object_id_t> existing_event;
    timing_lane_initial_value_source_t initial_value_source{
        timing_lane_initial_value_source_t::explicit_default};
    std::uint64_t document_revision{};
    bool valid{};
    diagnostic_list_t diagnostics;
};

// BPM, scroll/SV, speed, stop, delay, warp, kiai, and timing-anchor lanes are
// supported. Meter editing is outside this controller because the current
// typed editor command set has no meter-event insertion command. The caller
// supplies a value resolved either from an explicit default or the effective
// value at the cursor; this controller does not guess timing projections.
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
[[nodiscard]] auto update_timing_lane_pseudonote(const document_history_t& history,
                                                 timing_lane_pseudonote_preview_t& preview,
                                                 timing_lane_value_t value, bool timing_locked)
    -> bool;

// Moves an open existing marker through the same transient revision-checked
// preview. The caller supplies an already-snapped exact position; confirmation
// remains one typed update command and preserves the marker's stable ID and
// source provenance. New pseudo-notes keep their insertion row fixed.
[[nodiscard]] auto move_existing_timing_lane_pseudonote(
    const document_history_t& history, timing_lane_pseudonote_preview_t& preview,
    local_position_t exact_snapped_position, bool timing_locked) -> bool;

// Confirmation makes exactly one call to the existing revision-checked timing
// command API, using an update command for an existing event. No playable note
// is ever created by this controller.
[[nodiscard]] auto confirm_timing_lane_pseudonote(document_history_t& history,
                                                  timing_lane_pseudonote_preview_t& preview,
                                                  bool timing_locked) -> document_command_result_t;

// Deletes an existing marker through its typed editor command. The event kind,
// identity, lock, and expected revision are all checked before history changes.
[[nodiscard]] auto delete_existing_timing_lane_event(document_history_t& history,
                                                      chart_id_t chart, timing_lane_kind_t kind,
                                                      object_id_t event,
                                                      std::uint64_t expected_revision,
                                                      bool timing_locked)
    -> document_command_result_t;

void cancel_timing_lane_pseudonote(timing_lane_pseudonote_preview_t& preview) noexcept;

} // namespace wormedit

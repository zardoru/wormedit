#pragma once

// Editor actions and selection operations over the document history.

#include <wormedit/core/interaction_state.h>
#include <wormedit/core/time_shift.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace wormedit {

// Drops selected notes, objects and retained spans that no longer exist in `chart`. Runs a
// chart scan only when the document revision changed since the last check and something is
// selected, so a steady frame costs nothing. Never mutates the document. Returns true if the
// selection changed.
auto prune_stale_selection(const chart_document_t& chart, std::uint64_t document_revision,
                           editor_interaction_state_t& state) -> bool;

// The chart source family determines which authoring kinds are available.
[[nodiscard]] auto is_note_kind_allowed(const chart_document_t& chart, note_kind_t kind) -> bool;
[[nodiscard]] auto arm_note_kind(const document_history_t& history,
                                 editor_interaction_state_t& state, note_kind_t kind)
    -> editor_interaction_result_t;

// Dispatches one typed frontend action. Document edits are published through
// commit_document_command so validation, undo, and redo share one boundary.
[[nodiscard]] auto dispatch_editor_action(document_history_t& history,
                                          editor_interaction_state_t& state,
                                          const editor_interaction_action_t& action)
    -> editor_interaction_result_t;

// Typed objects are authoritative; legacy note-only callers fall back to their note list.
[[nodiscard]] auto editor_selection_targets(const editor_interaction_state_t& state)
    -> std::vector<editor_selection_object_t>;
[[nodiscard]] auto is_editor_selection_scope_accepted(editor_selection_scope_t scope,
    editor_selection_object_kind_t kind) -> bool;

// Select/invert only the requested families; selections outside that scope survive. With `from`,
// only objects at or after that position count (an object spanning it counts when it ends there or
// later).
[[nodiscard]] auto select_editor_scope(document_history_t& history, editor_interaction_state_t& state,
    editor_selection_scope_t scope, bool invert,
    std::optional<local_position_t> from = std::nullopt) -> editor_interaction_result_t;

// Canonical typed selection for Quantize preview and Apply. Existing BGM and note IDs
// may coincide; unrelated selected object families do not participate.
[[nodiscard]] auto editor_quantize_selection(const editor_interaction_state_t& state)
    -> quantize_selection_t;

// The step-move request for the current selection: selected notes and bar lines move by `rows`
// under the session's bar-line time policy.
[[nodiscard]] auto editor_shift_selection(const editor_interaction_state_t& state, std::int64_t rows)
    -> shift_selection_action_t;

} // namespace wormedit

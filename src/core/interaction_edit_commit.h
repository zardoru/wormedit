#pragma once

#include "interaction_internal.h"
#include <wormedit/core/interaction_cursor_pose.h>

namespace wormedit {

struct editor_edit_effect_t {
    std::string label;
    bool has_document_changes{true};
    std::optional<std::vector<object_id_t>> selection;
    std::optional<cursor_pose_t> cursor_pose;
    std::optional<local_position_t> cursor;
    std::optional<lane_id_t> lane;
    std::optional<object_id_t> created_note;
    std::optional<editor_object_audition_request_t> audition;
    bool consume_primary_hold{};
    bool retain_commit_diagnostics{};

    void apply_capture(editor_interaction_state_t& state, std::uint64_t revision,
                       bool changed) const {
        if (consume_primary_hold) {
            if (auto* holds = state.gesture<hold_gesture_t>()) {
                holds->discard_primary();
                if (!holds->has_pending())
                    state.clear();
            }
        }
        if (changed && state.active_gesture)
            std::visit(
                [revision](auto& gesture) { gesture.stamp = document_stamp_t::from(revision); },
                *state.active_gesture);
    }

    void apply_cursor(editor_interaction_state_t& state) const {
        if (cursor_pose)
            restore_cursor_pose(state, *cursor_pose);
        if (cursor)
            place_cursor_at(state, *cursor);
        if (lane)
            state.active_lane = *lane;
    }
};

struct preserve_editor_selection_t {
    void operator()(editor_interaction_state_t&) const {}
};

// Complex mixed selections project their existing domain ranges directly into state after
// publication. The compile-time effect avoids an extra allocated selection or type erasure.
template <class SelectionEffect>
[[nodiscard]] auto
finish_editor_edit(document_history_t& history, editor_interaction_state_t& state,
                   std::uint64_t before_revision, document_command_result_t command,
                   editor_edit_effect_t effect, SelectionEffect select)
    -> editor_interaction_result_t {
    if ((effect.has_document_changes && !command.applied) ||
        diagnostic_queries::has_errors(command.diagnostics))
        return {.diagnostics = std::move(command.diagnostics)};
    const auto changed = history.document.revision != before_revision;
    if (effect.selection)
        interaction_detail::set_selected_notes(state, std::move(*effect.selection));
    select(state);
    effect.apply_capture(state, history.document.revision, changed);
    effect.apply_cursor(state);
    if (effect.audition)
        effect.audition->document_revision = history.document.revision;
    auto result = editor_interaction_result_t::from(history, state, changed);
    result.created_note = effect.created_note;
    result.audition = effect.audition;
    if (effect.retain_commit_diagnostics)
        result.diagnostics = std::move(command.diagnostics);
    return result;
}

template <class SelectionEffect = preserve_editor_selection_t>
[[nodiscard]] auto commit_editor_edit(document_history_t& history,
                                      editor_interaction_state_t& state,
                                      document_command_transaction_t& transaction,
                                      editor_edit_effect_t effect, SelectionEffect select = {})
    -> editor_interaction_result_t {
    const auto before_revision = history.document.revision;
    auto command = effect.has_document_changes ? transaction.commit(std::move(effect.label))
                                               : transaction.finish_without_commit();
    return finish_editor_edit(history, state, before_revision, std::move(command),
                              std::move(effect), std::move(select));
}

template <class SelectionEffect = preserve_editor_selection_t>
[[nodiscard]] auto commit_editor_edit(editor_edit_context_t& context,
                                      document_command_transaction_t& transaction,
                                      editor_edit_effect_t effect, SelectionEffect select = {})
    -> editor_interaction_result_t {
    return commit_editor_edit(context.history, context.state, transaction, std::move(effect),
                              std::move(select));
}

// Domain commands with their own transaction keep that owner; publication of editor effects
// still has exactly the same success/failure boundary as a locally staged transaction.
template <class Command, class SelectionEffect = preserve_editor_selection_t>
    requires std::invocable<Command&>
[[nodiscard]] auto commit_editor_edit(editor_edit_context_t& context, editor_edit_effect_t effect,
                                      Command command, SelectionEffect select = {})
    -> editor_interaction_result_t {
    const auto before_revision = context.history.document.revision;
    auto committed = command();
    return finish_editor_edit(context.history, context.state, before_revision, std::move(committed),
                              std::move(effect), std::move(select));
}

} // namespace wormedit

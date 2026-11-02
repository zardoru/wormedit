#include "sys/storyboard/storyboard_runtime.h"
#include "sys/app/application_command_state.h"
#include "sys/storyboard/storyboard_inspector_view.h"
#include "sys/storyboard/storyboard_assets_view.h"
#include "sys/storyboard/storyboard_timeline_view.h"
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/osbx_edit.h>
#include <wormedit/core/osbx_lua_link.h>
#include <wormedit/core/storyboard_canvas.h>
#include <array>
#include <cmath>
#include <algorithm>
#include <concepts>
#include <type_traits>

namespace wormedit::sys {
namespace {
auto node_exists(const storyboard_document_t& storyboard, std::string_view id) -> bool {
    if (storyboard.osbx) {
        const auto prepared = osbx_lua_prepared_scope_t::find(*storyboard.osbx);
        const auto& scene = prepared ? *prepared : *storyboard.osbx;
        return std::ranges::find(scene.nodes, id, &osbx_node_t::id) != scene.nodes.end();
    }
    return storyboard.model && std::ranges::any_of(storyboard.model->sprites, [&](const auto& sprite) {
        return storyboard_canvas_target_t::legacy_node_id(sprite.stable_id) == id;
    });
}
auto valid_viewport(const ui::storyboard_viewport_t& viewport) -> bool {
    return std::isfinite(viewport.zoom) && viewport.zoom >= storyboard_canvas_geometry_t::minimum_zoom &&
        viewport.zoom <= storyboard_canvas_geometry_t::maximum_zoom && std::isfinite(viewport.pan_x) &&
        std::isfinite(viewport.pan_y) && std::isfinite(viewport.timeline_begin_ms) &&
        std::isfinite(viewport.timeline_pixels_per_ms) && viewport.timeline_pixels_per_ms > 0 &&
        viewport.background >= ui::storyboard_canvas_background_t::theme && viewport.background <= ui::storyboard_canvas_background_t::white;
}
auto view_feedback() -> compose_action_outcome_t {
    return compose_action_outcome_t::from({}, notification_severity_t::hint,
        notification_source_t::input, compose_feedback_delivery_t::none);
}
void pause_storyboard_transport(const application_command_state_t& application) {
    if (application.services.transport_preview_playing && application.services.transport_preview_playing() &&
        application.services.transport_play_pause)
        (void)application.services.transport_play_pause();
}
auto plan_hierarchy_edit(const ui::storyboard_hierarchy_mutation_t& action)
    -> result_t<osbx_edit_change_t, diagnostic_t> {
    using kind_t = ui::storyboard_hierarchy_mutation_kind_t;
    switch (action.kind) {
    case kind_t::add_group:
    case kind_t::add_sprite:
        return osbx_add_node_t{.kind = action.kind == kind_t::add_group ? osbx_node_kind_t::group : osbx_node_kind_t::sprite,
            .parent = action.parent, .layer = action.layer, .image = action.image};
    case kind_t::erase: return osbx_delete_node_t{.node = action.node};
    case kind_t::rename: return osbx_rename_node_t{.node = action.node, .new_id = action.text};
    case kind_t::duplicate: return osbx_duplicate_node_t{.node = action.node};
    case kind_t::drop: return osbx_drop_node_t{.node = action.node, .parent = action.parent,
        .before = action.before, .layer = action.layer};
    case kind_t::set_layer: return osbx_set_layer_t{.node = action.node, .layer = action.layer};
    case kind_t::none:
    case kind_t::toggle_editor_visibility:
        return std::unexpected(error(diagnostic_code_t::invalid_binding, "This hierarchy action is not a document edit."));
    }
    return std::unexpected(error(diagnostic_code_t::invalid_id, "Invalid hierarchy action."));
}
auto hierarchy_edit_reason(const storyboard_document_t& storyboard,
                           const ui::storyboard_hierarchy_mutation_t& action) -> std::string {
    const std::array<std::string_view, 3> targets{action.node,
        action.parent ? std::string_view{*action.parent} : std::string_view{},
        action.before ? std::string_view{*action.before} : std::string_view{}};
    for (const auto id : targets) {
        if (id.empty() || !node_exists(storyboard, id)) continue;
        if (auto reason = storyboard_node_edit_reason(storyboard, id); !reason.empty()) return reason;
    }
    return {};
}
auto inspector_track_kind(ui::storyboard_inspector_field_t field) -> std::optional<osbx_track_kind_t> {
    using field_t = ui::storyboard_inspector_field_t;
    switch (field) {
    case field_t::transform: return osbx_track_kind_t::transform;
    case field_t::opacity: return osbx_track_kind_t::opacity;
    case field_t::color: return osbx_track_kind_t::color;
    case field_t::blend: return osbx_track_kind_t::blend;
    default: return {};
    }
}
auto is_matching_inspector_target(const osbx_edit_change_t& change,
                                  const ui::storyboard_inspector_action_t& action) -> bool {
    return std::visit([&](const auto& edit) {
        using edit_t = std::remove_cvref_t<decltype(edit)>;
        if constexpr (requires { edit.track; }) {
            const auto kind = inspector_track_kind(action.field);
            if (!kind || edit.track != osbx_track_target_t{.node = action.node, .kind = *kind, .op = action.op}) return false;
            if constexpr (std::same_as<edit_t, osbx_add_key_t>) return edit.time_ms == action.key_time_ms;
            else if constexpr (std::same_as<edit_t, osbx_set_key_value_t>) return action.key && edit.key == *action.key;
            else return std::same_as<edit_t, osbx_set_default_t>;
        } else if constexpr (std::same_as<edit_t, osbx_set_order_t>)
            return action.field == ui::storyboard_inspector_field_t::order && edit.node == action.node;
        else if constexpr (std::same_as<edit_t, osbx_set_lifetime_t>)
            return action.field == ui::storyboard_inspector_field_t::lifetime && edit.node == action.node;
        else return false;
    }, change);
}
auto is_matching_storyboard_target(const osbx_edit_change_t& change, const ui::storyboard_action_t& action) -> bool {
    if (action.kind == ui::storyboard_action_kind_t::inspector)
        return is_matching_inspector_target(change, action.inspector);
    const auto* move = std::get_if<osbx_move_keys_t>(&change);
    return action.kind == ui::storyboard_action_kind_t::timeline &&
        action.timeline.kind == ui::storyboard_timeline_action_kind_t::move_keys && move && move->keys == action.timeline.keys;
}
auto dispatch_storyboard_gesture(ui::storyboard_inspector_phase_t phase, const osbx_edit_t& edit,
    const document_edit_inputs_t& inputs, editor_interaction_state_t* state) -> compose_action_outcome_t {
    if (!state) return compose_action_outcome_t::blocked("Storyboard input is unavailable.");
    using phase_t = ui::storyboard_inspector_phase_t;
    if (phase == phase_t::begin)
        return compose_action_outcome_t::from(dispatch_editor_action(inputs.history, *state,
            begin_osbx_edit_gesture_action_t{.edit = edit}), "Storyboard edit started.");
    return compose_action_outcome_t::from(dispatch_editor_action(inputs.history, *state,
        update_osbx_edit_gesture_action_t{.change = edit.change,
            .operation = phase == phase_t::apply ? proposal_operation_t::apply : proposal_operation_t::revise}),
        phase == phase_t::apply ? "Storyboard property changed." : "Storyboard edit staged.");
}
auto timeline_plan_diagnostic(failure_code_t code) -> diagnostic_t {
    if (code == failure_code_t::arithmetic_overflow)
        return error(diagnostic_code_t::invalid_resolution, "Storyboard key time exceeds the signed millisecond range.");
    if (code == failure_code_t::unavailable)
        return error(diagnostic_code_t::invalid_binding, "Storyboard track is read-only or its timing grid is unavailable.");
    return error(diagnostic_code_t::invalid_binding, "Storyboard key or time is invalid or no longer current.");
}
} // namespace
void storyboard_runtime_t::reconcile(const song_document_t& document) {
    const auto storyboard = std::ranges::find(document.storyboards, state_.selection.storyboard, &storyboard_document_t::id);
    if (storyboard == document.storyboards.end()) {
        state_.selection = {};
        state_.hidden_nodes.clear();
        return;
    }
    std::erase_if(state_.selection.nodes, [&](const auto& id) { return !node_exists(*storyboard, id); });
    std::erase_if(state_.hidden_nodes, [&](const auto& id) { return !node_exists(*storyboard, id); });
}
void storyboard_runtime_t::refill(ui::storyboard_view_t& view, const song_document_t& document,
                                  std::uint64_t session_generation) {
    // Auxiliary preview controls can be used before this tab is ever opened.
    // Their borrowed frame view must carry the same current project capture.
    view.project = document.id;
    view.revision = document.revision;
    view.session_generation = session_generation;
    if (project_ != document.id || session_generation_ != session_generation) {
        state_ = {};
        thumbnail_images_.clear(); thumbnail_resources_.clear();
        timeline_grid_enabled_ = false;
        timeline_preview_delta_.reset(); timeline_preview_reason_.clear();
        project_ = document.id;
        session_generation_ = session_generation;
    }
    reconcile(document);
    if (!view.visible || !state_.selection.storyboard) { thumbnail_images_.clear(); thumbnail_resources_.clear(); }
    view.refill_state(state_);
    for (auto& row : view.assets.images) row.thumbnail_requested = std::ranges::find(thumbnail_images_, row.id) != thumbnail_images_.end();
    for (auto& row : view.assets.resources) row.thumbnail_requested = std::ranges::find(thumbnail_resources_, row.resource) != thumbnail_resources_.end();
    view.timeline.grid_enabled = timeline_grid_enabled_;
    view.timeline.preview_delta_ms = timeline_preview_delta_;
    view.timeline.preview_reason = timeline_preview_reason_;
}
auto storyboard_runtime_t::edit(const ui::storyboard_action_t& action,
                                const document_edit_inputs_t& inputs,
                                const application_command_state_t& application) -> compose_action_outcome_t {
    if (action.kind == ui::storyboard_action_kind_t::create &&
        inputs.history.document.storyboards.size() >= inputs.workspace.limits.maximum_storyboards)
        return compose_action_outcome_t::from(error(diagnostic_code_t::invalid_resolution,
            "The project has reached its storyboard limit."), notification_source_t::edit);
    if (action.kind != ui::storyboard_action_kind_t::create && action.storyboard != state_.selection.storyboard)
        return compose_action_outcome_t::from(error(diagnostic_code_t::invalid_binding,
            stale_action_message("Storyboard selection")), notification_source_t::edit);
    if (action.kind == ui::storyboard_action_kind_t::detach_generator &&
        action.prepared_generation != osbx_lua_prepared_scope_t::generation())
        return compose_action_outcome_t::from(error(diagnostic_code_t::invalid_binding,
            stale_action_message("Storyboard prepared source")), notification_source_t::edit);
    pause_storyboard_transport(application);
    if (action.kind == ui::storyboard_action_kind_t::detach_generator)
        return compose_action_outcome_t::from(apply_osbx_edit(inputs.history, {.storyboard = action.storyboard,
            .change = osbx_detach_storyboard_t{.expected_owner = action.expected_owner}}),
            {.success = "Storyboard detached from generator.", .unchanged = "Storyboard was unchanged.",
             .rebind_failure = "Storyboard detached, but workspace rebinding failed."});
    const bool creating = action.kind == ui::storyboard_action_kind_t::create;
    const osbx_edit_t request{.storyboard = action.storyboard,
        .change = creating ? osbx_edit_change_t{osbx_create_storyboard_t{
            .name = "Storyboard " + std::to_string(inputs.history.document.storyboards.size() + 1)}}
                           : osbx_edit_change_t{osbx_delete_storyboard_t{}}};
    auto result = apply_osbx_edit_with_result(inputs.history, request);
    if (result.command.applied) {
        state_.hidden_nodes.clear();
        if (creating && result.payload) state_.selection = {.storyboard = *result.payload};
        else if (!creating) state_.selection = {};
    }
    return compose_action_outcome_t::from(std::move(result.command),
        {.success = creating ? "Storyboard created." : "Storyboard deleted.",
         .unchanged = "Storyboard was unchanged.",
         .rebind_failure = "Storyboard changed, but workspace rebinding failed."});
}
void storyboard_runtime_t::reconcile_hierarchy_edit(const ui::storyboard_action_t& action,
    const osbx_edit_created_ids_t& created, const song_document_t& document) {
    if (created.node) state_.selection = {.storyboard = action.storyboard, .nodes = {*created.node}};
    if (action.hierarchy.kind == ui::storyboard_hierarchy_mutation_kind_t::rename) {
        for (auto* ids : {&state_.selection.nodes, &state_.hidden_nodes})
            for (auto& id : *ids) if (id == action.hierarchy.node) id = action.hierarchy.text;
    }
    reconcile(document);
    if (state_.selection.nodes.empty() || action.hierarchy.kind == ui::storyboard_hierarchy_mutation_kind_t::erase) {
        state_.selection.op.reset();
        state_.selection.keys.clear();
    }
}
auto storyboard_runtime_t::edit_hierarchy(const ui::storyboard_action_t& action,
    const document_edit_inputs_t& inputs, const application_command_state_t& application,
    editor_interaction_state_t* compose, bool file_job_busy) -> compose_action_outcome_t {
    const auto& document = inputs.history.document;
    const auto board = std::ranges::find(document.storyboards, action.storyboard, &storyboard_document_t::id);
    if (action.storyboard != state_.selection.storyboard || board == document.storyboards.end() ||
        action.prepared_generation != osbx_lua_prepared_scope_t::generation())
        return compose_action_outcome_t::from(error(diagnostic_code_t::invalid_binding,
            stale_action_message("Storyboard hierarchy")), notification_source_t::input);
    const auto& mutation = action.hierarchy;
    if (mutation.kind == ui::storyboard_hierarchy_mutation_kind_t::none) return view_feedback();
    if (mutation.kind == ui::storyboard_hierarchy_mutation_kind_t::toggle_editor_visibility) {
        if (!node_exists(*board, mutation.node)) return compose_action_outcome_t::from(
            error(diagnostic_code_t::invalid_id, "Storyboard node no longer exists."), notification_source_t::input);
        if (std::erase(state_.hidden_nodes, mutation.node) == 0) state_.hidden_nodes.push_back(mutation.node);
        return view_feedback();
    }
    if (file_job_busy || (compose && compose->is_pending()) || application.project.clipboard.paste_session)
        return compose_action_outcome_t::blocked("Finish the active edit or file job before editing storyboards.");
    if (auto reason = hierarchy_edit_reason(*board, mutation); !reason.empty())
        return compose_action_outcome_t::from(error(diagnostic_code_t::invalid_binding,
            std::move(reason)), notification_source_t::edit);
    const auto change = plan_hierarchy_edit(mutation);
    if (!change) return compose_action_outcome_t::from(change.error(), notification_source_t::input);
    pause_storyboard_transport(application);
    auto result = apply_osbx_edit_with_created_ids(inputs.history, {.storyboard = action.storyboard, .change = *change});
    if (result.command.applied) reconcile_hierarchy_edit(action, result.payload, inputs.history.document);
    return compose_action_outcome_t::from(std::move(result.command),
        {.success = "Storyboard hierarchy changed.", .unchanged = "Storyboard was unchanged.",
         .rebind_failure = "Storyboard changed, but workspace rebinding failed."});
}
auto storyboard_runtime_t::is_current(const ui::storyboard_action_t& action,
                                     const document_edit_inputs_t& inputs, bool allow_player_preview) const -> bool {
    const document_stamp_t stamp{.project = action.project, .revision = action.revision};
    return project_ == inputs.history.document.id && action.session_generation == session_generation_ &&
        stamp.is_current(inputs.history.document) &&
        (inputs.workspace.active_tab == workspace_tab_t::storyboard || allow_player_preview);
}
auto storyboard_runtime_t::is_inspector_target_current(const ui::storyboard_action_t& action) const -> bool {
    if (action.storyboard != state_.selection.storyboard) return false;
    if (action.inspector.field == ui::storyboard_inspector_field_t::convert) return true;
    return action.prepared_generation == osbx_lua_prepared_scope_t::generation() &&
        state_.selection.nodes.size() == 1 && state_.selection.nodes.front() == action.inspector.node;
}
auto storyboard_runtime_t::is_matching_gesture(const ui::storyboard_action_t& action,
    const document_edit_inputs_t& inputs, const editor_interaction_state_t* compose) const -> bool {
    if (!compose ||
        action.project != inputs.history.document.id || action.session_generation != session_generation_)
        return false;
    const auto* pending = compose->gesture<pending_osbx_edit_gesture_t>();
    const auto* stamp = compose->gesture_stamp<pending_osbx_edit_gesture_t>();
    if (!pending || !stamp || stamp->project != action.project || stamp->revision != action.revision ||
        pending->edit.storyboard != action.storyboard) return false;
    return is_matching_storyboard_target(pending->edit.change, action);
}
auto storyboard_runtime_t::cancel_gesture(const ui::storyboard_action_t& action,
    const document_edit_inputs_t& inputs, editor_interaction_state_t* compose,
    const diagnostic_t* rejection) -> std::optional<compose_action_outcome_t> {
    using phase_t = ui::storyboard_inspector_phase_t;
    const auto phase = action.document_gesture_phase();
    if (phase != phase_t::cancel && phase != phase_t::apply && phase != phase_t::revise) return {};
    if (!is_matching_gesture(action, inputs, compose)) return {};
    auto result = dispatch_editor_action(inputs.history, *compose, cancel_editor_gesture_action_t{});
    timeline_preview_delta_.reset(); timeline_preview_reason_.clear();
    if (rejection) result.diagnostics.push_back(*rejection);
    return compose_action_outcome_t::from(std::move(result), "Storyboard edit cancelled.");
}
auto storyboard_runtime_t::reject_edit(const ui::storyboard_action_t& action,
    const document_edit_inputs_t& inputs, editor_interaction_state_t* compose,
    const diagnostic_t& diagnostic, notification_source_t source) -> compose_action_outcome_t {
    if (auto cancelled = cancel_gesture(action, inputs, compose, &diagnostic)) return std::move(*cancelled);
    return compose_action_outcome_t::from(diagnostic, source);
}
auto storyboard_runtime_t::apply_inspector(const ui::storyboard_action_t& action, const osbx_edit_t& edit,
    const document_edit_inputs_t& inputs) -> compose_action_outcome_t {
    auto result = apply_osbx_edit_with_result(inputs.history, edit);
    if (result.command.applied) {
        if (result.payload) { state_.selection = {.storyboard = *result.payload}; state_.hidden_nodes.clear(); }
        else if (action.inspector.field == ui::storyboard_inspector_field_t::id)
            state_.selection.nodes = {action.inspector.text};
        else if (action.inspector.field == ui::storyboard_inspector_field_t::remove_op) {
            state_.selection.op.reset(); state_.selection.keys.clear();
        }
    }
    return compose_action_outcome_t::from(std::move(result.command),
        {.success = "Storyboard property changed.", .unchanged = "Storyboard was unchanged.",
         .rebind_failure = "Storyboard changed, but workspace rebinding failed."});
}
auto storyboard_runtime_t::inspect(const ui::storyboard_action_t& action, const document_edit_inputs_t& inputs,
    const application_command_state_t& application, editor_interaction_state_t* compose,
    bool file_job_busy) -> compose_action_outcome_t {
    using phase_t = ui::storyboard_inspector_phase_t;
    if (!is_inspector_target_current(action)) {
        const auto diagnostic = error(diagnostic_code_t::invalid_binding, stale_action_message("Storyboard selection"));
        return reject_edit(action, inputs, compose, diagnostic, notification_source_t::input);
    }
    const bool direct_busy = action.inspector.phase == phase_t::commit && compose && compose->is_pending();
    if (file_job_busy || direct_busy || application.project.clipboard.paste_session) {
        const auto diagnostic = error(diagnostic_code_t::invalid_binding, "Finish the active edit or file job before editing storyboards.");
        return reject_edit(action, inputs, compose, diagnostic, notification_source_t::input);
    }
    pause_storyboard_transport(application);
    if (action.inspector.phase == phase_t::focus) return view_feedback();
    const auto change = plan_storyboard_inspector_edit(inputs.history.document, action.storyboard, action.inspector);
    if (!change) {
        const auto diagnostic = error(diagnostic_code_t::invalid_binding, change.error() == failure_code_t::invalid_data
            ? "Invalid storyboard field value" : "Storyboard field is unavailable or no longer current.");
        return reject_edit(action, inputs, compose, diagnostic, notification_source_t::edit);
    }
    const osbx_edit_t edit{.storyboard = action.storyboard, .change = *change};
    if (action.inspector.phase == phase_t::commit) return apply_inspector(action, edit, inputs);
    return dispatch_storyboard_gesture(action.inspector.phase, edit, inputs, compose);
}
auto storyboard_runtime_t::select_timeline_keys(const ui::storyboard_action_t& action,
    const song_document_t& document) -> compose_action_outcome_t {
    const auto board = std::ranges::find(document.storyboards, action.storyboard, &storyboard_document_t::id);
    if (board == document.storyboards.end() || !board->osbx) return view_feedback();
    const auto& scene = *board->osbx;
    const bool valid = std::ranges::all_of(action.timeline.keys, [&](const auto& key) {
        return std::ranges::find(state_.selection.nodes, key.track.node) != state_.selection.nodes.end() &&
            storyboard_timeline_key_time(scene, key).has_value();
    });
    if (!valid) return compose_action_outcome_t::from(error(diagnostic_code_t::invalid_binding,
        stale_action_message("Storyboard keys")), notification_source_t::input);
    state_.selection.keys.clear();
    for (const auto& key : action.timeline.keys)
        if (std::ranges::find(state_.selection.keys, key.key) == state_.selection.keys.end()) state_.selection.keys.push_back(key.key);
    state_.selection.op = action.timeline.track.kind == osbx_track_kind_t::transform ?
        std::optional{action.timeline.track.op} : std::nullopt;
    return view_feedback();
}
auto storyboard_runtime_t::is_timeline_target_current(const ui::storyboard_action_t& action) const -> bool {
    using kind_t = ui::storyboard_timeline_action_kind_t;
    using phase_t = ui::storyboard_inspector_phase_t;
    const auto& request = action.timeline;
    if (request.kind == kind_t::add_key)
        return std::ranges::find(state_.selection.nodes, request.track.node) != state_.selection.nodes.end();
    if (request.kind == kind_t::move_keys && request.phase != phase_t::begin &&
        request.phase != phase_t::revise && request.phase != phase_t::apply) return false;
    return !request.keys.empty() && std::ranges::all_of(request.keys, [&](const auto& key) {
        return std::ranges::find(state_.selection.nodes, key.track.node) != state_.selection.nodes.end() &&
            std::ranges::find(state_.selection.keys, key.key) != state_.selection.keys.end();
    });
}
auto storyboard_runtime_t::edit_timeline(const ui::storyboard_action_t& action,
    const document_edit_inputs_t& inputs, const application_command_state_t& application,
    editor_interaction_state_t* compose) -> compose_action_outcome_t {
    using kind_t = ui::storyboard_timeline_action_kind_t;
    using phase_t = ui::storyboard_inspector_phase_t;
    const auto& request = action.timeline;
    if (!is_timeline_target_current(action))
        return reject_edit(action, inputs, compose, error(diagnostic_code_t::invalid_binding,
            stale_action_message("Storyboard key selection")), notification_source_t::input);
    const auto timing = storyboard_timeline_timing_t::from(inputs.history, inputs.workspace.selected_chart, compose);
    const auto change = plan_storyboard_timeline_edit(inputs.history.document, action.storyboard, request, request.time, nullptr, timing);
    if (!change) return reject_edit(action, inputs, compose, timeline_plan_diagnostic(change.error()), notification_source_t::edit);
    pause_storyboard_transport(application);
    const osbx_edit_t edit{.storyboard = action.storyboard, .change = *change};
    if (request.kind != kind_t::move_keys) {
        auto result = apply_osbx_edit(inputs.history, edit);
        if (result.applied && request.kind == kind_t::erase_keys) state_.selection.keys.clear();
        return compose_action_outcome_t::from(std::move(result), {.success = "Storyboard keys changed.",
            .unchanged = "Storyboard keys were unchanged.", .rebind_failure = "Storyboard changed, but workspace rebinding failed."});
    }
    if (request.phase != phase_t::begin && !is_matching_gesture(action, inputs, compose))
        return compose_action_outcome_t::blocked("No matching storyboard key drag is active.");
    timeline_preview_delta_ = std::get<osbx_move_keys_t>(*change).delta_ms;
    auto outcome = dispatch_storyboard_gesture(request.phase, edit, inputs, compose);
    timeline_preview_reason_ = outcome.accepted ? std::string{} : std::string(outcome.message_text());
    if (request.phase == phase_t::apply) { timeline_preview_delta_.reset(); timeline_preview_reason_.clear(); }
    return outcome;
}
auto storyboard_runtime_t::timeline(const ui::storyboard_action_t& action,
    const document_edit_inputs_t& inputs, const application_command_state_t& application,
    editor_interaction_state_t* compose, bool file_job_busy) -> compose_action_outcome_t {
    using kind_t = ui::storyboard_timeline_action_kind_t;
    using phase_t = ui::storyboard_inspector_phase_t;
    const auto& request = action.timeline;
    if (action.storyboard != state_.selection.storyboard ||
        action.prepared_generation != osbx_lua_prepared_scope_t::generation())
        return reject_edit(action, inputs, compose, error(diagnostic_code_t::invalid_binding,
            stale_action_message("Storyboard timeline")), notification_source_t::input);
    if (request.kind == kind_t::none) return view_feedback();
    if (request.kind == kind_t::select_keys) return select_timeline_keys(action, inputs.history.document);
    if (request.kind == kind_t::set_grid) {
        const auto timing = storyboard_timeline_timing_t::from(inputs.history, inputs.workspace.selected_chart, compose);
        if (request.grid_enabled && !timing.is_available())
            return compose_action_outcome_t::blocked("Beat/grid snapping is unavailable. Millisecond snapping remains available.");
        timeline_grid_enabled_ = request.grid_enabled;
        return view_feedback();
    }
    const bool direct_busy = !action.is_document_gesture_begin() && request.phase == phase_t::commit && compose && compose->is_pending();
    if (file_job_busy || direct_busy || application.project.clipboard.paste_session)
        return reject_edit(action, inputs, compose, error(diagnostic_code_t::invalid_binding,
            "Finish the active edit or file job before editing storyboards."), notification_source_t::input);
    if (request.kind == kind_t::seek) {
        const auto time = storyboard_timeline_pointer_time(request.pointer_time_ms);
        const auto ms = time ? snap_storyboard_key_time(*time) : result_t<std::int64_t, failure_code_t>{std::unexpected(time.error())};
        if (!ms) return compose_action_outcome_t::from(timeline_plan_diagnostic(ms.error()), notification_source_t::input);
        if (!application.services.transport_seek_audio_milliseconds)
            return compose_action_outcome_t::blocked("The shared transport is unavailable.");
        return application.services.transport_seek_audio_milliseconds(*ms);
    }
    return edit_timeline(action, inputs, application, compose);
}
auto storyboard_runtime_t::update_view(const ui::storyboard_action_t& action,
    const song_document_t& document) -> compose_action_outcome_t {
    const auto storyboard = std::ranges::find(document.storyboards, action.storyboard, &storyboard_document_t::id);
    if (action.storyboard != state_.selection.storyboard || storyboard == document.storyboards.end())
        return compose_action_outcome_t::from(error(diagnostic_code_t::invalid_binding,
            stale_action_message("Storyboard selection")), notification_source_t::input);
    if (action.kind == ui::storyboard_action_kind_t::set_viewport) {
        if (!valid_viewport(action.viewport)) return compose_action_outcome_t::from(
            error(diagnostic_code_t::invalid_resolution, "Invalid storyboard viewport."), notification_source_t::input);
        state_.viewport = action.viewport;
        return view_feedback();
    }
    if (action.prepared_generation != osbx_lua_prepared_scope_t::generation() ||
        !std::ranges::all_of(action.nodes, [&](const auto& id) { return node_exists(*storyboard, id); }))
        return compose_action_outcome_t::from(error(diagnostic_code_t::invalid_binding,
            stale_action_message("Storyboard nodes")), notification_source_t::input);
    state_.selection.nodes.clear();
    for (const auto& id : action.nodes)
        if (std::ranges::find(state_.selection.nodes, id) == state_.selection.nodes.end()) state_.selection.nodes.push_back(id);
    state_.selection.op.reset(); state_.selection.keys.clear();
    return view_feedback();
}
auto storyboard_runtime_t::lua_capture_reason(const ui::storyboard_action_t& action,
    const document_edit_inputs_t& inputs, const application_command_state_t& application,
    const editor_interaction_state_t* compose, bool busy) const -> std::string {
    if (!is_current(action, inputs) || action.storyboard != state_.selection.storyboard ||
        action.prepared_generation != osbx_lua_prepared_scope_t::generation() ||
        action.lua_link.selected_keys != state_.selection.keys ||
        std::ranges::find(state_.selection.nodes, action.lua_link.target.track.node) == state_.selection.nodes.end())
        return stale_action_message("Storyboard Lua target");
    if (busy || (compose && compose->is_pending()) || application.project.clipboard.paste_session)
        return "Finish the active edit or file job before editing Lua links.";
    return {};
}
auto storyboard_runtime_t::lua_link(const ui::storyboard_action_t& action,
    const document_edit_inputs_t& inputs, const application_command_state_t& application,
    editor_interaction_state_t* compose, bool busy) -> compose_action_outcome_t {
    if (auto reason = lua_capture_reason(action, inputs, application, compose, busy); !reason.empty())
        return compose_action_outcome_t::blocked(reason);
    const auto source = storyboard_lua_source(inputs.history.document, action.storyboard, action.lua_link);
    if (!source) return compose_action_outcome_t::blocked(source.error());
    using kind_t = ui::storyboard_lua_action_kind_t;
    switch (action.lua_link.kind) {
    case kind_t::link: case kind_t::fill: case kind_t::reload: {
        if (!application.services.load_storyboard_lua) return compose_action_outcome_t::blocked("Lua source worker is unavailable.");
        auto outcome = application.services.load_storyboard_lua(action, *source);
        if (outcome.accepted) pause_storyboard_transport(application);
        return outcome;
    }
    case kind_t::bake: case kind_t::detach_children: {
        const auto board = std::ranges::find(inputs.history.document.storyboards, action.storyboard, &storyboard_document_t::id);
        const auto* prepared = osbx_lua_prepared_scope_t::prepared(*board->osbx,
            document_stamp_t::from(inputs.history.document), action.storyboard, action.prepared_generation);
        if (!prepared) return compose_action_outcome_t::blocked("Wait for the current Lua result before baking or detaching.");
        const auto& target = action.lua_link.target;
        const auto name = target.children ? "children" : target.track.kind == osbx_track_kind_t::transform
            ? "op/" + target.track.op : target.track.kind == osbx_track_kind_t::opacity ? "opacity" : "color";
        pause_storyboard_transport(application);
        return compose_action_outcome_t::from(apply_osbx_materialize_link(inputs.history,
            {.storyboard = action.storyboard, .node_id = target.track.node, .target = name, .prepared = *prepared}),
            {.success = "Lua result materialized.", .unchanged = "Lua result unchanged.", .rebind_failure = "Workspace rebinding failed."});
    }
    case kind_t::unlink: {
        const auto plan = plan_storyboard_lua_edit(inputs.history.document, action.storyboard, action.lua_link);
        if (!plan) return compose_action_outcome_t::blocked(plan.error());
        pause_storyboard_transport(application);
        return compose_action_outcome_t::from(apply_osbx_edit(inputs.history, {.storyboard = action.storyboard, .change = *plan}),
            {.success = "Lua link removed; authored keys retained.", .unchanged = "Lua link unchanged.", .rebind_failure = "Workspace rebinding failed."});
    }
    case kind_t::none: return view_feedback();
    }
    return compose_action_outcome_t::blocked("Invalid Lua action.");
}
auto storyboard_runtime_t::complete_lua_source(const ui::storyboard_action_t& action,
    const result_t<lua_source_descriptor_t, std::string>& source, const document_edit_inputs_t& inputs,
    const application_command_state_t& application, editor_interaction_state_t* compose, bool busy) -> bool {
    auto reason = lua_capture_reason(action, inputs, application, compose, busy);
    if (reason.empty() && !source) reason = source.error();
    if (!reason.empty()) {
        (void)finish_compose_action(inputs, inputs.notifications, compose_action_outcome_t::blocked(reason));
        return false;
    }
    const auto plan = plan_storyboard_lua_edit(inputs.history.document, action.storyboard, action.lua_link, &*source);
    if (!plan) {
        (void)finish_compose_action(inputs, inputs.notifications, compose_action_outcome_t::blocked(plan.error()));
        return false;
    }
    pause_storyboard_transport(application);
    auto result = apply_osbx_edit(inputs.history, {.storyboard = action.storyboard, .change = *plan});
    const auto applied = result.applied;
    (void)finish_compose_action(inputs, inputs.notifications, compose_action_outcome_t::from(std::move(result),
        {.success = "Lua source frozen and linked.", .unchanged = "Lua source unchanged.", .rebind_failure = "Workspace rebinding failed."}));
    return applied;
}

auto storyboard_runtime_t::transport(const ui::storyboard_action_t& action,
    const document_edit_inputs_t& inputs, const application_command_state_t& application,
    editor_interaction_state_t* compose, bool file_job_busy) -> compose_action_outcome_t {
    const auto& boards = inputs.history.document.storyboards;
    if (action.storyboard != state_.selection.storyboard ||
        std::ranges::find(boards, action.storyboard, &storyboard_document_t::id) == boards.end() ||
        action.prepared_generation != osbx_lua_prepared_scope_t::generation())
        return compose_action_outcome_t::from(error(diagnostic_code_t::invalid_binding,
            stale_action_message("Storyboard transport")), notification_source_t::input);
    if (file_job_busy || (compose && compose->is_pending()) || application.project.clipboard.paste_session)
        return compose_action_outcome_t::blocked("Finish the active edit or file job before changing the loop.");
    const auto& region = action.transport.loop;
    if (region && !region->is_playable())
        return compose_action_outcome_t::blocked("Select two distinct nonnegative key times to loop.");
    if (!application.services.transport_set_loop_audio_milliseconds)
        return compose_action_outcome_t::blocked("The shared audio transport is unavailable.");
    return application.services.transport_set_loop_audio_milliseconds(
        region ? region->begin_ms : 0, region ? region->end_ms : 0, region.has_value());
}

auto storyboard_runtime_t::set_conditional_layer(const ui::storyboard_action_t& action,
    const application_command_state_t& application, const editor_interaction_state_t* compose,
    bool file_job_busy) -> compose_action_outcome_t {
    if (file_job_busy || (compose && compose->is_pending()) || application.project.clipboard.paste_session)
        return compose_action_outcome_t::blocked("Finish the active edit or file job before changing preview state.");
    if (static_cast<unsigned>(action.conditional_layer) > static_cast<unsigned>(storyboard_conditional_layer_t::neither))
        return compose_action_outcome_t::from(error(diagnostic_code_t::invalid_id,
            "Invalid storyboard preview state."), notification_source_t::input);
    state_.conditional_layer = action.conditional_layer;
    return view_feedback();
}

auto storyboard_runtime_t::execute(const ui::storyboard_action_t& action,
    const document_edit_inputs_t& inputs, const application_command_state_t& application,
    editor_interaction_state_t* compose, bool file_job_busy) -> compose_action_outcome_t {
    if (action.is_document_gesture_cancel()) {
        if (auto cancelled = cancel_gesture(action, inputs, compose)) return std::move(*cancelled);
        return view_feedback();
    }
    const bool player_view_action = action.kind == ui::storyboard_action_kind_t::set_conditional_layer &&
        application.project.compose_view.player_preview_visible;
    if (!is_current(action, inputs, player_view_action)) {
        const auto diagnostic = error(diagnostic_code_t::invalid_binding, stale_action_message("Storyboard action"));
        return reject_edit(action, inputs, compose, diagnostic, notification_source_t::input);
    }
    switch (action.kind) {
    case ui::storyboard_action_kind_t::set_conditional_layer:
        return set_conditional_layer(action, application, compose, file_job_busy);
    case ui::storyboard_action_kind_t::select:
        if (std::ranges::find(inputs.history.document.storyboards, action.storyboard,
                &storyboard_document_t::id) == inputs.history.document.storyboards.end())
            return compose_action_outcome_t::from(error(diagnostic_code_t::invalid_id,
                "Storyboard no longer exists."), notification_source_t::input);
        if (state_.selection.storyboard != action.storyboard) {
            state_.selection = {.storyboard = action.storyboard};
            state_.hidden_nodes.clear(); thumbnail_images_.clear(); thumbnail_resources_.clear();
        }
        return compose_action_outcome_t::from("Storyboard selected.", notification_severity_t::hint,
                                              notification_source_t::input);
    case ui::storyboard_action_kind_t::select_nodes:
    case ui::storyboard_action_kind_t::set_viewport:
        return update_view(action, inputs.history.document);
    case ui::storyboard_action_kind_t::set_dock:
        state_.dock = action.dock;
        return compose_action_outcome_t::from({}, notification_severity_t::hint,
            notification_source_t::input, compose_feedback_delivery_t::none);
    case ui::storyboard_action_kind_t::create:
    case ui::storyboard_action_kind_t::erase:
    case ui::storyboard_action_kind_t::detach_generator:
        if (file_job_busy || (compose && compose->is_pending()) || application.project.clipboard.paste_session)
            return compose_action_outcome_t::blocked("Finish the active edit or file job before editing storyboards.");
        return edit(action, inputs, application);
    case ui::storyboard_action_kind_t::hierarchy:
        return edit_hierarchy(action, inputs, application, compose, file_job_busy);
    case ui::storyboard_action_kind_t::inspector:
        return inspect(action, inputs, application, compose, file_job_busy);
    case ui::storyboard_action_kind_t::timeline:
        return timeline(action, inputs, application, compose, file_job_busy);
    case ui::storyboard_action_kind_t::lua_link:
        return lua_link(action, inputs, application, compose, file_job_busy);
    case ui::storyboard_action_kind_t::assets:
        return assets(action, inputs, application, compose, file_job_busy);
    case ui::storyboard_action_kind_t::transport:
        return transport(action, inputs, application, compose, file_job_busy);
    case ui::storyboard_action_kind_t::video_export:
        return compose_action_outcome_t::blocked("Video export is routed by the application file-job owner.");
    case ui::storyboard_action_kind_t::none:
        return compose_action_outcome_t::from({}, notification_severity_t::hint,
            notification_source_t::input, compose_feedback_delivery_t::none);
    }
    return compose_action_outcome_t::from(error(diagnostic_code_t::invalid_id,
        "Invalid storyboard action."), notification_source_t::input);
}
auto storyboard_runtime_t::apply_assets(const ui::storyboard_action_t& action, const osbx_edit_t& edit,
    const document_edit_inputs_t& inputs) -> compose_action_outcome_t {
    auto result = apply_osbx_edit_with_created_ids(inputs.history, edit);
    if (result.command.applied && result.payload.node)
        state_.selection = {.storyboard = action.storyboard, .nodes = {*result.payload.node}};
    return compose_action_outcome_t::from(std::move(result.command),
        {.success = "Storyboard images changed.", .unchanged = "Storyboard images were unchanged.",
         .rebind_failure = "Storyboard changed, but workspace rebinding failed."});
}
auto storyboard_runtime_t::complete_assets_import(const ui::storyboard_action_t& action, const osbx_place_image_t& operation,
    const document_edit_inputs_t& inputs, const application_command_state_t& application,
    editor_interaction_state_t* compose, bool file_job_busy) -> bool {
    if (!is_current(action, inputs) || action.storyboard != state_.selection.storyboard ||
        action.prepared_generation != osbx_lua_prepared_scope_t::generation() || file_job_busy ||
        (compose && compose->is_pending()) || application.project.clipboard.paste_session) {
        (void)finish_compose_action(inputs, inputs.notifications,
            compose_action_outcome_t::blocked("Image import capture is stale or the editor is busy."));
        return false;
    }
    pause_storyboard_transport(application);
    auto result = apply_osbx_edit_with_created_ids(inputs.history, {.storyboard = action.storyboard, .change = operation});
    const bool applied = result.command.applied;
    if (applied && result.payload.node) state_.selection = {.storyboard = action.storyboard, .nodes = {*result.payload.node}};
    (void)finish_compose_action(inputs, inputs.notifications, compose_action_outcome_t::from(std::move(result.command),
        {.success = "Storyboard image imported.", .unchanged = "Storyboard was unchanged.",
         .rebind_failure = "Storyboard changed, but workspace rebinding failed."}));
    return applied;
}
void storyboard_runtime_t::update_thumbnail_demand(const ui::storyboard_action_t& action,
    const song_document_t& song, const osbx_document_t& scene) {
    thumbnail_images_.clear(); thumbnail_resources_.clear();
    for (const auto& id : action.assets.images)
        if (std::ranges::find(scene.images, id, &osbx_image_t::id) != scene.images.end() &&
            std::ranges::find(thumbnail_images_, id) == thumbnail_images_.end()) thumbnail_images_.push_back(id);
    for (const auto id : action.assets.resources)
        if (std::ranges::find(song.resources, id, &resource_entry_t::id) != song.resources.end() &&
            std::ranges::find(thumbnail_resources_, id) == thumbnail_resources_.end()) thumbnail_resources_.push_back(id);
}
auto storyboard_runtime_t::assets(const ui::storyboard_action_t& action, const document_edit_inputs_t& inputs,
    const application_command_state_t& application, editor_interaction_state_t* compose, bool busy) -> compose_action_outcome_t {
    using kind_t = ui::storyboard_assets_action_kind_t;
    const auto& song = inputs.history.document;
    const auto board = std::ranges::find(song.storyboards, action.storyboard, &storyboard_document_t::id);
    if (action.storyboard != state_.selection.storyboard || board == song.storyboards.end() || !board->osbx ||
        action.prepared_generation != osbx_lua_prepared_scope_t::generation())
        return compose_action_outcome_t::blocked("Storyboard image capture is stale.");
    if (action.assets.kind == kind_t::set_thumbnail_demand) {
        update_thumbnail_demand(action, song, *board->osbx);
        return view_feedback();
    }
    if (busy || (compose && compose->is_pending()) || application.project.clipboard.paste_session)
        return compose_action_outcome_t::blocked("Finish the active edit or file job before editing images.");
    if (board->model || board->owning_generator_recipe) return compose_action_outcome_t::blocked("Storyboard images are read-only.");
    if (action.assets.kind == kind_t::none) return compose_action_outcome_t::blocked("Invalid storyboard image action.");
    pause_storyboard_transport(application);
    if (action.assets.kind == kind_t::remove_image) {
        const auto prepared = osbx_lua_prepared_scope_t::find(*board->osbx);
        const auto allowed = validate_storyboard_image_removal(*board, action.assets.image, prepared.get());
        if (!allowed) return compose_action_outcome_t::blocked(allowed.error().message);
        return apply_assets(action, {.storyboard = action.storyboard, .change = osbx_remove_image_t{action.assets.image}}, inputs);
    }
    osbx_place_image_t operation;
    if ((action.assets.resource && action.assets.image.empty()) || action.assets.kind == kind_t::import_images)
        return application.services.import_storyboard_image ? application.services.import_storyboard_image(action) :
            compose_action_outcome_t::blocked("Image importing is unavailable.");
    if (action.assets.image.empty()) return compose_action_outcome_t::blocked("Choose an image.");
    operation.source = osbx_existing_image_t{action.assets.image};
    if (action.assets.kind == kind_t::add_sprite) operation.sprite = osbx_sprite_placement_t{
        .layer = action.assets.layer, .x = action.assets.position.x, .y = action.assets.position.y};
    return apply_assets(action, {.storyboard = action.storyboard, .change = std::move(operation)}, inputs);
}
auto storyboard_runtime_t::dispatch(const ui::storyboard_action_t& action,
    const document_edit_inputs_t& inputs, const application_command_state_t& application,
    editor_interaction_state_t* compose, bool file_job_busy) -> bool {
    if (action.kind == ui::storyboard_action_kind_t::none) return false;
    (void)finish_compose_action(inputs, inputs.notifications,
        execute(action, inputs, application, compose, file_job_busy));
    return true;
}
} // namespace wormedit::sys

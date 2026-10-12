#include "sys/compose_input_internal.h"
namespace wormedit::sys::compose_input_detail {
auto resource_pointer_spec(const WormUiComposeInputAction& input, compose_input_context_t& context)
    -> std::variant<resource_pointer_spec_t, std::string> {
    using namespace wormedit;
    if (context.gesture_active)
        return "Finish or cancel the active Compose gesture before editing a resource.";
    if (!context.chart() ||
        !compose_view_for_state(context.environment.history_ref, context.state) ||
        input.object_id == 0)
        return "The selected chart or armed resource is no longer available.";
    const auto kind = input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE
                          ? resource_placement_kind_t::sample_note
                      : input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGM
                          ? resource_placement_kind_t::bgm_event
                      : input.resource_role == WORM_UI_COMPOSE_RESOURCE_ROLE_BGA
                          ? resource_placement_kind_t::bga_background
                          : static_cast<resource_placement_kind_t>(255);
    if (kind == static_cast<resource_placement_kind_t>(255))
        return "Resource pointer placement used an unknown role.";
    if (kind == resource_placement_kind_t::bga_background &&
        (!editor_bga_lane_index(static_cast<visual_event_kind_t>(input.bga_lane_kind)) ||
         input.bga_lane_kind == static_cast<std::uint8_t>(visual_event_kind_t::video)))
        return "Unsupported BGA destination lane.";
    const auto required = kind == resource_placement_kind_t::sample_note ? resource_role_t::sample
                          : kind == resource_placement_kind_t::bgm_event ? resource_role_t::bgm
                                                                         : resource_role_t::bga;
    document_command_result_t validation{};
    if (!validate_resource_role(context.environment.history_ref.document,
                                resource_id_t{input.object_id}, required, validation))
        return validation.diagnostics.front().message;
    return resource_pointer_spec_t{resource_id_t{input.object_id}, kind, required,
                                   input.kind == WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE};
}

auto resource_pointer_bga_kind(const WormUiComposeInputAction& input,
                               wormedit::resource_placement_kind_t kind)
    -> std::optional<wormedit::visual_event_kind_t> {
    return kind == wormedit::resource_placement_kind_t::bga_background
               ? std::optional{static_cast<wormedit::visual_event_kind_t>(input.bga_lane_kind)}
               : std::nullopt;
}

void advance_dock_after_placement(const wormedit::song_document_t& document,
                                  wormedit::resource_id_t placed,
                                  wormedit::resource_role_t required_role) {
    const auto next = wormedit::next_eligible_placement_resource(document, placed, required_role);
    worm_ui_frontend_resource_placed(placed.value, next.value_or(wormedit::resource_id_t{}).value);
}

auto place_virtual_sample_resource(const WormUiComposeInputAction& input,
                                   compose_input_context_t& context,
                                   const wormedit::chart_document_t& chart,
                                   const wormedit::editor_interaction_view_model_t& view,
                                   const resource_pointer_spec_t& spec,
                                   wormedit::editor_interaction_result_t& result,
                                   std::string_view& accepted) -> bool {
    if (spec.replacing || spec.kind != wormedit::resource_placement_kind_t::sample_note)
        return false;
    const auto virtual_hit =
        context.virtual_bms_hit_at(chart, view, input.x_pixels, input.y_pixels);
    if (!virtual_hit)
        return false;
    result = wormedit::dispatch_editor_action(
        context.environment.history_ref, context.state,
        wormedit::place_virtual_bms_tap_action_t{virtual_hit->first, virtual_hit->second,
                                                 spec.resource});
    audition_created_note(
        context.environment.application_command_state_ref.editor_preferences.audition_on_click,
        context.environment.history_ref, context.state, result);
    if (result.document_changed)
        advance_dock_after_placement(context.environment.history_ref.document, spec.resource,
                                     spec.required_role);
    accepted = "Sample placed in BMS measure.";
    return true;
}

auto place_virtual_source_resource(const WormUiComposeInputAction& input,
                                   compose_input_context_t& context,
                                   const wormedit::chart_document_t& chart,
                                   const wormedit::editor_interaction_view_model_t& view,
                                   const resource_pointer_spec_t& spec,
                                   wormedit::editor_interaction_result_t& result,
                                   std::string_view& accepted) -> bool {
    using namespace wormedit;
    if (spec.replacing || spec.kind == resource_placement_kind_t::sample_note ||
        context.position_at_compose_y(chart, view, input.y_pixels))
        return false;
    const auto row = context.virtual_bms_row_at(chart, view, input.y_pixels);
    if (!row)
        return false;
    const auto applied = apply_virtual_bms_resource_placement(
        context.environment.history_ref, context.ui_action.compose_input_document_revision,
        {.chart = context.state.chart,
         .resource = spec.resource,
         .canvas_row = *row,
         .source_lane = spec.kind == resource_placement_kind_t::bgm_event
                            ? object_id_t{input.lane_id}
                            : object_id_t{},
         .kind = spec.kind,
         .snap_denominator = edit_snap_denominator(chart, context.state),
         .semantic_layers = context.state.armed_note_layers,
         .bga_kind = resource_pointer_bga_kind(input, spec.kind)});
    result.accepted = applied.applied;
    result.document_changed = applied.applied;
    result.diagnostics = applied.diagnostics;
    if (applied.applied)
        advance_dock_after_placement(context.environment.history_ref.document, spec.resource,
                                     spec.required_role);
    accepted = spec.kind == resource_placement_kind_t::bgm_event ? "BGM placed in BMS measure."
                                                                 : "BGA placed in BMS measure.";
    return true;
}

auto resolve_resource_pointer_target(
    const WormUiComposeInputAction& input, compose_input_context_t& context,
    const wormedit::chart_document_t& chart, const wormedit::editor_interaction_view_model_t& view,
    const resource_pointer_spec_t& spec, wormedit::editor_interaction_result_t& result,
    std::string_view& accepted) -> std::variant<resource_pointer_target_t, std::string> {
    using namespace wormedit;
    resource_pointer_target_t target{};
    if (spec.kind == resource_placement_kind_t::sample_note) {
        const auto hit = context.hit_target_at(chart, view, input.x_pixels, input.y_pixels);
        if (!hit) {
            if (place_virtual_sample_resource(input, context, chart, view, spec, result, accepted))
                target.handled_virtual = true;
            if (target.handled_virtual)
                return target;
            return "Click inside an editable playable lane to place a Sample.";
        }
        target.position = hit->position;
        target.lane = hit->lane;
        target.note = hit->note;
        return target;
    }
    target.position = context.position_at_compose_y(chart, view, input.y_pixels);
    if (!target.position &&
        place_virtual_source_resource(input, context, chart, view, spec, result, accepted)) {
        target.handled_virtual = true;
        return target;
    }
    return target;
}

auto replacement_target_for_resource(const WormUiComposeInputAction& input,
                                     const wormedit::chart_document_t& chart,
                                     const resource_pointer_spec_t& spec,
                                     wormedit::local_position_t position,
                                     const resource_pointer_target_t& pointer)
    -> std::variant<wormedit::editor_selection_object_t, std::string> {
    using namespace wormedit;
    if (spec.kind == resource_placement_kind_t::sample_note && pointer.note)
        return editor_selection_object_t{editor_selection_object_kind_t::note, *pointer.note};
    if (spec.kind == resource_placement_kind_t::bgm_event && input.lane_id != 0)
        return editor_selection_object_t{editor_selection_object_kind_t::bgm_event,
                                         object_id_t{input.lane_id}};
    std::optional<editor_selection_object_t> target;
    if (spec.kind == resource_placement_kind_t::bga_background) {
        if (input.lane_id != 0) {
            const auto event =
                std::ranges::find(chart.bga_events, object_id_t{input.lane_id}, &bga_event_t::id);
            if (event == chart.bga_events.end() ||
                editor_bga_lane_index(event->kind) !=
                    editor_bga_lane_index(static_cast<visual_event_kind_t>(input.bga_lane_kind)))
                return "The selected BGA event is stale or cannot be replaced in this lane.";
            return editor_selection_object_t{editor_selection_object_kind_t::bga_event, event->id};
        }
        for (const auto& event : chart.bga_events) {
            if (editor_bga_lane_index(event.kind) !=
                    editor_bga_lane_index(static_cast<visual_event_kind_t>(input.bga_lane_kind)) ||
                event.position != position)
                continue;
            if (target)
                return "More than one BGA background occupies this row.";
            target = editor_selection_object_t{editor_selection_object_kind_t::bga_event, event.id};
        }
    }
    if (!target)
        return "Click an existing object in the selected resource lane to replace it.";
    return *target;
}

auto dispatch_resource_pointer(const WormUiComposeInputAction& input,
                               compose_input_context_t& context,
                               wormedit::editor_interaction_result_t& result,
                               std::string_view& accepted) -> compose_input_family_result_t {
    using namespace wormedit;
    const auto spec_result = resource_pointer_spec(input, context);
    if (const auto* error = std::get_if<std::string>(&spec_result))
        return *error;
    const auto& spec = std::get<resource_pointer_spec_t>(spec_result);
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(context.environment.history_ref, context.state);
    if (!chart || !view)
        return "The selected chart or armed resource is no longer available.";
    const auto target_result =
        resolve_resource_pointer_target(input, context, *chart, *view, spec, result, accepted);
    if (const auto* error = std::get_if<std::string>(&target_result))
        return *error;
    const auto& pointer = std::get<resource_pointer_target_t>(target_result);
    if (!pointer.position) {
        return pointer.handled_virtual
                   ? compose_input_family_result_t::continue_with_result()
                   : compose_input_family_result_t{
                         "Pointer placement could not snap to a valid chart row."};
    }
    const auto position = resolve_resource_placement_position(
        *chart, *pointer.position, edit_snap_denominator(*chart, context.state));
    if (!position)
        return "Pointer placement could not snap to a valid chart row.";
    document_command_result_t applied;
    if (spec.replacing) {
        const auto replacement =
            replacement_target_for_resource(input, *chart, spec, *position, pointer);
        if (const auto* error = std::get_if<std::string>(&replacement))
            return *error;
        applied = apply_resource_assignment(
            context.environment.history_ref, context.ui_action.compose_input_document_revision,
            {.chart = context.state.chart,
             .resource = spec.resource,
             .objects = {std::get<editor_selection_object_t>(replacement)}});
    } else {
        applied = apply_resource_placement(
            context.environment.history_ref, context.ui_action.compose_input_document_revision,
            {.chart = context.state.chart,
             .resource = spec.resource,
             .position = *position,
             .lane = pointer.lane,
             .source_lane = spec.kind == resource_placement_kind_t::bgm_event
                                ? object_id_t{input.lane_id}
                                : object_id_t{},
             .kind = spec.kind,
             .semantic_layers = context.state.armed_note_layers,
             .bga_kind = resource_pointer_bga_kind(input, spec.kind)});
        if (applied.applied)
            advance_dock_after_placement(context.environment.history_ref.document, spec.resource,
                                         spec.required_role);
    }
    const auto unchanged = spec.replacing && !applied.applied && applied.diagnostics.empty();
    result.accepted = applied.applied || unchanged;
    result.document_changed = applied.applied;
    result.diagnostics = applied.diagnostics;
    if (result.accepted) {
        place_cursor_at(context.state, *position);
        if (pointer.lane)
            context.state.active_lane = pointer.lane;
    }
    accepted = unchanged        ? "Object already uses the armed resource."
               : spec.replacing ? "Object resource replaced at the pointer."
                                : "Resource placed at the pointer.";
    return compose_input_family_result_t::continue_with_result();
}
} // namespace wormedit::sys::compose_input_detail

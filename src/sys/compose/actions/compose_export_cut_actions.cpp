#include "sys/compose/actions/compose_actions_internal.h"
#include <wormedit/core/export_cut.h>
#include <wormedit/core/numeric_text.h>

namespace wormedit::sys::compose_action_detail {
namespace {
using namespace wormedit;

auto selected_cut_value(compose_action_context_t& context)
    -> result_t<export_cut_edit_value_t, std::string> {
    auto& history = context.environment.document.history;
    const auto* chart = selected_chart(history, context.environment.document.workspace);
    if (!chart || !context.state.retained_selection_range)
        return std::unexpected("Select a materialized area to create an export cut.");
    const auto* indices = history.indices(chart->id);
    if (!indices || !indices->measures || !indices->timing.value)
        return std::unexpected("The selected chart has no valid timing for an export cut.");
    const auto& selection = *context.state.retained_selection_range;
    const auto begin = indices->measures->row_of(selection.begin);
    const auto end = indices->measures->row_of(selection.end);
    if (!begin || !end || *end <= *begin)
        return std::unexpected("Export cuts require a nonempty materialized selection.");
    const auto grid = edit_snap_grid(*chart, context.state);
    const auto snapped_begin = snap_absolute_row(*chart, *begin, grid);
    const auto snapped_end = snap_absolute_row(*chart, *end, grid);
    const auto span = snapped_begin && snapped_end
        ? export_cut_interval_t::from_rows(*indices->timing.value, *snapped_begin, *snapped_end)
        : std::nullopt;
    if (!span) return std::unexpected("The selected area does not resolve to a positive source-song interval.");
    return export_cut_edit_value_t{.label = context.action.compose_marker_label.empty()
            ? std::nullopt : std::optional{context.action.compose_marker_label},
        .begin_milliseconds = span->begin_milliseconds, .end_milliseconds = span->end_milliseconds};
}

auto typed_cut_value(const wormedit::ui::workspace_action_t& action)
    -> result_t<export_cut_edit_value_t, std::string> {
    const auto begin = parse_integer<std::int64_t>(action.compose_marker_audio_begin.c_str());
    const auto end = parse_integer<std::int64_t>(action.compose_marker_audio_end.c_str());
    if (!begin || !end || *begin < 0 || *end <= *begin)
        return std::unexpected("Enter a nonnegative cut start and a later end in whole milliseconds.");
    return export_cut_edit_value_t{.label = action.compose_marker_label.empty()
            ? std::nullopt : std::optional{action.compose_marker_label},
        .begin_milliseconds = *begin, .end_milliseconds = *end};
}

auto build_export_cut_edit(compose_action_context_t& context)
    -> result_t<export_cut_edit_action_t, std::string> {
    const auto kind = context.action.compose_action;
    if (kind == WORM_UI_COMPOSE_ACTION_DELETE_EXPORT_CUT)
        return delete_edit_t<export_cut_edit_value_t>{.id = context.action.compose_export_cut_id};
    auto value = kind == WORM_UI_COMPOSE_ACTION_ADD_EXPORT_CUT_FROM_SELECTION
        ? selected_cut_value(context) : typed_cut_value(context.action);
    if (!value) return std::unexpected(std::move(value.error()));
    if (kind == WORM_UI_COMPOSE_ACTION_ADD_EXPORT_CUT_FROM_SELECTION)
        return insert_edit_t<export_cut_edit_value_t>{.value = std::move(*value)};
    return update_edit_t<export_cut_edit_value_t>{.id = context.action.compose_export_cut_id,
        .value = std::move(*value)};
}
} // namespace

auto edit_export_cut_action_t::validate(compose_action_context_t& context) const
    -> std::optional<compose_action_outcome_t> {
    return context.require_no_gesture("Finish or cancel the active Compose gesture before editing export cuts.");
}

auto edit_export_cut_action_t::perform(compose_action_context_t& context) const -> compose_action_outcome_t {
    auto edit = build_export_cut_edit(context);
    if (!edit) return compose_action_outcome_t::blocked(std::move(edit.error()), notification_source_t::edit);
    return compose_action_outcome_t::from(apply_export_cut_edit(context.environment.document.history, *edit),
        compose_action_description_t{.success = "Export cut updated.", .unchanged = "Export cut unchanged.",
            .rebind_failure = "Export cut changed, but workspace rebinding failed.",
            .source = notification_source_t::edit});
}
} // namespace wormedit::sys::compose_action_detail

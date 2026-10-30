#include "sys/compose/actions/compose_quantize.h"

#include <utility>
#include "sys/app/application_command_state.h"
#include "sys/compose/actions/compose_action_routes.h"
#include "sys/compose/actions/compose_quantize_request.h"
#include <wormedit/core/timing.h>
#include <wormedit/core/command.h>
#include <wormedit/core/interaction_dispatch.h>

namespace wormedit::sys {
namespace {

void normalize_compose_quantize_request(quantize_request_t& request,
                                        const editor_interaction_state_t& state) {
    request.collisions = quantization_collision_t::reject;
    request.allow_resolution_promotion = false;
    request.objects.clear();
    request.selection = editor_quantize_selection(state);
}

} // namespace

auto is_compose_quantize_request_valid(const quantize_request_t& request) -> bool {
    return request.divisions != 0 && request.space <= quantization_space_t::authored_measure &&
           request.tie <= quantization_tie_t::reject &&
           request.holds <= quantization_hold_t::both_endpoints;
}

auto compose_quantize_editor_for(document_history_t& history, editor_interaction_state_t& state,
                                 application_command_state_t& application)
    -> compose_quantize_editor_t& {
    auto& editor = application.project.quantize.quantize_editor;
    if (!editor || !editor->stamp.is_identity_current(history.document, state.chart)) {
        editor = compose_quantize_editor_t{
            .stamp = chart_stamp_t::from(history.document, state.chart),
            .request = {.divisions = state.active_snap_denominator,
                        .space = state.active_snap_space}};
    }
    return *editor;
}

auto can_apply_compose_quantize(const application_command_state_t& application,
                                const editor_interaction_state_t& state) -> bool {
    return !application.project.compose_view.compose_timing_focus &&
           !application.project.clipboard.paste_session && !state.is_pending();
}

auto toggle_compose_quantize_preview(document_history_t& history,
                                     editor_interaction_state_t& state,
                                     application_command_state_t& application) -> bool {
    auto& editor = compose_quantize_editor_for(history, state, application);
    editor.preview_enabled = !editor.preview_enabled;
    return editor.preview_enabled;
}

auto apply_compose_quantize_selection(document_history_t& history,
                                      editor_interaction_state_t& state,
                                      application_command_state_t& application,
                                      quantize_request_t request) -> compose_action_outcome_t {
    if (!is_compose_quantize_request_valid(request))
        return compose_action_outcome_t::blocked("Selection quantization has invalid settings.");
    normalize_compose_quantize_request(request, state);
    const auto* chart = history.read_chart(state.chart);
    if (!chart)
        return compose_action_outcome_t::blocked("The chart is no longer available.");
    const auto& geometry = history.indices(chart->id)->measures;
    if (!geometry)
        return compose_action_outcome_t::blocked("The selected area has invalid measure geometry.");
    auto report = preview_quantize_selection(*chart, request.selection->notes, request, &*geometry);
    auto result = apply_quantize_command(history, state.chart, request);
    if (!result.command.applied)
        return result.command.diagnostics.empty()
                   ? compose_action_outcome_t::blocked("Selection quantization rejected.")
                   : compose_action_outcome_t::from(std::move(result.command.diagnostics),
                                                    notification_source_t::edit,
                                                    compose_feedback_delivery_t::first_diagnostic);
    application.project.quantize.quantize_applied_report =
        compose_quantize_applied_report_t{
            .song = history.document.id,
            .chart = state.chart,
            .rows = std::move(report.report_rows),
            .total_rows = report.report_total_rows,
            .truncated = report.report_truncated};
    return compose_action_outcome_t::from(
        std::move(result.command),
        compose_action_description_t{
            .success = "Selected objects quantized.",
            .unchanged = {},
            .rebind_failure = "Selected objects quantized, but workspace rebinding failed.",
            .source = notification_source_t::edit,
            .diagnostic_policy = compose_diagnostic_feedback_policy_t::rejected_only});
}

namespace compose_action_detail {
namespace {
auto quantize_editor_for(compose_action_context_t& context)
    -> compose_quantize_editor_t& {
    return compose_quantize_editor_for(context.environment.document.history, context.state,
                                       context.environment.editor.commands);
}

// The Quantize grid and the editing snap are one grid: setting the target also sets the snap
// when the chart supports it. An unsupported grid stays quantize-only, silently; the space
// always follows.
void share_quantize_grid_with_snap(compose_action_context_t& context, wormedit::snap_grid_t grid) {
    auto& compose_view = context.environment.editor.commands.project.compose_view;
    compose_view.snap_space = grid.space;
    const auto result = wormedit::dispatch_editor_action(
        context.environment.document.history, context.state,
        wormedit::set_edit_snap_action_t{.denominator = grid.denominator, .space = grid.space});
    if (result.accepted && context.state.active_snap_denominator == grid.denominator)
        compose_view.requested_snap_denominators.insert_or_assign(context.state.chart.value,
                                                                  grid.denominator);
}

struct apply_compose_quantize_settings_action_t {
    auto operator==(const apply_compose_quantize_settings_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) const -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<apply_compose_quantize_settings_action_t, compose_action_context_t>);

auto apply_compose_quantize_settings_action_t::validate(compose_action_context_t&) const
    -> std::optional<compose_action_outcome_t> {
    return std::nullopt;
}

auto apply_compose_quantize_settings_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    auto& editor = quantize_editor_for(context);
    const auto request = quantize_request_from_action(context.action, {});
    if (!request)
        return compose_action_outcome_t::blocked(
            "Quantization settings must use a positive grid and supported policies.",
            notification_source_t::edit);
    editor.request = *request;
    editor.preset_index.reset();
    share_quantize_grid_with_snap(context, {.denominator = request->divisions, .space = request->space});
    return compose_action_outcome_t::from("Quantization settings updated.",
                                          notification_severity_t::info,
                                          notification_source_t::input);
}

struct select_compose_quantize_preset_action_t {
    decltype(wormedit::ui::workspace_action_t::compose_quantize_preset_index) index{};
    const quantization_preset_t* selected_preset{};
    auto operator==(const select_compose_quantize_preset_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<select_compose_quantize_preset_action_t, compose_action_context_t>);

auto select_compose_quantize_preset_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    index = context.action.compose_quantize_preset_index;
    if (index >= context.environment.document.history.document.quantization_presets.size())
        return compose_action_outcome_t::blocked(
            "The selected quantization preset is no longer available.",
            notification_source_t::edit);
    selected_preset = &context.environment.document.history.document.quantization_presets[index];
    return std::nullopt;
}

auto select_compose_quantize_preset_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto& preset = *selected_preset;
    auto& editor = quantize_editor_for(context);
    editor.request = {.divisions = preset.divisions, .space = preset.space, .tie = preset.tie,
                      .collisions = quantization_collision_t::reject, .holds = preset.holds};
    editor.preset_index = index;
    share_quantize_grid_with_snap(context, {.denominator = preset.divisions, .space = preset.space});
    return compose_action_outcome_t::from("Quantization preset selected.",
                                          notification_severity_t::info,
                                          notification_source_t::input);
}

auto validate_quantize_preset_save(const compose_action_context_t& context,
                                   std::size_t name_length,
                                   const std::optional<wormedit::quantize_request_t>& request)
    -> std::optional<std::string> {
    using namespace wormedit;
    if (name_length == 0 || name_length >= quantization_preset_t::maximum_name_bytes + 1)
        return "Enter a name for the quantization preset (maximum 64 bytes).";
    if (name_length > quantization_preset_t::maximum_name_bytes)
        return "Quantization preset names must be at most 64 bytes.";
    const std::string_view name(context.action.compose_quantize_preset_name.data(), name_length);
    if (std::ranges::any_of(context.environment.document.history.document.quantization_presets,
                            [&](const auto& preset) { return preset.name == name; }))
        return "A quantization preset already has that name.";
    if (context.environment.document.history.document.quantization_presets.size() >=
        quantization_preset_t::maximum_count)
        return "The project already has the maximum number of quantization presets.";
    if (!request || !is_compose_quantize_request_valid(*request))
        return "Quantization settings are invalid.";
    return std::nullopt;
}

struct save_compose_quantize_preset_action_t {
    std::optional<quantize_request_t> request{};
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<save_compose_quantize_preset_action_t, compose_action_context_t>);

auto save_compose_quantize_preset_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    const auto length = context.action.compose_quantize_preset_name.size();
    request = quantize_request_from_action(context.action, {});
    if (const auto error = validate_quantize_preset_save(context, length, request))
        return compose_action_outcome_t::blocked(*error, notification_source_t::edit);
    return std::nullopt;
}

auto save_compose_quantize_preset_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr std::string_view rebind_failure =
        "Quantization preset saved, but workspace rebinding failed.";
    auto transaction = document_command_transaction_t(context.environment.document.history);
    const auto index = transaction.current_document().quantization_presets.size();
    auto committed = transaction.finish_execution(transaction.stage(
        [this, &context](song_document_t& proposed) -> attempt_t<document_edit_effect_t> {
            proposed.quantization_presets.push_back({
                .name = context.action.compose_quantize_preset_name,
                .divisions = request->divisions, .space = request->space,
                .tie = request->tie, .holds = request->holds});
            return {.value = document_edit_effect_t{
                .changed = true, .command_label = "Save quantization preset"}};
        }));
    if (!committed)
        return committed.diagnostics.empty()
                   ? compose_action_outcome_t::blocked("Quantization preset could not be saved.",
                                                       notification_source_t::edit)
                   : compose_action_outcome_t::from(
                         std::move(committed),
                         compose_action_description_t{.rebind_failure = rebind_failure,
                                                      .source = notification_source_t::edit});
    auto& editor = quantize_editor_for(context);
    editor.request = *request;
    editor.preset_index = index;
    return compose_action_outcome_t::from(std::move(committed),
        compose_action_description_t{.success = "Quantization preset saved.",
            .rebind_failure = rebind_failure, .source = notification_source_t::edit,
            .diagnostic_policy = compose_diagnostic_feedback_policy_t::rejected_only});
}

struct delete_compose_quantize_preset_action_t {
    decltype(wormedit::ui::workspace_action_t::compose_quantize_preset_index) index{};
    auto operator==(const delete_compose_quantize_preset_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<delete_compose_quantize_preset_action_t, compose_action_context_t>);

auto delete_compose_quantize_preset_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    index = context.action.compose_quantize_preset_index;
    if (index >= context.environment.document.history.document.quantization_presets.size())
        return compose_action_outcome_t::blocked(
            "The selected quantization preset is no longer available.",
            notification_source_t::edit);
    return std::nullopt;
}

auto delete_compose_quantize_preset_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr std::string_view rebind_failure =
        "Quantization preset deleted, but workspace rebinding failed.";
    auto transaction = document_command_transaction_t(context.environment.document.history);
    auto committed = transaction.finish_execution(transaction.stage(
        [this](song_document_t& proposed) -> attempt_t<document_edit_effect_t> {
            proposed.quantization_presets.erase(proposed.quantization_presets.begin() +
                                                static_cast<std::ptrdiff_t>(index));
            return {.value = document_edit_effect_t{
                .changed = true, .command_label = "Delete quantization preset"}};
        }));
    if (!committed)
        return committed.diagnostics.empty()
                   ? compose_action_outcome_t::blocked("Quantization preset could not be deleted.",
                                                       notification_source_t::edit)
                   : compose_action_outcome_t::from(
                         std::move(committed),
                         compose_action_description_t{.rebind_failure = rebind_failure,
                                                      .source = notification_source_t::edit});
    auto& editor = quantize_editor_for(context);
    if (editor.preset_index) {
        if (*editor.preset_index == index) editor.preset_index.reset();
        else if (*editor.preset_index > index) --*editor.preset_index;
    }
    return compose_action_outcome_t::from(std::move(committed),
        compose_action_description_t{.success = "Quantization preset deleted.",
            .rebind_failure = rebind_failure, .source = notification_source_t::edit,
            .diagnostic_policy = compose_diagnostic_feedback_policy_t::rejected_only});
}

struct rename_compose_quantize_preset_action_t {
    decltype(wormedit::ui::workspace_action_t::compose_quantize_preset_index) index{};
    std::string_view name{};
    auto operator==(const rename_compose_quantize_preset_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<rename_compose_quantize_preset_action_t, compose_action_context_t>);

auto rename_compose_quantize_preset_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    index = context.action.compose_quantize_preset_index;
    const auto& presets = context.environment.document.history.document.quantization_presets;
    if (index >= presets.size())
        return compose_action_outcome_t::blocked(
            "The selected quantization preset is no longer available.",
            notification_source_t::edit);
    const auto length = context.action.compose_quantize_preset_name.size();
    if (length == 0 || length > quantization_preset_t::maximum_name_bytes)
        return compose_action_outcome_t::blocked(
            "Enter a name for the quantization preset (maximum 64 bytes).",
            notification_source_t::edit);
    name = std::string_view(context.action.compose_quantize_preset_name.data(), length);
    for (std::size_t other = 0; other < presets.size(); ++other)
        if (other != index && presets[other].name == name)
            return compose_action_outcome_t::blocked("A quantization preset already has that name.",
                                                     notification_source_t::edit);
    if (presets[index].name == name)
        return compose_action_outcome_t::from("Quantization preset name was unchanged.",
                                              notification_severity_t::info,
                                              notification_source_t::edit);
    return std::nullopt;
}

auto rename_compose_quantize_preset_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr std::string_view rebind_failure =
        "Quantization preset renamed, but workspace rebinding failed.";
    auto transaction = document_command_transaction_t(context.environment.document.history);
    auto committed = transaction.finish_execution(transaction.stage(
        [this](song_document_t& proposed) -> attempt_t<document_edit_effect_t> {
            proposed.quantization_presets[index].name = name;
            return {.value = document_edit_effect_t{
                .changed = true, .command_label = "Rename quantization preset"}};
        }));
    if (!committed)
        return committed.diagnostics.empty()
                   ? compose_action_outcome_t::blocked("Quantization preset could not be renamed.",
                                                       notification_source_t::edit)
                   : compose_action_outcome_t::from(
                         std::move(committed),
                         compose_action_description_t{.rebind_failure = rebind_failure,
                                                      .source = notification_source_t::edit});
    return compose_action_outcome_t::from(std::move(committed),
        compose_action_description_t{.success = "Quantization preset renamed.",
            .rebind_failure = rebind_failure, .source = notification_source_t::edit,
            .diagnostic_policy = compose_diagnostic_feedback_policy_t::rejected_only});
}

struct toggle_compose_quantize_preview_action_t {
    auto operator==(const toggle_compose_quantize_preview_action_t&) const -> bool = default;
    auto validate(compose_action_context_t&) const -> std::optional<compose_action_outcome_t> {
        return std::nullopt;
    }
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<toggle_compose_quantize_preview_action_t, compose_action_context_t>);

auto toggle_compose_quantize_preview_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    const auto enabled = toggle_compose_quantize_preview(context.environment.document.history,
                                                         context.state,
                                                         context.environment.editor.commands);
    return compose_action_outcome_t::from(
        enabled ? "Quantize preview enabled." : "Quantize preview disabled.",
        notification_severity_t::info, notification_source_t::input);
}

constexpr std::array quantize_route_entries{
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TOGGLE_QUANTIZE_PREVIEW,
                           .operation =
                               run_routed_compose_action<toggle_compose_quantize_preview_action_t>,
                           .bypasses_area_clear = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS,
                           .operation =
                               run_routed_compose_action<apply_compose_quantize_settings_action_t>,
                           .bypasses_area_clear = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SELECT_QUANTIZE_PRESET,
                           .operation =
                               run_routed_compose_action<select_compose_quantize_preset_action_t>,
                           .bypasses_area_clear = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SAVE_QUANTIZE_PRESET,
                           .operation =
                               run_routed_compose_action<save_compose_quantize_preset_action_t>,
                           .bypasses_area_clear = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_DELETE_QUANTIZE_PRESET,
                           .operation =
                               run_routed_compose_action<delete_compose_quantize_preset_action_t>,
                           .bypasses_area_clear = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_RENAME_QUANTIZE_PRESET,
                           .operation =
                               run_routed_compose_action<rename_compose_quantize_preset_action_t>,
                           .bypasses_area_clear = true},
};
}
static_assert(is_valid_route_module(quantize_route_entries));
constinit const std::span<const compose_action_route_t> compose_quantize_routes{quantize_route_entries};
}
} // namespace wormedit::sys

#include <wormedit/core/timing.h>
#include "sys/compose_actions_internal.h"
#include "sys/resource_action_context.h"
namespace wormedit::sys::compose_action_detail {
auto import_resource_role(const wormedit::ui::workspace_action_t& action)
    -> std::optional<wormedit::resource_role_t> {
    using namespace wormedit;
    if (!action.compose_resource_role) return std::nullopt;
    switch (*action.compose_resource_role) {
    case resource_role_t::sample:
    case resource_role_t::bgm:
    case resource_role_t::bga:
    case resource_role_t::music:
        return action.compose_resource_role;
    case resource_role_t::preview:
        return std::nullopt;
    }
    return std::nullopt;
}

auto inspect_imported_resource(const std::filesystem::path& path, wormedit::resource_role_t role)
    -> std::variant<imported_resource_metadata_t, std::string> {
    imported_resource_metadata_t metadata;
    if (role == wormedit::resource_role_t::bga) {
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        const auto decoded = wormedit::visual::open_image_rgba(
            {.kind = wormedit::asset_kind_t::image, .path = path});
        if (!decoded)
            return decoded.error().message.empty() ? "The image could not be decoded."
                                                   : decoded.error().message;
        metadata.image = std::pair{decoded->width, decoded->height};
#else
        return "Image import is unavailable in this build.";
#endif
    } else {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        wormedit::audio::decoder_t decoder;
        std::string error;
        if (!decoder.open(path, &error))
            return error.empty() ? "The audio file could not be decoded." : error;
        const auto info = decoder.info();
        if (!info)
            return "The audio decoder did not report source geometry.";
        metadata.audio = {.sample_rate = info->source_sample_rate,
                          .channels = info->source_channel_count,
                          .frame_count = info->source_frame_count,
                          .seekable = info->seekable};
#else
        return "Audio import is unavailable in this build.";
#endif
    }
    return metadata;
}

compose_action_outcome_t commit_primary_music(compose_action_context_t& context,
                                              const std::filesystem::path& path,
                                              const imported_resource_metadata_t& metadata) {
    constexpr std::string_view rebind_failure = "Song audio set, but workspace rebinding failed.";
    using namespace wormedit;
    const auto* chart =
        context.environment.document.history.document.find_chart(context.state.chart);
    auto name = path.filename().generic_string();
    if (name.size() > 255)
        name.clear();
    auto applied = apply_primary_music_set(
        context.environment.document.history,
        {.source_path = path,
         .name = std::move(name),
         .allocate_bms_identifier =
             chart != nullptr && chart->source_family == format_family_t::bms,
         .audio_stream = metadata.audio});
    if (!applied.diagnostics.empty())
        return compose_action_outcome_t::from(
            std::move(applied),
            compose_action_description_t{.rebind_failure = rebind_failure,
                                         .source = notification_source_t::import_});
    if (!applied.applied)
        return compose_action_outcome_t::from("That file is already the song audio.",
                                              notification_severity_t::info,
                                              notification_source_t::import_);

    const auto music = std::ranges::find_if(
        context.environment.document.history.document.resources, [](const auto& r) {
            return std::ranges::find(r.roles, resource_role_t::music) != r.roles.end();
        });

    auto outcome = compose_action_outcome_t::from(
        "Song audio set to " + path.filename().string() + ".", notification_severity_t::success,
        notification_source_t::import_, applied.applied);
    outcome.rebind_failure = rebind_failure;
    if (music != context.environment.document.history.document.resources.end())
        outcome.asset_selection = workspace_select_asset_action_t{
            .asset = music->asset, .slot = workspace_asset_slot_t::audio};
    outcome.diagnostics = std::move(applied.diagnostics);
    return outcome;
}

compose_action_outcome_t commit_resource_import(compose_action_context_t& context,
                                                wormedit::resource_role_t role,
                                                const std::filesystem::path& path,
                                                imported_resource_metadata_t metadata) {
    constexpr std::string_view rebind_failure =
        "Resource imported, but workspace rebinding failed.";
    using namespace wormedit;
    if (role == resource_role_t::music)
        return commit_primary_music(context, path, metadata);
    const auto* chart =
        context.environment.document.history.document.find_chart(context.state.chart);
    auto name = path.filename().generic_string();
    if (name.size() > 255)
        name.clear();
    auto applied = apply_resource_asset_import(
        context.environment.document.history,
        {.source_path = path,
         .role = role,
         .name = std::move(name),
         .allocate_bms_identifier =
             chart != nullptr && chart->source_family == format_family_t::bms,
         .audio_stream = metadata.audio,
         .image_geometry = metadata.image});
    if (!applied.diagnostics.empty())
        return compose_action_outcome_t::from(
            std::move(applied),
            compose_action_description_t{.rebind_failure = rebind_failure,
                                         .source = notification_source_t::import_});
    if (!applied.applied)
        return compose_action_outcome_t::from("Resource import made no document change.",
                                              notification_severity_t::info,
                                              notification_source_t::import_);
    const auto imported = context.environment.document.history.document.resources.back().asset;

    auto outcome = compose_action_outcome_t::from("External file linked as a resource.",
                                                  notification_severity_t::success,
                                                  notification_source_t::import_, applied.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.asset_selection = workspace_select_asset_action_t{
        .asset = imported,
        .slot = role == resource_role_t::bga ? workspace_asset_slot_t::visual
                                             : workspace_asset_slot_t::audio};
    outcome.diagnostics = std::move(applied.diagnostics);
    return outcome;
}

struct import_compose_resource_action_t {
    std::optional<resource_role_t> role{};
    std::filesystem::path path{};
    auto operator==(const import_compose_resource_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<import_compose_resource_action_t, compose_action_context_t>);

auto import_compose_resource_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before importing a resource.")) {
        blocked->source = notification_source_t::import_;
        return std::move(*blocked);
    }
    role = import_resource_role(context.action);
    if (!role)
        return compose_action_outcome_t::blocked("Resource import used an unknown catalog role.",
                                                 notification_source_t::import_);
    path = std::filesystem::path{context.action.compose_import_path}.lexically_normal();
    std::error_code error;
    if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error) || error)
        return compose_action_outcome_t::blocked(
            "Resource import requires an existing absolute file path.",
            notification_source_t::import_);
    return std::nullopt;
}

auto import_compose_resource_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto metadata = inspect_imported_resource(path, *role);
    if (const auto* failure = std::get_if<std::string>(&metadata))
        return compose_action_outcome_t::blocked(*failure, notification_source_t::import_);
    return commit_resource_import(context, *role, path,
                                  std::get<imported_resource_metadata_t>(std::move(metadata)));
}

struct copy_compose_resource_action_t {
    resource_copy_plan_t plan{};
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) -> compose_action_outcome_t;
};
static_assert(compose_action<copy_compose_resource_action_t, compose_action_context_t>);

auto copy_compose_resource_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.environment.document.history;
    if (context.environment.resources->jobs.copy.is_busy() ||
        context.environment.resources->jobs.exporter.is_busy() || context.metadata_import_busy)
        return compose_action_outcome_t::blocked(
            "Wait for the current resource copy, import, or export to finish.",
            notification_source_t::import_);
    if (context.environment.resources->asset_base.empty())
        return compose_action_outcome_t::blocked(
            "Open a saved native project before copying resource files into it.",
            notification_source_t::import_);
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before copying a resource.")) {
        blocked->source = notification_source_t::import_;
        return std::move(*blocked);
    }
    const asset_id_t id{action.compose_resource_asset_id.value};
    const auto* audio = history.document.find_audio_asset(id);
    const auto visual = std::ranges::find(history.document.visual_assets, id, &visual_asset_t::id);
    if (!id || (audio == nullptr && visual == history.document.visual_assets.end()))
        return compose_action_outcome_t::blocked(
            "The selected resource asset is no longer available.", notification_source_t::import_);
    const auto& source = audio != nullptr ? audio->source_path : visual->source_path;
    const bool exporting = action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT;
    if (!exporting && !source.is_absolute() && source.parent_path() == "assets" &&
        source.filename().string().starts_with(std::to_string(id.value) + "-"))
        return compose_action_outcome_t::from(
            "Resource source is already in the project assets directory.",
            notification_severity_t::info, notification_source_t::import_);
    std::filesystem::path output;
    if (exporting) {
        if (action.compose_output_group_index >= history.document.output_groups.size())
            return compose_action_outcome_t::blocked(
                "The selected output group is no longer available.",
                notification_source_t::export_);
        const auto& group = history.document.output_groups[action.compose_output_group_index];
        if (std::ranges::find(group.charts, context.state.chart) == group.charts.end())
            return compose_action_outcome_t::blocked(
                "The selected output group does not include this chart.",
                notification_source_t::export_);
        output = group.relative_output_path;
    }
    plan = resource_copy_plan_t{id, source, output, exporting};
    return std::nullopt;
}

auto copy_compose_resource_action_t::perform(compose_action_context_t& context)
    -> compose_action_outcome_t {
    return start_resource_copy(context, std::move(plan));
}

compose_action_outcome_t start_resource_copy(compose_action_context_t& context,
                                             resource_copy_plan_t plan) {
    using namespace wormedit;
    const auto source =
        plan.export_copy ? notification_source_t::export_ : notification_source_t::import_;
    auto started = context.environment.resources->jobs.copy.start(
        {.target = {.asset = plan.asset,
                    .document =
                        document_stamp_t::from(context.environment.document.history.document),
                    .source_path = plan.source,
                    .output_path = plan.output,
                    .export_copy = plan.export_copy},
         .project_directory = context.environment.resources->asset_base});
    if (!started) {
        if (started.error().code == failure_code_t::invalid_request)
            return compose_action_outcome_t::blocked(started.error().message, source);
        return compose_action_outcome_t::from(started.error().message,
                                              notification_severity_t::error, source, false, false);
    }
    auto outcome = compose_action_outcome_t::from(plan.export_copy
                                                      ? "Export copy started in the background."
                                                      : "Resource copy started in the background.",
                                                  notification_severity_t::info, source);
    outcome.delivery = compose_feedback_delivery_t::none;
    return outcome;
}

struct export_compose_output_group_action_t {
    output_group_export_plan_t plan{};
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) -> compose_action_outcome_t;
};
static_assert(compose_action<export_compose_output_group_action_t, compose_action_context_t>);

auto export_compose_output_group_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    const auto& doc = context.environment.document.history.document;
    if (context.environment.resources->asset_base.empty())
        return compose_action_outcome_t::blocked(
            "Open a saved native project before exporting an output group.",
            notification_source_t::export_);
    if (context.environment.resources->jobs.copy.is_busy() ||
        context.environment.resources->jobs.exporter.is_busy() || context.metadata_import_busy)
        return compose_action_outcome_t::blocked(
            "Wait for the current resource copy, import, or export to finish.",
            notification_source_t::export_);
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before exporting.")) {
        blocked->source = notification_source_t::export_;
        return std::move(*blocked);
    }
    const auto valid_group = [&](const auto& group) {
        return !group.charts.empty() && std::ranges::none_of(group.charts, [&](const auto id) {
            return doc.find_chart(id) == nullptr;
        });
    };
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_EXPORT_ALL_OUTPUT_GROUPS) {
        if (doc.output_groups.empty() || !std::ranges::all_of(doc.output_groups, valid_group))
            return compose_action_outcome_t::blocked(
                "Every export target must contain matching charts.",
                notification_source_t::export_);
        plan = *output_group_export_plan_t::from_all(doc, doc.output_groups);
        return std::nullopt;
    }
    if (context.action.compose_output_group_index >= doc.output_groups.size())
        return compose_action_outcome_t::blocked(
            "The selected output group is no longer available.", notification_source_t::export_);
    auto group = doc.output_groups[context.action.compose_output_group_index];
    if (!valid_group(group))
        return compose_action_outcome_t::blocked(
            "The selected output group has no matching charts.", notification_source_t::export_);
    std::optional<existing_files_policy_t> one_run_policy;
    switch (context.action.compose_existing_files) {
    case WORM_UI_EXISTING_FILES_SAVED:
        break;
    case WORM_UI_EXISTING_FILES_ASK:
        one_run_policy = existing_files_policy_t::ask;
        break;
    case WORM_UI_EXISTING_FILES_REPLACE:
        one_run_policy = existing_files_policy_t::replace;
        break;
    case WORM_UI_EXISTING_FILES_KEEP:
        one_run_policy = existing_files_policy_t::keep;
        break;
    default:
        return compose_action_outcome_t::blocked("The existing-files choice is invalid.",
                                                 notification_source_t::export_);
    }
    plan = output_group_export_plan_t::from(doc, std::move(group), one_run_policy);
    return std::nullopt;
}

struct confirm_output_export_action_t {
    bool cancel{};
    auto operator==(const confirm_output_export_action_t&) const -> bool = default;
    auto validate(compose_action_context_t&) -> std::optional<compose_action_outcome_t> {
        return std::nullopt;
    }
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t {
        if (cancel) {
            context.environment.resources->jobs.exporter.cancel_confirmation();
            return compose_action_outcome_t::from("Export cancelled before publication.",
                                                  notification_severity_t::info,
                                                  notification_source_t::export_);
        }
        auto started = context.environment.resources->jobs.exporter.confirm(
            context.environment.document.history);
        if (!started)
            return compose_action_outcome_t::blocked(started.error().message,
                                                     notification_source_t::export_);
        auto outcome = compose_action_outcome_t::from(
            "Output replacement started in the background.", notification_severity_t::info,
            notification_source_t::export_);
        outcome.delivery = compose_feedback_delivery_t::none;
        return outcome;
    }
};
static_assert(compose_action<confirm_output_export_action_t, compose_action_context_t>);

auto export_compose_output_group_action_t::perform(compose_action_context_t& context)
    -> compose_action_outcome_t {
    return start_output_export(context, std::move(plan));
}

compose_action_outcome_t start_output_export(compose_action_context_t& context,
                                             output_group_export_plan_t plan) {
    using namespace wormedit;
    auto started = context.environment.resources->jobs.exporter.start(
        context.environment.document.history.document, std::move(plan),
        context.environment.resources->asset_base);
    if (!started)
        return compose_action_outcome_t::from(
            started.error().message, notification_severity_t::error,
            notification_source_t::export_, false, false);
    auto outcome = compose_action_outcome_t::from(
        "Export target preparation started in the background.", notification_severity_t::info,
        notification_source_t::export_);
    outcome.delivery = compose_feedback_delivery_t::none;
    return outcome;
}

auto compose_placement_kind(const wormedit::ui::workspace_action_t& action)
    -> std::optional<wormedit::resource_placement_kind_t> {
    using namespace wormedit;
    if (!action.compose_resource_role) return std::nullopt;
    switch (*action.compose_resource_role) {
    case resource_role_t::sample:
        return resource_placement_kind_t::sample_note;
    case resource_role_t::bgm:
        return resource_placement_kind_t::bgm_event;
    case resource_role_t::bga:
        return resource_placement_kind_t::bga_background;
    case resource_role_t::music:
    case resource_role_t::preview:
        return std::nullopt;
    }
    return std::nullopt;
}

auto apply_compose_resource_placement(compose_action_context_t& context,
                                      wormedit::resource_placement_kind_t kind)
    -> wormedit::document_command_result_t {
    using namespace wormedit;
    const auto resource = context.action.compose_resource_id;
    if (context.state.virtual_bms_cursor_row)
        return apply_virtual_bms_resource_placement(
            context.environment.document.history,
            {.chart = context.state.chart,
             .resource = resource,
             .canvas_row = *context.state.virtual_bms_cursor_row,
             .lane = context.state.active_lane,
             .kind = kind,
             .semantic_layers = context.state.armed_note_layers});
    return apply_resource_placement(context.environment.document.history,
                                    {.chart = context.state.chart,
                                     .resource = resource,
                                     .position = context.state.cursor,
                                     .lane = context.state.active_lane,
                                     .kind = kind,
                                     .semantic_layers = context.state.armed_note_layers});
}

compose_action_outcome_t
finish_compose_resource_placement(compose_action_context_t& context,
                                  wormedit::resource_placement_kind_t kind) {
    using namespace wormedit;
    auto& state = context.state;
    if (state.virtual_bms_cursor_row) {
        const auto* chart = selected_chart(context.environment.document.history,
                                           context.environment.document.workspace);
        std::optional<local_position_t> position;
        if (chart) {
            const auto& measures = context.environment.document.history.indices(chart->id)->measures;
            if (measures)
                position = measures->position_at(*state.virtual_bms_cursor_row);
        }
        place_cursor_at(state, position.value_or(state.cursor));
    }
    const auto role = kind == resource_placement_kind_t::sample_note ? resource_role_t::sample
                      : kind == resource_placement_kind_t::bgm_event ? resource_role_t::bgm
                                                                     : resource_role_t::bga;
    const auto next =
        next_eligible_placement_resource(context.environment.document.history.document,
                                         context.action.compose_resource_id, role);
    worm_ui_frontend_resource_placed(context.action.compose_resource_id.value,
                                     next.value_or(resource_id_t{}).value);

    auto outcome = compose_action_outcome_t::from("Resource placed at the edit cursor.",
                                                  notification_severity_t::success,
                                                  notification_source_t::edit, true);
    outcome.rebind_failure = "Resource placed, but workspace rebinding failed.";
    return outcome;
}

struct place_compose_resource_action_t {
    std::optional<resource_placement_kind_t> kind{};
    auto operator==(const place_compose_resource_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t> {
        if (auto blocked = context.require_no_gesture(
                "Finish or cancel the active Compose gesture before placing a resource.")) {
            blocked->source = notification_source_t::edit;
            return std::move(*blocked);
        }
        kind = compose_placement_kind(context.action);
        if (!kind)
            return compose_action_outcome_t::blocked(
                "The selected resource role cannot be placed in the chart.",
                notification_source_t::edit);
        return std::nullopt;
    }
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t {
        auto applied = apply_compose_resource_placement(context, *kind);
        if (!applied.diagnostics.empty())
            return compose_action_outcome_t::from(std::move(applied.diagnostics),
                                                  notification_source_t::edit,
                                                  compose_feedback_delivery_t::message);
        if (!applied.applied)
            return compose_action_outcome_t::from("Resource placement made no document change.",
                                                  notification_severity_t::info,
                                                  notification_source_t::edit);
        return finish_compose_resource_placement(context, *kind);
    }
};
static_assert(compose_action<place_compose_resource_action_t, compose_action_context_t>);

struct select_compose_resource_uses_action_t {
    resource_id_t resource{};
    const chart_document_t* chart{};
    auto operator==(const select_compose_resource_uses_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<select_compose_resource_uses_action_t, compose_action_context_t>);

auto select_compose_resource_uses_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    resource = context.action.compose_resource_id;
    if (!resource || std::ranges::find(context.environment.document.history.document.resources,
                                       resource, &resource_entry_t::id) ==
                         context.environment.document.history.document.resources.end())
        return compose_action_outcome_t::blocked("The selected resource is no longer available.",
                                                 notification_source_t::edit);
    chart = context.environment.document.history.document.find_chart(context.state.chart);
    if (chart == nullptr)
        return compose_action_outcome_t::blocked("The selected chart is no longer available.",
                                                 notification_source_t::edit);
    return std::nullopt;
}

auto select_compose_resource_uses_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto uses = find_compose_resource_uses(*chart, resource);
    const auto count = uses.size();
    auto result = dispatch_editor_action(
        context.environment.document.history, context.state,
        select_editor_objects_action_t{.objects = std::move(uses),
                                       .mode = editor_area_selection_mode_t::replace});
    return compose_action_outcome_t::from(
        std::move(result), count == 0
                               ? "No uses of this resource exist in the active chart."
                               : std::to_string(count) + (count == 1 ? " resource use selected."
                                                                     : " resource uses selected."));
}

struct assign_compose_resource_action_t {
    auto operator==(const assign_compose_resource_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<assign_compose_resource_action_t, compose_action_context_t>);

auto assign_compose_resource_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before assigning a resource.")) {
        blocked->source = notification_source_t::edit;
        return std::move(*blocked);
    }
    return std::nullopt;
}

auto assign_compose_resource_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr std::string_view rebind_failure =
        "Resource assigned, but workspace rebinding failed.";
    auto applied =
        apply_resource_assignment(context.environment.document.history,
                                  {.chart = context.state.chart,
                                   .resource = context.action.compose_resource_id,
                                   .objects = context.state.selected_objects});
    if (!applied.diagnostics.empty())
        return compose_action_outcome_t::from(
            std::move(applied),
            compose_action_description_t{.rebind_failure = rebind_failure,
                                         .source = notification_source_t::edit});
    if (!applied.applied)
        return compose_action_outcome_t::from("Selected objects already use that resource.",
                                              notification_severity_t::info,
                                              notification_source_t::edit);

    auto outcome = compose_action_outcome_t::from("Resource assigned to selected objects.",
                                                  notification_severity_t::success,
                                                  notification_source_t::edit, applied.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(applied.diagnostics);
    return outcome;
}

struct create_compose_resource_action_t {
    resource_role_t role{};
    const chart_document_t* chart{};
    auto operator==(const create_compose_resource_action_t&) const -> bool = default;
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<create_compose_resource_action_t, compose_action_context_t>);

auto create_compose_resource_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    using namespace wormedit;
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before creating a resource.")) {
        blocked->source = notification_source_t::import_;
        return std::move(*blocked);
    }
    role = import_resource_role(context.action).value_or(resource_role_t::music);
    chart = context.environment.document.history.document.find_chart(context.state.chart);
    return std::nullopt;
}

auto create_compose_resource_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    constexpr std::string_view rebind_failure = "Resource created, but workspace rebinding failed.";
    auto applied = apply_resource_creation(
        context.environment.document.history,
        {.asset = context.action.compose_resource_asset_id,
         .role = role,
         .allocate_bms_identifier =
             chart != nullptr && chart->source_family == format_family_t::bms});
    if (!applied.diagnostics.empty())
        return compose_action_outcome_t::from(
            std::move(applied),
            compose_action_description_t{.rebind_failure = rebind_failure,
                                         .source = notification_source_t::import_});

    auto outcome = compose_action_outcome_t::from(
        role == resource_role_t::bga ? "BGA resource created from the selected image."
                                     : "Resource created from the selected audio asset.",
        notification_severity_t::success, notification_source_t::import_, applied.applied);
    outcome.rebind_failure = rebind_failure;
    outcome.diagnostics = std::move(applied.diagnostics);
    return outcome;
}

struct relink_compose_sound_action_t {
    const audio_asset_t* asset{};
    audio_fallback_t fallback{};
    const std::filesystem::path* chosen{};
    auto validate(compose_action_context_t& context) -> std::optional<compose_action_outcome_t>;
    auto perform(compose_action_context_t& context) const -> compose_action_outcome_t;
};
static_assert(compose_action<relink_compose_sound_action_t, compose_action_context_t>);

auto relink_compose_sound_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_resources())
        return std::move(*blocked);
    using namespace wormedit;
    const auto& action = context.action;
    if (action.compose_input_document_revision !=
        context.environment.document.history.document.revision)
        return compose_action_outcome_t::blocked("The sound list changed; choose the file again.",
                                                 notification_source_t::edit);
    const auto& document = context.environment.document.history.document;
    const auto resource = std::ranges::find(
        document.resources, action.compose_resource_id, &resource_entry_t::id);
    asset =
        resource == document.resources.end() ? nullptr : document.find_audio_asset(resource->asset);
    if (!asset)
        return compose_action_outcome_t::blocked("That sound is no longer in the project.",
                                                 notification_source_t::edit);
    fallback =
        resolve_audio_fallback(asset->source_path, context.environment.resources->asset_base);
    if (action.compose_fallback_candidate >= fallback.candidates.size())
        return compose_action_outcome_t::blocked("That file is no longer available.",
                                                 notification_source_t::edit);
    chosen = &fallback.candidates[action.compose_fallback_candidate];
    return std::nullopt;
}

auto relink_compose_sound_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto relinked = apply_resource_asset_relink(context.environment.document.history,
                                                {.asset = asset->id, .source_path = *chosen});
    if (relinked.applied) {
        auto outcome = compose_action_outcome_t::from(
            "Sound relinked to " + chosen->generic_string() + ".", notification_severity_t::success,
            notification_source_t::edit, true);
        outcome.rebind_failure = "Sound relinked, but workspace rebinding failed.";
        return outcome;
    }
    return compose_action_outcome_t::blocked(relinked.diagnostics.empty()
                                                 ? "The sound could not be relinked."
                                                 : relinked.diagnostics.front().message,
                                             notification_source_t::edit);
}

namespace {
constexpr auto resource_route_entries = std::array{
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SELECT_RESOURCE_USES,
                           .operation =
                               run_routed_compose_action<select_compose_resource_uses_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_ASSIGN_RESOURCE_TO_SELECTION,
                           .operation =
                               run_routed_compose_action<assign_compose_resource_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_CREATE_RESOURCE,
                           .operation =
                               run_routed_compose_action<create_compose_resource_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET,
                           .operation =
                               run_routed_compose_action<import_compose_resource_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK,
                           .operation = run_routed_compose_action<relink_compose_sound_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT,
                           .operation = run_routed_compose_action<copy_compose_resource_action_t>,
                           .requires_resources = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT,
                           .operation = run_routed_compose_action<copy_compose_resource_action_t>,
                           .requires_resources = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP,
                           .operation =
                               run_routed_compose_action<export_compose_output_group_action_t>,
                           .requires_resources = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_EXPORT_ALL_OUTPUT_GROUPS,
                           .operation =
                               run_routed_compose_action<export_compose_output_group_action_t>,
                           .requires_resources = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_CONFIRM_OUTPUT_EXPORT,
                           .operation =
                               run_routed_compose_action<confirm_output_export_action_t, false>,
                           .requires_resources = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_CANCEL_OUTPUT_EXPORT,
                           .operation =
                               run_routed_compose_action<confirm_output_export_action_t, true>,
                           .requires_resources = true},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE,
                           .operation = run_routed_compose_action<place_compose_resource_action_t>},
};
} // namespace
static_assert(is_valid_route_module(resource_route_entries));
constinit const std::span<const compose_action_route_t> compose_resource_routes{
    resource_route_entries};
} // namespace wormedit::sys::compose_action_detail

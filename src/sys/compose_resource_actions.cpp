#include "sys/compose_actions_internal.h"
namespace wormedit::sys::compose_action_detail {
auto stage_output_music(const wormedit::song_document_t& snapshot,
                        const wormedit::output_group_t& group,
                        const std::filesystem::path& asset_base, std::stop_token stop)
    -> std::optional<std::string> {
    using namespace wormedit;
    if (group.family != format_family_t::osu_mania && group.family != format_family_t::stepmania)
        return std::nullopt;
    const resource_entry_t* music{};
    for (const auto& resource : snapshot.resources) {
        if (std::ranges::find(resource.roles, resource_role_t::music) == resource.roles.end())
            continue;
        if (music)
            return "The output has more than one music resource.";
        music = &resource;
    }
    if (!music)
        return std::nullopt;
    const auto asset = std::ranges::find(snapshot.audio_assets, music->asset, &audio_asset_t::id);
    if (asset == snapshot.audio_assets.end())
        return "The music resource has no audio asset.";
    const auto copied =
        copy_resource_file_into_export({.project_directory = asset_base,
                                        .source_path = asset->source_path,
                                        .relative_output_path = group.relative_output_path,
                                        .asset = asset->id},
                                       stop);
    if (!copied)
        return "Could not stage music for export: " + copied.message;
    return std::nullopt;
}

void report_output_conversion_losses(const wormedit::conversion_result_t& converted,
                                     output_group_export_task_result_t& completed) {
    if (!completed.published)
        return;
    std::size_t count{};
    const wormedit::conversion_diagnostic_t* first{};
    for (const auto& diagnostic : converted.diagnostics) {
        if (!diagnostic.loss)
            continue;
        ++count;
        if (!first)
            first = &diagnostic;
        if (completed.detail.size() >= wormedit::notification_detail_max_bytes)
            continue;
        std::string detail = diagnostic.chart
                                 ? "Chart " + std::to_string(diagnostic.chart->value) + ": "
                                 : std::string{};
        if (!diagnostic.path.empty())
            detail += diagnostic.path + ": ";
        detail += diagnostic.message + "\n";
        completed.detail += detail;
        wormedit::truncate_notification_text(completed.detail,
                                             wormedit::notification_detail_max_bytes);
    }
    if (!first)
        return;
    completed.message = "Output group published with " + std::to_string(count) +
                        (count == 1 ? " conversion loss: " : " conversion losses: ") +
                        first->message;
    wormedit::truncate_notification_text(completed.message,
                                         wormedit::notification_message_max_bytes);
    completed.severity = wormedit::notification_severity_t::warning;
}

auto sliced_bms_destination_error(const wormedit::song_document_t& snapshot,
                                  const wormedit::output_group_t& group,
                                  const wormedit::conversion_request_t& request,
                                  const wormedit::conversion_result_t& converted)
    -> std::optional<std::string> {
    if (group.family != wormedit::format_family_t::bms ||
        !wormedit::conversion_needs_slice_spans(snapshot, request) ||
        !group.relative_output_path.has_extension())
        return std::nullopt;
    const auto requested = group.relative_output_path.extension();
    for (const auto& output : converted.outputs) {
        const auto inferred = std::filesystem::path(output.suggested_filename).extension();
        const bool incompatible =
            requested == ".pms" ? inferred != ".pms"
                                : inferred == ".pms" || (requested == ".bme" && inferred != ".bme");
        if (incompatible)
            return "BMSON lane layout requires " + inferred.string() + " output; selected " +
                   requested.string() + " filename is incompatible.";
    }
    return std::nullopt;
}

void complete_output_group_conversion(const wormedit::song_document_t& snapshot,
                                      const wormedit::output_group_t& group,
                                      wormedit::conversion_request_t& request,
                                      const std::filesystem::path& asset_base, std::stop_token stop,
                                      output_group_export_task_result_t& completed) {
    using namespace wormedit;
    const auto converted = convert_document(snapshot, request);
    if (!converted) {
        const auto error = std::ranges::find_if(converted.diagnostics, [](const auto& diagnostic) {
            return diagnostic.severity == diagnostic_severity_t::error;
        });
        completed.message =
            error == converted.diagnostics.end() ? "Output conversion failed." : error->message;
        completed.severity = notification_severity_t::error;
        return;
    }
    if (stop.stop_requested()) {
        completed.message = "Export cancelled before publication.";
        completed.severity = notification_severity_t::info;
        return;
    }
    if (auto error = sliced_bms_destination_error(snapshot, group, request, converted)) {
        completed.message = *error;
        completed.severity = notification_severity_t::error;
        return;
    }
    conversion_publication_options_t publication;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    if (conversion_needs_slice_spans(snapshot, request)) {
        publication.stage_generated_files = [&](const auto& staging) {
            return render_conversion_slice_plans(snapshot, converted, {asset_base, staging, stop});
        };
    }
#endif
    const auto published = publish_conversion_group_atomic(asset_base, group, request, converted,
                                                           std::move(publication));
    completed.published = published.published;
    completed.message = published.published ? "Output group published from the selected revision."
                        : published.errors.empty() ? "Output group publication failed."
                                                   : published.errors.front();
    completed.severity = !published.published          ? notification_severity_t::error
                         : !published.warnings.empty() ? notification_severity_t::warning
                                                       : notification_severity_t::success;
    if (published.published && !published.warnings.empty())
        completed.message += " " + published.warnings.front();
    if (published.published) {
        for (const auto& warning : published.warnings)
            completed.detail += warning + "\n";
        report_output_conversion_losses(converted, completed);
    }
}

void complete_sliced_output_group_conversion(const wormedit::song_document_t& snapshot,
                                             const wormedit::output_group_t& group,
                                             wormedit::conversion_request_t& request,
                                             const std::filesystem::path& asset_base,
                                             std::stop_token stop,
                                             output_group_export_task_result_t& completed) {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    if (auto error =
            wormedit::prepare_conversion_slice_sources(snapshot, request, asset_base, stop)) {
        completed.message = *error;
        completed.severity =
            stop.stop_requested() ? notification_severity_t::info : notification_severity_t::error;
        return;
    }
    complete_output_group_conversion(snapshot, group, request, asset_base, stop, completed);
#else
    const auto converted = convert_document(snapshot, request);
    if (!converted || (converted.slice_plans.empty() && converted.asset_copies.empty() &&
                       converted.silent_files.empty())) {
        complete_output_group_conversion(snapshot, group, request, asset_base, stop, completed);
        return;
    }
    completed.message = "BMSON sliced-audio conversion requires the audio decoder build.";
    completed.severity = notification_severity_t::error;
#endif
}

task_result_t run_output_group_export_task(const wormedit::song_document_t& snapshot,
                                           const wormedit::output_group_t& group,
                                           wormedit::conversion_request_t& request,
                                           const std::filesystem::path& asset_base,
                                           std::stop_token stop) {
    using namespace wormedit;
    auto completed = std::make_shared<output_group_export_task_result_t>();
    if (stop.stop_requested()) {
        completed->message = "Export cancelled before conversion.";
        completed->severity = notification_severity_t::info;
    } else if (conversion_needs_slice_spans(snapshot, request)) {
        complete_sliced_output_group_conversion(snapshot, group, request, asset_base, stop,
                                                *completed);
    } else if (auto staging_error = stage_output_music(snapshot, group, asset_base, stop)) {
        completed->message = *staging_error;
        completed->severity =
            stop.stop_requested() ? notification_severity_t::info : notification_severity_t::error;
    } else {
        const auto discovered = discover_staged_export_assets(snapshot, group, asset_base, stop);
        if (!discovered) {
            completed->message = discovered.errors.front();
            completed->severity = stop.stop_requested() ? notification_severity_t::info
                                                        : notification_severity_t::error;
        } else {
            request.staged_assets = discovered.mappings;
            complete_output_group_conversion(snapshot, group, request, asset_base, stop,
                                             *completed);
        }
    }
    return task_result_t::success(make_task_value(
        std::shared_ptr<const output_group_export_task_result_t>{std::move(completed)}));
}

auto import_resource_role(const WormUiWorkspaceAction& action)
    -> std::optional<wormedit::resource_role_t> {
    using namespace wormedit;
    switch (action.compose_resource_role) {
    case WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE:
        return resource_role_t::sample;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_BGM:
        return resource_role_t::bgm;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_BGA:
        return resource_role_t::bga;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_MUSIC:
        return resource_role_t::music;
    default:
        return std::nullopt;
    }
}

auto inspect_imported_resource(const std::filesystem::path& path, wormedit::resource_role_t role)
    -> std::variant<imported_resource_metadata_t, std::string> {
    imported_resource_metadata_t metadata;
    if (role == wormedit::resource_role_t::bga) {
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        const auto decoded = wormedit::visual::load_image_rgba(
            {.kind = wormedit::asset_kind_t::image, .path = path});
        if (!decoded)
            return decoded.message.empty() ? "The image could not be decoded." : decoded.message;
        metadata.image = std::pair{decoded.image->width, decoded.image->height};
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

std::optional<std::string> commit_primary_music(compose_action_context_t& context,
                                                const std::filesystem::path& path,
                                                const imported_resource_metadata_t& metadata) {
    using namespace wormedit;
    const auto chart = std::ranges::find(context.environment.document.history.document.charts,
                                         context.state.chart, &chart_document_t::id);
    auto name = path.filename().generic_string();
    if (name.size() > 255)
        name.clear();
    const auto applied = apply_primary_music_set(
        context.environment.document.history, context.action.compose_input_document_revision,
        {.source_path = path,
         .name = std::move(name),
         .allocate_bms_identifier =
             chart != context.environment.document.history.document.charts.end() &&
             chart->source_family == format_family_t::bms,
         .audio_stream = metadata.audio});
    if (!applied.diagnostics.empty())
        return publish_compose_diagnostic(context, applied.diagnostics.front(),
                                          notification_source_t::import_);
    if (!applied.applied)
        return compose_action_info(context, "That file is already the song audio.",
                                   notification_source_t::import_);
    const auto rebound =
        rebind_workspace(context.environment.document.workspace, context.environment.document.history);
    if (!rebound)
        return compose_mutation_rebind_failure(
            context, rebound, "Song audio set to " + path.filename().string() + ".",
            "Song audio set, but workspace rebinding failed.", notification_source_t::import_);
    const auto music =
        std::ranges::find_if(context.environment.document.history.document.resources, [](const auto& r) {
            return std::ranges::find(r.roles, resource_role_t::music) != r.roles.end();
        });
    if (music != context.environment.document.history.document.resources.end())
        (void)apply_workspace_action(
            context.environment.document.workspace, context.environment.document.history.document,
            context.environment.document.history.document.revision,
            workspace_select_asset_action_t{.asset = music->asset,
                                            .slot = wormedit::workspace_asset_slot_t::audio});
    return compose_action_success(context, "Song audio set to " + path.filename().string() + ".",
                                  notification_source_t::import_);
}

std::optional<std::string> commit_resource_import(compose_action_context_t& context,
                                                  wormedit::resource_role_t role,
                                                  const std::filesystem::path& path,
                                                  imported_resource_metadata_t metadata) {
    using namespace wormedit;
    if (role == resource_role_t::music)
        return commit_primary_music(context, path, metadata);
    const auto chart = std::ranges::find(context.environment.document.history.document.charts,
                                         context.state.chart, &chart_document_t::id);
    auto name = path.filename().generic_string();
    if (name.size() > 255)
        name.clear();
    const auto applied = apply_resource_asset_import(
        context.environment.document.history, context.action.compose_input_document_revision,
        {.source_path = path,
         .role = role,
         .name = std::move(name),
         .allocate_bms_identifier =
             chart != context.environment.document.history.document.charts.end() &&
             chart->source_family == format_family_t::bms,
         .audio_stream = metadata.audio,
         .image_geometry = metadata.image});
    if (!applied.diagnostics.empty())
        return publish_compose_diagnostic(context, applied.diagnostics.front(),
                                          notification_source_t::import_);
    if (!applied.applied)
        return compose_action_info(context, "Resource import made no document change.",
                                   notification_source_t::import_);
    const auto imported = context.environment.document.history.document.resources.back().asset;
    const auto rebound =
        rebind_workspace(context.environment.document.workspace, context.environment.document.history);
    if (!rebound)
        return compose_mutation_rebind_failure(
            context, rebound, "External file linked as a resource.",
            "Resource imported, but workspace rebinding failed.", notification_source_t::import_);
    if (role == resource_role_t::bga)
        (void)apply_workspace_action(
            context.environment.document.workspace, context.environment.document.history.document,
            context.environment.document.history.document.revision,
            workspace_select_asset_action_t{.asset = imported,
                                            .slot = wormedit::workspace_asset_slot_t::visual});
    else
        (void)apply_workspace_action(
            context.environment.document.workspace, context.environment.document.history.document,
            context.environment.document.history.document.revision,
            workspace_select_asset_action_t{.asset = imported,
                                            .slot = wormedit::workspace_asset_slot_t::audio});
    return compose_action_success(context, "External file linked as a resource.",
                                  notification_source_t::import_);
}

std::optional<std::string> import_resource_asset(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(
            context, "Finish or cancel the active Compose gesture before importing a resource.",
            notification_source_t::import_);
    const auto role = import_resource_role(context.action);
    if (!role)
        return compose_action_warning(context, "Resource import used an unknown catalog role.",
                                      notification_source_t::import_);
    const std::filesystem::path path =
        std::filesystem::path{context.action.compose_import_path}.lexically_normal();
    std::error_code error;
    if (!path.is_absolute() || !std::filesystem::is_regular_file(path, error) || error)
        return compose_action_warning(context,
                                      "Resource import requires an existing absolute file path.",
                                      notification_source_t::import_);
    auto metadata = inspect_imported_resource(path, *role);
    if (const auto* failure = std::get_if<std::string>(&metadata))
        return compose_action_warning(context, *failure, notification_source_t::import_);
    return commit_resource_import(context, *role, path,
                                  std::get<imported_resource_metadata_t>(std::move(metadata)));
}

std::optional<std::string>
dispatch_compose_import_resource_action(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET)
        return std::nullopt;
    return import_resource_asset(context);
}

auto prepare_resource_copy(compose_action_context_t& context)
    -> std::variant<resource_copy_plan_t, compose_feedback_t> {
    using namespace wormedit;
    auto& action = context.action;
    auto& history = context.environment.document.history;
    if (context.environment.resources.jobs.copy.busy() ||
        context.environment.resources.jobs.exporter.busy() || context.metadata_import_busy)
        return compose_feedback_t{
            "Wait for the current resource copy, import, or export to finish.",
            notification_severity_t::warning, notification_source_t::import_};
    if (context.environment.resources.asset_base.empty())
        return compose_feedback_t{
            "Open a saved native project before copying resource files into it.",
            notification_severity_t::warning, notification_source_t::import_};
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_feedback_t{
            "Finish or cancel the active Compose gesture before copying a resource.",
            notification_severity_t::warning, notification_source_t::import_};
    const asset_id_t id{action.compose_resource_asset_id};
    const auto audio = std::ranges::find(history.document.audio_assets, id, &audio_asset_t::id);
    const auto visual = std::ranges::find(history.document.visual_assets, id, &visual_asset_t::id);
    if (!id || (audio == history.document.audio_assets.end() &&
                visual == history.document.visual_assets.end()))
        return compose_feedback_t{"The selected resource asset is no longer available.",
                                  notification_severity_t::warning, notification_source_t::import_};
    const auto& source =
        audio != history.document.audio_assets.end() ? audio->source_path : visual->source_path;
    const bool exporting = action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT;
    if (!exporting && !source.is_absolute() && source.parent_path() == "assets" &&
        source.filename().string().starts_with(std::to_string(id.value) + "-"))
        return compose_feedback_t{"Resource source is already in the project assets directory.",
                                  notification_severity_t::info, notification_source_t::import_};
    std::filesystem::path output;
    if (exporting) {
        if (action.compose_output_group_index >= history.document.output_groups.size())
            return compose_feedback_t{"The selected output group is no longer available.",
                                      notification_severity_t::warning,
                                      notification_source_t::export_};
        const auto& group = history.document.output_groups[action.compose_output_group_index];
        if (std::ranges::find(group.charts, context.state.chart) == group.charts.end())
            return compose_feedback_t{"The selected output group does not include this chart.",
                                      notification_severity_t::warning,
                                      notification_source_t::export_};
        output = group.relative_output_path;
    }
    return resource_copy_plan_t{id, source, output, exporting};
}

std::optional<std::string> start_resource_copy(compose_action_context_t& context,
                                               resource_copy_plan_t plan) {
    using namespace wormedit;
    task_graph_t graph{1};
    const project_asset_copy_request_t request{.project_directory =
                                                   context.environment.resources.asset_base,
                                               .source_path = plan.source,
                                               .asset = plan.asset};
    const auto added = graph.add_task({}, [request, plan](std::span<const task_result_t>,
                                                          std::stop_token stop) {
        auto copied = std::make_shared<const project_asset_copy_result_t>(
            plan.export_copy
                ? copy_resource_file_into_export({.project_directory = request.project_directory,
                                                  .source_path = request.source_path,
                                                  .relative_output_path = plan.output,
                                                  .asset = request.asset,
                                                  .maximum_bytes = request.maximum_bytes},
                                                 stop)
                : copy_resource_file_into_project(request, stop));
        return task_result_t::success(make_task_value(std::move(copied)));
    });
    if (!added)
        return compose_action_error(
            context, added.error ? added.error->message : "Resource copy could not start.",
            plan.export_copy ? notification_source_t::export_ : notification_source_t::import_);
    auto submitted = context.environment.resources.jobs.scheduler.submit(std::move(graph));
    if (!submitted)
        return compose_action_error(
            context, submitted.error ? submitted.error->message : "Resource copy could not start.",
            plan.export_copy ? notification_source_t::export_ : notification_source_t::import_);
    auto pending = pending_project_resource_copy_t{
        .graph = std::move(submitted.graph),
        .task = *added.id,
        .asset = plan.asset,
        .song = context.environment.document.history.document.id,
        .revision = context.environment.document.history.document.revision,
        .source_path = plan.source,
        .output_path = plan.output,
        .export_copy = plan.export_copy};
    if (!context.environment.resources.jobs.copy.start(std::move(pending))) {
        pending.graph.cancel();
        return compose_action_warning(
            context, "Wait for the current resource copy, import, or export to finish.",
            plan.export_copy ? notification_source_t::export_ : notification_source_t::import_);
    }
    return plan.export_copy ? "Export copy started in the background."
                            : "Resource copy started in the background.";
}

std::optional<std::string>
dispatch_compose_copy_resource_action(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT &&
        context.action.compose_action != WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT)
        return std::nullopt;
    auto plan = prepare_resource_copy(context);
    if (const auto* error = std::get_if<compose_feedback_t>(&plan))
        return publish_compose_feedback(context, error->message, error->severity, error->source);
    return start_resource_copy(context, std::get<resource_copy_plan_t>(std::move(plan)));
}

auto prepare_output_export(compose_action_context_t& context)
    -> std::variant<output_export_plan_t, compose_feedback_t> {
    using namespace wormedit;
    const auto& doc = context.environment.document.history.document;
    if (context.environment.resources.asset_base.empty())
        return compose_feedback_t{"Open a saved native project before exporting an output group.",
                                  notification_severity_t::warning, notification_source_t::export_};
    if (context.environment.resources.jobs.copy.busy() ||
        context.environment.resources.jobs.exporter.busy() || context.metadata_import_busy)
        return compose_feedback_t{
            "Wait for the current resource copy, import, or export to finish.",
            notification_severity_t::warning, notification_source_t::export_};
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_feedback_t{"Finish or cancel the active Compose gesture before exporting.",
                                  notification_severity_t::warning, notification_source_t::export_};
    if (context.action.compose_output_group_index >= doc.output_groups.size())
        return compose_feedback_t{"The selected output group is no longer available.",
                                  notification_severity_t::warning, notification_source_t::export_};
    auto group = doc.output_groups[context.action.compose_output_group_index];
    if (group.charts.empty() ||
        std::ranges::find(group.charts, context.state.chart) == group.charts.end())
        return compose_feedback_t{"The selected output group has no matching charts.",
                                  notification_severity_t::warning, notification_source_t::export_};
    conversion_request_t request;
    request.target = group.family;
    if (group.family != format_family_t::native_project)
        request.charts = group.charts;
    request.overwrite = context.action.compose_export_overwrite;
    request.loss_policy = loss_policy_t::warn;
    if (group.family == format_family_t::stepmania) {
        request.stepmania = stepmania_export_policy_t{
            .file_profile = group.relative_output_path.extension() == ".sm" ||
                                    group.relative_output_path == "sm"
                                ? stepmania_profile_t::sm
                                : stepmania_profile_t::ssc};
    } else if (group.family == format_family_t::bms) {
        const auto extension = group.relative_output_path.extension().string();
        if ((extension == ".bme" || extension == ".bml" || extension == ".pms") &&
            !conversion_needs_slice_spans(doc, request))
            request.target_profile = extension.substr(1);
    }
    return output_export_plan_t{std::move(group), std::move(request)};
}

std::optional<std::string> start_output_export(compose_action_context_t& context,
                                               output_export_plan_t plan) {
    using namespace wormedit;
    auto snapshot = context.environment.document.history.document;
    task_graph_t graph{1};
    const auto added = graph.add_task(
        {}, [snapshot = std::move(snapshot), group = std::move(plan.group),
             request = std::move(plan.request), base = context.environment.resources.asset_base](
                std::span<const task_result_t>, std::stop_token stop) mutable {
            return run_output_group_export_task(snapshot, group, request, base, stop);
        });
    if (!added)
        return compose_action_error(context,
                                    added.error ? added.error->message : "Export could not start.",
                                    notification_source_t::export_);
    auto submitted = context.environment.resources.jobs.exporter.scheduler.submit(std::move(graph));
    if (!submitted)
        return compose_action_error(
            context, submitted.error ? submitted.error->message : "Export could not start.",
            notification_source_t::export_);
    auto pending = pending_output_group_export_t{
        .graph = std::move(submitted.graph),
        .task = *added.id,
        .song = context.environment.document.history.document.id,
        .revision = context.environment.document.history.document.revision};
    if (!context.environment.resources.jobs.exporter.start(std::move(pending))) {
        pending.graph.cancel();
        return compose_action_warning(
            context, "Wait for the current resource copy, import, or export to finish.",
            notification_source_t::export_);
    }
    return "Output group conversion started in the background.";
}

std::optional<std::string>
dispatch_compose_export_output_group_action(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP)
        return std::nullopt;
    auto plan = prepare_output_export(context);
    if (const auto* error = std::get_if<compose_feedback_t>(&plan))
        return publish_compose_feedback(context, error->message, error->severity, error->source);
    return start_output_export(context, std::get<output_export_plan_t>(std::move(plan)));
}

auto compose_placement_kind(const WormUiWorkspaceAction& action)
    -> std::optional<wormedit::resource_placement_kind_t> {
    using namespace wormedit;
    switch (action.compose_resource_role) {
    case WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE:
        return resource_placement_kind_t::sample_note;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_BGM:
        return resource_placement_kind_t::bgm_event;
    case WORM_UI_COMPOSE_RESOURCE_ROLE_BGA:
        return resource_placement_kind_t::bga_background;
    default:
        return std::nullopt;
    }
}

auto apply_compose_resource_placement(compose_action_context_t& context,
                                      wormedit::resource_placement_kind_t kind)
    -> wormedit::document_command_result_t {
    using namespace wormedit;
    const auto resource = resource_id_t{context.action.compose_resource_id};
    if (context.state.virtual_bms_cursor_row)
        return apply_virtual_bms_resource_placement(
            context.environment.document.history, context.action.compose_input_document_revision,
            {.chart = context.state.chart,
             .resource = resource,
             .canvas_row = *context.state.virtual_bms_cursor_row,
             .lane = context.state.active_lane,
             .kind = kind,
             .semantic_layers = context.state.armed_note_layers});
    return apply_resource_placement(context.environment.document.history,
                                    context.action.compose_input_document_revision,
                                    {.chart = context.state.chart,
                                     .resource = resource,
                                     .position = context.state.cursor,
                                     .lane = context.state.active_lane,
                                     .kind = kind,
                                     .semantic_layers = context.state.armed_note_layers});
}

std::optional<std::string>
finish_compose_resource_placement(compose_action_context_t& context,
                                  wormedit::resource_placement_kind_t kind) {
    using namespace wormedit;
    auto& state = context.state;
    if (state.virtual_bms_cursor_row) {
        const auto* chart =
            selected_chart(context.environment.document.history, context.environment.document.workspace);
        std::optional<local_position_t> position;
        if (chart) {
            const auto built = build_measure_index(*chart);
            if (const auto* index = std::get_if<measure_index_t>(&built))
                position = timing_position_at_row(*index, *state.virtual_bms_cursor_row);
        }
        place_cursor_at(state, position.value_or(state.cursor));
    }
    const auto role = kind == resource_placement_kind_t::sample_note ? resource_role_t::sample
                      : kind == resource_placement_kind_t::bgm_event ? resource_role_t::bgm
                                                                     : resource_role_t::bga;
    const auto next =
        next_eligible_placement_resource(context.environment.document.history.document,
                                         resource_id_t{context.action.compose_resource_id}, role);
    worm_ui_frontend_resource_placed(context.action.compose_resource_id,
                                     next.value_or(resource_id_t{}).value);
    const auto rebound =
        rebind_workspace(context.environment.document.workspace, context.environment.document.history);
    if (!rebound)
        return compose_mutation_rebind_failure(
            context, rebound, "Resource placed at the edit cursor.",
            "Resource placed, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, "Resource placed at the edit cursor.");
}

std::optional<std::string>
dispatch_compose_place_resource_action(compose_action_context_t& context) {
    if (context.action.compose_action != WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE)
        return std::nullopt;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(
            context, "Finish or cancel the active Compose gesture before placing a resource.");
    const auto kind = compose_placement_kind(context.action);
    if (!kind)
        return compose_action_warning(context,
                                      "The selected resource role cannot be placed in the chart.");
    const auto applied = apply_compose_resource_placement(context, *kind);
    if (!applied.diagnostics.empty())
        return publish_compose_diagnostic(context, applied.diagnostics.front());
    if (!applied.applied)
        return compose_action_info(context, "Resource placement made no document change.",
                                   notification_source_t::edit);
    return finish_compose_resource_placement(context, *kind);
}

std::optional<std::string> select_resource_uses(compose_action_context_t& context) {
    using namespace wormedit;
    const resource_id_t resource{context.action.compose_resource_id};
    if (!resource || std::ranges::find(context.environment.document.history.document.resources, resource,
                                       &resource_entry_t::id) ==
                         context.environment.document.history.document.resources.end())
        return compose_action_warning(context, "The selected resource is no longer available.");
    const auto chart = std::ranges::find(context.environment.document.history.document.charts,
                                         context.state.chart, &chart_document_t::id);
    if (chart == context.environment.document.history.document.charts.end())
        return compose_action_warning(context, "The selected chart is no longer available.");
    auto uses = find_compose_resource_uses(*chart, resource);
    const auto count = uses.size();
    const auto result = dispatch_editor_action(
        context.environment.document.history, context.state,
        select_editor_objects_action_t{.objects = std::move(uses),
                                       .mode = editor_area_selection_mode_t::replace});
    return compose_interaction_feedback(
        context, result,
        count == 0 ? "No uses of this resource exist in the active chart."
                   : std::to_string(count) +
                         (count == 1 ? " resource use selected." : " resource uses selected."));
}

std::optional<std::string> assign_resource_to_selection(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(
            context, "Finish or cancel the active Compose gesture before assigning a resource.");
    const auto applied = apply_resource_assignment(
        context.environment.document.history, context.action.compose_input_document_revision,
        {.chart = context.state.chart,
         .resource = resource_id_t{context.action.compose_resource_id},
         .objects = context.state.selected_objects});
    if (!applied.diagnostics.empty())
        return publish_compose_diagnostic(context, applied.diagnostics.front());
    if (!applied.applied)
        return compose_action_info(context, "Selected objects already use that resource.",
                                   notification_source_t::edit);
    const auto rebound =
        rebind_workspace(context.environment.document.workspace, context.environment.document.history);
    if (!rebound)
        return compose_mutation_rebind_failure(
            context, rebound, "Resource assigned to selected objects.",
            "Resource assigned, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, "Resource assigned to selected objects.");
}

std::optional<std::string> create_resource_from_asset(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(
            context, "Finish or cancel the active Compose gesture before creating a resource.",
            notification_source_t::import_);
    const auto role = import_resource_role(context.action).value_or(resource_role_t::music);
    const auto chart = std::ranges::find(context.environment.document.history.document.charts,
                                         context.state.chart, &chart_document_t::id);
    const auto applied = apply_resource_creation(
        context.environment.document.history, context.action.compose_input_document_revision,
        {.asset = asset_id_t{context.action.compose_resource_asset_id},
         .role = role,
         .allocate_bms_identifier =
             chart != context.environment.document.history.document.charts.end() &&
             chart->source_family == format_family_t::bms});
    if (!applied.diagnostics.empty())
        return publish_compose_diagnostic(context, applied.diagnostics.front(),
                                          notification_source_t::import_);
    const auto rebound =
        rebind_workspace(context.environment.document.workspace, context.environment.document.history);
    if (!rebound)
        return compose_mutation_rebind_failure(
            context, rebound,
            role == resource_role_t::bga ? "BGA resource created from the selected image."
                                         : "Resource created from the selected audio asset.",
            "Resource created, but workspace rebinding failed.", notification_source_t::import_);
    return compose_action_success(context,
                                  role == resource_role_t::bga
                                      ? "BGA resource created from the selected image."
                                      : "Resource created from the selected audio asset.",
                                  notification_source_t::import_);
}

std::string relink_sound_fallback(compose_action_context_t& context) {
    using namespace wormedit;
    const auto& action = context.action;
    if (action.compose_input_document_revision != context.environment.document.history.document.revision)
        return compose_action_warning(context, "The sound list changed; choose the file again.");
    const auto& document = context.environment.document.history.document;
    const auto resource = std::ranges::find(
        document.resources, resource_id_t{action.compose_resource_id}, &resource_entry_t::id);
    const auto asset =
        resource == document.resources.end()
            ? document.audio_assets.end()
            : std::ranges::find(document.audio_assets, resource->asset, &audio_asset_t::id);
    if (asset == document.audio_assets.end())
        return compose_action_warning(context, "That sound is no longer in the project.");
    const auto fallback =
        resolve_audio_fallback(asset->source_path, context.environment.resources.asset_base);
    if (action.compose_fallback_candidate >= fallback.candidates.size())
        return compose_action_warning(context, "That file is no longer available.");
    const auto& chosen = fallback.candidates[action.compose_fallback_candidate];
    const auto relinked =
        apply_resource_asset_relink(context.environment.document.history, document.revision,
                                    {.asset = asset->id, .source_path = chosen});
    if (relinked.applied)
        return compose_action_success(context, "Sound relinked to " + chosen.generic_string() + ".",
                                      notification_source_t::edit);
    return compose_action_warning(context, relinked.diagnostics.empty()
                                               ? "The sound could not be relinked."
                                               : relinked.diagnostics.front().message);
}

std::optional<std::string> dispatch_compose_resource_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SELECT_RESOURCE_USES:
        return select_resource_uses(context);
    case WORM_UI_COMPOSE_ACTION_ASSIGN_RESOURCE_TO_SELECTION:
        return assign_resource_to_selection(context);
    case WORM_UI_COMPOSE_ACTION_CREATE_RESOURCE:
        return create_resource_from_asset(context);
    case WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET:
        return dispatch_compose_import_resource_action(context);
    case WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT:
    case WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT:
        return dispatch_compose_copy_resource_action(context);
    case WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP:
        return dispatch_compose_export_output_group_action(context);
    case WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE:
        return dispatch_compose_place_resource_action(context);
    case WORM_UI_COMPOSE_ACTION_IMPORT_BGA_FILES:
    case WORM_UI_COMPOSE_ACTION_REMOVE_UNUSED_BGA:
        if (editor_interaction_has_pending_gesture(context.state))
            return compose_action_warning(
                context, "Finish or cancel the active Compose gesture before editing BGA.");
        return dispatch_compose_bga_action(context.action, context.environment.document.history,
                                           context.environment.document.workspace, context.state.chart,
                                           context.notifications);
    case WORM_UI_COMPOSE_ACTION_RELINK_SOUND_FALLBACK:
        return relink_sound_fallback(context);
    case WORM_UI_COMPOSE_ACTION_IMPORT_SOUND_FILES:
    case WORM_UI_COMPOSE_ACTION_ASSIGN_SOUND_SLOT:
    case WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS:
    case WORM_UI_COMPOSE_ACTION_GROUP_SOUNDS:
    case WORM_UI_COMPOSE_ACTION_UNGROUP_SOUNDS:
    case WORM_UI_COMPOSE_ACTION_AUDITION_SOUND:
    case WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS:
    case WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER:
    case WORM_UI_COMPOSE_ACTION_REMOVE_SOUND_LAYER:
    case WORM_UI_COMPOSE_ACTION_CLEAR_SOUND_LAYERS:
        if (editor_interaction_has_pending_gesture(context.state))
            return compose_action_warning(
                context, "Finish or cancel the active Compose gesture before editing sounds.");
        return dispatch_compose_sound_action(context.action, context.environment.document.history,
                                             context.environment.document.workspace, context.state.chart,
                                             context.environment.editor.commands,
                                             context.notifications);
    default:
        return std::nullopt;
    }
}
} // namespace wormedit::sys::compose_action_detail

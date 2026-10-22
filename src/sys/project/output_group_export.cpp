#include "sys/project/output_group_export.h"
#include <wormedit/core/catalogs.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/formats/conversion_publication.h>
#include <wormedit/formats/slice_spans.h>
#include <algorithm>
#include <utility>

namespace wormedit::sys {
namespace {
auto export_target(const output_group_export_plan_t& plan) -> output_group_export_target_t {
    return {.group = plan.group, .request = plan.request, .existing_files = plan.existing_files};
}

auto single_target_plan(output_group_export_target_t target) -> output_group_export_plan_t {
    return {.group = std::move(target.group),
            .request = std::move(target.request),
            .existing_files = target.existing_files};
}

auto plan_output_music(const output_group_export_request_t& input)
    -> result_t<std::vector<project_asset_copy_request_t>, std::string> {
    const auto& snapshot = input.document;
    const auto& group = input.plan.group;
    if (group.family != format_family_t::osu_mania && group.family != format_family_t::stepmania)
        return std::vector<project_asset_copy_request_t>{};
    const resource_entry_t* music{};
    for (const auto& resource : snapshot.resources) {
        if (std::ranges::find(resource.roles, resource_role_t::music) == resource.roles.end()) continue;
        if (music) return std::unexpected("The output has more than one music resource.");
        music = &resource;
    }
    if (!music) return std::vector<project_asset_copy_request_t>{};
    const auto* asset = snapshot.find_audio_asset(music->asset);
    if (!asset) return std::unexpected("The music resource has no audio asset.");
    return std::vector<project_asset_copy_request_t>{{.project_directory = input.asset_base,
        .source_path = asset->source_path, .asset = asset->id}};
}

auto prepare_output_assets(const output_group_export_request_t& input,
                           conversion_request_t& request, std::stop_token stop)
    -> result_t<std::vector<project_asset_copy_request_t>, std::string> {
    const auto discovered = discover_staged_export_assets(input.document, input.plan.group,
                                                         input.asset_base, stop);
    if (!discovered) return std::unexpected(discovered.errors.front());
    request.staged_assets = discovered.mappings;
    auto pending = input.plan.group.family == format_family_t::bms ||
                           input.plan.group.family == format_family_t::bmson
                       ? plan_pending_export_assets(input.document, input.plan.group,
                                                    input.asset_base, request.staged_assets)
                       : plan_output_music(input);
    if (!pending) return std::unexpected(pending.error());
    std::erase_if(*pending, [&](const auto& copy) {
        return std::ranges::any_of(request.staged_assets, [&](const auto& staged) {
            return staged.asset == copy.asset;
        });
    });
    if (!pending->empty() && pending->front().planned_filename.empty()) {
        const auto directory = export_asset_directory(input.plan.group.relative_output_path,
                                                       input.plan.group.destination_kind);
        if (!directory) return std::unexpected("Export asset directory is invalid.");
        auto planned = plan_project_asset_copies(*pending, *directory, stop);
        if (!planned) return std::unexpected(planned.error().message);
        *pending = std::move(*planned);
    }
    for (const auto& copy : *pending)
        request.staged_assets.push_back({copy.asset, std::filesystem::path{"assets"} / copy.destination_filename()});
    return pending;
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
    completed.message = "Output target published with " + std::to_string(count) +
                        (count == 1 ? " conversion loss: " : " conversion losses: ") +
                        first->message;
    wormedit::truncate_notification_text(completed.message,
                                         wormedit::notification_message_max_bytes);
    completed.severity = wormedit::notification_severity_t::warning;
}

void complete_output_publication(const wormedit::conversion_publication_result_t& published,
                                 const wormedit::conversion_result_t& converted,
                                 std::stop_token stop, output_group_export_task_result_t& completed) {
    using namespace wormedit;
    completed.published = published.published;
    completed.message = published.published ? "Output target published from the selected revision."
                        : published.errors.empty() ? "Output target publication failed."
                                                   : published.errors.front();
    completed.severity = !published.published && stop.stop_requested() ? notification_severity_t::info
                         : !published.published          ? notification_severity_t::error
                         : !published.warnings.empty() ? notification_severity_t::warning
                                                       : notification_severity_t::success;
    if (!published.published && stop.stop_requested())
        completed.message = "Export cancelled before publication.";
    if (published.published && !published.warnings.empty())
        completed.message += " " + published.warnings.front();
    if (published.published) {
        for (const auto& warning : published.warnings)
            completed.detail += warning + "\n";
        report_output_conversion_losses(converted, completed);
    }
}

auto validate_generated_asset_sources(const output_group_export_request_t& input,
                                      const conversion_result_t& converted) -> std::optional<std::string> {
    for (const auto& copy : converted.asset_copies) {
        const auto* audio = input.document.find_audio_asset(copy.asset);
        const auto visual = std::ranges::find(input.document.visual_assets, copy.asset, &visual_asset_t::id);
        if (!audio && visual == input.document.visual_assets.end())
            return "Generated export copy has no source asset.";
        const auto& source = audio ? audio->source_path : visual->source_path;
        const auto path = source.is_absolute() ? source : input.asset_base / source;
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error)
            return "Export source is unavailable: " + path.string();
    }
    return std::nullopt;
}

auto existing_publication_files(const std::filesystem::path& project_directory,
                                const conversion_publication_plan_payload_t& publication)
    -> result_t<std::vector<std::filesystem::path>, std::string> {
    std::vector<std::filesystem::path> conflicts;
    const auto inspect = [&](const std::filesystem::path& relative) -> std::optional<std::string> {
        std::error_code error;
        const bool exists = std::filesystem::exists(project_directory / relative, error);
        if (error)
            return "Could not inspect output destination.";
        if (exists)
            conflicts.push_back(relative);
        return std::nullopt;
    };
    for (const auto& file : publication.files)
        if (auto error = inspect(file.project_relative_path))
            return std::unexpected(*error);
    for (const auto& file : publication.generated_files)
        if (auto error = inspect(file))
            return std::unexpected(*error);
    return conflicts;
}

auto file_size_or_zero(const std::filesystem::path& path) -> std::uintmax_t {
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    return error ? 0 : bytes;
}

auto generated_file_bytes(const output_group_export_request_t& input,
                          const conversion_result_t& converted,
                          const std::filesystem::path& relative) -> std::uintmax_t {
    const auto name = relative.filename();
    for (const auto& plan : converted.slice_plans)
        for (const auto& span : plan.value.spans)
            if (span.filename == name)
                return 56 + (span.end_frame - span.begin_frame) * 2 * sizeof(float);
    if (std::ranges::find(converted.silent_files, name) != converted.silent_files.end())
        return 56 + 2 * sizeof(float);
    const auto copied = std::ranges::find(converted.asset_copies, name,
                                          &conversion_asset_copy_t::relative_path);
    if (copied == converted.asset_copies.end()) return 0;
    const auto* audio = input.document.find_audio_asset(copied->asset);
    const auto visual = std::ranges::find(input.document.visual_assets, copied->asset,
                                          &visual_asset_t::id);
    const auto source = audio ? audio->source_path
                              : visual == input.document.visual_assets.end()
                                    ? std::filesystem::path{}
                                    : visual->source_path;
    return file_size_or_zero(source.is_absolute() ? source : input.asset_base / source);
}

void build_file_plan(const output_group_export_request_t& input,
                     output_group_export_preparation_t& prepared) {
    const auto exists = [&](const std::filesystem::path& path) {
        return std::ranges::find(prepared.conflicts, path) != prepared.conflicts.end();
    };
    prepared.files.reserve(prepared.publication.files.size() +
                           prepared.publication.generated_files.size() +
                           prepared.publication.assets.size());
    for (const auto& file : prepared.publication.files) {
        prepared.files.push_back(
            {.path = file.project_relative_path,
             .action = exists(file.project_relative_path)
                           ? output_group_export_file_action_t::replace
                           : output_group_export_file_action_t::new_file,
             .bytes = prepared.converted.outputs[file.output_index].bytes.size()});
    }
    for (const auto& file : prepared.publication.generated_files) {
        const auto copied = std::ranges::find(prepared.converted.asset_copies, file.filename(),
                                              &conversion_asset_copy_t::relative_path) !=
                            prepared.converted.asset_copies.end();
        prepared.files.push_back(
            {.path = file,
             .action = exists(file) ? output_group_export_file_action_t::replace
                                    : copied ? output_group_export_file_action_t::copy
                                             : output_group_export_file_action_t::new_file,
             .bytes = generated_file_bytes(input, prepared.converted, file)});
    }
    for (const auto& asset : prepared.publication.assets) {
        const auto pending = std::ranges::find(
            prepared.pending_assets, asset.asset, &project_asset_copy_request_t::asset);
        const auto copied = pending != prepared.pending_assets.end();
        const auto source = copied
                                ? (pending->source_path.is_absolute()
                                       ? pending->source_path
                                       : input.asset_base / pending->source_path)
                                : input.asset_base / asset.project_relative_path;
        prepared.files.push_back(
            {.path = asset.project_relative_path,
             .action = copied ? output_group_export_file_action_t::copy
                              : output_group_export_file_action_t::reuse,
             .bytes = file_size_or_zero(source)});
    }
}

auto publish_prepared_output(const output_group_export_request_t& input,
                             const output_group_export_preparation_t& prepared,
                             std::stop_token stop) -> output_group_export_task_result_t {
    conversion_publication_options_t publication;
    publication.stop = stop;
    publication.progress = input.progress.get();
    publication.pending_assets = prepared.pending_assets;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    if (is_conversion_needs_slice_spans(input.document, prepared.request)) {
        publication.stage_generated_files = [&](const auto& staging) {
            return render_conversion_slice_plans(input.document, prepared.converted,
                                                 {input.asset_base, staging, stop});
        };
    }
#endif
    const auto published =
        publish_conversion_group_atomic(input.asset_base, input.plan.group, prepared.request,
                                        prepared.converted, std::move(publication));
    output_group_export_task_result_t completed;
    complete_output_publication(published, prepared.converted, stop, completed);
    return completed;
}

struct prepared_output_batch_t {
    std::vector<output_group_export_preparation_t> preparations;
    std::string conflicts;
};

auto prepare_output_targets(const output_group_export_request_t& input,
                            std::span<const output_group_export_target_t> targets,
                            std::stop_token stop)
    -> result_t<prepared_output_batch_t, output_group_export_task_result_t> {
    prepared_output_batch_t batch;
    batch.preparations.reserve(targets.size());
    for (const auto& target : targets) {
        auto target_request = input;
        target_request.plan = single_target_plan(target);
        auto prepared = output_group_export_preparation_t::from(target_request, stop);
        const auto prefix = target.group.name.empty() ? std::string{} : target.group.name + ": ";
        if (!prepared)
            return std::unexpected(output_group_export_task_result_t{
                .message = prefix + prepared.error(),
                .severity = stop.stop_requested() ? notification_severity_t::info
                                                  : notification_severity_t::error});
        if (!prepared->blocking_error.empty())
            return std::unexpected(
                output_group_export_task_result_t{.message = prefix + prepared->blocking_error});
        if (target.existing_files == existing_files_policy_t::ask)
            for (const auto& path : prepared->conflicts)
                batch.conflicts += target.group.name + ": " + path.generic_string() + "\n";
        if (!prepared->conflicts.empty() &&
            target.existing_files == existing_files_policy_t::keep)
            return std::unexpected(output_group_export_task_result_t{
                .message = prefix + "Existing files are kept by this export target."});
        batch.preparations.push_back(std::move(*prepared));
    }
    return batch;
}

} // namespace

auto output_group_export_plan_t::from_all(const song_document_t& document,
                                          std::span<const output_group_t> groups)
    -> std::optional<output_group_export_plan_t> {
    if (groups.empty())
        return std::nullopt;
    auto plan = from(document, groups.front());
    plan.additional_targets.reserve(groups.size() - 1);
    for (const auto& group : groups.subspan(1)) {
        auto target = from(document, group);
        plan.additional_targets.push_back(export_target(target));
    }
    return plan;
}

void output_group_export_plan_t::replace_asked_files() {
    if (existing_files == existing_files_policy_t::ask) {
        existing_files = existing_files_policy_t::replace;
        request.overwrite = true;
    }
    for (auto& target : additional_targets) {
        if (target.existing_files != existing_files_policy_t::ask)
            continue;
        target.existing_files = existing_files_policy_t::replace;
        target.request.overwrite = true;
    }
}

auto output_group_export_preparation_t::from(const output_group_export_request_t& input,
                                             std::stop_token stop)
    -> result_t<output_group_export_preparation_t, std::string> {
    if (stop.stop_requested()) return std::unexpected("Export cancelled before conversion.");
    if (input.plan.group.charts.empty()) return std::unexpected("Add at least one chart before exporting.");
    output_group_export_preparation_t prepared{.request = input.plan.request};
    if (is_conversion_needs_slice_spans(input.document, prepared.request)) {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        if (auto error = prepare_conversion_slice_sources(input.document, prepared.request, input.asset_base, stop))
            return std::unexpected(*error);
#else
        return std::unexpected("BMSON sliced-audio conversion requires the audio decoder build.");
#endif
    } else {
        auto pending = prepare_output_assets(input, prepared.request, stop);
        if (!pending) return std::unexpected(pending.error());
        prepared.pending_assets = std::move(*pending);
    }
    prepared.converted = convert_document(input.document, prepared.request, {},
                                          {.stop = stop, .progress = input.progress.get()});
    if (!prepared.converted) {
        const auto error = std::ranges::find(prepared.converted.diagnostics,
            diagnostic_severity_t::error, &conversion_diagnostic_t::severity);
        prepared.blocking_error = error == prepared.converted.diagnostics.end()
                                      ? "Output conversion failed." : error->message;
        return prepared;
    }
    if (stop.stop_requested()) return std::unexpected("Export cancelled before publication.");
    if (auto error = validate_generated_asset_sources(input, prepared.converted))
        return std::unexpected(*error);
    auto planning_request = prepared.request;
    planning_request.overwrite = true;
    auto publication = conversion_publication_plan_payload_t::from(
        input.asset_base, input.plan.group, planning_request, prepared.converted,
        prepared.pending_assets);
    if (!publication) return std::unexpected(publication.error().message);
    prepared.publication = std::move(*publication);
    auto conflicts = existing_publication_files(input.asset_base, prepared.publication);
    if (!conflicts)
        return std::unexpected(conflicts.error());
    prepared.conflicts = std::move(*conflicts);
    build_file_plan(input, prepared);
    return prepared;
}

auto run_output_group_export(const output_group_export_request_t& input, std::stop_token stop)
    -> output_group_export_task_result_t {
    std::vector<output_group_export_target_t> targets{export_target(input.plan)};
    targets.insert(targets.end(), input.plan.additional_targets.begin(),
                   input.plan.additional_targets.end());
    auto prepared = prepare_output_targets(input, targets, stop);
    if (!prepared) return std::move(prepared.error());
    if (!prepared->conflicts.empty())
        return {.confirmation_required = true,
                .message = "Replace existing files?",
                .severity = notification_severity_t::warning,
                .detail = std::move(prepared->conflicts)};
    output_group_export_task_result_t completed{
        .published = true,
        .message = targets.size() == 1
                       ? "Output target published from the selected revision."
                       : std::to_string(targets.size()) + " output targets published.",
        .severity = notification_severity_t::success};
    for (std::size_t index = 0; index < targets.size(); ++index) {
        auto target_request = input;
        target_request.plan = single_target_plan(targets[index]);
        auto result = publish_prepared_output(target_request, prepared->preparations[index], stop);
        if (!result.published) {
            result.message = targets[index].group.name + ": " + result.message;
            result.published = index != 0;
            return result;
        }
        if (targets.size() == 1)
            return result;
        if (result.severity == notification_severity_t::warning)
            completed.severity = notification_severity_t::warning;
        completed.detail += targets[index].group.name + ": " + result.message + "\n";
        if (!result.detail.empty())
            completed.detail += result.detail;
    }
    return completed;
}

} // namespace wormedit::sys

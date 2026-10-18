#include "sys/output_group_export.h"
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/formats/conversion_publication.h>
#include <wormedit/formats/slice_spans.h>
#include <algorithm>
#include <utility>

namespace wormedit::sys {
namespace {
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
    const auto* asset = snapshot.find_audio_asset(music->asset);
    if (asset == nullptr)
        return "The music resource has no audio asset.";
    const auto copied =
        create_export_asset_copy({.project_directory = asset_base,
                                        .source_path = asset->source_path,
                                        .relative_output_path = group.relative_output_path,
                                        .asset = asset->id},
                                       stop);
    if (!copied)
        return "Could not stage music for export: " + copied.error().message;
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
        !wormedit::is_conversion_needs_slice_spans(snapshot, request) ||
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
    if (is_conversion_needs_slice_spans(snapshot, request)) {
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

} // namespace

auto run_output_group_export(const output_group_export_request_t& input, std::stop_token stop)
    -> output_group_export_task_result_t {
    using namespace wormedit;
    const auto& snapshot = input.document;
    const auto& group = input.plan.group;
    auto request = input.plan.request;
    const auto& asset_base = input.asset_base;
    output_group_export_task_result_t completed;
    if (stop.stop_requested()) {
        completed.message = "Export cancelled before conversion.";
        completed.severity = notification_severity_t::info;
    } else if (is_conversion_needs_slice_spans(snapshot, request)) {
        complete_sliced_output_group_conversion(snapshot, group, request, asset_base, stop,
                                                completed);
    } else if (auto staging_error = stage_output_music(snapshot, group, asset_base, stop)) {
        completed.message = *staging_error;
        completed.severity =
            stop.stop_requested() ? notification_severity_t::info : notification_severity_t::error;
    } else {
        const auto discovered = discover_staged_export_assets(snapshot, group, asset_base, stop);
        if (!discovered) {
            completed.message = discovered.errors.front();
            completed.severity = stop.stop_requested() ? notification_severity_t::info
                                                        : notification_severity_t::error;
        } else {
            request.staged_assets = discovered.mappings;
            complete_output_group_conversion(snapshot, group, request, asset_base, stop,
                                             completed);
        }
    }
    return completed;
}

} // namespace wormedit::sys

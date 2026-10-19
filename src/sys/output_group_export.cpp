#include "sys/output_group_export.h"
#include <wormedit/core/catalogs.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/formats/conversion_publication.h>
#include <wormedit/formats/slice_spans.h>
#include <algorithm>
#include <utility>

namespace wormedit::sys {
namespace {
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

void complete_output_publication(const wormedit::conversion_publication_result_t& published,
                                 const wormedit::conversion_result_t& converted,
                                 std::stop_token stop, output_group_export_task_result_t& completed) {
    using namespace wormedit;
    completed.published = published.published;
    completed.message = published.published ? "Output group published from the selected revision."
                        : published.errors.empty() ? "Output group publication failed."
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

} // namespace

auto output_group_export_preparation_t::from(const output_group_export_request_t& input,
                                             std::stop_token stop)
    -> result_t<output_group_export_preparation_t, std::string> {
    if (stop.stop_requested()) return std::unexpected("Export cancelled before conversion.");
    if (input.plan.group.charts.empty()) return std::unexpected("Add at least one chart before exporting.");
    if (input.plan.group.family == format_family_t::bms &&
        input.plan.group.relative_output_path.has_extension() &&
        !is_bms_extension(input.plan.group.relative_output_path.extension().string()))
        return std::unexpected("Output extension does not match the BMS format family");
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
        return std::unexpected(error == prepared.converted.diagnostics.end()
                                   ? "Output conversion failed." : error->message);
    }
    if (stop.stop_requested()) return std::unexpected("Export cancelled before publication.");
    if (auto error = sliced_bms_destination_error(input.document, input.plan.group,
                                                 prepared.request, prepared.converted))
        return std::unexpected(*error);
    if (auto error = validate_generated_asset_sources(input, prepared.converted))
        return std::unexpected(*error);
    auto publication = conversion_publication_plan_payload_t::from(input.asset_base,
        input.plan.group, prepared.request, prepared.converted, false, prepared.pending_assets);
    if (!publication) return std::unexpected(publication.error().message);
    prepared.publication = std::move(*publication);
    return prepared;
}

auto run_output_group_export(const output_group_export_request_t& input, std::stop_token stop)
    -> output_group_export_task_result_t {
    auto prepared = output_group_export_preparation_t::from(input, stop);
    if (!prepared)
        return {.message = prepared.error(),
                .severity = stop.stop_requested() ? notification_severity_t::info
                                                  : notification_severity_t::error};
    conversion_publication_options_t publication;
    publication.stop = stop;
    publication.progress = input.progress.get();
    publication.pending_assets = prepared->pending_assets;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    if (is_conversion_needs_slice_spans(input.document, prepared->request)) {
        publication.stage_generated_files = [&](const auto& staging) {
            return render_conversion_slice_plans(input.document, prepared->converted,
                                                  {input.asset_base, staging, stop});
        };
    }
#endif
    const auto published = publish_conversion_group_atomic(input.asset_base, input.plan.group,
        prepared->request, prepared->converted, std::move(publication));
    output_group_export_task_result_t completed;
    complete_output_publication(published, prepared->converted, stop, completed);
    return completed;
}

} // namespace wormedit::sys

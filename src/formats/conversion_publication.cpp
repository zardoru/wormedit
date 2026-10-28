#include <wormedit/core/metadata_inheritance.h>
#include <wormedit/formats/conversion_publication.h>

#include <wormedit/core/resource_file_copy.h>
#include <wormedit/formats/osu_mania.h>

#include <algorithm>
#include <set>
#include <string_view>
#include <utility>

namespace wormedit {
namespace {

auto is_valid_relative(const std::filesystem::path& path) -> bool {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
        path != path.lexically_normal())
        return false;
    for (const auto& part : path) {
        if (part.empty() || part == "." || part == "..")
            return false;
        const auto bytes = part.string();
        if (std::ranges::any_of(bytes, [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
            return false;
    }
    return true;
}

auto is_safe_existing_components(const std::filesystem::path& root,
                              const std::filesystem::path& relative, bool final_must_be_regular,
                              std::string& error_message) -> bool {
    namespace fs = std::filesystem;
    auto current = root;
    std::error_code error;
    const auto root_status = fs::symlink_status(current, error);
    if (error || fs::is_symlink(root_status) || !fs::is_directory(root_status)) {
        error_message = "Project directory is unavailable or is a symlink.";
        return false;
    }
    for (auto it = relative.begin(); it != relative.end(); ++it) {
        current /= *it;
        const auto status = fs::symlink_status(current, error);
        if (error == std::errc::no_such_file_or_directory) {
            error.clear();
            if (final_must_be_regular) {
                error_message = "Staged asset file is missing.";
                return false;
            }
            return true;
        }
        if (error || fs::is_symlink(status)) {
            error_message = "Output path contains an unreadable or symlink component.";
            return false;
        }
        if (std::next(it) == relative.end()) {
            if (!fs::is_regular_file(status)) {
                error_message = "Output path has an incompatible existing entry.";
                return false;
            }
        } else if (!fs::is_directory(status)) {
            error_message = "Output path contains a non-directory component.";
            return false;
        }
    }
    return true;
}

auto append_conversion_files(const std::filesystem::path& project_directory,
                            const output_group_t& group, const conversion_request_t& request,
                            const conversion_result_t& converted,
                            conversion_publication_plan_payload_t& plan,
                            std::set<std::filesystem::path>& destinations)
    -> std::optional<std::string> {
    namespace fs = std::filesystem;
    const bool file_path = group.destination_kind == output_destination_kind_t::file;
    const auto output_directory =
        file_path ? group.relative_output_path.parent_path() : group.relative_output_path;
    for (std::size_t index = 0; index < converted.outputs.size(); ++index) {
        const auto& output = converted.outputs[index];
        const fs::path suggested{output.suggested_filename};
        if (output.target != request.target || !output.reimport_validated ||
            !is_valid_relative(suggested) || suggested.has_parent_path() ||
            (output.chart && !group.charts.empty() &&
             std::ranges::find(group.charts, *output.chart) == group.charts.end())) {
            return "Converted output has an invalid target, chart, or filename.";
        }
        const auto relative = file_path ? group.relative_output_path : output_directory / suggested;
        std::string error;
        if (!is_valid_relative(relative) || !destinations.insert(relative).second ||
            !is_safe_existing_components(project_directory, relative, false, error)) {
            return error.empty() ? "Output filenames collide or are invalid." : error;
        }
        std::error_code fs_error;
        if (fs::exists(project_directory / relative, fs_error) && !request.overwrite) {
            return "Output file already exists and overwrite is disabled.";
        }
        if (fs_error) {
            return "Could not inspect output destination.";
        }
        plan.files.push_back({index, relative});
    }
    return std::nullopt;
}

auto append_generated_file(const std::filesystem::path& name,
    const std::filesystem::path& project_directory, const std::filesystem::path& directory,
    bool overwrite, conversion_publication_plan_payload_t& plan,
    std::set<std::filesystem::path>& destinations) -> std::optional<std::string> {
    const auto relative = directory / name;
    std::string error;
    if (!is_valid_relative(name) || name.has_parent_path() ||
        !destinations.insert(relative).second ||
        !is_safe_existing_components(project_directory, relative, false, error))
        return error.empty() ? "Generated asset filenames collide or are invalid." : error;
    std::error_code fs_error;
    if (std::filesystem::exists(project_directory / relative, fs_error) && !overwrite)
        return "Generated asset exists and overwrite is disabled.";
    if (fs_error) return "Could not inspect generated asset destination.";
    plan.generated_files.push_back(relative);
    return std::nullopt;
}

auto append_generated_files(const std::filesystem::path& project_directory,
    const output_group_t& group, const conversion_request_t& request,
    const conversion_result_t& converted, conversion_publication_plan_payload_t& plan,
    std::set<std::filesystem::path>& destinations) -> std::optional<std::string> {
    const auto directory = group.destination_kind == output_destination_kind_t::file
        ? group.relative_output_path.parent_path() : group.relative_output_path;
    for (const auto& slice_plan : converted.slice_plans)
        for (const auto& span : slice_plan.value.spans)
            if (auto failure = append_generated_file(span.filename, project_directory, directory,
                    request.overwrite, plan, destinations)) return failure;
    for (const auto& cut : converted.cut_audio_plans)
        if (auto failure = append_generated_file(cut.filename, project_directory, directory,
                request.overwrite, plan, destinations)) return failure;
    for (const auto& name : converted.silent_files)
        if (auto failure = append_generated_file(name, project_directory, directory,
                request.overwrite, plan, destinations)) return failure;
    for (const auto& copy : converted.asset_copies)
        if (auto failure = append_generated_file(copy.relative_path, project_directory, directory,
                request.overwrite, plan, destinations)) return failure;
    return std::nullopt;
}

auto validate_pending_assets(const conversion_request_t& request,
                             std::span<const project_asset_copy_request_t> pending)
    -> std::optional<std::string> {
    std::set<asset_id_t> seen;
    for (const auto& copy : pending) {
        const auto matching = std::ranges::count_if(request.staged_assets, [&](const auto& staged) {
            return staged.asset == copy.asset &&
                   staged.relative_path == std::filesystem::path{"assets"} / copy.destination_filename();
        });
        if (!seen.insert(copy.asset).second || matching != 1)
            return "Pending asset must have one matching staged mapping.";
    }
    return std::nullopt;
}

auto append_staged_assets(const std::filesystem::path& project_directory,
                          const output_group_t& group, const conversion_request_t& request,
                          conversion_publication_plan_payload_t& plan,
                          std::set<std::filesystem::path>& destinations,
                          std::span<const project_asset_copy_request_t> pending_assets)
    -> std::optional<std::string> {
    namespace fs = std::filesystem;
    const auto asset_directory = group.destination_kind == output_destination_kind_t::directory
        ? std::optional{group.relative_output_path / "assets"}
        : export_asset_directory(group.relative_output_path, group.destination_kind);
    if (!asset_directory) {
        return "Output asset directory is invalid.";
    }
    std::set<asset_id_t> asset_ids;
    std::set<fs::path> asset_destinations;
    const auto output_directory = group.destination_kind == output_destination_kind_t::file
                                      ? group.relative_output_path.parent_path()
                                      : group.relative_output_path;
    for (const auto& staged : request.staged_assets) {
        const bool pending = std::ranges::any_of(pending_assets, [&](const auto& copy) {
            return copy.asset == staged.asset;
        });
        const auto relative = output_directory / staged.relative_path;
        std::string error;
        if (!staged.asset || !asset_ids.insert(staged.asset).second ||
            !is_valid_relative(staged.relative_path) ||
            staged.relative_path.parent_path() != fs::path{"assets"} || !is_valid_relative(relative) ||
            relative.parent_path() != *asset_directory || destinations.contains(relative) ||
            !asset_destinations.insert(relative).second ||
            !is_safe_existing_components(project_directory, relative, !pending, error)) {
            return error.empty() ? "Staged asset path is invalid or missing." : error;
        }
        plan.assets.push_back({staged.asset, relative});
    }
    return std::nullopt;
}

// Conversion copies these beside the osu! chart, where osu! looks them up by name.
auto osu_skin_sample_asset_ids(const song_document_t& song, const output_group_t& group) -> std::set<asset_id_t> {
    std::set<asset_id_t> result;
    if (group.family != format_family_t::osu_mania) return result;
    for (const auto& found : build_osu_skin_sample_assets(song)) result.insert(found.asset);
    return result;
}

auto referenced_export_assets(const song_document_t& song, const output_group_t& group)
    -> result_t<std::set<asset_id_t>, std::string> {
    std::set<asset_id_t> referenced;
    const auto skin_samples = osu_skin_sample_asset_ids(song, group);
    for (const auto& resource : song.resources) {
        if (resource.asset && !skin_samples.contains(resource.asset)) referenced.insert(resource.asset);
    }
    for (const auto chart_id : group.charts) {
        const auto chart = song.read_chart(chart_id);
        if (chart == nullptr)
            return std::unexpected(std::string{"Output group refers to a missing chart."});
        const auto audio = metadata_audio_references_t::from(song, chart->metadata.overrides);
        if (!audio.ok()) return std::unexpected(audio.diagnostics.front().message);
        referenced.insert(audio.value.assets.begin(), audio.value.assets.end());
        for (const auto& channel : chart->sound_channels) {
            if (channel.source_clip.source) referenced.insert(channel.source_clip.source);
        }
    }
    return referenced;
}

auto append_discovered_asset(const song_document_t& song, const output_group_t& group,
    const std::filesystem::path& project_directory, asset_id_t asset, std::stop_token stop,
    std::vector<conversion_request_t::staged_asset_t>& mappings) -> result_t<void, std::string> {
    const auto audio = song.find_audio_asset(asset);
    const auto visual = std::ranges::find(song.visual_assets, asset, &visual_asset_t::id);
    if ((audio == nullptr) == (visual == song.visual_assets.end()))
        return std::unexpected(std::string{"A resource has no unique source asset."});
    const auto& source = audio != nullptr ? audio->source_path : visual->source_path;
    const auto inspected = inspect_export_asset_file({.project_directory = project_directory,
        .source_path = source, .relative_output_path = group.relative_output_path,
        .destination_kind = group.destination_kind, .asset = asset}, stop);
    if (!inspected && inspected.error().code == failure_code_t::not_found) return {};
    if (!inspected)
        return std::unexpected(std::string{"Export asset #" + std::to_string(asset.value) + " (" + source.string() + "): " + inspected.error().message});
    const auto relative = export_asset_reference_path(
        group.relative_output_path, inspected->relative_path, group.destination_kind);
    if (!relative)
        return std::unexpected(std::string{"Verified export asset has an invalid chart-relative path."});
    mappings.push_back({asset, *relative});
    return {};
}

} // namespace

auto staged_asset_discovery_t::from(const song_document_t& song, const output_group_t& group,
    const std::filesystem::path& project_directory, std::stop_token stop) -> staged_asset_discovery_outcome_t {
    std::vector<conversion_request_t::staged_asset_t> mappings;
    if (group.family != format_family_t::bms && group.family != format_family_t::bmson &&
        group.family != format_family_t::osu_mania && group.family != format_family_t::stepmania)
        return mappings;
    if (!project_directory.is_absolute() ||
        !export_asset_directory(group.relative_output_path, group.destination_kind))
        return std::unexpected(staged_asset_discovery_error_t::from(std::move(mappings), "Export asset directory is invalid."));
    auto referenced = referenced_export_assets(song, group);
    if (!referenced) return std::unexpected(staged_asset_discovery_error_t::from(std::move(mappings), std::move(referenced.error())));
    for (const auto asset : *referenced) {
        if (stop.stop_requested())
            return std::unexpected(staged_asset_discovery_error_t::from(std::move(mappings), "Export asset discovery was cancelled."));
        auto appended = append_discovered_asset(song, group, project_directory, asset, stop, mappings);
        if (!appended) return std::unexpected(staged_asset_discovery_error_t::from(std::move(mappings), std::move(appended.error())));
    }
    return mappings;
}

auto plan_pending_export_assets(const song_document_t& song, const output_group_t& group,
    const std::filesystem::path& project_directory,
    std::span<const conversion_request_t::staged_asset_t> mappings)
    -> result_t<std::vector<project_asset_copy_request_t>, std::string> {
    const auto referenced = referenced_export_assets(song, group);
    if (!referenced) return std::unexpected(referenced.error());
    std::vector<project_asset_copy_request_t> pending;
    for (const auto id : *referenced) {
        if (std::ranges::any_of(mappings, [id](const auto& mapping) { return mapping.asset == id; }))
            continue;
        const auto* audio = song.find_audio_asset(id);
        const auto visual = std::ranges::find(song.visual_assets, id, &visual_asset_t::id);
        if ((audio == nullptr) == (visual == song.visual_assets.end()))
            return std::unexpected("Export asset #" + std::to_string(id.value) + " has no unique source.");
        pending.push_back({.project_directory = project_directory,
            .source_path = audio ? audio->source_path : visual->source_path, .asset = id});
    }
    const auto directory = export_asset_directory(group.relative_output_path, group.destination_kind);
    if (!directory)
        return std::unexpected("Export asset directory is invalid.");
    auto planned = plan_project_asset_copies(pending, *directory);
    if (!planned)
        return std::unexpected(planned.error().message);
    return std::move(*planned);
}

auto conversion_publication_plan_payload_t::from(const std::filesystem::path& project_directory,
                                 const output_group_t& group, const conversion_request_t& request,
                                 const conversion_result_t& converted,
                                 std::span<const project_asset_copy_request_t> pending_assets)
    -> result_t<conversion_publication_plan_payload_t, conversion_publication_plan_error_t> {
    namespace fs = std::filesystem;
    conversion_publication_plan_payload_t plan{};
    if (!project_directory.is_absolute() || !is_valid_relative(group.relative_output_path) ||
        group.family != request.target || !converted || converted.outputs.empty()) {
        return std::unexpected(conversion_publication_plan_error_t::from(std::move(plan), "Project, output group, or conversion result is invalid."));
    }
    if (group.destination_kind == output_destination_kind_t::file && converted.outputs.size() != 1) {
        return std::unexpected(conversion_publication_plan_error_t::from(std::move(plan), "A single chart filename cannot hold multiple outputs."));
    }
    const auto asset_directory = group.destination_kind == output_destination_kind_t::directory
        ? std::optional{group.relative_output_path / "assets"}
        : export_asset_directory(group.relative_output_path, group.destination_kind);
    if (!asset_directory)
        return std::unexpected(conversion_publication_plan_error_t::from(
            std::move(plan), "Output asset directory is invalid."));
    auto planned_assets = plan_project_asset_copies(pending_assets, *asset_directory);
    if (!planned_assets)
        return std::unexpected(conversion_publication_plan_error_t::from(
            std::move(plan), planned_assets.error().message));
    std::set<fs::path> destinations;
    if (auto error = append_conversion_files(project_directory, group, request, converted, plan,
                                             destinations);
        error.has_value()) {
        return std::unexpected(conversion_publication_plan_error_t::from(std::move(plan), std::move(*error)));
    }
    if (auto error = append_generated_files(project_directory, group, request, converted, plan, destinations);
        error.has_value()) {
        return std::unexpected(conversion_publication_plan_error_t::from(std::move(plan), std::move(*error)));
    }
    if (auto error = validate_pending_assets(request, *planned_assets))
        return std::unexpected(conversion_publication_plan_error_t::from(std::move(plan), *error));
    if (auto error = append_staged_assets(project_directory, group, request, plan, destinations, *planned_assets);
        error.has_value()) {
        return std::unexpected(conversion_publication_plan_error_t::from(std::move(plan), std::move(*error)));
    }
    return plan;
}

auto staged_asset_discovery_t::from(staged_asset_discovery_outcome_t&& outcome) -> staged_asset_discovery_t {
    if (!outcome) {
        staged_asset_discovery_t result{.mappings = std::move(outcome.error().mappings), .errors = {}};
        result.errors.push_back(std::move(outcome.error().message));
        return result;
    }
    return {.mappings = std::move(*outcome), .errors = {}};
}

auto discover_staged_export_assets(const song_document_t& song, const output_group_t& group,
    const std::filesystem::path& project_directory, std::stop_token stop) -> staged_asset_discovery_t {
    return staged_asset_discovery_t::from(staged_asset_discovery_t::from(song, group, project_directory, stop));
}

} // namespace wormedit

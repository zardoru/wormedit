#include <wormedit/formats/conversion_publication.h>

#include <wormedit/core/resource_file_copy.h>

#include <algorithm>
#include <set>
#include <string_view>
#include <utility>

namespace wormedit {
namespace {

auto reject(conversion_publication_plan_t plan, std::string message)
    -> conversion_publication_plan_t {
    plan.files.clear();
    plan.assets.clear();
    plan.errors.push_back(std::move(message));
    return plan;
}

auto valid_relative(const std::filesystem::path& path) -> bool {
    if (path.empty() || path.is_absolute() || path.has_root_name() ||
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

auto safe_existing_components(const std::filesystem::path& root,
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
                            conversion_publication_plan_t& plan,
                            std::set<std::filesystem::path>& destinations)
    -> std::optional<std::string> {
    namespace fs = std::filesystem;
    const bool file_path = group.relative_output_path.has_extension();
    const auto output_directory =
        file_path ? group.relative_output_path.parent_path() : group.relative_output_path;
    for (std::size_t index = 0; index < converted.outputs.size(); ++index) {
        const auto& output = converted.outputs[index];
        const fs::path suggested{output.suggested_filename};
        if (output.target != request.target || !output.reimport_validated ||
            !valid_relative(suggested) || suggested.has_parent_path() ||
            (output.chart && !group.charts.empty() &&
             std::ranges::find(group.charts, *output.chart) == group.charts.end())) {
            return "Converted output has an invalid target, chart, or filename.";
        }
        const auto relative = file_path ? group.relative_output_path : output_directory / suggested;
        std::string error;
        if (!valid_relative(relative) || !destinations.insert(relative).second ||
            !safe_existing_components(project_directory, relative, false, error)) {
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

auto append_staged_assets(const std::filesystem::path& project_directory,
                          const output_group_t& group, const conversion_request_t& request,
                          conversion_publication_plan_t& plan,
                          std::set<std::filesystem::path>& destinations)
    -> std::optional<std::string> {
    namespace fs = std::filesystem;
    const auto asset_directory = export_asset_directory(group.relative_output_path);
    if (!asset_directory) {
        return "Output asset directory is invalid.";
    }
    std::set<asset_id_t> asset_ids;
    std::set<fs::path> asset_destinations;
    const auto output_directory = group.relative_output_path.has_extension()
                                      ? group.relative_output_path.parent_path()
                                      : group.relative_output_path;
    for (const auto& staged : request.staged_assets) {
        const auto relative = output_directory / staged.relative_path;
        std::string error;
        if (!staged.asset || !asset_ids.insert(staged.asset).second ||
            !valid_relative(staged.relative_path) ||
            staged.relative_path.parent_path() != fs::path{"assets"} || !valid_relative(relative) ||
            relative.parent_path() != *asset_directory || destinations.contains(relative) ||
            !asset_destinations.insert(relative).second ||
            !safe_existing_components(project_directory, relative, true, error)) {
            return error.empty() ? "Staged asset path is invalid or missing." : error;
        }
        plan.assets.push_back({staged.asset, relative});
    }
    return std::nullopt;
}

auto referenced_export_assets(const song_document_t& song, const output_group_t& group,
                             staged_asset_discovery_t& result)
    -> std::optional<std::set<asset_id_t>> {
    std::set<asset_id_t> referenced;
    for (const auto& resource : song.resources) {
        if (resource.asset) {
            referenced.insert(resource.asset);
        }
    }
    for (const auto chart_id : group.charts) {
        const auto chart = std::ranges::find(song.charts, chart_id, &chart_document_t::id);
        if (chart == song.charts.end()) {
            result.errors.emplace_back("Output group refers to a missing chart.");
            return std::nullopt;
        }
        for (const auto& channel : chart->sound_channels) {
            if (channel.source_clip.source) {
                referenced.insert(channel.source_clip.source);
            }
        }
    }
    return referenced;
}

auto append_discovered_asset(const song_document_t& song, const output_group_t& group,
                             const std::filesystem::path& project_directory, asset_id_t asset,
                             std::stop_token stop, staged_asset_discovery_t& result) -> bool {
    const auto audio = std::ranges::find(song.audio_assets, asset, &audio_asset_t::id);
    const auto visual = std::ranges::find(song.visual_assets, asset, &visual_asset_t::id);
    if ((audio == song.audio_assets.end()) == (visual == song.visual_assets.end())) {
        result.errors.emplace_back("A resource has no unique source asset.");
        return false;
    }
    const auto& source = audio != song.audio_assets.end() ? audio->source_path : visual->source_path;
    const auto inspected = inspect_export_asset_copy({.project_directory = project_directory,
                                                       .source_path = source,
                                                       .relative_output_path = group.relative_output_path,
                                                       .asset = asset},
                                                      stop);
    if (inspected.status == project_asset_copy_status_t::not_staged) {
        return true;
    }
    if (inspected.status != project_asset_copy_status_t::reused) {
        result.errors.push_back("Export asset #" + std::to_string(asset.value) + ": " +
                                inspected.message);
        return false;
    }
    const auto relative =
        export_asset_reference_path(group.relative_output_path, inspected.relative_path);
    if (!relative) {
        result.errors.emplace_back("Verified export asset has an invalid chart-relative path.");
        return false;
    }
    result.mappings.push_back({asset, *relative});
    return true;
}

} // namespace

auto discover_staged_export_assets(const song_document_t& song, const output_group_t& group,
                                   const std::filesystem::path& project_directory,
                                   std::stop_token stop) -> staged_asset_discovery_t {
    staged_asset_discovery_t result;
    if (group.family != format_family_t::bms && group.family != format_family_t::bmson &&
        group.family != format_family_t::osu_mania) {
        return result;
    }
    if (!project_directory.is_absolute() || !export_asset_directory(group.relative_output_path)) {
        result.errors.emplace_back("Export asset directory is invalid.");
        return result;
    }
    const auto referenced = referenced_export_assets(song, group, result);
    if (!referenced.has_value()) {
        return result;
    }
    for (const auto asset : *referenced) {
        if (stop.stop_requested()) {
            result.errors.emplace_back("Export asset discovery was cancelled.");
            result.mappings.clear();
            return result;
        }
        if (!append_discovered_asset(song, group, project_directory, asset, stop, result)) {
            result.mappings.clear();
            return result;
        }
    }
    return result;
}

auto plan_conversion_publication(const std::filesystem::path& project_directory,
                                 const output_group_t& group, const conversion_request_t& request,
                                 const conversion_result_t& converted)
    -> conversion_publication_plan_t {
    namespace fs = std::filesystem;
    conversion_publication_plan_t plan;
    if (!project_directory.is_absolute() || !valid_relative(group.relative_output_path) ||
        group.family != request.target || !converted || converted.outputs.empty()) {
        return reject(std::move(plan), "Project, output group, or conversion result is invalid.");
    }
    if (group.relative_output_path.has_extension() && converted.outputs.size() != 1) {
        return reject(std::move(plan), "A single chart filename cannot hold multiple outputs.");
    }
    std::set<fs::path> destinations;
    if (auto error = append_conversion_files(project_directory, group, request, converted, plan,
                                             destinations);
        error.has_value()) {
        return reject(std::move(plan), std::move(*error));
    }
    if (auto error = append_staged_assets(project_directory, group, request, plan, destinations);
        error.has_value()) {
        return reject(std::move(plan), std::move(*error));
    }
    return plan;
}

} // namespace wormedit

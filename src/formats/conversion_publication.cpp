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

} // namespace

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
    const bool file_path = group.relative_output_path.has_extension();
    if (file_path && converted.outputs.size() != 1) {
        return reject(std::move(plan), "A single chart filename cannot hold multiple outputs.");
    }
    const auto output_directory =
        file_path ? group.relative_output_path.parent_path() : group.relative_output_path;
    std::set<fs::path> destinations;
    for (std::size_t index = 0; index < converted.outputs.size(); ++index) {
        const auto& output = converted.outputs[index];
        const fs::path suggested{output.suggested_filename};
        if (output.target != request.target || !output.reimport_validated ||
            !valid_relative(suggested) || suggested.has_parent_path() ||
            (output.chart && !group.charts.empty() &&
             std::ranges::find(group.charts, *output.chart) == group.charts.end())) {
            return reject(std::move(plan),
                          "Converted output has an invalid target, chart, or filename.");
        }
        const auto relative = file_path ? group.relative_output_path : output_directory / suggested;
        std::string error;
        if (!valid_relative(relative) || !destinations.insert(relative).second ||
            !safe_existing_components(project_directory, relative, false, error)) {
            return reject(std::move(plan),
                          error.empty() ? "Output filenames collide or are invalid." : error);
        }
        std::error_code fs_error;
        if (fs::exists(project_directory / relative, fs_error) && !request.overwrite) {
            return reject(std::move(plan), "Output file already exists and overwrite is disabled.");
        }
        if (fs_error) {
            return reject(std::move(plan), "Could not inspect output destination.");
        }
        plan.files.push_back({index, relative});
    }
    const auto asset_directory = export_asset_directory(group.relative_output_path);
    if (!asset_directory) {
        return reject(std::move(plan), "Output asset directory is invalid.");
    }
    std::set<asset_id_t> asset_ids;
    std::set<fs::path> asset_destinations;
    for (const auto& staged : request.staged_assets) {
        const auto relative = output_directory / staged.relative_path;
        std::string error;
        if (!staged.asset || !asset_ids.insert(staged.asset).second ||
            !valid_relative(staged.relative_path) ||
            staged.relative_path.parent_path() != fs::path{"assets"} || !valid_relative(relative) ||
            relative.parent_path() != *asset_directory || destinations.contains(relative) ||
            !asset_destinations.insert(relative).second ||
            !safe_existing_components(project_directory, relative, true, error)) {
            return reject(std::move(plan),
                          error.empty() ? "Staged asset path is invalid or missing." : error);
        }
        plan.assets.push_back({staged.asset, relative});
    }
    return plan;
}

} // namespace wormedit

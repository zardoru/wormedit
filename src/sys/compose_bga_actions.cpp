#include "sys/compose_bga_actions.h"
#include <wormedit/core/resource_assignment.h>
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_decoder.h>
#endif
#include <algorithm>
#include <filesystem>
#include <unordered_set>
namespace wormedit::sys {
namespace {
auto probe_image(const std::filesystem::path& path)
    -> std::optional<std::pair<std::uint32_t, std::uint32_t>> {
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    const auto image = visual::load_image_rgba({.path = path});
    if (image) return std::pair{image.image->width, image.image->height};
#else
    (void)path;
#endif
    return std::nullopt;
}
auto referenced_bga_resources(const song_document_t& document) -> std::unordered_set<std::uint64_t> {
    std::unordered_set<std::uint64_t> referenced;
    for (const auto& target : document.charts) {
        for (const auto& event : target.bga_events) referenced.insert(event.resource.value);
        for (const auto& measure : target.measures) {
            for (const auto& note : measure.notes) if (note.sound) referenced.insert(note.sound->value);
            for (const auto& event : measure.bgm_events) if (event.sound) referenced.insert(event.sound->value);
        }
    }
    return referenced;
}
auto finish_bga_edit(document_history_t& history, workspace_controller_t& workspace,
                     const document_command_result_t& result, std::string message) -> std::string {
    if (!result.diagnostics.empty()) return result.diagnostics.front().message;
    if (!result.applied) return "No BGA resources changed.";
    const auto rebound = rebind_workspace(workspace, history);
    if (!rebound) return "BGA changed, but workspace rebinding failed.";
    return message;
}
}
auto import_bga_files(document_history_t& history, workspace_controller_t& workspace,
                       chart_id_t chart_id, std::span<const std::string> paths) -> std::string {
    const auto chart = std::ranges::find(history.document.charts, chart_id, &chart_document_t::id);
    if (chart == history.document.charts.end()) return "Select a difficulty before adding images.";
    if (paths.empty() || paths.size() > WORM_UI_SOUND_DROP_CAPACITY) return "Invalid image import batch size.";
    auto candidate = make_document_history(history.document, 1);
    std::unordered_set<std::string> known;
    for (const auto& asset : history.document.visual_assets)
        known.insert(asset.source_path.lexically_normal().generic_string());
    std::vector<std::string> sorted{paths.begin(), paths.end()};
    std::ranges::sort(sorted);
    std::size_t added{}, skipped{};
    for (const auto& text : sorted) {
        const auto path = std::filesystem::path{text}.lexically_normal();
        const auto geometry = path.is_absolute() ? probe_image(path) : std::nullopt;
        if (!geometry || !known.insert(path.generic_string()).second) { ++skipped; continue; }
        const auto result = apply_resource_asset_import(candidate, candidate.document.revision,
            {.source_path = path, .role = resource_role_t::bga, .name = path.filename().string(),
             .allocate_bms_identifier = chart->source_family == format_family_t::bms,
             .image_geometry = geometry});
        if (!result.diagnostics.empty()) return result.diagnostics.front().message;
        if (!result.applied) return "Image import could not be prepared.";
        ++added;
    }
    if (!added) return "No images added; skipped " + std::to_string(skipped) + " unreadable or existing files.";
    const auto result = commit_document_command(history, std::move(candidate.document), "Import BGA images");
    return finish_bga_edit(history, workspace, result, "Added " + std::to_string(added) +
        " images; skipped " + std::to_string(skipped) + " unreadable or existing files.");
}
auto dispatch_compose_bga_action(const WormUiWorkspaceAction& action, document_history_t& history,
    workspace_controller_t& workspace, chart_id_t chart) -> std::optional<std::string> {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_IMPORT_BGA_FILES &&
        action.compose_action != WORM_UI_COMPOSE_ACTION_REMOVE_UNUSED_BGA) return std::nullopt;
    if (history.document.revision != action.compose_input_document_revision)
        return "BGA action ignored because the document changed.";
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_BGA_FILES) {
        if (action.compose_import_path_count > WORM_UI_SOUND_DROP_CAPACITY) return "Image import batch is too large.";
        std::vector<std::string> paths;
        for (std::size_t index = 0; index < action.compose_import_path_count; ++index)
            if (action.compose_import_paths && action.compose_import_paths[index])
                paths.emplace_back(action.compose_import_paths[index]);
        return import_bga_files(history, workspace, chart, paths);
    }
    const auto referenced = referenced_bga_resources(history.document);
    std::vector<resource_id_t> unused;
    for (const auto& resource : history.document.resources)
        if (std::ranges::find(resource.roles, resource_role_t::bga) != resource.roles.end() &&
            !referenced.contains(resource.id.value)) unused.push_back(resource.id);
    if (unused.empty()) return "No unused BGA resources.";
    const auto result = apply_resource_deletions(history, action.compose_input_document_revision, unused);
    return finish_bga_edit(history, workspace, result, "Removed " + std::to_string(unused.size()) + " unused BGA resources.");
}
}

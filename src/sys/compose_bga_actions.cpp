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
struct bga_feedback_t {
    notification_severity_t severity;
    std::string message;
};
auto bga_diagnostic_feedback(const diagnostic_t& diagnostic) -> bga_feedback_t {
    const auto severity = diagnostic.severity;
    return {severity == diagnostic_severity_t::error ? notification_severity_t::error
            : severity == diagnostic_severity_t::warning ? notification_severity_t::warning
                                                         : notification_severity_t::info,
            diagnostic.message};
}
auto publish_bga_feedback(const notification_sink_t& notifications, notification_source_t source,
                          bga_feedback_t feedback) -> std::string {
    notifications.post(feedback.severity, source, feedback.message);
    return std::move(feedback.message);
}
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
                     const document_command_result_t& result, std::string message,
                     notification_severity_t success_severity = notification_severity_t::success) -> bga_feedback_t {
    if (!result.diagnostics.empty()) return bga_diagnostic_feedback(result.diagnostics.front());
    if (!result.applied) return {notification_severity_t::info, "No BGA resources changed."};
    const auto rebound = rebind_workspace(workspace, history);
    if (!rebound) return {notification_severity_t::error, "BGA changed, but workspace rebinding failed."};
    return {success_severity, std::move(message)};
}
}
auto import_bga_feedback(document_history_t& history, workspace_controller_t& workspace,
                       chart_id_t chart_id, std::span<const std::string> paths) -> bga_feedback_t {
    const auto chart = std::ranges::find(history.document.charts, chart_id, &chart_document_t::id);
    if (chart == history.document.charts.end()) return bga_feedback_t{notification_severity_t::warning, "Select a difficulty before adding images."};
    if (paths.empty() || paths.size() > WORM_UI_SOUND_DROP_CAPACITY) return bga_feedback_t{notification_severity_t::warning, "Invalid image import batch size."};
    auto candidate = make_document_history(history.document, 1);
    std::unordered_set<std::string> known;
    for (const auto& asset : history.document.visual_assets)
        known.insert(asset.source_path.lexically_normal().generic_string());
    std::vector<std::string> sorted{paths.begin(), paths.end()};
    std::ranges::sort(sorted);
    std::size_t added{}, skipped{};
    bool skipped_unreadable{};
    for (const auto& text : sorted) {
        const auto path = std::filesystem::path{text}.lexically_normal();
        const auto geometry = path.is_absolute() ? probe_image(path) : std::nullopt;
        if (!geometry || !known.insert(path.generic_string()).second) {
            skipped_unreadable = skipped_unreadable || !geometry;
            ++skipped;
            continue;
        }
        const auto result = apply_resource_asset_import(candidate,
            {.source_path = path, .role = resource_role_t::bga, .name = path.filename().string(),
             .allocate_bms_identifier = chart->source_family == format_family_t::bms,
             .image_geometry = geometry});
        if (!result.diagnostics.empty()) return bga_diagnostic_feedback(result.diagnostics.front());
        if (!result.applied) return bga_feedback_t{notification_severity_t::error, "Image import could not be prepared."};
        ++added;
    }
    if (!added) return bga_feedback_t{skipped_unreadable ? notification_severity_t::warning : notification_severity_t::info, "No images added; skipped " + std::to_string(skipped) + " unreadable or existing files."};
    const auto result = commit_document_command(history, std::move(candidate.document), "Import BGA images");
    return finish_bga_edit(history, workspace, result, "Added " + std::to_string(added) +
        " images; skipped " + std::to_string(skipped) + " unreadable or existing files.",
        skipped_unreadable ? notification_severity_t::warning : notification_severity_t::success);
}
auto dispatch_bga_feedback(const WormUiWorkspaceAction& action, document_history_t& history,
    workspace_controller_t& workspace, chart_id_t chart) -> std::optional<bga_feedback_t> {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_IMPORT_BGA_FILES &&
        action.compose_action != WORM_UI_COMPOSE_ACTION_REMOVE_UNUSED_BGA) return std::nullopt;
    if (history.document.revision != action.compose_input_document_revision)
        return bga_feedback_t{notification_severity_t::warning, wormedit::stale_action_message("BGA action")};
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_BGA_FILES) {
        if (action.compose_import_path_count > WORM_UI_SOUND_DROP_CAPACITY) return bga_feedback_t{notification_severity_t::warning, "Image import batch is too large."};
        std::vector<std::string> paths;
        for (std::size_t index = 0; index < action.compose_import_path_count; ++index)
            if (action.compose_import_paths && action.compose_import_paths[index])
                paths.emplace_back(action.compose_import_paths[index]);
        return import_bga_feedback(history, workspace, chart, paths);
    }
    const auto referenced = referenced_bga_resources(history.document);
    std::vector<resource_id_t> unused;
    for (const auto& resource : history.document.resources)
        if (std::ranges::find(resource.roles, resource_role_t::bga) != resource.roles.end() &&
            !referenced.contains(resource.id.value)) unused.push_back(resource.id);
    if (unused.empty()) return bga_feedback_t{notification_severity_t::info, "No unused BGA resources."};
    const auto result = apply_resource_deletions(history, unused);
    return finish_bga_edit(history, workspace, result, "Removed " + std::to_string(unused.size()) + " unused BGA resources.");
}
auto import_bga_files(document_history_t& history, workspace_controller_t& workspace,
                      chart_id_t chart, std::span<const std::string> paths,
                      notification_sink_t notifications) -> std::string {
    return publish_bga_feedback(notifications, notification_source_t::import_,
                                import_bga_feedback(history, workspace, chart, paths));
}
auto dispatch_compose_bga_action(const WormUiWorkspaceAction& action, document_history_t& history,
                                 workspace_controller_t& workspace, chart_id_t chart,
                                 notification_sink_t notifications) -> std::optional<std::string> {
    auto feedback = dispatch_bga_feedback(action, history, workspace, chart);
    if (!feedback) return std::nullopt;
    return publish_bga_feedback(notifications,
        action.compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_BGA_FILES
            ? notification_source_t::import_ : notification_source_t::edit,
        std::move(*feedback));
}
}

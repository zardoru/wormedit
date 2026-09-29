#include "sys/application_frame_services.h"

#include "sys/project_open.h"

#include "ui/frontend.h"
#include "wormedit/window.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace wormedit::sys {
namespace {

auto autosave_status_text(const native_project_autosave_result_t& result) -> std::string {
    using status_t = native_project_autosave_status_t;
    if (result.status == status_t::published)
        return "Autosaved revision " + std::to_string(result.revision) + ".";
    if (result.status != status_t::failed)
        return {};
    if (result.diagnostics.empty())
        return "Autosave failed; the previous recovery backup was retained.";
    const auto& diagnostic = result.diagnostics.front();
    auto message = "Autosave failed: " + diagnostic.message;
    if (!diagnostic.path.empty())
        message += " (" + diagnostic.path.generic_string() + ")";
    return message;
}

auto offer_dropped_audio_project(const SDL_Event& event, SDL_Window* window,
                                 bool offer_audio_project_drop,
                                 std::string& pending_audio_project_path,
                                 std::string& status) -> bool {
    if (!offer_audio_project_drop || event.type != SDL_EVENT_DROP_FILE || !event.drop.data ||
        (event.drop.windowID != 0 && event.drop.windowID != SDL_GetWindowID(window))) return false;
    const std::string_view dropped{event.drop.data};
    if (dropped.size() >= WORM_UI_PROJECT_PATH_CAPACITY ||
        !std::filesystem::path{dropped}.is_absolute()) {
        status = "Drop an absolute local MP3 or Ogg file within the path limit.";
        return true;
    }
    auto extension = std::filesystem::path{dropped}.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension != ".mp3" && extension != ".ogg") return false;
    pending_audio_project_path.assign(dropped);
    status = "Choose the chart layout to import this audio as a new project.";
    return true;
}

auto offer_dropped_chart_project(const SDL_Event& event, SDL_Window* window,
                                 std::string& pending_chart_project_path,
                                 std::string& status) -> bool {
    if (event.type != SDL_EVENT_DROP_FILE || !event.drop.data ||
        (event.drop.windowID != 0 && event.drop.windowID != SDL_GetWindowID(window))) return false;
    const std::filesystem::path dropped{event.drop.data};
    if (!is_supported_project_file_extension(dropped)) return false;
    if (!dropped.is_absolute() ||
        std::string_view{event.drop.data}.size() >= WORM_UI_PROJECT_PATH_CAPACITY) {
        status = "Drop an absolute local chart or project file within the path limit.";
        return true;
    }
    pending_chart_project_path = dropped.string();
    return true;
}

} // namespace

auto poll_frontend_events(WormWindow& window, metadata_drop_state_t& metadata_drop,
                          const workspace_controller_t& workspace,
                          const document_history_t& history,
                          const std::filesystem::path& asset_base,
                          bool offer_audio_project_drop,
                          std::string& pending_audio_project_path,
                          std::string& pending_chart_project_path,
                          std::string& status) -> bool {
    bool running = true;
    SDL_Event event;
    worm_ui_frontend_begin_input();
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            running = false;
        const bool project_chart_drop = offer_dropped_chart_project(
            event, window.handle, pending_chart_project_path, status);
        const bool project_audio_drop = !project_chart_drop && offer_dropped_audio_project(
            event, window.handle, offer_audio_project_drop, pending_audio_project_path, status);
        if (!project_chart_drop && !project_audio_drop)
            metadata_drop.handle_file_drop(event, window.handle, workspace, history,
                                           asset_base, status);
        worm_ui_frontend_handle_event(&event);
    }
    worm_ui_frontend_end_input();
    if (workspace.active_tab != workspace_tab_t::metadata)
        metadata_drop.clear_paths();
    return running;
}

void poll_application_jobs(runtime::metadata_asset_t& metadata_assets,
                           runtime::resource_copy_t& resource_copy,
                           runtime::output_group_export_t& output_group_export,
                           document_history_t& history,
                           workspace_controller_t& workspace,
                           const std::filesystem::path& asset_base,
                           std::string& status) {
    if (auto completed = metadata_assets.poll(history, workspace, asset_base))
        status = std::move(*completed);
    if (auto completed = resource_copy.poll(history, workspace))
        status = std::move(*completed);
    if (auto completed = output_group_export.poll(history))
        status = std::move(*completed);
}

auto compose_frame_delta_seconds(std::uint64_t& last_ticks) -> double {
    const auto now_ticks = static_cast<std::uint64_t>(SDL_GetTicks());
    const auto delta_seconds =
        now_ticks >= last_ticks ? static_cast<double>(now_ticks - last_ticks) / 1000.0 : 0.0;
    last_ticks = now_ticks;
    return delta_seconds;
}

void tick_application_autosave(native_project_autosave_loop_t& autosave_loop,
                               const document_history_t& history,
                               bool loaded_primary,
                               const std::filesystem::path& requested_primary_path,
                               const std::filesystem::path& recovery_path,
                               std::string& status) {
    const auto target = make_native_project_autosave_target(
        history.document.id, loaded_primary, requested_primary_path, recovery_path);
    if (const auto result = autosave_loop.tick(
            history.document, is_document_dirty(history),
            native_project_autosave_loop_t::clock_t::now(), target)) {
        const auto message = autosave_status_text(*result);
        if (!message.empty())
            status = message;
    }
}

void rebind_workspace_after_edit(workspace_controller_t& workspace,
                                 const document_history_t& history,
                                 std::string& status) {
    // Every document mutation must leave the next frame bound to its new
    // revision. Keep this boundary authoritative even when a command path
    // forgets its local rebind after committing.
    if (workspace.song == history.document.id &&
        (workspace.document_revision != history.document.revision ||
         workspace.history.undo_depth != history.undo.size() ||
         workspace.history.redo_depth != history.redo.size() ||
         workspace.history.dirty != is_document_dirty(history))) {
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound)
            status = rebound.diagnostics.empty()
                         ? "Workspace could not follow the latest document edit."
                         : rebound.diagnostics.front().message;
    }
}

void render_application_frame(WormWindow& window) {
    int width = 0;
    int height = 0;
    worm_window_get_size(&window, &width, &height);
    worm_ui_frontend_render(width, height);
    worm_window_swap(&window);
    SDL_Delay(1);
}

} // namespace wormedit::sys

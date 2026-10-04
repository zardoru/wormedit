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


auto offer_dropped_audio_project(const SDL_Event& event, SDL_Window* window,
                                 bool offer_audio_project_drop,
                                 std::string& pending_audio_project_path,
                                 notification_sink_t& notifications) -> bool {
    if (!offer_audio_project_drop || event.type != SDL_EVENT_DROP_FILE || !event.drop.data ||
        (event.drop.windowID != 0 && event.drop.windowID != SDL_GetWindowID(window))) return false;
    const std::string_view dropped{event.drop.data};
    if (dropped.size() >= WORM_UI_PROJECT_PATH_CAPACITY ||
        !std::filesystem::path{dropped}.is_absolute()) {
        notifications.post(notification_severity_t::warning, notification_source_t::import_, "Drop an absolute local MP3 or Ogg file within the path limit.");
        return true;
    }
    auto extension = std::filesystem::path{dropped}.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension != ".mp3" && extension != ".ogg") return false;
    pending_audio_project_path.assign(dropped);
    notifications.post(notification_severity_t::hint, notification_source_t::import_, "Choose the chart layout to import this audio as a new project.");
    return true;
}

auto offer_dropped_chart_project(const SDL_Event& event, SDL_Window* window,
                                 std::string& pending_chart_project_path,
                                 notification_sink_t& notifications) -> bool {
    if (event.type != SDL_EVENT_DROP_FILE || !event.drop.data ||
        (event.drop.windowID != 0 && event.drop.windowID != SDL_GetWindowID(window))) return false;
    const std::filesystem::path dropped{event.drop.data};
    if (!is_supported_project_file_extension(dropped)) return false;
    if (!dropped.is_absolute() ||
        std::string_view{event.drop.data}.size() >= WORM_UI_PROJECT_PATH_CAPACITY) {
        notifications.post(notification_severity_t::warning, notification_source_t::import_, "Drop an absolute local chart or project file within the path limit.");
        return true;
    }
    pending_chart_project_path = dropped.string();
    return true;
}

auto collect_dropped_sound(const SDL_Event& event, SDL_Window* window,
                           const workspace_controller_t& workspace,
                           const document_history_t& history,
                           sound_drop_transaction_t& transaction,
                           std::vector<sound_drop_batch_t>& batches) -> bool {
    WormUiKeysoundDockRequest dock{};
    worm_ui_frontend_keysound_dock_request(&dock);
    const sound_drop_origin_t origin{
        .project_id = history.document.id.value,
        .chart_id = workspace.selected_chart ? workspace.selected_chart->value : 0,
        .revision = history.document.revision,
        .tab = static_cast<WormUiWorkspaceTab>(workspace.active_tab),
        .window_id = SDL_GetWindowID(window),
        .slots_mode = workspace.active_tab == workspace_tab_t::compose && dock.slots_mode,
        .first_slot = dock.selected_slot};
    const bool drop_event = event.type == SDL_EVENT_DROP_BEGIN ||
        event.type == SDL_EVENT_DROP_POSITION || event.type == SDL_EVENT_DROP_FILE ||
        event.type == SDL_EVENT_DROP_COMPLETE;
    const auto over_list = drop_event &&
        worm_ui_frontend_sound_list_contains(event.drop.windowID, event.drop.x, event.drop.y);
    auto result = transaction.handle(event, origin, over_list);
    if (result.completed) batches.push_back(std::move(*result.completed));
    return result.consumed;
}

auto collect_dropped_bga(const SDL_Event& event, SDL_Window* window,
                         const workspace_controller_t& workspace,
                         std::vector<std::string>& paths, notification_sink_t& notifications) -> bool {
    if (event.type != SDL_EVENT_DROP_FILE || !event.drop.data ||
        workspace.active_tab != workspace_tab_t::compose ||
        (event.drop.windowID && event.drop.windowID != SDL_GetWindowID(window)) ||
        !worm_ui_frontend_bga_list_contains(event.drop.windowID, event.drop.x, event.drop.y)) return false;
    const std::string_view path{event.drop.data};
    if (path.size() >= WORM_UI_PROJECT_PATH_CAPACITY || !std::filesystem::path{path}.is_absolute())
        notifications.post(notification_severity_t::warning, notification_source_t::import_, "Drop absolute local image files within the path limit.");
    else if (paths.size() < WORM_UI_SOUND_DROP_CAPACITY) paths.emplace_back(path);
    else notifications.post(notification_severity_t::warning, notification_source_t::import_, "Image drop batch is full.");
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
                          sound_drop_transaction_t& sound_drop_transaction,
                          std::vector<sound_drop_batch_t>& pending_sound_batches,
                          std::vector<std::string>& pending_bga_paths,
                          notification_sink_t& notifications) -> bool {
    bool running = true;
    SDL_Event event;
    worm_ui_frontend_begin_input();
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            running = false;
        const bool sound_drop = collect_dropped_sound(event, window.handle, workspace,
                                                      history, sound_drop_transaction, pending_sound_batches);
        const bool bga_drop = !sound_drop && collect_dropped_bga(event, window.handle, workspace,
                                                              pending_bga_paths, notifications);
        const bool project_chart_drop = !bga_drop && !sound_drop && offer_dropped_chart_project(
            event, window.handle, pending_chart_project_path, notifications);
        const bool project_audio_drop = !bga_drop && !sound_drop && !project_chart_drop &&
            offer_dropped_audio_project(
            event, window.handle, offer_audio_project_drop, pending_audio_project_path, notifications);
        if (!bga_drop && !sound_drop && !project_chart_drop && !project_audio_drop)
            metadata_drop.handle_file_drop(event, window.handle, workspace, history,
                                           asset_base, notifications);
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
    // Metadata runtime consumes and posts each terminal outcome itself.
    static_cast<void>(metadata_assets.poll(history, workspace, asset_base));
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
                               notification_sink_t& notifications) {
    const auto target = make_native_project_autosave_target(
        history.document.id, loaded_primary, requested_primary_path, recovery_path);
    if (const auto result = autosave_loop.tick(
            history.document, is_document_dirty(history),
            native_project_autosave_loop_t::clock_t::now(), target)) {
        if (result->status == native_project_autosave_status_t::published)
            notifications.post(notification_severity_t::success, notification_source_t::recovery,
                "Autosaved revision " + std::to_string(result->revision) + ".");
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

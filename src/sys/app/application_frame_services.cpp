#include "core/text_util.h"
#include "sys/app/application_frame_services.h"
#include "sys/app/application_edit_inputs.h"

#include "sys/project/project_open.h"
#include "sys/resources/resource_drop_router.h"
#include "sys/app/frame_pacing.h"

#include "ui/frontend.h"
#include "wormedit/window.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
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
    if (dropped.size() > maximum_project_path_bytes ||
        !std::filesystem::path{dropped}.is_absolute()) {
        notifications.post(notification_severity_t::warning, notification_source_t::import_, "Drop an absolute local MP3 or Ogg file within the path limit.");
        return true;
    }
    const auto extension = text_util::lowercase_extension(std::filesystem::path{dropped});
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
        std::string_view{event.drop.data}.size() > maximum_project_path_bytes) {
        notifications.post(notification_severity_t::warning, notification_source_t::import_, "Drop an absolute local chart or project file within the path limit.");
        return true;
    }
    pending_chart_project_path = dropped.string();
    return true;
}

auto collect_dropped_resources(const SDL_Event& event, SDL_Window* window,
                           const workspace_controller_t& workspace,
                           const document_history_t& history,
                           sound_drop_transaction_t& transaction,
                           std::vector<sound_drop_batch_t>& batches) -> bool {
    if (!is_resource_drop_event(event)) return false;
    wormedit::ui::keysound_dock_request_t dock{};
    worm_ui_frontend_keysound_dock_request(&dock);
    const sound_drop_origin_t origin{
        .project_id = history.document.id.value,
        .chart_id = workspace.selected_chart ? workspace.selected_chart->value : 0,
        .revision = history.document.revision,
        .tab = static_cast<WormUiWorkspaceTab>(workspace.active_tab),
        .window_id = SDL_GetWindowID(window),
        .slots_mode = workspace.active_tab == workspace_tab_t::compose && dock.slots_mode,
        .first_slot = dock.selected_slot,
        .slot_explicit = dock.selected_slot_explicit};
    auto result = handle_resource_drop(event, origin, transaction);
    if (result.completed) batches.push_back(std::move(*result.completed));
    return result.consumed;
}


} // namespace

auto poll_frontend_events(WormWindow& window, const frontend_event_inputs_t& environment,
                          bool offer_audio_project_drop, std::string& pending_chart_project_path,
                          sound_drop_transaction_t& sound_drop_transaction,
                          std::vector<sound_drop_batch_t>& pending_resource_batches) -> bool {
    auto& metadata_drop = environment.metadata_drop;
    auto& workspace = environment.document.workspace;
    auto& history = environment.document.history;
    auto& asset_base = environment.project.asset_base;
    auto& notifications = environment.document.notifications;
    auto& pending_audio_project_path = environment.project.pending_import.audio_project_path;

    bool running = true;
    SDL_Event event;
    worm_ui_frontend_begin_input();
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            running = false;
        if (event.type == SDL_EVENT_CLIPBOARD_UPDATE) environment.system_clipboard_changed = true;
        const bool sound_drop = collect_dropped_resources(event, window.handle, workspace,
                                                      history, sound_drop_transaction, pending_resource_batches);
        const bool project_chart_drop = !sound_drop && offer_dropped_chart_project(
            event, window.handle, pending_chart_project_path, notifications);
        const bool project_audio_drop = !sound_drop && !project_chart_drop &&
            offer_dropped_audio_project(
            event, window.handle, offer_audio_project_drop, pending_audio_project_path, notifications);
        if (!sound_drop && !project_chart_drop && !project_audio_drop)
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
                           runtime::preview_audio_export_t& preview_audio,
                           document_history_t& history,
                           workspace_controller_t& workspace,
                           const std::filesystem::path& asset_base) {
    // Metadata runtime consumes and posts each terminal outcome itself.
    static_cast<void>(metadata_assets.poll(history, workspace, asset_base));
    static_cast<void>(resource_copy.poll(history, workspace));
    static_cast<void>(preview_audio.poll(history, workspace));
    static_cast<void>(output_group_export.poll(history));
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
                               notification_sink_t& notifications,
                               const std::filesystem::path& asset_base) {
    auto target = native_project_autosave_target_t::from(
        history.document.id, loaded_primary, requested_primary_path, recovery_path);
    target.asset_base = asset_base;
    if (const auto result = autosave_loop.tick(
            history.document, is_document_dirty(history),
            native_project_autosave_loop_t::clock_t::now(), target)) {
        if (result->value.status == native_project_autosave_status_t::published)
            notifications.post(notification_severity_t::success, notification_source_t::recovery,
                "Autosaved revision " + std::to_string(result->value.revision) + ".");
    }
}

void rebind_workspace_after_edit(workspace_controller_t& workspace, document_history_t& history,
                                 notification_sink_t notifications,
                                 workspace_edit_feedback_t& feedback) {
    // Every document mutation must leave the next frame bound to its new
    // revision. Keep this boundary authoritative even when a command path
    // forgets its local rebind after committing.
    feedback.rebind(workspace, history, notifications);
}

void render_application_frame(WormWindow& window, std::uint64_t frame_start_ns) {
    int width = 0;
    int height = 0;
    worm_window_get_size(&window, &width, &height);
    worm_ui_frontend_render(width, height);
    worm_window_swap(&window);
    const auto flags = window.handle ? SDL_GetWindowFlags(window.handle) : SDL_WindowFlags{0};
    const auto inactive = (flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED |
                                    SDL_WINDOW_OCCLUDED)) != 0;
    const auto remainder = frame_pacing_remainder_ns(frame_start_ns, SDL_GetTicksNS(), inactive);
    if (remainder != 0) SDL_DelayPrecise(remainder);
}

} // namespace wormedit::sys

#pragma once
#include "bga_usage.h"
#include "generator.h"
#include "bms_program.h"
#include <wormedit/core/metadata_inheritance_types.h>
#include "timing_links.h"
#include "metadata_edit.h"
#include "timing_edit.h"
#include "sound_requests.h"
#include "compose_request.h"
#include "compose_window.h"
#include "frame_input.h"
#include "session_requests.h"
#include "audio_slicer.h"
#include <wormedit/core/timing_edit_policy.h>
#include <wormedit/core/preview_mix.h>
#include "workspace_types.h"
#include "file_jobs.h"
#include "lane_keys.h"
#include "options_notifications.h"
#include "sound_import.h"
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>



typedef enum WormUiClipboardAction {
    WORM_UI_CLIPBOARD_NONE = 0,
    WORM_UI_CLIPBOARD_COPY,
    WORM_UI_CLIPBOARD_CUT,
    WORM_UI_CLIPBOARD_PASTE,
} WormUiClipboardAction;



namespace wormedit::ui {
struct workspace_action_t {
    timing_link_action_t timing_link;
    // Everything Compose can request this frame: the kind, and the payload of its family.
    compose_request_t compose;
    wormedit::ui::file_jobs_action_t file_jobs;
    std::string menu_command_id;
    WormUiClipboardAction clipboard;
    wormedit::ui::notification_action_t notifications;
    wormedit::ui::options_action_t options;
    wormedit::ui::lane_keys_action_t lane_keys;
    wormedit::ui::project_chart_action_t project_chart;
    wormedit::ui::project_settings_action_t project_settings;
    wormedit::ui::project_output_group_action_t project_output_group;
    wormedit::ui::chart_import_action_t chart_import;
    wormedit::ui::tap_timing_action_t tap_timing;
    wormedit::ui::timing_settings_action_t timing_settings;
    // The complete next playback mix requested by the Mixer dock.
    std::optional<wormedit::preview_mix_t> preview_mix;
    WormUiWorkspaceActionKind kind;
    struct wormedit::ui::external_player_action_t external_player;
    project_open_request_t project_open;
    close_request_t close;
    recovery_request_t recovery;
    transport_request_t transport;
    asset_selection_request_t asset_selection;
    audio_offset_drag_request_t audio_offset_drag;
    compare_request_t compare;
    WormUiWorkspaceTab tab;
    std::optional<audio_slicer_action_t> audio_slicer;
    wormedit::ui::bga_usage_activation_t bga_usage;
    wormedit::ui::sound_usage_locate_request_t sound_usage;
    wormedit::player_preview_skin_t player_preview_skin;
    // Metadata payloads own their text and lists across frontend calls.
    metadata_edit_request_t metadata;
    // Input sampled this frame (keyboard, pointer, hover); not requests.
    frame_input_t input;
    wormedit::ui::request_stamp_t stamp;
    wormedit::ui::sound_properties_request_t sound_properties;
    std::optional<wormedit::ui::preview_audio_export_action_t> preview_audio_export;
    wormedit::ui::sound_import_request_t sound_import;

    wormedit::ui::timing_edit_request_t timing;
    bool command_menu_invoked;
    std::string command_menu_command_id;
};
/* The slice of a frame that sys/compose dispatches. */
[[nodiscard]] inline auto compose_window_of(const workspace_action_t& action) -> compose_window_t {
    return {action.compose, action.stamp, action.sound_import, action.sound_properties};
}
[[nodiscard]] inline auto compose_input_window_of(workspace_action_t& action) -> compose_input_window_t {
    return {action.input, action.compose, action.stamp, action.audio_offset_drag};
}
} // namespace wormedit::ui

// Private application/frontend boundary. Implementations own all widget
// toolkit state and translate semantic views into typed actions.
bool worm_ui_frontend_init(SDL_Window* window, SDL_Renderer* renderer);
// Select a loaded field palette; empty or unknown IDs use Wormedit. Chrome is unaffected.
bool worm_ui_frontend_select_field_scheme(const char* id);
// Resolve a layout override, the default, then Wormedit against semantic compatibility.
bool worm_ui_frontend_configure_field_scheme(const char* requested, const char* fallback,
    const wormedit::ui::compose_lane_view_t* lanes, size_t count);
// Core stage none/left/right; -1 uses chart layout, 0 left, 1 right.
int worm_ui_frontend_field_scratch_side(uint8_t stage);
void worm_ui_frontend_set_snap_colored_grid(bool enabled);
void worm_ui_frontend_set_snap_grid_opacity(uint32_t percent);
void worm_ui_frontend_set_field_waveform_opacity(uint32_t percent);
// Applies accepted program settings; theme errors leave the previous palette untouched.
std::string worm_ui_frontend_apply_display_settings(std::string_view theme, uint32_t scale_percent,
                                                  bool timing_overlay);
bool worm_ui_frontend_set_interface_scale(uint32_t percent);
// The scale in use: the requested scale, capped so the window keeps the minimum layout.
uint32_t worm_ui_frontend_interface_scale_percent(void);
bool worm_ui_frontend_is_title_bar_drag_position(float x, float y, int window_width);
// Initialize the same event/action frontend without a window or renderer for integration tests.
bool worm_ui_frontend_init_headless(int width, int height);
// Actual in-window chrome height; headless initialization keeps the fallback layout.
int worm_ui_frontend_title_bar_height(void);
// Called by frontend producers on the UI thread; the callback copies text before
// returning and must not throw. Shutdown clears the non-owning callback.
void worm_ui_frontend_set_notification_sink(void* context, WormUiNotificationPostCallback callback);
void worm_ui_frontend_notify(WormUiNotificationSeverity severity,
    WormUiNotificationProducerSource source, const char* message);
// Exercise the real dialog callback handoff without opening a native picker.
uint64_t worm_ui_frontend_begin_project_dialog_for_testing(WormUiWorkspaceActionKind kind);
void worm_ui_frontend_project_dialog_callback_for_testing(uint64_t generation,
    const char* const* files);
wormedit::ui::workspace_action_t worm_ui_frontend_consume_project_dialog_for_testing(void);
// Read queued draw text before render clears it; returns required bytes (no terminator).
size_t worm_ui_frontend_copy_drawn_text_for_testing(char* output, size_t capacity);

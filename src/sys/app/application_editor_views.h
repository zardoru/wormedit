#pragma once

#include "sys/audio/inline_preview_cursor.h"
#include "sys/audio/player_preview_presenter.h"
#include "sys/audio/tap_timing_runtime.h"
#include "sys/compose/views/compose_view.h"
#include "sys/metadata/metadata_drop_state.h"
#include "sys/notifications/notification_history_runtime.h"
#include "sys/project/chart_import_session.h"
#include "sys/project/compare_view.h"
#include "sys/project/project_save_paths.h"
#include "sys/project/project_view.h"
#include "sys/project/workspace_edit_feedback.h"
#include "sys/resources/bga_dock_runtime.h"
#include "sys/resources/sound_drop_transaction.h"
#include "sys/timing/timing_edit_runtime.h"
#include "sys/timing/timing_view.h"

#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/editor_navigation_input.h>
#include <wormedit/core/viewport_input.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::sys {

enum class close_target_t { none, application, project, new_project, open_file };
struct pending_close_t {
    close_target_t target{close_target_t::none};
    wormedit::song_id_t project;
    std::uint64_t revision{};
    bool save_requested{};
    std::string project_path;
    std::string layout_id;
    wormedit::text_encoding_t encoding_id{};
    std::string bms_lane_profile_id;
    bool awaiting_encoding{};
    // Shown in the confirmation only when a save started from it failed.
    std::string save_failure;
};

struct editor_views_t {
    // Captured edits, project scenes and drafts all start fresh on replacement.
    // The whole Tap assistant and chart-import dialog belong to the target project.
    struct project_t {
        std::optional<proposal_t<audio_offset_drag_t>> audio_offset_drag{};
        std::optional<workspace_tab_t> audio_offset_drag_tab{};
        timing_scene_cache_t timing_scene_cache{};
        runtime::timing_edit_t timing_edit{};
        compose_scene_cache_t compose_scene_cache{};
        std::optional<editor_interaction_state_t> compose_interaction_state{};
        chart_import_session_t chart_import{};
        tap_timing_runtime_t tap_runtime{};
        timing_grid_state_t timing_grid{};
        metadata_drop_state_t metadata_drop{};
    } project;

    // Application feedback, queued delivery and retained service/cache owners.
    // Project-aware caches keep their existing session/stamp invalidation.
    std::string close_feedback{};
    workspace_edit_feedback_t workspace_edit_feedback{};
    projection_notification_feedback_t projection_feedback{}, workspace_view_feedback{};
    notification_history_runtime_t notification_history{};
    wormedit::ui::notification_action_t pending_notification{};
    sound_drop_transaction_t sound_drop_transaction{};
    std::vector<sound_drop_batch_t> pending_resource_batches{};
    bga_dock_runtime_t bga_dock_runtime{};
    compare_scene_cache_t compare_scene_cache{};
    frontend_workspace_view_t workspace_frame{};
    frontend_workspace_view_t compare_first_frame{};
    frontend_workspace_view_t compare_second_frame{};
    player_preview_presenter_t player_preview_presenter{};
    viewport_input_controller_t compose_viewport_input{};
    editor_navigation_input_controller_t compose_navigation_input{};
    // Its generation survives replacement and advances before resetting views.
    project_chart_runtime_t project_chart_runtime{};
    pending_close_t pending_close{};
    // Owner policy retains both the playback anchor and follow fraction.
    inline_preview_cursor_t inline_preview_cursor{};
    std::optional<tap_timing_assist_publication_t> published_assist{};
    std::optional<std::uint64_t> last_tap_event_ns{};
    std::uint64_t tap_epoch{}, tap_epoch_begin_ns{};
    std::function<tap_timing_clock_t()> tap_clock;
    // Starts true so the first frame reads the clipboard the application started with.
    bool system_clipboard_changed{true};
    std::uint64_t compose_viewport_last_ticks{};
    project_portability_notice_t portability_notice{};

    void begin_project_session() {
        project = {};
        // Cancel captures while retaining application input settings.
        compose_viewport_input.escape();
        compose_navigation_input.cancel();
    }
};

} // namespace wormedit::sys

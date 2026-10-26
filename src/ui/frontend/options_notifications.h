#pragma once
#include "timing_preview.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace wormedit {
struct song_document_t;
class notification_center_t;
enum class move_collision_policy_t : std::uint32_t;
namespace sys {
struct options_runtime_t;
struct notification_history_runtime_t;
}
} // namespace wormedit


typedef enum WormUiOptionsCategory {
    WORM_UI_OPTIONS_GENERAL = 0, WORM_UI_OPTIONS_EDITOR, WORM_UI_OPTIONS_AUDIO,
    WORM_UI_OPTIONS_DISPLAY, WORM_UI_OPTIONS_INPUT, WORM_UI_OPTIONS_PLAYERS,
    WORM_UI_OPTIONS_CATEGORY_COUNT
} WormUiOptionsCategory;
typedef enum WormUiOptionsField {
    WORM_UI_OPTIONS_FIELD_AUTOSAVE, WORM_UI_OPTIONS_FIELD_RECENT_FILES,
    WORM_UI_OPTIONS_FIELD_SNAP, WORM_UI_OPTIONS_FIELD_RATE,
    WORM_UI_OPTIONS_FIELD_THEME, WORM_UI_OPTIONS_FIELD_SCALE,
    WORM_UI_OPTIONS_FIELD_LAYOUT, WORM_UI_OPTIONS_FIELD_SHORTCUT_PATH,
    WORM_UI_OPTIONS_FIELD_PLAYERS, WORM_UI_OPTIONS_FIELD_PAGE,
    WORM_UI_OPTIONS_FIELD_CONFIRM_CLOSE, WORM_UI_OPTIONS_FIELD_MINIMAP_CLICK,
    WORM_UI_OPTIONS_FIELD_KEEP_PITCH, WORM_UI_OPTIONS_FIELD_KEYSOUND_MEMORY, WORM_UI_OPTIONS_FIELD_TIMING_OVERLAY,
    WORM_UI_OPTIONS_FIELD_PLAYER_NAME, WORM_UI_OPTIONS_FIELD_PLAYER_PATH,
    WORM_UI_OPTIONS_FIELD_PLAYER_START, WORM_UI_OPTIONS_FIELD_PLAYER_HERE,
    WORM_UI_OPTIONS_FIELD_PLAYER_STOP, WORM_UI_OPTIONS_FIELD_PLAYER_SELECTED,
    WORM_UI_OPTIONS_FIELD_PLAYER_ADD, WORM_UI_OPTIONS_FIELD_PLAYER_REMOVE,
    WORM_UI_OPTIONS_FIELD_SCROLL_DIRECTION, WORM_UI_OPTIONS_FIELD_ANCHOR,
    WORM_UI_OPTIONS_FIELD_NOTE_PALETTE, WORM_UI_OPTIONS_FIELD_LANE_LEGEND,
    WORM_UI_OPTIONS_FIELD_AREA_ALPHA, WORM_UI_OPTIONS_FIELD_AUDITION_ON_CLICK,
    WORM_UI_OPTIONS_FIELD_MOVE_COLLISION_POLICY, WORM_UI_OPTIONS_FIELD_FIELD_SCHEME,
    WORM_UI_OPTIONS_FIELD_SNAP_COLORED_GRID, WORM_UI_OPTIONS_FIELD_SNAP_GRID_OPACITY,
    WORM_UI_OPTIONS_FIELD_INVERT_SCROLL_WHEEL, WORM_UI_OPTIONS_FIELD_FIELD_WAVEFORM_OPACITY,
    WORM_UI_OPTIONS_FIELD_P1_SCRATCH_POSITION, WORM_UI_OPTIONS_FIELD_P2_SCRATCH_POSITION,
    WORM_UI_OPTIONS_FIELD_LAYOUT_FIELD_SCHEME,
    WORM_UI_OPTIONS_FIELD_OSU_SKIN_DIRECTORY,
    WORM_UI_OPTIONS_FIELD_OUTPUT_DEVICE, WORM_UI_OPTIONS_FIELD_BUFFER_FRAMES,
    WORM_UI_OPTIONS_FIELD_METRONOME_VOLUME, WORM_UI_OPTIONS_FIELD_ACCENT_VOLUME,
    WORM_UI_OPTIONS_FIELD_COUNT_IN, WORM_UI_OPTIONS_FIELD_AUDITION_VOLUME,
    WORM_UI_OPTIONS_FIELD_PREVIEW_LATENCY, WORM_UI_OPTIONS_FIELD_REDUCED_MOTION,
    WORM_UI_OPTIONS_FIELD_DEFAULT_LANE_KEY, WORM_UI_OPTIONS_FIELD_DEFAULT_LANE_NUMBER_ROW,
    WORM_UI_OPTIONS_FIELD_DEFAULT_LANE_DFJK
} WormUiOptionsField;
namespace wormedit::ui {
struct options_diagnostic_view_t {
    WormUiOptionsField field{};
    std::string_view message;
    auto operator==(const options_diagnostic_view_t&) const -> bool = default;
};
} // namespace wormedit::ui
// Input & shortcuts rows: one per registry command, at most the registry's command cap.
namespace wormedit::ui {
struct shortcut_row_view_t {
    std::string_view command_id, label, chord;
    bool conflict{};
    auto operator==(const shortcut_row_view_t&) const -> bool = default;
};
struct lane_key_choice_view_t {
    std::uint16_t usage{};
    std::string label;
};
} // namespace wormedit::ui
namespace wormedit::ui {
struct options_view_t {
    bool visible{};
    std::uint64_t revision{};
    WormUiOptionsCategory category{};
    bool changed{}, can_apply{}, confirm_discard{};
    std::array<bool, WORM_UI_OPTIONS_CATEGORY_COUNT> changed_categories{};
    std::uint32_t autosave_minutes{}, recent_files_count{}, default_snap{}, ui_scale_percent{};
    std::array<std::uint32_t, 6> scroll_directions{};
    std::uint32_t playhead_anchor_percent{}, area_band_alpha{};
    wormedit::move_collision_policy_t move_collision_policy{};
    bool note_kind_palette_visible{}, lane_key_legend_visible{}, audition_on_click{}, invert_scroll_wheel{};
    bool confirm_unsaved_close{}, keep_pitch{}, frame_timing_overlay{}, snap_colored_grid{};
    bool reduced_motion{};
    std::uint32_t snap_grid_opacity_percent{}, field_waveform_opacity_percent{};
    bool minimap_click_moves_paused_playhead{};
    std::uint32_t keysound_budget_mib{};
    std::string_view osu_skin_directory;
    std::uint64_t rate_numerator{}, rate_denominator{};
    std::string_view output_device;
    std::uint32_t buffer_frames{}, metronome_volume{100}, accent_volume{100}, count_in_measures{}, audition_volume{100};
    std::int32_t preview_latency_ms{};
    std::vector<std::string> audio_devices;
    bool audio_output_pending{};
    std::uint32_t p1_scratch_position{}, p2_scratch_position{};
    std::string_view field_scheme, current_field_layout_key, current_field_layout_name;
    std::string_view current_layout_field_scheme;
    std::vector<wormedit::ui::compose_lane_view_t> current_field_layout_lanes;
    std::string_view theme, shortcut_profile_path;
    std::vector<external_player_definition_view_t> players;
    std::size_t selected_player{};
    std::vector<options_diagnostic_view_t> diagnostics;
    std::string_view status;
    std::vector<shortcut_row_view_t> shortcut_rows;
    bool shortcut_capturing{};
    std::string_view shortcut_capture_command_id;
    std::uint32_t default_lane_count{4};
    std::vector<std::uint16_t> default_lane_bindings;
    std::vector<lane_key_choice_view_t> lane_key_choices;

    void reset() {
        auto lanes = std::move(current_field_layout_lanes);
        auto retained_players = std::move(players);
        auto retained_diagnostics = std::move(diagnostics);
        auto shortcuts = std::move(shortcut_rows);
        auto devices = std::move(audio_devices);
        auto bindings = std::move(default_lane_bindings);
        auto keys = std::move(lane_key_choices);
        *this = {};
        current_field_layout_lanes = std::move(lanes);
        players = std::move(retained_players);
        diagnostics = std::move(retained_diagnostics);
        shortcut_rows = std::move(shortcuts);
        audio_devices = std::move(devices);
        default_lane_bindings = std::move(bindings);
        lane_key_choices = std::move(keys);
        current_field_layout_lanes.clear();
        players.clear();
        diagnostics.clear();
        shortcut_rows.clear();
        audio_devices.clear();
        default_lane_bindings.clear();
        lane_key_choices.clear();
    }
    void refill(wormedit::sys::options_runtime_t&);
    [[nodiscard]] static auto from(wormedit::sys::options_runtime_t&) -> options_view_t;
};
} // namespace wormedit::ui
typedef enum WormUiOptionsActionKind {
    WORM_UI_OPTIONS_NONE = 0, WORM_UI_OPTIONS_OPEN, WORM_UI_OPTIONS_CLOSE,
    WORM_UI_OPTIONS_SELECT_CATEGORY, WORM_UI_OPTIONS_APPLY,
    WORM_UI_OPTIONS_REVERT, WORM_UI_OPTIONS_DEFAULTS, WORM_UI_OPTIONS_EDIT_FIELD,
    WORM_UI_OPTIONS_KEEP_EDITING, WORM_UI_OPTIONS_DISCARD_AND_CLOSE,
    WORM_UI_OPTIONS_SHORTCUT_CAPTURE, WORM_UI_OPTIONS_SHORTCUT_CANCEL,
    WORM_UI_OPTIONS_REFRESH_AUDIO_DEVICES, WORM_UI_OPTIONS_SELECT_LANE_COUNT
} WormUiOptionsActionKind;
// EDIT_FIELD payload is consumed immediately; strings are bounded and owned.
namespace wormedit::ui {
struct options_action_t {
    WormUiOptionsActionKind kind{};
    std::uint64_t revision{};
    WormUiOptionsCategory category{};
    WormUiOptionsField field{};
    std::uint64_t integer{}, denominator{};
    std::int64_t signed_integer{};
    bool boolean{};
    std::uint32_t player_index{}, family_index{};
    std::uint32_t lane_count{}, lane_index{};
    std::string shortcut_command_id;
    std::string text;
    auto operator==(const options_action_t&) const -> bool = default;
};
} // namespace wormedit::ui


typedef enum WormUiNotificationSeverity {
    WORM_UI_NOTIFICATION_HINT = 0,
    WORM_UI_NOTIFICATION_INFO,
    WORM_UI_NOTIFICATION_SUCCESS,
    WORM_UI_NOTIFICATION_WARNING,
    WORM_UI_NOTIFICATION_ERROR
} WormUiNotificationSeverity;

typedef enum WormUiNotificationProducerSource {
    WORM_UI_NOTIFICATION_PRODUCER_INPUT,
    WORM_UI_NOTIFICATION_PRODUCER_SYSTEM
} WormUiNotificationProducerSource;

typedef void (*WormUiNotificationPostCallback)(void* context, WormUiNotificationSeverity severity,
    WormUiNotificationProducerSource source, const char* message);

namespace wormedit::ui {
struct notification_toast_view_t {
    std::uint64_t id{};
    wormedit::notification_severity_t severity{};
    std::string_view message;
    std::uint32_t repeat_count{};
    auto operator==(const notification_toast_view_t&) const -> bool = default;
};
} // namespace wormedit::ui


typedef enum WormUiNotificationTargetAvailability {
    WORM_UI_NOTIFICATION_NO_TARGET = 0,
    WORM_UI_NOTIFICATION_TARGET_POSITION,
    WORM_UI_NOTIFICATION_TARGET_NONPOSITIONAL,
    WORM_UI_NOTIFICATION_TARGET_MISSING
} WormUiNotificationTargetAvailability;

namespace wormedit::ui {
struct notification_history_row_view_t {
    std::uint64_t id{};
    wormedit::notification_severity_t severity{};
    wormedit::notification_source_t source{};
    std::string_view message, detail;
    std::uint32_t repeat_count{};
    std::uint64_t age_seconds{};
    bool read{};
    WormUiNotificationTargetAvailability target{};
    auto operator==(const notification_history_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct notification_history_view_t {
    bool visible{};
    std::vector<notification_history_row_view_t> rows;

    void reset() {
        visible = false;
        rows.clear();
    }
    void refill(wormedit::sys::notification_history_runtime_t&, const wormedit::notification_center_t&,
                const wormedit::song_document_t&, std::chrono::nanoseconds now,
                std::uint64_t document_generation = 0);
    [[nodiscard]] static auto from(wormedit::sys::notification_history_runtime_t&,
                const wormedit::notification_center_t&, const wormedit::song_document_t&,
                std::chrono::nanoseconds now, std::uint64_t document_generation = 0)
                -> notification_history_view_t;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct notification_view_t {
    static constexpr std::size_t maximum_visible_toasts = 3;
    bool edit_gesture_active{}, history_available{};
    notification_history_view_t history;
    std::string_view current_text;
    wormedit::notification_severity_t current_severity{};
    std::size_t unread_warnings{}, unread_errors{};
    std::vector<notification_toast_view_t> toasts;

    void reset() {
        auto retained_history = std::move(history);
        auto retained_toasts = std::move(toasts);
        *this = {};
        history = std::move(retained_history);
        toasts = std::move(retained_toasts);
        history.reset();
        toasts.clear();
    }
    void refill(const wormedit::notification_center_t&, std::chrono::nanoseconds now);
    [[nodiscard]] static auto from(const wormedit::notification_center_t&,
                std::chrono::nanoseconds now) -> notification_view_t;
};
} // namespace wormedit::ui

typedef enum WormUiNotificationActionKind {
    WORM_UI_NOTIFICATION_ACTION_NONE = 0,
    WORM_UI_NOTIFICATION_ACTION_DISMISS,
    WORM_UI_NOTIFICATION_ACTION_OPEN_HISTORY,
    WORM_UI_NOTIFICATION_ACTION_CLOSE_HISTORY,
    WORM_UI_NOTIFICATION_ACTION_COPY_DETAILS,
    WORM_UI_NOTIFICATION_ACTION_GO_TO,
    WORM_UI_NOTIFICATION_ACTION_DISMISS_ALL
} WormUiNotificationActionKind;

namespace wormedit::ui {
struct notification_action_t {
    WormUiNotificationActionKind kind{};
    std::uint64_t id{};
    auto operator==(const notification_action_t&) const -> bool = default;
};
} // namespace wormedit::ui

#pragma once
#include "file_jobs.h"
#include <wormedit/core/vocabulary.h>
#include "lane_keys.h"
#include "options_notifications.h"
#include "catalog.h"
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <span>
#include <string_view>
#include <vector>

enum class new_project_lane_role_t {
    playable,
    scratch,
    autoplay,
    invisible,
    mine,
    free_zone,
    foot_pedal,
};

struct new_project_layout_entry_view_t {
    std::string_view id;
    std::string_view label;
    std::string_view game;
    std::string_view lane_shape;
    std::string_view variant;
    std::vector<new_project_lane_role_t> preview_lane_roles;
};

struct new_project_layout_catalog_view_t {
    std::span<const new_project_layout_entry_view_t> entries;

    [[nodiscard]] auto find(std::string_view id) const -> const new_project_layout_entry_view_t* {
        for (const auto& entry : entries)
            if (entry.id == id)
                return &entry;
        return nullptr;
    }
};

typedef enum WormUiCloseDecision {
    WORM_UI_CLOSE_NONE = 0, WORM_UI_CLOSE_CANCEL, WORM_UI_CLOSE_DISCARD, WORM_UI_CLOSE_SAVE
} WormUiCloseDecision;
namespace wormedit::ui {

struct close_confirmation_view_t {
    bool visible, application, has_native_path;
    wormedit::song_id_t project_id;
    uint64_t revision;
    std::string_view status_text;

    auto operator==(const close_confirmation_view_t&) const -> bool = default;
};
struct compare_chart_view_t {
    wormedit::chart_id_t id;
    std::string_view label;

    auto operator==(const compare_chart_view_t&) const -> bool = default;
};
struct compare_diagnostic_view_t {
    std::string_view label;
    uint64_t row{};
    wormedit::measure_id_t measure;
    bool has_row{};

    auto operator==(const compare_diagnostic_view_t&) const -> bool = default;
};
struct compare_view_t {
    bool visible{};
    bool has_two_difficulties{};
    wormedit::chart_id_t first_chart_id, second_chart_id;
    std::string_view first_difficulty, second_difficulty;
    const wormedit::ui::compose_view_t* first_pane{};
    const wormedit::ui::compose_view_t* second_pane{};
    std::vector<compare_chart_view_t> charts;
    double scroll_pixels{};
    std::vector<compare_diagnostic_view_t> diagnostics;
    wormedit::chart_id_t diagnostic_chart_id;

    auto operator==(const compare_view_t&) const -> bool = default;
};

struct menu_command_view_t {
    std::string_view id;
    std::string_view label;
    std::string_view shortcut;
    bool enabled;
    bool checked;

    auto operator==(const menu_command_view_t&) const -> bool = default;
};

struct recent_file_view_t {
    std::string_view label;
    std::string_view path;

    auto operator==(const recent_file_view_t&) const -> bool = default;
};

struct command_menu_row_view_t {
    std::string_view id;
    std::string_view label;
    std::string_view shortcut; /* platform chord text; empty when the command has no key */
    bool enabled;

    auto operator==(const command_menu_row_view_t&) const -> bool = default;
};

struct workspace_view_t {
    wormedit::ui::file_jobs_view_t file_jobs;
    new_project_layout_catalog_view_t new_project_layouts;
    wormedit::ui::kind_catalog_view_t<wormedit::audio::playback_rate_id_t> playback_rates;
    wormedit::ui::kind_catalog_view_t<wormedit::text_encoding_t> bms_encodings;
    wormedit::audio::playback_rate_id_t transport_rate_id{};
    std::vector<recent_file_view_t> recent_files;
    std::vector<menu_command_view_t> menu_commands;
    wormedit::ui::notification_view_t notifications;
    wormedit::ui::options_view_t options;
    const wormedit::ui::lane_keys_view_t* lane_keys{};
    close_confirmation_view_t close_confirmation;
    wormedit::ui::chart_import_view_t chart_import;
    wormedit::ui::tap_timing_view_t tap_timing;
    bool timing_settings_visible;
    uint8_t metronome_volume;
    uint8_t metronome_count_in;
    bool project_loaded;
    bool project_has_native_path;
    wormedit::song_id_t project_id;
    wormedit::chart_id_t selected_chart_id;
    uint64_t document_revision;
    // A dropped MP3/OGG awaiting confirmation as a new project's audio.
    std::string_view pending_new_project_audio_path;
    // A BMS-family source whose text encoding must be selected before opening.
    std::string_view pending_bms_encoding_path;
    std::string_view project_title;
    std::string_view artist;
    std::string_view selected_difficulty;
    std::string_view status_text;
    std::string_view git_hash;
    WormUiWorkspaceTab active_tab;
    std::array<wormedit::ui::workspace_tab_item_view_t, WORM_UI_WORKSPACE_TAB_COUNT> tabs;
    std::string_view tab_heading;
    bool transport_available;
    bool transport_playing;
    bool transport_paused;
    bool transport_metronome_enabled;
    bool transport_metronome_available;
    uint8_t transport_rate_index;
    bool minimap_click_moves_paused_playhead;
    bool transport_rate_available;
    bool transport_keep_pitch;
    bool transport_keep_pitch_available;
    uint64_t transport_frame;
    uint64_t transport_end_frame;
    uint32_t transport_sample_rate;
    std::vector<std::string> summary_lines;
    bool history_available;
    bool dirty;
    size_t undo_depth;
    size_t redo_depth;
    bool recovery_panel_visible;
    // Computed from the controller rows before presentation ordering.
    bool recovery_has_actionable_candidate;
    std::string_view recovery_heading;
    std::string_view recovery_notice;
    bool recovery_rows_truncated;
    std::vector<wormedit::ui::recovery_candidate_view_t> recovery_candidates;
    std::vector<std::string> recovery_diagnostics;
    wormedit::ui::setup_view_t setup;
    wormedit::ui::metadata_view_t metadata;
    wormedit::ui::timing_view_t timing;
    wormedit::ui::compose_view_t compose;
    wormedit::ui::audio_slices_view_t audio_slices;
    compare_view_t compare;
    wormedit::ui::player_preview_view_t player_preview;
    struct wormedit::ui::external_player_view_t external_player;
    std::vector<command_menu_row_view_t> command_menu_rows;
    wormedit::ui::kind_catalog_view_t<wormedit::sound_kind_t> sound_kinds;

    static constexpr std::size_t maximum_visible_recovery_candidates = 4;
    static constexpr std::size_t maximum_visible_recovery_diagnostics = 4;

    [[nodiscard]] auto has_scrollable_recovery() const -> bool {
        return recovery_candidates.size() > maximum_visible_recovery_candidates ||
               recovery_diagnostics.size() > maximum_visible_recovery_diagnostics;
    }

    [[nodiscard]] auto recovery_panel_height(
        std::optional<bool> save_as_destination_entered = std::nullopt) const -> float {
        const auto rows = std::min(recovery_candidates.size(), maximum_visible_recovery_candidates);
        const auto diagnostics = std::min(recovery_diagnostics.size(), maximum_visible_recovery_diagnostics);
        float height = 40.0f + (!recovery_notice.empty() ? 22.0f : 0.0f) +
                       static_cast<float>(rows) * 48.0f + static_cast<float>(diagnostics) * 18.0f;
        for (std::size_t index = 0; index < rows; ++index) {
            const auto& candidate = recovery_candidates[index];
            if (candidate.can(wormedit::native_project_recovery_action_t::save_as_new_project)) {
                height += 25.0f;
                const bool has_destination = save_as_destination_entered.value_or(
                    !candidate.path_label.empty());
                if (!has_destination) height += 18.0f;
            }
            if (!candidate.disabled_reason.empty()) height += 20.0f;
        }
        return height;
    }
};

} // namespace wormedit::ui

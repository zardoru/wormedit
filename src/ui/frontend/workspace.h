#pragma once

#include "workspace_types.h"
#include "file_jobs.h"
#include "lane_keys.h"
#include "options_notifications.h"
#include "compose.h"
#include "catalog.h"
#include "project.h"
#include "metadata_audio.h"
#include "timing_preview.h"
#include "transport_status.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::ui {
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
    bool pending_bms_profile_required{};
    text_encoding_t pending_bms_encoding{};
    kind_catalog_view_t<std::string_view> bms_lane_profiles;
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
    std::optional<transport_position_view_t> transport_position;
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
    std::vector<command_menu_row_view_t> command_menu_rows;
    wormedit::ui::kind_catalog_view_t<wormedit::sound_kind_t> sound_kinds;

    static constexpr std::size_t maximum_visible_recovery_candidates = 4;
    static constexpr std::size_t maximum_visible_recovery_diagnostics = 4;

    [[nodiscard]] auto has_scrollable_recovery() const -> bool {
        return recovery_candidates.size() > maximum_visible_recovery_candidates ||
               recovery_diagnostics.size() > maximum_visible_recovery_diagnostics;
    }

    // Rows of the recovery card. The renderer lays these out and the views reserve their sum.
    struct recovery_card_layout_t {
        static constexpr float padding = 10.0f; // card padding on every side
        static constexpr float spacing = 4.0f;  // row spacing (metrics::gap_small)
        static constexpr float header = 24.0f;  // heading and notice
        static constexpr float candidate = 28.0f; // status, file name and offered actions
        static constexpr float detail = 18.0f;  // comparison and full path
        static constexpr float inactive = 22.0f; // one line for a candidate without actions
        static constexpr float field = 28.0f;   // Save as destination
        static constexpr float note = 18.0f;    // hints, truncation and diagnostics
    };

    [[nodiscard]] static auto recovery_candidate_height(const recovery_candidate_view_t& candidate,
                                                        std::optional<bool> save_as_destination_entered)
        -> float {
        using layout = recovery_card_layout_t;
        if (candidate.actions.empty()) return layout::inactive + layout::spacing;
        float height = layout::candidate + layout::detail + 2.0f * layout::spacing;
        if (candidate.can(wormedit::native_project_recovery_action_t::save_as_new_project)) {
            height += layout::field + layout::spacing;
            if (!save_as_destination_entered.value_or(!candidate.path_label.empty()))
                height += layout::note + layout::spacing;
        }
        return height;
    }

    [[nodiscard]] auto recovery_panel_height(
        std::optional<bool> save_as_destination_entered = std::nullopt) const -> float {
        using layout = recovery_card_layout_t;
        const auto rows = std::min(recovery_candidates.size(), maximum_visible_recovery_candidates);
        const auto diagnostics = std::min(recovery_diagnostics.size(), maximum_visible_recovery_diagnostics);
        float height = 2.0f * layout::padding + 2.0f + layout::header + layout::spacing +
                       static_cast<float>(diagnostics) * (layout::note + layout::spacing);
        if (recovery_rows_truncated) height += layout::note + layout::spacing;
        for (std::size_t index = 0; index < rows; ++index)
            height += recovery_candidate_height(recovery_candidates[index], save_as_destination_entered);
        return height;
    }
};

} // namespace wormedit::ui

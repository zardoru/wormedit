#pragma once

#include "ui/frontend.h"

#include <wormedit/core/interaction.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/recovery_controller.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/core/workspace.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wormedit::sys {

struct flattened_workspace_t {
    WormUiWorkspaceView view{};
    std::array<std::string, WORM_UI_WORKSPACE_SUMMARY_CAPACITY> summary_storage;
    std::array<std::string, WORM_UI_RECOVERY_ROW_CAPACITY> recovery_path_storage;
    std::array<std::string, WORM_UI_RECOVERY_ROW_CAPACITY> recovery_summary_storage;
    std::array<std::string, WORM_UI_RECOVERY_ROW_CAPACITY> recovery_disabled_storage;
    std::array<std::string, WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY> recovery_diagnostic_storage;
    std::string setup_policy_status_storage;
    std::vector<const char*> metadata_search_tag_storage;
    std::vector<std::size_t> metadata_search_tag_length_storage;
    std::array<WormUiMetadataFormatFieldView, WORM_UI_METADATA_FORMAT_FIELD_CAPACITY>
        metadata_format_field_storage{};
    std::array<WormUiMetadataFormatTargetView, WORM_UI_METADATA_FORMAT_TARGET_CAPACITY>
        metadata_format_target_storage{};
    std::string timing_status_storage;
    std::string timing_recipe_status_storage;
    std::vector<WormUiTimingSpeedView> timing_speed_storage;
    std::vector<WormUiTimingMarkerView> timing_marker_storage;
    std::vector<WormUiTimingGridLineView> timing_grid_storage;
    std::vector<WormUiTimingRecipePoint> timing_recipe_candidate_storage;
    std::vector<WormUiTimingRecipePoint> timing_recipe_composed_storage;
    std::string compose_status_storage;
    std::string compose_hover_tooltip_storage;
    std::string compose_selected_note_label_storage;
    std::array<WormUiComposeBookmarkView, WORM_UI_COMPOSE_BOOKMARK_CAPACITY>
        compose_bookmark_storage{};
    std::array<std::string, WORM_UI_COMPOSE_BOOKMARK_CAPACITY> compose_bookmark_label_storage;
    std::array<WormUiComposeSectionView, WORM_UI_COMPOSE_SECTION_CAPACITY>
        compose_section_storage{};
    std::array<std::string, WORM_UI_COMPOSE_SECTION_CAPACITY> compose_section_label_storage;
    std::array<WormUiComposeAudioBookmarkView, WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY>
        compose_audio_bookmark_storage{};
    std::array<std::string, WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY>
        compose_audio_bookmark_label_storage;
    std::array<WormUiComposeAudioSectionView, WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY>
        compose_audio_section_storage{};
    std::array<std::string, WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY>
        compose_audio_section_label_storage;
    std::vector<WormUiComposeBandView> compose_band_storage;
    std::vector<WormUiComposeLaneView> compose_lane_storage;
    std::vector<WormUiComposeGridLineView> compose_grid_storage;
    std::vector<wormedit::exact_chart_row_t> compose_grid_row_storage;
    std::vector<WormUiComposeGridLineView> compose_proposed_grid_storage;
    std::vector<WormUiComposeTimingPreviewRectView> compose_timing_preview_rect_storage;
    std::vector<WormUiTimingMarkerView> compose_timing_preview_marker_storage;
    std::vector<WormUiTimingMarkerView> compose_timing_marker_storage;
    std::vector<WormUiComposeDifferenceView> compose_difference_storage;
    std::vector<WormUiComposeHoldView> compose_hold_storage;
    std::vector<WormUiComposeNoteView> compose_note_storage;
    std::vector<WormUiComposeBgmLaneView> compose_bgm_lane_storage;
    std::vector<WormUiComposeBgmEventView> compose_bgm_event_storage;
    std::unordered_map<std::uint64_t, std::pair<std::string, std::string>> compose_bgm_sound_labels;
    std::vector<WormUiComposeWaveformRowView> compose_waveform_storage;
    std::vector<WormUiComposeWaveformOverviewRowView> compose_waveform_overview_storage;
    std::vector<WormUiComposeBgmDraftView> compose_bgm_draft_storage;
    std::vector<std::uint64_t> compose_bmson_draft_playable_lane_storage;
    std::vector<std::uint8_t> compose_bmson_draft_restart_storage;
    std::optional<WormUiComposeBgmMovePreviewView> compose_bgm_move_storage;
    std::optional<WormUiComposeCursorView> compose_cursor_storage;
    std::array<WormUiComposeSemanticLayerView, WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY>
        compose_semantic_layer_storage{};
    std::array<std::string, WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY>
        compose_semantic_layer_name_storage;
    std::array<WormUiComposeBranchRowView, WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY>
        compose_branch_row_storage{};
    std::array<WormUiComposeBranchChoiceView, WORM_UI_COMPOSE_BRANCH_CHOICE_CAPACITY>
        compose_branch_choice_storage{};
    std::array<std::string, WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY> compose_branch_text_storage;
    std::array<std::string, WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY>
        compose_branch_diagnostic_storage;
    std::array<WormUiComposeResourceView, WORM_UI_COMPOSE_RESOURCE_CAPACITY>
        compose_resource_storage{};
    std::array<std::string, WORM_UI_COMPOSE_RESOURCE_CAPACITY> compose_resource_index_storage;
    std::array<std::string, WORM_UI_COMPOSE_RESOURCE_CAPACITY> compose_resource_name_storage;
    std::array<std::string, WORM_UI_COMPOSE_RESOURCE_CAPACITY> compose_resource_path_storage;
    std::array<WormUiComposeOutputGroupView, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY>
        compose_output_group_storage{};
    std::array<std::string, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY>
        compose_output_group_name_storage;
    std::array<std::string, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY>
        compose_output_group_path_storage;
    std::array<std::string, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY>
        compose_output_group_asset_directory_storage;
    std::string metadata_project_directory_storage;
    std::string metadata_music_source_storage;
    std::string metadata_copy_preview_source_storage;
    std::array<WormUiComposeOutputGroupView, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY>
        metadata_output_group_storage{};
    std::array<std::string, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY>
        metadata_output_group_name_storage;
    std::array<std::string, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY>
        metadata_output_group_path_storage;
    std::array<std::string, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY>
        metadata_output_group_asset_directory_storage;
    std::array<WormUiComposeLaneKeyBinding, WORM_UI_COMPOSE_LANE_KEY_BINDING_CAPACITY>
        compose_lane_key_storage{};
    std::optional<wormedit::editor_interaction_view_model_t> compose_interaction_view_storage;
    std::vector<std::uint64_t> audio_asset_storage;
    std::vector<WormUiAudioWaveformColumnView> audio_waveform_storage;
};

[[nodiscard]] auto flatten_workspace(const workspace_view_t& source,
                                     const song_document_t& document, const std::string& status,
                                     const native_project_recovery_controller_view_t& recovery,
                                     bool recovery_scan_configured,
                                     const stepmania_export_policy_t& stepmania_policy)
    -> flattened_workspace_t;

} // namespace wormedit::sys

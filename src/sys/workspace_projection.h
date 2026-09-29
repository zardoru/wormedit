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
    // Refresh pointers into inline storage after flatten_workspace returns or this object moves.
    void rebind_view_pointers();

    struct {
        std::array<std::string, WORM_UI_WORKSPACE_SUMMARY_CAPACITY> lines;
    } summary;
    struct {
        std::array<std::string, WORM_UI_RECOVERY_ROW_CAPACITY> path;
        std::array<std::string, WORM_UI_RECOVERY_ROW_CAPACITY> summary;
        std::array<std::string, WORM_UI_RECOVERY_ROW_CAPACITY> disabled_reason;
        std::array<std::string, WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY> diagnostic;
    } recovery;
    struct {
        std::string policy_status;
    } setup;
    struct {
        std::vector<const char*> search_tag;
        std::vector<std::size_t> search_tag_length;
        std::array<WormUiMetadataFormatFieldView, WORM_UI_METADATA_FORMAT_FIELD_CAPACITY>
            format_field{};
        std::array<WormUiMetadataFormatTargetView, WORM_UI_METADATA_FORMAT_TARGET_CAPACITY>
            format_target{};
        std::string project_directory;
        std::string music_source;
        std::string copy_preview_source;
        struct {
            std::array<WormUiComposeOutputGroupView, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY>
                view{};
            std::array<std::string, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY> name;
            std::array<std::string, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY> path;
            std::array<std::string, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY> asset_directory;
        } output_group;
    } metadata;
    struct {
        std::string status;
        std::string recipe_status;
        std::vector<WormUiTimingSpeedView> speed;
        std::vector<WormUiTimingMarkerView> marker;
        std::vector<WormUiTimingGridLineView> grid;
        std::vector<WormUiTimingRecipePoint> recipe_candidate;
        std::vector<WormUiTimingRecipePoint> recipe_composed;
    } timing;
    struct {
        std::string status;
        std::string hover_tooltip;
        std::string selected_note_label;
        std::array<WormUiComposeBookmarkView, WORM_UI_COMPOSE_BOOKMARK_CAPACITY> bookmark{};
        std::array<std::string, WORM_UI_COMPOSE_BOOKMARK_CAPACITY> bookmark_label;
        std::array<WormUiComposeSectionView, WORM_UI_COMPOSE_SECTION_CAPACITY> section{};
        std::array<std::string, WORM_UI_COMPOSE_SECTION_CAPACITY> section_label;
        struct {
            std::array<WormUiComposeAudioBookmarkView, WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY>
                bookmark{};
            std::array<std::string, WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY> bookmark_label;
            std::array<WormUiComposeAudioSectionView, WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY>
                section{};
            std::array<std::string, WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY> section_label;
        } audio_marker;
        std::vector<WormUiComposeBandView> band;
        std::vector<WormUiComposeLaneView> lane;
        struct {
            std::vector<WormUiComposeGridLineView> line;
            std::vector<wormedit::exact_chart_row_t> row;
            std::vector<WormUiComposeGridLineView> proposed;
        } grid;
        std::vector<WormUiComposeTimingPreviewRectView> timing_preview_rect;
        std::vector<WormUiTimingMarkerView> timing_preview_marker;
        std::vector<WormUiTimingMarkerView> timing_marker;
        std::vector<std::pair<std::string, std::string>> timing_marker_labels;
        std::vector<WormUiComposeDifferenceView> difference;
        std::vector<WormUiComposeHoldView> hold;
        std::vector<WormUiComposeNoteView> note;
        std::unordered_map<std::uint64_t, std::pair<std::string, std::string>> sound_labels;
        struct {
            std::vector<WormUiComposeBgmLaneView> lane;
            std::vector<WormUiComposeBgmEventView> event;
            std::vector<WormUiComposeBgmDraftView> draft;
            std::optional<WormUiComposeBgmMovePreviewView> move;
        } bgm;
        struct {
            std::vector<WormUiComposeWaveformRowView> row;
            std::vector<WormUiComposeWaveformOverviewRowView> overview_row;
        } waveform;
        std::vector<std::uint64_t> bmson_draft_playable_lane;
        std::vector<std::uint8_t> bmson_draft_restart;
        std::optional<WormUiComposeCursorView> cursor;
        struct {
            std::array<WormUiComposeSemanticLayerView,
                       WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY> view{};
            std::array<std::string, WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY> name;
        } semantic_layer;
        struct {
            std::array<WormUiComposeBranchRowView, WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY> row{};
            std::array<WormUiComposeBranchChoiceView, WORM_UI_COMPOSE_BRANCH_CHOICE_CAPACITY>
                choice{};
            std::array<std::string, WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY> text;
            std::array<std::string, WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY> diagnostic;
        } branch;
        struct {
            std::array<WormUiComposeResourceView, WORM_UI_COMPOSE_RESOURCE_CAPACITY> view{};
            std::array<std::string, WORM_UI_COMPOSE_RESOURCE_CAPACITY> index;
            std::array<std::string, WORM_UI_COMPOSE_RESOURCE_CAPACITY> name;
            std::array<std::string, WORM_UI_COMPOSE_RESOURCE_CAPACITY> path;
        } resource;
        struct {
            std::array<WormUiComposeOutputGroupView, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY>
                view{};
            std::array<std::string, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY> name;
            std::array<std::string, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY> path;
            std::array<std::string, WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY> asset_directory;
        } output_group;
        std::array<WormUiComposeLaneKeyBinding, WORM_UI_COMPOSE_LANE_KEY_BINDING_CAPACITY>
            lane_key{};
        std::optional<wormedit::editor_interaction_view_model_t> interaction_view;
    } compose;
    struct {
        std::vector<std::uint64_t> asset;
        std::vector<WormUiAudioWaveformColumnView> waveform;
    } audio;
};

[[nodiscard]] auto flatten_workspace(const workspace_view_t& source,
                                     const song_document_t& document, const std::string& status,
                                     const native_project_recovery_controller_view_t& recovery,
                                     bool recovery_scan_configured,
                                     const stepmania_export_policy_t& stepmania_policy)
    -> flattened_workspace_t;

} // namespace wormedit::sys

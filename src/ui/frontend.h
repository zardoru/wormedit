#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WORM_UI_WORKSPACE_TAB_COUNT 7
#define WORM_UI_WORKSPACE_SUMMARY_CAPACITY 16
#define WORM_UI_RECOVERY_ROW_CAPACITY 4
#define WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY 4
#define WORM_UI_RECOVERY_DESTINATION_CAPACITY 512
#define WORM_UI_METADATA_SEARCH_TAG_CAPACITY 4096
#define WORM_UI_METADATA_SEARCH_TAG_BYTES 4096
#define WORM_UI_METADATA_TITLE_BYTES 16384
#define WORM_UI_METADATA_CHART_TEXT_BYTES 16384
#define WORM_UI_METADATA_FORMAT_NAME_BYTES 1024
#define WORM_UI_METADATA_FORMAT_VALUE_BYTES 1048576
#define WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY 16384
#define WORM_UI_METADATA_FORMAT_FIELD_CAPACITY 128
#define WORM_UI_METADATA_FORMAT_TARGET_CAPACITY 14
#define WORM_UI_COMPOSE_BAND_CAPACITY 4096
#define WORM_UI_COMPOSE_LANE_CAPACITY 32768
#define WORM_UI_COMPOSE_GRID_CAPACITY 16384
#define WORM_UI_COMPOSE_DIFFERENCE_CAPACITY 32768
#define WORM_UI_COMPOSE_HOLD_CAPACITY 32768
#define WORM_UI_COMPOSE_NOTE_CAPACITY 32768
#define WORM_UI_COMPOSE_BGM_LANE_CAPACITY 4096
#define WORM_UI_COMPOSE_BGM_EVENT_CAPACITY 32768
#define WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY 32768
#define WORM_UI_COMPOSE_LANE_KEY_BINDING_CAPACITY 128
#define WORM_UI_COMPOSE_NOTE_LABEL_BYTES 4096
#define WORM_UI_COMPOSE_IMPORT_PATH_BYTES 4096
#define WORM_UI_COMPOSE_BOOKMARK_CAPACITY 64
#define WORM_UI_COMPOSE_SECTION_CAPACITY 64
#define WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY 64
#define WORM_UI_COMPOSE_MARKER_LABEL_BYTES 4096
#define WORM_UI_COMPOSE_MARKER_TIME_BYTES 32
#define WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY 16
#define WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY 16
#define WORM_UI_SHORTCUT_EVENT_CAPACITY 64
#define WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY 96
#define WORM_UI_COMPOSE_BRANCH_CHOICE_CAPACITY 96
#define WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY 4
#define WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY 9
#define WORM_UI_COMPOSE_SEMANTIC_LAYER_NAME_CAPACITY 128
#define WORM_UI_COMPOSE_RESOURCE_CAPACITY 128
#define WORM_UI_TIMING_SPEED_CAPACITY 4096
#define WORM_UI_TIMING_MARKER_CAPACITY 16384
#define WORM_UI_TIMING_GRID_CAPACITY 16384
#define WORM_UI_TIMING_RECIPE_POINT_CAPACITY 4096
#define WORM_UI_TIMING_RECIPE_RATIONAL_TEXT_BYTES 64
#define WORM_UI_TIMING_RECIPE_INTEGER_TEXT_BYTES 32
#define WORM_UI_AUDIO_ASSET_CAPACITY 128
#define WORM_UI_AUDIO_WAVEFORM_COLUMN_CAPACITY 4096
#define WORM_UI_PLAYER_PREVIEW_LANE_CAPACITY 64
#define WORM_UI_PLAYER_PREVIEW_NOTE_CAPACITY 4096
#define WORM_UI_PLAYER_PREVIEW_HOLD_CAPACITY 4096
#define WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER_CAPACITY 5
#define WORM_UI_PLAYER_PREVIEW_STORYBOARD_CAPACITY 27
#define WORM_UI_PLAYER_PREVIEW_IMAGE_MAX_DIMENSION 16384
#define WORM_UI_COMPOSE_MIN_PIXELS_PER_MEASURE 24.0
#define WORM_UI_COMPOSE_MAX_PIXELS_PER_MEASURE 4096.0

typedef enum WormUiRecoveryAction {
    WORM_UI_RECOVERY_ACTION_INSPECT = 1 << 0,
    WORM_UI_RECOVERY_ACTION_RECOVER = 1 << 1,
    WORM_UI_RECOVERY_ACTION_SAVE_AS = 1 << 2,
    WORM_UI_RECOVERY_ACTION_DISCARD = 1 << 3,
    WORM_UI_RECOVERY_ACTION_QUARANTINE = 1 << 4,
} WormUiRecoveryAction;

typedef struct WormUiRecoveryCandidateView {
    const char* path_label;
    const char* summary;
    const char* disabled_reason;
    size_t action_mask;
    uint64_t scan_generation;
    size_t row_index;
    size_t candidate_index;
} WormUiRecoveryCandidateView;

typedef enum WormUiWorkspaceActionKind {
    WORM_UI_WORKSPACE_ACTION_NONE = 0,
    WORM_UI_WORKSPACE_ACTION_SWITCH_TAB,
    WORM_UI_WORKSPACE_ACTION_QUIT,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_STOP,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK,
    WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET,
    WORM_UI_WORKSPACE_ACTION_SELECT_VISUAL_ASSET,
    WORM_UI_WORKSPACE_ACTION_TOGGLE_PLAYER_PREVIEW,
    WORM_UI_WORKSPACE_ACTION_SELECT_PLAYER_PREVIEW_SKIN,
    WORM_UI_WORKSPACE_ACTION_NAVIGATE_PREVIOUS_MARKER,
    WORM_UI_WORKSPACE_ACTION_NAVIGATE_NEXT_MARKER,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD,
    WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE,
    WORM_UI_WORKSPACE_ACTION_SET_STEPMANIA_EXPORT_POLICY,
} WormUiWorkspaceActionKind;

typedef enum WormUiWorkspaceTab {
    WORM_UI_WORKSPACE_TAB_SETUP = 0,
    WORM_UI_WORKSPACE_TAB_METADATA,
    WORM_UI_WORKSPACE_TAB_TIMING,
    WORM_UI_WORKSPACE_TAB_COMPOSE,
    WORM_UI_WORKSPACE_TAB_STORYBOARD,
    WORM_UI_WORKSPACE_TAB_AUDIO_SLICES,
    WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE,
} WormUiWorkspaceTab;

typedef struct WormUiWorkspaceTabItem {
    const char* label;
    bool enabled;
} WormUiWorkspaceTabItem;

typedef enum WormUiStepmaniaFileProfile {
    WORM_UI_STEPMANIA_FILE_SM = 0,
    WORM_UI_STEPMANIA_FILE_SSC,
} WormUiStepmaniaFileProfile;

typedef enum WormUiStepmaniaTimingSyntax {
    WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS = 0,
    WORM_UI_STEPMANIA_TIMING_LEGACY_NEGATIVE_BPM,
    WORM_UI_STEPMANIA_TIMING_LEGACY_NEGATIVE_STOP,
} WormUiStepmaniaTimingSyntax;

typedef enum WormUiStepmaniaImplementation {
    WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY = 0,
    WORM_UI_STEPMANIA_IMPLEMENTATION_STEP_MANIA_5_1,
    WORM_UI_STEPMANIA_IMPLEMENTATION_ETTERNA_0_75_1,
} WormUiStepmaniaImplementation;

typedef struct WormUiSetupView {
    bool visible;
    size_t chart_count;
    size_t audio_asset_count;
    size_t visual_asset_count;
    WormUiStepmaniaFileProfile stepmania_file_profile;
    WormUiStepmaniaTimingSyntax stepmania_timing_syntax;
    WormUiStepmaniaImplementation stepmania_implementation;
    bool stepmania_policy_valid;
    const char* stepmania_policy_status;
} WormUiSetupView;

typedef struct WormUiMetadataFormatFieldView {
    bool chart_scoped;
    uint8_t family;
    bool first_group_for_family;
    size_t group_index;
    size_t field_index;
    size_t group_field_count;
    const char* name;
    const char* value;
    size_t name_bytes;
    size_t value_bytes;
    bool has_source_order;
    size_t source_order;
} WormUiMetadataFormatFieldView;

typedef struct WormUiMetadataFormatTargetView {
    bool chart_scoped;
    uint8_t family;
    bool group_exists;
    size_t group_index;
    size_t field_count;
} WormUiMetadataFormatTargetView;

typedef struct WormUiAudioWaveformColumnView {
    float minimum[2];
    float maximum[2];
    float absolute_peak[2];
    float rms[2];
} WormUiAudioWaveformColumnView;

typedef struct WormUiMetadataView {
    bool visible;
    bool song_fields_editable;
    bool chart_fields_editable;
    bool audio_offset_editable;
    bool search_tags_editable;
    uint64_t document_revision;
    bool has_chart;
    uint64_t chart_id;
    const char* title;
    const char* artist;
    size_t title_bytes;
    size_t artist_bytes;
    const char* chart_name;
    const char* chart_author;
    size_t chart_name_bytes;
    size_t chart_author_bytes;
    bool has_chart_rating;
    int64_t chart_rating_numerator;
    uint64_t chart_rating_denominator;
    bool timing_locked;
    int64_t audio_offset_numerator;
    uint64_t audio_offset_denominator;
    bool audio_waveform_has_selected_asset;
    uint64_t audio_waveform_selected_asset_id;
    bool audio_waveform_analysis_pending;
    bool audio_waveform_available;
    uint64_t audio_waveform_total_frames;
    uint32_t audio_waveform_sample_rate;
    const WormUiAudioWaveformColumnView* audio_waveform_columns;
    size_t audio_waveform_column_count;
    bool audio_offset_drag_active;
    int64_t audio_offset_drag_delta_frames;
    int64_t proposed_audio_offset_numerator;
    uint64_t proposed_audio_offset_denominator;
    const char* const* search_tags;
    const size_t* search_tag_bytes;
    size_t search_tag_count;
    bool format_fields_truncated;
    const WormUiMetadataFormatFieldView* format_fields;
    size_t format_field_total_count;
    size_t format_field_count;
    const WormUiMetadataFormatTargetView* format_targets;
    size_t format_target_count;
} WormUiMetadataView;

typedef struct WormUiAudioSlicesView {
    bool visible;
    bool assets_truncated;
    const uint64_t* asset_ids;
    size_t asset_count;
    bool has_selected_asset;
    uint64_t selected_asset_id;
    bool analysis_pending;
    bool waveform_available;
    uint64_t total_frames;
    uint32_t sample_rate;
    const WormUiAudioWaveformColumnView* waveform_columns;
    size_t waveform_column_count;
    bool has_chart;
    uint64_t chart_id;
    uint64_t document_revision;
    bool timing_locked;
    int64_t offset_numerator;
    uint64_t offset_denominator;
    bool offset_drag_active;
    int64_t offset_drag_delta_frames;
    int64_t proposed_offset_numerator;
    uint64_t proposed_offset_denominator;
} WormUiAudioSlicesView;

typedef enum WormUiAudioOffsetDragAction {
    WORM_UI_AUDIO_OFFSET_DRAG_NONE = 0,
    WORM_UI_AUDIO_OFFSET_DRAG_BEGIN,
    WORM_UI_AUDIO_OFFSET_DRAG_UPDATE,
    WORM_UI_AUDIO_OFFSET_DRAG_COMMIT,
    WORM_UI_AUDIO_OFFSET_DRAG_CANCEL,
} WormUiAudioOffsetDragAction;

typedef struct WormUiComposeRect {
    double left;
    double top;
    double right;
    double bottom;
} WormUiComposeRect;

typedef struct WormUiComposeBandView {
    WormUiComposeRect bounds;
    bool split_stage;
} WormUiComposeBandView;

typedef struct WormUiComposeLaneView {
    WormUiComposeRect bounds;
    uint64_t lane_id;
    uint32_t display_order;
    uint8_t role;
    uint8_t stage;
    uint8_t glyph;
} WormUiComposeLaneView;

typedef struct WormUiComposeGridLineView {
    double y_pixels;
    uint8_t kind;
    uint8_t emphasis;
    uint32_t measure_number;
} WormUiComposeGridLineView;

typedef struct WormUiComposeDifferenceView {
    WormUiComposeRect bounds;
} WormUiComposeDifferenceView;

typedef struct WormUiComposeHoldView {
    WormUiComposeRect bounds;
    uint64_t note_id;
    uint64_t lane_id;
    bool selected;
    bool proposal_origin;
    bool proposal_ghost;
    bool semantic_color_visible;
    uint8_t semantic_red;
    uint8_t semantic_green;
    uint8_t semantic_blue;
    uint8_t semantic_alpha;
} WormUiComposeHoldView;

typedef struct WormUiComposeNoteView {
    WormUiComposeRect bounds;
    uint64_t note_id;
    uint64_t lane_id;
    uint8_t glyph;
    uint8_t kind;
    bool tail;
    bool selected;
    bool proposal_origin;
    bool proposal_ghost;
    bool semantic_color_visible;
    uint8_t semantic_red;
    uint8_t semantic_green;
    uint8_t semantic_blue;
    uint8_t semantic_alpha;
    bool paint_preview;
    bool paint_erase_preview;
    bool paint_invalid_preview;
} WormUiComposeNoteView;

typedef struct WormUiComposeCursorView {
    WormUiComposeRect bounds;
    uint64_t lane_id;
} WormUiComposeCursorView;

typedef struct WormUiComposeBgmLaneView {
    WormUiComposeRect bounds;
    uint64_t source_lane_id;
    size_t source_order;
} WormUiComposeBgmLaneView;

typedef struct WormUiComposeBgmEventView {
    WormUiComposeRect bounds;
    uint64_t event_id;
    uint64_t source_lane_id;
    uint64_t sound_id;
    bool has_sound;
    bool bmson_slice;
    bool bmson_restart;
    bool selected;
} WormUiComposeBgmEventView;

typedef struct WormUiComposeBgmDraftView {
    WormUiComposeRect bounds;
    bool restart;
} WormUiComposeBgmDraftView;

// A visible palette slot. The application retains stable IDs, names, and
// colors; the frontend must return the ID rather than a transient index.
typedef struct WormUiComposeSemanticLayerView {
    uint64_t layer_id;
    const char* name;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
    size_t assigned_note_count;
    bool visible;
    bool locked;
    bool selected;
} WormUiComposeSemanticLayerView;

typedef struct WormUiComposeBookmarkView {
    uint64_t bookmark_id;
    const char* label;
    uint64_t measure_number;
    uint64_t row;
} WormUiComposeBookmarkView;

typedef struct WormUiComposeSectionView {
    uint64_t section_id;
    const char* label;
    uint64_t begin_measure_number;
    uint64_t begin_row;
    uint64_t end_measure_number;
    uint64_t end_row;
} WormUiComposeSectionView;

typedef struct WormUiComposeAudioBookmarkView {
    uint64_t bookmark_id;
    const char* label;
    int64_t audio_milliseconds;
    bool projection_visible;
    double projected_y_pixels;
} WormUiComposeAudioBookmarkView;

typedef struct WormUiComposeAudioSectionView {
    uint64_t section_id;
    const char* label;
    int64_t begin_audio_milliseconds;
    int64_t end_audio_milliseconds;
    bool begin_projection_visible;
    bool end_projection_visible;
    bool span_projection_visible;
    double projected_begin_y_pixels;
    double projected_end_y_pixels;
    double projected_span_top_y_pixels;
    double projected_span_bottom_y_pixels;
} WormUiComposeAudioSectionView;

typedef enum WormUiComposeSelectionMode {
    WORM_UI_COMPOSE_SELECTION_REPLACE = 0,
    WORM_UI_COMPOSE_SELECTION_ADD,
    WORM_UI_COMPOSE_SELECTION_TOGGLE,
} WormUiComposeSelectionMode;

typedef struct WormUiComposeLaneKeyBinding {
    uint32_t scancode;
    uint64_t lane_id;
} WormUiComposeLaneKeyBinding;

// Shortcut keys cross the frontend boundary as USB HID Keyboard/Keypad
// usages. Modifier bits match shortcut_modifier_t's actual modifiers.
typedef struct WormUiShortcutEvent {
    uint16_t hid_usage;
    uint8_t modifiers;
    bool repeat;
} WormUiShortcutEvent;

typedef enum WormUiShortcutModifier {
    WORM_UI_SHORTCUT_MODIFIER_SHIFT = 1 << 0,
    WORM_UI_SHORTCUT_MODIFIER_CONTROL = 1 << 1,
    WORM_UI_SHORTCUT_MODIFIER_ALT = 1 << 2,
    WORM_UI_SHORTCUT_MODIFIER_META = 1 << 3,
} WormUiShortcutModifier;

typedef struct WormUiComposeBranchRowView {
    const char* display_text;
    size_t depth;
    bool active;
    bool selected;
    bool display_text_truncated;
    bool has_source_order;
    size_t source_order;
} WormUiComposeBranchRowView;

typedef enum WormUiComposeBranchKind {
    WORM_UI_COMPOSE_BRANCH_RANDOM = 0,
    WORM_UI_COMPOSE_BRANCH_SWITCH,
} WormUiComposeBranchKind;

typedef struct WormUiComposeBranchChoiceView {
    uint64_t branch_id;
    WormUiComposeBranchKind kind;
    bool has_source_order;
    size_t source_order;
    bool active;
    bool editable;
    bool has_selected_value;
    int64_t selected_value;
    bool has_previous_value;
    int64_t previous_value;
    bool has_next_value;
    int64_t next_value;
    bool can_clear;
} WormUiComposeBranchChoiceView;

typedef enum WormUiTimingLaneKind {
    WORM_UI_TIMING_LANE_BPM = 0,
    WORM_UI_TIMING_LANE_SCROLL,
    WORM_UI_TIMING_LANE_SPEED,
    WORM_UI_TIMING_LANE_STOP,
    WORM_UI_TIMING_LANE_DELAY,
    WORM_UI_TIMING_LANE_WARP,
    WORM_UI_TIMING_LANE_KIAI,
    WORM_UI_TIMING_LANE_ANCHOR,
} WormUiTimingLaneKind;

typedef struct WormUiTimingMarkerView {
    uint64_t object_id;
    uint64_t source_order;
    uint64_t storage_order;
    double marker_y_pixels;
    double marker_left_fraction;
    double marker_right_fraction;
    WormUiTimingLaneKind lane;
} WormUiTimingMarkerView;

typedef enum WormUiComposeResourceRole {
    WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE = 1 << 0,
    WORM_UI_COMPOSE_RESOURCE_ROLE_BGM = 1 << 1,
    WORM_UI_COMPOSE_RESOURCE_ROLE_BGA = 1 << 2,
} WormUiComposeResourceRole;

typedef struct WormUiComposeResourceView {
    uint64_t resource_id;
    uint64_t asset_id;
    uint8_t roles;
    bool audio_asset;
    bool selected_audio_asset;
    bool image_asset;
    bool selected_visual_asset;
    bool missing;
    const char* target_index;
    const char* name;
    const char* source_name;
} WormUiComposeResourceView;

// Renderer-neutral field primitives projected into bounded scalar storage.
// Coordinates are relative to the scene viewport; no chart/core types cross
// the private frontend boundary.
typedef struct WormUiComposeView {
    bool visible;
    bool scene_available;
    bool truncated;
    bool note_paint_mode_active;
    bool semantic_layer_paint_mode_active;
    size_t selected_note_count;
    size_t selected_object_count;
    uint64_t selected_bmson_bgm_event_id;
    size_t staged_bmson_marker_count;
    bool beat_marker_navigation_available;
    bool audio_marker_navigation_available;
    bool audio_marker_transport_available;
    const WormUiComposeBookmarkView* beat_bookmarks;
    size_t beat_bookmark_count;
    bool beat_bookmarks_truncated;
    const WormUiComposeSectionView* beat_sections;
    size_t beat_section_count;
    bool beat_sections_truncated;
    bool beat_section_selection_span_available;
    uint64_t beat_section_selection_begin_measure_number;
    uint64_t beat_section_selection_begin_row;
    uint64_t beat_section_selection_end_measure_number;
    uint64_t beat_section_selection_end_row;
    const WormUiComposeAudioBookmarkView* audio_bookmarks;
    size_t audio_bookmark_count;
    bool audio_bookmarks_truncated;
    const WormUiComposeAudioSectionView* audio_sections;
    size_t audio_section_count;
    bool audio_sections_truncated;
    const WormUiComposeSemanticLayerView* semantic_layers;
    size_t semantic_layer_count;
    size_t semantic_layer_total_count;
    size_t semantic_layer_page;
    size_t semantic_layer_page_count;
    bool semantic_layers_truncated;
    bool semantic_layer_target_locked;
    bool semantic_layer_target_visible;
    bool semantic_layer_target_required_filter;
    bool semantic_layer_target_excluded_filter;
    bool semantic_layer_solo_active;
    uint64_t project_id;
    uint64_t chart_id;
    uint64_t document_revision;
    double viewport_width_pixels;
    double viewport_height_pixels;
    double playable_field_left_pixels;
    double playable_field_right_pixels;
    double timing_document_begin_fraction;
    double timing_document_end_fraction;
    double pixels_per_standard_measure;
    bool playhead_visible;
    double playhead_y_pixels;
    const char* status_text;
    const char* selected_note_label;
    uint64_t selected_note_label_note_id;
    bool focus_note_label_editor;
    bool hover_note_visible;
    uint64_t hover_note_id;
    const char* hover_tooltip;
    bool viewport_autoscroll_active;
    double viewport_autoscroll_anchor_x_pixels;
    double viewport_autoscroll_anchor_y_pixels;
    double viewport_autoscroll_pointer_x_pixels;
    double viewport_autoscroll_pointer_y_pixels;
    const WormUiComposeBandView* bands;
    size_t band_count;
    const WormUiComposeLaneView* lanes;
    size_t lane_count;
    const WormUiComposeGridLineView* grid_lines;
    size_t grid_line_count;
    const WormUiTimingMarkerView* timing_markers;
    size_t timing_marker_count;
    uint8_t timing_lane_visibility_mask;
    bool timing_stage_active;
    bool timing_stage_valid;
    WormUiTimingLaneKind timing_stage_lane;
    double timing_stage_y_pixels;
    const WormUiComposeResourceView* resources;
    uint64_t selected_audio_asset_id;
    uint64_t selected_visual_asset_id;
    size_t resource_count;
    size_t resource_total_count;
    bool resources_truncated;
    const WormUiComposeDifferenceView* differences;
    size_t difference_count;
    const WormUiComposeHoldView* hold_bodies;
    size_t hold_body_count;
    const WormUiComposeNoteView* note_endpoints;
    size_t note_endpoint_count;
    const WormUiComposeBgmLaneView* bgm_lanes;
    size_t bgm_lane_count;
    const WormUiComposeBgmEventView* bgm_events;
    size_t bgm_event_count;
    const WormUiComposeBgmDraftView* bmson_marker_drafts;
    size_t bmson_marker_draft_count;
    const WormUiComposeCursorView* cursor;
    bool area_selection_preview_visible;
    WormUiComposeRect area_selection_preview_bounds;
    WormUiComposeSelectionMode area_selection_preview_mode;
    bool space_range_preview_visible;
    WormUiComposeRect space_range_preview_bounds;
    WormUiComposeSelectionMode space_range_preview_mode;
    double space_range_anchor_y_pixels;
    double space_range_tail_y_pixels;
    const WormUiComposeLaneKeyBinding* lane_key_bindings;
    size_t lane_key_binding_count;
    bool branch_outline_available;
    bool branch_rows_truncated;
    bool branch_choices_truncated;
    bool branch_display_text_truncated;
    bool branch_diagnostics_truncated;
    const WormUiComposeBranchRowView* branch_rows;
    size_t branch_row_count;
    const WormUiComposeBranchChoiceView* branch_choices;
    size_t branch_choice_count;
    const char* branch_diagnostics[WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY];
    size_t branch_diagnostic_count;
} WormUiComposeView;

typedef struct WormUiTimingSpeedView {
    uint64_t object_id;
    uint64_t source_order;
    uint64_t storage_order;
    double marker_y_pixels;
    double marker_left_fraction;
    double marker_right_fraction;
    double affected_begin_y_pixels;
    double affected_end_y_pixels;
    double multiplier;
    double transition_duration;
    bool transition_is_seconds;
    bool transition_clipped;
} WormUiTimingSpeedView;

// Same numeric kind ordering as editor_grid_line_kind_t, kept scalar across
// the frontend boundary: active snap, beat, group, then measure.
typedef struct WormUiTimingGridLineView {
    double y_pixels;
    uint8_t kind;
    uint8_t emphasis;
} WormUiTimingGridLineView;

typedef enum WormUiTimingPauseUnit {
    WORM_UI_TIMING_PAUSE_SECONDS = 0,
    WORM_UI_TIMING_PAUSE_MILLISECONDS,
    WORM_UI_TIMING_PAUSE_BMS_STOP_TICKS,
    WORM_UI_TIMING_PAUSE_BEATS,
} WormUiTimingPauseUnit;

typedef enum WormUiTimingRecipeOutput {
    WORM_UI_TIMING_RECIPE_OUTPUT_SV = 0,
    WORM_UI_TIMING_RECIPE_OUTPUT_SCROLLS,
    WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS,
} WormUiTimingRecipeOutput;

typedef enum WormUiTimingRecipeCurve {
    WORM_UI_TIMING_RECIPE_CURVE_CONSTANT = 0,
    WORM_UI_TIMING_RECIPE_CURVE_LINEAR,
    WORM_UI_TIMING_RECIPE_CURVE_EXPONENTIAL,
    WORM_UI_TIMING_RECIPE_CURVE_BUMP,
} WormUiTimingRecipeCurve;

typedef enum WormUiTimingRecipeSamplingMode {
    WORM_UI_TIMING_RECIPE_SAMPLING_FIXED = 0,
    WORM_UI_TIMING_RECIPE_SAMPLING_ADAPTIVE,
} WormUiTimingRecipeSamplingMode;

typedef enum WormUiTimingRecipeFixedExtent {
    WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT = 0,
    WORM_UI_TIMING_RECIPE_FIXED_STEP_ROWS,
} WormUiTimingRecipeFixedExtent;

typedef struct WormUiTimingRecipeRationalText {
    char numerator[WORM_UI_TIMING_RECIPE_RATIONAL_TEXT_BYTES];
    char denominator[WORM_UI_TIMING_RECIPE_RATIONAL_TEXT_BYTES];
} WormUiTimingRecipeRationalText;

// The frontend uses fractions already normalized by the application. Row
// fraction runs top-to-bottom through the chart; value fraction maps left-to-
// right across the Scroll/SV lane. Both coordinates must be finite in [0, 1].
typedef struct WormUiTimingRecipePoint {
    double row_fraction;
    double value_fraction;
} WormUiTimingRecipePoint;

typedef struct WormUiTimingRecipeForm {
    WormUiTimingRecipeOutput output;
    WormUiTimingRecipeCurve curve;
    WormUiTimingRecipeSamplingMode sampling_mode;
    WormUiTimingRecipeFixedExtent fixed_extent;
    WormUiTimingRecipeRationalText value_from;
    WormUiTimingRecipeRationalText value_to;
    WormUiTimingRecipeRationalText shape;
    WormUiTimingRecipeRationalText bump_start;
    WormUiTimingRecipeRationalText bump_length;
    WormUiTimingRecipeRationalText bump_x;
    WormUiTimingRecipeRationalText bump_y;
    WormUiTimingRecipeRationalText tolerance;
    WormUiTimingRecipeRationalText transition_duration;
    bool transition_is_seconds;
    char fixed_point_count[WORM_UI_TIMING_RECIPE_INTEGER_TEXT_BYTES];
    char fixed_step_rows[WORM_UI_TIMING_RECIPE_INTEGER_TEXT_BYTES];
    char adaptive_max_points[WORM_UI_TIMING_RECIPE_INTEGER_TEXT_BYTES];
} WormUiTimingRecipeForm;

typedef struct WormUiTimingRecipeDraftView {
    bool selection_armed;
    bool active;
    bool valid;
    uint64_t chart_id;
    uint64_t document_revision;
    uint64_t recipe_id;
    double range_begin_fraction;
    double range_end_fraction;
    WormUiTimingRecipeForm form;
    const WormUiTimingRecipePoint* candidate_points;
    size_t candidate_point_count;
    const WormUiTimingRecipePoint* composed_points;
    size_t composed_point_count;
    const char* status_text;
} WormUiTimingRecipeDraftView;

// Renderer-neutral scalar projection of the timing pane. SPEEDS spans are
// evaluated by the timing core before crossing this private C boundary.
typedef struct WormUiTimingView {
    bool visible;
    bool scene_available;
    bool truncated;
    double viewport_width_pixels;
    double viewport_height_pixels;
    uint64_t chart_id;
    uint64_t document_revision;
    uint64_t total_rows;
    uint64_t active_snap_denominator;
    const uint64_t* supported_snap_denominators;
    size_t supported_snap_denominator_count;
    const char* status_text;
    size_t measure_count;
    size_t bpm_count;
    size_t pause_count;
    size_t warp_count;
    size_t bookmark_count;
    size_t section_count;
    size_t anchor_count;
    size_t speed_total_count;
    const WormUiTimingSpeedView* speeds;
    size_t speed_count;
    const WormUiTimingMarkerView* markers;
    size_t marker_count;
    const WormUiTimingGridLineView* grid_lines;
    size_t grid_line_count;
    WormUiTimingRecipeDraftView sv_recipe;
    bool stage_active;
    bool stage_valid;
    uint64_t stage_existing_object_id;
    WormUiTimingLaneKind stage_lane;
    double stage_marker_y_pixels;
    double stage_value;
    double stage_secondary_value;
    bool stage_transition_is_seconds;
    bool stage_toggle_primary;
    WormUiTimingPauseUnit stage_pause_unit;
    char stage_primary_decimal_text[64];
    bool stage_primary_decimal_exact;
    char stage_primary_numerator_text[64];
    char stage_primary_denominator_text[64];
    char stage_secondary_numerator_text[64];
    char stage_secondary_denominator_text[64];
    const char* stage_status_text;
} WormUiTimingView;

typedef enum WormUiTimingRecipeActionKind {
    WORM_UI_TIMING_RECIPE_ACTION_NONE = 0,
    WORM_UI_TIMING_RECIPE_ACTION_ARM_SELECTION,
    WORM_UI_TIMING_RECIPE_ACTION_BEGIN,
    WORM_UI_TIMING_RECIPE_ACTION_UPDATE,
    WORM_UI_TIMING_RECIPE_ACTION_UPDATE_RANGE,
    WORM_UI_TIMING_RECIPE_ACTION_APPLY,
    WORM_UI_TIMING_RECIPE_ACTION_CANCEL,
} WormUiTimingRecipeActionKind;

// BEGIN and UPDATE_RANGE carry normalized chart-time fractions. The app snaps
// them to its active exact grid and returns the current preview through the
// view above. UPDATE carries form data only; Apply is the only durable action.
typedef struct WormUiTimingRecipeAction {
    WormUiTimingRecipeActionKind kind;
    uint64_t chart_id;
    uint64_t document_revision;
    double range_begin_fraction;
    double range_end_fraction;
    WormUiTimingRecipeForm form;
} WormUiTimingRecipeAction;

typedef enum WormUiTimingStageAction {
    WORM_UI_TIMING_STAGE_NONE = 0,
    WORM_UI_TIMING_STAGE_BEGIN,
    WORM_UI_TIMING_STAGE_UPDATE,
    // MOVE carries a normalized target row for an already-open existing
    // marker. The application snaps it to the active exact timing grid.
    WORM_UI_TIMING_STAGE_MOVE,
    WORM_UI_TIMING_STAGE_CONFIRM,
    WORM_UI_TIMING_STAGE_CANCEL,
    WORM_UI_TIMING_STAGE_DELETE,
} WormUiTimingStageAction;

typedef enum WormUiTimingGridAction {
    WORM_UI_TIMING_GRID_ACTION_NONE = 0,
    WORM_UI_TIMING_GRID_ACTION_SELECT,
} WormUiTimingGridAction;

typedef enum WormUiTimingMeterAction {
    WORM_UI_TIMING_METER_ACTION_NONE = 0,
    WORM_UI_TIMING_METER_ACTION_APPLY,
} WormUiTimingMeterAction;

typedef enum WormUiPlayerPreviewSkin {
    WORM_UI_PLAYER_PREVIEW_SKIN_NEUTRAL = 0,
    WORM_UI_PLAYER_PREVIEW_SKIN_MANIA,
    WORM_UI_PLAYER_PREVIEW_SKIN_ARROWS,
} WormUiPlayerPreviewSkin;

typedef enum WormUiVisualFitPolicy {
    WORM_UI_VISUAL_FIT_STRETCH = 0,
    WORM_UI_VISUAL_FIT_CONTAIN,
    WORM_UI_VISUAL_FIT_COVER,
} WormUiVisualFitPolicy;

typedef enum WormUiPlayerPreviewVisualKind {
    WORM_UI_PLAYER_PREVIEW_VISUAL_BACKGROUND = 0,
    WORM_UI_PLAYER_PREVIEW_VISUAL_POOR,
    WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER,
    WORM_UI_PLAYER_PREVIEW_VISUAL_VIDEO,
    WORM_UI_PLAYER_PREVIEW_VISUAL_STORYBOARD,
} WormUiPlayerPreviewVisualKind;

typedef enum WormUiPlayerPreviewVisualDiagnostic {
    WORM_UI_PLAYER_PREVIEW_VISUAL_DIAGNOSTIC_NONE = 0,
    WORM_UI_PLAYER_PREVIEW_VISUAL_DIAGNOSTIC_VIDEO_FRAME_LATE,
    WORM_UI_PLAYER_PREVIEW_VISUAL_DIAGNOSTIC_VIDEO_DECODE_FAILED,
} WormUiPlayerPreviewVisualDiagnostic;

typedef struct WormUiPlayerPreviewRect {
    double left;
    double top;
    double right;
    double bottom;
} WormUiPlayerPreviewRect;

typedef struct WormUiPlayerPreviewLaneView {
    uint64_t lane_id;
    uint8_t role;
    uint8_t glyph;
    uint32_t display_order;
    WormUiPlayerPreviewRect bounds;
} WormUiPlayerPreviewLaneView;

typedef struct WormUiPlayerPreviewReceptorView {
    uint64_t lane_id;
    uint8_t glyph;
    WormUiPlayerPreviewRect bounds;
} WormUiPlayerPreviewReceptorView;

typedef struct WormUiPlayerPreviewNoteView {
    uint64_t note_id;
    uint64_t lane_id;
    uint8_t kind;
    uint8_t glyph;
    bool tail;
    bool semantic_color_visible;
    uint8_t semantic_red;
    uint8_t semantic_green;
    uint8_t semantic_blue;
    uint8_t semantic_alpha;
    WormUiPlayerPreviewRect bounds;
} WormUiPlayerPreviewNoteView;

typedef struct WormUiPlayerPreviewHoldView {
    uint64_t note_id;
    uint64_t lane_id;
    bool semantic_color_visible;
    uint8_t semantic_red;
    uint8_t semantic_green;
    uint8_t semantic_blue;
    uint8_t semantic_alpha;
    WormUiPlayerPreviewRect bounds;
} WormUiPlayerPreviewHoldView;

typedef struct WormUiPlayerPreviewVisualLayerView {
    uint64_t object_id;
    uint64_t resource_id;
    uint64_t event_sample_frame;
    WormUiPlayerPreviewVisualKind kind;
    size_t draw_order;
    size_t source_order;
    bool video_clock_ready;
    uint64_t video_elapsed_sample_frames;
    uint64_t video_time_numerator;
    uint32_t video_time_denominator;
    bool static_image;
    bool video_frame;
    bool image_ready;
    bool image_pending;
    WormUiPlayerPreviewVisualDiagnostic diagnostic;
    const char* diagnostic_text;
    uint64_t image_content_id;
    uint32_t image_width;
    uint32_t image_height;
    const uint8_t* rgba_pixels;
    size_t rgba_byte_count;
} WormUiPlayerPreviewVisualLayerView;

typedef struct WormUiPlayerPreviewStoryboardSpriteView {
    uint64_t stable_id;
    uint64_t resource_id;
    size_t source_order;
    uint8_t layer_order;
    double x;
    double y;
    double origin_x;
    double origin_y;
    double scale_x;
    double scale_y;
    double rotation_radians;
    double opacity;
    double red;
    double green;
    double blue;
    bool image_ready;
    bool image_pending;
    uint64_t image_content_id;
    uint32_t image_width;
    uint32_t image_height;
    const uint8_t* rgba_pixels;
    size_t rgba_byte_count;
} WormUiPlayerPreviewStoryboardSpriteView;

typedef struct WormUiPlayerPreviewView {
    bool visible;
    bool scene_available;
    bool truncated;
    bool pre_zero;
    bool split_stage;
    WormUiPlayerPreviewSkin skin;
    WormUiVisualFitPolicy visual_fit;
    uint64_t project_id;
    uint64_t chart_id;
    uint64_t document_revision;
    uint64_t sample_frame;
    double viewport_width_pixels;
    double viewport_height_pixels;
    const char* status_text;
    const WormUiPlayerPreviewLaneView* lanes;
    size_t lane_count;
    const WormUiPlayerPreviewReceptorView* receptors;
    size_t receptor_count;
    const WormUiPlayerPreviewHoldView* hold_bodies;
    size_t hold_body_count;
    const WormUiPlayerPreviewNoteView* note_endpoints;
    size_t note_endpoint_count;
    const WormUiPlayerPreviewVisualLayerView* visual_layers;
    size_t visual_layer_count;
    const WormUiPlayerPreviewStoryboardSpriteView* storyboard_sprites;
    size_t storyboard_sprite_count;
} WormUiPlayerPreviewView;

// Private scalar view consumed by the selected frontend adapter. The owning
// application keeps every pointed-to string alive for the duration of compose.
typedef struct WormUiWorkspaceView {
    const char* project_title;
    const char* artist;
    const char* selected_difficulty;
    const char* status_text;
    const char* git_hash;
    WormUiWorkspaceTab active_tab;
    WormUiWorkspaceTabItem tabs[WORM_UI_WORKSPACE_TAB_COUNT];
    const char* tab_heading;
    bool transport_available;
    bool transport_playing;
    bool transport_paused;
    uint64_t transport_frame;
    uint64_t transport_end_frame;
    uint32_t transport_sample_rate;
    const char* summary_lines[WORM_UI_WORKSPACE_SUMMARY_CAPACITY];
    size_t summary_line_count;
    bool history_available;
    bool dirty;
    size_t undo_depth;
    size_t redo_depth;
    bool recovery_panel_visible;
    const char* recovery_heading;
    const char* recovery_notice;
    bool recovery_rows_truncated;
    size_t recovery_candidate_count;
    WormUiRecoveryCandidateView recovery_candidates[WORM_UI_RECOVERY_ROW_CAPACITY];
    size_t recovery_diagnostic_count;
    const char* recovery_diagnostics[WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY];
    WormUiSetupView setup;
    WormUiMetadataView metadata;
    WormUiTimingView timing;
    WormUiComposeView compose;
    WormUiAudioSlicesView audio_slices;
    WormUiPlayerPreviewView player_preview;
} WormUiWorkspaceView;

typedef enum WormUiComposeActionKind {
    WORM_UI_COMPOSE_ACTION_NONE = 0,
    WORM_UI_COMPOSE_ACTION_ZOOM_IN,
    WORM_UI_COMPOSE_ACTION_ZOOM_OUT,
    WORM_UI_COMPOSE_ACTION_SCROLL_UP,
    WORM_UI_COMPOSE_ACTION_SCROLL_DOWN,
    WORM_UI_COMPOSE_ACTION_RESET_VIEW,
    WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE,
    WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_LANE_VISIBILITY,
    WORM_UI_COMPOSE_ACTION_SELECT_RESOURCE_USES,
    WORM_UI_COMPOSE_ACTION_ASSIGN_RESOURCE_TO_SELECTION,
    WORM_UI_COMPOSE_ACTION_CREATE_RESOURCE,
    WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET,
    WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT,
    WORM_UI_COMPOSE_ACTION_MOVE_BMSON_BGM_TO_CURSOR,
    WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_CONTINUE_AT_CURSOR,
    WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_RESTART_AT_CURSOR,
    WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_CONTINUE_AT_CURSOR,
    WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_RESTART_AT_CURSOR,
    WORM_UI_COMPOSE_ACTION_COMMIT_STAGED_BMSON_BGM,
    WORM_UI_COMPOSE_ACTION_CANCEL_STAGED_BMSON_BGM,
    WORM_UI_COMPOSE_ACTION_DELETE_BMSON_BGM_MARKER,
    WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE,
    WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL,
    WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL,
    WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION,
    WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION,
    WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL,
    WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN,
    WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END,
    WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION,
    WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION,
    WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_SECTION,
    WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION,
    WORM_UI_COMPOSE_ACTION_NAVIGATE_PREVIOUS_AUDIO_MARKER,
    WORM_UI_COMPOSE_ACTION_NAVIGATE_NEXT_AUDIO_MARKER,
    WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_CLEAR_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_REMOVE_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_PAINT_MODE,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK,
    WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER_NOTES,
    WORM_UI_COMPOSE_ACTION_PREVIOUS_SEMANTIC_LAYER_PAGE,
    WORM_UI_COMPOSE_ACTION_NEXT_SEMANTIC_LAYER_PAGE,
    WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR,
    WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS,
    WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP,
    WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER,
    WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER,
    WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE,
    WORM_UI_COMPOSE_ACTION_CLEAR_BMS_BRANCH_CHOICE,
} WormUiComposeActionKind;

typedef enum WormUiComposeInputKind {
    WORM_UI_COMPOSE_INPUT_NONE = 0,
    WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_BACKWARD,
    WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD,
    WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
    WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE,
    WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE,
    WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK,
    WORM_UI_COMPOSE_INPUT_BMSON_RESTART_TOGGLE,
    WORM_UI_COMPOSE_INPUT_NOTE_DOUBLE_CLICK,
    WORM_UI_COMPOSE_INPUT_DELETE_NOTES,
    WORM_UI_COMPOSE_INPUT_UNDO,
    WORM_UI_COMPOSE_INPUT_REDO,
    WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS,
    WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE,
    WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN,
    WORM_UI_COMPOSE_INPUT_ALT_POINTER_UPDATE,
    WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE,
    WORM_UI_COMPOSE_INPUT_AREA_SELECTION_BEGIN,
    WORM_UI_COMPOSE_INPUT_AREA_SELECTION_UPDATE,
    WORM_UI_COMPOSE_INPUT_AREA_SELECTION_RELEASE,
    WORM_UI_COMPOSE_INPUT_SPACE_RANGE_SELECTION,
    WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE,
    WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN,
    WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE,
    WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE,
    WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN,
    WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_UPDATE,
    WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE,
    WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN,
    WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE,
    WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE,
    WORM_UI_COMPOSE_INPUT_NAVIGATION_PRESS,
    WORM_UI_COMPOSE_INPUT_NAVIGATION_RELEASE,
    WORM_UI_COMPOSE_INPUT_NAVIGATION_CANCEL,
} WormUiComposeInputKind;

typedef enum WormUiComposeNavigationAction {
    WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD = 0,
    WORM_UI_COMPOSE_NAVIGATION_CURSOR_FORWARD,
    WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD,
    WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD,
} WormUiComposeNavigationAction;

typedef struct WormUiComposeInputAction {
    WormUiComposeInputKind kind;
    uint64_t lane_id;
    uint64_t object_id;
    uint8_t resource_role;
    bool successive;
    WormUiComposeSelectionMode selection_mode;
    WormUiComposeNavigationAction navigation_action;
    double x_pixels;
    double y_pixels;
} WormUiComposeInputAction;

typedef enum WormUiComposeViewportInputKind {
    WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL = 0,
    WORM_UI_COMPOSE_VIEWPORT_INPUT_SMOOTH_WHEEL,
} WormUiComposeViewportInputKind;

typedef struct WormUiComposeViewportInput {
    WormUiComposeViewportInputKind kind;
    bool zoom_modifier;
    double delta_x;
    double delta_y;
    double cursor_x_pixels;
    double cursor_y_pixels;
} WormUiComposeViewportInput;

typedef enum WormUiMetadataEditKind {
    WORM_UI_METADATA_EDIT_NONE = 0,
    WORM_UI_METADATA_EDIT_SONG,
    WORM_UI_METADATA_EDIT_CHART,
    WORM_UI_METADATA_EDIT_AUDIO_OFFSET,
    WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD,
    WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD,
    WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD,
} WormUiMetadataEditKind;

typedef struct WormUiWorkspaceAction {
    WormUiWorkspaceActionKind kind;
    WormUiWorkspaceTab tab;
    uint64_t recovery_scan_generation;
    uint64_t transport_seek_frame;
    uint64_t audio_asset_id;
    uint64_t visual_asset_id;
    WormUiPlayerPreviewSkin player_preview_skin;
    WormUiStepmaniaFileProfile stepmania_file_profile;
    WormUiStepmaniaTimingSyntax stepmania_timing_syntax;
    WormUiStepmaniaImplementation stepmania_implementation;
    WormUiAudioOffsetDragAction audio_offset_drag_action;
    uint64_t audio_offset_drag_chart_id;
    uint64_t audio_offset_drag_document_revision;
    uint32_t audio_offset_drag_sample_rate;
    int64_t audio_offset_drag_delta_frames;
    size_t recovery_row_index;
    size_t recovery_candidate_index;
    char recovery_save_as_destination[WORM_UI_RECOVERY_DESTINATION_CAPACITY];
    // Metadata pointers refer to frontend-owned staging buffers and are valid
    // only until the next compose call; the app dispatches them immediately.
    WormUiMetadataEditKind metadata_edit;
    uint64_t metadata_document_revision;
    uint64_t metadata_chart_id;
    bool metadata_has_chart_rating;
    const char* metadata_chart_rating_numerator_text;
    const char* metadata_chart_rating_denominator_text;
    const char* metadata_audio_offset_numerator_text;
    const char* metadata_audio_offset_denominator_text;
    const char* metadata_title;
    const char* metadata_artist;
    const char* metadata_chart_name;
    const char* metadata_chart_author;
    const char* const* metadata_search_tags;
    size_t metadata_search_tag_count;
    bool metadata_format_chart_scoped;
    uint8_t metadata_format_family;
    size_t metadata_format_index;
    const char* metadata_format_name;
    const char* metadata_format_value;
    WormUiComposeActionKind compose_action;
    uint64_t compose_input_chart_id;
    uint64_t compose_input_document_revision;
    double compose_input_viewport_width_pixels;
    bool compose_has_pointer_hover;
    double compose_pointer_hover_x_pixels;
    double compose_pointer_hover_y_pixels;
    bool compose_viewport_pointer_valid;
    double compose_viewport_pointer_x_pixels;
    double compose_viewport_pointer_y_pixels;
    bool compose_viewport_middle_click;
    double compose_viewport_middle_click_x_pixels;
    double compose_viewport_middle_click_y_pixels;
    bool compose_note_label_focus_consumed;
    char compose_note_label[WORM_UI_COMPOSE_NOTE_LABEL_BYTES + 1];
    uint64_t compose_bookmark_id;
    uint64_t compose_section_id;
    char compose_marker_label[WORM_UI_COMPOSE_MARKER_LABEL_BYTES + 1];
    char compose_marker_audio_begin[WORM_UI_COMPOSE_MARKER_TIME_BYTES];
    char compose_marker_audio_end[WORM_UI_COMPOSE_MARKER_TIME_BYTES];
    uint64_t compose_semantic_layer_id;
    uint64_t compose_resource_id;
    uint64_t compose_bmson_bgm_event_id;
    uint64_t compose_resource_asset_id;
    uint8_t compose_resource_role;
    char compose_import_path[WORM_UI_COMPOSE_IMPORT_PATH_BYTES + 1];
    uint8_t compose_timing_lane;
    uint64_t compose_branch_id;
    int64_t compose_branch_value;
    char compose_semantic_layer_name[WORM_UI_COMPOSE_SEMANTIC_LAYER_NAME_CAPACITY];
    uint8_t compose_semantic_layer_red;
    uint8_t compose_semantic_layer_green;
    uint8_t compose_semantic_layer_blue;
    uint8_t compose_semantic_layer_alpha;
    WormUiComposeInputAction compose_inputs[WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY];
    size_t compose_input_count;
    WormUiComposeViewportInput compose_viewport_inputs[WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY];
    size_t compose_viewport_input_count;
    WormUiTimingGridAction timing_grid_action;
    uint64_t timing_grid_chart_id;
    uint64_t timing_grid_document_revision;
    uint64_t timing_grid_denominator;
    WormUiTimingMeterAction timing_meter_action;
    uint64_t timing_meter_chart_id;
    uint64_t timing_meter_document_revision;
    char timing_meter_measure_text[32];
    char timing_meter_numerator_text[32];
    char timing_meter_denominator_text[32];
    WormUiTimingStageAction timing_stage_action;
    uint64_t timing_chart_id;
    uint64_t timing_document_revision;
    uint64_t timing_object_id;
    WormUiTimingLaneKind timing_lane;
    bool timing_has_target_y;
    double timing_target_y_fraction;
    double timing_value;
    double timing_secondary_value;
    bool timing_transition_is_seconds;
    bool timing_toggle_primary;
    WormUiTimingPauseUnit timing_pause_unit;
    char timing_bpm_decimal_text[64];
    bool timing_bpm_decimal_edited;
    int timing_bpm_step;
    char timing_primary_numerator_text[64];
    char timing_primary_denominator_text[64];
    char timing_secondary_numerator_text[64];
    char timing_secondary_denominator_text[64];
    WormUiTimingRecipeAction timing_recipe_action;
    WormUiShortcutEvent shortcut_events[WORM_UI_SHORTCUT_EVENT_CAPACITY];
    size_t shortcut_event_count;
    bool shortcut_overflowed;
    bool shortcut_window_focused;
    bool shortcut_text_entry_active;
    bool shortcut_ime_composition_active;
    bool shortcut_compose_lane_focused;
    uint64_t shortcut_focused_lane_id;
} WormUiWorkspaceAction;

// Private application/frontend boundary. Implementations own all widget
// toolkit state and translate semantic views into typed actions.
bool worm_ui_frontend_init(SDL_Window* window, SDL_Renderer* renderer);
void worm_ui_frontend_shutdown(void);
void worm_ui_frontend_begin_input(void);
bool worm_ui_frontend_handle_event(const SDL_Event* event);
void worm_ui_frontend_end_input(void);
WormUiWorkspaceAction worm_ui_frontend_compose(const WormUiWorkspaceView* view);
void worm_ui_frontend_resource_placed(uint64_t placed_resource_id, uint64_t next_resource_id);
void worm_ui_frontend_render(int window_width, int window_height);

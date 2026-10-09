#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef union SDL_Event SDL_Event;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Window SDL_Window;

#define WORM_UI_WORKSPACE_TAB_COUNT 7
#define WORM_UI_WORKSPACE_SUMMARY_CAPACITY 16
#define WORM_UI_RECOVERY_ROW_CAPACITY 4
#define WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY 4
#define WORM_UI_RECOVERY_DESTINATION_CAPACITY 512
#define WORM_UI_PROJECT_PATH_CAPACITY 4097
#define WORM_UI_METADATA_SEARCH_TAG_CAPACITY 4096
#define WORM_UI_METADATA_SEARCH_TAG_BYTES 4096
#define WORM_UI_METADATA_TITLE_BYTES 16384
#define WORM_UI_METADATA_CHART_TEXT_BYTES 16384
#define WORM_UI_METADATA_FORMAT_NAME_BYTES 1024
#define WORM_UI_METADATA_FORMAT_VALUE_BYTES 1048576
#define WORM_UI_METADATA_OUTPUT_PATH_BYTES 4096
#define WORM_UI_METADATA_FORMAT_GROUP_FIELD_CAPACITY 16384
#define WORM_UI_METADATA_FORMAT_FIELD_CAPACITY 128
#define WORM_UI_METADATA_FORMAT_TARGET_CAPACITY 14
#define WORM_UI_METADATA_CHANNEL_MAP_LANE_CAPACITY 256
#define WORM_UI_METADATA_BATCH_CHART_CAPACITY 256
#define WORM_UI_COMPOSE_BAND_CAPACITY 4096
#define WORM_UI_COMPOSE_LANE_CAPACITY 32768
#define WORM_UI_COMPOSE_GRID_CAPACITY 16384
#define WORM_UI_COMPOSE_DIFFERENCE_CAPACITY 32768
#define WORM_UI_COMPOSE_HOLD_CAPACITY 32768
#define WORM_UI_COMPOSE_NOTE_CAPACITY 32768
#define WORM_UI_COMPOSE_BGM_LANE_CAPACITY 4096
#define WORM_UI_COMPOSE_LANE_PROFILE_CAPACITY 64
#define WORM_UI_COMPOSE_LANE_PROFILE_NAME_BYTES 48
#define WORM_UI_COMPOSE_LAYOUT_PROFILE_CAPACITY 32
#define WORM_UI_COMPOSE_LAYOUT_PROFILE_NAME_BYTES 64
#define WORM_UI_COMPOSE_LAYOUT_PROFILE_ID_BYTES 64
#define WORM_UI_COMPOSE_MEASURE_REMOVAL_MESSAGE_BYTES 192
#define WORM_UI_COMPOSE_LAYOUT_SEGMENT_CAPACITY 2048
#define WORM_UI_COMPOSE_BGM_EVENT_CAPACITY 32768
#define WORM_UI_COMPOSE_BGA_EVENT_CAPACITY 32768
#define WORM_UI_COMPOSE_BGA_OVERLAP_STACK_THRESHOLD 2
#define WORM_UI_COMPOSE_WAVEFORM_ROW_CAPACITY 4096
#define WORM_UI_COMPOSE_CHANNEL_WAVEFORM_ROW_CAPACITY 4096
#define WORM_UI_COMPOSE_WAVEFORM_OVERVIEW_CAPACITY 512
#define WORM_UI_COMPOSE_BGM_BAR_ROW_CAPACITY 1024
// Window chrome taken by the BGM bar row under the transport seek bar (48 px row + 4 px spacing).
#define WORM_UI_BGM_BAR_CHROME_HEIGHT 52.0f
#define WORM_UI_COMPOSE_BGM_BAR_MARK_CAPACITY 1024
#define WORM_UI_COMPOSE_BGM_BAR_BOOKMARK_CAPACITY 128
#define WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY 32768
#define WORM_UI_COMPOSE_LANE_KEY_BINDING_CAPACITY 128
#define WORM_UI_COMPOSE_NOTE_LABEL_BYTES 4096
#define WORM_UI_COMPOSE_QUANTIZE_PRESET_CAPACITY 256
#define WORM_UI_COMPOSE_QUANTIZE_PRESET_NAME_BYTES 64
#define WORM_UI_COMPOSE_QUANTIZE_REPORT_CAPACITY 256
#define WORM_UI_COMPOSE_IMPORT_PATH_BYTES 4096
#define WORM_UI_COMPOSE_BOOKMARK_CAPACITY 64
#define WORM_UI_COMPOSE_SECTION_CAPACITY 64
#define WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY 64
#define WORM_UI_COMPOSE_MARKER_LABEL_BYTES 4096
#define WORM_UI_COMPOSE_MARKER_TIME_BYTES 32
#define WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY 16
#define WORM_UI_COMPOSE_DEAD_SPAN_CAPACITY 512
#define WORM_UI_COMPOSE_KIAI_SPAN_CAPACITY 512
#define WORM_UI_COMPOSE_RECIPE_AREA_CAPACITY 256
#define WORM_UI_COMPOSE_SPEED_SPAN_CAPACITY 4096
#define WORM_UI_COMPOSE_RECIPE_GRAPH_CAPACITY 16384
#define WORM_UI_COMPOSE_SV_GRAPH_CAPACITY 8192
#define WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY 16
#define WORM_UI_SHORTCUT_EVENT_CAPACITY 64
#define WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY 96
#define WORM_UI_COMPOSE_BRANCH_CHOICE_CAPACITY 96
#define WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY 4
#define WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY 9
#define WORM_UI_COMPOSE_SEMANTIC_LAYER_WINDOW_CAPACITY 256
#define WORM_UI_COMPOSE_NOTE_LAYER_WINDOW_CAPACITY 18
#define WORM_UI_COMPOSE_SEMANTIC_LAYER_NAME_CAPACITY 128
#define WORM_UI_COMPOSE_RESOURCE_CAPACITY 128
#define WORM_UI_COMPOSE_KEYSOUND_CAPACITY 4096
#define WORM_UI_SOUND_DROP_CAPACITY 1295
#define WORM_UI_COMPOSE_KEYSOUND_LAYER_CAPACITY 64
#define WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY 32
#define WORM_UI_TIMING_SPEED_CAPACITY 4096
#define WORM_UI_TIMING_MARKER_CAPACITY 16384
#define WORM_UI_TIMING_GRID_CAPACITY 16384
#define WORM_UI_TIMING_RECIPE_RATIONAL_TEXT_BYTES 64
#define WORM_UI_TIMING_RECIPE_INTEGER_TEXT_BYTES 32
#define WORM_UI_AUDIO_ASSET_CAPACITY 128
#define WORM_UI_SOUND_ROW_CAPACITY 512
#define WORM_UI_SOUND_USAGE_ROW_CAPACITY 1024
#define WORM_UI_AUDIO_WAVEFORM_COLUMN_CAPACITY 4096
#define WORM_UI_PLAYER_PREVIEW_LANE_CAPACITY 64
#define WORM_UI_PLAYER_PREVIEW_NOTE_CAPACITY 4096
#define WORM_UI_PLAYER_PREVIEW_HOLD_CAPACITY 4096
#define WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER_CAPACITY 6
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
    WORM_UI_WORKSPACE_ACTION_NEW_PROJECT,
    WORM_UI_WORKSPACE_ACTION_OPEN_FILE,
    WORM_UI_WORKSPACE_ACTION_CANCEL_BMS_ENCODING,
    WORM_UI_WORKSPACE_ACTION_CLOSE_PROJECT,
    WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT,
    WORM_UI_WORKSPACE_ACTION_SWITCH_TAB,
    WORM_UI_WORKSPACE_ACTION_QUIT,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_STOP,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_TOGGLE_METRONOME,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_RATE,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_KEEP_PITCH,
    WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_LOOP_SELECTION,
    WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET,
    WORM_UI_WORKSPACE_ACTION_SELECT_SOUND_RESOURCE,
    WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE,
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
    WORM_UI_WORKSPACE_ACTION_CANCEL_AUDIO_PROJECT,
    WORM_UI_WORKSPACE_ACTION_COMPARE_SELECT_CHART,
    WORM_UI_WORKSPACE_ACTION_COMPARE_SCROLL,
    WORM_UI_WORKSPACE_ACTION_COMPARE_DIAGNOSTIC_JUMP,
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

#define WORM_UI_PROJECT_CHART_CAPACITY 256

typedef struct WormUiProjectChartRow {
    uint64_t chart_id;
    const char* name;
    const char* author;
    const char* rating;
    const char* mode;
    uint32_t key_count;
    const char* output_groups;
    uint64_t note_count;
    const char* length;
    const char* diagnostic;
} WormUiProjectChartRow;

typedef enum WormUiProjectChartActionKind {
    WORM_UI_PROJECT_CHART_NONE = 0,
    WORM_UI_PROJECT_CHART_NEW,
    WORM_UI_PROJECT_CHART_DUPLICATE,
    WORM_UI_PROJECT_CHART_DELETE,
    WORM_UI_PROJECT_CHART_UP,
    WORM_UI_PROJECT_CHART_DOWN,
    WORM_UI_PROJECT_CHART_OPEN,
    WORM_UI_PROJECT_CHART_BATCH_METADATA,
} WormUiProjectChartActionKind;

typedef struct WormUiProjectChartAction {
    WormUiProjectChartActionKind kind;
    uint64_t document_revision;
    uint64_t session_generation;
    uint64_t chart_id;
    size_t selected_count;
    uint64_t selected_chart_ids[WORM_UI_PROJECT_CHART_CAPACITY];
    bool edit_author;
    const char* author;
    bool edit_rating;
    bool unset_rating;
    const char* rating_numerator;
    const char* rating_denominator;
} WormUiProjectChartAction;

typedef struct WormUiSetupView {
    bool visible;
    uint64_t song_id;
    uint64_t document_revision;
    uint64_t session_generation;
    const WormUiProjectChartRow* charts;
    size_t chart_row_count;
    bool charts_truncated;
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

// One lane of the active layout segment on a BMS-family chart. channels is ordered visible,
// invisible, long_note, mine; an unset channel is the empty string.
typedef struct WormUiMetadataChannelMapLaneView {
    uint64_t lane_id;
    const char* name;
    const char* channels[4];
} WormUiMetadataChannelMapLaneView;

// One difficulty offered by the Metadata multi-select; name is valid for the frame.
typedef struct WormUiMetadataChartChoice {
    uint64_t chart_id;
    const char* name;
} WormUiMetadataChartChoice;

typedef struct WormUiAudioWaveformColumnView {
    float minimum[2];
    float maximum[2];
    float absolute_peak[2];
    float rms[2];
} WormUiAudioWaveformColumnView;

typedef enum WormUiMetadataDropDestination {
    WORM_UI_METADATA_DROP_NONE,
    WORM_UI_METADATA_DROP_PROJECT,
    WORM_UI_METADATA_DROP_EXPORT,
    WORM_UI_METADATA_DROP_OUTPUT_PATH,
} WormUiMetadataDropDestination;

typedef struct WormUiMetadataDropTarget {
    WormUiMetadataDropDestination destination;
    size_t output_group_index;
} WormUiMetadataDropTarget;

typedef struct WormUiMetadataView {
    bool visible;
    bool song_fields_editable;
    bool chart_fields_editable;
    bool audio_offset_editable;
    bool audio_offset_locked;
    bool search_tags_editable;
    uint64_t document_revision;
    bool has_chart;
    uint64_t chart_id;
    uint8_t selected_chart_source_family;
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
    uint64_t chart_divisions_per_standard_measure;
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
    const char* project_directory;
    const char* selected_music_source;
    uint64_t selected_music_asset_id;
    uint64_t next_import_asset_id;
    bool import_copy_pending;
    size_t selected_music_resource_count;
    size_t selected_music_channel_count;
    const struct WormUiComposeOutputGroupView* output_groups;
    size_t output_group_count;
    bool output_groups_truncated;
    const char* dropped_music_path;
    WormUiMetadataDropTarget dropped_music_target;
    const char* dropped_output_relative_path;
    size_t dropped_output_group_index;
    const char* copy_preview_text;
    bool copy_preview_pending;
    bool copy_preview_export;
    bool copy_preview_import;
    size_t copy_preview_group_index;
    uint64_t copy_preview_asset_id;
    const char* copy_preview_source;
    uint64_t channel_map_layout_segment_id;
    bool channel_map_lanes_truncated;
    const WormUiMetadataChannelMapLaneView* channel_map_lanes;
    size_t channel_map_lane_count;
    // Preview state of the sealed proposal from the last Preview action; Apply needs it valid,
    // present and conflict-free.
    bool channel_map_preview_present;
    bool channel_map_preview_valid;
    size_t channel_map_preview_affected_note_count;
    size_t channel_map_preview_conflict_count;
    const char* channel_map_preview_message;
    bool channel_map_apply_enabled;
    const WormUiMetadataChartChoice* chart_choices;
    size_t chart_choice_count;
    bool chart_choices_truncated;
} WormUiMetadataView;

typedef struct WormUiSoundRowView {
    uint64_t resource_id;
    uint64_t asset_id;
    const char* name;
    const char* tags_text;
    const char* source_path;
    // 0 unspecified, 1 stem, 2 keysound, 3 sample (sound_kind_t, effective kind).
    int kind;
    uint32_t use_count;
    uint64_t duration_frames;
    uint32_t sample_rate;
    bool missing;
} WormUiSoundRowView;

typedef struct WormUiSoundUsageRowView {
    uint64_t chart_id;
    uint64_t measure_id;
    uint64_t row;
    uint64_t object_id;
    uint64_t channel_id;
    uint64_t clip_id;
    uint64_t referenced_resource_id;
    uint64_t referenced_asset_id;
    int kind;
    int scope;
    bool position_valid;
    const char* chart_label;
    const char* position_label;
} WormUiSoundUsageRowView;

typedef struct WormUiAudioSlicesView {
    const WormUiSoundRowView* sound_rows;
    size_t sound_row_count;
    bool sound_rows_truncated;
    uint64_t selected_resource_id;
    uint64_t selected_channel_id;
    uint64_t selected_clip_id;
    const WormUiSoundUsageRowView* usage_rows;
    size_t usage_row_count;
    size_t usage_total_count;
    bool usage_rows_truncated;
    bool usage_valid;
    uint64_t usage_song_id;
    uint64_t usage_document_revision;
    uint64_t usage_resource_id;
    uint64_t usage_asset_id;
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

typedef struct WormUiSoundUsageRequest {
    uint64_t selected_project_id;
    uint64_t selected_resource_id;
    uint64_t selected_channel_id;
    uint64_t selected_clip_id;
    bool find_uses_open;
} WormUiSoundUsageRequest;

typedef enum WormUiAudioOffsetDragAction {
    WORM_UI_AUDIO_OFFSET_DRAG_NONE = 0,
    WORM_UI_AUDIO_OFFSET_DRAG_BEGIN,
    WORM_UI_AUDIO_OFFSET_DRAG_UPDATE,
    WORM_UI_AUDIO_OFFSET_DRAG_COMMIT,
    WORM_UI_AUDIO_OFFSET_DRAG_CANCEL,
    WORM_UI_AUDIO_OFFSET_NUDGE,
} WormUiAudioOffsetDragAction;

typedef struct WormUiComposeRect {
    double left;
    double top;
    double right;
    double bottom;
} WormUiComposeRect;

typedef struct WormUiComposePasteGhostView {
    WormUiComposeRect bounds;
    uint8_t kind; /* 0 note endpoint, 1 hold body, 2 BGM event, 3 timing object */
    uint8_t glyph; /* note glyph; for kind 3 the WormUiTimingLaneKind. Kind 3 bounds carry only top/bottom */
} WormUiComposePasteGhostView;

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
    uint8_t key_color; // wormedit::lane_key_color_t; 0 = generic.
} WormUiComposeLaneView;

typedef struct WormUiComposeGridLineView {
    double y_pixels;
    uint8_t kind;
    uint8_t emphasis;
    uint32_t measure_number;
    uint64_t chart_row;
} WormUiComposeGridLineView;

typedef struct WormUiComposeLaneLayoutSegmentView {
    uint64_t segment_id;
    uint32_t measure_number;
    uint64_t row;
    double y_pixels;
    const char* profile_name;
} WormUiComposeLaneLayoutSegmentView;

typedef struct WormUiComposeTimingPreviewRectView {
    WormUiComposeRect bounds;
    uint8_t kind; // 0 note endpoint, 1 BGM event, 2 hold body
} WormUiComposeTimingPreviewRectView;

/* A Stop, Delay or Warp as a dead span: time the audio spends where notes cannot sit. Bounds are
   viewport pixels in drawn orientation. */
typedef struct WormUiComposeDeadSpanView {
    WormUiComposeRect bounds;
    uint8_t kind; /* 0 stop, 1 delay, 2 warp */
    uint64_t object_id;
    const char* label;
} WormUiComposeDeadSpanView;

/* One merged Kiai area. Only top/bottom of bounds are meaningful: the area is drawn inside the
   Kiai timing lane. Head is the opening edge in chart order, tail the closing edge. */
typedef struct WormUiComposeKiaiSpanView {
    WormUiComposeRect bounds;
    uint64_t head_object_id;
    uint64_t tail_object_id; /* 0 when the area runs to the end of the chart */
    uint64_t begin_row;
    uint64_t end_row;
    bool open_ended;
    bool head_visible;
    bool tail_visible;
    bool selected;
} WormUiComposeKiaiSpanView;

/* One vertex of the SV graph. x is normalized to the visible value range in [0, 1] and drawn
   inside the SV timing lane; y is a viewport pixel row. */
typedef struct WormUiComposeSvGraphPointView {
    double x_fraction;
    double y_pixels;
} WormUiComposeSvGraphPointView;

/* One stored SV recipe, shown as an area in its timing lane (SV for sv/scrolls output, SPD for
   speeds). Only top/bottom of bounds are meaningful. The recipe's own relative value is drawn as a
   continuous line normalized to the visible range: graph_count vertices starting at graph_first
   in the view's recipe_graph_points. */
typedef struct WormUiComposeRecipeAreaView {
    WormUiComposeRect bounds;
    uint64_t recipe_id;
    const char* label;
    uint8_t lane; /* WormUiTimingLaneKind */
    uint8_t curve;
    bool enabled;
    bool selected;
    bool evaluable;
    bool begin_visible;
    bool end_visible;
    /* The graph shows the unapplied form candidate instead of the stored recipe. */
    bool previewing;
    size_t graph_first;
    size_t graph_count;
} WormUiComposeRecipeAreaView;

/* The transition extent of one StepMania SPEEDS event, drawn in the SPD lane (top/bottom of bounds
   only). transition_length is in beats, or seconds when transition_is_seconds. */
typedef struct WormUiComposeSpeedSpanView {
    WormUiComposeRect bounds;
    uint64_t object_id;
    double multiplier;
    double transition_length;
    bool transition_is_seconds;
    bool clipped;
} WormUiComposeSpeedSpanView;

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
    /* Stored and shown, not editable or evaluable yet (native schema 2, T-189). */
    WORM_UI_TIMING_RECIPE_CURVE_LUA_EXPRESSION,
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

/* The selected SV recipe's editable form, as stored in the document. name and expression_source
   are valid for the frame; expression_source is non-empty only for a Lua expression recipe, whose
   curve cannot be edited yet. status_text carries the last preview or apply diagnostic. */
typedef struct WormUiComposeRecipeFormView {
    bool visible;
    bool enabled;
    uint64_t recipe_id;
    const char* name;
    const char* expression_source;
    const char* status_text;
    WormUiTimingRecipeForm form;
} WormUiComposeRecipeFormView;

typedef struct WormUiComposeDifferenceView {
    WormUiComposeRect bounds;
} WormUiComposeDifferenceView;

typedef struct WormUiComposeHoldView {
    WormUiComposeRect bounds;
    uint64_t note_id;
    uint64_t lane_id;
    uint32_t display_lane_number; // 1-based chart layout order; 0 when unavailable.
    uint8_t key_color; // lane key color of the owning lane; 0 = generic.
    uint8_t snap_class; // subdivision of the owning note head.
    bool selected;
    bool proposal_origin;
    bool proposal_ghost;
    bool semantic_color_visible;
    uint8_t semantic_red;
    uint8_t semantic_green;
    uint8_t semantic_blue;
    uint8_t semantic_alpha;
    uint8_t move_conflict; // 0 = valid; core note_move_conflict_t otherwise.
    bool move_conflict_occupant;
} WormUiComposeHoldView;

typedef struct WormUiComposeNoteView {
    WormUiComposeRect bounds;
    uint64_t note_id;
    uint64_t lane_id;
    uint32_t display_lane_number; // 1-based chart layout order; 0 when unavailable.
    uint8_t key_color; // lane key color of the owning lane; 0 = generic.
    const char* sound_label;
    const char* sound_detail;
    uint8_t glyph;
    uint8_t snap_class; // 4, 8, 12, 16, 24, 32, 48, 64; 0 = other.
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
    uint8_t move_conflict; // 0 = valid; core note_move_conflict_t otherwise.
    bool move_conflict_occupant;
} WormUiComposeNoteView;

typedef struct WormUiComposeCursorView {
    WormUiComposeRect bounds;
    uint64_t lane_id;
} WormUiComposeCursorView;

typedef struct WormUiComposeBgmLaneView {
    WormUiComposeRect bounds;
    uint64_t source_lane_id;
    size_t source_order;
    bool stem;
    double width_pixels;
    double resize_edge_pixels;
    const char* filename;
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
    bool has_tail;
    bool proposal_ghost;
    bool move_conflict;
    const char* sound_label;
    const char* sound_detail;
    bool semantic_color_visible;
    uint8_t semantic_red;
    uint8_t semantic_green;
    uint8_t semantic_blue;
    uint8_t semantic_alpha;
    float head_top_pixels;
    float head_bottom_pixels;
} WormUiComposeBgmEventView;

typedef struct WormUiComposeBgmDraftView {
    WormUiComposeRect bounds;
    bool restart;
    bool playable;
} WormUiComposeBgmDraftView;

typedef struct WormUiComposeWaveformRowView {
    double y_pixels;
    float absolute_peak[2];
    float rms[2];
    bool sample_point;
    float samples[2];
} WormUiComposeWaveformRowView;

typedef struct WormUiComposeWaveformOverviewRowView {
    double audio_fraction;
    float absolute_peak;
} WormUiComposeWaveformOverviewRowView;

typedef struct WormUiComposeBgmBarMarkView {
    double audio_fraction;
    uint32_t measure;
} WormUiComposeBgmBarMarkView;

// A beat or audio bookmark placed on the BGM bar; the label is owned by the bookmark view.
typedef struct WormUiComposeBgmBarBookmarkView {
    double audio_fraction;
    const char* label;
    bool audio;
} WormUiComposeBgmBarBookmarkView;

typedef struct WormUiComposeBgmMovePreviewView {
    WormUiComposeRect bounds;
    bool valid;
} WormUiComposeBgmMovePreviewView;

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
    size_t assigned_bgm_count;
} WormUiComposeSemanticLayerView;

typedef struct WormUiComposeBookmarkView {
    uint64_t bookmark_id;
    const char* label;
    uint64_t measure_number;
    uint64_t row;
    uint64_t chart_row;
    bool projection_visible;
    double projected_y_pixels;
    bool proposed_projection_visible;
    double proposed_y_pixels;
} WormUiComposeBookmarkView;

typedef struct WormUiComposeSectionView {
    uint64_t section_id;
    const char* label;
    uint64_t begin_measure_number;
    uint64_t begin_row;
    uint64_t end_measure_number;
    uint64_t end_row;
    uint64_t begin_chart_row;
    uint64_t end_chart_row;
    bool begin_projection_visible;
    bool end_projection_visible;
    bool span_projection_visible;
    double projected_begin_y_pixels;
    double projected_end_y_pixels;
    double projected_span_top_y_pixels;
    double projected_span_bottom_y_pixels;
    bool proposed_begin_projection_visible;
    bool proposed_end_projection_visible;
    bool proposed_span_projection_visible;
    double proposed_begin_y_pixels;
    double proposed_end_y_pixels;
    double proposed_span_top_y_pixels;
    double proposed_span_bottom_y_pixels;
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
    uint64_t timestamp_ns;
    bool tap_timing_space;
} WormUiShortcutEvent;

typedef enum WormUiShortcutModifier {
    WORM_UI_SHORTCUT_MODIFIER_SHIFT = 1 << 0,
    WORM_UI_SHORTCUT_MODIFIER_CONTROL = 1 << 1,
    WORM_UI_SHORTCUT_MODIFIER_ALT = 1 << 2,
    WORM_UI_SHORTCUT_MODIFIER_META = 1 << 3,
} WormUiShortcutModifier;

typedef enum WormUiWaveformPieceKind {
    WORM_UI_WAVEFORM_PIECE_STEM = 0,
    WORM_UI_WAVEFORM_PIECE_PACKED_EVENT = 2,
} WormUiWaveformPieceKind;

typedef struct WormUiComposeStemWaveformView {
    /* Stable channel identity, independent of visible ordering and lane position. */
    uint64_t stem_key;
    uint64_t source_lane_id;
    const WormUiComposeWaveformRowView* rows;
    size_t row_count;
    /* The audio is still decoding or missing: draw a placeholder band instead of a waveform. */
    bool pending;
    WormUiWaveformPieceKind piece_kind;
    /* Packed pending bands use event-bounded viewport coordinates. */
    bool bounded;
    double begin_y_pixels;
    double end_y_pixels;
} WormUiComposeStemWaveformView;

/* Where a stem's audio is cut: the row of one slice point, drawn in the stem's BGM lane even when
   a note in the field stands for the point. Continue points are green and restarts orange; they
   light up with the point's tile (the note when object_is_note, else the BGM marker). */
typedef struct WormUiComposeStemCutView {
    double y_pixels;
    uint64_t bgm_lane_id;
    bool restart;
    bool object_is_note;
    uint64_t object_id;
} WormUiComposeStemCutView;

typedef struct WormUiComposeBranchRowView {
    const char* display_text;
    size_t depth;
    bool active;
    bool selected;
    bool display_text_truncated;
    bool has_source_order;
    size_t source_order;
    /* The control node this row shows. Raw statements are the lines the parser did not understand;
       choice blocks are #RANDOM/#SWITCH, which carry a WormUiComposeBranchChoiceView by branch_id. */
    uint64_t node_id;
    bool raw_statement;
    bool choice_block;
    /* Nonzero only for raw statements; identity and full text are borrowed from the retained tree. */
    uint64_t statement_id;
    const char* statement_text;
    size_t statement_text_bytes;
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
    WORM_UI_TIMING_LANE_COUNT,
} WormUiTimingLaneKind;

typedef struct WormUiTimingMarkerView {
    uint64_t object_id;
    uint64_t source_order;
    uint64_t storage_order;
    double marker_y_pixels;
    double marker_left_fraction;
    double marker_right_fraction;
    uint64_t chart_row;
    WormUiTimingLaneKind lane;
    const char* short_label;
    const char* detail;
    bool fixed_initial;
    bool selected;
} WormUiTimingMarkerView;

typedef enum WormUiComposeResourceRole {
    WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE = 1 << 0,
    WORM_UI_COMPOSE_RESOURCE_ROLE_BGM = 1 << 1,
    WORM_UI_COMPOSE_RESOURCE_ROLE_BGA = 1 << 2,
    /* Import-only: sets or replaces the song's main audio. */
    WORM_UI_COMPOSE_RESOURCE_ROLE_MUSIC = 1 << 3,
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

/* One row of the Compose keysound listbox: a placeable sample of the catalog. */
typedef struct WormUiComposeBgaRowView {
    uint64_t resource_id;
    const char* slot;
    const char* name;
    uint32_t use_count;
    bool missing;
    bool video;
    uint64_t image_content_id;
    uint32_t image_width, image_height;
    const uint8_t* rgba_pixels;
    size_t rgba_byte_count;
} WormUiComposeBgaRowView;

typedef struct WormUiBgaDockRequest {
    bool visible;
    bool ids_available;
    char search[64];
    size_t first_row, row_limit;
    bool sort_by_id;
} WormUiBgaDockRequest;

typedef struct WormUiKeysoundDockRequest {
    bool sort_by_id;
    bool slots_mode;
    uint16_t selected_slot;
    char search[64];
    size_t first_row, row_limit;
    char automatic_search[64];
    bool group_filter_active;
    const uint64_t* group_filter_ids;
    size_t group_filter_count;
    bool include_ungrouped;
    uint8_t layer_filter_kind; /* 0 any, 1 no layer, 2 specific layer */
    uint64_t layer_filter_id;
    const uint64_t* collapsed_group_ids;
    size_t collapsed_group_count;
    bool ungrouped_collapsed;
    const uint64_t* selected_resource_ids;
    size_t selected_resource_count;
    bool include_selected_metadata;
    size_t automatic_first_row, automatic_row_limit;
    uint64_t primary_resource_id;
    size_t filter_layer_first_row, filter_layer_row_limit;
} WormUiKeysoundDockRequest;

typedef struct WormUiBookmarkChooserRequest {
    size_t first_row, row_limit;
    bool open;
} WormUiBookmarkChooserRequest;

typedef struct WormUiBookmarkChooserRowView {
    uint64_t bookmark_id;
    uint8_t anchor;
    const char* position_label;
    const char* name;
    const char* display_label;
    bool navigation_available;
} WormUiBookmarkChooserRowView;

typedef struct WormUiBookmarkChooserView {
    uint64_t project_id, chart_id, document_revision;
    const WormUiBookmarkChooserRowView* rows;
    size_t row_count, total_count, first_row;
    bool truncated;
} WormUiBookmarkChooserView;

enum {
    WORM_UI_KEYSOUND_LAYER_FILTER_ANY = 0,
    WORM_UI_KEYSOUND_LAYER_FILTER_NONE = 1,
    WORM_UI_KEYSOUND_LAYER_FILTER_SPECIFIC = 2,
};

typedef struct WormUiComposeSoundGroupView {
    uint64_t group_id;
    const char* name;
} WormUiComposeSoundGroupView;

typedef struct WormUiComposeKeysoundRowView {
    uint64_t resource_id;
    /* BMS #WAV index such as "01", or an empty string. */
    const char* slot;
    const char* name;
    bool missing;
    /* Bit i is set when the sound's default layers include keysound_layers[i]. */
    uint64_t layer_mask;
    /* Bit i means adding keysound_layers[i] conflicts with existing note layers. */
    uint64_t note_layer_conflict_mask;
    uint64_t sound_group_id;
    uint64_t stem_source_lane_id;
    const char* stem_lane_name;
} WormUiComposeKeysoundRowView;

typedef enum WormUiComposeKeysoundDisplayRowKind {
    WORM_UI_COMPOSE_KEYSOUND_GROUP_HEADER = 0,
    WORM_UI_COMPOSE_KEYSOUND_SOUND_ROW = 1,
} WormUiComposeKeysoundDisplayRowKind;

typedef struct WormUiComposeKeysoundDisplayRowView {
    WormUiComposeKeysoundDisplayRowKind kind;
    uint64_t group_id;
    const char* group_name;
    size_t group_sound_count;
    bool expanded;
    uint64_t resource_id;
} WormUiComposeKeysoundDisplayRowView;

typedef struct WormUiComposeKeysoundAdvanceRowView {
    uint64_t resource_id;
    bool missing;
} WormUiComposeKeysoundAdvanceRowView;

typedef struct WormUiComposeKeysoundSelectionView {
    uint64_t resource_id;
    uint64_t group_id;
    uint64_t layer_mask;
    uint64_t note_layer_conflict_mask;
    const char* slot;
    bool missing;
} WormUiComposeKeysoundSelectionView;

typedef struct WormUiComposeBgaEventView {
    uint64_t event_id;
    uint64_t resource_id;
    uint64_t canvas_row;
    size_t overlap_count;
    uint8_t kind;
    const char* name;
    const char* bmp_id;
    bool missing;
    bool selected;
    WormUiComposeRect bounds;
} WormUiComposeBgaEventView;

typedef struct WormUiComposeSoundSlotRowView {
    uint16_t number;
    const char* slot;
    uint64_t resource_id;
    const char* name;
    uint32_t use_count;
    bool missing, placeable;
} WormUiComposeSoundSlotRowView;

typedef struct WormUiComposeKeysoundLayerView {
    uint64_t layer_id;
    const char* name;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
    bool locked;
} WormUiComposeKeysoundLayerView;

typedef struct WormUiComposeOutputGroupView {
    size_t document_index;
    const char* name;
    const char* relative_output_path;
    const char* relative_asset_directory;
} WormUiComposeOutputGroupView;

typedef struct WormUiComposeMeasureRowView {
    uint64_t measure_id;
    /* The number "go to measure" takes: #000-based for BMS charts, 1-based otherwise. */
    uint64_t number;
    uint64_t length_numerator, length_denominator;
    bool has_meter;
    uint32_t meter_numerator, meter_denominator;
} WormUiComposeMeasureRowView;

typedef struct WormUiComposeQuantizePresetView {
    const char* name;
    uint64_t divisions;
    uint8_t space;
    uint8_t tie;
    uint8_t holds;
} WormUiComposeQuantizePresetView;

typedef struct WormUiComposeQuantizePositionView {
    uint64_t measure_id;
    uint64_t row;
} WormUiComposeQuantizePositionView;

typedef struct WormUiComposeQuantizeReportRowView {
    uint64_t object_id;
    uint64_t lane_id;
    uint64_t start_before_absolute_row;
    uint64_t start_after_absolute_row;
    WormUiComposeQuantizePositionView start_before;
    WormUiComposeQuantizePositionView start_after;
    bool has_end_before;
    bool has_end_after;
    uint64_t end_before_absolute_row;
    uint64_t end_after_absolute_row;
    WormUiComposeQuantizePositionView end_before;
    WormUiComposeQuantizePositionView end_after;
    bool moved;
    bool collision;
    uint64_t collision_peer_id;
    bool has_collision_peer;
    bool selected;
    bool head_halfway;
    bool tail_halfway;
    bool stationary_tail_annotation;
    bool bgm;
} WormUiComposeQuantizeReportRowView;

typedef struct WormUiComposeQuantizeGhostView {
    uint64_t object_id;
    uint64_t lane_id;
    uint32_t before_head_glyph;
    uint32_t after_head_glyph;
    uint32_t before_tail_glyph;
    uint32_t after_tail_glyph;
    WormUiComposeRect before_head;
    WormUiComposeRect after_head;
    WormUiComposeRect before_tail;
    WormUiComposeRect after_tail;
    double before_head_anchor_x, before_head_anchor_y;
    double after_head_anchor_x, after_head_anchor_y;
    double before_tail_anchor_x, before_tail_anchor_y;
    double after_tail_anchor_x, after_tail_anchor_y;
    bool has_tail;
    bool moved_head;
    bool moved_tail;
    bool head_halfway;
    bool tail_halfway;
    bool stationary_tail_annotation;
    bool collision;
} WormUiComposeQuantizeGhostView;

// Renderer-neutral field primitives projected into bounded scalar storage.
// Coordinates are relative to the scene viewport; no chart/core types cross
// the private frontend boundary.
typedef struct WormUiComposeView {
    // A modeless program window owns this frame's content input.
    bool input_occluded;
    uint32_t armed_note_kind;
    uint32_t allowed_note_kind_mask;
    bool note_kind_palette_visible, lane_key_legend_visible;
    // Editor preference: whether clicks, placements, and selections play their sound.
    bool audition_on_click;
    uint32_t area_band_alpha;
    bool visible;
    bool scene_available;
    // Full chart index presence, independent of viewport and visibility filters.
    bool has_bgm_events;
    // BMS charts only: the BGM source column count and whether the toolbar may change it.
    uint32_t bgm_column_count;
    bool bgm_column_count_editable;
    // Lane profiles the layout at the cursor can switch to (0 = none); the current one is marked.
    uint8_t lane_profile_choice_count;
    int8_t lane_profile_current;
    char lane_profile_names[WORM_UI_COMPOSE_LANE_PROFILE_CAPACITY]
                           [WORM_UI_COMPOSE_LANE_PROFILE_NAME_BYTES];
    // Layout profiles can change the lane count; this editor is separate from
    // the same-count profile switch above.
    uint8_t lane_layout_profile_choice_count;
    int8_t lane_layout_profile_current;
    uint32_t lane_layout_edit_measure_number;
    uint64_t lane_layout_edit_segment_id;
    bool lane_layout_edit_is_update;
    bool lane_layout_edit_available;
    // Removing the cursor measure: `measure_removal_available` is false at a virtual cursor.
    // Nonempty measures confirm first, and `measure_removal_message` explains a blocked removal.
    bool measure_removal_available;
    bool measure_removal_has_contents;
    bool measure_removal_blocked;
    uint64_t measure_removal_measure_id;
    char measure_removal_message[WORM_UI_COMPOSE_MEASURE_REMOVAL_MESSAGE_BYTES];
    size_t measure_removal_deleted_notes, measure_removal_deleted_bgm_events;
    size_t measure_removal_deleted_bga_events, measure_removal_shortened_holds;
    size_t measure_removal_moved_positions;
    char lane_layout_profile_names[WORM_UI_COMPOSE_LAYOUT_PROFILE_CAPACITY]
                                  [WORM_UI_COMPOSE_LAYOUT_PROFILE_NAME_BYTES];
    char lane_layout_profile_ids[WORM_UI_COMPOSE_LAYOUT_PROFILE_CAPACITY]
                                [WORM_UI_COMPOSE_LAYOUT_PROFILE_ID_BYTES];
    const WormUiComposeLaneLayoutSegmentView* lane_layout_segments;
    size_t lane_layout_segment_count;
    bool downscroll;
    bool bms_measure_labels;
    // The active chart came from BMSON, so the Sounds dock offers its BMSON tab.
    bool bmson_tools_available;
    uint64_t compose_measure_count;
    uint32_t selected_bms_measure_number_plus_one;
    bool truncated;
    uint8_t draw_source_kind; /* 0 none, 1 sound, 2 BGA */
    uint64_t draw_source_id;
    const char* draw_source_label; /* frame-owned, bounded UTF-8; includes resource identity */
    bool draw_source_missing;
    bool draw_source_is_stem;
    uint64_t draw_source_stem_lane_id;
    const char* draw_source_stem_lane_name;
    size_t semantic_layer_target_row;
    bool note_paint_mode_active;
    bool semantic_layer_paint_mode_active;
    bool semantic_layer_gesture_active;
    size_t selected_note_count;
    bool same_sound_selection_available;
    bool move_to_bgm_available;
    char move_to_bgm_shortcut[64];
    // Grids the chart can snap to (the Quantize chips outside it are quantize-only).
    uint64_t supported_snap_denominators[32];
    size_t supported_snap_denominator_count;
    // Quantization request currently selected in the inspector.
    uint64_t quantize_divisions;
    uint8_t quantize_space;
    uint8_t quantize_tie;
    uint8_t quantize_holds;
    bool quantize_selected_preset_valid;
    size_t quantize_selected_preset_index;
    const WormUiComposeQuantizePresetView* quantize_presets;
    size_t quantize_preset_count;
    /* Measures dock rows in chart order, bounded; `measure_row_ids` lists their IDs for selection. */
    const WormUiComposeMeasureRowView* measure_rows;
    const uint64_t* measure_row_ids;
    size_t measure_row_count;
    bool measure_rows_truncated;
    // Dry-run counts and bounded report for the current request.
    size_t quantize_preview_moved, quantize_preview_unchanged, quantize_preview_blocked;
    size_t quantize_preview_colliding_objects;
    size_t quantize_preview_halfway_notes, quantize_preview_holds;
    bool quantize_q_bound;
    const WormUiComposeQuantizeReportRowView* quantize_preview_rows;
    size_t quantize_preview_row_count;
    size_t quantize_preview_total_rows;
    bool quantize_preview_rows_truncated;
    uint64_t quantize_source_resolution;
    uint64_t quantize_display_resolution;
    uint64_t quantize_resolution_scale;
    bool quantize_geometry_valid;
    bool quantize_preview_drawable;
    bool quantize_ghosts_drawable;
    const WormUiComposeQuantizeGhostView* quantize_ghosts;
    size_t quantize_ghost_count;
    char quantize_preview_message[96];
    // Report from the most recently applied request, retained across frames.
    const WormUiComposeQuantizeReportRowView* quantize_applied_rows;
    size_t quantize_applied_row_count;
    size_t quantize_applied_total_rows;
    bool quantize_applied_rows_truncated;
    size_t selected_object_count;
    uint64_t selected_assigned_resource_id;
    uint64_t selected_bgm_event_id;
    uint64_t selected_bmson_bgm_event_id;
    uint64_t selected_bmson_note_event_id;
    uint64_t selected_bmson_source_lane_id;
    size_t staged_bmson_marker_count;
    const uint64_t* staged_bmson_marker_playable_lanes;
    const uint8_t* staged_bmson_marker_restarts;
    bool beat_marker_navigation_available;
    bool audio_marker_navigation_available;
    bool audio_marker_transport_available;
    const WormUiComposeBookmarkView* beat_bookmarks;
    size_t beat_bookmark_count;
    bool beat_bookmarks_truncated;
    const WormUiComposeSectionView* beat_sections;
    size_t beat_section_count;
    bool beat_sections_truncated;
    bool area_range_available;
    bool area_range_virtual;
    bool area_delete_supported;
    bool transport_loop_selection_available;
    bool transport_loop_selection_enabled;
    uint64_t area_range_begin_row;
    uint64_t area_range_end_row;
    uint64_t area_canvas_rows;
    uint64_t area_rows_per_measure;
    uint64_t area_snap_denominator;
    // False while pointer edits land on the finest chart row instead of the active grid.
    bool edit_snap_enabled;
    /* Y-lock: dragging notes changes lanes only, never rows. */
    bool y_lock;
    double area_scroll_pixels;
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
    size_t semantic_layer_first_row;
    const WormUiComposeSemanticLayerView* note_menu_layers;
    size_t note_menu_layer_count;
    size_t note_menu_layer_first_row;
    size_t note_menu_layer_total_count;
    WormUiComposeSemanticLayerView semantic_layer_target;
    bool semantic_layer_target_none;
    size_t semantic_layer_unassigned_note_count;
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
    // Exact semantic filter changes invalidate retained static field pixels.
    uint64_t static_scene_style_generation;
    // Immutable expanded scalar scene; unavailable on truncated/transient windows.
    const struct WormUiComposeView* static_scene;
    uint64_t static_scene_source_generation;
    double static_scene_world_origin_pixels;
    double static_scene_covered_begin_pixels;
    double static_scene_covered_end_pixels;
    size_t static_scene_accounted_bytes;
    double viewport_width_pixels;
    double viewport_height_pixels;
    double bgm_horizontal_scroll_pixels;
    double bgm_maximum_horizontal_scroll_pixels;
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
    bool bga_lane_visible;
    const WormUiComposeBgaEventView* bga_events;
    size_t bga_event_count;
    WormUiComposeRect bga_lane_bounds;
    uint8_t bga_revealed_lane_mask;
    size_t bga_lane_count;
    WormUiComposeRect bga_lane_rects[4];
    uint8_t bga_lane_kinds[4];
    bool bga_move_preview_valid;
    uint64_t bga_move_preview_event_id;
    double bga_move_preview_y_pixels;
    const WormUiComposeBgaRowView* bga_rows;
    size_t bga_row_count, bga_total_matching, bga_first_row;
    size_t bga_ids_used, bga_ids_total, bga_largest_free_run;
    bool bga_ids_available;
    const uint64_t* bga_matching_ids;
    const uint64_t* bga_advance_ids;
    size_t bga_advance_count;
    const char* bga_search;
    bool paint_ghost_visible;
    WormUiComposeNoteView paint_ghost;
    bool hover_note_visible;
    uint64_t hover_note_id;
    const char* hover_tooltip;
    int64_t hover_timing_source_frame;
    uint64_t hover_timing_total_frames;
    uint32_t hover_timing_sample_rate;
    bool hover_timing_valid;
    char hover_timing_label[64];
    bool viewport_autoscroll_active;
    double viewport_autoscroll_anchor_x_pixels;
    double viewport_autoscroll_anchor_y_pixels;
    double viewport_autoscroll_pointer_x_pixels;
    double viewport_autoscroll_pointer_y_pixels;
    bool clipboard_copy_available;
    bool clipboard_paste_available;
    bool paste_active;
    bool paste_valid;
    const WormUiComposePasteGhostView* paste_ghosts;
    size_t paste_ghost_count;
    const char* paste_diagnostic;
    const WormUiComposeBandView* bands;
    size_t band_count;
    const WormUiComposeLaneView* lanes;
    size_t lane_count;
    const WormUiComposeGridLineView* grid_lines;
    size_t grid_line_count;
    const WormUiComposeGridLineView* proposed_grid_lines;
    size_t proposed_grid_line_count;
    const WormUiComposeTimingPreviewRectView* timing_preview_rects;
    size_t timing_preview_rect_count;
    const WormUiTimingMarkerView* timing_preview_markers;
    size_t timing_preview_marker_count;
    const WormUiTimingMarkerView* timing_markers;
    size_t timing_marker_count;
    const WormUiComposeDeadSpanView* dead_spans;
    size_t dead_span_count;
    const WormUiComposeKiaiSpanView* kiai_spans;
    size_t kiai_span_count;
    const WormUiComposeSpeedSpanView* speed_spans;
    size_t speed_span_count;
    WormUiComposeRecipeFormView recipe_form;
    const WormUiComposeRecipeAreaView* recipe_areas;
    size_t recipe_area_count;
    const WormUiComposeSvGraphPointView* recipe_graph_points;
    size_t recipe_graph_point_count;
    const WormUiComposeSvGraphPointView* sv_graph_points;
    size_t sv_graph_point_count;
    double sv_graph_minimum;
    double sv_graph_maximum;
    double sv_graph_one_fraction; /* negative when 1.0x is outside the visible range */
    /* Interpolated speed curve; same format as the SV graph, drawn in the speed lane. */
    const WormUiComposeSvGraphPointView* speed_graph_points;
    size_t speed_graph_point_count;
    double speed_graph_one_fraction;
    uint8_t timing_lane_visibility_mask;
    // Compose mode specialization; see ui/compose_mode.h. The mask above is
    // already the effective one for this mode.
    bool timing_focus;
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
    /* Keysound listbox: every placeable sample, plus the chart layers its masks index. */
    const WormUiComposeKeysoundRowView* keysounds;
    size_t keysound_count;
    const WormUiComposeKeysoundRowView* keysound_selected_rows;
    size_t keysound_selected_row_count;
    const WormUiComposeKeysoundSelectionView* keysound_selection_rows;
    size_t keysound_selection_row_count;
    const WormUiComposeKeysoundDisplayRowView* keysound_display_rows;
    /* True when section/filter projection was used, including legitimate empty windows. */
    bool keysound_section_projection_available;
    size_t keysound_display_row_count, keysound_display_total_count;
    size_t keysound_display_first_row;
    bool keysound_display_rows_truncated;
    bool keysound_primary_display_index_valid;
    size_t keysound_primary_display_index;
    uint64_t keysound_projected_primary_resource_id;
    bool keysound_projected_filters_unrestricted;
    const uint64_t* keysound_listed_resource_ids;
    size_t keysound_listed_resource_count;
    const WormUiComposeKeysoundAdvanceRowView* keysound_advance_rows;
    size_t keysound_advance_row_count;
    const uint64_t* keysound_all_resource_ids;
    size_t keysound_all_resource_count;
    const WormUiComposeSoundGroupView* compose_sound_groups;
    size_t compose_sound_group_count;
    size_t keysound_total_count;
    bool keysounds_truncated;
    const WormUiComposeKeysoundLayerView* keysound_layers;
    size_t keysound_layer_count;
    const WormUiComposeKeysoundLayerView* keysound_filter_layers;
    size_t keysound_filter_layer_count, keysound_filter_layer_first;
    size_t keysound_filter_layer_total;
    const char* keysound_filter_selected_layer_name;
    bool keysound_filter_selected_layer_valid;
    bool keysounds_available;
    size_t keysound_ids_used, keysound_ids_total, keysound_largest_free_run;
    bool keysound_ids_available;
    const WormUiComposeSoundSlotRowView* sound_slots;
    size_t sound_slot_count, sound_slot_first_row, sound_slot_total_matching;
    bool sound_slots_mode;
    uint16_t sound_slot_selected_number;
    size_t sound_slot_selected_row;
    const WormUiComposeOutputGroupView* output_groups;
    size_t output_group_count;
    bool output_groups_truncated;
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
    const WormUiComposeWaveformRowView* waveform_rows;
    size_t waveform_row_count;
    /* BMSON stems other than the selected one, each drawn in its BGM lane. */
    const WormUiComposeStemWaveformView* stem_waveforms;
    size_t stem_waveform_count;
    const WormUiComposeStemCutView* stem_cuts;
    size_t stem_cut_count;
    const WormUiComposeWaveformRowView* channel_waveform_rows;
    size_t channel_waveform_row_count;
    uint64_t channel_waveform_source_lane_id;
    const WormUiComposeWaveformOverviewRowView* waveform_overview_rows;
    size_t waveform_overview_row_count;
    uint64_t waveform_overview_generation;
    double waveform_overview_visible_begin_fraction;
    double waveform_overview_visible_end_fraction;
    bool waveform_beat_zero_visible;
    double waveform_beat_zero_fraction;
    uint64_t waveform_overview_total_frames;
    uint32_t waveform_overview_sample_rate;
    // The BGM bar shows [begin, end] of the song at full row resolution, with measure starts.
    const WormUiComposeWaveformOverviewRowView* bgm_bar_rows;
    size_t bgm_bar_row_count;
    double bgm_bar_begin_fraction;
    double bgm_bar_end_fraction;
    const WormUiComposeBgmBarMarkView* bgm_bar_marks;
    size_t bgm_bar_mark_count;
    const WormUiComposeBgmBarBookmarkView* bgm_bar_bookmarks;
    size_t bgm_bar_bookmark_count;
    // The bar row exists only for songs with audio and while the toolbar toggle keeps it on:
    // available = the song has audio, reserved = the row is laid out (and takes vertical space).
    bool bgm_bar_available;
    bool bgm_bar_reserved;
    bool source_playhead_available;
    uint64_t source_playhead_asset_id;
    uint64_t source_playhead_frame;
    uint32_t source_playhead_sample_rate;
    uint64_t source_playhead_chart_id;
    uint64_t source_playhead_document_revision;
    bool audio_offset_editable;
    bool audio_offset_locked;
    int64_t waveform_audio_offset_numerator;
    uint64_t waveform_audio_offset_denominator;
    bool waveform_offset_drag_active;
    bool beat_fit_drag_active;
    bool offset_fit_drag_active;
    double offset_fit_target_y_pixels;
    uint64_t beat_fit_chart_row;
    double beat_fit_anchor_y_pixels;
    double beat_fit_beat_y_pixels;
    uint64_t beat_fit_target_sample_frame;
    double beat_fit_target_y_pixels;
    int64_t beat_fit_original_bpm_numerator;
    uint64_t beat_fit_original_bpm_denominator;
    int64_t beat_fit_proposed_bpm_numerator;
    uint64_t beat_fit_proposed_bpm_denominator;
    bool beat_fit_onset_snap_active;
    bool beat_fit_snap_active;
    int64_t beat_fit_snap_step_numerator;
    uint64_t beat_fit_snap_step_denominator;
    bool waveform_proposed_beat_zero_visible;
    double waveform_proposed_beat_zero_fraction;
    int64_t waveform_proposed_audio_offset_numerator;
    uint64_t waveform_proposed_audio_offset_denominator;
    bool waveform_timing_locked;
    bool waveform_analysis_pending;
    const WormUiComposeBgmDraftView* bmson_marker_drafts;
    size_t bmson_marker_draft_count;
    const WormUiComposeBgmMovePreviewView* bmson_marker_move_preview;
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
    bool lane_keys_unbound;
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
    size_t selected_bgm_event_count;
    size_t semantic_layer_unassigned_bgm_count;
    WormUiBookmarkChooserView bookmark_chooser;
    size_t move_conflict_count;
    size_t move_to_bgm_count; // Unique moved-note count, before geometry truncation.
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


// Renderer-neutral scalar projection of the timing pane. SPEEDS spans are
// evaluated by the timing core before crossing this private C boundary.
typedef struct WormUiTimingView {
    bool input_occluded;
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
    bool initial_bpm_editable;
    bool initial_bpm_present;
    uint64_t initial_bpm_event_id;
    double initial_bpm_value;
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
    // Begins a new event at the target row with its default value (a BPM equal
    // to the one in force, or an anchor) and confirms it in one step.
    WORM_UI_TIMING_STAGE_QUICK_INSERT,
} WormUiTimingStageAction;

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
    WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER2,
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
// Plain view/action data; the application owns persistence and launching.
struct WormUiExternalPlayerDefinitionView {
    const char* name;
    const char* path;
    const char* start_arguments;
    const char* here_arguments;
    const char* stop_arguments;
};
struct WormUiExternalPlayerView {
    int visible;
    uint64_t revision;
    const struct WormUiExternalPlayerDefinitionView* players;
    size_t player_count;
    size_t selected;
};
enum WormUiExternalPlayerActionKind {
    WORM_UI_EXTERNAL_PLAYER_NONE,
    WORM_UI_EXTERNAL_PLAYER_SELECT,
    WORM_UI_EXTERNAL_PLAYER_SAVE,
    WORM_UI_EXTERNAL_PLAYER_ADD,
    WORM_UI_EXTERNAL_PLAYER_REMOVE,
    WORM_UI_EXTERNAL_PLAYER_CLOSE,
    WORM_UI_EXTERNAL_PLAYER_OPEN,
    WORM_UI_EXTERNAL_PLAYER_PLAY_START,
    WORM_UI_EXTERNAL_PLAYER_PLAY_HERE,
    WORM_UI_EXTERNAL_PLAYER_STOP
};
struct WormUiExternalPlayerAction {
    enum WormUiExternalPlayerActionKind kind;
    size_t selected;
    char name[4097];
    char path[4097];
    char start_arguments[4097];
    char here_arguments[4097];
    char stop_arguments[4097];
};

typedef struct WormUiTapTimingView {
    bool visible;
    bool playing;
    bool source_available;
    bool estimate_available;
    bool stable;
    bool downbeat_flash;
    uint16_t beats_per_measure;
    uint16_t active_segment;
    uint64_t tap_count;
    uint64_t tap_capacity;
    double bpm;
    double ms_per_beat;
    double offset_ms;
    double deviation_ms;
    double confidence;
    uint8_t bpm_choice;
    bool apply_available;
    uint64_t apply_revision;
    double proposed_bpm;
    double proposed_offset_ms;
    const char* status;
} WormUiTapTimingView;

typedef struct WormUiTapTimingAction {
    bool toggle;
    bool close;
    bool reset;
    bool choose_bpm;
    uint8_t bpm_choice;
    bool apply;
    uint64_t apply_revision;
} WormUiTapTimingAction;

typedef struct WormUiTimingSettingsAction {
    bool toggle;
    bool close;
    bool change_enable;
    bool enabled;
    bool change_volume;
    uint8_t volume;
    bool change_count_in;
    uint8_t count_in;
} WormUiTimingSettingsAction;

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
    WORM_UI_OPTIONS_FIELD_MOVE_COLLISION_POLICY, WORM_UI_OPTIONS_FIELD_FIELD_SCHEME
} WormUiOptionsField;
typedef struct WormUiOptionsDiagnostic {
    WormUiOptionsField field;
    const char* message;
} WormUiOptionsDiagnostic;
// Input & shortcuts rows: one per registry command, at most the registry's command cap.
#define WORM_UI_SHORTCUT_ROW_CAPACITY 512
typedef struct WormUiShortcutRowView {
    const char* label;
    const char* chord;
    bool conflict;
} WormUiShortcutRowView;
typedef struct WormUiOptionsView {
    bool visible;
    uint64_t revision;
    WormUiOptionsCategory category;
    bool changed;
    bool can_apply;
    uint32_t autosave_minutes, recent_files_count, default_snap, ui_scale_percent;
    uint32_t scroll_directions[6];
    uint32_t playhead_anchor_percent, area_band_alpha;
    uint32_t move_collision_policy;
    bool note_kind_palette_visible, lane_key_legend_visible;
    bool audition_on_click;
    bool confirm_unsaved_close, keep_pitch, frame_timing_overlay;
    bool minimap_click_moves_paused_playhead;
    uint32_t keysound_budget_mib;
    uint64_t rate_numerator, rate_denominator;
    const char* field_scheme;
    const char* theme;
    const char* dock_layout;
    const char* shortcut_profile_path;
    const struct WormUiExternalPlayerDefinitionView* players;
    size_t player_count, selected_player;
    const WormUiOptionsDiagnostic* diagnostics;
    size_t diagnostic_count;
    const char* status;
    // Input & shortcuts page only; empty when that page is not the active category.
    const WormUiShortcutRowView* shortcut_rows;
    size_t shortcut_row_count;
    bool shortcut_capturing;
    size_t shortcut_capture_row;
} WormUiOptionsView;
typedef enum WormUiOptionsActionKind {
    WORM_UI_OPTIONS_NONE = 0, WORM_UI_OPTIONS_OPEN, WORM_UI_OPTIONS_CLOSE,
    WORM_UI_OPTIONS_SELECT_CATEGORY, WORM_UI_OPTIONS_APPLY,
    WORM_UI_OPTIONS_REVERT, WORM_UI_OPTIONS_DEFAULTS, WORM_UI_OPTIONS_EDIT_FIELD,
    WORM_UI_OPTIONS_SHORTCUT_CAPTURE, WORM_UI_OPTIONS_SHORTCUT_CANCEL
} WormUiOptionsActionKind;
// EDIT_FIELD payload is consumed immediately; strings are bounded and owned.
#define WORM_UI_OPTIONS_TEXT_CAPACITY 4097
typedef struct WormUiOptionsAction {
    WormUiOptionsActionKind kind;
    uint64_t revision;
    WormUiOptionsCategory category;
    WormUiOptionsField field;
    uint64_t integer, denominator;
    bool boolean;
    uint32_t player_index;
    uint32_t family_index;
    // SHORTCUT_CAPTURE: index into the view's shortcut rows.
    uint32_t shortcut_row;
    char text[WORM_UI_OPTIONS_TEXT_CAPACITY];
} WormUiOptionsAction;

#define WORM_UI_NOTIFICATION_TOAST_CAPACITY 3

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

typedef struct WormUiNotificationToastView {
    uint64_t id;
    WormUiNotificationSeverity severity;
    const char* message;
    uint32_t repeat_count;
} WormUiNotificationToastView;

#define WORM_UI_NOTIFICATION_HISTORY_CAPACITY 256

typedef enum WormUiNotificationTargetAvailability {
    WORM_UI_NOTIFICATION_NO_TARGET = 0,
    WORM_UI_NOTIFICATION_TARGET_POSITION,
    WORM_UI_NOTIFICATION_TARGET_NONPOSITIONAL,
    WORM_UI_NOTIFICATION_TARGET_MISSING
} WormUiNotificationTargetAvailability;

typedef struct WormUiNotificationHistoryRow {
    uint64_t id;
    WormUiNotificationSeverity severity;
    uint32_t source;
    const char* message;
    const char* detail;
    uint32_t repeat_count;
    uint64_t age_seconds;
    bool read;
    WormUiNotificationTargetAvailability target;
} WormUiNotificationHistoryRow;

typedef struct WormUiNotificationHistoryView {
    bool visible;
    size_t row_count;
    const WormUiNotificationHistoryRow* rows;
} WormUiNotificationHistoryView;

typedef struct WormUiNotificationView {
    bool edit_gesture_active;
    bool history_available;
    WormUiNotificationHistoryView history;
    const char* current_text;
    WormUiNotificationSeverity current_severity;
    size_t unread_warnings;
    size_t unread_errors;
    size_t toast_count;
    WormUiNotificationToastView toasts[WORM_UI_NOTIFICATION_TOAST_CAPACITY];
} WormUiNotificationView;

typedef enum WormUiNotificationActionKind {
    WORM_UI_NOTIFICATION_ACTION_NONE = 0,
    WORM_UI_NOTIFICATION_ACTION_DISMISS,
    WORM_UI_NOTIFICATION_ACTION_OPEN_HISTORY,
    WORM_UI_NOTIFICATION_ACTION_CLOSE_HISTORY,
    WORM_UI_NOTIFICATION_ACTION_COPY_DETAILS,
    WORM_UI_NOTIFICATION_ACTION_GO_TO,
    WORM_UI_NOTIFICATION_ACTION_DISMISS_ALL
} WormUiNotificationActionKind;

typedef struct WormUiNotificationAction {
    WormUiNotificationActionKind kind;
    uint64_t id;
} WormUiNotificationAction;

typedef enum WormUiCloseDecision {
    WORM_UI_CLOSE_NONE = 0, WORM_UI_CLOSE_CANCEL, WORM_UI_CLOSE_DISCARD, WORM_UI_CLOSE_SAVE
} WormUiCloseDecision;
typedef struct WormUiCloseConfirmationView {
    bool visible, application, has_native_path;
    uint64_t project_id, revision;
    const char* status_text;
} WormUiCloseConfirmationView;
typedef struct WormUiCompareView {
    bool visible;
    bool has_two_difficulties;
    uint64_t first_chart_id;
    uint64_t second_chart_id;
    const char* first_difficulty;
    const char* second_difficulty;
    const WormUiComposeView* first_pane;
    const WormUiComposeView* second_pane;
    const uint64_t* chart_ids;
    const char* const* chart_names;
    size_t chart_count;
    double scroll_pixels;
    size_t diagnostic_count;
    const char* const* diagnostic_labels;
    const uint64_t* diagnostic_rows;
    const uint64_t* diagnostic_measures;
    const uint8_t* diagnostic_has_rows;
    uint64_t diagnostic_chart_id;
} WormUiCompareView;

typedef struct WormUiWorkspaceView {
    WormUiNotificationView notifications;
    WormUiOptionsView options;
    WormUiCloseConfirmationView close_confirmation;
    WormUiTapTimingView tap_timing;
    bool timing_settings_visible;
    uint8_t metronome_volume;
    uint8_t metronome_count_in;
    bool project_loaded;
    bool project_has_native_path;
    uint64_t project_id;
    uint64_t selected_chart_id;
    uint64_t document_revision;
    // A dropped MP3/OGG awaiting confirmation as a new project's audio.
    const char* pending_new_project_audio_path;
    // A BMS-family source whose text encoding must be selected before opening.
    const char* pending_bms_encoding_path;
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
    const char* summary_lines[WORM_UI_WORKSPACE_SUMMARY_CAPACITY];
    size_t summary_line_count;
    bool history_available;
    bool dirty;
    size_t undo_depth;
    size_t redo_depth;
    bool recovery_panel_visible;
    // Computed from all controller rows before the UI row limit is applied.
    bool recovery_has_actionable_candidate;
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
    WormUiCompareView compare;
    WormUiPlayerPreviewView player_preview;
    struct WormUiExternalPlayerView external_player;
} WormUiWorkspaceView;

typedef struct WormUiSoundImportItem {
    const char* path;
    const char* name;
    uint8_t kind;
    bool kind_fixed;
} WormUiSoundImportItem;

typedef struct WormUiSoundSlotImportItem {
    const char* path;
    const char* name;
    uint64_t occupied_resource_id;
    const char* occupied_name;
    bool ambiguous;
} WormUiSoundSlotImportItem;

typedef struct WormUiSoundGroupAssignment {
    uint64_t group_id;
    const char* name;
    const uint64_t* sound_ids;
    size_t sound_count;
} WormUiSoundGroupAssignment;

typedef struct WormUiSoundImportGroup {
    const char* name;
    uint64_t existing_group_id;
    const size_t* member_indices;
    size_t member_count;
} WormUiSoundImportGroup;

typedef enum WormUiComposeActionKind {
    WORM_UI_COMPOSE_ACTION_NONE = 0,
    WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET,
    WORM_UI_COMPOSE_ACTION_AREA_RANGE_RESTORE,
    WORM_UI_COMPOSE_ACTION_AREA_RANGE_CLEAR,
    WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE,
    WORM_UI_COMPOSE_ACTION_AREA_DELETE,
    WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE,
    /* Sets the cursor measure's time signature from timing_meter_numerator_text and
       timing_meter_denominator_text. */
    WORM_UI_COMPOSE_ACTION_SET_MEASURE_METER,
    /* Sets the meter from timing_meter_*_text on every measure in compose_measure_ids as one undo step. */
    WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER,
    /* Shift+Alt bar line drag: compose_measure_ids[0] is the measure the line ends; it takes the
       timing_meter_*_text meter and the next measure takes compose_second_meter_*. */
    WORM_UI_COMPOSE_ACTION_MOVE_BAR_LINE,
    /* The selected SV recipe form (compose_recipe_form, compose_recipe_id): preview shows the
       candidate in the lane without writing, apply stores it as one undo step, clear drops the
       preview. */
    WORM_UI_COMPOSE_ACTION_PREVIEW_SV_RECIPE_FORM,
    WORM_UI_COMPOSE_ACTION_APPLY_SV_RECIPE_FORM,
    WORM_UI_COMPOSE_ACTION_CLEAR_SV_RECIPE_PREVIEW,
    WORM_UI_COMPOSE_ACTION_GO_TO_MEASURE,
    WORM_UI_COMPOSE_ACTION_SET_BGM_COLUMN_COUNT,
    WORM_UI_COMPOSE_ACTION_SET_LANE_PROFILE,
    WORM_UI_COMPOSE_ACTION_APPLY_LANE_LAYOUT_PROFILE,
    WORM_UI_COMPOSE_ACTION_ZOOM_IN,
    WORM_UI_COMPOSE_ACTION_ZOOM_OUT,
    WORM_UI_COMPOSE_ACTION_SCROLL_UP,
    WORM_UI_COMPOSE_ACTION_SCROLL_DOWN,
    WORM_UI_COMPOSE_ACTION_RESET_VIEW,
    WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE,
    WORM_UI_COMPOSE_ACTION_SELECT_TOOL,
    WORM_UI_COMPOSE_ACTION_DRAW_TOOL,
    WORM_UI_COMPOSE_ACTION_LAYER_TOOL,
    WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_LANE_VISIBILITY,
    WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_FOCUS,
    WORM_UI_COMPOSE_ACTION_TOGGLE_EDIT_SNAP,
    WORM_UI_COMPOSE_ACTION_TOGGLE_Y_LOCK,
    WORM_UI_COMPOSE_ACTION_RATE_SLOWER,
    WORM_UI_COMPOSE_ACTION_RATE_FASTER,
    WORM_UI_COMPOSE_ACTION_SELECT_RESOURCE_USES,
    WORM_UI_COMPOSE_ACTION_ASSIGN_RESOURCE_TO_SELECTION,
    WORM_UI_COMPOSE_ACTION_CREATE_RESOURCE,
    WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET,
    WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT,
    WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT,
    WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP,
    WORM_UI_COMPOSE_ACTION_MOVE_BMSON_BGM_TO_CURSOR,
    WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_CONTINUE_AT_CURSOR,
    WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_RESTART_AT_CURSOR,
    WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_CONTINUE_AT_CURSOR,
    WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_RESTART_AT_CURSOR,
    WORM_UI_COMPOSE_ACTION_COMMIT_STAGED_BMSON_BGM,
    WORM_UI_COMPOSE_ACTION_CANCEL_STAGED_BMSON_BGM,
    WORM_UI_COMPOSE_ACTION_DELETE_BMSON_BGM_MARKER,
    WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_BGM_TO_ACTIVE_LANE,
    WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_NOTE_TO_BGM,
    WORM_UI_COMPOSE_ACTION_PLACE_RESOURCE,
    /* Keysound listbox actions. The sound list is compose_sound_resource_ids. */
    WORM_UI_COMPOSE_ACTION_IMPORT_SOUND_FILES,
    WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT,
    WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_SLOT_BATCH,
    WORM_UI_COMPOSE_ACTION_STAGE_SOUND_SLOT_BATCH,
    WORM_UI_COMPOSE_ACTION_TOGGLE_BGA_LANE_VISIBILITY,
    WORM_UI_COMPOSE_ACTION_ASSIGN_SOUND_SLOT,
    WORM_UI_COMPOSE_ACTION_IMPORT_BGA_FILES,
    WORM_UI_COMPOSE_ACTION_REMOVE_UNUSED_BGA,
    WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS,
    WORM_UI_COMPOSE_ACTION_GROUP_SOUNDS,
    WORM_UI_COMPOSE_ACTION_UNGROUP_SOUNDS,
    WORM_UI_COMPOSE_ACTION_SUGGEST_SOUND_GROUPS,
    WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS,
    WORM_UI_COMPOSE_ACTION_AUDITION_SOUND,
    WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER,
    WORM_UI_COMPOSE_ACTION_REMOVE_SOUND_LAYER,
    WORM_UI_COMPOSE_ACTION_CLEAR_SOUND_LAYERS,
    WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL,
    WORM_UI_COMPOSE_ACTION_APPLY_AUDIO_OFFSET,
    WORM_UI_COMPOSE_ACTION_TOGGLE_AUDIO_OFFSET_LOCK,
    WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL,
    // Search text travels in compose_note_label.
    WORM_UI_COMPOSE_ACTION_SEARCH_NOTE_LABEL,
    WORM_UI_COMPOSE_ACTION_SEARCH_NEXT_NOTE_LABEL,
    WORM_UI_COMPOSE_ACTION_SEARCH_PREVIOUS_NOTE_LABEL,
    WORM_UI_COMPOSE_ACTION_INSERT_MEASURE_BEFORE,
    WORM_UI_COMPOSE_ACTION_INSERT_MEASURE_AFTER,
    WORM_UI_COMPOSE_ACTION_REMOVE_EMPTY_MEASURE,
    // Confirmed removal of the cursor measure together with its contents.
    WORM_UI_COMPOSE_ACTION_REMOVE_MEASURE_WITH_CONTENTS,
    WORM_UI_COMPOSE_ACTION_TRANSFORM_MIRROR,
    WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_LEFT,
    WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_RIGHT,
    WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_EARLIER,
    WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_LATER,
    WORM_UI_COMPOSE_ACTION_ADD_BEAT_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION,
    WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_GO_TO_BOOKMARK,
    WORM_UI_COMPOSE_ACTION_RENAME_BOOKMARK,
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
    WORM_UI_COMPOSE_ACTION_SELECT_NO_SEMANTIC_LAYER,
    WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_WINDOW,
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
    WORM_UI_COMPOSE_ACTION_EDIT_BMS_BRANCH_STATEMENT,
    WORM_UI_COMPOSE_ACTION_INSERT_BMS_BRANCH_STATEMENT,
    WORM_UI_COMPOSE_ACTION_DELETE_BMS_BRANCH_STATEMENT,
    WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_RESTART,
    WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_CONTINUE,
    WORM_UI_COMPOSE_ACTION_SELECT_QUANTIZE_PRESET,
    WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS,
    WORM_UI_COMPOSE_ACTION_SAVE_QUANTIZE_PRESET,
    WORM_UI_COMPOSE_ACTION_DELETE_QUANTIZE_PRESET,
    WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU,
    WORM_UI_COMPOSE_ACTION_SET_NOTE_LAYER_MENU_WINDOW,
    WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU,
    WORM_UI_COMPOSE_ACTION_ASSIGN_NOTE_MENU_LAYER,
    WORM_UI_COMPOSE_ACTION_RENAME_QUANTIZE_PRESET,
} WormUiComposeActionKind;

typedef enum WormUiComposeInputKind {
    WORM_UI_COMPOSE_INPUT_NONE = 0,
    WORM_UI_COMPOSE_INPUT_PASTE_UPDATE,
    WORM_UI_COMPOSE_INPUT_PASTE_COMMIT,
    WORM_UI_COMPOSE_INPUT_PASTE_CANCEL,
    WORM_UI_COMPOSE_INPUT_ARM_NOTE_KIND,
    WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_BACKWARD,
    WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD,
    WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_VISIBLE,
    WORM_UI_COMPOSE_INPUT_SELECT_SAME_SOUND_CHART,
    WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
    WORM_UI_COMPOSE_INPUT_MEASURE_SELECT,
    WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE,
    WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_REPLACE,
    WORM_UI_COMPOSE_INPUT_BGM_EVENT_CLICK,
    WORM_UI_COMPOSE_INPUT_BMSON_RESTART_TOGGLE,
    /* A click on empty space in a stem lane adds a continue (or, with Shift, restart) point at the
       snapped row, in the channel of the marker named by object_id. */
    /* The selected playable notes were dropped on the BGM area; lane_id is the BGM source lane
       under the pointer (0 for none). BMSON stem taps return to their stem's lane. */
    WORM_UI_COMPOSE_INPUT_NOTE_MOVE_TO_BGM,
    /* A BGM event was dropped on the playable field: lane_id is the playfield lane under the
       pointer and object_id the dragged event. It keeps its row and becomes a tap. */
    WORM_UI_COMPOSE_INPUT_BGM_MOVE_TO_LANE,
    WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_CONTINUE,
    WORM_UI_COMPOSE_INPUT_BMSON_STEM_POINT_RESTART,
    WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_CONTINUE,
    WORM_UI_COMPOSE_INPUT_BMSON_RAZOR_RESTART,
    WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_BEGIN,
    WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_UPDATE,
    WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_RELEASE,
    WORM_UI_COMPOSE_INPUT_BMSON_BGM_MOVE_CANCEL,
    WORM_UI_COMPOSE_INPUT_BMSON_AUDITION_DRAFT,
    WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_ACTIVE_LANE,
    WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_BGM,
    WORM_UI_COMPOSE_INPUT_BMSON_TOGGLE_DRAFT_RESTART,
    WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT,
    WORM_UI_COMPOSE_INPUT_NOTE_DOUBLE_CLICK,
    WORM_UI_COMPOSE_INPUT_DELETE_SELECTION,
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
    WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_SEEK,
    WORM_UI_COMPOSE_INPUT_WAVEFORM_OVERVIEW_CLICK,
    WORM_UI_COMPOSE_INPUT_SET_MINIMAP_PAUSED_BEHAVIOR,
    WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN,
    WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE,
    WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE,
    WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL,
    WORM_UI_COMPOSE_INPUT_OFFSET_FIT_BEGIN,
    WORM_UI_COMPOSE_INPUT_OFFSET_FIT_UPDATE,
    WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE,
    WORM_UI_COMPOSE_INPUT_OFFSET_FIT_CANCEL,
    WORM_UI_COMPOSE_INPUT_TIMING_MARKER_SELECT,
    WORM_UI_COMPOSE_INPUT_TIMING_GROUP_MOVE,
    WORM_UI_COMPOSE_INPUT_BGA_EVENT_CLICK,
    WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE,
    WORM_UI_COMPOSE_INPUT_BOOKMARK_MOVE,
    WORM_UI_COMPOSE_INPUT_KIAI_SPAN_SELECT,
    WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT,
    WORM_UI_COMPOSE_INPUT_RECIPE_SELECT,
    WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT,
    WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND,
    WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP,
    // BGM bar view: audio_fraction anchors a zoom of x_pixels steps, x_pixels pans by a
    // fraction of the song, and reset shows the whole song again.
    WORM_UI_COMPOSE_INPUT_BGM_BAR_ZOOM,
    WORM_UI_COMPOSE_INPUT_BGM_BAR_PAN,
    WORM_UI_COMPOSE_INPUT_BGM_BAR_RESET,
    // Toolbar toggle: show or hide the whole bar row to win vertical space.
    WORM_UI_COMPOSE_INPUT_BGM_BAR_TOGGLE,
    // Speed span edge drag: span_edge 0 moves the event to y_pixels, 1 sets its transition length
    // from the event's row to y_pixels; object_id is the Speed event.
    WORM_UI_COMPOSE_INPUT_SPEED_SPAN_EDIT,
    WORM_UI_COMPOSE_INPUT_LATCHED_RANGE_SELECTION,
    WORM_UI_COMPOSE_INPUT_BGA_LANE_REVEAL,
    WORM_UI_COMPOSE_INPUT_DRAW_SOURCE,
    WORM_UI_COMPOSE_INPUT_BMSON_PACKED_DRAW,
    WORM_UI_COMPOSE_INPUT_BMSON_EMPTY_SELECT,
} WormUiComposeInputKind;

typedef enum WormUiComposeNavigationAction {
    WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD = 0,
    WORM_UI_COMPOSE_NAVIGATION_CURSOR_FORWARD,
    WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD,
    WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD,
} WormUiComposeNavigationAction;

typedef struct WormUiComposeInputAction {
    WormUiComposeInputKind kind;
    uint32_t note_kind;
    uint64_t lane_id;
    uint64_t object_id;
    uint64_t timing_chart_id;
    uint64_t timing_document_revision;
    WormUiTimingLaneKind timing_lane;
    uint64_t timing_anchor_chart_row;
    uint8_t resource_role;
    bool successive;
    bool razor_stroke_start;
    bool note_move_to_bgm;
    WormUiComposeSelectionMode selection_mode;
    WormUiComposeNavigationAction navigation_action;
    double x_pixels;
    double y_pixels;
    double audio_fraction;
    bool overview_shift;
    bool overview_primary;
    bool beat_fit_ctrl_held;
    bool beat_fit_onset_snap_held;
    int64_t beat_fit_snap_step_numerator;
    uint64_t beat_fit_snap_step_denominator;
    /* RECIPE_COMMAND: span_edge is 0 toggle enabled, 1 materialize, 2 delete; object_id is the recipe.
       KIAI_SPAN_EDIT, SPEED_SPAN_EDIT and RECIPE_RANGE_EDIT: span_edge is a WormUiSpanHandle; the body
       moves by the pointer travel from span_anchor_y_pixels to y_pixels.
       object_id is the area's head event (0 when creating). */
    uint8_t span_edge;
    double span_anchor_y_pixels;
    /* NOTE_PAINT_BEGIN: sample resource written into every note of the stroke; 0 paints silent notes. */
    uint64_t resource_id;
    uint8_t bga_lane_kind;
    bool bga_lane_kind_valid;
    uint8_t paint_intent; /* NOTE_PAINT: 0 legacy toggle, 1 draw, 2 erase;
                            SEMANTIC_LAYER_PAINT_BEGIN: 1 add, 2 remove. */
} WormUiComposeInputAction;

/* Which part of a span (Kiai area, Speed transition, SV recipe) a drag holds; carried in
   WormUiComposeInputAction::span_edge by KIAI_SPAN_EDIT, SPEED_SPAN_EDIT and RECIPE_RANGE_EDIT.
   CREATE only applies to Kiai: a new area from the press row to the pointer row. */
typedef enum WormUiSpanHandle {
    WORM_UI_SPAN_HANDLE_BEGIN = 0,
    WORM_UI_SPAN_HANDLE_END = 1,
    WORM_UI_SPAN_HANDLE_BODY = 2,
    WORM_UI_SPAN_HANDLE_CREATE = 3,
} WormUiSpanHandle;

typedef enum WormUiComposeViewportInputKind {
    WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL = 0,
    WORM_UI_COMPOSE_VIEWPORT_INPUT_SMOOTH_WHEEL,
    WORM_UI_COMPOSE_VIEWPORT_INPUT_PINCH_ZOOM,
} WormUiComposeViewportInputKind;

typedef struct WormUiComposeViewportInput {
    WormUiComposeViewportInputKind kind;
    bool zoom_modifier;
    double delta_x;
    double delta_y;
    double cursor_x_pixels;
    double cursor_y_pixels;
    double zoom_factor;
} WormUiComposeViewportInput;

typedef enum WormUiMetadataEditKind {
    WORM_UI_METADATA_EDIT_NONE = 0,
    WORM_UI_METADATA_EDIT_SONG,
    WORM_UI_METADATA_EDIT_CHART,
    WORM_UI_METADATA_EDIT_AUDIO_OFFSET,
    WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD,
    WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD,
    WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD,
    WORM_UI_METADATA_EDIT_OUTPUT_PATH,
    WORM_UI_METADATA_EDIT_REDIVIDE_CHART,
    WORM_UI_METADATA_EDIT_PREVIEW_CHANNEL_MAP,
    WORM_UI_METADATA_EDIT_APPLY_CHANNEL_MAP,
    // Author and/or rating written to metadata_batch_chart_ids in one command. The author is
    // metadata_chart_author; the rating is metadata_has_chart_rating plus the numerator/denominator
    // texts (no rating clears it).
    WORM_UI_METADATA_EDIT_BATCH_CHART,
} WormUiMetadataEditKind;

typedef enum WormUiClipboardAction {
    WORM_UI_CLIPBOARD_NONE = 0,
    WORM_UI_CLIPBOARD_COPY,
    WORM_UI_CLIPBOARD_CUT,
    WORM_UI_CLIPBOARD_PASTE,
} WormUiClipboardAction;

/* A pointer press on the timing lanes, as raw as the panel knows it: y is a drawn-orientation
   viewport pixel row. The viewport height, downscroll flag and visible document slice travel with
   it so one sys function turns it into a document fraction (and then a row). */
typedef struct WormUiTimingTarget {
    double screen_y;
    double viewport_height_pixels;
    double document_begin_fraction;
    double document_end_fraction;
    bool downscroll;
} WormUiTimingTarget;

typedef struct WormUiWorkspaceAction {
    char menu_command_id[96];
    WormUiClipboardAction clipboard;
    WormUiNotificationAction notifications;
    WormUiOptionsAction options;
    WormUiCloseDecision close_decision;
    uint64_t close_project_id, close_revision;
    WormUiProjectChartAction project_chart;
    WormUiTapTimingAction tap_timing;
    WormUiTimingSettingsAction timing_settings;
    WormUiWorkspaceActionKind kind;
    struct WormUiExternalPlayerAction external_player;
    char project_path[WORM_UI_PROJECT_PATH_CAPACITY];
    // OPEN_FILE: 0 automatic, 1 UTF-8, 2 CP932, 3 EUC-KR, 4 CP949,
    // 5 Windows-1252.
    uint8_t bms_encoding_index;
    // Initial layout index: 4K, 5K, 6K, 7K, 8K, 9K, 10K, 14K.
    uint8_t new_project_layout_index;
    WormUiWorkspaceTab tab;
    uint64_t recovery_scan_generation;
    uint64_t transport_seek_frame;
    // Current field span, stamped onto BGM-bar view inputs for centered zoom/pan retention.
    double minimap_visible_begin_fraction;
    double minimap_visible_end_fraction;
    bool minimap_window_capture_active;
    double minimap_window_capture_begin_fraction;
    double minimap_window_capture_end_fraction;
    int64_t hover_source_frame;
    bool hover_source_frame_valid;
    uint64_t hover_source_total_frames;
    uint32_t hover_source_sample_rate;
    uint64_t transport_loop_chart_id;
    uint64_t transport_loop_document_revision;
    uint64_t transport_loop_begin_row;
    uint64_t transport_loop_end_row;
    bool transport_loop_enabled;
    uint8_t transport_rate_index;
    bool transport_keep_pitch;
    uint64_t audio_asset_id;
    uint64_t audio_resource_id;
    uint64_t sound_usage_song_id;
    uint64_t sound_usage_document_revision;
    uint64_t sound_usage_resource_id;
    uint64_t sound_usage_asset_id;
    uint64_t sound_usage_chart_id;
    uint64_t sound_usage_measure_id;
    uint64_t sound_usage_row;
    uint64_t sound_usage_object_id;
    uint64_t sound_usage_channel_id;
    uint64_t sound_usage_clip_id;
    uint64_t sound_usage_referenced_resource_id;
    uint64_t sound_usage_referenced_asset_id;
    int sound_usage_kind;
    int sound_usage_scope;
    bool sound_usage_position_valid;
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
    uint64_t compare_chart_id;
    uint64_t compare_diagnostic_row;
    uint64_t compare_diagnostic_measure;
    uint64_t compare_diagnostic_chart;
    double compare_scroll_delta;
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
    bool metadata_inspect_copy;
    bool metadata_inspect_export;
    bool metadata_inspect_import;
    bool metadata_import_copy_project;
    bool metadata_import_copy_export;
    bool metadata_format_chart_scoped;
    uint8_t metadata_format_family;
    size_t metadata_format_index;
    const char* metadata_format_name;
    const char* metadata_format_value;
    size_t metadata_output_group_index;
    const char* metadata_output_path;
    // REDIVIDE_CHART: target divisions per standard measure (exact, 0 when unset).
    uint64_t metadata_redivision_resolution;
    // Channel-map preview: lane ids and 4 texts per lane (visible, invisible, long_note, mine).
    uint64_t metadata_channel_map_layout_segment_id;
    size_t metadata_channel_map_lane_count;
    const uint64_t* metadata_channel_map_lane_ids;
    const char* const* metadata_channel_map_texts;
    const uint64_t* metadata_batch_chart_ids;
    size_t metadata_batch_chart_count;
    bool metadata_batch_edit_author;
    bool metadata_batch_edit_rating;
    WormUiComposeActionKind compose_action;
    uint64_t compose_area_begin_row;
    uint64_t compose_area_end_row;
    bool compose_area_virtual;
    bool compose_area_begin_edge;
    bool compose_area_clear_requested;
    uint64_t compose_measure_number;
    uint32_t compose_bgm_column_count;
    uint32_t compose_lane_profile_index;
    char compose_lane_layout_profile_id[WORM_UI_COMPOSE_LAYOUT_PROFILE_ID_BYTES];
    int64_t compose_audio_offset_numerator;
    uint64_t compose_audio_offset_denominator;
    uint64_t compose_input_chart_id;
    uint64_t compose_input_project_id;
    uint64_t compose_input_document_revision;
    size_t compose_quantize_preset_index;
    bool compose_quantize_preset_selected;
    uint64_t compose_quantize_divisions;
    uint8_t compose_quantize_space;
    uint8_t compose_quantize_tie;
    uint8_t compose_quantize_holds;
    char compose_quantize_preset_name[WORM_UI_COMPOSE_QUANTIZE_PRESET_NAME_BYTES + 1];
    double compose_input_viewport_width_pixels;
    uint64_t compose_sound_hover_object_id;
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
    uint64_t compose_note_context_id;
    size_t compose_semantic_layer_first_row;
    size_t compose_semantic_layer_row_count;
    uint64_t compose_resource_id;
    uint64_t compose_bmson_bgm_event_id;
    uint64_t compose_resource_asset_id;
    /* Frontend-owned staging; valid through immediate workspace-action dispatch. */
    const uint64_t* compose_sound_resource_ids;
    size_t compose_sound_resource_count;
    const WormUiSoundGroupAssignment* compose_sound_group_assignments;
    size_t compose_sound_group_assignment_count;
    uint64_t compose_sound_group_id;
    bool compose_sound_group_keep_ids_together;
    char compose_sound_group_name[65];
    bool compose_sound_overwrite_note_layers;
    /* Renumber WAV or BMP identifiers for the selected resources, or all in the namespace. */
    bool compose_renumber_all;
    bool compose_renumber_bga;
    const char* const* compose_import_paths;
    size_t compose_import_path_count;
    /* Frontend-owned staging; valid through immediate workspace-action dispatch. */
    const uint64_t* compose_measure_ids;
    size_t compose_measure_id_count;
    uint32_t compose_second_meter_numerator, compose_second_meter_denominator;
    bool sound_import_pick_requested;
    /* 0 generic import, 1 Stems section, 2 Keysounds section. */
    uint8_t sound_import_kind;
    uint64_t sound_import_project_id;
    const WormUiSoundImportItem* sound_import_items;
    size_t sound_import_item_count;
    const WormUiSoundImportGroup* sound_import_groups;
    size_t sound_import_group_count;
    uint16_t compose_sound_slot;
    bool compose_sound_slot_confirmed;
    size_t compose_output_group_index;
    bool compose_export_overwrite;
    uint8_t compose_resource_role;
    char compose_import_path[WORM_UI_COMPOSE_IMPORT_PATH_BYTES + 1];
    uint8_t compose_timing_lane;
    uint64_t compose_branch_id;
    int64_t compose_branch_value;
    uint64_t compose_branch_statement_id;
    /* Frontend-owned staging text; valid through immediate workspace-action dispatch. */
    const char* compose_branch_statement_text;
    size_t compose_branch_statement_text_bytes;
    char compose_semantic_layer_name[WORM_UI_COMPOSE_SEMANTIC_LAYER_NAME_CAPACITY];
    uint8_t compose_semantic_layer_red;
    uint8_t compose_semantic_layer_green;
    uint8_t compose_semantic_layer_blue;
    uint8_t compose_semantic_layer_alpha;
    WormUiComposeInputAction compose_inputs[WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY];
    size_t compose_input_count;
    WormUiComposeViewportInput compose_viewport_inputs[WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY];
    size_t compose_viewport_input_count;
    WormUiTimingRecipeForm compose_recipe_form;
    uint64_t compose_recipe_id;
    char timing_meter_numerator_text[32];
    char timing_meter_denominator_text[32];
    WormUiTimingStageAction timing_stage_action;
    uint64_t timing_chart_id;
    uint64_t timing_document_revision;
    uint64_t timing_object_id;
    WormUiTimingLaneKind timing_lane;
    bool timing_has_target_y;
    WormUiTimingTarget timing_target;
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
    WormUiShortcutEvent shortcut_events[WORM_UI_SHORTCUT_EVENT_CAPACITY];
    size_t shortcut_event_count;
    bool shortcut_overflowed;
    bool shortcut_window_focused;
    bool shortcut_notification_history_focused;
    bool shortcut_text_entry_active;
    bool shortcut_ime_composition_active;
    bool shortcut_compose_lane_focused;
    uint64_t shortcut_focused_lane_id;
} WormUiWorkspaceAction;

// Private application/frontend boundary. Implementations own all widget
// toolkit state and translate semantic views into typed actions.
bool worm_ui_frontend_init(SDL_Window* window, SDL_Renderer* renderer);
// Select a loaded field palette; empty or unknown IDs use Wormedit. Chrome is unaffected.
bool worm_ui_frontend_select_field_scheme(const char* id);
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
WormUiWorkspaceAction worm_ui_frontend_consume_project_dialog_for_testing(void);
// Read queued draw text before render clears it; returns required bytes (no terminator).
size_t worm_ui_frontend_copy_drawn_text_for_testing(char* output, size_t capacity);
typedef struct WormUiTestingDrawnText {
    float x, y, width, height;
    char text[128];
} WormUiTestingDrawnText;
typedef struct WormUiTestingRect {
    float x, y, width, height;
} WormUiTestingRect;
// Copy bounded text command labels and their window-coordinate rectangles for event tests.
size_t worm_ui_frontend_copy_drawn_text_bounds_for_testing(WormUiTestingDrawnText* output,
                                                            size_t capacity);
// Return the current Compose canvas viewport in window coordinates, when one was drawn this frame.
bool worm_ui_frontend_compose_viewport_for_testing(WormUiTestingRect* output);
bool worm_ui_frontend_window_bounds_for_testing(const char* name, WormUiTestingRect* output);
// Hide the real Notifications window in headless tests, without changing its model view.
bool worm_ui_frontend_hide_notification_history_for_testing(void);
void worm_ui_frontend_shutdown(void);
void worm_ui_frontend_begin_input(void);
bool worm_ui_frontend_handle_event(const SDL_Event* event);
WormUiMetadataDropTarget worm_ui_frontend_metadata_drop_target(uint32_t window_id, float x,
                                                                float y);
/* True when a window position lies over the currently rendered Compose or Audio/Slices
   sound listbox, where dropped audio files are added as sounds. */
bool worm_ui_frontend_sound_list_contains(uint32_t window_id, float x, float y);
void worm_ui_frontend_end_input(void);
bool worm_ui_frontend_request_new_project(void);
bool worm_ui_frontend_request_save_project(uint64_t project_id);
bool worm_ui_frontend_stage_sound_import(uint64_t project, uint64_t chart, uint64_t revision,
    const WormUiSoundImportItem* items, size_t item_count,
    const WormUiSoundImportGroup* existing_groups, size_t group_count,
    bool chart_has_bgm, const char* message);
void worm_ui_frontend_sound_import_result(bool applied, const char* message);
WormUiWorkspaceAction worm_ui_frontend_compose(const WormUiWorkspaceView* view);
// Draw a recoverable workspace error when the model could not produce a view.
WormUiWorkspaceAction worm_ui_frontend_workspace_error(
    const char* diagnostic, const WormUiNotificationView* notifications,
    const WormUiCloseConfirmationView* close);
void worm_ui_frontend_resource_placed(uint64_t placed_resource_id, uint64_t next_resource_id);
void worm_ui_frontend_render(int window_width, int window_height);

#define WORM_UI_FRAME_TIMING_CAPACITY 240
#define WORM_UI_FRAME_TIMING_PHASE_COUNT 27
typedef struct WormUiFrameTimingStats {
    size_t count;
    double average_ms;
    double p99_ms;
    /* p99 of the main thread's CPU time; 0 samples where the platform cannot measure it. A wall
       p99 well above it means the frame waited (preempted or blocked) rather than worked. */
    size_t cpu_count;
    double cpu_p99_ms;
} WormUiFrameTimingStats;
#define WORM_UI_FRAME_GPU_WORK_COUNT 5
typedef struct WormUiFrameGpuWorkStats {
    size_t count;
    double average;
    double p99;
} WormUiFrameGpuWorkStats;
typedef struct WormUiFrameGpuWork {
    uint64_t vertices;
    uint64_t indices;
    uint64_t draw_calls;
    uint64_t clip_rect_changes;
    uint64_t texture_switches;
} WormUiFrameGpuWork;
typedef struct WormUiFrameTimingView {
    size_t count;
    double frame_ms[WORM_UI_FRAME_TIMING_CAPACITY];
    WormUiFrameTimingStats frame;
    const char* phase_names[WORM_UI_FRAME_TIMING_PHASE_COUNT];
    WormUiFrameTimingStats phases[WORM_UI_FRAME_TIMING_PHASE_COUNT];
    uint32_t heap_allocations_available;
    uint64_t projection_heap_allocations_latest;
    uint64_t projection_heap_bytes_latest;
    WormUiFrameGpuWorkStats projection_heap_allocations;
    WormUiFrameGpuWorkStats projection_heap_bytes;
    // Application GPU work excludes diagnostic windows (the frame-timing panel),
    // whose own submission is reported separately in the diagnostic_* fields.
    uint32_t gpu_work_available;
    WormUiFrameGpuWork gpu_work_latest;
    const char* gpu_work_names[WORM_UI_FRAME_GPU_WORK_COUNT];
    WormUiFrameGpuWorkStats gpu_work[WORM_UI_FRAME_GPU_WORK_COUNT];
    uint32_t diagnostic_gpu_work_available;
    WormUiFrameGpuWork diagnostic_gpu_work_latest;
    WormUiFrameGpuWorkStats diagnostic_gpu_work[WORM_UI_FRAME_GPU_WORK_COUNT];
} WormUiFrameTimingView;
// Called only while the frontend's diagnostic overlay is visible.
WormUiFrameTimingView worm_ui_frame_timing_view(void);
void worm_ui_frame_timing_record_gpu_work(WormUiFrameGpuWork work);
void worm_ui_frame_timing_record_diagnostic_gpu_work(WormUiFrameGpuWork work);
typedef enum WormUiFrameRenderPhase {
    WORM_UI_FRAME_RENDER_CONVERT,
    WORM_UI_FRAME_RENDER_SUBMIT,
    WORM_UI_FRAME_RENDER_PRESENT
} WormUiFrameRenderPhase;
// The returned token is opaque to the frontend and restores the prior phase.
uint32_t worm_ui_frame_render_phase_begin(WormUiFrameRenderPhase phase);
void worm_ui_frame_render_phase_end(uint32_t previous);

void worm_ui_frontend_bga_dock_request(WormUiBgaDockRequest* request);
void worm_ui_frontend_sound_usage_request(WormUiSoundUsageRequest* request);
void worm_ui_frontend_select_sound_usage_source(uint64_t project_id, uint64_t resource_id,
                                              uint64_t channel_id, uint64_t clip_id);
void worm_ui_frontend_bga_move_preview(uint64_t* chart_id, uint64_t* document_revision,
                                       uint64_t* event_id, double* y_pixels);
void worm_ui_frontend_keysound_dock_request(WormUiKeysoundDockRequest* request);
void worm_ui_frontend_bookmark_chooser_request(WormUiBookmarkChooserRequest* request);
bool worm_ui_frontend_open_bookmark_chooser(uint64_t project_id, uint64_t chart_id, uint64_t revision);
/* Width in logical pixels the BMS branch dock took from the left of the Compose field last frame;
   the Compose projection narrows its viewport by it. Zero while the dock is hidden. */
int worm_ui_frontend_compose_left_dock_pixels(void);
double worm_ui_frontend_compose_bgm_lane_width(uint64_t project_id, uint64_t chart_id,
                                             uint64_t lane_id);
/* Queues one dropped file for exact slot assignment; false when automatic mode is active.
   In slots mode invalid/stale requests are consumed without falling back to allocation. */
bool worm_ui_frontend_stage_sound_slot_batch(uint64_t project, uint64_t chart, uint64_t revision,
    uint16_t first_slot, const WormUiSoundSlotImportItem* items, size_t count, const char* reason);

bool worm_ui_frontend_stage_sound_slot_file(uint64_t project, uint64_t chart, uint64_t revision,
                                            const char* path, size_t file_count);
void worm_ui_frontend_sound_slot_batch_result(bool applied, const char* message);
bool worm_ui_frontend_stage_sound_slot_file_at(uint64_t project, uint64_t chart,
    uint64_t revision, uint16_t slot, uint64_t occupied_resource, const char* path);
bool worm_ui_frontend_bga_list_contains(uint32_t window_id, float x, float y);

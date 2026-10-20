#pragma once
#include "compose_measure_lane.h"
#include "metadata_audio.h"
#include "hitsounds.h"
#include "preview_audio.h"

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
    uint8_t snap_class; // Exact standard-measure subdivision; 0 = other.
    // The measure a real (not virtual) measure line starts; none for every other line.
    wormedit::measure_id_t measure_id;
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
    bool sound_is_stem;
    bool sound_missing;
    uint8_t glyph;
    const char* sound_label;
    const char* sound_detail;
    uint64_t sound_resource_id;
    const wormedit::ui::hitsound_note_view_t* hitsound;
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
    bool muted;
    bool solo;
    bool mix_silent;
    double width_pixels;
    double resize_edge_pixels;
    const char* filename;
    uint64_t bmson_channel_id;
    bool bmson_channel_continues;
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
    bool armable_stem; // Valid stem resource, independent of Sounds filtering.
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
    bool assist_tick_enabled;
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
    const char* source_path;
    uint64_t use_count;
} WormUiComposeResourceView;

/* One row of the Compose keysound listbox: a placeable sample of the catalog. */
typedef struct WormUiComposeBgaRowView {
    uint64_t resource_id;
    const char* slot;
    const char* name;
    uint64_t use_count;
    bool missing;
    bool video;
    const char* source_path;
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
    uint8_t use_filter; /* 0 all, 1 used, 2 unused, 3 missing */
} WormUiBgaDockRequest;

typedef struct WormUiSoundPickerRowView {
    uint64_t resource_id;
    const char* label;
    bool missing;
} WormUiSoundPickerRowView;
typedef struct WormUiSoundPickerView {
    char query[96];
    WormUiSoundPickerRowView rows[20];
    size_t row_count, total_count;
    bool ready;
} WormUiSoundPickerView;

typedef struct WormUiKeysoundDockRequest {
    bool sound_picker_open;
    char sound_picker_query[96];
    bool sort_by_id;
    bool slots_mode;
    uint16_t selected_slot;
    bool selected_slot_explicit;
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
    uint8_t use_filter; /* 0 all, 1 used, 2 unused, 3 missing */
} WormUiKeysoundDockRequest;

typedef struct WormUiBookmarkChooserRequest {
    size_t first_row, row_limit;
    bool open;
    char query[256];
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
    const char* query;
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

#define WORM_UI_KEYSOUND_FALLBACK_CAPACITY 256
#define WORM_UI_KEYSOUND_FALLBACK_CANDIDATES 6

/* A missing sound's BMS audio fallback outcome (docs/references/bms-audio-fallback.md). */
typedef struct WormUiKeysoundFallbackView {
    uint64_t resource_id;
    uint8_t outcome; /* 1 one fallback file was found, 2 several candidates need a choice */
    size_t candidate_count;
    const char* candidates[WORM_UI_KEYSOUND_FALLBACK_CANDIDATES]; /* project-relative paths */
} WormUiKeysoundFallbackView;

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
    uint64_t use_count;
    const char* source_path;
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
    uint64_t use_count;
    bool missing, placeable;
    const char* source_path;
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
    WormUiExistingFilesChoice existing_files;
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
    WormUiKindCatalogView timing_lanes;
    WormUiKindCatalogView timing_lane_captions;
    WormUiKindCatalogView note_palette;
    // A modeless program window owns this frame's content input.
    bool input_occluded;
    uint32_t armed_note_kind;
    uint32_t allowed_note_kind_mask;
    bool note_kind_palette_visible, lane_key_legend_visible;
    wormedit::ui::hitsound_palette_view_t hitsounds;
    // Editor preference: whether clicks, placements, and selections play their sound.
    bool audition_on_click;
    uint32_t area_band_alpha;
    bool visible;
    bool scene_available;
    bool editor_gesture_active;
    bool editor_gesture_current;
    // Application acknowledgement of an Alt/Option or armed Hold/Roll pointer begin.
    bool editor_pointer_hold_active;
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
    uint64_t keyboard_context_note_id; // First selected note, including offscreen multi-selection.
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
    bool quantize_preview_enabled;
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
    wormedit::ui::preview_audio_range_view_t preview_audio_range;
    uint64_t area_canvas_rows;
    uint64_t area_rows_per_measure;
    uint64_t area_snap_denominator;
    // False while pointer edits land on the finest chart row instead of the active grid.
    bool edit_snap_enabled;
    /* Y-lock: dragging notes changes lanes only, never rows. */
    bool y_lock;
    wormedit::ui::compose_measure_lane_view_t measure_lane;
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
    bool semantic_layer_assist_available;
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
    const uint64_t* bga_all_resource_ids;
    size_t bga_all_resource_count;
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
    const WormUiKeysoundFallbackView* keysound_fallbacks;
    size_t keysound_fallback_count;
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
    bool export_confirmation_required;
    uint64_t export_confirmation_project_id;
    uint64_t export_confirmation_revision;
    const char* export_confirmation_message;
    const char* export_confirmation_detail;
    const WormUiComposeDifferenceView* differences;
    size_t difference_count;
    const WormUiComposeHoldView* hold_bodies;
    size_t hold_body_count;
    const WormUiComposeNoteView* note_endpoints;
    size_t note_endpoint_count;
    const WormUiComposeBgmLaneView* bgm_lanes;
    size_t bgm_lane_count;
    bool bgm_mix_available;
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
    WormUiSoundPickerView sound_picker;
    size_t move_conflict_count;
    size_t move_to_bgm_count; // Unique moved-note count, before geometry truncation.
} WormUiComposeView;

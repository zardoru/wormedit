#pragma once
#include "catalog.h"
#include "project.h"

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

// One key-count profile offered for the chart's family. Custom is listed disabled with its reason.
typedef struct WormUiMetadataLaneProfileChoice {
    const char* label;
    bool enabled;
    const char* reason;
} WormUiMetadataLaneProfileChoice;

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
    WormUiKindCatalogView format_families;
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
    // Key-count selector: the chart's family profiles, then Custom. current is the chart's own index.
    const WormUiMetadataLaneProfileChoice* lane_profile_choices;
    size_t lane_profile_choice_count;
    size_t lane_profile_current;
    bool lane_profile_preview_present;
    bool lane_profile_preview_valid;
    size_t lane_profile_mapped_note_count;
    size_t lane_profile_dropped_note_count;
    size_t lane_profile_collision_count;
    const char* lane_profile_preview_message;
    bool lane_profile_apply_enabled;
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
    uint64_t use_count;
    uint64_t duration_frames;
    uint32_t sample_rate;
    bool missing;
    uint64_t bmson_channel_id;
    bool bmson_channel_continues;
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

typedef struct WormUiAudioWaveformViewportRequest {
    uint64_t asset_id;
    uint64_t total_frames;
    uint64_t begin_frame;
    double frames_per_pixel;
    uint32_t pixel_width;
    bool valid;
} WormUiAudioWaveformViewportRequest;

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
    uint64_t waveform_begin_frame;
    uint64_t waveform_end_frame;
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
    WormUiKindCatalogView sound_kinds;
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

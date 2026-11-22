#pragma once
#include "audio_slicer.h"
#include "catalog.h"
#include "project.h"
#include "timing_links.h"
#include <wormedit/core/metadata_limits.h>
#include <wormedit/core/metadata_inheritance_types.h>
#include <array>
#include <string>
#include <vector>

namespace wormedit::ui {
inline constexpr std::size_t metadata_channel_map_lane_limit = 256;
inline constexpr std::size_t audio_waveform_column_limit = 4096;
inline constexpr std::size_t audio_sound_row_limit = 512;
}
#include <string_view>

namespace wormedit::ui { struct compose_output_group_view_t; }

namespace wormedit::ui {
struct metadata_format_field_view_t {
    bool chart_scoped;
    format_family_t family;
    bool first_group_for_family;
    size_t group_index;
    size_t field_index;
    size_t group_field_count{};
    std::string name;
    std::string value;
    bool has_source_order;
    size_t source_order;
    auto operator==(const metadata_format_field_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct metadata_format_target_view_t {
    bool chart_scoped;
    format_family_t family;
    bool group_exists;
    size_t group_index;
    size_t field_count{};
    auto operator==(const metadata_format_target_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

// One lane of the active layout segment on a BMS-family chart. channels is ordered visible,
// invisible, long_note, mine; an unset channel is the empty string.
namespace wormedit::ui {
struct metadata_channel_map_lane_view_t {
    lane_id_t lane_id;
    std::string name;
    std::array<std::string, 4> channels;
    auto operator==(const metadata_channel_map_lane_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

// One key-count profile offered for the chart's family. Custom is listed disabled with its reason.
namespace wormedit::ui {
struct metadata_lane_profile_choice_view_t {
    std::string label;
    bool enabled;
    std::string reason;
    auto operator==(const metadata_lane_profile_choice_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

// One difficulty offered by the Metadata multi-select; name is valid for the frame.
namespace wormedit::ui {
struct metadata_chart_choice_view_t {
    chart_id_t chart_id;
    std::string name;
    auto operator==(const metadata_chart_choice_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct audio_waveform_column_view_t {
    float minimum[2];
    float maximum[2];
    float absolute_peak[2];
    float rms[2];
    auto operator==(const audio_waveform_column_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

typedef enum WormUiMetadataDropDestination {
    WORM_UI_METADATA_DROP_NONE,
    WORM_UI_METADATA_DROP_PROJECT,
    WORM_UI_METADATA_DROP_EXPORT,
    WORM_UI_METADATA_DROP_OUTPUT_PATH,
} WormUiMetadataDropDestination;

namespace wormedit::ui {
struct metadata_drop_target_t {
    WormUiMetadataDropDestination destination;
    size_t output_group_index;
    auto operator==(const metadata_drop_target_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct metadata_inheritance_field_view_t {
    inherited_metadata_field_t field;
    std::string label;
    std::string inherited;
    std::optional<std::string> override_value;
    auto operator==(const metadata_inheritance_field_view_t&) const -> bool = default;
};
struct metadata_view_t {
    timing_link_context_t timing_link_context;
    std::vector<timing_link_category_view_t> timing_links;
    wormedit::ui::kind_catalog_view_t<wormedit::format_family_t> format_families;
    bool visible;
    bool song_fields_editable;
    bool chart_fields_editable;
    bool audio_offset_editable;
    bool audio_offset_locked;
    bool search_tags_editable;
    uint64_t document_revision;
    bool has_chart;
    chart_id_t chart_id;
    format_family_t selected_chart_source_family;
    std::string title;
    std::string artist;
    std::string chart_name;
    std::string chart_author;
    std::vector<metadata_inheritance_field_view_t> inheritance;
    bool has_chart_rating;
    int64_t chart_rating_numerator;
    uint64_t chart_rating_denominator;
    uint64_t chart_divisions_per_standard_measure;
    bool timing_locked;
    int64_t audio_offset_numerator;
    uint64_t audio_offset_denominator;
    bool audio_waveform_has_selected_asset;
    asset_id_t audio_waveform_selected_asset_id;
    bool audio_waveform_analysis_pending;
    bool audio_waveform_available;
    uint64_t audio_waveform_total_frames;
    uint32_t audio_waveform_sample_rate;
    std::vector<wormedit::ui::audio_waveform_column_view_t> audio_waveform_columns;
    bool audio_offset_drag_active;
    int64_t audio_offset_drag_delta_frames;
    int64_t proposed_audio_offset_numerator;
    uint64_t proposed_audio_offset_denominator;
    std::vector<std::string> search_tags;
    std::vector<wormedit::ui::metadata_format_field_view_t> format_fields;
    size_t format_field_total_count{};
    std::vector<wormedit::ui::metadata_format_target_view_t> format_targets;
    std::string project_directory;
    std::string selected_music_source;
    asset_id_t selected_music_asset_id;
    asset_id_t next_import_asset_id;
    bool import_copy_pending;
    size_t selected_music_resource_count{};
    size_t selected_music_channel_count{};
    std::vector<wormedit::ui::compose_output_group_view_t> output_groups;
    // Temporary: T-698 converts the nested Compose row text away from frame-arena storage.
    std::string dropped_music_path;
    wormedit::ui::metadata_drop_target_t dropped_music_target;
    std::string dropped_output_relative_path;
    size_t dropped_output_group_index;
    std::string copy_preview_text;
    bool copy_preview_pending;
    bool copy_preview_export;
    bool copy_preview_import;
    size_t copy_preview_group_index;
    asset_id_t copy_preview_asset_id;
    std::string copy_preview_source;
    object_id_t channel_map_layout_segment_id;
    std::vector<wormedit::ui::metadata_channel_map_lane_view_t> channel_map_lanes;
    // Preview state of the sealed proposal from the last Preview action; Apply needs it valid,
    // present and conflict-free.
    bool channel_map_preview_present;
    bool channel_map_preview_valid;
    size_t channel_map_preview_affected_note_count{};
    size_t channel_map_preview_conflict_count{};
    std::string channel_map_preview_message;
    bool channel_map_apply_enabled;
    bool channel_map_editable;
    // Key-count selector: the chart's family profiles, then Custom. current is the chart's own index.
    std::vector<wormedit::ui::metadata_lane_profile_choice_view_t> lane_profile_choices;
    size_t lane_profile_current;
    bool lane_profile_preview_present;
    bool lane_profile_preview_valid;
    size_t lane_profile_mapped_note_count{};
    size_t lane_profile_dropped_note_count{};
    size_t lane_profile_collision_count{};
    std::string lane_profile_preview_message;
    bool lane_profile_apply_enabled;
    std::vector<wormedit::ui::metadata_chart_choice_view_t> chart_choices;
    [[nodiscard]] auto find_format_field(format_family_t family, bool chart_scoped,
                                          std::size_t field_index) const
        -> const metadata_format_field_view_t* {
        for (const auto& field : format_fields)
            if (field.family == family && field.chart_scoped == chart_scoped &&
                field.first_group_for_family && field.field_index == field_index) return &field;
        return nullptr;
    }
    [[nodiscard]] auto find_song_format_target(format_family_t family) const
        -> const metadata_format_target_view_t* {
        for (const auto& target : format_targets)
            if (!target.chart_scoped && target.family == family) return &target;
        return nullptr;
    }
    void reset();
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct sound_row_view_t {
    resource_id_t resource_id;
    asset_id_t asset_id;
    std::string name;
    std::string tags_text;
    std::vector<std::string> tags;
    std::string source_path;
    // 0 unspecified, 1 stem, 2 keysound, 3 sample (sound_kind_t, effective kind).
    sound_kind_t kind;
    uint64_t use_count{};
    uint64_t duration_frames;
    uint32_t sample_rate;
    bool missing;
    object_id_t bmson_channel_id;
    bool bmson_channel_continues;
    auto operator==(const sound_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct sound_usage_row_view_t {
    chart_id_t chart_id;
    measure_id_t measure_id;
    uint64_t row;
    object_id_t object_id;
    object_id_t channel_id;
    clip_id_t clip_id;
    resource_id_t referenced_resource_id;
    asset_id_t referenced_asset_id;
    sound_usage_kind_t kind;
    sound_usage_scope_t scope;
    bool position_valid;
    std::string chart_label;
    std::string position_label;
    auto operator==(const sound_usage_row_view_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct audio_waveform_viewport_request_t {
    asset_id_t asset_id;
    uint64_t total_frames;
    uint64_t begin_frame;
    double frames_per_pixel;
    uint32_t pixel_width;
    bool valid;
    auto operator==(const audio_waveform_viewport_request_t&) const -> bool = default;
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct audio_slices_view_t {
    audio_slicer_view_t slicer;
    std::vector<wormedit::ui::sound_row_view_t> sound_rows;
    bool sound_rows_truncated;
    resource_id_t selected_resource_id;
    object_id_t selected_channel_id;
    clip_id_t selected_clip_id;
    std::vector<wormedit::ui::sound_usage_row_view_t> usage_rows;
    size_t usage_total_count{};
    bool usage_rows_truncated;
    bool usage_valid;
    song_id_t usage_song_id;
    uint64_t usage_document_revision;
    resource_id_t usage_resource_id;
    asset_id_t usage_asset_id;
    bool visible;
    std::vector<asset_id_t> asset_ids;
    bool has_selected_asset;
    asset_id_t selected_asset_id;
    bool analysis_pending;
    bool waveform_available;
    uint64_t total_frames;
    uint64_t waveform_begin_frame;
    uint64_t waveform_end_frame;
    uint32_t sample_rate;
    std::vector<wormedit::ui::audio_waveform_column_view_t> waveform_columns;
    bool has_chart;
    chart_id_t chart_id;
    uint64_t document_revision;
    bool timing_locked;
    int64_t offset_numerator;
    uint64_t offset_denominator;
    bool offset_drag_active;
    int64_t offset_drag_delta_frames;
    int64_t proposed_offset_numerator;
    uint64_t proposed_offset_denominator;
    wormedit::ui::kind_catalog_view_t<wormedit::sound_kind_t> sound_kinds;
    void reset();
};
} // namespace wormedit::ui

namespace wormedit::ui {
struct sound_usage_request_t {
    song_id_t selected_project_id;
    resource_id_t selected_resource_id;
    object_id_t selected_channel_id;
    clip_id_t selected_clip_id;
    bool find_uses_open;
    auto operator==(const sound_usage_request_t&) const -> bool = default;
};
} // namespace wormedit::ui

typedef enum WormUiAudioOffsetDragAction {
    WORM_UI_AUDIO_OFFSET_DRAG_NONE = 0,
    WORM_UI_AUDIO_OFFSET_DRAG_BEGIN,
    WORM_UI_AUDIO_OFFSET_DRAG_UPDATE,
    WORM_UI_AUDIO_OFFSET_DRAG_COMMIT,
    WORM_UI_AUDIO_OFFSET_DRAG_CANCEL,
    WORM_UI_AUDIO_OFFSET_NUDGE,
} WormUiAudioOffsetDragAction;

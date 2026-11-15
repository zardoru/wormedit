#pragma once
#include <wormedit/core/metadata_inheritance_types.h>
#include <wormedit/core/vocabulary.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

typedef enum WormUiMetadataEditKind {
    WORM_UI_METADATA_EDIT_NONE = 0,
    WORM_UI_METADATA_EDIT_SONG,
    WORM_UI_METADATA_EDIT_CHART,
    WORM_UI_METADATA_EDIT_INHERITANCE,
    WORM_UI_METADATA_EDIT_AUDIO_OFFSET,
    WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD,
    WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD,
    WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD,
    WORM_UI_METADATA_EDIT_OUTPUT_PATH,
    WORM_UI_METADATA_EDIT_REDIVIDE_CHART,
    WORM_UI_METADATA_EDIT_PREVIEW_CHANNEL_MAP,
    WORM_UI_METADATA_EDIT_APPLY_CHANNEL_MAP,
    // Author and/or rating written to batch_chart_ids in one command. The author is
    // chart_author; the rating is has_chart_rating plus the numerator/denominator
    // texts (no rating clears it).
    WORM_UI_METADATA_EDIT_BATCH_CHART,
    // Key-count preview and apply: the index names a choice in the chart's family list.
    WORM_UI_METADATA_EDIT_PREVIEW_LANE_PROFILE,
    WORM_UI_METADATA_EDIT_APPLY_LANE_PROFILE,
} WormUiMetadataEditKind;

namespace wormedit::ui {
struct metadata_channel_map_lane_t {
    lane_id_t lane;
    std::optional<std::string> visible;
    std::optional<std::string> invisible;
    std::optional<std::string> long_note;
    std::optional<std::string> mine;

    auto operator==(const metadata_channel_map_lane_t&) const -> bool = default;
};
} // namespace wormedit::ui

/* Payload of one metadata edit request. `kind` selects the command; the other members are
   the fields that command reads. Written by the metadata panel, read by sys/metadata. */
namespace wormedit::ui {
struct metadata_edit_request_t {
    WormUiMetadataEditKind kind{};
    uint64_t document_revision;
    chart_id_t chart_id;
    bool has_chart_rating;
    std::optional<std::string> chart_rating_numerator_text;
    std::optional<std::string> chart_rating_denominator_text;
    std::optional<std::string> audio_offset_numerator_text;
    std::optional<std::string> audio_offset_denominator_text;
    std::optional<std::string> title;
    std::optional<std::string> artist;
    std::optional<std::string> chart_name;
    std::optional<std::string> chart_author;
    metadata_overrides_t overrides;
    std::vector<std::optional<std::string>> search_tags;
    bool inspect_copy;
    bool inspect_export;
    bool inspect_import;
    bool import_copy_project;
    bool import_copy_export;
    bool format_chart_scoped;
    format_family_t format_family;
    size_t format_index;
    std::optional<std::string> format_name;
    std::optional<std::string> format_value;
    size_t output_group_index;
    std::optional<std::string> output_path;
    // REDIVIDE_CHART: target divisions per standard measure (exact, 0 when unset).
    uint64_t redivision_resolution;
    // Channel-map preview owns one record per lane; empty channel text means unset.
    object_id_t channel_map_layout_segment_id;
    std::vector<metadata_channel_map_lane_t> channel_map_lanes;
    std::vector<chart_id_t> batch_chart_ids;
    bool batch_edit_author;
    bool batch_edit_rating;
    size_t lane_profile_index;
};
} // namespace wormedit::ui

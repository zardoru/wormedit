#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit {

enum class osu_mania_file_profile_t {
    stable_v14,
    lazer_v128,
};

enum class osu_mania_parse_diagnostic_code_t {
    invalid_encoding,
    unsupported_version,
    invalid_section,
    malformed_field,
    unsupported_mode,
    invalid_difficulty,
    invalid_timing_point,
    invalid_hit_object,
    invalid_resource_path,
    invalid_storyboard,
    unsupported_storyboard,
    storyboard_limit,
    missing_timing_anchor,
    metadata_limit,
    measure_limit,
    event_limit,
    source_limit,
    internal_conversion_error,
    timing_quantization,
};

using osu_mania_parse_diagnostic_t =
    text_parse_diagnostic_t<osu_mania_parse_diagnostic_code_t,
                            osu_mania_parse_diagnostic_code_t::malformed_field>;

struct osu_mania_parse_options_t {
    std::size_t max_source_bytes{16U * 1024U * 1024U};
    std::size_t max_lines{1'000'000};
    std::size_t max_events{500'000};
    std::size_t max_measures{65'536};
    std::optional<std::filesystem::path> asset_base_directory;
    // Applies to non-timeline key/value and retained custom-section rows.
    std::size_t max_metadata_fields{4'096};
    std::size_t max_metadata_bytes{1U * 1024U * 1024U};
};

using osu_mania_parse_outcome_t = diagnosed_t<std::optional<song_document_t>, std::vector<osu_mania_parse_diagnostic_t>>;

enum class osu_mania_write_diagnostic_code_t {
    missing_chart,
    invalid_document,
    unsupported_feature,
    invalid_layout,
    unrepresentable_position,
    invalid_resource,
    invalid_metadata,
    metadata_limit,
    timestamp_quantization,
    lossy_transform,
    output_limit,
};

using osu_mania_write_diagnostic_t =
    text_write_diagnostic_t<osu_mania_write_diagnostic_code_t,
                            osu_mania_write_diagnostic_code_t::invalid_document>;

struct osu_mania_write_options_t {
    // When omitted, retain an imported supported profile; newly authored
    // documents default to stable v14.
    std::optional<osu_mania_file_profile_t> profile;
    std::optional<chart_id_t> chart;
    bool preserve_unknown_fields{true};
    std::size_t max_output_bytes{64U * 1024U * 1024U};
    std::size_t max_lines{1'000'000};
    std::size_t max_events{500'000};
    std::size_t max_measures{65'536};
    std::size_t max_rows_per_measure{1'000'000};
    // Applies to serialized non-timeline fields, including retained fields.
    std::size_t max_metadata_fields{4'096};
    std::size_t max_metadata_bytes{1U * 1024U * 1024U};
};

using osu_mania_write_outcome_t = diagnosed_t<std::optional<std::string>, std::vector<osu_mania_write_diagnostic_t>>;

// Parses one stable-v14 or lazer-v128 UTF-8 osu!mania .osu file. Other
// file-format versions reject explicitly. Parsing is bounded and transactional:
// the song is published only when the entire source has passed validation.
// Hit-object milliseconds remain available exactly in chart format metadata
// under osu.hitobject.<object-id>.time/.end_time while measure rows are derived
// from the ordered uninherited timing-point phase resets on a 960-division grid.
// Off-grid spans use bounded microsecond timing and adjusted editable BPMs;
// quantization emits a warning. Raw timing provenance is retained, and the
// source-time origin is explicit, including negative-time prefix measures.
[[nodiscard]] auto parse_osu_mania_bytes(std::span<const std::byte> bytes,
                                         osu_mania_parse_options_t options = {})
    -> osu_mania_parse_outcome_t;

// Emits one deterministic UTF-8 osu!mania .osu chart. Stable-v14 hit-object
// times are integer milliseconds rounded to nearest, with exact
// half-millisecond ties away from zero. Lazer-v128 emits finite decimal times
// and preserves unchanged imported spellings. An omitted profile retains a
// supported imported version and otherwise selects stable v14.
[[nodiscard]] auto write_osu_mania(const song_document_t& song,
                                   osu_mania_write_options_t options = {})
    -> osu_mania_write_outcome_t;

} // namespace wormedit

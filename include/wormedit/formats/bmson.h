#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit {

enum class bmson_parse_diagnostic_code_t {
    invalid_json,
    unsupported_version,
    missing_field,
    invalid_type,
    invalid_value,
    integer_out_of_range,
    invalid_geometry,
    duplicate_event,
    unresolved_resource,
};

enum class bmson_write_diagnostic_code_t {
    missing_chart,
    invalid_document,
    unsupported_feature,
    invalid_geometry,
    unrepresentable_number,
    lossy_transform,
    unresolved_resource,
    duplicate_event,
};

struct bmson_parse_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    bmson_parse_diagnostic_code_t code{bmson_parse_diagnostic_code_t::invalid_json};
    std::string message;
    std::string json_path;
    std::optional<std::size_t> byte_offset;
};

struct bmson_parse_options_t {
    std::size_t max_source_bytes{16U * 1024U * 1024U};
    std::size_t max_nesting_depth{128};
    std::size_t max_json_values{1'000'000};
    std::size_t max_events{500'000};
    std::size_t max_measures{65'536};
    // When provided, relative asset paths are checked under this directory.
    // Without it, paths are retained and marked unresolved for a later relink.
    std::optional<std::filesystem::path> asset_base_directory;
};

struct bmson_parse_result_t {
    std::optional<song_document_t> song;
    std::vector<bmson_parse_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
};

// Imports strict UTF-8 BMSON 1.0 bytes into Wormedit's loss-aware document.
// Original JSON bytes are retained through format metadata for unchanged saves.
[[nodiscard]] auto parse_bmson_bytes(std::span<const std::byte> bytes,
                                     bmson_parse_options_t options = {})
    -> bmson_parse_result_t;

struct bmson_write_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    bmson_write_diagnostic_code_t code{bmson_write_diagnostic_code_t::invalid_document};
    std::string message;
    std::string json_path;
};

struct bmson_write_options_t {
    // BMSON stores one chart per file. Omit this only when the song has exactly one chart.
    std::optional<chart_id_t> chart;
    // Retain unmodeled but valid source fields from an imported BMSON document.
    bool preserve_source_fields{true};
};

struct bmson_write_result_t {
    std::optional<std::string> json;
    std::vector<bmson_write_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
};

// Exports one chart as BMSON 1.0 JSON. Positions and durations are represented exactly;
// unsupported document features are reported instead of being silently discarded.
[[nodiscard]] auto write_bmson(const song_document_t& song,
                               bmson_write_options_t options = {})
    -> bmson_write_result_t;

} // namespace wormedit

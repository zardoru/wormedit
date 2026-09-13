#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace wormedit {

enum class bms_write_diagnostic_code_t {
    missing_chart,
    invalid_document,
    unsupported_feature,
    missing_profile,
    invalid_geometry,
    unrepresentable_number,
    unresolved_resource,
    duplicate_event,
    resource_limit,
    lossy_transform,
};

struct bms_write_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    bms_write_diagnostic_code_t code{bms_write_diagnostic_code_t::invalid_document};
    std::string message;
    std::string path;
};

struct bms_write_options_t {
    // BMS-family files contain one chart. Omit this only for a single-chart song.
    std::optional<chart_id_t> chart;
    // Select a built-in lane profile when the chart has no authored BMS channel map.
    std::optional<std::string> lane_profile_id;
    // Original bytes are emitted only while their import provenance is still unchanged.
    bool preserve_unchanged_source{true};
    // BMS channel payloads are bounded by both this slot count and the parser's line limit.
    std::size_t max_slots_per_line{500'000};
};

struct bms_write_result_t {
    std::optional<std::string> bytes;
    std::vector<bms_write_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
};

// Writes unchanged imported BMS bytes exactly, or emits a deterministic UTF-8
// BMS projection for metadata, resources, measure lengths, initial BPM, and taps.
// Other semantic features fail with typed diagnostics until their export rules land.
[[nodiscard]] auto write_bms(const song_document_t& song, bms_write_options_t options = {})
    -> bms_write_result_t;

} // namespace wormedit

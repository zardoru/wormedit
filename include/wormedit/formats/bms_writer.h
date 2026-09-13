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
    unsupported_branch_policy,
    branch_provenance,
};

struct bms_write_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    bms_write_diagnostic_code_t code{bms_write_diagnostic_code_t::invalid_document};
    std::string message;
    std::string path;
};

enum class bms_branch_policy_t {
    preserve_tree,
    flatten_active,
};

struct bms_write_options_t {
    // BMS-family files contain one chart. Omit this only for a single-chart song.
    std::optional<chart_id_t> chart;
    // Select a built-in lane profile when the chart has no authored BMS channel map.
    std::optional<std::string> lane_profile_id;
    // Original bytes are emitted only while their import provenance is still unchanged.
    bool preserve_unchanged_source{true};
    // Full-tree preservation is the safe default. Active-projection flattening
    // is a separate, explicitly lossy operation and is not implemented yet.
    bms_branch_policy_t branch_policy{bms_branch_policy_t::preserve_tree};
    // BMS channel payloads are bounded by both this slot count and the parser's line limit.
    std::size_t max_slots_per_line{500'000};
};

struct bms_write_result_t {
    std::optional<std::string> bytes;
    std::vector<bms_write_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
};

// Writes unchanged imported BMS bytes exactly, or emits deterministic UTF-8
// BMS for metadata, resources, measure lengths, initial BPM, taps, holds,
// repeated BGM source lanes, and representable BGA events. Unsupported semantic
// features fail with typed diagnostics.
[[nodiscard]] auto write_bms(const song_document_t& song, bms_write_options_t options = {})
    -> bms_write_result_t;

} // namespace wormedit

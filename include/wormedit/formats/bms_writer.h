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
    unsupported_encoding,
    unrepresentable_text,
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
    // is a separate, explicitly lossy operation and requires resolved choices.
    bms_branch_policy_t branch_policy{bms_branch_policy_t::preserve_tree};
    // Omit to retain exact unchanged source bytes when possible and otherwise
    // emit UTF-8 with BOM. Supplying an encoding forces normalized output in
    // that encoding; unknown is rejected and legacy codecs require iconv.
    std::optional<text_encoding_t> output_encoding;
    // BMS channel payloads are bounded by both this slot count and the parser's line limit.
    std::size_t max_slots_per_line{500'000};
};

struct bms_write_result_t {
    std::optional<std::string> bytes;
    std::vector<bms_write_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
};

// Writes unchanged imported BMS bytes exactly, or emits deterministic normalized
// BMS in the requested encoding for metadata, resources, measure lengths, initial
// BPM, taps, holds, repeated BGM source lanes, and representable BGA events.
// Explicit flatten_active output also emits projected BPM changes, BMS STOP-tick
// pauses, and exact supported measure ratios; unsupported pause units fail rather
// than being converted approximately.
// Unsupported semantic features fail with typed diagnostics. Preserve-tree output
// always emits every retained branch and its control statements; it never emits
// only the selected materialized projection or only its patches. Provenanced branch
// metadata/resource edits, measure-length edits, direct/extended BPM edits, channel
// events, and uniquely attributable pauses replace only their retained source
// statements. Other branch content remains source-backed. The paired BMS API also
// supports exact-provenance replacement of inactive raw branch statements and
// insertion after a source-verified statement in an inactive leaf; direct control-
// structure editing remains outside this bounded writer contract. It also supports
// exact-provenance deletion of one raw statement directly inside an inactive leaf,
// updating the retained source bytes and all later source identities atomically.
[[nodiscard]] auto write_bms(const song_document_t& song, bms_write_options_t options = {})
    -> bms_write_result_t;

} // namespace wormedit

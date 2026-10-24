#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/bms_program.h>
#include <wormedit/formats/bms_export_profile.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
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

using bms_write_diagnostic_t =
    format_diagnostic_t<bms_write_diagnostic_code_t, std::string,
                        bms_write_diagnostic_code_t::invalid_document>;

enum class bms_branch_policy_t {
    preserve_tree,
    flatten_active,
};

struct bms_write_options_t {
    [[nodiscard]] static auto from_profile(std::string_view name) -> std::optional<bms_write_options_t>;
    bms_export_profile_t profile{bms_export_profile_t::beatoraja};
    // BMS-family files contain one chart. Omit this only for a single-chart song.
    std::optional<chart_id_t> chart;
    // Select a built-in lane profile when the chart has no authored BMS channel map.
    std::optional<std::string> lane_profile_id;
    // Original bytes are emitted only while their import provenance is still unchanged.
    bool preserve_unchanged_source{true};
    // Re-encode untouched legacy source as UTF-8 without normalizing its lines.
    // Disable to retain original import bytes under the same source profile.
    bool convert_untouched_source_to_utf8{true};
    // Preserve actual control flow, not the original placement of ordinary timing.
    // Without control flow, edited BPM/STOP events are emitted normally. Discarding
    // control flow requires explicit flatten_active and resolved branch choices.
    bms_branch_policy_t branch_policy{bms_branch_policy_t::preserve_tree};
    // Omit to retain exact unchanged source bytes when possible and otherwise
    // use the player default (UTF-8 BOM for beatoraja, CP932 for LR2).
    // An explicit low-level override forces normalization; LR2 requires CP932.
    std::optional<text_encoding_t> output_encoding;
    // BMS channel payloads are bounded by both this slot count and the parser's line limit.
    std::size_t max_slots_per_line{500'000};
};

using bms_write_outcome_t = attempt_t<std::string, std::vector<bms_write_diagnostic_t>>;

// Serializes the complete scoped program, including inactive controls, definitions,
// content and Text. Original bytes may be reused only after semantic equivalence
// is proved, including any recorded automatic asset-path resolution. Explicit
// flatten_active exports the resolved playback outcome and reports discarded flow.
// Legacy native BMS inputs migrate losslessly before reaching this writer.
// Ordinary interchange inputs use their authored lane map and shared catalog.
// Unsupported features, inexact geometry and unprovable migration return typed
// diagnostics without partial output; native span losses are reported explicitly.
[[nodiscard]] auto write_bms(const song_document_t& song, bms_write_options_t options = {})
    -> bms_write_outcome_t;

// Prepared scoped export only; T-744 activates this owner publicly. Preserving
// all scopes is independent of evaluation choices. Flattening is explicit and
// governed by loss_policy, using the shared playable projection.
struct bms_program_write_options_t {
    bms_write_options_t format;
    bms_program_evaluation_t evaluation;
    loss_policy_t loss_policy{loss_policy_t::reject};
    // Optional read-only import backing. Passthrough verifies typed equivalence
    // against these bytes, so a stale unchanged flag cannot discard an edit.
    std::optional<encoding_provenance_t> source;
    bms_program_limits_t program_limits;
    std::size_t maximum_input_objects{1'000'000};
    std::size_t maximum_output_bytes{16U * 1024U * 1024U};
    std::size_t maximum_lines{262'144};
    [[nodiscard]] auto can_verify_source() const -> bool {
        return source && source->unchanged_since_import && source->original_bytes &&
            format.preserve_unchanged_source && !format.output_encoding &&
            format.branch_policy == bms_branch_policy_t::preserve_tree;
    }
};
[[nodiscard]] auto write_bms_program(const bms_prepared_program_t& prepared,
    bms_program_write_options_t options = {}) -> bms_write_outcome_t;

} // namespace wormedit

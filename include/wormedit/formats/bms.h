#pragma once

#include <wormedit/core/bms_profile_detection.h>
#include <wormedit/core/document.h>
#include <wormedit/core/bms_program.h>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit {

enum class bms_parse_diagnostic_code_t {
    invalid_encoding,
    unsupported_encoding,
    malformed_line,
    invalid_number,
    invalid_measure,
    malformed_control,
    duplicate_resource,
    unsupported_channel,
    missing_profile,
    unresolved_reference,
    invalid_branch_selection,
    unpaired_long_note,
};

struct bms_source_location_t {
    std::size_t line{};   // One-based physical line number.
    std::size_t column{}; // One-based UTF-8 byte column in decoded text.
    std::optional<bms_source_span_t> source_span;

    auto operator==(const bms_source_location_t&) const -> bool = default;
};

using bms_parse_diagnostic_t =
    format_diagnostic_t<bms_parse_diagnostic_code_t, bms_source_location_t,
                        bms_parse_diagnostic_code_t::malformed_line>;

struct bms_parse_options_t {
    // Non-ASCII input without a UTF-8 BOM is intentionally ambiguous. Select its codec here.
    std::optional<text_encoding_t> encoding_override;
    // Optional filename extension (with or without a leading dot) used only
    // as evidence for conservative automatic lane-profile detection.
    std::string file_extension;
    std::size_t max_source_bytes{16U * 1024U * 1024U};
    std::size_t max_line_bytes{1024U * 1024U};
    std::size_t max_nesting_depth{256};
    std::size_t max_lines{262144};
    // Exact BMS subdivisions may promote the chart's row resolution. Payloads
    // requiring a larger resolution remain raw and receive a diagnostic.
    std::uint64_t max_chart_resolution{1'000'000'000ULL};
    // Optional built-in profile selected by the project or importing caller.
    std::optional<std::string> lane_profile_id;
    // Explicit authored channel mapping for native migration and conversion validation.
    std::optional<lane_input_profile_t> authored_lane_profile;
    // Optional explicit choice for a RANDOM or SWITCH block. The source order
    // is the zero-based physical line order of the block's opening directive.
    // Unselected blocks retain every branch in the source tree and project no
    // branch-local channel payloads.
    struct branch_choice_t {
        std::size_t control_source_order{};
        std::int64_t selected_value{};
    };
    std::vector<branch_choice_t> branch_choices;
};

struct bms_parse_payload_t {
    std::optional<song_document_t> song{};
    encoding_provenance_t encoding{};
    // Ambiguous/custom-required evidence retains candidates and raw channel order for UI review.
    std::optional<bms_profile_detection_result_t> lane_profile_detection{};
};
using bms_parse_outcome_t = diagnosed_t<bms_parse_payload_t, std::vector<bms_parse_diagnostic_t>>;

struct bms_program_import_payload_t {
    std::optional<bms_prepared_program_t> prepared;
    encoding_provenance_t encoding;
};
using bms_program_import_outcome_t = diagnosed_t<bms_program_import_payload_t, std::vector<bms_parse_diagnostic_t>>;
[[nodiscard]] auto prepare_bms_program(std::span<const std::byte> bytes,
    bms_parse_options_t options = {}, bms_program_limits_t limits = {}) -> bms_program_import_outcome_t;

// Decode and classify opaque Text using the same policy as scoped export.
[[nodiscard]] auto validate_bms_program_text(const bms_text_t& text,
    std::size_t maximum_line_bytes = bms_text_t::maximum_line_bytes)
    -> result_t<std::string, error_t<failure_code_t>>;

// ASCII and UTF-8 are always available. Legacy encodings depend on the conversion
// backend provided by the build and host; unsupported requests fail explicitly.
[[nodiscard]] auto is_bms_text_encoding_supported(text_encoding_t encoding) -> bool;

// Parses BMS-family bytes into a loss-aware source tree and materializes supported
// root-level semantics. Branch-local metadata, definitions, measure geometry, and
// object payloads are projected when a RANDOM/SWITCH choice is explicitly supplied.
[[nodiscard]] auto parse_bms_bytes(std::span<const std::byte> bytes,
                                   bms_parse_options_t options = {}) -> bms_parse_outcome_t;

} // namespace wormedit

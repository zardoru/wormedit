#pragma once

#include <wormedit/core/document.h>

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
    unresolved_reference,
    invalid_branch_selection,
    unpaired_long_note,
};

struct bms_parse_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    bms_parse_diagnostic_code_t code{bms_parse_diagnostic_code_t::malformed_line};
    std::string message;
    std::size_t line{};   // One-based physical line number.
    std::size_t column{}; // One-based UTF-8 byte column in decoded text.
    std::optional<bms_source_span_t> source_span;
};

struct bms_parse_options_t {
    // Non-ASCII input without a UTF-8 BOM is intentionally ambiguous. Select its codec here.
    std::optional<text_encoding_t> encoding_override;
    std::size_t max_source_bytes{16U * 1024U * 1024U};
    std::size_t max_line_bytes{1024U * 1024U};
    std::size_t max_nesting_depth{256};
    std::size_t max_lines{262144};
    // Exact BMS subdivisions may promote the chart's row resolution. Payloads
    // requiring a larger resolution remain raw and receive a diagnostic.
    std::uint64_t max_chart_resolution{1'000'000'000ULL};
    // Optional built-in profile selected by the project or importing caller.
    std::optional<std::string> lane_profile_id;
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

struct bms_parse_result_t {
    std::optional<song_document_t> song;
    encoding_provenance_t encoding;
    std::vector<bms_parse_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
};

// ASCII and UTF-8 are always available. Legacy encodings depend on the conversion
// backend provided by the build and host; unsupported requests fail explicitly.
[[nodiscard]] auto is_bms_text_encoding_supported(text_encoding_t encoding) -> bool;

// Parses BMS-family bytes into a loss-aware source tree and materializes supported
// root-level semantics. Branch-local metadata, definitions, measure geometry, and
// object payloads are projected when a RANDOM/SWITCH choice is explicitly supplied.
[[nodiscard]] auto parse_bms_bytes(std::span<const std::byte> bytes,
                                   bms_parse_options_t options = {}) -> bms_parse_result_t;

} // namespace wormedit

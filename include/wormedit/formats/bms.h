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

// Parses BMS-family bytes into a loss-aware source tree. This initial reader handles
// metadata, resources, measure lengths, and control flow; chart-channel payloads remain raw.
[[nodiscard]] auto parse_bms_bytes(std::span<const std::byte> bytes,
                                   bms_parse_options_t options = {}) -> bms_parse_result_t;

} // namespace wormedit

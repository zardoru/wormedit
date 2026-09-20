#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit {

enum class stepmania_profile_t {
    sm,
    ssc,
};

// Timing compatibility only; these profiles describe accepted/emitted warp
// syntax, not a specific StepMania release or complete implementation.
enum class stepmania_implementation_profile_t {
    unspecified,
    native_warps,
    legacy_negative_bpm,
    legacy_negative_stop,
};

enum class stepmania_parse_diagnostic_code_t {
    invalid_encoding,
    malformed_tag,
    duplicate_field,
    unsupported_version,
    unsupported_step_type,
    invalid_timing,
    legacy_warp_encoding,
    implementation_profile_mismatch,
    invalid_notes,
    invalid_hold,
    position_not_representable,
    source_limit,
    event_limit,
    chart_limit,
    measure_limit,
    division_limit,
};

struct stepmania_parse_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    stepmania_parse_diagnostic_code_t code{stepmania_parse_diagnostic_code_t::malformed_tag};
    std::string message;
    std::size_t source_order{};
    std::optional<std::size_t> byte_offset;
};

struct stepmania_parse_options_t {
    stepmania_profile_t profile{stepmania_profile_t::sm};
    stepmania_implementation_profile_t source_implementation_profile{
        stepmania_implementation_profile_t::unspecified};
    std::size_t max_source_bytes{16U * 1024U * 1024U};
    std::size_t max_fields{100'000};
    std::size_t max_charts{4096};
    std::size_t max_events{500'000};
    std::size_t max_measures{65'536};
    std::uint64_t max_divisions_per_standard_measure{1'000'000};
};

struct stepmania_parse_result_t {
    std::optional<song_document_t> song;
    std::vector<stepmania_parse_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
};

// Parses a complete .sm or .ssc source transactionally. SSC is bounded to the
// audited StepMania 5.1 v0.83 semantics (with missing VERSION defaulting to
// 0.83); song fields and each #NOTEDATA block remain separately addressable.
[[nodiscard]] auto parse_stepmania_bytes(std::span<const std::byte> bytes,
                                         stepmania_parse_options_t options = {})
    -> stepmania_parse_result_t;

enum class stepmania_write_diagnostic_code_t {
    missing_chart,
    invalid_document,
    unsupported_feature,
    unrepresentable_position,
    conflicting_song_timing,
    conflicting_song_metadata,
    legacy_warp_conversion,
    invalid_metadata,
    output_limit,
    invalid_note_layout,
};

struct stepmania_write_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    stepmania_write_diagnostic_code_t code{stepmania_write_diagnostic_code_t::invalid_document};
    std::string message;
    std::string field;
};

struct stepmania_write_options_t {
    stepmania_profile_t profile{stepmania_profile_t::sm};
    stepmania_implementation_profile_t target_implementation_profile{
        stepmania_implementation_profile_t::unspecified};
    std::optional<chart_id_t> chart;
    warp_encoding_t warp_encoding{warp_encoding_t::native};
    bool preserve_unknown_fields{true};
    std::size_t max_output_bytes{64U * 1024U * 1024U};
    std::size_t max_charts{4096};
    std::size_t max_rows_per_measure{65'536};
};

struct stepmania_write_result_t {
    std::optional<std::string> text;
    std::vector<stepmania_write_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
};

// Emits deterministic UTF-8 StepMania text. Unsupported editor-only
// annotations are surfaced as loss diagnostics rather than silently dropped.
[[nodiscard]] auto write_stepmania(const song_document_t& song,
                                   stepmania_write_options_t options = {})
    -> stepmania_write_result_t;

} // namespace wormedit

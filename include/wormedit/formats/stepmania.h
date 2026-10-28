#pragma once

#include <wormedit/formats/conversion.h>
#include <wormedit/formats/text_source_location.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit {

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

using stepmania_parse_diagnostic_t =
    format_diagnostic_t<stepmania_parse_diagnostic_code_t, text_source_location_t,
                            stepmania_parse_diagnostic_code_t::malformed_tag>;

struct stepmania_parse_options_t {
    stepmania_profile_t profile{stepmania_profile_t::sm};
    stepmania_timing_syntax_t source_timing_syntax{
        stepmania_timing_syntax_t::unspecified};
    stepmania_implementation_profile_t source_implementation_profile{
        stepmania_implementation_profile_t::unspecified};
    std::size_t max_source_bytes{16U * 1024U * 1024U};
    std::size_t max_fields{100'000};
    std::size_t max_charts{4096};
    std::size_t max_events{500'000};
    std::size_t max_measures{65'536};
    std::uint64_t max_divisions_per_standard_measure{1'000'000};
    std::optional<std::filesystem::path> asset_base_directory;
};

using stepmania_parse_outcome_t = attempt_t<song_document_t, std::vector<stepmania_parse_diagnostic_t>>;

// Parses a complete .sm or .ssc source transactionally. SSC is bounded to
// audited v0.83 semantics (with missing VERSION defaulting to 0.83); named
// StepMania 5.1 and Etterna 0.75.1 subsets require native SSC warps. Song
// fields and each #NOTEDATA block remain separately addressable.
[[nodiscard]] auto parse_stepmania_bytes(std::span<const std::byte> bytes,
                                         stepmania_parse_options_t options = {})
    -> stepmania_parse_outcome_t;

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

using stepmania_write_diagnostic_t =
    format_diagnostic_t<stepmania_write_diagnostic_code_t, std::string,
                            stepmania_write_diagnostic_code_t::invalid_document>;

struct stepmania_write_options_t {
    // Resolves file, syntax and implementation choices. Document-specific loss
    // is still diagnosed transactionally by write_stepmania.
    [[nodiscard]] static auto from(stepmania_export_policy_t policy)
        -> result_t<stepmania_write_options_t, error_t<failure_code_t>>;

    stepmania_profile_t profile{stepmania_profile_t::sm};
    stepmania_timing_syntax_t target_timing_syntax{
        stepmania_timing_syntax_t::unspecified};
    stepmania_implementation_profile_t target_implementation_profile{
        stepmania_implementation_profile_t::unspecified};
    std::optional<chart_id_t> chart;
    warp_encoding_t warp_encoding{warp_encoding_t::native};
    bool preserve_unknown_fields{true};
    // Reports a loss warning when nonterminating timing values need decimal rounding.
    bool allow_timing_approximation{false};
    std::size_t max_output_bytes{64U * 1024U * 1024U};
    std::size_t max_charts{4096};
    std::size_t max_rows_per_measure{65'536};
};

using stepmania_write_outcome_t = attempt_t<std::string, std::vector<stepmania_write_diagnostic_t>>;

using stepmania_export_policy_outcome_t = result_t<stepmania_write_options_t, error_t<failure_code_t>>;

// Emits deterministic UTF-8 StepMania text. Unsupported editor-only
// annotations are surfaced as loss diagnostics rather than silently dropped.
[[nodiscard]] auto write_stepmania(const song_document_t& song,
                                   stepmania_write_options_t options = {})
    -> stepmania_write_outcome_t;

} // namespace wormedit

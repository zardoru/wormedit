#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit {

enum class osu_mania_parse_diagnostic_code_t {
    invalid_encoding,
    invalid_section,
    malformed_field,
    unsupported_mode,
    invalid_difficulty,
    invalid_timing_point,
    invalid_hit_object,
    invalid_resource_path,
    missing_timing_anchor,
    measure_limit,
    event_limit,
    source_limit,
    internal_conversion_error,
};

struct osu_mania_parse_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    osu_mania_parse_diagnostic_code_t code{osu_mania_parse_diagnostic_code_t::malformed_field};
    std::string message;
    std::size_t source_order{};
    std::optional<std::size_t> byte_offset;
};

struct osu_mania_parse_options_t {
    std::size_t max_source_bytes{16U * 1024U * 1024U};
    std::size_t max_lines{1'000'000};
    std::size_t max_events{500'000};
    std::size_t max_measures{65'536};
    std::optional<std::filesystem::path> asset_base_directory;
};

struct osu_mania_parse_result_t {
    std::optional<song_document_t> song;
    std::vector<osu_mania_parse_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
};

// Parses one UTF-8 osu!mania .osu file. Parsing is bounded and transactional:
// the song is published only when the entire source has passed validation.
// Hit-object milliseconds remain available exactly in chart format metadata
// under osu.hitobject.<object-id>.time/.end_time while measure rows are derived
// from the ordered uninherited timing-point phase resets.
[[nodiscard]] auto parse_osu_mania_bytes(std::span<const std::byte> bytes,
                                         osu_mania_parse_options_t options = {})
    -> osu_mania_parse_result_t;

} // namespace wormedit

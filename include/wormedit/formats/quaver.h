#pragma once
#include <wormedit/core/document.h>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>

namespace wormedit {
enum class quaver_diagnostic_code_t {
    invalid_encoding, invalid_yaml, source_limit, structure_limit, invalid_version,
    invalid_mode, missing_content, invalid_value, invalid_sample, invalid_hold,
    invalid_path, measure_limit, quantization, retained_feature, missing_chart,
    unsupported_feature, invalid_document, output_limit,
};
using quaver_diagnostic_t = format_diagnostic_t<quaver_diagnostic_code_t, std::string>;
struct quaver_parse_options_t {
    std::size_t max_source_bytes{16U * 1024U * 1024U};
    std::size_t max_nodes{2'000'000};
    std::size_t max_depth{32};
    std::size_t max_events{500'000};
    std::size_t max_measures{65'536};
    std::optional<std::filesystem::path> asset_base_directory;
};
struct quaver_write_options_t {
    std::optional<chart_id_t> chart;
    std::size_t max_output_bytes{64U * 1024U * 1024U};
    std::size_t max_events{500'000};
};
using quaver_parse_outcome_t = attempt_t<song_document_t, std::vector<quaver_diagnostic_t>>;
using quaver_write_outcome_t = attempt_t<std::string, std::vector<quaver_diagnostic_t>>;
// Experimental, post-1.0 profile. Single UTF-8 YAML document; aliases reject.
// Unknown keys are ignored. Unmodeled groups/flags and exact source times live
// in existing format metadata/provenance and survive native save/load.
[[nodiscard]] auto parse_quaver_bytes(std::span<const std::byte> bytes,
    quaver_parse_options_t options = {}) -> quaver_parse_outcome_t;
// Deterministic CRLF YAML. Unsupported modeled features reject atomically.
[[nodiscard]] auto write_quaver(const song_document_t& song,
    quaver_write_options_t options = {}) -> quaver_write_outcome_t;
}

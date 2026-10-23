#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit {
struct document_history_t;

enum class native_project_diagnostic_code_t {
    malformed_json,
    unsupported_version,
    resource_limit,
    invalid_document,
    serialization_failure,
};

using native_project_diagnostic_t =
    format_diagnostic_t<native_project_diagnostic_code_t, std::string>;

struct native_project_limits_t {
    std::size_t maximum_source_bytes{64U * 1024U * 1024U};
    std::size_t maximum_json_nodes{2'000'000};
    std::size_t maximum_nesting_depth{128};
};

using native_project_write_outcome_t = attempt_t<std::string, std::vector<native_project_diagnostic_t>>;

using native_project_parse_outcome_t = attempt_t<song_document_t, std::vector<native_project_diagnostic_t>>;

// The codec is independent of project packaging and filesystem publication. It
// emits one deterministic, versioned UTF-8 JSON payload containing the complete
// editable song document, including retained source provenance and BMS trees.
[[nodiscard]] auto write_native_project(const song_document_t& song,
                                        native_project_limits_t limits = {})
    -> native_project_write_outcome_t;

// Parsing is all-or-error: bounded JSON is decoded into a temporary document,
// enum domains and the complete document graph are validated, and only then is
// the document published in the result.
[[nodiscard]] auto parse_native_project(std::string_view bytes, native_project_limits_t limits = {})
    -> native_project_parse_outcome_t;

// Prepared, one-way migration only until T-744. Returns a detached proposal;
// unsupported or unprovable legacy state leaves the complete original untouched.
[[nodiscard]] auto prepare_native_bms_migration(document_history_t& history,
    chart_id_t chart, native_project_limits_t limits = {}) -> native_project_parse_outcome_t;

} // namespace wormedit

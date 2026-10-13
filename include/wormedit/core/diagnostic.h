#pragma once

#include <wormedit/core/id.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace wormedit {

enum class diagnostic_severity_t {
    info,
    warning,
    error,
};

enum class diagnostic_code_t {
    duplicate_id,
    invalid_id,
    invalid_resolution,
    invalid_measure,
    unknown_measure,
    row_out_of_range,
    unknown_lane,
    invalid_hold,
    dangling_resource,
    dangling_asset,
    duplicate_binding,
    invalid_binding,
    invalid_stage,
    invalid_rational,
    collision,
    unsupported_feature,
    approximated_feature,
    dropped_feature,
};

template <class Code, Code DefaultCode> struct object_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    Code code{DefaultCode};
    std::string message;
    std::optional<object_id_t> object;
};

template <class Code, Code DefaultCode> struct path_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    Code code{DefaultCode};
    std::string message;
    std::string path;
};

template <class Code, Code DefaultCode> struct text_parse_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    Code code{DefaultCode};
    std::string message;
    std::size_t source_order{};
    std::optional<std::size_t> byte_offset;
};

template <class Code, Code DefaultCode> struct text_write_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    Code code{DefaultCode};
    std::string message;
    std::string field;
};

using diagnostic_t = object_diagnostic_t<diagnostic_code_t, diagnostic_code_t::invalid_id>;

using diagnostic_list_t = std::vector<diagnostic_t>;

namespace diagnostic_queries {

template <class Diagnostic>
[[nodiscard]] auto has_errors(const std::vector<Diagnostic>& diagnostics) -> bool {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == diagnostic_severity_t::error) return true;
    }
    return false;
}

} // namespace diagnostic_queries

} // namespace wormedit

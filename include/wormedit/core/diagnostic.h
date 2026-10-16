#pragma once

#include <wormedit/core/id.h>

#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <vector>

namespace wormedit {

enum class diagnostic_severity_t {
    info,
    warning,
    error,
};

// The optional default preserves each codec's existing default diagnostic code.
template <class Code, class Location, Code DefaultCode = Code{}> struct format_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    Code code{DefaultCode};
    std::string message;
    Location location{};

    auto operator==(const format_diagnostic_t&) const -> bool = default;
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
    auto operator==(const object_diagnostic_t&) const -> bool = default;
};

template <class Code, Code DefaultCode, class Path = std::string> struct path_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    Code code{DefaultCode};
    std::string message;
    Path path{};
    auto operator==(const path_diagnostic_t&) const -> bool = default;
};

using diagnostic_t = object_diagnostic_t<diagnostic_code_t, diagnostic_code_t::invalid_id>;

using diagnostic_list_t = std::vector<diagnostic_t>;

namespace diagnostic_queries {

[[nodiscard]] auto has_errors(const auto& diagnostics) -> bool
    requires std::ranges::input_range<decltype(diagnostics)>
{
    for (const auto& diagnostic : diagnostics) {
        if constexpr (requires { diagnostic.severity; }) {
            if (diagnostic.severity == diagnostic_severity_t::error) return true;
        } else {
            // Error-only reports make their implicit severity explicit on the type.
            if (diagnostic.is_error()) return true;
        }
    }
    return false;
}

// A single failure can stay inline on an event path; use the same severity rule.
template <class Diagnostic>
[[nodiscard]] auto has_errors(const std::optional<Diagnostic>& diagnostic) -> bool {
    return diagnostic && has_errors(std::span<const Diagnostic>{&*diagnostic, 1});
}

// Phase-specific reports retain their original lists and own the aggregate query.
template <class Report>
[[nodiscard]] auto has_errors(const Report& report) -> bool
    requires requires { report.has_errors(); } {
    return report.has_errors();
}

} // namespace diagnostic_queries

} // namespace wormedit

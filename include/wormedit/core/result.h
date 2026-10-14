#pragma once

#include <wormedit/core/diagnostic.h>

#include <expected>
#include <string>

namespace wormedit {

template <class T, class E> using result_t = std::expected<T, E>;

template <class Code> struct error_t {
    Code code;
    std::string message;

    auto operator==(const error_t&) const -> bool = default;
};

// Typed diagnostic vectors retain their codes and metadata without a second list.
// The default remains the shared diagnostic_list_t.
template <class T, class Diagnostics = diagnostic_list_t> struct diagnosed_t {
    T value;
    Diagnostics diagnostics;

    [[nodiscard]] auto ok() const -> bool {
        return !diagnostic_queries::has_errors(diagnostics);
    }
};

} // namespace wormedit

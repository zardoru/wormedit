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

template <class T> struct diagnosed_t {
    T value;
    diagnostic_list_t diagnostics;

    [[nodiscard]] auto ok() const -> bool {
        return !diagnostic_queries::has_errors(diagnostics);
    }
};

} // namespace wormedit

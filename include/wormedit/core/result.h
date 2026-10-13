#pragma once

#include <wormedit/core/diagnostic.h>

#include <algorithm>
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
        return std::ranges::none_of(diagnostics, &diagnosed_t::is_error);
    }

  private:
    [[nodiscard]] static auto is_error(const diagnostic_t& diagnostic) -> bool {
        return diagnostic.severity == diagnostic_severity_t::error;
    }
};

} // namespace wormedit

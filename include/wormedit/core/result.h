#pragma once

#include <wormedit/core/diagnostic.h>

#include <expected>
#include <concepts>
#include <optional>
#include <string>

namespace wormedit {

template <class T, class E> using result_t = std::expected<T, E>;

template <class Code> struct error_t {
    Code code;
    std::string message;

    auto operator==(const error_t&) const -> bool = default;
};

enum class diagnostic_acceptance_policy_t { allow_warnings, require_empty };

// Typed diagnostic vectors retain their codes and metadata without a second list.
// The default remains the shared diagnostic_list_t.
template <class T, class Diagnostics = diagnostic_list_t> struct diagnosed_t {
    T value;
    Diagnostics diagnostics;

    [[nodiscard]] auto ok() const -> bool {
        return !diagnostic_queries::has_errors(diagnostics);
    }

    // Boolean dispatches carry a separate acceptance flag. Boundaries that have
    // always rejected warnings keep that policy explicit without another wrapper.
    [[nodiscard]] auto is_accepted(diagnostic_acceptance_policy_t policy) const -> bool
        requires std::same_as<T, bool> && requires { diagnostics.empty(); }
    {
        return value && (policy == diagnostic_acceptance_policy_t::require_empty
                             ? diagnostics.empty() : ok());
    }
};

using dispatch_outcome_t = diagnosed_t<bool>;

// Payload presence and diagnostic severity are independent: warnings can accompany
// an absent value, and errors can retain a partial value for inspection.
template <class T, class Diagnostics = diagnostic_list_t>
using attempt_t = diagnosed_t<std::optional<T>, Diagnostics>;

} // namespace wormedit

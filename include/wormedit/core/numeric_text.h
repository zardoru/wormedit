#pragma once

#include <wormedit/core/document.h>

#include <optional>
#include <string_view>

namespace wormedit {

// Locale-independent signed decimal, optionally followed by e/E and a signed
// exponent. No whitespace, fractions, nonfinite values, or rounding; rejects
// values that cannot fit a reduced signed_rational_t.
[[nodiscard]] auto parse_exact_decimal(std::string_view text)
    -> std::optional<signed_rational_t>;

} // namespace wormedit

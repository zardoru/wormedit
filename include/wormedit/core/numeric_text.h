#pragma once

#include <wormedit/core/document.h>

#include <charconv>
#include <optional>
#include <string>
#include <string_view>

namespace wormedit {

template <class Integer>
[[nodiscard]] auto parse_integer(std::string_view text, Integer& value) noexcept -> bool {
    if (text.empty())
        return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

template <class Integer>
[[nodiscard]] auto parse_integer(const char* text) noexcept -> std::optional<Integer> {
    if (!text)
        return std::nullopt;
    Integer value{};
    return parse_integer(std::string_view{text}, value) ? std::optional{value} : std::nullopt;
}

// Locale-independent signed decimal, optionally followed by e/E and a signed
// exponent. No whitespace, fractions, nonfinite values, or rounding; rejects
// values that cannot fit a reduced signed_rational_t.
[[nodiscard]] auto parse_exact_decimal(std::string_view text,
    std::optional<std::uint32_t> maximum_exponent = std::nullopt)
    -> std::optional<signed_rational_t>;

// An exact value typed as a decimal ("-0.25", "1e-3") or a fraction ("-1/4"), with surrounding
// whitespace ignored. Fractions take a signed integer over a positive one. The result is reduced.
[[nodiscard]] auto parse_exact_rational(std::string_view text) -> std::optional<signed_rational_t>;

// The text parse_exact_rational reads back to `value`: a terminating decimal of at most
// `maximum_fraction_digits` digits, otherwise the reduced fraction.
[[nodiscard]] auto format_exact_rational(signed_rational_t value,
    std::size_t maximum_fraction_digits = 6) -> std::string;

// `value + count * step`, exactly; nothing when the result does not fit.
[[nodiscard]] auto offset_by_steps(signed_rational_t value, signed_rational_t step,
    std::int64_t count) -> std::optional<signed_rational_t>;

} // namespace wormedit

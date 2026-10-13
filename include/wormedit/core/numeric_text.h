#pragma once

#include <wormedit/core/document.h>

#include <charconv>
#include <optional>
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

} // namespace wormedit

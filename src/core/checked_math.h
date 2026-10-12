#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
namespace wormedit::checked_math {
inline constexpr auto magnitude(std::int64_t value) noexcept -> std::uint64_t {
    return value >= 0 ? static_cast<std::uint64_t>(value)
                      : static_cast<std::uint64_t>(-(value + 1)) + 1;
}
inline constexpr auto checked_multiply(std::uint64_t left, std::uint64_t right) noexcept
    -> std::optional<std::uint64_t> {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return std::nullopt;
    return left * right;
}
inline constexpr auto checked_multiply(std::uint64_t left, std::uint64_t right,
                                       std::uint64_t& output) noexcept -> bool {
    const auto result = checked_multiply(left, right);
    if (!result)
        return false;
    output = *result;
    return true;
}
inline constexpr auto checked_signed_multiply(std::int64_t value, std::uint64_t factor) noexcept
    -> std::optional<std::int64_t> {
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto absolute = magnitude(value);
    const auto limit = value < 0 ? maximum + 1 : maximum;
    if (absolute != 0 && factor > limit / absolute)
        return std::nullopt;
    const auto product = absolute * factor;
    if (value < 0 && product == maximum + 1)
        return std::numeric_limits<std::int64_t>::min();
    return value < 0 ? -static_cast<std::int64_t>(product) : static_cast<std::int64_t>(product);
}
inline constexpr auto checked_add(std::int64_t left, std::int64_t right) noexcept
    -> std::optional<std::int64_t> {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
        return std::nullopt;
    return left + right;
}
inline constexpr auto checked_add(std::uint64_t left, std::uint64_t right,
                                  std::uint64_t& output) noexcept -> bool {
    if (left > std::numeric_limits<std::uint64_t>::max() - right)
        return false;
    output = left + right;
    return true;
}
inline constexpr auto checked_add(std::int64_t left, std::int64_t right,
                                  std::int64_t& output) noexcept -> bool {
    const auto result = checked_add(left, right);
    if (!result)
        return false;
    output = *result;
    return true;
}
inline constexpr auto checked_lcm(std::uint64_t left, std::uint64_t right) noexcept
    -> std::optional<std::uint64_t> {
    if (left == 0 || right == 0)
        return std::nullopt;
    return checked_multiply(left / std::gcd(left, right), right);
}
inline constexpr auto
account_size(std::size_t& total, std::size_t value,
             std::size_t maximum = std::numeric_limits<std::size_t>::max()) noexcept -> bool {
    if (total > maximum || value > maximum - total)
        return false;
    total += value;
    return true;
}
} // namespace wormedit::checked_math

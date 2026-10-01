#pragma once

#include <cstdint>

// Portable unsigned 128-bit arithmetic for the few places that need exact
// 64x64 products and quotients. Avoids compiler-specific __int128 so MSVC builds.
namespace wormedit::wide_math {

struct u128_t {
    std::uint64_t high{};
    std::uint64_t low{};

    auto operator==(const u128_t&) const -> bool = default;
};

[[nodiscard]] constexpr auto less(const u128_t& left, const u128_t& right) -> bool {
    return left.high != right.high ? left.high < right.high : left.low < right.low;
}

[[nodiscard]] constexpr auto fits_u64(const u128_t& value) -> bool { return value.high == 0; }

[[nodiscard]] constexpr auto multiply(std::uint64_t left, std::uint64_t right) -> u128_t {
    constexpr std::uint64_t mask = 0xFFFFFFFFULL;
    const auto left_low = left & mask;
    const auto left_high = left >> 32;
    const auto right_low = right & mask;
    const auto right_high = right >> 32;
    const auto low_low = left_low * right_low;
    const auto cross_a = left_high * right_low + (low_low >> 32);
    const auto cross_b = left_low * right_high + (cross_a & mask);
    return {left_high * right_high + (cross_a >> 32) + (cross_b >> 32),
            (cross_b << 32) | (low_low & mask)};
}

[[nodiscard]] constexpr auto add(const u128_t& left, std::uint64_t right) -> u128_t {
    const auto low = left.low + right;
    return {left.high + (low < left.low ? 1U : 0U), low};
}

[[nodiscard]] constexpr auto subtract(const u128_t& left, const u128_t& right) -> u128_t {
    const auto low = left.low - right.low;
    return {left.high - right.high - (left.low < right.low ? 1U : 0U), low};
}

struct divmod_t {
    u128_t quotient;
    u128_t remainder;
};

// Shift-and-subtract long division; divisor must be non-zero.
[[nodiscard]] constexpr auto divide(const u128_t& dividend, const u128_t& divisor) -> divmod_t {
    divmod_t result;
    for (int bit = 127; bit >= 0; --bit) {
        const auto source = bit >= 64 ? (dividend.high >> (bit - 64)) & 1U
                                      : (dividend.low >> bit) & 1U;
        result.remainder = {(result.remainder.high << 1) | (result.remainder.low >> 63),
                            (result.remainder.low << 1) | source};
        if (!less(result.remainder, divisor)) {
            result.remainder = subtract(result.remainder, divisor);
            if (bit >= 64) result.quotient.high |= std::uint64_t{1} << (bit - 64);
            else result.quotient.low |= std::uint64_t{1} << bit;
        }
    }
    return result;
}

}  // namespace wormedit::wide_math

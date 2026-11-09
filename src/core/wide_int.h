#pragma once

#include <compare>
#include <concepts>
#include <cstdint>
#include <type_traits>

// 128-bit integers for exact rational arithmetic. GCC and Clang get the compiler's __int128;
// MSVC has none, so there (or with WORMEDIT_PORTABLE_INT128 defined) a small two-limb class
// stands in. Only the operations the timing and BMS code needs are provided.
namespace wormedit::wide_int {

class portable_u128_t {
public:
    constexpr portable_u128_t() = default;
    // Implicit, like a built-in integer conversion. Signed sources sign-extend, so
    // portable_u128_t{-1} is the maximum, as it is for unsigned __int128.
    template<std::integral T>
    constexpr portable_u128_t(T value) noexcept : low_(static_cast<std::uint64_t>(value)) {
        if constexpr (std::is_signed_v<T>) high_ = value < 0 ? ~std::uint64_t{} : 0;
    }
    template<std::integral T>
    [[nodiscard]] explicit constexpr operator T() const noexcept {
        if constexpr (std::same_as<T, bool>) return (low_ | high_) != 0;
        else return static_cast<T>(low_);
    }

    [[nodiscard]] constexpr auto low() const noexcept -> std::uint64_t { return low_; }
    [[nodiscard]] constexpr auto high() const noexcept -> std::uint64_t { return high_; }

    [[nodiscard]] friend constexpr auto operator==(const portable_u128_t& a,
                                                   const portable_u128_t& b) noexcept -> bool {
        return a.low_ == b.low_ && a.high_ == b.high_;
    }
    [[nodiscard]] friend constexpr auto operator<=>(const portable_u128_t& a,
        const portable_u128_t& b) noexcept -> std::strong_ordering {
        if (a.high_ != b.high_) return a.high_ <=> b.high_;
        return a.low_ <=> b.low_;
    }

    [[nodiscard]] constexpr auto operator-() const noexcept -> portable_u128_t {
        return portable_u128_t{} - *this;
    }
    [[nodiscard]] friend constexpr auto operator+(const portable_u128_t& a,
                                                  const portable_u128_t& b) noexcept -> portable_u128_t {
        portable_u128_t result;
        result.low_ = a.low_ + b.low_;
        result.high_ = a.high_ + b.high_ + (result.low_ < a.low_ ? 1U : 0U);
        return result;
    }
    [[nodiscard]] friend constexpr auto operator-(const portable_u128_t& a,
                                                  const portable_u128_t& b) noexcept -> portable_u128_t {
        portable_u128_t result;
        result.low_ = a.low_ - b.low_;
        result.high_ = a.high_ - b.high_ - (a.low_ < b.low_ ? 1U : 0U);
        return result;
    }
    [[nodiscard]] friend constexpr auto operator*(const portable_u128_t& a,
                                                  const portable_u128_t& b) noexcept -> portable_u128_t {
        constexpr std::uint64_t mask = 0xFFFFFFFFULL;
        const auto a_low = a.low_ & mask, a_high = a.low_ >> 32;
        const auto b_low = b.low_ & mask, b_high = b.low_ >> 32;
        const auto low_low = a_low * b_low;
        const auto cross_a = a_high * b_low + (low_low >> 32);
        const auto cross_b = a_low * b_high + (cross_a & mask);
        portable_u128_t result;
        result.low_ = (cross_b << 32) | (low_low & mask);
        result.high_ = a_high * b_high + (cross_a >> 32) + (cross_b >> 32) +
                       a.low_ * b.high_ + a.high_ * b.low_;
        return result;
    }
    [[nodiscard]] friend constexpr auto operator/(const portable_u128_t& a,
                                                  const portable_u128_t& b) noexcept -> portable_u128_t {
        return divide(a, b, false);
    }
    [[nodiscard]] friend constexpr auto operator%(const portable_u128_t& a,
                                                  const portable_u128_t& b) noexcept -> portable_u128_t {
        return divide(a, b, true);
    }
    constexpr auto operator++() noexcept -> portable_u128_t& { return *this = *this + 1; }
    constexpr auto operator--() noexcept -> portable_u128_t& { return *this = *this - 1; }
    constexpr auto operator+=(const portable_u128_t& other) noexcept -> portable_u128_t& {
        return *this = *this + other;
    }
    constexpr auto operator-=(const portable_u128_t& other) noexcept -> portable_u128_t& {
        return *this = *this - other;
    }
    constexpr auto operator*=(const portable_u128_t& other) noexcept -> portable_u128_t& {
        return *this = *this * other;
    }
    constexpr auto operator/=(const portable_u128_t& other) noexcept -> portable_u128_t& {
        return *this = *this / other;
    }
    constexpr auto operator%=(const portable_u128_t& other) noexcept -> portable_u128_t& {
        return *this = *this % other;
    }

private:
    // Division by zero returns zero rather than trapping; callers check the divisor.
    [[nodiscard]] static constexpr auto divide(const portable_u128_t& dividend,
        const portable_u128_t& divisor, bool want_remainder) noexcept -> portable_u128_t {
        portable_u128_t quotient;
        portable_u128_t remainder;
        if (divisor.high_ == 0 && divisor.low_ == 0) return {};
        if (dividend.high_ == 0 && divisor.high_ == 0) {
            quotient.low_ = dividend.low_ / divisor.low_;
            remainder.low_ = dividend.low_ % divisor.low_;
            return want_remainder ? remainder : quotient;
        }
        for (int bit = 127; bit >= 0; --bit) {
            const auto source = bit >= 64 ? (dividend.high_ >> (bit - 64)) & 1U
                                          : (dividend.low_ >> bit) & 1U;
            remainder.high_ = (remainder.high_ << 1) | (remainder.low_ >> 63);
            remainder.low_ = (remainder.low_ << 1) | source;
            if (remainder >= divisor) {
                remainder -= divisor;
                if (bit >= 64) quotient.high_ |= std::uint64_t{1} << (bit - 64);
                else quotient.low_ |= std::uint64_t{1} << bit;
            }
        }
        return want_remainder ? remainder : quotient;
    }

    std::uint64_t low_{};
    std::uint64_t high_{};
};

// Two's complement over portable_u128_t.
class portable_i128_t {
public:
    constexpr portable_i128_t() = default;
    template<std::integral T>
    constexpr portable_i128_t(T value) noexcept : bits_(value) {}
    template<std::integral T>
    [[nodiscard]] explicit constexpr operator T() const noexcept { return static_cast<T>(bits_); }
    [[nodiscard]] explicit constexpr operator portable_u128_t() const noexcept { return bits_; }

    // Reinterprets the bits, like static_cast<__int128>(unsigned __int128).
    explicit constexpr portable_i128_t(const portable_u128_t& bits) noexcept : bits_(bits) {}
    [[nodiscard]] constexpr auto negative() const noexcept -> bool { return (bits_.high() >> 63) != 0; }

    [[nodiscard]] friend constexpr auto operator==(const portable_i128_t& a,
                                                   const portable_i128_t& b) noexcept -> bool {
        return a.bits_ == b.bits_;
    }
    [[nodiscard]] friend constexpr auto operator<=>(const portable_i128_t& a,
        const portable_i128_t& b) noexcept -> std::strong_ordering {
        if (a.negative() != b.negative()) return a.negative() ? std::strong_ordering::less
                                                              : std::strong_ordering::greater;
        return a.bits_ <=> b.bits_;
    }
    [[nodiscard]] constexpr auto operator-() const noexcept -> portable_i128_t {
        return portable_i128_t{-bits_};
    }
    [[nodiscard]] friend constexpr auto operator+(const portable_i128_t& a,
                                                  const portable_i128_t& b) noexcept -> portable_i128_t {
        return portable_i128_t{a.bits_ + b.bits_};
    }
    [[nodiscard]] friend constexpr auto operator-(const portable_i128_t& a,
                                                  const portable_i128_t& b) noexcept -> portable_i128_t {
        return portable_i128_t{a.bits_ - b.bits_};
    }
    [[nodiscard]] friend constexpr auto operator*(const portable_i128_t& a,
                                                  const portable_i128_t& b) noexcept -> portable_i128_t {
        return portable_i128_t{a.bits_ * b.bits_};
    }

    constexpr auto operator++() noexcept -> portable_i128_t& { return *this = *this + 1; }
    constexpr auto operator--() noexcept -> portable_i128_t& { return *this = *this - 1; }
    // Truncating division like the built-in; the remainder takes the dividend's sign.
    [[nodiscard]] friend constexpr auto operator/(const portable_i128_t& a,
                                                  const portable_i128_t& b) noexcept -> portable_i128_t {
        const auto quotient = a.magnitude() / b.magnitude();
        return a.negative() != b.negative() ? -portable_i128_t{quotient} : portable_i128_t{quotient};
    }
    [[nodiscard]] friend constexpr auto operator%(const portable_i128_t& a,
                                                  const portable_i128_t& b) noexcept -> portable_i128_t {
        const auto remainder = a.magnitude() % b.magnitude();
        return a.negative() ? -portable_i128_t{remainder} : portable_i128_t{remainder};
    }

private:
    [[nodiscard]] constexpr auto magnitude() const noexcept -> portable_u128_t {
        return negative() ? -bits_ : bits_;
    }
    portable_u128_t bits_;
};

// True when left - right does not fit; `result` receives the wrapped difference.
[[nodiscard]] constexpr auto sub_overflow(const portable_i128_t& left, const portable_i128_t& right,
                                          portable_i128_t& result) noexcept -> bool {
    result = left - right;
    return (left.negative() != right.negative()) && (result.negative() != left.negative());
}

#if defined(__SIZEOF_INT128__) && !defined(WORMEDIT_PORTABLE_INT128)
__extension__ using wide_unsigned_t = unsigned __int128;
__extension__ using wide_signed_t = __int128;

// True when left - right does not fit; `result` receives the wrapped difference.
[[nodiscard]] inline auto sub_overflow(wide_signed_t left, wide_signed_t right,
                                       wide_signed_t& result) noexcept -> bool {
    return __builtin_sub_overflow(left, right, &result);
}
#else
using wide_unsigned_t = portable_u128_t;
using wide_signed_t = portable_i128_t;
#endif

}  // namespace wormedit::wide_int

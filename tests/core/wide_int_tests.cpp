#include "core/wide_int.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <random>

namespace {

using wormedit::wide_int::portable_i128_t;
using wormedit::wide_int::portable_u128_t;

TEST_CASE("portable 128-bit arithmetic matches small exact values", "[wide-int]") {
    const portable_u128_t big = portable_u128_t{std::numeric_limits<std::uint64_t>::max()} + 1;
    REQUIRE(big.high() == 1);
    REQUIRE(big.low() == 0);
    REQUIRE(big - 1 == portable_u128_t{std::numeric_limits<std::uint64_t>::max()});
    REQUIRE(portable_u128_t{-1}.high() == ~std::uint64_t{});
    REQUIRE(static_cast<std::uint64_t>(big * 3 / 3 + 5) == 5);
    REQUIRE(big % 7 == portable_u128_t{2});  // 2^64 mod 7 == 2
    REQUIRE((big * big) == portable_u128_t{});  // wraps at 2^128

    const portable_i128_t negative = -portable_i128_t{5};
    REQUIRE(negative < 0);
    REQUIRE(negative + 5 == portable_i128_t{0});
    REQUIRE(static_cast<portable_u128_t>(negative) == portable_u128_t{-5});
    REQUIRE(portable_i128_t{std::numeric_limits<std::int64_t>::min()} < negative);
    REQUIRE(portable_i128_t{-7} / 2 == portable_i128_t{-3});
    REQUIRE(portable_i128_t{-7} % 2 == portable_i128_t{-1});
    REQUIRE(portable_i128_t{7} / -2 == portable_i128_t{-3});
    REQUIRE(portable_i128_t{7} % -2 == portable_i128_t{1});
}

TEST_CASE("portable subtraction reports signed overflow", "[wide-int]") {
    const auto maximum = portable_i128_t{static_cast<portable_u128_t>(-1) / 2};
    portable_i128_t result;
    REQUIRE_FALSE(wormedit::wide_int::sub_overflow(portable_i128_t{3}, portable_i128_t{5}, result));
    REQUIRE(result == portable_i128_t{-2});
    REQUIRE(wormedit::wide_int::sub_overflow(-maximum - 1, maximum, result));
    REQUIRE(wormedit::wide_int::sub_overflow(maximum, -maximum, result));
}

#ifdef __SIZEOF_INT128__
__extension__ using builtin_u128_t = unsigned __int128;

auto to_builtin(const portable_u128_t& value) -> builtin_u128_t {
    return (static_cast<builtin_u128_t>(value.high()) << 64) | value.low();
}

TEST_CASE("portable unsigned arithmetic matches __int128", "[wide-int]") {
    std::mt19937_64 random{12345};
    for (int round = 0; round < 20000; ++round) {
        // Vary magnitudes so 64-bit, mixed and full-width operands are all exercised.
        const auto make = [&] {
            const auto kind = random() % 3;
            portable_u128_t value = random();
            if (kind != 0) value = value * portable_u128_t{random()} + portable_u128_t{random()};
            if (kind == 2) value = value * portable_u128_t{random()};
            return value;
        };
        const auto a = make();
        auto b = make();
        if (b == 0) b = 1;
        const auto x = to_builtin(a);
        const auto y = to_builtin(b);
        REQUIRE(to_builtin(a + b) == x + y);
        REQUIRE(to_builtin(a - b) == x - y);
        REQUIRE(to_builtin(a * b) == x * y);
        REQUIRE(to_builtin(a / b) == x / y);
        REQUIRE(to_builtin(a % b) == x % y);
        REQUIRE((a < b) == (x < y));
        REQUIRE((a == b) == (x == y));
    }
}
#endif

}  // namespace

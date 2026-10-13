#include <wormedit/core/rational.h>
#include <wormedit/core/numeric_text.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>

using namespace wormedit;

TEST_CASE("Exact decimal text accepts signed scientific notation without rounding",
          "[unit][core][rational][numeric-text]") {
    const std::pair<std::string_view, signed_rational_t> cases[] = {
        {"1.00001E+07", {10'000'100, 1}}, {"25e-1", {5, 2}},
        {"-1.25E-1", {-1, 8}}, {"+.5e+1", {5, 1}}, {"1.e2", {100, 1}},
        {"0e2147483647", {0, 1}}, {"2e-19", {1, 5'000'000'000'000'000'000ULL}},
        {"92233720368547758070e-1", {INT64_MAX, 1}},
        {"-9223372036854775808e0", {INT64_MIN, 1}}, {"0.000", {0, 1}},
    };
    for (const auto& [text, expected] : cases) {
        INFO(text);
        CHECK(parse_exact_decimal(text) == expected);
    }
}

TEST_CASE("Exact decimal text rejects malformed exponents and unrepresentable values",
          "[unit][core][rational][numeric-text]") {
    for (const auto text : {"", ".", "e2", "1e", "1e+", "1e+-2", "1e--2", "1e++2",
                            "1e2e3", "1.2.0", "1/2", "NaN", "inf", " 1", "1 ",
                            "9223372036854775808", "-9223372036854775809",
                            "1e-20", "1e2147483647", "1e-2147483648", "1e2147483648"}) {
        INFO(text);
        CHECK_FALSE(parse_exact_decimal(text));
    }
}

TEST_CASE("IEEE-754 rationalization distinguishes exact and approximate values",
          "[unit][core][rational]") {
    const auto exact = rationalize_positive_double(0.5);
    REQUIRE(exact);
    CHECK(exact.status == rationalization_status_t::exact);
    CHECK((exact.value == rational_t{1, 2}));
    CHECK(exact.absolute_error_bound == 0.0L);
    CHECK(exact.iterations == 0);

    const auto approximate =
        rationalize_positive_double(0.1, {.maximum_denominator = 100, .iteration_limit = 64});
    REQUIRE(approximate);
    CHECK(approximate.status == rationalization_status_t::approximation);
    CHECK((approximate.value == rational_t{1, 10}));
    CHECK(approximate.absolute_error_bound > 0.0L);
}

TEST_CASE("IEEE-754 rationalization obeys denominator and iteration bounds",
          "[unit][core][rational]") {
    const auto denominator_bounded = rationalize_positive_double(
        3.141592653589793, {.maximum_denominator = 1, .iteration_limit = 64});
    REQUIRE(denominator_bounded);
    CHECK(denominator_bounded.status == rationalization_status_t::approximation);
    CHECK(denominator_bounded.value->denominator == 1);
    CHECK(denominator_bounded.iterations <= 64);

    const auto iteration_bounded = rationalize_positive_double(
        3.141592653589793, {.maximum_denominator = 1'000'000, .iteration_limit = 1});
    REQUIRE(iteration_bounded);
    CHECK(iteration_bounded.status == rationalization_status_t::approximation);
    CHECK(iteration_bounded.value->denominator == 1);
    CHECK(iteration_bounded.iterations == 1);

    const auto below_one_single_iteration =
        rationalize_positive_double(0.9, {.maximum_denominator = 3, .iteration_limit = 1});
    REQUIRE(below_one_single_iteration);
    CHECK((below_one_single_iteration.value == rational_t{1, 1}));

    const auto small_positive =
        rationalize_positive_double(std::numeric_limits<double>::denorm_min(),
                                    {.maximum_denominator = 100, .iteration_limit = 64});
    REQUIRE(small_positive);
    CHECK(small_positive.value->numerator > 0);
    CHECK(small_positive.value->denominator == 100);
}

TEST_CASE("IEEE-754 rationalization rejects invalid values and excessive error",
          "[unit][core][rational][negative]") {
    CHECK(rationalize_positive_double(0.0).error == rationalization_error_t::non_positive);
    CHECK(rationalize_positive_double(-0.25).error == rationalization_error_t::non_positive);
    CHECK(rationalize_positive_double(std::numeric_limits<double>::infinity()).error ==
          rationalization_error_t::non_finite);
    CHECK(rationalize_positive_double(std::numeric_limits<double>::quiet_NaN()).error ==
          rationalization_error_t::non_finite);
    CHECK(
        rationalize_positive_double(0.5, {.maximum_denominator = 0, .iteration_limit = 64}).error ==
        rationalization_error_t::invalid_limits);

    const auto rejected = rationalize_positive_double(
        0.1,
        {.maximum_denominator = 100, .iteration_limit = 64, .maximum_absolute_error = 1.0e-30L});
    CHECK_FALSE(rejected);
    CHECK(rejected.status == rationalization_status_t::error);
    CHECK(rejected.error == rationalization_error_t::approximation_exceeds_error_limit);
    CHECK(rejected.absolute_error_bound > 1.0e-30L);
}

TEST_CASE("snap filtering preserves configured order and exact fallback", "[unit][core][snap]") {
    constexpr std::array<std::uint64_t, 18> configured{4,  8,   6,   12,  16,  24,  32, 48, 64,
                                                       96, 192, 240, 320, 480, 960, 7,  0,  8};
    const auto state = filter_snap_denominators(960, configured, 6);

    REQUIRE(state);
    CHECK(state.active_denominator == 6);
    CHECK_FALSE(state.active_was_fallback);
    CHECK_FALSE(state.inserted_resolution_fallback);
    CHECK(
        (state.supported_denominators ==
         std::vector<std::uint64_t>{4, 8, 6, 12, 16, 24, 32, 48, 64, 96, 192, 240, 320, 480, 960}));

    constexpr std::array<std::uint64_t, 4> coarse_snaps{4, 8, 6, 12};
    const auto omitted_fallback = filter_snap_denominators(192, coarse_snaps, 5);
    REQUIRE(omitted_fallback);
    CHECK(omitted_fallback.active_denominator == 4);
    CHECK(omitted_fallback.active_was_fallback);
    CHECK_FALSE(omitted_fallback.inserted_resolution_fallback);
    CHECK(omitted_fallback.supported_denominators.front() == 4);

    constexpr std::array<std::uint64_t, 2> unsupported_snaps{5, 7};
    const auto exact_only_fallback = filter_snap_denominators(192, unsupported_snaps, 5);
    REQUIRE(exact_only_fallback);
    CHECK(exact_only_fallback.active_denominator == 192);
    CHECK(exact_only_fallback.active_was_fallback);
    CHECK(exact_only_fallback.inserted_resolution_fallback);
    CHECK((exact_only_fallback.supported_denominators == std::vector<std::uint64_t>{192}));
}

TEST_CASE("snap filtering rejects zero resolution", "[unit][core][snap][negative]") {
    const auto state = filter_snap_denominators(0, std::span<const std::uint64_t>{}, 1);
    CHECK_FALSE(state);
    CHECK(state.error == snap_state_error_t::zero_resolution);
}

#include "../../src/core/text_util.h"
#include "../../src/core/timing_arithmetic.h"
#include <wormedit/core/bms36.h>

namespace {
void check_rational_conversions() {
    CHECK(to_long_double(rational_t{1, 0}) == 0);
    CHECK(to_double(signed_rational_t{0, 0}) == 0);
    CHECK(to_double(signed_rational_t{-3, 2}) == -1.5);
}
void check_row_clamp_policies() {
    CHECK(nearest_row(-2, 10, row_clamp_policy_t::both_ends) == 0);
    CHECK_FALSE(nearest_row(-2, 10, row_clamp_policy_t::upper_only));
    CHECK(nearest_row(12, 10, row_clamp_policy_t::upper_only) == 9);
    CHECK(nearest_row(4.5L, 10, row_clamp_policy_t::upper_only) == 5);
}
void check_explicit_trim_policy() {
    CHECK(text_util::trim("\v\f value\r\n", text_util::ascii_whitespace) == "value");
    CHECK(text_util::trim("\v value\t", " \t") == "\v value");
}
void check_bounded_utf8_validation() {
    CHECK(text_util::valid_bounded_text("é", 2));
    CHECK_FALSE(text_util::valid_bounded_text("é", 1));
    CHECK_FALSE(text_util::valid_bounded_text(std::string_view{"a\0b", 3}, 4));
    CHECK_FALSE(text_util::valid_bounded_text("\xc0\x80", 4));
}
void check_utf8_truncation_and_base36_policy() {
    CHECK(text_util::truncate_utf8("aéz", 2) == "a");
    CHECK(text_util::truncate_utf8("aéz", 3) == "aé");
    CHECK(bms36_value('a') == 10);
    CHECK(bms36_value('a', bms36_case_policy_t::uppercase_only) == -1);
}
} // namespace

TEST_CASE("shared helpers guard invalid rationals and preserve row clamp policies", "[unit][core][helpers]") {
    check_rational_conversions();
    check_row_clamp_policies();
}

TEST_CASE("exact subtraction reduces wide intermediates before rejecting overflow", "[unit][core][helpers]") {
    const auto value = timing_arithmetic::subtract_exact({INT64_MAX, 2}, {INT64_MAX, 3});
    REQUIRE(value);
    CHECK((*value == signed_rational_t{INT64_MAX, 6}));
    CHECK_FALSE(timing_arithmetic::subtract_exact({INT64_MIN, 1}, {1, 1}));
    CHECK_FALSE(timing_arithmetic::subtract_exact({1, 0}, {1, 1}));
}

TEST_CASE("text helpers use explicit whitespace and validate bounded UTF-8", "[unit][core][helpers]") {
    check_explicit_trim_policy();
    check_bounded_utf8_validation();
    check_utf8_truncation_and_base36_policy();
}

TEST_CASE("decimal exponent policy accepts careful reduction within the caller limit", "[unit][core][helpers]") {
    CHECK_FALSE(parse_exact_decimal("0e19", 18));
    CHECK(parse_exact_decimal("0e18", 18));
    CHECK((parse_exact_decimal("92233720368547758070e-1", 18) == signed_rational_t{INT64_MAX, 1}));
    CHECK((parse_exact_decimal("-9223372036854775808", 18) == signed_rational_t{INT64_MIN, 1}));
}

TEST_CASE("wide arithmetic and row fractions reduce without intermediate overflow", "[unit][core][helpers]") {
    const auto maximum = UINT64_MAX;
    CHECK((timing_arithmetic::rows_to_beat_fraction(maximum, maximum) == signed_rational_t{4, 1}));
    CHECK_FALSE(timing_arithmetic::rows_to_beat_fraction(1, 0));
    CHECK_FALSE(timing_arithmetic::rows_to_beat_fraction(1, 1, 0));
    CHECK((checked_math::gcd(wide_math::u128_t{2, 0}, wide_math::u128_t{3, 0}) == wide_math::u128_t{1, 0}));
    CHECK_FALSE(checked_math::checked_multiply(wide_math::u128_t{maximum, maximum}, 2));
}

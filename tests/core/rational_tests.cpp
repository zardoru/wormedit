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

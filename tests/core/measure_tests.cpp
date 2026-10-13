#include <wormedit/core/measure.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <vector>

using namespace wormedit;

TEST_CASE("measure length text is parsed exactly", "[unit][core][measure]")
{
    CHECK((parse_measure_length("1").value == rational_t{1, 1}));
    CHECK((parse_measure_length("0.75").value == rational_t{3, 4}));
    CHECK((parse_measure_length(".7500").value == rational_t{3, 4}));
    CHECK((parse_measure_length(" 6 / 8 ").value == rational_t{3, 4}));
    CHECK((parse_measure_length("1.25").value == rational_t{5, 4}));
}

TEST_CASE("invalid measure length text has a typed failure", "[unit][core][measure][negative]")
{
    CHECK(parse_measure_length("").error == measure_error_t::empty);
    CHECK(parse_measure_length("0").error == measure_error_t::non_positive);
    CHECK(parse_measure_length("-0.75").error == measure_error_t::non_positive);
    CHECK(parse_measure_length("3/0").error == measure_error_t::zero_denominator);
    CHECK(parse_measure_length("3/4/5").error == measure_error_t::invalid_syntax);
    CHECK(parse_measure_length("0.7x").error == measure_error_t::invalid_syntax);
    CHECK(parse_measure_length("18446744073709551616").error == measure_error_t::overflow);
    CHECK(parse_measure_length("0.00000000000000000001").error == measure_error_t::overflow);
}

TEST_CASE("measure capacity is exact and authored meter spelling survives", "[unit][core][measure]")
{
    const measure_definition_t compound{
        .length = {3, 4},
        .meter = meter_t{6, 8},
    };
    const auto result = validate_measure(default_divisions_per_standard_measure, compound);

    REQUIRE(result);
    CHECK(result.row_capacity == 720);
    CHECK((compound.meter == meter_t{6, 8}));
}

TEST_CASE("measure validation rejects incompatible geometry", "[unit][core][measure][negative]")
{
    CHECK(validate_measure(0, {{1, 1}, std::nullopt}).error
        == measure_error_t::zero_resolution);
    CHECK(validate_measure(960, {{1, 7}, std::nullopt}).error
        == measure_error_t::non_integral_capacity);
    CHECK(validate_measure(960, {{3, 4}, meter_t{4, 4}}).error
        == measure_error_t::meter_mismatch);
    CHECK(validate_measure(960, {{1, 0}, std::nullopt}).error
        == measure_error_t::zero_denominator);
    CHECK(validate_measure(
              std::numeric_limits<std::uint64_t>::max(),
              {{2, 1}, std::nullopt})
              .error
        == measure_error_t::overflow);
}

TEST_CASE("default BMSON grid preserves editor rows exactly", "[unit][core][bmson]")
{
    const auto grid_960 = bmson_grid_t::from(960);
    REQUIRE(grid_960);
    CHECK(grid_960->resolution == 240);
    CHECK(grid_960->pulses_per_editor_row == 1);
    CHECK(bmson_pulse_for_row(*grid_960, 721) == 721);

    const auto grid_192 = bmson_grid_t::from(192);
    REQUIRE(grid_192);
    CHECK(grid_192->resolution == 48);
    CHECK(grid_192->pulses_per_editor_row == 1);

    const auto grid_10 = bmson_grid_t::from(10);
    REQUIRE(grid_10);
    CHECK(grid_10->resolution == 5);
    CHECK(grid_10->pulses_per_editor_row == 2);
    CHECK(bmson_pulse_for_row(*grid_10, 7) == 14);
}

TEST_CASE("BMSON mapping rejects invalid grids and overflow", "[unit][core][bmson][negative]")
{
    CHECK_FALSE(bmson_grid_t::from(0));
    CHECK_FALSE(bmson_pulse_for_row({}, 1));
    CHECK_FALSE(bmson_pulse_for_row(
        {.resolution = 1, .pulses_per_editor_row = 2},
        std::numeric_limits<std::uint64_t>::max()));
}

TEST_CASE("suggest_meters lists denominators 4, 8 and 16 that fit exactly", "[unit][core][measure]")
{
    using meters_t = std::vector<meter_t>;
    CHECK(suggest_meters({1, 1}) == meters_t{{4, 4}, {8, 8}, {16, 16}});
    CHECK(suggest_meters({3, 4}) == meters_t{{3, 4}, {6, 8}, {12, 16}});
    CHECK(suggest_meters({5, 4}) == meters_t{{5, 4}, {10, 8}, {20, 16}});
    CHECK(suggest_meters({7, 8}) == meters_t{{7, 8}, {14, 16}});
    CHECK(suggest_meters({1, 3}).empty());
}

TEST_CASE("suggest_meters normalizes input and rejects degenerate lengths", "[unit][core][measure][negative]")
{
    using meters_t = std::vector<meter_t>;
    CHECK(suggest_meters({6, 8}) == meters_t{{3, 4}, {6, 8}, {12, 16}});
    CHECK(suggest_meters({1, 16}) == meters_t{{1, 16}});
    CHECK(suggest_meters({1, 32}).empty());
    CHECK(suggest_meters({0, 1}).empty());
    CHECK(suggest_meters({1, 0}).empty());
    CHECK(suggest_meters({std::numeric_limits<std::uint64_t>::max(), 1}).empty());
}

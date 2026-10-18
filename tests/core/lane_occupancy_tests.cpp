#include "../../src/core/lane_occupancy.h"
#include <array>

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;

TEST_CASE("lane occupancy uses closed tap and hold ranges", "[core][lane-occupancy]") {
    const auto occupied = lane_occupancy_t::from({
        {.lane = lane_occupancy_key_t::from(lane_id_t{1}), .begin = 100, .end = 300, .note = object_id_t{1}},
        {.lane = lane_occupancy_key_t::from(lane_id_t{2}), .begin = 200, .end = 200, .note = object_id_t{2}},
    });
    CHECK(occupied.collides(lane_id_t{1}, 100, 100));
    CHECK(occupied.collides(lane_id_t{1}, 200, 200));
    CHECK(occupied.collides(lane_id_t{1}, 300, 300));
    CHECK_FALSE(occupied.collides(lane_id_t{1}, 301, 301));
    CHECK_FALSE(occupied.collides(lane_id_t{3}, 200, 200));
    CHECK(occupied.collides(lane_id_t{1}, 50, 150));
}

TEST_CASE("lane occupancy isolates BGM numeric lane aliases", "[core][lane-occupancy]") {
    const auto occupied = lane_occupancy_t::from({
        {.lane = lane_occupancy_key_t::from(object_id_t{1}), .begin = 100, .end = 100, .note = object_id_t{1}},
    });
    CHECK(occupied.collides_bgm(object_id_t{1}, 100));
    CHECK_FALSE(occupied.collides(lane_id_t{1}, 100, 100));
    CHECK_FALSE(occupied.collides_bgm(object_id_t{2}, 100));
    CHECK_FALSE(lane_occupancy_t::from({}).collides(lane_id_t{1}, 0, 0));
}

TEST_CASE("lane occupancy pair policy ignores only existing-existing overlaps", "[core][lane-occupancy]") {
    const std::vector<lane_occupant_t> existing{
        {.lane = lane_occupancy_key_t::from(lane_id_t{1}), .begin = 100, .end = 300, .note = object_id_t{1}},
        {.lane = lane_occupancy_key_t::from(lane_id_t{1}), .begin = 200, .end = 400, .note = object_id_t{2}},
    };
    const auto occupied = lane_occupancy_t::from(existing);
    CHECK(occupied.first_collision(lane_occupancy_pairs_t::all) != nullptr);
    CHECK(occupied.first_collision(lane_occupancy_pairs_t::proposed) == nullptr);
    auto proposed = existing;
    proposed.push_back({.lane = lane_occupancy_key_t::from(lane_id_t{1}), .begin = 400, .end = 500,
        .note = object_id_t{3}, .proposed = true});
    const auto mixed = lane_occupancy_t::from(std::move(proposed));
    REQUIRE(mixed.first_collision(lane_occupancy_pairs_t::proposed) != nullptr);
    CHECK(mixed.first_collision(lane_occupancy_pairs_t::proposed)->note == object_id_t{3});
}

TEST_CASE("lane occupancy returns first input head representative", "[core][lane-occupancy]") {
    const auto occupied = lane_occupancy_t::from({
        {.lane = lane_occupancy_key_t::from(lane_id_t{1}), .begin = 0, .end = 0, .note = object_id_t{30}},
        {.lane = lane_occupancy_key_t::from(lane_id_t{1}), .begin = 0, .end = 0, .note = object_id_t{10}},
        {.lane = lane_occupancy_key_t::from(lane_id_t{1}), .begin = 0, .end = 0, .note = object_id_t{20}},
    });
    REQUIRE(occupied.first_at_head(lane_id_t{1}, 0) != nullptr);
    CHECK(occupied.first_at_head(lane_id_t{1}, 0)->note == object_id_t{30});
    CHECK(occupied.find(object_id_t{10})->ordinal == 1);
    CHECK(occupied.first_at_head(lane_id_t{2}, 0) == nullptr);
}

TEST_CASE("lane occupancy retains exact rows above double integer precision", "[core][lane-occupancy]") {
    constexpr auto row = (std::uint64_t{1} << 54) + 1;
    const auto occupied = lane_occupancy_t::from({
        {.lane = lane_occupancy_key_t::from(lane_id_t{1}), .begin = row, .end = row + 2, .note = object_id_t{1}},
    });
    CHECK_FALSE(occupied.collides(lane_id_t{1}, row - 1, row - 1));
    CHECK(occupied.collides(lane_id_t{1}, row, row));
    CHECK(occupied.collides(lane_id_t{1}, row + 2, row + 2));
    CHECK_FALSE(occupied.collides(lane_id_t{1}, row + 3, row + 3));
}

TEST_CASE("lane occupancy row coverage includes endpoints and empty inverted ranges", "[core][lane-occupancy]") {
    const std::array<std::uint64_t, 5> rows{99, 100, 200, 300, 301};
    const lane_occupant_t hold{.begin = 100, .end = 300};
    const auto covered = hold.covered_rows(rows);
    REQUIRE(covered.size() == 3);
    CHECK(covered.front() == 100);
    CHECK(covered.back() == 300);
    CHECK(lane_occupant_t{.begin = 300, .end = 100}.covered_rows(rows).empty());
}

TEST_CASE("lane occupancy excludes selected note IDs during one indexed build", "[core][lane-occupancy]") {
    chart_document_t chart{.id = chart_id_t{1}, .measures = {{.id = measure_id_t{1}, .notes = {
        {.id = object_id_t{9}, .position = {measure_id_t{1}, 100}, .lane = lane_id_t{1}},
    }}}};
    const auto index = measure_index_t::from(chart);
    REQUIRE(index.has_value());
    const std::unordered_set<std::uint64_t> excluded{9};
    const auto original = lane_occupancy_t::from(chart, *index);
    const auto selected = lane_occupancy_t::from(chart, *index, &excluded);
    CHECK(original.collides(lane_id_t{1}, 100, 100));
    CHECK_FALSE(selected.collides(lane_id_t{1}, 100, 100));
    CHECK(selected.entries().empty());
}

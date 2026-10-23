#include "sys/resources/bga_usage_view.h"
#include "sys/resources/resource_usage_collector.h"
#include "../support/bga_usage_fixture.h"
#include <catch2/catch_test_macros.hpp>
using namespace wormedit;
using namespace wormedit::sys;

TEST_CASE("BGA uses order every event lane across charts and shared assets", "[sys][bga-usage]") {
    const auto view = bga_usage_view_t::from(test::bga_usage_song(), resource_id_t{20});
    REQUIRE(view.rows.size() == 5);
    CHECK(view.rows[0].target.event == object_id_t{33});
    CHECK(view.rows[1].target.event == object_id_t{32});
    CHECK(view.rows[2].target.kind == visual_event_kind_t::layer2);
    CHECK(view.rows[3].target.kind == visual_event_kind_t::poor);
    CHECK(view.rows[4].target.chart == chart_id_t{8});
}
TEST_CASE("BGA uses include video and retain missing catalog entries", "[sys][bga-usage]") {
    auto song = test::bga_usage_song();
    song.resources.front().missing = true;
    const auto missing = bga_usage_view_t::from(song, resource_id_t{20});
    const auto video = bga_usage_view_t::from(song, resource_id_t{22});
    CHECK(missing.valid);
    CHECK(missing.total_matching == 5);
    REQUIRE(video.rows.size() == 1);
    CHECK(video.rows.front().target.kind == visual_event_kind_t::video);
    CHECK(bga_usage_view_t::from(song, resource_id_t{23}).rows.empty());
    CHECK_FALSE(bga_usage_view_t::from(song, resource_id_t{999}).valid);
}
TEST_CASE("BGA projection bounds earliest uses independently of authored order", "[sys][bga-usage]") {
    auto song = test::bga_usage_song();
    auto& events = song.charts.front().bga_events;
    events.clear();
    for (std::size_t i = resource_usage_projection_limit + 2; i > 0; --i)
        events.push_back({.id = object_id_t{100 + i}, .position = {measure_id_t{1}, i}, .resource = resource_id_t{20}});
    const auto view = bga_usage_view_t::from(song, resource_id_t{20});
    CHECK(view.rows.size() == resource_usage_projection_limit);
    CHECK(view.total_matching == resource_usage_projection_limit + 3);
    CHECK(view.truncated);
    CHECK(view.rows.front().target.position.row == 1);
    CHECK(view.rows.back().target.position.row == resource_usage_projection_limit);
}

#include "sys/bookmark_navigation.h"
#include <wormedit/core/timing.h>

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
auto chooser_document() -> song_document_t {
    chart_document_t chart{
        .id = chart_id_t{2},
        .divisions_per_standard_measure = 960,
        .measures = {
            {.id = measure_id_t{10}, .definition = {{1, 1}, meter_t{4, 4}}},
            {.id = measure_id_t{11}, .definition = {{1, 1}, meter_t{4, 4}}},
            {.id = measure_id_t{12}, .definition = {{1, 1}, meter_t{4, 4}}},
        },
        .bpms = {{.id = object_id_t{50},
                  .position = {measure_id_t{10}, 0},
                  .beats_per_minute = {120, 1}}},
    };
    chart.bookmarks = {
        {.id = object_id_t{101}, .label = "same", .anchor = marker_anchor_t::beat,
         .position = local_position_t{measure_id_t{10}, 240}},
        {.id = object_id_t{102}, .label = "same", .anchor = marker_anchor_t::audio_time,
         .audio_milliseconds = 500},
        {.id = object_id_t{103}, .label = "", .anchor = marker_anchor_t::beat,
         .position = local_position_t{measure_id_t{10}, 720}},
    };
    return {.id = song_id_t{1}, .revision = 7, .charts = {std::move(chart)}};
}
void check_projection_header(const bookmark_chooser_view_t& projection) {
    CHECK(projection.document_revision == 7);
    CHECK(projection.total_count == 3);
    REQUIRE(projection.rows.size() == 3);
}
void check_first_beat_row(const bookmark_chooser_view_t& projection) {
    CHECK(projection.rows[0].bookmark_id == object_id_t{101});
    CHECK(projection.rows[0].position_label == "Measure 001 · row 240");
    CHECK(projection.rows[0].name == "same");
}
void check_second_audio_row(const bookmark_chooser_view_t& projection) {
    CHECK(projection.rows[1].bookmark_id == object_id_t{102});
    CHECK(projection.rows[1].anchor == marker_anchor_t::audio_time);
    CHECK(projection.rows[1].position_label == "Audio 00:00.500");
    CHECK(projection.rows[0].display_label != projection.rows[1].display_label);
}
void check_unnamed_row(const bookmark_chooser_view_t& projection) {
    CHECK(projection.rows[2].bookmark_id == object_id_t{103});
    CHECK(projection.rows[2].name == "(unnamed)");
    CHECK(projection.rows[2].display_label.find("#103") != std::string::npos);
}
void check_page_endpoints(const bookmark_chooser_view_t& page) {
    CHECK(page.total_count == 70);
    CHECK(page.first_row == 64);
    REQUIRE(page.rows.size() == 6);
    CHECK(page.rows.front().bookmark_id == object_id_t{264});
    CHECK(page.rows.back().bookmark_id == object_id_t{269});
}
} // namespace

TEST_CASE("bookmark chooser orders beat and audio anchors with disambiguated labels",
          "[unit][sys][bookmark-chooser]") {
    const auto document = chooser_document();
    const auto result = bookmark_chooser_view_t::from(document, chart_id_t{2}, 0, 16);
    REQUIRE(result.is_success());
    check_projection_header(result);
    check_first_beat_row(result);
    check_second_audio_row(result);
    check_unnamed_row(result);
}

TEST_CASE("bookmark chooser sorts the complete list before paging beyond sixty-four rows",
          "[unit][sys][bookmark-chooser]") {
    auto document = chooser_document();
    auto& bookmarks = document.charts.front().bookmarks;
    bookmarks.clear();
    for (std::uint64_t id = 200; id < 270; ++id) {
        bookmarks.push_back({.id = object_id_t{id}, .label = "bookmark",
            .anchor = marker_anchor_t::beat,
            .position = local_position_t{measure_id_t{10}, 0}});
    }
    const auto page = bookmark_chooser_view_t::from(document, chart_id_t{2}, 64, 8);
    REQUIRE(page.is_success());
    check_page_endpoints(page);
    const auto empty = bookmark_chooser_view_t::from(document, chart_id_t{2}, 70, 8);
    REQUIRE(empty.is_success());
    CHECK(empty.rows.empty());
    CHECK(empty.total_count == 70);
}

TEST_CASE("bookmark chooser rejects invalid paging and missing charts",
          "[unit][sys][bookmark-chooser]") {
    const auto document = chooser_document();
    CHECK_FALSE(bookmark_chooser_view_t::from(document, chart_id_t{2}, 0, 0).is_success());
    CHECK_FALSE(bookmark_chooser_view_t::from(document, chart_id_t{99}, 0, 8).is_success());
}

TEST_CASE("bookmark chooser searches every bookmark before paging with case insensitive labels",
          "[unit][sys][bookmark-chooser]") {
    auto document = chooser_document();
    auto& bookmarks = document.charts.front().bookmarks;
    for (std::uint64_t id = 200; id < 280; ++id)
        bookmarks.push_back({.id = object_id_t{id}, .label = id == 279 ? "Final Bridge 東京" : "other",
            .anchor = marker_anchor_t::beat,
            .position = local_position_t{measure_id_t{12}, id}});
    const auto bridge = bookmark_chooser_view_t::from(document, chart_id_t{2}, 0, 12, "BRIDGE");
    REQUIRE(bridge.rows.size() == 1);
    CHECK(bridge.total_count == 1);
    CHECK(bridge.rows.front().bookmark_id == object_id_t{279});
    const auto cjk = bookmark_chooser_view_t::from(document, chart_id_t{2}, 0, 12, "東京");
    REQUIRE(cjk.rows.size() == 1);
    CHECK(cjk.rows.front().bookmark_id == object_id_t{279});
    const auto position = bookmark_chooser_view_t::from(document, chart_id_t{2}, 0, 12, "Measure 001");
    CHECK(position.total_count == 2);
    const auto missing = bookmark_chooser_view_t::from(document, chart_id_t{2}, 0, 12, "not present");
    CHECK(missing.total_count == 0);
    CHECK(missing.rows.empty());
}

TEST_CASE("Bookmark chooser retains a supplied snapshot geometry failure", "[sys][bookmark-chooser]") {
    const auto document = chooser_document();
    chart_indices_t indices;
    indices.measures = std::unexpected(diagnostic_list_t{});
    const auto result = bookmark_chooser_view_t::from(document, chart_id_t{2}, 0, 16, {}, {.committed = &indices});
    REQUIRE_FALSE(result.is_success());
    CHECK(result.status == "Bookmark positions could not be resolved.");
    CHECK(result.rows.empty());
}

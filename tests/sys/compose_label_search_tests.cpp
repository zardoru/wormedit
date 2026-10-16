#include "sys/compose_label_search.h"

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

auto labeled_chart() -> chart_document_t {
    chart_document_t chart{
        .id = chart_id_t{1},
        .measures = {{.id = measure_id_t{1}}, {.id = measure_id_t{2}}},
    };
    const auto add = [&](std::size_t measure, std::uint64_t id, std::uint64_t row,
                         std::optional<std::string> label) {
        chart.measures[measure].notes.push_back({.id = object_id_t{id},
                                                 .position = {chart.measures[measure].id, row},
                                                 .lane = lane_id_t{1},
                                                 .kind = note_kind_t::tap,
                                                 .label = std::move(label)});
    };
    add(0, 10, 0, "Drop");
    add(0, 11, 480, "other");
    add(1, 12, 240, "DROP two");
    return chart;
}

} // namespace

TEST_CASE("label search first lands on the earliest match", "[sys][label-search]") {
    const auto chart = labeled_chart();
    const auto measures = measure_index_t::from(chart).value();
    const auto hit = find_label_search_hit(chart, measures, "drop", {measure_id_t{2}, 0},
                                           label_search_step_t::first);
    REQUIRE(hit);
    CHECK(hit->matches.size() == 2);
    CHECK(hit->target == local_position_t{measure_id_t{1}, 0});
}

TEST_CASE("label search steps cycle past the cursor", "[sys][label-search]") {
    const auto chart = labeled_chart();
    const auto measures = measure_index_t::from(chart).value();
    const auto next = find_label_search_hit(chart, measures, "drop", {measure_id_t{1}, 0},
                                            label_search_step_t::next);
    REQUIRE(next);
    CHECK(next->target == local_position_t{measure_id_t{2}, 240});
    const auto wrapped = find_label_search_hit(chart, measures, "drop", {measure_id_t{2}, 240},
                                               label_search_step_t::next);
    REQUIRE(wrapped);
    CHECK(wrapped->target == local_position_t{measure_id_t{1}, 0});
    const auto previous = find_label_search_hit(chart, measures, "drop", {measure_id_t{1}, 0},
                                                label_search_step_t::previous);
    REQUIRE(previous);
    CHECK(previous->target == local_position_t{measure_id_t{2}, 240});
}

TEST_CASE("label search without a match reports none", "[sys][label-search]") {
    const auto chart = labeled_chart();
    const auto measures = measure_index_t::from(chart).value();
    CHECK_FALSE(find_label_search_hit(chart, measures, "zzz", {measure_id_t{1}, 0},
                                      label_search_step_t::first));
}

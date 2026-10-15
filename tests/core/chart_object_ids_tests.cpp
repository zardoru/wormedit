#include "../../src/core/chart_object_ids.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace wormedit;

namespace {
auto chart_with_declaration(int domain, object_id_t id) -> chart_document_t {
    chart_document_t chart;
    switch (domain) {
    case 0: chart.bgm_source_lanes.push_back({.id = id}); break;
    case 1: chart.lane_layouts.push_back({.id = id}); break;
    case 2: chart.bpms.push_back({.id = id}); break;
    case 3: chart.pauses.push_back({.id = id}); break;
    case 4: chart.warps.push_back({.id = id}); break;
    case 5: chart.scrolls.push_back({.id = id}); break;
    case 6: chart.speeds.push_back({.id = id}); break;
    case 7: chart.kiai.push_back({.id = id}); break;
    case 8: chart.timing_anchors.push_back({.id = id}); break;
    case 9: chart.bookmarks.push_back({.id = id}); break;
    case 10: chart.sections.push_back({.id = id}); break;
    case 11: chart.bga_events.push_back({.id = id}); break;
    case 12: chart.sound_channels.push_back({.id = id}); break;
    case 13:
        chart.measures.resize(1);
        chart.measures.front().notes.push_back({.id = id});
        break;
    case 14:
        chart.measures.resize(1);
        chart.measures.front().bgm_events.push_back({.id = id});
        break;
    case 15:
        chart.sound_channels.push_back({.id = object_id_t{1}, .events = {{.id = id}}});
        break;
    }
    return chart;
}
} // namespace

TEST_CASE("Chart object ceiling includes every declaration domain", "[chart-object-ids]") {
    const auto domain = GENERATE(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const auto maximum = GENERATE(std::uint64_t{41}, std::numeric_limits<std::uint64_t>::max());
    const auto chart = chart_with_declaration(domain, object_id_t{maximum});
    const auto scan = object_id_ceiling_t::from(chart);
    CHECK(scan.maximum == maximum);
    const auto expected_count = domain == 15 ? 2 : 1;
    const auto has_expected_count = scan.count == expected_count &&
        chart_object_id_count(chart, object_id_t{maximum}) == 1;
    CHECK(has_expected_count);
    const auto next = next_chart_object_id(chart);
    CHECK_FALSE(next.over_limit);
    const auto is_exhausted = maximum == std::numeric_limits<std::uint64_t>::max();
    CHECK(next.exhausted == is_exhausted);
    const auto expected_next = is_exhausted ? std::nullopt : std::optional{object_id_t{maximum + 1}};
    CHECK(next.id == expected_next);
}

TEST_CASE("Chart object allocation preserves empty and capacity boundaries", "[chart-object-ids]") {
    chart_document_t chart;
    const auto empty = object_id_ceiling_t::from(chart);
    const auto is_empty = empty.count == 0 && empty.maximum == 0 &&
        next_chart_object_id(chart).id == object_id_t{1};
    CHECK(is_empty);
    CHECK(next_chart_object_id(chart, 0).over_limit);
    chart.bookmarks = {{.id = object_id_t{7}}, {.id = object_id_t{11}}};
    const auto is_at_capacity = next_chart_object_id(chart, 1).over_limit &&
        next_chart_object_id(chart, 2).over_limit;
    CHECK(is_at_capacity);
    CHECK(next_chart_object_id(chart, 3).id == object_id_t{12});
    const auto bounded = object_id_ceiling_t::from(chart, 1);
    const auto is_bounded = bounded.count == 1 && bounded.maximum == 7;
    CHECK(is_bounded);
}

TEST_CASE("Chart object ceiling excludes references and other ID domains", "[chart-object-ids]") {
    chart_document_t chart;
    chart.id = chart_id_t{999};
    chart.measures.push_back({.id = measure_id_t{999}});
    chart.semantic_layers.push_back({.id = layer_id_t{999}});
    chart.sv_recipes.push_back({.id = recipe_id_t{999}});
    chart.sound_channels.push_back({.id = object_id_t{4}, .source_clip = {.id = clip_id_t{999}}});
    chart.bgm_source_lanes.push_back({.id = object_id_t{7}, .stem_channel = object_id_t{999}});
    const auto scan = object_id_ceiling_t::from(chart);
    CHECK(scan.count == 2);
    CHECK(scan.maximum == 7);
    CHECK(next_chart_object_id(chart).id == object_id_t{8});
}

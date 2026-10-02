#include "sys/compose_quantize_preview.h"

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

auto chart_with_notes(std::initializer_list<std::uint64_t> rows) -> chart_document_t {
    chart_document_t chart{
        .id = chart_id_t{1},
        .measures = {{.id = measure_id_t{1}}},
        .lane_layouts = {{.id = object_id_t{1},
                          .position = {measure_id_t{1}, 0},
                          .layout = {.profile_id = "test",
                                     .lanes = {{.id = lane_id_t{1}, .name = "Lane 1"}}}}},
    };
    std::uint64_t id = 10;
    for (const auto row : rows)
        chart.measures[0].notes.push_back({.id = object_id_t{id++},
                                           .position = {measure_id_t{1}, row},
                                           .lane = lane_id_t{1},
                                           .kind = note_kind_t::tap});
    return chart;
}

} // namespace

TEST_CASE("quantize preview counts moved and unchanged notes", "[sys][quantize-preview]") {
    const auto chart = chart_with_notes({0, 250});
    const auto preview = preview_quantize_selection(chart, {object_id_t{10}, object_id_t{11}}, 4);
    CHECK(preview.selected == 2);
    CHECK(preview.moved == 1);
    CHECK(preview.unchanged == 1);
    CHECK(preview.blocked == 0);
}

TEST_CASE("quantize preview blocks on collisions", "[sys][quantize-preview]") {
    const auto chart = chart_with_notes({0, 100});
    const auto preview = preview_quantize_selection(chart, {object_id_t{10}, object_id_t{11}}, 4);
    CHECK(preview.blocked > 0);
    CHECK_FALSE(preview.message.empty());
}

TEST_CASE("quantize preview of an empty selection reports a hint", "[sys][quantize-preview]") {
    const auto preview = preview_quantize_selection(chart_with_notes({0}), {}, 4);
    CHECK(preview.selected == 0);
    CHECK_FALSE(preview.message.empty());
}

TEST_CASE("quantize preview cache recomputes when inputs change", "[sys][quantize-preview]") {
    const auto chart = chart_with_notes({0, 250});
    quantize_preview_cache_t cache;
    const std::vector<object_id_t> selection{object_id_t{11}};
    CHECK(cache.get(chart, 1, selection, 4).moved == 1);
    CHECK(cache.get(chart, 1, selection, 4).moved == 1);
    CHECK(cache.get(chart, 1, selection, 1920).moved == 0);
}

#include <wormedit/core/quantize.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>

using namespace wormedit;

namespace {

auto make_chart(rational_t first_length = {1, 1}, bool second_measure = false) -> chart_document_t {
    chart_document_t chart{
        .id = chart_id_t{1},
        .measures =
            {
                {
                    .id = measure_id_t{1},
                    .definition = {first_length, std::nullopt},
                },
            },
        .lane_layouts =
            {
                {
                    .id = object_id_t{1},
                    .position = {measure_id_t{1}, 0},
                    .layout =
                        {
                            .profile_id = "test",
                            .lanes =
                                {
                                    {.id = lane_id_t{1}, .name = "Lane 1"},
                                },
                        },
                },
            },
    };
    if (second_measure) {
        chart.measures.push_back({.id = measure_id_t{2}});
    }
    return chart;
}

auto add_note(chart_document_t& chart, object_id_t id, measure_id_t measure, std::uint64_t row,
              note_kind_t kind = note_kind_t::tap,
              std::optional<local_position_t> end = std::nullopt) -> void {
    const auto owner = std::ranges::find_if(
        chart.measures, [measure](const auto& candidate) { return candidate.id == measure; });
    REQUIRE(owner != chart.measures.end());
    owner->notes.push_back({
        .id = id,
        .position = {measure, row},
        .lane = lane_id_t{1},
        .kind = kind,
        .end = end,
    });
}

auto find_note(const chart_document_t& chart, object_id_t id) -> const note_t& {
    for (const auto& measure : chart.measures) {
        const auto note = std::ranges::find_if(
            measure.notes, [id](const auto& candidate) { return candidate.id == id; });
        if (note != measure.notes.end()) {
            return *note;
        }
    }
    FAIL("note is missing from quantized chart");
    std::abort();
}

auto has_code(const diagnostic_list_t& diagnostics, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

} // namespace

TEST_CASE("standard-measure quantization uses deterministic nearest rows",
          "[unit][core][quantize]") {
    auto chart = make_chart();
    add_note(chart, object_id_t{2}, measure_id_t{1}, 130);

    auto request = quantize_request_t{.divisions = 4};
    const auto nearest = quantize_chart_exact(chart, request);
    REQUIRE(nearest);
    CHECK(find_note(*nearest.chart, object_id_t{2}).position.row == 240);
    REQUIRE(nearest.quantized_moves.size() == 1);
    CHECK(nearest.quantized_moves[0].before.row == 130);
    CHECK(nearest.quantized_moves[0].after.row == 240);

    chart.measures[0].notes[0].position.row = 120;
    request.tie = quantization_tie_t::earlier;
    const auto earlier = quantize_chart_exact(chart, request);
    REQUIRE(earlier);
    CHECK(find_note(*earlier.chart, object_id_t{2}).position.row == 0);

    request.tie = quantization_tie_t::later;
    const auto later = quantize_chart_exact(chart, request);
    REQUIRE(later);
    CHECK(find_note(*later.chart, object_id_t{2}).position.row == 240);

    request.tie = quantization_tie_t::reject;
    const auto rejected = quantize_chart_exact(chart, request);
    CHECK_FALSE(rejected);
    CHECK(has_code(rejected.diagnostics, diagnostic_code_t::collision));
    CHECK(chart.measures[0].notes[0].position.row == 120);
}

TEST_CASE("authored-measure quantization uses the local exact capacity", "[unit][core][quantize]") {
    auto chart = make_chart({3, 4});
    add_note(chart, object_id_t{2}, measure_id_t{1}, 100);

    const auto result =
        quantize_chart_exact(chart, {
                                        .divisions = 4,
                                        .space = quantization_space_t::authored_measure,
                                    });
    REQUIRE(result);
    CHECK(find_note(*result.chart, object_id_t{2}).position.row == 180);

    auto large_exact_chart = make_chart({2, std::uint64_t{1} << 63U});
    large_exact_chart.divisions_per_standard_measure = std::uint64_t{1} << 63U;
    const auto large_exact = quantize_chart_exact(
        large_exact_chart, {.divisions = 2, .space = quantization_space_t::authored_measure});
    REQUIRE(large_exact);
    CHECK_FALSE(large_exact.promoted_to.has_value());
    CHECK(large_exact.chart->divisions_per_standard_measure == std::uint64_t{1} << 63U);
}

TEST_CASE("quantization promotes resolution only with explicit permission",
          "[unit][core][quantize]") {
    auto chart = make_chart();
    add_note(chart, object_id_t{2}, measure_id_t{1}, 100);

    const auto refused = quantize_chart_exact(chart, {.divisions = 7});
    CHECK_FALSE(refused);
    CHECK(has_code(refused.diagnostics, diagnostic_code_t::invalid_resolution));

    const auto promoted = quantize_chart_exact(chart, {
                                                          .divisions = 7,
                                                          .allow_resolution_promotion = true,
                                                      });
    REQUIRE(promoted);
    CHECK(promoted.promoted_from == 960);
    CHECK(promoted.promoted_to == 6720);
    CHECK(promoted.chart->divisions_per_standard_measure == 6720);
    CHECK(find_note(*promoted.chart, object_id_t{2}).position.row == 960);
    REQUIRE(promoted.resolution_moves.size() == 1);
    REQUIRE(promoted.quantized_moves.size() == 1);
    CHECK(chart.divisions_per_standard_measure == 960);
    CHECK(chart.measures[0].notes[0].position.row == 100);
}

TEST_CASE("cross-measure quantization rehomes sparse measure objects", "[unit][core][quantize]") {
    auto chart = make_chart({1, 1}, true);
    add_note(chart, object_id_t{2}, measure_id_t{1}, 900);

    const auto result = quantize_chart_exact(chart, {.divisions = 1});
    REQUIRE(result);
    CHECK(result.chart->measures[0].notes.empty());
    REQUIRE(result.chart->measures[1].notes.size() == 1);
    CHECK(result.chart->measures[1].notes[0].position.measure == measure_id_t{2});
    CHECK(result.chart->measures[1].notes[0].position.row == 0);
}

TEST_CASE("quantization collision and hold policies reject atomically",
          "[unit][core][quantize][negative]") {
    auto collision_chart = make_chart();
    add_note(collision_chart, object_id_t{2}, measure_id_t{1}, 50);
    add_note(collision_chart, object_id_t{3}, measure_id_t{1}, 100);

    const auto collision = quantize_chart_exact(collision_chart, {.divisions = 4});
    CHECK_FALSE(collision);
    CHECK(has_code(collision.diagnostics, diagnostic_code_t::collision));
    CHECK(collision.quantized_moves.size() == 2);
    CHECK(collision_chart.measures[0].notes[0].position.row == 50);

    const auto allowed =
        quantize_chart_exact(collision_chart, {
                                                  .divisions = 4,
                                                  .collisions = quantization_collision_t::allow,
                                              });
    REQUIRE(allowed);
    CHECK(allowed.chart->measures[0].notes[0].position.row == 0);
    CHECK(allowed.chart->measures[0].notes[1].position.row == 0);

    auto hold_chart = make_chart();
    add_note(hold_chart, object_id_t{2}, measure_id_t{1}, 130, note_kind_t::hold,
             local_position_t{measure_id_t{1}, 200});
    const auto reversed =
        quantize_chart_exact(hold_chart, {
                                             .divisions = 4,
                                             .holds = quantization_hold_t::start_only,
                                         });
    CHECK_FALSE(reversed);
    CHECK(has_code(reversed.diagnostics, diagnostic_code_t::invalid_hold));
    CHECK(hold_chart.measures[0].notes[0].position.row == 130);
}

TEST_CASE("quantization selection is explicit and validated", "[unit][core][quantize]") {
    auto chart = make_chart();
    add_note(chart, object_id_t{2}, measure_id_t{1}, 130);
    add_note(chart, object_id_t{3}, measure_id_t{1}, 131);

    const auto selected = quantize_chart_exact(chart, {
                                                          .divisions = 4,
                                                          .objects = {object_id_t{2}},
                                                      });
    REQUIRE(selected);
    CHECK(find_note(*selected.chart, object_id_t{2}).position.row == 240);
    CHECK(find_note(*selected.chart, object_id_t{3}).position.row == 131);

    const auto missing = quantize_chart_exact(chart, {
                                                         .divisions = 4,
                                                         .objects = {object_id_t{99}},
                                                     });
    CHECK_FALSE(missing);
    CHECK(has_code(missing.diagnostics, diagnostic_code_t::invalid_id));
}

TEST_CASE("quantization preserves unambiguous active lane layouts",
          "[unit][core][quantize][negative]") {
    auto chart = make_chart();
    chart.lane_layouts.push_back({
        .id = object_id_t{4},
        .position = {measure_id_t{1}, 120},
        .layout =
            {
                .profile_id = "later",
                .lanes = {{.id = lane_id_t{2}, .name = "Lane 2"}},
            },
    });

    const auto collapsed_layouts = quantize_chart_exact(chart, {.divisions = 4});
    CHECK_FALSE(collapsed_layouts);
    CHECK(has_code(collapsed_layouts.diagnostics, diagnostic_code_t::collision));

    chart.lane_layouts[1].position.row = 170;
    add_note(chart, object_id_t{2}, measure_id_t{1}, 130);
    const auto crossed_boundary = quantize_chart_exact(chart, {
                                                                  .divisions = 4,
                                                                  .objects = {object_id_t{2}},
                                                              });
    CHECK_FALSE(crossed_boundary);
    CHECK(has_code(crossed_boundary.diagnostics, diagnostic_code_t::unknown_lane));
    CHECK(chart.measures[0].notes[0].position.row == 130);
}

TEST_CASE("quantize crosses exact fractional measure boundaries with unordered IDs",
          "[unit][core][quantize][geometry]") {
    auto chart = make_chart({3, 4}, true);
    chart.measures[1].id = measure_id_t{9};
    chart.measures.push_back({.id = measure_id_t{3}, .definition = {{1, 2}, std::nullopt}});
    add_note(chart, object_id_t{20}, measure_id_t{1}, 710);
    add_note(chart, object_id_t{21}, measure_id_t{9}, 950);
    const auto result = quantize_chart_exact(chart, {.divisions = 4});
    REQUIRE(result);
    const local_position_t first_boundary{measure_id_t{9}, 0};
    const local_position_t second_boundary{measure_id_t{3}, 0};
    CHECK(find_note(*result.chart, object_id_t{20}).position == first_boundary);
    CHECK(find_note(*result.chart, object_id_t{21}).position == second_boundary);
    CHECK(chart.measures[0].notes[0].position.row == 710);
    CHECK(chart.measures[1].notes[0].position.row == 950);
}

TEST_CASE("quantize rejects unknown and capacity positions without geometry repair",
          "[unit][core][quantize][geometry][negative]") {
    auto chart = make_chart({3, 4}, true);
    add_note(chart, object_id_t{20}, measure_id_t{1}, 720);
    const auto at_capacity = quantize_chart_exact(chart, {.divisions = 4});
    CHECK_FALSE(at_capacity);
    CHECK(has_code(at_capacity.diagnostics, diagnostic_code_t::row_out_of_range));
    chart.measures[0].notes[0].position = {measure_id_t{99}, 0};
    const auto unknown = quantize_chart_exact(chart, {.divisions = 4});
    CHECK_FALSE(unknown);
    CHECK(has_code(unknown.diagnostics, diagnostic_code_t::row_out_of_range));
}

TEST_CASE("Typed quantize isolates numeric aliases and quantizes BGM endpoints",
          "[unit][core][quantize][mixed-quantize]") {
    auto chart = make_chart();
    add_note(chart, object_id_t{20}, measure_id_t{1}, 250);
    chart.bpms.push_back({.id = object_id_t{20}, .position = {measure_id_t{1}, 125}, .beats_per_minute = {120, 1}});
    chart.bgm_source_lanes.push_back({.id = object_id_t{30}});
    chart.measures.front().bgm_events.push_back({.id = object_id_t{20},
        .position = {measure_id_t{1}, 130}, .source_lane = object_id_t{30},
        .end = local_position_t{measure_id_t{1}, 610}});
    const auto result = quantize_chart_exact(chart, {.divisions = 4,
        .selection = quantize_selection_t{{object_id_t{20}}, {object_id_t{20}}}});
    REQUIRE(result);
    CHECK(find_note(*result.chart, object_id_t{20}).position.row == 240);
    CHECK(result.chart->measures.front().bgm_events.front().position.row == 240);
    CHECK(result.chart->measures.front().bgm_events.front().end->row == 720);
    CHECK(result.chart->bpms.front().position.row == 125);
    CHECK(result.quantized_moves.size() == 3);
}

TEST_CASE("Typed quantize rejects a missing BGM or collapsed tail without partial notes",
          "[unit][core][quantize][mixed-quantize]") {
    auto chart = make_chart();
    add_note(chart, object_id_t{20}, measure_id_t{1}, 250);
    chart.bgm_source_lanes.push_back({.id = object_id_t{30}});
    chart.measures.front().bgm_events.push_back({.id = object_id_t{21},
        .position = {measure_id_t{1}, 130}, .source_lane = object_id_t{30},
        .end = local_position_t{measure_id_t{1}, 140}});
    const auto collapsed = quantize_chart_exact(chart, {.divisions = 4,
        .selection = quantize_selection_t{{object_id_t{20}}, {object_id_t{21}}}});
    CHECK_FALSE(collapsed);
    CHECK(has_code(collapsed.diagnostics, diagnostic_code_t::invalid_hold));
    const auto missing = quantize_chart_exact(chart, {.divisions = 4,
        .selection = quantize_selection_t{{object_id_t{20}}, {object_id_t{99}}}});
    CHECK_FALSE(missing);
    CHECK(has_code(missing.diagnostics, diagnostic_code_t::invalid_id));
    CHECK(chart.measures.front().notes.front().position.row == 250);
}

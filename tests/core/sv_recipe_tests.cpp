#include <wormedit/core/sv_recipe.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>

using namespace wormedit;

namespace {

auto make_recipe_chart() -> chart_document_t {
    return {
        .id = chart_id_t{10},
        .measures = {{.id = measure_id_t{11}}, {.id = measure_id_t{12}}},
    };
}

auto make_linear_recipe(recipe_id_t id, measure_id_t first, measure_id_t second,
                        signed_rational_t from, signed_rational_t to) -> sv_recipe_t {
    auto recipe = sv_recipe_t{
        .id = id,
        .name = "rise",
        .range = {{first, 0}, {second, 0}},
        .output = sv_recipe_output_t::sv,
        .curve = sv_recipe_curve_t::linear,
        .parameters = {.value_from = from, .value_to = to},
    };
    recipe.sampling.fixed_point_count = 3;
    return recipe;
}

auto make_recipe_song() -> song_document_t {
    song_document_t song{.id = song_id_t{1}};
    song.charts.push_back(make_recipe_chart());
    return song;
}

} // namespace

TEST_CASE("SV recipes sample built-in constant, linear, exponential, and bump curves",
          "[unit][sv-recipe]") {
    auto chart = make_recipe_chart();
    auto linear =
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {3, 1});
    auto sampled = sample_sv_recipe(chart, linear);
    REQUIRE(sampled);
    REQUIRE(sampled.points.size() == 3);
    CHECK(sampled.points[0].value == Catch::Approx(1.0));
    CHECK(sampled.points[1].value == Catch::Approx(2.0));
    CHECK(sampled.points[2].value == Catch::Approx(3.0));

    linear.curve = sv_recipe_curve_t::exponential;
    linear.parameters.shape = {0, 1};
    sampled = sample_sv_recipe(chart, linear);
    REQUIRE(sampled);
    CHECK(sampled.points[1].value == Catch::Approx(2.0));

    auto constant = linear;
    constant.curve = sv_recipe_curve_t::constant;
    constant.parameters.value_from = {7, 4};
    sampled = sample_sv_recipe(chart, constant);
    REQUIRE(sampled);
    CHECK(sampled.points[0].value == Catch::Approx(1.75));
    CHECK(sampled.points[1].value == Catch::Approx(1.75));

    auto bump = linear;
    bump.curve = sv_recipe_curve_t::bump;
    bump.parameters.bump_start = {1, 4};
    bump.parameters.bump_length = {1, 2};
    bump.parameters.bump_x = {3, 1};
    bump.parameters.bump_y = {1, 1};
    bump.sampling.fixed_point_count.reset();
    bump.sampling.fixed_step_rows = 240;
    sampled = sample_sv_recipe(chart, bump);
    REQUIRE(sampled);
    CHECK(sampled.points.size() == 5);
    CHECK(sampled.points[0].value == Catch::Approx(1.0));
    CHECK(sampled.points[1].value == Catch::Approx(3.0));
    CHECK(sampled.points[2].value == Catch::Approx(3.0));
    CHECK(sampled.points[3].value == Catch::Approx(1.0));
    CHECK(sampled.points[4].value == Catch::Approx(1.0));

    bump.sampling.fixed_step_rows.reset();
    bump.sampling.fixed_point_count = 2;
    sampled = sample_sv_recipe(chart, bump);
    REQUIRE(sampled);
    REQUIRE(sampled.points.size() == 4);
    CHECK(sampled.points[1].absolute_row == 240);
    CHECK(sampled.points[1].value == Catch::Approx(3.0));
    CHECK(sampled.points[2].absolute_row == 720);
    CHECK(sampled.points[2].value == Catch::Approx(1.0));
}

TEST_CASE("SV recipes use bounded adaptive sampling and preserve exact row positions",
          "[unit][sv-recipe]") {
    auto chart = make_recipe_chart();
    auto recipe =
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {0, 1}, {1, 1});
    recipe.curve = sv_recipe_curve_t::exponential;
    recipe.parameters.shape = {8, 1};
    recipe.sampling.mode = sv_recipe_sampling_mode_t::adaptive;
    recipe.sampling.fixed_point_count.reset();
    recipe.sampling.tolerance = {1, 1000};
    recipe.sampling.max_points = 32;
    const auto sampled = sample_sv_recipe(chart, recipe);
    REQUIRE(sampled);
    CHECK(sampled.points.size() > 2);
    CHECK(sampled.points.size() <= 32);
    CHECK((sampled.points.front().position == local_position_t{measure_id_t{11}, 0}));
    CHECK((sampled.points.back().position == local_position_t{measure_id_t{12}, 0}));
    for (std::size_t i = 1; i < sampled.points.size(); ++i) {
        CHECK(sampled.points[i - 1].absolute_row < sampled.points[i].absolute_row);
    }
}

TEST_CASE("overlapping SV recipes multiply each other and the authored baseline",
          "[unit][sv-recipe]") {
    auto chart = make_recipe_chart();
    chart.scrolls.push_back(
        {.id = object_id_t{30}, .position = {measure_id_t{11}, 0}, .multiplier = {2, 1}});
    auto half =
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 2}, {1, 2});
    half.name = "half";
    half.curve = sv_recipe_curve_t::constant;
    auto rise =
        make_linear_recipe(recipe_id_t{21}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1});
    chart.sv_recipes = {half, rise};

    const auto compiled = compile_sv_recipes(chart, sv_recipe_output_t::sv);
    REQUIRE(compiled);
    REQUIRE(compiled.points.size() == 3);
    CHECK(compiled.points[0].value == Catch::Approx(1.0));
    CHECK(compiled.points[1].value == Catch::Approx(1.5));
    CHECK(compiled.points[2].value == Catch::Approx(2.0));
    CHECK(chart.scrolls.size() == 1);
    CHECK(chart.sv_recipes.size() == 2);

    chart.sv_recipes[0].enabled = false;
    const auto disabled = compile_sv_recipes(chart, sv_recipe_output_t::sv);
    REQUIRE(disabled);
    CHECK(disabled.points.front().value == Catch::Approx(2.0));
}

TEST_CASE("adjacent SV recipes use half-open boundary ownership", "[unit][sv-recipe]") {
    auto chart = make_recipe_chart();
    auto first =
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {2, 1}, {2, 1});
    first.curve = sv_recipe_curve_t::constant;
    auto second =
        make_linear_recipe(recipe_id_t{21}, measure_id_t{12}, measure_id_t{12}, {3, 1}, {3, 1});
    second.range.end.row = 480;
    second.curve = sv_recipe_curve_t::constant;
    chart.sv_recipes = {first, second};

    const auto compiled = compile_sv_recipes(chart, sv_recipe_output_t::sv);
    REQUIRE(compiled);
    const auto boundary =
        std::ranges::find(compiled.points, std::uint64_t{960}, &sv_recipe_point_t::absolute_row);
    REQUIRE(boundary != compiled.points.end());
    CHECK(boundary->value == Catch::Approx(3.0));
}

TEST_CASE("SPEEDS recipes retain transition settings in transient compiled points",
          "[unit][sv-recipe]") {
    auto chart = make_recipe_chart();
    auto recipe =
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1});
    recipe.output = sv_recipe_output_t::speeds;
    recipe.transition_duration = rational_t{3, 2};
    recipe.transition_is_seconds = true;
    chart.sv_recipes.push_back(recipe);

    const auto compiled = compile_sv_recipes(chart, sv_recipe_output_t::speeds);
    REQUIRE(compiled);
    REQUIRE(compiled.points.size() == 3);
    CHECK((compiled.points[1].transition_duration == rational_t{3, 2}));
    CHECK(compiled.points[1].transition_is_seconds);

    chart.speeds.push_back({.id = object_id_t{31},
                            .position = {measure_id_t{11}, 0},
                            .multiplier = {2, 1},
                            .transition_beats = {1, 1}});
    const auto transitioning_baseline = compile_sv_recipes(chart, sv_recipe_output_t::speeds);
    CHECK_FALSE(transitioning_baseline);

    chart.speeds.front().position = {measure_id_t{12}, 480};
    const auto later_transition = compile_sv_recipes(chart, sv_recipe_output_t::speeds);
    CHECK(later_transition);
}

TEST_CASE("export baking materializes ordinary events only in a detached chart copy",
          "[unit][sv-recipe][export]") {
    auto chart = make_recipe_chart();
    auto recipe =
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1});
    recipe.output = sv_recipe_output_t::sv;
    chart.sv_recipes.push_back(recipe);

    const auto baked = bake_sv_recipes_for_export(chart);
    REQUIRE(baked);
    REQUIRE(baked.chart->sv_recipes.empty());
    REQUIRE(baked.chart->scrolls.size() == 3);
    CHECK((baked.chart->scrolls[0].multiplier == signed_rational_t{1, 1}));
    CHECK((baked.chart->scrolls[1].multiplier == signed_rational_t{3, 2}));
    CHECK((baked.chart->scrolls[2].multiplier == signed_rational_t{1, 1}));
    CHECK(chart.scrolls.empty());
    CHECK(chart.sv_recipes.size() == 1);

    auto malformed = chart;
    malformed.sv_recipes.front().range.end = {measure_id_t{11}, 0};
    const auto rejected = bake_sv_recipes_for_export(malformed);
    CHECK_FALSE(rejected);
    CHECK_FALSE(rejected.diagnostics.empty());
}

TEST_CASE("SV recipe commands are revision checked and share undo history",
          "[unit][sv-recipe][history]") {
    auto history = make_document_history(make_recipe_song());
    const auto initial_revision = history.document.revision;
    auto recipe = make_linear_recipe({}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1});
    auto inserted = apply_sv_recipe_edit(history, initial_revision,
                                         insert_sv_recipe_action_t{chart_id_t{10}, recipe});
    REQUIRE(inserted);
    REQUIRE(history.document.charts.front().sv_recipes.size() == 1);
    const auto id = history.document.charts.front().sv_recipes.front().id;
    CHECK(id);
    CHECK(is_document_dirty(history));

    const auto stale = apply_sv_recipe_edit(
        history, initial_revision, set_sv_recipe_enabled_action_t{chart_id_t{10}, id, false});
    CHECK_FALSE(stale);
    CHECK(history.document.charts.front().sv_recipes.front().enabled);

    auto moved = apply_sv_recipe_edit(
        history, history.document.revision,
        move_sv_recipe_range_action_t{
            chart_id_t{10}, id, {{measure_id_t{11}, 240}, {measure_id_t{12}, 240}}});
    REQUIRE(moved);
    CHECK(history.document.charts.front().sv_recipes.front().range.begin.row == 240);
    CHECK(undo_document_command(history));
    CHECK(history.document.charts.front().sv_recipes.front().range.begin.row == 0);
    CHECK(redo_document_command(history));
    CHECK(history.document.charts.front().sv_recipes.front().range.begin.row == 240);

    const auto old_revision = history.document.revision;
    const auto range = history.document.charts.front().sv_recipes.front().range;
    const auto invalid_drag = apply_sv_recipe_edit(
        history, old_revision,
        move_sv_recipe_range_action_t{
            chart_id_t{10}, id, {{measure_id_t{12}, 240}, {measure_id_t{11}, 240}}});
    CHECK_FALSE(invalid_drag);
    CHECK(history.document.charts.front().sv_recipes.front().range == range);
}

TEST_CASE("SV recipe order and enabled state edits remain undoable", "[unit][sv-recipe][history]") {
    auto chart = make_recipe_chart();
    chart.sv_recipes = {
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1}),
        make_linear_recipe(recipe_id_t{21}, measure_id_t{11}, measure_id_t{12}, {2, 1}, {3, 1}),
    };
    auto song = song_document_t{.id = song_id_t{1}, .charts = {chart}};
    auto history = make_document_history(std::move(song));
    auto result =
        apply_sv_recipe_edit(history, history.document.revision,
                             reorder_sv_recipe_action_t{chart_id_t{10}, recipe_id_t{21}, 0});
    REQUIRE(result);
    CHECK(history.document.charts.front().sv_recipes.front().id == recipe_id_t{21});
    result = apply_sv_recipe_edit(
        history, history.document.revision,
        set_sv_recipe_enabled_action_t{chart_id_t{10}, recipe_id_t{21}, false});
    REQUIRE(result);
    CHECK_FALSE(history.document.charts.front().sv_recipes.front().enabled);
    CHECK(undo_document_command(history));
    CHECK(history.document.charts.front().sv_recipes.front().enabled);
}

TEST_CASE("SV recipe range drag snaps, previews without mutation, and commits once",
          "[unit][sv-recipe][interaction]") {
    auto song = make_recipe_song();
    song.charts.front().sv_recipes.push_back(
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1}));
    auto history = make_document_history(std::move(song));
    const auto revision = history.document.revision;

    auto drag = begin_sv_recipe_range_drag(history, chart_id_t{10}, recipe_id_t{20},
                                           sv_recipe_range_handle_t::begin, {measure_id_t{11}, 17},
                                           4, false);
    REQUIRE(drag.status == sv_recipe_range_drag_status_t::previewing);
    REQUIRE(update_sv_recipe_range_drag(history, drag, {measure_id_t{11}, 370}, false));
    CHECK((drag.preview_range.begin == local_position_t{measure_id_t{11}, 480}));
    CHECK((history.document.charts.front().sv_recipes.front().range.begin ==
           local_position_t{measure_id_t{11}, 0}));
    CHECK(history.document.revision == revision);

    const auto committed = commit_sv_recipe_range_drag(history, drag, false);
    REQUIRE(committed);
    CHECK(drag.status == sv_recipe_range_drag_status_t::committed);
    CHECK((history.document.charts.front().sv_recipes.front().range.begin ==
           local_position_t{measure_id_t{11}, 480}));
    CHECK(history.undo.size() == 1);
}

TEST_CASE("SV recipe endpoint crossing normalizes while whole-span drag preserves length",
          "[unit][sv-recipe][interaction]") {
    auto song = make_recipe_song();
    song.charts.front().sv_recipes.push_back(
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1}));
    auto history = make_document_history(std::move(song));

    auto endpoint = begin_sv_recipe_range_drag(history, chart_id_t{10}, recipe_id_t{20},
                                               sv_recipe_range_handle_t::begin,
                                               {measure_id_t{11}, 0}, 4, false);
    REQUIRE(update_sv_recipe_range_drag(history, endpoint, {measure_id_t{12}, 481}, false));
    CHECK((endpoint.preview_range.begin == local_position_t{measure_id_t{12}, 0}));
    CHECK((endpoint.preview_range.end == local_position_t{measure_id_t{12}, 480}));
    cancel_sv_recipe_range_drag(endpoint);
    CHECK(endpoint.status == sv_recipe_range_drag_status_t::cancelled);

    auto whole = begin_sv_recipe_range_drag(history, chart_id_t{10}, recipe_id_t{20},
                                            sv_recipe_range_handle_t::whole_span,
                                            {measure_id_t{11}, 0}, 4, false);
    REQUIRE(update_sv_recipe_range_drag(history, whole, {measure_id_t{11}, 370}, false));
    CHECK((whole.preview_range.begin == local_position_t{measure_id_t{11}, 480}));
    CHECK((whole.preview_range.end == local_position_t{measure_id_t{12}, 480}));
    CHECK(history.document.charts.front().sv_recipes.front().range == whole.original_range);
}

TEST_CASE("SV recipe range drag rejects zero spans, locks, and stale revisions atomically",
          "[unit][sv-recipe][interaction][negative]") {
    auto song = make_recipe_song();
    song.charts.front().sv_recipes.push_back(
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1}));
    auto history = make_document_history(std::move(song));
    const auto original = history.document.charts.front().sv_recipes.front().range;

    auto zero = begin_sv_recipe_range_drag(history, chart_id_t{10}, recipe_id_t{20},
                                           sv_recipe_range_handle_t::begin, {measure_id_t{11}, 0},
                                           4, false);
    CHECK_FALSE(update_sv_recipe_range_drag(history, zero, {measure_id_t{12}, 0}, false));
    CHECK(zero.preview_range == original);
    CHECK(zero.status == sv_recipe_range_drag_status_t::previewing);

    auto locked =
        begin_sv_recipe_range_drag(history, chart_id_t{10}, recipe_id_t{20},
                                   sv_recipe_range_handle_t::end, {measure_id_t{12}, 0}, 4, true);
    CHECK(locked.status == sv_recipe_range_drag_status_t::rejected);

    auto stale =
        begin_sv_recipe_range_drag(history, chart_id_t{10}, recipe_id_t{20},
                                   sv_recipe_range_handle_t::end, {measure_id_t{12}, 0}, 4, false);
    auto proposed = history.document;
    proposed.metadata.title = "revision change";
    REQUIRE(commit_document_command(history, std::move(proposed), "Unrelated edit"));
    CHECK_FALSE(update_sv_recipe_range_drag(history, stale, {measure_id_t{12}, 480}, false));
    CHECK(stale.status == sv_recipe_range_drag_status_t::rejected);
    CHECK(history.document.charts.front().sv_recipes.front().range == original);
}

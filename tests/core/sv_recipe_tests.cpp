#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/workspace.h>

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

auto make_constant_recipe(recipe_id_t id, measure_id_t first, measure_id_t second,
                          signed_rational_t value) -> sv_recipe_t {
    auto recipe = make_linear_recipe(id, first, second, value, value);
    recipe.name = "constant";
    recipe.curve = sv_recipe_curve_t::constant;
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
    auto history = document_history_t::from(make_recipe_song());
    const auto initial_revision = history.document.revision;
    auto recipe = make_linear_recipe({}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1});
    auto inserted = apply_sv_recipe_edit(history,
                                         insert_sv_recipe_action_t{chart_id_t{10}, recipe});
    REQUIRE(inserted);
    REQUIRE(history.document.charts.front().sv_recipes.size() == 1);
    const auto id = history.document.charts.front().sv_recipes.front().id;
    CHECK(id);
    CHECK(is_document_dirty(history));


    auto moved = apply_sv_recipe_edit(
        history,
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
        history,
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
    auto history = document_history_t::from(std::move(song));
    auto result =
        apply_sv_recipe_edit(history,
                             reorder_sv_recipe_action_t{chart_id_t{10}, recipe_id_t{21}, 0});
    REQUIRE(result);
    CHECK(history.document.charts.front().sv_recipes.front().id == recipe_id_t{21});
    result = apply_sv_recipe_edit(
        history,
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
    auto history = document_history_t::from(std::move(song));
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
    auto history = document_history_t::from(std::move(song));

    auto endpoint = begin_sv_recipe_range_drag(history, chart_id_t{10}, recipe_id_t{20},
                                               sv_recipe_range_handle_t::begin,
                                               {measure_id_t{11}, 0}, 4, false);
    REQUIRE(update_sv_recipe_range_drag(history, endpoint, {measure_id_t{12}, 481}, false));
    CHECK((endpoint.preview_range.begin == local_position_t{measure_id_t{12}, 0}));
    CHECK((endpoint.preview_range.end == local_position_t{measure_id_t{12}, 480}));
    cancel_sv_recipe_range_drag(endpoint);
    CHECK(endpoint.status == sv_recipe_range_drag_status_t::cancelled);

    auto chart_end =
        begin_sv_recipe_range_drag(history, chart_id_t{10}, recipe_id_t{20},
                                   sv_recipe_range_handle_t::end, {measure_id_t{12}, 0}, 4, false);
    REQUIRE(update_sv_recipe_range_drag(history, chart_end, {measure_id_t{12}, 959}, false));
    CHECK((chart_end.preview_range.end == local_position_t{measure_id_t{12}, 960}));
    cancel_sv_recipe_range_drag(chart_end);

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
    auto history = document_history_t::from(std::move(song));
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

TEST_CASE("SV recipe draft previews raw and composed values without mutation, then applies once",
          "[unit][sv-recipe][draft]") {
    auto song = make_recipe_song();
    auto& chart = song.charts.front();
    chart.speeds.push_back({.id = object_id_t{30},
                            .position = {measure_id_t{11}, 0},
                            .multiplier = {2, 1},
                            .transition_beats = {0, 1}});
    auto existing =
        make_constant_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {2, 1});
    existing.output = sv_recipe_output_t::speeds;
    existing.transition_duration = rational_t{1, 1};
    existing.transition_is_seconds = false;
    chart.sv_recipes.push_back(existing);
    auto history = document_history_t::from(std::move(song));
    const auto initial_revision = history.document.revision;
    const auto initial_undo_size = history.undo.size();

    auto candidate = make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {3, 1});
    candidate.output = sv_recipe_output_t::speeds;
    candidate.transition_duration = rational_t{3, 2};
    candidate.transition_is_seconds = true;
    auto draft = begin_sv_recipe_draft(history, chart_id_t{10}, candidate);
    REQUIRE(draft.status == sv_recipe_draft_status_t::editing);
    CHECK_FALSE(draft.candidate.id);
    const auto preview = preview_sv_recipe_draft(history, draft);
    REQUIRE(preview);
    REQUIRE(preview.candidate_points.points.size() == 3);
    CHECK(preview.candidate_points.points.front().value == Catch::Approx(3.0));
    REQUIRE(preview.effective_output.points.size() == 3);
    CHECK(preview.effective_output.points.front().value == Catch::Approx(12.0));
    CHECK((preview.effective_output.points.front().transition_duration == rational_t{3, 2}));
    CHECK(preview.effective_output.points.front().transition_is_seconds);
    CHECK(history.document.revision == initial_revision);
    CHECK(history.undo.size() == initial_undo_size);
    CHECK(history.document.charts.front().sv_recipes.size() == 1);

    const auto applied = apply_sv_recipe_draft(history, draft);
    REQUIRE(applied);
    CHECK(draft.status == sv_recipe_draft_status_t::applied);
    CHECK(history.document.revision == initial_revision + 1);
    CHECK(history.undo.size() == initial_undo_size + 1);
    REQUIRE(history.document.charts.front().sv_recipes.size() == 2);
    CHECK(history.document.charts.front().sv_recipes.back().id);

    auto cancelled = begin_sv_recipe_draft(history, chart_id_t{10}, candidate);
    REQUIRE(cancelled.status == sv_recipe_draft_status_t::editing);
    const auto revision_before_cancel = history.document.revision;
    const auto undo_before_cancel = history.undo.size();
    cancel_sv_recipe_draft(cancelled);
    CHECK(cancelled.status == sv_recipe_draft_status_t::cancelled);
    CHECK(history.document.revision == revision_before_cancel);
    CHECK(history.undo.size() == undo_before_cancel);
    CHECK(history.document.charts.front().sv_recipes.size() == 2);
}

TEST_CASE("applying an SV recipe requires rebinding the workspace before its next frame",
          "[unit][sv-recipe][workspace]") {
    auto history = document_history_t::from(make_recipe_song());
    auto binding = workspace_controller_t::from(history);
    REQUIRE(binding.workspace);
    auto workspace = *binding.workspace;
    workspace.active_tab = workspace_tab_t::compose;
    REQUIRE(workspace_view_t::from(workspace, history.document));

    auto candidate = make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {2, 1});
    auto draft = begin_sv_recipe_draft(history, chart_id_t{10}, candidate);
    REQUIRE(draft.status == sv_recipe_draft_status_t::editing);
    REQUIRE(preview_sv_recipe_draft(history, draft));
    REQUIRE(apply_sv_recipe_draft(history, draft));

    CHECK_FALSE(workspace_view_t::from(workspace, history.document));
    REQUIRE(rebind_workspace(workspace, history));
    const auto next_frame = workspace_view_t::from(workspace, history.document);
    REQUIRE(next_frame);
    CHECK(next_frame.view->active_tab == workspace_tab_t::compose);
    CHECK(next_frame.view->document_revision == history.document.revision);
}

TEST_CASE("SV recipe edit drafts substitute in stable order and preserve replacement ID",
          "[unit][sv-recipe][draft]") {
    auto song = make_recipe_song();
    auto& chart = song.charts.front();
    auto first = make_constant_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {2, 1});
    first.output = sv_recipe_output_t::speeds;
    first.transition_duration = rational_t{1, 1};
    first.transition_is_seconds = false;
    auto middle = make_constant_recipe(recipe_id_t{21}, measure_id_t{11}, measure_id_t{12}, {3, 1});
    middle.output = sv_recipe_output_t::speeds;
    middle.transition_duration = rational_t{2, 1};
    middle.transition_is_seconds = false;
    auto last = make_constant_recipe(recipe_id_t{22}, measure_id_t{11}, measure_id_t{12}, {5, 1});
    last.output = sv_recipe_output_t::speeds;
    last.transition_duration = rational_t{7, 1};
    last.transition_is_seconds = true;
    chart.sv_recipes = {first, middle, last};
    auto history = document_history_t::from(std::move(song));

    auto draft = begin_existing_sv_recipe_draft(history, chart_id_t{10}, recipe_id_t{21});
    REQUIRE(draft.status == sv_recipe_draft_status_t::editing);
    auto replacement = draft.candidate;
    replacement.parameters.value_from = {4, 1};
    REQUIRE(update_sv_recipe_draft(history, draft, replacement));
    const auto preview = preview_sv_recipe_draft(history, draft);
    REQUIRE(preview);
    REQUIRE(preview.candidate_points.points.size() == 3);
    CHECK(preview.candidate_points.points.front().value == Catch::Approx(4.0));
    REQUIRE(preview.effective_output.points.size() == 3);
    CHECK(preview.effective_output.points.front().value == Catch::Approx(40.0));
    CHECK((preview.effective_output.points.front().transition_duration == rational_t{7, 1}));
    CHECK(preview.effective_output.points.front().transition_is_seconds);
    CHECK((history.document.charts.front().sv_recipes[1].parameters.value_from ==
           signed_rational_t{3, 1}));

    const auto applied = apply_sv_recipe_draft(history, draft);
    REQUIRE(applied);
    CHECK(history.document.charts.front().sv_recipes.size() == 3);
    CHECK(history.document.charts.front().sv_recipes[1].id == recipe_id_t{21});
    CHECK((history.document.charts.front().sv_recipes[1].parameters.value_from ==
           signed_rational_t{4, 1}));
    CHECK(history.document.charts.front().sv_recipes[2].id == recipe_id_t{22});
    CHECK(history.undo.size() == 1);
}

TEST_CASE("SV recipe draft range snapping normalizes direction and remains transient",
          "[unit][sv-recipe][draft]") {
    auto history = document_history_t::from(make_recipe_song());
    const auto revision = history.document.revision;
    const auto undo_size = history.undo.size();
    auto draft =
        begin_sv_recipe_draft(history, chart_id_t{10},
                              make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {2, 1}));
    REQUIRE(draft.status == sv_recipe_draft_status_t::editing);
    REQUIRE(set_sv_recipe_draft_range(history, draft, {measure_id_t{12}, 451},
                                      {measure_id_t{11}, 17}, 4));
    CHECK((draft.candidate.range.begin == local_position_t{measure_id_t{11}, 0}));
    CHECK((draft.candidate.range.end == local_position_t{measure_id_t{12}, 480}));
    CHECK(history.document.revision == revision);
    CHECK(history.undo.size() == undo_size);

    REQUIRE(set_sv_recipe_draft_range(history, draft, {measure_id_t{11}, 0},
                                      {measure_id_t{12}, 959}, 4));
    CHECK((draft.candidate.range.begin == local_position_t{measure_id_t{11}, 0}));
    CHECK((draft.candidate.range.end == local_position_t{measure_id_t{12}, 960}));
    const auto chart_end_preview = preview_sv_recipe_draft(history, draft);
    REQUIRE(chart_end_preview);
    REQUIRE_FALSE(chart_end_preview.candidate_points.points.empty());
    CHECK(chart_end_preview.candidate_points.points.back().absolute_row == 1919);

    const auto before_failed_range = draft.candidate.range;
    CHECK_FALSE(
        set_sv_recipe_draft_range(history, draft, {measure_id_t{11}, 0}, {measure_id_t{11}, 0}, 4));
    CHECK(draft.candidate.range == before_failed_range);
    CHECK(history.document.revision == revision);

    auto uneven_song = make_recipe_song();
    uneven_song.charts.front().measures.back().definition = {{7, 8}, meter_t{7, 8}};
    auto uneven_history = document_history_t::from(std::move(uneven_song));
    auto uneven_draft =
        begin_sv_recipe_draft(uneven_history, chart_id_t{10},
                              make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {2, 1}));
    REQUIRE(set_sv_recipe_draft_range(uneven_history, uneven_draft, {measure_id_t{11}, 0},
                                      {measure_id_t{12}, 840}, 4));
    CHECK((uneven_draft.candidate.range.end == local_position_t{measure_id_t{12}, 840}));
    REQUIRE(set_sv_recipe_draft_range(uneven_history, uneven_draft, {measure_id_t{11}, 0},
                                      {measure_id_t{12}, 838}, 4));
    CHECK((uneven_draft.candidate.range.end == local_position_t{measure_id_t{12}, 840}));
    const auto uneven_preview = preview_sv_recipe_draft(uneven_history, uneven_draft);
    REQUIRE(uneven_preview);
    REQUIRE_FALSE(uneven_preview.candidate_points.points.empty());
    CHECK(uneven_preview.candidate_points.points.back().absolute_row == 1799);
}

TEST_CASE("SV recipe drafts reject invalid candidates and stale revisions without publishing",
          "[unit][sv-recipe][draft][negative]") {
    auto history = document_history_t::from(make_recipe_song());
    auto invalid = make_constant_recipe({}, measure_id_t{11}, measure_id_t{11}, {2, 1});
    auto draft = begin_sv_recipe_draft(history, chart_id_t{10}, invalid);
    REQUIRE(draft.status == sv_recipe_draft_status_t::editing);
    const auto revision = history.document.revision;
    CHECK_FALSE(preview_sv_recipe_draft(history, draft));
    const auto rejected_apply = apply_sv_recipe_draft(history, draft);
    CHECK_FALSE(rejected_apply);
    CHECK(draft.status == sv_recipe_draft_status_t::editing);
    CHECK(history.document.revision == revision);
    CHECK(history.document.charts.front().sv_recipes.empty());

    auto valid = make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {2, 1});
    REQUIRE(update_sv_recipe_draft(history, draft, valid));
    REQUIRE(preview_sv_recipe_draft(history, draft));
    REQUIRE(apply_sv_recipe_draft(history, draft));
    CHECK(history.document.charts.front().sv_recipes.size() == 1);

    auto stale = begin_sv_recipe_draft(history, chart_id_t{10}, valid);
    REQUIRE(stale.status == sv_recipe_draft_status_t::editing);
    auto proposed = history.document;
    proposed.metadata.title = "revision change";
    REQUIRE(commit_document_command(history, std::move(proposed), "Unrelated edit"));
    const auto after_external_edit = history.document.revision;
    CHECK_FALSE(preview_sv_recipe_draft(history, stale));
    CHECK_FALSE(apply_sv_recipe_draft(history, stale));
    CHECK(stale.status == sv_recipe_draft_status_t::cancelled);
    CHECK(history.document.revision == after_external_edit);
    CHECK(history.document.charts.front().sv_recipes.size() == 1);
}

TEST_CASE("materializing a recipe replaces it with ordinary events in one undo step",
          "[unit][sv-recipe][materialize]") {
    auto song = make_recipe_song();
    auto& chart = song.charts.front();
    chart.scrolls.push_back({.id = object_id_t{5},
                             .position = {measure_id_t{11}, 0},
                             .multiplier = {1, 1},
                             .provenance = {.source_order = 4}});
    auto recipe =
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1});
    recipe.enabled = false;  // a disabled recipe still materializes
    chart.sv_recipes.push_back(recipe);
    auto history = document_history_t::from(song);

    const auto result = apply_sv_recipe_edit(
        history,
        materialize_sv_recipe_action_t{chart_id_t{10}, recipe_id_t{20}});
    REQUIRE(result);
    const auto& after = history.document.charts.front();
    CHECK(after.sv_recipes.empty());
    REQUIRE(after.scrolls.size() == 3);
    CHECK((after.scrolls[1].multiplier == signed_rational_t{3, 2}));
    CHECK(after.scrolls[0].provenance.profile == "sv-recipe-materialized");
    CHECK(after.scrolls[0].provenance.source_order >= 5);
    // The materialized events match what export baking produces for the same recipe.
    auto expected = song.charts.front();
    expected.sv_recipes.front().enabled = true;
    const auto baked = bake_sv_recipes_for_export(expected);
    REQUIRE(baked);
    for (std::size_t index = 0; index < after.scrolls.size(); ++index) {
        CHECK(after.scrolls[index].position == baked.chart->scrolls[index].position);
        CHECK(after.scrolls[index].multiplier == baked.chart->scrolls[index].multiplier);
    }
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().sv_recipes.size() == 1);
    CHECK(history.document.charts.front().scrolls.size() == 1);
}

TEST_CASE("materializing rejects unknown and non-evaluable recipes without a revision",
          "[unit][sv-recipe][materialize]") {
    auto song = make_recipe_song();
    auto recipe =
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1});
    song.charts.front().sv_recipes.push_back(recipe);
    auto expression = recipe;
    expression.id = recipe_id_t{21};
    expression.name = "wave";
    expression.curve = sv_recipe_curve_t::lua_expression;
    expression.expression_source = "return 1 + t";
    song.charts.front().sv_recipes.push_back(expression);
    auto history = document_history_t::from(song);
    const auto revision = history.document.revision;

    CHECK_FALSE(apply_sv_recipe_edit(history,
                                     materialize_sv_recipe_action_t{chart_id_t{10}, recipe_id_t{99}}));
    const auto unsupported = apply_sv_recipe_edit(
        history, materialize_sv_recipe_action_t{chart_id_t{10}, recipe_id_t{21}});
    CHECK_FALSE(unsupported);
    CHECK(unsupported.diagnostics.front().code == diagnostic_code_t::unsupported_feature);
    CHECK(history.document.revision == revision);
    CHECK(history.document.charts.front().sv_recipes.size() == 2);
}

TEST_CASE("Lua expression recipes are stored and validated but never sampled",
          "[unit][sv-recipe][lua]") {
    auto chart = make_recipe_chart();
    auto recipe =
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1});
    recipe.curve = sv_recipe_curve_t::lua_expression;
    CHECK_FALSE(validate_sv_recipe(chart, recipe).empty());  // an expression needs source
    recipe.expression_source = "return 1 + t";
    CHECK(validate_sv_recipe(chart, recipe).empty());
    const auto sampled = sample_sv_recipe(chart, recipe);
    CHECK_FALSE(sampled);
    CHECK(sampled.diagnostics.front().code == diagnostic_code_t::unsupported_feature);
    chart.sv_recipes.push_back(recipe);
    CHECK_FALSE(bake_sv_recipes_for_export(chart));

    auto stray = make_linear_recipe(recipe_id_t{22}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1});
    stray.expression_source = "return 2";
    CHECK_FALSE(validate_sv_recipe(chart, stray).empty());  // source belongs to the expression curve
    recipe.expression_source.assign(16 * 1024 + 1, 'x');
    CHECK_FALSE(validate_sv_recipe(chart, recipe).empty());
}

TEST_CASE("SV recipes reject malformed UTF-8 and NUL-bearing bounded text", "[unit][sv-recipe]") {
    const auto chart = make_recipe_chart();
    auto recipe = make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1});
    recipe.name = "\xc0\x80";
    CHECK_FALSE(validate_sv_recipe(chart, recipe).empty());
    recipe.name = std::string{"a\0b", 3};
    CHECK_FALSE(validate_sv_recipe(chart, recipe).empty());
    recipe.name = "expression";
    recipe.curve = sv_recipe_curve_t::lua_expression;
    recipe.expression_source = "return 1 -- \xc0\x80";
    CHECK_FALSE(validate_sv_recipe(chart, recipe).empty());
}

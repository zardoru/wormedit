#include "../support/workspace_result_assertions.h"
#include "../support/stale_action_assertions.h"
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/workspace.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <type_traits>

using namespace wormedit;

static_assert(std::is_aggregate_v<sv_recipe_draft_t>);
static_assert(std::is_aggregate_v<sv_recipe_range_drag_t>);
static_assert(std::is_aggregate_v<proposal_t<sv_recipe_draft_t>>);

namespace {

void require_export_bake(const sv_recipe_export_bake_outcome_t& result) {
    REQUIRE(result.value);
    REQUIRE(result.diagnostics.empty());
}

void check_export_bake_rejection(const sv_recipe_export_bake_outcome_t& result) {
    CHECK_FALSE((result.value && result.diagnostics.empty()));
}

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

auto make_range_proposal(const document_history_t& history, sv_recipe_range_handle_t handle,
                          local_position_t pointer) -> proposal_outcome_t<sv_recipe_range_drag_t> {
    auto proposal = sv_recipe_range_drag_t::propose(history, chart_id_t{10}, recipe_id_t{20},
        handle, pointer, 4, false);
    REQUIRE(proposal.value);
    return proposal;
}

void check_locked_range(const proposal_outcome_t<sv_recipe_range_drag_t>& outcome) {
    CHECK_FALSE(outcome.value);
    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(outcome.diagnostics.front().message == "SV recipe range dragging is unavailable while timing is locked");
}

void check_retained_range(const document_history_t& history,
    const proposal_t<sv_recipe_range_drag_t>& previous,
    const proposal_outcome_t<sv_recipe_range_drag_t>& rejected, position_span_t retained) {
    CHECK_FALSE(rejected.value);
    REQUIRE_FALSE(rejected.diagnostics.empty());
    CHECK(previous.payload.preview_range == retained);
    CHECK(previous.payload.lifecycle.status() == proposal_status_t::active);
    CHECK(history.undo.empty());
}

auto checked_draft(proposal_outcome_t<sv_recipe_draft_t> outcome)
    -> proposal_outcome_t<sv_recipe_draft_t> {
    REQUIRE(outcome.value);
    return outcome;
}

void revise_draft(const document_history_t& history, proposal_outcome_t<sv_recipe_draft_t>& draft,
                  sv_recipe_t candidate) {
    REQUIRE(draft.value);
    auto revised = draft.value->revise(history, {.diagnostics = std::move(draft.diagnostics)}, std::move(candidate));
    REQUIRE(revised.value);
    draft = std::move(revised);
}

void revise_draft_range(const document_history_t& history,
    proposal_outcome_t<sv_recipe_draft_t>& draft, position_span_t hits, std::uint64_t snap) {
    REQUIRE(draft.value);
    auto revised = draft.value->revise(history, {.diagnostics = std::move(draft.diagnostics)}, hits, snap);
    REQUIRE(revised.value);
    draft = std::move(revised);
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
    require_export_bake(baked);
    REQUIRE(baked.value->sv_recipes.empty());
    REQUIRE(baked.value->scrolls.size() == 3);
    CHECK((baked.value->scrolls[0].multiplier == signed_rational_t{1, 1}));
    CHECK((baked.value->scrolls[1].multiplier == signed_rational_t{3, 2}));
    CHECK((baked.value->scrolls[2].multiplier == signed_rational_t{1, 1}));
    CHECK(chart.scrolls.empty());
    CHECK(chart.sv_recipes.size() == 1);

    auto malformed = chart;
    malformed.sv_recipes.front().range.end = {measure_id_t{11}, 0};
    const auto rejected = bake_sv_recipes_for_export(malformed);
    check_export_bake_rejection(rejected);
    CHECK_FALSE(rejected.diagnostics.empty());
}

TEST_CASE("SV recipe commands are revision checked and share undo history",
          "[unit][sv-recipe][history]") {
    auto history = document_history_t::from(make_recipe_song());
    const auto initial_revision = history.document.revision;
    auto recipe = make_linear_recipe({}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1});
    auto inserted = apply_sv_recipe_edit(history, wormedit::insert_edit_t<wormedit::sv_recipe_t>{
                                                      .chart = chart_id_t{10}, .value = recipe});
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

    auto drag = make_range_proposal(history, sv_recipe_range_handle_t::begin, {measure_id_t{11}, 17});
    REQUIRE(drag.value->payload.lifecycle.status() == proposal_status_t::active);
    const auto drag_revised = drag.value->revise(history, {}, local_position_t{measure_id_t{11}, 370});
    REQUIRE(drag_revised.value);
    CHECK((drag.value->payload.preview_range.begin == local_position_t{measure_id_t{11}, 480}));
    CHECK((history.document.charts.front().sv_recipes.front().range.begin ==
           local_position_t{measure_id_t{11}, 0}));
    CHECK(history.document.revision == revision);

    const auto committed = apply_proposal(history, *drag.value);
    REQUIRE(committed);
    CHECK(drag.value->payload.lifecycle.status() == proposal_status_t::applied);
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

    auto endpoint = make_range_proposal(history, sv_recipe_range_handle_t::begin, {measure_id_t{11}, 0});
    const auto endpoint_revised = endpoint.value->revise(history, {}, local_position_t{measure_id_t{12}, 481});
    REQUIRE(endpoint_revised.value);
    CHECK((endpoint.value->payload.preview_range.begin == local_position_t{measure_id_t{12}, 0}));
    CHECK((endpoint.value->payload.preview_range.end == local_position_t{measure_id_t{12}, 480}));
    endpoint.value->payload.lifecycle.cancel();
    CHECK(endpoint.value->payload.lifecycle.status() == proposal_status_t::cancelled);

    auto chart_end =
        make_range_proposal(history, sv_recipe_range_handle_t::end, {measure_id_t{12}, 0});
    const auto chart_end_revised = chart_end.value->revise(history, {}, local_position_t{measure_id_t{12}, 959});
    REQUIRE(chart_end_revised.value);
    CHECK((chart_end.value->payload.preview_range.end == local_position_t{measure_id_t{12}, 960}));
    chart_end.value->payload.lifecycle.cancel();

    auto whole = make_range_proposal(history, sv_recipe_range_handle_t::whole_span, {measure_id_t{11}, 0});
    const auto whole_revised = whole.value->revise(history, {}, local_position_t{measure_id_t{11}, 370});
    REQUIRE(whole_revised.value);
    CHECK((whole.value->payload.preview_range.begin == local_position_t{measure_id_t{11}, 480}));
    CHECK((whole.value->payload.preview_range.end == local_position_t{measure_id_t{12}, 480}));
    CHECK(history.document.charts.front().sv_recipes.front().range == whole.value->payload.original_range);
}

TEST_CASE("SV recipe range drag rejects zero spans, locks, and stale revisions atomically",
          "[unit][sv-recipe][interaction][negative]") {
    auto song = make_recipe_song();
    song.charts.front().sv_recipes.push_back(
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1}));
    auto history = document_history_t::from(std::move(song));
    const auto original = history.document.charts.front().sv_recipes.front().range;

    auto zero = make_range_proposal(history, sv_recipe_range_handle_t::begin, {measure_id_t{11}, 0});
    const auto zero_revised = zero.value->revise(history, {}, local_position_t{measure_id_t{12}, 0});
    CHECK_FALSE(zero_revised.value);
    CHECK(zero.value->payload.preview_range == original);
    CHECK(zero.value->payload.lifecycle.status() == proposal_status_t::active);

    auto locked =
        sv_recipe_range_drag_t::propose(history, chart_id_t{10}, recipe_id_t{20},
                                   sv_recipe_range_handle_t::end, {measure_id_t{12}, 0}, 4, true);
    check_locked_range(locked);

    auto stale =
        make_range_proposal(history, sv_recipe_range_handle_t::end, {measure_id_t{12}, 0});
    auto proposed = history.document;
    proposed.metadata.title = "revision change";
    REQUIRE(commit_document_command(history, std::move(proposed), "Unrelated edit"));
    const auto stale_revised = stale.value->revise(history, {}, local_position_t{measure_id_t{12}, 480});
    CHECK_FALSE(stale_revised.value);
    test::check_stale_action_message(stale_revised.diagnostics, "SV recipe range drag");
    CHECK(stale.value->payload.lifecycle.status() == proposal_status_t::rejected);
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
    auto draft = checked_draft(sv_recipe_draft_t::propose(history, chart_id_t{10}, candidate));
    REQUIRE(draft.value->payload.lifecycle.status() == proposal_status_t::active);
    CHECK_FALSE(draft.value->payload.candidate.id);
    const auto preview = preview_sv_recipe_draft(history, *draft.value);
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

    const auto applied = apply_proposal(history, *draft.value);
    REQUIRE(applied);
    CHECK(draft.value->payload.lifecycle.status() == proposal_status_t::applied);
    CHECK(history.document.revision == initial_revision + 1);
    CHECK(history.undo.size() == initial_undo_size + 1);
    REQUIRE(history.document.charts.front().sv_recipes.size() == 2);
    CHECK(history.document.charts.front().sv_recipes.back().id);

    auto cancelled = checked_draft(sv_recipe_draft_t::propose(history, chart_id_t{10}, candidate));
    REQUIRE(cancelled.value->payload.lifecycle.status() == proposal_status_t::active);
    const auto revision_before_cancel = history.document.revision;
    const auto undo_before_cancel = history.undo.size();
    cancelled.value->payload.lifecycle.cancel();
    CHECK(cancelled.value->payload.lifecycle.status() == proposal_status_t::cancelled);
    CHECK(history.document.revision == revision_before_cancel);
    CHECK(history.undo.size() == undo_before_cancel);
    CHECK(history.document.charts.front().sv_recipes.size() == 2);
}

TEST_CASE("applying an SV recipe requires rebinding the workspace before its next frame",
          "[unit][sv-recipe][workspace]") {
    auto history = document_history_t::from(make_recipe_song());
    auto binding = workspace_controller_t::from(history);
    REQUIRE(binding.value);
    auto workspace = *binding.value;
    workspace.active_tab = workspace_tab_t::compose;
    wormedit::test::require_workspace_result(workspace_view_t::from(workspace, history.document));

    auto candidate = make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {2, 1});
    auto draft = checked_draft(sv_recipe_draft_t::propose(history, chart_id_t{10}, candidate));
    REQUIRE(draft.value->payload.lifecycle.status() == proposal_status_t::active);
    REQUIRE(preview_sv_recipe_draft(history, *draft.value));
    REQUIRE(apply_proposal(history, *draft.value));

    wormedit::test::check_workspace_rejection(workspace_view_t::from(workspace, history.document));
    REQUIRE(rebind_workspace(workspace, history));
    const auto next_frame = workspace_view_t::from(workspace, history.document);
    wormedit::test::require_workspace_result(next_frame);
    CHECK(next_frame.value->active_tab == workspace_tab_t::compose);
    CHECK(next_frame.value->document_revision == history.document.revision);
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

    auto draft = checked_draft(sv_recipe_draft_t::propose(history, chart_id_t{10}, recipe_id_t{21}));
    REQUIRE(draft.value->payload.lifecycle.status() == proposal_status_t::active);
    auto replacement = draft.value->payload.candidate;
    replacement.parameters.value_from = {4, 1};
    revise_draft(history, draft, replacement);
    const auto preview = preview_sv_recipe_draft(history, *draft.value);
    REQUIRE(preview);
    REQUIRE(preview.candidate_points.points.size() == 3);
    CHECK(preview.candidate_points.points.front().value == Catch::Approx(4.0));
    REQUIRE(preview.effective_output.points.size() == 3);
    CHECK(preview.effective_output.points.front().value == Catch::Approx(40.0));
    CHECK((preview.effective_output.points.front().transition_duration == rational_t{7, 1}));
    CHECK(preview.effective_output.points.front().transition_is_seconds);
    CHECK((history.document.charts.front().sv_recipes[1].parameters.value_from ==
           signed_rational_t{3, 1}));

    const auto applied = apply_proposal(history, *draft.value);
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
        checked_draft(sv_recipe_draft_t::propose(history, chart_id_t{10},
                              make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {2, 1})));
    REQUIRE(draft.value->payload.lifecycle.status() == proposal_status_t::active);
    revise_draft_range(history, draft, {{measure_id_t{12}, 451}, {measure_id_t{11}, 17}}, 4);
    CHECK((draft.value->payload.candidate.range.begin == local_position_t{measure_id_t{11}, 0}));
    CHECK((draft.value->payload.candidate.range.end == local_position_t{measure_id_t{12}, 480}));
    CHECK(history.document.revision == revision);
    CHECK(history.undo.size() == undo_size);

    revise_draft_range(history, draft, {{measure_id_t{11}, 0}, {measure_id_t{12}, 959}}, 4);
    CHECK((draft.value->payload.candidate.range.begin == local_position_t{measure_id_t{11}, 0}));
    CHECK((draft.value->payload.candidate.range.end == local_position_t{measure_id_t{12}, 960}));
    const auto chart_end_preview = preview_sv_recipe_draft(history, *draft.value);
    REQUIRE(chart_end_preview);
    REQUIRE_FALSE(chart_end_preview.candidate_points.points.empty());
    CHECK(chart_end_preview.candidate_points.points.back().absolute_row == 1919);

    const auto before_failed_range = draft.value->payload.candidate.range;
    const auto failed_range = draft.value->revise(history, {},
        position_span_t{{measure_id_t{11}, 0}, {measure_id_t{11}, 0}}, 4);
    CHECK_FALSE(failed_range.value);
    CHECK(draft.value->payload.candidate.range == before_failed_range);
    CHECK(history.document.revision == revision);

    auto uneven_song = make_recipe_song();
    uneven_song.charts.front().measures.back().definition = {{7, 8}, meter_t{7, 8}};
    auto uneven_history = document_history_t::from(std::move(uneven_song));
    auto uneven_draft =
        checked_draft(sv_recipe_draft_t::propose(uneven_history, chart_id_t{10},
                              make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {2, 1})));
    revise_draft_range(uneven_history, uneven_draft, {{measure_id_t{11}, 0}, {measure_id_t{12}, 840}}, 4);
    CHECK((uneven_draft.value->payload.candidate.range.end == local_position_t{measure_id_t{12}, 840}));
    revise_draft_range(uneven_history, uneven_draft, {{measure_id_t{11}, 0}, {measure_id_t{12}, 838}}, 4);
    CHECK((uneven_draft.value->payload.candidate.range.end == local_position_t{measure_id_t{12}, 840}));
    const auto uneven_preview = preview_sv_recipe_draft(uneven_history, *uneven_draft.value);
    REQUIRE(uneven_preview);
    REQUIRE_FALSE(uneven_preview.candidate_points.points.empty());
    CHECK(uneven_preview.candidate_points.points.back().absolute_row == 1799);
}

TEST_CASE("SV recipe drafts reject invalid candidates and stale revisions without publishing",
          "[unit][sv-recipe][draft][negative]") {
    auto history = document_history_t::from(make_recipe_song());
    auto invalid = make_constant_recipe({}, measure_id_t{11}, measure_id_t{11}, {2, 1});
    auto draft = checked_draft(sv_recipe_draft_t::propose(history, chart_id_t{10}, invalid));
    REQUIRE(draft.value->payload.lifecycle.status() == proposal_status_t::active);
    const auto revision = history.document.revision;
    CHECK_FALSE(preview_sv_recipe_draft(history, *draft.value));
    const auto rejected_apply = apply_proposal(history, *draft.value);
    CHECK_FALSE(rejected_apply);
    CHECK(draft.value->payload.lifecycle.status() == proposal_status_t::active);
    CHECK(history.document.revision == revision);
    CHECK(history.document.charts.front().sv_recipes.empty());

    auto valid = make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {2, 1});
    revise_draft(history, draft, valid);
    REQUIRE(preview_sv_recipe_draft(history, *draft.value));
    REQUIRE(apply_proposal(history, *draft.value));
    CHECK(history.document.charts.front().sv_recipes.size() == 1);

    auto stale = checked_draft(sv_recipe_draft_t::propose(history, chart_id_t{10}, valid));
    REQUIRE(stale.value->payload.lifecycle.status() == proposal_status_t::active);
    auto proposed = history.document;
    proposed.metadata.title = "revision change";
    REQUIRE(commit_document_command(history, std::move(proposed), "Unrelated edit"));
    const auto after_external_edit = history.document.revision;
    CHECK_FALSE(preview_sv_recipe_draft(history, *stale.value));
    const auto expired = apply_proposal(history, *stale.value);
    CHECK_FALSE(expired);
    test::check_stale_action_message(expired.diagnostics, "SV recipe draft");
    CHECK(stale.value->payload.lifecycle.status() == proposal_status_t::cancelled);
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
    require_export_bake(baked);
    for (std::size_t index = 0; index < after.scrolls.size(); ++index) {
        CHECK(after.scrolls[index].position == baked.value->scrolls[index].position);
        CHECK(after.scrolls[index].multiplier == baked.value->scrolls[index].multiplier);
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
    check_export_bake_rejection(bake_sv_recipes_for_export(chart));

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

TEST_CASE("SV draft stamps keep equal-revision project replacement editable",
          "[unit][sv-recipe][proposal-stamp]") {
    auto history = document_history_t::from(make_recipe_song());
    auto candidate = make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {2, 1});
    auto draft = checked_draft(sv_recipe_draft_t::propose(history, chart_id_t{10}, candidate));
    CHECK(draft.value->stamp == chart_stamp_t::from(history.document, chart_id_t{10}));
    history.document.id = song_id_t{99};
    revise_draft(history, draft, candidate);
    CHECK(apply_proposal(history, *draft.value).applied);
    CHECK(draft.value->payload.lifecycle.status() == proposal_status_t::applied);
    CHECK(history.undo.size() == 1);
}

TEST_CASE("SV range stamps keep equal-revision project replacement draggable",
          "[unit][sv-recipe][proposal-stamp]") {
    auto song = make_recipe_song();
    song.charts.front().sv_recipes.push_back(
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1}));
    auto history = document_history_t::from(std::move(song));
    auto drag = make_range_proposal(history, sv_recipe_range_handle_t::begin, {measure_id_t{11}, 17});
    CHECK(drag.value->stamp == chart_stamp_t::from(history.document, chart_id_t{10}));
    history.document.id = song_id_t{99};
    const auto drag_revised = drag.value->revise(history, {}, local_position_t{measure_id_t{11}, 370});
    REQUIRE(drag_revised.value);
    CHECK(apply_proposal(history, *drag.value).applied);
    CHECK(drag.value->payload.lifecycle.status() == proposal_status_t::applied);
    CHECK(history.undo.size() == 1);
}

TEST_CASE("SV draft Apply keeps binding diagnostics and lock priority",
          "[unit][sv-recipe][proposal-stamp]") {
    auto history = document_history_t::from(make_recipe_song());
    auto draft = checked_draft(sv_recipe_draft_t::propose(history, chart_id_t{10},
        make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {2, 1})));
    ++history.document.revision;
    const auto result = apply_proposal(history, *draft.value, proposal_application_input_t{.is_timing_locked = true});
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().code == diagnostic_code_t::invalid_binding);
    CHECK(result.diagnostics.front().message == "SV recipe draft was cancelled by timing lock");
    CHECK(draft.value->payload.lifecycle.status() == proposal_status_t::cancelled);
    CHECK(history.undo.empty());
}

TEST_CASE("SV range Apply keeps captured-stale binding diagnostics",
          "[unit][sv-recipe][proposal-stamp]") {
    auto song = make_recipe_song();
    song.charts.front().sv_recipes.push_back(
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1}));
    auto history = document_history_t::from(std::move(song));
    auto drag = make_range_proposal(history, sv_recipe_range_handle_t::begin, {measure_id_t{11}, 17});
    ++history.document.revision;
    const auto result = apply_proposal(history, *drag.value);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().code == diagnostic_code_t::invalid_binding);
    test::check_stale_action_message(result.diagnostics, "SV recipe range drag");
    CHECK(drag.value->payload.lifecycle.status() == proposal_status_t::rejected);
    CHECK(history.undo.empty());
}

TEST_CASE("SV range failed revisions retain the last valid applicable range",
          "[unit][sv-recipe][interaction][proposal-stamp]") {
    auto song = make_recipe_song();
    song.charts.front().sv_recipes.push_back(
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1}));
    auto history = document_history_t::from(std::move(song));
    auto drag = make_range_proposal(history, sv_recipe_range_handle_t::begin, {measure_id_t{11}, 0});
    const auto valid = drag.value->revise(history, {}, local_position_t{measure_id_t{11}, 370});
    REQUIRE(valid.value);
    const auto retained = drag.value->payload.preview_range;
    const auto rejected = drag.value->revise(history, {}, local_position_t{measure_id_t{12}, 0});
    check_retained_range(history, *drag.value, rejected, retained);
    REQUIRE(apply_proposal(history, *drag.value));
    CHECK(history.document.charts.front().sv_recipes.front().range == retained);
    REQUIRE(history.undo.size() == 1);
    CHECK(history.undo.front().label == "Move SV recipe range");
}

TEST_CASE("SV draft failed builds expose diagnostics without an applicable proposal",
          "[unit][sv-recipe][proposal-stamp]") {
    const auto history = document_history_t::from(make_recipe_song());
    const auto candidate = make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {2, 1});
    const auto locked = sv_recipe_draft_t::propose(history, chart_id_t{10}, candidate, true);
    CHECK_FALSE(locked.value);
    REQUIRE(locked.diagnostics.size() == 1);
    CHECK(locked.diagnostics.front().message == "SV recipe editing is unavailable while timing is locked");
    const auto missing = sv_recipe_draft_t::propose(history, chart_id_t{10}, recipe_id_t{99});
    CHECK_FALSE(missing.value);
    REQUIRE(missing.diagnostics.size() == 1);
    CHECK(missing.diagnostics.front().message == "SV recipe draft target is missing or ambiguous");
}

TEST_CASE("SV draft range revisions transfer existing string storage",
          "[unit][sv-recipe][proposal-stamp]") {
    const auto history = document_history_t::from(make_recipe_song());
    auto candidate = make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {2, 1});
    candidate.name.assign(512, 'n');
    auto initial = checked_draft(sv_recipe_draft_t::propose(history, chart_id_t{10}, candidate));
    const auto* name_storage = initial.value->payload.candidate.name.data();
    const auto revised = initial.value->revise(history, {},
        position_span_t{{measure_id_t{11}, 17}, {measure_id_t{12}, 451}}, 4);
    REQUIRE(revised.value);
    CHECK(revised.value->payload.candidate.name == candidate.name);
    CHECK(revised.value->payload.candidate.name.data() == name_storage);
    CHECK((revised.value->payload.candidate.range.end == local_position_t{measure_id_t{12}, 480}));
    CHECK(history.undo.empty());
}

TEST_CASE("SV draft inactive views borrow the latest outcome diagnostics",
          "[unit][sv-recipe][proposal-stamp]") {
    auto history = document_history_t::from(make_recipe_song());
    auto candidate = make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {2, 1});
    candidate.output = sv_recipe_output_t::speeds;
    auto draft = checked_draft(sv_recipe_draft_t::propose(history, chart_id_t{10}, candidate));
    const auto blocked = apply_proposal(history, *draft.value, proposal_application_input_t{.is_timing_locked = true});
    REQUIRE(blocked.diagnostics.size() == 1);
    CHECK(draft.value->payload.lifecycle.status() == proposal_status_t::cancelled);
    const auto preview = preview_sv_recipe_draft(history, *draft.value, blocked.diagnostics);
    CHECK(preview.diagnostics == blocked.diagnostics);
    CHECK(preview.effective_output.output == sv_recipe_output_t::speeds);
    const auto repeated = apply_proposal(history, *draft.value, proposal_application_input_t{.diagnostics = blocked.diagnostics});
    CHECK(repeated.diagnostics == blocked.diagnostics);
    CHECK(history.undo.empty());
}

TEST_CASE("SV preparation reads an already staged transaction document",
          "[unit][sv-recipe][proposal-stamp]") {
    auto history = document_history_t::from(make_recipe_song());
    auto transaction = document_command_transaction_t::from(history);
    CHECK(&transaction.current_document() == &history.document);
    auto& staged = transaction.document();
    CHECK(&transaction.current_document() == &staged);
    staged.charts.front().sv_recipes.push_back(
        make_constant_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {2, 1}));
    const auto applied = apply_sv_recipe_edit(transaction,
        insert_edit_t<sv_recipe_t>{.chart = chart_id_t{10},
            .value = make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {3, 1})});
    REQUIRE(applied);
    REQUIRE(history.document.charts.front().sv_recipes.size() == 2);
    CHECK(history.document.charts.front().sv_recipes.back().id == recipe_id_t{21});
    CHECK(history.undo.size() == 1);
}

TEST_CASE("SV shared samples retain tolerance and strict legacy warning rejection", "[unit][sv-recipe][results]") {
    sv_recipe_sample_outcome_t samples{.value = {.tolerance_met = false},
        .diagnostics = {{.severity = diagnostic_severity_t::warning,
            .code = diagnostic_code_t::unsupported_feature, .message = "warning"}}};
    CHECK(samples.ok());
    const auto sampled = sv_recipe_sample_result_t::from(std::move(samples));
    CHECK_FALSE(sampled);
    CHECK_FALSE(sampled.tolerance_met);
}

TEST_CASE("SV shared compilation retains output and strict legacy warning rejection", "[unit][sv-recipe][results]") {
    sv_recipe_compilation_outcome_t compilation{.value = {.output = sv_recipe_output_t::speeds},
        .diagnostics = {{.severity = diagnostic_severity_t::warning,
            .code = diagnostic_code_t::unsupported_feature, .message = "warning"}}};
    CHECK(compilation.ok());
    auto compiled = sv_recipe_compilation_t::from(std::move(compilation));
    CHECK_FALSE(compiled);
    const auto restored = std::move(compiled).into_outcome();
    CHECK(restored.ok());
    CHECK(restored.value.output == sv_recipe_output_t::speeds);
}

TEST_CASE("SV shared export bake retains chart and strict legacy warning rejection", "[unit][sv-recipe][results]") {
    sv_recipe_export_bake_outcome_t bake{.value = make_recipe_chart(),
        .diagnostics = {{.severity = diagnostic_severity_t::warning,
            .code = diagnostic_code_t::unsupported_feature, .message = "warning"}}};
    CHECK(bake.ok());
    const auto baked = std::move(bake);
    check_export_bake_rejection(baked);
    CHECK(baked.value.has_value());
}

TEST_CASE("SV primary sample factory retains exact curve points", "[unit][sv-recipe][results]") {
    const auto chart = make_recipe_chart();
    const auto recipe = make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {3, 1});
    const auto samples = sv_recipe_samples_t::from(chart, recipe);
    REQUIRE(samples.ok());
    REQUIRE(samples.value.points.size() == 3);
    CHECK(samples.value.points[1].value == Catch::Approx(2.0));
}

TEST_CASE("SV primary compilation factory retains the output family", "[unit][sv-recipe][results]") {
    const auto compilation = sv_recipe_compiled_output_t::from(make_recipe_chart(), sv_recipe_output_t::sv);
    REQUIRE(compilation.ok());
    CHECK(compilation.value.output == sv_recipe_output_t::sv);
}

TEST_CASE("SV primary bake factory publishes a chart without recipes", "[unit][sv-recipe][results]") {
    const auto bake = bake_sv_recipes_for_export(make_recipe_chart());
    REQUIRE(bake.ok());
    REQUIRE(bake.value.has_value());
    CHECK(bake.value->sv_recipes.empty());
}

TEST_CASE("SV draft no-op application completes without an Undo record", "[unit][sv-recipe][proposal]") {
    auto song = make_recipe_song();
    song.charts.front().sv_recipes.push_back(
        make_constant_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {2, 1}));
    auto history = document_history_t::from(std::move(song));
    auto pending = checked_draft(sv_recipe_draft_t::propose(history, chart_id_t{10}, recipe_id_t{20}));
    const auto result = apply_proposal(history, *pending.value);
    CHECK_FALSE(result.applied);
    CHECK(result.diagnostics.empty());
    CHECK(pending.value->payload.lifecycle.status() == proposal_status_t::applied);
    CHECK(history.undo.empty());
}

TEST_CASE("SV draft failed range revisions reuse diagnostics and keep the capture active",
          "[unit][sv-recipe][proposal]") {
    auto history = document_history_t::from(make_recipe_song());
    auto pending = checked_draft(sv_recipe_draft_t::propose(history, chart_id_t{10},
        make_constant_recipe({}, measure_id_t{11}, measure_id_t{12}, {2, 1})));
    diagnostic_list_t diagnostics;
    diagnostics.reserve(4);
    diagnostics.push_back(error(diagnostic_code_t::invalid_hold, "Previous range error"));
    const auto* storage = diagnostics.data();
    const auto revised = pending.value->revise(history, {.diagnostics = std::move(diagnostics)},
        position_span_t{{measure_id_t{11}, 0}, {measure_id_t{11}, 0}}, 4);
    CHECK_FALSE(revised.value);
    REQUIRE(revised.diagnostics.size() == 1);
    CHECK(revised.diagnostics.data() == storage);
    CHECK(pending.value->payload.lifecycle.is_active());
    CHECK(history.undo.empty());
}

TEST_CASE("SV range unchanged captures cancel before target validation", "[unit][sv-recipe][proposal]") {
    auto song = make_recipe_song();
    song.charts.front().sv_recipes.push_back(
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1}));
    auto history = document_history_t::from(std::move(song));
    auto pending = make_range_proposal(history, sv_recipe_range_handle_t::end, {measure_id_t{12}, 0});
    history.document.charts.clear();
    const auto result = apply_proposal(history, *pending.value);
    CHECK_FALSE(result.applied);
    CHECK(result.diagnostics.empty());
    CHECK(pending.value->payload.lifecycle.status() == proposal_status_t::cancelled);
    CHECK(history.undo.empty());
}

TEST_CASE("SV range changed captures reject a numeric no-op", "[unit][sv-recipe][proposal]") {
    auto song = make_recipe_song();
    song.charts.front().sv_recipes.push_back(
        make_linear_recipe(recipe_id_t{20}, measure_id_t{11}, measure_id_t{12}, {1, 1}, {2, 1}));
    auto history = document_history_t::from(std::move(song));
    auto pending = make_range_proposal(history, sv_recipe_range_handle_t::end, {measure_id_t{12}, 0});
    auto revised = pending.value->revise(history, {}, local_position_t{measure_id_t{12}, 240});
    REQUIRE(revised.value);
    pending = std::move(revised);
    history.document.charts.front().sv_recipes.front().range = pending.value->payload.preview_range;
    const auto result = apply_proposal(history, *pending.value);
    CHECK_FALSE(result.applied);
    CHECK(result.diagnostics.empty());
    CHECK(pending.value->payload.lifecycle.status() == proposal_status_t::rejected);
    CHECK(history.undo.empty());
}

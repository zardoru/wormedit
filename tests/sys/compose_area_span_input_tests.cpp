// Kiai, preview, SV recipe and speed-span input and projection.
#include "frame_timing_fixtures.h"
#include "../support/stale_action_assertions.h"
#include "compose_area_dispatch_fixture.h"
#include "sys/app/application_defaults.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

namespace {
auto kiai_test_song() -> song_document_t {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    const auto measure = chart.measures.front().id;
    chart.kiai = {{.id = object_id_t{9001}, .position = {measure, 240}, .enabled = true,
                   .provenance = {.source_order = 1}},
                  {.id = object_id_t{9002}, .position = {measure, 720}, .enabled = false,
                   .provenance = {.source_order = 2}}};
    return song;
}

auto kiai_areas(compose_test_fixture_t& fixture) -> std::vector<kiai_span_t> {
    const auto& chart = fixture.history.document.charts.front();
    const auto built = measure_index_t::from(chart);
    return kiai_spans_of(chart, built.value());
}
} // namespace

TEST_CASE("Captured BMSON draft edits and auditions reject stale revisions independently of outer input",
          "[sys][bmson-draft-stamp]") {
    const auto request = GENERATE(
        std::pair{WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT, std::string_view{"BMSON region edit"}},
        std::pair{WORM_UI_COMPOSE_INPUT_BMSON_AUDITION_DRAFT, std::string_view{"BMSON region audition"}});
    compose_test_fixture_t fixture{kiai_test_song()};
    fixture.project();
    fixture.application.project.compose_gesture.bmson_marker_draft = application_command_state_t::bmson_marker_draft_proposal_t{
        .stamp = chart_stamp_t::from(fixture.history.document, fixture.interaction->chart),
        .payload = {.reference = object_id_t{42}, .markers = {bmson_sound_marker_insertion_t{}}}};
    ++fixture.application.project.compose_gesture.bmson_marker_draft->stamp.document.revision;
    auto action = kiai_input(fixture, request.first, 1);
    test::check_stale_action_message(dispatch_paste_action(fixture, action), request.second);
    expect_compose_test(fixture.history.undo.empty());
    expect_compose_test(fixture.application.project.compose_gesture.bmson_marker_draft.has_value());
}

TEST_CASE("Compose inner timing stamps reject stale selection and move requests",
          "[sys][timing-input-stamp]") {
    const auto request = GENERATE(
        std::pair{WORM_UI_COMPOSE_INPUT_TIMING_MARKER_SELECT, std::string_view{"Timing marker selection"}},
        std::pair{WORM_UI_COMPOSE_INPUT_TIMING_GROUP_MOVE, std::string_view{"Timing marker move"}},
        std::pair{WORM_UI_COMPOSE_INPUT_SPEED_SPAN_EDIT, std::string_view{"Speed edit"}},
        std::pair{WORM_UI_COMPOSE_INPUT_BGA_EVENT_CLICK, std::string_view{"BGA event selection"}},
        std::pair{WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE, std::string_view{"BGA event move"}},
        std::pair{WORM_UI_COMPOSE_INPUT_BOOKMARK_MOVE, std::string_view{"Bookmark move"}});
    compose_test_fixture_t fixture{kiai_test_song()};
    fixture.project();
    fixture.interaction->selected_objects = {{editor_selection_object_kind_t::bga_event, object_id_t{9001}}};
    auto action = kiai_input(fixture, request.first, 9001);
    ++action.input.compose_inputs[0].timing_document_revision;
    test::check_stale_action_message(dispatch_paste_action(fixture, action), request.second);
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Kiai edge drags edit the area, Delete removes the selected area and stale input is rejected",
          "[sys][kiai-spans]") {
    compose_test_fixture_t fixture{kiai_test_song()};
    fixture.application.project.compose_view.requested_snap_denominators.emplace(
        fixture.history.document.charts.front().id.value, 8);
    fixture.project();
    REQUIRE(kiai_areas(fixture).size() == 1);

    auto stale = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT, 9001, 1,
                            kiai_screen_y(fixture, 480));
    stale.input.compose_inputs[0].timing_document_revision += 1;
    test::check_stale_action_message(dispatch_paste_action(fixture, stale), "Kiai edit");
    CHECK(fixture.history.undo.empty());

    fixture.application.project.timing_recipe.timing_locked = true;
    auto locked = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT, 9001, 1,
                             kiai_screen_y(fixture, 480));
    CHECK(dispatch_paste_action(fixture, locked) == "Timing is locked.");
    fixture.application.project.timing_recipe.timing_locked = false;

    auto tail_drag = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT, 9001, 1,
                                kiai_screen_y(fixture, 480), kiai_screen_y(fixture, 720));
    CHECK(dispatch_paste_action(fixture, tail_drag) == "Kiai area edited.");
    auto areas = kiai_areas(fixture);
    REQUIRE(areas.size() == 1);
    CHECK(areas[0].begin_row == 240);
    CHECK(areas[0].end_row == 480);
    CHECK(areas[0].head == object_id_t{9001});
    CHECK(fixture.history.undo.size() == 1);

    // Dragging the head past the fixed tail swaps the edges.
    fixture.project();
    auto head_drag = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT, 9001, 0,
                                kiai_screen_y(fixture, 600), kiai_screen_y(fixture, 240));
    CHECK(dispatch_paste_action(fixture, head_drag) == "Kiai area edited.");
    areas = kiai_areas(fixture);
    REQUIRE(areas.size() == 1);
    CHECK(areas[0].begin_row == 480);
    CHECK(areas[0].end_row == 600);

    fixture.project();
    auto select = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_SELECT, 9001);
    CHECK(dispatch_paste_action(fixture, select) == "Kiai area selected.");
    CHECK(fixture.application.project.timing_recipe.kiai_selected_head == 9001);
    CHECK(invoke_compose_registered_command(fixture).value.is_invoked());
    CHECK(kiai_areas(fixture).empty());
    CHECK(fixture.application.project.timing_recipe.kiai_selected_head == 0);
    CHECK(fixture.history.undo.size() == 3);
}

namespace {
// One 960-row measure at 120 BPM: row 240 is 500 ms, row 480 is 1000 ms.
auto preview_test_song(std::optional<std::int64_t> length) -> song_document_t {
    auto song = compose_test_song();
    song.metadata.preview = song_preview_t{.start_milliseconds = 500, .length_milliseconds = length};
    return song;
}

auto preview_drag(compose_test_fixture_t& fixture, std::uint8_t edge, std::uint64_t from_row,
                  std::uint64_t to_row) -> std::string {
    auto action = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_PREVIEW_SPAN_EDIT, 0, edge,
                             kiai_screen_y(fixture, to_row), kiai_screen_y(fixture, from_row));
    return dispatch_paste_action(fixture, action);
}
} // namespace

TEST_CASE("A preview end drag snaps the length and is one undo step", "[sys][song-preview]") {
    compose_test_fixture_t fixture{preview_test_song(500)};
    project_with_eighth_snap(fixture);
    expect_compose_test(fixture.flattened.view.compose.preview_span.available);
    expect_compose_test(preview_drag(fixture, WORM_UI_SPAN_HANDLE_END, 480, 700) == "Preview edited.");
    expect_compose_test(fixture.history.document.metadata.preview ==
                        song_preview_t{.start_milliseconds = 500, .length_milliseconds = 1000});
    expect_compose_test(fixture.history.undo.size() == 1);
}

TEST_CASE("Dragging a point-only preview start moves the point without inventing a length",
          "[sys][song-preview]") {
    compose_test_fixture_t fixture{preview_test_song(std::nullopt)};
    project_with_eighth_snap(fixture);
    expect_compose_test(!fixture.flattened.view.compose.preview_span.has_length);
    preview_drag(fixture, WORM_UI_SPAN_HANDLE_BEGIN, 240, 470);
    expect_compose_test(fixture.history.document.metadata.preview == song_preview_t{.start_milliseconds = 1000});
}

TEST_CASE("A stale preview drag publishes nothing", "[sys][song-preview]") {
    compose_test_fixture_t fixture{preview_test_song(500)};
    project_with_eighth_snap(fixture);
    auto action = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_PREVIEW_SPAN_EDIT, 0, WORM_UI_SPAN_HANDLE_END,
                             kiai_screen_y(fixture, 700), kiai_screen_y(fixture, 480));
    ++action.input.compose_inputs[0].timing_document_revision;
    test::check_stale_action_message(dispatch_paste_action(fixture, action), "Preview edit");
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("A Kiai creation drag adds a merged area and is one undo step", "[sys][kiai-spans]") {
    compose_test_fixture_t fixture{kiai_test_song()};
    fixture.application.project.compose_view.requested_snap_denominators.emplace(
        fixture.history.document.charts.front().id.value, 8);
    fixture.project();
    auto create = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT, 0,
                             WORM_UI_SPAN_HANDLE_CREATE, kiai_screen_y(fixture, 120),
                             kiai_screen_y(fixture, 360));
    CHECK(dispatch_paste_action(fixture, create) == "Kiai area edited.");
    // 120..360 overlaps the existing 240..720 area: one merged area.
    const auto areas = kiai_areas(fixture);
    REQUIRE(areas.size() == 1);
    CHECK(areas[0].begin_row == 120);
    CHECK(areas[0].end_row == 720);
    CHECK(fixture.history.undo.size() == 1);
    REQUIRE(undo_document_command(fixture.history));
    CHECK(kiai_areas(fixture).front().begin_row == 240);
}

namespace {
auto recipe_range_rows(compose_test_fixture_t& fixture) -> std::pair<std::uint64_t, std::uint64_t> {
    const auto& range = fixture.history.document.charts.front().sv_recipes.front().range;
    return {range.begin.row, range.end.row};
}
} // namespace

TEST_CASE("SV recipe inputs select, resize and move the span, run commands and reject stale input",
          "[sys][recipe-areas]") {
    compose_test_fixture_t fixture{recipe_test_song()};
    fixture.project();
    REQUIRE(fixture.flattened.view.compose.recipe_areas.size() == 1);

    auto select = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_SELECT, 7001);
    CHECK(dispatch_paste_action(fixture, select) == "SV recipe selected.");
    CHECK(fixture.application.project.timing_recipe.recipe_selected == 7001);
    fixture.project();
    CHECK(fixture.flattened.view.compose.recipe_areas[0].selected);

    auto stale = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT, 7001, 1,
                            kiai_screen_y(fixture, 480));
    stale.input.compose_inputs[0].timing_document_revision += 1;
    test::check_stale_action_message(dispatch_paste_action(fixture, stale), "SV recipe edit");

    // End edge dragged to row 480; the span stays ordered when an edge crosses the other.
    auto end_drag = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT, 7001, 1,
                               kiai_screen_y(fixture, 480));
    end_drag.input.compose_inputs[0].span_anchor_y_pixels = kiai_screen_y(fixture, 720);
    CHECK(dispatch_paste_action(fixture, end_drag) == "SV recipe moved.");
    CHECK(recipe_range_rows(fixture) == std::pair<std::uint64_t, std::uint64_t>{240, 480});
    CHECK(fixture.history.undo.size() == 1);

    fixture.project();
    auto cross = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT, 7001, 0,
                            kiai_screen_y(fixture, 720));
    cross.input.compose_inputs[0].span_anchor_y_pixels = kiai_screen_y(fixture, 240);
    CHECK(dispatch_paste_action(fixture, cross) == "SV recipe moved.");
    CHECK(recipe_range_rows(fixture) == std::pair<std::uint64_t, std::uint64_t>{480, 720});

    // The body dragged by the pointer travel moves the whole span, keeping its length.
    fixture.project();
    auto body = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT, 7001, 2,
                           kiai_screen_y(fixture, 240));
    body.input.compose_inputs[0].span_anchor_y_pixels = kiai_screen_y(fixture, 480);
    CHECK(dispatch_paste_action(fixture, body) == "SV recipe moved.");
    CHECK(recipe_range_rows(fixture) == std::pair<std::uint64_t, std::uint64_t>{240, 480});

    // A locked timeline rejects edits and commands without a revision.
    fixture.project();
    fixture.application.project.timing_recipe.timing_locked = true;
    const auto revision = fixture.history.document.revision;
    auto locked_edit = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT, 7001, 1,
                                  kiai_screen_y(fixture, 720));
    locked_edit.input.compose_inputs[0].span_anchor_y_pixels = kiai_screen_y(fixture, 480);
    CHECK(dispatch_paste_action(fixture, locked_edit) != "SV recipe moved.");
    auto locked_command = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001, 0);
    CHECK(dispatch_paste_action(fixture, locked_command) == "Timing is locked.");
    CHECK(fixture.history.document.revision == revision);
    fixture.application.project.timing_recipe.timing_locked = false;

    auto toggle = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001, 0);
    CHECK(dispatch_paste_action(fixture, toggle) == "SV recipe disabled.");
    CHECK_FALSE(fixture.history.document.charts.front().sv_recipes.front().enabled);

    fixture.project();
    auto materialize = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001, 1);
    CHECK(dispatch_paste_action(fixture, materialize) == "SV recipe materialized into events.");
    CHECK(fixture.history.document.charts.front().sv_recipes.empty());
    CHECK_FALSE(fixture.history.document.charts.front().scrolls.empty());
    CHECK(fixture.application.project.timing_recipe.recipe_selected == 0);
    auto missing = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001, 2);
    CHECK(dispatch_paste_action(fixture, missing) == "The SV recipe no longer exists.");
}

TEST_CASE("Recipe duplication input publishes one adjacent copy and inspector entry",
          "[sys][recipe-areas]") {
    compose_test_fixture_t fixture{recipe_test_song()};
    fixture.project();
    auto duplicate = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001,
        static_cast<std::uint8_t>(ui::compose_recipe_command_t::duplicate));
    CHECK(dispatch_paste_action(fixture, duplicate) == "SV recipe duplicated.");
    REQUIRE(fixture.history.undo.size() == 1);
    fixture.project();
    const auto& entries = fixture.flattened.view.compose.recipe_entries;
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].id == recipe_id_t{7001});
    CHECK(entries[1].id == fixture.history.document.charts.front().sv_recipes.back().id);
    test::check_stale_action_message(dispatch_paste_action(fixture, duplicate), "Compose input");
}

TEST_CASE("Recipe order inputs project stored order and undo restores it",
          "[sys][recipe-areas]") {
    auto song = recipe_test_song();
    auto second = song.charts.front().sv_recipes.front();
    second.id = recipe_id_t{7002};
    song.charts.front().sv_recipes.push_back(second);
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    auto earlier = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7002,
        static_cast<std::uint8_t>(ui::compose_recipe_command_t::earlier));
    CHECK(dispatch_paste_action(fixture, earlier) == "SV recipe reordered.");
    fixture.project();
    const auto& entries = fixture.flattened.view.compose.recipe_entries;
    CHECK(entries == std::vector<ui::compose_recipe_entry_view_t>{
        {.id = recipe_id_t{7002}, .label = "1. rise"}, {.id = recipe_id_t{7001}, .label = "2. rise"}});
    REQUIRE(undo_document_command(fixture.history));
    CHECK(fixture.history.document.charts.front().sv_recipes.front().id == recipe_id_t{7001});
}

TEST_CASE("Recipe boundary and unknown inputs preserve the stored recipe list",
          "[sys][recipe-areas]") {
    compose_test_fixture_t fixture{recipe_test_song()};
    fixture.project();
    auto boundary = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001,
        static_cast<std::uint8_t>(ui::compose_recipe_command_t::earlier));
    CHECK(dispatch_paste_action(fixture, boundary) == "SV recipe is already at the order boundary.");
    auto unknown = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001, 255);
    CHECK(dispatch_paste_action(fixture, unknown) == "Unknown SV recipe command.");
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Area recipe creation projects a visible span for native and scoped BMS charts",
          "[sys][recipe-areas]") {
    const auto layout = GENERATE("osu-mania-7k", "bme-7k-outside-scratch");
    auto document = make_initial_document(song_id_t{991}, layout);
    REQUIRE(document);
    compose_test_fixture_t fixture{std::move(*document)};
    fixture.project();
    const auto chart_id = fixture.interaction->chart;
    const auto measure = fixture.history.read_chart(chart_id)->measures.front().id;
    fixture.interaction->retained_selection_range = position_span_t{{measure, 0}, {measure, 480}};
    wormedit::ui::workspace_action_t action{};
    action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE;
    action.stamp.chart_id = chart_id;
    action.stamp.document_revision = fixture.history.document.revision;
    const auto result = dispatch_compose_area_action(action, fixture.history, *fixture.interaction);
    REQUIRE(result);
    CHECK(*result == "SV recipe added over the selected area.");
    REQUIRE(fixture.history.read_chart(chart_id)->sv_recipes.size() == 1);
    fixture.project();
    REQUIRE(fixture.flattened.view.compose.recipe_areas.size() == 1);
    CHECK(fixture.flattened.view.compose.recipe_areas.front().bounds.bottom >
          fixture.flattened.view.compose.recipe_areas.front().bounds.top);
}

TEST_CASE("Delete removes the selected SV recipe and the area menu adds one over the retained area",
          "[sys][recipe-areas]") {
    compose_test_fixture_t fixture{recipe_test_song()};
    fixture.project();
    fixture.application.project.timing_recipe.recipe_selected = 7001;
    CHECK(invoke_compose_registered_command(fixture).value.is_invoked());
    CHECK(fixture.history.document.charts.front().sv_recipes.empty());
    CHECK(fixture.application.project.timing_recipe.recipe_selected == 0);
    REQUIRE(undo_document_command(fixture.history));
    CHECK(fixture.history.document.charts.front().sv_recipes.size() == 1);

    fixture.project();
    const auto measure = fixture.history.document.charts.front().measures.front().id;
    fixture.interaction->retained_selection_range =
        position_span_t{{measure, 0}, {measure, 240}};
    wormedit::ui::workspace_action_t action{};
    action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE;
    action.stamp.chart_id.value = fixture.history.document.charts.front().id.value;
    action.stamp.document_revision = fixture.history.document.revision;
    const auto status = dispatch_compose_area_action(action, fixture.history, *fixture.interaction);
    REQUIRE(status.has_value());
    CHECK(*status == "SV recipe added over the selected area.");
    const auto& recipes = fixture.history.document.charts.front().sv_recipes;
    REQUIRE(recipes.size() == 2);
    CHECK(recipes.back().range.begin.row == 0);
    CHECK(recipes.back().range.end.row == 240);
    CHECK(recipes.back().curve == sv_recipe_curve_t::linear);

    action.stamp.document_revision += 1;
    const auto stale = dispatch_compose_area_action(action, fixture.history, *fixture.interaction);
    REQUIRE(stale.has_value());
    CHECK(stale->find("ignored") != std::string::npos);
    CHECK(fixture.history.document.charts.front().sv_recipes.size() == 2);
}

TEST_CASE("Compose projects SPEEDS transitions as spans and names the transition on the marker",
          "[sys][speed-spans]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    const auto measure = chart.measures.front().id;
    chart.speeds = {{.id = object_id_t{8001},
                     .position = {measure, 0},
                     .multiplier = {2, 1},
                     .transition_beats = {2, 1},
                     .provenance = {.source_order = 0}},
                    {.id = object_id_t{8002},
                     .position = {measure, 720},
                     .multiplier = {1, 1},
                     .transition_beats = {1, 2},
                     .transition_is_seconds = true,
                     .provenance = {.source_order = 1}}};
    compose_test_fixture_t fixture{song};
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    REQUIRE(view.speed_spans.size() == 2);
    for (size_t index = 0; index < view.speed_spans.size(); ++index) {
        CHECK(view.speed_spans[index].bounds.bottom > view.speed_spans[index].bounds.top);
        CHECK(view.speed_spans[index].object_id.value != 0);
    }
    CHECK(view.speed_spans[0].multiplier == Catch::Approx(2.0));
    CHECK_FALSE(view.speed_spans[0].transition_is_seconds);
    CHECK(view.speed_spans[0].transition_length == Catch::Approx(2.0));
    CHECK(view.speed_spans[1].transition_is_seconds);
    bool named = false;
    for (size_t index = 0; index < view.timing_markers.size(); ++index)
        if (view.timing_markers[index].lane == wormedit::timing_lane_kind_t::speed &&
            std::string(view.timing_markers[index].detail).find("transition 2 beats") != std::string::npos)
            named = true;
    CHECK(named);
}

namespace {
// "rise" (7001), a later SV recipe sharing its last rows (7002), and the same rows as SPEEDS (7003).
auto overlapping_recipe_song() -> song_document_t {
    auto song = recipe_test_song();
    auto& chart = song.charts.front();
    auto overlapping = chart.sv_recipes.front();
    overlapping.id = recipe_id_t{7002};
    overlapping.range.begin.row = 600;
    auto other_family = overlapping;
    other_family.id = recipe_id_t{7003};
    other_family.output = sv_recipe_output_t::speeds;
    other_family.transition_duration = rational_t{0, 1};
    other_family.transition_is_seconds = false;
    chart.sv_recipes.push_back(overlapping);
    chart.sv_recipes.push_back(other_family);
    return song;
}
} // namespace

TEST_CASE("the selected recipe's entries mark recipes sharing its rows and output family",
          "[sys][recipe-areas][recipe-form]") {
    compose_test_fixture_t fixture{overlapping_recipe_song()};
    fixture.application.project.timing_recipe.recipe_selected = 7001;
    fixture.project();
    const auto& entries = fixture.flattened.view.compose.recipe_entries;
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].overlaps_selection);
    CHECK(entries[1].overlaps_selection);
    CHECK_FALSE(entries[2].overlaps_selection);
}

TEST_CASE("the selected recipe's form view carries its whole shape and the chart's grids",
          "[sys][recipe-areas][recipe-form]") {
    compose_test_fixture_t fixture{recipe_test_song()};
    fixture.application.project.timing_recipe.recipe_selected = 7001;
    fixture.project();
    const auto& form = fixture.flattened.view.compose.recipe_form;
    // Three points (1, 2, 3) step across the whole span; the end point lies past it.
    REQUIRE(form.shape_points.size() >= 2);
    CHECK(form.shape_points.front().position == 0.0);
    CHECK(form.shape_points.back().position == 1.0);
    CHECK(form.shape_points.back().value == 1.0);
    CHECK(form.shape_maximum == 2.0);
    const auto measure = fixture.history.document.charts.front().divisions_per_standard_measure;
    CHECK(std::ranges::any_of(form.spacing_options,
        [&](const auto& option) { return option.denominator * option.rows == measure; }));
}

TEST_CASE("the selected recipe's form and its unapplied preview reach the Compose view",
          "[sys][recipe-areas][recipe-form]") {
    compose_test_fixture_t fixture{recipe_test_song()};
    fixture.project();
    CHECK_FALSE(fixture.flattened.view.compose.recipe_form.visible);
    CHECK_FALSE(fixture.flattened.view.compose.recipe_areas[0].previewing);

    fixture.application.project.timing_recipe.recipe_selected = 7001;
    fixture.project();
    const auto& form_view = fixture.flattened.view.compose.recipe_form;
    REQUIRE(form_view.visible);
    CHECK(form_view.recipe_id == recipe_id_t{7001});
    CHECK(std::string(form_view.name) == "rise");
    CHECK(form_view.form.curve == wormedit::sv_recipe_curve_t::linear);
    CHECK(std::string(form_view.form.value_to.text) == "3");
    CHECK(form_view.status_text.empty());

    // A preview replaces that recipe's graph until the selection or the document moves on.
    const auto& chart = fixture.history.document.charts.front();
    auto form = wormedit::ui::timing_recipe_form_t::from(chart.sv_recipes.front());
    std::snprintf(form.value_to.text, sizeof(form.value_to.text), "9");
    fixture.application.project.timing_recipe.recipe_preview = wormedit::sys::recipe_form_preview_t::propose(
        fixture.history.document, chart, chart.sv_recipes.front(), form);
    fixture.project();
    CHECK(fixture.flattened.view.compose.recipe_areas[0].previewing);
    CHECK(fixture.flattened.view.compose.recipe_areas[0].graph_count >= 2);

    form.curve = wormedit::sv_recipe_curve_t::exponential;
    std::snprintf(form.shape.text, sizeof(form.shape.text), "99");
    fixture.application.project.timing_recipe.recipe_preview = wormedit::sys::recipe_form_preview_t::propose(
        fixture.history.document, chart, chart.sv_recipes.front(), form);
    fixture.project();
    REQUIRE(!fixture.flattened.view.compose.recipe_form.status_text.empty());
    CHECK_FALSE(std::string(fixture.flattened.view.compose.recipe_form.status_text).empty());

    fixture.application.project.timing_recipe.recipe_selected = 0;
    fixture.project();
    CHECK_FALSE(fixture.application.project.timing_recipe.recipe_preview.has_value());
    CHECK_FALSE(fixture.flattened.view.compose.recipe_form.visible);

    fixture.application.project.timing_recipe.recipe_selected = 7001;
    fixture.application.project.timing_recipe.recipe_preview = wormedit::sys::recipe_form_preview_t::propose(
        fixture.history.document, chart, chart.sv_recipes.front(), form);
    auto& preview = *fixture.application.project.timing_recipe.recipe_preview;
    auto expired = *preview.editor();
    expired.stamp.document.revision += 5;
    recipe_form_outcome_t rejected{std::move(expired)};
    rejected.diagnostics = std::move(preview.diagnostics);
    rejected.complete(false);
    preview = std::move(rejected);
    fixture.project();
    CHECK_FALSE(fixture.application.project.timing_recipe.recipe_preview.has_value());
}
#endif

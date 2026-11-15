// BMSON marker proposals, razor capture and Draw proposals.
#include "frame_timing_support.h"
#include <wormedit/formats/bmson.h>
#include "sys/compose/views/compose_projection_context.h"
#include "ui/field/color.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <optional>
#include <set>
#include <span>
#include <string_view>
#include <utility>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
struct bmson_marker_fixture_t {
    document_history_t history;
    editor_interaction_state_t state;
    object_id_t reference;

    static auto from() -> bmson_marker_fixture_t {
        constexpr std::string_view text = R"({"version":"1.0.0","info":{"title":"Markers","artist":"test","mode_hint":"beat-7k","resolution":240,"init_bpm":120},"lines":[{"y":0},{"y":960}],"sound_channels":[{"name":"stem.wav","notes":[{"x":0,"y":0,"c":false}]}]})";
        auto parsed = parse_bmson_bytes(std::as_bytes(std::span{text.data(), text.size()}));
        REQUIRE(parsed.value);
        bmson_marker_fixture_t fixture{.history = document_history_t::from(std::move(*parsed.value))};
        const auto& chart = fixture.history.document.charts.front();
        fixture.reference = chart.measures.front().bgm_events.front().id;
        fixture.state.chart = chart.id;
        fixture.state.selected_objects.push_back({editor_selection_object_kind_t::bgm_event,
                                                  fixture.reference});
        return fixture;
    }
};
} // namespace

TEST_CASE("Rejected BMSON marker construction allocates only its existing feedback string",
          "[sys][bmson-marker][proposal-shape]") {
    document_history_t history;
    editor_interaction_state_t state;
    const std::array markers{bmson_sound_marker_insertion_t{}};
    allocations = 0;
    count_allocations = true;
    auto rejected = application_command_state_t::bmson_marker_draft_t::propose(
        history, state, object_id_t{42}, markers);
    count_allocations = false;
    const auto counted = allocations;
    CHECK_FALSE(rejected.value);
    REQUIRE(rejected.diagnostics);
    CHECK(counted == 1);
    CHECK(rejected.diagnostics->message == "Select one BMSON BGM marker to choose its sound channel.");
    CHECK(history.undo.empty());
}

TEST_CASE("A rejected BMSON marker append keeps the prior valid batch and its storage",
          "[sys][bmson-marker][proposal-shape]") {
    auto fixture = bmson_marker_fixture_t::from();
    const auto measure = fixture.history.document.charts.front().measures.front().id;
    const std::array markers{bmson_sound_marker_insertion_t{{measure, 120}, false}};
    auto proposed = application_command_state_t::bmson_marker_draft_t::propose(
        fixture.history, fixture.state, fixture.reference, markers);
    REQUIRE(proposed.value);
    auto prior = std::move(proposed.value);
    const auto* storage = prior->payload.markers.data();
    const auto rejected = application_command_state_t::bmson_marker_draft_t::revise(
        fixture.history, fixture.state, prior, fixture.reference, markers);
    CHECK_FALSE(rejected.value);
    REQUIRE(rejected.diagnostics);
    CHECK(rejected.diagnostics->message == "That BMSON pulse is already staged.");
    CHECK(prior->payload.markers.size() == 1);
    CHECK(prior->payload.markers.data() == storage);
}

TEST_CASE("BMSON proposal adoption moves markers prepared by the existing native scratch check",
          "[sys][bmson-marker][proposal-shape]") {
    auto fixture = bmson_marker_fixture_t::from();
    const auto measure = fixture.history.document.charts.front().measures.front().id;
    const std::array markers{bmson_sound_marker_insertion_t{{measure, 120}, false}};
    auto proposed = application_command_state_t::bmson_marker_draft_t::propose(
        fixture.history, fixture.state, fixture.reference, markers);
    REQUIRE(proposed.value);
    auto prepared = proposed.value->payload.markers;
    prepared.front().restart = true;
    auto temporary = document_history_t::from(fixture.history.document, 1);
    REQUIRE(insert_bmson_sound_markers(temporary, proposed.value->stamp.chart,
                                     fixture.reference, prepared));
    const auto* storage = prepared.data();
    const auto revised = proposed.value->revise(std::move(prepared));
    REQUIRE(revised.value);
    CHECK(revised.value->payload.markers.data() == storage);
    CHECK(revised.value->payload.markers.front().restart);
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Razor capture quietly has no proposal when its marker prerequisite is absent",
          "[sys][bmson-razor][proposal-shape]") {
    document_history_t history;
    allocations = 0;
    count_allocations = true;
    const auto proposed = application_command_state_t::bmson_razor_stroke_t::propose(
        history, chart_id_t{1}, nullptr, object_id_t{42}, {8}, {measure_id_t{1}, 120});
    count_allocations = false;
    const auto counted = allocations;
    CHECK_FALSE(proposed.value);
    CHECK(proposed.diagnostics.empty());
    CHECK(counted == 0);
    CHECK(history.undo.empty());
}

TEST_CASE("Razor capture keeps revision-only marker identity and captures the current document",
          "[sys][bmson-razor][proposal-shape]") {
    auto fixture = bmson_marker_fixture_t::from();
    const local_position_t position{fixture.history.document.charts.front().measures.front().id, 120};
    const std::array markers{bmson_sound_marker_insertion_t{position, false}};
    auto proposed = application_command_state_t::bmson_marker_draft_t::propose(
        fixture.history, fixture.state, fixture.reference, markers);
    REQUIRE(proposed.value);
    fixture.history.document.id = song_id_t{99};
    const auto captured = application_command_state_t::bmson_razor_stroke_t::propose(
        fixture.history, fixture.state.chart, &*proposed.value, fixture.reference, {8}, position);
    REQUIRE(captured.value);
    CHECK(captured.value->stamp == chart_stamp_t::from(fixture.history.document, fixture.state.chart));
    CHECK(captured.value->payload.reference == fixture.reference);
    CHECK(captured.value->payload.snap == snap_grid_t{8});
    CHECK(captured.value->payload.last_position == position);
}

TEST_CASE("Razor revision adopts an acknowledged staged cell without allocating",
          "[sys][bmson-razor][proposal-shape]") {
    auto fixture = bmson_marker_fixture_t::from();
    const auto measure = fixture.history.document.charts.front().measures.front().id;
    const std::array markers{bmson_sound_marker_insertion_t{{measure, 120}, false},
                             bmson_sound_marker_insertion_t{{measure, 240}, false}};
    auto proposed = application_command_state_t::bmson_marker_draft_t::propose(
        fixture.history, fixture.state, fixture.reference, markers);
    REQUIRE(proposed.value);
    auto captured = application_command_state_t::bmson_razor_stroke_t::propose(
        fixture.history, fixture.state.chart, &*proposed.value, fixture.reference, {8}, {measure, 120});
    REQUIRE(captured.value);
    const auto stamp = captured.value->stamp;
    allocations = 0;
    count_allocations = true;
    const auto revised = captured.value->revise(local_position_t{measure, 240});
    count_allocations = false;
    const auto counted = allocations;
    REQUIRE(revised.value);
    CHECK(counted == 0);
    CHECK(revised.value->stamp == stamp);
    CHECK(revised.value->payload.last_position == local_position_t{measure, 240});
}

namespace {
auto make_draw_point_input(bool bgm = false) -> wormedit::ui::compose_input_action_t {
    wormedit::ui::compose_input_action_t input{};
    input.resource_role = bgm ? wormedit::resource_role_t::bgm : wormedit::resource_role_t::bga;
    input.paint_intent = 2;
    input.lane_id.value = 42;
    input.bga_lane_kind_valid = true;
    input.bga_lane_kind = static_cast<decltype(input.bga_lane_kind)>(visual_event_kind_t::layer);
    return input;
}

auto make_prepared_draw_stroke(const document_history_t& history,
    const editor_interaction_state_t& state, const wormedit::ui::compose_input_action_t& input)
    -> application_command_state_t::bga_draw_stroke_proposal_t {
    const auto& chart = history.document.charts.front();
    auto built = measure_index_t::from(chart);
    auto& geometry = built;
    REQUIRE(geometry);
    application_command_state_t::bga_draw_stroke_proposal_t initial{
        .stamp = chart_stamp_t::from(history.document, chart.id),
        .payload = {.erase = true, .geometry = std::move(*geometry)}};
    auto proposed = initial.revise(input, chart, state, 120, true);
    REQUIRE(proposed.value);
    return std::move(*proposed.value);
}
} // namespace

TEST_CASE("Draw proposal revision adds no allocations beyond the existing duplicate set insertion",
          "[sys][draw-proposal][proposal-shape]") {
    auto fixture = bmson_marker_fixture_t::from();
    fixture.state.active_snap_denominator = 8;
    fixture.state.snap_enabled = true;
    const auto input = make_draw_point_input();
    auto stroke = make_prepared_draw_stroke(fixture.history, fixture.state, input);
    const auto* geometry = stroke.payload.geometry.entries.data();
    const auto* targets = stroke.payload.targets.data();
    const auto kind = static_cast<uint8_t>(input.bga_lane_kind);
    std::set<std::pair<uint64_t, uint8_t>> control;
    control.emplace(uint64_t{120}, kind);
    allocations = 0;
    count_allocations = true;
    control.emplace(uint64_t{120}, kind);
    count_allocations = false;
    const auto previous_cost = allocations;
    allocations = 0;
    count_allocations = true;
    const auto revised = stroke.revise(input,
        fixture.history.document.charts.front(), fixture.state, 120, false);
    count_allocations = false;
    const auto current_cost = allocations;
    REQUIRE(revised.value);
    CHECK(current_cost == previous_cost);
    CHECK(revised.value->payload.geometry.entries.data() == geometry);
    CHECK(revised.value->payload.targets.data() == targets);
    CHECK(revised.value->payload.targets.size() == 1);
    CHECK(revised.value->payload.visited.size() == 1);
}

TEST_CASE("Draw proposal begins at the first cell and preserves snap interpolation",
          "[sys][draw-proposal][proposal-shape]") {
    auto fixture = bmson_marker_fixture_t::from();
    fixture.state.active_snap_denominator = 8;
    fixture.state.snap_enabled = true;
    const auto input = make_draw_point_input(true);
    auto stroke = make_prepared_draw_stroke(fixture.history, fixture.state, input);
    const auto* geometry = stroke.payload.geometry.entries.data();
    const auto revised = stroke.revise(input,
        fixture.history.document.charts.front(), fixture.state, 360, false);
    REQUIRE(revised.value);
    CHECK(revised.value->payload.targets.size() == 3);
    CHECK(revised.value->payload.targets.front().position.row == 120);
    CHECK(revised.value->payload.last_row == 360);
    CHECK(revised.value->payload.geometry.entries.data() == geometry);
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Draw proposal rejects a missing cell without an applicable value or allocation",
          "[sys][draw-proposal][proposal-shape]") {
    auto fixture = bmson_marker_fixture_t::from();
    fixture.state.active_snap_denominator = 8;
    fixture.state.snap_enabled = true;
    const auto input = make_draw_point_input();
    auto stroke = make_prepared_draw_stroke(fixture.history, fixture.state, input);
    allocations = 0;
    count_allocations = true;
    const auto rejected = stroke.revise(input,
        fixture.history.document.charts.front(), fixture.state, std::nullopt, false);
    count_allocations = false;
    const auto counted = allocations;
    CHECK_FALSE(rejected.value);
    CHECK(rejected.diagnostics.empty());
    CHECK(counted == 0);
    CHECK(fixture.history.undo.empty());
}

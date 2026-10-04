#include <wormedit/core/editor_scene.h>
#include <wormedit/core/interaction.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <variant>

namespace {

using namespace wormedit;

auto layered_bgm_chart() -> chart_document_t {
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.measures = {{.id = measure_id_t{1},
                       .definition = {{1, 1}, meter_t{4, 4}},
                       .bgm_events = {
                           {.id = object_id_t{20}, .position = {measure_id_t{1}, 120},
                            .source_lane = object_id_t{30},
                            .semantic_layers = {layer_id_t{7}}},
                           {.id = object_id_t{21}, .position = {measure_id_t{1}, 360},
                            .source_lane = object_id_t{30}},
                           {.id = object_id_t{22}, .position = {measure_id_t{1}, 600},
                            .source_lane = object_id_t{30},
                            .semantic_layers = {layer_id_t{9}}}}}};
    chart.bgm_source_lanes = {{.id = object_id_t{30}, .name = "BGM"}};
    chart.semantic_layers = {
        {.id = layer_id_t{7}, .name = "Verse", .color = {20, 80, 140, 190}},
        {.id = layer_id_t{8}, .name = "Chorus", .color = {150, 50, 40, 255}, .locked = true},
        {.id = layer_id_t{9}, .name = "Hidden", .color = {5, 6, 7, 255}, .visible = false},
    };
    chart.lane_layouts = {{.id = object_id_t{40},
                           .position = {measure_id_t{1}, 0},
                           .layout = {.profile_id = "layers",
                                      .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}};
    return chart;
}

auto interaction_song() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    auto chart = layered_bgm_chart();
    chart.measures.front().notes = {{.id = object_id_t{10},
                                     .position = {measure_id_t{1}, 0},
                                     .lane = lane_id_t{1}, .kind = note_kind_t::tap}};
    song.charts.push_back(std::move(chart));
    return song;
}

auto scene_view() -> editor_interaction_view_model_t {
    return {.chart = chart_id_t{1},
            .active_lane = lane_id_t{1},
            .active_lane_name = "Key",
            .cursor = {measure_id_t{1}, 0},
            .active_snap_denominator = 4,
            .supported_snap_denominators = {4},
            .document_revision = 1,
            .pixels_per_standard_measure = 192.0,
            .viewport_height_pixels = 192.0};
}

auto scene_index(const chart_document_t& chart) -> editor_field_scene_index_t {
    const auto built = build_editor_field_scene_index(chart, 1);
    REQUIRE(std::holds_alternative<editor_field_scene_index_t>(built));
    return std::get<editor_field_scene_index_t>(built);
}

auto make_state(document_history_t& history) -> editor_interaction_state_t {
    constexpr std::array snaps{std::uint64_t{4}};
    auto result = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
                                          {measure_id_t{1}, 0}, snaps, 4);
    REQUIRE(result);
    return std::move(*result.state);
}

auto begin_bgm_paint(document_history_t& history, editor_interaction_state_t& state,
                     layer_id_t layer, object_id_t event) -> editor_interaction_result_t {
    return dispatch_editor_action(
        history, state,
        begin_semantic_layer_paint_gesture_action_t{
            .layer = layer, .note = object_id_t{}, .pointer_captured = true,
            .bgm_event = event});
}

auto update_bgm_paint(document_history_t& history, editor_interaction_state_t& state,
                      object_id_t event) -> editor_interaction_result_t {
    return dispatch_editor_action(
        history, state,
        update_semantic_layer_paint_gesture_action_t{
            .note = object_id_t{}, .pointer_captured = true, .bgm_event = event});
}

auto end_bgm_paint(document_history_t& history, editor_interaction_state_t& state)
    -> editor_interaction_result_t {
    return dispatch_editor_action(
        history, state,
        end_semantic_layer_paint_gesture_action_t{.note = object_id_t{}, .pointer_captured = true});
}

} // namespace

TEST_CASE("BGM scene geometry carries a visible semantic color",
          "[unit][core][editor-scene][semantic-layer][bgm]") {
    const auto chart = layered_bgm_chart();
    const auto index = scene_index(chart);
    REQUIRE(index.bgm_events.size() == 3);
    CHECK(index.bgm_events[0].semantic_color == color_rgba_t{20, 80, 140, 190});
    CHECK_FALSE(index.bgm_events[1].semantic_color);
    CHECK_FALSE(index.bgm_events[2].semantic_color);
}

TEST_CASE("BGM scene geometry obeys solo, required, excluded, and hidden-layer filters",
          "[unit][core][editor-scene][semantic-layer][bgm]") {
    const auto chart = layered_bgm_chart();
    const auto index = scene_index(chart);
    const editor_field_scene_request_t base{
        .field_left_pixels = 0.0, .field_width_pixels = 192.0,
        .bgm_pane_left_pixels = 192.0, .bgm_pane_width_pixels = 36.0};
    const auto compose = [&](editor_field_scene_request_t request) {
        return make_editor_field_scene(chart, index, scene_view(), request);
    };
    auto request = base;
    request.solo_semantic_layer = layer_id_t{7};
    auto result = compose(request);
    REQUIRE(result);
    REQUIRE(result.scene->bgm_events.size() == 1);
    CHECK(result.scene->bgm_events.front().event == object_id_t{20});
}

TEST_CASE("BGM scene requires and excludes semantic membership consistently",
          "[unit][core][editor-scene][semantic-layer][bgm]") {
    const auto chart = layered_bgm_chart();
    const auto index = scene_index(chart);
    auto request = editor_field_scene_request_t{
        .field_left_pixels = 0.0, .field_width_pixels = 192.0,
        .bgm_pane_left_pixels = 192.0, .bgm_pane_width_pixels = 36.0};
    request.required_semantic_layers = {layer_id_t{7}};
    request.excluded_semantic_layers = {layer_id_t{8}};
    const auto result = make_editor_field_scene(chart, index, scene_view(), request);
    REQUIRE(result);
    REQUIRE(result.scene->bgm_events.size() == 1);
    CHECK(result.scene->bgm_events.front().event == object_id_t{20});
}

TEST_CASE("BGM-only paint toggles membership on a stroke and undoes as one command",
          "[unit][core][interaction][semantic-layer][paint][bgm]") {
    auto history = make_document_history(interaction_song());
    auto state = make_state(history);
    REQUIRE(begin_bgm_paint(history, state, layer_id_t{7}, object_id_t{20}));
    REQUIRE(update_bgm_paint(history, state, object_id_t{21}));
    REQUIRE(end_bgm_paint(history, state));
    CHECK(history.undo.size() == 1);
    CHECK(history.document.charts.front().measures.front().bgm_events[0].semantic_layers.empty());
    CHECK(history.document.charts.front().measures.front().bgm_events[1].semantic_layers ==
          std::vector{layer_id_t{7}});
}

TEST_CASE("BGM-only paint can restore the removed membership through one undoable reversal",
          "[unit][core][interaction][semantic-layer][paint][bgm]") {
    auto history = make_document_history(interaction_song());
    auto state = make_state(history);
    REQUIRE(begin_bgm_paint(history, state, layer_id_t{7}, object_id_t{20}));
    REQUIRE(end_bgm_paint(history, state));
    REQUIRE(history.document.charts.front().measures.front().bgm_events[0].semantic_layers.empty());
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures.front().bgm_events[0].semantic_layers ==
          std::vector{layer_id_t{7}});
}

TEST_CASE("BGM paint rejects a locked layer and cancels on pointer capture loss",
          "[unit][core][interaction][semantic-layer][paint][bgm]") {
    auto history = make_document_history(interaction_song());
    auto state = make_state(history);
    const auto locked = begin_bgm_paint(history, state, layer_id_t{8}, object_id_t{20});
    CHECK_FALSE(locked);
    REQUIRE(begin_bgm_paint(history, state, layer_id_t{7}, object_id_t{20}));
    const auto cancelled = dispatch_editor_action(
        history, state,
        update_semantic_layer_paint_gesture_action_t{
            .note = object_id_t{}, .pointer_captured = false, .bgm_event = object_id_t{21}});
    REQUIRE(cancelled);
    CHECK_FALSE(state.pending_semantic_layer_paint_gesture);
    CHECK(history.undo.empty());
}

TEST_CASE("BGM paint rejects a stale gesture after a document edit",
          "[unit][core][interaction][semantic-layer][paint][bgm]") {
    auto history = make_document_history(interaction_song());
    auto state = make_state(history);
    REQUIRE(begin_bgm_paint(history, state, layer_id_t{7}, object_id_t{20}));
    auto changed = history.document;
    changed.metadata.title = "External edit";
    REQUIRE(commit_document_command(history, std::move(changed), "External edit"));
    const auto stale = end_bgm_paint(history, state);
    CHECK_FALSE(stale);
    CHECK_FALSE(stale.diagnostics.empty());
    CHECK(history.document.charts.front().measures.front().bgm_events[0].semantic_layers ==
          std::vector{layer_id_t{7}});
}

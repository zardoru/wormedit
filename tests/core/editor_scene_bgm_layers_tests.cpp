#include <wormedit/core/editor_scene.h>
#include <wormedit/core/interaction.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
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
    auto result = editor_interaction_state_t::from(history, chart_id_t{1}, lane_id_t{1},
                                          {measure_id_t{1}, 0}, snaps, 4);
    REQUIRE(result);
    return std::move(*result.state);
}

auto begin_bgm_paint(document_history_t& history, editor_interaction_state_t& state,
                     layer_id_t layer, object_id_t event,
                     semantic_layer_paint_mode_t mode = semantic_layer_paint_mode_t::add)
    -> editor_interaction_result_t {
    return dispatch_editor_action(
        history, state,
        begin_semantic_layer_paint_gesture_action_t{
            .layer = layer, .note = object_id_t{}, .pointer_captured = true,
            .bgm_event = event, .mode = mode});
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
    return dispatch_editor_action(history, state,
                                  update_semantic_layer_paint_gesture_action_t{
                                      .note = object_id_t{},
                                      .pointer_captured = true,
                                      .phase = wormedit::editor_gesture_phase_t::commit});
}

auto bgm_width_chart() -> chart_document_t {
    auto chart = layered_bgm_chart();
    chart.bgm_source_lanes = {
        {.id = object_id_t{30}, .name = "Stem 1", .stem_channel = object_id_t{100}},
        {.id = object_id_t{31}, .name = "Packed"},
        {.id = object_id_t{32}, .name = "Stem 2", .stem_channel = object_id_t{101}}};
    chart.measures.front().bgm_events = {
        {.id = object_id_t{40}, .position = {measure_id_t{1}, 120},
         .source_lane = object_id_t{30}},
        {.id = object_id_t{41}, .position = {measure_id_t{1}, 240},
         .source_lane = object_id_t{31}},
        {.id = object_id_t{42}, .position = {measure_id_t{1}, 360},
         .source_lane = object_id_t{32}}};
    return chart;
}

auto bgm_width_scene(const chart_document_t& chart, const editor_field_scene_request_t& request)
    -> editor_field_scene_result_t {
    const auto index = scene_index(chart);
    return make_editor_field_scene(chart, index, scene_view(), request);
}

auto bgm_width_rules_are_consistent() -> bool {
    const auto chart = bgm_width_chart();
    editor_field_scene_request_t defaults{.field_left_pixels = 0, .field_width_pixels = 100,
        .bgm_pane_left_pixels = 100, .bgm_pane_width_pixels = 108};
    const auto implicit = bgm_width_scene(chart, defaults);
    defaults.bgm_lane_width_pixels = 40;
    defaults.bgm_pane_width_pixels = 120;
    const auto legacy = bgm_width_scene(chart, defaults);
    const std::array widths{300.0, 480.0, 96.0};
    defaults.bgm_lane_widths_pixels = widths;
    defaults.bgm_pane_width_pixels = 432;
    const auto custom = bgm_width_scene(chart, defaults);
    if (!implicit || !legacy || !custom || implicit.scene->bgm_lanes.size() != 3 ||
        legacy.scene->bgm_lanes.size() != 3 || custom.scene->bgm_lanes.size() != 3) return false;
    if (implicit.scene->bgm_lanes[2].bounds.right != 208 ||
        legacy.scene->bgm_lanes[2].bounds.right != 220 ||
        custom.scene->bgm_lanes[0].bounds.right != 400 ||
        custom.scene->bgm_lanes[1].bounds.right != 436 ||
        custom.scene->bgm_lanes[2].bounds.right != 532 ||
        editor_bgm_content_width_pixels(chart, defaults) != 432) return false;
    const std::array boundary_widths{20.0};
    defaults.bgm_lane_widths_pixels = boundary_widths;
    if (editor_bgm_lane_width_pixels(chart, defaults, 0) != 36 ||
        editor_bgm_lane_width_pixels(chart, defaults, 1) != 36 ||
        editor_bgm_lane_width_pixels(chart, defaults, 2) != 36) return false;
    const std::array nan_widths{std::numeric_limits<double>::quiet_NaN(), 1000.0, 96.0};
    defaults.bgm_lane_widths_pixels = nan_widths;
    return editor_bgm_lane_width_pixels(chart, defaults, 0) == 36 &&
        editor_bgm_lane_width_pixels(chart, defaults, 1) == 36 &&
        editor_bgm_lane_width_pixels(chart, defaults, 2) == 96;
}

auto bgm_widened_lane_keeps_unclipped_hit_bounds() -> bool {
    const auto chart = bgm_width_chart();
    const std::array widths{300.0, 480.0, 96.0};
    editor_field_scene_request_t request{.field_left_pixels = 0, .field_width_pixels = 100,
        .bgm_pane_left_pixels = 100, .bgm_pane_width_pixels = 100,
        .bgm_horizontal_scroll_pixels = 50};
    request.bgm_lane_widths_pixels = widths;
    const auto scene = bgm_width_scene(chart, request);
    if (!scene || scene.scene->bgm_lanes.empty() || scene.scene->bgm_events.empty()) return false;
    const auto& lane = scene.scene->bgm_lanes.front();
    const auto& event = scene.scene->bgm_events.front();
    return lane.bounds.left == 100 && lane.bounds.right == 200 &&
        lane.unclipped_left_pixels == 50 && lane.unclipped_right_pixels == 350 &&
        event.bounds.left == lane.bounds.left && event.bounds.right == lane.bounds.right;
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

namespace {
// Keep Catch assertion macro control flow out of the gesture scenario itself.
template <typename T> void require_layer_brush(const T& value) { REQUIRE(static_cast<bool>(value)); }
template <typename T> void check_layer_brush(const T& value) { CHECK(static_cast<bool>(value)); }
template <typename T> void check_no_layer_brush(const T& value) { CHECK_FALSE(static_cast<bool>(value)); }
} // namespace

TEST_CASE("BGM-only paint adds mixed membership on a stroke and undoes as one command",
          "[unit][core][interaction][semantic-layer][paint][bgm]") {
    auto history = document_history_t::from(interaction_song());
    auto state = make_state(history);
    require_layer_brush(begin_bgm_paint(history, state, layer_id_t{7}, object_id_t{20}));
    require_layer_brush(update_bgm_paint(history, state, object_id_t{21}));
    require_layer_brush(dispatch_editor_action(history, state,
        update_semantic_layer_paint_gesture_action_t{object_id_t{10}, true}));
    require_layer_brush(end_bgm_paint(history, state));
    check_layer_brush(history.undo.size() == 1);
    check_layer_brush(history.document.charts.front().measures.front().bgm_events[0].semantic_layers ==
          std::vector{layer_id_t{7}});
    check_layer_brush(history.document.charts.front().measures.front().bgm_events[1].semantic_layers ==
          std::vector{layer_id_t{7}});
    check_layer_brush(history.document.charts.front().measures.front().notes[0].semantic_layers ==
          std::vector{layer_id_t{7}});
    require_layer_brush(undo_document_command(history));
    check_layer_brush(history.document.charts.front().measures.front().notes[0].semantic_layers.empty());
    check_layer_brush(history.document.charts.front().measures.front().bgm_events[1].semantic_layers.empty());
}

TEST_CASE("BGM-only paint can restore the removed membership through one undoable reversal",
          "[unit][core][interaction][semantic-layer][paint][bgm]") {
    auto history = document_history_t::from(interaction_song());
    auto state = make_state(history);
    require_layer_brush(begin_bgm_paint(history, state, layer_id_t{7}, object_id_t{20},
                            semantic_layer_paint_mode_t::remove));
    require_layer_brush(update_bgm_paint(history, state, object_id_t{21}));
    require_layer_brush(end_bgm_paint(history, state));
    require_layer_brush(history.document.charts.front().measures.front().bgm_events[0].semantic_layers.empty());
    check_layer_brush(history.document.charts.front().measures.front().bgm_events[1].semantic_layers.empty());
    require_layer_brush(undo_document_command(history));
    check_layer_brush(history.document.charts.front().measures.front().bgm_events[0].semantic_layers ==
          std::vector{layer_id_t{7}});
}

TEST_CASE("BGM paint rejects a locked layer and cancels on pointer capture loss",
          "[unit][core][interaction][semantic-layer][paint][bgm]") {
    auto history = document_history_t::from(interaction_song());
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

TEST_CASE("BGM stem widths resolve clamps, packed defaults, and legacy scalar widths",
          "[unit][core][editor-scene][bgm-width]") {
    CHECK(bgm_width_rules_are_consistent());
}

TEST_CASE("BGM lane events clip while widened stem resize edges remain addressable",
          "[unit][core][editor-scene][bgm-width]") {
    CHECK(bgm_widened_lane_keeps_unclipped_hit_bounds());
}

TEST_CASE("BGM paint rejects a stale gesture after a document edit",
          "[unit][core][interaction][semantic-layer][paint][bgm]") {
    auto history = document_history_t::from(interaction_song());
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

TEST_CASE("BGM hold tails remain visible with offscreen heads through the bounded index",
          "[unit][core][editor-scene][bgm][t423-tail]") {
    auto chart = layered_bgm_chart();
    auto& events = chart.measures.front().bgm_events;
    events = {{.id = object_id_t{20}, .position = {measure_id_t{1}, 0},
               .source_lane = object_id_t{30}, .end = local_position_t{measure_id_t{1}, 900}}};
    const auto index = scene_index(chart);
    auto view = scene_view();
    view.viewport_scroll_pixels = 100;
    view.viewport_height_pixels = 64;
    view.selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{20}}};
    editor_field_scene_request_t request{
        .field_width_pixels = 192, .bgm_pane_left_pixels = 192, .bgm_pane_width_pixels = 36};
    const auto result = make_editor_field_scene(chart, index, view, request);
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    require_layer_brush(result);
    require_layer_brush(result.scene->bgm_events.size() == 1);
    const auto& geometry = result.scene->bgm_events.front();
    CHECK(geometry.has_tail);
    CHECK(geometry.selected);
    CHECK(geometry.head_bounds.bottom < 0);
    CHECK(geometry.bounds.bottom > view.viewport_height_pixels);
}

TEST_CASE("BGM interval queries skip large populations of expired heads",
          "[unit][core][editor-scene][bgm][t423-tail]") {
    auto chart = layered_bgm_chart();
    chart.divisions_per_standard_measure = 100000;
    auto& events = chart.measures.front().bgm_events;
    events.clear();
    for (std::uint64_t row = 0; row < 50000; ++row)
        events.push_back({.id = object_id_t{row + 100}, .position = {measure_id_t{1}, row},
                          .source_lane = object_id_t{30}});
    events.front().end = local_position_t{measure_id_t{1}, 99000};
    const auto index = scene_index(chart);
    auto view = scene_view();
    view.pixels_per_standard_measure = 1000;
    view.viewport_scroll_pixels = 900;
    view.viewport_height_pixels = 64;
    editor_field_scene_request_t request{
        .field_width_pixels = 192, .bgm_pane_left_pixels = 192, .bgm_pane_width_pixels = 36};
    request.maximum_composition_operations = 512;
    const auto result = make_editor_field_scene(chart, index, view, request);
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    require_layer_brush(result);
    require_layer_brush(result.scene->bgm_events.size() == 1);
    CHECK(result.scene->bgm_events.front().event == object_id_t{100});
}

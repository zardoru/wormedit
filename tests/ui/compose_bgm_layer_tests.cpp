#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

namespace {

auto button_event(bool down, float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    return event;
}

auto motion_event(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

auto click_at(nuklear_event_harness_t& harness, float x, float y)
    -> std::vector<workspace_action_snapshot_t> {
    const std::array<event_step_t, 2> script{{
        {.events = {motion_event(x, y), button_event(true, x, y)}},
        {.events = {motion_event(x, y), button_event(false, x, y)}}}};
    return harness.run_script(script);
}

} // namespace

TEST_CASE("Layers dock assigns a mixed note and BGM selection with separate counts",
          "[unit][frontend-state][event-harness][semantic-layer][bgm]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& state = worm_ui_nuklear_compose_state();
    state.inspector_visible = false;
    state.sounds_panel_visible = false;
    state.layers_panel_visible = true;
    WormUiComposeSemanticLayerView layer{
        .layer_id = 7, .name = "Verse", .red = 80,
        .assigned_note_count = 3, .visible = true,
        .assigned_bgm_count = 2};
    auto& view = harness.view().compose;
    view.semantic_layers = &layer;
    view.semantic_layer_count = 1;
    view.semantic_layer_total_count = 1;
    view.selected_note_count = 3;
    view.selected_bgm_event_count = 2;
    (void)harness.frame();
    REQUIRE(harness.drawn_text_center("Verse").has_value());
    const auto row = harness.drawn_text_center("3 notes · 2 BGM");
    REQUIRE(row.has_value());
    const auto clicked = click_at(harness, row->first, row->second);
    REQUIRE_FALSE(clicked.empty());
    CHECK(clicked.back().get().compose_action == WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER);
    CHECK(clicked.back().get().compose_semantic_layer_id == 7);
}

TEST_CASE("Layer painting can begin over a BGM event in its pane",
          "[unit][frontend-state][event-harness][semantic-layer][bgm][paint]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& state = worm_ui_nuklear_compose_state();
    state.inspector_visible = false;
    state.layers_panel_visible = false;
    state.sounds_panel_visible = false;
    auto& view = harness.view().compose;
    view.semantic_layer_paint_mode_active = true;
    view.semantic_layer_target = {.layer_id = 7, .name = "Verse", .visible = true};
    const std::array hover{motion_event(300, 400)};
    const auto pointer = harness.frame(hover);
    REQUIRE(pointer.get().compose_has_pointer_hover);
    const auto x = pointer.get().compose_pointer_hover_x_pixels;
    const auto y = pointer.get().compose_pointer_hover_y_pixels;
    WormUiComposeBgmEventView event{};
    event.bounds = {x - 48, y - 24, x + 48, y + 24};
    event.event_id = 42;
    event.source_lane_id = 1;
    view.bgm_events = &event;
    view.bgm_event_count = 1;
    const auto frames = click_at(harness, 300, 400);
    bool began{};
    for (const auto& frame : frames) {
        for (std::size_t index = 0; index < frame.get().compose_input_count; ++index) {
            began = began || frame.get().compose_inputs[index].kind ==
                                 WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN;
        }
    }
    CHECK(began);
}

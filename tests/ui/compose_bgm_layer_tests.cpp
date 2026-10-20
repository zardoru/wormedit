#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <source_location>

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

auto click_at(nuklear_event_harness_t& harness, float x, float y,
              SDL_Keymod modifiers = SDL_KMOD_NONE)
    -> std::vector<workspace_action_snapshot_t> {
    const std::array<event_step_t, 2> script{{
        {.events = {motion_event(x, y), button_event(true, x, y)}, .modifiers = modifiers},
        {.events = {motion_event(x, y), button_event(false, x, y)}, .modifiers = modifiers}}};
    return harness.run_script(script);
}

} // namespace

TEST_CASE("Layers dock assigns a mixed note and BGM selection with a combined object count",
          "[unit][frontend-state][event-harness][semantic-layer][bgm]") {
    nuklear_event_harness_t harness{1280, 900};
    REQUIRE(harness.ready());
    auto& state = worm_ui_nuklear_compose_state();
    state.dock_layout.inspector_visible = false;
    state.dock_layout.sounds_panel_visible = false;
    state.dock_layout.layers_panel_visible = true;
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
    const auto row = harness.drawn_text_center("5 objects");
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
    state.dock_layout.inspector_visible = false;
    state.dock_layout.layers_panel_visible = false;
    state.dock_layout.sounds_panel_visible = false;
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

namespace {
// Keep Catch assertion macro control flow out of the gesture scenario itself.
template <typename T> void require_layer_brush(const T& value) { REQUIRE(static_cast<bool>(value)); }
template <typename T> void check_layer_brush(const T& value,
    std::source_location where = std::source_location::current()) {
    INFO("gesture assertion at " << where.line());
    CHECK(static_cast<bool>(value));
}
template <typename T> void check_no_layer_brush(const T& value) { CHECK_FALSE(static_cast<bool>(value)); }
} // namespace

TEST_CASE("Alt Layer stroke emits remove intent and releases over BGM",
          "[unit][frontend-state][event-harness][semantic-layer][bgm][paint]") {
    nuklear_event_harness_t harness{1280, 900};
    require_layer_brush(harness.ready());
    auto& state = worm_ui_nuklear_compose_state();
    state.dock_layout.inspector_visible = state.dock_layout.layers_panel_visible = state.dock_layout.sounds_panel_visible = false;
    auto& view = harness.view().compose;
    view.semantic_layer_paint_mode_active = true;
    view.semantic_layer_target = {.layer_id = 7, .name = "Verse", .visible = true};
    const std::array hover{motion_event(300, 400)};
    const auto pointer = harness.frame(hover);
    WormUiComposeBgmEventView bgm{};
    bgm.bounds = {pointer.get().compose_pointer_hover_x_pixels - 48,
                  pointer.get().compose_pointer_hover_y_pixels - 24,
                  pointer.get().compose_pointer_hover_x_pixels + 48,
                  pointer.get().compose_pointer_hover_y_pixels + 24};
    bgm.event_id = 42;
    bgm.source_lane_id = 1;
    view.bgm_events = &bgm;
    view.bgm_event_count = 1;
    SDL_Event alt{};
    alt.type = SDL_EVENT_KEY_DOWN;
    alt.key.type = SDL_EVENT_KEY_DOWN;
    alt.key.key = SDLK_LALT;
    alt.key.scancode = SDL_SCANCODE_LALT;
    alt.key.mod = SDL_KMOD_ALT;
    alt.key.down = true;
    (void)harness.frame(std::array{alt}, SDL_KMOD_ALT);
    const auto frames = click_at(harness, 300, 400, SDL_KMOD_ALT);
    bool began{}, released{};
    for (const auto& frame : frames) {
        for (std::size_t index = 0; index < frame.get().compose_input_count; ++index) {
            const auto& input = frame.get().compose_inputs[index];
            if (input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_BEGIN) {
                began = true;
                check_layer_brush(input.paint_intent == 2);
            }
            released = released || input.kind == WORM_UI_COMPOSE_INPUT_SEMANTIC_LAYER_PAINT_RELEASE;
        }
    }
    check_layer_brush(began);
    check_layer_brush(released);
}

TEST_CASE("Layers row selects the brush target without assigning the selected objects",
          "[unit][frontend-state][event-harness][semantic-layer][paint]") {
    nuklear_event_harness_t harness{1280, 900};
    require_layer_brush(harness.ready());
    auto& state = worm_ui_nuklear_compose_state();
    state.dock_layout.inspector_visible = state.dock_layout.sounds_panel_visible = false;
    state.dock_layout.layers_panel_visible = true;
    WormUiComposeSemanticLayerView layer{.layer_id = 7, .name = "Verse", .visible = true};
    auto& view = harness.view().compose;
    view.semantic_layers = &layer;
    view.semantic_layer_count = view.semantic_layer_total_count = 1;
    view.selected_note_count = 1;
    view.semantic_layer_paint_mode_active = true;
    (void)harness.frame();
    const auto row = harness.drawn_text_center("Verse");
    require_layer_brush(row.has_value());
    const auto frames = click_at(harness, row->first, row->second);
    REQUIRE_FALSE(frames.empty());
    check_layer_brush(frames.back().get().compose_action == WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER);
    check_layer_brush(frames.back().get().compose_semantic_layer_id == 7);
}

namespace {
void initialize_chip(nuklear_event_harness_t& harness, uint8_t kind, uint64_t id,
                     const char* label) {
    auto& state = worm_ui_nuklear_compose_state();
    state.dock_layout.inspector_visible = state.dock_layout.layers_panel_visible = state.dock_layout.sounds_panel_visible = false;
    state.dock_layout.bga_panel_visible = false;
    auto& view = harness.view().compose;
    view.note_paint_mode_active = true;
    view.keysounds_available = true;
    view.draw_source_kind = kind;
    view.draw_source_id = id;
    view.draw_source_label = label;
}

auto click_chip(nuklear_event_harness_t& harness, const char* label)
    -> std::vector<workspace_action_snapshot_t> {
    (void)harness.frame();
    const auto source = harness.drawn_text_center(label);
    require_layer_brush(source.has_value());
    return click_at(harness, source->first, source->second);
}

void check_chip_inputs_empty(const std::vector<workspace_action_snapshot_t>& frames) {
    for (const auto& frame : frames) check_layer_brush(frame.get().compose_input_count == 0);
}
}

TEST_CASE("Draw chip opens a hidden Sounds dock and waits for the exact unrestricted projection",
          "[unit][event-harness][tool-chip]") {
    nuklear_event_harness_t harness{1280, 900};
    require_layer_brush(harness.ready());
    initialize_chip(harness, 1, 179, "0A 音楽.wav");
    auto& state = worm_ui_nuklear_compose_state();
    auto& view = harness.view().compose;
    state.keysound_catalog.keysound_search[0] = 'z';
    state.keysound_filters.keysound_slots_mode = true;
    view.keysound_section_projection_available = true;
    view.keysound_projected_primary_resource_id = 100;
    view.keysound_primary_display_index_valid = true;
    view.keysound_primary_display_index = 2;
    check_chip_inputs_empty(click_chip(harness, "0A 音楽.wav"));
    (void)harness.frame();
    WormUiKeysoundDockRequest request{};
    worm_ui_frontend_keysound_dock_request(&request);
    check_layer_brush(state.dock_layout.sounds_panel_visible && state.keysound_catalog.keysound_selection.primary == 179);
    check_layer_brush(request.primary_resource_id == 179 && !request.slots_mode);
    check_layer_brush(!request.automatic_search[0] && !request.group_filter_active);
    check_layer_brush(request.include_ungrouped && !request.layer_filter_kind);
    check_layer_brush(state.pointer_capture.source_chip_reveal_id == 179 && state.keysound_catalog.keysound_reveal_primary);
    view.keysound_projected_primary_resource_id = request.primary_resource_id;
    view.keysound_projected_filters_unrestricted = true;
    view.keysound_primary_display_index = 80;
    view.keysound_display_total_count = 81;
    (void)harness.frame();
    worm_ui_frontend_keysound_dock_request(&request);
    check_layer_brush(request.automatic_first_row > 8);
    check_layer_brush(state.pointer_capture.source_chip_reveal_id == 179 && state.keysound_catalog.keysound_reveal_primary);
    const uint64_t id{179};
    WormUiComposeKeysoundRowView row{.resource_id = id, .slot = "0A", .name = "音楽.wav"};
    WormUiComposeKeysoundDisplayRowView display{.kind = WORM_UI_COMPOSE_KEYSOUND_SOUND_ROW,
                                               .resource_id = id};
    view.keysounds = &row;
    view.keysound_count = 1;
    view.keysound_listed_resource_ids = &id;
    view.keysound_listed_resource_count = 1;
    view.keysound_display_rows = &display;
    view.keysound_display_row_count = 1;
    view.keysound_display_first_row = 80;
    (void)harness.frame();
    check_layer_brush(harness.drawn_text_center("音楽.wav").has_value());
    check_layer_brush(state.pointer_capture.source_chip_reveal_id == 0 && !state.keysound_catalog.keysound_reveal_primary);
}

TEST_CASE("Draw chip opens hidden BGA and retains reveal until its exact off-page resource arrives",
          "[unit][event-harness][tool-chip][bga]") {
    nuklear_event_harness_t harness{1280, 900};
    require_layer_brush(harness.ready());
    initialize_chip(harness, 2, 501, "501 preview image");
    auto& state = worm_ui_nuklear_compose_state();
    auto& view = harness.view().compose;
    state.bga_catalog.bga_search[0] = 'z';
    view.bga_search = "z";
    check_chip_inputs_empty(click_chip(harness, "501 preview image"));
    (void)harness.frame();
    WormUiBgaDockRequest request{};
    worm_ui_frontend_bga_dock_request(&request);
    check_layer_brush(state.dock_layout.bga_panel_visible && state.bga_catalog.bga_selection.primary == 501);
    check_layer_brush(!request.search[0] && state.pointer_capture.source_chip_reveal_id == 501);
    std::array<uint64_t, 80> ids{};
    for (std::size_t index = 0; index < ids.size(); ++index) ids[index] = index + 100;
    ids.back() = 501;
    view.bga_search = request.search;
    view.bga_matching_ids = ids.data();
    view.bga_total_matching = ids.size();
    (void)harness.frame();
    check_layer_brush(state.pointer_capture.source_chip_reveal_id == 501);
    WormUiComposeBgaRowView row{.resource_id = 501, .slot = "0A", .name = "preview image"};
    view.bga_rows = &row;
    view.bga_row_count = 1;
    view.bga_first_row = 79;
    (void)harness.frame();
    check_layer_brush(state.pointer_capture.source_chip_reveal_id == 0 && !state.bga_catalog.bga_reveal_primary);
    check_layer_brush(harness.drawn_text().find("preview") != std::string::npos);
}

TEST_CASE("Tool chip owns a held click across source change without painting on release",
          "[unit][event-harness][tool-chip]") {
    nuklear_event_harness_t harness{1280, 900};
    require_layer_brush(harness.ready());
    initialize_chip(harness, 1, 179, "0A sample");
    (void)harness.frame();
    const auto source = harness.drawn_text_center("0A sample");
    require_layer_brush(source.has_value());
    const auto pressed = harness.frame(std::array{motion_event(source->first, source->second),
        button_event(true, source->first, source->second)});
    check_chip_inputs_empty({pressed});
    harness.view().compose.draw_source_id = 180;
    harness.view().compose.document_revision = 2;
    const auto held = harness.frame(std::array{motion_event(300, 400)});
    check_layer_brush(held.get().compose_input_count == 0);
    check_layer_brush(worm_ui_nuklear_compose_state().pointer_capture.tool_chip_pointer_canceled);
    const auto released = harness.frame(std::array{button_event(false, 300, 400)});
    check_layer_brush(released.get().compose_input_count == 0);
    check_no_layer_brush(worm_ui_nuklear_compose_state().pointer_capture.tool_chip_pointer_active);
}

TEST_CASE("Layer chip opens hidden Layers for its off-page target and ignores an absent target",
          "[unit][event-harness][tool-chip][semantic-layer]") {
    nuklear_event_harness_t harness{1280, 900};
    require_layer_brush(harness.ready());
    initialize_chip(harness, 0, 0, "no sound resource");
    auto& view = harness.view().compose;
    view.note_paint_mode_active = false;
    view.semantic_layer_paint_mode_active = true;
    view.semantic_layer_target = {.layer_id = 100, .name = "Drums 99", .visible = true};
    view.semantic_layer_target_row = 99;
    view.semantic_layer_total_count = 100;
    check_chip_inputs_empty(click_chip(harness, "Drums 99"));
    const auto revealed = harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    check_layer_brush(state.dock_layout.layers_panel_visible && state.pointer_capture.layer_chip_reveal_id == 0);
    check_layer_brush(revealed.get().compose_semantic_layer_first_row > 8);
    state.dock_layout.layers_panel_visible = false;
    view.semantic_layer_target = {.name = "Pick a layer"};
    check_chip_inputs_empty(click_chip(harness, "Pick a layer"));
    check_no_layer_brush(state.dock_layout.layers_panel_visible);
}

TEST_CASE("Tool chip leaves outside canvas presses available and missing source does not paint",
          "[unit][event-harness][tool-chip]") {
    nuklear_event_harness_t harness{1280, 900};
    require_layer_brush(harness.ready());
    initialize_chip(harness, 0, 0, "no sound resource");
    const auto outside = click_at(harness, 300, 400);
    bool began{};
    for (const auto& frame : outside) {
        for (std::size_t index = 0; index < frame.get().compose_input_count; ++index)
            began |= frame.get().compose_inputs[index].kind == WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN;
    }
    check_layer_brush(began);
    harness.view().compose.draw_source_kind = 1;
    harness.view().compose.draw_source_id = 179;
    harness.view().compose.draw_source_label = "missing source";
    harness.view().compose.draw_source_missing = true;
    check_chip_inputs_empty(click_chip(harness, "missing source"));
    check_no_layer_brush(worm_ui_nuklear_compose_state().dock_layout.sounds_panel_visible);
    check_chip_inputs_empty(click_at(harness, 300, 400));
}

TEST_CASE("A short Compose viewport never exposes an invisible tool-chip hit target",
          "[unit][event-harness][tool-chip]") {
    nuklear_event_harness_t harness{1280, 180};
    require_layer_brush(harness.ready());
    initialize_chip(harness, 1, 179, "0A sample");
    (void)harness.frame();
    check_no_layer_brush(harness.drawn_text_center("0A sample").has_value());
    (void)click_at(harness, 250, 170);
    check_no_layer_brush(worm_ui_nuklear_compose_state().pointer_capture.tool_chip_pointer_active);
    check_no_layer_brush(worm_ui_nuklear_compose_state().dock_layout.sounds_panel_visible);
}

TEST_CASE("BMSON armed source chip distinguishes stems and packed keysounds",
          "[unit][event-harness][tool-chip][stem-reassign]") {
    const auto stem = GENERATE(false, true);
    nuklear_event_harness_t harness{1280, 900};
    require_layer_brush(harness.ready());
    initialize_chip(harness, 1, 179, "voice.wav");
    auto& view = harness.view().compose;
    view.bmson_tools_available = true;
    view.draw_source_is_stem = stem;
    (void)harness.frame();
    check_layer_brush(harness.drawn_text().find(stem ? "Draw stem" : "Draw packed keysound") != std::string::npos);
    check_chip_inputs_empty(click_chip(harness, "voice.wav"));
    check_layer_brush(worm_ui_nuklear_compose_state().dock_layout.sounds_panel_visible);
}

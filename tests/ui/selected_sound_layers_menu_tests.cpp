#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_area_menu.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/context_menu.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <source_location>

namespace {
void check_sound_menu(bool value, std::source_location source = std::source_location::current()) {
    INFO("sound menu assertion line " << source.line());
    CHECK(value);
}
void require_sound_menu(bool value) { REQUIRE(value); }

auto sound_menu_click(nuklear_event_harness_t& harness, float x, float y, Uint8 button = SDL_BUTTON_LEFT)
    -> workspace_action_snapshot_t {
    SDL_Event motion{}; motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = x; motion.motion.y = y;
    SDL_Event click{}; click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    click.button.button = button; click.button.down = true; click.button.x = x; click.button.y = y;
    const auto down = harness.frame(std::array{motion, click});
    click.type = SDL_EVENT_MOUSE_BUTTON_UP; click.button.down = false;
    const auto up = harness.frame(std::array{motion, click});
    return down.get().compose_action != WORM_UI_COMPOSE_ACTION_NONE ? down : up;
}

auto sound_menu_key(nuklear_event_harness_t& harness, SDL_Keycode key, SDL_Scancode scan)
    -> workspace_action_snapshot_t {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.down = true;
    event.key.key = key;
    event.key.scancode = scan;
    const auto down = harness.frame(std::array{event});
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    const auto up = harness.frame(std::array{event});
    return down.get().compose_action != WORM_UI_COMPOSE_ACTION_NONE ? down : up;
}

struct sound_menu_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    std::array<WormUiComposeSemanticLayerView, 2> layers{{
        {.layer_id = 7, .name = "Percussion", .red = 255, .visible = true},
        {.layer_id = 8, .name = "Locked vocals", .blue = 255, .locked = true}}};
    sound_menu_fixture_t() {
        require_sound_menu(harness.ready());
        auto& state = worm_ui_nuklear_compose_state();
        state.inspector_visible = state.sounds_panel_visible = state.layers_panel_visible = false;
        auto& view = harness.view().compose;
        view.area_range_available = true;
        view.area_rows_per_measure = 960;
        view.area_canvas_rows = 9600;
        view.area_range_begin_row = 0;
        view.area_range_end_row = 9600;
        view.selected_note_count = 2;
        view.selected_bgm_event_count = 1;
        view.note_menu_layers = layers.data();
        view.note_menu_layer_count = view.note_menu_layer_total_count = layers.size();
        (void)harness.frame();
        (void)sound_menu_click(harness, 900, 400, SDL_BUTTON_RIGHT);
        (void)harness.frame();
        require_sound_menu(compose_area_menu_open());
    }
    auto click_label(const char* label) -> workspace_action_snapshot_t {
        const auto point = harness.drawn_text_center(label);
        require_sound_menu(point.has_value());
        return sound_menu_click(harness, point->first, point->second);
    }
    void expand() {
        const auto action = click_label("Add selected sounds to layer…");
        check_sound_menu(action.get().compose_action == WORM_UI_COMPOSE_ACTION_OPEN_SELECTED_SOUND_LAYER_MENU);
        (void)harness.frame();
    }
};
}

TEST_CASE("Area menu adds selected sounds to an explicit unlocked layer",
          "[frontend-state][event-harness][selected-sound-layers]") {
    sound_menu_fixture_t fixture;
    fixture.expand();
    check_sound_menu(fixture.harness.drawn_text_center("Locked vocals").has_value());
    const auto locked = fixture.click_label("Locked vocals");
    check_sound_menu(locked.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_sound_menu(compose_area_menu_open());
    const auto selected = fixture.click_label("Percussion");
    check_sound_menu(selected.get().compose_action == WORM_UI_COMPOSE_ACTION_ADD_SELECTED_SOUNDS_TO_LAYER);
    check_sound_menu(selected.get().compose_semantic_layer_id == 7);
    check_sound_menu(selected.get().compose_input_chart_id == fixture.harness.view().compose.chart_id);
    check_sound_menu(selected.get().compose_input_document_revision == fixture.harness.view().compose.document_revision);
    check_sound_menu(!compose_area_menu_open());
}

TEST_CASE("Area sound layer picker cancels on stale context and preserves disabled selection",
          "[frontend-state][event-harness][selected-sound-layers]") {
    sound_menu_fixture_t fixture;
    fixture.expand();
    ++fixture.harness.view().compose.document_revision;
    const auto stale = fixture.harness.frame();
    check_sound_menu(!compose_area_menu_open());
    check_sound_menu(stale.get().compose_action != WORM_UI_COMPOSE_ACTION_ADD_SELECTED_SOUNDS_TO_LAYER);
}

TEST_CASE("Area sound layer picker releases its window request when dismissed",
          "[frontend-state][event-harness][selected-sound-layers]") {
    sound_menu_fixture_t fixture;
    fixture.expand();
    const auto dismissed = sound_menu_click(fixture.harness, 100, 650);
    check_sound_menu(dismissed.get().compose_action == WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU);
    check_sound_menu(dismissed.get().compose_input_chart_id == fixture.harness.view().compose.chart_id);
    check_sound_menu(dismissed.get().compose_input_document_revision == fixture.harness.view().compose.document_revision);
    check_sound_menu(!compose_area_menu_open());
}

TEST_CASE("Area sound layer command remains disabled without selected notes or BGM",
          "[frontend-state][event-harness][selected-sound-layers]") {
    sound_menu_fixture_t fixture;
    fixture.harness.view().compose.selected_note_count = 0;
    fixture.harness.view().compose.selected_bgm_event_count = 0;
    (void)fixture.harness.frame();
    const auto disabled = fixture.click_label("Add selected sounds to layer…");
    check_sound_menu(disabled.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_sound_menu(compose_area_menu_open());
}

TEST_CASE("Keyboard area context reaches the selected sound layer picker and returns with Left",
          "[frontend-state][event-harness][selected-sound-layers]") {
    sound_menu_fixture_t fixture;
    (void)sound_menu_key(fixture.harness, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    wormedit::ui::chrome::context_menu_request_open(wormedit::ui::chrome::KeyboardContextTarget::compose);
    (void)fixture.harness.frame();
    check_sound_menu(compose_area_menu_open());
    (void)sound_menu_key(fixture.harness, SDLK_DOWN, SDL_SCANCODE_DOWN);
    const auto expanded = sound_menu_key(fixture.harness, SDLK_RIGHT, SDL_SCANCODE_RIGHT);
    check_sound_menu(expanded.get().compose_action == WORM_UI_COMPOSE_ACTION_OPEN_SELECTED_SOUND_LAYER_MENU);
    check_sound_menu(fixture.harness.drawn_text_center("Percussion").has_value());
    const auto back = sound_menu_key(fixture.harness, SDLK_LEFT, SDL_SCANCODE_LEFT);
    check_sound_menu(back.get().compose_action == WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU);
    check_sound_menu(fixture.harness.drawn_text_center("Quantize selection").has_value());
}

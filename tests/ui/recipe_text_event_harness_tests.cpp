#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"
#include "ui/nuklear/chrome/text_input.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <array>
#include <cstdio>
#include <source_location>
#include <string_view>

namespace {

void check_recipe(bool condition, const std::source_location location = std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    CHECK(condition);
}

auto key_event(SDL_Keycode key, SDL_Scancode scancode, bool down = true, bool repeat = false) -> SDL_Event {
    SDL_Event event{};
    const auto type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.type = type;
    event.key.type = type;
    event.key.key = key;
    event.key.scancode = scancode;
    event.key.down = down;
    event.key.repeat = repeat;
    return event;
}

void click_text(nuklear_event_harness_t& harness, std::string_view text) {
    const auto center = harness.drawn_text_center(text);
    REQUIRE(center);
    (void)wormedit::test::click_at(harness, center->first, center->second);
}

void prepare_recipe(nuklear_event_harness_t& harness) {
    auto& view = harness.view().compose;
    view.recipe_form.visible = true;
    view.recipe_form.enabled = true;
    view.recipe_form.recipe_id = {42};
    view.recipe_form.name = "Recipe 42";
    view.recipe_form.form.curve = wormedit::sv_recipe_curve_t::linear;
    std::snprintf(view.recipe_form.form.value_from.text, sizeof(view.recipe_form.form.value_from.text), "1.2345");
    std::snprintf(view.recipe_form.form.value_to.text, sizeof(view.recipe_form.form.value_to.text), "0.7");
    std::snprintf(view.recipe_form.form.fixed_point_count, sizeof(view.recipe_form.form.fixed_point_count), "16");
    worm_ui_nuklear_compose_state().dock_layout.layout.activate(wormedit::dock_panel_t::inspector, false);
    (void)harness.frame();
}

} // namespace

TEST_CASE("SV recipe text deletion owns shortcuts while its tooltip is visible",
          "[frontend-state][event-harness][recipe-text]") {
    const bool opened_dropdown = GENERATE(false, true);
    const bool backspace = GENERATE(false, true);
    const bool repeat = GENERATE(false, true);
    nuklear_event_harness_t harness{1280, 1000};
    REQUIRE(harness.ready());
    prepare_recipe(harness);
    if (opened_dropdown) {
        click_text(harness, "Linear");
        click_text(harness, "Exponential");
    }
    click_text(harness, "1.2345");
    check_recipe(harness.drawn_text().find("The value at the start") != std::string::npos);
    const auto position = key_event(SDLK_HOME, SDL_SCANCODE_HOME);
    const auto positioned = harness.frame(std::array{position});
    check_recipe(positioned.get().shortcut_text_entry_active);
    check_recipe(positioned.get().compose_inputs.empty());
    (void)harness.frame(std::array{key_event(position.key.key, position.key.scancode, false)});
    if (backspace) {
        (void)harness.frame(std::array{key_event(SDLK_RIGHT, SDL_SCANCODE_RIGHT)});
        (void)harness.frame(std::array{key_event(SDLK_RIGHT, SDL_SCANCODE_RIGHT, false)});
    }

    const auto erase = key_event(backspace ? SDLK_BACKSPACE : SDLK_DELETE,
                                backspace ? SDL_SCANCODE_BACKSPACE : SDL_SCANCODE_DELETE, true, repeat);
    const auto edited = harness.frame(std::array{erase});
    check_recipe(edited.get().shortcut_text_entry_active);
    REQUIRE(edited.get().shortcut_events.size() == 1);
    check_recipe(edited.get().shortcut_events.front().hid_usage ==
          (backspace ? wormedit::shortcut_key_t::backspace : wormedit::shortcut_key_t::delete_key));
    check_recipe(edited.get().shortcut_events.front().repeat == repeat);
    check_recipe(edited.get().compose_inputs.empty());
    check_recipe(edited.get().compose_action == WORM_UI_COMPOSE_ACTION_PREVIEW_SV_RECIPE_FORM);
    check_recipe(std::string_view{edited.get().compose_recipe_form.value_from.text} == ".2345");
    (void)harness.frame(std::array{key_event(erase.key.key, erase.key.scancode, false)});

    // Returning to the canvas must restore ordinary selection deletion.
    (void)wormedit::test::click_at(harness, 300, 300);
    const auto canvas = harness.frame(std::array{erase});
    check_recipe(!canvas.get().shortcut_text_entry_active);
    REQUIRE(canvas.get().shortcut_events.size() == 1);
}

TEST_CASE("Only a live popup overrides its parent text editor", "[frontend-state][recipe-text]") {
    using wormedit::ui::chrome::has_active_text_edit;
    nk_context context{};
    nk_window window{}, popup{};
    check_recipe(!has_active_text_edit(nullptr));
    check_recipe(!has_active_text_edit(&context));
    context.active = &window;
    window.edit.active = 1;
    check_recipe(has_active_text_edit(&context));
    window.popup.win = &popup;
    check_recipe(has_active_text_edit(&context));
    window.popup.active = 1;
    check_recipe(!has_active_text_edit(&context));
    popup.edit.active = 1;
    check_recipe(has_active_text_edit(&context));
    window.popup.active = 0;
    window.edit.active = 0;
    check_recipe(!has_active_text_edit(&context));
}

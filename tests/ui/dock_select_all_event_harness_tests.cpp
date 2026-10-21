#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_canvas.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <source_location>

namespace {

void check_select_all(bool condition, const std::source_location location =
    std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    CHECK(condition);
}

void require_select_all(bool condition, const std::source_location location =
    std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    REQUIRE(condition);
}

void seed_sound_rows(wormedit::ui::compose_view_t& view) {
    static const std::array rows{
        wormedit::ui::compose_keysound_row_view_t{.resource_id = wormedit::resource_id_t{5}, .slot = "01", .name = "Kick"},
        wormedit::ui::compose_keysound_row_view_t{.resource_id = wormedit::resource_id_t{9}, .slot = "02", .name = "Snare"}};
    static const std::array<std::uint64_t, 2> ids{5, 9};
    view.keysounds_available = true;
    view.project_id.value = 31;
    view.chart_id.value = 7;
    view.keysounds.assign(std::begin(rows), std::end(rows));
    view.keysounds.resize(rows.size());
    view.keysound_total_count = rows.size();
    view.keysound_listed_resource_ids.assign(std::begin(ids), std::end(ids));
    view.keysound_listed_resource_ids.resize(ids.size());
    view.keysound_all_resource_ids.assign(std::begin(ids), std::end(ids));
    view.keysound_all_resource_ids.resize(ids.size());
}

void hover(nuklear_event_harness_t& harness, float x, float y) {
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = x;
    motion.motion.y = y;
    (void)harness.frame(std::array{motion});
}

// Sends Ctrl+A and returns that frame's workspace action.
auto press_ctrl_a(nuklear_event_harness_t& harness) -> workspace_action_snapshot_t {
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.type = SDL_EVENT_KEY_DOWN;
    key.key.down = true;
    key.key.key = SDLK_A;
    key.key.scancode = SDL_SCANCODE_A;
    return harness.frame(std::array{key}, SDL_KMOD_CTRL);
}

} // namespace

TEST_CASE("Ctrl+A over the Sounds list selects its rows and not chart notes",
          "[unit][frontend-state][event-harness][dock-select-all]") {
    nuklear_event_harness_t harness{1280, 900};
    require_select_all(harness.ready());
    seed_sound_rows(harness.view().compose);
    (void)harness.frame();
    auto& state = worm_ui_nuklear_compose_state();
    require_select_all(state.sounds_dock.sound_list_valid);
    require_select_all(state.keysound_catalog.keysound_shown.size() == 2);
    hover(harness, state.sounds_dock.sound_list_x + state.sounds_dock.sound_list_width * 0.5f,
          state.sounds_dock.sound_list_y + state.sounds_dock.sound_list_height * 0.5f);
    const auto action = press_ctrl_a(harness).get();
    check_select_all(state.keysound_catalog.keysound_selection.selected == std::vector<std::uint64_t>{5, 9});
    check_select_all(action.shortcut_events.size() == 0);
}

TEST_CASE("Ctrl+A over the BGA list selects its rows and an empty list selects nothing",
          "[unit][frontend-state][event-harness][dock-select-all]") {
    nuklear_event_harness_t harness{1280, 900};
    require_select_all(harness.ready());
    (void)harness.frame();
    hover(harness, 30.0f, 30.0f);
    auto& state = worm_ui_nuklear_compose_state();
    // The dock records its bounds while rendering; set them after the hover frame.
    state.bga_catalog.bga_list_valid = true;
    state.bga_catalog.bga_list_x = 10.0f;
    state.bga_catalog.bga_list_y = 10.0f;
    state.bga_catalog.bga_list_width = 50.0f;
    state.bga_catalog.bga_list_height = 50.0f;
    state.bga_catalog.bga_shown = {3, 4};
    auto action = press_ctrl_a(harness).get();
    check_select_all(state.bga_catalog.bga_selection.selected == std::vector<std::uint64_t>{3, 4});
    check_select_all(action.shortcut_events.size() == 0);

    worm_ui_keysound_clear(state.bga_catalog.bga_selection);
    // The frame above reset the recorded bounds, so arm an empty list again.
    state.bga_catalog.bga_list_valid = true;
    state.bga_catalog.bga_shown.clear();
    action = press_ctrl_a(harness).get();
    check_select_all(state.bga_catalog.bga_selection.selected.empty());
    check_select_all(action.shortcut_events.size() == 0);
}

TEST_CASE("Ctrl+A over the canvas still reaches the select-all shortcut",
          "[unit][frontend-state][event-harness][dock-select-all]") {
    nuklear_event_harness_t harness{1280, 900};
    require_select_all(harness.ready());
    seed_sound_rows(harness.view().compose);
    (void)harness.frame();
    const auto viewport = compose_canvas_viewport_for_testing();
    require_select_all(viewport.has_value());
    hover(harness, viewport->x + 20.0f, viewport->y + 20.0f);
    const auto action = press_ctrl_a(harness).get();
    check_select_all(action.shortcut_events.size() == 1);
    check_select_all(worm_ui_nuklear_compose_state().keysound_catalog.keysound_selection.selected.empty());
}

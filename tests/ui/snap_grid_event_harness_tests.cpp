#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_state.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <source_location>

namespace {
void check_grid(bool value, std::source_location source = std::source_location::current()) { INFO("grid check line " << source.line()); CHECK(value); }
void require_grid(bool value) { REQUIRE(value); }
auto mouse(nuklear_event_harness_t& harness, float x, float y, bool down)
    -> workspace_action_snapshot_t {
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = x;
    motion.motion.y = y;
    SDL_Event button{};
    button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    button.button.button = SDL_BUTTON_LEFT;
    button.button.down = down;
    button.button.x = x;
    button.button.y = y;
    const std::array events{motion, button};
    return harness.frame(events);
}
auto press(nuklear_event_harness_t& harness, SDL_Keycode code, SDL_Scancode scan)
    -> workspace_action_snapshot_t {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = code;
    event.key.scancode = scan;
    event.key.down = true;
    return harness.frame(std::span{&event, 1});
}
void prepare(nuklear_event_harness_t& harness) {
    require_grid(harness.ready());
    auto& view = harness.view().compose;
    view.area_snap_denominator = 4;
    const std::array<std::uint64_t, 5> snaps{4, 8, 16, 7, 12};
    view.supported_snap_denominators.assign(snaps.begin(), snaps.end());
    static_cast<void>(harness.frame());
    const auto target = harness.drawn_text_center("1/4");
    require_grid(target.has_value());
    static_cast<void>(mouse(harness, target->first, target->second, true));
    static_cast<void>(mouse(harness, target->first, target->second, false));
    require_grid(worm_ui_nuklear_compose_state().snap_grid.snap_grid_menu.open);
}
void check_snap(const wormedit::ui::workspace_action_t& action, std::uint64_t denominator) {
    INFO("action " << action.compose_action << " snap " << action.compose_snap_denominator);
    check_grid(action.compose_action == WORM_UI_COMPOSE_ACTION_SET_EDIT_SNAP);
    check_grid(action.compose_snap_denominator == denominator);
    check_grid(action.compose_input_project_id.value == 1);
    check_grid(action.compose_input_chart_id.value == 1);
    check_grid(action.compose_input_document_revision == 1);
    check_grid(action.compose_inputs.size() == 0);
    check_grid(action.shortcut_events.size() == 0);
}
}

TEST_CASE("Snap fraction badge opens supported grids and applies keyboard choice without leakage", "[frontend-state][event-harness][snap-grid]") {
    nuklear_event_harness_t harness{1280, 800};
    prepare(harness);
    check_grid(harness.drawn_text().find("Editing grid") != std::string::npos);
    check_grid(harness.drawn_text().find("Snap 1/") == std::string::npos);
    check_grid(press(harness, SDLK_RIGHT, SDL_SCANCODE_RIGHT).get().shortcut_events.size() == 0);
    check_grid(worm_ui_nuklear_compose_state().snap_grid.snap_grid_menu.open);
    check_grid(worm_ui_nuklear_compose_state().snap_grid.snap_grid_choice == 1);
    const auto selected = press(harness, SDLK_RETURN, SDL_SCANCODE_RETURN);
    check_snap(selected.get(), 8);
    check_grid(!worm_ui_nuklear_compose_state().snap_grid.snap_grid_menu.open);
    check_grid(harness.frame().get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
}

TEST_CASE("Snap popup mouse choice supports custom denominators", "[frontend-state][event-harness][snap-grid]") {
    nuklear_event_harness_t harness{1280, 800};
    prepare(harness);
    const auto target = harness.drawn_text_center("1/7");
    require_grid(target.has_value());
    const auto selected = mouse(harness, target->first, target->second, true);
    check_snap(selected.get(), 7);
    const auto released = mouse(harness, target->first, target->second, false);
    check_grid(released.get().compose_inputs.size() == 0);
}

TEST_CASE("Snap popup cancels on Escape outside click stale context focus loss and modal ownership", "[frontend-state][event-harness][snap-grid]") {
    for (int cause = 0; cause < 9; ++cause) {
        INFO("cause " << cause);
        nuklear_event_harness_t harness{1280, 800};
        prepare(harness);
        workspace_action_snapshot_t result{{}};
        if (cause == 0) result = press(harness, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        if (cause == 1) result = mouse(harness, 500, 150, true);
        if (cause == 2) ++harness.view().compose.document_revision;
        if (cause == 3) ++harness.view().compose.chart_id.value;
        if (cause == 4) ++harness.view().compose.project_id.value;
        if (cause == 5) harness.view().options.visible = true;
        if (cause == 6) harness.view().compose.paste_active = true;
        if (cause == 7) harness.view().active_tab = WORM_UI_WORKSPACE_TAB_METADATA;
        if (cause == 8) {
            SDL_Event lost{};
            lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            result = harness.frame(std::span{&lost, 1});
        }
        if (cause >= 2 && cause < 8) result = press(harness, SDLK_RETURN, SDL_SCANCODE_RETURN);
        check_grid(result.get().compose_action != WORM_UI_COMPOSE_ACTION_SET_EDIT_SNAP);
        // Focus loss may emit the existing cancel/release input, never a chart press.
        for (std::size_t index = 0; index < result.get().compose_inputs.size(); ++index)
            check_grid(result.get().compose_inputs[index].kind == WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
        check_grid(!worm_ui_nuklear_compose_state().snap_grid.snap_grid_menu.open);
    }
}

TEST_CASE("Snap popup offers fifth grids and a stamped selection fitting action", "[frontend-state][event-harness][fitting-snap]") {
    nuklear_event_harness_t h{1280,900};
    prepare(h);
    auto& view=h.view().compose;
    view.selected_note_count=2;
    view.supported_snap_denominators[1]=5;
    (void)h.frame();
    check_grid(h.drawn_text_center("1/5").has_value());
    const auto target=h.drawn_text_center("Find fitting snap");
    require_grid(target.has_value());
    const auto picked=mouse(h,target->first,target->second,true);
    check_grid(picked.get().compose_action==WORM_UI_COMPOSE_ACTION_FIND_FITTING_SNAP);
    check_grid(picked.get().compose_input_chart_id.value==1);
    check_grid(picked.get().compose_input_document_revision==1);
    check_grid(picked.get().compose_inputs.size()==0);
    (void)mouse(h,target->first,target->second,false);
    check_grid(!worm_ui_nuklear_compose_state().snap_grid.snap_grid_menu.open);
}

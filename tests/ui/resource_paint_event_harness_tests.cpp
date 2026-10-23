#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_state.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <array>
#include <set>

using namespace wormedit;
namespace {
struct resource_brush_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    ui::testing_rect_view_t viewport;
    resource_brush_fixture_t() {
        REQUIRE(harness.ready());
        auto& state = worm_ui_nuklear_compose_state();
        state.dock_layout.layout.show(wormedit::dock_panel_t::inspector, false);
        state.dock_layout.layout.show(wormedit::dock_panel_t::sounds, false);
        state.dock_layout.layout.show(wormedit::dock_panel_t::layers, false);
        auto& view = harness.view().compose;
        view.resource_paint_mode_active = view.resource_paint_available = true;
        view.resource_paint_has_sample = true;
        view.draw_source_kind = 1;
        view.draw_source_id = resource_id_t{2};
        view.draw_source_label = "two.wav";
        for (std::uint64_t i = 0; i < 10; ++i)
            view.note_endpoints.push_back({.bounds = {250, 100.0 + i * 40, 350, 110.0 + i * 40},
                .note_id = object_id_t{100 + i}, .lane_id = lane_id_t{1}});
        (void)harness.frame();
        REQUIRE(worm_ui_frontend_compose_viewport_for_testing(&viewport));
    }
    auto point(double x, double y) -> std::pair<float, float> {
        const auto& view = harness.view().compose;
        return {viewport.x + static_cast<float>(x / view.viewport_width_pixels * viewport.width),
                viewport.y + static_cast<float>(y / view.viewport_height_pixels * viewport.height)};
    }
    auto press() -> workspace_action_snapshot_t {
        const auto [x, y] = point(300, 80);
        return harness.frame(std::array{test::mouse_motion(x, y), test::mouse_button(true, x, y)});
    }
};
auto has_input(const workspace_action_snapshot_t& frame, WormUiComposeInputKind kind) -> bool {
    return std::ranges::any_of(frame.get().compose_inputs, [kind](const auto& input) { return input.kind == kind; });
}
}

TEST_CASE("Keysound brush sweeps ten notes in one pointer motion without placing objects",
          "[event-harness][resource-paint]") {
    resource_brush_fixture_t fixture;
    REQUIRE(has_input(fixture.press(), WORM_UI_COMPOSE_INPUT_RESOURCE_PAINT_BEGIN));
    const auto [x, y] = fixture.point(300, 500);
    const auto motion = test::mouse_motion(x, y);
    const auto moved = fixture.harness.frame(std::span{&motion, 1});
    std::set<std::uint64_t> visited;
    for (const auto& input : moved.get().compose_inputs)
        if (input.kind == WORM_UI_COMPOSE_INPUT_RESOURCE_PAINT_UPDATE && input.object_id)
            visited.insert(input.object_id.value);
    CHECK(visited == std::set<std::uint64_t>{100, 101, 102, 103, 104, 105, 106, 107, 108, 109});
    const auto release = test::mouse_button(false, x, y);
    const auto released = fixture.harness.frame(std::span{&release, 1});
    CHECK(has_input(released, WORM_UI_COMPOSE_INPUT_RESOURCE_PAINT_RELEASE));
    CHECK_FALSE(has_input(released, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE));
    CHECK_FALSE(has_input(released, WORM_UI_COMPOSE_INPUT_POINTER_CLICK));
}

TEST_CASE("Keysound brush cancellation never publishes a late release",
          "[event-harness][resource-paint]") {
    resource_brush_fixture_t fixture;
    (void)fixture.press();
    const auto escape = GENERATE(false, true);
    SDL_Event event{};
    if (escape) {
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = SDLK_ESCAPE;
        event.key.scancode = SDL_SCANCODE_ESCAPE;
        event.key.down = true;
    } else {
        event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    }
    CHECK(has_input(fixture.harness.frame(std::span{&event, 1}), WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE));
    const auto [x, y] = fixture.point(300, 500);
    const auto release = test::mouse_button(false, x, y);
    CHECK_FALSE(has_input(fixture.harness.frame(std::span{&release, 1}), WORM_UI_COMPOSE_INPUT_RESOURCE_PAINT_RELEASE));
    CHECK_FALSE(worm_ui_nuklear_compose_state().pointer_capture.resource_paint_pointer_active);
}

TEST_CASE("Keysound tool exposes hitsound-only mode and exits with Escape",
          "[event-harness][resource-paint]") {
    resource_brush_fixture_t fixture;
    fixture.harness.view().compose.resource_paint_has_sample = false;
    (void)fixture.harness.frame();
    CHECK(fixture.harness.drawn_text().find("osu! hitsound palette") != std::string::npos);
    SDL_Event escape{};
    escape.type = SDL_EVENT_KEY_DOWN;
    escape.key.key = SDLK_ESCAPE;
    escape.key.scancode = SDL_SCANCODE_ESCAPE;
    escape.key.down = true;
    CHECK(fixture.harness.frame(std::span{&escape, 1}).get().compose_action == WORM_UI_COMPOSE_ACTION_SELECT_TOOL);
}

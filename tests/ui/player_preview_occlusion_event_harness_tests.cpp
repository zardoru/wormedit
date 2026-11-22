#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_canvas.h"
#include "ui/nuklear/compose/compose_state.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace {

using wormedit::test::click_at;
using wormedit::test::mouse_button;
using wormedit::test::mouse_motion;

// A BGM lane whose mute/solo button can sit under the floating player preview.
struct preview_occlusion_fixture_t {
    nuklear_event_harness_t harness{1600, 900};
    bool solo{};

    [[nodiscard]] auto kind() const -> WormUiComposeInputKind {
        return solo ? WORM_UI_COMPOSE_INPUT_BGM_SOLO : WORM_UI_COMPOSE_INPUT_BGM_MUTE;
    }

    // Without docks the canvas reaches the right edge, where the preview floats.
    void hide_docks() {
        for (const auto& panel : wormedit::dock_panel_catalog)
            worm_ui_nuklear_compose_state().dock_layout.layout.show(panel.id, false);
    }

    void place_lane(double left) {
        auto& compose = harness.view().compose;
        wormedit::ui::compose_bgm_lane_view_t lane{};
        lane.bounds = {left, 0, left + 72, compose.viewport_height_pixels};
        lane.source_lane_id = wormedit::object_id_t{77};
        compose.bgm_lanes = {lane};
        compose.bgm_mix_available = true;
        compose.note_paint_mode_active = true;
    }

    void show_preview(bool visible) {
        auto& preview = harness.view().player_preview;
        preview.visible = visible;
        preview.viewport_width_pixels = 320;
        preview.viewport_height_pixels = 480;
        preview.status_text = "Preview unavailable";
        (void)harness.frame();
    }

    // Screen center of the lane's button; requires a drawn frame.
    [[nodiscard]] auto button_center() const -> std::pair<float, float> {
        const auto viewport = compose_canvas_viewport_for_testing();
        REQUIRE(viewport.has_value());
        const auto rect = compose_bgm_mix_button_rect(
            harness_view().compose.bgm_lanes.front(), harness_view().compose,
            nk_rect(viewport->x, viewport->y, viewport->width, viewport->height), solo);
        REQUIRE(rect.w > 0);
        return {rect.x + rect.w / 2, rect.y + rect.h / 2};
    }

    [[nodiscard]] auto mix_inputs(const std::vector<workspace_action_snapshot_t>& frames) const
        -> std::size_t {
        std::size_t count = 0;
        for (const auto& frame : frames)
            for (const auto& input : frame.get().input.compose_inputs) count += input.kind == kind();
        return count;
    }

    [[nodiscard]] auto click_button() -> std::size_t {
        const auto [x, y] = button_center();
        return mix_inputs(click_at(harness, x, y));
    }

  private:
    [[nodiscard]] auto harness_view() const -> const wormedit::ui::workspace_view_t& {
        return const_cast<nuklear_event_harness_t&>(harness).view();
    }
};

void require_under_preview(const nuklear_event_harness_t& harness, std::pair<float, float> point) {
    const auto title = harness.drawn_text_center("Player preview");
    REQUIRE(title.has_value());
    REQUIRE(point.first > title->first - 60);
    REQUIRE(point.second > title->second);
}

} // namespace

TEST_CASE("A click on the floating player preview does not reach the Compose canvas behind it",
          "[frontend-state][event-harness][player-preview][occlusion]") {
    preview_occlusion_fixture_t fixture;
    fixture.solo = GENERATE(false, true);
    REQUIRE(fixture.harness.ready());
    fixture.hide_docks();
    fixture.place_lane(1330);
    fixture.show_preview(false);
    CHECK(fixture.click_button() == 1);

    fixture.show_preview(true);
    require_under_preview(fixture.harness, fixture.button_center());
    CHECK(fixture.click_button() == 0);
    CHECK_FALSE(worm_ui_nuklear_compose_state().stem_lanes.bgm_mix_pointer_active);
}

TEST_CASE("A canvas press released over the player preview still belongs to Compose",
          "[frontend-state][event-harness][player-preview][occlusion]") {
    preview_occlusion_fixture_t fixture;
    REQUIRE(fixture.harness.ready());
    fixture.hide_docks();
    fixture.place_lane(1330);
    fixture.show_preview(true);
    const auto [x, y] = fixture.button_center();
    const std::array press{mouse_motion(x - 400, y), mouse_button(true, x - 400, y)};
    (void)fixture.harness.frame(press);
    const std::array release{mouse_motion(x, y), mouse_button(false, x, y)};
    (void)fixture.harness.frame(release);
    // Ownership is released with the button: the next canvas click lands normally.
    fixture.place_lane(930);
    (void)fixture.harness.frame();
    CHECK(fixture.click_button() == 1);
}


namespace {

auto has_action(const std::vector<workspace_action_snapshot_t>& frames, WormUiWorkspaceActionKind kind)
    -> bool {
    return std::ranges::any_of(frames, [kind](const auto& frame) { return frame.get().kind == kind; });
}

auto seek_frame_of(const std::vector<workspace_action_snapshot_t>& frames) -> std::optional<std::uint64_t> {
    for (const auto& frame : frames)
        if (frame.get().kind == WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK)
            return frame.get().transport.seek_frame;
    return std::nullopt;
}

auto space_key(bool down) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.down = down;
    event.key.key = SDLK_SPACE;
    event.key.scancode = SDL_SCANCODE_SPACE;
    return event;
}

// A full-screen preview over a chart whose transport can play, change rate and seek.
void cover_with_full_screen_preview(nuklear_event_harness_t& harness) {
    auto& view = harness.view();
    view.player_preview.visible = true;
    view.player_preview.layout = wormedit::player_preview_layout_t::full_screen;
    view.player_preview.viewport_width_pixels = 320;
    view.player_preview.viewport_height_pixels = 480;
    view.player_preview.status_text = "Preview unavailable";
    view.transport_available = true;
    view.transport_rate_available = true;
    view.transport_keep_pitch_available = true;
    view.transport_end_frame = 48'000 * 100;
    view.transport_sample_rate = 48'000;
}

} // namespace

TEST_CASE("A full-screen player preview skips drawing the workspace under it",
          "[frontend-state][event-harness][player-preview][full-screen]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    cover_with_full_screen_preview(harness);
    reset_compose_canvas_viewport_for_testing();
    (void)harness.frame();
    CHECK_FALSE(compose_canvas_viewport_for_testing().has_value());

    harness.view().player_preview.layout = wormedit::player_preview_layout_t::floating;
    (void)harness.frame();
    CHECK(compose_canvas_viewport_for_testing().has_value());
}

TEST_CASE("The full-screen preview's transport strip plays and changes rate and pitch",
          "[frontend-state][event-harness][player-preview][full-screen][transport]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    cover_with_full_screen_preview(harness);
    (void)harness.frame();
    CHECK(has_action(harness.click_widget("Play from cursor (Space)"),
                     WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE));
    CHECK(has_action(harness.click_widget("Slower playback rate"),
                     WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_RATE));
    CHECK(has_action(harness.click_widget("Preserve pitch at reduced speed"),
                     WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_KEEP_PITCH));
}

TEST_CASE("The full-screen preview's transport strip stops and seeks during playback",
          "[frontend-state][event-harness][player-preview][full-screen][transport]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    cover_with_full_screen_preview(harness);
    harness.view().transport_playing = true;
    (void)harness.frame();
    CHECK(has_action(harness.click_widget("Stop playback"), WORM_UI_WORKSPACE_ACTION_TRANSPORT_STOP));
    // The seek bar fills the rest of the strip, right of the keep-pitch toggle.
    const auto keep_pitch = harness.widget_center("Preserve pitch at reduced speed");
    const auto seek = seek_frame_of(click_at(harness, 1200, keep_pitch.second));
    REQUIRE(seek.has_value());
    CHECK(*seek > 0);
}

TEST_CASE("Space still reaches the shortcut table under a full-screen player preview",
          "[frontend-state][event-harness][player-preview][full-screen][shortcut]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    cover_with_full_screen_preview(harness);
    (void)harness.frame();
    const auto down = space_key(true);
    const auto pressed = harness.frame({&down, 1});
    const auto& events = pressed.get().input.shortcuts.events;
    CHECK(std::ranges::any_of(events, [](const auto& event) {
        return event.hid_usage == static_cast<wormedit::shortcut_key_t>(SDL_SCANCODE_SPACE);
    }));
    const auto up = space_key(false);
    (void)harness.frame({&up, 1});
}

namespace {

auto key_press(SDL_Keycode code, SDL_Scancode scan, bool down = true) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.down = down;
    event.key.key = code;
    event.key.scancode = scan;
    return event;
}

auto wheel_at(float x, float y, float amount) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.mouse_x = x;
    event.wheel.mouse_y = y;
    event.wheel.y = amount;
    return event;
}

auto compose_input_count(const workspace_action_snapshot_t& frame, WormUiComposeInputKind kind) -> std::size_t {
    std::size_t count = 0;
    for (const auto& input : frame.get().input.compose_inputs) count += input.kind == kind;
    return count;
}

// Presses and releases a key under the given modifiers; returns the press frame.
auto tap_key(nuklear_event_harness_t& harness, SDL_Keycode code, SDL_Scancode scan,
             SDL_Keymod modifiers = SDL_KMOD_NONE) -> workspace_action_snapshot_t {
    const auto down = key_press(code, scan);
    auto pressed = harness.frame({&down, 1}, modifiers);
    const auto up = key_press(code, scan, false);
    (void)harness.frame({&up, 1}, modifiers);
    return pressed;
}

} // namespace

TEST_CASE("Page keys and Go to measure navigate Compose under a full-screen preview",
          "[frontend-state][event-harness][player-preview][full-screen][navigation]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    cover_with_full_screen_preview(harness);
    (void)harness.frame();
    CHECK(compose_input_count(tap_key(harness, SDLK_PAGEDOWN, SDL_SCANCODE_PAGEDOWN),
                              WORM_UI_COMPOSE_INPUT_NAVIGATION_STEP) == 1);
    // Lane keys would place notes out of sight; they do nothing here.
    CHECK(tap_key(harness, SDLK_1, SDL_SCANCODE_1).get().input.compose_inputs.empty());

    (void)tap_key(harness, SDLK_G, SDL_SCANCODE_G, SDL_KMOD_CTRL);
    CHECK(worm_ui_nuklear_compose_state().dialogs.go_to_measure_open);
    (void)harness.frame();
    CHECK(harness.drawn_text().find("Go to measure") != std::string::npos);
    (void)tap_key(harness, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    (void)harness.frame();
    CHECK_FALSE(worm_ui_nuklear_compose_state().dialogs.go_to_measure_open);
}

TEST_CASE("The plain wheel scrolls Compose under a full-screen preview while Ctrl+wheel zooms it",
          "[frontend-state][event-harness][player-preview][full-screen][navigation]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    cover_with_full_screen_preview(harness);
    (void)harness.frame();
    const auto plain = wheel_at(800, 400, -1.0F);
    const auto scrolled = harness.frame({&plain, 1});
    REQUIRE(scrolled.get().input.compose_viewport_inputs.size() == 1);
    CHECK(scrolled.get().input.compose_viewport_inputs[0].kind == WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL);
    CHECK_FALSE(scrolled.get().input.compose_viewport_inputs[0].zoom_modifier);

    const auto zoomed = harness.frame({&plain, 1}, SDL_KMOD_CTRL);
    CHECK(zoomed.get().input.compose_viewport_inputs.empty());
}

TEST_CASE("Bookmark navigation shortcuts still reach the command table under a full-screen preview",
          "[frontend-state][event-harness][player-preview][full-screen][navigation]") {
    nuklear_event_harness_t harness{1600, 900};
    REQUIRE(harness.ready());
    cover_with_full_screen_preview(harness);
    (void)harness.frame();
    const auto pressed = tap_key(harness, SDLK_DOWN, SDL_SCANCODE_DOWN, SDL_KMOD_CTRL);
    CHECK(std::ranges::any_of(pressed.get().input.shortcuts.events, [](const auto& event) {
        return event.hid_usage == static_cast<wormedit::shortcut_key_t>(SDL_SCANCODE_DOWN);
    }));
}

#include "nuklear_event_harness.h"
#include "ui/nuklear/compose_canvas.h"
#include "ui/nuklear/compose_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <source_location>
#include <string_view>

namespace {

void check_rename(bool condition, const std::source_location location =
    std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    CHECK(condition);
}

void require_rename(bool condition, const std::source_location location =
    std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    REQUIRE(condition);
}

auto mouse_button(bool down, float x, float y) -> SDL_Event {
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

auto pointer_motion(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

auto press_enter() -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_RETURN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    return event;
}

auto press_escape() -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_ESCAPE;
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    return event;
}

auto press_one() -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_1;
    event.key.scancode = SDL_SCANCODE_1;
    return event;
}

auto release_escape() -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_UP;
    event.key.type = SDL_EVENT_KEY_UP;
    event.key.key = SDLK_ESCAPE;
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    return event;
}

auto bookmark_target(nuklear_event_harness_t& harness, double document_y)
    -> std::optional<std::pair<float, float>> {
    const auto viewport = compose_canvas_viewport_for_testing();
    if (!viewport) return std::nullopt;
    const auto& view = harness.view().compose;
    const auto document_x = view.playable_field_right_pixels - 12.0;
    return std::pair<float, float>{
        viewport->x + static_cast<float>(document_x * viewport->width /
                                         view.viewport_width_pixels),
        viewport->y + static_cast<float>(document_y * viewport->height /
                                         view.viewport_height_pixels)};
}

void seed_bookmark(nuklear_event_harness_t& harness, WormUiComposeBookmarkView& bookmark) {
    auto& view = harness.view().compose;
    view.playable_field_right_pixels = view.viewport_width_pixels * 0.62;
    bookmark = {.bookmark_id = 91,
                .label = "Intro",
                .measure_number = 1,
                .row = 120,
                .chart_row = 120,
                .projection_visible = true,
                .projected_y_pixels = 240.0};
    view.beat_bookmarks = &bookmark;
    view.beat_bookmark_count = 1;
}

auto click_bookmark(nuklear_event_harness_t& harness, float x, float y)
    -> std::array<workspace_action_snapshot_t, 2> {
    const std::array down{pointer_motion(x, y), mouse_button(true, x, y)};
    const std::array up{pointer_motion(x, y), mouse_button(false, x, y)};
    return {harness.frame(down), harness.frame(up)};
}

void open_bookmark_editor(nuklear_event_harness_t& harness,
                          const WormUiComposeBookmarkView& bookmark) {
    (void)harness.frame();
    const auto target = bookmark_target(harness, bookmark.projected_y_pixels);
    require_rename(target.has_value());
    const auto [x, y] = *target;
    (void)click_bookmark(harness, x, y);
    (void)harness.frame();
    require_rename(worm_ui_nuklear_compose_state().bookmark_rename.active);
}

} // namespace

TEST_CASE("bookmark line opens a captured rename editor and Enter stages exact identity",
          "[unit][frontend-state][event-harness][bookmark-rename]") {
    nuklear_event_harness_t harness{1280, 900};
    require_rename(harness.ready());
    WormUiComposeBookmarkView bookmark{};
    seed_bookmark(harness, bookmark);
    (void)harness.frame();
    const auto target = bookmark_target(harness, bookmark.projected_y_pixels);
    require_rename(target.has_value());
    const auto [x, y] = *target;
    const auto clicked = click_bookmark(harness, x, y);
    for (const auto& frame : clicked) {
        check_rename(frame.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
        check_rename(frame.get().compose_input_count == 0);
    }
    (void)harness.frame();
    require_rename(worm_ui_nuklear_compose_state().bookmark_rename.active);
    check_rename(std::string_view{worm_ui_nuklear_compose_state().bookmark_rename.label} == "Intro");
    check_rename(harness.drawn_text().find("Intro") != std::string::npos);

    const std::array number_key{press_one()};
    const auto shortcut_suppressed = harness.frame(number_key).get();
    check_rename(shortcut_suppressed.compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_rename(shortcut_suppressed.compose_input_count == 0);

    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "Renamé";
    static_cast<void>(harness.frame(std::span{&text, 1}));
    const std::array enter{press_enter()};
    const auto renamed = harness.frame(enter).get();
    check_rename(renamed.compose_action == WORM_UI_COMPOSE_ACTION_RENAME_BOOKMARK);
    check_rename(renamed.compose_bookmark_id == bookmark.bookmark_id);
    check_rename(renamed.compose_input_project_id == harness.view().compose.project_id);
    check_rename(renamed.compose_input_chart_id == harness.view().compose.chart_id);
    check_rename(renamed.compose_input_document_revision == harness.view().compose.document_revision);
    check_rename(std::string_view{renamed.compose_marker_label} == "Renamé");
    check_rename(!worm_ui_nuklear_compose_state().bookmark_rename.active);
    check_rename(worm_ui_nuklear_compose_state().bookmark_rename.release_pending);
}

TEST_CASE("Escape cancels bookmark rename and outside clicks do not place a chart event",
          "[unit][frontend-state][event-harness][bookmark-rename]") {
    nuklear_event_harness_t harness{1280, 900};
    require_rename(harness.ready());
    WormUiComposeBookmarkView bookmark{};
    seed_bookmark(harness, bookmark);
    (void)harness.frame();
    const auto target = bookmark_target(harness, bookmark.projected_y_pixels);
    require_rename(target.has_value());
    const auto [x, y] = *target;
    (void)click_bookmark(harness, x, y);
    (void)harness.frame();
    require_rename(worm_ui_nuklear_compose_state().bookmark_rename.active);
    const std::array escape{press_escape()};
    const auto cancelled = harness.frame(escape).get();
    check_rename(cancelled.compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_rename(cancelled.compose_input_count == 0);
    check_rename(!worm_ui_nuklear_compose_state().bookmark_rename.active);
    check_rename(worm_ui_nuklear_compose_state().bookmark_rename.release_pending);

    const std::array release_events{release_escape()};
    (void)harness.frame(release_events);
    const auto outside = click_bookmark(harness, 70.0f, 400.0f);
    for (const auto& frame : outside) {
        check_rename(frame.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
        check_rename(frame.get().compose_input_count == 0);
    }
}

TEST_CASE("blur cancels bookmark rename without forwarding the outside click",
          "[unit][frontend-state][event-harness][bookmark-rename]") {
    nuklear_event_harness_t harness{1280, 900};
    require_rename(harness.ready());
    WormUiComposeBookmarkView bookmark{};
    seed_bookmark(harness, bookmark);
    (void)harness.frame();
    const auto target = bookmark_target(harness, bookmark.projected_y_pixels);
    require_rename(target.has_value());
    const auto [x, y] = *target;
    (void)click_bookmark(harness, x, y);
    (void)harness.frame();
    require_rename(worm_ui_nuklear_compose_state().bookmark_rename.active);

    const auto outside = click_bookmark(harness, 70.0f, 400.0f);
    for (const auto& frame : outside) {
        check_rename(frame.get().compose_action == WORM_UI_COMPOSE_ACTION_NONE);
        check_rename(frame.get().compose_input_count == 0);
    }
    check_rename(!worm_ui_nuklear_compose_state().bookmark_rename.active);
    check_rename(worm_ui_nuklear_compose_state().bookmark_rename.release_pending);
}

TEST_CASE("bookmark rename cancels when its document revision becomes stale",
          "[unit][frontend-state][event-harness][bookmark-rename]") {
    nuklear_event_harness_t harness{1280, 900};
    require_rename(harness.ready());
    WormUiComposeBookmarkView bookmark{};
    seed_bookmark(harness, bookmark);
    open_bookmark_editor(harness, bookmark);
    ++harness.view().compose.document_revision;
    const auto frame = harness.frame().get();
    check_rename(frame.compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_rename(frame.compose_input_count == 0);
    check_rename(!worm_ui_nuklear_compose_state().bookmark_rename.active);
}

TEST_CASE("bookmark rename cancels when the captured bookmark is deleted",
          "[unit][frontend-state][event-harness][bookmark-rename]") {
    nuklear_event_harness_t harness{1280, 900};
    require_rename(harness.ready());
    WormUiComposeBookmarkView bookmark{};
    seed_bookmark(harness, bookmark);
    open_bookmark_editor(harness, bookmark);
    harness.view().compose.beat_bookmark_count = 0;
    ++harness.view().compose.document_revision;
    const auto frame = harness.frame().get();
    check_rename(frame.compose_action == WORM_UI_COMPOSE_ACTION_NONE);
    check_rename(frame.compose_input_count == 0);
    check_rename(!worm_ui_nuklear_compose_state().bookmark_rename.active);
}

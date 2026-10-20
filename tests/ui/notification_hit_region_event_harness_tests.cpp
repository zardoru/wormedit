#include "sys/menu_command_view.h"
#include "nuklear_event_harness.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace {
auto pointer_event(SDL_EventType type, float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.type = type;
        event.motion.windowID = 0;
        event.motion.x = x;
        event.motion.y = y;
    } else {
        event.button.type = type;
        event.button.windowID = 0;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.clicks = 1;
        event.button.x = x;
        event.button.y = y;
    }
    return event;
}

auto click(nuklear_event_harness_t& harness, float x, float y)
    -> std::array<workspace_action_snapshot_t, 2> {
    const std::array down{pointer_event(SDL_EVENT_MOUSE_MOTION, x, y),
                          pointer_event(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y)};
    const std::array up{pointer_event(SDL_EVENT_MOUSE_MOTION, x, y),
                        pointer_event(SDL_EVENT_MOUSE_BUTTON_UP, x, y)};
    return {harness.frame(down), harness.frame(up)};
}

void set_unread_notifications(nuklear_event_harness_t& harness) {
    auto& notifications = harness.view().notifications;
    notifications.history_available = true;
    notifications.unread_warnings = 6;
}

void configure_screenshot_compose_view(nuklear_event_harness_t& harness) {
    static std::array<wormedit::ui::compose_lane_view_t, 10> lanes{};
    static constexpr std::array<wormedit::ui::compose_note_view_t, 1> notes{{
        {.bounds = {85.0, 96.0, 105.0, 112.0}, .note_id = {1}, .lane_id = {1},
         .display_lane_number = 1, .selected = true},
    }};
    for (std::size_t index = 0; index < lanes.size(); ++index) {
        lanes[index] = {.bounds = {40.0 + 70.0 * static_cast<double>(index), 0.0,
                                   110.0 + 70.0 * static_cast<double>(index), 512.0},
                        .lane_id = {index + 1}, .display_order = static_cast<std::uint32_t>(index)};
    }
    auto& compose = harness.view().compose;
    compose.compose_measure_count = 4;
    compose.lanes.assign(lanes.data(), lanes.data() + lanes.size());
    compose.note_endpoints.assign(notes.data(), notes.data() + notes.size());
    compose.selected_note_count = 1;
    compose.bgm_bar_available = false;
    compose.bgm_bar_reserved = true;
}

auto opened_history(const auto& frames) -> bool {
    return std::ranges::any_of(frames, [](const auto& frame) {
        return frame.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_OPEN_HISTORY;
    });
}

auto click_text(nuklear_event_harness_t& harness, std::string_view text)
    -> std::optional<std::array<workspace_action_snapshot_t, 2>> {
    const auto center = harness.drawn_text_center(text);
    if (!center) return std::nullopt;
    return click(harness, center->first, center->second);
}

void check_blank_canvas_grid(nuklear_event_harness_t& harness) {
    constexpr std::array xs{805.0f, 866.0f, 927.0f, 988.0f, 989.0f, 1049.0f, 1110.0f, 1173.0f};
    constexpr std::array ys{210.0f, 271.0f, 332.0f, 393.0f, 454.0f, 515.0f, 576.0f,
                            606.0f, 637.0f, 698.0f, 759.0f, 820.0f, 881.0f, 942.0f, 1003.0f};
    for (const auto x : xs)
        for (const auto y : ys)
            CHECK_FALSE(opened_history(click(harness, x, y)));
}

void check_history_opens_on_click(
    const std::array<workspace_action_snapshot_t, 2>& frames) {
    CHECK(opened_history(frames));
    CHECK(std::ranges::count_if(frames, [](const auto& frame) {
        return frame.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_OPEN_HISTORY;
    }) == 1);
}

void check_blank_drag_does_not_transfer_to_badge(nuklear_event_harness_t& harness) {
    const auto badge = harness.drawn_text_center("6 warnings · 0 errors");
    REQUIRE(badge.has_value());
    const std::array down{pointer_event(SDL_EVENT_MOUSE_MOTION, 990.0f, 600.0f),
                          pointer_event(SDL_EVENT_MOUSE_BUTTON_DOWN, 990.0f, 600.0f)};
    const auto move = pointer_event(SDL_EVENT_MOUSE_MOTION, badge->first, badge->second);
    const auto up = pointer_event(SDL_EVENT_MOUSE_BUTTON_UP, badge->first, badge->second);
    (void)harness.frame(down);
    (void)harness.frame(std::span{&move, 1});
    const auto release = harness.frame(std::span{&up, 1});
    CHECK(release.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
}

void check_canvas_drag_keeps_its_pointer_route(nuklear_event_harness_t& harness) {
    const std::array down{pointer_event(SDL_EVENT_MOUSE_MOTION, 805.0f, 260.0f),
                          pointer_event(SDL_EVENT_MOUSE_BUTTON_DOWN, 805.0f, 260.0f)};
    const auto move = pointer_event(SDL_EVENT_MOUSE_MOTION, 989.0f, 606.0f);
    const auto up = pointer_event(SDL_EVENT_MOUSE_BUTTON_UP, 989.0f, 606.0f);
    (void)harness.frame(down);
    const auto motion = harness.frame(std::span{&move, 1});
    const auto release = harness.frame(std::span{&up, 1});
    CHECK(motion.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
    CHECK(release.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
}

void check_hidden_view_menu_does_not_claim_canvas(nuklear_event_harness_t& harness) {
    const auto opened = click_text(harness, "View");
    REQUIRE(opened.has_value());
    static_cast<void>(harness.frame());
    REQUIRE(harness.drawn_text().find("Notifications") != std::string::npos);
    const auto dismissed = click(harness, 989.0f, 606.0f);
    CHECK_FALSE(opened_history(dismissed));
    static_cast<void>(harness.frame());
    CHECK_FALSE(opened_history(click(harness, 989.0f, 606.0f)));
}
} // namespace

TEST_CASE("Blank Compose canvas clicks cannot open notification history",
          "[unit][frontend-state][event-harness][notifications]") {
    nuklear_event_harness_t harness{1913, 1040};
    REQUIRE(harness.ready());
    configure_screenshot_compose_view(harness);
    set_unread_notifications(harness);
    (void)harness.frame(); // Establish the real Compose and status-bar hit layout.

    check_blank_canvas_grid(harness);
    check_blank_drag_does_not_transfer_to_badge(harness);
    check_canvas_drag_keeps_its_pointer_route(harness);

    // The rendered warning badge begins below this logical-pixel footer edge.
    CHECK_FALSE(opened_history(click(harness, 1115.5f, 1003.0f)));
}

TEST_CASE("Notification badge and View menu open history through their actual controls",
          "[unit][frontend-state][event-harness][notifications]") {
    nuklear_event_harness_t harness{1913, 1040};
    REQUIRE(harness.ready());
    configure_screenshot_compose_view(harness);
    set_unread_notifications(harness);
    (void)harness.frame();

    const auto badge = click_text(harness, "6 warnings · 0 errors");
    REQUIRE(badge.has_value());
    check_history_opens_on_click(*badge);

    wormedit::sys::command_shortcut_text_t menu_shortcuts_0;

    wormedit::sys::build_menu_commands_view(wormedit::command_registry_t::from({}), harness.view(), menu_shortcuts_0);
    const auto view_menu = click_text(harness, "View");
    REQUIRE(view_menu.has_value());
    static_cast<void>(harness.frame()); // Render the opened native menu before selecting an item.
    REQUIRE(harness.drawn_text().find("Notifications") != std::string::npos);
    const auto menu_item = click_text(harness, "Notifications");
    REQUIRE(menu_item.has_value());
    const auto count = std::ranges::count_if(*menu_item, [](const auto& frame) {
        return std::string_view{frame.get().menu_command_id} == "view.notifications";
    });
    CHECK(count == 1);
}

TEST_CASE("An already open Notification window does not claim blank Compose clicks",
          "[unit][frontend-state][event-harness][notifications]") {
    nuklear_event_harness_t harness{1913, 1040};
    REQUIRE(harness.ready());
    configure_screenshot_compose_view(harness);
    set_unread_notifications(harness);
    harness.view().notifications.history.visible = true;
    (void)harness.frame();
    const auto frames = click(harness, 990.0f, 600.0f);
    CHECK_FALSE(opened_history(frames));
    CHECK(frames[1].get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
}

TEST_CASE("New unread badge and dismissed View popup do not claim middle canvas input",
          "[unit][frontend-state][event-harness][notifications]") {
    nuklear_event_harness_t harness{1913, 1040};
    REQUIRE(harness.ready());
    configure_screenshot_compose_view(harness);
    (void)harness.frame(); // Establish footer geometry before the badge appears.
    set_unread_notifications(harness);
    const auto after_badge = harness.frame();
    CHECK(after_badge.get().notifications.kind == WORM_UI_NOTIFICATION_ACTION_NONE);

    const auto motion = pointer_event(SDL_EVENT_MOUSE_MOTION, 989.0f, 606.0f);
    CHECK(harness.frame(std::span{&motion, 1}).get().notifications.kind ==
          WORM_UI_NOTIFICATION_ACTION_NONE);
    check_hidden_view_menu_does_not_claim_canvas(harness);
}

TEST_CASE("Long transient status and unread badge leave the middle Compose canvas unowned",
          "[unit][frontend-state][event-harness][notifications]") {
    nuklear_event_harness_t harness{1913, 1040};
    REQUIRE(harness.ready());
    configure_screenshot_compose_view(harness);
    static_cast<void>(harness.frame());
    set_unread_notifications(harness);
    std::array<char, 512> status{};
    std::ranges::fill(status, 'w');
    status.back() = '\0';
    harness.view().notifications.current_text = status.data();
    (void)harness.frame();
    CHECK_FALSE(opened_history(click(harness, 989.0f, 606.0f)));
}

TEST_CASE("A dismissed badge cannot reopen history on release after its press action",
          "[unit][frontend-state][event-harness][notifications]") {
    nuklear_event_harness_t harness{1913, 1040};
    REQUIRE(harness.ready());
    configure_screenshot_compose_view(harness);
    set_unread_notifications(harness);
    (void)harness.frame();
    const auto badge = harness.drawn_text_center("6 warnings · 0 errors");
    REQUIRE(badge.has_value());

    const std::array down{pointer_event(SDL_EVENT_MOUSE_MOTION, badge->first, badge->second),
                          pointer_event(SDL_EVENT_MOUSE_BUTTON_DOWN, badge->first, badge->second)};
    CHECK(harness.frame(down).get().notifications.kind ==
          WORM_UI_NOTIFICATION_ACTION_OPEN_HISTORY);
    harness.view().compose.bgm_bar_reserved = false;
    harness.view().notifications.unread_warnings = 0;
    (void)harness.frame();
    const auto up = pointer_event(SDL_EVENT_MOUSE_BUTTON_UP, badge->first, badge->second);
    CHECK(harness.frame(std::span{&up, 1}).get().notifications.kind ==
          WORM_UI_NOTIFICATION_ACTION_NONE);
}

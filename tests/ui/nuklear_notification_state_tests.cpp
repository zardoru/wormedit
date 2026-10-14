#define NK_IMPLEMENTATION
#include "sys/workspace_view.h"
#include "ui/nuklear/notification_toasts.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <ranges>
#include <tuple>
#include <string>
#include <span>

namespace {
using namespace wormedit;
using namespace wormedit::ui::nuklear;
using namespace std::chrono_literals;

auto one_toast_state() -> notification_toast_state_t {
    notification_toast_state_t state;
    state.count = 1;
    state.ids[0] = 42;
    state.bounds[0] = {400, 400, 200, 80};
    state.close_bounds[0] = {580, 402, 20, 18};
    return state;
}

auto blocked_escape_contexts(const WormUiNotificationView& view) {
    return std::array{
        notification_escape_target(view, false, false, false, false, false),
        notification_escape_target(view, true, true, false, false, false),
        notification_escape_target(view, true, false, true, false, false),
        notification_escape_target(view, true, false, false, true, false),
        notification_escape_target(view, true, false, false, false, true)};
}

auto notification_test_text_width(nk_handle, float, const char*, int length) -> float {
    return static_cast<float>(length) * 9.0f;
}

struct notification_test_context_t {
    nk_user_font font{};
    nk_context context{};
    notification_test_context_t() {
        font.height = 15;
        font.width = notification_test_text_width;
        REQUIRE(nk_init_default(&context, &font));
    }
    ~notification_test_context_t() { nk_free(&context); }
};

auto rendered_message_bytes(nk_context& context) -> std::size_t {
    std::size_t bytes{};
    const nk_command* command;
    nk_foreach(command, &context) {
        if (command->type != NK_COMMAND_TEXT) continue;
        const auto* text = reinterpret_cast<const nk_command_text*>(command);
        const auto characters = std::span{text->string, static_cast<std::size_t>(text->length)};
        if (std::ranges::all_of(characters, [](char ch) { return ch == 'W'; }))
            bytes += characters.size();
    }
    return bytes;
}

auto message_commands_fit_view(nk_context& context, int width, int height) -> bool {
    struct nk_rect clip = nk_rect(0, 0, static_cast<float>(width), static_cast<float>(height) - 42);
    const nk_command* command;
    nk_foreach(command, &context) {
        if (command->type == NK_COMMAND_SCISSOR) {
            const auto* scissor = reinterpret_cast<const nk_command_scissor*>(command);
            clip = nk_rect(scissor->x, scissor->y, scissor->w, scissor->h);
        }
        if (command->type != NK_COMMAND_TEXT) continue;
        const auto* text = reinterpret_cast<const nk_command_text*>(command);
        const auto characters = std::span{text->string, static_cast<std::size_t>(text->length)};
        if (!std::ranges::all_of(characters, [](char ch) { return ch == 'W'; })) continue;
        if (text->x < 0 || text->y < 0 || text->x + text->w > width ||
            text->y + text->h > height - 42) return false;
        if (text->x < clip.x || text->y < clip.y || text->x + text->w > clip.x + clip.w ||
            text->y + text->h > clip.y + clip.h) return false;
    }
    return true;
}

void check_overlay_focus_frame(nk_context& primary, nk_context& overlay,
                                const WormUiNotificationView& view,
                                notification_toast_state_t& state) {
    nk_input_begin(&primary);
    nk_input_motion(&primary, 1150, 650);
    nk_input_char(&primary, '7');
    nk_input_end(&primary);
    nk_begin(&primary, "editor", nk_rect(0, 0, 1280, 792), 0);
    nk_layout_row_dynamic(&primary, 25, 1);
    nk_label(&primary, "Editor", NK_TEXT_LEFT);
    nk_end(&primary);
    const auto* active = primary.active;
    REQUIRE(active);
    const auto flags = active->flags;
    render_notification_overlay(&primary, &overlay, view, state, 1280, 792);
    CHECK(primary.active == active);
    CHECK(active->flags == flags);
    CHECK((primary.input.keyboard.text_len == 1 && primary.input.keyboard.text[0] == '7'));
    CHECK(rendered_message_bytes(overlay) == 400);
    CHECK(message_commands_fit_view(overlay, 1280, 792));
    nk_clear(&primary);
    nk_clear(&overlay);
}
} // namespace

TEST_CASE("notification projection borrows text and bounds the newest three toasts",
          "[unit][notifications][frontend]") {
    notification_time_t now{};
    notification_center_t center{[&now] { return now; }};
    for (int index = 0; index < 5; ++index)
        center.post(notification_severity_t::warning, notification_source_t::system,
                     "Warning " + std::to_string(index));
    center.post(notification_severity_t::hint, notification_source_t::input, "Hint");
    center.drain_inbox(now);
    const auto view = sys::build_notification_view(center, now);
    const auto entries = center.borrowed_snapshot(notification_capacity);
    REQUIRE(view.toast_count == 3);
    CHECK(std::tuple{view.unread_warnings, view.unread_errors} ==
          std::tuple{std::size_t{5}, std::size_t{0}});
    CHECK(view.current_text == center.current_line()->message.c_str());
    CHECK((view.toasts[0].message == entries[4].message.c_str() &&
           view.toasts[2].id == entries[2].id));
}

TEST_CASE("notification warnings expire at eight seconds while errors remain",
          "[unit][notifications][frontend]") {
    notification_time_t now{};
    notification_center_t center{[&now] { return now; }};
    center.post(notification_severity_t::warning, notification_source_t::system, "Warning");
    center.post(notification_severity_t::error, notification_source_t::system, "Error");
    center.drain_inbox(now);
    REQUIRE(sys::build_notification_view(center, 7999ms).toast_count == 2);
    const auto expired = sys::build_notification_view(center, 8s);
    REQUIRE(expired.toast_count == 1);
    CHECK(expired.toasts[0].severity == WORM_UI_NOTIFICATION_ERROR);
    CHECK(expired.unread_warnings == 1);
}

TEST_CASE("notification repeats refresh warning lifetime and dismissal is persistent",
          "[unit][notifications][frontend]") {
    notification_time_t now{};
    notification_center_t center{[&now] { return now; }};
    center.post(notification_severity_t::warning, notification_source_t::system, "Warning");
    center.drain_inbox(now);
    now = 1500ms;
    center.post(notification_severity_t::warning, notification_source_t::system, "Warning");
    center.drain_inbox(now);
    const auto refreshed = sys::build_notification_view(center, 8s);
    REQUIRE(refreshed.toast_count == 1);
    CHECK(refreshed.toasts[0].repeat_count == 2);
    REQUIRE(center.dismiss(refreshed.toasts[0].id));
    const auto dismissed = sys::build_notification_view(center, 8s);
    CHECK((dismissed.toast_count == 0 && dismissed.unread_warnings == 0 &&
           dismissed.current_text == nullptr));
}

TEST_CASE("notification pointer ownership stays inside toasts and preserves existing gestures",
          "[unit][notifications][frontend]") {
    const auto state = one_toast_state();
    CHECK((notification_pointer_owned(state, 410, 410, false) &&
           !notification_pointer_owned(state, 410, 410, true) &&
           !notification_pointer_owned(state, 399, 410, false) &&
           !notification_pointer_owned(state, 600, 410, false)));
}

TEST_CASE("notification close captures only its click and emits one dismissal",
          "[unit][notifications][frontend]") {
    auto state = one_toast_state();
    CHECK((!notification_pointer_edge(state, 590, 410, 1, true, false) &&
           !notification_pointer_edge(state, 410, 390, 1, true, true)));
    REQUIRE(notification_pointer_edge(state, 590, 410, 1, true, true));
    CHECK(notification_pointer_owned(state, 20, 20, false));
    REQUIRE(notification_pointer_edge(state, 590, 410, 1, false, false));
    const auto action = notification_take_action(state);
    CHECK((action.kind == WORM_UI_NOTIFICATION_ACTION_DISMISS && action.id == 42 &&
           notification_take_action(state).kind == WORM_UI_NOTIFICATION_ACTION_NONE));
}

TEST_CASE("notification disappearance and focus loss release capture without dismissal",
          "[unit][notifications][frontend]") {
    auto state = one_toast_state();
    REQUIRE(notification_pointer_edge(state, 590, 410, 1, true, true));
    state.count = 0;
    REQUIRE(notification_pointer_edge(state, 20, 20, 1, false, false));
    CHECK(state.captured_id == 0);
    CHECK(notification_take_action(state).kind == WORM_UI_NOTIFICATION_ACTION_NONE);
    state.captured_id = 42;
    state.captured_buttons = 1;
    state.captured_close = true;
    state.dismiss_pending = 42;
    notification_focus_lost(state);
    CHECK((state.captured_id == 0 && state.captured_buttons == 0 &&
           !state.captured_close && state.dismiss_pending == 0));
}

TEST_CASE("notification Escape respects text IME gesture modal and window focus ownership",
          "[unit][notifications][frontend]") {
    WormUiNotificationView view{};
    view.toast_count = 2;
    view.toasts[0].id = 42;
    view.toasts[1].id = 7;
    CHECK(notification_escape_target(view, true, false, false, false, false) == 42);
    const auto blocked = blocked_escape_contexts(view);
    CHECK(std::ranges::all_of(blocked, [](const auto& target) { return !target; }));
    view.toast_count = 0;
    CHECK_FALSE(notification_escape_target(view, true, false, false, false, false));
}

TEST_CASE("notification body presses never dismiss when released over the close control",
          "[unit][notifications][frontend]") {
    auto state = one_toast_state();
    REQUIRE(notification_pointer_edge(state, 410, 410, 1, true, true));
    CHECK(notification_pointer_owned(state, 20, 20, false));
    REQUIRE(notification_pointer_edge(state, 590, 410, 1, false, false));
    CHECK((notification_take_action(state).kind == WORM_UI_NOTIFICATION_ACTION_NONE &&
           !notification_pointer_owned(state, 20, 20, false)));
}

TEST_CASE("notification captures matching releases for every pressed mouse button",
          "[unit][notifications][frontend]") {
    auto state = one_toast_state();
    REQUIRE(notification_pointer_edge(state, 410, 410, 3, true, true));
    REQUIRE(notification_pointer_edge(state, 20, 20, 1, true, false));
    const auto first_release = notification_pointer_edge(state, 20, 20, 3, false, false);
    const auto still_owned = notification_pointer_owned(state, 20, 20, false);
    const auto last_release = notification_pointer_edge(state, 20, 20, 1, false, false);
    CHECK((first_release && still_owned && last_release &&
           !notification_pointer_owned(state, 20, 20, false)));
    CHECK(notification_take_action(state).kind == WORM_UI_NOTIFICATION_ACTION_NONE);
}

TEST_CASE("notification toast geometry leaves the status bar accessible",
          "[unit][notifications][frontend]") {
    const auto newest = notification_toast_bounds(1024, 768, 0);
    const auto older = notification_toast_bounds(1024, 768, 2);
    CHECK((newest.x >= 0 && newest.x + newest.width <= 1024 &&
           newest.y + newest.height <= 768 - 30 && older.y >= 0));
}

TEST_CASE("notification toast geometry shows the newest card in small viewports",
          "[unit][notifications][frontend]") {
    const auto small = notification_toast_bounds(200, 80, 0);
    CHECK((small.y >= 0 && small.height > 0 && small.y + small.height <= 80 - 30));
    CHECK(notification_toast_bounds(200, 80, 1).height == 0);
    CHECK(notification_toast_bounds(200, 250, 0).height > 0);
    CHECK(notification_toast_bounds(200, 250, 2).height == 0);
    CHECK(notification_toast_bounds(200, 30, 0).height == 0);
}

TEST_CASE("notification renderer isolates initial windows and hover details from editor focus",
          "[unit][notifications][frontend]") {
    notification_test_context_t primary;
    notification_test_context_t overlay;
    const std::string message(200, 'W');
    WormUiNotificationView view{};
    view.toast_count = 1;
    view.toasts[0] = {42, WORM_UI_NOTIFICATION_ERROR, message.c_str(), 1};
    notification_toast_state_t state{};
    for (int frame = 0; frame < 3; ++frame)
        check_overlay_focus_frame(primary.context, overlay.context, view, state);
}

TEST_CASE("notification overlay allocation failure clears invisible input ownership",
          "[unit][notifications][frontend]") {
    notification_test_context_t primary;
    auto state = one_toast_state();
    state.captured_buttons = 1;
    state.dismiss_pending = 42;
    WormUiNotificationView view{};
    view.toast_count = 1;
    view.toasts[0] = {42, WORM_UI_NOTIFICATION_ERROR, "Error", 1};
    render_notification_overlay(&primary.context, nullptr, view, state, 1280, 792);
    CHECK(state.count == 0);
    CHECK_FALSE(notification_pointer_owned(state, 590, 410, false));
    CHECK_FALSE(notification_pointer_edge(state, 590, 410, 1, true, true));
    CHECK(notification_take_action(state).kind == WORM_UI_NOTIFICATION_ACTION_NONE);
}

TEST_CASE("notification hover preserves keyboard-owned editor popup rendering",
          "[unit][notifications][frontend]") {
    auto state = one_toast_state();
    CHECK(notification_content_occluded(state, 590, 410, false, false));
    CHECK_FALSE(notification_content_occluded(state, 590, 410, false, true));
    CHECK(notification_pointer_edge(state, 590, 410, 1, true, true));
    CHECK(notification_pointer_edge(state, 590, 410, 1, false, true));
    CHECK(notification_take_action(state).id == 42);
}

TEST_CASE("notification wrapped detail stays inside a small viewport above status",
          "[unit][notifications][frontend]") {
    notification_test_context_t primary;
    notification_test_context_t overlay;
    const std::string message(200, 'W');
    WormUiNotificationView view{};
    view.toast_count = 1;
    view.toasts[0] = {42, WORM_UI_NOTIFICATION_ERROR, message.c_str(), 1};
    notification_toast_state_t state{};
    nk_input_begin(&primary.context);
    nk_input_motion(&primary.context, 370, 60);
    nk_input_end(&primary.context);
    render_notification_overlay(&primary.context, &overlay.context, view, state, 400, 200);
    CHECK(rendered_message_bytes(overlay.context) >= 200);
    CHECK(message_commands_fit_view(overlay.context, 400, 200));
}

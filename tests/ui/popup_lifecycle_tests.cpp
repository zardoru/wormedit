#include "ui/nuklear/popup_lifecycle.h"

#include <catch2/catch_test_macros.hpp>

#include <source_location>

namespace {
void check_lifecycle(bool condition, const std::source_location location =
                                         std::source_location::current()) {
    INFO("assertion caller line " << location.line());
    CHECK(condition);
}
} // namespace

TEST_CASE("Popup lifecycle compares the complete captured context",
          "[frontend-state][popup-lifecycle]") {
    using wormedit::ui::chrome::PopupContextStamp;
    using wormedit::ui::chrome::popup_context_matches;
    const PopupContextStamp captured{.project_id = 11, .chart_id = 7, .revision = 90,
                                     .object_id = 42};
    check_lifecycle(popup_context_matches(captured, captured));
    check_lifecycle(!popup_context_matches(captured, {.project_id = 12, .chart_id = 7,
                                                      .revision = 90, .object_id = 42}));
    check_lifecycle(!popup_context_matches(captured, {.project_id = 11, .chart_id = 8,
                                                      .revision = 90, .object_id = 42}));
    check_lifecycle(!popup_context_matches(captured, {.project_id = 11, .chart_id = 7,
                                                      .revision = 91, .object_id = 42}));
    check_lifecycle(!popup_context_matches(captured, {.project_id = 11, .chart_id = 7,
                                                      .revision = 90, .object_id = 43}));
}

TEST_CASE("Popup lifecycle holds input through edges and releases on a quiet frame",
          "[frontend-state][popup-lifecycle]") {
    using wormedit::ui::chrome::popup_arm_release;
    using wormedit::ui::chrome::popup_owns_input;
    using wormedit::ui::chrome::popup_update_release_pending;
    nk_context context{};
    bool release_pending = false;
    popup_arm_release(release_pending);
    check_lifecycle(popup_owns_input(false, release_pending));

    context.input.mouse.buttons[NK_BUTTON_RIGHT].down = nk_true;
    context.input.mouse.buttons[NK_BUTTON_RIGHT].clicked = 1;
    check_lifecycle(popup_update_release_pending(&context, release_pending));
    context.input.mouse.buttons[NK_BUTTON_RIGHT].down = nk_false;
    check_lifecycle(popup_update_release_pending(&context, release_pending));
    context.input.mouse.buttons[NK_BUTTON_RIGHT].clicked = nk_false;
    check_lifecycle(!popup_update_release_pending(&context, release_pending));
    check_lifecycle(!release_pending);
    check_lifecycle(!popup_owns_input(false, release_pending));
    check_lifecycle(popup_owns_input(true, false));
}

TEST_CASE("Popup dismissal detects either pointer button and Escape",
          "[frontend-state][popup-lifecycle]") {
    using wormedit::ui::chrome::popup_dismiss_requested;
    nk_context context{};
    const auto bounds = nk_rect(20, 20, 100, 80);
    context.input.mouse.pos = nk_vec2(5, 5);
    context.input.mouse.buttons[NK_BUTTON_LEFT].down = nk_true;
    context.input.mouse.buttons[NK_BUTTON_LEFT].clicked = nk_true;
    check_lifecycle(popup_dismiss_requested(&context, bounds));
    context.input.mouse.buttons[NK_BUTTON_LEFT].down = nk_false;
    context.input.mouse.buttons[NK_BUTTON_LEFT].clicked = nk_false;
    context.input.mouse.buttons[NK_BUTTON_RIGHT].down = nk_true;
    context.input.mouse.buttons[NK_BUTTON_RIGHT].clicked = nk_true;
    check_lifecycle(popup_dismiss_requested(&context, bounds));
    context.input.mouse.buttons[NK_BUTTON_RIGHT].down = nk_false;
    context.input.mouse.buttons[NK_BUTTON_RIGHT].clicked = nk_false;
    context.input.mouse.buttons[NK_BUTTON_LEFT].down = nk_false;
    context.input.mouse.buttons[NK_BUTTON_LEFT].clicked = nk_true;
    check_lifecycle(!popup_dismiss_requested(&context, bounds));
    context.input.mouse.buttons[NK_BUTTON_LEFT].clicked = 2;
    check_lifecycle(popup_dismiss_requested(&context, bounds));
    context.input.mouse.buttons[NK_BUTTON_LEFT].clicked = nk_false;
    context.input.mouse.pos = nk_vec2(50, 50);
    context.input.keyboard.keys[NK_KEY_TEXT_RESET_MODE].down = nk_true;
    context.input.keyboard.keys[NK_KEY_TEXT_RESET_MODE].clicked = nk_true;
    check_lifecycle(popup_dismiss_requested(&context, bounds));
    context.input.keyboard.keys[NK_KEY_TEXT_RESET_MODE].down = nk_false;
    context.input.keyboard.keys[NK_KEY_TEXT_RESET_MODE].clicked = nk_false;
    check_lifecycle(!popup_dismiss_requested(&context, bounds));
}

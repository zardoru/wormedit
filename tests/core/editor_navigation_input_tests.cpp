#include <wormedit/core/editor_navigation_input.h>

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;

TEST_CASE("editor navigation repeats deterministically and accelerates",
          "[unit][core][navigation-input]") {
    editor_navigation_input_controller_t controller({
        .initial_repeat_delay_milliseconds = 100,
        .repeat_interval_milliseconds = 50,
        .accelerate_after_milliseconds = 250,
        .accelerated_repeat_interval_milliseconds = 10,
    });

    const auto initial = controller.press(editor_navigation_action_t::page_forward, 1'000);
    REQUIRE(initial.count == 1);
    CHECK(initial[0] == editor_navigation_action_t::page_forward);
    CHECK(controller.advance(1'099).is_empty());
    REQUIRE(controller.advance(1'100).count == 1);
    REQUIRE(controller.advance(1'200).count == 2);
    // At 1250ms the accelerated interval applies; a long delayed frame is
    // bounded to the fixed action-batch capacity.
    REQUIRE(controller.advance(1'300).count ==
            maximum_editor_navigation_actions_per_advance);
    CHECK(controller.advance(1'300).is_empty());
}

TEST_CASE("editor navigation defaults honor the one and three second thresholds",
          "[unit][core][navigation-input]") {
    editor_navigation_input_controller_t controller;
    CHECK(controller.settings().initial_repeat_delay_milliseconds == 1'000);
    CHECK(controller.settings().accelerate_after_milliseconds == 3'000);
    REQUIRE(controller.press(editor_navigation_action_t::cursor_forward, 0).count == 1);
    CHECK(controller.advance(999).is_empty());
    REQUIRE(controller.advance(1'000).count == 1);
}

TEST_CASE("editor navigation ignores native duplicates and cancels on context changes",
          "[unit][core][navigation-input]") {
    editor_navigation_input_controller_t controller;
    REQUIRE(controller.press(editor_navigation_action_t::cursor_backward, 10).count == 1);
    CHECK(controller.press(editor_navigation_action_t::cursor_backward, 11).is_empty());
    controller.release(editor_navigation_action_t::cursor_forward);
    CHECK(controller.is_active());
    controller.focus_lost();
    CHECK_FALSE(controller.is_active());
    CHECK(controller.advance(1'000).is_empty());

    REQUIRE(controller.press(editor_navigation_action_t::page_backward, 20).count == 1);
    controller.text_input_started();
    CHECK_FALSE(controller.is_active());
    REQUIRE(controller.press(editor_navigation_action_t::cursor_forward, 30).count == 1);
    controller.modal_opened();
    CHECK_FALSE(controller.is_active());
}

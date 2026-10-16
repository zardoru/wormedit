#include "ui/field/rect.h"
#include <type_traits>
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
    using wormedit::ui::chrome::retained_popup_t;
    using wormedit::ui::chrome::popup_update_release_pending;
    nk_context context{};
    bool release_pending = false;
    popup_arm_release(release_pending);
    check_lifecycle(retained_popup_t{false, release_pending}.owns_input());

    context.input.mouse.buttons[NK_BUTTON_RIGHT].down = nk_true;
    context.input.mouse.buttons[NK_BUTTON_RIGHT].clicked = 1;
    check_lifecycle(popup_update_release_pending(&context, release_pending));
    context.input.mouse.buttons[NK_BUTTON_RIGHT].down = nk_false;
    check_lifecycle(popup_update_release_pending(&context, release_pending));
    context.input.mouse.buttons[NK_BUTTON_RIGHT].clicked = nk_false;
    check_lifecycle(!popup_update_release_pending(&context, release_pending));
    check_lifecycle(!release_pending);
    check_lifecycle(!retained_popup_t{false, release_pending}.owns_input());
    check_lifecycle(retained_popup_t{true, false}.owns_input());
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

TEST_CASE("Retained popups and view stamps remain aggregates with shared queries",
          "[frontend-state][popup-lifecycle]") {
    using namespace wormedit::ui::chrome;
    STATIC_REQUIRE(std::is_aggregate_v<retained_popup_t>);
    retained_popup_t popup;
    check_lifecycle(!popup.owns_input());
    popup.open = true;
    check_lifecycle(popup.owns_input());
    popup.open = false;
    popup.release_pending = true;
    check_lifecycle(popup.owns_input());
    WormUiComposeView view{};
    view.project_id = 11;
    view.chart_id = 7;
    view.document_revision = 90;
    check_lifecycle(popup_context_matches(PopupContextStamp::from(view, 42), {11, 7, 90, 42}));
    WormUiSetupView setup{};
    setup.song_id = 12;
    setup.session_generation = 8;
    setup.document_revision = 91;
    check_lifecycle(popup_context_matches(PopupContextStamp::from(setup, 43), {12, 8, 91, 43}));
}

TEST_CASE("SDL rectangle helpers preserve inclusive half-open and clipped-empty edges",
          "[frontend-state][popup-lifecycle]") {
    using namespace wormedit::ui;
    const SDL_FRect rect{10, 20, 30, 40};
    check_lifecycle(is_point_in_rect(rect, 10, 20));
    check_lifecycle(!is_point_in_rect(rect, 40, 60));
    check_lifecycle(is_point_in_rect(rect, 40, 60, rect_edges_t::inclusive));
    check_lifecycle(!has_rect_overlap(rect, {40, 20, 30, 40}));
    check_lifecycle(has_rect_overlap(rect, {39, 20, 30, 40}));
    const auto disjoint = rect_intersection(rect, {50, 30, 20, 20});
    check_lifecycle(disjoint.x == 50);
    check_lifecycle(disjoint.y == 30);
    check_lifecycle(disjoint.w == 0);
    check_lifecycle(disjoint.h == 20);
    const auto overlap = rect_intersection(rect, {20, 30, 40, 50});
    check_lifecycle(overlap.x == 20);
    check_lifecycle(overlap.y == 30);
    check_lifecycle(overlap.w == 20);
    check_lifecycle(overlap.h == 30);
}

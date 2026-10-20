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
    wormedit::ui::setup_view_t setup{};
    setup.song_id = wormedit::song_id_t{12};
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

namespace {
void require_lifecycle(bool value) { REQUIRE(value); }

auto popup_glyph_width(nk_handle, float, const char*, int length) -> float {
    return static_cast<float>(length) * 8.0f;
}

struct retained_popup_fixture_t {
    nk_user_font font{};
    nk_context context{};
    wormedit::ui::chrome::retained_popup_t state{.open = true};
    nk_window* host{};

    retained_popup_fixture_t() {
        font.height = 15.0f;
        font.width = popup_glyph_width;
        require_lifecycle(nk_init_default(&context, &font));
        nk_input_begin(&context);
        nk_input_end(&context);
        require_lifecycle(nk_begin(&context, "Retained host", nk_rect(0, 0, 800, 600),
                                   NK_WINDOW_NO_SCROLLBAR));
        host = context.current;
    }
    ~retained_popup_fixture_t() { nk_end(&context); nk_free(&context); }
    retained_popup_fixture_t(const retained_popup_fixture_t&) = delete;
    auto operator=(const retained_popup_fixture_t&) -> retained_popup_fixture_t& = delete;

    auto begin(wormedit::ui::chrome::retained_popup_begin_failure_t failure)
        -> wormedit::ui::chrome::retained_popup_frame_t {
        return wormedit::ui::chrome::begin_retained_popup(&context, state, "Retained picker",
            NK_WINDOW_BORDER, nk_rect(100, 100, 400, 300), failure);
    }
    void block_begin() {
        require_lifecycle(nk_popup_begin(&context, NK_POPUP_STATIC, "Other popup",
            NK_WINDOW_BORDER, nk_rect(50, 50, 200, 200)));
        nk_popup_end(&context);
    }
};
} // namespace

TEST_CASE("Retained picker release returns frame ownership before all-key quietness retires it",
          "[frontend-state][popup-lifecycle]") {
    using namespace wormedit::ui::chrome;
    nk_context context{};
    retained_popup_t state{.release_pending = true};
    context.input.keyboard.keys[NK_KEY_SHIFT].down = true;
    check_lifecycle(release_retained_popup(&context, state, PopupReleaseKeys::all));
    check_lifecycle(state.release_pending);
    context.input.keyboard.keys[NK_KEY_SHIFT].down = false;
    context.input.keyboard.keys[NK_KEY_SHIFT].clicked = 1;
    check_lifecycle(release_retained_popup(&context, state, PopupReleaseKeys::all));
    check_lifecycle(state.release_pending);
    context.input.keyboard.keys[NK_KEY_SHIFT].clicked = 0;
    check_lifecycle(release_retained_popup(&context, state, PopupReleaseKeys::all));
    check_lifecycle(!state.owns_input());
    check_lifecycle(!release_retained_popup(&context, state, PopupReleaseKeys::all));
}

TEST_CASE("Retained picker begin failures preserve their explicit cancel and retain policies",
          "[frontend-state][popup-lifecycle]") {
    using namespace wormedit::ui::chrome;
    for (const auto failure : {retained_popup_begin_failure_t::cancel,
                               retained_popup_begin_failure_t::retain}) {
        for (const bool guarded : {false, true}) {
            retained_popup_fixture_t fixture;
            fixture.state.release_pending = guarded;
            fixture.state.close_pending = true;
            fixture.block_begin();
            auto popup = fixture.begin(failure);
            const bool canceled = failure == retained_popup_begin_failure_t::cancel;
            check_lifecycle(!popup.has_contents());
            check_lifecycle(popup.owns_input() == (canceled || guarded));
            check_lifecycle(fixture.state.open == !canceled);
            check_lifecycle(fixture.state.release_pending == canceled);
            check_lifecycle(!fixture.state.close_pending);
            end_retained_popup(&fixture.context, fixture.state, popup,
                               retained_popup_edit_focus_t::unfocus_on_close,
                               retained_popup_close_trigger_t::closed);
            check_lifecycle(fixture.context.current == fixture.host);
        }
    }
}

TEST_CASE("Retained picker terminal frames close and end once with explicit edit focus policy",
          "[frontend-state][popup-lifecycle]") {
    using namespace wormedit::ui::chrome;
    for (const auto focus : {retained_popup_edit_focus_t::preserve,
                             retained_popup_edit_focus_t::unfocus_on_close}) {
        retained_popup_fixture_t fixture;
        auto popup = fixture.begin(retained_popup_begin_failure_t::retain);
        require_lifecycle(popup.has_contents());
        check_lifecycle(popup.owns_input());
        auto* native_popup = fixture.context.current;
        native_popup->edit.active = 1;
        fixture.state.cancel();
        end_retained_popup(&fixture.context, fixture.state, popup, focus,
                           retained_popup_close_trigger_t::closed_or_pending);
        check_lifecycle(fixture.context.current == fixture.host);
        check_lifecycle(!fixture.state.open && fixture.state.release_pending);
        check_lifecycle(!fixture.state.close_pending);
        check_lifecycle(native_popup->edit.active ==
            (focus == retained_popup_edit_focus_t::preserve ? 1 : 0));
        end_retained_popup(&fixture.context, fixture.state, popup, focus,
                           retained_popup_close_trigger_t::closed_or_pending);
        check_lifecycle(fixture.context.current == fixture.host);
    }
}

TEST_CASE("Retained picker close triggers distinguish pending native retirement from closed state",
          "[frontend-state][popup-lifecycle]") {
    using namespace wormedit::ui::chrome;
    for (const auto trigger : {retained_popup_close_trigger_t::closed,
                               retained_popup_close_trigger_t::closed_or_pending}) {
        retained_popup_fixture_t fixture;
        auto popup = fixture.begin(retained_popup_begin_failure_t::retain);
        require_lifecycle(popup.has_contents());
        fixture.state.close_pending = true;
        end_retained_popup(&fixture.context, fixture.state, popup,
                           retained_popup_edit_focus_t::preserve, trigger);
        const bool closed = trigger == retained_popup_close_trigger_t::closed_or_pending;
        check_lifecycle(fixture.state.open);
        check_lifecycle(fixture.state.close_pending == !closed);
        check_lifecycle((fixture.host->popup.active != 0) == !closed);
        check_lifecycle(fixture.context.current == fixture.host);
    }
}

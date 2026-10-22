#include "ui/nuklear/chrome/notification_panel.h"

#include <catch2/catch_test_macros.hpp>
#include <array>
#include <string>
#include <string_view>

using namespace wormedit::ui::nuklear;

namespace {
auto history_row() -> wormedit::ui::notification_history_row_view_t {
    return {1, wormedit::notification_severity_t::warning, wormedit::notification_source_t::document, "Warning", "Details", 2, 3, false,
            WORM_UI_NOTIFICATION_TARGET_POSITION};
}
}

TEST_CASE("notification history owns its complete row list", "[notifications][frontend]") {
    wormedit::ui::notification_history_view_t view{true, {history_row()}};
    view.rows.resize(257, history_row());
    CHECK(notification_history_view_valid(view));
    CHECK(view.rows.size() == 257);
    view.rows.clear();
    CHECK(notification_history_view_valid(view));
}

TEST_CASE("notification history rejects invalid public row fields", "[notifications][frontend]") {
    wormedit::ui::notification_history_view_t view{true, {history_row()}};
    auto& row = view.rows.front();
    SECTION("identity") { row.id = 0; }
    SECTION("source") { row.source = static_cast<wormedit::notification_source_t>(9); }
    SECTION("severity") { row.severity = static_cast<wormedit::notification_severity_t>(5); }
    SECTION("target") { row.target = static_cast<WormUiNotificationTargetAvailability>(4); }
    CHECK_FALSE(notification_history_view_valid(view));
}

TEST_CASE("notification history filters every severity and retains all in default view",
          "[notifications][frontend]") {
    notification_panel_state_t state;
    auto row = history_row();
    for (int severity = 0; severity < 5; ++severity) {
        row.severity = static_cast<wormedit::notification_severity_t>(severity);
        state.severity_filter = 0;
        CHECK(notification_row_visible(state, row));
        for (int filter = 1; filter <= 5; ++filter) {
            state.severity_filter = filter;
            CHECK(notification_row_visible(state, row) == (filter == severity + 1));
        }
    }
}

namespace {
auto history_text_width(nk_handle, float, const char*, int length) -> float {
    return static_cast<float>(length) * 8;
}

struct history_render_context_t {
    nk_user_font font{};
    nk_context context{};
    history_render_context_t() {
        font.height = 15;
        font.width = history_text_width;
        REQUIRE(nk_init_default(&context, &font));
    }
    ~history_render_context_t() { nk_free(&context); }
};

auto history_rendered_text(nk_context& context, const char* expected) -> bool {
    const nk_command* command;
    nk_foreach(command, &context) {
        if (command->type != NK_COMMAND_TEXT) continue;
        const auto* text = reinterpret_cast<const nk_command_text*>(command);
        if (std::string_view{text->string, static_cast<std::size_t>(text->length)}.find(expected) !=
            std::string_view::npos) return true;
    }
    return false;
}

void check_locked_history_primary(nk_context& primary, const nk_window* active, nk_flags flags) {
    CHECK(primary.active == active);
    CHECK(active->flags == flags);
    CHECK((primary.input.keyboard.text_len == 1 && primary.input.keyboard.text[0] == '7'));
}
}

TEST_CASE("locked history uses secondary context without altering primary focus ROM or typing",
          "[notifications][frontend][renderer]") {
    history_render_context_t primary;
    history_render_context_t overlay;
    nk_input_begin(&primary.context);
    nk_input_char(&primary.context, '7');
    nk_input_end(&primary.context);
    nk_begin(&primary.context, "editor", nk_rect(0, 0, 1000, 700), 0);
    nk_layout_row_dynamic(&primary.context, 25, 1);
    nk_label(&primary.context, "Editor", NK_TEXT_LEFT);
    nk_end(&primary.context);
    const auto* active = primary.context.active;
    REQUIRE(active);
    const auto flags = active->flags;
    overlay.context.style = primary.context.style;
    auto row = history_row();
    row.detail = "first line\nsecond line";
    notification_panel_state_t state;
    state.expanded_id = row.id;
    wormedit::ui::notification_action_t action{};
    render_notification_panel(&overlay.context, {true, {row}}, state, action, 1000, 700, false);
    check_locked_history_primary(primary.context, active, flags);
    CHECK(history_rendered_text(overlay.context, "first line"));
    CHECK(history_rendered_text(overlay.context, "second line"));
    CHECK(action.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
}

TEST_CASE("expanded history retains complete bounded detail in static scroll group",
          "[notifications][frontend][renderer]") {
    history_render_context_t rendered;
    auto row = history_row();
    const std::string detail(4096, 'D');
    row.detail = detail.c_str();
    notification_panel_state_t state;
    state.expanded_id = row.id;
    wormedit::ui::notification_action_t action{};
    render_notification_panel(&rendered.context, {true, {row}}, state, action, 1000, 700);
    CHECK(std::string_view{state.detail_text.data()} == detail);
    CHECK(nk_window_is_active(&rendered.context, "Notifications"));
    CHECK(action.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
}

namespace {
void draw_history_detail_frame(history_render_context_t& rendered, const wormedit::ui::notification_history_row_view_t& row,
                               notification_panel_state_t& state, bool scroll) {
    nk_clear(&rendered.context);
    nk_input_begin(&rendered.context);
    nk_input_motion(&rendered.context, 100, 300);
    if (scroll) nk_input_scroll(&rendered.context, nk_vec2(0, -100));
    nk_input_end(&rendered.context);
    wormedit::ui::notification_action_t action{};
    render_notification_panel(&rendered.context, {true, {row}}, state, action, 1000, 700);
    CHECK(action.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
}
}

TEST_CASE("expanded history wheel scroll reaches final line without editing the payload",
          "[notifications][frontend][renderer]") {
    history_render_context_t rendered;
    auto row = history_row();
    std::string detail;
    for (int line = 0; line < 60; ++line) detail += "A visible detail line\n";
    detail += "FINAL DETAIL LINE";
    row.detail = detail.c_str();
    notification_panel_state_t state;
    state.expanded_id = row.id;
    draw_history_detail_frame(rendered, row, state, false);
    CHECK_FALSE(history_rendered_text(rendered.context, "FINAL DETAIL LINE"));
    draw_history_detail_frame(rendered, row, state, true);
    draw_history_detail_frame(rendered, row, state, false);
    CHECK(history_rendered_text(rendered.context, "FINAL DETAIL LINE"));
    CHECK(std::string_view{state.detail_text.data()} == detail);
}

namespace {
void check_history_pointer_pair(std::uint32_t button, bool interactive) {
    notification_panel_state_t state;
    const auto bounds = nk_rect(10, 10, 100, 100);
    const auto expected = interactive ? notification_history_pointer_route_t::interactive
                                      : notification_history_pointer_route_t::blocked;
    CHECK(notification_history_pointer_edge(state, bounds, 20, 20, button, true, true,
                                             interactive) == expected);
    state.pointer_owned_this_frame = false;
    CHECK(notification_history_pointer_edge(state, bounds, 900, 900, button, false, false,
                                             !interactive) == expected);
    CHECK(state.pointer_owned_this_frame);
    CHECK(state.pointer_routes[button - 1] == notification_history_pointer_route_t::none);
}
}

TEST_CASE("history captures every button and its matching release across focus ownership changes",
          "[notifications][frontend][input]") {
    for (std::uint32_t button = 1; button <= 32; ++button) {
        check_history_pointer_pair(button, true);
        check_history_pointer_pair(button, false);
    }
}

TEST_CASE("history leaves external gestures and invisible locked views uncaptured",
          "[notifications][frontend][input]") {
    notification_panel_state_t state;
    const auto bounds = nk_rect(10, 10, 100, 100);
    CHECK(notification_history_pointer_edge(state, bounds, 200, 200, 1, true, true, true) ==
          notification_history_pointer_route_t::none);
    CHECK(notification_history_pointer_edge(state, bounds, 20, 20, 1, false, true, true) ==
          notification_history_pointer_route_t::none);
    CHECK(notification_history_pointer_edge(state, bounds, 20, 20, 1, true, false, false) ==
          notification_history_pointer_route_t::none);
    CHECK_FALSE(state.pointer_owned_this_frame);
}

namespace {
void check_history_context_geometry(nk_context& context, const notification_panel_state_t& state,
                                    struct nk_rect moved) {
    const auto* locked = nk_window_find(&context, "Notifications");
    REQUIRE(locked);
    CHECK(locked->bounds.x == moved.x);
    CHECK(locked->bounds.w == moved.w);
    CHECK(state.severity_filter == 4);
    CHECK(state.expanded_id == 1);
    CHECK(state.detail_scroll_y == 20);
}
}

TEST_CASE("history context switch preserves moved geometry filter and detail scroll",
          "[notifications][frontend][renderer]") {
    history_render_context_t primary;
    history_render_context_t overlay;
    auto row = history_row();
    std::string detail;
    for (int line = 0; line < 60; ++line) detail += "Detail line\n";
    row.detail = detail.c_str();
    notification_panel_state_t state;
    state.expanded_id = row.id;
    state.severity_filter = 4;
    wormedit::ui::notification_action_t action{};
    render_notification_panel(&primary.context, {true, {row}}, state, action, 1000, 700);
    const auto moved = nk_rect(480, 40, 500, 400);
    nk_window_set_bounds(&primary.context, "Notifications", moved);
    nk_clear(&primary.context);
    render_notification_panel(&primary.context, {true, {row}}, state, action, 1000, 700);
    state.detail_scroll_y = 20;
    render_notification_panel(&overlay.context, {true, {row}}, state, action, 1000, 700, false);
    check_history_context_geometry(overlay.context, state, moved);
    CHECK(action.kind == WORM_UI_NOTIFICATION_ACTION_NONE);
}

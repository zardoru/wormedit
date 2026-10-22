#include "ui/nuklear/chrome/context_menu.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <map>
#include <string>
#include <source_location>

namespace chrome = wormedit::ui::chrome;

namespace {
void verify_menu(bool value, std::source_location location = std::source_location::current()) {
    INFO(location.file_name() << ":" << location.line());
    CHECK(value);
}

auto glyph_width(nk_handle, float, const char*, int length) -> float {
    return static_cast<float>(length) * 8.0f;
}

struct frame_input_t {
    struct nk_vec2 mouse{};
    int left{-1}, right{-1}, escape{-1}; // -1 unchanged, 0 up, 1 down
};

struct menu_fixture_t {
    nk_user_font font{};
    nk_context context{};
    chrome::ContextMenu menu{.title = "Test actions"};
    chrome::PopupContextStamp stamp{1, 2, 3, 4};
    struct nk_rect popup{}, rows{};
    bool parent_clicked{}, enabled_clicked{}, disabled_clicked{}, drawn{};
    bool navigation_items{};
    int activated_row{-1};
    std::map<std::string, nk_color> text_colors;

    menu_fixture_t() {
        font.height = 15.0f;
        font.width = glyph_width;
        REQUIRE(nk_init_default(&context, &font));
    }
    ~menu_fixture_t() { chrome::context_menu_reset(menu); chrome::context_menu_cancel_keyboard(); nk_free(&context); }
    menu_fixture_t(const menu_fixture_t&) = delete;
    auto operator=(const menu_fixture_t&) -> menu_fixture_t& = delete;

    void input(const frame_input_t& in) {
        nk_input_begin(&context);
        nk_input_motion(&context, static_cast<int>(in.mouse.x), static_cast<int>(in.mouse.y));
        const auto x = static_cast<int>(in.mouse.x);
        const auto y = static_cast<int>(in.mouse.y);
        if (in.left >= 0) nk_input_button(&context, NK_BUTTON_LEFT, x, y, in.left);
        if (in.right >= 0) nk_input_button(&context, NK_BUTTON_RIGHT, x, y, in.right);
        if (in.escape >= 0) nk_input_key(&context, NK_KEY_TEXT_RESET_MODE, in.escape);
        nk_input_end(&context);
    }

    // One frame: the parent window holds a button above the menu's anchor area.
    void frame(const frame_input_t& in, const std::function<void()>& body) {
        input(in);
        drawn = false;
        if (nk_begin(&context, "Parent", nk_rect(0, 0, 400, 300), NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_dynamic(&context, 25, 1);
            parent_clicked = nk_button_label(&context, "Parent action") != 0;
            body();
        }
        nk_end(&context);
        record_text();
        nk_clear(&context);
    }

    void record_text() {
        text_colors.clear();
        const nk_command* command{};
        nk_foreach(command, &context) {
            if (command->type != NK_COMMAND_TEXT) continue;
            const auto* text = reinterpret_cast<const nk_command_text*>(command);
            text_colors[std::string(text->string, static_cast<std::size_t>(text->length))] =
                text->foreground;
        }
    }

    void draw_items() {
        popup = nk_window_get_bounds(&context);
        rows = nk_window_get_content_region(&context);
        if (navigation_items) {
            for (int row = 0; row < 4; ++row)
                if (chrome::context_menu_item(menu, &context, row == 1 ? "First" : row == 3 ? "Last" : "Disabled",
                                              row % 2 == 0 ? "Not now" : nullptr)) activated_row = row;
        }
        if (!navigation_items) enabled_clicked = chrome::context_menu_item(menu, &context, "Enabled") || enabled_clicked;
        if (!navigation_items) disabled_clicked =
            chrome::context_menu_item(menu, &context, "Disabled", "Not now") || disabled_clicked;
        drawn = true;
    }

    void menu_frame(const frame_input_t& in, bool open_now = false,
                    chrome::PopupContextStamp current = {1, 2, 3, 4}) {
        frame(in, [&] {
            (void)chrome::context_menu_update(menu, &context);
            if (open_now) chrome::context_menu_open(menu, in.mouse, stamp);
            if (!chrome::context_menu_begin(menu, &context, current,
                                            nk_vec2(chrome::context_menu_width,
                                                    chrome::context_menu_height(navigation_items ? 4 : 2))))
                return;
            draw_items();
            chrome::context_menu_end(menu, &context);
        });
    }

    auto row_center(int index) const -> struct nk_vec2 {
        const auto step = chrome::context_menu_row_height + context.style.window.spacing.y;
        return nk_vec2(rows.x + rows.w * 0.5f,
                       rows.y + static_cast<float>(index) * step +
                           chrome::context_menu_row_height * 0.5f);
    }

    void open_at(struct nk_vec2 at) {
        menu_frame({.mouse = at, .right = 1}, true);
        menu_frame({.mouse = at, .right = 0});
    }

    void left_click(struct nk_vec2 at) {
        menu_frame({.mouse = at, .left = 1});
        menu_frame({.mouse = at, .left = 0});
    }
};

auto contains(struct nk_rect outer, struct nk_rect inner) -> bool {
    return inner.x >= outer.x && inner.y >= outer.y && inner.x + inner.w <= outer.x + outer.w &&
           inner.y + inner.h <= outer.y + outer.h;
}
} // namespace

TEST_CASE("Context menu survives its opening press and stays inside the parent",
          "[frontend-state][event-harness][context-menu]") {
    menu_fixture_t fixture;
    fixture.menu_frame({.mouse = nk_vec2(390, 290), .right = 1}, true);
    verify_menu(fixture.drawn);
    verify_menu(contains(nk_rect(0, 0, 400, 300), fixture.popup));
    fixture.menu_frame({.mouse = nk_vec2(390, 290), .right = 0});
    fixture.menu_frame({.mouse = nk_vec2(390, 290)});
    verify_menu(fixture.drawn);
    verify_menu(fixture.menu.open);
}

TEST_CASE("Context menu closes on an outside press and latches its release",
          "[frontend-state][event-harness][context-menu]") {
    menu_fixture_t fixture;
    fixture.open_at(nk_vec2(40, 60));
    const auto outside = nk_vec2(380, 280);
    fixture.menu_frame({.mouse = outside, .left = 1});
    verify_menu(!fixture.drawn);
    verify_menu(!fixture.menu.open);
    verify_menu(chrome::context_menu_owns_input(fixture.menu));
    fixture.menu_frame({.mouse = outside, .left = 0});
    verify_menu(chrome::context_menu_owns_input(fixture.menu));
    fixture.menu_frame({.mouse = outside});
    verify_menu(!chrome::context_menu_owns_input(fixture.menu));
    // The covered parent is reachable once the popup is gone.
    fixture.menu_frame({.mouse = nk_vec2(20, 15), .left = 1});
    verify_menu(fixture.parent_clicked);
}

TEST_CASE("Context menu closes on Escape and on a stale context",
          "[frontend-state][event-harness][context-menu]") {
    menu_fixture_t fixture;
    fixture.open_at(nk_vec2(40, 60));
    fixture.menu_frame({.mouse = nk_vec2(40, 60), .escape = 1});
    verify_menu(!fixture.menu.open);
    fixture.menu_frame({.mouse = nk_vec2(40, 60), .escape = 0});
    fixture.menu_frame({.mouse = nk_vec2(40, 60)});
    fixture.open_at(nk_vec2(40, 60));
    verify_menu(fixture.menu.open);
    fixture.menu_frame({.mouse = nk_vec2(40, 60)}, false, {1, 2, 4, 4});
    verify_menu(!fixture.drawn);
    verify_menu(!fixture.menu.open);
}

TEST_CASE("Context menu disabled items never activate and enabled items close the menu",
          "[frontend-state][event-harness][context-menu]") {
    menu_fixture_t fixture;
    fixture.open_at(nk_vec2(40, 60));
    fixture.menu_frame({.mouse = nk_vec2(40, 60)});
    // Disabled rows are drawn dimmed, as in the menubar menus.
    REQUIRE(fixture.text_colors.contains("Enabled"));
    REQUIRE(fixture.text_colors.contains("Disabled"));
    const auto enabled = fixture.text_colors["Enabled"];
    const auto disabled = fixture.text_colors["Disabled"];
    verify_menu(disabled.r + disabled.g + disabled.b < enabled.r + enabled.g + enabled.b);
    fixture.left_click(fixture.row_center(1));
    verify_menu(!fixture.disabled_clicked);
    verify_menu(fixture.menu.open);
    fixture.left_click(fixture.row_center(0));
    verify_menu(fixture.enabled_clicked);
    verify_menu(!fixture.menu.open);
    verify_menu(chrome::context_menu_owns_input(fixture.menu));
}

TEST_CASE("Context menu closed outside its frame is retired and releases the parent",
          "[frontend-state][event-harness][context-menu]") {
    menu_fixture_t fixture;
    fixture.open_at(nk_vec2(40, 60));
    chrome::context_menu_close(fixture.menu);
    verify_menu(fixture.menu.close_pending);
    fixture.menu_frame({.mouse = nk_vec2(300, 250)});
    verify_menu(!fixture.menu.close_pending);
    verify_menu(!fixture.drawn);
    fixture.menu_frame({.mouse = nk_vec2(20, 15), .left = 1});
    verify_menu(fixture.parent_clicked);
}

TEST_CASE("Context menu closed from a nested group finishes at the menu end",
          "[frontend-state][event-harness][context-menu]") {
    menu_fixture_t fixture;
    fixture.open_at(nk_vec2(40, 60));
    fixture.frame({.mouse = nk_vec2(40, 60)}, [&] {
        (void)chrome::context_menu_update(fixture.menu, &fixture.context);
        REQUIRE(chrome::context_menu_begin(fixture.menu, &fixture.context, fixture.stamp,
                                           nk_vec2(220, 120)));
        nk_layout_row_dynamic(&fixture.context, 60, 1);
        if (nk_group_begin(&fixture.context, "Nested", NK_WINDOW_NO_SCROLLBAR)) {
            chrome::context_menu_close(fixture.menu, &fixture.context);
            nk_group_end(&fixture.context);
        }
        verify_menu(fixture.menu.close_pending);
        chrome::context_menu_end(fixture.menu, &fixture.context);
        verify_menu(!fixture.menu.close_pending);
    });
    verify_menu(!fixture.menu.open);
    fixture.menu_frame({.mouse = nk_vec2(20, 15)});
    fixture.menu_frame({.mouse = nk_vec2(20, 15), .left = 1});
    verify_menu(fixture.parent_clicked);
}

namespace {
void menu_key(menu_fixture_t& fixture, chrome::ContextMenuKey key, bool repeat = false) {
    const auto token = static_cast<std::uint32_t>(key) + 10;
    verify_menu(chrome::context_menu_capture_key(key, token, true, repeat));
    fixture.menu_frame({.mouse = {100, 100}});
    verify_menu(chrome::context_menu_capture_key(key, token, false, false));
}
}

TEST_CASE("Context keyboard navigation skips disabled rows and activates once",
          "[frontend-state][context-menu][keyboard-context]") {
    menu_fixture_t fixture;
    fixture.navigation_items = true;
    fixture.menu_frame({.mouse = {100, 100}}, true);
    menu_key(fixture, chrome::ContextMenuKey::down);
    verify_menu(fixture.menu.selected_row == 1);
    menu_key(fixture, chrome::ContextMenuKey::down);
    verify_menu(fixture.menu.selected_row == 3);
    menu_key(fixture, chrome::ContextMenuKey::home);
    verify_menu(fixture.menu.selected_row == 1);
    menu_key(fixture, chrome::ContextMenuKey::end);
    verify_menu(fixture.menu.selected_row == 3);
    menu_key(fixture, chrome::ContextMenuKey::up);
    verify_menu(fixture.menu.selected_row == 1);
    const auto token = 100U;
    verify_menu(chrome::context_menu_capture_key(chrome::ContextMenuKey::activate, token, true, false));
    fixture.menu_frame({.mouse = {100, 100}});
    verify_menu(!fixture.menu.open && fixture.activated_row == 1);
    verify_menu(chrome::context_menu_capture_key(chrome::ContextMenuKey::activate, token, true, true));
    verify_menu(chrome::context_menu_capture_key(chrome::ContextMenuKey::activate, token, false, false));
    verify_menu(!chrome::context_menu_capture_key(chrome::ContextMenuKey::down, 101, true, false));
}

TEST_CASE("Context keyboard Escape and stale stamps retire without activation",
          "[frontend-state][context-menu][keyboard-context]") {
    menu_fixture_t fixture;
    fixture.menu_frame({.mouse = {100, 100}}, true);
    menu_key(fixture, chrome::ContextMenuKey::escape);
    verify_menu(!fixture.menu.open && !fixture.enabled_clicked);
    fixture.menu_frame({.mouse = {100, 100}}, true);
    verify_menu(chrome::context_menu_capture_key(chrome::ContextMenuKey::down, 1, true, false));
    verify_menu(chrome::context_menu_capture_key(chrome::ContextMenuKey::activate, 2, true, false));
    fixture.menu_frame({.mouse = {100, 100}}, false, {1, 2, 99, 4});
    verify_menu(!fixture.menu.open && !fixture.enabled_clicked);
    chrome::context_menu_cancel_keyboard();
    verify_menu(!chrome::context_menu_keyboard_owned());
}

TEST_CASE("Context keyboard focus uses absolute virtual rows across pages",
          "[frontend-state][context-menu][keyboard-context]") {
    menu_fixture_t fixture;
    fixture.menu_frame({.mouse = {100, 100}}, true);
    chrome::context_menu_set_row_count(fixture.menu, 200);
    chrome::context_menu_set_row_enabled(fixture.menu, 199, false);
    chrome::context_menu_set_row_enabled(fixture.menu, 198, true);
    verify_menu(chrome::context_menu_capture_key(chrome::ContextMenuKey::end, 1, true, false));
    verify_menu(fixture.menu.selected_row == 198);
    fixture.frame({.mouse = {100, 100}}, [&] {
        verify_menu(chrome::context_menu_begin(fixture.menu, &fixture.context, fixture.stamp,
            nk_vec2(220, chrome::context_menu_height(2))));
        chrome::context_menu_set_row_cursor(fixture.menu, 198);
        (void)chrome::context_menu_item(fixture.menu, &fixture.context, "Last enabled");
        (void)chrome::context_menu_item(fixture.menu, &fixture.context, "Locked", "Not now");
        chrome::context_menu_end(fixture.menu, &fixture.context);
    });
    verify_menu(fixture.menu.selected_row == 198);
    verify_menu(chrome::context_menu_capture_key(chrome::ContextMenuKey::activate, 2, true, false));
    fixture.frame({.mouse = {100, 100}}, [&] {
        verify_menu(chrome::context_menu_begin(fixture.menu, &fixture.context, fixture.stamp,
            nk_vec2(220, chrome::context_menu_height(1))));
        chrome::context_menu_set_row_cursor(fixture.menu, 198);
        verify_menu(chrome::context_menu_item(fixture.menu, &fixture.context, "Last enabled"));
        chrome::context_menu_end(fixture.menu, &fixture.context);
    });
    verify_menu(!fixture.menu.open);
}

TEST_CASE("Specialized popup keys bypass action navigation while prior releases stay captured",
          "[frontend-state][context-menu][keyboard-context]") {
    menu_fixture_t fixture;
    fixture.menu_frame({.mouse = {100, 100}}, true);
    verify_menu(chrome::context_menu_capture_key(chrome::ContextMenuKey::activate, 2, true, false));
    fixture.menu.shared_keyboard_navigation = false;
    verify_menu(chrome::context_menu_keyboard_owned());
    verify_menu(chrome::context_menu_capture_key(chrome::ContextMenuKey::activate, 2, false, false));
    verify_menu(!chrome::context_menu_keyboard_owned());
    verify_menu(!chrome::context_menu_capture_key(chrome::ContextMenuKey::down, 3, true, false));
    verify_menu(!chrome::context_menu_capture_key(chrome::ContextMenuKey::none, 4, true, false));
    verify_menu(!chrome::context_menu_capture_key(chrome::ContextMenuKey::down, 3, false, false));
    chrome::context_menu_close_active_keyboard();
    verify_menu(!fixture.menu.open);
}

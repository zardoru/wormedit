#include "ui/nuklear/dialog.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <source_location>

namespace chrome = wormedit::ui::chrome;

namespace {

void verify(bool value, std::source_location location = std::source_location::current()) {
    INFO(location.file_name() << ":" << location.line());
    CHECK(value);
}

auto glyph_width(nk_handle, float, const char*, int length) -> float {
    return static_cast<float>(length) * 8.0f;
}

struct frame_input_t {
    struct nk_vec2 mouse{-1, -1};
    int left{-1};    // -1 unchanged, 0 up, 1 down
    int escape{-1};
    int enter{-1};
};

struct dialog_fixture_t {
    nk_user_font font{};
    nk_context context{};
    chrome::Dialog dialog;
    struct nk_rect parent_button{}, window{}, body{}, footer{}, primary{};
    bool parent_clicked{};
    chrome::DialogResult result{chrome::DialogResult::none};
    bool modal{true};
    bool with_primary{true};
    bool open{true};
    struct nk_vec2 area{800, 600};
    float width{420};

    dialog_fixture_t() {
        font.height = 15.0f;
        font.width = glyph_width;
        REQUIRE(nk_init_default(&context, &font));
    }
    ~dialog_fixture_t() { nk_free(&context); }
    dialog_fixture_t(const dialog_fixture_t&) = delete;
    auto operator=(const dialog_fixture_t&) -> dialog_fixture_t& = delete;

    void input(const frame_input_t& in) {
        nk_input_begin(&context);
        nk_input_motion(&context, static_cast<int>(in.mouse.x), static_cast<int>(in.mouse.y));
        if (in.left >= 0)
            nk_input_button(&context, NK_BUTTON_LEFT, static_cast<int>(in.mouse.x),
                            static_cast<int>(in.mouse.y), in.left);
        if (in.escape >= 0) nk_input_key(&context, NK_KEY_TEXT_RESET_MODE, in.escape);
        if (in.enter >= 0) nk_input_key(&context, NK_KEY_ENTER, in.enter);
        nk_input_end(&context);
    }

    void draw_dialog() {
        chrome::DialogOptions options{.modal = modal, .width = width, .area = area};
        if (chrome::dialog_begin(dialog, &context, "Test dialog", "Discard changes?", options)) {
            nk_layout_row_dynamic(&context, 20, 1);
            body = nk_widget_bounds(&context);
            nk_label(&context, "Unsaved changes will be lost.", NK_TEXT_LEFT);
            nk_layout_row_dynamic(&context, 40, 1);
            nk_label(&context, "Second row", NK_TEXT_LEFT);
            window = nk_window_get_bounds(&context);
            chrome::dialog_footer_begin(dialog, &context, with_primary ? 3 : 2, true);
            chrome::dialog_button(dialog, &context, "Discard", chrome::DialogButtonRole::destructive);
            chrome::dialog_button(dialog, &context, "Cancel", chrome::DialogButtonRole::secondary);
            if (with_primary) {
                primary = nk_widget_bounds(&context);
                chrome::dialog_button(dialog, &context, "Save", chrome::DialogButtonRole::primary);
            }
            footer = nk_widget_bounds(&context);
        }
        result = chrome::dialog_end(dialog, &context);
    }

    void frame(const frame_input_t& in) {
        input(in);
        if (nk_begin(&context, "Parent", nk_rect(0, 0, 800, 600), NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_static(&context, 25, 120, 1);
            parent_button = nk_widget_bounds(&context);
            parent_clicked = nk_button_label(&context, "Parent action") != 0;
        }
        nk_end(&context);
        if (open) draw_dialog();
        nk_clear(&context);
    }

    void settle() {
        frame({});
        frame({});
    }
};

} // namespace

TEST_CASE("Dialog insets are equal and its height fits the content after the second frame",
          "[unit][frontend-state][event-harness][dialog]") {
    dialog_fixture_t f;
    f.frame({});
    f.frame({});
    const auto left = f.body.x - f.window.x;
    const auto right = f.window.x + f.window.w - (f.body.x + f.body.w);
    verify(std::abs(left - right) < 0.5f);
    verify(left >= chrome::dialog_padding);
    // The footer button row is the last row; the window ends one padding below it.
    const auto expected = f.primary.y + chrome::dialog_button_height + chrome::dialog_padding - f.window.y;
    verify(std::abs(f.window.h - expected) <= 1.0f);
    // Centered in the area.
    verify(std::abs((f.window.x + f.window.w * 0.5f) - 400.0f) <= 1.0f);
    verify(std::abs((f.window.y + f.window.h * 0.5f) - 300.0f) <= 1.0f);
}

TEST_CASE("Dialog restores the window padding and spacing",
          "[unit][frontend-state][event-harness][dialog]") {
    dialog_fixture_t f;
    const auto padding = f.context.style.window.padding;
    const auto spacing = f.context.style.window.spacing;
    f.settle();
    verify(f.context.style.window.padding.x == padding.x && f.context.style.window.padding.y == padding.y);
    verify(f.context.style.window.spacing.x == spacing.x && f.context.style.window.spacing.y == spacing.y);
}

TEST_CASE("Dialog maps Escape to cancel and Enter to the primary action",
          "[unit][frontend-state][event-harness][dialog]") {
    dialog_fixture_t f;
    f.settle();
    f.frame({.escape = 1});
    verify(f.result == chrome::DialogResult::cancel);
    f.frame({.escape = 0});
    verify(f.result == chrome::DialogResult::none);
    f.frame({.enter = 1});
    verify(f.result == chrome::DialogResult::primary);
    f.frame({.enter = 0});
    verify(f.result == chrome::DialogResult::none);
}

TEST_CASE("Dialog Enter does nothing without a primary button",
          "[unit][frontend-state][event-harness][dialog]") {
    dialog_fixture_t f;
    f.with_primary = false;
    f.settle();
    f.frame({.enter = 1});
    verify(f.result == chrome::DialogResult::none);
}

TEST_CASE("Dialog footer buttons report their role and sit right-aligned",
          "[unit][frontend-state][event-harness][dialog]") {
    dialog_fixture_t f;
    f.settle();
    // Click one footer cell; the first non-none result of the press frames wins.
    const auto press = [&](float x, float y) {
        auto seen = chrome::DialogResult::none;
        for (const auto& in : {frame_input_t{.mouse = {x, y}}, frame_input_t{.mouse = {x, y}, .left = 1},
                               frame_input_t{.mouse = {x, y}, .left = 0}}) {
            f.frame(in);
            if (seen == chrome::DialogResult::none) seen = f.result;
        }
        return seen;
    };
    const auto primary = f.primary;
    verify(primary.x + primary.w <= f.window.x + f.window.w - chrome::dialog_padding + 1.0f);
    verify(primary.w == chrome::dialog_button_width && primary.h == chrome::dialog_button_height);
    verify(press(primary.x + 8, primary.y + 8) == chrome::DialogResult::primary);
    // The destructive button is the far-left footer cell.
    verify(press(f.window.x + chrome::dialog_padding + 8, primary.y + 8) == chrome::DialogResult::destructive);
    // The secondary button sits just left of the primary one.
    verify(press(primary.x - chrome::dialog_spacing - chrome::dialog_button_width * 0.5f, primary.y + 8) ==
           chrome::DialogResult::secondary);
}

TEST_CASE("Dialog cannot be dragged",
          "[unit][frontend-state][event-harness][dialog]") {
    dialog_fixture_t f;
    f.settle();
    const auto start = f.window;
    const auto x = f.window.x + 40, y = f.window.y + 6;
    f.frame({.mouse = {x, y}});
    f.frame({.mouse = {x, y}, .left = 1});
    f.frame({.mouse = {x + 120, y + 90}});
    f.frame({.mouse = {x + 120, y + 90}, .left = 0});
    f.frame({});
    verify(f.window.x == start.x && f.window.y == start.y);
}

TEST_CASE("Modal dialog blocks input to windows behind it",
          "[unit][frontend-state][event-harness][dialog]") {
    for (const auto modal : {true, false}) {
        dialog_fixture_t f;
        f.modal = modal;
        f.settle();
        const auto x = f.parent_button.x + 10, y = f.parent_button.y + 10;
        bool clicked = false;
        f.frame({.mouse = {x, y}});
        f.frame({.mouse = {x, y}, .left = 1});
        clicked |= f.parent_clicked;
        f.frame({.mouse = {x, y}, .left = 0});
        clicked |= f.parent_clicked;
        verify(clicked == !modal);
    }
}

TEST_CASE("Dialog clamps to a small window with a 16 px margin",
          "[unit][frontend-state][event-harness][dialog]") {
    dialog_fixture_t f;
    f.area = {300, 150};
    f.width = 420;
    f.settle();
    verify(f.dialog.bounds.x >= chrome::dialog_padding - 0.5f);
    verify(f.dialog.bounds.x + f.dialog.bounds.w <= 300 - chrome::dialog_padding + 0.5f);
    verify(f.dialog.bounds.y >= chrome::dialog_padding - 0.5f);
    verify(f.dialog.bounds.y + f.dialog.bounds.h <= 150 - chrome::dialog_padding + 0.5f);
}

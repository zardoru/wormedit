#define NK_IMPLEMENTATION
#include "ui/nuklear/chrome.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <source_location>
#include <string>
#include <string_view>

namespace {
void expect_combo(bool value, std::source_location where = std::source_location::current()) {
    INFO("combo check caller line " << where.line());
    REQUIRE(value);
}

struct combo_context_t {
    nk_user_font font{};
    nk_context context{};
    combo_context_t() {
        font.height = 14;
        font.width = [](nk_handle, float, const char*, int count) { return count * 7.0f; };
        expect_combo(nk_init_default(&context, &font));
        wormedit::ui::chrome::apply_theme(&context);
    }
    ~combo_context_t() { nk_free(&context); }
};

void input_frame(nk_context& context, struct nk_vec2 point, bool down) {
    nk_input_begin(&context);
    nk_input_motion(&context, static_cast<int>(point.x), static_cast<int>(point.y));
    nk_input_button(&context, NK_BUTTON_LEFT, static_cast<int>(point.x),
                     static_cast<int>(point.y), down);
    nk_input_end(&context);
}

auto text_position(nk_context& context, std::string_view label) -> struct nk_vec2 {
    const nk_command* command;
    nk_foreach(command, &context) {
        if (command->type != NK_COMMAND_TEXT) continue;
        const auto* text = reinterpret_cast<const nk_command_text*>(command);
        if (std::string_view(text->string, static_cast<std::size_t>(text->length)) == label)
            return nk_vec2(text->x + text->w * 0.5f, text->y + text->h * 0.5f);
    }
    expect_combo(false);
    return nk_vec2(0, 0);
}

auto popup_combo_frame(nk_context& context, int selected) -> int {
    expect_combo(nk_begin(&context, "Host", nk_rect(0, 0, 400, 400), NK_WINDOW_NO_SCROLLBAR));
    expect_combo(nk_popup_begin(&context, NK_POPUP_STATIC, "Popup", NK_WINDOW_NO_SCROLLBAR,
                               nk_rect(20, 20, 250, 180)));
    expect_combo(wormedit::ui::chrome::popup_panel(&context));
    const char* choices[]{"First", "Second", "Third"};
    nk_layout_row_dynamic(&context, 24, 1);
    const auto next = wormedit::ui::chrome::safe_combo(
        &context, choices, 3, selected, 24, nk_vec2(200, 150));
    nk_popup_end(&context);
    nk_end(&context);
    return next;
}

auto ordinary_combo_frame(nk_context& context, int count = 3, int item_height = 24,
                          float maximum_height = 150) -> bool {
    expect_combo(nk_begin(&context, "Ordinary", nk_rect(0, 0, 400, 400), NK_WINDOW_NO_SCROLLBAR));
    const char* choices[]{"First", "Second", "Third"};
    nk_layout_row_dynamic(&context, 24, 1);
    static_cast<void>(wormedit::ui::chrome::safe_combo(
        &context, choices, count, 0, item_height, nk_vec2(200, maximum_height)));
    const bool open = context.current->popup.active != 0;
    nk_end(&context);
    return open;
}

void check_combo_vertical_padding(nk_context& context) {
    const auto* host = nk_window_find(&context, "Ordinary");
    expect_combo(host && host->popup.win);
    const auto bounds = host->popup.win->bounds;
    float first = bounds.y + bounds.h;
    float last = bounds.y;
    bool has_scrollbar{};
    const nk_command* command;
    nk_foreach(command, &context) {
        if (command->type == NK_COMMAND_RECT_FILLED) {
            const auto* rect = reinterpret_cast<const nk_command_rect_filled*>(command);
            if (rect->x >= bounds.x + bounds.w - context.style.window.scrollbar_size.x &&
                rect->y >= bounds.y && rect->y + rect->h <= bounds.y + bounds.h)
                has_scrollbar = true;
        }
        if (command->type != NK_COMMAND_TEXT) continue;
        const auto* text = reinterpret_cast<const nk_command_text*>(command);
        if (text->y < bounds.y) continue;
        first = std::min(first, static_cast<float>(text->y));
        last = std::max(last, text->y + text->height);
    }
    expect_combo(last > first);
    const auto top = first - bounds.y;
    const auto bottom = bounds.y + bounds.h - last;
    INFO("popup " << bounds.y << "," << bounds.h << " text " << first << "," << last
         << " padding " << top << "," << bottom);
    CHECK(std::abs(top - bottom) <= 1.0f);
    CHECK_FALSE(has_scrollbar);
}

}

TEST_CASE("Popup combo guard selects inline choices without creating a nested popup", "[unit][frontend][popup-combo]") {
    combo_context_t fixture;
    auto& context = fixture.context;
    input_frame(context, nk_vec2(0, 0), false);
    auto selected = popup_combo_frame(context, 0);
    const auto third = text_position(context, "Third");
    nk_clear(&context);
    input_frame(context, third, true);
    selected = popup_combo_frame(context, selected);
    nk_clear(&context);
    input_frame(context, third, false);
    selected = popup_combo_frame(context, selected);
    expect_combo(selected == 2);
}

TEST_CASE("Ordinary window combo guard keeps the native dropdown", "[unit][frontend][popup-combo]") {
    combo_context_t fixture;
    auto& context = fixture.context;
    input_frame(context, nk_vec2(0, 0), false);
    expect_combo(!ordinary_combo_frame(context));
    const auto first = text_position(context, "First");
    nk_clear(&context);
    input_frame(context, first, true);
    static_cast<void>(ordinary_combo_frame(context));
    nk_clear(&context);
    input_frame(context, first, false);
    expect_combo(ordinary_combo_frame(context));
}

TEST_CASE("Ordinary dropdowns balance top and bottom padding around their choices",
          "[unit][frontend][popup-combo]") {
    const auto count = GENERATE(1, 2, 3);
    const auto height = GENERATE(24, 28);
    combo_context_t fixture;
    auto& context = fixture.context;
    input_frame(context, nk_vec2(0, 0), false);
    expect_combo(!ordinary_combo_frame(context, count, height));
    const auto first = text_position(context, "First");
    nk_clear(&context);
    input_frame(context, first, true);
    static_cast<void>(ordinary_combo_frame(context, count, height));
    nk_clear(&context);
    input_frame(context, first, false);
    expect_combo(ordinary_combo_frame(context, count, height));
    check_combo_vertical_padding(context);
    nk_clear(&context);
    input_frame(context, nk_vec2(0, 0), false);
    expect_combo(ordinary_combo_frame(context, count, height, 40));
    CHECK(nk_window_find(&context, "Ordinary")->popup.win->bounds.h <= 40);
}

TEST_CASE("Popup label-combo guard rejects nesting safely", "[unit][frontend][popup-combo]") {
    combo_context_t fixture;
    auto& context = fixture.context;
    input_frame(context, nk_vec2(0, 0), false);
    expect_combo(nk_begin(&context, "Host", nk_rect(0, 0, 400, 400), NK_WINDOW_NO_SCROLLBAR));
    expect_combo(nk_popup_begin(&context, NK_POPUP_STATIC, "Popup", NK_WINDOW_NO_SCROLLBAR,
                               nk_rect(20, 20, 250, 180)));
    nk_layout_row_dynamic(&context, 24, 1);
    expect_combo(!wormedit::ui::chrome::safe_combo_begin_label(&context, "Retained choice", nk_vec2(200, 150)));
    nk_popup_end(&context);
    nk_end(&context);
    static_cast<void>(text_position(context, "Retained choice"));
}


namespace {
void check_active_edit(nk_flags flags, bool focus_pending) {
    CHECK((flags & NK_EDIT_ACTIVE) != 0);
    CHECK_FALSE(focus_pending);
}

auto autofocus_popup_frame(nk_context& context, char* text, bool& focus_pending)
    -> nk_flags {
    expect_combo(nk_begin(&context, "Edit host", nk_rect(0, 0, 400, 400), NK_WINDOW_NO_SCROLLBAR));
    expect_combo(nk_popup_begin(&context, NK_POPUP_STATIC, "Timing value", NK_WINDOW_NO_SCROLLBAR,
                               nk_rect(20, 20, 250, 100)));
    nk_layout_row_dynamic(&context, 25, 1);
    const auto flags = wormedit::ui::chrome::autofocus_text_field(
        &context, static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT,
        text, 64, nk_filter_default, focus_pending);
    nk_layout_row_dynamic(&context, 20, 1);
    nk_label(&context, "Enter to apply", NK_TEXT_LEFT);
    nk_popup_end(&context);
    nk_end(&context);
    return flags;
}
}

TEST_CASE("Inline popup autofocus accepts typing without an extra click and reports blur",
          "[unit][frontend][inline-timing]") {
    combo_context_t fixture;
    auto& context = fixture.context;
    char text[64] = "125";
    bool focus_pending = true;
    input_frame(context, nk_vec2(0, 0), false);
    const auto first = autofocus_popup_frame(context, text, focus_pending);
    check_active_edit(first, focus_pending);
    const auto label = text_position(context, "Enter to apply");
    nk_clear(&context);
    nk_input_begin(&context);
    nk_input_unicode(&context, '2');
    nk_input_unicode(&context, '.');
    nk_input_unicode(&context, '5');
    nk_input_end(&context);
    const auto typed = autofocus_popup_frame(context, text, focus_pending);
    check_active_edit(typed, focus_pending);
    CHECK(std::string(text) == "2.5");
    nk_clear(&context);
    SECTION("Enter commits without a click") {
        nk_input_begin(&context);
        nk_input_key(&context, NK_KEY_ENTER, nk_true);
        nk_input_end(&context);
        const auto committed = autofocus_popup_frame(context, text, focus_pending);
        CHECK((committed & NK_EDIT_COMMITTED) != 0);
    }
    SECTION("Clicking the popup label deactivates the value field") {
        input_frame(context, label, true);
        const auto blurred = autofocus_popup_frame(context, text, focus_pending);
        CHECK((blurred & NK_EDIT_DEACTIVATED) != 0);
    }
}

TEST_CASE("Dismissed inline popup releases its parent input lock",
          "[unit][frontend][inline-timing]") {
    combo_context_t fixture;
    auto& context = fixture.context;
    char text[64] = "125";
    bool focus_pending = true;
    input_frame(context, nk_vec2(0, 0), false);
    static_cast<void>(autofocus_popup_frame(context, text, focus_pending));
    nk_clear(&context);
    input_frame(context, nk_vec2(0, 0), false);
    expect_combo(nk_begin(&context, "Edit host", nk_rect(0, 0, 400, 400), NK_WINDOW_NO_SCROLLBAR));
    auto* parent = context.current;
    wormedit::ui::chrome::dismiss_popup(&context, "Timing value", nk_rect(20, 20, 250, 100));
    CHECK(parent->popup.active == 0);
    nk_end(&context);
    CHECK((parent->flags & NK_WINDOW_ROM) == 0);
}

namespace {
auto themed_combo_frame(nk_context& context, int selected, bool disabled = false) -> int {
    const auto background = context.style.window.background;
    const auto fixed = context.style.window.fixed_background;
    const auto* font = context.style.font;
    expect_combo(nk_begin(&context, "Themed combo", nk_rect(0, 0, 400, 400), NK_WINDOW_NO_SCROLLBAR));
    nk_layout_row_dynamic(&context, wormedit::ui::metrics::control_default, 1);
    const char* choices[]{"First", "Second", "Third"};
    if (disabled) nk_widget_disable_begin(&context);
    selected = wormedit::ui::chrome::safe_combo(&context, choices, 3, selected, 25, nk_vec2(200, 150));
    if (disabled) nk_widget_disable_end(&context);
    nk_end(&context);
    expect_combo(std::memcmp(&background, &context.style.window.background, sizeof(background)) == 0);
    expect_combo(std::memcmp(&fixed, &context.style.window.fixed_background, sizeof(fixed)) == 0);
    expect_combo(context.style.font == font);
    return selected;
}

auto has_popup_fill(nk_context& context) -> bool {
    const auto popup = wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::popup);
    const nk_command* command;
    nk_foreach(command, &context) {
        if (command->type != NK_COMMAND_RECT_FILLED) continue;
        const auto* rect = reinterpret_cast<const nk_command_rect_filled*>(command);
        if (rect->rounding == wormedit::ui::metrics::radius_surface &&
            std::memcmp(&rect->color, &popup, sizeof(popup)) == 0) return true;
    }
    return false;
}
}

TEST_CASE("Themed combo paints the popup role restores style and selects a value",
          "[unit][frontend][popup-combo][components]") {
    combo_context_t fixture;
    auto& context = fixture.context;
    input_frame(context, nk_vec2(0, 0), false);
    auto selected = themed_combo_frame(context, 0);
    const auto header = text_position(context, "First");
    nk_clear(&context);
    input_frame(context, header, true);
    selected = themed_combo_frame(context, selected);
    nk_clear(&context);
    input_frame(context, header, false);
    selected = themed_combo_frame(context, selected);
    expect_combo(has_popup_fill(context));
    const auto third = text_position(context, "Third");
    nk_clear(&context);
    input_frame(context, third, true);
    selected = themed_combo_frame(context, selected);
    nk_clear(&context);
    input_frame(context, third, false);
    selected = themed_combo_frame(context, selected);
    expect_combo(selected == 2);
}

TEST_CASE("Disabled themed combo cannot open a dropdown", "[unit][frontend][popup-combo][components]") {
    combo_context_t fixture;
    auto& context = fixture.context;
    input_frame(context, nk_vec2(0, 0), false);
    expect_combo(themed_combo_frame(context, 0, true) == 0);
    const auto header = text_position(context, "First");
    nk_clear(&context);
    input_frame(context, header, true);
    expect_combo(themed_combo_frame(context, 0, true) == 0);
    nk_clear(&context);
    input_frame(context, header, false);
    expect_combo(themed_combo_frame(context, 0, true) == 0);
    expect_combo(!has_popup_fill(context));
}

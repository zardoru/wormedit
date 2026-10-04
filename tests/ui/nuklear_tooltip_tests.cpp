#include "ui/nuklear/tooltip.h"
#include "ui/nuklear/chrome.h"

#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cctype>
#include <string>
#include <source_location>

namespace tip = wormedit::ui::nuklear::tooltip;
namespace {
void verify_tooltip(bool value, std::source_location location = std::source_location::current()) {
    INFO(location.file_name() << ":" << location.line());
    REQUIRE(value);
}

auto glyph_width(nk_handle, float, const char* text, int length) -> float {
    int glyphs = 0;
    for (int index = 0; index < length; ++index)
        if ((static_cast<unsigned char>(text[index]) & 0xc0U) != 0x80U) ++glyphs;
    return static_cast<float>(glyphs) * 8.0f;
}

struct tooltip_context_t {
    nk_user_font font{};
    nk_context context{};
    tooltip_context_t() {
        font.height = 15.0f;
        font.width = glyph_width;
        verify_tooltip(nk_init_default(&context, &font));
    }
    ~tooltip_context_t() { nk_free(&context); }
};

auto compact(std::string_view text) -> std::string {
    std::string result;
    for (const unsigned char value : text)
        if (!std::isspace(value)) result.push_back(static_cast<char>(value));
    return result;
}

void check_inside(struct nk_rect rect, struct nk_rect viewport) {
    verify_tooltip(rect.x >= viewport.x);
    verify_tooltip(rect.y >= viewport.y);
    verify_tooltip(rect.x + rect.w <= viewport.x + viewport.w + 1.0f);
    verify_tooltip(rect.y + rect.h <= viewport.y + viewport.h + 1.0f);
}

auto drawn_tooltip_text(nk_context& context, struct nk_rect viewport) -> std::string {
    std::string drawn;
    const nk_command* command{};
    nk_foreach(command, &context) {
        if (command->type == NK_COMMAND_RECT) {
            const auto* border = reinterpret_cast<const nk_command_rect*>(command);
            check_inside(nk_rect(border->x, border->y, border->w, border->h), viewport);
        }
        if (command->type != NK_COMMAND_TEXT) continue;
        const auto* text = reinterpret_cast<const nk_command_text*>(command);
        check_inside(nk_rect(text->x, text->y, text->w, text->h), viewport);
        const auto width = glyph_width({}, text->height, text->string, text->length);
        verify_tooltip(width <= static_cast<float>(text->w) + 1.0f);
        drawn.append(text->string, static_cast<std::size_t>(text->length));
    }
    return drawn;
}

void draw_at(tooltip_context_t& rendered, struct nk_rect viewport, struct nk_vec2 mouse,
             std::string_view message, float preferred_width = 420.0f,
             const char* instructions = nullptr) {
    nk_clear(&rendered.context);
    nk_input_begin(&rendered.context);
    nk_input_motion(&rendered.context, static_cast<int>(mouse.x), static_cast<int>(mouse.y));
    nk_input_end(&rendered.context);
    tip::set_viewport(viewport);
    verify_tooltip(nk_begin(&rendered.context, "Tooltip parent", nk_rect(23, 19, 140, 100),
                            NK_WINDOW_NO_SCROLLBAR));
    auto* parent = rendered.context.current;
    const auto plan = tip::plan_text(&rendered.context, message, preferred_width);
    verify_tooltip(plan.has_value());
    const auto placement = tip::place(&rendered.context, plan->outer_width, plan->content_height);
    verify_tooltip(placement.has_value());
    check_inside(placement->outer_bounds, viewport);
    if (instructions)
        wormedit::ui::chrome::timing_tooltip(&rendered.context, message.data(), instructions);
    else tip::text(&rendered.context, message, preferred_width);
    verify_tooltip(parent->popup.win != nullptr);
    nk_end(&rendered.context);
    const auto expected = std::string(message) + (instructions ? instructions : "");
    verify_tooltip(compact(drawn_tooltip_text(rendered.context, viewport)) == compact(expected));
}
}

TEST_CASE("Shared tooltip draws complete text within all four logical viewport corners",
          "[frontend-state][event-harness][tooltip][tooltip-bounds]") {
    tooltip_context_t rendered;
    const auto viewport = nk_rect(0, 0, 480, 320);
    const std::array corners{nk_vec2(1, 1), nk_vec2(479, 1),
                             nk_vec2(1, 319), nk_vec2(479, 319)};
    for (const auto mouse : corners)
        draw_at(rendered, viewport, mouse, "Clear the selection");
}

TEST_CASE("Shared tooltip wraps long words newlines and UTF8 without clipping glyphs",
          "[frontend-state][event-harness][tooltip][tooltip-bounds]") {
    tooltip_context_t rendered;
    const std::string message = "First line\n" + std::string(110, 'W') +
                                "\nÚltima línea 日本語";
    draw_at(rendered, nk_rect(0, 0, 240, 240), nk_vec2(238, 238), message);
}

TEST_CASE("Shared tooltip recomputes wrapping and placement after viewport resize",
          "[frontend-state][event-harness][tooltip][tooltip-bounds]") {
    tooltip_context_t rendered;
    const auto message = "Layers: give the selected sounds a semantic layer so notes inherit it";
    draw_at(rendered, nk_rect(0, 0, 640, 360), nk_vec2(639, 359), message);
    draw_at(rendered, nk_rect(0, 0, 220, 180), nk_vec2(219, 179), message);
}

TEST_CASE("Shared tooltip respects nonzero viewport origin and font padding",
          "[frontend-state][event-harness][tooltip][tooltip-bounds]") {
    tooltip_context_t rendered;
    rendered.context.style.window.padding = nk_vec2(7, 5);
    rendered.context.style.window.menu_padding = nk_vec2(9, 6);
    rendered.context.style.window.menu_border = 2;
    rendered.context.style.text.padding = nk_vec2(11, 3);
    draw_at(rendered, nk_rect(12, 9, 300, 180), nk_vec2(311, 188),
            "Sounds with notes keep their full readable tooltip");
}

TEST_CASE("Timing tooltip keeps long timing text and shorter instructions visible at an edge",
          "[frontend-state][event-harness][tooltip][tooltip-bounds]") {
    tooltip_context_t rendered;
    const auto timing = "Measure 311 / row 239 / full precise imported timestamp 156.835 seconds";
    draw_at(rendered, nk_rect(0, 0, 280, 240), nk_vec2(279, 239), timing, 420,
            "Click to seek");
}

TEST_CASE("Short tooltip keeps its natural compact width",
          "[frontend-state][event-harness][tooltip][tooltip-bounds]") {
    tooltip_context_t rendered;
    tip::set_viewport(nk_rect(0, 0, 640, 360));
    verify_tooltip(nk_begin(&rendered.context, "Tooltip size", nk_rect(0, 0, 640, 360),
                            NK_WINDOW_NO_SCROLLBAR));
    const auto plan = tip::plan_text(&rendered.context, "Clear the selection");
    verify_tooltip(plan.has_value());
    verify_tooltip(plan->outer_width < 220.0f);
    nk_end(&rendered.context);
}

#include "ui/nuklear/chrome.h"
#include "ui/nuklear/ui_gallery.h"
#include "ui/nuklear/ui_metrics.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <functional>
#include <vector>

namespace chrome = wormedit::ui::chrome;
namespace metrics = wormedit::ui::metrics;

namespace {

auto glyph_width(nk_handle, float, const char*, int length) -> float {
    return static_cast<float>(length) * 8.0f;
}

struct frame_input_t {
    float x{-1.0f}, y{-1.0f};
    int left{-1};      // -1 unchanged, 0 up, 1 down
    int key_left{-1};  // same convention
    int key_right{-1};
};

struct fixture_t {
    nk_user_font font{};
    nk_context context{};

    fixture_t() {
        font.height = 15.0f;
        font.width = glyph_width;
        REQUIRE(nk_init_default(&context, &font));
        wormedit::ui::chrome::apply_theme(&context);
    }
    ~fixture_t() { nk_free(&context); }
    fixture_t(const fixture_t&) = delete;
    auto operator=(const fixture_t&) -> fixture_t& = delete;

    void input(const frame_input_t& in) {
        nk_input_begin(&context);
        nk_input_motion(&context, static_cast<int>(in.x), static_cast<int>(in.y));
        if (in.left >= 0)
            nk_input_button(&context, NK_BUTTON_LEFT, static_cast<int>(in.x),
                            static_cast<int>(in.y), in.left);
        if (in.key_left >= 0) nk_input_key(&context, NK_KEY_LEFT, in.key_left);
        if (in.key_right >= 0) nk_input_key(&context, NK_KEY_RIGHT, in.key_right);
        nk_input_end(&context);
    }

    // One frame in a 400x300 window. The body lays out its own rows.
    void frame(const frame_input_t& in, const std::function<void()>& body) {
        input(in);
        if (nk_begin(&context, "Components", nk_rect(0, 0, 400, 300), NK_WINDOW_NO_SCROLLBAR))
            body();
        nk_end(&context);
        nk_clear(&context);
    }
};

void verify_inside(struct nk_rect inner, struct nk_rect outer) {
    CHECK(inner.x >= outer.x);
    CHECK(inner.y >= outer.y);
    CHECK(inner.x + inner.w <= outer.x + outer.w);
    CHECK(inner.y + inner.h <= outer.y + outer.h);
}

struct command_summary_t {
    std::vector<struct nk_rect> scissors;
    std::vector<struct nk_rect> fills;
};

auto summarize(nk_context& context) -> command_summary_t {
    command_summary_t summary;
    const nk_command* command{};
    nk_foreach(command, &context) {
        if (command->type == NK_COMMAND_SCISSOR) {
            const auto* scissor = reinterpret_cast<const nk_command_scissor*>(command);
            // Nuklear's own "no clip" reset (nk_null_rect); backends clamp it themselves.
            if (scissor->x <= -8192) continue;
            summary.scissors.push_back(nk_rect(static_cast<float>(scissor->x), static_cast<float>(scissor->y),
                                               static_cast<float>(scissor->w), static_cast<float>(scissor->h)));
        } else if (command->type == NK_COMMAND_RECT_FILLED) {
            const auto* rect = reinterpret_cast<const nk_command_rect_filled*>(command);
            summary.fills.push_back(nk_rect(static_cast<float>(rect->x), static_cast<float>(rect->y),
                                            static_cast<float>(rect->w), static_cast<float>(rect->h)));
        }
    }
    return summary;
}

constexpr float button_y = 14.0f;

} // namespace

TEST_CASE("components tokens", "[components]") {
    CHECK(metrics::control_compact == 24.0f);
    CHECK(metrics::control_default == 28.0f);
    CHECK(metrics::control_primary == 32.0f);
    CHECK(metrics::radius_control == 4.0f);
    CHECK(metrics::radius_chip == 12.0f);
    CHECK(metrics::radius_bar == 6.0f);
    CHECK(metrics::radius_surface == 8.0f);
    CHECK(metrics::elevation(0).steps == 0);
    CHECK(metrics::elevation(1).steps == 8);
    CHECK(metrics::elevation(1).offset_y == 4.0f);
    CHECK(metrics::elevation(2).steps == 12);
    CHECK(metrics::elevation(2).offset_y == 6.0f);
    CHECK(metrics::elevation(2).alpha > metrics::elevation(1).alpha);
}

TEST_CASE("components shadow clip stays inside the limit", "[components]") {
    const auto limit = nk_rect(0, 0, 800, 600);
    const auto bounds = nk_rect(780, 560, 100, 80); // hangs over the right and bottom edges
    verify_inside(chrome::surface_shadow_clip(bounds, 2, limit), limit);
    verify_inside(chrome::surface_shadow_clip(nk_rect(-30, -30, 60, 60), 1, limit), limit);
    const auto clip = chrome::surface_shadow_clip(nk_rect(300, 200, 100, 80), 2, limit);
    CHECK(clip.x < 300.0f);
    CHECK(clip.y + clip.h > 280.0f);
    const auto flat = chrome::surface_shadow_clip(nk_rect(300, 200, 100, 80), 0, limit);
    CHECK(flat.x == 300.0f);
    CHECK(flat.w == 100.0f);
}

TEST_CASE("components level two surface casts its shadow outside the window", "[components]") {
    fixture_t fixture;
    const auto target = nk_rect(0, 0, 640, 480);
    const auto bounds = nk_rect(200, 150, 240, 160);
    wormedit::ui::nuklear::tooltip::set_viewport(target);
    fixture.input({});
    const auto surface = chrome::surface_window_begin(&fixture.context, "Dialog", bounds,
                                                      NK_WINDOW_NO_SCROLLBAR, 2);
    REQUIRE(surface.open);
    chrome::surface_window_end(&fixture.context, surface);
    const auto summary = summarize(fixture.context);
    int outside = 0;
    for (const auto& fill : summary.fills) {
        if (fill.x < bounds.x && fill.y + fill.h > bounds.y + bounds.h) ++outside;
    }
    CHECK(outside >= metrics::elevation(2).steps);
    for (const auto& scissor : summary.scissors) verify_inside(scissor, target);
    // The window fill itself is transparent, so the shadow shows through it.
    CHECK(fixture.context.style.window.fixed_background.type == NK_STYLE_ITEM_COLOR);
    CHECK(fixture.context.style.window.fixed_background.data.color.a == 255);
}

TEST_CASE("components surface at the viewport edge keeps scissors inside the target",
          "[components]") {
    fixture_t fixture;
    const auto target = nk_rect(0, 0, 320, 240);
    wormedit::ui::nuklear::tooltip::set_viewport(target);
    fixture.input({});
    const auto surface = chrome::surface_window_begin(&fixture.context, "Edge", nk_rect(200, 150, 120, 90),
                                                      NK_WINDOW_NO_SCROLLBAR, 2);
    REQUIRE(surface.open);
    chrome::surface_window_end(&fixture.context, surface);
    for (const auto& scissor : summarize(fixture.context).scissors) verify_inside(scissor, target);
}

TEST_CASE("components button states", "[components]") {
    using chrome::ButtonKind;
    using chrome::ControlState;
    const auto primary = chrome::button_look(ButtonKind::primary, ControlState::normal);
    CHECK(primary.fill.b == chrome::color(wormedit::ui::theme::UiColor::selected).b);
    const auto disabled = chrome::button_look(ButtonKind::primary, ControlState::disabled);
    CHECK(disabled.fill.r == chrome::color(wormedit::ui::theme::UiColor::surface).r);
    CHECK(disabled.text.r == metrics::text_faint.r);
    const auto ghost = chrome::button_look(ButtonKind::ghost, ControlState::normal);
    CHECK(ghost.fill.a == 0);
    CHECK(chrome::button_look(ButtonKind::ghost, ControlState::hover).fill.a != 0);
    CHECK(chrome::button_look(ButtonKind::destructive, ControlState::normal).stroke.r ==
          metrics::destructive.r);
}

TEST_CASE("components button click and disabled non-activation", "[components]") {
    fixture_t fixture;
    bool enabled_clicked = false;
    bool disabled_clicked = false;
    const auto body = [&] {
        nk_layout_row_dynamic(&fixture.context, metrics::control_default, 2);
        enabled_clicked = chrome::Button{"Apply", chrome::ButtonKind::primary}.render(&fixture.context);
        disabled_clicked =
            chrome::Button{"Apply", chrome::ButtonKind::primary, "Nothing to apply", false}.render(
                &fixture.context);
    };
    fixture.frame({100, button_y + 4, -1, -1, -1}, body);
    fixture.frame({100, button_y + 4, 1, -1, -1}, body);
    CHECK(enabled_clicked);
    CHECK_FALSE(disabled_clicked);
    fixture.frame({100, button_y + 4, 0, -1, -1}, body);
    CHECK_FALSE(enabled_clicked);
    // The disabled button sits in the right-hand half of the row.
    fixture.frame({300, button_y + 4, -1, -1, -1}, body);
    fixture.frame({300, button_y + 4, 1, -1, -1}, body);
    CHECK_FALSE(disabled_clicked);
    CHECK_FALSE(enabled_clicked);
}

TEST_CASE("components checkbox toggles from anywhere in its cell", "[components]") {
    fixture_t fixture;
    bool toggled_a = false;
    bool toggled_b = false;
    const auto body = [&] {
        nk_layout_row_dynamic(&fixture.context, metrics::control_default, 1);
        toggled_a = chrome::Checkbox{"Snap", chrome::CheckState::off}.render(&fixture.context);
        nk_layout_row_dynamic(&fixture.context, metrics::control_default, 1);
        toggled_b = chrome::Checkbox{"Locked", chrome::CheckState::on, nullptr, false}.render(
            &fixture.context);
    };
    fixture.frame({}, body);
    fixture.frame({380, 14, 0, -1, -1}, body); // far right of the cell, away from the box
    fixture.frame({380, 14, 1, -1, -1}, body);
    CHECK(toggled_a);
    fixture.frame({380, 14, 0, -1, -1}, body);
    fixture.frame({380, 50, 0, -1, -1}, body);
    fixture.frame({380, 50, 1, -1, -1}, body);
    CHECK_FALSE(toggled_b);
    CHECK(chrome::toggled(chrome::CheckState::mixed) == chrome::CheckState::on);
    CHECK(chrome::toggled(chrome::CheckState::on) == chrome::CheckState::off);
    CHECK(chrome::toggled(chrome::CheckState::off) == chrome::CheckState::on);
}

TEST_CASE("components segmented hit areas and keyboard", "[components]") {
    const auto bounds = nk_rect(0, 0, 300, 28);
    CHECK(chrome::segment_at(bounds, 3, 10) == 0);
    CHECK(chrome::segment_at(bounds, 3, 150) == 1);
    CHECK(chrome::segment_at(bounds, 3, 299) == 2);
    CHECK(chrome::segment_at(bounds, 3, 300) == -1);
    CHECK(chrome::segment_at(bounds, 3, -1) == -1);
    CHECK(chrome::segment_step(0, 3, -1) == 0);
    CHECK(chrome::segment_step(2, 3, 1) == 2);
    CHECK(chrome::segment_step(1, 3, 1) == 2);

    fixture_t fixture;
    static const char* const labels[] = {"One", "Two", "Three"};
    int selected = 0;
    bool focus = false;
    const auto body = [&](bool enabled) {
        nk_layout_row_dynamic(&fixture.context, metrics::control_default, 1);
        selected = chrome::Segmented{labels, 3, selected, &focus, nullptr, enabled}.render(&fixture.context);
    };
    const auto enabled_body = [&] { body(true); };
    fixture.frame({}, enabled_body);
    fixture.frame({350, 14, 0, -1, -1}, enabled_body);
    fixture.frame({350, 14, 1, -1, -1}, enabled_body); // third segment
    CHECK(selected == 2);
    CHECK(focus);
    fixture.frame({350, 14, 0, -1, -1}, enabled_body);
    fixture.frame({350, 14, -1, 1, -1}, enabled_body); // Left
    CHECK(selected == 1);
    fixture.frame({350, 14, -1, 0, -1}, enabled_body);
    fixture.frame({350, 14, -1, -1, 1}, enabled_body); // Right
    CHECK(selected == 2);
    // A click elsewhere releases the keyboard.
    fixture.frame({350, 200, 0, -1, -1}, enabled_body);
    fixture.frame({350, 200, 1, -1, -1}, enabled_body);
    CHECK_FALSE(focus);
    fixture.frame({350, 200, 0, -1, -1}, enabled_body);
    fixture.frame({350, 200, -1, 1, -1}, enabled_body);
    CHECK(selected == 2);

    // Disabled: neither clicks nor keys change anything.
    selected = 1;
    focus = true;
    const auto disabled_body = [&] { body(false); };
    fixture.frame({50, 14, 0, -1, -1}, disabled_body);
    fixture.frame({50, 14, 1, -1, -1}, disabled_body);
    fixture.frame({50, 14, 0, 1, -1}, disabled_body);
    CHECK(selected == 1);
}

TEST_CASE("components chip grid aligns rows and reports the clicked chip", "[components]") {
    fixture_t fixture;
    static const char* const labels[] = {"1/16", "1/8", "1/4", "1/3", "1/6"};
    chrome::ChipGrid::Result result;
    const auto body = [&] {
        result = chrome::ChipGrid{labels, 5, 0, 3, "+"}.render(&fixture.context);
    };
    fixture.frame({}, body);
    // Three columns of a 400 px window: the second row starts one control_compact plus
    // item spacing lower, and its first chip sits in the same column as row one's.
    const float row_two_y = 4.0f + metrics::control_compact + fixture.context.style.window.spacing.y + 8.0f;
    fixture.frame({60, row_two_y, 0, -1, -1}, body);
    fixture.frame({60, row_two_y, 1, -1, -1}, body);
    CHECK(result.chosen == 3);
    CHECK_FALSE(result.add);
    fixture.frame({60, row_two_y, 0, -1, -1}, body);
    fixture.frame({200, row_two_y, 0, -1, -1}, body);
    fixture.frame({200, row_two_y, 1, -1, -1}, body);
    CHECK(result.chosen == 4);
    fixture.frame({200, row_two_y, 0, -1, -1}, body);
    fixture.frame({340, row_two_y, 0, -1, -1}, body);
    fixture.frame({340, row_two_y, 1, -1, -1}, body);
    CHECK(result.add);
    CHECK(result.chosen == -1);
}

TEST_CASE("components selection bar actions and disabled action", "[components]") {
    fixture_t fixture;
    static const chrome::BarAction actions[] = {
        {"Group", chrome::ButtonKind::secondary, nullptr, true},
        {"Move", chrome::ButtonKind::secondary, "Not yet", false},
        {"Delete", chrome::ButtonKind::destructive, nullptr, true}};
    int clicked = -2;
    const auto body = [&] {
        clicked = chrome::SelectionBar{"3 selected", actions, 3}.render(&fixture.context);
    };
    fixture.frame({}, body);
    // Actions are laid out from the right edge: Delete is last, Move before it.
    fixture.frame({370, 20, 0, -1, -1}, body);
    fixture.frame({370, 20, 1, -1, -1}, body);
    CHECK(clicked == 2);
    fixture.frame({370, 20, 0, -1, -1}, body);
    const float delete_width = 6 * 8.0f + 2 * metrics::gap_large;
    const float move_x = 400.0f - 4.0f - metrics::gap_medium - delete_width - metrics::gap_small - 20.0f;
    fixture.frame({move_x, 20, 0, -1, -1}, body);
    fixture.frame({move_x, 20, 1, -1, -1}, body);
    CHECK(clicked == -1);
}

TEST_CASE("components gallery draws inside the render target", "[components]") {
    fixture_t fixture;
    const int width = 900;
    const int height = 420;
    wormedit::ui::nuklear::tooltip::set_viewport(
        nk_rect(0, 0, static_cast<float>(width), static_cast<float>(height)));
    wormedit::ui::gallery::state_t state;
    fixture.input({});
    wormedit::ui::gallery::render(&fixture.context, state, width, height);
    const auto summary = summarize(fixture.context);
    CHECK_FALSE(summary.fills.empty());
    for (const auto& scissor : summary.scissors)
        verify_inside(scissor, nk_rect(0, 0, static_cast<float>(width), static_cast<float>(height)));
}

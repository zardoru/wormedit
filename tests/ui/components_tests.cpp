#include "ui/nuklear/chrome/catalog_chrome.h"
#include "ui/nuklear/chrome/content_layout.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/ui_gallery.h"
#include "ui/nuklear/chrome/ui_metrics.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <string>

#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
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

struct interface_theme_fixture_t {
    std::filesystem::path directory = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
        "assets/themes";

    interface_theme_fixture_t() { wormedit::ui::theme::load(directory); }
    ~interface_theme_fixture_t() { wormedit::ui::theme::load(directory); }
};

void check_control_color(nk_color actual, nk_color expected) {
    CHECK(nk_color_u32(actual) == nk_color_u32(expected));
}

void check_mixer_toggle_label(fixture_t& fixture, chrome::MixToggleKind kind, bool active) {
    bool drew_label = false;
    fixture.frame({}, [&] {
        nk_layout_row_dynamic(&fixture.context, metrics::control_compact, 1);
        chrome::MixToggle{.kind = kind, .active = active}.render(&fixture.context);
        const nk_command* command{};
        nk_foreach(command, &fixture.context) {
            if (command->type != NK_COMMAND_TEXT) continue;
            const auto* text = reinterpret_cast<const nk_command_text*>(command);
            const auto expected = active ? nk_rgb(255, 255, 255)
                : chrome::color(wormedit::ui::theme::UiColor::text);
            check_control_color(text->foreground, expected);
            drew_label = true;
        }
    });
    CHECK(drew_label);
}

void verify_inside(struct nk_rect inner, struct nk_rect outer) {
    CHECK(inner.x >= outer.x);
    CHECK(inner.y >= outer.y);
    CHECK(inner.x + inner.w <= outer.x + outer.w);
    CHECK(inner.y + inner.h <= outer.y + outer.h);
}

auto property_value_bounds(nk_context& context, std::string_view value) -> struct nk_rect {
    struct nk_rect clip{};
    const nk_command* command{};
    nk_foreach(command, &context) {
        if (command->type == NK_COMMAND_SCISSOR) {
            const auto* scissor = reinterpret_cast<const nk_command_scissor*>(command);
            clip = nk_rect(scissor->x, scissor->y, scissor->w, scissor->h);
        }
        if (command->type != NK_COMMAND_TEXT) continue;
        const auto* text = reinterpret_cast<const nk_command_text*>(command);
        if (std::string_view(text->string, static_cast<std::size_t>(text->length)) != value) continue;
        const auto width = text->font->width(text->font->userdata, text->height, text->string, text->length);
        const auto bounds = nk_rect(text->x, text->y, width, text->height);
        verify_inside(bounds, clip);
        return bounds;
    }
    FAIL("Numeric property did not draw its value");
    return nk_rect(0, 0, 0, 0);
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

void check_content_height(float actual, float expected) {
    CHECK(actual == expected);
}

void check_editor_selection(const nk_style_edit& edit) {
    const auto selected_text = nk_color_u32(chrome::color(wormedit::ui::theme::UiColor::selected_text));
    CHECK(nk_color_u32(edit.selected_text_normal) == selected_text);
    CHECK(nk_color_u32(edit.selected_text_hover) == selected_text);
}

void check_editor_cursor(const nk_style_edit& edit) {
    CHECK(nk_color_u32(edit.cursor_text_normal) ==
          nk_color_u32(chrome::color(wormedit::ui::theme::UiColor::surface)));
    CHECK(nk_color_u32(edit.cursor_text_hover) ==
          nk_color_u32(chrome::color(wormedit::ui::theme::UiColor::surface_hover)));
}

} // namespace

TEST_CASE("Interface palettes keep selected text and block cursors legible in shared editors",
          "[components][theme]") {
    namespace theme = wormedit::ui::theme;
    const auto directory = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()
        / "assets/themes";
    theme::load(directory);
    const auto generation = theme::field_generation();
    for (const auto& palette : theme::interface_themes()) {
        CAPTURE(palette.id);
        CHECK(theme::reload_interface_theme(palette.id).empty());
        fixture_t fixture;
        check_editor_selection(fixture.context.style.edit);
        check_editor_selection(fixture.context.style.property.edit);
        check_editor_cursor(fixture.context.style.edit);
        CHECK(theme::field_generation() == generation);
    }
    CHECK(theme::reload_interface_theme("").empty());
}

TEST_CASE("Numeric property values fit their scissor without an inset border",
          "[components][property-field]") {
    const auto initial = GENERATE(1, 512, 8192, -128);
    fixture_t fixture;
    fixture.font.height = GENERATE(13.0f, 18.0f);
    auto& context = fixture.context;
    int value = initial;
    fixture.frame({}, [&] {
        nk_layout_row_dynamic(&context, metrics::control_default, 1);
        nk_property_int(&context, "Memory", -8192, &value, 8192, 1, 1);
        static_cast<void>(property_value_bounds(context, std::to_string(initial)));
    });
    CHECK(context.style.property.edit.border == 0);
    CHECK(context.style.edit.border > 0);
}

TEST_CASE("Content height keeps reserve spacing and panel minima",
          "[components][content-layout]") {
    using namespace wormedit::ui::layout;
    const auto minimum = GENERATE(48.0f, 60.0f, 80.0f, 160.0f);
    const content_height_policy_t policy{
        .minimum = minimum, .reserved = 44.0f, .include_spacing = true};
    check_content_height(policy.height(400.0f, 120.0f, 6.0f), 230.0f);
    check_content_height(policy.height(100.0f, 120.0f, 6.0f), minimum);
    check_content_height(content_height_policy_t{.minimum = minimum, .reserved = 44.0f}
        .height(400.0f, 120.0f, 6.0f), 236.0f);
    check_content_height(content_height_policy_t{.minimum = minimum, .include_spacing = true}
        .height(400.0f, 120.0f, 6.0f), 274.0f);
}

TEST_CASE("Content height applies caller cap before the minimum",
          "[components][content-layout]") {
    using namespace wormedit::ui::layout;
    const content_height_policy_t capped{
        .minimum = 48.0f, .reserved = 12.0f, .maximum = 180.0f};
    check_content_height(capped.height(400.0f, 120.0f, 6.0f), 180.0f);
    check_content_height(capped.height(250.0f, 120.0f, 6.0f), 118.0f);
    check_content_height(capped.height(100.0f, 120.0f, 6.0f), 48.0f);
    check_content_height(content_height_policy_t{.minimum = 48.0f, .maximum = 20.0f}
        .height(400.0f, 120.0f, 6.0f), 48.0f);
}

TEST_CASE("Content height preserves operand order for invalid geometry",
          "[components][content-layout]") {
    using namespace wormedit::ui::layout;
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const content_height_policy_t minimum_first{.minimum = 60.0f};
    const content_height_policy_t available_first{
        .minimum = 60.0f, .clamp_order = content_height_clamp_order_t::available_first};
    check_content_height(minimum_first.height(nan, 120.0f, 6.0f), 60.0f);
    CHECK(std::isnan(available_first.height(nan, 120.0f, 6.0f)));
    check_content_height(content_height_policy_t{.minimum = 48.0f, .maximum = 180.0f}
        .height(nan, 120.0f, 6.0f), 180.0f);
    check_content_height(content_height_policy_t{.minimum = 48.0f, .maximum = nan}
        .height(400.0f, 120.0f, 6.0f), 48.0f);
}

TEST_CASE("Content height reads logical content origin and the next row without consuming it",
          "[components][content-layout]") {
    fixture_t fixture;
    auto& context = fixture.context;
    fixture.input({});
    CHECK(nk_begin(&context, "Translated layout", nk_rect(43, 91, 400, 300),
                   NK_WINDOW_NO_SCROLLBAR));
    nk_layout_row_dynamic(&context, 30, 1);
    nk_label(&context, "Previous row", NK_TEXT_LEFT);
    const auto region = nk_window_get_content_region(&context);
    const auto next = nk_widget_position(&context);
    CHECK(region.y >= 91.0f);
    CHECK(next.y > region.y);
    const wormedit::ui::layout::content_height_policy_t policy{
        .minimum = 60.0f, .reserved = 44.0f, .include_spacing = true};
    const auto height = wormedit::ui::layout::remaining_content_height(&context, policy);
    check_content_height(height, region.y + region.h - next.y - 44.0f - context.style.window.spacing.y);
    check_content_height(nk_widget_position(&context).y, next.y);
    nk_layout_row_dynamic(&context, height, 1);
    check_content_height(nk_widget_bounds(&context).h, height);
    nk_end(&context);
    nk_clear(&context);
}

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
    CHECK(disabled.fill.r == chrome::disabled_fill().r);
    CHECK(disabled.text.r == chrome::text_faint().r);
    const auto ghost = chrome::button_look(ButtonKind::ghost, ControlState::normal);
    CHECK(ghost.fill.a == 0);
    CHECK(chrome::button_look(ButtonKind::ghost, ControlState::hover).fill.a != 0);
    CHECK(chrome::button_look(ButtonKind::destructive, ControlState::normal).stroke.r ==
          metrics::destructive.r);
}

TEST_CASE("Mixer toggle labels use contrasting text in bundled interface themes", "[components][theme][mixer]") {
    interface_theme_fixture_t themes;
    const auto kind = GENERATE(chrome::MixToggleKind::mute, chrome::MixToggleKind::solo);
    const auto active = GENERATE(false, true);
    for (const auto& theme : wormedit::ui::theme::interface_themes()) {
        CAPTURE(theme.id, active);
        REQUIRE(wormedit::ui::theme::reload_interface_theme(theme.id).empty());
        fixture_t fixture;
        check_mixer_toggle_label(fixture, kind, active);
        if (!active)
            check_control_color(chrome::mix_toggle_fill(kind, active),
                                chrome::color(wormedit::ui::theme::UiColor::surface));
    }
}

TEST_CASE("Selected controls retain the theme highlight across interaction states", "[components][theme]") {
    interface_theme_fixture_t themes;
    const auto disabled = GENERATE(false, true);
    for (const auto& theme : wormedit::ui::theme::interface_themes()) {
        CAPTURE(theme.id, disabled);
        REQUIRE(wormedit::ui::theme::reload_interface_theme(theme.id).empty());
        fixture_t fixture;
        fixture.frame({}, [&] {
            if (disabled) nk_widget_disable_begin(&fixture.context);
            const auto style = chrome::button_style(&fixture.context, true);
            const auto fill = chrome::color(wormedit::ui::theme::UiColor::selected);
            const auto text = chrome::color(wormedit::ui::theme::UiColor::selected_text);
            for (const auto& background : {style.normal, style.hover, style.active})
                check_control_color(nk_rgb_factor(background.data.color, style.color_factor_background), fill);
            for (const auto foreground : {style.text_normal, style.text_hover, style.text_active})
                check_control_color(foreground, text);
            if (disabled) nk_widget_disable_end(&fixture.context);
            for (const auto state : {chrome::ControlState::normal, chrome::ControlState::hover,
                                     chrome::ControlState::active}) {
                const auto look = chrome::button_look(chrome::ButtonKind::primary, state);
                check_control_color(look.fill, fill);
                check_control_color(look.text, text);
            }
        });
    }
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

TEST_CASE("components gallery stock widgets draw inside a full-size target", "[components]") {
    fixture_t fixture;
    // Tall enough for the stock widgets section below the existing controls.
    const int width = 1280;
    const int height = 1024;
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

namespace {

struct popup_paint_t {
    bool opaque_fill{};
    int visible_shadows{};
    bool text_after_fill{};
};

auto clip_covers(struct nk_rect clip, struct nk_rect bounds) -> bool {
    return clip.x <= bounds.x && clip.y <= bounds.y &&
        clip.x + clip.w >= bounds.x + bounds.w &&
        clip.y + clip.h >= bounds.y + bounds.h;
}

auto opaque_panel(const nk_command_rect_filled& fill, struct nk_rect bounds) -> bool {
    return fill.x == bounds.x && fill.y == bounds.y && fill.w == bounds.w &&
        fill.h == bounds.h && fill.color.a == 255;
}

auto shadow_fill(const nk_command_rect_filled& fill) -> bool {
    return fill.color.r == 0 && fill.color.g == 0 && fill.color.b == 0 &&
        fill.color.a > 0 && fill.color.a < 255;
}

auto popup_paint(nk_context& context, struct nk_rect bounds) -> popup_paint_t {
    popup_paint_t result;
    struct nk_rect clip{};
    const nk_command* command{};
    nk_foreach(command, &context) {
        if (command->type == NK_COMMAND_SCISSOR) {
            const auto& scissor = *reinterpret_cast<const nk_command_scissor*>(command);
            clip = nk_rect(scissor.x, scissor.y, scissor.w, scissor.h);
        } else if (command->type == NK_COMMAND_RECT_FILLED) {
            const auto& fill = *reinterpret_cast<const nk_command_rect_filled*>(command);
            const bool covers_panel = clip_covers(clip, bounds);
            if (opaque_panel(fill, bounds) && covers_panel)
                result.opaque_fill = true;
            if (shadow_fill(fill) && covers_panel &&
                clip.y + clip.h > bounds.y + bounds.h)
                ++result.visible_shadows;
        } else if (command->type == NK_COMMAND_TEXT && result.opaque_fill) {
            result.text_after_fill = true;
        }
    }
    return result;
}

void check_rect_equal(struct nk_rect actual, struct nk_rect expected) {
    CHECK(actual.x == expected.x);
    CHECK(actual.y == expected.y);
    CHECK(actual.w == expected.w);
    CHECK(actual.h == expected.h);
}

void check_style_alpha(const nk_context& context, nk_byte background, nk_byte fixed) {
    CHECK(context.style.window.background.a == background);
    CHECK(context.style.window.fixed_background.data.color.a == fixed);
}

void check_popup_paint(const popup_paint_t& paint) {
    CHECK(paint.opaque_fill);
    CHECK(paint.visible_shadows == metrics::elevation(1).steps);
    CHECK(paint.text_after_fill);
}

} // namespace

TEST_CASE("dropdown surfaces extend beyond a short host on opening and later frames",
          "[components][dropdown-surfaces]") {
    fixture_t fixture;
    auto& context = fixture.context;
    const auto target = nk_rect(0, 0, 640, 480);
    const auto host = nk_rect(20, 20, 220, 50);
    wormedit::ui::nuklear::tooltip::set_viewport(target);
    const auto background = context.style.window.background;
    const auto fixed = context.style.window.fixed_background;
    int opened = 0;
    const auto render = [&](const frame_input_t& input) {
        fixture.input(input);
        REQUIRE(nk_begin(&context, "Short popup host", host, NK_WINDOW_NO_SCROLLBAR));
        nk_layout_row_dynamic(&context, 24, 1);
        const bool open = chrome::safe_combo_begin_label(&context, "Choose", nk_vec2(180, 120));
        struct nk_rect popup_bounds{};
        if (open) {
            popup_bounds = context.current->bounds;
            CHECK(popup_bounds.y + popup_bounds.h > host.y + host.h);
            nk_layout_row_dynamic(&context, 24, 1);
            nk_combo_item_label(&context, "First", NK_TEXT_LEFT);
            nk_combo_item_label(&context, "Second", NK_TEXT_LEFT);
            chrome::safe_combo_end(&context);
        }
        nk_end(&context);
        if (open) {
            check_popup_paint(popup_paint(context, popup_bounds));
            ++opened;
        }
        check_style_alpha(context, background.a, fixed.data.color.a);
        nk_clear(&context);
    };
    render({});
    render({50, 35, 1});
    render({50, 35, 0});
    render({-1, -1});
    CHECK(opened >= 2);
}

TEST_CASE("unelevated surface fills outside its content clip and restores the clip",
          "[components][dropdown-surfaces]") {
    fixture_t fixture;
    fixture.input({});
    REQUIRE(nk_begin(&fixture.context, "Surface", nk_rect(0, 0, 400, 300),
                     NK_WINDOW_NO_SCROLLBAR));
    auto* canvas = nk_window_get_canvas(&fixture.context);
    const auto bounds = nk_rect(40, 40, 180, 100);
    const auto inset = nk_rect(60, 60, 140, 60);
    nk_push_scissor(canvas, inset);
    chrome::paint_surface(canvas, bounds, 0, nk_rect(0, 0, 400, 300));
    check_rect_equal(canvas->clip, inset);
    nk_end(&fixture.context);
    const auto paint = popup_paint(fixture.context, bounds);
    CHECK(paint.opaque_fill);
    CHECK(paint.visible_shadows == 0);
}

TEST_CASE("menu surfaces use the viewport beyond a short header host",
          "[components][dropdown-surfaces]") {
    fixture_t fixture;
    auto& context = fixture.context;
    wormedit::ui::nuklear::tooltip::set_viewport(nk_rect(0, 0, 640, 480));
    int opened = 0;
    const auto render = [&](const frame_input_t& input) {
        fixture.input(input);
        REQUIRE(nk_begin(&context, "Menu header", nk_rect(20, 20, 220, 50),
                         NK_WINDOW_NO_SCROLLBAR));
        nk_layout_row_dynamic(&context, 24, 1);
        const bool open = nk_menu_begin_label(&context, "Menu", NK_TEXT_LEFT,
                                              nk_vec2(180, 120));
        struct nk_rect bounds{};
        if (open) {
            bounds = context.current->bounds;
            const auto saved = chrome::menu_surface_begin(&context);
            nk_layout_row_dynamic(&context, 24, 1);
            nk_menu_item_label(&context, "Action", NK_TEXT_LEFT);
            nk_menu_end(&context);
            chrome::menu_surface_end(&context, saved);
        }
        nk_end(&context);
        if (open) {
            check_popup_paint(popup_paint(context, bounds));
            ++opened;
        }
        nk_clear(&context);
    };
    render({});
    render({50, 35, 1});
    render({50, 35, 0});
    render({-1, -1});
    CHECK(opened >= 2);
}

TEST_CASE("dropdown surface fallback covers the popup without a viewport",
          "[components][dropdown-surfaces]") {
    fixture_t fixture;
    fixture.input({});
    REQUIRE(nk_begin(&fixture.context, "Fallback parent", nk_rect(0, 0, 220, 50),
                     NK_WINDOW_NO_SCROLLBAR));
    wormedit::ui::nuklear::tooltip::set_viewport(nk_rect(0, 0, 0, 0));
    // Exercise the shared surface with a valid popup whose parent is only a header.
    REQUIRE(nk_popup_begin(&fixture.context, NK_POPUP_STATIC, "Fallback popup",
                          NK_WINDOW_NO_SCROLLBAR, nk_rect(20, 30, 180, 120)));
    const auto bounds = fixture.context.current->bounds;
    chrome::menu_panel(&fixture.context);
    nk_popup_end(&fixture.context);
    nk_end(&fixture.context);
    CHECK(popup_paint(fixture.context, bounds).opaque_fill);
}

namespace {
void check_native_icon(bool value) { CHECK(value); }

void native_test_glyph(nk_command_buffer* canvas, struct nk_rect bounds, nk_color tint) {
    nk_fill_circle(canvas, bounds, tint);
}

auto native_icon_fill(nk_context* context) -> nk_color {
    const nk_command* command{};
    nk_foreach(command, context) {
        if (command->type != NK_COMMAND_RECT_FILLED) continue;
        const auto* rect = reinterpret_cast<const nk_command_rect_filled*>(command);
        if (rect->h == metrics::control_default) return rect->color;
    }
    return {};
}

struct native_icon_options_t {
    bool selected{};
    bool disabled{};
    bool parent_disabled{};
    nk_button_behavior behavior{NK_BUTTON_DEFAULT};
    bool enabled{true};
    bool clipped{};
};

struct native_icon_probe_t {
    fixture_t fixture;
    bool drawn{};
    bool clicked{};
    bool parent_still_disabled{};
    bool style_preserved{};
    bool hovered{};
    nk_color fill{};
    nk_color tint{};
    nk_color tooltip_tint{};
    std::string text;

    void frame(frame_input_t input, native_icon_options_t options = {}) {
        text.clear();
        wormedit::ui::nuklear::tooltip::set_viewport(nk_rect(0, 0, 400, 300));
        fixture.frame(input, [&] {
            auto* context = &fixture.context;
            nk_layout_row_dynamic(context, metrics::control_default, 1);
            nk_button_set_behavior(context, options.behavior);
            if (options.clipped) context->current->layout->clip = nk_rect(0, 250, 400, 20);
            if (options.parent_disabled) nk_widget_disable_begin(context);
            const auto before = context->style.button.color_factor_background;
            const auto text_before = context->style.text.color_factor;
            if (options.disabled && !options.parent_disabled) nk_widget_disable_begin(context);
            if (drawn)
                clicked = chrome::DrawnIconButton{.draw = native_test_glyph,
                    .tooltip = "Native icon reason", .selected = options.selected,
                    .enabled = options.enabled}.render(context);
            else
                clicked = chrome::IconButton{.glyph = wormedit::ui::lucide::Glyph::audio_lines,
                    .tooltip = "Native icon reason", .selected = options.selected,
                    .enabled = options.enabled}.render(context);
            hovered = chrome::button_hovered(context, nk_rect(8, 8, 384, 28));
            if (options.disabled && !options.parent_disabled) nk_widget_disable_end(context);
            parent_still_disabled = context->current->widgets_disabled != 0;
            style_preserved = before == context->style.button.color_factor_background &&
                text_before == context->style.text.color_factor;
            if (options.parent_disabled) nk_widget_disable_end(context);
            fill = native_icon_fill(context);
            const nk_command* command{};
            nk_foreach(command, context) {
                if (command->type == NK_COMMAND_CIRCLE_FILLED)
                    tint = reinterpret_cast<const nk_command_circle_filled*>(command)->color;
                if (command->type == NK_COMMAND_TEXT) {
                    const auto* label = reinterpret_cast<const nk_command_text*>(command);
                    text.append(label->string, static_cast<std::size_t>(label->length));
                    tooltip_tint = label->foreground;
                }
            }
        });
    }
};
} // namespace

TEST_CASE("Native icon widgets keep native press repeat and release behavior",
          "[components][native-icon]") {
    native_icon_probe_t probe;
    probe.drawn = GENERATE(false, true);
    probe.frame({100, 18, 0, -1, -1});
    check_native_icon(!probe.clicked);
    probe.frame({100, 18, 1, -1, -1});
    check_native_icon(probe.clicked);
    probe.frame({100, 18, -1, -1, -1});
    check_native_icon(!probe.clicked);
    probe.frame({100, 18, -1, -1, -1}, {.behavior = NK_BUTTON_REPEATER});
    check_native_icon(probe.clicked);
    probe.frame({100, 18, 0, -1, -1});
    check_native_icon(!probe.clicked);
}

TEST_CASE("Native icon disabled scopes keep feedback and parent ownership",
          "[components][native-icon]") {
    native_icon_probe_t probe;
    probe.drawn = GENERATE(false, true);
    const bool parent = GENERATE(false, true);
    const native_icon_options_t options{.selected = true, .disabled = true, .parent_disabled = parent};
    probe.frame({100, 18, 0, -1, -1}, options);
    probe.frame({100, 18, 1, -1, -1}, options);
    check_native_icon(!probe.clicked);
    check_native_icon(probe.parent_still_disabled == parent);
    check_native_icon(probe.style_preserved);
    check_native_icon(probe.hovered);
    check_native_icon(probe.text.find("Native icon reason") != std::string::npos);
    const auto& text_style = probe.fixture.context.style.text;
    check_native_icon(nk_color_u32(probe.tooltip_tint) ==
                      nk_color_u32(nk_rgb_factor(text_style.color, text_style.disabled_factor)));
    if (probe.drawn)
        check_native_icon(probe.tint.r == chrome::color(wormedit::ui::theme::UiColor::selected_text).r);
}

TEST_CASE("Native icon availability restores parent input and untouched style factors",
          "[components][native-icon]") {
    native_icon_probe_t probe;
    probe.drawn = GENERATE(false, true);
    const bool parent = GENERATE(false, true);
    const native_icon_options_t options{.selected = true, .parent_disabled = parent, .enabled = false};
    probe.frame({100, 18, 0, -1, -1}, options);
    probe.frame({100, 18, 1, -1, -1}, options);
    check_native_icon(!probe.clicked);
    check_native_icon(probe.parent_still_disabled == parent);
    check_native_icon(probe.style_preserved);
    check_native_icon(probe.text.find("Native icon reason") != std::string::npos);
    const auto& text_style = probe.fixture.context.style.text;
    check_native_icon(nk_color_u32(probe.tooltip_tint) ==
                      nk_color_u32(nk_rgb_factor(text_style.color, text_style.disabled_factor)));
    probe.frame({100, 18, 0, -1, -1});
    probe.frame({100, 18, 1, -1, -1});
    check_native_icon(probe.clicked);
}

TEST_CASE("Native icon glyph tint keeps selected and hover precedence",
          "[components][native-icon]") {
    native_icon_probe_t probe;
    probe.drawn = true;
    const bool selected = GENERATE(false, true);
    probe.frame({-1, -1, 0, -1, -1}, {.selected = selected});
    const auto normal = chrome::color(selected ? wormedit::ui::theme::UiColor::selected_text
                                               : wormedit::ui::theme::UiColor::text);
    check_native_icon(nk_color_u32(probe.tint) == nk_color_u32(normal));
    probe.frame({100, 18, 0, -1, -1}, {.selected = selected});
    check_native_icon(nk_color_u32(probe.tint) == nk_color_u32(chrome::color(selected
        ? wormedit::ui::theme::UiColor::selected_text : wormedit::ui::theme::UiColor::text_bright)));
}

TEST_CASE("Classic Mac selected icons keep their highlight when input is unavailable",
          "[components][native-icon][theme]") {
    interface_theme_fixture_t themes;
    REQUIRE(wormedit::ui::theme::reload_interface_theme("ui-mac-classic").empty());
    native_icon_probe_t probe;
    probe.drawn = GENERATE(false, true);
    const auto options = GENERATE(native_icon_options_t{.selected = true},
        native_icon_options_t{.selected = true, .parent_disabled = true},
        native_icon_options_t{.selected = true, .enabled = false});
    probe.frame({-1, -1, 0, -1, -1}, options);
    check_control_color(probe.fill, chrome::color(wormedit::ui::theme::UiColor::selected));
    probe.frame({100, 18, 0, -1, -1}, options);
    check_control_color(probe.fill, chrome::color(wormedit::ui::theme::UiColor::selected));
    probe.frame({100, 18, 1, -1, -1}, options);
    check_control_color(probe.fill, chrome::color(wormedit::ui::theme::UiColor::selected));
}

TEST_CASE("Native icon invalid cells never activate",
          "[components][native-icon]") {
    native_icon_probe_t probe;
    probe.drawn = GENERATE(false, true);
    probe.frame({100, 18, 0, -1, -1}, {.clipped = true});
    probe.frame({100, 18, 1, -1, -1}, {.clipped = true});
    check_native_icon(!probe.clicked);
    check_native_icon(probe.style_preserved);
    check_native_icon(!probe.parent_still_disabled);
}

namespace {
namespace catalog = wormedit::ui::nuklear;
void check_catalog(bool value) { CHECK(value); }
void require_catalog(bool value) { REQUIRE(value); }

struct catalog_label_t {
    std::string text;
    nk_uint foreground{};
    short y{};
    auto operator==(const catalog_label_t&) const -> bool = default;
};
auto catalog_labels(nk_context& context) -> std::vector<catalog_label_t> {
    std::vector<catalog_label_t> labels;
    const nk_command* command{};
    nk_foreach(command, &context) {
        if (command->type != NK_COMMAND_TEXT) continue;
        const auto& text = *reinterpret_cast<const nk_command_text*>(command);
        labels.push_back({.text = std::string(text.string, static_cast<std::size_t>(text.length)),
            .foreground = nk_color_u32(text.foreground), .y = text.y});
    }
    return labels;
}
} // namespace

TEST_CASE("Catalog header geometry preserves domain reservations and narrow clamps",
          "[components][catalog-chrome]") {
    using catalog::catalog_columns_t;
    using catalog::catalog_layout_t;
    check_catalog(catalog_columns_t::from(400, 4, catalog_layout_t::sounds) ==
        catalog_columns_t{.body_width = 348, .id_offset = 6, .name_offset = 42, .name_width = 214});
    check_catalog(catalog_columns_t::from(400, 4, catalog_layout_t::bga) ==
        catalog_columns_t{.body_width = 374, .id_offset = 4, .name_offset = 64, .name_width = 216});
    check_catalog(catalog_columns_t::from(80, 4, catalog_layout_t::sounds) ==
        catalog_columns_t{.body_width = 28, .id_offset = 6, .name_offset = 42, .name_width = 24});
    check_catalog(catalog_columns_t::from(80, 4, catalog_layout_t::bga) ==
        catalog_columns_t{.body_width = 54, .id_offset = 4, .name_offset = 64, .name_width = 12});
    check_catalog(catalog_columns_t::from(20, 8, catalog_layout_t::sounds).body_width == -40);
}

TEST_CASE("Catalog header renders the shared facts in order and muted color",
          "[components][catalog-chrome]") {
    fixture_t fixture;
    const auto layout = GENERATE(catalog::catalog_layout_t::sounds, catalog::catalog_layout_t::bga);
    std::vector<catalog_label_t> labels;
    fixture.frame({}, [&] {
        catalog::render_catalog_header(&fixture.context, layout);
        labels = catalog_labels(fixture.context);
    });
    require_catalog(labels.size() == 4);
    const std::array expected{"ID", "Name", "Uses", "State"};
    for (std::size_t index = 0; index < labels.size(); ++index) {
        check_catalog(labels[index].text == expected[index]);
        check_catalog(labels[index].foreground ==
            nk_color_u32(chrome::color(wormedit::ui::theme::UiColor::text_muted)));
    }
}

TEST_CASE("Catalog use filter reports selected clicks and respects disabled input",
          "[components][catalog-chrome]") {
    fixture_t fixture;
    const bool blocked = GENERATE(false, true);
    std::optional<std::uint8_t> chosen;
    const auto body = [&] {
        chosen = catalog::render_catalog_use_filter(&fixture.context, 2, blocked);
    };
    fixture.frame({250, 18, 0, -1, -1}, body);
    check_catalog(!chosen);
    fixture.frame({250, 18, 1, -1, -1}, body);
    check_catalog(blocked ? !chosen : chosen == 2);
    fixture.frame({250, 18, 0, -1, -1}, body);
    check_catalog(!chosen);
}

TEST_CASE("Catalog counts and capacity keep formatting and the strict warning threshold",
          "[components][catalog-chrome]") {
    check_catalog(catalog::grouped_catalog_count(0) == "0");
    check_catalog(catalog::grouped_catalog_count(999) == "999");
    check_catalog(catalog::grouped_catalog_count(1000) == "1,000");
    check_catalog(catalog::grouped_catalog_count(1'234'567) == "1,234,567");
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if constexpr (sizeof(std::size_t) == 8)
        check_catalog(catalog::grouped_catalog_count(maximum) == "18,446,744,073,709,551,615");
    else
        check_catalog(catalog::grouped_catalog_count(maximum) == "4,294,967,295");
    check_catalog(!catalog::catalog_capacity_t{.used = 0, .total = 0}.is_near_capacity());
    check_catalog(!catalog::catalog_capacity_t{.used = 90, .total = 100}.is_near_capacity());
    check_catalog(catalog::catalog_capacity_t{.used = 91, .total = 100}.is_near_capacity());
    check_catalog(!catalog::catalog_capacity_t{.used = maximum, .total = maximum}.is_near_capacity());
}

TEST_CASE("Catalog capacity footer keeps two rows text and colors",
          "[components][catalog-chrome]") {
    fixture_t fixture;
    const bool amber = GENERATE(false, true);
    std::vector<catalog_label_t> labels;
    fixture.frame({}, [&] {
        catalog::render_catalog_capacity(&fixture.context,
            {.used = amber ? 1000U : 900U, .total = 1000, .largest_free_run = 1234});
        labels = catalog_labels(fixture.context);
    });
    require_catalog(labels.size() == 2);
    check_catalog(labels[0].text == (amber ? "1,000 / 1,000 IDs" : "900 / 1,000 IDs"));
    check_catalog(labels[1].text == "Largest free run: 1234");
    check_catalog(labels[1].y - labels[0].y == 20 + fixture.context.style.window.spacing.y);
    check_catalog(labels[0].foreground == nk_color_u32(amber ? nk_rgb(226, 166, 64)
        : fixture.context.style.text.color));
    check_catalog(labels[1].foreground == nk_color_u32(fixture.context.style.text.color));
}

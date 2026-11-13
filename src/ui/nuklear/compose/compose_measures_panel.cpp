#include "ui/nuklear/compose/compose_measures_panel.h"
#include "ui/nuklear/chrome/content_layout.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/compose/compose_action_stamp.h"
#include "ui/nuklear/compose/compose_state.h"
#include "ui/nuklear/chrome/tooltip.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <nuklear.h>
#include <span>

namespace {
constexpr float row_height = 26.0f;

auto raise(wormedit::ui::workspace_action_t& action, const wormedit::ui::compose_view_t& view,
           WormUiComposeActionKind kind) -> bool {
    return wormedit::ui::raise_compose_action(action, view, kind,
                                              wormedit::ui::compose_project_stamp_t::preserve);
}

auto ids_of(const wormedit::ui::compose_view_t& view) -> std::span<const std::uint64_t> {
    return view.measure_row_ids;
}

// Fixed columns: the number is right-aligned in a width that fits six digits, so the Meter and
// Length columns stay put as the measure numbers roll over to more digits.
constexpr float cell_inset = 6.0f;
constexpr float meter_column = 64.0f;

auto text_width(const nk_user_font* font, const char* text) -> float {
    return font->width(font->userdata, font->height, text, static_cast<int>(std::strlen(text)));
}

auto number_column(const nk_user_font* font) -> float { return text_width(font, "000000") + cell_inset; }

void draw_cells(struct nk_command_buffer* canvas, const nk_user_font* font, struct nk_rect bounds, float inset,
                const char* number, const char* meter, const char* length, struct nk_color tint) {
    const auto y = bounds.y + (bounds.h - font->height) * 0.5f;
    const auto number_width = number_column(font);
    const auto text = [&](float x, const char* value) {
        nk_draw_text(canvas, nk_rect(x, y, std::max(0.0f, bounds.x + bounds.w - x), font->height), value,
                     static_cast<int>(std::strlen(value)), font, nk_rgba(0, 0, 0, 0), tint);
    };
    text(bounds.x + inset + number_width - cell_inset - text_width(font, number), number);
    text(bounds.x + inset + number_width + cell_inset, meter);
    text(bounds.x + inset + number_width + cell_inset + meter_column, length);
}

void row(nk_context* context, const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action,
         worm_ui_nuklear_compose_state_t& state, std::size_t index) {
    nk_layout_row_dynamic(context, row_height, 1);
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT) && !view.input_occluded;
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID || index >= view.measure_rows.size()) return;
    const auto& value = view.measure_rows[index];
    const auto selected = worm_ui_keysound_is_selected(state.measures_dock.measure_selection, value.measure_index);
    auto* canvas = nk_window_get_canvas(context);
    if (selected || hovered) nk_fill_rect(canvas, bounds, 3, nk_rgb(44, 72, 109));
    char number[24], meter[24], length[48];
    std::snprintf(number, sizeof(number), "%llu", static_cast<unsigned long long>(value.number));
    if (value.has_meter) std::snprintf(meter, sizeof(meter), "%u/%u", value.meter_numerator, value.meter_denominator);
    else std::snprintf(meter, sizeof(meter), "--");
    std::snprintf(length, sizeof(length), "%llu/%llu", static_cast<unsigned long long>(value.length_numerator),
                  static_cast<unsigned long long>(value.length_denominator));
    draw_cells(canvas, context->style.font, bounds, cell_inset, number, meter, length, nk_rgb(222, 228, 238));
    if (!clicked) return;
    const auto modifiers = SDL_GetModState();
    worm_ui_keysound_click(state.measures_dock.measure_selection, ids_of(view), index,
        (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, (modifiers & SDL_KMOD_SHIFT) != 0);
    // A plain or Shift click also moves the cursor, so the field follows the list.
    if (value.measure_index == state.measures_dock.measure_selection.primary &&
        raise(action, view, WORM_UI_COMPOSE_ACTION_GO_TO_MEASURE))
        action.compose_measure_number = value.number;
}

struct measure_definition_row_t {
    const char* label;
    std::span<char> numerator;
    std::span<char> denominator;
    const char* button;
    auto render(nk_context* context, bool disabled) const -> bool {
        nk_layout_row_template_begin(context, 27);
        nk_layout_row_template_push_static(context, 52.0f);
        nk_layout_row_template_push_variable(context, 40.0f);
        nk_layout_row_template_push_static(context, 10.0f);
        nk_layout_row_template_push_variable(context, 40.0f);
        nk_layout_row_template_push_static(context, 58.0f);
        nk_layout_row_template_end(context);
        nk_label(context, label, NK_TEXT_LEFT);
        (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
            numerator.data(), static_cast<int>(numerator.size()), nk_filter_decimal);
        nk_label(context, "/", NK_TEXT_CENTERED);
        (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
            denominator.data(), static_cast<int>(denominator.size()), nk_filter_decimal);
        if (disabled) nk_widget_disable_begin(context);
        const auto clicked = wormedit::ui::chrome::button_label(context, button);
        if (disabled) nk_widget_disable_end(context);
        return clicked && !disabled;
    }
};

void meter_controls(nk_context* context, const wormedit::ui::compose_view_t& view,
                    wormedit::ui::workspace_action_t& action, worm_ui_nuklear_compose_state_t& state) {
    const measure_definition_row_t row{.label = "Meter", .numerator = state.measures_dock.measures_meter_numerator,
        .denominator = state.measures_dock.measures_meter_denominator, .button = "Apply"};
    if (!row.render(context, state.measures_dock.measure_selection.selected.empty()) ||
        !raise(action, view, WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER)) return;
    action.compose_measure_ids = state.measures_dock.measure_selection.selected;
    action.timing_meter_numerator_text = row.numerator.data();
    action.timing_meter_denominator_text = row.denominator.data();
}

void length_controls(nk_context* context, const wormedit::ui::compose_view_t& view,
                     wormedit::ui::workspace_action_t& action, worm_ui_nuklear_compose_state_t& state) {
    const measure_definition_row_t row{.label = "Length", .numerator = state.measures_dock.measures_length_numerator,
        .denominator = state.measures_dock.measures_length_denominator, .button = "Set"};
    if (!row.render(context, state.measures_dock.measure_selection.selected.empty()) ||
        !raise(action, view, WORM_UI_COMPOSE_ACTION_SET_MEASURES_LENGTH)) return;
    action.compose_measure_ids = state.measures_dock.measure_selection.selected;
    action.compose_measure_length_numerator_text = row.numerator.data();
    action.compose_measure_length_denominator_text = row.denominator.data();
}
} // namespace

void worm_ui_nuklear_render_compose_measures(nk_context* context, const wormedit::ui::compose_view_t* view,
    wormedit::ui::workspace_action_t* action, worm_ui_nuklear_compose_state_t* state) {
    char title[64];
    std::snprintf(title, sizeof(title), "Measures  ·  %zu selected", state->measures_dock.measure_selection.selected.size());
    wormedit::ui::chrome::panel_header(context, title);
    nk_layout_row_dynamic(context, 27, 2);
    if (wormedit::ui::chrome::button_label(context, "Select all")) worm_ui_keysound_select_all(state->measures_dock.measure_selection, ids_of(*view));
    if (wormedit::ui::chrome::button_label(context, "Clear")) worm_ui_keysound_clear(state->measures_dock.measure_selection);
    meter_controls(context, *view, *action, *state);
    length_controls(context, *view, *action, *state);
    nk_layout_row_dynamic(context, 34, 1);
    nk_label_wrap(context, "Length clears incompatible meters.");
    worm_ui_keysound_prune(state->measures_dock.measure_selection, ids_of(*view));
    nk_layout_row_dynamic(context, wormedit::ui::metrics::row_text, 1);
    struct nk_rect header{};
    if (nk_widget(&header, context) != NK_WIDGET_INVALID) {
        // Rows sit inside the list's border and padding; start the header cells at the same x.
        const auto inset = cell_inset + context->style.window.group_padding.x + context->style.window.group_border;
        draw_cells(nk_window_get_canvas(context), context->style.font, header, inset, "No.", "Meter", "Length",
                   wormedit::ui::chrome::color(wormedit::ui::theme::UiColor::text_muted));
        if (nk_input_is_mouse_hovering_rect(&context->input, header))
            wormedit::ui::nuklear::tooltip::text(context,
                "Length: duration relative to a 4/4 measure (1/1 = four beats).\n"
                "Meter: the time signature as authored, kept unreduced; -- means none.");
    }
    const auto height = wormedit::ui::layout::remaining_content_height(context,
        {.minimum = 60.0f, .reserved = 5.0f});
    nk_layout_row_dynamic(context, height, 1);
    struct nk_list_view list{};
    if (nk_list_view_begin(context, &list, "Compose measures list", NK_WINDOW_BORDER,
                           static_cast<int>(row_height),
                           static_cast<int>(std::max<std::size_t>(view->measure_rows.size(), 1)))) {
        for (auto index = list.begin; index < std::min(list.end, static_cast<int>(view->measure_rows.size())); ++index)
            row(context, *view, *action, *state, static_cast<std::size_t>(index));
        nk_list_view_end(&list);
    }

}

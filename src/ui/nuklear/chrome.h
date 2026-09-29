#pragma once

#include "wormedit/nuklear_config.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/lucide_icons.h"

namespace wormedit::ui::chrome {

using theme::UiColor;
inline auto color(UiColor role) -> nk_color { return theme::ui(role); }

inline const char* status_hint{};

inline void begin_frame() { status_hint = nullptr; }

inline auto button_style(nk_context* context, bool selected) -> nk_style_button {
    auto style = context->style.button;
    style.normal = nk_style_item_color(selected ? color(UiColor::selected)
                                                 : nk_rgba(0, 0, 0, 0));
    style.hover = nk_style_item_color(color(UiColor::surface_hover));
    style.active = nk_style_item_color(color(UiColor::surface_active));
    style.border = 0.0f;
    style.rounding = 4.0f;
    style.text_normal = selected ? color(UiColor::selected_text) : color(UiColor::text);
    style.text_hover = color(UiColor::text_bright);
    style.text_active = color(UiColor::text_bright);
    return style;
}

inline void button_feedback(nk_context* context, struct nk_rect bounds, bool selected,
                            const char* tooltip) {
    if ((context->last_widget_state & NK_WIDGET_STATE_HOVER) != 0) {
        status_hint = tooltip;
        nk_stroke_rect(nk_window_get_canvas(context), bounds, 4.0f, 1.0f,
                       color(UiColor::border_hover));
    } else if (selected) {
        nk_stroke_rect(nk_window_get_canvas(context), bounds, 4.0f, 1.0f,
                       color(UiColor::selected));
    }
}

struct IconButton {
    lucide::Glyph glyph;
    const char* tooltip;
    bool selected{false};

    static constexpr float width = 34.0f;

    static void layout_columns(nk_context* context, int count = 1) {
        for (int index = 0; index < count; ++index)
            nk_layout_row_template_push_static(context, width);
    }

    auto render(nk_context* context) const -> bool {
        const auto bounds = nk_widget_bounds(context);
        auto style = button_style(context, selected);
        const bool clicked = nk_button_label_styled(context, &style, "") != 0;
        const auto tint = (context->last_widget_state & NK_WIDGET_STATE_HOVER) != 0
                              ? color(UiColor::text_bright)
                              : selected ? color(UiColor::selected_text) : color(UiColor::text);
        lucide::draw(context, bounds, glyph, tint);
        button_feedback(context, bounds, selected, tooltip);
        return clicked;
    }
};

inline auto mode_button(nk_context* context, const char* label, const char* tooltip,
                        bool selected) -> bool {
    const auto bounds = nk_widget_bounds(context);
    const auto style = button_style(context, selected);
    const bool clicked = nk_button_label_styled(context, &style, label) != 0;
    button_feedback(context, bounds, selected, tooltip);
    return clicked;
}

inline void apply_theme(nk_context* context) {
    nk_style_default(context);
    context->style.window.background = color(UiColor::background);
    context->style.window.fixed_background = nk_style_item_color(color(UiColor::background));
    context->style.window.group_border_color = color(UiColor::border);
    context->style.window.border_color = color(UiColor::border);
    context->style.text.color = color(UiColor::text);
    context->style.button.normal = nk_style_item_color(color(UiColor::surface));
    context->style.button.hover = nk_style_item_color(color(UiColor::surface_hover));
    context->style.button.active = nk_style_item_color(color(UiColor::surface_active));
    context->style.button.border_color = color(UiColor::border);
    context->style.button.text_normal = color(UiColor::text);
    context->style.button.text_hover = color(UiColor::text_bright);
    context->style.button.text_active = color(UiColor::text_bright);
    context->style.button.rounding = 4.0f;
    context->style.tab.background = nk_style_item_color(color(UiColor::surface));
}

} // namespace wormedit::ui::chrome

#pragma once

#include "ui/nuklear/chrome/chrome.h"

#include <algorithm>
#include <cstring>

namespace wormedit::ui::chrome {

// One shared small prompt for a single line of text: a title, a text field with a placeholder,
// and one hint line (or an error in its place). Rename bookmark, note and section labels, and
// Go to measure all use it, so they share the surface, spacing and key hints.
//
//   MenuSurface saved{};
//   if (text_prompt_begin(context, "Go to measure", bounds, saved)) {
//       const auto flags = text_prompt_body(context, {...}, focus_pending);
//       ...                                        // Enter: NK_EDIT_COMMITTED
//       nk_popup_end(context);                     // after nk_popup_close when finishing
//       text_prompt_end(context, saved);
//   }
//
// The popup is a form popup surface (see begin_form_popup): rounded panel, soft shadow, no stock
// square border, `menu_padding` all around.

inline constexpr float text_prompt_width = 320.0f;
inline constexpr float text_prompt_padding = menu_padding;

// Padding, three rows (title, field, hint) and the gaps between them. A static popup does not
// measure its content, so the height is derived from the same tokens the rows use.
inline constexpr float text_prompt_height =
    2.0f * text_prompt_padding + title_row_height + metrics::control_default + metrics::row_text +
    2.0f * metrics::gap_small;

struct TextPromptContent {
    const char* title{};
    const char* placeholder{};  // Shown in the empty field.
    const char* hint{};         // Key hints, e.g. "Enter to apply · Esc to cancel".
    const char* error{};        // Replaces the hint when not empty.
    char* buffer{};
    int capacity{};
    nk_plugin_filter filter{nk_filter_default};
    nk_flags flags{static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT};
};

// Starts the popup in the current window's content-region coordinates. Returns false when
// Nuklear refuses it; then nothing needs ending. On success pass `saved` to text_prompt_end()
// after nk_popup_end().
inline auto text_prompt_begin(nk_context* context, const char* name, struct nk_rect bounds,
                              MenuSurface& saved) -> bool {
    auto& window = context->style.window;
    const auto padding = window.popup_padding;
    const auto border = window.popup_border;
    window.popup_padding = nk_vec2(text_prompt_padding, text_prompt_padding);
    window.popup_border = 0.0f;
    const auto began = nk_popup_begin(context, NK_POPUP_STATIC, name, NK_WINDOW_NO_SCROLLBAR, bounds) != 0;
    window.popup_padding = padding;
    window.popup_border = border;
    if (began) saved = menu_surface_begin(context);
    return began;
}

inline void text_prompt_end(nk_context* context, const MenuSurface& saved) {
    menu_surface_end(context, saved);
}

// Width and placement shared by every caller: centered on `anchor_x`, clamped to the region.
inline auto text_prompt_width_for(struct nk_rect region) -> float {
    return std::min(text_prompt_width, region.w);
}

// Draws the title, field and hint rows and returns the field's edit flags.
inline auto text_prompt_body(nk_context* context, const TextPromptContent& content,
                             bool& focus_pending) -> nk_flags {
    popup_title(context, content.title ? content.title : "");

    nk_layout_row_dynamic(context, metrics::control_default, 1);
    const auto field = nk_widget_bounds(context);
    const auto flags = autofocus_text_field(context, content.flags, content.buffer, content.capacity,
                                            content.filter, focus_pending);
    const bool empty = content.buffer[0] == '\0';
    if (empty && content.placeholder && content.placeholder[0]) {
        const auto* font = context->style.font;
        const auto inset = context->style.edit.padding.x + context->style.edit.border + 2.0f;
        const auto text_height = font ? font->height : metrics::font_body;
        nk_draw_text(nk_window_get_canvas(context),
                     nk_rect(field.x + inset, field.y + (field.h - text_height) * 0.5f,
                             std::max(0.0f, field.w - 2.0f * inset), text_height),
                     content.placeholder, static_cast<int>(std::strlen(content.placeholder)), font,
                     nk_rgba(0, 0, 0, 0), text_faint());
    }

    nk_layout_row_dynamic(context, metrics::row_text, 1);
    const bool has_error = content.error && content.error[0];
    caption(context, has_error ? content.error : (content.hint ? content.hint : ""),
            has_error ? destructive_text() : text_faint());
    return flags;
}

} // namespace wormedit::ui::chrome

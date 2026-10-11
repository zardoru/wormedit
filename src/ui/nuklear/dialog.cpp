#include "ui/nuklear/dialog.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::ui::chrome {
namespace {

constexpr float default_window_height = 120.0f;
constexpr float close_row_height = 24.0f;

auto dialog_area(const DialogOptions& options, nk_context* context) -> struct nk_vec2 {
    if (options.area.x > 0.0f && options.area.y > 0.0f) return options.area;
    const auto& viewport = nuklear::tooltip::viewport();
    if (viewport.w > 0.0f && viewport.h > 0.0f) return nk_vec2(viewport.w, viewport.h);
    return nk_vec2(static_cast<float>(context->input.mouse.pos.x) + 800.0f, 600.0f);
}

// Window bounds: centered, at most the area minus a 16 px margin on each side.
auto dialog_window_bounds(const Dialog& dialog, const DialogOptions& options,
                          struct nk_vec2 area, bool& clamped) -> struct nk_rect {
    const auto available_w = std::max(1.0f, area.x - 2.0f * dialog_padding);
    const auto available_h = std::max(1.0f, area.y - 2.0f * dialog_padding);
    const auto wanted_h = options.resizable ? options.height :
        dialog.window_height > 0.0f ? dialog.window_height : default_window_height;
    const auto width = std::min(options.width, available_w);
    const auto height = std::min(wanted_h, available_h);
    clamped = wanted_h > available_h;
    return nk_rect(std::floor((area.x - width) * 0.5f), std::floor((area.y - height) * 0.5f),
                   width, height);
}

void clamp_dialog_bounds(Dialog& dialog, const DialogOptions& options, struct nk_vec2 area) {
    if (options.resizable) {
        dialog.bounds.w = std::clamp(dialog.bounds.w, std::min(options.minimum_size.x, area.x), area.x);
        dialog.bounds.h = std::clamp(dialog.bounds.h, std::min(options.minimum_size.y, area.y), area.y);
    }
    dialog.bounds.x = std::clamp(dialog.bounds.x, 0.0f, area.x - dialog.bounds.w);
    dialog.bounds.y = std::clamp(dialog.bounds.y, 0.0f, area.y - dialog.bounds.h);
}

void update_dialog_drag(Dialog& dialog, nk_context* context, const DialogOptions& options) {
    const auto& input = context->input;
    if (!options.movable || options.no_input ||
        nk_input_is_key_pressed(&input, NK_KEY_TEXT_RESET_MODE)) {
        dialog.dragging = false;
        return;
    }
    if (!input.mouse.buttons[NK_BUTTON_LEFT].down) dialog.dragging = false;
    if (dialog.dragging) {
        dialog.bounds.x = input.mouse.pos.x - dialog.drag_offset.x;
        dialog.bounds.y = input.mouse.pos.y - dialog.drag_offset.y;
    }
}

void retain_dialog_bounds(Dialog& dialog, nk_context* context, const char* id,
                          const DialogOptions& options, struct nk_vec2 area) {
    const auto* retained = nk_window_find(context, id);
    if ((!options.resizable && !options.movable) || !retained) return;
    if (options.resizable) dialog.bounds = retained->bounds;
    else if (options.movable) {
        dialog.bounds.x = retained->bounds.x;
        dialog.bounds.y = retained->bounds.y;
    }
    update_dialog_drag(dialog, context, options);
    clamp_dialog_bounds(dialog, options, area);
    nk_window_set_bounds(context, id, dialog.bounds);
}

// The dimming backdrop swallows input for everything behind a modal dialog.
void draw_backdrop(nk_context* context, const char* id, struct nk_vec2 area) {
    const std::string name = std::string(id) + "##dialog-backdrop";
    auto& style = context->style.window;
    const auto saved_background = style.fixed_background;
    const auto saved_padding = style.padding;
    const auto saved_border = style.border;
    style.fixed_background = nk_style_item_color(nk_rgba(0, 0, 0, 89));
    style.padding = nk_vec2(0, 0);
    style.border = 0.0f;
    nk_window_show(context, name.c_str(), NK_SHOWN);
    nk_window_set_focus(context, name.c_str());
    nk_begin(context, name.c_str(), nk_rect(0, 0, area.x, area.y),
             NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND);
    nk_end(context);
    style.fixed_background = saved_background;
    style.padding = saved_padding;
    style.border = saved_border;
}

void draw_title_row(Dialog& dialog, nk_context* context, const char* title) {
    if (!dialog.closable && !dialog.movable) {
        nk_layout_row_dynamic(context, dialog_title_height, 1);
        nk_label_colored(context, title, NK_TEXT_LEFT, color(UiColor::text_bright));
        return;
    }
    nk_layout_row_template_begin(context, close_row_height);
    if (dialog.movable) nk_layout_row_template_push_static(context, 18.0f);
    nk_layout_row_template_push_variable(context, 1.0f);
    if (dialog.closable) nk_layout_row_template_push_static(context, close_row_height);
    nk_layout_row_template_end(context);
    if (dialog.movable) {
        struct nk_rect grip{};
        nk_widget(&grip, context);
        lucide::draw(context, grip, lucide::Glyph::grip_vertical, color(UiColor::text_muted));
        dialog.drag_handle = grip;
        const auto title_bounds = nk_widget_bounds(context);
        dialog.drag_handle.w = title_bounds.x + title_bounds.w - grip.x;
        if (nk_input_is_mouse_hovering_rect(&context->input, dialog.drag_handle))
            nuklear::tooltip::text(context, "Drag to move window");
        if (!(context->current->flags & (NK_WINDOW_ROM | NK_WINDOW_NO_INPUT)) &&
            nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT) &&
            nk_input_is_mouse_hovering_rect(&context->input, dialog.drag_handle)) {
            dialog.dragging = true;
            dialog.drag_offset = nk_vec2(context->input.mouse.pos.x - dialog.bounds.x,
                                        context->input.mouse.pos.y - dialog.bounds.y);
        }
    }
    nk_label_colored(context, title, NK_TEXT_LEFT, color(UiColor::text_bright));
    if (dialog.closable && IconButton{lucide::Glyph::x, "Close"}.render(context))
        dialog.result = DialogResult::cancel;
}

} // namespace

auto dialog_begin(Dialog& dialog, nk_context* context, const char* id, const char* title,
                  const DialogOptions& options) -> bool {
    dialog.result = DialogResult::none;
    dialog.primary_drawn = false;
    dialog.modal = options.modal;
    dialog.closable = options.closable;
    dialog.escape_cancels = options.escape_cancels.value_or(options.modal);
    dialog.enter_confirms = options.enter_confirms.value_or(options.modal);
    dialog.resizable = options.resizable;
    dialog.movable = options.movable;
    dialog.enter_pending = dialog.enter_confirms && nk_input_is_key_pressed(&context->input, NK_KEY_ENTER);
    const auto area = dialog_area(options, context);
    if (options.modal) draw_backdrop(context, id, area);
    bool clamped{};
    dialog.bounds = dialog_window_bounds(dialog, options, area, clamped);
    retain_dialog_bounds(dialog, context, id, options, area);
    auto& window = context->style.window;
    dialog.saved_padding = window.padding;
    dialog.saved_spacing = window.spacing;
    // The painted surface draws the rounded 1 px border; Nuklear's square one would leave
    // stray pixels at the corners.
    dialog.saved_border = window.border;
    dialog.saved_minimum_size = window.min_size;
    if (options.resizable) window.min_size = options.minimum_size;
    window.border = 0.0f;
    window.padding = nk_vec2(dialog_padding, dialog_padding);
    window.spacing = nk_vec2(dialog_spacing, dialog_spacing);
    dialog.saved_window_flags = NK_WINDOW_BORDER | (clamped && !options.resizable ? 0 : NK_WINDOW_NO_SCROLLBAR) |
                                (options.resizable ? NK_WINDOW_SCALABLE : 0) |
                                (options.no_input ? NK_WINDOW_NO_INPUT : 0);
    nk_window_show(context, id, NK_SHOWN);
    // A modal keeps focus; a tool window is focused by Nuklear when it first appears.
    if (options.modal) nk_window_set_focus(context, id);
    dialog.surface = surface_window_begin(context, id, dialog.bounds, dialog.saved_window_flags, 2);
    dialog.open = dialog.surface.open;
    if (!dialog.open) return false;
    dialog.bounds = nk_window_get_bounds(context);
    draw_title_row(dialog, context, title);
    return true;
}

namespace {

// Greedy word wrap that splits long words by byte so a line never overflows.
auto wrap_dialog_text(nk_context* context, std::string_view text, float width)
    -> std::vector<std::string> {
    std::vector<std::string> lines;
    const auto* font = context->style.font;
    const auto measure = [&](std::string_view part) {
        return font->width(font->userdata, font->height, part.data(), static_cast<int>(part.size()));
    };
    std::string line;
    std::size_t position = 0;
    while (position < text.size()) {
        const auto end = text.find(' ', position);
        const auto word = text.substr(position, end == std::string_view::npos ? end : end - position);
        position = end == std::string_view::npos ? text.size() : end + 1;
        const auto candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
        if (measure(candidate) <= width || line.empty()) {
            line = candidate;
            continue;
        }
        lines.push_back(line);
        line = std::string(word);
    }
    if (!line.empty() || lines.empty()) lines.push_back(line);
    return lines;
}

} // namespace

void dialog_text(Dialog& dialog, nk_context* context, const char* text, struct nk_color tint) {
    (void)dialog;
    const auto width = nk_window_get_content_region(context).w;
    auto& spacing = context->style.window.spacing;
    const auto saved = spacing.y;
    const auto lines = wrap_dialog_text(context, text ? text : "", width);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        // Lines of one paragraph sit tight; the last row keeps the normal row gap.
        spacing.y = index + 1 < lines.size() ? 2.0f : saved;
        nk_layout_row_dynamic(context, context->style.font->height, 1);
        nk_label_colored(context, lines[index].c_str(), NK_TEXT_LEFT, tint);
    }
    spacing.y = saved;
}

void dialog_footer_begin(Dialog& dialog, nk_context* context, int buttons, bool destructive_first,
                         float button_width) {
    (void)dialog;
    buttons = std::max(1, buttons);
    nk_layout_row_template_begin(context, dialog_button_height);
    const auto fixed = destructive_first ? buttons - 1 : buttons;
    if (destructive_first) nk_layout_row_template_push_static(context, button_width);
    nk_layout_row_template_push_variable(context, 1.0f);
    for (int index = 0; index < fixed; ++index)
        nk_layout_row_template_push_static(context, button_width);
    nk_layout_row_template_end(context);
    // The variable spacer follows the destructive button, or leads the row without one.
    if (!destructive_first) nk_spacing(context, 1);
}

auto dialog_button(Dialog& dialog, nk_context* context, const char* label,
                   DialogButtonRole role, bool enabled) -> bool {
    const auto kind = role == DialogButtonRole::primary ? ButtonKind::primary
                    : role == DialogButtonRole::destructive ? ButtonKind::destructive
                                                            : ButtonKind::secondary;
    if (role == DialogButtonRole::primary) dialog.primary_drawn = enabled;
    auto clicked = Button{label, kind, nullptr, enabled}.render(context);
    if (role == DialogButtonRole::primary && enabled && dialog.enter_pending && !clicked) {
        clicked = true;
        dialog.enter_pending = false;
    }
    if (clicked && dialog.result == DialogResult::none)
        dialog.result = role == DialogButtonRole::primary ? DialogResult::primary
                      : role == DialogButtonRole::destructive ? DialogResult::destructive
                                                              : DialogResult::secondary;
    // The footer's variable spacer cell follows the far-left destructive button.
    if (role == DialogButtonRole::destructive) nk_spacing(context, 1);
    return clicked;
}

auto dialog_end(Dialog& dialog, nk_context* context) -> DialogResult {
    if (dialog.open) {
        const auto* layout = context->current->layout;
        // Nuklear stores each row's height with the trailing row spacing included; the last
        // declared row ends at at_y + its height, and one padding closes the window.
        const auto bottom = layout->at_y + layout->row.height - dialog_spacing;
        if (!dialog.resizable)
            dialog.window_height = std::ceil(bottom - context->current->bounds.y + dialog_padding);
        dialog.has_primary = dialog.primary_drawn;
        const auto& input = context->input;
        if (dialog.result == DialogResult::none) {
            if (dialog.escape_cancels && nk_input_is_key_pressed(&input, NK_KEY_TEXT_RESET_MODE))
                dialog.result = DialogResult::cancel;
        }
    }
    surface_window_end(context, dialog.surface);
    context->style.window.padding = dialog.saved_padding;
    context->style.window.spacing = dialog.saved_spacing;
    context->style.window.border = dialog.saved_border;
    context->style.window.min_size = dialog.saved_minimum_size;
    dialog.open = false;
    return dialog.result;
}

} // namespace wormedit::ui::chrome

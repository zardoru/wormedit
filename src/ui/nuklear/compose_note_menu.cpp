#include "ui/nuklear/compose_note_menu.h"
#include "ui/nuklear/tooltip.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/context_menu.h"
#include "ui/nuklear/theme.h"
#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {
namespace chrome = wormedit::ui::chrome;

constexpr float row_height = chrome::context_menu_row_height;
constexpr float primary_width = 120;
struct NoteMenu {
    chrome::ContextMenu popup{.title = "Note actions"};
    bool submenu{}, consumed{}, request_close{};
    std::uint64_t note{};
} menu;

auto stamp(const WormUiComposeView& view) -> chrome::PopupContextStamp {
    return {view.project_id, view.chart_id, view.document_revision, 0};
}

bool current(const WormUiComposeView& view) {
    return chrome::popup_context_matches(menu.popup.stamp, stamp(view));
}

bool raise(const WormUiComposeView& view, WormUiWorkspaceAction& action,
           WormUiComposeActionKind kind, std::uint64_t layer = 0) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return false;
    action.compose_action = kind;
    action.compose_input_chart_id = view.chart_id;
    action.compose_input_document_revision = view.document_revision;
    action.compose_note_context_id = menu.note;
    action.compose_semantic_layer_id = layer;
    return true;
}

std::uint64_t note_at(const WormUiComposeView& view, double x, double y) {
    for (std::size_t index = view.note_endpoint_count; index-- > 0;) {
        const auto& note = view.note_endpoints[index];
        if (note.proposal_ghost || note.paint_preview || !note.note_id) continue;
        const auto& rect = note.bounds;
        if (x >= rect.left && x <= rect.right &&
            std::abs(y - (rect.top + rect.bottom) * 0.5) <= 8) return note.note_id;
    }
    for (std::size_t index = view.hold_body_count; index-- > 0;) {
        const auto& hold = view.hold_bodies[index];
        const auto& rect = hold.bounds;
        if (!hold.proposal_ghost && hold.note_id && x >= rect.left && x <= rect.right &&
            y >= rect.top && y <= rect.bottom) return hold.note_id;
    }
    return 0;
}

// The popup has closed without an assignment; tell the application.
void finish_close(const WormUiComposeView& view, WormUiWorkspaceAction& action) {
    menu.consumed = true;
    menu.request_close = !raise(view, action, WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU);
}

void draw_layer_swatch(nk_context* context, const WormUiComposeSemanticLayerView& layer) {
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    nk_fill_rect(nk_window_get_canvas(context),
        nk_rect(bounds.x + 2, bounds.y + (bounds.h - 10) * 0.5f, 10, 10), 2,
        nk_rgba(layer.red, layer.green, layer.blue, layer.alpha));
}

bool render_layer_submenu_button(nk_context* context) {
    const auto bounds = nk_widget_bounds(context);
    const auto clicked = chrome::context_menu_button(context, "Layer");
    const auto x = bounds.x + bounds.w - 12;
    const auto y = bounds.y + bounds.h * 0.5f;
    const auto tint = context->style.contextual_button.text_normal;
    auto* canvas = nk_window_get_canvas(context);
    nk_stroke_line(canvas, x - 2, y - 4, x + 2, y, 1.5f, tint);
    nk_stroke_line(canvas, x + 2, y, x - 2, y + 4, 1.5f, tint);
    return clicked;
}

auto layer_disabled_reason(const WormUiComposeView& view,
                           const WormUiComposeSemanticLayerView& layer) -> const char* {
    if (layer.locked) return "Layer is locked";
    if (view.timing_focus || view.semantic_layer_gesture_active)
        return "Finish editing before assigning layers";
    return nullptr;
}

void render_layer(nk_context* context, const WormUiComposeView& view,
                   WormUiWorkspaceAction& action, const WormUiComposeSemanticLayerView& layer) {
    nk_layout_row_begin(context, NK_STATIC, row_height, 2);
    nk_layout_row_push(context, 14);
    draw_layer_swatch(context, layer);
    nk_layout_row_push(context, std::max(1.0f, nk_window_get_content_region(context).w - 24));
    const auto clicked =
        chrome::context_menu_button(context, layer.name, layer_disabled_reason(view, layer));
    if (clicked && raise(view, action, WORM_UI_COMPOSE_ACTION_ASSIGN_NOTE_MENU_LAYER, layer.layer_id)) {
        // Inside the layer list group; context_menu_end() closes the popup.
        chrome::context_menu_close(menu.popup);
        menu.consumed = true;
    }
    nk_layout_row_end(context);
}

void render_layer_row(nk_context* context, const WormUiComposeView& view,
                       WormUiWorkspaceAction& action, std::size_t row) {
    if (row == 0) {
        const auto neutral = wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::semantic_layer_none);
        render_layer(context, view, action, {.name = "No layer", .red = neutral.r,
            .green = neutral.g, .blue = neutral.b, .alpha = neutral.a});
        return;
    }
    const auto index = row - 1;
    if (index < view.note_menu_layer_first_row ||
        index - view.note_menu_layer_first_row >= view.note_menu_layer_count) {
        nk_layout_row_dynamic(context, row_height, 1);
        nk_label(context, "Loading layers…", NK_TEXT_LEFT);
        return;
    }
    render_layer(context, view, action, view.note_menu_layers[index - view.note_menu_layer_first_row]);
}

void render_submenu(nk_context* context, const WormUiComposeView& view, WormUiWorkspaceAction& action) {
    if (view.note_menu_layer_total_count > 100'000) return;
    nk_list_view list{};
    const auto count = static_cast<int>(view.note_menu_layer_total_count + 1);
    if (!nk_list_view_begin(context, &list, "Note layer rows", NK_WINDOW_BORDER,
                            static_cast<int>(row_height), count)) return;
    for (int offset = list.begin; offset < list.end; ++offset)
        render_layer_row(context, view, action, static_cast<std::size_t>(offset));
    const auto first = list.begin > 0 ? static_cast<std::size_t>(list.begin - 1) : 0;
    const auto requested = std::min<std::size_t>(WORM_UI_COMPOSE_NOTE_LAYER_WINDOW_CAPACITY,
                                               std::min(view.note_menu_layer_total_count - std::min(first, view.note_menu_layer_total_count),
                                                   static_cast<std::size_t>(list.end - list.begin)));
    nk_list_view_end(&list);
    if (menu.popup.open && view.note_menu_layer_total_count &&
        (first != view.note_menu_layer_first_row || requested > view.note_menu_layer_count) &&
        raise(view, action, WORM_UI_COMPOSE_ACTION_SET_NOTE_LAYER_MENU_WINDOW)) {
        action.compose_semantic_layer_first_row = first;
        action.compose_semantic_layer_row_count = std::max<std::size_t>(1, requested);
    }
}

void render_expanded(nk_context* context, const WormUiComposeView& view,
                     WormUiWorkspaceAction& action) {
    const auto region = nk_window_get_content_region(context);
    nk_layout_row_begin(context, NK_STATIC, std::max(row_height, region.h), 2);
    nk_layout_row_push(context, primary_width);
    if (nk_group_begin(context, "Note menu primary", NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(context, row_height, 1);
        (void)render_layer_submenu_button(context);
        nk_group_end(context);
    }
    nk_layout_row_push(context,
                       std::max(row_height, region.w - primary_width - context->style.window.spacing.x));
    render_submenu(context, view, action);
    nk_layout_row_end(context);
}

void render(nk_context* context, const WormUiComposeView& view, WormUiWorkspaceAction& action) {
    if (chrome::popup_panel(context) || !menu.popup.open) return;
    const auto size = menu.submenu
        ? nk_vec2(392, 422)
        : nk_vec2(primary_width + 2 * chrome::menu_padding, chrome::context_menu_height(1));
    if (!chrome::context_menu_begin(menu.popup, context, stamp(view), size)) {
        if (!menu.popup.open) finish_close(view, action);
        return;
    }
    if (menu.submenu) {
        render_expanded(context, view, action);
    } else {
        nk_layout_row_dynamic(context, row_height, 1);
        menu.submenu = render_layer_submenu_button(context);
    }
    chrome::context_menu_end(menu.popup, context);
}

void open_at_pointer(nk_context* context, const WormUiComposeView& view,
                     WormUiWorkspaceAction& action, struct nk_rect viewport, double right) {
    const auto mouse = context->input.mouse.pos;
    if (!nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT) || mouse.x >= right ||
        !nk_input_is_mouse_hovering_rect(&context->input, viewport) ||
        wormedit::ui::chrome::popup_panel(context)) return;
    const auto x = (mouse.x - viewport.x) * view.viewport_width_pixels / viewport.w;
    const auto y = (mouse.y - viewport.y) * view.viewport_height_pixels / viewport.h;
    const auto note = note_at(view, x, y);
    if (!note) return;
    menu.note = note;
    if (!raise(view, action, WORM_UI_COMPOSE_ACTION_OPEN_NOTE_LAYER_MENU)) return;
    chrome::context_menu_open(menu.popup, mouse, stamp(view));
    menu.consumed = true;
    menu.submenu = false;
}
} // namespace

bool compose_note_menu_open() { return menu.popup.open; }
bool compose_note_menu_consumed() { return menu.consumed || menu.popup.release_pending; }
void begin_compose_note_menu_frame() {
    menu.consumed = chrome::context_menu_owns_input(menu.popup) || menu.popup.close_pending;
}
void reset_compose_note_menu() { menu = {}; }

void prepare_compose_note_menu(const WormUiComposeView* view, bool active, bool focused,
                               WormUiWorkspaceAction* action) {
    if (menu.popup.open &&
        (!active || !focused || !view || !view->visible || view->input_occluded || !current(*view))) {
        chrome::context_menu_close(menu.popup);
        menu.request_close = true;
        menu.consumed = true;
    }
    if (menu.request_close && active && view && action &&
        raise(*view, *action, WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU)) menu.request_close = false;
}

void dismiss_compose_note_popup(nk_context* context) {
    chrome::context_menu_retire(menu.popup, context);
}

void process_compose_note_menu(nk_context* context, const WormUiComposeView& view,
                               WormUiWorkspaceAction& action, struct nk_rect viewport,
                               double input_right, bool focused) {
    (void)chrome::context_menu_update(menu.popup, context);
    if (!view.scene_available || !focused || view.input_occluded) return;
    if (!menu.popup.open && !menu.consumed && !view.paste_active && !view.semantic_layer_gesture_active)
        open_at_pointer(context, view, action, viewport, input_right);
    render(context, view, action);
}

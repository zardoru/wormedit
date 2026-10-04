#include "ui/nuklear/compose_note_menu.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/popup_lifecycle.h"
#include "ui/nuklear/theme.h"
#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {
constexpr const char* title = "Note actions";
constexpr float row_height = 24;
struct NoteMenu {
    bool open{}, submenu{}, consumed{}, release_pending{}, close_pending{}, request_close{};
    bool opened_this_frame{};
    std::uint64_t project{}, chart{}, revision{}, note{};
    float anchor_x{}, anchor_y{};
    struct nk_rect bounds{};
} menu;

bool current(const WormUiComposeView& view) {
    return wormedit::ui::chrome::popup_context_matches(
        {menu.project, menu.chart, menu.revision, 0},
        {view.project_id, view.chart_id, view.document_revision, 0});
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

void close(nk_context* context, WormUiWorkspaceAction& action, const WormUiComposeView& view) {
    menu.open = false;
    menu.consumed = true;
    wormedit::ui::chrome::popup_arm_release(menu.release_pending);
    menu.request_close = !raise(view, action, WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU);
    wormedit::ui::chrome::dismiss_popup(context, title, menu.bounds);
}

void update_bounds(nk_context* context) {
    const auto region = nk_window_get_content_region(context);
    const auto width = std::min(menu.submenu ? 392.0f : 136.0f, region.w);
    const auto height = std::min(menu.submenu ? 422.0f : 38.0f, region.h);
    menu.bounds = nk_rect(std::clamp(menu.anchor_x - region.x, 0.0f,
                                    std::max(0.0f, region.w - width)),
                          std::clamp(menu.anchor_y - region.y, 0.0f,
                                    std::max(0.0f, region.h - height)), width, height);
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
    const auto clicked = nk_button_label(context, "Layer");
    const auto x = bounds.x + bounds.w - 12;
    const auto y = bounds.y + bounds.h * 0.5f;
    const auto tint = context->style.text.color;
    auto* canvas = nk_window_get_canvas(context);
    nk_stroke_line(canvas, x - 2, y - 4, x + 2, y, 1.5f, tint);
    nk_stroke_line(canvas, x + 2, y, x - 2, y + 4, 1.5f, tint);
    return clicked;
}

void render_layer(nk_context* context, const WormUiComposeView& view,
                   WormUiWorkspaceAction& action, const WormUiComposeSemanticLayerView& layer) {
    nk_layout_row_begin(context, NK_STATIC, row_height, 2);
    nk_layout_row_push(context, 14);
    draw_layer_swatch(context, layer);
    nk_layout_row_push(context, std::max(1.0f, nk_window_get_content_region(context).w - 24));
    const auto bounds = nk_widget_bounds(context);
    const bool disabled = layer.locked || view.timing_focus || view.semantic_layer_gesture_active;
    if (disabled) nk_widget_disable_begin(context);
    const auto clicked = nk_button_label(context, layer.name);
    if (disabled) nk_widget_disable_end(context);
    if (disabled && nk_input_is_mouse_hovering_rect(&context->input, bounds))
        nk_tooltip(context, layer.locked ? "Layer is locked" : "Finish editing before assigning layers");
    if (clicked && !disabled && raise(view, action, WORM_UI_COMPOSE_ACTION_ASSIGN_NOTE_MENU_LAYER, layer.layer_id)) {
        menu.open = false;
        menu.consumed = true;
        wormedit::ui::chrome::popup_arm_release(menu.release_pending);
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
    if (menu.open && view.note_menu_layer_total_count &&
        (first != view.note_menu_layer_first_row || requested > view.note_menu_layer_count) &&
        raise(view, action, WORM_UI_COMPOSE_ACTION_SET_NOTE_LAYER_MENU_WINDOW)) {
        action.compose_semantic_layer_first_row = first;
        action.compose_semantic_layer_row_count = std::max<std::size_t>(1, requested);
    }
}

void render(nk_context* context, const WormUiComposeView& view, WormUiWorkspaceAction& action) {
    if (wormedit::ui::chrome::popup_panel(context)) return;
    if (menu.close_pending) {
        wormedit::ui::chrome::dismiss_popup(context, title, menu.bounds);
        menu.close_pending = false;
    }
    if (!menu.open) return;
    update_bounds(context);
    const auto bounds = wormedit::ui::chrome::popup_screen_bounds(context, menu.bounds);
    if (!current(view) || wormedit::ui::chrome::popup_dismiss_requested(
            context, bounds, !menu.opened_this_frame)) {
        close(context, action, view);
        return;
    }
    menu.opened_this_frame = false;
    if (!nk_popup_begin(context, NK_POPUP_STATIC, title,
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, menu.bounds)) {
        menu.open = false;
        menu.request_close = true;
        return;
    }
    const auto expanded = menu.submenu;
    nk_layout_row_begin(context, NK_STATIC, std::max(24.0f, menu.bounds.h - 12), expanded ? 2 : 1);
    nk_layout_row_push(context, 120);
    if (nk_group_begin(context, "Note menu primary", NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(context, 24, 1);
        if (render_layer_submenu_button(context)) menu.submenu = true;
        nk_group_end(context);
    }
    if (expanded) {
        nk_layout_row_push(context, std::max(24.0f, menu.bounds.w - 138));
        render_submenu(context, view, action);
    }
    nk_layout_row_end(context);
    if (!menu.open) nk_popup_close(context);
    nk_popup_end(context);
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
    menu.open = menu.consumed = true;
    menu.submenu = false;
    menu.project = view.project_id;
    menu.chart = view.chart_id;
    menu.revision = view.document_revision;
    menu.anchor_x = mouse.x;
    menu.anchor_y = mouse.y;
    menu.opened_this_frame = true;
}
} // namespace

bool compose_note_menu_open() { return menu.open; }
bool compose_note_menu_consumed() { return menu.consumed || menu.release_pending; }
void begin_compose_note_menu_frame() { menu.consumed = menu.open || menu.release_pending || menu.close_pending; }
void reset_compose_note_menu() { menu = {}; }

void prepare_compose_note_menu(const WormUiComposeView* view, bool active, bool focused,
                               WormUiWorkspaceAction* action) {
    if (menu.open && (!active || !focused || !view || !view->visible || view->input_occluded || !current(*view))) {
        menu.close_pending = menu.request_close = true;
        menu.open = false;
        menu.consumed = true;
    }
    if (menu.request_close && active && view && action &&
        raise(*view, *action, WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU)) menu.request_close = false;
}

void dismiss_compose_note_popup(nk_context* context) {
    if (menu.close_pending && !wormedit::ui::chrome::popup_panel(context)) {
        wormedit::ui::chrome::dismiss_popup(context, title, menu.bounds);
        menu.close_pending = false;
    }
}

void process_compose_note_menu(nk_context* context, const WormUiComposeView& view,
                               WormUiWorkspaceAction& action, struct nk_rect viewport,
                               double input_right, bool focused) {
    menu.opened_this_frame = false;
    (void)wormedit::ui::chrome::popup_update_release_pending(context, menu.release_pending);
    if (!view.scene_available || !focused || view.input_occluded) return;
    if (!menu.open && !menu.consumed && !view.paste_active && !view.semantic_layer_gesture_active)
        open_at_pointer(context, view, action, viewport, input_right);
    render(context, view, action);
}

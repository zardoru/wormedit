#pragma once
#include "ui/frontend.h"
struct nk_context;
struct nk_rect;

bool compose_note_menu_open();
bool compose_note_menu_consumed();
void begin_compose_note_menu_frame();
void reset_compose_note_menu();
void prepare_compose_note_menu(const wormedit::ui::compose_view_t*, bool active, bool focused,
                               wormedit::ui::workspace_action_t*);
void dismiss_compose_note_popup(nk_context*);
void process_compose_note_menu(nk_context*, const wormedit::ui::compose_view_t&, wormedit::ui::workspace_action_t&,
                               struct nk_rect viewport, double input_right, bool focused);

// The submenu contract is independently bounded and safe for frontend-only callers.
inline bool worm_ui_nuklear_note_layer_window_valid(const wormedit::ui::compose_view_t* view) {
    if (!view || view->note_menu_layers.size() > wormedit::ui::note_layer_window_limit ||
        (view->note_menu_layers.size() && view->note_menu_layers.empty()) ||
        view->note_menu_layer_total_count > 100'000 ||
        view->note_menu_layer_first_row > view->note_menu_layer_total_count) return false;
    if (view->note_menu_layers.size() > view->note_menu_layer_total_count - view->note_menu_layer_first_row)
        return false;
    for (size_t index = 0; index < view->note_menu_layers.size(); ++index)
        if (!view->note_menu_layers[index].layer_id) return false;
    return true;
}

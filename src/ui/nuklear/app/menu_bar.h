#pragma once
#include "ui/frontend.h"
#include "ui/workspace_menu.h"
#include "wormedit/nuklear_config.h"

namespace wormedit::ui {
auto build_workspace_menu(const wormedit::ui::workspace_view_t& view, bool blocked, bool text_owner,
                          bool frame_timing) -> workspace_menu_t;
auto render_workspace_menu(nk_context* context, const workspace_menu_t& model,
                           const char* title, bool focused, bool& popup_open,
                           bool& release_pending) -> workspace_menu_action_t;
}

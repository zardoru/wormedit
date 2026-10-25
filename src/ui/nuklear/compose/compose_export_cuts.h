#pragma once
struct nk_context;
namespace wormedit::ui { struct compose_view_t; struct workspace_action_t; }
void worm_ui_nuklear_render_compose_export_cuts(nk_context* context,
    const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action);

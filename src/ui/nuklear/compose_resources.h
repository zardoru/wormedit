#pragma once

#include "ui/frontend.h"
#include "ui/nuklear/compose_state.h"

struct nk_context;

void worm_ui_nuklear_render_compose_resources(struct nk_context* context,
                                              const wormedit::ui::compose_view_t* view,
                                              wormedit::ui::workspace_action_t* action,
                                              worm_ui_nuklear_compose_state_t* state);

// Modal surfaces render after the workspace, never inside the inspector window.
void worm_ui_nuklear_open_export_dialog(const wormedit::ui::compose_view_t& view,
    worm_ui_nuklear_compose_state_t& state);
void worm_ui_nuklear_open_layer_creation(const wormedit::ui::compose_view_t& view,
    worm_ui_nuklear_compose_state_t& state);
void worm_ui_nuklear_render_compose_dialogs(nk_context* context, const wormedit::ui::compose_view_t& view,
    wormedit::ui::workspace_action_t& action, worm_ui_nuklear_compose_state_t& state);

enum class compose_resource_origin_t { selection, sounds, bga };
enum class compose_resource_reset_reason_t { project_changed, chart_changed, source_cleared };

// Own the complete arming state together; never clear an ID independently of its origin.
void set_armed_resource(worm_ui_nuklear_compose_state_t& state, std::uint64_t id,
                        compose_resource_origin_t origin);
void reset_armed_resource(worm_ui_nuklear_compose_state_t& state,
                          compose_resource_reset_reason_t reason);
auto armed_resource_origin(const worm_ui_nuklear_compose_state_t& state) -> compose_resource_origin_t;

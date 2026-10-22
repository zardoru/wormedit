#pragma once
#include "ui/frontend.h"
struct nk_context;
struct SDL_KeyboardEvent;

void prepare_command_menu(const wormedit::ui::workspace_view_t* view, bool available);
auto open_command_menu() -> bool;
auto command_menu_owns_input() -> bool;
auto capture_command_menu_key(nk_context* context, const SDL_KeyboardEvent& event, bool ime) -> bool;
auto capture_command_menu_text(nk_context* context, const SDL_Event& event) -> bool;
void cancel_command_menu();
void reset_command_menu();
void render_command_menu(nk_context* context, const wormedit::ui::workspace_view_t& view,
                         wormedit::ui::workspace_action_t& action, int width, int height);

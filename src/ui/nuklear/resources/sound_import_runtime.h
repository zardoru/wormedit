#pragma once

#include "ui/frontend/compose_request.h"
#include "ui/frontend/sound_requests.h"
#include "ui/frontend/workspace.h"
#include "ui/nuklear/resources/sound_import_state.h"

struct nk_context;
struct SDL_Window;

auto worm_ui_nuklear_pending_sound_import() -> worm_ui_sound_import_state_t&;
bool worm_ui_nuklear_sound_import_active();
void worm_ui_nuklear_reset_sound_import();
void worm_ui_nuklear_sound_import_pick(SDL_Window* window, const wormedit::ui::workspace_view_t& view, wormedit::ui::sound_import_kind_t kind = wormedit::ui::sound_import_kind_t::generic);
void worm_ui_nuklear_consume_sound_picker(const wormedit::ui::workspace_view_t& view,
                                         wormedit::ui::compose_request_t& compose, wormedit::ui::request_stamp_t& stamp, wormedit::ui::sound_import_request_t& sound_import);
void worm_ui_nuklear_render_pending_sound_import(nk_context* context,
    const wormedit::ui::workspace_view_t& view, wormedit::ui::compose_request_t& compose, wormedit::ui::request_stamp_t& stamp, wormedit::ui::sound_import_request_t& sound_import, int width, int height);

#pragma once

#include "ui/frontend.h"

struct nk_command_buffer;
struct nk_rect;

// Backend-private upload/composition boundary. It consumes borrowed scalar
// RGBA views immediately and retains only OpenGL texture identities.
[[nodiscard]] bool worm_ui_nuklear_draw_player_preview_visual_layers(
    struct nk_command_buffer* canvas, struct nk_rect viewport, const WormUiPlayerPreviewView* view);

void worm_ui_nuklear_shutdown_player_preview_textures(void);

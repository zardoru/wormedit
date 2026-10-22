#pragma once

#include "ui/frontend.h"
#include "wormedit/nuklear_config.h"

struct WormUiNuklearImageQuad {
    struct nk_image image;
    struct nk_vec2 points[4];
    struct nk_vec2 uvs[4];
    struct nk_color color;
};

void worm_ui_nuklear_begin_image_quad_frame(void);

// Implemented beside NK_IMPLEMENTATION because it appends directly to pinned
// Nuklear's private converted-vertex stream.
void worm_ui_nuklear_append_image_quad(void* canvas, short x, short y, unsigned short width,
                                       unsigned short height, nk_handle callback_data);

// Adds one textured quad to Nuklear's converted draw list while preserving
// command order and the active clip. Points are supplied in texture order:
// top-left, top-right, bottom-right, bottom-left.
[[nodiscard]] bool worm_ui_nuklear_push_image_quad(struct nk_command_buffer* canvas,
                                                   const struct nk_image* image,
                                                   const struct nk_vec2* points,
                                                   struct nk_color color);

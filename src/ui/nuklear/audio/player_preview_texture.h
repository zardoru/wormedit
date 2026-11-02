#pragma once

#include "ui/frontend.h"

#include <cstddef>
#include <cstdint>

struct nk_command_buffer;
struct nk_rect;
struct SDL_Renderer;

enum class worm_ui_nuklear_preview_visual_failure_t : std::uint8_t {
    none,
    invalid_image,
    texture_allocation,
    upload_out_of_memory,
    upload_rejected,
    upload_size_mismatch,
    texture_identifier_unsupported,
    layout_invalid,
    command_capacity,
};

struct worm_ui_nuklear_preview_visual_result_t {
    bool drew_image{};
    std::size_t failure_count{};
    std::uint64_t first_failed_resource_id{};
    worm_ui_nuklear_preview_visual_failure_t first_failure{
        worm_ui_nuklear_preview_visual_failure_t::none};
};

// Begin once at the complete frontend image-quad frame boundary, before canvas
// AND auxiliary player recording (BGA and both sprite passes).
// The previous frame must already have completed deferred conversion/submission.
// Textures borrowed by this frame remain pinned until its submission is complete;
// never call this between passes or while commands from this frame are pending.
void worm_ui_nuklear_begin_player_preview_texture_frame();

// Assets thumbnails share the current frame's upload cache and pins. This
// records one contain-fit image and never begins or retires a texture frame.
[[nodiscard]] worm_ui_nuklear_preview_visual_result_t
worm_ui_nuklear_draw_player_preview_thumbnail(struct nk_command_buffer*, struct nk_rect,
    struct SDL_Renderer*, wormedit::song_id_t, std::uint64_t revision,
    const wormedit::ui::player_preview_visual_layer_view_t&);

// Backend-private upload/composition boundary. It consumes borrowed scalar
// RGBA views immediately, retains only SDL renderer texture identities, and reports
// bounded renderer failures without changing document or decode state.
[[nodiscard]] worm_ui_nuklear_preview_visual_result_t
worm_ui_nuklear_draw_player_preview_visual_layers(struct nk_command_buffer* canvas,
                                                  struct nk_rect viewport,
                                                  struct SDL_Renderer* renderer,
                                                  const wormedit::ui::player_preview_view_t* view);

[[nodiscard]] worm_ui_nuklear_preview_visual_result_t
worm_ui_nuklear_draw_player_preview_storyboard_sprites(struct nk_command_buffer* canvas,
                                                       struct nk_rect viewport,
                                                       struct SDL_Renderer* renderer,
                                                       const wormedit::ui::player_preview_view_t* view,
                                                       bool foreground);

[[nodiscard]] worm_ui_nuklear_preview_visual_result_t
worm_ui_nuklear_draw_player_preview_storyboard_sprites(struct nk_command_buffer*, struct nk_rect,
    struct SDL_Renderer*, wormedit::song_id_t, std::uint64_t revision,
    std::span<const wormedit::ui::player_preview_storyboard_sprite_view_t>, bool foreground);

[[nodiscard]] const char* worm_ui_nuklear_preview_visual_failure_message(
    worm_ui_nuklear_preview_visual_failure_t failure) noexcept;

void worm_ui_nuklear_shutdown_player_preview_textures(void);

namespace wormedit::ui { struct workspace_view_t; }
[[nodiscard]] auto worm_ui_nuklear_pin_primary_preview_textures(SDL_Renderer*, const wormedit::ui::workspace_view_t&)
    -> worm_ui_nuklear_preview_visual_result_t;

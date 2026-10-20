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

[[nodiscard]] const char* worm_ui_nuklear_preview_visual_failure_message(
    worm_ui_nuklear_preview_visual_failure_t failure) noexcept;

void worm_ui_nuklear_shutdown_player_preview_textures(void);

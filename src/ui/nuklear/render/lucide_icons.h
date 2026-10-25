#pragma once

#include "wormedit/nuklear_config.h"

#include <cstdint>
#include <span>

struct SDL_Renderer;
struct SDL_Texture;

namespace wormedit::ui::lucide {

enum class Glyph : std::uint8_t {
    zoom_in, zoom_out, panel_right_open, panel_right_close,
    rotate_ccw, paintbrush, chevron_left, chevron_right,
    play, pause, square, eye, minus, maximize, minimize_2, x,
    audio_lines, layers, chevrons_left, chevrons_right, magnet, metronome,
    lock, lock_open, eye_off, music_2,
    chevron_up, chevron_down,
    grip_vertical,
    waves_horizontal, gauge, hourglass, fast_forward, flame, anchor,
    count
};

struct immutable_texture_snapshot_t {
    SDL_Renderer* renderer{};
    std::span<SDL_Texture* const> textures;
    std::uint64_t generation{};
};
// Borrowed immutable images; valid until initialize/shutdown. A publication or
// clear advances generation, even if SDL later reuses a texture pointer.
auto immutable_textures() -> immutable_texture_snapshot_t;

auto initialize(SDL_Renderer* renderer) -> bool;
void shutdown();
// Borrowed for the field renderer; valid until shutdown.
auto texture(Glyph glyph) -> SDL_Texture*;
void draw(nk_context* context, struct nk_rect button, Glyph glyph, struct nk_color tint);

} // namespace wormedit::ui::lucide

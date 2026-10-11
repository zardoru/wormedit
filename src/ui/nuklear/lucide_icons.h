#pragma once

#include "wormedit/nuklear_config.h"

#include <cstdint>

struct SDL_Renderer;

namespace wormedit::ui::lucide {

enum class Glyph : std::uint8_t {
    zoom_in, zoom_out, panel_right_open, panel_right_close,
    rotate_ccw, paintbrush, chevron_left, chevron_right,
    play, pause, square, eye, minus, maximize, minimize_2, x,
    audio_lines, layers, chevrons_left, chevrons_right, magnet, metronome,
    lock, lock_open, eye_off, music_2,
    chevron_up, chevron_down,
    grip_vertical,
    count
};

auto initialize(SDL_Renderer* renderer) -> bool;
void shutdown();
void draw(nk_context* context, struct nk_rect button, Glyph glyph, struct nk_color tint);

} // namespace wormedit::ui::lucide

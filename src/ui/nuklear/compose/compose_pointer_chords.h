#pragma once

#include "ui/frontend.h"
#include <SDL3/SDL_keyboard.h>
#include <array>
#include <cstdint>

// Ordered canvas chord table; press-time ownership survives modifier changes.
enum class compose_pointer_chord_t { plain, add, toggle, hold, same_visible, same_chart };

inline constexpr std::uint16_t compose_pointer_modifier_mask = SDL_KMOD_SHIFT | SDL_KMOD_CTRL |
    SDL_KMOD_ALT | SDL_KMOD_GUI | SDL_KMOD_MODE | SDL_KMOD_LEVEL5;

inline auto compose_pointer_chord(std::uint16_t modifiers) -> compose_pointer_chord_t {
    constexpr std::uint16_t shift = 1, primary = 2, alt = 4, other = 8;
#ifdef __APPLE__
    constexpr auto primary_mask = SDL_KMOD_GUI;
    constexpr auto other_primary_mask = SDL_KMOD_CTRL;
#else
    constexpr auto primary_mask = SDL_KMOD_CTRL;
    constexpr auto other_primary_mask = SDL_KMOD_GUI;
#endif
    const auto bits = ((modifiers & SDL_KMOD_SHIFT) ? shift : 0) |
        ((modifiers & primary_mask) ? primary : 0) |
        ((modifiers & SDL_KMOD_ALT) ? alt : 0) |
        ((modifiers & other_primary_mask) ? other : 0);
    constexpr std::array table{
        compose_pointer_chord_t::plain, compose_pointer_chord_t::add,
        compose_pointer_chord_t::toggle, compose_pointer_chord_t::same_visible,
        compose_pointer_chord_t::hold, compose_pointer_chord_t::hold,
        compose_pointer_chord_t::same_chart, compose_pointer_chord_t::hold,
        compose_pointer_chord_t::toggle, compose_pointer_chord_t::toggle,
        compose_pointer_chord_t::toggle, compose_pointer_chord_t::same_visible,
        compose_pointer_chord_t::hold, compose_pointer_chord_t::hold,
        compose_pointer_chord_t::same_chart, compose_pointer_chord_t::hold};
    return table[bits];
}

inline auto compose_pointer_selection_mode(std::uint16_t modifiers) -> WormUiComposeSelectionMode {
    if (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) return WORM_UI_COMPOSE_SELECTION_TOGGLE;
    return (modifiers & SDL_KMOD_SHIFT) != 0
        ? WORM_UI_COMPOSE_SELECTION_ADD : WORM_UI_COMPOSE_SELECTION_REPLACE;
}

inline auto compose_pointer_same_sound(std::uint16_t modifiers) -> bool {
    const auto chord = compose_pointer_chord(modifiers);
    return chord == compose_pointer_chord_t::same_visible || chord == compose_pointer_chord_t::same_chart;
}

inline auto compose_wheel_zooms(std::uint16_t modifiers) -> bool {
    return (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
}

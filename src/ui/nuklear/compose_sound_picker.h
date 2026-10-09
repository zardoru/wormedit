#pragma once

#include "ui/frontend.h"
#include <array>
#include <cstdint>

struct nk_context;
struct worm_ui_nuklear_compose_state_t;
struct sound_picker_state_t {
    bool open{}, release_pending{}, close_pending{}, focus_pending{};
    std::uint64_t project{}, chart{}, revision{};
    std::size_t selected{};
    std::array<char, 96> query{};
};
void open_compose_sound_picker(sound_picker_state_t&, std::uint64_t project,
                               std::uint64_t chart, std::uint64_t revision);
void cancel_compose_sound_picker(sound_picker_state_t&);
[[nodiscard]] auto compose_sound_picker_owns_input(const sound_picker_state_t&) -> bool;
void render_compose_sound_picker(nk_context*, const WormUiComposeView&,
    WormUiWorkspaceAction&, worm_ui_nuklear_compose_state_t&, bool focused);

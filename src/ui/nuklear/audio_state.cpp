#include "ui/nuklear/audio_state.h"

auto worm_ui_nuklear_audio_state() noexcept -> worm_ui_nuklear_audio_state_t& {
    static worm_ui_nuklear_audio_state_t state;
    return state;
}

void worm_ui_nuklear_reset_audio_state() {
    worm_ui_nuklear_audio_state() = {};
}

auto worm_ui_nuklear_audio_list_key_command(int direction, bool audition) noexcept -> bool {
    auto& state = worm_ui_nuklear_audio_state();
    if (!state.sound_list_valid || !state.sound_list_focused)
        return false;
    if (direction < 0) state.list_move_pending = -1;
    else if (direction > 0) state.list_move_pending = 1;
    state.list_audition_pending = state.list_audition_pending || audition;
    return true;
}

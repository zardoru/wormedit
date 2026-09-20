#include "ui/nuklear/audio_state.h"

auto worm_ui_nuklear_audio_state() noexcept -> worm_ui_nuklear_audio_state_t& {
    static worm_ui_nuklear_audio_state_t state;
    return state;
}

void worm_ui_nuklear_reset_audio_state() {
    worm_ui_nuklear_audio_state() = {};
}

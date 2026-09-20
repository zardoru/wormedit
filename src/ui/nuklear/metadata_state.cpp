#include "ui/nuklear/metadata_state.h"

auto worm_ui_nuklear_metadata_state() noexcept -> worm_ui_nuklear_metadata_state_t& {
    static worm_ui_nuklear_metadata_state_t state;
    return state;
}

void worm_ui_nuklear_reset_metadata_state() {
    worm_ui_nuklear_metadata_state() = {};
}

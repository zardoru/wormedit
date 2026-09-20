#include "ui/nuklear/compose_state.h"

auto worm_ui_nuklear_compose_state() noexcept -> worm_ui_nuklear_compose_state_t& {
    static worm_ui_nuklear_compose_state_t state;
    return state;
}

void worm_ui_nuklear_reset_compose_state() {
    worm_ui_nuklear_compose_state() = {};
}

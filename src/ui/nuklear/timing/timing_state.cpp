#include "ui/nuklear/timing/timing_state.h"


auto worm_ui_nuklear_timing_state() noexcept -> worm_ui_nuklear_timing_state_t& {
    static worm_ui_nuklear_timing_state_t state;
    return state;
}

void worm_ui_nuklear_reset_timing_state() {
    worm_ui_nuklear_timing_state() = {};
}

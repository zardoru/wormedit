#pragma once

#include "ui/frontend/storyboard_transport.h"

struct nk_context;
namespace wormedit::ui {
// Only the storyboard-specific loop and frame-cost readout. Play/stop, scrub,
// rate, pitch, audio and musical position reuse the global transport controls.
// True only for a new loop request; the caller wraps it in a captured action.
[[nodiscard]] auto render_storyboard_transport(nk_context*, const storyboard_transport_view_t&,
    storyboard_transport_action_t&, bool input_available) -> bool;
} // namespace wormedit::ui

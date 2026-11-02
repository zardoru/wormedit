#pragma once

#include "ui/frontend/storyboard_transport.h"
#include "sys/app/frame_timing.h"

namespace wormedit::ui { struct storyboard_timeline_view_t; }
namespace wormedit::sys {

// No transport clock or playback state is stored here: the existing global
// controls and scene presenter continue to consume the common frame snapshot.
void build_storyboard_transport_view(ui::storyboard_transport_view_t&,
    const ui::storyboard_timeline_view_t&, bool available, bool loop_enabled,
    const frame_timing_t& timer = frame_timing());

} // namespace wormedit::sys

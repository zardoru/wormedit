#pragma once
#include "sys/storyboard/storyboard_video_export.h"
namespace wormedit::sys {
// UI-thread projection only; work counters are advisory and never authorize
// publication. A completed worker report owns that decision.
void build_storyboard_video_export_view(ui::storyboard_video_export_view_t&,
    const storyboard_video_export_t&);
}

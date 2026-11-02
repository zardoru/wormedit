#include "sys/storyboard/storyboard_video_export_view.h"
namespace wormedit::sys {
void build_storyboard_video_export_view(ui::storyboard_video_export_view_t& view,
    const storyboard_video_export_t& runtime) { runtime.refill(view); }
}

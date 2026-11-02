#include "sys/storyboard/storyboard_transport_view.h"
#include "ui/frontend/storyboard_timeline.h"
#include <algorithm>

namespace wormedit::sys {
namespace {
auto selected_interval(const ui::storyboard_timeline_view_t& timeline)
    -> std::optional<ui::storyboard_loop_region_t> {
    std::optional<ui::storyboard_loop_region_t> result;
    for (const auto& row : timeline.rows) {
        for (const auto& key : row.keys) {
            if (!key.selected) continue;
            if (!result) result = ui::storyboard_loop_region_t{.begin_ms = key.time_ms, .end_ms = key.time_ms};
            result->begin_ms = std::min(result->begin_ms, key.time_ms);
            result->end_ms = std::max(result->end_ms, key.time_ms);
        }
    }
    return result;
}
} // namespace

void build_storyboard_transport_view(ui::storyboard_transport_view_t& view,
    const ui::storyboard_timeline_view_t& timeline, bool available, bool loop_enabled,
    const frame_timing_t& timer) {
    view.selection = selected_interval(timeline);
    view.available = available;
    view.loop_enabled = loop_enabled;
    view.frame_ms = timer.size() == 0 ? std::nullopt
        : std::optional{timer.sample(timer.size() - 1).frame_ms};
}

} // namespace wormedit::sys

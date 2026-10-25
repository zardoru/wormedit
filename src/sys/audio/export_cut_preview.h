#pragma once

#include <wormedit/core/export_cut_frames.h>
#include <wormedit/audio/preview_bridge.h>

namespace wormedit::sys {

// Splits resolved source voices and music into kept pieces on one collapsed
// sample clock. No cut changes the source registry or authored chart data.
[[nodiscard]] auto prepare_export_cut_preview(preview_schedule_t& schedule,
    std::vector<audio::preview_object_source_binding_t>& bindings,
    std::span<const export_cut_t> cuts, std::uint64_t source_end)
    -> attempt_t<export_cut_frame_timeline_t>;

} // namespace wormedit::sys

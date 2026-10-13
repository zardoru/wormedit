#include "sys/bookmark_navigation.h"
#if defined(WORMEDIT_HAS_PROJECT_AUDIO_OUTPUTS)
#include "sys/chart_preview_state.h"
#endif
namespace wormedit::sys {
auto bookmark_navigation_t::preserve_preview_revision(chart_id_t chart,
    std::uint64_t previous, std::uint64_t next) -> bool {
#if defined(WORMEDIT_HAS_PROJECT_AUDIO_OUTPUTS)
    if (!preview || !preview_output || !workspace || !preview_output->playing() ||
        workspace->selected_chart != chart || preview->chart != chart || preview->revision != previous)
        return false;
    return preview->preserve_bookmark_revision(chart, previous, next);
#else
    static_cast<void>(chart);
    static_cast<void>(previous);
    static_cast<void>(next);
    return false;
#endif
}
} // namespace wormedit::sys

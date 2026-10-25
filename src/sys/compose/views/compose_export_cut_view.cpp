#include "sys/compose/views/compose_view_internal.h"

namespace wormedit::sys::compose_projection_detail {
void build_compose_export_cuts_view(const compose_projection_context_t<>& input) {
    auto& view = input.frame.view();
    const auto& cuts = input.history().document.export_cuts;
    view.export_cuts.resize(cuts.size());
    view.export_cut_overlay_truncated = false;
    std::size_t visible{};
    const auto& timing = input.cache().timing.marker_timing_index;
    for (std::size_t index = 0; index < cuts.size(); ++index) {
        const auto& cut = cuts[index];
        auto& output = view.export_cuts[index];
        output.id = cut.id;
        output.label.assign(cut.label ? std::string_view{*cut.label} : std::string_view{});
        output.span = {.available = true, .has_length = true,
            .start_milliseconds = cut.begin_milliseconds, .end_milliseconds = cut.end_milliseconds};
        if (wormedit::ui::compose_export_cuts_visible(compose_mode_of(input.frame.application_state)) && timing)
            build_compose_audio_span_view(output.span,
                build_compose_audio_marker_y_view(input, *timing, cut.begin_milliseconds),
                build_compose_audio_marker_y_view(input, *timing, cut.end_milliseconds), view);
        if (output.span.span_projection_visible && ++visible > wormedit::ui::maximum_export_cut_overlays) {
            output.span.begin_projection_visible = false;
            output.span.end_projection_visible = false;
            output.span.span_projection_visible = false;
            view.export_cut_overlay_truncated = true;
        }
    }
}
} // namespace wormedit::sys::compose_projection_detail

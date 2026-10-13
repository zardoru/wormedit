#include "sys/workspace_frame_view.h"

#include <algorithm>
#include <ranges>

namespace wormedit::sys {

void populate_workspace_frame_view(flattened_workspace_t& flattened,
                                   const workspace_frame_view_args_t& args) {
    flattened.view.metadata.timing_locked = args.timing_locked;
    flattened.view.metadata.audio_waveform_has_selected_asset =
        args.workspace.transport.audio_asset.has_value();
    flattened.view.metadata.audio_waveform_selected_asset_id =
        args.workspace.transport.audio_asset ? args.workspace.transport.audio_asset->value : 0;

    auto& audio_view = flattened.view.audio_slices;
    audio_view.document_revision = args.history.document.revision;
    audio_view.timing_locked = args.timing_locked;
    if (args.selected_chart) {
        const auto selected = args.history.document.find_chart(*args.selected_chart);
        if (selected != nullptr) {
            audio_view.has_chart = true;
            audio_view.chart_id = selected->id.value;
            audio_view.offset_numerator =
                selected->beat_zero_audio_offset_seconds.numerator;
            audio_view.offset_denominator =
                selected->beat_zero_audio_offset_seconds.denominator;
        }
    }
    if (args.audio_offset_drag) {
        const auto& drag = *args.audio_offset_drag;
        audio_view.offset_drag_active = true;
        audio_view.offset_drag_delta_frames = drag.delta_frames;
        audio_view.proposed_offset_numerator = drag.proposed_offset_seconds.numerator;
        audio_view.proposed_offset_denominator = drag.proposed_offset_seconds.denominator;
        flattened.view.metadata.audio_offset_drag_active = true;
        flattened.view.metadata.audio_offset_drag_delta_frames = drag.delta_frames;
        flattened.view.metadata.proposed_audio_offset_numerator =
            drag.proposed_offset_seconds.numerator;
        flattened.view.metadata.proposed_audio_offset_denominator =
            drag.proposed_offset_seconds.denominator;
    }

    flattened.view.transport_available = args.transport.available;
    flattened.view.transport_playing = args.transport.playing;
    flattened.view.transport_paused = args.transport.paused;
    flattened.view.transport_frame = args.transport.frame;
    flattened.view.transport_end_frame = args.transport.end_frame;
    flattened.view.transport_sample_rate = args.transport.sample_rate;

    args.player_preview_presenter.populate(
        flattened.view.player_preview, args.history, args.selected_chart,
        args.player_preview_visible, args.player_preview_skin, flattened.view.transport_frame,
        flattened.view.transport_sample_rate, args.asset_base
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        , args.player_image_job
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
        , args.player_video_job
#endif
    );
}

} // namespace wormedit::sys

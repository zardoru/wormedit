#include "sys/frame_timing.h"
#include "sys/application_frame_projection.h"

#include "sys/bmson_waveform_projection.h"
#include "sys/compose_view.h"
#include "sys/compose_waveform_overlays.h"
#include "sys/metadata_view_projection.h"
#include "sys/waveform_view_projection.h"

#include <wormedit/window.h>

namespace wormedit::sys {

auto project_application_frame(application_frame_projection_context_t context)
    -> flattened_workspace_t {
    frame_phase_scope_t frame_projection(frame_phase_t::workspace);
    auto flattened = flatten_workspace(
        context.model, context.history.document, context.status, context.recovery_controller.view(),
        context.recovery_scan_configured, context.stepmania_export_policy);
    flattened.rebind_view_pointers();

    workspace_frame_transport_t frame_transport;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    frame_transport.available =
        context.workspace.selected_chart.has_value() && context.preview_output_available;
    frame_transport.playing = context.preview_session.active() || context.audition_session.active();
    frame_transport.paused = context.preview_state.paused;
    frame_transport.frame =
        context.preview_session.active()    ? context.preview_session.timeline_frame()
        : context.audition_session.active() ? context.audition_session.timeline_frame()
                                            : context.preview_state.resume_frame;
    frame_transport.end_frame = context.preview_state.end_frame;
    frame_transport.sample_rate = audio::output_sample_rate;
#endif
    populate_workspace_frame_view(
        flattened,
        {
            .history = context.history,
            .workspace = context.model,
            .selected_chart = context.workspace.selected_chart,
            .timing_locked = context.application_command_state.timing_locked,
            .audio_offset_drag = context.audio_offset_drag,
            .transport = frame_transport,
            .player_preview_presenter = context.player_preview_presenter,
            .player_preview_visible = context.application_command_state.player_preview_visible,
            .player_preview_skin = context.application_command_state.player_preview_skin,
            .asset_base = context.asset_base,
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
            .player_image_job = context.player_image_job,
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
            .player_video_job = context.player_video_job,
#endif
        });
    populate_metadata_view_projection(flattened, context.history, context.workspace,
                                      context.asset_base, context.metadata_drop,
                                      context.metadata_asset_runtime);

    int compose_window_width = 0;
    int compose_window_height = 0;
    worm_window_get_size(&context.window, &compose_window_width, &compose_window_height);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    populate_audio_waveform_views(flattened, context.waveform_runtime.waveform_analysis().get(),
                                  context.waveform_runtime.waveform_job().busy(),
                                  compose_window_width);
#endif
    const bool timing_for_compose = flattened.view.compose.visible;
    if (timing_for_compose) {
        flattened.view.timing.visible = true;
    }
    populate_timing_view(flattened, context.history, context.workspace, context.timing_scene_cache,
                         compose_window_width, compose_window_height, context.timing_grid,
                         context.timing_edit.stage_view(), context.timing_edit.recipe_draft_view(),
                         context.timing_edit.recipe_preview_view(),
                         context.timing_edit.recipe_selection_binding.has_value());
    flattened.view.timing.initial_bpm_editable =
        flattened.view.timing.scene_available && !context.application_command_state.timing_locked;
    if (timing_for_compose) {
        flattened.view.timing.visible = false;
    }
    populate_compose_view(flattened, context.history, context.workspace,
                          context.compose_scene_cache, compose_window_width, compose_window_height,
                          context.compose_interaction_state, context.application_command_state,
                          context.timing_edit.stage_view());
    if (context.workspace.selected_chart) {
        for (const auto& chart : context.history.document.charts) {
            if (chart.id == *context.workspace.selected_chart) {
                flattened.view.compose.downscroll = compose_field_downscroll(chart);
                break;
            }
        }
    }
    context.compose_scene_cache.waveform_overview_total_frames = 0;
    context.compose_scene_cache.waveform_overview_sample_rate = 0;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    const auto* compose_waveform_analysis =
        context.waveform_runtime.waveform_asset() == context.workspace.transport.audio_asset
            ? context.waveform_runtime.waveform_analysis().get()
            : nullptr;
    std::optional<audio::aligned_waveform_window_t> detail_window;
    if (compose_waveform_analysis && compose_waveform_analysis->waveform &&
        context.compose_scene_cache.marker_timing_index && context.compose_interaction_state &&
        flattened.view.compose.visible && flattened.view.compose.scene_available) {
        detail_window = audio::aligned_waveform_window(*compose_waveform_analysis->waveform,
            *context.compose_scene_cache.marker_timing_index,
            {.viewport_scroll_pixels = context.compose_interaction_state->viewport_scroll_pixels,
             .viewport_height_pixels = flattened.view.compose.viewport_height_pixels,
             .pixels_per_standard_measure = flattened.view.compose.pixels_per_standard_measure});
    }
    context.waveform_runtime.service_compose_detail(context.workspace.transport.audio_asset, detail_window);
    const bool compose_waveform_overview_available = populate_compose_waveform_projection(
        flattened, context.compose_scene_cache, compose_waveform_analysis,
        context.waveform_runtime.waveform_job().busy(),
        context.workspace.transport.audio_asset.has_value(),
        context.compose_interaction_state ? &*context.compose_interaction_state : nullptr,
        context.waveform_runtime.detail_analysis().get(), &context.waveform_runtime.decoded_blocks());
    populate_compose_waveform_overlays(
        compose_waveform_overview_available, flattened, context.history, context.workspace,
        context.compose_scene_cache,
        context.compose_interaction_state ? &*context.compose_interaction_state : nullptr,
        context.audio_offset_drag, context.audio_offset_drag_tab, compose_waveform_analysis);
    populate_bmson_waveform_projection({
        .flattened = flattened,
        .history = context.history,
        .workspace = context.workspace,
        .interaction_state =
            context.compose_interaction_state ? &*context.compose_interaction_state : nullptr,
        .cache = context.compose_scene_cache,
        .audition_sources = context.audition_sources,
        .transport_waveform = {context.waveform_runtime.waveform_asset(),
                               context.waveform_runtime.waveform_analysis()},
        .channel_waveform = {context.waveform_runtime.channel_waveform_asset(),
                             context.waveform_runtime.channel_waveform_analysis()},
        .audition_source_generation = context.audition_source_generation,
    });
#endif
    return flattened;
}

} // namespace wormedit::sys

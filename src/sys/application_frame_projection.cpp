#include "sys/application_frame_projection.h"
#include "sys/compose_hover_timing.h"
#include "sys/frame_timing.h"

#include "sys/bmson_waveform_projection.h"
#include "sys/compose_view.h"
#include "sys/compose_waveform_overlays.h"
#include "sys/metadata_view_projection.h"
#include "sys/waveform_view_projection.h"

#include <wormedit/audio/playback_rate.h>
#include <wormedit/window.h>

#include <algorithm>

namespace wormedit::sys {
namespace {

void project_hover_timing(flattened_workspace_t& flattened, const document_history_t& history,
                          const workspace_controller_t& workspace, compose_scene_cache_t& cache) {
    auto& view = flattened.view.compose;
    view.hover_timing_valid = false;
    view.hover_timing_source_frame = 0;
    view.hover_timing_total_frames = 0;
    view.hover_timing_sample_rate = 0;
    view.hover_timing_label[0] = '\0';
    const auto& hover = cache.hover_timing;
    if (!hover.valid || !workspace.selected_chart || !cache.marker_timing_index ||
        !cache.cached_measure_index || !view.visible || !view.scene_available ||
        view.selected_audio_asset_id == 0 || hover.project_id != history.document.id.value ||
        hover.chart_id != workspace.selected_chart->value ||
        hover.document_revision != history.document.revision ||
        hover.audio_asset_id != view.selected_audio_asset_id ||
        !((hover.total_frames == view.waveform_overview_total_frames &&
           hover.sample_rate == view.waveform_overview_sample_rate) ||
          (hover.total_frames == flattened.view.transport_end_frame &&
           hover.sample_rate == flattened.view.transport_sample_rate)))
        return;
    const auto chart = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                         &chart_document_t::id);
    if (chart == history.document.charts.end())
        return;
    if (!format_compose_hover_timing(
            *chart, *cache.cached_measure_index, *cache.marker_timing_index, hover.source_frame,
            hover.sample_rate, view.hover_timing_label, sizeof(view.hover_timing_label)))
        return;
    view.hover_timing_source_frame = hover.source_frame;
    view.hover_timing_total_frames = hover.total_frames;
    view.hover_timing_sample_rate = hover.sample_rate;
    view.hover_timing_valid = true;
}

} // namespace
namespace {

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
// While stopped, the seek bar shows the Compose cursor as the playhead. Uses the previous
// frame's cached indexes, so it is absent until Compose has built a scene.
struct stopped_playhead_t {
    std::uint64_t frame{};
    std::uint64_t end_frame{};
};

auto stopped_playhead(const application_frame_projection_context_t& context)
    -> std::optional<stopped_playhead_t> {
    const auto& cache = context.compose_scene_cache;
    const auto* state = context.compose_interaction_state ? &*context.compose_interaction_state : nullptr;
    if (!state || !context.workspace.selected_chart || state->chart != *context.workspace.selected_chart ||
        state->virtual_bms_cursor_row || !cache.marker_timing_index || !cache.cached_measure_index ||
        cache.chart != state->chart)
        return std::nullopt;
    const auto at = timing_at(*cache.marker_timing_index, *cache.cached_measure_index, state->cursor);
    const auto end = timing_at(*cache.marker_timing_index, cache.marker_timing_index->total_rows);
    if (!at.seconds || !end.seconds) return std::nullopt;
    // Times before the audio start clamp to frame zero, as the preview start does.
    const auto to_frame = [](signed_rational_t seconds) -> std::optional<std::uint64_t> {
        if (seconds.numerator < 0) return std::uint64_t{0};
        return sample_frame_at(seconds, audio::output_sample_rate).frame;
    };
    const auto frame = to_frame(*at.seconds);
    const auto end_frame = to_frame(*end.seconds);
    if (!frame || !end_frame) return std::nullopt;
    return stopped_playhead_t{*frame, std::max(*end_frame, *frame)};
}

auto prepare_frame_stems(const application_frame_projection_context_t& context,
                         const flattened_workspace_t& flattened)
    -> std::vector<const sound_channel_t*> {
    if (!flattened.view.compose.visible || !flattened.view.compose.scene_available ||
        !context.workspace.selected_chart || !context.compose_interaction_state ||
        context.workspace.active_tab != workspace_tab_t::compose)
        return {};
    const auto chart = std::ranges::find(context.history.document.charts,
        *context.workspace.selected_chart, &chart_document_t::id);
    if (chart == context.history.document.charts.end() || chart->source_family != format_family_t::bmson)
        return {};
    const auto timing_matches = context.compose_scene_cache.chart == chart->id &&
        context.compose_scene_cache.revision == context.history.document.revision &&
        context.compose_scene_cache.marker_timing_index.has_value();
    const auto* timing = timing_matches ? &*context.compose_scene_cache.marker_timing_index : nullptr;
    const auto bindings = context.audition_sources.bindings();
    return indexed_visible_stem_channels(context.compose_scene_cache.stem_index,
        context.history.document, *chart, context.compose_interaction_state->cursor.measure,
        stem_window_measures(flattened.view.compose.viewport_height_pixels,
                             flattened.view.compose.pixels_per_standard_measure), bindings, timing);
}
#endif

} // namespace


auto project_application_frame(application_frame_projection_context_t context)
    -> flattened_workspace_t {
    frame_phase_scope_t frame_projection(frame_phase_t::workspace);
    flattened_workspace_t flattened;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    std::vector<const sound_channel_t*> frame_visible_stems;
#endif
    static const std::string empty_legacy_feedback;
    {
        frame_phase_scope_t flatten(frame_phase_t::workspace_flatten);
        WormUiKeysoundDockRequest keysound_request{};
        worm_ui_frontend_keysound_dock_request(&keysound_request);
        WormUiSoundUsageRequest sound_usage_request{};
        worm_ui_frontend_sound_usage_request(&sound_usage_request);
        WormUiBookmarkChooserRequest bookmark_request{};
        worm_ui_frontend_bookmark_chooser_request(&bookmark_request);
        flattened = flatten_workspace(
            context.model, context.history.document, empty_legacy_feedback,
                              context.recovery_controller.view(), context.recovery_scan_configured,
                              context.stepmania_export_policy, context.frame_arena,
            keysound_request.sort_by_id ? dock_sort_t::bms_id : dock_sort_t::natural_name,
            keysound_request, sound_usage_request, bookmark_request,
            &context.compose_scene_cache.keysound_list_cache);
        flattened.rebind_view_pointers();
        flattened.view.transport_metronome_enabled =
            context.application_command_state.transport_metronome_enabled;
        flattened.view.transport_metronome_available =
            context.application_command_state.transport_metronome_available;
    }

    {
        frame_phase_scope_t frame_view(frame_phase_t::workspace_frame_view);
        workspace_frame_transport_t frame_transport;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        frame_transport.available =
            context.workspace.selected_chart.has_value() && context.preview_output_available;
        frame_transport.playing =
            context.preview_state.playing(context.preview_session) || context.audition_session.active();
        frame_transport.paused = context.preview_state.paused;
        frame_transport.frame =
            context.preview_state.playing(context.preview_session) ? context.preview_state.current_frame(context.preview_session)
            : context.audition_session.active() ? context.audition_session.timeline_frame()
                                                : context.preview_state.resume_frame;
        frame_transport.end_frame = context.preview_state.end_frame;
        frame_transport.sample_rate = audio::output_sample_rate;
        if (!frame_transport.playing && !frame_transport.paused) {
            if (const auto stopped = stopped_playhead(context)) {
                frame_transport.frame = stopped->frame;
                frame_transport.end_frame = std::max(frame_transport.end_frame, stopped->end_frame);
            }
        }
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
    }
    {
        frame_phase_scope_t metadata(frame_phase_t::workspace_metadata);
        populate_metadata_view_projection(flattened, context.history, context.workspace,
                                          context.asset_base, context.metadata_drop,
                                          context.metadata_asset_runtime);
    }

    flattened.view.metadata.audio_offset_locked =
        context.application_command_state.audio_offset_locked;
    if (context.application_command_state.audio_offset_locked) {
        flattened.view.metadata.audio_offset_editable = false;
        flattened.view.audio_slices.timing_locked = true;
    }

    int compose_window_width = 0;
    int compose_window_height = 0;
    worm_window_get_size(&context.window, &compose_window_width, &compose_window_height);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    {
        frame_phase_scope_t audio_waveforms(frame_phase_t::workspace_audio_waveforms);
        populate_audio_waveform_views(flattened, context.waveform_runtime.waveform_analysis().get(),
                                      context.waveform_runtime.waveform_job().busy(),
                                      compose_window_width);
    }
#endif
    {
        frame_phase_scope_t timing(frame_phase_t::workspace_timing);
        if (flattened.view.compose.visible && !flattened.view.timing.visible) {
            populate_timing_view_for_compose(flattened, context.history, context.workspace,
                context.timing_scene_cache, compose_window_width, compose_window_height, context.timing_grid,
                context.timing_edit.stage_view());
        }
        flattened.view.timing.initial_bpm_editable =
            flattened.view.timing.scene_available && !context.application_command_state.timing_locked;
    }
    {
    frame_phase_scope_t compose_controls(frame_phase_t::workspace_compose_controls);
    const auto left_dock = context.application_command_state.compose_left_dock_pixels
        ? context.application_command_state.compose_left_dock_pixels() : 0;
    populate_compose_view(flattened, context.history, context.workspace,
                          context.compose_scene_cache, std::max(0, compose_window_width - left_dock),
                          compose_window_height,
                          context.compose_interaction_state, context.application_command_state,
                          context.timing_edit.stage_view(), {}, &context.bga_move_preview);
    }
    context.compose_scene_cache.waveform_overview_total_frames = 0;
    context.compose_scene_cache.waveform_overview_sample_rate = 0;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    const auto* compose_waveform_analysis =
        context.waveform_runtime.waveform_asset() == context.workspace.transport.audio_asset
            ? context.waveform_runtime.waveform_analysis().get()
            : nullptr;
    bool compose_waveform_overview_available = false;
    {
        frame_phase_scope_t compose_waveform(frame_phase_t::workspace_compose_waveform);
        std::optional<audio::aligned_waveform_window_t> detail_window;
        if (compose_waveform_analysis && compose_waveform_analysis->waveform &&
            context.compose_scene_cache.marker_timing_index && context.compose_interaction_state &&
            flattened.view.compose.visible && flattened.view.compose.scene_available) {
            detail_window = audio::aligned_waveform_window(
                *compose_waveform_analysis->waveform,
                *context.compose_scene_cache.marker_timing_index,
                {.viewport_scroll_pixels =
                     context.compose_interaction_state->viewport_scroll_pixels,
                 .viewport_height_pixels = flattened.view.compose.viewport_height_pixels,
                 .pixels_per_standard_measure =
                     flattened.view.compose.pixels_per_standard_measure});
        }
        context.waveform_runtime.service_compose_detail(
            context.workspace.transport.audio_asset, detail_window,
            flattened.view.transport_playing,
            static_cast<double>(
                audio::playback_rates[std::min<std::size_t>(
                                          context.application_command_state.transport_rate_index,
                                          8)]
                    .numerator) /
                audio::playback_rates[std::min<std::size_t>(
                                          context.application_command_state.transport_rate_index,
                                          8)]
                    .denominator);
        compose_waveform_overview_available = populate_compose_waveform_projection(
            flattened, context.compose_scene_cache, compose_waveform_analysis,
            context.waveform_runtime.waveform_job().busy(),
            context.workspace.transport.audio_asset.has_value(),
            context.compose_interaction_state ? &*context.compose_interaction_state : nullptr,
            context.waveform_runtime.detail_analysis().get(),
            &context.waveform_runtime.decoded_blocks(), context.waveform_runtime.playback_style(),
            context.application_command_state.bgm_bar_range,
            &context.application_command_state.bgm_bar_pan_override,
            &context.application_command_state.bgm_bar_window_capture);
    }
    {
        frame_phase_scope_t overlays(frame_phase_t::workspace_waveform_overlays);
        populate_compose_waveform_overlays(
            compose_waveform_overview_available, flattened, context.history, context.workspace,
            context.compose_scene_cache,
            context.compose_interaction_state ? &*context.compose_interaction_state : nullptr,
            context.audio_offset_drag, context.audio_offset_drag_tab, compose_waveform_analysis);
    }
    {
        frame_phase_scope_t source_playhead(frame_phase_t::workspace_source_playhead);
    }
    frame_visible_stems = prepare_frame_stems(context, flattened);
    context.waveform_runtime.service_compose_stem_waveforms(
        context.history.document, context.workspace, context.compose_scene_cache,
        context.compose_interaction_state ? &*context.compose_interaction_state : nullptr,
        frame_visible_stems);
    {
        frame_phase_scope_t bmson(frame_phase_t::workspace_bmson);
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
            .stem_analysis = [&runtime = context.waveform_runtime](asset_id_t asset) {
                return runtime.stem_waveform_analysis(asset);
            },
            .visible_stems = std::span<const sound_channel_t* const>(frame_visible_stems),
        });
    }
#endif
    // Sticky across the waveform's analysis flicker: the row only goes away when the song has no
    // audio.
    if (flattened.view.compose.visible) {
        context.compose_scene_cache.bgm_bar_audio_seen =
            flattened.view.compose.has_bgm_events ||
            context.workspace.transport.audio_asset.has_value() ||
            flattened.view.compose.bgm_bar_row_count != 0 ||
            flattened.view.compose.waveform_overview_row_count != 0;
    }
    project_hover_timing(flattened, context.history, context.workspace,
                         context.compose_scene_cache);
    return flattened;
}

} // namespace wormedit::sys

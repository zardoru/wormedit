#include "sys/compose_input_internal.h"
namespace wormedit::sys::compose_input_detail {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
auto fit_visible_source_frame(const wormedit::timing_index_t& timing,
                              const wormedit::editor_interaction_state_t& state,
                              const wormedit::audio::waveform_t& waveform, bool end)
    -> std::optional<std::uint64_t> {
    if (timing.divisions_per_standard_measure == 0 ||
        !std::isfinite(state.pixels_per_standard_measure) ||
        state.pixels_per_standard_measure <= 0 || !std::isfinite(state.viewport_scroll_pixels) ||
        !std::isfinite(state.viewport_height_pixels))
        return std::nullopt;
    const auto pixel = state.viewport_scroll_pixels + (end ? state.viewport_height_pixels : 0);
    const auto row =
        std::clamp(static_cast<long double>(pixel) * timing.divisions_per_standard_measure /
                       state.pixels_per_standard_measure,
                   0.0L, static_cast<long double>(timing.total_rows));
    const auto at =
        timing_at(timing, static_cast<std::uint64_t>(end ? std::ceil(row) : std::floor(row)),
                  end ? wormedit::timing_boundary_side_t::after_stops
                      : wormedit::timing_boundary_side_t::before_delays);
    if (!at || at.seconds->denominator == 0)
        return std::nullopt;
    const auto frame = static_cast<long double>(at.seconds->numerator) * waveform.sample_rate /
                       at.seconds->denominator;
    if (!std::isfinite(frame))
        return std::nullopt;
    if (frame <= 0)
        return 0;
    if (frame >= static_cast<long double>(waveform.total_frames))
        return waveform.total_frames;
    return static_cast<std::uint64_t>(end ? std::ceil(frame) : std::floor(frame));
}
#endif

void capture_fit_onsets(compose_scene_cache_t& cache,
                        const wormedit::editor_interaction_state_t& state,
                        const wormedit::workspace_controller_t& workspace) {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    cache.timing.beat_fit_onsets = {};
    cache.timing.beat_fit_onset_source.reset();
    if (!cache.timing.fit_waveform || !cache.timing.fit_waveform->waveform ||
        cache.timing.fit_waveform_asset != workspace.transport.audio_asset ||
        cache.timing.fit_waveform->waveform->sample_rate != cache.timing.beat_fit_drag->sample_rate)
        return;
    const auto& waveform = *cache.timing.fit_waveform->waveform;
    cache.timing.beat_fit_onset_source = waveform.key;
    const auto first = fit_visible_source_frame(*cache.timing.marker_timing_index, state, waveform, false);
    const auto last = fit_visible_source_frame(*cache.timing.marker_timing_index, state, waveform, true);
    if (!first || !last || *first >= *last)
        return;
    cache.timing.beat_fit_onsets = capture_compose_onsets(waveform, *first, *last);
#endif
}

auto fit_onset_target(compose_scene_cache_t& cache, const WormUiComposeInputAction& input,
                      std::uint64_t frame) -> std::uint64_t {
    cache.timing.beat_fit_onset_snapped = false;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    if (!input.beat_fit_onset_snap_held)
        return frame;
    // A captured cache is bound to its source identity; asynchronous refreshes
    // may fill coverage but never trigger a second query during this drag.
    if (cache.timing.beat_fit_onset_source &&
        (!cache.timing.fit_waveform || !cache.timing.fit_waveform->waveform ||
         cache.timing.fit_waveform->waveform->key != *cache.timing.beat_fit_onset_source))
        return frame;
    if (const auto onset =
            nearest_compose_onset(cache.timing.beat_fit_onsets, frame, cache.timing.beat_fit_drag->sample_rate)) {
        cache.timing.beat_fit_onset_snapped = true;
        return *onset;
    }
#endif
    return frame;
}

auto dispatch_compose_beat_fit_input(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    application_command_state_t& application_state, compose_scene_cache_t& scene_cache,
    const wormedit::chart_document_t* input_chart) -> std::optional<compose_input_family_result_t> {
    using namespace wormedit;
    switch (input.kind) {
    case WORM_UI_COMPOSE_INPUT_BEAT_FIT_BEGIN:
        return begin_compose_beat_fit(input, history, workspace, state, application_state,
                                      scene_cache, input_chart);
    case WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE:
    case WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE: {
        auto source_input = input;
        source_input.y_pixels =
            compose_source_y(input.y_pixels, state.viewport_height_pixels,
                             input_chart && compose_field_downscroll(*input_chart));
        return update_compose_beat_fit(source_input, history, workspace, state, application_state,
                                       scene_cache);
    }
    case WORM_UI_COMPOSE_INPUT_BEAT_FIT_CANCEL:
        scene_cache.clear_beat_fit_capture();
        return compose_input_family_result_t::stop_with_feedback(
            "Beat fit cancelled; timing was unchanged.", notification_severity_t::hint,
            notification_source_t::input);
    default:
        return std::nullopt;
    }
}

auto dispatch_compose_offset_fit_input(const WormUiComposeInputAction& input,
                                       compose_input_context_t& context)
    -> std::optional<compose_input_family_result_t> {
    using namespace wormedit;
    auto& cache = context.environment.controls.cache;
    auto& action = context.ui_action;
    if (input.kind == WORM_UI_COMPOSE_INPUT_OFFSET_FIT_CANCEL) {
        cache.timing.offset_fit_drag.reset();
        action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_CANCEL;
        return compose_input_family_result_t::stop_with_feedback(
            "Offset fit cancelled; timing was unchanged.", notification_severity_t::hint,
            notification_source_t::input);
    }
    if (input.kind == WORM_UI_COMPOSE_INPUT_OFFSET_FIT_BEGIN) {
        if (context.environment.commands.audio_offset_edits_locked() ||
            cache.timing.offset_fit_drag || !cache.timing.marker_timing_index ||
            !context.environment.document.workspace.transport.audio_asset || input.object_id == 0) {
            return compose_input_family_result_t{
                "Offset fitting requires an unlocked waveform and idle chart gesture."};
        }
        cache.clear_beat_fit_capture();
        const auto status = begin_compose_beat_fit(
            input, context.environment.document.history, context.environment.document.workspace,
            context.state, context.environment.commands, cache,
            context.input_chart);
        if (!cache.timing.beat_fit_drag)
            return status;
        cache.timing.offset_fit_drag = compose_scene_cache_t::offset_fit_drag_t{
            .beat = *cache.timing.beat_fit_drag,
            .timing = *cache.timing.marker_timing_index,
            .asset = *context.environment.document.workspace.transport.audio_asset,
            .target_y_pixels = input.y_pixels};
        cache.timing.beat_fit_drag.reset();
        action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_BEGIN;
        action.audio_offset_drag_chart_id = context.state.chart.value;
        action.audio_offset_drag_document_revision =
            context.environment.document.history.document.revision;
        action.audio_offset_drag_sample_rate = cache.timing.offset_fit_drag->beat.sample_rate;
        action.audio_offset_drag_delta_frames = 0;
        return compose_input_family_result_t::stop_with_feedback(
            "Dragging beat to shift waveform offset.", notification_severity_t::hint,
            notification_source_t::input);
    }
    if (input.kind != WORM_UI_COMPOSE_INPUT_OFFSET_FIT_UPDATE &&
        input.kind != WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE)
        return std::nullopt;
    if (!cache.timing.offset_fit_drag ||
        cache.timing.offset_fit_drag->asset != context.environment.document.workspace.transport.audio_asset ||
        context.environment.commands.audio_offset_edits_locked()) {
        cache.timing.offset_fit_drag.reset();
        action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_CANCEL;
        return compose_input_family_result_t{
            "Offset fit expired after its chart or waveform changed."};
    }
    const auto& document = context.environment.document.history.document;
    if (!document_stamp_t{.project = document.id,
                          .revision = cache.timing.offset_fit_drag->beat.document_revision}
             .is_current(document)) {
        cache.timing.offset_fit_drag.reset();
        action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_CANCEL;
        return compose_input_family_result_t{stale_action_message("Offset fit")};
    }
    const auto& drag = *cache.timing.offset_fit_drag;
    cache.timing.offset_fit_drag->target_y_pixels = input.y_pixels;
    const auto target = sample_frame_at_chart_y(
        drag.timing, context.state.viewport_scroll_pixels,
        context.state.pixels_per_standard_measure, context.input_source_y(input.y_pixels),
        context.state.viewport_height_pixels, drag.beat.sample_rate);
    if (!target || *target >= cache.waveform.waveform_overview_total_frames) {
        if (input.kind == WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE) {
            cache.timing.offset_fit_drag.reset();
            action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_CANCEL;
        }
        return compose_input_family_result_t{"Offset target is outside the source audio range."};
    }
    constexpr auto max_frame = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (*target > max_frame || drag.beat.original_sample_frame > max_frame) {
        cache.timing.offset_fit_drag.reset();
        action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_CANCEL;
        return compose_input_family_result_t{"Offset target exceeds the supported frame range."};
    }
    const auto delta = static_cast<std::int64_t>(*target) -
                       static_cast<std::int64_t>(drag.beat.original_sample_frame);
    action.audio_offset_drag_action = input.kind == WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE
                                          ? WORM_UI_AUDIO_OFFSET_DRAG_COMMIT
                                          : WORM_UI_AUDIO_OFFSET_DRAG_UPDATE;
    action.audio_offset_drag_chart_id = drag.beat.chart.value;
    action.audio_offset_drag_document_revision = drag.beat.document_revision;
    action.audio_offset_drag_sample_rate = drag.beat.sample_rate;
    action.audio_offset_drag_delta_frames = delta;
    if (input.kind == WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE)
        cache.timing.offset_fit_drag.reset();
    return compose_input_family_result_t::stop_with_feedback(
        input.kind == WORM_UI_COMPOSE_INPUT_OFFSET_FIT_RELEASE
            ? "Offset fit committed."
            : "Previewing exact waveform offset.",
        notification_severity_t::hint, notification_source_t::input);
}

auto previous_beat_fit_anchor(const wormedit::chart_document_t& chart,
                              const wormedit::measure_index_t& measures, std::uint64_t beat_row)
    -> std::optional<wormedit::local_position_t> {
    std::optional<wormedit::local_position_t> anchor;
    std::uint64_t anchor_row{};
    const auto consider = [&](wormedit::local_position_t position) {
        const auto row = wormedit::absolute_row(measures, position);
        if (row && *row < beat_row && (!anchor || *row > anchor_row)) {
            anchor = position;
            anchor_row = *row;
        }
    };
    for (const auto& bpm : chart.bpms)
        consider(bpm.position);
    for (const auto& timing_anchor : chart.timing_anchors)
        consider(timing_anchor.position);
    return anchor;
}

auto begin_compose_beat_fit(
    const WormUiComposeInputAction& input, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, wormedit::editor_interaction_state_t& state,
    application_command_state_t& application_state, compose_scene_cache_t& scene_cache,
    const wormedit::chart_document_t* input_chart) -> compose_input_family_result_t {
    using namespace wormedit;
    if (application_state.project.timing_recipe.timing_locked || scene_cache.timing.beat_fit_drag ||
        editor_interaction_has_pending_gesture(state) || !scene_cache.scene.index ||
        !scene_cache.timing.marker_timing_index || scene_cache.waveform.waveform_overview_sample_rate == 0 ||
        scene_cache.waveform.waveform_overview_total_frames == 0 || !input_chart || input.object_id == 0) {
        return compose_input_family_result_t{
            "Beat fitting requires an unlocked waveform and idle chart gesture."};
    }
    const auto& measures = scene_cache.scene.index->measures;
    const auto beat_row = input.object_id;
    const auto measure = std::ranges::upper_bound(measures.entries, beat_row, {},
                                                  &measure_index_entry_t::absolute_begin_row);
    if (measure == measures.entries.begin()) {
        return compose_input_family_result_t{"Selected beat is outside the chart."};
    }
    const auto& entry = *std::prev(measure);
    if (beat_row - entry.absolute_begin_row >= entry.row_capacity) {
        return compose_input_family_result_t{"Selected beat is outside the chart."};
    }
    const auto beat = local_position_t{entry.id, beat_row - entry.absolute_begin_row};
    const auto anchor = previous_beat_fit_anchor(*input_chart, measures, beat_row);
    if (!anchor) {
        return compose_input_family_result_t{
            "No earlier BPM change or timing anchor can anchor this beat fit."};
    }
    const auto begun = begin_beat_interval_anchor_drag(
        history.document, input_chart->id, *anchor, beat, scene_cache.waveform.waveform_overview_sample_rate,
        application_state.project.timing_recipe.timing_locked);
    if (!begun) {
        return input_diagnostic_feedback(begun.diagnostics, "Beat fit could not start.",
                                         notification_severity_t::warning,
                                         notification_source_t::edit);
    }
    if (begun.drag->original_sample_frame >= scene_cache.waveform.waveform_overview_total_frames) {
        return compose_input_family_result_t{"Selected beat lies after the source audio ends."};
    }
    scene_cache.timing.beat_fit_drag = begun.drag;
    scene_cache.timing.beat_fit_asset = workspace.transport.audio_asset;
    scene_cache.timing.beat_fit_onset_snapped = false;
    capture_fit_onsets(scene_cache, state, workspace);
    return compose_input_family_result_t::stop_with_feedback(
        "Dragging beat against the fixed waveform; release to apply.",
        notification_severity_t::hint, notification_source_t::input);
}

auto update_beat_fit_proposal(const WormUiComposeInputAction& input,
                              const wormedit::document_history_t& history,
                              const wormedit::beat_interval_drag_t& drag,
                              std::uint64_t target_frame, bool timing_locked)
    -> wormedit::beat_interval_drag_result_t {
    using namespace wormedit;
    const auto frame_update =
        update_beat_interval_drag(history.document, drag, target_frame, timing_locked);
    if (!frame_update || !input.beat_fit_ctrl_held || input.beat_fit_onset_snap_held)
        return frame_update;
    const auto snapped_bpm =
        quantize_bpm(frame_update.drag->proposed_bpm,
                     {input.beat_fit_snap_step_numerator, input.beat_fit_snap_step_denominator});
    if (!snapped_bpm)
        return {};
    return update_beat_interval_drag_to_bpm(history.document, *frame_update.drag, *snapped_bpm,
                                            timing_locked);
}

auto update_compose_beat_fit(const WormUiComposeInputAction& input,
                             wormedit::document_history_t& history,
                             wormedit::workspace_controller_t& workspace,
                             wormedit::editor_interaction_state_t& state,
                             application_command_state_t& application_state,
                             compose_scene_cache_t& scene_cache) -> compose_input_family_result_t {
    using namespace wormedit;
    if (!scene_cache.timing.beat_fit_drag || !scene_cache.timing.marker_timing_index ||
        scene_cache.timing.beat_fit_drag->document_revision != history.document.revision ||
        scene_cache.timing.beat_fit_asset != workspace.transport.audio_asset ||
        scene_cache.waveform.waveform_overview_sample_rate != scene_cache.timing.beat_fit_drag->sample_rate) {
        scene_cache.clear_beat_fit_capture();
        return compose_input_family_result_t{"Beat fit has no matching captured transaction."};
    }
    const auto frame = sample_frame_at_chart_y(
        *scene_cache.timing.marker_timing_index, state.viewport_scroll_pixels,
        state.pixels_per_standard_measure, input.y_pixels, state.viewport_height_pixels,
        scene_cache.timing.beat_fit_drag->sample_rate);
    if (!frame || *frame >= scene_cache.waveform.waveform_overview_total_frames) {
        if (input.kind == WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE) {
            scene_cache.clear_beat_fit_capture();
        }
        return compose_input_family_result_t{"Beat target is outside the source audio range."};
    }
    const auto target_frame = fit_onset_target(scene_cache, input, *frame);
    const auto updated = update_beat_fit_proposal(input, history, *scene_cache.timing.beat_fit_drag,
                                                  target_frame, application_state.project.timing_recipe.timing_locked);
    if (updated &&
        updated.drag->proposed_sample_frame >= scene_cache.waveform.waveform_overview_total_frames) {
        if (input.kind == WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE) {
            scene_cache.clear_beat_fit_capture();
        }
        return compose_input_family_result_t{
            "Snapped beat target is outside the source audio range."};
    }
    if (!updated) {
        if (input.kind == WORM_UI_COMPOSE_INPUT_BEAT_FIT_RELEASE) {
            scene_cache.clear_beat_fit_capture();
        }
        return input_diagnostic_feedback(updated.diagnostics, "Beat fit target was rejected.",
                                         notification_severity_t::warning,
                                         notification_source_t::edit);
    }
    scene_cache.timing.beat_fit_drag = updated.drag;
    scene_cache.timing.beat_fit_drag->snap_active =
        input.beat_fit_ctrl_held && !input.beat_fit_onset_snap_held;
    if (scene_cache.timing.beat_fit_drag->snap_active) {
        scene_cache.timing.beat_fit_drag->snap_step = {input.beat_fit_snap_step_numerator,
                                                input.beat_fit_snap_step_denominator};
    }
    if (input.kind == WORM_UI_COMPOSE_INPUT_BEAT_FIT_UPDATE) {
        return compose_input_family_result_t::stop_with_feedback(
            "Previewing exact sample-frame beat fit.", notification_severity_t::hint,
            notification_source_t::input);
    }
    const auto committed = commit_beat_interval_drag(history, *scene_cache.timing.beat_fit_drag,
                                                     application_state.project.timing_recipe.timing_locked);
    scene_cache.clear_beat_fit_capture();
    if (!committed.applied) {
        return input_diagnostic_feedback(committed.diagnostics, "Beat fit was unchanged.",
                                         notification_severity_t::info,
                                         notification_source_t::edit);
    }
    const auto rebound = rebind_workspace(workspace, history);
    return compose_input_family_result_t::stop_with_feedback(
        rebound ? "Beat interval fitted to the selected audio frame."
                : "Beat fit applied, but workspace rebinding failed.",
        rebound ? notification_severity_t::success : notification_severity_t::error,
        rebound ? notification_source_t::edit : notification_source_t::document);
}

auto seek_compose_waveform(const WormUiComposeInputAction& input, compose_input_context_t& context,
                           wormedit::editor_interaction_result_t& result,
                           std::string_view& accepted)
    -> std::optional<compose_input_family_result_t> {
    using namespace wormedit;
    const auto& cache = context.environment.controls.cache;
    const auto& state = context.state;
    if (!std::isfinite(input.audio_fraction) || input.audio_fraction < 0.0 ||
        input.audio_fraction > 1.0 || !cache.timing.marker_timing_index ||
        cache.waveform.waveform_overview_total_frames == 0 || cache.waveform.waveform_overview_sample_rate == 0 ||
        !context.input_chart)
        return compose_input_family_result_t{"Waveform overview seek is unavailable."};
    if (editor_interaction_has_pending_gesture(state))
        return compose_input_family_result_t{
            "Finish or cancel the active gesture before navigating the waveform."};
    const auto& timing = *cache.timing.marker_timing_index;
    const auto frame = static_cast<std::int64_t>(
        input.audio_fraction * static_cast<long double>(cache.waveform.waveform_overview_total_frames - 1));
    const auto musical =
        musical_position_at(timing, signed_rational_t{frame, cache.waveform.waveform_overview_sample_rate});
    long double row = 0.0L;
    if (musical && musical.absolute_row && musical.absolute_row->denominator != 0) {
        row = static_cast<long double>(musical.absolute_row->numerator) /
              musical.absolute_row->denominator;
    } else {
        const auto end = timing_at(timing, timing.total_rows);
        if (end && end.seconds) {
            const auto end_seconds =
                static_cast<long double>(end.seconds->numerator) / end.seconds->denominator;
            const auto audio_seconds =
                static_cast<long double>(frame) / cache.waveform.waveform_overview_sample_rate;
            row = audio_seconds >= end_seconds ? static_cast<long double>(timing.total_rows) : 0.0L;
        }
    }
    const auto target = static_cast<double>(row * state.pixels_per_standard_measure /
                                                timing.divisions_per_standard_measure -
                                            state.viewport_height_pixels * 0.5);
    result = scroll_compose_navigation_view(context.environment.document.history, context.state, target);
    accepted = "Compose waveform overview navigated.";
    return std::nullopt;
}

auto click_compose_waveform(const WormUiComposeInputAction& input, compose_input_context_t& context,
                            wormedit::editor_interaction_result_t& result,
                            std::string_view& accepted)
    -> std::optional<compose_input_family_result_t> {
    using namespace wormedit;
    if (input.overview_shift)
        return seek_compose_waveform(input, context, result, accepted);
    auto& cache = context.environment.controls.cache;
    if (!cache.timing.marker_timing_index || !cache.scene.index || !context.input_chart ||
        editor_interaction_has_pending_gesture(context.state))
        return compose_input_family_result_t{
            "Waveform overview seek is unavailable during an active gesture."};
    const auto end =
        context.environment.commands.services.transport_preview_end_frame
            ? context.environment.commands.services.transport_preview_end_frame()
            : std::nullopt;
    const auto target = compose_minimap_target(
        input.audio_fraction, cache.waveform.waveform_overview_total_frames,
        cache.waveform.waveform_overview_sample_rate, audio::output_sample_rate, *cache.timing.marker_timing_index,
        cache.scene.index->measures, edit_snap_denominator(*context.input_chart, context.state), end);
    if (!target)
        return compose_input_family_result_t{"Waveform overview seek is unavailable."};
    const auto position = local_position_for_row(cache.scene.index->measures, target->snapped_row);
    if (!position)
        return compose_input_family_result_t{"Waveform cursor position is unavailable."};
    const auto outcome =
        navigate_compose_position(context.environment.document.history, context.state,
                                  context.environment.commands,
                                  {.position = *position,
                                   .preview_frame = target->frame,
                                   .minimap = true,
                                   .primary = input.overview_primary},
                                  context.environment.document.history.document.revision);
    result.accepted = outcome.accepted;
    result.diagnostics = outcome.diagnostics;
    if (outcome.accepted)
        retain_compose_minimap_start(context.environment.commands,
                                     context.environment.document.history, context.state, *target,
                                     *cache.timing.marker_timing_index);
    accepted = "Compose waveform position moved.";
    auto feedback = compose_input_family_result_t::stop_with_feedback(
        outcome.status,
        outcome.accepted ? notification_severity_t::hint : notification_severity_t::warning,
        notification_source_t::input);
    feedback.notification_posted = outcome.feedback_posted;
    return feedback;
}
} // namespace wormedit::sys::compose_input_detail

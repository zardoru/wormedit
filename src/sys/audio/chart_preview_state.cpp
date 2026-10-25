#include "sys/audio/chart_preview_state.h"
#include "sys/audio/preview_cursor_frame.h"
#include "sys/audio/export_cut_preview.h"
#include "sys/timing/timing_settings.h"

#include <wormedit/audio/audition_resolver.h>
#include <wormedit/audio/decoder.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <unordered_set>
#include <unordered_map>

namespace wormedit::sys::runtime {
void assign_preview_bgm_mix_groups(const chart_document_t& chart,
    std::span<audio::preview_object_source_binding_t> bindings) {
    std::unordered_map<std::uint64_t, std::uint64_t> channels, events, objects;
    for (const auto& lane : chart.bgm_source_lanes)
        if (lane.stem_channel) channels.emplace(lane.stem_channel->value, lane.id.value);
    for (const auto& channel : chart.sound_channels) {
        const auto owner = channels.find(channel.id.value);
        if (owner == channels.end()) continue;
        for (const auto& event : channel.events) events.emplace(event.id.value, owner->second);
    }
    for (const auto& measure : chart.measures) {
        for (const auto& bgm : measure.bgm_events) objects.emplace(bgm.id.value, bgm.source_lane.value);
        for (const auto& note : measure.notes) {
            const auto owner = events.find(note.sound_channel_event.value_or(object_id_t{}).value);
            if (owner != events.end()) objects.emplace(note.id.value, owner->second);
        }
    }
    for (auto& binding : bindings) {
        const auto owner = objects.find(binding.object.value);
        for (auto& voice : binding.voices)
            voice.mix_group = owner == objects.end() ? 0 : owner->second;
    }
}

void chart_preview_t::set_bgm_mix(chart_id_t selected, std::span<const std::uint64_t> muted,
                                  audio::preview_session_t& session) {
    if (bgm_mix_chart == selected && std::ranges::equal(bgm_muted_lanes, muted)) return;
    session.set_muted_mix_groups(muted);
    bgm_mix_chart = selected;
    bgm_muted_lanes.assign(muted.begin(), muted.end());
}

auto chart_preview_t::report(notification_severity_t severity, bool accepted, std::string message)
    -> std::string {
    feedback = {severity, accepted};
    return message;
}

namespace {
auto preview_diagnostic_feedback(chart_preview_t& preview, const auto& diagnostics, std::string_view fallback)
    -> std::string {
    if (diagnostics.empty()) return preview.report(notification_severity_t::error, false, std::string{fallback});
    const auto fatal = std::ranges::find_if(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
    const auto& diagnostic = fatal == diagnostics.end() ? diagnostics.front() : *fatal;
    const auto severity = diagnostic.severity;
    return preview.report(to_notification_severity(severity, notification_severity_t::info), false,
                          diagnostic.message);
}

using audio::preview_music_object;

auto registered_music_asset(const song_document_t& document, const chart_document_t& chart,
                            project_audio_sources_t& sources)
    -> std::optional<std::pair<resource_id_t, asset_id_t>> {
    const auto selected = audio::preview_music_selection_t::from(document, chart);
    if (!selected.ok() || !selected.value || !sources.binding(document, selected.value->asset)) return {};
    return std::pair{selected.value->resource, selected.value->asset};
}

auto preview_end_frame(
    const preview_schedule_t& schedule,
    const std::vector<audio::preview_object_source_binding_t>& bindings,
    std::span<const audio::preview_metronome_source_binding_t> metronome_bindings)
    -> std::uint64_t {
    std::uint64_t end_frame{};
    const auto include_event_ends = [&](const auto& events) {
        for (const auto& event : events) {
            const auto binding = std::ranges::lower_bound(
                bindings, event.object, {}, &audio::preview_object_source_binding_t::object);
            if (binding == bindings.end() || binding->object != event.object) {
                continue;
            }
            for (const auto& voice : binding->voices) {
                if (voice.frame_count <=
                    std::numeric_limits<std::uint64_t>::max() - event.sample_frame) {
                    end_frame = std::max(end_frame, event.sample_frame + voice.frame_count);
                }
            }
        }
    };
    include_event_ends(schedule.playable);
    include_event_ends(schedule.bgm);
    for (const auto& event : schedule.metronome) {
        const auto binding = std::ranges::find(metronome_bindings, event.accent,
                                               &audio::preview_metronome_source_binding_t::accent);
        if (binding != metronome_bindings.end() &&
            binding->voice.frame_count <=
                std::numeric_limits<std::uint64_t>::max() - event.sample_frame) {
            end_frame = std::max(end_frame, event.sample_frame + binding->voice.frame_count);
        }
    }
    for (const auto& music : schedule.music_events())
        if (music.frame_count <= std::numeric_limits<std::uint64_t>::max() - music.sample_frame)
            end_frame = std::max(end_frame, music.sample_frame + music.frame_count);
    return end_frame;
}

auto chart_visual_end_frame(const timing_index_t& timing,
                            const preview_schedule_t& schedule)
    -> std::optional<std::uint64_t> {
    std::uint64_t end_frame = audio::output_sample_rate;
    const auto chart_end = timing.query_seconds(timing.total_rows);
    if (!chart_end)
        return std::nullopt;
    if (chart_end->value.numerator >= 0) {
        const auto frame = sample_frame_t::from(chart_end->value, audio::output_sample_rate);
        if (!frame)
            return std::nullopt;
        end_frame = std::max(end_frame, frame->value.frame);
    }
    for (const auto& event : schedule.bga)
        end_frame = std::max(end_frame, event.sample_frame);
    for (const auto& event : schedule.metronome)
        end_frame = std::max(end_frame, event.sample_frame);
    return end_frame;
}

} // namespace

bool chart_preview_t::preserve_bookmark_revision(chart_id_t chart_id,
                                                 std::uint64_t previous_revision,
                                                 std::uint64_t next_revision) noexcept {
    if (!compiled.value().schedule || !compiled.value().timing || chart() != chart_id || revision() != previous_revision ||
        previous_revision == std::numeric_limits<std::uint64_t>::max() ||
        next_revision != previous_revision + 1)
        return false;
    auto key = *compiled.key();
    std::get<1>(key) = next_revision;
    return compiled.rebind(key);
}

bool chart_preview_t::matches_selection(
    const song_document_t& document, const workspace_controller_t& workspace) const {
    return chart() && revision() == document.revision && workspace.selected_chart &&
           *workspace.selected_chart == *chart();
}

auto chart_preview_t::layer_assist_frames() const -> std::vector<std::uint64_t> {
    std::vector<std::uint64_t> frames;
    if (!compiled.value().schedule || chart() != layer_assist_chart || layer_assist_layers.empty()) return frames;
    std::unordered_set<std::uint64_t> selected;
    for (const auto layer : layer_assist_layers) selected.insert(layer.value);
    for (const auto& tick : compiled.value().schedule->layer_ticks)
        if (std::ranges::any_of(tick.layers, [&](layer_id_t layer) {
                return selected.contains(layer.value);
            })) frames.push_back(tick.sample_frame);
    std::ranges::sort(frames);
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    return frames;
}

auto chart_preview_t::set_layer_assist(chart_id_t selected, std::span<const layer_id_t> layers,
                                      audio::preview_session_t& session) -> bool {
    if (layer_assist_chart == selected && std::ranges::equal(layers, layer_assist_layers)) return true;
    const auto previous_chart = layer_assist_chart;
    auto previous_layers = layer_assist_layers;
    layer_assist_chart = selected;
    layer_assist_layers.assign(layers.begin(), layers.end());
    if (session.set_layer_tick_frames(layer_assist_frames())) return true;
    layer_assist_chart = previous_chart;
    layer_assist_layers = std::move(previous_layers);
    return false;
}

auto chart_preview_t::compile_if_needed(
    const chart_document_t& selected_chart, const song_document_t& document,
    project_audio_sources_t& sources)
    -> std::optional<std::string> {
    sources.ensure_current(document);
    metronome = sources.metronome;
    const auto metronome_bindings = sources.metronome_bindings();
    sources.registry().await_pending();
    const auto selected_music = registered_music_asset(document, selected_chart, sources);
    const auto selected_music_asset = selected_music
                                          ? std::optional<asset_id_t>{selected_music->second}
                                          : std::nullopt;
    const source_key_t key{selected_chart.id, document.revision, selected_music_asset, count_in_measures(), timing_focus && !document.export_cuts.empty()};
    const auto retained_resume = compiled.value().schedule && chart() == selected_chart.id &&
        revision() == document.revision && music_asset() == selected_music_asset ? resume_frame : 0;
    const compilation_request_t request{selected_chart, document, sources, metronome_bindings, selected_music};
    std::optional<std::string> error;
    const auto builds = compiled.build_count();
    const auto& data = compiled.get_or_build(key, [this, &request, &error](compiled_t& value) {
        error = build_compiled(request, value);
        return !error;
    }, [](const compiled_t& value) { return value.schedule.has_value(); });
    if (!error && compiled.build_count() != builds)
        resume_frame = std::min(retained_resume, data.end_frame);
    return error;
}

auto chart_preview_t::build_compiled(const compilation_request_t& request, compiled_t& data)
    -> std::optional<std::string> {
    const auto& selected_chart = request.chart;
    const auto& document = request.document;
    auto& sources = request.sources;
    const auto metronome_bindings = request.metronome_bindings;
    const auto& selected_music = request.selected_music;
    auto built_timing = timing_index_t::from(selected_chart, {.allow_bounded_clock = true});
    if (!built_timing.value)
        return preview_diagnostic_feedback(*this, built_timing.diagnostics, "Chart timing could not be compiled.");
    const auto count_in = timing_count_in_pulses(selected_chart, count_in_measures());
    if (!count_in)
        return report(notification_severity_t::warning, false,
                      "Count-in exceeds the first meter's pulse limits.");
    auto built_schedule = preview_schedule_t::from(
        selected_chart, *built_timing.value,
        {.sample_rate = audio::output_sample_rate,
         .include_metronome = true, .count_in_pulses = *count_in, .include_layer_ticks = true});
    if (!built_schedule.value)
        return preview_diagnostic_feedback(*this, built_schedule.diagnostics, "Chart preview could not be compiled.");
    auto resolved = sources.resolve_preview(document, selected_chart, *built_schedule.value);
    if (!resolved.diagnostics.empty())
        return preview_diagnostic_feedback(*this, resolved.diagnostics, "Chart audio could not be resolved.");

    if (selected_music) {
        const auto source = sources.binding(document, selected_music->second);
        if (source && source->output_frame_count != 0) {
            resolved.value.append_music(selected_music->first, *source, *built_schedule.value);
        }
    }

    const auto visual_end_frame =
        chart_visual_end_frame(*built_timing.value, *built_schedule.value);
    if (!visual_end_frame)
        return report(notification_severity_t::error, false, "Chart visual endpoint exceeds the preview clock.");
    const auto compiled_end_frame = std::max(
        preview_end_frame(*built_schedule.value, resolved.value.bindings, metronome_bindings),
        *visual_end_frame);

    data.schedule = std::move(*built_schedule.value);
    data.timing = std::move(*built_timing.value);
    data.bindings = std::move(resolved.value.bindings);
    assign_preview_bgm_mix_groups(selected_chart, data.bindings);
    data.retained_metronome_bindings.assign(metronome_bindings.begin(), metronome_bindings.end());
    data.cut_frames.reset();
    data.cut_warning.clear();
    if (!timing_focus) {
        auto cuts = prepare_export_cut_preview(*data.schedule, data.bindings, document.export_cuts, compiled_end_frame);
        if (!cuts.diagnostics.empty()) {
            if (std::ranges::any_of(cuts.diagnostics, [](const auto& value) { return value.severity == diagnostic_severity_t::error; }))
                return preview_diagnostic_feedback(*this, cuts.diagnostics, "Export cuts could not be prepared.");
            data.cut_warning = cuts.diagnostics.front().message;
        }
        data.cut_frames = std::move(cuts.value);
    }
    data.end_frame = data.cut_frames ? data.cut_frames->collapsed_frame(compiled_end_frame) : compiled_end_frame;
    data.skipped_unavailable_audio_objects = resolved.value.skipped_unavailable_objects;
    return std::nullopt;
}

bool chart_preview_t::playing(const audio::preview_session_t& session) const {
    return output_observer ? output_observer->playing() : session.is_active();
}

std::uint64_t chart_preview_t::current_frame(
    const audio::preview_session_t& session) const {
    if (output_observer && output_observer->playing()) return output_observer->timeline_frame();
    return playing(session) ? session.timeline_frame() : resume_frame;
}

void chart_preview_t::update_presentation_frame(std::uint64_t expected_presentation_ns) {
    presentation_frame.reset();
    if (!output_observer || expected_presentation_ns == 0) {
        presentation_clock.reset();
        return;
    }
    presentation_report = output_observer->playhead();
    if (!presentation_report.active) {
        presentation_clock.reset();
        return;
    }
    presentation_frame = presentation_clock.frame(presentation_report, expected_presentation_ns,
                                                  audio::output_sample_rate);
}

auto chart_preview_t::display_frame(const audio::preview_session_t& session) const -> std::uint64_t {
    auto frame = current_frame(session);
    if (!playing(session)) return frame;
    if (presentation_frame && output_observer) {
        const auto report = output_observer->playhead();
        if (report.active && report.generation == presentation_report.generation &&
            (report.transport_generation == presentation_report.transport_generation ||
             report.loop_end_frame > report.start_frame) &&
            report.start_frame == presentation_report.start_frame &&
            report.rate_index == presentation_report.rate_index &&
            report.loop_end_frame == presentation_report.loop_end_frame)
            frame = *presentation_frame;
    }
    const auto delta = static_cast<std::int64_t>(display_latency_ms) * audio::output_sample_rate / 1000;
    if (delta < 0) return frame - std::min(frame, static_cast<std::uint64_t>(-delta));
    const auto advance = static_cast<std::uint64_t>(delta);
    return frame + std::min(advance, std::numeric_limits<std::uint64_t>::max() - frame);
}

namespace {

auto frame_to_milliseconds(std::uint64_t frame, std::uint32_t sample_rate)
    -> std::optional<std::int64_t> {
    if (sample_rate == 0)
        return std::nullopt;
    const auto whole_seconds = frame / sample_rate;
    const auto remaining_frames = frame % sample_rate;
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (whole_seconds > maximum / 1'000)
        return std::nullopt;
    const auto milliseconds = whole_seconds * 1'000 + remaining_frames * 1'000 / sample_rate;
    if (milliseconds > maximum)
        return std::nullopt;
    return static_cast<std::int64_t>(milliseconds);
}

auto milliseconds_to_frame(std::int64_t milliseconds, std::uint32_t sample_rate)
    -> std::uint64_t {
    if (milliseconds <= 0 || sample_rate == 0)
        return 0;
    const auto unsigned_milliseconds = static_cast<std::uint64_t>(milliseconds);
    const auto whole_seconds = unsigned_milliseconds / 1'000;
    const auto remainder = unsigned_milliseconds % 1'000;
    if (whole_seconds > std::numeric_limits<std::uint64_t>::max() / sample_rate)
        return std::numeric_limits<std::uint64_t>::max();
    const auto base = whole_seconds * sample_rate;
    const auto fractional_frames = (remainder * sample_rate + 999) / 1'000;
    if (fractional_frames > std::numeric_limits<std::uint64_t>::max() - base)
        return std::numeric_limits<std::uint64_t>::max();
    return base + fractional_frames;
}


} // namespace

namespace {
auto preview_started_feedback(chart_preview_t& preview, bool from_beginning, bool was_paused, bool at_cursor)
    -> std::string {
    const auto& data = preview.compiled.value();
    std::string status = from_beginning ? "Chart preview started from the beginning."
           : was_paused ? "Chart preview resumed."
           : at_cursor ? "Chart preview started at the cursor."
                          : "Chart preview started.";
    if (data.skipped_unavailable_audio_objects != 0) {
        status += " Audio unavailable for " +
                  std::to_string(data.skipped_unavailable_audio_objects) +
                  " chart object(s); preview continues silently for them.";
    }
    if (!data.cut_warning.empty()) status += " " + data.cut_warning;
    return preview.report((data.skipped_unavailable_audio_objects || !data.cut_warning.empty()) ? notification_severity_t::warning
                                                  : notification_severity_t::success, true, std::move(status));
}
} // namespace

auto chart_preview_t::play_pause(
    const chart_document_t* selected_chart, const song_document_t& document,
    project_audio_sources_t& sources,
    sdl_audition_output_t& audition_output, audio::preview_session_t& preview_session,
    sdl_preview_output_t& preview_output, std::optional<local_position_t> start_cursor,
    bool from_beginning, std::optional<std::uint64_t> exact_start_frame,
    double start_cursor_subrow, const result_t<measure_index_t, diagnostic_list_t>* geometry) -> std::string {
    output_observer = &preview_output;
    if (preview_output.playing() && !from_beginning) {
        const auto frame = preview_output.timeline_frame();
        static_cast<void>(preview_output.stop());
        pause_at(frame);
        return report(notification_severity_t::success, true, "Chart preview paused.");
    }
    if (selected_chart == nullptr)
        return report(notification_severity_t::warning, false, "Select a chart before starting preview.");

    if (auto error = compile_if_needed(*selected_chart, document, sources))
        return *error;

    const auto was_paused = paused;
    if (from_beginning) {
        if (preview_output.playing()) static_cast<void>(preview_output.stop());
        resume_frame = 0;
        paused = false;
    } else if (!paused && exact_start_frame) {
        resume_frame = std::min(collapsed_frame(*exact_start_frame), compiled.value().end_frame);
    } else if (!paused && start_cursor) {
        const auto frame = wormedit::sys::preview_cursor_frame(*selected_chart, *compiled.value().timing,
                                                              *start_cursor, start_cursor_subrow, geometry);
        if (const auto* error = std::get_if<std::string>(&frame))
            return report(notification_severity_t::warning, false, *error);
        resume_frame = collapsed_frame(std::get<std::uint64_t>(frame));
    }

    static_cast<void>(audition_output.stop());
    (void)preview_session.set_layer_tick_frames(layer_assist_frames());
    const auto started = preview_output.start(*compiled.value().schedule, compiled.value().bindings, sources.metronome_bindings(),
                                              resume_frame, compiled.value().end_frame);
    if (!started)
        return report(notification_severity_t::error, false,
                      started.message().empty() ? "Chart preview could not start." : std::string{started.message()});
    paused = false;
    return preview_started_feedback(*this, from_beginning, was_paused, start_cursor.has_value());
}

namespace {
auto restore_preview_after_seek_failure(const chart_preview_t& preview,
    std::uint64_t previous_frame, audio::preview_session_t& session,
    sdl_preview_output_t& output, std::string failure) -> std::string {
    if (session.is_active()) return failure;
    const auto restored = output.start(*preview.compiled.value().schedule, preview.compiled.value().bindings,
        preview.compiled.value().retained_metronome_bindings, previous_frame, preview.compiled.value().end_frame);
    if (restored) return failure + " Playback continues from its previous position.";
    return failure + " Playback could not be restored: " +
        (restored.message().empty() ? "audio output is unavailable." : std::string{restored.message()});
}
} // namespace

auto chart_preview_t::seek(std::uint64_t frame, const song_document_t& document,
                           const workspace_controller_t& workspace,
                           sdl_audition_output_t& audition_output,
                           audio::preview_session_t& preview_session,
                           sdl_preview_output_t& preview_output) -> std::string {
    if (!compiled.value().schedule || !matches_selection(document, workspace))
        return report(notification_severity_t::warning, false,
                      "Chart preview seek is unavailable or outside the compiled timeline.");
    frame = std::min(frame, compiled.value().end_frame);
    static_cast<void>(audition_output.stop());
    output_observer = &preview_output;
    if (preview_output.playing()) {
        const auto previous_frame = preview_output.timeline_frame();
        const auto sought = preview_output.seek(frame);
        if (!sought)
            return report(notification_severity_t::error, false,
                restore_preview_after_seek_failure(*this, previous_frame, preview_session, preview_output,
                    sought.message().empty() ? "Chart preview seek failed." : std::string{sought.message()}));
        paused = false;
    } else {
        paused = true;
    }
    resume_frame = frame;
    return report(notification_severity_t::hint, true, "Chart preview moved to " +
                  std::to_string(frame / audio::output_sample_rate) + " seconds.");
}

auto chart_preview_t::audio_milliseconds(
    const song_document_t& document, const workspace_controller_t& workspace,
    const audio::preview_session_t& session) const -> std::optional<std::int64_t> {
    if (!compiled.value().schedule || !matches_selection(document, workspace))
        return std::nullopt;
    return frame_to_milliseconds(source_frame(current_frame(session)), audio::output_sample_rate);
}

auto chart_preview_t::seek_audio_milliseconds(
    std::int64_t milliseconds, const song_document_t& document,
    const workspace_controller_t& workspace, sdl_audition_output_t& audition_output,
    audio::preview_session_t& preview_session, sdl_preview_output_t& preview_output)
    -> std::string {
    if (!audio_milliseconds(document, workspace, preview_session))
        return report(notification_severity_t::warning, false,
                      "Chart preview seek is unavailable for the selected chart.");
    const auto requested_source = milliseconds_to_frame(milliseconds, audio::output_sample_rate);
    const auto unclamped = collapsed_frame(requested_source);
    const auto frame = std::min(unclamped, compiled.value().end_frame);
    const auto seek_status = seek(frame, document, workspace, audition_output, preview_session,
                                  preview_output);
    if (!feedback.accepted)
        return seek_status;
    if (milliseconds < 0)
        return report(notification_severity_t::warning, true, "Audio marker precedes the source; chart preview clamped to 0 ms.");
    if (requested_source > source_frame(compiled.value().end_frame))
        return report(notification_severity_t::warning, true, "Audio marker follows the compiled timeline; chart preview clamped to its end.");
    return report(notification_severity_t::hint, true,
                  "Chart preview moved to the audio marker at " + std::to_string(milliseconds) + " ms.");
}


auto chart_preview_t::source_frame(std::uint64_t frame) const -> std::uint64_t {
    return compiled.value().cut_frames ? compiled.value().cut_frames->source_frame(frame) : frame;
}

auto chart_preview_t::collapsed_frame(std::uint64_t frame) const -> std::uint64_t {
    return compiled.value().cut_frames ? compiled.value().cut_frames->collapsed_frame(frame) : frame;
}

auto chart_preview_t::synchronize_timing_focus(bool focus, const chart_document_t* selected,
    const song_document_t& document, project_audio_sources_t& sources,
    sdl_audition_output_t& audition, audio::preview_session_t& session, sdl_preview_output_t& output)
    -> std::optional<std::string> {
    if (timing_focus == focus) return std::nullopt;
    timing_focus = focus;
    if (document.export_cuts.empty() || !selected || !compiled.value().schedule) return std::nullopt;
    const auto source = source_frame(current_frame(session));
    const bool active = playing(session);
    const bool retained_pause = paused;
    auto loop = session.loop_region();
    if (loop) {
        loop->begin_frame = source_frame(loop->begin_frame);
        loop->end_frame = source_frame(loop->end_frame);
    }
    if (active) static_cast<void>(output.stop());
    if (auto error = compile_if_needed(*selected, document, sources)) return error;
    resume_frame = std::min(collapsed_frame(source), compiled.value().end_frame);
    paused = retained_pause;
    if (loop) {
        loop->begin_frame = collapsed_frame(loop->begin_frame);
        loop->end_frame = collapsed_frame(loop->end_frame);
        if (loop->begin_frame >= loop->end_frame) loop.reset();
        static_cast<void>(output.set_loop_region(loop));
    }
    if (!active) return std::nullopt;
    return play_pause(selected, document, sources, audition, session, output, std::nullopt, false, source);
}

void chart_preview_t::stop(sdl_audition_output_t& audition_output,
                           sdl_preview_output_t& preview_output) {
    static_cast<void>(audition_output.stop());
    static_cast<void>(preview_output.stop());
    reset_transport();
}

void chart_preview_t::pause_at(std::uint64_t frame) {
    resume_frame = frame;
    paused = true;
}

void chart_preview_t::reset_transport() {
    resume_frame = 0;
    paused = false;
}

void chart_preview_t::invalidate() {
    compiled.invalidate([](compiled_t& data) {
        data.schedule.reset();
        data.timing.reset();
        data.bindings.clear();
        data.retained_metronome_bindings.clear();
        data.cut_frames.reset();
        data.cut_warning.clear();
        data.end_frame = 0;
        data.skipped_unavailable_audio_objects = 0;
    });
    resume_frame = 0;
    paused = false;
}

} // namespace wormedit::sys::runtime

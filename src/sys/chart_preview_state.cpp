#include "sys/chart_preview_state.h"
#include "sys/preview_cursor_frame.h"
#include "sys/timing_settings.h"

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

constexpr object_id_t preview_music_object{std::numeric_limits<std::uint64_t>::max()};

auto registered_music_asset(const song_document_t& document,
                            project_audio_sources_t& sources)
    -> std::optional<std::pair<resource_id_t, asset_id_t>> {
    const auto music = std::ranges::find_if(document.resources, [&](const auto& resource) {
        return std::ranges::find(resource.roles, resource_role_t::music) != resource.roles.end() &&
               sources.binding(document, resource.asset).has_value();
    });
    if (music == document.resources.end()) return std::nullopt;
    return std::pair{music->id, music->asset};
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
    if (schedule.music && schedule.music->frame_count <=
                              std::numeric_limits<std::uint64_t>::max() -
                                  schedule.music->sample_frame)
        end_frame = std::max(end_frame,
                             schedule.music->sample_frame + schedule.music->frame_count);
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

bool chart_preview_t::reusable(chart_id_t chart_id, std::uint64_t document_revision,
                               std::optional<asset_id_t> selected_music_asset) const {
    return schedule && chart == chart_id && revision == document_revision &&
           music_asset == selected_music_asset && count_in_measures() == compiled_count_in_measures;
}

bool chart_preview_t::preserve_bookmark_revision(chart_id_t chart_id,
                                                 std::uint64_t previous_revision,
                                                 std::uint64_t next_revision) noexcept {
    if (!schedule || !timing || chart != chart_id || revision != previous_revision ||
        previous_revision == std::numeric_limits<std::uint64_t>::max() ||
        next_revision != previous_revision + 1)
        return false;
    revision = next_revision;
    return true;
}

bool chart_preview_t::matches_selection(
    const song_document_t& document, const workspace_controller_t& workspace) const {
    return chart && revision == document.revision && workspace.selected_chart &&
           *workspace.selected_chart == *chart;
}

auto chart_preview_t::layer_assist_frames() const -> std::vector<std::uint64_t> {
    std::vector<std::uint64_t> frames;
    if (!schedule || chart != layer_assist_chart || layer_assist_layers.empty()) return frames;
    std::unordered_set<std::uint64_t> selected;
    for (const auto layer : layer_assist_layers) selected.insert(layer.value);
    for (const auto& tick : schedule->layer_ticks)
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
    const auto selected_music = registered_music_asset(document, sources);
    const auto selected_music_asset = selected_music
                                          ? std::optional<asset_id_t>{selected_music->second}
                                          : std::nullopt;
    if (reusable(selected_chart.id, document.revision, selected_music_asset))
        return std::nullopt;
    const auto retained_resume = schedule && chart == selected_chart.id &&
        revision == document.revision && music_asset == selected_music_asset
        ? resume_frame : 0;

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
    auto resolved = audio::preview_bindings_resolution_t::from(
        document, selected_chart.id, *built_schedule.value, sources.bindings(document),
        {.skip_unavailable_preview_sources = true});
    if (!resolved.diagnostics.empty())
        return preview_diagnostic_feedback(*this, resolved.diagnostics, "Chart audio could not be resolved.");

    if (selected_music) {
        const auto source = sources.binding(document, selected_music->second);
        if (source && source->output_frame_count != 0) {
            built_schedule.value->music = preview_schedule_t::music_event_t::from(
                selected_music->first, source->output_frame_count);
            if (built_schedule.value->music) {
                resolved.value.bindings.push_back({
                    .object = preview_music_object,
                    .voices = {{.source = source->source,
                                .source_start_frame = 0,
                                .frame_count = source->output_frame_count,
                                .gain = 1.0F}}});
            }
        }
    }

    const auto visual_end_frame =
        chart_visual_end_frame(*built_timing.value, *built_schedule.value);
    if (!visual_end_frame)
        return report(notification_severity_t::error, false, "Chart visual endpoint exceeds the preview clock.");
    const auto compiled_end_frame = std::max(
        preview_end_frame(*built_schedule.value, resolved.value.bindings, metronome_bindings),
        *visual_end_frame);

    schedule = std::move(*built_schedule.value);
    timing = std::move(*built_timing.value);
    bindings = std::move(resolved.value.bindings);
    assign_preview_bgm_mix_groups(selected_chart, bindings);
    retained_metronome_bindings.assign(metronome_bindings.begin(), metronome_bindings.end());
    chart = selected_chart.id;
    revision = document.revision;
    compiled_count_in_measures = count_in_measures();
    music_asset = selected_music_asset;
    resume_frame = std::min(retained_resume, compiled_end_frame);
    end_frame = compiled_end_frame;
    skipped_unavailable_audio_objects = resolved.value.skipped_unavailable_objects;
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

auto chart_preview_t::play_pause(
    const chart_document_t* selected_chart, const song_document_t& document,
    project_audio_sources_t& sources,
    sdl_audition_output_t& audition_output, audio::preview_session_t& preview_session,
    sdl_preview_output_t& preview_output, std::optional<local_position_t> start_cursor,
    bool from_beginning, std::optional<std::uint64_t> exact_start_frame,
    double start_cursor_subrow) -> std::string {
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
        resume_frame = std::min(*exact_start_frame, end_frame);
    } else if (!paused && start_cursor) {
        const auto frame = wormedit::sys::preview_cursor_frame(*selected_chart, *timing,
                                                              *start_cursor, start_cursor_subrow);
        if (const auto* error = std::get_if<std::string>(&frame))
            return report(notification_severity_t::warning, false, *error);
        resume_frame = std::get<std::uint64_t>(frame);
    }

    static_cast<void>(audition_output.stop());
    (void)preview_session.set_layer_tick_frames(layer_assist_frames());
    const auto started = preview_output.start(*schedule, bindings, sources.metronome_bindings(),
                                              resume_frame, end_frame);
    if (!started)
        return report(notification_severity_t::error, false,
                      started.error().message().empty() ? "Chart preview could not start." : std::string{started.error().message()});
    paused = false;
    std::string status = from_beginning ? "Chart preview started from the beginning."
           : was_paused ? "Chart preview resumed."
           : start_cursor ? "Chart preview started at the cursor."
                          : "Chart preview started.";
    if (skipped_unavailable_audio_objects != 0) {
        status += " Audio unavailable for " +
                  std::to_string(skipped_unavailable_audio_objects) +
                  " chart object(s); preview continues silently for them.";
    }
    return report(skipped_unavailable_audio_objects ? notification_severity_t::warning
                                                  : notification_severity_t::success, true, std::move(status));
}

namespace {
auto restore_preview_after_seek_failure(const chart_preview_t& preview,
    std::uint64_t previous_frame, audio::preview_session_t& session,
    sdl_preview_output_t& output, std::string failure) -> std::string {
    if (session.is_active()) return failure;
    const auto restored = output.start(*preview.schedule, preview.bindings,
        preview.retained_metronome_bindings, previous_frame, preview.end_frame);
    if (restored) return failure + " Playback continues from its previous position.";
    return failure + " Playback could not be restored: " +
        (restored.error().message().empty() ? "audio output is unavailable." : std::string{restored.error().message()});
}
} // namespace

auto chart_preview_t::seek(std::uint64_t frame, const song_document_t& document,
                           const workspace_controller_t& workspace,
                           sdl_audition_output_t& audition_output,
                           audio::preview_session_t& preview_session,
                           sdl_preview_output_t& preview_output) -> std::string {
    if (!schedule || !matches_selection(document, workspace))
        return report(notification_severity_t::warning, false,
                      "Chart preview seek is unavailable or outside the compiled timeline.");
    frame = std::min(frame, end_frame);
    static_cast<void>(audition_output.stop());
    output_observer = &preview_output;
    if (preview_output.playing()) {
        const auto previous_frame = preview_output.timeline_frame();
        const auto sought = preview_output.seek(frame);
        if (!sought)
            return report(notification_severity_t::error, false,
                restore_preview_after_seek_failure(*this, previous_frame, preview_session, preview_output,
                    sought.error().message().empty() ? "Chart preview seek failed." : std::string{sought.error().message()}));
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
    if (!schedule || !matches_selection(document, workspace))
        return std::nullopt;
    return frame_to_milliseconds(current_frame(session), audio::output_sample_rate);
}

auto chart_preview_t::seek_audio_milliseconds(
    std::int64_t milliseconds, const song_document_t& document,
    const workspace_controller_t& workspace, sdl_audition_output_t& audition_output,
    audio::preview_session_t& preview_session, sdl_preview_output_t& preview_output)
    -> std::string {
    if (!audio_milliseconds(document, workspace, preview_session))
        return report(notification_severity_t::warning, false,
                      "Chart preview seek is unavailable for the selected chart.");
    const auto unclamped = milliseconds_to_frame(milliseconds, audio::output_sample_rate);
    const auto frame = std::min(unclamped, end_frame);
    const auto seek_status = seek(frame, document, workspace, audition_output, preview_session,
                                  preview_output);
    if (!feedback.accepted)
        return seek_status;
    if (milliseconds < 0)
        return report(notification_severity_t::warning, true, "Audio marker precedes the source; chart preview clamped to 0 ms.");
    if (unclamped > end_frame)
        return report(notification_severity_t::warning, true, "Audio marker follows the compiled timeline; chart preview clamped to its end.");
    return report(notification_severity_t::hint, true,
                  "Chart preview moved to the audio marker at " + std::to_string(milliseconds) + " ms.");
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
    schedule.reset();
    timing.reset();
    bindings.clear();
    retained_metronome_bindings.clear();
    chart.reset();
    revision = 0;
    music_asset.reset();
    resume_frame = 0;
    end_frame = 0;
    skipped_unavailable_audio_objects = 0;
    paused = false;
}

} // namespace wormedit::sys::runtime

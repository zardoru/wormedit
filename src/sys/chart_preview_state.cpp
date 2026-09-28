#include "sys/chart_preview_state.h"

#include <wormedit/audio/audition_resolver.h>
#include <wormedit/audio/decoder.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <limits>

namespace wormedit::sys::runtime {

bool chart_preview_t::reusable(chart_id_t chart_id,
                                     std::uint64_t document_revision) const {
    return schedule && chart == chart_id && revision == document_revision;
}

bool chart_preview_t::matches_selection(
    const song_document_t& document, const workspace_controller_t& workspace) const {
    return chart && revision == document.revision && workspace.selected_chart &&
           *workspace.selected_chart == *chart;
}

auto chart_preview_t::compile_if_needed(
    const chart_document_t& selected_chart, const song_document_t& document,
    const audio::decoded_source_registry_t& sources,
    const std::vector<audio::preview_metronome_source_binding_t>& metronome_bindings)
    -> std::optional<std::string> {
    if (reusable(selected_chart.id, document.revision))
        return std::nullopt;

    auto built_timing = build_timing_index(selected_chart);
    if (!built_timing)
        return built_timing.diagnostics.empty() ? "Chart timing could not be compiled."
                                                : built_timing.diagnostics.front().message;
    auto built_schedule = build_preview_schedule(
        selected_chart, *built_timing.index,
        {.sample_rate = audio::output_sample_rate,
         .include_metronome = !metronome_bindings.empty()});
    if (!built_schedule)
        return built_schedule.diagnostics.empty() ? "Chart preview could not be compiled."
                                                  : built_schedule.diagnostics.front().message;
    auto resolved = audio::resolve_preview_bindings(
        document, selected_chart.id, *built_schedule.schedule, sources.bindings());
    if (!resolved)
        return resolved.diagnostics.empty() ? "Chart audio could not be resolved."
                                            : resolved.diagnostics.front().message;

    std::uint64_t compiled_end_frame{};
    const auto include_event_ends = [&](const auto& events) {
        for (const auto& event : events) {
            const auto binding = std::ranges::lower_bound(
                resolved.bindings, event.object, {},
                &audio::preview_object_source_binding_t::object);
            if (binding == resolved.bindings.end() || binding->object != event.object)
                continue;
            for (const auto& voice : binding->voices) {
                if (voice.frame_count <=
                    std::numeric_limits<std::uint64_t>::max() - event.sample_frame) {
                    compiled_end_frame =
                        std::max(compiled_end_frame, event.sample_frame + voice.frame_count);
                }
            }
        }
    };
    include_event_ends(built_schedule.schedule->playable);
    include_event_ends(built_schedule.schedule->bgm);
    for (const auto& event : built_schedule.schedule->metronome) {
        const auto binding = std::ranges::find(metronome_bindings, event.accent,
                                                &audio::preview_metronome_source_binding_t::accent);
        if (binding != metronome_bindings.end() &&
            binding->voice.frame_count <=
                std::numeric_limits<std::uint64_t>::max() - event.sample_frame) {
            compiled_end_frame =
                std::max(compiled_end_frame, event.sample_frame + binding->voice.frame_count);
        }
    }

    schedule = std::move(*built_schedule.schedule);
    timing = std::move(*built_timing.index);
    bindings = std::move(resolved.bindings);
    chart = selected_chart.id;
    revision = document.revision;
    resume_frame = 0;
    end_frame = compiled_end_frame;
    return std::nullopt;
}

std::uint64_t chart_preview_t::current_frame(
    const audio::preview_session_t& session) const {
    return session.active() ? session.timeline_frame() : resume_frame;
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
    const audio::decoded_source_registry_t& sources,
    const std::vector<audio::preview_metronome_source_binding_t>& metronome_bindings,
    sdl_audition_output_t& audition_output, audio::preview_session_t& preview_session,
    sdl_preview_output_t& preview_output) -> std::string {
    if (preview_session.active()) {
        const auto frame = preview_session.timeline_frame();
        static_cast<void>(preview_output.stop());
        pause_at(frame);
        return "Chart preview paused.";
    }
    if (selected_chart == nullptr)
        return "Select a chart before starting preview.";

    if (auto error = compile_if_needed(*selected_chart, document, sources, metronome_bindings))
        return *error;

    static_cast<void>(audition_output.stop());
    const auto started = preview_output.start(*schedule, bindings, metronome_bindings, resume_frame);
    if (!started)
        return started.message.empty() ? "Chart preview could not start." : started.message;
    paused = false;
    return resume_frame == 0 ? "Chart preview started." : "Chart preview resumed.";
}

auto chart_preview_t::seek(std::uint64_t frame, const song_document_t& document,
                           const workspace_controller_t& workspace,
                           sdl_audition_output_t& audition_output,
                           audio::preview_session_t& preview_session,
                           sdl_preview_output_t& preview_output) -> std::string {
    if (!schedule || !matches_selection(document, workspace) || frame > end_frame)
        return "Chart preview seek is unavailable or outside the compiled timeline.";
    static_cast<void>(audition_output.stop());
    if (preview_session.active()) {
        const auto sought = preview_output.seek(frame);
        if (!sought)
            return sought.message.empty() ? "Chart preview seek failed." : sought.message;
        paused = false;
    } else {
        paused = true;
    }
    resume_frame = frame;
    return "Chart preview moved to " +
           std::to_string(frame / audio::output_sample_rate) + " seconds.";
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
        return "Chart preview seek is unavailable for the selected chart.";
    const auto unclamped = milliseconds_to_frame(milliseconds, audio::output_sample_rate);
    const auto frame = std::min(unclamped, end_frame);
    const auto seek_status = seek(frame, document, workspace, audition_output, preview_session,
                                  preview_output);
    if (!seek_status.starts_with("Chart preview moved"))
        return seek_status;
    if (milliseconds < 0)
        return "Audio marker precedes the source; chart preview clamped to 0 ms.";
    if (unclamped > end_frame)
        return "Audio marker follows the compiled timeline; chart preview clamped to its end.";
    return "Chart preview moved to the audio marker at " + std::to_string(milliseconds) + " ms.";
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
    chart.reset();
    revision = 0;
    resume_frame = 0;
    end_frame = 0;
    paused = false;
}

} // namespace wormedit::sys::runtime

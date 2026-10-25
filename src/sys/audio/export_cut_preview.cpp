#include "sys/audio/export_cut_preview.h"

#include <algorithm>
#include <limits>
#include <map>

namespace wormedit::sys {
namespace {

inline constexpr std::size_t maximum_cut_preview_voices = 200'000;

struct cut_preview_builder_t {
    const export_cut_frame_timeline_t& frames;
    const std::vector<audio::preview_object_source_binding_t>& sources;
    std::uint64_t next_object{};
    std::vector<audio::preview_object_source_binding_t> bindings;
    diagnostic_list_t diagnostics;
    std::size_t voice_count{};

    auto new_object() -> std::optional<object_id_t> {
        if (next_object >= audio::preview_music_object.value - 1) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                "Export-cut preview exhausted its derived audio identities."));
            return std::nullopt;
        }
        return object_id_t{++next_object};
    }

    auto append_voice_fragments(const preview_audio_event_t& event,
        const audio::preview_voice_source_t& voice,
        std::map<std::uint64_t, std::vector<audio::preview_voice_source_t>>& groups) -> bool {
        if (voice.frame_count > std::numeric_limits<std::uint64_t>::max() - event.sample_frame) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                "Export-cut preview voice exceeds its source clock."));
            return false;
        }
        for (const auto& kept : frames.kept_intervals(event.sample_frame, event.sample_frame + voice.frame_count)) {
            if (++voice_count > maximum_cut_preview_voices) {
                diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                    "Export-cut preview exceeds its bounded voice capacity."));
                return false;
            }
            auto fragment = voice;
            fragment.source_start_frame += kept.begin_frame - event.sample_frame;
            fragment.frame_count = kept.end_frame - kept.begin_frame;
            groups[kept.collapsed_begin_frame].push_back(fragment);
        }
        return true;
    }

    auto append_event(const preview_audio_event_t& source, std::vector<preview_audio_event_t>& target) -> bool {
        const auto binding = std::ranges::lower_bound(sources, source.object, {},
            &audio::preview_object_source_binding_t::object);
        if (binding == sources.end() || binding->object != source.object) {
            if (!frames.contains(source.sample_frame)) {
                auto event = source;
                event.sample_frame = frames.collapsed_frame(event.sample_frame);
                target.push_back(event);
            }
            return true;
        }
        std::map<std::uint64_t, std::vector<audio::preview_voice_source_t>> groups;
        for (const auto& voice : binding->voices)
            if (!append_voice_fragments(source, voice, groups)) return false;
        auto first = true;
        for (auto& [frame, voices] : groups) {
            const auto id = first ? std::optional{source.object} : new_object();
            if (!id) return false;
            auto event = source;
            event.object = *id;
            event.sample_frame = frame;
            target.push_back(event);
            bindings.push_back({.object = *id, .voices = std::move(voices)});
            first = false;
        }
        return true;
    }

    auto family(std::span<const preview_audio_event_t> source, std::vector<preview_audio_event_t>& target, bool bgm = false) -> bool {
        target.clear();
        for (const auto& event : source)
            if (!append_event(event, target)) return false;
        if (bgm) std::ranges::stable_sort(target, {}, &preview_audio_event_t::bgm_sort_key);
        else std::ranges::stable_sort(target, {}, &preview_audio_event_t::playable_sort_key);
        return true;
    }
};

auto maximum_preview_object(const preview_schedule_t& schedule,
                            std::span<const audio::preview_object_source_binding_t> bindings) -> std::uint64_t {
    std::uint64_t result{};
    for (const auto& binding : bindings)
        if (binding.object != audio::preview_music_object) result = std::max(result, binding.object.value);
    for (const auto& event : schedule.playable) result = std::max(result, event.object.value);
    for (const auto& event : schedule.bgm) result = std::max(result, event.object.value);
    return result;
}

void prepare_music_segments(preview_schedule_t& target, const preview_schedule_t& source,
                            const export_cut_frame_timeline_t& frames) {
    target.music_segments.clear();
    for (const auto& music : source.music_events()) {
        if (music.frame_count > std::numeric_limits<std::uint64_t>::max() - music.sample_frame) continue;
        for (const auto& kept : frames.kept_intervals(music.sample_frame, music.sample_frame + music.frame_count))
            target.music_segments.push_back({.resource = music.resource, .sample_frame = kept.collapsed_begin_frame,
                .source_start_frame = music.source_start_frame + kept.begin_frame - music.sample_frame,
                .frame_count = kept.end_frame - kept.begin_frame});
    }
    // A fully removed music source must not revive the original fallback range.
    target.music.reset();
}

void prepare_preview_markers(preview_schedule_t& target, const export_cut_frame_timeline_t& frames) {
    std::erase_if(target.metronome, [&](const auto& event) { return frames.contains(event.sample_frame); });
    for (auto& event : target.metronome) event.sample_frame = frames.collapsed_frame(event.sample_frame);
    std::erase_if(target.layer_ticks, [&](const auto& event) { return frames.contains(event.sample_frame); });
    for (auto& event : target.layer_ticks) event.sample_frame = frames.collapsed_frame(event.sample_frame);
    for (auto& event : target.bga) event.sample_frame = frames.collapsed_frame(event.sample_frame);
    target.discontinuities.clear();
}

} // namespace

auto prepare_export_cut_preview(preview_schedule_t& schedule,
    std::vector<audio::preview_object_source_binding_t>& bindings,
    std::span<const export_cut_t> cuts, std::uint64_t source_end) -> attempt_t<export_cut_frame_timeline_t> {
    if (cuts.empty()) return {};
    auto timeline = export_cut_timeline_t::from(cuts);
    if (!timeline.ok()) return {.diagnostics = std::move(timeline.diagnostics)};
    auto frames = export_cut_frame_timeline_t::from(timeline.value, schedule.sample_rate, source_end);
    if (!frames.ok()) return {.diagnostics = std::move(frames.diagnostics)};
    if (frames.value.collapsed_frame(source_end) == 0) {
        return {.diagnostics = {{.severity = diagnostic_severity_t::warning,
            .code = diagnostic_code_t::invalid_resolution,
            .message = "Export cuts cover the whole song; preview keeps the full source timeline."}}};
    }
    if (frames.value.is_empty()) return {};
    auto prepared = schedule;
    cut_preview_builder_t builder{.frames = frames.value, .sources = bindings,
        .next_object = maximum_preview_object(schedule, bindings)};
    if (!builder.family(schedule.playable, prepared.playable) || !builder.family(schedule.bgm, prepared.bgm, true))
        return {.diagnostics = std::move(builder.diagnostics)};
    const auto music = std::ranges::find(bindings, audio::preview_music_object,
        &audio::preview_object_source_binding_t::object);
    if (music != bindings.end()) builder.bindings.push_back(*music);
    prepare_music_segments(prepared, schedule, frames.value);
    prepare_preview_markers(prepared, frames.value);
    std::ranges::sort(builder.bindings, {}, &audio::preview_object_source_binding_t::object);
    schedule = std::move(prepared);
    bindings = std::move(builder.bindings);
    return {.value = std::move(frames.value)};
}

} // namespace wormedit::sys

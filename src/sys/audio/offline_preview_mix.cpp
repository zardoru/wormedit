#include "sys/audio/offline_preview_mix.h"
#include "sys/audio/audio_source_registration.h"
#include "core/checked_math.h"
#include <wormedit/audio/hitsound_resolver.h>
#include <algorithm>
#include <limits>

namespace wormedit::sys {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
auto offline_preview_mix_t::from(const song_document_t& song, chart_id_t chart,
    const offline_preview_paths_t& paths, bool samples, bool music)
    -> result_t<std::unique_ptr<offline_preview_mix_t>, error_t<failure_code_t>> {
    auto result = std::make_unique<offline_preview_mix_t>();
    if (auto prepared = result->prepare(song, chart, paths, samples, music); !prepared)
        return std::unexpected(prepared.error());
    return result;
}
auto offline_preview_mix_t::include_music(const song_document_t& song, const chart_document_t& chart)
    -> result_t<void, error_t<failure_code_t>> {
    const auto selected = audio::preview_music_selection_t::from(song, chart);
    if (!selected.ok()) return std::unexpected(error_t{failure_code_t::invalid_request, selected.diagnostics.front().message});
    if (!selected.value) return {};
    const auto source = sources_.open_source_binding(selected.value->asset);
    if (!source) return std::unexpected(source.error());
    bindings_.append_music(selected.value->resource, *source, schedule_);
    if (schedule_.music) {
        std::uint64_t end{};
        if (!checked_math::checked_add(schedule_.music->sample_frame, schedule_.music->frame_count, end))
            return std::unexpected(error_t{failure_code_t::arithmetic_overflow, std::string{"Song duration overflow."}});
        end_frame_ = std::max(end_frame_, end);
    }
    return {};
}
auto offline_preview_mix_t::prepare(const song_document_t& song, chart_id_t chart_id,
    const offline_preview_paths_t& paths, bool samples, bool music) -> result_t<void, error_t<failure_code_t>> {
    const auto* chart = song.read_chart(chart_id);
    if (!chart) return std::unexpected(error_t{failure_code_t::invalid_request, std::string{"Preview chart is unavailable."}});
    const auto timing = timing_index_t::from(*chart, {.allow_bounded_clock = true});
    if (!timing.value) return std::unexpected(error_t{failure_code_t::invalid_request, std::string{"Preview timing is invalid."}});
    const auto end = timing.value->seconds_at(timing.value->total_rows);
    if (end.seconds) end_frame_ = sample_frame_at(*end.seconds, audio::output_sample_rate).frame.value_or(0);
    (void)register_audition_sources(song, paths.project, 1, sources_);
    auto built = preview_schedule_t::from(*chart, *timing.value,
        {.sample_rate = audio::output_sample_rate, .include_metronome = false});
    if (!built.value) return std::unexpected(error_t{failure_code_t::invalid_request, std::string{"Preview schedule is invalid."}});
    schedule_ = std::move(*built.value);
    if (samples) {
        audio::hitsound_resolver_t hitsounds;
        hitsounds.environment = {.map_directory = paths.project, .skin_directory = paths.skin, .bundle_directory = paths.bundle};
        const auto extras = hitsounds.bindings(song, *chart, sources_, 1);
        auto resolved = audio::preview_bindings_resolution_t::from(song, chart_id, schedule_, sources_.bindings(), {.hitsounds = extras});
        if (!resolved.diagnostics.empty()) return std::unexpected(error_t{failure_code_t::invalid_request, resolved.diagnostics.front().message});
        bindings_ = std::move(resolved.value);
    } else { schedule_.playable.clear(); schedule_.bgm.clear(); }
    // Resolvable music contributes the same song clock even when muted.
    // No-audio output can fall back to the chart clock if music is missing.
    if (auto included = include_music(song, *chart); !included && music) return included;
    if (!music) { schedule_.music.reset(); schedule_.music_segments.clear(); }
    return {};
}
auto offline_preview_mix_t::render(std::uint64_t begin, std::uint64_t end,
    audio::offline_mix_options_t options, std::stop_token stop) -> result_t<audio::offline_mix_t, error_t<failure_code_t>> {
    return audio::render_offline_interval(sources_, schedule_, bindings_.bindings, begin, end, options, stop);
}
auto offline_preview_mix_t::render_to(std::uint64_t begin, std::uint64_t end,
    const audio::offline_mix_sink_t& sink, std::stop_token stop) -> result_t<void, error_t<failure_code_t>> {
    if (end <= begin) return std::unexpected(error_t{failure_code_t::invalid_request, std::string{"Select a nonempty PCM interval."}});
    const auto bytes = checked_math::checked_multiply(end - begin,
        static_cast<std::uint64_t>(audio::output_channel_count * sizeof(float)));
    if (!bytes || *bytes > std::numeric_limits<std::size_t>::max())
        return std::unexpected(error_t{failure_code_t::arithmetic_overflow, std::string{"Selected PCM interval exceeds its byte limit."}});
    return audio::render_offline_interval_to(sources_, schedule_, bindings_.bindings, begin, end, sink,
        {.maximum_timeline_frames = end, .maximum_output_frames = end - begin,
         .maximum_output_bytes = static_cast<std::size_t>(*bytes)}, stop);
}
#endif
}

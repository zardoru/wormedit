#include <wormedit/audio/offline_mix.h>

#include <algorithm>
#include <limits>
#include <new>

namespace wormedit::audio {
namespace {

auto validate_offline_interval(std::uint32_t sample_rate, std::uint64_t begin, std::uint64_t end,
                   const offline_mix_options_t& options, std::size_t& output_samples)
    -> std::optional<failure_code_t> {
    if (sample_rate != output_sample_rate || begin >= end || end > options.maximum_timeline_frames ||
        options.maximum_read_frames == 0 || options.maximum_read_frames > audio::maximum_read_frames ||
        options.pump.maximum_voices == 0 || options.pump.maximum_blocks == 0)
        return failure_code_t::invalid_request;
    const auto frames = end - begin;
    if (frames > options.maximum_output_frames ||
        frames > std::numeric_limits<std::size_t>::max() / output_channel_count)
        return failure_code_t::capacity_exceeded;
    output_samples = static_cast<std::size_t>(frames) * output_channel_count;
    if (output_samples > options.maximum_output_bytes / sizeof(float))
        return failure_code_t::capacity_exceeded;
    if (output_samples > std::vector<float>{}.max_size())
        return failure_code_t::capacity_exceeded;
    return std::nullopt;
}

auto materialization_status(const preview_bridge_pump_effect_t& result)
    -> std::optional<failure_code_t> {
    const auto& effect = result;
    if (effect.missing_sources != 0) return failure_code_t::unavailable;
    if (effect.status == failure_code_t::capacity_exceeded ||
        effect.status == failure_code_t::arithmetic_overflow ||
        effect.status == failure_code_t::transport_failed)
        return failure_code_t::capacity_exceeded;
    return effect.status == std::monostate{}
               ? std::nullopt
               : std::optional{failure_code_t::render_failed};
}

auto prime_offline_session(preview_session_t& session, preview_session_start_outcome_t started,
                           preview_pump_options_t options) -> preview_session_start_outcome_t {
    if (!started) return started;
    const auto pumped = session.pump_session(options);
    const auto& effect = pumped;
    started->detail.prime = effect.decode;
    if (!pumped.status.is_in_phase(::wormedit::audio::preview_session_pump_phase_t::ready)) {
        return std::unexpected(preview_session_start_error_t{
            .code = failure_code_t::prime_failed,
            .detail = std::move(started->detail),
            .token = started->token});
    }
    return started;
}

auto prime_status(const preview_session_start_error_t& error) -> failure_code_t {
    const auto& detail = error.detail;
    if (detail.materialize) {
        const auto status = materialization_status(*detail.materialize);
        if (status) return *status;
    }
    if (detail.prime && detail.prime->status == failure_code_t::capacity_exceeded)
        return failure_code_t::capacity_exceeded;
    return failure_code_t::decode_failed;
}

auto pump_session(preview_session_t& session, preview_pump_options_t options,
                  std::uint64_t current, std::uint64_t end, std::size_t chunk_frames)
    -> std::optional<failure_code_t> {
    options.lookahead_frames = static_cast<std::size_t>(
        std::min<std::uint64_t>(chunk_frames, end - current));
    const auto pumped = session.pump_session(options);
    const auto& effect = pumped;
    if (effect.materialize) {
        const auto status = materialization_status(*effect.materialize);
        if (status) return status;
    }
    if (pumped.status.is_in_phase(::wormedit::audio::preview_session_pump_phase_t::ready)) return std::nullopt;
    if (effect.decode) {
        const auto& decoded = *effect.decode;
        if (decoded.status == failure_code_t::capacity_exceeded)
            return failure_code_t::capacity_exceeded;
    }
    return failure_code_t::decode_failed;
}

auto render_chunk(preview_session_t& session, std::span<float> samples,
                  std::size_t expected_frames) -> bool {
    const auto rendered = session.render_frames(samples);
    const bool valid_status = rendered.status.is_in_phase(session_render_phase_t::mixed) ||
                              rendered.status.is_in_phase(session_render_phase_t::complete);
    return valid_status && rendered.mix.frames == expected_frames &&
           rendered.mix.missing_blocks == 0 && rendered.mix.dropped_voices == 0 &&
           rendered.mix.dropped_music_voices == 0;
}

void apply_fade(std::span<float> samples, std::uint64_t frames,
    std::uint64_t first_output_frame, std::uint32_t fade_ms) {
    if (fade_ms == 0 || frames == 0) return;
    const auto requested = static_cast<std::uint64_t>(fade_ms) * output_sample_rate / 1000U;
    const auto fade_frames = std::min(frames, std::max<std::uint64_t>(1, requested));
    const auto first_faded = frames - fade_frames;
    for (std::size_t i = 0; i < samples.size() / output_channel_count; ++i) {
        const auto frame = first_output_frame + i;
        if (frame < first_faded) continue;
        const auto offset = frame - first_faded;
        const auto gain = fade_frames == 1 ? 0.0F :
            static_cast<float>(fade_frames - 1U - offset) / static_cast<float>(fade_frames - 1U);
        samples[i * output_channel_count] *= gain;
        samples[i * output_channel_count + 1] *= gain;
    }
}

struct offline_interval_t {
    preview_session_t& session;
    const offline_mix_sink_t& sink;
    const offline_mix_options_t& options;
    std::uint64_t begin{}, end{};
    std::stop_token stop;

    auto emit(std::span<float> rendered, std::uint64_t first_frame)
        -> result_t<void, error_t<failure_code_t>> {
        const auto rendered_end = first_frame + rendered.size() / output_channel_count;
        const auto selected_first = std::max(first_frame, begin);
        const auto selected_end = std::min(rendered_end, end);
        if (selected_first >= selected_end) return {};
        auto selected = rendered.subspan(static_cast<std::size_t>(selected_first - first_frame) * output_channel_count,
            static_cast<std::size_t>(selected_end - selected_first) * output_channel_count);
        apply_fade(selected, end - begin, selected_first - begin, options.fade_out_ms);
        return sink(selected_first, selected);
    }
    auto render_chunks(std::span<float> chunk) -> result_t<void, error_t<failure_code_t>> {
        std::uint64_t current{};
        while (current < end) {
            if (stop.stop_requested()) return std::unexpected(error_t{failure_code_t::cancelled,
                std::string{"Offline mix cancelled."}});
            if (current != 0) {
                const auto failure = pump_session(session, options.pump, current, end, options.maximum_read_frames);
                if (failure) return std::unexpected(error_t{*failure, std::string{"Could not pump offline preview session."}});
            }
            const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(options.maximum_read_frames, end - current));
            auto samples = chunk.first(frames * output_channel_count);
            if (!render_chunk(session, samples, frames)) return std::unexpected(error_t{
                failure_code_t::render_failed, std::string{"Preview mix did not produce the complete selected interval."}});
            if (auto emitted = emit(samples, current); !emitted) return emitted;
            current += frames;
            if (!session.is_active() && current < end) return std::unexpected(error_t{
                failure_code_t::render_failed, std::string{"Offline preview session ended early."}});
        }
        if (stop.stop_requested()) return std::unexpected(error_t{failure_code_t::cancelled,
            std::string{"Offline mix cancelled."}});
        return {};
    }
};

} // namespace

void apply_offline_mix_fade(std::vector<float>& samples, std::uint32_t fade_ms) {
    apply_fade(samples, samples.size() / output_channel_count, 0, fade_ms);
}

auto render_offline_interval_to(decoded_source_registry_t& sources, const preview_schedule_t& schedule,
                             std::span<const preview_object_source_binding_t> objects,
                             std::uint64_t begin_frame, std::uint64_t end_frame, const offline_mix_sink_t& sink,
                             offline_mix_options_t options, std::stop_token stop)
    -> result_t<void, error_t<failure_code_t>> {
    if (!sink) return std::unexpected(error_t{failure_code_t::invalid_request, std::string{"Offline mix sink is required."}});
    std::size_t output_samples{};
    const auto valid = validate_offline_interval(schedule.sample_rate, begin_frame, end_frame, options, output_samples);
    if (valid)
        return std::unexpected(error_t{*valid, valid == failure_code_t::capacity_exceeded
                                 ? "Selected interval exceeds offline mix limits."
                                 : std::string{"Invalid offline mix interval or options."}});
    if (stop.stop_requested())
        return std::unexpected(error_t{failure_code_t::cancelled, std::string{"Offline mix cancelled."}});

    std::vector<float> chunk;
    try {
        chunk.resize(options.maximum_read_frames * output_channel_count);
    } catch (const std::bad_alloc&) {
        return std::unexpected(error_t{failure_code_t::capacity_exceeded,
            std::string{"Could not allocate offline mix buffer."}});
    }

    decoded_block_cache_t cache{preview_block_cache_limits()};
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    session.set_metronome_enabled(false);
    auto pump_options = options.pump;
    pump_options.lookahead_frames = static_cast<std::size_t>(
        std::min<std::uint64_t>(options.maximum_read_frames, end_frame));
    const auto started = prime_offline_session(
        session, session.start_session(schedule, objects, {}, 0, pump_options, end_frame), pump_options);
    if (!started) {
        const auto failure = prime_status(started.error());
        static_cast<void>(session.stop());
        return std::unexpected(error_t{failure, std::string{"Could not start or prime offline preview session."}});
    }
    if (started.value().detail.materialize && materialization_status(*started.value().detail.materialize).has_value()) {
        const auto failure = materialization_status(*started.value().detail.materialize);
        static_cast<void>(session.stop());
        return std::unexpected(error_t{*failure, std::string{"Preview schedule could not be fully materialized."}});
    }

    offline_interval_t interval{.session = session, .sink = sink, .options = options,
        .begin = begin_frame, .end = end_frame, .stop = stop};
    const auto rendered = interval.render_chunks(chunk);
    static_cast<void>(session.stop());
    return rendered;
}

auto render_offline_interval(decoded_source_registry_t& sources, const preview_schedule_t& schedule,
    std::span<const preview_object_source_binding_t> objects, std::uint64_t begin_frame, std::uint64_t end_frame,
    offline_mix_options_t options, std::stop_token stop) -> result_t<offline_mix_t, error_t<failure_code_t>> {
    offline_mix_t result;
    const auto sink = [&](std::uint64_t first, std::span<const float> samples) -> result_t<void, error_t<failure_code_t>> {
        if (result.interleaved_stereo.empty()) {
            try { result.interleaved_stereo.resize(static_cast<std::size_t>(end_frame - begin_frame) * output_channel_count); }
            catch (const std::bad_alloc&) { return std::unexpected(error_t{failure_code_t::capacity_exceeded,
                std::string{"Could not allocate offline mix buffer."}}); }
        }
        const auto offset = static_cast<std::size_t>(first - begin_frame) * output_channel_count;
        std::ranges::copy(samples, result.interleaved_stereo.begin() + static_cast<std::ptrdiff_t>(offset));
        return {};
    };
    auto rendered = render_offline_interval_to(sources, schedule, objects, begin_frame, end_frame, sink, options, stop);
    if (!rendered) return std::unexpected(std::move(rendered.error()));
    return result;
}

} // namespace wormedit::audio

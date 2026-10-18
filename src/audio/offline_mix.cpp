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

auto copy_selected_frames(std::span<const float> rendered, std::uint64_t first_frame,
                          std::uint64_t begin_frame, std::uint64_t end_frame,
                          std::vector<float>& output) -> void {
    const auto rendered_frames = rendered.size() / output_channel_count;
    const auto selected_first = std::max(first_frame, begin_frame);
    const auto selected_end = std::min(first_frame + rendered_frames, end_frame);
    if (selected_first >= selected_end) return;
    const auto input_offset = static_cast<std::size_t>(selected_first - first_frame) * output_channel_count;
    const auto output_offset = static_cast<std::size_t>(selected_first - begin_frame) * output_channel_count;
    const auto count = static_cast<std::size_t>(selected_end - selected_first) * output_channel_count;
    std::copy_n(rendered.begin() + static_cast<std::ptrdiff_t>(input_offset), count,
                output.begin() + static_cast<std::ptrdiff_t>(output_offset));
}

auto render_chunks(preview_session_t& session, std::span<float> chunk,
                   std::vector<float>& output, std::uint64_t begin, std::uint64_t end,
                   std::size_t maximum_chunk, preview_pump_options_t pump_options,
                   std::stop_token stop) -> std::optional<failure_code_t> {
    std::uint64_t current{};
    while (current < end) {
        if (stop.stop_requested()) return failure_code_t::cancelled;
        if (current != 0) {
            const auto pumped = pump_session(session, pump_options, current, end, maximum_chunk);
            if (pumped) return pumped;
        }
        const auto frames = static_cast<std::size_t>(
            std::min<std::uint64_t>(maximum_chunk, end - current));
        auto samples = chunk.first(frames * output_channel_count);
        if (!render_chunk(session, samples, frames)) return failure_code_t::render_failed;
        copy_selected_frames(samples, current, begin, end, output);
        current += frames;
        if (!session.is_active() && current < end) return failure_code_t::render_failed;
    }
    return std::nullopt;
}

auto apply_fade(std::vector<float>& samples, std::uint64_t frames, std::uint32_t fade_ms) -> void {
    if (fade_ms == 0 || frames == 0) return;
    const auto requested = static_cast<std::uint64_t>(fade_ms) * output_sample_rate / 1000U;
    const auto fade_frames = std::min(frames, std::max<std::uint64_t>(1, requested));
    const auto first = frames - fade_frames;
    for (std::uint64_t frame = first; frame < frames; ++frame) {
        const auto offset = frame - first;
        const auto gain = fade_frames == 1
                              ? 0.0F
                              : static_cast<float>(fade_frames - 1U - offset) /
                                    static_cast<float>(fade_frames - 1U);
        const auto sample = static_cast<std::size_t>(frame) * output_channel_count;
        samples[sample] *= gain;
        samples[sample + 1] *= gain;
    }
}

} // namespace

auto render_offline_interval(decoded_source_registry_t& sources, const preview_schedule_t& schedule,
                             std::span<const preview_object_source_binding_t> objects,
                             std::uint64_t begin_frame, std::uint64_t end_frame,
                             offline_mix_options_t options, std::stop_token stop)
    -> result_t<offline_mix_t, error_t<failure_code_t>> {
    std::size_t output_samples{};
    const auto valid = validate_offline_interval(schedule.sample_rate, begin_frame, end_frame, options, output_samples);
    if (valid)
        return std::unexpected(error_t{*valid, valid == failure_code_t::capacity_exceeded
                                 ? "Selected interval exceeds offline mix limits."
                                 : std::string{"Invalid offline mix interval or options."}});
    if (stop.stop_requested())
        return std::unexpected(error_t{failure_code_t::cancelled, std::string{"Offline mix cancelled."}});

    offline_mix_t result;
    std::vector<float> chunk;
    try {
        result.interleaved_stereo.resize(output_samples);
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
        result.interleaved_stereo.clear();
        return std::unexpected(error_t{failure, std::string{"Could not start or prime offline preview session."}});
    }
    if (started.value().detail.materialize && materialization_status(*started.value().detail.materialize).has_value()) {
        const auto failure = materialization_status(*started.value().detail.materialize);
        static_cast<void>(session.stop());
        result.interleaved_stereo.clear();
        return std::unexpected(error_t{*failure, std::string{"Preview schedule could not be fully materialized."}});
    }

    const auto rendered = render_chunks(session, chunk, result.interleaved_stereo,
                                        begin_frame, end_frame, options.maximum_read_frames,
                                        pump_options, stop);
    static_cast<void>(session.stop());
    if (rendered)
        return std::unexpected(error_t{*rendered, rendered == failure_code_t::cancelled
                                    ? "Offline mix cancelled."
                                    : std::string{"Preview mix did not produce the complete selected interval."}});
    apply_fade(result.interleaved_stereo, end_frame - begin_frame, options.fade_out_ms);
    return result;
}

} // namespace wormedit::audio

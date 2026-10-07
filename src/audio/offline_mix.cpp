#include <wormedit/audio/offline_mix.h>

#include <algorithm>
#include <limits>
#include <new>

namespace wormedit::audio {
namespace {

auto failed(offline_mix_status_t status, std::string message) -> offline_mix_result_t {
    return {.status = status, .interleaved_stereo = {}, .message = std::move(message)};
}

auto valid_request(std::uint32_t sample_rate, std::uint64_t begin, std::uint64_t end,
                   const offline_mix_options_t& options, std::size_t& output_samples)
    -> offline_mix_status_t {
    if (sample_rate != output_sample_rate || begin >= end || end > options.maximum_timeline_frames ||
        options.maximum_read_frames == 0 || options.maximum_read_frames > audio::maximum_read_frames ||
        options.pump.maximum_voices == 0 || options.pump.maximum_blocks == 0)
        return offline_mix_status_t::invalid_interval;
    const auto frames = end - begin;
    if (frames > options.maximum_output_frames ||
        frames > std::numeric_limits<std::size_t>::max() / output_channel_count)
        return offline_mix_status_t::capacity_exceeded;
    output_samples = static_cast<std::size_t>(frames) * output_channel_count;
    if (output_samples > options.maximum_output_bytes / sizeof(float))
        return offline_mix_status_t::capacity_exceeded;
    if (output_samples > std::vector<float>{}.max_size())
        return offline_mix_status_t::capacity_exceeded;
    return offline_mix_status_t::mixed;
}

auto materialization_status(const preview_bridge_pump_result_t& result)
    -> offline_mix_status_t {
    if (result.missing_sources != 0) return offline_mix_status_t::missing_sources;
    if (result.status == preview_bridge_pump_status_t::capacity_limited ||
        result.status == preview_bridge_pump_status_t::voice_id_exhausted ||
        result.status == preview_bridge_pump_status_t::transport_rejected_voice)
        return offline_mix_status_t::capacity_exceeded;
    return result.status == preview_bridge_pump_status_t::ready
               ? offline_mix_status_t::mixed
               : offline_mix_status_t::incomplete_mix;
}

auto prime_offline_session(preview_session_t& session, preview_session_start_result_t started,
                           preview_pump_options_t options) -> preview_session_start_result_t {
    if (!started) return started;
    const auto pumped = session.pump(options);
    started.prime = pumped.decode;
    if (!pumped) started.status = preview_session_start_status_t::prime_failed;
    return started;
}

auto prime_status(const preview_session_start_result_t& started) -> offline_mix_status_t {
    if (started.materialize) {
        const auto status = materialization_status(*started.materialize);
        if (status != offline_mix_status_t::mixed) return status;
    }
    if (started.prime && *started.prime) return offline_mix_status_t::mixed;
    if (started.prime && started.prime->status == preview_pump_status_t::capacity_exceeded)
        return offline_mix_status_t::capacity_exceeded;
    return offline_mix_status_t::decode_failed;
}

auto pump_session(preview_session_t& session, preview_pump_options_t options,
                  std::uint64_t current, std::uint64_t end, std::size_t chunk_frames)
    -> offline_mix_status_t {
    options.lookahead_frames = static_cast<std::size_t>(
        std::min<std::uint64_t>(chunk_frames, end - current));
    const auto pumped = session.pump(options);
    if (pumped.materialize) {
        const auto status = materialization_status(*pumped.materialize);
        if (status != offline_mix_status_t::mixed) return status;
    }
    if (pumped) return offline_mix_status_t::mixed;
    if (pumped.decode && pumped.decode->status == preview_pump_status_t::capacity_exceeded)
        return offline_mix_status_t::capacity_exceeded;
    return offline_mix_status_t::decode_failed;
}

auto render_chunk(preview_session_t& session, std::span<float> samples,
                  std::size_t expected_frames) -> bool {
    const auto rendered = session.render(samples);
    const bool valid_status = rendered.status == preview_session_render_status_t::mixed ||
                              rendered.status == preview_session_render_status_t::complete;
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
                   std::stop_token stop) -> offline_mix_status_t {
    std::uint64_t current{};
    while (current < end) {
        if (stop.stop_requested()) return offline_mix_status_t::cancelled;
        if (current != 0) {
            const auto pumped = pump_session(session, pump_options, current, end, maximum_chunk);
            if (pumped != offline_mix_status_t::mixed) return pumped;
        }
        const auto frames = static_cast<std::size_t>(
            std::min<std::uint64_t>(maximum_chunk, end - current));
        auto samples = chunk.first(frames * output_channel_count);
        if (!render_chunk(session, samples, frames)) return offline_mix_status_t::incomplete_mix;
        copy_selected_frames(samples, current, begin, end, output);
        current += frames;
        if (!session.active() && current < end) return offline_mix_status_t::incomplete_mix;
    }
    return offline_mix_status_t::mixed;
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
    -> offline_mix_result_t {
    std::size_t output_samples{};
    const auto valid = valid_request(schedule.sample_rate, begin_frame, end_frame, options, output_samples);
    if (valid != offline_mix_status_t::mixed)
        return failed(valid, valid == offline_mix_status_t::capacity_exceeded
                                 ? "Selected interval exceeds offline mix limits."
                                 : "Invalid offline mix interval or options.");
    if (stop.stop_requested()) return failed(offline_mix_status_t::cancelled, "Offline mix cancelled.");

    offline_mix_result_t result{.status = offline_mix_status_t::mixed,
                                .interleaved_stereo = {}, .message = {}};
    std::vector<float> chunk;
    try {
        result.interleaved_stereo.resize(output_samples);
        chunk.resize(options.maximum_read_frames * output_channel_count);
    } catch (const std::bad_alloc&) {
        return failed(offline_mix_status_t::capacity_exceeded, "Could not allocate offline mix buffer.");
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
        session, session.start(schedule, objects, {}, 0, pump_options, end_frame), pump_options);
    if (!started) {
        const auto failure = prime_status(started);
        static_cast<void>(session.stop());
        result.interleaved_stereo.clear();
        return failed(failure, "Could not start or prime offline preview session.");
    }
    if (started.materialize && materialization_status(*started.materialize) != offline_mix_status_t::mixed) {
        const auto failure = materialization_status(*started.materialize);
        static_cast<void>(session.stop());
        result.interleaved_stereo.clear();
        return failed(failure, "Preview schedule could not be fully materialized.");
    }

    const auto rendered = render_chunks(session, chunk, result.interleaved_stereo,
                                        begin_frame, end_frame, options.maximum_read_frames,
                                        pump_options, stop);
    static_cast<void>(session.stop());
    if (rendered != offline_mix_status_t::mixed)
        return failed(rendered, rendered == offline_mix_status_t::cancelled
                                    ? "Offline mix cancelled."
                                    : "Preview mix did not produce the complete selected interval.");
    apply_fade(result.interleaved_stereo, end_frame - begin_frame, options.fade_out_ms);
    return result;
}

} // namespace wormedit::audio

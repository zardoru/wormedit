#pragma once

#include <wormedit/audio/preview_session.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace wormedit::audio {

struct offline_mix_options_t {
    // Absolute timeline limit bounds prefix rendering from frame zero.
    std::uint64_t maximum_timeline_frames{output_sample_rate * 60U * 30U};
    std::uint64_t maximum_output_frames{output_sample_rate * 60U * 10U};
    std::size_t maximum_output_bytes{512U * 1024U * 1024U};
    std::size_t maximum_read_frames{audio::maximum_read_frames};
    std::uint32_t fade_out_ms{};
    // Per-pump capacity limits; lookahead_frames is replaced by the chunk size.
    preview_pump_options_t pump{};
};

enum class offline_mix_status_t {
    mixed,
    invalid_interval,
    capacity_exceeded,
    cancelled,
    missing_sources,
    incomplete_mix,
    decode_failed,
};

struct offline_mix_result_t {
    offline_mix_status_t status{offline_mix_status_t::invalid_interval};
    std::vector<float> interleaved_stereo;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == offline_mix_status_t::mixed;
    }
};

// Mixes [begin_frame, end_frame) through the same preview bridge and transport
// used by playback, without metronome clicks. Renders from timeline zero and
// discards the prefix so earlier voices retain their tails inside the selection.
// Errors and cancellation return no PCM. The schedule must outlive this call.
[[nodiscard]] auto render_offline_interval(
    decoded_source_registry_t& sources, const preview_schedule_t& schedule,
    std::span<const preview_object_source_binding_t> objects,
    std::uint64_t begin_frame, std::uint64_t end_frame,
    offline_mix_options_t options = {}, std::stop_token stop = {}) -> offline_mix_result_t;

} // namespace wormedit::audio

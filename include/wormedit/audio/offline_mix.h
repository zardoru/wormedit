#pragma once

#include <wormedit/audio/preview_session.h>
#include <wormedit/core/result.h>

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

struct offline_mix_t {
    std::vector<float> interleaved_stereo;
    auto operator==(const offline_mix_t&) const -> bool = default;
};

// Applies the same endpoint fade as offline rendering to bounded stereo PCM.
void apply_offline_mix_fade(std::vector<float>& interleaved_stereo, std::uint32_t fade_out_ms);

// Mixes [begin_frame, end_frame) through the same preview bridge and transport
// used by playback, without metronome clicks. Renders from timeline zero and
// discards the prefix so earlier voices retain their tails inside the selection.
// Errors and cancellation return no PCM. The schedule must outlive this call.
[[nodiscard]] auto render_offline_interval(
    decoded_source_registry_t& sources, const preview_schedule_t& schedule,
    std::span<const preview_object_source_binding_t> objects,
    std::uint64_t begin_frame, std::uint64_t end_frame,
    offline_mix_options_t options = {}, std::stop_token stop = {})
        -> result_t<offline_mix_t, error_t<failure_code_t>>;

} // namespace wormedit::audio

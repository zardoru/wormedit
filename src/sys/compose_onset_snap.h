#pragma once

#include <wormedit/audio/onset.h>
#include <wormedit/audio/waveform.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace wormedit::sys {

struct compose_onset_snap_t {
    std::vector<std::uint64_t> frames;
};

namespace detail {

[[nodiscard]] inline auto ceil_to_multiple(std::uint64_t value, std::uint64_t multiple)
    -> std::optional<std::uint64_t> {
    if (multiple == 0) return std::nullopt;
    const auto remainder = value % multiple;
    if (remainder == 0) return value;
    const auto increment = multiple - remainder;
    if (value > std::numeric_limits<std::uint64_t>::max() - increment) return std::nullopt;
    return value + increment;
}

} // namespace detail

// Captures bounded, source-frame-exact onset candidates from the existing cache.
// Any unavailable or invalid detector result intentionally produces no candidates.
[[nodiscard]] inline auto capture_compose_onsets(const audio::waveform_t& waveform,
    std::uint64_t begin_frame, std::uint64_t end_frame) -> compose_onset_snap_t {
    compose_onset_snap_t captured;
    const auto base_bin = static_cast<std::uint64_t>(waveform.key.frames_per_bin);
    const auto rate = static_cast<std::uint64_t>(waveform.sample_rate);
    if (base_bin == 0 || rate == 0 || begin_frame >= end_frame) return captured;

    const auto minimum_window = std::max(base_bin, rate / 100);
    const auto window = detail::ceil_to_multiple(minimum_window, base_bin);
    if (!window) return captured;
    const auto separation = detail::ceil_to_multiple((rate * 4U + 99U) / 100U, *window);
    if (!separation) return captured;

    const auto detected = audio::waveform_onset_index_t::from(waveform,
        {.begin_frame = begin_frame,
         .end_frame = end_frame,
         .window_frames = *window,
         .minimum_rms = 0.08,
         .minimum_rms_rise = 0.05,
         .minimum_separation_frames = *separation,
         .maximum_onsets = 4'096});
    if (!detected) return captured;
    captured.frames = detected->frames;
    return captured;
}

// Finds the closest candidate no farther than 30 ms. A tie resolves to the
// earlier source frame; selection uses only the captured vector.
[[nodiscard]] inline auto nearest_compose_onset(const compose_onset_snap_t& captured,
    std::uint64_t target_frame, std::uint32_t sample_rate) -> std::optional<std::uint64_t> {
    if (sample_rate == 0 || captured.frames.empty()) return std::nullopt;
    const auto maximum_distance = static_cast<std::uint64_t>(sample_rate) * 30U / 1'000U;
    std::optional<std::uint64_t> nearest;
    auto nearest_distance = std::numeric_limits<std::uint64_t>::max();
    for (const auto frame : captured.frames) {
        const auto distance = frame >= target_frame ? frame - target_frame : target_frame - frame;
        if (distance > maximum_distance) continue;
        if (!nearest || distance < nearest_distance || (distance == nearest_distance && frame < *nearest)) {
            nearest = frame;
            nearest_distance = distance;
        }
    }
    return nearest;
}

} // namespace wormedit::sys

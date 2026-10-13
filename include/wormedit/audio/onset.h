#pragma once

#include <wormedit/audio/waveform.h>
#include <wormedit/core/result.h>
#include <utility>

namespace wormedit::audio {

inline constexpr std::size_t maximum_onset_frames = 65'536;

struct onset_request_t {
    std::uint64_t begin_frame{};
    std::uint64_t end_frame{};
    std::uint64_t window_frames{1'024};
    double minimum_rms{0.08};
    double minimum_rms_rise{0.05};
    std::uint64_t minimum_separation_frames{2'048};
    std::size_t maximum_onsets{4'096};
};

enum class onset_status_t { detected, not_ready, invalid_request, invalid_waveform, capacity_exceeded };

struct waveform_onset_index_t {
    [[nodiscard]] static auto from(const waveform_t& waveform, onset_request_t request)
        -> result_t<waveform_onset_index_t, onset_status_t>;

    std::vector<std::uint64_t> frames;
    auto operator==(const waveform_onset_index_t&) const -> bool = default;
};
using waveform_onset_outcome_t = result_t<waveform_onset_index_t, onset_status_t>;

struct onset_result_t {
    [[nodiscard]] static auto from(waveform_onset_outcome_t&& outcome) -> onset_result_t {
        if (!outcome)
            return {.status = outcome.error(), .frames = {}};
        return {.status = onset_status_t::detected, .frames = std::move(outcome->frames)};
    }

    onset_status_t status{onset_status_t::invalid_request};
    std::vector<std::uint64_t> frames;
    [[nodiscard]] explicit operator bool() const noexcept { return status == onset_status_t::detected; }
};

// Cache-only RMS-rise detection on source-zero-aligned, nonoverlapping windows.
// Stereo channels are combined by maximum window RMS, never by summing PCM.
// An onset is represented by its window's start, within [begin_frame,end_frame).
// The preceding window supplies the baseline (silence at source frame zero).
// The cache must cover every whole window and baseline, except a source-tail
// window may be shorter. Base bins must divide the window width and their
// origin must align to the source-zero bin grid; otherwise not_ready.
// Missing/unfinished coverage returns not_ready with no partial frames.
// No decoding or source-sized scratch allocation occurs; output is capped.
[[nodiscard]] auto detect_waveform_onsets(const waveform_t& waveform, onset_request_t request)
    -> onset_result_t;

} // namespace wormedit::audio

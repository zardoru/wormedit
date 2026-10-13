#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/result.h>

#include <cstdint>
#include <optional>

namespace wormedit {

struct audio_offset_drag_t {
    [[nodiscard]] static auto from(const song_document_t& document, chart_id_t chart,
                                  std::uint32_t sample_rate, bool timing_locked = false)
        -> diagnosed_t<std::optional<audio_offset_drag_t>>;
    [[nodiscard]] auto revise(const song_document_t& document, std::int64_t delta_frames,
                              bool timing_locked = false) const
        -> diagnosed_t<std::optional<audio_offset_drag_t>>;

    chart_id_t chart;
    std::uint64_t document_revision{};
    std::uint32_t sample_rate{};
    signed_rational_t original_offset_seconds;
    signed_rational_t proposed_offset_seconds;
    std::int64_t delta_frames{};
};

using audio_offset_drag_outcome_t = diagnosed_t<std::optional<audio_offset_drag_t>>;

struct audio_offset_drag_result_t {
    [[nodiscard]] static auto from(audio_offset_drag_outcome_t&& outcome)
        -> audio_offset_drag_result_t;

    std::optional<audio_offset_drag_t> drag;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return drag.has_value() && diagnostics.empty();
    }
};

// Positive frame deltas move beat zero later in the audio. Every update is
// derived from the captured original offset, so pointer sampling frequency
// cannot accumulate rounding error.
[[nodiscard]] auto begin_audio_offset_drag(const song_document_t& document, chart_id_t chart,
                                           std::uint32_t sample_rate, bool timing_locked = false)
    -> audio_offset_drag_result_t;

[[nodiscard]] auto update_audio_offset_drag(const song_document_t& document,
                                            const audio_offset_drag_t& drag,
                                            std::int64_t delta_frames, bool timing_locked = false)
    -> audio_offset_drag_result_t;

[[nodiscard]] auto commit_audio_offset_drag(document_history_t& history,
                                            const audio_offset_drag_t& drag,
                                            bool timing_locked = false)
    -> document_command_result_t;

// Shared exact command for numeric Metadata edits and completed waveform drags.
[[nodiscard]] auto apply_audio_offset_edit(document_history_t& history,
                                           std::uint64_t expected_revision, chart_id_t chart,
                                           signed_rational_t offset_seconds,
                                           bool timing_locked = false) -> document_command_result_t;

} // namespace wormedit

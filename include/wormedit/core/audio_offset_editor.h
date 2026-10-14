#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/result.h>

#include <cstdint>
#include <optional>

namespace wormedit {

// Positive frame deltas move beat zero later in the audio. Every update is
// derived from the captured original offset, so pointer sampling frequency
// cannot accumulate rounding error.
struct audio_offset_drag_t {
    [[nodiscard]] static auto from(const song_document_t& document, chart_id_t chart,
                                  std::uint32_t sample_rate, bool timing_locked = false)
        -> diagnosed_t<std::optional<audio_offset_drag_t>>;
    [[nodiscard]] auto revise(const song_document_t& document, std::int64_t delta_frames,
                              bool timing_locked = false) const
        -> diagnosed_t<std::optional<audio_offset_drag_t>>;

    chart_stamp_t stamp;
    std::uint32_t sample_rate{};
    signed_rational_t original_offset_seconds;
    signed_rational_t proposed_offset_seconds;
    std::int64_t delta_frames{};

    [[nodiscard]] auto is_valid() const noexcept -> bool {
        return static_cast<bool>(stamp.chart) && sample_rate != 0;
    }
};

using audio_offset_drag_outcome_t = diagnosed_t<std::optional<audio_offset_drag_t>>;

[[nodiscard]] auto apply_audio_offset_drag(document_history_t& history,
                                            const audio_offset_drag_t& drag,
                                            bool timing_locked = false)
    -> document_command_result_t;

// Shared exact command for numeric Metadata edits and completed waveform drags.
[[nodiscard]] auto apply_audio_offset_edit(document_history_t& history, chart_id_t chart,
                                           signed_rational_t offset_seconds,
                                           bool timing_locked = false) -> document_command_result_t;

// Uses the ready transaction supplied by a captured offset proposal.
[[nodiscard]] auto apply_audio_offset_edit(document_command_transaction_t& transaction,
    chart_id_t chart, signed_rational_t offset_seconds, bool timing_locked = false)
    -> document_command_result_t;

} // namespace wormedit

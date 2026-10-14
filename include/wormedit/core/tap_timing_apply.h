#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/tap_tempo.h>

#include <cstdint>
#include <optional>

namespace wormedit {

enum class tap_bpm_choice_t { raw, nearest_one, nearest_half };

struct tap_timing_apply_proposal_t {
    chart_id_t chart;
    std::uint64_t document_revision{};
    std::uint32_t sample_rate{};
    std::uint16_t beats_per_measure{};
    tap_tempo_estimate_t estimate;
    tap_bpm_choice_t choice{tap_bpm_choice_t::raw};
    signed_rational_t beats_per_minute;
    signed_rational_t beat_zero_audio_offset_seconds;
};

struct tap_timing_apply_proposal_result_t {
    std::optional<tap_timing_apply_proposal_t> proposal;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return proposal.has_value() && diagnostics.empty();
    }
};

[[nodiscard]] auto propose_tap_timing_apply(const song_document_t& document, chart_id_t chart,
                                            const tap_tempo_estimate_t& estimate,
                                            std::uint32_t sample_rate,
                                            std::uint16_t beats_per_measure,
                                            tap_bpm_choice_t choice,
                                            bool timing_locked = false)
    -> tap_timing_apply_proposal_result_t;

// Builds the same BPM/offset edit used by the commit path for read-only grid preview.
[[nodiscard]] auto build_tap_timing_preview(
    const song_document_t& document, const tap_timing_apply_proposal_t& proposal)
    -> std::optional<chart_document_t>;

[[nodiscard]] auto apply_tap_timing_proposal(document_history_t& history,
                                             const tap_timing_apply_proposal_t& proposal,
                                             bool timing_locked = false)
    -> document_command_result_t;

} // namespace wormedit

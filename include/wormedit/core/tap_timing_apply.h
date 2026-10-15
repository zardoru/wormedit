#pragma once

#include <wormedit/core/proposal.h>
#include <wormedit/core/tap_tempo.h>

#include <cstdint>
#include <optional>

namespace wormedit {

enum class tap_bpm_choice_t { raw, nearest_one, nearest_half };

struct tap_timing_apply_proposal_t {
    [[nodiscard]] static auto propose(const song_document_t& document, chart_id_t chart,
                                  const tap_tempo_estimate_t& estimate, std::uint32_t sample_rate,
                                  std::uint16_t beats_per_measure, tap_bpm_choice_t choice,
                                  bool timing_locked = false)
        -> proposal_outcome_t<tap_timing_apply_proposal_t>;
    [[nodiscard]] auto revise(const chart_stamp_t& stamp, const song_document_t& document,
                              tap_bpm_choice_t choice, bool timing_locked = false) const
        -> proposal_outcome_t<tap_timing_apply_proposal_t>;

    std::uint32_t sample_rate{};
    std::uint16_t beats_per_measure{};
    tap_tempo_estimate_t estimate;
    tap_bpm_choice_t choice{tap_bpm_choice_t::raw};
    signed_rational_t beats_per_minute;
    signed_rational_t beat_zero_audio_offset_seconds;

    auto operator==(const tap_timing_apply_proposal_t&) const -> bool = default;
};

static_assert(staged_proposal<tap_timing_apply_proposal_t,
    std::tuple<const song_document_t&, chart_id_t, const tap_tempo_estimate_t&, std::uint32_t,
               std::uint16_t, tap_bpm_choice_t, bool>,
    std::tuple<const chart_stamp_t&, const song_document_t&, tap_bpm_choice_t, bool>>);

using tap_timing_apply_proposal_outcome_t = proposal_outcome_t<tap_timing_apply_proposal_t>;


// Builds the same BPM/offset edit used by the commit path for read-only grid preview.
[[nodiscard]] auto make_tap_timing_preview(
    const song_document_t& document, const proposal_t<tap_timing_apply_proposal_t>& proposal)
    -> std::optional<chart_document_t>;

[[nodiscard]] auto apply_tap_timing_proposal(document_history_t& history,
                                             const proposal_t<tap_timing_apply_proposal_t>& proposal,
                                             bool timing_locked = false)
    -> document_command_result_t;

} // namespace wormedit

#pragma once

#include <wormedit/core/proposal.h>
#include <wormedit/core/tap_tempo.h>

#include <cstdint>
#include <optional>

namespace wormedit {

enum class tap_bpm_choice_t { raw, nearest_one, nearest_half };

struct tap_timing_apply_proposal_t {
    static constexpr proposal_capture_policy_t apply_policy{
        .stale_label = "Tap timing proposal", .identity = document_identity_policy_t::revision_only,
        .locked_code = diagnostic_code_t::unsupported_feature,
        .locked_message = "tap timing apply is disabled while timing is locked"};
    static constexpr auto revision_policy = apply_policy;
    [[nodiscard]] auto prepare_document(const proposal_read_scope_t<chart_stamp_t>&) const
        -> std::optional<song_document_t>;
    [[nodiscard]] auto apply(proposal_edit_scope_t<chart_stamp_t>&,
                             bool timing_locked = false) const
        -> diagnosed_t<std::optional<std::string>>;

    [[nodiscard]] static auto propose(const song_document_t& document, chart_id_t chart,
                                  const tap_tempo_estimate_t& estimate, std::uint32_t sample_rate,
                                  std::uint16_t beats_per_measure, tap_bpm_choice_t choice,
                                  bool timing_locked = false)
        -> proposal_outcome_t<tap_timing_apply_proposal_t>;
    [[nodiscard]] auto revise(const proposal_read_scope_t<chart_stamp_t>& scope, tap_bpm_choice_t choice) const
        -> proposal_outcome_t<tap_timing_apply_proposal_t>;

    std::uint32_t sample_rate{};
    std::uint16_t beats_per_measure{};
    tap_tempo_estimate_t estimate;
    tap_bpm_choice_t choice{tap_bpm_choice_t::raw};
    signed_rational_t beats_per_minute;
    signed_rational_t beat_zero_audio_offset_seconds;

    auto operator==(const tap_timing_apply_proposal_t&) const -> bool = default;
private:
    [[nodiscard]] static auto from(const proposal_read_scope_t<chart_stamp_t>&,
        const tap_tempo_estimate_t& estimate, std::uint32_t sample_rate,
        std::uint16_t beats_per_measure, tap_bpm_choice_t choice)
        -> result_t<tap_timing_apply_proposal_t, diagnostic_t>;
};

static_assert(staged_proposal<tap_timing_apply_proposal_t,
    std::tuple<const song_document_t&, chart_id_t, const tap_tempo_estimate_t&, std::uint32_t,
               std::uint16_t, tap_bpm_choice_t, bool>,
    std::tuple<const proposal_read_scope_t<chart_stamp_t>&, tap_bpm_choice_t>>);

using tap_timing_apply_proposal_outcome_t = proposal_outcome_t<tap_timing_apply_proposal_t>;


// Builds the same BPM/offset edit used by the commit path for read-only grid preview.
[[nodiscard]] auto make_tap_timing_preview(
    const song_document_t& document, const proposal_t<tap_timing_apply_proposal_t>& proposal)
    -> std::optional<chart_document_t>;


} // namespace wormedit

#pragma once

#include <wormedit/core/proposal.h>

#include <cstdint>
#include <optional>

namespace wormedit {

// Positive frame deltas move beat zero later in the audio. Every update is
// derived from the captured original offset, so pointer sampling frequency
// cannot accumulate rounding error.
struct audio_offset_drag_t {
    static constexpr proposal_capture_policy_t apply_policy{
        .stale_label = "Audio offset drag",
        .identity = document_identity_policy_t::revision_only};
    static constexpr proposal_capture_policy_t revision_policy{
        .stale_label = apply_policy.stale_label, .identity = apply_policy.identity,
        .stale_code = apply_policy.stale_code,
        .locked_code = diagnostic_code_t::unsupported_feature,
        .locked_message = "audio offset dragging is disabled while timing is locked"};
    [[nodiscard]] auto apply(proposal_edit_scope_t<chart_stamp_t>&,
                             bool timing_locked = false) const -> diagnosed_t<std::optional<std::string>>;

    [[nodiscard]] static auto propose(const song_document_t& document, chart_id_t chart,
                                  std::uint32_t sample_rate, bool timing_locked = false)
        -> proposal_outcome_t<audio_offset_drag_t>;
    [[nodiscard]] auto revise(const proposal_read_scope_t<chart_stamp_t>& scope,
                              std::int64_t delta_frames) const
        -> proposal_outcome_t<audio_offset_drag_t>;

    std::uint32_t sample_rate{};
    signed_rational_t original_offset_seconds;
    signed_rational_t proposed_offset_seconds;
    std::int64_t delta_frames{};

    auto operator==(const audio_offset_drag_t&) const -> bool = default;
};

static_assert(staged_proposal<audio_offset_drag_t,
    std::tuple<const song_document_t&, chart_id_t, std::uint32_t, bool>,
    std::tuple<const proposal_read_scope_t<chart_stamp_t>&, std::int64_t>>);

using audio_offset_drag_outcome_t = proposal_outcome_t<audio_offset_drag_t>;

// Shared exact command for numeric Metadata edits and completed waveform drags.
[[nodiscard]] auto apply_audio_offset_edit(document_history_t& history, chart_id_t chart,
                                           signed_rational_t offset_seconds,
                                           bool timing_locked = false) -> document_command_result_t;

} // namespace wormedit

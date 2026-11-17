#pragma once
#include <wormedit/core/command.h>
#include <wormedit/core/result.h>
#include <wormedit/core/timing_edit_policy.h>
namespace wormedit {
struct timing_reposition_report_t {
    std::size_t moved_objects{}, rounded_positions{};
    auto operator==(const timing_reposition_report_t&) const -> bool = default;
};
// Repositions every timed object by its exact old audio time: unchanged BPM, stop, delay and
// warp events first (in time order, so each maps through the tempo already placed before it),
// then notes and hold tails, BGM, sound-channel events, SV/speed/kiai, sample controls, BGA,
// timing anchors and SV recipe ranges. Events the edit itself changed keep their new position.
// Audio-time markers already own seconds; beat-anchored markers and lane layout segments keep
// their musical position. Categories linked from another chart are left to that chart. The
// caller stages this on its timing edit's working chart and publishes through one transaction.
[[nodiscard]] auto preserve_audio_positions(const chart_document_t& before,
    chart_document_t& after, timing_anchoring_t) -> diagnosed_t<timing_reposition_report_t>;
[[nodiscard]] auto stage_timing_reposition(document_command_transaction_t&, chart_id_t,
    timing_anchoring_t, attempt_t<document_edit_effect_t>, timing_reposition_report_t* report = nullptr)
    -> attempt_t<document_edit_effect_t>;
}

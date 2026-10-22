#pragma once
#include <wormedit/core/command.h>
#include <wormedit/core/result.h>
#include <wormedit/core/timing_edit_policy.h>
namespace wormedit {
struct timing_reposition_report_t {
    std::size_t moved_objects{}, rounded_positions{};
    auto operator==(const timing_reposition_report_t&) const -> bool = default;
};
// Repositions sound-bearing objects, including hold tails, by exact old audio times.
// Audio-time bookmarks/sections already own seconds and remain untouched. The caller
// stages this on its timing edit's working chart and publishes through one transaction.
[[nodiscard]] auto preserve_audio_positions(const chart_document_t& before,
    chart_document_t& after, timing_anchoring_t) -> diagnosed_t<timing_reposition_report_t>;
[[nodiscard]] auto stage_timing_reposition(document_command_transaction_t&, chart_id_t,
    timing_anchoring_t, attempt_t<document_edit_effect_t>, timing_reposition_report_t* report = nullptr)
    -> attempt_t<document_edit_effect_t>;
}

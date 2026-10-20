#pragma once

#include "ui/frontend/actions.h"
#include <wormedit/core/command.h>
#include <wormedit/core/workspace.h>

#include <cstdint>
#include <optional>
#include <string>

namespace wormedit::sys {

inline auto validate_audio_offset_drag_binding(const wormedit::ui::workspace_action_t& action,
    const song_document_t& document, const workspace_controller_t& workspace,
    std::optional<std::uint32_t> selected_sample_rate) -> std::optional<std::string> {
    const auto supported_tab = workspace.active_tab == workspace_tab_t::audio_slices ||
                               workspace.active_tab == workspace_tab_t::metadata ||
                               workspace.active_tab == workspace_tab_t::compose;
    if (!supported_tab || !workspace.selected_chart || !selected_sample_rate ||
        *selected_sample_rate != action.audio_offset_drag_sample_rate)
        return "Audio offset drag expired after its chart or waveform changed.";
    const chart_stamp_t stamp{
        .document = {.project = document.id, .revision = action.audio_offset_drag_document_revision},
        .chart = chart_id_t{action.audio_offset_drag_chart_id}};
    if (!stamp.is_current(document, *workspace.selected_chart))
        return stale_action_message("Audio offset drag");
    return std::nullopt;
}

} // namespace wormedit::sys

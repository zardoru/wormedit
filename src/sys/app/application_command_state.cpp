#include "sys/app/application_command_state.h"

#include <algorithm>

namespace wormedit::sys {

auto application_command_state_t::project_t::semantic_layer_state_t::effective_solo_layer(
    const wormedit::chart_document_t& chart) const -> std::optional<wormedit::layer_id_t> {
    if (!semantic_layer_solo_active || semantic_layer_target_none || !semantic_layer_target)
        return std::nullopt;
    const auto found = std::ranges::find(chart.semantic_layers, *semantic_layer_target,
                                       &wormedit::semantic_layer_t::id);
    return found == chart.semantic_layers.end() ? std::nullopt : semantic_layer_target;
}

} // namespace wormedit::sys

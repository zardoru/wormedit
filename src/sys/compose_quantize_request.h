#pragma once

#include "ui/frontend.h"
#include <wormedit/core/quantize.h>

namespace wormedit::sys {
inline auto quantize_request_from_action(const WormUiWorkspaceAction& action,
                                 const std::vector<wormedit::object_id_t>& objects)
    -> std::optional<wormedit::quantize_request_t> {
    using namespace wormedit;
    if (action.compose_quantize_space > static_cast<std::uint8_t>(quantization_space_t::authored_measure) ||
        action.compose_quantize_tie > static_cast<std::uint8_t>(quantization_tie_t::reject) ||
        action.compose_quantize_holds > static_cast<std::uint8_t>(quantization_hold_t::both_endpoints))
        return std::nullopt;
    const auto divisions = action.compose_quantize_divisions;
    if (divisions == 0) return std::nullopt;
    return quantize_request_t{.divisions = divisions,
                              .space = static_cast<quantization_space_t>(action.compose_quantize_space),
                              .tie = static_cast<quantization_tie_t>(action.compose_quantize_tie),
                              .collisions = quantization_collision_t::reject,
                              .holds = static_cast<quantization_hold_t>(action.compose_quantize_holds),
                              .allow_resolution_promotion = false,
                              .objects = objects};
}

} // namespace wormedit::sys

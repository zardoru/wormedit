#pragma once

#include "ui/frontend.h"
#include <wormedit/core/quantize.h>

namespace wormedit::sys {
inline auto quantize_request_from_action(const wormedit::ui::compose_quantize_request_t& request,
                                 const std::vector<wormedit::object_id_t>& objects)
    -> std::optional<wormedit::quantize_request_t> {
    using namespace wormedit;
    if (request.space < quantization_space_t::standard_measure ||
        request.tie < quantization_tie_t::earlier ||
        request.holds < quantization_hold_t::start_only ||
        request.space > quantization_space_t::authored_measure ||
        request.tie > quantization_tie_t::reject ||
        request.holds > quantization_hold_t::both_endpoints)
        return std::nullopt;
    const auto divisions = request.divisions;
    if (divisions == 0) return std::nullopt;
    return quantize_request_t{.divisions = divisions,
                              .space = request.space,
                              .tie = request.tie,
                              .collisions = quantization_collision_t::reject,
                              .holds = request.holds,
                              .allow_resolution_promotion = false,
                              .objects = objects};
}

} // namespace wormedit::sys

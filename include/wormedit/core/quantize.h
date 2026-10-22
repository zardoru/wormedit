#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace wormedit {

// Builds an atomic quantization proposal without mutating the source chart.
// A required global redivision occurs only when explicitly allowed by the
// request and is reported separately from grid-snapping movement.
struct quantization_plan_t {
    [[nodiscard]] static auto from(const chart_document_t& chart, const quantize_request_t& request)
        -> diagnosed_t<quantization_plan_t>;

    // Present only when every proposed movement and resulting invariant is valid.
    // Preview movement lists remain available when publication fails.
    std::optional<chart_document_t> chart;
    std::vector<redivision_move_t> resolution_moves;
    std::vector<redivision_move_t> quantized_moves;
    std::optional<std::uint64_t> promoted_from;
    std::optional<std::uint64_t> promoted_to;
};
using quantization_outcome_t = diagnosed_t<quantization_plan_t>;

// Shared validation for audio-time repositioning after a timing edit.
[[nodiscard]] auto validate_repositioned_audio(const chart_document_t& before,
    const chart_document_t& after) -> diagnostic_list_t;

} // namespace wormedit

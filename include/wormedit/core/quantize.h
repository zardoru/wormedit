#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace wormedit {

struct quantization_plan_t {
    [[nodiscard]] static auto from(const chart_document_t& chart, const quantize_request_t& request)
        -> diagnosed_t<quantization_plan_t>;

    std::optional<chart_document_t> chart;
    std::vector<redivision_move_t> resolution_moves;
    std::vector<redivision_move_t> quantized_moves;
    std::optional<std::uint64_t> promoted_from;
    std::optional<std::uint64_t> promoted_to;
};
using quantization_outcome_t = diagnosed_t<quantization_plan_t>;

struct quantization_result_t {
    [[nodiscard]] static auto from(quantization_outcome_t&& outcome) -> quantization_result_t;

    // Present only when every proposed movement and resulting invariant is
    // valid. Preview movement lists remain available when publication fails.
    std::optional<chart_document_t> chart;
    std::vector<redivision_move_t> resolution_moves;
    std::vector<redivision_move_t> quantized_moves;
    diagnostic_list_t diagnostics;
    std::optional<std::uint64_t> promoted_from;
    std::optional<std::uint64_t> promoted_to;

    [[nodiscard]] explicit operator bool() const noexcept {
        return chart.has_value();
    }
};

// Builds an atomic quantization proposal without mutating the source chart.
// A required global redivision occurs only when explicitly allowed by the
// request and is reported separately from grid-snapping movement.
[[nodiscard]] auto quantize_chart_exact(const chart_document_t& chart,
                                        const quantize_request_t& request) -> quantization_result_t;

} // namespace wormedit

#pragma once
#include <wormedit/core/command.h>
#include <string_view>
namespace wormedit {
inline auto validate_chart_position(const measure_index_t& index, local_position_t position,
    diagnostic_list_t& diagnostics, std::string_view description) -> bool {
    if (index.row_of(position)) return true;
    diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
        std::string(description) + " position is outside the selected chart's measure geometry"));
    return false;
}
inline auto validate_chart_position(const chart_document_t& chart, local_position_t position,
    diagnostic_list_t& diagnostics, std::string_view description) -> bool {
    const auto measures = measure_index_t::from(chart);
    if (!measures) {
        diagnostics = measures.error();
        return false;
    }
    return validate_chart_position(*measures, position, diagnostics, description);
}
} // namespace wormedit

#pragma once
#include <wormedit/core/interaction_types.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/virtual_canvas.h>
#include <cmath>
#include <wormedit/core/export_cut_axis.h>
namespace wormedit {

inline auto position_at(const measure_index_t& index, std::uint64_t absolute) -> std::optional<local_position_t> {
    return index.position_at(absolute);
}
template<class RowOf>
auto effective_layout_by_row(const chart_document_t& chart, local_position_t position, const RowOf& row_of)
    -> const lane_layout_t* {
    const auto row = row_of(position);
    if (!row) return nullptr;
    const lane_layout_t* selected{};
    std::uint64_t selected_row{};
    for (const auto& segment : chart.lane_layouts) {
        const auto candidate = row_of(segment.position);
        if (candidate && *candidate <= *row && (!selected || *candidate >= selected_row)) {
            selected = &segment.layout;
            selected_row = *candidate;
        }
    }
    return selected;
}
struct measure_row_resolver_t {
    const measure_index_t& index;
    auto operator()(local_position_t position) const -> std::optional<std::uint64_t> {
        return index.row_of(position);
    }
};
inline auto effective_layout(const chart_document_t& chart, const measure_index_t& index,
                             local_position_t position) -> const lane_layout_t* {
    return layout_at(chart, index, position);
}
inline auto maximum_viewport_scroll(const chart_document_t& chart, const measure_index_t& index,
                             double scale, double viewport_height,
                             double end_fraction = editor_cursor_edge_margin_fraction,
                             const export_cut_axis_t* cut_axis = nullptr) -> std::optional<double> {
    auto total_rows = index.total_rows;
    if (has_chart_virtual_tail(chart)) {
        const auto canvas = build_virtual_canvas(chart);
        if (!std::holds_alternative<virtual_canvas_t>(canvas)) {
            return std::nullopt;
        }
        total_rows = std::get<virtual_canvas_t>(canvas).total_rows;
    }
    const auto display_rows = cut_axis ? cut_axis->collapsed_row(total_rows) : total_rows;
    const auto content_height = static_cast<double>(display_rows) * scale /
                                static_cast<double>(chart.divisions_per_standard_measure);
    if (!std::isfinite(content_height) || content_height < 0.0 || !std::isfinite(viewport_height) ||
        viewport_height <= 0.0) {
        return std::nullopt;
    }
    if (!is_editor_viewport_margins_valid(editor_cursor_edge_margin_fraction, end_fraction))
        return std::nullopt;
    return editor_maximum_viewport_scroll(content_height, viewport_height, end_fraction);
}
} // namespace wormedit

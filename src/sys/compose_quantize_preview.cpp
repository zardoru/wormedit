#include "sys/compose_quantize_preview.h"

#include <wormedit/core/quantize.h>

#include <algorithm>
#include <unordered_set>

namespace wormedit::sys {

auto preview_quantize_selection(const chart_document_t& chart,
                                const std::vector<object_id_t>& selected,
                                std::uint64_t snap_denominator) -> quantize_preview_t {
    quantize_preview_t preview{.selected = selected.size()};
    if (selected.empty()) {
        preview.message = "Select notes to preview quantization.";
        return preview;
    }
    const auto result = quantize_chart_exact(
        chart, {.divisions = snap_denominator, .objects = selected});
    std::unordered_set<std::uint64_t> moved;
    for (const auto& move : result.quantized_moves) moved.insert(move.object.value);
    preview.moved = moved.size();
    preview.unchanged = preview.selected > preview.moved ? preview.selected - preview.moved : 0;
    preview.blocked = result.chart ? 0 : std::max<std::size_t>(result.diagnostics.size(), 1);
    if (!result.chart && !result.diagnostics.empty())
        preview.message = result.diagnostics.front().message;
    return preview;
}

auto quantize_preview_cache_t::get(const chart_document_t& chart, std::uint64_t revision,
                                   const std::vector<object_id_t>& selected,
                                   std::uint64_t snap_denominator) -> const quantize_preview_t& {
    if (!value_ || chart_ != chart.id || revision_ != revision || snap_ != snap_denominator ||
        selected_ != selected) {
        value_ = preview_quantize_selection(chart, selected, snap_denominator);
        chart_ = chart.id;
        revision_ = revision;
        snap_ = snap_denominator;
        selected_ = selected;
    }
    return *value_;
}

} // namespace wormedit::sys

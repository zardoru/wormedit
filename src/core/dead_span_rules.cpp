#include <wormedit/core/dead_span_rules.h>

#include <algorithm>

namespace wormedit {

auto is_note_kind_playable(note_kind_t kind) noexcept -> bool {
    return kind == note_kind_t::tap || kind == note_kind_t::hold || kind == note_kind_t::roll ||
           kind == note_kind_t::lift;
}

auto is_row_in_warp(const chart_document_t& chart, const measure_index_t& index,
                 std::uint64_t absolute) -> bool {
    return std::ranges::any_of(chart.warps, [&](const auto& warp) {
        const auto begin = absolute_row(index, warp.span.begin);
        const auto end = absolute_row(index, warp.span.end);
        return begin && end && absolute > *begin && absolute < *end;
    });
}

auto is_head_blocked_by_warp(const chart_document_t& chart, const measure_index_t& index,
                          std::uint64_t absolute, note_kind_t kind) -> bool {
    return is_note_kind_playable(kind) && is_row_in_warp(chart, index, absolute);
}

} // namespace wormedit

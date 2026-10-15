#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/kiai_spans.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wormedit::sys {

using wormedit::kiai_span_t;

struct kiai_spans_view_t {
    // Disjoint and ordered by begin_row.
    std::vector<kiai_span_t> spans;
    // True when an event could not be placed or the limit cut the list.
    bool truncated{false};

    // Projects the Kiai events into merged intervals. Events are ordered by absolute row, then by
    // source order and id; an "on" while already on and an "off" while already off are ignored, so
    // overlapping or duplicate areas merge. Never mutates the chart.
    [[nodiscard]] static auto from(const chart_document_t& chart,
                                   const measure_index_t& measures, std::size_t limit)
        -> kiai_spans_view_t;
};

} // namespace wormedit::sys

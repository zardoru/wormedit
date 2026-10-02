#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wormedit::sys {

// A merged Kiai interval, derived from the chart's point on/off events. Rows are absolute and the
// interval is half-open [begin_row, end_row). An interval left open by the last event extends to
// the end of the chart.
struct kiai_span_t {
    std::uint64_t begin_row{};
    std::uint64_t end_row{};
    // The "on" event that opens the interval and the "off" event that closes it (invalid id when
    // the interval runs to the end of the chart).
    object_id_t head;
    object_id_t tail;
    bool open_ended{false};

    auto operator==(const kiai_span_t&) const -> bool = default;
};

struct kiai_projection_t {
    // Disjoint and ordered by begin_row.
    std::vector<kiai_span_t> spans;
    // True when an event could not be placed or the limit cut the list.
    bool truncated{false};
};

// Projects the Kiai events into merged intervals. Events are ordered by absolute row, then by
// source order and id; an "on" while already on and an "off" while already off are ignored, so
// overlapping or duplicate areas merge. Never mutates the chart.
[[nodiscard]] auto project_kiai_spans(const chart_document_t& chart,
                                      const measure_index_t& measures, std::size_t limit)
    -> kiai_projection_t;

} // namespace wormedit::sys

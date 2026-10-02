#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wormedit {

// A merged Kiai interval derived from the chart's point on/off events. Rows are absolute and the
// interval is half-open [begin_row, end_row). An interval left open by the last event ends at the
// chart's last row (`end_row == total_rows`, `open_ended`, no tail).
struct kiai_span_t {
    std::uint64_t begin_row{};
    std::uint64_t end_row{};
    object_id_t head;
    object_id_t tail;
    bool open_ended{false};

    auto operator==(const kiai_span_t&) const -> bool = default;
};

struct kiai_span_edit_t {
    // The area to replace or delete, identified by its opening event. Unset creates a new area.
    std::optional<object_id_t> head;
    // The area's new rows. Unset deletes the area named by `head`. Ending at the chart's last row
    // leaves the area open-ended. Overlapping or touching areas merge.
    std::optional<std::pair<std::uint64_t, std::uint64_t>> rows;
};

// Disjoint spans ordered by begin_row. Events order by absolute row, then source order and id;
// an "on" while already on and an "off" while already off are ignored, a zero-length area (on and
// off at one row) is omitted, and events outside the measure geometry are skipped.
[[nodiscard]] auto kiai_spans_of(const chart_document_t& chart, const measure_index_t& measures)
    -> std::vector<kiai_span_t>;

// Rewrites the chart's Kiai events so their coverage equals the current coverage with `edit`
// applied and overlaps merged. Events that still bound an area keep identity, effect bits and
// provenance; redundant events are dropped and new boundaries get fresh ids and source orders from
// `allocate_id`. Returns an error message and leaves the chart unchanged on failure.
[[nodiscard]] auto apply_kiai_span_edit(chart_document_t& chart, const measure_index_t& measures,
                                        const kiai_span_edit_t& edit,
                                        const std::function<std::optional<object_id_t>()>& allocate_id,
                                        std::size_t maximum_events) -> std::optional<std::string>;

} // namespace wormedit

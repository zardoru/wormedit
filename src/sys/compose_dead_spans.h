#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/timing.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wormedit::sys {

// Timing discontinuities that a note cannot sit inside. Stops and delays occupy a single row but
// take time; warps occupy rows but take none.
enum class dead_span_kind_t { stop, delay, warp };

struct dead_span_t {
    dead_span_kind_t kind{dead_span_kind_t::stop};
    // First object of the span; pauses sharing one row and kind merge into one span.
    object_id_t object;
    std::size_t merged_count{1};
    // Absolute chart rows. A stop or delay has begin_row == end_row; a warp is half-open
    // [begin_row, end_row).
    std::uint64_t begin_row{};
    std::uint64_t end_row{};
    // Elapsed time the span adds. Always zero for a warp.
    signed_rational_t seconds;

    auto operator==(const dead_span_t&) const -> bool = default;
};

struct dead_span_projection_t {
    // Ordered by begin_row, then stop before delay before warp, then source object id.
    std::vector<dead_span_t> spans;
    // True when the limit cut the list or an event could not be resolved.
    bool truncated{false};
};

// Projects every Stop, Delay and Warp of the chart. Stops and delays take their time from the
// timing index, so units, BPM and warp suppression are already resolved; zero-time pauses and
// unresolvable positions are omitted. Never mutates the chart.
[[nodiscard]] auto project_dead_spans(const chart_document_t& chart,
                                      const measure_index_t& measures,
                                      const timing_index_t& timing, std::size_t limit)
    -> dead_span_projection_t;

// True when `row` lies in a warp's half-open range [begin_row, end_row). Stops and delays are
// not rows to skip: whether a note at their row is valid depends on the discontinuity profile.
[[nodiscard]] auto row_is_in_warp(const dead_span_projection_t& projection, std::uint64_t row)
    -> bool;

} // namespace wormedit::sys

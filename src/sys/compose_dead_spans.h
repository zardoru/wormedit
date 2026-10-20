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

struct dead_spans_view_t {
    // Ordered by begin_row, then stop before delay before warp, then source object id.
    std::vector<dead_span_t> spans;
    // True when the limit cut the list or an event could not be resolved.
    bool truncated{false};

    // Projects every Stop, Delay and Warp of the chart. Stops and delays take their time from the
    // timing index, so units, BPM and warp suppression are already resolved; zero-time pauses and
    // unresolvable positions are omitted. Never mutates the chart.
    [[nodiscard]] static auto from(const chart_document_t& chart,
                                   const measure_index_t& measures,
                                   const timing_index_t& timing, std::size_t limit)
        -> dead_spans_view_t;
};

// True when `row` lies in a warp's half-open range [begin_row, end_row). Stops and delays are
// not rows to skip: whether a note at their row is valid depends on the discontinuity profile.
[[nodiscard]] auto row_is_in_warp(const dead_spans_view_t& projection, std::uint64_t row)
    -> bool;

// Height of the band a stop or delay gets beside its row.
inline constexpr double dead_span_pause_band_pixels = 14.0;

// A dead span in viewport pixels. Warps cover their own rows; a stop or delay has no rows, so it
// gets a band of `pause_band_pixels` beside its row: a delay sits before the row (notes on the
// row come after the delay) and a stop after it (notes on the row come before the stop).
struct dead_span_band_t {
    dead_span_kind_t kind{dead_span_kind_t::stop};
    object_id_t object;
    // Screen space, top < bottom, in the same orientation the field is drawn.
    double top{};
    double bottom{};
    // Elapsed seconds the span adds, for the label; zero for a warp.
    signed_rational_t seconds;
};

// Bands intersecting [0, viewport_height], in span order.
[[nodiscard]] auto build_dead_span_bands_view(const dead_spans_view_t& projection,
                                           double pixels_per_row, double scroll_pixels,
                                           double viewport_height, bool downscroll,
                                           double pause_band_pixels)
    -> std::vector<dead_span_band_t>;

// Refill a retained projection buffer without discarding its capacity.
void build_dead_span_bands_view(const dead_spans_view_t& projection,
                                double pixels_per_row, double scroll_pixels,
                                double viewport_height, bool downscroll,
                                double pause_band_pixels, std::vector<dead_span_band_t>& bands);

// True when a pointer at `y_pixels` (screen space) is inside any band.
[[nodiscard]] auto pointer_in_dead_span(const std::vector<dead_span_band_t>& bands, double y_pixels)
    -> bool;

} // namespace wormedit::sys

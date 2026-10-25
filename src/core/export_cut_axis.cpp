#include <wormedit/core/export_cut_axis.h>
#include <wormedit/core/editor_axis.h>
#include <wormedit/core/interaction_state.h>
#include <wormedit/core/rational.h>
#include "timing_arithmetic.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wormedit {
namespace {

auto boundary_row(const timing_index_t& timing, std::int64_t milliseconds,
                  timing_warp_inverse_side_t side) -> std::optional<long double> {
    if (timing.rows.empty()) return std::nullopt;
    const signed_rational_t seconds{milliseconds, 1'000};
    if (to_long_double(seconds) <= to_long_double(timing.rows.front().before_delays_seconds)) return 0;
    if (to_long_double(seconds) >= to_long_double(timing.rows.back().after_stops_seconds))
        return timing.total_rows;
    const auto position = timing.query_position(seconds, {.warp_side = side});
    if (!position) return std::nullopt;
    return std::clamp(to_long_double(position->value), 0.0L, static_cast<long double>(timing.total_rows));
}

auto preceding_interval(std::span<const export_cut_row_interval_t> intervals, long double row)
    -> std::size_t {
    return static_cast<std::size_t>(std::upper_bound(intervals.begin(), intervals.end(), row,
        [](long double value, const export_cut_row_interval_t& interval) { return value < interval.begin; }) -
        intervals.begin());
}

} // namespace

auto export_cut_axis_t::from(const timing_index_t& timing, const export_cut_timeline_t& cuts)
    -> diagnosed_t<export_cut_axis_t> {
    diagnosed_t<export_cut_axis_t> result;
    result.value.timeline_ = cuts;
    for (const auto& cut : cuts.intervals()) {
        const auto begin = boundary_row(timing, cut.begin_milliseconds, timing_warp_inverse_side_t::entry);
        const auto end = boundary_row(timing, cut.end_milliseconds, timing_warp_inverse_side_t::entry);
        if (!begin || !end) {
            result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                "Export cut boundaries cannot be projected onto this chart's timing."));
            return result;
        }
        if (*end <= *begin) continue;
        auto& intervals = result.value.intervals_;
        if (!intervals.empty() && *begin <= intervals.back().end)
            intervals.back().end = std::max(intervals.back().end, *end);
        else intervals.push_back({.begin = *begin, .end = *end});
    }
    long double removed{};
    for (const auto& interval : result.value.intervals_) {
        result.value.seams_.push_back(interval.begin - removed);
        removed += interval.end - interval.begin;
    }
    result.value.retain_event_visibility(timing);
    return result;
}

auto export_cut_axis_t::kept_intervals(long double begin, long double end) const
    -> std::vector<export_cut_row_interval_t> {
    std::vector<export_cut_row_interval_t> kept;
    auto cursor = begin;
    auto at = preceding_interval(intervals_, begin);
    if (at && intervals_[at - 1].end > begin) --at;
    for (; at < intervals_.size() && intervals_[at].begin < end; ++at) {
        if (cursor < intervals_[at].begin) append_kept_interval(cursor, intervals_[at].begin, kept);
        cursor = std::max(cursor, intervals_[at].end);
    }
    if (cursor < end) append_kept_interval(cursor, end, kept);
    for (const auto& point : row_visibility_)
        if (!point.hidden && point.row >= begin && point.row < end) kept.push_back({point.row, point.row});
    std::ranges::sort(kept, {}, &export_cut_row_interval_t::begin);
    return kept;
}

void export_cut_axis_t::retain_event_visibility(const timing_index_t& timing) {
    const auto cuts = timeline_.intervals();
    std::size_t audio{}, geometry{};
    const auto at_or_after = [](signed_rational_t seconds, std::int64_t milliseconds) {
        return timing_arithmetic::compare(seconds, {milliseconds, 1'000}).value_or(-1) >= 0;
    };
    for (const auto& boundary : timing.rows) {
        while (audio < cuts.size() && at_or_after(boundary.event_seconds, cuts[audio].end_milliseconds)) ++audio;
        while (geometry < intervals_.size() && boundary.absolute_row >= intervals_[geometry].end) ++geometry;
        const auto hidden = audio < cuts.size() && at_or_after(boundary.event_seconds, cuts[audio].begin_milliseconds);
        const auto geometric = geometry < intervals_.size() && boundary.absolute_row >= intervals_[geometry].begin;
        if (hidden != geometric)
            row_visibility_.push_back({static_cast<long double>(boundary.absolute_row), hidden});
    }
}

void export_cut_axis_t::append_kept_interval(long double begin, long double end,
    std::vector<export_cut_row_interval_t>& kept) const {
    auto cursor = begin;
    const auto first = std::ranges::lower_bound(row_visibility_, begin, {}, &row_visibility_t::row);
    for (auto point = first; point != row_visibility_.end() && point->row < end; ++point) {
        if (!point->hidden) continue;
        if (cursor < point->row) kept.push_back({cursor, point->row});
        cursor = std::nextafter(point->row, std::numeric_limits<long double>::infinity());
    }
    if (cursor < end) kept.push_back({cursor, end});
}

auto export_cut_axis_t::collapsed_row(long double row) const -> long double {
    const auto at = preceding_interval(intervals_, row);
    if (!at) return row;
    const auto& cut = intervals_[at - 1];
    return row < cut.end ? seams_[at - 1] : row - cut.end + seams_[at - 1];
}

auto export_cut_axis_t::source_boundary_row(long double row) const -> long double {
    const auto at = static_cast<std::size_t>(std::upper_bound(seams_.begin(), seams_.end(), row) - seams_.begin());
    return at ? row + intervals_[at - 1].end - seams_[at - 1] : row;
}

auto export_cut_axis_t::source_row(long double row) const -> long double {
    return next_kept_row(source_boundary_row(row));
}

auto export_cut_axis_t::source_row_before_seam(long double row) const -> long double {
    const auto at = std::lower_bound(seams_.begin(), seams_.end(), row);
    if (at != seams_.end() && *at == row)
        return intervals_[static_cast<std::size_t>(at - seams_.begin())].begin;
    return source_boundary_row(row);
}

auto export_cut_axis_t::contains_span(long double begin, long double end) const -> bool {
    return begin <= end && contains_row(begin) && contains_row(end) && collapsed_row(begin) == collapsed_row(end);
}

auto export_cut_axis_t::is_geometrically_hidden(long double row) const -> bool {
    const auto at = preceding_interval(intervals_, row);
    return at && row < intervals_[at - 1].end;
}

auto export_cut_axis_t::contains_row(long double row) const -> bool {
    const auto point = std::ranges::lower_bound(row_visibility_, row, {}, &row_visibility_t::row);
    return point != row_visibility_.end() && point->row == row ? point->hidden : is_geometrically_hidden(row);
}

auto export_cut_axis_t::next_kept_row(long double row) const -> long double {
    if (!contains_row(row)) return row;
    const auto at = preceding_interval(intervals_, row);
    auto next = at && row < intervals_[at - 1].end ? intervals_[at - 1].end : row;
    if (contains_row(next)) next = std::nextafter(next, std::numeric_limits<long double>::infinity());
    return next;
}

auto export_cut_axis_t::previous_kept_row(long double row) const -> long double {
    if (!contains_row(row)) return row;
    const auto at = preceding_interval(intervals_, row);
    const auto begin = at && row < intervals_[at - 1].end ? intervals_[at - 1].begin : row;
    return contains_row(begin) ? std::nextafter(begin, -std::numeric_limits<long double>::infinity()) : begin;
}

auto editor_axis_projection_t::from(const editor_interaction_state_t& state, std::uint64_t divisions)
    -> editor_axis_projection_t {
    return {.cut_axis = state.cut_axis.get(),
        .pixels_per_row = static_cast<long double>(state.pixels_per_standard_measure) / divisions,
        .viewport_scroll_pixels = state.viewport_scroll_pixels,
        .pixels_per_standard_measure = state.pixels_per_standard_measure,
        .divisions_per_standard_measure = divisions};
}

auto editor_axis_projection_t::from(const editor_interaction_view_model_t& view, std::uint64_t divisions)
    -> editor_axis_projection_t {
    return {.cut_axis = view.cut_axis.get(),
        .pixels_per_row = static_cast<long double>(view.pixels_per_standard_measure) / divisions,
        .viewport_scroll_pixels = view.viewport_scroll_pixels,
        .pixels_per_standard_measure = view.pixels_per_standard_measure,
        .divisions_per_standard_measure = divisions};
}

auto editor_axis_projection_t::y_for_row(long double row) const -> double {
    const auto source = cut_axis ? cut_axis->collapsed_row(row) : row;
    const auto world = divisions_per_standard_measure
        ? source * static_cast<long double>(pixels_per_standard_measure) / divisions_per_standard_measure
        : source * pixels_per_row;
    return static_cast<double>(world - viewport_scroll_pixels);
}

auto editor_axis_projection_t::row_at_y(long double y, bool before_seam) const -> long double {
    const auto row = divisions_per_standard_measure
        ? (y + viewport_scroll_pixels) * divisions_per_standard_measure / pixels_per_standard_measure
        : (y + viewport_scroll_pixels) / pixels_per_row;
    return cut_axis ? (before_seam ? cut_axis->source_row_before_seam(row) : cut_axis->source_row(row)) : row;
}

auto editor_axis_projection_t::content_height(long double rows) const -> double {
    const auto source = cut_axis ? cut_axis->collapsed_row(rows) : rows;
    return static_cast<double>(divisions_per_standard_measure
        ? source * static_cast<long double>(pixels_per_standard_measure) / divisions_per_standard_measure
        : source * pixels_per_row);
}

auto editor_axis_projection_t::contains_row(long double row) const -> bool {
    return cut_axis && cut_axis->contains_row(row);
}

} // namespace wormedit

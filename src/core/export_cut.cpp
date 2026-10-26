#include <wormedit/core/export_cut.h>

#include "checked_math.h"
#include "text_util.h"
#include "timing_arithmetic.h"
#include "wide_math.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace wormedit {
namespace {

auto before_milliseconds(signed_rational_t seconds, std::int64_t milliseconds) -> bool {
    if (seconds.numerator < 0) return true;
    return wide_math::u128_less(wide_math::multiply(static_cast<std::uint64_t>(seconds.numerator), 1000),
        wide_math::multiply(static_cast<std::uint64_t>(milliseconds), seconds.denominator));
}

auto preceding_interval(std::span<const export_cut_interval_t> intervals, std::int64_t source)
    -> std::optional<std::size_t> {
    const auto after = std::ranges::upper_bound(intervals, source, {}, &export_cut_interval_t::begin_milliseconds);
    if (after == intervals.begin()) return std::nullopt;
    return static_cast<std::size_t>(after - intervals.begin() - 1);
}

auto preceding_interval_seconds(std::span<const export_cut_interval_t> intervals, signed_rational_t source)
    -> std::optional<std::size_t> {
    const auto after = std::upper_bound(intervals.begin(), intervals.end(), source,
        [](signed_rational_t time, const export_cut_interval_t& interval) {
            return before_milliseconds(time, interval.begin_milliseconds);
        });
    if (after == intervals.begin()) return std::nullopt;
    return static_cast<std::size_t>(after - intervals.begin() - 1);
}

auto preceding_interval_seconds(std::span<const export_cut_interval_t> intervals, double source)
    -> std::optional<std::size_t> {
    const auto after = std::upper_bound(intervals.begin(), intervals.end(), source,
        [](double time, const export_cut_interval_t& interval) {
            return time < static_cast<double>(interval.begin_milliseconds) / 1000;
        });
    if (after == intervals.begin()) return std::nullopt;
    return static_cast<std::size_t>(after - intervals.begin() - 1);
}

} // namespace

auto export_cut_interval_t::milliseconds_at(const timing_index_t& timing, std::uint64_t row)
    -> std::optional<std::int64_t> {
    const auto query = timing.seconds_at(row);
    if (!query.seconds || query.seconds->numerator < 0) return std::nullopt;
    // Both edges belong to the millisecond containing the picked row. Rounding
    // an exclusive end upward would remove that row from the retained timeline;
    // rounding a start upward could leave its first selected row outside the cut.
    const auto frame = sample_frame_at(*query.seconds, 1000, sample_rounding_t::floor).frame;
    if (!frame || *frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    return static_cast<std::int64_t>(*frame);
}

auto export_cut_interval_t::from_rows(const timing_index_t& timing, std::uint64_t begin_row,
    std::uint64_t end_row) -> std::optional<export_cut_interval_t> {
    if (end_row <= begin_row) return std::nullopt;
    const auto begin = milliseconds_at(timing, begin_row);
    const auto end = milliseconds_at(timing, end_row);
    if (!begin || !end || *end <= *begin) return std::nullopt;
    return export_cut_interval_t{.begin_milliseconds = *begin, .end_milliseconds = *end};
}

auto export_cut_t::is_valid() const -> bool {
    return begin_milliseconds >= 0 && end_milliseconds > begin_milliseconds &&
        (!label || text_util::is_valid_bounded_text(*label, maximum_export_cut_label_bytes));
}

auto validate_export_cuts(std::span<const export_cut_t> cuts) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    if (cuts.size() > maximum_export_cuts)
        return {error(diagnostic_code_t::invalid_resolution, "The song exceeds the export-cut count limit.")};
    std::unordered_set<std::uint64_t> ids;
    std::size_t label_bytes{};
    for (const auto& cut : cuts) {
        if (!cut.id || !ids.insert(cut.id.value).second)
            diagnostics.push_back(error(diagnostic_code_t::duplicate_id, "Export-cut identities must be nonzero and unique."));
        if (!cut.is_valid())
            diagnostics.push_back(error(diagnostic_code_t::invalid_rational,
                "Export cuts need a nonnegative start, a later end and a bounded UTF-8 label."));
        if (cut.label && !checked_math::account_size(label_bytes, cut.label->size(), maximum_export_cut_total_label_bytes))
            diagnostics.push_back(error(diagnostic_code_t::invalid_resolution, "Export-cut labels exceed the song byte limit."));
    }
    return diagnostics;
}

auto export_cut_timeline_t::from(std::span<const export_cut_t> cuts) -> diagnosed_t<export_cut_timeline_t> {
    diagnosed_t<export_cut_timeline_t> result{.diagnostics = validate_export_cuts(cuts)};
    if (!result.ok()) return result;
    auto& intervals = result.value.intervals_;
    intervals.reserve(cuts.size());
    for (const auto& cut : cuts)
        intervals.push_back({.begin_milliseconds = cut.begin_milliseconds, .end_milliseconds = cut.end_milliseconds});
    std::ranges::sort(intervals, {}, &export_cut_interval_t::begin_milliseconds);
    std::size_t retained{};
    for (const auto interval : intervals) {
        if (retained && interval.begin_milliseconds <= intervals[retained - 1].end_milliseconds)
            intervals[retained - 1].end_milliseconds = std::max(interval.end_milliseconds, intervals[retained - 1].end_milliseconds);
        else intervals[retained++] = interval;
    }
    intervals.resize(retained);
    std::int64_t removed{};
    for (const auto interval : intervals) {
        result.value.collapsed_seams_.push_back(interval.begin_milliseconds - removed);
        removed += interval.end_milliseconds - interval.begin_milliseconds;
    }
    return result;
}

auto export_cut_timeline_t::contains(std::int64_t source_milliseconds) const -> bool {
    const auto index = preceding_interval(intervals_, source_milliseconds);
    return index && source_milliseconds < intervals_[*index].end_milliseconds;
}

auto export_cut_timeline_t::contains_seconds(signed_rational_t source_seconds) const -> bool {
    if (!source_seconds.denominator) return false;
    const auto index = preceding_interval_seconds(intervals_, source_seconds);
    return index && before_milliseconds(source_seconds, intervals_[*index].end_milliseconds);
}

auto export_cut_timeline_t::intersects(export_cut_interval_t source_span) const -> bool {
    if (source_span.end_milliseconds <= source_span.begin_milliseconds) return false;
    const auto first = std::ranges::upper_bound(intervals_, source_span.begin_milliseconds, {},
        &export_cut_interval_t::end_milliseconds);
    return first != intervals_.end() && first->begin_milliseconds < source_span.end_milliseconds;
}

auto export_cut_timeline_t::collapsed_milliseconds(std::int64_t source) const -> std::int64_t {
    const auto index = preceding_interval(intervals_, source);
    if (!index) return source;
    const auto& cut = intervals_[*index];
    if (source < cut.end_milliseconds) return collapsed_seams_[*index];
    return source - (cut.end_milliseconds - collapsed_seams_[*index]);
}

auto export_cut_timeline_t::source_milliseconds(std::int64_t collapsed) const -> std::optional<std::int64_t> {
    const auto after = std::ranges::upper_bound(collapsed_seams_, collapsed);
    if (after == collapsed_seams_.begin()) return collapsed;
    const auto index = static_cast<std::size_t>(after - collapsed_seams_.begin() - 1);
    return checked_math::checked_add(collapsed, intervals_[index].end_milliseconds - collapsed_seams_[index]);
}

auto export_cut_timeline_t::next_kept_milliseconds(std::int64_t source) const -> std::int64_t {
    const auto index = preceding_interval(intervals_, source);
    return index && source < intervals_[*index].end_milliseconds ? intervals_[*index].end_milliseconds : source;
}

auto export_cut_timeline_t::collapsed_seconds(signed_rational_t source) const -> std::optional<signed_rational_t> {
    if (!source.denominator) return std::nullopt;
    const auto index = preceding_interval_seconds(intervals_, source);
    if (!index) return source;
    const auto& cut = intervals_[*index];
    if (before_milliseconds(source, cut.end_milliseconds))
        return timing_arithmetic::try_normalize({collapsed_seams_[*index], 1000});
    return timing_arithmetic::subtract_exact(source,
        {cut.end_milliseconds - collapsed_seams_[*index], 1000});
}

auto export_cut_timeline_t::source_seconds(signed_rational_t collapsed) const -> std::optional<signed_rational_t> {
    if (!collapsed.denominator) return std::nullopt;
    const auto after = std::upper_bound(collapsed_seams_.begin(), collapsed_seams_.end(), collapsed,
        [](signed_rational_t time, std::int64_t seam) { return before_milliseconds(time, seam); });
    if (after == collapsed_seams_.begin()) return collapsed;
    const auto index = static_cast<std::size_t>(after - collapsed_seams_.begin() - 1);
    return timing_arithmetic::add_exact(collapsed,
        {intervals_[index].end_milliseconds - collapsed_seams_[index], 1000});
}

auto export_cut_timeline_t::collapsed_seconds(double source) const -> double {
    if (!std::isfinite(source)) return source;
    const auto index = preceding_interval_seconds(intervals_, source);
    if (!index) return source;
    const auto& cut = intervals_[*index];
    if (source < static_cast<double>(cut.end_milliseconds) / 1000)
        return static_cast<double>(collapsed_seams_[*index]) / 1000;
    return source - static_cast<double>(cut.end_milliseconds - collapsed_seams_[*index]) / 1000;
}

auto export_cut_timeline_t::source_seconds(double collapsed) const -> double {
    if (!std::isfinite(collapsed)) return collapsed;
    const auto after = std::upper_bound(collapsed_seams_.begin(), collapsed_seams_.end(), collapsed,
        [](double time, std::int64_t seam) { return time < static_cast<double>(seam) / 1000; });
    if (after == collapsed_seams_.begin()) return collapsed;
    const auto index = static_cast<std::size_t>(after - collapsed_seams_.begin() - 1);
    return collapsed + static_cast<double>(intervals_[index].end_milliseconds - collapsed_seams_[index]) / 1000;
}

auto export_cut_timeline_t::kept_intervals(std::int64_t begin, std::int64_t end) const
    -> std::vector<export_cut_interval_t> {
    std::vector<export_cut_interval_t> result;
    auto cursor = begin;
    for (const auto& cut : intervals_) {
        if (cursor >= end) break;
        if (cut.end_milliseconds <= cursor) continue;
        if (cut.begin_milliseconds >= end) break;
        if (cut.begin_milliseconds > cursor)
            result.push_back({.begin_milliseconds = cursor, .end_milliseconds = cut.begin_milliseconds});
        cursor = std::max(cursor, cut.end_milliseconds);
    }
    if (cursor < end) result.push_back({.begin_milliseconds = cursor, .end_milliseconds = end});
    return result;
}

auto export_cut_timeline_t::removed_milliseconds() const -> std::int64_t {
    return intervals_.empty() ? 0 : intervals_.back().end_milliseconds - collapsed_seams_.back();
}

} // namespace wormedit

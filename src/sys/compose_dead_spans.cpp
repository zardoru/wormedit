#include "core/timing_arithmetic.h"
#include "sys/compose_dead_spans.h"

#include <algorithm>
#include <map>
#include <tuple>

namespace wormedit::sys {
namespace {

auto pause_seconds(const timing_index_t& timing, std::uint64_t row, pause_kind_t kind)
    -> std::optional<signed_rational_t> {
    const auto before = timing.seconds_at(row, timing_boundary_side_t::before_delays);
    const auto event = timing.seconds_at(row, timing_boundary_side_t::event);
    const auto after = timing.seconds_at(row, timing_boundary_side_t::after_stops);
    if (!before.seconds || !event.seconds || !after.seconds) return std::nullopt;
    return kind == pause_kind_t::delay ? timing_arithmetic::subtract_exact(*event.seconds, *before.seconds)
                                       : timing_arithmetic::subtract_exact(*after.seconds, *event.seconds);
}

struct pause_group_t {
    object_id_t first;
    std::size_t count{};
};

void add_pause_spans(const chart_document_t& chart, const measure_index_t& measures,
                     const timing_index_t& timing, dead_span_projection_t& result) {
    std::map<std::tuple<std::uint64_t, int>, pause_group_t> groups;
    for (const auto& pause : chart.pauses) {
        const auto row = (measures).row_of(pause.position);
        if (!row) {
            result.truncated = true;
            continue;
        }
        auto& group = groups[{*row, static_cast<int>(pause.kind)}];
        group.first = group.count == 0 ? pause.id
                                       : (pause.id.value < group.first.value ? pause.id : group.first);
        ++group.count;
    }
    for (const auto& [key, group] : groups) {
        const auto [row, kind_value] = key;
        const auto kind = static_cast<pause_kind_t>(kind_value);
        const auto seconds = pause_seconds(timing, row, kind);
        if (!seconds) {
            result.truncated = true;
            continue;
        }
        if (seconds->numerator <= 0) continue;
        result.spans.push_back({.kind = kind == pause_kind_t::stop ? dead_span_kind_t::stop
                                                                   : dead_span_kind_t::delay,
                                .object = group.first,
                                .merged_count = group.count,
                                .begin_row = row,
                                .end_row = row,
                                .seconds = *seconds});
    }
}

void add_warp_spans(const chart_document_t& chart, const measure_index_t& measures,
                    dead_span_projection_t& result) {
    for (const auto& warp : chart.warps) {
        const auto begin = (measures).row_of(warp.span.begin);
        const auto end = (measures).row_of(warp.span.end);
        if (!begin || !end || *end <= *begin) {
            result.truncated = true;
            continue;
        }
        result.spans.push_back({.kind = dead_span_kind_t::warp,
                                .object = warp.id,
                                .begin_row = *begin,
                                .end_row = *end,
                                .seconds = {0, 1}});
    }
}

} // namespace

auto project_dead_spans(const chart_document_t& chart, const measure_index_t& measures,
                        const timing_index_t& timing, std::size_t limit)
    -> dead_span_projection_t {
    dead_span_projection_t result;
    add_pause_spans(chart, measures, timing, result);
    add_warp_spans(chart, measures, result);
    std::ranges::sort(result.spans, [](const dead_span_t& left, const dead_span_t& right) {
        return std::tie(left.begin_row, left.kind, left.object.value) <
               std::tie(right.begin_row, right.kind, right.object.value);
    });
    if (result.spans.size() > limit) {
        result.spans.resize(limit);
        result.truncated = true;
    }
    return result;
}

auto project_dead_span_bands(const dead_span_projection_t& projection, double pixels_per_row,
                             double scroll_pixels, double viewport_height, bool downscroll,
                             double pause_band_pixels) -> std::vector<dead_span_band_t> {
    std::vector<dead_span_band_t> bands;
    if (!(pixels_per_row > 0.0)) return bands;
    const auto source_y = [&](std::uint64_t row) {
        return static_cast<double>(row) * pixels_per_row - scroll_pixels;
    };
    for (const auto& span : projection.spans) {
        double begin = source_y(span.begin_row);
        double end = source_y(span.end_row);
        if (span.kind == dead_span_kind_t::stop) end = begin + pause_band_pixels;
        if (span.kind == dead_span_kind_t::delay) {
            end = begin;
            begin -= pause_band_pixels;
        }
        if (end < 0.0 || begin > viewport_height) continue;
        const auto top = downscroll ? viewport_height - end : begin;
        const auto bottom = downscroll ? viewport_height - begin : end;
        bands.push_back({span.kind, span.object, top, bottom, span.seconds});
    }
    return bands;
}

auto pointer_in_dead_span(const std::vector<dead_span_band_t>& bands, double y_pixels) -> bool {
    return std::ranges::any_of(bands, [y_pixels](const dead_span_band_t& band) {
        return y_pixels >= band.top && y_pixels < band.bottom;
    });
}

auto row_is_in_warp(const dead_span_projection_t& projection, std::uint64_t row) -> bool {
    return std::ranges::any_of(projection.spans, [row](const dead_span_t& span) {
        return span.kind == dead_span_kind_t::warp && row >= span.begin_row && row < span.end_row;
    });
}

} // namespace wormedit::sys

#include "sys/bookmark_chooser_projection.h"

#include <wormedit/core/timing.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <limits>
#include <tuple>

namespace wormedit::sys {
namespace {

struct ordered_row_t {
    bookmark_chooser_row_t row;
    std::optional<long double> absolute_row;
};

auto find_chart(const song_document_t& document, chart_id_t id) -> const chart_document_t* {
    const auto found = std::ranges::find(document.charts, id, &chart_document_t::id);
    return found == document.charts.end() ? nullptr : &*found;
}

auto row_for_position(const measure_index_t& measures, local_position_t position)
    -> std::optional<std::uint64_t> {
    const auto found = std::ranges::find(measures.entries, position.measure,
                                         &measure_index_entry_t::id);
    if (found == measures.entries.end() || position.row >= found->row_capacity)
        return std::nullopt;
    return found->absolute_begin_row + position.row;
}

auto position_label(const measure_index_t& measures, local_position_t position)
    -> std::string {
    const auto found = std::ranges::find(measures.entries, position.measure,
                                         &measure_index_entry_t::id);
    if (found == measures.entries.end()) return "Unavailable beat position";
    const auto number = static_cast<std::size_t>(found - measures.entries.begin()) + 1;
    char buffer[96]{};
    std::snprintf(buffer, sizeof(buffer), "Measure %03zu · row %llu", number,
                  static_cast<unsigned long long>(position.row));
    return buffer;
}

auto audio_position_label(std::int64_t milliseconds) -> std::string {
    const bool negative = milliseconds < 0;
    const auto magnitude = negative
        ? static_cast<std::uint64_t>(-(milliseconds + 1)) + 1
        : static_cast<std::uint64_t>(milliseconds);
    const auto minutes = magnitude / 60'000;
    const auto seconds = (magnitude / 1'000) % 60;
    const auto millis = magnitude % 1'000;
    char buffer[96]{};
    std::snprintf(buffer, sizeof(buffer), "%s%02llu:%02llu.%03llu",
                  negative ? "Audio −" : "Audio ",
                  static_cast<unsigned long long>(minutes),
                  static_cast<unsigned long long>(seconds),
                  static_cast<unsigned long long>(millis));
    return buffer;
}

auto make_row(const bookmark_t& bookmark, const measure_index_t& measures,
              const timing_index_t* timing) -> ordered_row_t {
    bookmark_chooser_row_t row{.bookmark_id = bookmark.id,
                               .anchor = bookmark.anchor,
                               .beat_position = bookmark.position,
                               .audio_milliseconds = bookmark.audio_milliseconds,
                               .name = bookmark.label,
                               .navigation_available = true};
    std::optional<long double> absolute;
    if (bookmark.anchor == marker_anchor_t::beat && bookmark.position) {
        row.position_label = position_label(measures, *bookmark.position);
        if (const auto exact = row_for_position(measures, *bookmark.position))
            absolute = static_cast<long double>(*exact);
        else
            row.navigation_available = false;
    } else if (bookmark.audio_milliseconds) {
        row.position_label = audio_position_label(*bookmark.audio_milliseconds);
        if (timing) {
            const auto musical = musical_position_at(*timing,
                signed_rational_t{*bookmark.audio_milliseconds, 1'000});
            if (musical.absolute_row) {
                absolute = static_cast<long double>(musical.absolute_row->numerator) /
                           static_cast<long double>(musical.absolute_row->denominator);
            }
        }
    }
    if (row.name.empty()) row.name = "(unnamed)";
    row.display_label = row.position_label + " · " + row.name + " · #" +
                        std::to_string(bookmark.id.value);
    return {.row = std::move(row), .absolute_row = absolute};
}

auto ordered_before(const ordered_row_t& left, const ordered_row_t& right) -> bool {
    if (left.absolute_row && right.absolute_row && *left.absolute_row != *right.absolute_row)
        return *left.absolute_row < *right.absolute_row;
    if (left.absolute_row.has_value() != right.absolute_row.has_value())
        return left.absolute_row.has_value();
    if (!left.absolute_row && !right.absolute_row) {
        const auto left_audio = left.row.audio_milliseconds.value_or(0);
        const auto right_audio = right.row.audio_milliseconds.value_or(0);
        if (left_audio != right_audio) return left_audio < right_audio;
    }
    return left.row.bookmark_id < right.row.bookmark_id;
}

} // namespace

auto project_bookmark_chooser(const song_document_t& document, chart_id_t chart_id,
                              std::size_t first_row, std::size_t row_limit)
    -> bookmark_chooser_projection_t {
    bookmark_chooser_projection_t result{.chart = chart_id,
                                         .document_revision = document.revision,
                                         .first_row = first_row,
                                         .row_limit = row_limit};
    if (row_limit == 0) {
        result.status = "Bookmark chooser page size must be positive.";
        return result;
    }
    const auto* chart = find_chart(document, chart_id);
    if (!chart) {
        result.status = "Bookmark chooser requires an active chart.";
        return result;
    }
    const auto measure_build = build_measure_index(*chart);
    if (!std::holds_alternative<measure_index_t>(measure_build)) {
        result.status = "Bookmark positions could not be resolved.";
        return result;
    }
    const auto& measures = std::get<measure_index_t>(measure_build);
    const auto timing_build = build_timing_index(*chart, {.allow_bounded_clock = true});
    const auto* timing = timing_build.index ? &*timing_build.index : nullptr;
    std::vector<ordered_row_t> ordered;
    ordered.reserve(chart->bookmarks.size());
    for (const auto& bookmark : chart->bookmarks)
        ordered.push_back(make_row(bookmark, measures, timing));
    std::ranges::sort(ordered, ordered_before);
    result.total_count = ordered.size();
    if (first_row >= ordered.size()) return result;
    const auto after = std::min(ordered.size(), first_row +
        std::min(row_limit, std::numeric_limits<std::size_t>::max() - first_row));
    result.rows.reserve(after - first_row);
    for (auto index = first_row; index < after; ++index)
        result.rows.push_back(std::move(ordered[index].row));
    return result;
}

} // namespace wormedit::sys

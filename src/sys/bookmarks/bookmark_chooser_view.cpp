#include "core/text_util.h"
#include <wormedit/core/rational.h>
#include "sys/bookmarks/bookmark_navigation.h"
#include "sys/compose/views/compose_view.h"
#include <wormedit/core/command.h>
#include <wormedit/core/workspace.h>

#include <wormedit/core/timing.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <limits>
#include <tuple>

namespace wormedit::sys {
auto bookmark_chooser_indices_t::from(const document_history_t& history, const workspace_view_t& workspace, bool is_open,
                                      const compose_scene_cache_t& cache) -> bookmark_chooser_indices_t {
    const auto* compose = std::get_if<workspace_compose_view_t>(&workspace.tab_view);
    if (!is_open || !compose) return {};
    const auto chart_id = compose->chart;
    const auto* indices = history.indices(chart_id);
    const auto* chart = history.document.find_chart(chart_id);
    const auto* clock = indices && indices->measures && chart &&
        cache.is_scene_key_current(history.document, *chart) &&
        cache.scene.cached_measure_index.get() == &*indices->measures
            ? &cache.timing.marker_timing_index : nullptr;
    return {indices, clock};
}

namespace {

struct ordered_row_t {
    bookmark_chooser_row_t row;
    std::optional<long double> absolute_row;
};

auto position_label(const measure_index_t& measures, local_position_t position)
    -> std::string {
    const auto* found = measures.entry_for(position.measure);
    if (!found) return "Unavailable beat position";
    const auto number = static_cast<std::size_t>(found - measures.entries.data()) + 1;
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
        if (const auto exact = measures.row_of(*bookmark.position))
            absolute = static_cast<long double>(*exact);
        else
            row.navigation_available = false;
    } else if (bookmark.audio_milliseconds) {
        row.position_label = audio_position_label(*bookmark.audio_milliseconds);
        if (timing) {
            const auto musical = timing->query_position(signed_rational_t{*bookmark.audio_milliseconds, 1'000});
            if (musical) {
                absolute = wormedit::to_long_double(musical->value);
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

auto matches_query(std::string_view label, std::string_view query) -> bool {
    return std::search(label.begin(), label.end(), query.begin(), query.end(),
        [](unsigned char left, unsigned char right) {
            return wormedit::text_util::ascii_iequal(static_cast<char>(left), static_cast<char>(right));
        }) != label.end();
}

} // namespace

auto bookmark_chooser_view_t::from(const song_document_t& document, chart_id_t chart_id,
                              std::size_t first_row, std::size_t row_limit, std::string_view query,
                                    const bookmark_chooser_indices_t& indices)
    -> bookmark_chooser_view_t {
    bookmark_chooser_view_t result{.chart = chart_id,
                                         .document_revision = document.revision,
                                         .first_row = first_row,
                                         .row_limit = row_limit};
    if (row_limit == 0) {
        result.status = "Bookmark chooser page size must be positive.";
        return result;
    }
    const auto* chart = document.find_chart(chart_id);
    if (!chart) {
        result.status = "Bookmark chooser requires an active chart.";
        return result;
    }
    std::optional<result_t<measure_index_t, diagnostic_list_t>> local;
    if (!indices.committed) local = measure_index_t::from(*chart);
    const auto& measure_build = indices.committed ? indices.committed->measures : *local;
    if (!measure_build) {
        result.status = "Bookmark positions could not be resolved.";
        return result;
    }
    const auto& measures = *measure_build;
    const auto timing_build = indices.bounded_clock ? decltype(timing_index_t::from(*chart)){}
        : timing_index_t::from(*chart, {.allow_bounded_clock = true});
    const auto& timing_value = indices.bounded_clock ? *indices.bounded_clock : timing_build.value;
    const auto* timing = timing_value ? &*timing_value : nullptr;
    std::vector<ordered_row_t> ordered;
    ordered.reserve(chart->bookmarks.size());
    for (const auto& bookmark : chart->bookmarks) {
        auto row = make_row(bookmark, measures, timing);
        if (query.empty() || matches_query(row.row.display_label, query))
            ordered.push_back(std::move(row));
    }
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

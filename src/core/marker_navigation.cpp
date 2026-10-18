#include "chart_object_ids.h"
#include <wormedit/core/marker_navigation.h>
#include <wormedit/core/command.h>

#include <algorithm>
#include <iterator>
#include <ranges>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

namespace wormedit {

auto has_marker_navigation_targets(const chart_document_t& chart) -> bool {
    return !chart.bookmarks.empty() || !chart.sections.empty();
}

namespace {

using chart_query_t = diagnosed_t<const chart_document_t*>;

auto validate_chart_query(const song_document_t& song, std::uint64_t expected_revision,
                          chart_id_t chart_id, marker_navigation_limits_t limits) -> chart_query_t {
    chart_query_t query{};
    const document_stamp_t stamp{.project = song.id, .revision = expected_revision};
    if (!stamp.is_current(song)) {
        query.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, stale_action_message("Marker navigation")));
        return query;
    }
    const auto found = song.find_chart(chart_id);
    if (!chart_id || found == nullptr) {
        query.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "marker navigation refers to an unknown chart"));
        return query;
    }
    if (std::ranges::count(song.charts, chart_id, &chart_document_t::id) != 1) {
        query.diagnostics.push_back(
            error(diagnostic_code_t::duplicate_id, "marker navigation chart ID is ambiguous"));
        return query;
    }
    if (limits.maximum_marker_objects == 0) {
        query.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution, "marker navigation cap must be positive"));
        return query;
    }
    if (found->bookmarks.size() > limits.maximum_marker_objects ||
        found->sections.size() > limits.maximum_marker_objects - found->bookmarks.size()) {
        query.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution,
                  "selected chart exceeds the configured marker-object cap"));
        return query;
    }
    query.value = &*found;
    return query;
}

auto resolve_marker_position_row(const measure_lookup_t& measures, local_position_t position,
                                 std::optional<object_id_t> object, diagnostic_list_t& diagnostics)
    -> std::optional<std::uint64_t> {
    const auto* measure = measures.entry_for(position.measure);
    if (measure == nullptr) {
        diagnostics.push_back(error(diagnostic_code_t::unknown_measure,
                                    "marker position refers to an unknown measure", object));
        return std::nullopt;
    }
    if (position.row >= measure->row_capacity) {
        diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
                                    "marker position lies outside its measure", object));
        return std::nullopt;
    }
    return measure->row_of(position.row);
}

auto is_valid_direction(marker_navigation_direction_t direction) -> bool {
    return direction == marker_navigation_direction_t::previous ||
           direction == marker_navigation_direction_t::next;
}

auto is_valid_anchor(marker_anchor_t anchor) -> bool {
    return anchor == marker_anchor_t::beat || anchor == marker_anchor_t::audio_time;
}

template <typename coordinate_t> struct candidate_t {
    coordinate_t coordinate{};
    marker_navigation_item_t item;
};

auto kind_order(marker_navigation_kind_t kind) -> std::uint8_t {
    switch (kind) {
    case marker_navigation_kind_t::bookmark:
        return 0;
    case marker_navigation_kind_t::section_begin:
        return 1;
    case marker_navigation_kind_t::section_end:
        return 2;
    }
    return 3;
}

template <typename coordinate_t>
auto marker_candidate_less(const candidate_t<coordinate_t>& left,
                           const candidate_t<coordinate_t>& right) -> bool {
    return std::tuple{left.coordinate, left.item.source_order, kind_order(left.item.kind)} <
           std::tuple{right.coordinate, right.item.source_order, kind_order(right.item.kind)};
}

template <typename coordinate_t> struct marker_search_t {
    coordinate_t cursor{};
    marker_navigation_direction_t direction{};
    marker_anchor_t anchor{};
    marker_navigation_filter_t filter;
    const measure_lookup_t* measures{};
    diagnostic_list_t& diagnostics;
    std::optional<candidate_t<coordinate_t>> best;

    void consider(coordinate_t coordinate, marker_navigation_item_t item) {
        if ((direction == marker_navigation_direction_t::next && coordinate <= cursor) ||
            (direction == marker_navigation_direction_t::previous && coordinate >= cursor))
            return;
        candidate_t<coordinate_t> candidate{.coordinate = coordinate, .item = std::move(item)};
        if (!best || (direction == marker_navigation_direction_t::next
                          ? marker_candidate_less(candidate, *best)
                          : marker_candidate_less(*best, candidate)))
            best = std::move(candidate);
    }
};

template <typename coordinate_t>
void consider_bookmark(marker_search_t<coordinate_t>& search, const bookmark_t& bookmark) {
    if (!is_valid_anchor(bookmark.anchor)) {
        search.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                           "bookmark has an unknown anchor kind", bookmark.id));
        return;
    }
    if (bookmark.anchor != search.anchor)
        return;
    if (!bookmark.id) {
        search.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "bookmark uses the reserved zero object ID"));
        return;
    }
    if (search.anchor == marker_anchor_t::beat) {
        if (!bookmark.position || bookmark.audio_milliseconds) {
            search.diagnostics.push_back(
                error(diagnostic_code_t::invalid_id,
                      "beat-anchored bookmark has inconsistent position fields", bookmark.id));
            return;
        }
        const auto row = resolve_marker_position_row(*search.measures, *bookmark.position,
                                                     bookmark.id, search.diagnostics);
        if (row)
            search.consider(*row, {.object = bookmark.id,
                                   .kind = marker_navigation_kind_t::bookmark,
                                   .source_order = bookmark.id.value,
                                   .beat_position = bookmark.position});
        return;
    }
    if (bookmark.position || !bookmark.audio_milliseconds) {
        search.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                           "audio-time bookmark has inconsistent position fields",
                                           bookmark.id));
        return;
    }
    search.consider(*bookmark.audio_milliseconds,
                    {.object = bookmark.id,
                     .kind = marker_navigation_kind_t::bookmark,
                     .source_order = bookmark.id.value,
                     .audio_milliseconds = bookmark.audio_milliseconds});
}

template <typename coordinate_t>
void collect_bookmarks(marker_search_t<coordinate_t>& search, const chart_document_t& chart) {
    if (!search.filter.include_bookmarks)
        return;
    for (const auto& bookmark : chart.bookmarks)
        consider_bookmark(search, bookmark);
}

template <typename coordinate_t>
void consider_beat_section(marker_search_t<coordinate_t>& search, const section_t& section) {
    if (!section.beat_span || section.audio_millisecond_span) {
        search.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                           "beat-anchored section has inconsistent span fields",
                                           section.id));
        return;
    }
    const auto begin = resolve_marker_position_row(*search.measures, section.beat_span->begin,
                                                   section.id, search.diagnostics);
    const auto end = resolve_marker_position_row(*search.measures, section.beat_span->end,
                                                 section.id, search.diagnostics);
    if (!begin || !end)
        return;
    if (*end <= *begin) {
        search.diagnostics.push_back(error(diagnostic_code_t::invalid_hold,
                                           "beat-anchored section end must be after its start",
                                           section.id));
        return;
    }
    if (search.filter.include_section_begins)
        search.consider(*begin, {.object = section.id,
                                 .kind = marker_navigation_kind_t::section_begin,
                                 .source_order = section.id.value,
                                 .beat_position = section.beat_span->begin});
    if (search.filter.include_section_ends)
        search.consider(*end, {.object = section.id,
                               .kind = marker_navigation_kind_t::section_end,
                               .source_order = section.id.value,
                               .beat_position = section.beat_span->end});
}

template <typename coordinate_t>
void consider_audio_section(marker_search_t<coordinate_t>& search, const section_t& section) {
    if (section.beat_span || !section.audio_millisecond_span) {
        search.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                           "audio-time section has inconsistent span fields",
                                           section.id));
        return;
    }
    const auto [begin, end] = *section.audio_millisecond_span;
    if (end <= begin) {
        search.diagnostics.push_back(error(diagnostic_code_t::invalid_hold,
                                           "audio-time section end must be after its start",
                                           section.id));
        return;
    }
    if (search.filter.include_section_begins)
        search.consider(end > begin ? static_cast<coordinate_t>(begin) : coordinate_t{},
                        {.object = section.id,
                         .kind = marker_navigation_kind_t::section_begin,
                         .source_order = section.id.value,
                         .audio_milliseconds = begin});
    if (search.filter.include_section_ends)
        search.consider(static_cast<coordinate_t>(end),
                        {.object = section.id,
                         .kind = marker_navigation_kind_t::section_end,
                         .source_order = section.id.value,
                         .audio_milliseconds = end});
}

template <typename coordinate_t>
void consider_section(marker_search_t<coordinate_t>& search, const section_t& section) {
    if (!is_valid_anchor(section.anchor)) {
        search.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "section has an unknown anchor kind", section.id));
        return;
    }
    if (section.anchor != search.anchor)
        return;
    if (!section.id) {
        search.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "section uses the reserved zero object ID"));
        return;
    }
    if (search.anchor == marker_anchor_t::beat)
        consider_beat_section(search, section);
    else
        consider_audio_section(search, section);
}

template <typename coordinate_t>
void collect_sections(marker_search_t<coordinate_t>& search, const chart_document_t& chart) {
    if (!search.filter.include_section_begins && !search.filter.include_section_ends)
        return;
    for (const auto& section : chart.sections)
        consider_section(search, section);
}

// Projects audio-time markers onto absolute rows for beat navigation.
struct audio_projection_t {
    const timing_index_t* timing{};
    const measure_index_t* measures{};
    diagnostic_list_t warnings;
    bool timing_usable{true};
};

struct projected_row_t {
    std::uint64_t row{};
    local_position_t position;
};

void warn(audio_projection_t& projection, std::string message,
          std::optional<object_id_t> object = std::nullopt) {
    projection.warnings.push_back({.severity = diagnostic_severity_t::warning,
                                   .code = diagnostic_code_t::row_out_of_range,
                                   .message = std::move(message),
                                   .object = object});
}

auto locate_row(const measure_index_t& measures, std::uint64_t row)
    -> std::optional<projected_row_t> {
    const auto next = std::ranges::upper_bound(measures.entries, row, {},
                                               &measure_index_entry_t::absolute_begin_row);
    if (next == measures.entries.begin() || row >= measures.total_rows)
        return std::nullopt;
    const auto& entry = *std::prev(next);
    if (row - entry.absolute_begin_row >= entry.row_capacity)
        return std::nullopt;
    return projected_row_t{row, {entry.id, row - entry.absolute_begin_row}};
}

auto project_audio_milliseconds(audio_projection_t& projection, std::int64_t milliseconds,
                                object_id_t object) -> std::optional<projected_row_t> {
    if (!projection.timing_usable)
        return std::nullopt;
    const auto query = (*projection.timing).row_at({milliseconds, 1000});
    if (!query.absolute_row) {
        if (query.error && query.error->code == timing_error_code_t::invalid_document) {
            projection.timing_usable = false;
            warn(projection, "timing index is unusable; audio-time markers are not navigable");
        } else {
            warn(projection, "audio-time marker lies outside the chart timing range", object);
        }
        return std::nullopt;
    }
    const auto& row = *query.absolute_row;
    const auto located =
        row.numerator < 0 || row.denominator == 0
            ? std::nullopt
            : locate_row(*projection.measures,
                         static_cast<std::uint64_t>(row.numerator) / row.denominator);
    if (!located)
        warn(projection, "audio-time marker maps outside the chart rows", object);
    return located;
}

void consider_projected(marker_search_t<std::uint64_t>& search, audio_projection_t& projection,
                        marker_navigation_item_t item) {
    const auto projected =
        project_audio_milliseconds(projection, *item.audio_milliseconds, item.object);
    if (!projected)
        return;
    item.beat_position = projected->position;
    search.consider(projected->row, std::move(item));
}

void project_audio_bookmark(marker_search_t<std::uint64_t>& search, audio_projection_t& projection,
                            const bookmark_t& bookmark) {
    if (bookmark.anchor != marker_anchor_t::audio_time || !bookmark.id)
        return;
    if (bookmark.position || !bookmark.audio_milliseconds) {
        search.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                           "audio-time bookmark has inconsistent position fields",
                                           bookmark.id));
        return;
    }
    consider_projected(search, projection,
                       {.object = bookmark.id,
                        .kind = marker_navigation_kind_t::bookmark,
                        .source_order = bookmark.id.value,
                        .audio_milliseconds = bookmark.audio_milliseconds});
}

void project_audio_section(marker_search_t<std::uint64_t>& search, audio_projection_t& projection,
                           const section_t& section) {
    if (section.anchor != marker_anchor_t::audio_time || !section.id)
        return;
    if (section.beat_span || !section.audio_millisecond_span ||
        section.audio_millisecond_span->second <= section.audio_millisecond_span->first) {
        search.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                           "audio-time section has an inconsistent or empty span",
                                           section.id));
        return;
    }
    const auto [begin, end] = *section.audio_millisecond_span;
    if (search.filter.include_section_begins)
        consider_projected(search, projection,
                           {.object = section.id,
                            .kind = marker_navigation_kind_t::section_begin,
                            .source_order = section.id.value,
                            .audio_milliseconds = begin});
    if (search.filter.include_section_ends)
        consider_projected(search, projection,
                           {.object = section.id,
                            .kind = marker_navigation_kind_t::section_end,
                            .source_order = section.id.value,
                            .audio_milliseconds = end});
}

void collect_projected_audio_markers(marker_search_t<std::uint64_t>& search,
                                     audio_projection_t& projection,
                                     const chart_document_t& chart) {
    if (search.filter.include_bookmarks)
        for (const auto& bookmark : chart.bookmarks)
            project_audio_bookmark(search, projection, bookmark);
    for (const auto& section : chart.sections)
        project_audio_section(search, projection, section);
}

auto validate_selected_marker(const chart_document_t& chart, const marker_navigation_item_t& item,
                              marker_navigation_outcome_t& result) -> bool {
    const auto count = chart_object_id_count(chart, item.object, 2);
    if (count == 0) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id,
                  "selected marker ID is not present in the chart object index", item.object));
        return false;
    }
    if (count != 1) {
        result.diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                                           "selected marker ID is ambiguous across chart objects",
                                           item.object));
        return false;
    }
    return true;
}

template <typename coordinate_t>
auto navigate_impl(const song_document_t& song, chart_query_t&& query,
                   chart_id_t chart_id, coordinate_t cursor,
                   marker_navigation_direction_t direction, marker_navigation_filter_t filter,
                   marker_anchor_t anchor,
                   const measure_lookup_t* measures, audio_projection_t* projection = nullptr)
    -> marker_navigation_outcome_t {
    marker_navigation_outcome_t result{
        .value = {.document_revision = song.revision, .chart = chart_id}};
    if (!query.diagnostics.empty()) {
        result.value.status = failure_code_t::invalid_request;
        result.diagnostics = std::move(query.diagnostics);
        return result;
    }
    if (!is_valid_direction(direction)) {
        result.value.status = failure_code_t::invalid_request;
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "marker navigation direction is unknown"));
        return result;
    }
    if (anchor == marker_anchor_t::beat && !measures) {
        result.value.status = failure_code_t::invalid_request;
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_measure, "beat navigation requires a measure index"));
        return result;
    }
    marker_search_t<coordinate_t> search{cursor,   direction,          anchor,      filter,
                                         measures, result.diagnostics, std::nullopt};
    collect_bookmarks(search, *query.value);
    collect_sections(search, *query.value);
    if (projection) {
        if constexpr (std::is_same_v<coordinate_t, std::uint64_t>)
            collect_projected_audio_markers(search, *projection, *query.value);
    }
    if (!result.diagnostics.empty()) {
        result.value.status = failure_code_t::invalid_request;
        return result;
    }
    if (projection)
        result.diagnostics.insert(result.diagnostics.end(), projection->warnings.begin(),
                                  projection->warnings.end());
    if (!search.best)
        return result;
    if (!validate_selected_marker(*query.value, search.best->item, result)) {
        result.value.status = failure_code_t::invalid_request;
        return result;
    }
    result.value.status = marker_navigation_phase_t::found;
    result.value.item = std::move(search.best->item);
    return result;
}

} // namespace

auto marker_navigation_match_t::from(const song_document_t& song, std::uint64_t expected_revision,
                                     chart_id_t chart, local_position_t beat_cursor,
                                     marker_navigation_direction_t direction,
                                     marker_navigation_filter_t filter,
                                     marker_navigation_limits_t limits,
                                     const timing_index_t* timing) -> marker_navigation_outcome_t {
    marker_navigation_outcome_t result{
        .value = {.document_revision = song.revision, .chart = chart}};
    auto query = validate_chart_query(song, expected_revision, chart, limits);
    if (!query.diagnostics.empty()) {
        result.value.status = failure_code_t::invalid_request;
        result.diagnostics = std::move(query.diagnostics);
        return result;
    }
    const auto built = measure_index_t::from(*query.value);
    if (!built) {
        result.value.status = failure_code_t::invalid_request;
        result.diagnostics = built.error();
        return result;
    }
    const auto& measures = *built;
    const auto measure_lookup = measure_lookup_t::from(measures);
    const auto cursor_row =
        resolve_marker_position_row(measure_lookup, beat_cursor, std::nullopt, result.diagnostics);
    if (!cursor_row.has_value()) {
        result.value.status = failure_code_t::invalid_request;
        return result;
    }
    audio_projection_t projection{.timing = timing, .measures = &measures};
    return navigate_impl(song, std::move(query), chart, *cursor_row, direction, filter,
                         marker_anchor_t::beat, &measure_lookup, timing ? &projection : nullptr);
}

auto marker_navigation_match_t::from(const song_document_t& song, std::uint64_t expected_revision,
                                     chart_id_t chart, std::int64_t audio_millisecond_cursor,
                                     marker_navigation_direction_t direction,
                                     marker_navigation_filter_t filter,
                                     marker_navigation_limits_t limits)
    -> marker_navigation_outcome_t {
    auto query = validate_chart_query(song, expected_revision, chart, limits);
    return navigate_impl(song, std::move(query), chart, audio_millisecond_cursor, direction,
                         filter, marker_anchor_t::audio_time, nullptr);
}

auto section_selection_t::from(const song_document_t& song, std::uint64_t expected_revision,
                               chart_id_t chart_id, object_id_t section_id,
                               marker_navigation_limits_t limits) -> diagnosed_t<section_selection_t> {
    diagnosed_t<section_selection_t> result{
        .value = {.status = failure_code_t::invalid_request,
                  .document_revision = song.revision,
                  .chart = chart_id,
                  .section = section_id}};
    auto query = validate_chart_query(song, expected_revision, chart_id, limits);
    if (!query.diagnostics.empty()) {
        result.diagnostics = std::move(query.diagnostics);
        return result;
    }
    if (!section_id) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                           "section selection uses the reserved zero object ID"));
        return result;
    }
    const auto found = std::ranges::find(query.value->sections, section_id, &section_t::id);
    if (found == query.value->sections.end()) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                           "section selection refers to an unknown section",
                                           section_id));
        return result;
    }
    if (std::ranges::count(query.value->sections, section_id, &section_t::id) != 1) {
        result.diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                                           "section selection ID is ambiguous", section_id));
        return result;
    }
    if (chart_object_id_count(*query.value, section_id, 2) != 1) {
        result.diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                                           "section selection ID is ambiguous across chart objects",
                                           section_id));
        return result;
    }

    if (found->anchor == marker_anchor_t::audio_time) {
        if (found->beat_span.has_value() || !found->audio_millisecond_span.has_value() ||
            found->audio_millisecond_span->second <= found->audio_millisecond_span->first) {
            result.diagnostics.push_back(error(diagnostic_code_t::invalid_hold,
                                               "audio-time section has an invalid selection span",
                                               section_id));
            return result;
        }
        result.value.status = section_selection_phase_t::timing_projection_required;
        return result;
    }
    if (found->anchor != marker_anchor_t::beat || !found->beat_span.has_value() ||
        found->audio_millisecond_span.has_value()) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                           "section has inconsistent anchor and span fields",
                                           section_id));
        return result;
    }

    const auto built = measure_index_t::from(*query.value);
    if (!built) {
        result.diagnostics = built.error();
        return result;
    }
    const auto& measures = *built;
    const auto measure_lookup = measure_lookup_t::from(measures);
    const auto begin = resolve_marker_position_row(measure_lookup, found->beat_span->begin,
                                                   section_id, result.diagnostics);
    const auto end = resolve_marker_position_row(measure_lookup, found->beat_span->end, section_id,
                                                 result.diagnostics);
    if (!begin.has_value() || !end.has_value()) {
        return result;
    }
    if (*end <= *begin) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_hold,
                                           "beat-anchored section end must be after its start",
                                           section_id));
        return result;
    }
    result.value.status = section_selection_phase_t::resolved;
    result.value.beat_span = found->beat_span;
    return result;
}

} // namespace wormedit

#include <wormedit/core/marker_navigation.h>

#include <algorithm>
#include <ranges>
#include <iterator>
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

auto error(diagnostic_code_t code, std::string message,
           std::optional<object_id_t> object = std::nullopt) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error,
            .code = code,
            .message = std::move(message),
            .object = object};
}

void reject(diagnostic_list_t& diagnostics, diagnostic_code_t code, std::string message,
            std::optional<object_id_t> object = std::nullopt) {
    diagnostics.push_back(error(code, std::move(message), object));
}

struct chart_query_t {
    const chart_document_t* chart{};
    diagnostic_list_t diagnostics;
};

auto validate_chart_query(const song_document_t& song, std::uint64_t expected_revision,
                          chart_id_t chart_id, marker_navigation_limits_t limits) -> chart_query_t {
    chart_query_t query;
    if (expected_revision != song.revision) {
        reject(query.diagnostics, diagnostic_code_t::invalid_id,
               "marker navigation document revision is stale");
        return query;
    }
    const auto found = std::ranges::find(song.charts, chart_id, &chart_document_t::id);
    if (!chart_id || found == song.charts.end()) {
        reject(query.diagnostics, diagnostic_code_t::invalid_id,
               "marker navigation refers to an unknown chart");
        return query;
    }
    if (std::ranges::count(song.charts, chart_id, &chart_document_t::id) != 1) {
        reject(query.diagnostics, diagnostic_code_t::duplicate_id,
               "marker navigation chart ID is ambiguous");
        return query;
    }
    if (limits.maximum_marker_objects == 0) {
        reject(query.diagnostics, diagnostic_code_t::invalid_resolution,
               "marker navigation cap must be positive");
        return query;
    }
    if (found->bookmarks.size() > limits.maximum_marker_objects ||
        found->sections.size() > limits.maximum_marker_objects - found->bookmarks.size()) {
        reject(query.diagnostics, diagnostic_code_t::invalid_resolution,
               "selected chart exceeds the configured marker-object cap");
        return query;
    }
    query.chart = &*found;
    return query;
}

template <typename callback_t>
auto visit_chart_object_ids(const chart_document_t& chart, callback_t&& callback) -> bool {
    const auto visit = [&callback](const auto& values) {
        for (const auto& value : values) {
            if (!callback(value.id)) {
                return false;
            }
        }
        return true;
    };
    if (!visit(chart.bgm_source_lanes) || !visit(chart.lane_layouts) || !visit(chart.bpms) ||
        !visit(chart.pauses) || !visit(chart.warps) || !visit(chart.scrolls) ||
        !visit(chart.speeds) || !visit(chart.kiai) || !visit(chart.timing_anchors) ||
        !visit(chart.bookmarks) || !visit(chart.sections) || !visit(chart.bga_events) ||
        !visit(chart.sound_channels)) {
        return false;
    }
    for (const auto& measure : chart.measures) {
        if (!visit(measure.notes) || !visit(measure.bgm_events)) {
            return false;
        }
    }
    for (const auto& channel : chart.sound_channels) {
        if (!visit(channel.events)) {
            return false;
        }
    }
    return true;
}

auto chart_object_id_count(const chart_document_t& chart, object_id_t sought) -> std::size_t {
    auto count = std::size_t{};
    (void)visit_chart_object_ids(chart, [&](object_id_t id) {
        if (id == sought) {
            ++count;
        }
        return count < 2;
    });
    return count;
}

using measure_lookup_t = std::unordered_map<std::uint64_t, const measure_index_entry_t*>;

auto build_measure_lookup(const measure_index_t& measures) -> measure_lookup_t {
    measure_lookup_t lookup;
    lookup.reserve(measures.entries.size());
    for (const auto& entry : measures.entries) {
        lookup.emplace(entry.id.value, &entry);
    }
    return lookup;
}

auto validate_position(const measure_lookup_t& measures, local_position_t position,
                       std::optional<object_id_t> object, diagnostic_list_t& diagnostics)
    -> std::optional<std::uint64_t> {
    const auto measure = measures.find(position.measure.value);
    if (measure == measures.end()) {
        reject(diagnostics, diagnostic_code_t::unknown_measure,
               "marker position refers to an unknown measure", object);
        return std::nullopt;
    }
    if (position.row >= measure->second->row_capacity) {
        reject(diagnostics, diagnostic_code_t::row_out_of_range,
               "marker position lies outside its measure", object);
        return std::nullopt;
    }
    return measure->second->absolute_begin_row + position.row;
}

auto valid_direction(marker_navigation_direction_t direction) -> bool {
    return direction == marker_navigation_direction_t::previous ||
           direction == marker_navigation_direction_t::next;
}

auto valid_anchor(marker_anchor_t anchor) -> bool {
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
auto precedes(const candidate_t<coordinate_t>& left, const candidate_t<coordinate_t>& right)
    -> bool {
    return std::tuple{left.coordinate, left.item.source_order, kind_order(left.item.kind)} <
           std::tuple{right.coordinate, right.item.source_order, kind_order(right.item.kind)};
}

template <typename coordinate_t>
struct marker_search_t {
    coordinate_t cursor{};
    marker_navigation_direction_t direction{};
    marker_anchor_t anchor{};
    marker_navigation_filter_t filter;
    const measure_lookup_t* measures{};
    diagnostic_list_t& diagnostics;
    std::optional<candidate_t<coordinate_t>> best;

    void consider(coordinate_t coordinate, marker_navigation_item_t item) {
        if ((direction == marker_navigation_direction_t::next && coordinate <= cursor) ||
            (direction == marker_navigation_direction_t::previous && coordinate >= cursor)) return;
        candidate_t<coordinate_t> candidate{.coordinate = coordinate, .item = std::move(item)};
        if (!best || (direction == marker_navigation_direction_t::next
                          ? precedes(candidate, *best) : precedes(*best, candidate)))
            best = std::move(candidate);
    }
};

template <typename coordinate_t>
void consider_bookmark(marker_search_t<coordinate_t>& search,
                       const bookmark_t& bookmark) {
    if (!valid_anchor(bookmark.anchor)) {
        reject(search.diagnostics, diagnostic_code_t::invalid_id,
               "bookmark has an unknown anchor kind", bookmark.id);
        return;
    }
    if (bookmark.anchor != search.anchor) return;
    if (!bookmark.id) {
        reject(search.diagnostics, diagnostic_code_t::invalid_id,
               "bookmark uses the reserved zero object ID");
        return;
    }
    if (search.anchor == marker_anchor_t::beat) {
        if (!bookmark.position || bookmark.audio_milliseconds) {
            reject(search.diagnostics, diagnostic_code_t::invalid_id,
                   "beat-anchored bookmark has inconsistent position fields", bookmark.id);
            return;
        }
        const auto row = validate_position(*search.measures, *bookmark.position,
                                           bookmark.id, search.diagnostics);
        if (row) search.consider(*row, {.object = bookmark.id,
            .kind = marker_navigation_kind_t::bookmark, .source_order = bookmark.id.value,
            .beat_position = bookmark.position});
        return;
    }
    if (bookmark.position || !bookmark.audio_milliseconds) {
        reject(search.diagnostics, diagnostic_code_t::invalid_id,
               "audio-time bookmark has inconsistent position fields", bookmark.id);
        return;
    }
    search.consider(*bookmark.audio_milliseconds, {.object = bookmark.id,
        .kind = marker_navigation_kind_t::bookmark, .source_order = bookmark.id.value,
        .audio_milliseconds = bookmark.audio_milliseconds});
}

template <typename coordinate_t>
void collect_bookmarks(marker_search_t<coordinate_t>& search, const chart_document_t& chart) {
    if (!search.filter.include_bookmarks) return;
    for (const auto& bookmark : chart.bookmarks) consider_bookmark(search, bookmark);
}

template <typename coordinate_t>
void consider_beat_section(marker_search_t<coordinate_t>& search, const section_t& section) {
    if (!section.beat_span || section.audio_millisecond_span) {
        reject(search.diagnostics, diagnostic_code_t::invalid_id,
               "beat-anchored section has inconsistent span fields", section.id);
        return;
    }
    const auto begin = validate_position(*search.measures, section.beat_span->begin,
                                         section.id, search.diagnostics);
    const auto end = validate_position(*search.measures, section.beat_span->end,
                                       section.id, search.diagnostics);
    if (!begin || !end) return;
    if (*end <= *begin) {
        reject(search.diagnostics, diagnostic_code_t::invalid_hold,
               "beat-anchored section end must be after its start", section.id);
        return;
    }
    if (search.filter.include_section_begins)
        search.consider(*begin, {.object = section.id,
            .kind = marker_navigation_kind_t::section_begin, .source_order = section.id.value,
            .beat_position = section.beat_span->begin});
    if (search.filter.include_section_ends)
        search.consider(*end, {.object = section.id,
            .kind = marker_navigation_kind_t::section_end, .source_order = section.id.value,
            .beat_position = section.beat_span->end});
}

template <typename coordinate_t>
void consider_audio_section(marker_search_t<coordinate_t>& search, const section_t& section) {
    if (section.beat_span || !section.audio_millisecond_span) {
        reject(search.diagnostics, diagnostic_code_t::invalid_id,
               "audio-time section has inconsistent span fields", section.id);
        return;
    }
    const auto [begin, end] = *section.audio_millisecond_span;
    if (end <= begin) {
        reject(search.diagnostics, diagnostic_code_t::invalid_hold,
               "audio-time section end must be after its start", section.id);
        return;
    }
    if (search.filter.include_section_begins)
        search.consider(end > begin ? static_cast<coordinate_t>(begin) : coordinate_t{},
            {.object = section.id, .kind = marker_navigation_kind_t::section_begin,
             .source_order = section.id.value, .audio_milliseconds = begin});
    if (search.filter.include_section_ends)
        search.consider(static_cast<coordinate_t>(end), {.object = section.id,
            .kind = marker_navigation_kind_t::section_end, .source_order = section.id.value,
            .audio_milliseconds = end});
}

template <typename coordinate_t>
void consider_section(marker_search_t<coordinate_t>& search, const section_t& section) {
    if (!valid_anchor(section.anchor)) {
        reject(search.diagnostics, diagnostic_code_t::invalid_id,
               "section has an unknown anchor kind", section.id);
        return;
    }
    if (section.anchor != search.anchor) return;
    if (!section.id) {
        reject(search.diagnostics, diagnostic_code_t::invalid_id,
               "section uses the reserved zero object ID");
        return;
    }
    if (search.anchor == marker_anchor_t::beat)
        consider_beat_section(search, section);
    else
        consider_audio_section(search, section);
}

template <typename coordinate_t>
void collect_sections(marker_search_t<coordinate_t>& search, const chart_document_t& chart) {
    if (!search.filter.include_section_begins && !search.filter.include_section_ends) return;
    for (const auto& section : chart.sections) consider_section(search, section);
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
    if (next == measures.entries.begin() || row >= measures.total_rows) return std::nullopt;
    const auto& entry = *std::prev(next);
    if (row - entry.absolute_begin_row >= entry.row_capacity) return std::nullopt;
    return projected_row_t{row, {entry.id, row - entry.absolute_begin_row}};
}

auto project_audio_milliseconds(audio_projection_t& projection, std::int64_t milliseconds,
                                object_id_t object) -> std::optional<projected_row_t> {
    if (!projection.timing_usable) return std::nullopt;
    const auto query = musical_position_at(*projection.timing, {milliseconds, 1000});
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
    const auto located = row.numerator < 0 || row.denominator == 0
                             ? std::nullopt
                             : locate_row(*projection.measures,
                                          static_cast<std::uint64_t>(row.numerator) /
                                              row.denominator);
    if (!located) warn(projection, "audio-time marker maps outside the chart rows", object);
    return located;
}

void consider_projected(marker_search_t<std::uint64_t>& search, audio_projection_t& projection,
                        marker_navigation_item_t item) {
    const auto projected = project_audio_milliseconds(projection, *item.audio_milliseconds,
                                                      item.object);
    if (!projected) return;
    item.beat_position = projected->position;
    search.consider(projected->row, std::move(item));
}

void project_audio_bookmark(marker_search_t<std::uint64_t>& search,
                            audio_projection_t& projection, const bookmark_t& bookmark) {
    if (bookmark.anchor != marker_anchor_t::audio_time || !bookmark.id) return;
    if (bookmark.position || !bookmark.audio_milliseconds) {
        reject(search.diagnostics, diagnostic_code_t::invalid_id,
               "audio-time bookmark has inconsistent position fields", bookmark.id);
        return;
    }
    consider_projected(search, projection,
                       {.object = bookmark.id, .kind = marker_navigation_kind_t::bookmark,
                        .source_order = bookmark.id.value,
                        .audio_milliseconds = bookmark.audio_milliseconds});
}

void project_audio_section(marker_search_t<std::uint64_t>& search,
                           audio_projection_t& projection, const section_t& section) {
    if (section.anchor != marker_anchor_t::audio_time || !section.id) return;
    if (section.beat_span || !section.audio_millisecond_span ||
        section.audio_millisecond_span->second <= section.audio_millisecond_span->first) {
        reject(search.diagnostics, diagnostic_code_t::invalid_id,
               "audio-time section has an inconsistent or empty span", section.id);
        return;
    }
    const auto [begin, end] = *section.audio_millisecond_span;
    if (search.filter.include_section_begins)
        consider_projected(search, projection,
                           {.object = section.id, .kind = marker_navigation_kind_t::section_begin,
                            .source_order = section.id.value, .audio_milliseconds = begin});
    if (search.filter.include_section_ends)
        consider_projected(search, projection,
                           {.object = section.id, .kind = marker_navigation_kind_t::section_end,
                            .source_order = section.id.value, .audio_milliseconds = end});
}

void collect_projected_audio_markers(marker_search_t<std::uint64_t>& search,
                                     audio_projection_t& projection,
                                     const chart_document_t& chart) {
    if (search.filter.include_bookmarks)
        for (const auto& bookmark : chart.bookmarks)
            project_audio_bookmark(search, projection, bookmark);
    for (const auto& section : chart.sections) project_audio_section(search, projection, section);
}

auto validate_selected_marker(const chart_document_t& chart, const marker_navigation_item_t& item,
                              marker_navigation_result_t& result) -> bool {
    const auto count = chart_object_id_count(chart, item.object);
    if (count == 0) {
        reject(result.diagnostics, diagnostic_code_t::invalid_id,
               "selected marker ID is not present in the chart object index", item.object);
        return false;
    }
    if (count != 1) {
        reject(result.diagnostics, diagnostic_code_t::duplicate_id,
               "selected marker ID is ambiguous across chart objects", item.object);
        return false;
    }
    return true;
}

template <typename coordinate_t>
auto navigate_impl(const song_document_t& song, std::uint64_t expected_revision,
                   chart_id_t chart_id, coordinate_t cursor,
                   marker_navigation_direction_t direction, marker_navigation_filter_t filter,
                   marker_navigation_limits_t limits, marker_anchor_t anchor,
                   const measure_lookup_t* measures, audio_projection_t* projection = nullptr)
    -> marker_navigation_result_t {
    marker_navigation_result_t result{.document_revision = song.revision, .chart = chart_id};
    auto query = validate_chart_query(song, expected_revision, chart_id, limits);
    if (!query.diagnostics.empty()) {
        result.status = marker_navigation_status_t::invalid_request;
        result.diagnostics = std::move(query.diagnostics);
        return result;
    }
    if (!valid_direction(direction)) {
        result.status = marker_navigation_status_t::invalid_request;
        reject(result.diagnostics, diagnostic_code_t::invalid_id,
               "marker navigation direction is unknown");
        return result;
    }
    if (anchor == marker_anchor_t::beat && !measures) {
        result.status = marker_navigation_status_t::invalid_request;
        reject(result.diagnostics, diagnostic_code_t::invalid_measure,
               "beat navigation requires a measure index");
        return result;
    }
    marker_search_t<coordinate_t> search{cursor, direction, anchor, filter, measures,
                                         result.diagnostics, std::nullopt};
    collect_bookmarks(search, *query.chart);
    collect_sections(search, *query.chart);
    if (projection) {
        if constexpr (std::is_same_v<coordinate_t, std::uint64_t>)
            collect_projected_audio_markers(search, *projection, *query.chart);
    }
    if (!result.diagnostics.empty()) {
        result.status = marker_navigation_status_t::invalid_request;
        return result;
    }
    if (projection)
        result.diagnostics.insert(result.diagnostics.end(), projection->warnings.begin(),
                                  projection->warnings.end());
    if (!search.best) return result;
    if (!validate_selected_marker(*query.chart, search.best->item, result)) {
        result.status = marker_navigation_status_t::invalid_request;
        return result;
    }
    result.status = marker_navigation_status_t::found;
    result.item = std::move(search.best->item);
    return result;
}

} // namespace

auto navigate_markers(const song_document_t& song, std::uint64_t expected_revision,
                      chart_id_t chart, local_position_t beat_cursor,
                      marker_navigation_direction_t direction, marker_navigation_filter_t filter,
                      marker_navigation_limits_t limits, const timing_index_t* timing)
    -> marker_navigation_result_t {
    auto result = marker_navigation_result_t{.document_revision = song.revision, .chart = chart};
    auto query = validate_chart_query(song, expected_revision, chart, limits);
    if (!query.diagnostics.empty()) {
        result.status = marker_navigation_status_t::invalid_request;
        result.diagnostics = std::move(query.diagnostics);
        return result;
    }
    const auto built = build_measure_index(*query.chart);
    if (!std::holds_alternative<measure_index_t>(built)) {
        result.status = marker_navigation_status_t::invalid_request;
        result.diagnostics = std::get<diagnostic_list_t>(built);
        return result;
    }
    const auto& measures = std::get<measure_index_t>(built);
    const auto measure_lookup = build_measure_lookup(measures);
    const auto cursor_row =
        validate_position(measure_lookup, beat_cursor, std::nullopt, result.diagnostics);
    if (!cursor_row.has_value()) {
        result.status = marker_navigation_status_t::invalid_request;
        return result;
    }
    audio_projection_t projection{.timing = timing, .measures = &measures};
    return navigate_impl(song, expected_revision, chart, *cursor_row, direction, filter, limits,
                         marker_anchor_t::beat, &measure_lookup, timing ? &projection : nullptr);
}

auto navigate_markers(const song_document_t& song, std::uint64_t expected_revision,
                      chart_id_t chart, std::int64_t audio_millisecond_cursor,
                      marker_navigation_direction_t direction, marker_navigation_filter_t filter,
                      marker_navigation_limits_t limits) -> marker_navigation_result_t {
    return navigate_impl(song, expected_revision, chart, audio_millisecond_cursor, direction,
                         filter, limits, marker_anchor_t::audio_time, nullptr);
}

auto resolve_section_selection(const song_document_t& song, std::uint64_t expected_revision,
                               chart_id_t chart_id, object_id_t section_id,
                               marker_navigation_limits_t limits) -> section_selection_result_t {
    section_selection_result_t result{.status = section_selection_status_t::invalid_request,
                                      .document_revision = song.revision,
                                      .chart = chart_id,
                                      .section = section_id};
    auto query = validate_chart_query(song, expected_revision, chart_id, limits);
    if (!query.diagnostics.empty()) {
        result.diagnostics = std::move(query.diagnostics);
        return result;
    }
    if (!section_id) {
        reject(result.diagnostics, diagnostic_code_t::invalid_id,
               "section selection uses the reserved zero object ID");
        return result;
    }
    const auto found = std::ranges::find(query.chart->sections, section_id, &section_t::id);
    if (found == query.chart->sections.end()) {
        reject(result.diagnostics, diagnostic_code_t::invalid_id,
               "section selection refers to an unknown section", section_id);
        return result;
    }
    if (std::ranges::count(query.chart->sections, section_id, &section_t::id) != 1) {
        reject(result.diagnostics, diagnostic_code_t::duplicate_id,
               "section selection ID is ambiguous", section_id);
        return result;
    }
    if (chart_object_id_count(*query.chart, section_id) != 1) {
        reject(result.diagnostics, diagnostic_code_t::duplicate_id,
               "section selection ID is ambiguous across chart objects", section_id);
        return result;
    }

    if (found->anchor == marker_anchor_t::audio_time) {
        if (found->beat_span.has_value() || !found->audio_millisecond_span.has_value() ||
            found->audio_millisecond_span->second <= found->audio_millisecond_span->first) {
            reject(result.diagnostics, diagnostic_code_t::invalid_hold,
                   "audio-time section has an invalid selection span", section_id);
            return result;
        }
        result.status = section_selection_status_t::timing_projection_required;
        return result;
    }
    if (found->anchor != marker_anchor_t::beat || !found->beat_span.has_value() ||
        found->audio_millisecond_span.has_value()) {
        reject(result.diagnostics, diagnostic_code_t::invalid_id,
               "section has inconsistent anchor and span fields", section_id);
        return result;
    }

    const auto built = build_measure_index(*query.chart);
    if (!std::holds_alternative<measure_index_t>(built)) {
        result.diagnostics = std::get<diagnostic_list_t>(built);
        return result;
    }
    const auto& measures = std::get<measure_index_t>(built);
    const auto measure_lookup = build_measure_lookup(measures);
    const auto begin =
        validate_position(measure_lookup, found->beat_span->begin, section_id, result.diagnostics);
    const auto end =
        validate_position(measure_lookup, found->beat_span->end, section_id, result.diagnostics);
    if (!begin.has_value() || !end.has_value()) {
        return result;
    }
    if (*end <= *begin) {
        reject(result.diagnostics, diagnostic_code_t::invalid_hold,
               "beat-anchored section end must be after its start", section_id);
        return result;
    }
    result.status = section_selection_status_t::resolved;
    result.beat_span = found->beat_span;
    return result;
}

} // namespace wormedit

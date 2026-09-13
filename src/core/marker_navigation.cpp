#include <wormedit/core/marker_navigation.h>

#include <algorithm>
#include <ranges>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>

namespace wormedit {
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
auto navigate_impl(const song_document_t& song, std::uint64_t expected_revision,
                   chart_id_t chart_id, coordinate_t cursor,
                   marker_navigation_direction_t direction, marker_navigation_filter_t filter,
                   marker_navigation_limits_t limits, marker_anchor_t anchor,
                   const measure_lookup_t* measures) -> marker_navigation_result_t {
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
    if (anchor == marker_anchor_t::beat && measures == nullptr) {
        result.status = marker_navigation_status_t::invalid_request;
        reject(result.diagnostics, diagnostic_code_t::invalid_measure,
               "beat navigation requires a measure index");
        return result;
    }

    std::optional<candidate_t<coordinate_t>> best;
    const auto consider = [&](coordinate_t coordinate, marker_navigation_item_t item) {
        if ((direction == marker_navigation_direction_t::next && coordinate <= cursor) ||
            (direction == marker_navigation_direction_t::previous && coordinate >= cursor)) {
            return;
        }
        candidate_t<coordinate_t> candidate{.coordinate = coordinate, .item = std::move(item)};
        if (!best.has_value() ||
            (direction == marker_navigation_direction_t::next ? precedes(candidate, *best)
                                                              : precedes(*best, candidate))) {
            best = std::move(candidate);
        }
    };

    if (filter.include_bookmarks) {
        for (const auto& bookmark : query.chart->bookmarks) {
            if (!valid_anchor(bookmark.anchor)) {
                reject(result.diagnostics, diagnostic_code_t::invalid_id,
                       "bookmark has an unknown anchor kind", bookmark.id);
                continue;
            }
            if (bookmark.anchor != anchor) {
                continue;
            }
            if (!bookmark.id) {
                reject(result.diagnostics, diagnostic_code_t::invalid_id,
                       "bookmark uses the reserved zero object ID");
                continue;
            }
            if (anchor == marker_anchor_t::beat) {
                if (!bookmark.position.has_value() || bookmark.audio_milliseconds.has_value()) {
                    reject(result.diagnostics, diagnostic_code_t::invalid_id,
                           "beat-anchored bookmark has inconsistent position fields", bookmark.id);
                    continue;
                }
                const auto row = validate_position(*measures, *bookmark.position, bookmark.id,
                                                   result.diagnostics);
                if (row.has_value()) {
                    consider(*row, {.object = bookmark.id,
                                    .kind = marker_navigation_kind_t::bookmark,
                                    .source_order = bookmark.id.value,
                                    .beat_position = bookmark.position});
                }
            } else {
                if (bookmark.position.has_value() || !bookmark.audio_milliseconds.has_value()) {
                    reject(result.diagnostics, diagnostic_code_t::invalid_id,
                           "audio-time bookmark has inconsistent position fields", bookmark.id);
                    continue;
                }
                consider(*bookmark.audio_milliseconds,
                         {.object = bookmark.id,
                          .kind = marker_navigation_kind_t::bookmark,
                          .source_order = bookmark.id.value,
                          .audio_milliseconds = bookmark.audio_milliseconds});
            }
        }
    }

    if (filter.include_section_begins || filter.include_section_ends) {
        for (const auto& section : query.chart->sections) {
            if (!valid_anchor(section.anchor)) {
                reject(result.diagnostics, diagnostic_code_t::invalid_id,
                       "section has an unknown anchor kind", section.id);
                continue;
            }
            if (section.anchor != anchor) {
                continue;
            }
            if (!section.id) {
                reject(result.diagnostics, diagnostic_code_t::invalid_id,
                       "section uses the reserved zero object ID");
                continue;
            }
            if (anchor == marker_anchor_t::beat) {
                if (!section.beat_span.has_value() || section.audio_millisecond_span.has_value()) {
                    reject(result.diagnostics, diagnostic_code_t::invalid_id,
                           "beat-anchored section has inconsistent span fields", section.id);
                    continue;
                }
                const auto begin = validate_position(*measures, section.beat_span->begin,
                                                     section.id, result.diagnostics);
                const auto end = validate_position(*measures, section.beat_span->end, section.id,
                                                   result.diagnostics);
                if (!begin.has_value() || !end.has_value()) {
                    continue;
                }
                if (*end <= *begin) {
                    reject(result.diagnostics, diagnostic_code_t::invalid_hold,
                           "beat-anchored section end must be after its start", section.id);
                    continue;
                }
                if (filter.include_section_begins) {
                    consider(*begin, {.object = section.id,
                                      .kind = marker_navigation_kind_t::section_begin,
                                      .source_order = section.id.value,
                                      .beat_position = section.beat_span->begin});
                }
                if (filter.include_section_ends) {
                    consider(*end, {.object = section.id,
                                    .kind = marker_navigation_kind_t::section_end,
                                    .source_order = section.id.value,
                                    .beat_position = section.beat_span->end});
                }
            } else {
                if (section.beat_span.has_value() || !section.audio_millisecond_span.has_value()) {
                    reject(result.diagnostics, diagnostic_code_t::invalid_id,
                           "audio-time section has inconsistent span fields", section.id);
                    continue;
                }
                const auto [begin, end] = *section.audio_millisecond_span;
                if (end <= begin) {
                    reject(result.diagnostics, diagnostic_code_t::invalid_hold,
                           "audio-time section end must be after its start", section.id);
                    continue;
                }
                if (filter.include_section_begins) {
                    consider(begin, {.object = section.id,
                                     .kind = marker_navigation_kind_t::section_begin,
                                     .source_order = section.id.value,
                                     .audio_milliseconds = begin});
                }
                if (filter.include_section_ends) {
                    consider(end, {.object = section.id,
                                   .kind = marker_navigation_kind_t::section_end,
                                   .source_order = section.id.value,
                                   .audio_milliseconds = end});
                }
            }
        }
    }

    if (!result.diagnostics.empty()) {
        result.status = marker_navigation_status_t::invalid_request;
        result.item.reset();
        return result;
    }
    if (best.has_value()) {
        const auto id_count = chart_object_id_count(*query.chart, best->item.object);
        if (id_count == 0) {
            result.status = marker_navigation_status_t::invalid_request;
            reject(result.diagnostics, diagnostic_code_t::invalid_id,
                   "selected marker ID is not present in the chart object index",
                   best->item.object);
            return result;
        }
        if (id_count != 1) {
            result.status = marker_navigation_status_t::invalid_request;
            reject(result.diagnostics, diagnostic_code_t::duplicate_id,
                   "selected marker ID is ambiguous across chart objects", best->item.object);
            return result;
        }
        result.status = marker_navigation_status_t::found;
        result.item = std::move(best->item);
    }
    return result;
}

} // namespace

auto navigate_markers(const song_document_t& song, std::uint64_t expected_revision,
                      chart_id_t chart, local_position_t beat_cursor,
                      marker_navigation_direction_t direction, marker_navigation_filter_t filter,
                      marker_navigation_limits_t limits) -> marker_navigation_result_t {
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
    return navigate_impl(song, expected_revision, chart, *cursor_row, direction, filter, limits,
                         marker_anchor_t::beat, &measure_lookup);
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

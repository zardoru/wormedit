#include <wormedit/core/interaction.h>

#include <wormedit/core/virtual_canvas.h>

#include <algorithm>
#include <cmath>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace wormedit {
namespace {

auto error(diagnostic_code_t code, std::string message,
           std::optional<object_id_t> object = std::nullopt) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error,
            .code = code,
            .message = std::move(message),
            .object = object};
}

auto position_at(const measure_index_t& index, std::uint64_t absolute)
    -> std::optional<local_position_t> {
    if (absolute >= index.total_rows)
        return std::nullopt;
    const auto after = std::ranges::upper_bound(index.entries, absolute, {},
                                                &measure_index_entry_t::absolute_begin_row);
    if (after == index.entries.begin())
        return std::nullopt;
    const auto entry = std::prev(after);
    return local_position_t{.measure = entry->id, .row = absolute - entry->absolute_begin_row};
}

auto editable_lane(const lane_definition_t& lane) -> bool {
    return lane.role != lane_role_t::autoplay && lane.role != lane_role_t::invisible &&
           lane.role != lane_role_t::mine;
}

auto effective_layout(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t position) -> const lane_layout_t* {
    const auto row = absolute_row(index, position);
    if (!row)
        return nullptr;
    const lane_layout_segment_t* selected{};
    std::uint64_t selected_row{};
    for (const auto& segment : chart.lane_layouts) {
        const auto candidate = absolute_row(index, segment.position);
        if (candidate && *candidate <= *row && (selected == nullptr || *candidate >= selected_row)) {
            selected = &segment;
            selected_row = *candidate;
        }
    }
    return selected == nullptr ? nullptr : &selected->layout;
}

auto find_active_lane(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t position, lane_id_t id) -> const lane_definition_t* {
    const auto* layout = effective_layout(chart, index, position);
    if (!layout)
        return nullptr;
    const auto lane = std::ranges::find_if(layout->lanes, [id](const auto& value) {
        return value.id == id && editable_lane(value);
    });
    return lane == layout->lanes.end() ? nullptr : &*lane;
}

auto scope_accepts(editor_selection_scope_t scope, editor_selection_object_kind_t kind) -> bool {
    switch (kind) {
    case editor_selection_object_kind_t::note: return scope.notes;
    case editor_selection_object_kind_t::bgm_event: return scope.bgm;
    case editor_selection_object_kind_t::bga_event: return scope.bga;
    case editor_selection_object_kind_t::bpm_event:
    case editor_selection_object_kind_t::stop_event:
    case editor_selection_object_kind_t::delay_event:
    case editor_selection_object_kind_t::warp_event:
    case editor_selection_object_kind_t::scroll_event:
    case editor_selection_object_kind_t::speed_event:
    case editor_selection_object_kind_t::kiai_event:
    case editor_selection_object_kind_t::timing_anchor:
    case editor_selection_object_kind_t::bookmark:
    case editor_selection_object_kind_t::section: return scope.timing;
    }
    return false;
}

auto maximum_viewport_scroll(const chart_document_t& chart, const measure_index_t& index,
                             double scale, double viewport_height) -> std::optional<double> {
    auto total_rows = index.total_rows;
    if (chart.source_family == format_family_t::bms) {
        const auto canvas = build_virtual_canvas(chart);
        const auto* built = std::get_if<virtual_canvas_t>(&canvas);
        if (!built)
            return std::nullopt;
        total_rows = built->total_rows;
    }
    const auto content_height = static_cast<double>(total_rows) * scale /
                                static_cast<double>(chart.divisions_per_standard_measure);
    if (!std::isfinite(content_height) || content_height < 0.0 || !std::isfinite(viewport_height) ||
        viewport_height <= 0.0)
        return std::nullopt;
    return editor_maximum_viewport_scroll(content_height, viewport_height);
}

auto minimum_viewport_scroll(double viewport_height) -> double {
    return -viewport_height * editor_cursor_edge_margin_fraction;
}

} // namespace

namespace {

struct area_selection_bounds_t {
    double left{};
    double right{};
    double field_left{};
    double field_right{};
    double top{};
    double bottom{};
    long double minimum_row{};
    long double maximum_row{};
    std::uint64_t query_minimum{};
    std::uint64_t query_maximum{};
    std::size_t candidate_count{};
    bool empty{};
};

struct area_layout_segment_t {
    std::uint64_t row{};
    const lane_layout_t* layout{};
};

using lane_bounds_t = std::pair<double, double>;

struct note_area_query_t {
    const chart_document_t& chart;
    const editor_hit_test_index_t& index;
    const editor_interaction_view_model_t& view;
    area_selection_bounds_t bounds;
    std::vector<area_layout_segment_t> layouts;
    std::unordered_map<const lane_layout_t*,
                       std::unordered_map<std::uint64_t, lane_bounds_t>> lane_bounds;

    auto prepare_layouts() -> void {
        layouts.reserve(chart.lane_layouts.size());
        for (const auto& segment : chart.lane_layouts) {
            const auto row = absolute_row(index.measures, segment.position);
            if (row) layouts.push_back({*row, &segment.layout});
        }
        std::ranges::stable_sort(layouts, {}, &area_layout_segment_t::row);
        std::vector<area_layout_segment_t> unique;
        unique.reserve(layouts.size());
        for (const auto& segment : layouts) {
            if (!unique.empty() && unique.back().row == segment.row)
                unique.back() = segment;
            else
                unique.push_back(segment);
        }
        layouts = std::move(unique);
    }

    auto layout_at(long double row) const -> const lane_layout_t* {
        const auto after = std::ranges::upper_bound(layouts, row, {}, &area_layout_segment_t::row);
        return after == layouts.begin() ? nullptr : std::prev(after)->layout;
    }

    auto lane_intersects(const lane_layout_t* layout, lane_id_t lane) -> bool {
        if (!layout) return false;
        auto cached = lane_bounds.find(layout);
        if (cached == lane_bounds.end()) {
            std::vector<const lane_definition_t*> editable;
            editable.reserve(layout->lanes.size());
            for (const auto& candidate : layout->lanes)
                if (editable_lane(candidate)) editable.push_back(&candidate);
            std::ranges::sort(editable, [](const auto* a, const auto* b) {
                if (a->display_order != b->display_order) return a->display_order < b->display_order;
                return a->id.value < b->id.value;
            });
            std::unordered_map<std::uint64_t, lane_bounds_t> columns;
            columns.reserve(editable.size());
            const auto width = bounds.field_right - bounds.field_left;
            const auto lane_width = width / static_cast<double>(editable.size());
            for (std::size_t i = 0; i < editable.size(); ++i) {
                const auto left = bounds.field_left + static_cast<double>(i) * lane_width;
                columns.emplace(editable[i]->id.value, lane_bounds_t{left, left + lane_width});
            }
            cached = lane_bounds.emplace(layout, std::move(columns)).first;
        }
        const auto found = cached->second.find(lane.value);
        if (found == cached->second.end()) return false;
        const auto [left, right] = found->second;
        return bounds.left == bounds.right ? bounds.left >= left && bounds.left < right
                                           : bounds.left < right && bounds.right > left;
    }

    auto point_intersects(lane_id_t lane, std::uint64_t row) -> bool {
        const auto y = static_cast<long double>(row) *
            static_cast<long double>(view.pixels_per_standard_measure) /
            static_cast<long double>(chart.divisions_per_standard_measure) -
            static_cast<long double>(view.viewport_scroll_pixels);
        return y >= static_cast<long double>(bounds.top) &&
               y <= static_cast<long double>(bounds.bottom) &&
               lane_intersects(layout_at(static_cast<long double>(row)), lane);
    }

    auto body_intersects(lane_id_t lane, std::uint64_t begin_row,
                         std::uint64_t end_row) -> bool {
        auto begin = std::max(bounds.minimum_row, static_cast<long double>(begin_row));
        auto end = std::min(bounds.maximum_row, static_cast<long double>(end_row));
        if (begin > end || begin_row == end_row || layouts.empty()) return false;
        if (begin == end)
            return begin > static_cast<long double>(begin_row) &&
                   begin < static_cast<long double>(end_row) &&
                   lane_intersects(layout_at(begin), lane);
        auto after = std::ranges::upper_bound(layouts, begin, {}, &area_layout_segment_t::row);
        if (after == layouts.begin()) return false;
        auto segment = std::prev(after);
        while (segment != layouts.end()) {
            const auto next = std::next(segment);
            const auto segment_begin = std::max(begin, static_cast<long double>(segment->row));
            const auto segment_end = next == layouts.end()
                ? end : std::min(end, static_cast<long double>(next->row));
            if (segment_begin < segment_end && lane_intersects(segment->layout, lane)) return true;
            if (next == layouts.end() || static_cast<long double>(next->row) >= end) break;
            segment = next;
        }
        return false;
    }
};

auto valid_area_note_request(const chart_document_t& chart, const editor_hit_test_index_t& index,
                             const editor_interaction_view_model_t& view,
                             const editor_area_selection_request_t& request) -> bool {
    return index.chart == chart.id && view.chart == chart.id &&
        index.document_revision == view.document_revision &&
        index.measure_buckets.size() == index.measures.entries.size() &&
        index.note_span_tree_leaf_base != 0 &&
        index.note_span_tree_leaf_base >= index.note_spans.size() &&
        index.note_span_tree_leaf_base <= std::numeric_limits<std::size_t>::max() / 2 &&
        index.note_span_tree_max_end.size() == index.note_span_tree_leaf_base * 2 &&
        std::isfinite(request.start_x_pixels) && std::isfinite(request.start_y_pixels) &&
        std::isfinite(request.end_x_pixels) && std::isfinite(request.end_y_pixels) &&
        std::isfinite(request.field_left_pixels) && std::isfinite(request.field_width_pixels) &&
        request.field_width_pixels > 0.0 && std::isfinite(view.pixels_per_standard_measure) &&
        view.pixels_per_standard_measure >= minimum_pixels_per_standard_measure &&
        view.pixels_per_standard_measure <= maximum_pixels_per_standard_measure &&
        std::isfinite(view.viewport_scroll_pixels) && std::isfinite(view.viewport_height_pixels) &&
        view.viewport_height_pixels > 0.0 && chart.divisions_per_standard_measure != 0 &&
        index.measures.total_rows != 0 &&
        std::isfinite(request.field_left_pixels + request.field_width_pixels);
}

auto make_area_bounds(const chart_document_t& chart, const editor_hit_test_index_t& index,
                      const editor_interaction_view_model_t& view,
                      const editor_area_selection_request_t& request) -> area_selection_bounds_t {
    area_selection_bounds_t result;
    result.field_left = request.field_left_pixels;
    result.field_right = request.field_left_pixels + request.field_width_pixels;
    result.left = std::max(std::min(request.start_x_pixels, request.end_x_pixels),
                           request.field_left_pixels);
    result.right = std::min(std::max(request.start_x_pixels, request.end_x_pixels),
                            result.field_right);
    result.top = std::max(0.0, std::min(request.start_y_pixels, request.end_y_pixels));
    result.bottom = std::min(view.viewport_height_pixels,
                             std::max(request.start_y_pixels, request.end_y_pixels));
    if (result.left > result.right || result.top > result.bottom) {
        result.empty = true;
        return result;
    }
    const auto row_at = [&](double y) {
        return (static_cast<long double>(y) + view.viewport_scroll_pixels) *
               static_cast<long double>(chart.divisions_per_standard_measure) /
               static_cast<long double>(view.pixels_per_standard_measure);
    };
    result.minimum_row = std::max(row_at(result.top), 0.0L);
    result.maximum_row = std::min(row_at(result.bottom),
        static_cast<long double>(index.measures.total_rows - 1));
    if (result.minimum_row > result.maximum_row) {
        result.empty = true;
        return result;
    }
    result.query_minimum = static_cast<std::uint64_t>(std::floor(result.minimum_row));
    result.query_maximum = static_cast<std::uint64_t>(std::ceil(result.maximum_row));
    const auto after = std::ranges::upper_bound(index.note_spans, result.query_maximum, {},
                                                &editor_note_span_entry_t::begin_row);
    result.candidate_count = static_cast<std::size_t>(
        std::distance(index.note_spans.begin(), after));
    return result;
}

auto collect_area_note_spans(note_area_query_t& query, std::vector<object_id_t>& selected,
                             std::size_t node, std::size_t first, std::size_t last) -> void {
    const auto& index = query.index;
    const auto& bounds = query.bounds;
    if (first >= bounds.candidate_count || index.note_span_tree_max_end[node] < bounds.query_minimum)
        return;
    if (last - first == 1) {
        if (first >= index.note_spans.size()) return;
        const auto& span = index.note_spans[first];
        if (query.point_intersects(span.lane, span.begin_row) ||
            query.point_intersects(span.lane, span.end_row) ||
            query.body_intersects(span.lane, span.begin_row, span.end_row))
            selected.push_back(span.note);
        return;
    }
    const auto middle = first + (last - first) / 2;
    collect_area_note_spans(query, selected, node * 2, first, middle);
    collect_area_note_spans(query, selected, node * 2 + 1, middle, last);
}

} // namespace

auto editor_notes_in_area_selection(const chart_document_t& chart,
                                    const editor_hit_test_index_t& index,
                                    const editor_interaction_view_model_t& view,
                                    const editor_area_selection_request_t& request)
    -> std::variant<std::vector<object_id_t>, diagnostic_list_t> {
    if (!valid_area_note_request(chart, index, view, request))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "area selection geometry or hit-test index is invalid")};
    auto query = note_area_query_t{.chart = chart, .index = index, .view = view,
                                   .bounds = make_area_bounds(chart, index, view, request),
                                   .layouts = {}, .lane_bounds = {}};
    if (query.bounds.empty) return std::vector<object_id_t>{};
    query.prepare_layouts();
    std::vector<object_id_t> selected;
    if (query.bounds.candidate_count != 0)
        collect_area_note_spans(query, selected, 1, 0, index.note_span_tree_leaf_base);
    std::ranges::sort(selected, {}, &object_id_t::value);
    return selected;
}

namespace {

auto valid_row_selection_index(const chart_document_t& chart,
                               const editor_hit_test_index_t& index,
                               std::uint64_t first_row, std::uint64_t last_row) -> bool {
    return index.chart == chart.id && index.measures.total_rows != 0 && first_row <= last_row &&
        index.selection_span_tree_leaf_base != 0 &&
        index.selection_span_tree_leaf_base >= index.selection_spans.size() &&
        index.selection_span_tree_leaf_base <= std::numeric_limits<std::size_t>::max() / 2 &&
        index.selection_span_tree_max_end.size() == index.selection_span_tree_leaf_base * 2;
}

auto validate_row_canvas(const chart_document_t& chart, std::uint64_t last_row)
    -> std::optional<diagnostic_list_t> {
    if (chart.source_family != format_family_t::bms)
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "selection row interval is outside the chart")};
    const auto canvas = build_virtual_canvas(chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&canvas)) return *diagnostics;
    if (last_row >= std::get<virtual_canvas_t>(canvas).total_rows)
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "selection row interval is outside the BMS canvas")};
    return std::nullopt;
}

auto collect_row_selection(const chart_document_t& chart, const editor_hit_test_index_t& index,
                           editor_selection_scope_t scope, std::size_t candidate_count,
                           std::uint64_t first_row, std::uint64_t last_row,
                           std::vector<editor_selection_object_t>& selected,
                           std::size_t node, std::size_t first, std::size_t last) -> void {
    if (first >= candidate_count || index.selection_span_tree_max_end[node] < first_row) return;
    if (last - first == 1) {
        if (first >= index.selection_spans.size()) return;
        const auto& span = index.selection_spans[first];
        if (span.begin_row > last_row || span.end_row < first_row ||
            !scope_accepts(scope, span.object.kind)) return;
        if (span.object.kind == editor_selection_object_kind_t::note) {
            const auto position = position_at(index.measures, span.begin_row);
            if (!position || !span.lane ||
                find_active_lane(chart, index.measures, *position, *span.lane) == nullptr) return;
        }
        selected.push_back(span.object);
        return;
    }
    const auto middle = first + (last - first) / 2;
    collect_row_selection(chart, index, scope, candidate_count, first_row, last_row,
                          selected, node * 2, first, middle);
    collect_row_selection(chart, index, scope, candidate_count, first_row, last_row,
                          selected, node * 2 + 1, middle, last);
}

} // namespace

auto editor_objects_in_row_range(const chart_document_t& chart,
                                 const editor_hit_test_index_t& index, std::uint64_t first_row,
                                 std::uint64_t last_row, editor_selection_scope_t scope)
    -> std::variant<std::vector<editor_selection_object_t>, diagnostic_list_t> {
    if (!valid_row_selection_index(chart, index, first_row, last_row))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "selection index or row interval is invalid")};
    if (last_row >= index.measures.total_rows) {
        if (auto failure = validate_row_canvas(chart, last_row)) return *failure;
    }
    if (first_row >= index.measures.total_rows)
        return std::vector<editor_selection_object_t>{};
    last_row = std::min(last_row, index.measures.total_rows - 1);
    const auto after = std::ranges::upper_bound(index.selection_spans, last_row, {},
                                                &editor_selection_span_entry_t::begin_row);
    const auto count = static_cast<std::size_t>(
        std::distance(index.selection_spans.begin(), after));
    std::vector<editor_selection_object_t> selected;
    if (count != 0)
        collect_row_selection(chart, index, scope, count, first_row, last_row, selected,
                              1, 0, index.selection_span_tree_leaf_base);
    std::ranges::sort(selected);
    return selected;
}

namespace {

auto valid_non_note_area(const chart_document_t& chart, const editor_hit_test_index_t& index,
                         const editor_interaction_view_model_t& view,
                         const editor_area_selection_request_t& request,
                         std::optional<double> maximum_scroll) -> bool {
    return index.chart == chart.id && view.chart == chart.id &&
        index.document_revision == view.document_revision &&
        std::isfinite(request.start_y_pixels) && std::isfinite(request.end_y_pixels) &&
        std::isfinite(view.pixels_per_standard_measure) &&
        view.pixels_per_standard_measure >= minimum_pixels_per_standard_measure &&
        view.pixels_per_standard_measure <= maximum_pixels_per_standard_measure &&
        std::isfinite(view.viewport_scroll_pixels) && std::isfinite(view.viewport_height_pixels) &&
        view.viewport_height_pixels > 0.0 && maximum_scroll &&
        view.viewport_scroll_pixels >= minimum_viewport_scroll(view.viewport_height_pixels) &&
        view.viewport_scroll_pixels <= *maximum_scroll &&
        chart.divisions_per_standard_measure != 0 && index.measures.total_rows != 0 &&
        index.selection_span_tree_leaf_base != 0 &&
        index.selection_span_tree_leaf_base >= index.selection_spans.size() &&
        index.selection_span_tree_leaf_base <= std::numeric_limits<std::size_t>::max() / 2 &&
        index.selection_span_tree_max_end.size() == index.selection_span_tree_leaf_base * 2;
}

auto collect_float_selection(const editor_hit_test_index_t& index,
                             editor_selection_scope_t scope, long double low, long double high,
                             std::size_t candidate_count,
                             std::vector<editor_selection_object_t>& selected,
                             std::size_t node, std::size_t first, std::size_t last) -> void {
    if (first >= candidate_count ||
        static_cast<long double>(index.selection_span_tree_max_end[node]) < low) return;
    if (last - first == 1) {
        if (first >= index.selection_spans.size()) return;
        const auto& span = index.selection_spans[first];
        if (static_cast<long double>(span.begin_row) > high ||
            static_cast<long double>(span.end_row) < low ||
            !scope_accepts(scope, span.object.kind)) return;
        selected.push_back(span.object);
        return;
    }
    const auto middle = first + (last - first) / 2;
    collect_float_selection(index, scope, low, high, candidate_count, selected,
                            node * 2, first, middle);
    collect_float_selection(index, scope, low, high, candidate_count, selected,
                            node * 2 + 1, middle, last);
}

auto append_note_area_selection(const chart_document_t& chart,
                                const editor_hit_test_index_t& index,
                                const editor_interaction_view_model_t& view,
                                const editor_area_selection_request_t& request,
                                std::vector<editor_selection_object_t>& selected)
    -> std::optional<diagnostic_list_t> {
    if (!request.scope.notes) return std::nullopt;
    auto notes = editor_notes_in_area_selection(chart, index, view, request);
    if (auto* diagnostics = std::get_if<diagnostic_list_t>(&notes)) return *diagnostics;
    for (const auto id : std::get<std::vector<object_id_t>>(notes))
        selected.push_back({editor_selection_object_kind_t::note, id});
    return std::nullopt;
}

auto valid_area_object_geometry(const editor_area_object_geometry_t& item) -> bool {
    return item.object.kind != editor_selection_object_kind_t::note && item.object.id.value != 0 &&
        std::isfinite(item.left_pixels) && std::isfinite(item.right_pixels) &&
        std::isfinite(item.top_pixels) && std::isfinite(item.bottom_pixels) &&
        item.left_pixels <= item.right_pixels && item.top_pixels <= item.bottom_pixels;
}

auto valid_area_geometry_projection(const editor_area_geometry_snapshot_t& snapshot,
                                    const editor_interaction_view_model_t& view) -> bool {
    if (!snapshot.projection) return true;
    const auto& projection = *snapshot.projection;
    return std::isfinite(projection.viewport_scroll_pixels) &&
        projection.pixels_per_standard_measure == view.pixels_per_standard_measure &&
        projection.viewport_height_pixels == view.viewport_height_pixels;
}

auto append_projected_area_selection(const chart_document_t& chart,
    const editor_interaction_view_model_t& view, const editor_area_selection_request_t& request,
    std::vector<editor_selection_object_t>& selected) -> std::optional<diagnostic_list_t> {
    const auto& snapshot = *request.object_geometry;
    if (snapshot.chart != chart.id || snapshot.document_revision != view.document_revision ||
        snapshot.objects.size() > 200000 || !valid_area_geometry_projection(snapshot, view) ||
        !std::isfinite(request.start_x_pixels) || !std::isfinite(request.end_x_pixels) ||
        !std::isfinite(request.start_y_pixels) || !std::isfinite(request.end_y_pixels))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "projected selection geometry is stale or invalid")};
    const auto left = std::min(request.start_x_pixels, request.end_x_pixels);
    const auto right = std::max(request.start_x_pixels, request.end_x_pixels);
    const auto scroll_delta = snapshot.projection
        ? view.viewport_scroll_pixels - snapshot.projection->viewport_scroll_pixels : 0.0;
    const auto top = std::min(request.start_y_pixels, request.end_y_pixels) + scroll_delta;
    const auto bottom = std::max(request.start_y_pixels, request.end_y_pixels) + scroll_delta;
    for (const auto& item : snapshot.objects) {
        if (!valid_area_object_geometry(item))
            return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                           "projected selection rectangle is invalid")};
        if (scope_accepts(request.scope, item.object.kind) &&
            item.left_pixels <= right && item.right_pixels >= left &&
            item.top_pixels <= bottom && item.bottom_pixels >= top)
            selected.push_back(item.object);
    }
    return std::nullopt;
}

auto append_non_note_area_selection(const chart_document_t& chart,
                                    const editor_hit_test_index_t& index,
                                    const editor_interaction_view_model_t& view,
                                    const editor_area_selection_request_t& request,
                                    std::vector<editor_selection_object_t>& selected)
    -> std::optional<diagnostic_list_t> {
    if (request.object_geometry)
        return append_projected_area_selection(chart, view, request, selected);
    if (!(request.scope.bgm || request.scope.timing || request.scope.bga)) return std::nullopt;
    const auto maximum_scroll = maximum_viewport_scroll(
        chart, index.measures, view.pixels_per_standard_measure, view.viewport_height_pixels);
    if (!valid_non_note_area(chart, index, view, request, maximum_scroll))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "area selection geometry or index is invalid")};
    const auto top = std::max(0.0, std::min(request.start_y_pixels, request.end_y_pixels));
    const auto bottom = std::min(view.viewport_height_pixels,
                                 std::max(request.start_y_pixels, request.end_y_pixels));
    if (top > bottom) return std::nullopt;
    const auto row_at = [&](double y) {
        return (static_cast<long double>(y) + view.viewport_scroll_pixels) *
               static_cast<long double>(chart.divisions_per_standard_measure) /
               static_cast<long double>(view.pixels_per_standard_measure);
    };
    const auto max_row = static_cast<long double>(index.measures.total_rows - 1);
    const auto low = std::max(row_at(top), 0.0L);
    const auto high = std::min(row_at(bottom), max_row);
    const editor_selection_scope_t scope{.notes = false, .bgm = request.scope.bgm,
        .timing = request.scope.timing, .bga = request.scope.bga};
    const auto after = std::ranges::upper_bound(index.selection_spans, high,
        [](long double row, std::uint64_t begin_row) {
            return row < static_cast<long double>(begin_row);
        }, &editor_selection_span_entry_t::begin_row);
    const auto count = static_cast<std::size_t>(
        std::distance(index.selection_spans.begin(), after));
    if (count != 0 && low <= high)
        collect_float_selection(index, scope, low, high, count, selected,
                                 1, 0, index.selection_span_tree_leaf_base);
    return std::nullopt;
}

} // namespace

auto editor_objects_in_area_selection(const chart_document_t& chart,
                                      const editor_hit_test_index_t& index,
                                      const editor_interaction_view_model_t& view,
                                      const editor_area_selection_request_t& request)
    -> std::variant<std::vector<editor_selection_object_t>, diagnostic_list_t> {
    std::vector<editor_selection_object_t> selected;
    if (auto failure = append_note_area_selection(chart, index, view, request, selected))
        return *failure;
    if (auto failure = append_non_note_area_selection(chart, index, view, request, selected))
        return *failure;
    std::ranges::sort(selected);
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    return selected;
}



} // namespace wormedit

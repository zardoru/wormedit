#include <wormedit/core/rational.h>
#include "interaction_geometry.h"
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/virtual_canvas.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit {
namespace {

auto layout_at_row(const editor_field_scene_index_t& index, std::uint64_t row)
    -> const editor_field_scene_layout_source_t* {
    const auto after = std::ranges::upper_bound(index.layouts, row, {},
                                                &editor_field_scene_layout_source_t::begin_row);
    if (after == index.layouts.begin()) {
        return nullptr;
    }
    const auto found = std::prev(after);
    return row < found->end_row ? &*found : nullptr;
}

auto has_lane(const editor_field_scene_layout_source_t* layout, lane_id_t lane) -> bool {
    return layout != nullptr && std::ranges::any_of(layout->layout.lanes, [&](const auto& entry) {
               return entry.id == lane && entry.is_editable();
           });
}

auto is_finite_nonnegative(double value) -> bool {
    return std::isfinite(value) && value >= 0.0;
}

auto is_viewport_scroll_in_range(double scroll, double height, long double maximum,
                              double minimum_fraction) -> bool {
    return std::isfinite(scroll) && scroll >= -height * minimum_fraction &&
           static_cast<long double>(scroll) <= maximum;
}

auto ordered_vertical_bounds(double first, double second) -> std::pair<double, double> {
    return std::minmax(first, second);
}

auto endpoint_vertical_bounds(double snap_y, double height, lane_glyph_t glyph,
                              editor_field_scroll_direction_t direction)
    -> std::pair<double, double> {
    return editor_note_head_bounds(snap_y, height, glyph,
        direction == editor_field_scroll_direction_t::down);
}

auto is_rect_intersecting_viewport(const editor_field_rect_t& rect, double height) -> bool {
    return rect.bottom >= 0.0 && rect.top <= height;
}

struct lane_bounds_t {
    lane_id_t lane;
    lane_glyph_t glyph{lane_glyph_t::bar};
    double left{};
    double right{};
};

struct visible_band_t {
    std::uint64_t begin_row{};
    std::uint64_t end_row{};
    long double clipped_begin_row{};
    long double clipped_end_row{};
    std::size_t scene_band{};
    const lane_layout_t* layout{};
    std::vector<lane_bounds_t> lanes;
};

auto lane_bounds(const visible_band_t& band, lane_id_t lane) -> const lane_bounds_t* {
    const auto found = std::ranges::find(band.lanes, lane, &lane_bounds_t::lane);
    return found == band.lanes.end() ? nullptr : &*found;
}

} // namespace

auto editor_field_scene_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

namespace {

auto append_scene_layouts(const chart_document_t& chart, editor_field_scene_index_t& index,
                          editor_field_scene_index_options_t options,
                          diagnostic_list_t& diagnostics) -> bool {
    if (chart.lane_layouts.size() > options.maximum_layout_segments) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                    "field-scene layout count exceeds its configured cap"));
        return false;
    }

    struct positioned_layout_t {
        std::uint64_t row{};
        lane_layout_t layout;
    };
    std::vector<positioned_layout_t> layouts;
    layouts.reserve(chart.lane_layouts.size());
    std::size_t lane_definition_count{};
    for (const auto& segment : chart.lane_layouts) {
        if (segment.layout.lanes.size() >
            options.maximum_lane_definitions - lane_definition_count) {
            diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "field-scene lane definition count exceeds its configured cap", segment.id));
            return false;
        }
        lane_definition_count += segment.layout.lanes.size();
        std::unordered_set<std::uint64_t> lane_ids;
        lane_ids.reserve(segment.layout.lanes.size());
        for (const auto& lane : segment.layout.lanes) {
            if (!lane.id || !lane_ids.insert(lane.id.value).second) {
                diagnostics.push_back(error(
                    diagnostic_code_t::duplicate_id,
                    "field-scene layout has a missing or duplicate lane identity", segment.id));
                break;
            }
        }
        const auto row = absolute_row(index.measures, segment.position);
        if (!row) {
            diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
                                        "field-scene layout starts outside chart geometry",
                                        segment.id));
            continue;
        }
        layouts.push_back({*row, segment.layout});
    }
    std::ranges::stable_sort(layouts, {}, &positioned_layout_t::row);
    for (std::size_t layout_index = 0; layout_index < layouts.size(); ++layout_index) {
        if (layout_index != 0 && layouts[layout_index - 1].row == layouts[layout_index].row) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_measure,
                                        "field-scene layouts cannot share a start row"));
            continue;
        }
        const auto end = layout_index + 1 < layouts.size() ? layouts[layout_index + 1].row
                                                           : index.measures.total_rows;
        index.layouts.push_back({.begin_row = layouts[layout_index].row,
                                 .end_row = end,
                                 .layout = std::move(layouts[layout_index].layout)});
    }

    return true;
}

struct scene_note_index_context_t {
    editor_field_scene_index_t& index;
    diagnostic_list_t& diagnostics;
    std::unordered_set<std::uint64_t>& note_ids;
};

template <typename ColorForNote>
auto append_scene_note(const measure_chunk_t& measure, const note_t& note,
                       scene_note_index_context_t& context, ColorForNote&& semantic_color_for_note)
    -> void {
    const auto begin = absolute_row(context.index.measures, note.position);
    const auto end = note.end ? absolute_row(context.index.measures, *note.end) : begin;
    if (!note.id || !context.note_ids.insert(note.id.value).second) {
        context.diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                                            "field-scene note identity is missing or duplicated",
                                            note.id));
        return;
    }
    if (!begin || !end || *end < *begin) {
        context.diagnostics.push_back(error(diagnostic_code_t::invalid_hold,
                                            "field-scene note span is outside chart geometry",
                                            note.id));
        return;
    }
    if (note.position.measure != measure.id) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_measure,
                  "field-scene note is stored outside its position's measure", note.id));
        return;
    }
    if (!has_lane(layout_at_row(context.index, *begin), note.lane) ||
        !has_lane(layout_at_row(context.index, *end), note.lane)) {
        context.diagnostics.push_back(error(diagnostic_code_t::unknown_lane,
                                            "field-scene note endpoint has no active lane",
                                            note.id));
        return;
    }
    context.index.notes.push_back({.note = note.id,
                                   .lane = note.lane,
                                   .sound = note.sound,
                                   .kind = note.kind,
                                   .begin_row = *begin,
                                   .end_row = *end,
                                   .semantic_color = semantic_color_for_note(note),
                                   .semantic_layers = note.semantic_layers});
}

auto append_scene_notes(const chart_document_t& chart, editor_field_scene_index_t& index,
                        editor_field_scene_index_options_t options, diagnostic_list_t& diagnostics)
    -> bool {
    std::size_t note_count{};
    for (const auto& measure : chart.measures) {
        if (measure.notes.size() > options.maximum_notes - note_count) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                        "field-scene note count exceeds its configured cap"));
            return false;
        }
        note_count += measure.notes.size();
    }
    index.notes.reserve(note_count);
    std::unordered_set<std::uint64_t> note_ids;
    note_ids.reserve(note_count);
    struct visible_palette_color_t {
        std::size_t order{};
        color_rgba_t color;
    };
    std::unordered_map<std::uint64_t, visible_palette_color_t> visible_palette_colors;
    visible_palette_colors.reserve(chart.semantic_layers.size());
    for (std::size_t order = 0; order < chart.semantic_layers.size(); ++order) {
        const auto& layer = chart.semantic_layers[order];
        if (layer.visible) {
            visible_palette_colors.emplace(layer.id.value,
                                           visible_palette_color_t{order, layer.color});
        }
    }
    const auto semantic_color_for_note = [&](const note_t& note) -> std::optional<color_rgba_t> {
        // Palette order is meaningful: the first visible layer is the note's
        // compact field color. This deliberately does not infer a blend from
        // multiple memberships, whose authoring semantics are still open.
        const visible_palette_color_t* chosen{};
        for (const auto layer : note.semantic_layers) {
            const auto found = visible_palette_colors.find(layer.value);
            if (found != visible_palette_colors.end() &&
                (!chosen || found->second.order < chosen->order)) {
                chosen = &found->second;
            }
        }
        return chosen ? std::optional{chosen->color} : std::nullopt;
    };
    scene_note_index_context_t context{index, diagnostics, note_ids};
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            append_scene_note(measure, note, context, semantic_color_for_note);
        }
    }
    return true;
}

struct scene_bgm_index_context_t {
    editor_field_scene_index_t& index;
    diagnostic_list_t& diagnostics;
    std::unordered_set<std::uint64_t>& bgm_event_ids;
    const std::unordered_set<std::uint64_t>& bgm_source_lanes;
    const std::unordered_map<std::uint64_t, bool>& bmson_restart_by_event;
    const std::unordered_map<std::uint64_t, color_rgba_t>& visible_semantic_colors;
};

auto append_scene_bgm_event(const measure_chunk_t& measure, const bgm_event_t& event,
                            scene_bgm_index_context_t& context) -> void {
    const auto row = absolute_row(context.index.measures, event.position);
    const auto end = event.end ? absolute_row(context.index.measures, *event.end) : row;
    if (!event.id) {
        context.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                            "field-scene BGM event has invalid source geometry",
                                            event.id));
        return;
    }
    if (!context.bgm_event_ids.insert(event.id.value).second) {
        context.diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                                            "field-scene BGM event identity is duplicated",
                                            event.id));
        return;
    }
    if (!event.source_lane || !context.bgm_source_lanes.contains(event.source_lane.value) || !row ||
        event.position.measure != measure.id || !end || *end < *row ||
        (event.end && *end == *row)) {
        context.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                            "field-scene BGM event has invalid source geometry",
                                            event.id));
        return;
    }
    const auto semantic_color = event.semantic_layers.empty()
                                    ? context.visible_semantic_colors.end()
                                    : context.visible_semantic_colors.find(
                                          event.semantic_layers.front().value);
    context.index.bgm_events.push_back(
        {.event = event.id,
         .source_lane = event.source_lane,
         .row = *row,
         .end_row = *end,
         .sound = event.sound,
         .bmson_restart = event.sound_channel_event ? [&]() -> std::optional<bool> {
             const auto found =
                 context.bmson_restart_by_event.find(event.sound_channel_event->value);
             return found == context.bmson_restart_by_event.end()
                        ? std::nullopt
                        : std::optional<bool>{found->second};
         }()
         : std::nullopt,
         .semantic_color = semantic_color == context.visible_semantic_colors.end()
                               ? std::nullopt
                               : std::optional{semantic_color->second},
         .semantic_layers = event.semantic_layers});
}

auto build_bgm_end_tree(editor_field_scene_index_t& index) -> void {
    while (index.bgm_tree_leaf_base < index.bgm_events.size())
        index.bgm_tree_leaf_base *= 2;
    index.bgm_tree_max_end.assign(index.bgm_tree_leaf_base * 2, 0);
    for (std::size_t event = 0; event < index.bgm_events.size(); ++event)
        index.bgm_tree_max_end[index.bgm_tree_leaf_base + event] = index.bgm_events[event].end_row;
    for (auto node = index.bgm_tree_leaf_base; node-- > 1;)
        index.bgm_tree_max_end[node] = std::max(index.bgm_tree_max_end[node * 2],
                                              index.bgm_tree_max_end[node * 2 + 1]);
}

auto visible_semantic_layer_colors(const chart_document_t& chart)
    -> std::unordered_map<std::uint64_t, color_rgba_t> {
    std::unordered_map<std::uint64_t, color_rgba_t> colors;
    colors.reserve(chart.semantic_layers.size());
    for (const auto& layer : chart.semantic_layers)
        if (layer.visible) colors.emplace(layer.id.value, layer.color);
    return colors;
}

auto append_scene_bgm_events(const chart_document_t& chart, editor_field_scene_index_t& index,
                             editor_field_scene_index_options_t options,
                             diagnostic_list_t& diagnostics) -> bool {
    std::size_t bgm_event_count{};
    for (const auto& measure : chart.measures) {
        if (measure.bgm_events.size() > options.maximum_bgm_events - bgm_event_count) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                        "field-scene BGM event count exceeds its configured cap"));
            return false;
        }
        bgm_event_count += measure.bgm_events.size();
    }
    index.bgm_events.reserve(bgm_event_count);
    std::unordered_map<std::uint64_t, bool> bmson_restart_by_event;
    if (chart.source_family == format_family_t::bmson) {
        for (const auto& channel : chart.sound_channels) {
            std::unordered_map<std::uint64_t, bool> restart_by_row;
            std::vector<std::pair<object_id_t, std::uint64_t>> channel_events;
            channel_events.reserve(channel.events.size());
            for (const auto& event : channel.events) {
                const auto row = absolute_row(index.measures, event.position);
                if (!row) {
                    diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
                                                "BMSON source event is outside chart geometry",
                                                event.id));
                    continue;
                }
                restart_by_row[*row] = restart_by_row[*row] || !event.continuation;
                channel_events.emplace_back(event.id, *row);
            }
            for (const auto& [id, row] : channel_events) {
                bmson_restart_by_event.emplace(id.value, restart_by_row[row]);
            }
        }
    }
    std::unordered_set<std::uint64_t> bgm_source_lanes;
    std::unordered_set<std::uint64_t> bgm_event_ids;
    const auto semantic_colors = visible_semantic_layer_colors(chart);
    bgm_source_lanes.reserve(chart.bgm_source_lanes.size());
    bgm_event_ids.reserve(bgm_event_count);
    for (const auto& lane : chart.bgm_source_lanes) {
        if (!lane.id || !bgm_source_lanes.insert(lane.id.value).second) {
            diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                                        "field-scene BGM source lane identity is invalid",
                                        lane.id));
        }
    }
    scene_bgm_index_context_t context{index, diagnostics, bgm_event_ids, bgm_source_lanes,
                                      bmson_restart_by_event, semantic_colors};
    for (const auto& measure : chart.measures) {
        for (const auto& event : measure.bgm_events) {
            append_scene_bgm_event(measure, event, context);
        }
    }
    return true;
}

auto append_scene_bga_events(const chart_document_t& chart, editor_field_scene_index_t& index,
                            editor_field_scene_index_options_t options,
                            diagnostic_list_t& diagnostics) -> bool {
    std::unordered_set<std::uint64_t> ids;
    ids.reserve(chart.bga_events.size());
    std::size_t supported_count{};
    for (const auto& event : chart.bga_events) {
        if (!event.id || !ids.insert(event.id.value).second) {
            diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                                        "field-scene BGA event identity is invalid", event.id));
            continue;
        }
        if (!editor_bga_lane_index(event.kind)) continue;
        if (supported_count == options.maximum_bga_events) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                        "field-scene BGA event count exceeds its configured cap"));
            return false;
        }
        ++supported_count;
        const auto row = absolute_row(index.measures, event.position);
        if (!row) {
            diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
                                        "field-scene BGA event is outside chart geometry", event.id));
            continue;
        }
        index.bga_events.push_back({.event = event.id, .resource = event.resource,
                                    .kind = event.kind, .row = *row});
    }
    if (!diagnostics.empty()) return false;
    std::ranges::sort(index.bga_events, [](const auto& left, const auto& right) {
        return std::tie(left.row, left.event.value) < std::tie(right.row, right.event.value);
    });
    return true;
}

} // namespace

auto build_editor_field_scene_index(const chart_document_t& chart, std::uint64_t document_revision,
                                    editor_field_scene_index_options_t options)
    -> std::variant<editor_field_scene_index_t, diagnostic_list_t> {
    diagnostic_list_t diagnostics;
    if (!chart.id || chart.divisions_per_standard_measure == 0 || options.maximum_notes == 0 ||
        options.maximum_bgm_events == 0 || options.maximum_bga_events == 0 ||
        options.maximum_layout_segments == 0 ||
        options.maximum_lane_definitions == 0) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                    "field-scene index limits and chart identity must be valid"));
        return diagnostics;
    }
    const auto built_measures = build_measure_index(chart);
    if (const auto* failures = std::get_if<diagnostic_list_t>(&built_measures)) {
        return *failures;
    }

    editor_field_scene_index_t index{
        .chart = chart.id,
        .document_revision = document_revision,
        .measures = std::get<measure_index_t>(built_measures),
    };
    if (!append_scene_layouts(chart, index, options, diagnostics)) {
        return diagnostics;
    }
    if (!append_scene_notes(chart, index, options, diagnostics)) {
        return diagnostics;
    }
    if (!append_scene_bgm_events(chart, index, options, diagnostics)) {
        return diagnostics;
    }
    if (!append_scene_bga_events(chart, index, options, diagnostics)) {
        return diagnostics;
    }
    if (!diagnostics.empty()) {
        return diagnostics;
    }

    std::ranges::sort(index.notes, [](const auto& left, const auto& right) {
        return std::tie(left.begin_row, left.end_row, left.note.value) <
               std::tie(right.begin_row, right.end_row, right.note.value);
    });
    std::ranges::sort(index.bgm_events, [](const auto& left, const auto& right) {
        return std::tie(left.row, left.source_lane.value, left.event.value) <
               std::tie(right.row, right.source_lane.value, right.event.value);
    });
    build_bgm_end_tree(index);
    index.note_indices_by_id.reserve(index.notes.size());
    for (std::size_t note = 0; note < index.notes.size(); ++note) {
        if (!index.note_indices_by_id.emplace(index.notes[note].note.value, note).second) {
            diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                                        "field-scene note index has duplicate identities",
                                        index.notes[note].note));
            return diagnostics;
        }
    }
    while (index.note_tree_leaf_base < index.notes.size()) {
        if (index.note_tree_leaf_base > std::numeric_limits<std::size_t>::max() / 2) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                        "field-scene note tree exceeds addressable storage"));
            return diagnostics;
        }
        index.note_tree_leaf_base *= 2;
    }
    index.note_tree_max_end.assign(index.note_tree_leaf_base * 2, 0);
    for (std::size_t note = 0; note < index.notes.size(); ++note) {
        index.note_tree_max_end[index.note_tree_leaf_base + note] = index.notes[note].end_row;
    }
    for (auto node = index.note_tree_leaf_base; node-- > 1;) {
        index.note_tree_max_end[node] =
            std::max(index.note_tree_max_end[node * 2], index.note_tree_max_end[node * 2 + 1]);
    }
    return index;
}

namespace {

auto is_valid_scene_index(const chart_document_t& chart, const editor_field_scene_index_t& index,
                       const editor_interaction_view_model_t& view) -> bool {
    return chart.id == index.chart && view.chart == chart.id &&
           view.document_revision == index.document_revision && index.measures.total_rows != 0 &&
           index.note_tree_leaf_base != 0 && index.note_tree_leaf_base >= index.notes.size() &&
           index.note_tree_leaf_base <= std::numeric_limits<std::size_t>::max() / 2 &&
           index.note_tree_max_end.size() == index.note_tree_leaf_base * 2 &&
           index.note_indices_by_id.size() == index.notes.size() &&
           index.bgm_tree_leaf_base >= index.bgm_events.size() &&
           index.bgm_tree_leaf_base != 0 &&
           index.bgm_tree_leaf_base <= std::numeric_limits<std::size_t>::max() / 2 &&
           index.bgm_tree_max_end.size() == index.bgm_tree_leaf_base * 2;
}

auto is_valid_scene_request_geometry(const editor_field_scene_request_t& request,
                                  double viewport_height) -> bool {
    return is_finite_nonnegative(request.field_left_pixels) &&
           std::isfinite(request.field_width_pixels) && request.field_width_pixels > 0.0 &&
           is_finite_nonnegative(request.split_stage_gutter_pixels) &&
           std::isfinite(request.note_head_height_pixels) &&
           request.note_head_height_pixels > 0.0 &&
           (request.scroll_direction == editor_field_scroll_direction_t::down ||
            request.scroll_direction == editor_field_scroll_direction_t::up) &&
           (!request.fixed_cursor_screen_y_pixels ||
            (std::isfinite(*request.fixed_cursor_screen_y_pixels) &&
             *request.fixed_cursor_screen_y_pixels >= 0.0 &&
             *request.fixed_cursor_screen_y_pixels <= viewport_height)) &&
           std::isfinite(request.comparison_difference_height_pixels) &&
           request.comparison_difference_height_pixels > 0.0 &&
           std::isfinite(request.hold_body_width_ratio) && request.hold_body_width_ratio > 0.0 &&
           request.hold_body_width_ratio <= 1.0 &&
           is_finite_nonnegative(request.bgm_pane_left_pixels) &&
           is_finite_nonnegative(request.bgm_pane_width_pixels) &&
           std::isfinite(request.bgm_pane_left_pixels + request.bgm_pane_width_pixels) &&
           std::isfinite(request.bgm_lane_width_pixels) && request.bgm_lane_width_pixels > 0.0 &&
           is_finite_nonnegative(request.bgm_horizontal_scroll_pixels) &&
           is_finite_nonnegative(request.bga_pane_left_pixels) &&
           is_finite_nonnegative(request.bga_pane_width_pixels) &&
           std::isfinite(request.bga_pane_left_pixels + request.bga_pane_width_pixels);
}

auto is_valid_scene_request_limits(const editor_field_scene_request_t& request) -> bool {
    return request.maximum_grid_lines != 0 && request.maximum_authored_groupings != 0 &&
           request.authored_groupings.size() <= request.maximum_authored_groupings &&
           request.maximum_comparison_difference_rows != 0 &&
           request.comparison_difference_rows.size() <=
               request.maximum_comparison_difference_rows &&
           request.maximum_scene_primitives != 0 && request.maximum_composition_operations != 0;
}

auto is_valid_scene_view(const editor_interaction_view_model_t& view) -> bool {
    return std::isfinite(view.pixels_per_standard_measure) &&
           view.pixels_per_standard_measure >= minimum_pixels_per_standard_measure &&
           view.pixels_per_standard_measure <= maximum_pixels_per_standard_measure &&
           std::isfinite(view.viewport_scroll_pixels) &&
           std::isfinite(view.viewport_height_pixels) && view.viewport_height_pixels > 0.0 &&
           is_editor_viewport_margins_valid(view.viewport_minimum_margin_fraction,
                                         view.viewport_end_margin_fraction);
}

struct scene_geometry_context_t {
    const chart_document_t& chart;
    const editor_field_scene_index_t& index;
    const editor_interaction_view_model_t& view;
    const editor_field_scene_request_t& request;
    editor_field_scene_t& scene;
    diagnostic_list_t& diagnostics;
    long double query_low{};
    long double query_high{};
    std::uint64_t canvas_rows{};
};

struct visible_paint_target_t {
    paint_cell_key_t cell;
    lane_glyph_t glyph{lane_glyph_t::bar};
    editor_field_rect_t bounds;
};

struct scene_paint_preview_t {
    std::vector<visible_paint_target_t> visible_targets;
    std::unordered_set<paint_cell_key_t, paint_cell_hash_t> target_cells;
    std::unordered_set<paint_cell_key_t, paint_cell_hash_t> visible_cells;
    std::unordered_set<paint_cell_key_t, paint_cell_hash_t> consumed_cells;
    std::unordered_set<paint_cell_key_t, paint_cell_hash_t> invalid_cells;
    std::unordered_map<paint_cell_key_t, std::size_t, paint_cell_hash_t> overlap_counts;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> visible_rows_by_lane;
};

struct note_render_context_t {
    scene_geometry_context_t& geometry;
    const std::vector<visible_band_t>& visible_bands;
    bool selected{};
    bool proposal_origin{};
};

struct proposal_geometry_t {
    object_id_t note;
    lane_id_t lane;
    std::optional<resource_id_t> sound;
    note_kind_t kind{note_kind_t::tap};
    std::uint64_t begin_row{};
    std::optional<std::uint64_t> end_row;
    bool selected{};
    std::optional<color_rgba_t> semantic_color;
    note_move_conflict_t conflict{note_move_conflict_t::none};
    std::optional<std::int64_t> display_column;
    std::size_t display_columns{};
    std::optional<long double> display_begin_row;
    std::optional<long double> display_end_row;
};

struct proposal_render_context_t {
    scene_geometry_context_t& geometry;
    const std::vector<visible_band_t>& visible_bands;
};

struct duration_proposal_t {
    const editor_field_scene_note_source_t* source{};
    const note_duration_preview_t* preview{};
    std::optional<std::uint64_t> end_row;
};

struct duration_proposals_t {
    std::vector<duration_proposal_t> proposals;
    std::unordered_set<std::uint64_t> origin_notes;
};

template <typename Operate>
auto append_duration_proposal(scene_geometry_context_t& context,
                              const note_duration_preview_t& preview,
                              std::unordered_set<std::uint64_t>& expected,
                              duration_proposals_t& duration, Operate&& operate) -> bool {
    if (!operate() || preview.note.value == 0 || !expected.erase(preview.note.value)) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id,
                  "field-scene duration preview does not match its selected notes"));
        return false;
    }
    const auto found = context.index.note_indices_by_id.find(preview.note.value);
    if (found == context.index.note_indices_by_id.end() ||
        (preview.kind != note_kind_t::tap && preview.kind != note_kind_t::hold)) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_hold,
                  "field-scene duration preview refers to an invalid note proposal"));
        return false;
    }
    std::optional<std::uint64_t> end_row;
    if (preview.kind == note_kind_t::tap) {
        if (preview.end) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_hold,
                      "field-scene tap duration preview must not carry a tail"));
            return false;
        }
    } else {
        if (!preview.end) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_hold,
                      "field-scene hold duration preview is missing its tail"));
            return false;
        }
        end_row = absolute_row(context.index.measures, *preview.end);
        if (!end_row || *end_row <= context.index.notes[found->second].begin_row) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_hold,
                      "field-scene hold duration preview has an invalid tail"));
            return false;
        }
    }
    duration.origin_notes.insert(preview.note.value);
    duration.proposals.push_back(
        {.source = &context.index.notes[found->second], .preview = &preview, .end_row = end_row});
    return true;
}

template <typename Operate>
auto collect_duration_proposals(scene_geometry_context_t& context, duration_proposals_t& duration,
                                Operate&& operate) -> bool {
    if (!context.view.pending_note_duration_gesture) {
        return true;
    }
    const auto& pending = *context.view.pending_note_duration_gesture;
    if (pending.document_revision != context.index.document_revision || pending.notes.empty() ||
        pending.notes.size() != pending.preview.size() ||
        pending.notes.size() > context.index.notes.size()) {
        context.diagnostics.push_back(error(diagnostic_code_t::invalid_hold,
                                            "field-scene duration preview is malformed or stale"));
        return false;
    }
    std::unordered_set<std::uint64_t> expected;
    expected.reserve(pending.notes.size());
    for (const auto note : pending.notes) {
        if (note.value == 0 || !expected.insert(note.value).second) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_id,
                      "field-scene duration preview contains duplicate note identities"));
            return false;
        }
    }
    duration.proposals.reserve(pending.preview.size());
    duration.origin_notes.reserve(pending.preview.size());
    for (const auto& preview : pending.preview) {
        if (!append_duration_proposal(context, preview, expected, duration, operate)) {
            return false;
        }
    }
    if (!expected.empty()) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id,
                  "field-scene duration preview omitted a selected note proposal"));
        return false;
    }
    return true;
}

struct move_proposal_t {
    const editor_field_scene_note_source_t* source{};
    const note_move_preview_t* preview{};
    std::uint64_t begin_row{};
    std::optional<std::uint64_t> end_row;
};

struct move_proposals_t {
    std::vector<move_proposal_t> proposals;
    std::unordered_set<std::uint64_t> origin_notes;
};

struct hold_proposal_t {
    lane_id_t lane;
    std::uint64_t head_row{};
    std::optional<std::uint64_t> tail_row;
    bool adds_note{};
};

struct hold_proposals_t {
    std::vector<hold_proposal_t> proposals;
    std::unordered_set<std::uint64_t> origin_notes;
};

struct hold_removal_validation_t {
    lane_id_t lane;
    std::uint64_t head_row{};
    std::uint64_t final_tail{};
    const char* duplicate_message{};
    const char* unknown_message{};
    const char* unrelated_message{};
};

template <typename Operate>
auto record_hold_removals(scene_geometry_context_t& context, hold_proposals_t& holds,
                          const std::vector<object_id_t>& removals,
                          const hold_removal_validation_t& validation, Operate&& operate) -> bool {
    holds.origin_notes.reserve(removals.size());
    for (const auto note : removals) {
        if (!operate() || note.value == 0 || !holds.origin_notes.insert(note.value).second) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_id, validation.duplicate_message));
            return false;
        }
        const auto found = context.index.note_indices_by_id.find(note.value);
        if (found == context.index.note_indices_by_id.end()) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_id, validation.unknown_message));
            return false;
        }
        const auto& source = context.index.notes[found->second];
        if ((source.kind != note_kind_t::tap && source.kind != note_kind_t::hold) ||
            source.lane != validation.lane || source.begin_row > validation.final_tail ||
            source.end_row < validation.head_row) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_hold, validation.unrelated_message, note));
            return false;
        }
    }
    return true;
}

struct regular_hold_rows_t {
    std::uint64_t anchor{};
    std::optional<std::uint64_t> moving;
};

auto is_valid_regular_hold_placement(const pending_hold_placement_preview_t& preview,
                                  regular_hold_rows_t rows, std::optional<std::uint64_t> head,
                                  std::optional<std::uint64_t> tail) -> bool {
    const auto final_tail = tail.value_or(head.value_or(0));
    return head && (!tail || *tail > *head) && rows.anchor >= *head && rows.anchor <= final_tail &&
           (!rows.moving || (*rows.moving >= *head && *rows.moving <= final_tail)) &&
           preview.removals &&
           (preview.adds_note || (!preview.tail && !preview.removals->empty())) &&
           (!preview.adds_note || preview.tail || preview.removals->empty());
}

template <typename Operate>
auto append_placed_hold(scene_geometry_context_t& context, hold_proposals_t& holds,
                        const pending_hold_t& pending, regular_hold_rows_t rows, Operate&& operate)
    -> bool {
    const auto& preview = *pending.placement_preview;
    const auto head = absolute_row(context.index.measures, preview.head);
    const auto tail = preview.tail ? absolute_row(context.index.measures, *preview.tail)
                                   : std::optional<std::uint64_t>{};
    if (!is_valid_regular_hold_placement(preview, rows, head, tail)) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_hold,
                  "field-scene hold placement preview is malformed or does not cover its gesture"));
        return false;
    }
    const hold_removal_validation_t validation{
        pending.lane,
        *head,
        tail.value_or(*head),
        "field-scene hold placement preview contains duplicate note identities",
        "field-scene hold placement preview refers to an unknown note",
        "field-scene hold placement preview removes an unrelated note"};
    if (!record_hold_removals(context, holds, *preview.removals, validation, operate)) {
        return false;
    }
    holds.proposals.push_back({.lane = pending.lane,
                               .head_row = *head,
                               .tail_row = tail,
                               .adds_note = preview.adds_note});
    return true;
}

template <typename Operate>
auto append_regular_hold(scene_geometry_context_t& context, hold_proposals_t& holds,
                         const pending_hold_t& pending, Operate&& operate) -> bool {
    if (pending.anchor_canvas_row || pending.preview_canvas_row) {
        return true;
    }
    const auto anchor = absolute_row(context.index.measures, pending.position);
    if (pending.document_revision != context.index.document_revision || !anchor ||
        !has_lane(layout_at_row(context.index, *anchor), pending.lane)) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_hold, "field-scene hold preview is stale or invalid"));
        return false;
    }
    std::optional<std::uint64_t> moving;
    if (pending.preview_end) {
        moving = absolute_row(context.index.measures, *pending.preview_end);
        if (!moving || !has_lane(layout_at_row(context.index, *moving), pending.lane)) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_hold,
                      "field-scene hold preview has an invalid proposed endpoint"));
            return false;
        }
    }
    if (pending.placement_preview) {
        return append_placed_hold(context, holds, pending, {*anchor, moving}, operate);
    }
    const auto head = moving ? std::min(*anchor, *moving) : *anchor;
    const auto tail = moving ? std::max(*anchor, *moving) : *anchor;
    holds.proposals.push_back(
        {.lane = pending.lane,
         .head_row = head,
         .tail_row = head == tail ? std::optional<std::uint64_t>{} : std::optional{tail},
         .adds_note = true});
    return true;
}

template <typename Operate>
auto collect_regular_holds(scene_geometry_context_t& context, hold_proposals_t& holds,
                           Operate&& operate) -> bool {
    auto pending_holds = context.view.additional_keyboard_holds;
    if (context.view.pending_hold) {
        pending_holds.push_back(*context.view.pending_hold);
    }
    for (const auto& pending : pending_holds) {
        if (!append_regular_hold(context, holds, pending, operate)) {
            return false;
        }
    }
    return true;
}

template <typename Operate>
auto append_virtual_hold(scene_geometry_context_t& context, hold_proposals_t& holds,
                         const editor_interaction_view_model_t::virtual_hold_preview_t& pending,
                         Operate&& operate) -> bool {
    if (pending.document_revision != context.index.document_revision ||
        !has_chart_virtual_tail(context.chart) ||
        !context.request.include_virtual_bms_tail || pending.anchor_row >= context.canvas_rows ||
        pending.end_row >= context.canvas_rows || context.index.layouts.empty() ||
        !has_lane(&context.index.layouts.back(), pending.lane)) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_hold, "virtual BMS hold preview is stale or invalid"));
        return false;
    }
    const auto final_tail = pending.tail_row.value_or(pending.head_row);
    if (pending.head_row > std::min(pending.anchor_row, pending.end_row) ||
        final_tail < std::max(pending.anchor_row, pending.end_row) ||
        (pending.tail_row && *pending.tail_row <= pending.head_row) || !pending.removals ||
        (!pending.adds_note && (pending.tail_row || pending.removals->empty())) ||
        (pending.adds_note && !pending.tail_row && !pending.removals->empty())) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_hold, "virtual BMS hold preview is malformed"));
        return false;
    }
    const hold_removal_validation_t validation{
        pending.lane,
        pending.head_row,
        final_tail,
        "virtual BMS hold preview contains duplicate note identities",
        "virtual BMS hold preview refers to an unknown note",
        "virtual BMS hold preview removes an unrelated note"};
    if (!record_hold_removals(context, holds, *pending.removals, validation, operate)) {
        return false;
    }
    holds.proposals.push_back({.lane = pending.lane,
                               .head_row = pending.head_row,
                               .tail_row = pending.tail_row,
                               .adds_note = pending.adds_note});
    return true;
}

template <typename Operate>
auto collect_virtual_holds(scene_geometry_context_t& context, hold_proposals_t& holds,
                           Operate&& operate) -> bool {
    auto virtual_holds = context.view.pending_canvas_keyboard_holds;
    if (context.view.pending_virtual_hold) {
        virtual_holds.push_back(*context.view.pending_virtual_hold);
    }
    for (const auto& pending : virtual_holds) {
        if (!append_virtual_hold(context, holds, pending, operate)) {
            return false;
        }
    }
    return true;
}

auto is_valid_move_display_rows(const note_move_preview_t& preview, note_kind_t kind,
                             std::uint64_t chart_rows) -> bool {
    if (!preview.display_begin_row && !preview.display_end_row)
        return preview.conflict != note_move_conflict_t::out_of_chart;
    if (preview.conflict != note_move_conflict_t::out_of_chart || !preview.display_begin_row ||
        !std::isfinite(*preview.display_begin_row))
        return false;
    const auto begin = *preview.display_begin_row;
    if (kind == note_kind_t::tap)
        return !preview.display_end_row && (begin < 0 || begin >= chart_rows);
    return preview.display_end_row && std::isfinite(*preview.display_end_row) &&
           *preview.display_end_row > begin &&
           (begin < 0 || *preview.display_end_row >= chart_rows);
}

auto is_valid_move_conflict(const editor_field_scene_index_t& index,
                         const note_move_preview_t& preview) -> bool {
    switch (preview.conflict) {
    case note_move_conflict_t::none:
        return !preview.occupant;
    case note_move_conflict_t::occupied:
        if (!preview.occupant)
            return false;
        break;
    case note_move_conflict_t::out_of_lane:
    case note_move_conflict_t::out_of_chart:
        if (!preview.display_column || preview.display_columns == 0)
            return false;
        break;
    case note_move_conflict_t::other:
        break;
    default:
        return false;
    }
    return !preview.occupant || (preview.occupant != preview.note &&
                                 index.note_indices_by_id.contains(preview.occupant->value));
}

template <typename Operate>
auto append_move_proposal(scene_geometry_context_t& context, const note_move_preview_t& preview,
                          std::unordered_set<std::uint64_t>& expected, move_proposals_t& moves,
                          Operate&& operate) -> bool {
    if (!operate() || preview.note.value == 0 || !expected.erase(preview.note.value) ||
        !is_valid_move_conflict(context.index, preview)) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id,
                  "field-scene note move preview does not match its selected notes"));
        return false;
    }
    const auto found = context.index.note_indices_by_id.find(preview.note.value);
    const auto begin_row = absolute_row(context.index.measures, preview.position);
    const auto invalid_lane_preview = (preview.conflict == note_move_conflict_t::out_of_lane ||
                                       preview.conflict == note_move_conflict_t::out_of_chart) &&
                                      preview.display_column && preview.display_columns != 0;
    if (found == context.index.note_indices_by_id.end() || !begin_row ||
        (!invalid_lane_preview &&
         !has_lane(layout_at_row(context.index, *begin_row), preview.lane))) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id,
                  "field-scene note move preview refers to an invalid note proposal"));
        return false;
    }
    const auto& source = context.index.notes[found->second];
    if (!is_valid_move_display_rows(preview, source.kind, context.index.measures.total_rows)) {
        context.diagnostics.push_back(error(diagnostic_code_t::invalid_hold,
                                            "field-scene note move display rows are malformed"));
        return false;
    }
    std::optional<std::uint64_t> end_row;
    if (source.kind == note_kind_t::tap) {
        if (preview.end) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_hold,
                      "field-scene moved tap preview must not carry a tail"));
            return false;
        }
    } else if (source.kind == note_kind_t::hold) {
        if (!preview.end) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_hold,
                      "field-scene moved hold preview is missing its tail"));
            return false;
        }
        end_row = absolute_row(context.index.measures, *preview.end);
        if (!end_row || (!preview.display_begin_row && *end_row <= *begin_row) ||
            (!invalid_lane_preview &&
             !has_lane(layout_at_row(context.index, *end_row), preview.lane))) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_hold,
                      "field-scene moved hold preview has an invalid tail"));
            return false;
        }
    } else {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_hold,
                  "field-scene note move only supports tap and hold proposals"));
        return false;
    }
    moves.origin_notes.insert(preview.note.value);
    moves.proposals.push_back(
        {.source = &source, .preview = &preview, .begin_row = *begin_row, .end_row = end_row});
    return true;
}

void mark_scene_move_occupants(editor_field_scene_t& scene,
                               const editor_interaction_view_model_t& view) {
    if (!view.pending_note_move_gesture)
        return;
    const auto& pending = *view.pending_note_move_gesture;
    std::unordered_set<std::uint64_t> occupants;
    for (const auto id : pending.occupants)
        occupants.insert(id.value);
    for (const auto& proposal : pending.preview) {
        if (proposal.occupant)
            occupants.insert(proposal.occupant->value);
    }
    for (auto& endpoint : scene.note_endpoints)
        endpoint.move_conflict_occupant =
            !endpoint.proposal_ghost && occupants.contains(endpoint.note.value);
    for (auto& body : scene.hold_bodies)
        body.move_conflict_occupant = !body.proposal_ghost && occupants.contains(body.note.value);
}

template <typename Operate>
auto collect_move_proposals(scene_geometry_context_t& context, move_proposals_t& moves,
                            Operate&& operate) -> bool {
    if (!context.view.pending_note_move_gesture) {
        return true;
    }
    const auto& pending = *context.view.pending_note_move_gesture;
    if (pending.document_revision != context.index.document_revision ||
        (pending.notes.empty() && std::ranges::find(context.index.bgm_events, pending.reference_note,
            &editor_field_scene_bgm_source_t::event) == context.index.bgm_events.end()) ||
        pending.notes.size() != pending.preview.size() ||
        pending.notes.size() > context.index.notes.size()) {
        context.diagnostics.push_back(error(diagnostic_code_t::invalid_hold,
                                            "field-scene note move preview is malformed or stale"));
        return false;
    }
    if (pending.to_bgm) return true;
    std::unordered_set<std::uint64_t> expected;
    expected.reserve(pending.notes.size());
    for (const auto note : pending.notes) {
        if (note.value == 0 || !expected.insert(note.value).second) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_id,
                      "field-scene note move preview contains duplicate note identities"));
            return false;
        }
    }
    moves.proposals.reserve(pending.preview.size());
    moves.origin_notes.reserve(pending.preview.size());
    for (const auto& preview : pending.preview) {
        if (!append_move_proposal(context, preview, expected, moves, operate)) {
            return false;
        }
    }
    if (!expected.empty()) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id,
                  "field-scene note move preview omitted a selected note proposal"));
        return false;
    }
    return true;
}

auto proposal_source_layout(const proposal_render_context_t& render,
                            const proposal_geometry_t& proposal)
    -> const editor_field_scene_layout_source_t* {
    const auto& index = render.geometry.index;
    const auto found = index.note_indices_by_id.find(proposal.note.value);
    return found == index.note_indices_by_id.end()
               ? nullptr
               : layout_at_row(index, index.notes[found->second].begin_row);
}

auto proposal_source_stage(const proposal_render_context_t& render,
                           const proposal_geometry_t& proposal) -> stage_group_t {
    const auto* source = proposal_source_layout(render, proposal);
    if (!source)
        return stage_group_t::none;
    const auto& index = render.geometry.index;
    const auto lane = index.notes[index.note_indices_by_id.at(proposal.note.value)].lane;
    const auto found = source->layout.find(lane);
    return found == nullptr ? stage_group_t::none : found->stage;
}

auto proposal_target_stage(const lane_layout_t& layout, const proposal_geometry_t& proposal,
                           stage_group_t source_stage) -> stage_group_t {
    if (proposal.lane.value != 0 || !layout.split_stage)
        return source_stage;
    const lane_definition_t* after = nullptr;
    const lane_definition_t* before = nullptr;
    for (const auto& lane : layout.lanes) {
        if (!lane.is_editable())
            continue;
        if (static_cast<std::int64_t>(lane.display_order) >= *proposal.display_column) {
            if (!after || lane.display_order < after->display_order)
                after = &lane;
        } else if (!before || lane.display_order > before->display_order) {
            before = &lane;
        }
    }
    return after ? after->stage : before ? before->stage : source_stage;
}

auto proposal_physical_column(const lane_layout_t& layout, std::int64_t column, stage_group_t stage)
    -> double {
    double rank = 0;
    std::int64_t last = -1;
    for (const auto& lane : layout.lanes) {
        if (!lane.is_editable())
            continue;
        if (layout.split_stage && lane.stage != stage) {
            rank += lane.stage < stage ? 1 : 0;
        } else {
            last = std::max(last, static_cast<std::int64_t>(lane.display_order));
            rank += static_cast<std::int64_t>(lane.display_order) < column ? 1 : 0;
        }
    }
    // A negative requested column remains outside the field rather than collapsing to slot zero.
    return column < 0 ? static_cast<double>(column)
                      : rank + std::max<std::int64_t>(0, column - last - 1);
}

auto proposal_lane_bounds(const proposal_render_context_t& render, const visible_band_t& band,
                          const proposal_geometry_t& proposal) -> std::optional<lane_bounds_t> {
    if (const auto* bounds = lane_bounds(band, proposal.lane))
        return *bounds;
    if ((proposal.conflict != note_move_conflict_t::out_of_lane &&
         proposal.conflict != note_move_conflict_t::out_of_chart) ||
        !proposal.display_column)
        return std::nullopt;
    const auto* source = proposal_source_layout(render, proposal);
    const auto* layout = !band.lanes.empty() ? band.layout : source ? &source->layout : band.layout;
    const auto columns = !band.lanes.empty() ? band.lanes.size()
                                             : std::max<std::size_t>(1, proposal.display_columns);
    const auto split = layout && layout->split_stage && layout->split_after_column
                           ? std::min<std::size_t>(*layout->split_after_column, columns)
                           : columns;
    const auto& request = render.geometry.request;
    const auto gutter = split > 0 && split < columns ? request.split_stage_gutter_pixels : 0.0;
    const auto width = (request.field_width_pixels - gutter) / static_cast<double>(columns);
    const auto column =
        layout && is_editor_scratch_presentation_active(*layout, request.scratch_on_right)
            ? static_cast<double>(*proposal.display_column)
            : layout ? proposal_physical_column(
                  *layout, *proposal.display_column,
                  proposal_target_stage(*layout, proposal, proposal_source_stage(render, proposal)))
            : static_cast<double>(*proposal.display_column);
    const auto left = request.field_left_pixels + width * column + (column >= split ? gutter : 0.0);
    return lane_bounds_t{proposal.lane, lane_glyph_t::bar, left, left + width};
}

auto proposal_band_rows(const proposal_render_context_t& render, const visible_band_t& band,
                        const proposal_geometry_t& proposal, long double begin, long double end)
    -> std::pair<long double, long double> {
    auto low = band.clipped_begin_row;
    auto high = band.clipped_end_row;
    if (proposal.conflict == note_move_conflict_t::out_of_chart) {
        if (band.begin_row == 0)
            low = std::min(low, begin);
        if (band.end_row == render.geometry.canvas_rows)
            high = std::max(high, end);
    }
    return {std::max(begin, low), std::min(end, high)};
}

template <typename Operate, typename Consume, typename YForRow>
auto append_proposal_hold_bodies(proposal_render_context_t& render,
                                 const proposal_geometry_t& proposal, Operate&& operate,
                                 Consume&& consume, YForRow&& y_for_row) -> bool {
    const auto begin_row = proposal.display_begin_row.value_or(proposal.begin_row);
    const auto end_row =
        proposal.display_end_row.value_or(proposal.end_row.value_or(proposal.begin_row));
    if (proposal.kind != note_kind_t::hold || !proposal.end_row || end_row <= begin_row) {
        return true;
    }
    for (const auto& band : render.visible_bands) {
        if (!operate()) {
            return false;
        }
        const auto [begin, end] = proposal_band_rows(render, band, proposal, begin_row, end_row);
        if (begin >= end) {
            continue;
        }
        const auto bounds = proposal_lane_bounds(render, band, proposal);
        if (!bounds) {
            continue;
        }
        if (!consume()) {
            return false;
        }
        const auto lane_width = bounds->right - bounds->left;
        const auto body_width = lane_width * render.geometry.request.hold_body_width_ratio;
        const auto center = (bounds->left + bounds->right) / 2.0;
        const auto [top, bottom] = ordered_vertical_bounds(y_for_row(begin), y_for_row(end));
        render.geometry.scene.hold_bodies.push_back(
            {.note = proposal.note,
             .lane = proposal.lane,
             .selected = proposal.selected,
             .proposal_ghost = true,
             .semantic_color = proposal.semantic_color,
             .bounds = {center - body_width / 2.0, top, center + body_width / 2.0, bottom},
             .move_conflict = proposal.conflict});
    }
    return true;
}

template <typename Consume, typename YForRow, typename BandForRow>
auto append_proposal_endpoint(proposal_render_context_t& render,
                              const proposal_geometry_t& proposal, long double row, bool tail,
                              Consume&& consume, YForRow&& y_for_row, BandForRow&& band_for_row)
    -> bool {
    const auto layout_row =
        tail ? proposal.end_row.value_or(proposal.begin_row) : proposal.begin_row;
    const auto* band = band_for_row(layout_row);
    if (band == nullptr) {
        return true;
    }
    const auto bounds = proposal_lane_bounds(render, *band, proposal);
    if (!bounds) {
        return true;
    }
    const auto y = y_for_row(row);
    const auto [top, bottom] =
        endpoint_vertical_bounds(y, render.geometry.request.note_head_height_pixels, bounds->glyph,
                                 render.geometry.request.scroll_direction);
    const editor_field_rect_t rect{bounds->left, top, bounds->right, bottom};
    if (!is_rect_intersecting_viewport(rect, render.geometry.view.viewport_height_pixels)) {
        return true;
    }
    if (!consume()) {
        return false;
    }
    render.geometry.scene.note_endpoints.push_back({.note = proposal.note,
                                                    .lane = proposal.lane,
                                                    .sound = proposal.sound,
                                                    .glyph = bounds->glyph,
                                                    .kind = proposal.kind,
                                                    .tail = tail,
                                                    .selected = proposal.selected,
                                                    .proposal_ghost = true,
                                                    .semantic_color = proposal.semantic_color,
                                                    .bounds = rect,
                                                    .move_conflict = proposal.conflict});
    return true;
}

template <typename Operate, typename Consume, typename YForRow, typename BandForRow>
auto append_proposal_geometry(proposal_render_context_t& render,
                              const proposal_geometry_t& proposal, Operate&& operate,
                              Consume&& consume, YForRow&& y_for_row, BandForRow&& band_for_row)
    -> bool {
    if (!append_proposal_hold_bodies(render, proposal, operate, consume, y_for_row) ||
        !append_proposal_endpoint(render, proposal,
                                  proposal.display_begin_row.value_or(proposal.begin_row), false,
                                  consume, y_for_row, band_for_row)) {
        return false;
    }
    return proposal.kind != note_kind_t::hold || !proposal.end_row ||
           append_proposal_endpoint(render, proposal,
                                    proposal.display_end_row.value_or(*proposal.end_row), true,
                                    consume, y_for_row, band_for_row);
}

struct scene_proposal_composition_t {
    proposal_render_context_t& render;
    const duration_proposals_t& duration;
    const move_proposals_t& moves;
    const hold_proposals_t& holds;
    const std::unordered_set<std::uint64_t>& selected;
};

template <typename Operate, typename Consume, typename YForRow, typename BandForRow>
auto compose_note_proposals(scene_proposal_composition_t& composition, Operate&& operate,
                            Consume&& consume, YForRow&& y_for_row, BandForRow&& band_for_row)
    -> bool {
    auto& diagnostics = composition.render.geometry.diagnostics;
    for (const auto& proposal : composition.duration.proposals) {
        const proposal_geometry_t geometry{
            proposal.source->note,
            proposal.source->lane,
            proposal.source->sound,
            proposal.preview->kind,
            proposal.source->begin_row,
            proposal.end_row,
            composition.selected.contains(proposal.source->note.value),
            proposal.source->semantic_color};
        if (!append_proposal_geometry(composition.render, geometry, operate, consume, y_for_row,
                                      band_for_row)) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                        "field-scene duration preview exceeds a composition cap"));
            return false;
        }
    }
    for (const auto& proposal : composition.moves.proposals) {
        const proposal_geometry_t geometry{
            proposal.source->note,
            proposal.preview->lane,
            proposal.source->sound,
            proposal.source->kind,
            proposal.begin_row,
            proposal.end_row,
            composition.selected.contains(proposal.source->note.value),
            proposal.source->semantic_color,
            proposal.preview->conflict,
            proposal.preview->display_column,
            proposal.preview->display_columns,
            proposal.preview->display_begin_row,
            proposal.preview->display_end_row};
        if (!append_proposal_geometry(composition.render, geometry, operate, consume, y_for_row,
                                      band_for_row)) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                        "field-scene note move preview exceeds a composition cap"));
            return false;
        }
    }
    for (const auto& hold : composition.holds.proposals) {
        if (!hold.adds_note) {
            continue;
        }
        const auto kind = hold.tail_row ? note_kind_t::hold : note_kind_t::tap;
        const proposal_geometry_t geometry{{}, hold.lane, {}, kind, hold.head_row, hold.tail_row,
                                           false, {}};
        if (!append_proposal_geometry(composition.render, geometry, operate, consume, y_for_row,
                                      band_for_row)) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                        "field-scene hold preview exceeds a composition cap"));
            return false;
        }
    }
    return true;
}

auto is_note_visible_in_scene(const editor_field_scene_note_source_t& note,
                           const scene_geometry_context_t& context) -> bool {
    if (static_cast<long double>(note.end_row) < context.query_low ||
        static_cast<long double>(note.begin_row) > context.query_high) {
        return false;
    }
    const auto& request = context.request;
    const auto has_layer = [&](layer_id_t layer) {
        return std::ranges::find(note.semantic_layers, layer) != note.semantic_layers.end();
    };
    return (!request.solo_semantic_layer || has_layer(*request.solo_semantic_layer)) &&
           std::ranges::all_of(request.required_semantic_layers, has_layer) &&
           !std::ranges::any_of(request.excluded_semantic_layers, has_layer);
}

auto is_bgm_event_visible_in_scene(const editor_field_scene_bgm_source_t& event,
                                const editor_field_scene_request_t& request) -> bool {
    const auto has_layer = [&](layer_id_t layer) {
        return std::ranges::find(event.semantic_layers, layer) != event.semantic_layers.end();
    };
    return (!request.solo_semantic_layer || has_layer(*request.solo_semantic_layer)) &&
           std::ranges::all_of(request.required_semantic_layers, has_layer) &&
           !std::ranges::any_of(request.excluded_semantic_layers, has_layer);
}

template <typename Operate>
auto record_paint_overlaps(const editor_field_scene_note_source_t& note,
                           scene_paint_preview_t& paint, Operate&& operate) -> bool {
    const auto target_rows = paint.visible_rows_by_lane.find(note.lane.value);
    if (target_rows == paint.visible_rows_by_lane.end()) {
        return true;
    }
    const auto first = std::ranges::lower_bound(target_rows->second, note.begin_row);
    const auto after = std::ranges::upper_bound(first, target_rows->second.end(), note.end_row);
    for (auto target = first; target != after; ++target) {
        if (!operate()) {
            return false;
        }
        const auto removable_tap = note.kind == note_kind_t::tap &&
                                   note.begin_row == note.end_row && note.begin_row == *target;
        const paint_cell_key_t cell{note.lane, *target};
        const auto overlap_count = ++paint.overlap_counts[cell];
        if (!removable_tap || overlap_count > 1) {
            paint.invalid_cells.insert(cell);
        }
    }
    return true;
}

template <typename Operate, typename Consume, typename YForRow>
auto append_note_hold_bodies(const editor_field_scene_note_source_t& note,
                             note_render_context_t& render, Operate&& operate, Consume&& consume,
                             YForRow&& y_for_row) -> bool {
    if (note.end_row <= note.begin_row) {
        return true;
    }
    for (const auto& band : render.visible_bands) {
        if (!operate()) {
            return false;
        }
        const auto begin =
            std::max(static_cast<long double>(note.begin_row), band.clipped_begin_row);
        const auto end = std::min(static_cast<long double>(note.end_row), band.clipped_end_row);
        if (begin >= end) {
            continue;
        }
        const auto* bounds = lane_bounds(band, note.lane);
        if (bounds == nullptr) {
            continue;
        }
        if (!consume()) {
            return false;
        }
        const auto lane_width = bounds->right - bounds->left;
        const auto body_width = lane_width * render.geometry.request.hold_body_width_ratio;
        const auto center = (bounds->left + bounds->right) / 2.0;
        const auto [top, bottom] = ordered_vertical_bounds(y_for_row(begin), y_for_row(end));
        render.geometry.scene.hold_bodies.push_back(
            {.note = note.note,
             .lane = note.lane,
             .selected = render.selected,
             .proposal_origin = render.proposal_origin,
             .semantic_color = note.semantic_color,
             .bounds = {center - body_width / 2.0, top, center + body_width / 2.0, bottom}});
    }
    return true;
}

struct scene_note_composition_t {
    scene_geometry_context_t& geometry;
    scene_paint_preview_t& paint;
    const std::unordered_set<std::uint64_t>& selected;
    const duration_proposals_t& duration;
    const move_proposals_t& moves;
    const hold_proposals_t& holds;
    const std::vector<visible_band_t>& visible_bands;
    std::vector<std::pair<std::size_t, paint_cell_key_t>> paint_endpoint_cells;
};

template <typename Consume, typename YForRow, typename BandForRow>
auto append_indexed_note_endpoint(scene_note_composition_t& composition,
                                  const editor_field_scene_note_source_t& note,
                                  const note_render_context_t& render, std::uint64_t row, bool tail,
                                  Consume&& consume, YForRow&& y_for_row, BandForRow&& band_for_row)
    -> bool {
    const auto* band = band_for_row(row);
    if (band == nullptr) {
        return true;
    }
    const auto* bounds = lane_bounds(*band, note.lane);
    if (bounds == nullptr) {
        return true;
    }
    const auto y = y_for_row(row);
    const auto [top, bottom] =
        endpoint_vertical_bounds(y, composition.geometry.request.note_head_height_pixels,
                                 bounds->glyph, composition.geometry.request.scroll_direction);
    const editor_field_rect_t rect{bounds->left, top, bounds->right, bottom};
    if (!is_rect_intersecting_viewport(rect, composition.geometry.view.viewport_height_pixels)) {
        return true;
    }
    if (!consume()) {
        return false;
    }
    const paint_cell_key_t cell{note.lane, row};
    const auto paint_target = !tail && composition.paint.visible_cells.contains(cell);
    const auto invalid_preview = paint_target && composition.paint.invalid_cells.contains(cell);
    const auto erase_preview = paint_target && !invalid_preview && note.kind == note_kind_t::tap &&
                               note.begin_row == note.end_row;
    if (paint_target) {
        composition.paint.consumed_cells.insert(cell);
        composition.paint_endpoint_cells.emplace_back(
            composition.geometry.scene.note_endpoints.size(), cell);
    }
    composition.geometry.scene.note_endpoints.push_back({.note = note.note,
                                                         .lane = note.lane,
                                                         .sound = note.sound,
                                                         .glyph = bounds->glyph,
                                                         .kind = note.kind,
                                                         .tail = tail,
                                                         .selected = render.selected,
                                                         .proposal_origin = render.proposal_origin,
                                                         .semantic_color = note.semantic_color,
                                                         .bounds = rect,
                                                         .paint_preview = paint_target,
                                                         .paint_erase_preview = erase_preview,
                                                         .paint_invalid_preview = invalid_preview});
    return true;
}

template <typename Operate, typename Consume, typename YForRow, typename BandForRow>
auto append_indexed_note(scene_note_composition_t& composition,
                         const editor_field_scene_note_source_t& note, Operate&& operate,
                         Consume&& consume, YForRow&& y_for_row, BandForRow&& band_for_row)
    -> bool {
    if (!is_note_visible_in_scene(note, composition.geometry)) {
        return true;
    }
    if (!record_paint_overlaps(note, composition.paint, operate)) {
        return false;
    }
    const auto selected = composition.selected.contains(note.note.value);
    const auto origin = composition.duration.origin_notes.contains(note.note.value) ||
                        composition.moves.origin_notes.contains(note.note.value) ||
                        composition.holds.origin_notes.contains(note.note.value);
    note_render_context_t render{composition.geometry, composition.visible_bands, selected, origin};
    if (!append_note_hold_bodies(note, render, operate, consume, y_for_row) ||
        !append_indexed_note_endpoint(composition, note, render, note.begin_row, false, consume,
                                      y_for_row, band_for_row)) {
        return false;
    }
    return note.end_row <= note.begin_row ||
           append_indexed_note_endpoint(composition, note, render, note.end_row, true, consume,
                                        y_for_row, band_for_row);
}

template <typename Operate, typename Consume, typename YForRow, typename BandForRow>
auto compose_indexed_notes(scene_note_composition_t& composition, Operate&& operate,
                           Consume&& consume, YForRow&& y_for_row, BandForRow&& band_for_row)
    -> bool {
    const auto& index = composition.geometry.index;
    const auto after_candidates = std::ranges::upper_bound(
        index.notes, composition.geometry.query_high,
        [](long double row, std::uint64_t begin) { return row < static_cast<long double>(begin); },
        &editor_field_scene_note_source_t::begin_row);
    const auto candidate_count =
        static_cast<std::size_t>(std::distance(index.notes.begin(), after_candidates));
    composition.paint_endpoint_cells.reserve(composition.paint.visible_cells.size());
    bool failed{};
    const auto visit = [&](auto&& self, std::size_t node, std::size_t first,
                           std::size_t last) -> void {
        if (failed || first >= candidate_count ||
            static_cast<long double>(index.note_tree_max_end[node]) <
                composition.geometry.query_low) {
            return;
        }
        if (!operate()) {
            failed = true;
            return;
        }
        if (last - first == 1) {
            if (first < index.notes.size()) {
                failed = !append_indexed_note(composition, index.notes[first], operate, consume,
                                              y_for_row, band_for_row);
            }
            return;
        }
        const auto middle = first + (last - first) / 2;
        self(self, node * 2, first, middle);
        self(self, node * 2 + 1, middle, last);
    };
    if (candidate_count != 0) {
        visit(visit, 1, 0, index.note_tree_leaf_base);
    }
    if (failed) {
        composition.geometry.diagnostics.push_back(error(
            diagnostic_code_t::invalid_resolution,
            "field-scene note composition exceeds its configured primitive or operation cap"));
        return false;
    }
    for (const auto& [endpoint_index, cell] : composition.paint_endpoint_cells) {
        auto& endpoint = composition.geometry.scene.note_endpoints[endpoint_index];
        if (composition.paint.invalid_cells.contains(cell)) {
            endpoint.paint_erase_preview = false;
            endpoint.paint_invalid_preview = true;
        }
    }
    return true;
}

template <typename Operate, typename Consume>
auto compose_remaining_paint_targets(scene_note_composition_t& composition, Operate&& operate,
                                     Consume&& consume) -> bool {
    for (const auto& target : composition.paint.visible_targets) {
        if (composition.paint.consumed_cells.contains(target.cell)) {
            continue;
        }
        if (!operate() || !consume()) {
            composition.geometry.diagnostics.push_back(error(
                diagnostic_code_t::invalid_resolution,
                "field-scene paint preview exceeds its configured primitive or operation cap"));
            return false;
        }
        composition.geometry.scene.note_endpoints.push_back(
            {.note = object_id_t{},
             .lane = target.cell.lane,
             .glyph = target.glyph,
             .kind = note_kind_t::tap,
             .tail = false,
             .selected = false,
             .semantic_color = std::nullopt,
             .bounds = target.bounds,
             .paint_preview = true,
             .paint_erase_preview = false,
             .paint_invalid_preview = composition.paint.invalid_cells.contains(target.cell)});
    }
    return true;
}

template <typename Operate, typename YForRow, typename BandForRow>
auto append_visible_paint_target(scene_geometry_context_t& context, scene_paint_preview_t& paint,
                                 const note_paint_target_t& target, Operate&& operate,
                                 YForRow&& y_for_row, BandForRow&& band_for_row) -> bool {
    if (!operate()) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution,
                  "field-scene paint preview exceeds its configured operation cap"));
        return false;
    }
    const auto row = absolute_row(context.index.measures, target.position);
    if (!row || *row >= context.index.measures.total_rows) {
        context.diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
                                            "field-scene paint target is outside chart geometry"));
        return false;
    }
    if (!has_lane(layout_at_row(context.index, *row), target.lane)) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::unknown_lane, "field-scene paint target has no active lane"));
        return false;
    }
    const paint_cell_key_t cell{target.lane, *row};
    if (!paint.target_cells.insert(cell).second) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::collision, "field-scene paint targets must be unique"));
        return false;
    }
    if (static_cast<long double>(*row) < context.query_low ||
        static_cast<long double>(*row) > context.query_high) {
        return true;
    }
    const auto* band = band_for_row(*row);
    if (band == nullptr) {
        return true;
    }
    const auto* lane = lane_bounds(*band, target.lane);
    if (lane == nullptr) {
        return true;
    }
    const auto y = y_for_row(*row);
    const auto [top, bottom] = endpoint_vertical_bounds(
        y, context.request.note_head_height_pixels, lane->glyph, context.request.scroll_direction);
    const editor_field_rect_t bounds{lane->left, top, lane->right, bottom};
    if (is_rect_intersecting_viewport(bounds, context.view.viewport_height_pixels)) {
        paint.visible_cells.insert(cell);
        paint.visible_rows_by_lane[target.lane.value].push_back(*row);
        paint.visible_targets.push_back({.cell = cell, .glyph = lane->glyph, .bounds = bounds});
    }
    return true;
}

template <typename Operate, typename YForRow, typename BandForRow>
auto collect_visible_paint_targets(scene_geometry_context_t& context, scene_paint_preview_t& paint,
                                   Operate&& operate, YForRow&& y_for_row,
                                   BandForRow&& band_for_row) -> bool {
    if (!context.view.pending_note_paint_gesture) {
        return true;
    }
    const auto& targets = *context.view.pending_note_paint_gesture->targets;
    paint.target_cells.reserve(targets.size());
    paint.visible_cells.reserve(targets.size());
    paint.consumed_cells.reserve(targets.size());
    paint.invalid_cells.reserve(targets.size());
    paint.overlap_counts.reserve(targets.size());
    paint.visible_targets.reserve(targets.size());
    for (const auto& target : targets) {
        if (!append_visible_paint_target(context, paint, target, operate, y_for_row,
                                         band_for_row)) {
            return false;
        }
    }
    for (auto& [lane, rows] : paint.visible_rows_by_lane) {
        (void)lane;
        std::ranges::sort(rows);
    }
    return true;
}

template <typename Consume, typename YForRow>
auto append_visible_layout_band(scene_geometry_context_t& context,
                                const editor_field_scene_layout_source_t& source,
                                std::uint64_t source_end, Consume&& consume, YForRow&& y_for_row,
                                std::vector<visible_band_t>& visible_bands) -> bool {
    const auto& request = context.request;
    const auto lanes = editor_presented_lanes(source.layout, request.scratch_on_right);
    if (lanes.empty() && !context.view.pending_note_move_gesture) {
        return true;
    }
    const auto split = source.layout.split_stage && source.layout.split_after_column
                           ? std::min<std::size_t>(*source.layout.split_after_column, lanes.size())
                           : lanes.size();
    const auto gutter = split > 0 && split < lanes.size() ? request.split_stage_gutter_pixels : 0.0;
    if (gutter >= request.field_width_pixels) {
        context.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                            "split-stage gutter consumes the note field"));
        return false;
    }
    const auto clipped_begin =
        std::max(context.query_low, static_cast<long double>(source.begin_row));
    const auto clipped_end = std::min(context.query_high, static_cast<long double>(source_end));
    if (!consume(1 + lanes.size())) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution,
                  "field-scene geometry exceeds its configured primitive cap"));
        return false;
    }
    const auto scene_band = context.scene.layout_bands.size();
    const auto [band_top, band_bottom] =
        ordered_vertical_bounds(y_for_row(clipped_begin), y_for_row(clipped_end));
    context.scene.layout_bands.push_back({.begin_row = source.begin_row,
                                          .end_row = source_end,
                                          .top_pixels = band_top,
                                          .bottom_pixels = band_bottom,
                                          .profile_id = source.layout.profile_id,
                                          .split_stage = source.layout.split_stage});
    visible_band_t band{.begin_row = source.begin_row,
                        .end_row = source_end,
                        .clipped_begin_row = clipped_begin,
                        .clipped_end_row = clipped_end,
                        .scene_band = scene_band,
                        .layout = &source.layout};
    band.lanes.reserve(lanes.size());
    for (std::size_t lane_index = 0; lane_index < lanes.size(); ++lane_index) {
        const auto [left, right] = editor_presented_lane_bounds(source.layout, lanes.size(),
            lane_index, request.field_left_pixels, request.field_width_pixels,
            request.split_stage_gutter_pixels);
        band.lanes.push_back({lanes[lane_index]->id, lanes[lane_index]->glyph, left, right});
        context.scene.lanes.push_back({.layout_band = scene_band,
                                       .lane = lanes[lane_index]->id,
                                       .role = lanes[lane_index]->role,
                                       .stage = lanes[lane_index]->stage,
                                       .glyph = lanes[lane_index]->glyph,
                                       .display_order = lanes[lane_index]->display_order,
                                       .key_color = lanes[lane_index]->key_color,
                                       .bounds = {left, band_top, right, band_bottom}});
    }
    visible_bands.push_back(std::move(band));
    return true;
}

template <typename Operate, typename Consume, typename YForRow>
auto compose_visible_layouts(scene_geometry_context_t& context, Operate&& operate,
                             Consume&& consume, YForRow&& y_for_row,
                             std::vector<visible_band_t>& visible_bands) -> bool {
    for (const auto& source : context.index.layouts) {
        const auto source_end =
            &source == &context.index.layouts.back() ? context.canvas_rows : source.end_row;
        if (static_cast<long double>(source_end) <= context.query_low ||
            static_cast<long double>(source.begin_row) >= context.query_high) {
            continue;
        }
        if (!operate()) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "field-scene composition exceeds its configured operation cap"));
            return false;
        }
        if (!append_visible_layout_band(context, source, source_end, consume, y_for_row,
                                        visible_bands)) {
            return false;
        }
    }
    return true;
}

template <typename Operate, typename Consume, typename YForRow>
auto compose_comparison_rows(scene_geometry_context_t& context, Operate&& operate,
                             Consume&& consume, YForRow&& y_for_row) -> bool {
    std::vector<std::pair<std::uint64_t, local_position_t>> rows;
    rows.reserve(context.request.comparison_difference_rows.size());
    for (const auto position : context.request.comparison_difference_rows) {
        const auto row = absolute_row(context.index.measures, position);
        if (!row) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::row_out_of_range,
                      "field-scene comparison row is outside chart geometry"));
            return false;
        }
        rows.emplace_back(*row, position);
    }
    std::ranges::sort(rows,
                      [](const auto& left, const auto& right) { return left.first < right.first; });
    const auto duplicate = std::ranges::adjacent_find(
        rows, [](const auto& left, const auto& right) { return left.first == right.first; });
    if (duplicate != rows.end()) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::collision,
                  "field-scene comparison rows must be unique exact positions"));
        return false;
    }
    const auto half_height = context.request.comparison_difference_height_pixels / 2.0;
    for (const auto& [row, position] : rows) {
        if (static_cast<long double>(row) < context.query_low ||
            static_cast<long double>(row) > context.query_high) {
            continue;
        }
        if (!operate() || !consume()) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "field-scene comparison rows exceed a composition cap"));
            return false;
        }
        const auto y = y_for_row(row);
        const editor_field_rect_t bounds{context.request.field_left_pixels, y - half_height,
                                         context.request.field_left_pixels +
                                             context.request.field_width_pixels,
                                         y + half_height};
        if (is_rect_intersecting_viewport(bounds, context.view.viewport_height_pixels)) {
            context.scene.comparison_differences.push_back(
                {.position = position, .bounds = bounds});
        }
    }
    return true;
}

template <typename Consume, typename YForRow>
auto compose_scene_grid(scene_geometry_context_t& context, std::uint64_t visible_begin,
                        std::uint64_t visible_end, Consume&& consume, YForRow&& y_for_row) -> bool {
    const auto grid = make_editor_grid_lines(
        context.chart, {.visible_begin_row = visible_begin,
                        .visible_end_row = visible_end,
                        .active_snap_denominator = context.view.active_snap_denominator,
                        .include_virtual_bms_tail = context.request.include_virtual_bms_tail,
                        .pixels_per_standard_measure = context.view.pixels_per_standard_measure,
                        .density = context.request.grid_density,
                        .authored_groupings = context.request.authored_groupings,
                        .maximum_line_count = context.request.maximum_grid_lines});
    context.diagnostics = grid.diagnostics;
    if (!grid) {
        return false;
    }
    if (!consume(grid.lines.size())) {
        context.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                            "field-scene grid exceeds its primitive cap"));
        return false;
    }
    context.scene.grid_lines.reserve(grid.lines.size());
    for (const auto& line : grid.lines) {
        const auto row = static_cast<long double>(line.chart_row.whole_rows) +
                         to_long_double(line.chart_row.fractional_rows);
        context.scene.grid_lines.push_back({.y_pixels = y_for_row(row),
                                            .kind = line.kind,
                                            .emphasis = line.emphasis,
                                            .chart_row = line.chart_row});
    }
    return true;
}

auto scene_cursor_row(const scene_geometry_context_t& context) -> std::optional<std::uint64_t> {
    const auto anchor = context.request.fixed_cursor_screen_y_pixels
        ? context.request.fixed_cursor_screen_y_pixels : context.view.fixed_cursor_screen_y_pixels;
    if (!anchor) return context.view.virtual_bms_cursor_row
        ? context.view.virtual_bms_cursor_row : absolute_row(context.index.measures, context.view.cursor);
    const auto source = context.request.scroll_direction == editor_field_scroll_direction_t::down
        ? context.view.viewport_height_pixels - *anchor : *anchor;
    const auto row = (context.view.viewport_scroll_pixels + source) *
        static_cast<long double>(context.chart.divisions_per_standard_measure) /
        context.view.pixels_per_standard_measure;
    if (!context.canvas_rows) return std::nullopt;
    const auto high = std::min(context.query_high, static_cast<long double>(context.canvas_rows - 1));
    return static_cast<std::uint64_t>(std::clamp(row, std::min(std::max(0.0L, context.query_low), high), high));
}

template <typename Consume, typename YForRow, typename BandForRow>
auto compose_scene_cursor(scene_geometry_context_t& context, Consume&& consume, YForRow&& y_for_row,
                          BandForRow&& band_for_row) -> bool {
    (void)y_for_row;
    const auto row = scene_cursor_row(context);
    if (!row || static_cast<long double>(*row) < context.query_low ||
        static_cast<long double>(*row) > context.query_high) {
        return true;
    }
    const auto* band = band_for_row(*row);
    if (band == nullptr) {
        return true;
    }
    auto* lane = lane_bounds(*band, context.view.active_lane);
    if (!lane && (context.request.fixed_cursor_screen_y_pixels || context.view.fixed_cursor_screen_y_pixels))
        lane = band->lanes.empty() ? nullptr : &band->lanes.front();
    if (lane == nullptr) {
        return true;
    }
    if (!consume()) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution,
                  "field-scene cursor exceeds its configured primitive cap"));
        return false;
    }
    const auto y = editor_cursor_screen_y_pixels(context.view, *row,
        context.chart.divisions_per_standard_measure, context.request.scroll_direction,
        context.request.fixed_cursor_screen_y_pixels);
    context.scene.cursor = editor_field_cursor_geometry_t{
        .lane = lane->lane, .bounds = {lane->left, y - 1.0, lane->right, y + 1.0}};
    return true;
}

using visible_bgm_lanes_t = std::unordered_map<std::uint64_t, editor_field_rect_t>;

template <typename Operate, typename Consume>
auto append_visible_bgm_lanes(scene_geometry_context_t& context, Operate&& operate,
                              Consume&& consume, visible_bgm_lanes_t& visible_lanes) -> bool {
    const auto& chart = context.chart;
    const auto& request = context.request;
    const auto pane_left = request.bgm_pane_left_pixels;
    const auto pane_right = pane_left + request.bgm_pane_width_pixels;
    visible_lanes.reserve(chart.bgm_source_lanes.size());
    double content_offset{};
    for (std::size_t lane_index = 0; lane_index < chart.bgm_source_lanes.size(); ++lane_index) {
        if (!operate()) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "field-scene BGM lanes exceed the configured operation cap"));
            return false;
        }
        const auto& lane = chart.bgm_source_lanes[lane_index];
        const auto offset = request.bgm_lane_widths_pixels.empty()
            ? static_cast<double>(lane_index) * request.bgm_lane_width_pixels : content_offset;
        const auto unclipped_left = pane_left + offset - request.bgm_horizontal_scroll_pixels;
        const auto lane_width = editor_bgm_lane_width_pixels(chart, request, lane_index);
        const auto unclipped_right = unclipped_left + lane_width;
        content_offset += lane_width;
        if (unclipped_right <= pane_left || unclipped_left >= pane_right) {
            continue;
        }
        if (!consume()) {
            context.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "field-scene BGM lanes exceed the configured primitive cap"));
            return false;
        }
        const editor_field_rect_t bounds{std::max(pane_left, unclipped_left), 0.0,
                                         std::min(pane_right, unclipped_right),
                                         context.view.viewport_height_pixels};
        visible_lanes.emplace(lane.id.value, bounds);
        context.scene.bgm_lanes.push_back(
            {.source_lane = lane.id, .source_order = lane.source_order, .bounds = bounds,
             .unclipped_left_pixels = unclipped_left,
             .unclipped_right_pixels = unclipped_right});
    }
    return true;
}

template <typename Consume, typename YForRow>
auto append_bgm_geometry(scene_geometry_context_t& context,
                          const editor_field_scene_bgm_source_t& event,
                          const visible_bgm_lanes_t& lanes,
                          const std::unordered_set<std::uint64_t>& selected,
                          Consume&& consume, YForRow&& y_for_row) -> bool {
    if (!is_bgm_event_visible_in_scene(event, context.request)) return true;
    const auto lane = lanes.find(event.source_lane.value);
    if (lane == lanes.end()) return true;
    const auto [top, bottom] = endpoint_vertical_bounds(
        y_for_row(event.row), context.request.note_head_height_pixels, lane_glyph_t::bar,
        context.request.scroll_direction);
    const editor_field_rect_t head{lane->second.left, top, lane->second.right, bottom};
    auto bounds = head;
    const auto has_tail = event.end_row > event.row;
    if (has_tail) {
        const auto tail_y = y_for_row(event.end_row);
        bounds.top = std::min(bounds.top, tail_y);
        bounds.bottom = std::max(bounds.bottom, tail_y);
    }
    if (!is_rect_intersecting_viewport(bounds, context.view.viewport_height_pixels)) return true;
    if (!consume()) return false;
    context.scene.bgm_events.push_back(
        {.event = event.event, .source_lane = event.source_lane, .sound = event.sound,
         .bmson_restart = event.bmson_restart, .selected = selected.contains(event.event.value),
         .bounds = bounds, .head_bounds = head, .has_tail = has_tail,
         .semantic_color = event.semantic_color});
    return true;
}

template <typename Operate, typename Consume, typename YForRow>
auto append_visible_bgm_events(scene_geometry_context_t& context, const visible_bgm_lanes_t& lanes,
                               Operate&& operate, Consume&& consume, YForRow&& y_for_row) -> bool {
    std::unordered_set<std::uint64_t> selected;
    for (const auto& object : context.view.selected_objects)
        if (object.kind == editor_selection_object_kind_t::bgm_event) selected.insert(object.id.value);
    const auto& index = context.index;
    const auto after = std::ranges::upper_bound(
        index.bgm_events, context.query_high,
        [](long double row, std::uint64_t begin) { return row < static_cast<long double>(begin); },
        &editor_field_scene_bgm_source_t::row);
    const auto count = static_cast<std::size_t>(std::distance(index.bgm_events.begin(), after));
    bool failed{};
    const auto visit = [&](auto&& self, std::size_t node, std::size_t first, std::size_t last) -> void {
        if (failed || first >= count ||
            static_cast<long double>(index.bgm_tree_max_end[node]) < context.query_low) return;
        if (!operate()) { failed = true; return; }
        if (last - first == 1) {
            failed = !append_bgm_geometry(context, index.bgm_events[first], lanes, selected,
                                          consume, y_for_row);
            return;
        }
        const auto middle = first + (last - first) / 2;
        self(self, node * 2, first, middle);
        self(self, node * 2 + 1, middle, last);
    };
    if (count != 0) visit(visit, 1, 0, index.bgm_tree_leaf_base);
    if (failed) context.diagnostics.push_back(error(
        diagnostic_code_t::invalid_resolution,
        "field-scene BGM events exceed the configured primitive or operation cap"));
    return !failed;
}

template <typename Operate, typename Consume>
auto append_bgm_preview_lanes(scene_geometry_context_t& context, visible_bgm_lanes_t& lanes,
                              Operate&& operate, Consume&& consume) -> bool {
    if (!context.view.pending_note_move_gesture) return true;
    const auto& request = context.request;
    auto offset = editor_bgm_content_width_pixels(context.chart, request);
    for (const auto& lane : context.view.pending_note_move_gesture->new_bgm_lanes) {
        if (!operate()) return false;
        const auto left = request.bgm_pane_left_pixels + offset - request.bgm_horizontal_scroll_pixels;
        const auto right = left + request.bgm_lane_width_pixels;
        offset += request.bgm_lane_width_pixels;
        const auto pane_right = request.bgm_pane_left_pixels + request.bgm_pane_width_pixels;
        if (right <= request.bgm_pane_left_pixels || left >= pane_right) continue;
        if (!consume()) return false;
        const editor_field_rect_t bounds{std::max(left, request.bgm_pane_left_pixels), 0,
            std::min(right, pane_right), context.view.viewport_height_pixels};
        lanes.emplace(lane.id.value, bounds);
        context.scene.bgm_lanes.push_back({.source_lane = lane.id, .source_order = lane.source_order, .bounds = bounds,
            .unclipped_left_pixels = left, .unclipped_right_pixels = right});
    }
    return true;
}

template <typename Operate, typename Consume, typename YForRow>
auto append_bgm_move_previews(scene_geometry_context_t& context, const visible_bgm_lanes_t& lanes,
                              Operate&& operate, Consume&& consume, YForRow&& y_for_row) -> bool {
    if (!context.view.pending_note_move_gesture) return true;
    const auto& pending = *context.view.pending_note_move_gesture;
    for (const auto& preview : pending.bgm_preview) {
        if (!operate()) return false;
        const auto begin = preview.canvas_begin_row ? preview.canvas_begin_row : absolute_row(context.index.measures, preview.position);
        const auto end = preview.canvas_end_row ? preview.canvas_end_row
            : preview.end ? absolute_row(context.index.measures, *preview.end) : begin;
        if (!begin || !end) return false;
        const auto count = context.scene.bgm_events.size();
        const editor_field_scene_bgm_source_t source{.event = preview.note, .source_lane = preview.lane,
            .row = *begin, .end_row = *end, .sound = preview.sound};
        if (!append_bgm_geometry(context, source, lanes, {}, consume, y_for_row)) return false;
        if (context.scene.bgm_events.size() == count) continue;
        auto& event = context.scene.bgm_events.back();
        event.proposal_ghost = true;
        event.move_conflict = !pending.bgm_diagnostics.empty();
    }
    return true;
}

template <typename Operate, typename Consume, typename YForRow>
auto compose_bgm_pane(scene_geometry_context_t& context, Operate&& operate, Consume&& consume,
                      YForRow&& y_for_row) -> bool {
    const auto& request = context.request;
    if (request.bgm_pane_width_pixels <= 0.0) {
        return true;
    }
    const auto content_width = request.bgm_lane_widths_pixels.empty()
        ? static_cast<long double>(context.chart.bgm_source_lanes.size()) *
              static_cast<long double>(request.bgm_lane_width_pixels)
        : static_cast<long double>(editor_bgm_content_width_pixels(context.chart, request));
    const auto preview_width = context.view.pending_note_move_gesture
        ? context.view.pending_note_move_gesture->new_bgm_lanes.size() * request.bgm_lane_width_pixels : 0.0;
    const auto maximum_scroll =
        std::max(0.0L, content_width + preview_width - static_cast<long double>(request.bgm_pane_width_pixels));
    if (static_cast<long double>(request.bgm_horizontal_scroll_pixels) > maximum_scroll) {
        context.diagnostics.push_back(
            error(diagnostic_code_t::row_out_of_range,
                  "field-scene BGM horizontal scroll exceeds its content"));
        return false;
    }
    visible_bgm_lanes_t visible_lanes;
    return append_visible_bgm_lanes(context, operate, consume, visible_lanes) &&
           append_visible_bgm_events(context, visible_lanes, operate, consume, y_for_row) &&
           append_bgm_preview_lanes(context, visible_lanes, operate, consume) &&
           append_bgm_move_previews(context, visible_lanes, operate, consume, y_for_row);
}

template <typename Operate, typename Consume, typename YForRow>
auto append_bga_lane_events(scene_geometry_context_t& context,
                            const editor_field_bga_lane_geometry_t& lane,
                            const std::unordered_set<std::uint64_t>& selected,
                            Operate&& operate, Consume&& consume, YForRow&& y_for_row) -> bool {
    const auto& request = context.request;
    const auto lane_index = editor_bga_lane_index(lane.kind);
    const auto first_row = static_cast<std::uint64_t>(std::max(0.0L, std::ceil(context.query_low)));
    const auto last_row = static_cast<std::uint64_t>(std::max(0.0L, std::floor(context.query_high)));
    const auto first = std::ranges::lower_bound(context.index.bga_events, first_row, {},
                                                &editor_field_scene_bga_source_t::row);
    const auto after = std::ranges::upper_bound(first, context.index.bga_events.end(), last_row, {},
                                                &editor_field_scene_bga_source_t::row);
    std::optional<std::uint64_t> overlap_row;
    double overlap_count{};
    double overlap_index{};
    for (auto event = first; event != after; ++event) {
        if (editor_bga_lane_index(event->kind) != lane_index) continue;
        if (!operate()) {
            context.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                                "field-scene BGA events exceed operation cap"));
            return false;
        }
        if (overlap_row != event->row) {
            const auto same_row = std::ranges::equal_range(first, after, event->row, {},
                                                          &editor_field_scene_bga_source_t::row);
            overlap_count = static_cast<double>(std::ranges::count_if(same_row, [&](const auto& other) {
                return editor_bga_lane_index(other.kind) == lane_index;
            }));
            overlap_index = 0;
            overlap_row = event->row;
        }
        const auto event_overlap_index = overlap_index++;
        const bool stacked = overlap_count > bga_lane_overlap_stack_threshold;
        const auto lane_width = lane.bounds.right - lane.bounds.left;
        const auto width = stacked ? lane_width : lane_width / overlap_count;
        const auto x = stacked ? lane.bounds.left : lane.bounds.left + width * event_overlap_index;
        const auto y = y_for_row(event->row);
        const auto [top, bottom] = endpoint_vertical_bounds(
            y, request.note_head_height_pixels, lane_glyph_t::bar, request.scroll_direction);
        const editor_field_rect_t bounds{x, top, x + width, bottom};
        if (!is_rect_intersecting_viewport(bounds, context.view.viewport_height_pixels)) continue;
        if (!consume()) {
            context.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                                "field-scene BGA events exceed primitive cap"));
            return false;
        }
        context.scene.bga_events.push_back({.event = event->event, .resource = event->resource,
            .kind = event->kind, .row = event->row,
            .overlap_count = static_cast<std::size_t>(overlap_count),
            .selected = selected.contains(event->event.value), .bounds = bounds});
    }
    return true;
}

template <typename Operate, typename Consume, typename YForRow>
auto compose_bga_lane(scene_geometry_context_t& context, Operate&& operate, Consume&& consume,
                      YForRow&& y_for_row) -> bool {
    const auto& request = context.request;
    if (!context.view.bga_lane_visible || request.bga_pane_width_pixels <= 0.0) return true;
    const auto mask = editor_bga_visible_lane_mask(context.chart, context.view.bga_revealed_lane_mask);
    if (!mask) return true;
    const editor_field_rect_t bounds{request.bga_pane_left_pixels, 0.0,
        request.bga_pane_left_pixels + request.bga_pane_width_pixels, context.view.viewport_height_pixels};
    context.scene.bga_lane = editor_field_bga_lane_geometry_t{.bounds = bounds};
    constexpr std::array kinds{visual_event_kind_t::background, visual_event_kind_t::layer,
                               visual_event_kind_t::layer2, visual_event_kind_t::poor};
    const auto width = request.bga_pane_width_pixels / std::popcount(mask);
    std::unordered_set<std::uint64_t> selected;
    selected.reserve(context.view.selected_objects.size());
    for (const auto& object : context.view.selected_objects)
        if (object.kind == editor_selection_object_kind_t::bga_event)
            selected.insert(object.id.value);
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        if (!(mask & (1U << index))) continue;
        if (!operate() || !consume()) {
            context.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                                "field-scene BGA lane exceeds its configured cap"));
            return false;
        }
        const auto left = bounds.left + width * context.scene.bga_lanes.size();
        const editor_field_bga_lane_geometry_t lane{
            .bounds = {left, bounds.top, left + width, bounds.bottom}, .kind = kinds[index]};
        context.scene.bga_lanes.push_back(lane);
        if (!append_bga_lane_events(context, lane, selected, operate, consume, y_for_row)) return false;
    }
    return true;
}

auto scene_canvas_rows(const chart_document_t& chart, const editor_field_scene_index_t& index,
                       const editor_field_scene_request_t& request, diagnostic_list_t& diagnostics)
    -> std::optional<std::uint64_t> {
    if (!request.include_virtual_bms_tail || !has_chart_virtual_tail(chart)) {
        return index.measures.total_rows;
    }
    auto built = build_virtual_canvas(chart);
    if (const auto* failures = std::get_if<diagnostic_list_t>(&built)) {
        diagnostics = *failures;
        return std::nullopt;
    }
    return std::get<virtual_canvas_t>(built).total_rows;
}

auto is_valid_scene_paint_preview(const editor_interaction_view_model_t& view,
                               const editor_field_scene_index_t& index,
                               diagnostic_list_t& diagnostics) -> bool {
    if (!view.pending_note_paint_gesture) {
        return true;
    }
    const auto& pending = *view.pending_note_paint_gesture;
    if (pending.document_revision != index.document_revision) {
        diagnostics.push_back(
            error(diagnostic_code_t::invalid_hold, "field-scene paint preview is stale"));
        return false;
    }
    if (!pending.targets || pending.targets->empty() ||
        pending.targets->size() > maximum_note_paint_targets) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                    "field-scene paint preview target count is invalid"));
        return false;
    }
    return true;
}

auto is_valid_scene_semantic_filter(const chart_document_t& chart,
                                 const editor_field_scene_request_t& request) -> bool {
    const auto known_layer = [&](layer_id_t id) {
        return std::ranges::find(chart.semantic_layers, id, &semantic_layer_t::id) !=
               chart.semantic_layers.end();
    };
    const auto valid_filter = [&](const std::vector<layer_id_t>& layers) {
        std::unordered_set<std::uint64_t> ids;
        for (const auto layer : layers) {
            if (!layer || !known_layer(layer) || !ids.insert(layer.value).second) {
                return false;
            }
        }
        return true;
    };
    const auto contradictory =
        std::ranges::any_of(request.required_semantic_layers, [&](layer_id_t layer) {
            return std::ranges::find(request.excluded_semantic_layers, layer) !=
                   request.excluded_semantic_layers.end();
        });
    return (!request.solo_semantic_layer || known_layer(*request.solo_semantic_layer)) &&
           valid_filter(request.required_semantic_layers) &&
           valid_filter(request.excluded_semantic_layers) && !contradictory;
}

struct scene_visible_rows_t {
    std::uint64_t canvas_rows{};
    std::uint64_t begin{};
    std::uint64_t end{};
    long double query_low{};
    long double query_high{};
};

auto visible_scene_rows(const chart_document_t& chart, const editor_interaction_view_model_t& view,
                        const editor_field_scene_request_t& request, std::uint64_t canvas_rows)
    -> scene_visible_rows_t {
    const auto pixels_per_row = static_cast<long double>(view.pixels_per_standard_measure) /
                                chart.divisions_per_standard_measure;
    const auto row_at_pixel = [&](long double pixel) {
        return (pixel + static_cast<long double>(view.viewport_scroll_pixels)) / pixels_per_row;
    };
    const auto low = std::clamp(row_at_pixel(0), 0.0L, static_cast<long double>(canvas_rows));
    const auto high = std::clamp(row_at_pixel(view.viewport_height_pixels), 0.0L,
                                 static_cast<long double>(canvas_rows));
    const auto overscan =
        static_cast<long double>(request.note_head_height_pixels) / pixels_per_row;
    auto begin = static_cast<std::uint64_t>(std::floor(low));
    auto end = static_cast<std::uint64_t>(std::ceil(high));
    begin = std::min(begin, canvas_rows - 1);
    end = std::clamp(end, begin + 1, canvas_rows);
    return {.canvas_rows = canvas_rows,
            .begin = begin,
            .end = end,
            .query_low = std::max(0.0L, low - overscan),
            .query_high = std::min(static_cast<long double>(canvas_rows), high + overscan)};
}

auto prepare_scene_visible_rows(const chart_document_t& chart,
                                const editor_field_scene_index_t& index,
                                const editor_interaction_view_model_t& view,
                                const editor_field_scene_request_t& request,
                                diagnostic_list_t& diagnostics)
    -> std::optional<scene_visible_rows_t> {
    if (!is_valid_scene_index(chart, index, view) ||
        !is_valid_scene_request_geometry(request, view.viewport_height_pixels) ||
        !is_valid_scene_request_limits(request) || !is_valid_scene_view(view)) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                    "field-scene index, view, or geometry is invalid"));
        return std::nullopt;
    }
    const auto canvas_rows = scene_canvas_rows(chart, index, request, diagnostics);
    if (!canvas_rows) {
        return std::nullopt;
    }
    const auto content_height = static_cast<double>(*canvas_rows) * view.pixels_per_standard_measure /
                                static_cast<double>(chart.divisions_per_standard_measure);
    const auto maximum_scroll =
        editor_maximum_viewport_scroll(content_height, view.viewport_height_pixels,
                                       view.viewport_end_margin_fraction);
    if (!is_viewport_scroll_in_range(view.viewport_scroll_pixels, view.viewport_height_pixels,
                                  maximum_scroll, view.viewport_minimum_margin_fraction)) {
        diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
                                    "field-scene viewport scroll exceeds chart content"));
        return std::nullopt;
    }
    if (!is_valid_scene_paint_preview(view, index, diagnostics)) {
        return std::nullopt;
    }
    if (!is_valid_scene_semantic_filter(chart, request)) {
        diagnostics.push_back(
            error(diagnostic_code_t::invalid_id,
                  "field-scene semantic-layer filter refers to invalid chart layers"));
        return std::nullopt;
    }
    return visible_scene_rows(chart, view, request, *canvas_rows);
}

auto is_valid_note_transform_previews(const editor_interaction_view_model_t& view,
                                   diagnostic_list_t& diagnostics) -> bool {
    const auto count = static_cast<unsigned>(view.pending_hold.has_value() ||
                                             view.pending_virtual_hold.has_value() ||
                                             !view.pending_canvas_keyboard_holds.empty()) +
                       static_cast<unsigned>(view.pending_note_duration_gesture.has_value()) +
                       static_cast<unsigned>(view.pending_note_move_gesture.has_value());
    if (count > 1) {
        diagnostics.push_back(
            error(diagnostic_code_t::invalid_hold,
                  "field-scene cannot compose simultaneous note-transform previews"));
        return false;
    }
    return true;
}

auto visible_band_at_row(const std::vector<visible_band_t>& bands, std::uint64_t row)
    -> const visible_band_t* {
    const auto after = std::ranges::upper_bound(bands, row, {}, &visible_band_t::begin_row);
    if (after == bands.begin()) {
        return nullptr;
    }
    const auto& candidate = *std::prev(after);
    return row < candidate.end_row ? &candidate : nullptr;
}

} // namespace

auto editor_bgm_lane_width_pixels(const chart_document_t& chart,
                                  const editor_field_scene_request_t& request,
                                  std::size_t lane_index) -> double {
    if (request.bgm_lane_widths_pixels.empty()) return request.bgm_lane_width_pixels;
    if (lane_index >= chart.bgm_source_lanes.size() ||
        lane_index >= request.bgm_lane_widths_pixels.size() ||
        !chart.bgm_source_lanes[lane_index].stem_channel) return 36.0;
    const auto width = request.bgm_lane_widths_pixels[lane_index];
    return std::isnan(width) ? 36.0 : std::clamp(width, 36.0, 480.0);
}

auto editor_bgm_content_width_pixels(const chart_document_t& chart,
                                     const editor_field_scene_request_t& request) -> double {
    if (request.bgm_lane_widths_pixels.empty())
        return static_cast<double>(static_cast<long double>(chart.bgm_source_lanes.size()) *
                                   static_cast<long double>(request.bgm_lane_width_pixels));
    long double width{};
    for (std::size_t lane = 0; lane < chart.bgm_source_lanes.size(); ++lane)
        width += editor_bgm_lane_width_pixels(chart, request, lane);
    return static_cast<double>(std::min(width,
        static_cast<long double>(std::numeric_limits<double>::max())));
}

auto make_editor_field_scene(const chart_document_t& chart, const editor_field_scene_index_t& index,
                             const editor_interaction_view_model_t& view,
                             const editor_field_scene_request_t& request)
    -> editor_field_scene_result_t {
    editor_field_scene_result_t result;
    const auto visible =
        prepare_scene_visible_rows(chart, index, view, request, result.diagnostics);
    if (!visible) {
        return result;
    }

    editor_field_scene_t scene{.visible_begin_row = visible->begin,
                               .visible_end_row = visible->end};
    std::size_t primitive_count{};
    std::size_t operations{};
    const auto consume = [&](std::size_t count = 1) -> bool {
        if (count > request.maximum_scene_primitives - primitive_count) {
            return false;
        }
        primitive_count += count;
        return true;
    };
    const auto operate = [&]() -> bool {
        if (operations >= request.maximum_composition_operations) {
            return false;
        }
        ++operations;
        return true;
    };
    const auto y_for_row = [&](long double row) {
        const auto offset = row * static_cast<long double>(view.pixels_per_standard_measure) /
                                chart.divisions_per_standard_measure -
                            static_cast<long double>(view.viewport_scroll_pixels);
        return static_cast<double>(request.scroll_direction == editor_field_scroll_direction_t::down
                                       ? static_cast<long double>(view.viewport_height_pixels) -
                                             offset
                                       : offset);
    };

    scene_geometry_context_t geometry{chart,
                                      index,
                                      view,
                                      request,
                                      scene,
                                      result.diagnostics,
                                      visible->query_low,
                                      visible->query_high,
                                      visible->canvas_rows};
    std::vector<visible_band_t> visible_bands;
    if (!compose_visible_layouts(geometry, operate, consume, y_for_row, visible_bands)) {
        return result;
    }
    const auto visible_band_for_row = [&](std::uint64_t row) -> const visible_band_t* {
        return visible_band_at_row(visible_bands, row);
    };

    scene_paint_preview_t paint_preview;
    if (!collect_visible_paint_targets(geometry, paint_preview, operate, y_for_row,
                                       visible_band_for_row)) {
        return result;
    }

    if (!compose_comparison_rows(geometry, operate, consume, y_for_row)) {
        return result;
    }

    if (!compose_scene_grid(geometry, visible->begin, visible->end, consume, y_for_row)) {
        return result;
    }

    std::unordered_set<std::uint64_t> selected;
    selected.reserve(view.selected_notes.size());
    for (const auto note : view.selected_notes) {
        selected.insert(note.value);
    }
    if (!is_valid_note_transform_previews(view, result.diagnostics)) {
        return result;
    }
    duration_proposals_t duration;
    if (!collect_duration_proposals(geometry, duration, operate)) {
        return result;
    }
    move_proposals_t moves;
    if (!collect_move_proposals(geometry, moves, operate)) {
        return result;
    }
    hold_proposals_t holds;
    if (!collect_regular_holds(geometry, holds, operate) ||
        !collect_virtual_holds(geometry, holds, operate)) {
        return result;
    }
    scene_note_composition_t note_composition{geometry, paint_preview, selected,      duration,
                                              moves,    holds,         visible_bands, {}};
    if (!compose_indexed_notes(note_composition, operate, consume, y_for_row,
                               visible_band_for_row) ||
        !compose_remaining_paint_targets(note_composition, operate, consume)) {
        return result;
    }

    proposal_render_context_t proposal_render{geometry, visible_bands};
    scene_proposal_composition_t composition{proposal_render, duration, moves, holds, selected};
    if (!compose_note_proposals(composition, operate, consume, y_for_row, visible_band_for_row)) {
        return result;
    }

    if (!compose_bgm_pane(geometry, operate, consume, y_for_row)) {
        return result;
    }
    if (!compose_bga_lane(geometry, operate, consume, y_for_row)) {
        return result;
    }

    if (!compose_scene_cursor(geometry, consume, y_for_row, visible_band_for_row)) {
        return result;
    }

    mark_scene_move_occupants(scene, view);
    result.scene = std::move(scene);
    return result;
}

namespace {
auto window_request_scalars(const editor_field_scene_request_t& request) {
    return std::tuple{request.field_left_pixels, request.field_width_pixels, request.scratch_on_right,
        request.split_stage_gutter_pixels, request.note_head_height_pixels,
        request.scroll_direction, request.comparison_difference_height_pixels,
        request.hold_body_width_ratio, request.bgm_pane_left_pixels,
        request.bgm_pane_width_pixels, request.bgm_lane_width_pixels,
        request.bgm_horizontal_scroll_pixels, request.bga_pane_left_pixels,
        request.bga_pane_width_pixels, request.include_virtual_bms_tail,
        request.solo_semantic_layer, request.maximum_grid_lines,
        request.maximum_authored_groupings, request.maximum_comparison_difference_rows,
        request.maximum_scene_primitives, request.maximum_composition_operations,
        request.grid_density.minimum_group_spacing_pixels,
        request.grid_density.minimum_beat_spacing_pixels,
        request.grid_density.minimum_active_snap_spacing_pixels};
}
auto has_window_edit_preview(const editor_interaction_view_model_t& view) -> bool {
    return view.pending_hold || !view.additional_keyboard_holds.empty() ||
        view.pending_virtual_hold || !view.pending_canvas_keyboard_holds.empty() ||
        view.pending_note_duration_gesture || view.pending_note_move_gesture ||
        view.pending_note_paint_gesture;
}
auto is_window_bgm_width_current(const editor_field_scene_window_t& window,
                             const editor_field_scene_request_t& request) -> bool {
    const auto widths = request.bgm_lane_widths_pixels;
    if (window.bgm_lane_widths_snapshot.size() != widths.size()) return false;
    for (std::size_t index = 0; index < widths.size(); ++index) {
        const auto previous = window.bgm_lane_widths_snapshot[index];
        if (previous != widths[index] && !(std::isnan(previous) && std::isnan(widths[index])))
            return false;
    }
    return true;
}
auto scene_window_height(const editor_interaction_view_model_t& view,
                         const editor_field_scene_request_t& request) -> double {
    return view.viewport_height_pixels * 3.0 +
           request.retained_window_minimum_margin_pixels * 2.0;
}
auto is_window_key_current(const editor_field_scene_window_t& window,
                        const editor_interaction_view_model_t& view,
                        const editor_field_scene_request_t& request) -> bool {
    const auto& previous = window.view;
    return window.scene && previous.chart == view.chart &&
        previous.document_revision == view.document_revision &&
        previous.pixels_per_standard_measure == view.pixels_per_standard_measure &&
        previous.viewport_height_pixels == scene_window_height(view, request) &&
        previous.bga_lane_visible == view.bga_lane_visible &&
        previous.bga_revealed_lane_mask == view.bga_revealed_lane_mask &&
        window.request.retained_window_minimum_margin_pixels ==
            request.retained_window_minimum_margin_pixels &&
        previous.active_snap_denominator == view.active_snap_denominator &&
        previous.selected_notes == view.selected_notes &&
        previous.selected_objects == view.selected_objects &&
        is_window_bgm_width_current(window, request) &&
        window_request_scalars(window.request) == window_request_scalars(request) &&
        window.request.required_semantic_layers == request.required_semantic_layers &&
        window.request.excluded_semantic_layers == request.excluded_semantic_layers;
}
auto is_view_within_window(const editor_field_scene_window_t& window,
                          const editor_interaction_view_model_t& view) -> bool {
    const auto begin = window.view.viewport_scroll_pixels;
    const auto end = begin + window.view.viewport_height_pixels;
    const auto height = view.viewport_height_pixels;
    const auto minimum_margin = std::max(height * 0.25,
        window.request.retained_window_minimum_margin_pixels);
    const auto low_margin = view.viewport_scroll_pixels - begin;
    const auto high_margin = end - view.viewport_scroll_pixels - height;
    // At the chart's beginning, the missing leading margin contains no objects.
    return low_margin >= std::min(minimum_margin, view.viewport_scroll_pixels) &&
           high_margin >= minimum_margin;
}
auto scene_y_for_row(const chart_document_t& chart, const editor_interaction_view_model_t& view,
                     editor_field_scroll_direction_t direction, long double row) -> double {
    const auto offset = row * static_cast<long double>(view.pixels_per_standard_measure) /
                            chart.divisions_per_standard_measure -
                        static_cast<long double>(view.viewport_scroll_pixels);
    return static_cast<double>(direction == editor_field_scroll_direction_t::down
                                   ? static_cast<long double>(view.viewport_height_pixels) - offset
                                   : offset);
}
auto translated_window_rect(editor_field_rect_t rect, double shift) -> editor_field_rect_t {
    rect.top += shift;
    rect.bottom += shift;
    return rect;
}
template <typename Geometry>
void project_window_rects(const std::vector<Geometry>& source, std::vector<Geometry>& target,
                          double shift, double height) {
    target.reserve(source.size());
    for (const auto& value : source) {
        auto translated = value;
        translated.bounds = translated_window_rect(value.bounds, shift);
        if constexpr (std::is_same_v<Geometry, editor_field_bgm_event_geometry_t>)
            translated.head_bounds = translated_window_rect(value.head_bounds, shift);
        if (is_rect_intersecting_viewport(translated.bounds, height))
            target.push_back(std::move(translated));
    }
}
void project_window_holds(const editor_field_scene_t& source, editor_field_scene_t& target,
                          double shift, double clip_top, double clip_bottom) {
    target.hold_bodies.reserve(source.hold_bodies.size());
    for (const auto& value : source.hold_bodies) {
        auto translated = value;
        translated.bounds = translated_window_rect(value.bounds, shift);
        translated.bounds.top = std::max(translated.bounds.top, clip_top);
        translated.bounds.bottom = std::min(translated.bounds.bottom, clip_bottom);
        if (translated.bounds.top < translated.bounds.bottom)
            target.hold_bodies.push_back(std::move(translated));
    }
}
void project_window_grid(const editor_field_scene_t& source, editor_field_scene_t& target,
                         double shift) {
    for (const auto& value : source.grid_lines) {
        if (value.chart_row.whole_rows < target.visible_begin_row ||
            value.chart_row.whole_rows >= target.visible_end_row)
            continue;
        auto translated = value;
        translated.y_pixels += shift;
        target.grid_lines.push_back(std::move(translated));
    }
}
auto is_valid_window_request(const chart_document_t& chart, const editor_field_scene_index_t& index,
                          const editor_interaction_view_model_t& view,
                          const editor_field_scene_request_t& request) -> bool {
    return std::isfinite(request.retained_window_minimum_margin_pixels) &&
           request.retained_window_minimum_margin_pixels >= 0 &&
           std::isfinite(scene_window_height(view, request)) &&
           !has_window_edit_preview(view) && request.authored_groupings.empty() &&
           request.comparison_difference_rows.empty() && is_valid_scene_index(chart, index, view) &&
           is_valid_scene_view(view) &&
           is_valid_scene_request_geometry(request, view.viewport_height_pixels);
}
auto is_reusable_window_current(const editor_field_scene_window_t& window,
                             const editor_interaction_view_model_t& view,
                             const editor_field_scene_request_t& request) -> bool {
    return is_window_key_current(window, view, request) && is_view_within_window(window, view);
}
auto is_window_scroll_valid(const chart_document_t& chart, const editor_interaction_view_model_t& view,
                         const editor_field_scene_window_t& window) -> bool {
    const auto content_height = static_cast<double>(window.canvas_rows) *
                                view.pixels_per_standard_measure /
                                static_cast<double>(chart.divisions_per_standard_measure);
    return is_viewport_scroll_in_range(
        view.viewport_scroll_pixels, view.viewport_height_pixels,
        editor_maximum_viewport_scroll(content_height, view.viewport_height_pixels,
                                       view.viewport_end_margin_fraction),
        view.viewport_minimum_margin_fraction);
}
auto project_scene_window_metadata_unchecked(const chart_document_t& chart,
                                             const editor_field_scene_index_t& index,
                                             const editor_interaction_view_model_t& view,
                                             const editor_field_scene_request_t& request,
                                             const editor_field_scene_window_t& window)
    -> editor_field_scene_window_metadata_t {
    editor_field_scene_window_metadata_t metadata;
    auto& result = metadata.projection;
    const auto visible = visible_scene_rows(chart, view, request, window.canvas_rows);
    editor_field_scene_t scene{.visible_begin_row = visible.begin, .visible_end_row = visible.end};
    scene_geometry_context_t geometry{chart,
                                      index,
                                      view,
                                      request,
                                      scene,
                                      result.diagnostics,
                                      visible.query_low,
                                      visible.query_high,
                                      visible.canvas_rows};
    const auto y_for_row = [&](long double row) {
        return scene_y_for_row(chart, view, request.scroll_direction, row);
    };
    const auto consume = [](std::size_t = 1) { return true; };
    const auto operate = [] { return true; };
    std::vector<visible_band_t> bands;
    if (!compose_visible_layouts(geometry, operate, consume, y_for_row, bands))
        return metadata;
    const auto [clip_top, clip_bottom] =
        ordered_vertical_bounds(y_for_row(visible.query_low), y_for_row(visible.query_high));
    metadata.hold_clip_top = clip_top;
    metadata.hold_clip_bottom = clip_bottom;
    visible_bgm_lanes_t bgm_lanes;
    if (request.bgm_pane_width_pixels > 0 &&
        !append_visible_bgm_lanes(geometry, operate, consume, bgm_lanes))
        return metadata;
    if (!compose_bga_lane(geometry, operate, consume, y_for_row))
        return metadata;
    const auto band_for_row = [&](std::uint64_t row) { return visible_band_at_row(bands, row); };
    if (!compose_scene_cursor(geometry, consume, y_for_row, band_for_row))
        return metadata;
    result.scene = std::move(scene);
    return metadata;
}
auto project_scene_window(const chart_document_t& chart, const editor_field_scene_index_t& index,
                          const editor_interaction_view_model_t& view,
                          const editor_field_scene_request_t& request,
                          const editor_field_scene_window_t& window)
    -> editor_field_scene_result_t {
    auto metadata = project_scene_window_metadata_unchecked(chart, index, view, request, window);
    auto result = std::move(metadata.projection);
    if (!result)
        return result;
    auto& scene = *result.scene;
    // The metadata pass composes BGA events for metadata-only callers. The full
    // window path projects its cached event geometry below, so keep only that
    // shifted copy in the returned scene.
    scene.bga_events.clear();
    const auto shift = request.scroll_direction == editor_field_scroll_direction_t::down
                           ? view.viewport_height_pixels - window.view.viewport_height_pixels +
                                 view.viewport_scroll_pixels - window.view.viewport_scroll_pixels
                           : window.view.viewport_scroll_pixels - view.viewport_scroll_pixels;
    project_window_grid(*window.scene, scene, shift);
    project_window_rects(window.scene->note_endpoints, scene.note_endpoints, shift,
                         view.viewport_height_pixels);
    project_window_holds(*window.scene, scene, shift, metadata.hold_clip_top,
                         metadata.hold_clip_bottom);
    project_window_rects(window.scene->bgm_events, scene.bgm_events, shift,
                         view.viewport_height_pixels);
    project_window_rects(window.scene->bga_events, scene.bga_events, shift,
                         view.viewport_height_pixels);
    return result;
}
auto is_projected_window_within_limit(const editor_field_scene_result_t& result,
                                 const editor_field_scene_request_t& request) -> bool {
    if (!result)
        return false;
    const auto& scene = *result.scene;
    const auto count = scene.layout_bands.size() + scene.lanes.size() +
                       scene.comparison_differences.size() + scene.grid_lines.size() +
                       scene.hold_bodies.size() + scene.note_endpoints.size() +
                       scene.bgm_lanes.size() + scene.bgm_events.size() +
                       scene.bga_lanes.size() +
                       scene.bga_events.size() +
                       static_cast<std::size_t>(scene.cursor.has_value());
    return count <= request.maximum_scene_primitives;
}
auto rebuild_scene_window(const chart_document_t& chart, const editor_field_scene_index_t& index,
                          const editor_interaction_view_model_t& view,
                          const editor_field_scene_request_t& request,
                          editor_field_scene_window_t& window) -> bool {
    auto expanded = view;
    expanded.viewport_height_pixels = scene_window_height(view, request);
    expanded.viewport_scroll_pixels =
        std::max(0.0, view.viewport_scroll_pixels - view.viewport_height_pixels -
                         request.retained_window_minimum_margin_pixels);
    // A tall window at the end may exceed the allowed viewport scroll. Keep its
    // beginning in the content, while preserving coverage of the original view.
    diagnostic_list_t diagnostics;
    const auto canvas = scene_canvas_rows(chart, index, request, diagnostics);
    if (!canvas)
        return false;
    const auto content_height = static_cast<double>(*canvas) * view.pixels_per_standard_measure /
                                static_cast<double>(chart.divisions_per_standard_measure);
    expanded.viewport_scroll_pixels =
        std::min(expanded.viewport_scroll_pixels,
                 editor_maximum_viewport_scroll(content_height, expanded.viewport_height_pixels,
                                                expanded.viewport_end_margin_fraction));
    auto composed = make_editor_field_scene(chart, index, expanded, request);
    if (!composed)
        return false;
    window.scene = std::move(composed.scene);
    window.view = std::move(expanded);
    window.request = request;
    window.bgm_lane_widths_snapshot.assign(request.bgm_lane_widths_pixels.begin(),
                                           request.bgm_lane_widths_pixels.end());
    window.request.bgm_lane_widths_pixels = {};
    window.canvas_rows = *canvas;
    ++window.rebuild_count;
    return true;
}
} // namespace

auto can_reuse_editor_field_scene_window(const chart_document_t& chart,
                                         const editor_field_scene_index_t& index,
                                         const editor_interaction_view_model_t& view,
                                         const editor_field_scene_request_t& request,
                                         const editor_field_scene_window_t& window) -> bool {
    return is_valid_window_request(chart, index, view, request) &&
           is_reusable_window_current(window, view, request) &&
           is_window_scroll_valid(chart, view, window);
}
auto project_editor_field_scene_window_metadata(const chart_document_t& chart,
                                                const editor_field_scene_index_t& index,
                                                const editor_interaction_view_model_t& view,
                                                const editor_field_scene_request_t& request,
                                                const editor_field_scene_window_t& window)
    -> editor_field_scene_window_metadata_t {
    if (!can_reuse_editor_field_scene_window(chart, index, view, request, window))
        return {};
    auto metadata = project_scene_window_metadata_unchecked(chart, index, view, request, window);
    if (!is_projected_window_within_limit(metadata.projection, request))
        return {};
    return metadata;
}

auto make_editor_field_scene_reusing_window(const chart_document_t& chart,
                                            const editor_field_scene_index_t& index,
                                            const editor_interaction_view_model_t& view,
                                            const editor_field_scene_request_t& request,
                                            editor_field_scene_window_t& window)
    -> editor_field_scene_result_t {
    // Complex transient previews and authored grouping/comparison overlays keep
    // their existing validation and composition path.
    if (!is_valid_window_request(chart, index, view, request)) {
        window.scene.reset();
        return make_editor_field_scene(chart, index, view, request);
    }
    const auto matched = is_reusable_window_current(window, view, request);
    if (!matched && !rebuild_scene_window(chart, index, view, request, window)) {
        window.scene.reset();
        return make_editor_field_scene(chart, index, view, request);
    }
    if (!is_window_scroll_valid(chart, view, window))
        return make_editor_field_scene(chart, index, view, request);
    auto result = project_scene_window(chart, index, view, request, window);
    if (!is_projected_window_within_limit(result, request)) {
        window.scene.reset();
        return make_editor_field_scene(chart, index, view, request);
    }
    if (matched) ++window.reuse_count;
    return result;
}

} // namespace wormedit

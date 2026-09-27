#include <wormedit/core/editor_scene.h>
#include <wormedit/core/virtual_canvas.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
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

auto editable_lane(const lane_definition_t& lane) -> bool {
    return lane.role != lane_role_t::autoplay && lane.role != lane_role_t::invisible &&
           lane.role != lane_role_t::mine;
}

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
               return entry.id == lane && editable_lane(entry);
           });
}

auto finite_nonnegative(double value) -> bool {
    return std::isfinite(value) && value >= 0.0;
}

auto rect_intersects_viewport(const editor_field_rect_t& rect, double height) -> bool {
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

auto build_editor_field_scene_index(const chart_document_t& chart, std::uint64_t document_revision,
                                    editor_field_scene_index_options_t options)
    -> std::variant<editor_field_scene_index_t, diagnostic_list_t> {
    diagnostic_list_t diagnostics;
    if (!chart.id || chart.divisions_per_standard_measure == 0 || options.maximum_notes == 0 ||
        options.maximum_bgm_events == 0 ||
        options.maximum_layout_segments == 0 || options.maximum_lane_definitions == 0) {
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
    if (chart.lane_layouts.size() > options.maximum_layout_segments) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                    "field-scene layout count exceeds its configured cap"));
        return diagnostics;
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
            return diagnostics;
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

    std::size_t note_count{};
    for (const auto& measure : chart.measures) {
        if (measure.notes.size() > options.maximum_notes - note_count) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                        "field-scene note count exceeds its configured cap"));
            return diagnostics;
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
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto begin = absolute_row(index.measures, note.position);
            const auto end = note.end ? absolute_row(index.measures, *note.end) : begin;
            if (!note.id || !note_ids.insert(note.id.value).second) {
                diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                                            "field-scene note identity is missing or duplicated",
                                            note.id));
                continue;
            }
            if (!begin || !end || *end < *begin) {
                diagnostics.push_back(error(diagnostic_code_t::invalid_hold,
                                            "field-scene note span is outside chart geometry",
                                            note.id));
                continue;
            }
            if (note.position.measure != measure.id) {
                diagnostics.push_back(
                    error(diagnostic_code_t::invalid_measure,
                          "field-scene note is stored outside its position's measure", note.id));
                continue;
            }
            if (!has_lane(layout_at_row(index, *begin), note.lane) ||
                !has_lane(layout_at_row(index, *end), note.lane)) {
                diagnostics.push_back(error(diagnostic_code_t::unknown_lane,
                                            "field-scene note endpoint has no active lane",
                                            note.id));
                continue;
            }
            index.notes.push_back({.note = note.id,
                                   .lane = note.lane,
                                   .kind = note.kind,
                                   .begin_row = *begin,
                                   .end_row = *end,
                                   .semantic_color = semantic_color_for_note(note),
                                   .semantic_layers = note.semantic_layers});
        }
    }
    std::size_t bgm_event_count{};
    for (const auto& measure : chart.measures) {
        if (measure.bgm_events.size() > options.maximum_bgm_events - bgm_event_count) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                        "field-scene BGM event count exceeds its configured cap"));
            return diagnostics;
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
    bgm_source_lanes.reserve(chart.bgm_source_lanes.size());
    bgm_event_ids.reserve(bgm_event_count);
    for (const auto& lane : chart.bgm_source_lanes) {
        if (!lane.id || !bgm_source_lanes.insert(lane.id.value).second) {
            diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                                        "field-scene BGM source lane identity is invalid",
                                        lane.id));
        }
    }
    for (const auto& measure : chart.measures) {
        for (const auto& event : measure.bgm_events) {
            const auto row = absolute_row(index.measures, event.position);
            if (!event.id) {
                diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                            "field-scene BGM event has invalid source geometry",
                                            event.id));
                continue;
            }
            if (!bgm_event_ids.insert(event.id.value).second) {
                diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                                            "field-scene BGM event identity is duplicated",
                                            event.id));
                continue;
            }
            if (!event.source_lane || !bgm_source_lanes.contains(event.source_lane.value) || !row ||
                event.position.measure != measure.id) {
                diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                            "field-scene BGM event has invalid source geometry",
                                            event.id));
                continue;
            }
            index.bgm_events.push_back({.event = event.id,
                                        .source_lane = event.source_lane,
                                        .row = *row,
                                        .sound = event.sound,
                                        .bmson_restart = event.sound_channel_event
                                            ? [&]() -> std::optional<bool> {
                                                  const auto found = bmson_restart_by_event.find(
                                                      event.sound_channel_event->value);
                                                  return found == bmson_restart_by_event.end()
                                                             ? std::nullopt
                                                             : std::optional<bool>{found->second};
                                              }()
                                            : std::nullopt});
        }
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

auto make_editor_field_scene(const chart_document_t& chart, const editor_field_scene_index_t& index,
                             const editor_interaction_view_model_t& view,
                             const editor_field_scene_request_t& request)
    -> editor_field_scene_result_t {
    editor_field_scene_result_t result;
    if (chart.id != index.chart || view.chart != chart.id ||
        view.document_revision != index.document_revision || index.measures.total_rows == 0 ||
        index.note_tree_leaf_base == 0 || index.note_tree_leaf_base < index.notes.size() ||
        index.note_tree_leaf_base > std::numeric_limits<std::size_t>::max() / 2 ||
        index.note_tree_max_end.size() != index.note_tree_leaf_base * 2 ||
        index.note_indices_by_id.size() != index.notes.size() ||
        !finite_nonnegative(request.field_left_pixels) ||
        !std::isfinite(request.field_width_pixels) || request.field_width_pixels <= 0.0 ||
        !finite_nonnegative(request.split_stage_gutter_pixels) ||
        !std::isfinite(request.note_head_height_pixels) || request.note_head_height_pixels <= 0.0 ||
        !std::isfinite(request.comparison_difference_height_pixels) ||
        request.comparison_difference_height_pixels <= 0.0 ||
        !std::isfinite(request.hold_body_width_ratio) || request.hold_body_width_ratio <= 0.0 ||
        request.hold_body_width_ratio > 1.0 || !finite_nonnegative(request.bgm_pane_left_pixels) ||
        !finite_nonnegative(request.bgm_pane_width_pixels) ||
        !std::isfinite(request.bgm_pane_left_pixels + request.bgm_pane_width_pixels) ||
        !std::isfinite(request.bgm_lane_width_pixels) || request.bgm_lane_width_pixels <= 0.0 ||
        !finite_nonnegative(request.bgm_horizontal_scroll_pixels) ||
        request.maximum_grid_lines == 0 || request.maximum_authored_groupings == 0 ||
        request.authored_groupings.size() > request.maximum_authored_groupings ||
        request.maximum_comparison_difference_rows == 0 ||
        request.comparison_difference_rows.size() > request.maximum_comparison_difference_rows ||
        request.maximum_scene_primitives == 0 || request.maximum_composition_operations == 0 ||
        view.selected_notes.size() > index.notes.size() ||
        !std::isfinite(view.pixels_per_standard_measure) ||
        view.pixels_per_standard_measure < minimum_pixels_per_standard_measure ||
        view.pixels_per_standard_measure > maximum_pixels_per_standard_measure ||
        !finite_nonnegative(view.viewport_scroll_pixels) ||
        !std::isfinite(view.viewport_height_pixels) || view.viewport_height_pixels <= 0.0) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                           "field-scene index, view, or geometry is invalid"));
        return result;
    }

    auto canvas_rows = index.measures.total_rows;
    if (request.include_virtual_bms_tail && chart.source_family == format_family_t::bms) {
        auto built_canvas = build_virtual_canvas(chart);
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built_canvas)) {
            result.diagnostics = *diagnostics;
            return result;
        }
        canvas_rows = std::get<virtual_canvas_t>(built_canvas).total_rows;
    }

    const auto pixels_per_row = static_cast<long double>(view.pixels_per_standard_measure) /
                                chart.divisions_per_standard_measure;
    const auto content_height = static_cast<long double>(canvas_rows) * pixels_per_row;
    const auto maximum_scroll =
        std::max(0.0L, content_height - static_cast<long double>(view.viewport_height_pixels));
    if (static_cast<long double>(view.viewport_scroll_pixels) > maximum_scroll) {
        result.diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
                                           "field-scene viewport scroll exceeds chart content"));
        return result;
    }
    if (view.pending_note_paint_gesture.has_value()) {
        const auto& pending = *view.pending_note_paint_gesture;
        if (pending.document_revision != index.document_revision) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_hold, "field-scene paint preview is stale"));
            return result;
        }
        if (!pending.targets || pending.targets->empty() ||
            pending.targets->size() > maximum_note_paint_targets) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "field-scene paint preview target count is invalid"));
            return result;
        }
    }
    const auto known_layer = [&](layer_id_t id) {
        return std::ranges::find(chart.semantic_layers, id, &semantic_layer_t::id) !=
               chart.semantic_layers.end();
    };
    const auto valid_filter = [&](const std::vector<layer_id_t>& layers) {
        std::unordered_set<std::uint64_t> ids;
        for (const auto layer : layers) {
            if (!layer || !known_layer(layer) || !ids.insert(layer.value).second) return false;
        }
        return true;
    };
    const auto contradictory_filter = std::ranges::any_of(
        request.required_semantic_layers, [&](layer_id_t layer) {
            return std::ranges::find(request.excluded_semantic_layers, layer) !=
                   request.excluded_semantic_layers.end();
        });
    if ((request.solo_semantic_layer.has_value() && !known_layer(*request.solo_semantic_layer)) ||
        !valid_filter(request.required_semantic_layers) ||
        !valid_filter(request.excluded_semantic_layers) || contradictory_filter) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id,
                  "field-scene semantic-layer filter refers to invalid chart layers"));
        return result;
    }

    const auto row_at_pixel = [&](long double pixel) {
        return (pixel + static_cast<long double>(view.viewport_scroll_pixels)) / pixels_per_row;
    };
    const auto visible_low =
        std::clamp(row_at_pixel(0), 0.0L, static_cast<long double>(canvas_rows));
    const auto visible_high = std::clamp(row_at_pixel(view.viewport_height_pixels), 0.0L,
                                         static_cast<long double>(canvas_rows));
    const auto overscan_rows =
        static_cast<long double>(request.note_head_height_pixels) / (2.0L * pixels_per_row);
    const auto query_low = std::max(0.0L, visible_low - overscan_rows);
    const auto query_high =
        std::min(static_cast<long double>(canvas_rows), visible_high + overscan_rows);
    auto visible_begin = static_cast<std::uint64_t>(std::floor(visible_low));
    auto visible_end = static_cast<std::uint64_t>(std::ceil(visible_high));
    visible_begin = std::min(visible_begin, canvas_rows - 1);
    visible_end = std::clamp(visible_end, visible_begin + 1, canvas_rows);

    editor_field_scene_t scene{.visible_begin_row = visible_begin, .visible_end_row = visible_end};
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
        return static_cast<double>(row * pixels_per_row -
                                   static_cast<long double>(view.viewport_scroll_pixels));
    };

    std::vector<visible_band_t> visible_bands;
    for (const auto& source : index.layouts) {
        const auto source_end = &source == &index.layouts.back() ? canvas_rows : source.end_row;
        if (static_cast<long double>(source_end) <= query_low ||
            static_cast<long double>(source.begin_row) >= query_high) {
            continue;
        }
        if (!operate()) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "field-scene composition exceeds its configured operation cap"));
            return result;
        }
        std::vector<const lane_definition_t*> lanes;
        for (const auto& lane : source.layout.lanes) {
            if (editable_lane(lane)) {
                lanes.push_back(&lane);
            }
        }
        std::ranges::sort(lanes, [](const auto* left, const auto* right) {
            return std::tie(left->display_order, left->id.value) <
                   std::tie(right->display_order, right->id.value);
        });
        if (lanes.empty()) {
            continue;
        }
        const auto split =
            source.layout.split_stage && source.layout.split_after_column
                ? std::min<std::size_t>(*source.layout.split_after_column, lanes.size())
                : lanes.size();
        const auto gutter =
            split > 0 && split < lanes.size() ? request.split_stage_gutter_pixels : 0.0;
        if (gutter >= request.field_width_pixels) {
            result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                               "split-stage gutter consumes the note field"));
            return result;
        }
        const auto lane_width =
            (request.field_width_pixels - gutter) / static_cast<double>(lanes.size());
        const auto clipped_begin = std::max(query_low, static_cast<long double>(source.begin_row));
        const auto clipped_end = std::min(query_high, static_cast<long double>(source_end));
        if (!consume(1 + lanes.size())) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "field-scene geometry exceeds its configured primitive cap"));
            return result;
        }
        const auto scene_band = scene.layout_bands.size();
        scene.layout_bands.push_back({.begin_row = source.begin_row,
                                      .end_row = source_end,
                                      .top_pixels = y_for_row(clipped_begin),
                                      .bottom_pixels = y_for_row(clipped_end),
                                      .profile_id = source.layout.profile_id,
                                      .split_stage = source.layout.split_stage});
        visible_band_t band{.begin_row = source.begin_row,
                            .end_row = source_end,
                            .clipped_begin_row = clipped_begin,
                            .clipped_end_row = clipped_end,
                            .scene_band = scene_band};
        band.lanes.reserve(lanes.size());
        for (std::size_t lane_index = 0; lane_index < lanes.size(); ++lane_index) {
            const auto left = request.field_left_pixels + lane_width * lane_index +
                              (lane_index >= split ? gutter : 0.0);
            const auto right = left + lane_width;
            band.lanes.push_back({lanes[lane_index]->id, lanes[lane_index]->glyph, left, right});
            scene.lanes.push_back(
                {.layout_band = scene_band,
                 .lane = lanes[lane_index]->id,
                 .role = lanes[lane_index]->role,
                 .stage = lanes[lane_index]->stage,
                 .glyph = lanes[lane_index]->glyph,
                 .display_order = lanes[lane_index]->display_order,
                 .bounds = {left, y_for_row(clipped_begin), right, y_for_row(clipped_end)}});
        }
        visible_bands.push_back(std::move(band));
    }
    const auto visible_band_for_row = [&](std::uint64_t row) -> const visible_band_t* {
        const auto after =
            std::ranges::upper_bound(visible_bands, row, {}, &visible_band_t::begin_row);
        if (after == visible_bands.begin()) {
            return nullptr;
        }
        const auto& candidate = *std::prev(after);
        return row < candidate.end_row ? &candidate : nullptr;
    };

    struct visible_paint_target_t {
        paint_cell_key_t cell;
        lane_glyph_t glyph{lane_glyph_t::bar};
        editor_field_rect_t bounds;
    };
    std::vector<visible_paint_target_t> visible_paint_targets;
    std::unordered_set<paint_cell_key_t, paint_cell_hash_t> paint_target_cells;
    std::unordered_set<paint_cell_key_t, paint_cell_hash_t> visible_paint_cells;
    std::unordered_set<paint_cell_key_t, paint_cell_hash_t> consumed_paint_cells;
    std::unordered_set<paint_cell_key_t, paint_cell_hash_t> invalid_paint_cells;
    std::unordered_map<paint_cell_key_t, std::size_t, paint_cell_hash_t> paint_overlap_counts;
    std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> visible_paint_rows_by_lane;
    if (view.pending_note_paint_gesture.has_value()) {
        const auto& targets = *view.pending_note_paint_gesture->targets;
        paint_target_cells.reserve(targets.size());
        visible_paint_cells.reserve(targets.size());
        consumed_paint_cells.reserve(targets.size());
        invalid_paint_cells.reserve(targets.size());
        paint_overlap_counts.reserve(targets.size());
        visible_paint_targets.reserve(targets.size());
        for (const auto& target : targets) {
            if (!operate()) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_resolution,
                          "field-scene paint preview exceeds its configured operation cap"));
                return result;
            }
            const auto row = absolute_row(index.measures, target.position);
            if (!row || *row >= index.measures.total_rows) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::row_out_of_range,
                          "field-scene paint target is outside chart geometry"));
                return result;
            }
            if (!has_lane(layout_at_row(index, *row), target.lane)) {
                result.diagnostics.push_back(error(diagnostic_code_t::unknown_lane,
                                                   "field-scene paint target has no active lane"));
                return result;
            }
            const paint_cell_key_t cell{target.lane, *row};
            if (!paint_target_cells.insert(cell).second) {
                result.diagnostics.push_back(error(diagnostic_code_t::collision,
                                                   "field-scene paint targets must be unique"));
                return result;
            }
            if (static_cast<long double>(*row) < query_low ||
                static_cast<long double>(*row) > query_high) {
                continue;
            }
            const auto* band = visible_band_for_row(*row);
            if (band == nullptr) {
                continue;
            }
            const auto* lane = lane_bounds(*band, target.lane);
            if (lane == nullptr) {
                continue;
            }
            const auto y = y_for_row(*row);
            const auto half_height = request.note_head_height_pixels / 2.0;
            const editor_field_rect_t bounds{lane->left, y - half_height, lane->right,
                                             y + half_height};
            if (rect_intersects_viewport(bounds, view.viewport_height_pixels)) {
                visible_paint_cells.insert(cell);
                visible_paint_rows_by_lane[target.lane.value].push_back(*row);
                visible_paint_targets.push_back(
                    {.cell = cell, .glyph = lane->glyph, .bounds = bounds});
            }
        }
        for (auto& [lane, rows] : visible_paint_rows_by_lane) {
            (void)lane;
            std::ranges::sort(rows);
        }
    }

    std::vector<std::pair<std::uint64_t, local_position_t>> comparison_rows;
    comparison_rows.reserve(request.comparison_difference_rows.size());
    for (const auto position : request.comparison_difference_rows) {
        const auto row = absolute_row(index.measures, position);
        if (!row) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::row_out_of_range,
                      "field-scene comparison row is outside chart geometry"));
            return result;
        }
        comparison_rows.emplace_back(*row, position);
    }
    std::ranges::sort(comparison_rows,
                      [](const auto& left, const auto& right) { return left.first < right.first; });
    const auto duplicate_comparison =
        std::ranges::adjacent_find(comparison_rows, [](const auto& left, const auto& right) {
            return left.first == right.first;
        });
    if (duplicate_comparison != comparison_rows.end()) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::collision,
                  "field-scene comparison rows must be unique exact positions"));
        return result;
    }
    const auto half_difference_height = request.comparison_difference_height_pixels / 2.0;
    for (const auto& [row, position] : comparison_rows) {
        if (static_cast<long double>(row) < query_low ||
            static_cast<long double>(row) > query_high) {
            continue;
        }
        if (!operate() || !consume()) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "field-scene comparison rows exceed a composition cap"));
            return result;
        }
        const auto y = y_for_row(row);
        const editor_field_rect_t bounds{request.field_left_pixels, y - half_difference_height,
                                         request.field_left_pixels + request.field_width_pixels,
                                         y + half_difference_height};
        if (rect_intersects_viewport(bounds, view.viewport_height_pixels)) {
            scene.comparison_differences.push_back({.position = position, .bounds = bounds});
        }
    }

    const auto grid = make_editor_grid_lines(
        chart, {.visible_begin_row = visible_begin,
                .visible_end_row = visible_end,
                .active_snap_denominator = view.active_snap_denominator,
                .include_virtual_bms_tail = request.include_virtual_bms_tail,
                .pixels_per_standard_measure = view.pixels_per_standard_measure,
                .density = request.grid_density,
                .authored_groupings = request.authored_groupings,
                .maximum_line_count = request.maximum_grid_lines});
    if (!grid) {
        result.diagnostics = grid.diagnostics;
        return result;
    }
    result.diagnostics = grid.diagnostics;
    if (!consume(grid.lines.size())) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                           "field-scene grid exceeds its primitive cap"));
        return result;
    }
    scene.grid_lines.reserve(grid.lines.size());
    for (const auto& line : grid.lines) {
        const auto row = static_cast<long double>(line.chart_row.whole_rows) +
                         static_cast<long double>(line.chart_row.fractional_rows.numerator) /
                             line.chart_row.fractional_rows.denominator;
        scene.grid_lines.push_back({.y_pixels = y_for_row(row),
                                    .kind = line.kind,
                                    .emphasis = line.emphasis,
                                    .chart_row = line.chart_row});
    }

    std::unordered_set<std::uint64_t> selected;
    selected.reserve(view.selected_notes.size());
    for (const auto note : view.selected_notes) {
        selected.insert(note.value);
    }
    const auto preview_gesture_count =
        static_cast<unsigned>(view.pending_hold.has_value()) +
        static_cast<unsigned>(view.pending_note_duration_gesture.has_value()) +
        static_cast<unsigned>(view.pending_note_move_gesture.has_value());
    if (preview_gesture_count > 1) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_hold,
                  "field-scene cannot compose simultaneous note-transform previews"));
        return result;
    }
    struct duration_proposal_t {
        const editor_field_scene_note_source_t* source{};
        const note_duration_preview_t* preview{};
        std::optional<std::uint64_t> end_row;
    };
    std::vector<duration_proposal_t> duration_proposals;
    std::unordered_set<std::uint64_t> duration_origin_notes;
    if (view.pending_note_duration_gesture.has_value()) {
        const auto& pending = *view.pending_note_duration_gesture;
        if (pending.document_revision != index.document_revision || pending.notes.empty() ||
            pending.notes.size() != pending.preview.size() ||
            pending.notes.size() > index.notes.size()) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_hold,
                      "field-scene duration preview is malformed or stale"));
            return result;
        }
        std::unordered_set<std::uint64_t> expected;
        expected.reserve(pending.notes.size());
        for (const auto note : pending.notes) {
            if (note.value == 0 || !expected.insert(note.value).second) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_id,
                          "field-scene duration preview contains duplicate note identities"));
                return result;
            }
        }
        duration_proposals.reserve(pending.preview.size());
        duration_origin_notes.reserve(pending.preview.size());
        for (const auto& preview : pending.preview) {
            if (!operate() || preview.note.value == 0 || !expected.erase(preview.note.value)) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_id,
                          "field-scene duration preview does not match its selected notes"));
                return result;
            }
            const auto found = index.note_indices_by_id.find(preview.note.value);
            if (found == index.note_indices_by_id.end() ||
                (preview.kind != note_kind_t::tap && preview.kind != note_kind_t::hold)) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_hold,
                          "field-scene duration preview refers to an invalid note proposal"));
                return result;
            }
            std::optional<std::uint64_t> end_row;
            if (preview.kind == note_kind_t::tap) {
                if (preview.end.has_value()) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_hold,
                              "field-scene tap duration preview must not carry a tail"));
                    return result;
                }
            } else {
                if (!preview.end.has_value()) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_hold,
                              "field-scene hold duration preview is missing its tail"));
                    return result;
                }
                end_row = absolute_row(index.measures, *preview.end);
                if (!end_row || *end_row <= index.notes[found->second].begin_row) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_hold,
                              "field-scene hold duration preview has an invalid tail"));
                    return result;
                }
            }
            duration_origin_notes.insert(preview.note.value);
            duration_proposals.push_back(
                {.source = &index.notes[found->second], .preview = &preview, .end_row = end_row});
        }
        if (!expected.empty()) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_id,
                      "field-scene duration preview omitted a selected note proposal"));
            return result;
        }
    }
    struct move_proposal_t {
        const editor_field_scene_note_source_t* source{};
        const note_move_preview_t* preview{};
        std::uint64_t begin_row{};
        std::optional<std::uint64_t> end_row;
    };
    std::vector<move_proposal_t> move_proposals;
    std::unordered_set<std::uint64_t> move_origin_notes;
    if (view.pending_note_move_gesture.has_value()) {
        const auto& pending = *view.pending_note_move_gesture;
        if (pending.document_revision != index.document_revision || pending.notes.empty() ||
            pending.notes.size() != pending.preview.size() ||
            pending.notes.size() > index.notes.size()) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_hold,
                      "field-scene note move preview is malformed or stale"));
            return result;
        }
        std::unordered_set<std::uint64_t> expected;
        expected.reserve(pending.notes.size());
        for (const auto note : pending.notes) {
            if (note.value == 0 || !expected.insert(note.value).second) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_id,
                          "field-scene note move preview contains duplicate note identities"));
                return result;
            }
        }
        move_proposals.reserve(pending.preview.size());
        move_origin_notes.reserve(pending.preview.size());
        for (const auto& preview : pending.preview) {
            if (!operate() || preview.note.value == 0 || !expected.erase(preview.note.value)) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_id,
                          "field-scene note move preview does not match its selected notes"));
                return result;
            }
            const auto found = index.note_indices_by_id.find(preview.note.value);
            const auto begin_row = absolute_row(index.measures, preview.position);
            if (found == index.note_indices_by_id.end() || !begin_row ||
                !has_lane(layout_at_row(index, *begin_row), preview.lane)) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_id,
                          "field-scene note move preview refers to an invalid note proposal"));
                return result;
            }
            const auto& source = index.notes[found->second];
            std::optional<std::uint64_t> end_row;
            if (source.kind == note_kind_t::tap) {
                if (preview.end.has_value()) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_hold,
                              "field-scene moved tap preview must not carry a tail"));
                    return result;
                }
            } else if (source.kind == note_kind_t::hold) {
                if (!preview.end.has_value()) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_hold,
                              "field-scene moved hold preview is missing its tail"));
                    return result;
                }
                end_row = absolute_row(index.measures, *preview.end);
                if (!end_row || *end_row <= *begin_row ||
                    !has_lane(layout_at_row(index, *end_row), preview.lane)) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_hold,
                              "field-scene moved hold preview has an invalid tail"));
                    return result;
                }
            } else {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_hold,
                          "field-scene note move only supports tap and hold proposals"));
                return result;
            }
            move_origin_notes.insert(preview.note.value);
            move_proposals.push_back({.source = &source,
                                      .preview = &preview,
                                      .begin_row = *begin_row,
                                      .end_row = end_row});
        }
        if (!expected.empty()) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_id,
                      "field-scene note move preview omitted a selected note proposal"));
            return result;
        }
    }
    struct hold_proposal_t {
        lane_id_t lane;
        std::uint64_t head_row{};
        std::optional<std::uint64_t> tail_row;
        bool adds_note{};
    };
    std::optional<hold_proposal_t> hold_proposal;
    std::unordered_set<std::uint64_t> hold_origin_notes;
    if (view.pending_hold.has_value()) {
        const auto& pending = *view.pending_hold;
        const auto anchor_row = absolute_row(index.measures, pending.position);
        if (pending.document_revision != index.document_revision || !anchor_row ||
            !has_lane(layout_at_row(index, *anchor_row), pending.lane)) {
            result.diagnostics.push_back(error(diagnostic_code_t::invalid_hold,
                                               "field-scene hold preview is stale or invalid"));
            return result;
        }
        std::optional<std::uint64_t> moving_row;
        if (pending.preview_end.has_value()) {
            moving_row = absolute_row(index.measures, *pending.preview_end);
            if (!moving_row || !has_lane(layout_at_row(index, *moving_row), pending.lane)) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_hold,
                          "field-scene hold preview has an invalid proposed endpoint"));
                return result;
            }
        }
        if (!pending.placement_preview.has_value()) {
            const auto head_row = moving_row ? std::min(*anchor_row, *moving_row) : *anchor_row;
            const auto tail_row = moving_row ? std::max(*anchor_row, *moving_row) : *anchor_row;
            hold_proposal = {.lane = pending.lane,
                             .head_row = head_row,
                             .tail_row = head_row == tail_row ? std::optional<std::uint64_t>{}
                                                              : std::optional{tail_row},
                             .adds_note = true};
        } else {
            const auto& preview = *pending.placement_preview;
            const auto head_row = absolute_row(index.measures, preview.head);
            const auto tail_row = preview.tail ? absolute_row(index.measures, *preview.tail)
                                               : std::optional<std::uint64_t>{};
            const auto final_tail = tail_row.value_or(head_row.value_or(0));
            if (!head_row || (tail_row && *tail_row <= *head_row) || *anchor_row < *head_row ||
                *anchor_row > final_tail ||
                (moving_row && (*moving_row < *head_row || *moving_row > final_tail)) ||
                !preview.removals ||
                (!preview.adds_note && (preview.tail.has_value() || preview.removals->empty())) ||
                (preview.adds_note && !preview.tail.has_value() && !preview.removals->empty())) {
                result.diagnostics.push_back(error(diagnostic_code_t::invalid_hold,
                                                   "field-scene hold placement preview is "
                                                   "malformed or does not cover its gesture"));
                return result;
            }
            hold_origin_notes.reserve(preview.removals->size());
            for (const auto note : *preview.removals) {
                if (!operate() || note.value == 0 || !hold_origin_notes.insert(note.value).second) {
                    result.diagnostics.push_back(error(
                        diagnostic_code_t::invalid_id,
                        "field-scene hold placement preview contains duplicate note identities"));
                    return result;
                }
                const auto found = index.note_indices_by_id.find(note.value);
                if (found == index.note_indices_by_id.end()) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_id,
                              "field-scene hold placement preview refers to an unknown note"));
                    return result;
                }
                const auto& source = index.notes[found->second];
                if ((source.kind != note_kind_t::tap && source.kind != note_kind_t::hold) ||
                    source.lane != pending.lane || source.begin_row > final_tail ||
                    source.end_row < *head_row) {
                    result.diagnostics.push_back(error(
                        diagnostic_code_t::invalid_hold,
                        "field-scene hold placement preview removes an unrelated note", note));
                    return result;
                }
            }
            hold_proposal = {.lane = pending.lane,
                             .head_row = *head_row,
                             .tail_row = tail_row,
                             .adds_note = preview.adds_note};
        }
    }
    const auto after_candidates = std::ranges::upper_bound(
        index.notes, query_high,
        [](long double row, std::uint64_t begin) { return row < static_cast<long double>(begin); },
        &editor_field_scene_note_source_t::begin_row);
    const auto candidate_count =
        static_cast<std::size_t>(std::distance(index.notes.begin(), after_candidates));
    bool failed{};
    std::vector<std::pair<std::size_t, paint_cell_key_t>> paint_endpoint_cells;
    paint_endpoint_cells.reserve(visible_paint_cells.size());
    const auto add_note = [&](const editor_field_scene_note_source_t& note) {
        if (static_cast<long double>(note.end_row) < query_low ||
            static_cast<long double>(note.begin_row) > query_high) {
            return;
        }
        if (request.solo_semantic_layer.has_value() &&
            std::ranges::find(note.semantic_layers, *request.solo_semantic_layer) ==
                note.semantic_layers.end()) {
            return;
        }
        const auto has_layer = [&](layer_id_t layer) {
            return std::ranges::find(note.semantic_layers, layer) != note.semantic_layers.end();
        };
        if (!std::ranges::all_of(request.required_semantic_layers, has_layer) ||
            std::ranges::any_of(request.excluded_semantic_layers, has_layer)) {
            return;
        }
        if (const auto target_rows = visible_paint_rows_by_lane.find(note.lane.value);
            target_rows != visible_paint_rows_by_lane.end()) {
            const auto first = std::ranges::lower_bound(target_rows->second, note.begin_row);
            const auto after =
                std::ranges::upper_bound(first, target_rows->second.end(), note.end_row);
            for (auto target = first; target != after; ++target) {
                if (!operate()) {
                    failed = true;
                    return;
                }
                const auto removable_tap = note.kind == note_kind_t::tap &&
                                           note.begin_row == note.end_row &&
                                           note.begin_row == *target;
                const paint_cell_key_t cell{note.lane, *target};
                const auto overlap_count = ++paint_overlap_counts[cell];
                if (!removable_tap || overlap_count > 1) {
                    invalid_paint_cells.insert(cell);
                }
            }
        }
        const auto note_selected = selected.contains(note.note.value);
        const auto proposal_origin = duration_origin_notes.contains(note.note.value) ||
                                     move_origin_notes.contains(note.note.value) ||
                                     hold_origin_notes.contains(note.note.value);
        const auto add_endpoint = [&](std::uint64_t row, bool tail) {
            const auto* band = visible_band_for_row(row);
            if (band == nullptr) {
                return;
            }
            const auto* bounds = lane_bounds(*band, note.lane);
            if (bounds == nullptr) {
                return;
            }
            const auto y = y_for_row(row);
            const auto half_height = request.note_head_height_pixels / 2.0;
            const editor_field_rect_t rect{bounds->left, y - half_height, bounds->right,
                                           y + half_height};
            if (!rect_intersects_viewport(rect, view.viewport_height_pixels)) {
                return;
            }
            if (!consume()) {
                failed = true;
                return;
            }
            const paint_cell_key_t cell{note.lane, row};
            const auto paint_target = !tail && visible_paint_cells.contains(cell);
            const auto invalid_preview = paint_target && invalid_paint_cells.contains(cell);
            const auto erase_preview = paint_target && !invalid_preview &&
                                       note.kind == note_kind_t::tap &&
                                       note.begin_row == note.end_row;
            if (paint_target) {
                consumed_paint_cells.insert(cell);
                paint_endpoint_cells.emplace_back(scene.note_endpoints.size(), cell);
            }
            scene.note_endpoints.push_back({.note = note.note,
                                            .lane = note.lane,
                                            .glyph = bounds->glyph,
                                            .kind = note.kind,
                                            .tail = tail,
                                            .selected = note_selected,
                                            .proposal_origin = proposal_origin,
                                            .semantic_color = note.semantic_color,
                                            .bounds = rect,
                                            .paint_preview = paint_target,
                                            .paint_erase_preview = erase_preview,
                                            .paint_invalid_preview = invalid_preview});
        };
        if (note.end_row > note.begin_row) {
            for (const auto& band : visible_bands) {
                if (!operate()) {
                    failed = true;
                    return;
                }
                const auto begin =
                    std::max(static_cast<long double>(note.begin_row), band.clipped_begin_row);
                const auto end =
                    std::min(static_cast<long double>(note.end_row), band.clipped_end_row);
                if (begin >= end) {
                    continue;
                }
                const auto* bounds = lane_bounds(band, note.lane);
                if (bounds == nullptr) {
                    continue;
                }
                if (!consume()) {
                    failed = true;
                    return;
                }
                const auto lane_width = bounds->right - bounds->left;
                const auto body_width = lane_width * request.hold_body_width_ratio;
                const auto center = (bounds->left + bounds->right) / 2.0;
                scene.hold_bodies.push_back(
                    {.note = note.note,
                     .lane = note.lane,
                     .selected = note_selected,
                     .proposal_origin = proposal_origin,
                     .semantic_color = note.semantic_color,
                     .bounds = {center - body_width / 2.0, y_for_row(begin),
                                center + body_width / 2.0, y_for_row(end)}});
            }
        }
        add_endpoint(note.begin_row, false);
        if (!failed && note.end_row > note.begin_row) {
            add_endpoint(note.end_row, true);
        }
    };
    const auto visit = [&](auto&& self, std::size_t node, std::size_t first,
                           std::size_t last) -> void {
        if (failed || first >= candidate_count ||
            static_cast<long double>(index.note_tree_max_end[node]) < query_low) {
            return;
        }
        if (!operate()) {
            failed = true;
            return;
        }
        if (last - first == 1) {
            if (first < index.notes.size()) {
                add_note(index.notes[first]);
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
        result.diagnostics.push_back(error(
            diagnostic_code_t::invalid_resolution,
            "field-scene note composition exceeds its configured primitive or operation cap"));
        return result;
    }
    for (const auto& [endpoint_index, cell] : paint_endpoint_cells) {
        auto& endpoint = scene.note_endpoints[endpoint_index];
        if (invalid_paint_cells.contains(cell)) {
            endpoint.paint_erase_preview = false;
            endpoint.paint_invalid_preview = true;
        }
    }

    for (const auto& target : visible_paint_targets) {
        if (consumed_paint_cells.contains(target.cell)) {
            continue;
        }
        if (!operate() || !consume()) {
            result.diagnostics.push_back(error(
                diagnostic_code_t::invalid_resolution,
                "field-scene paint preview exceeds its configured primitive or operation cap"));
            return result;
        }
        scene.note_endpoints.push_back(
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
             .paint_invalid_preview = invalid_paint_cells.contains(target.cell)});
    }

    bool proposal_failed{};
    const auto append_proposal_geometry = [&](object_id_t note, lane_id_t lane, note_kind_t kind,
                                              std::uint64_t begin_row,
                                              std::optional<std::uint64_t> end_row,
                                              bool selected_note,
                                              std::optional<color_rgba_t> semantic_color =
                                                  std::nullopt) {
        if (kind == note_kind_t::hold && end_row.has_value() && *end_row > begin_row) {
            for (const auto& band : visible_bands) {
                if (!operate()) {
                    proposal_failed = true;
                    return;
                }
                const auto begin =
                    std::max(static_cast<long double>(begin_row), band.clipped_begin_row);
                const auto end = std::min(static_cast<long double>(*end_row), band.clipped_end_row);
                if (begin >= end) {
                    continue;
                }
                const auto* bounds = lane_bounds(band, lane);
                if (bounds == nullptr) {
                    continue;
                }
                if (!consume()) {
                    proposal_failed = true;
                    return;
                }
                const auto lane_width = bounds->right - bounds->left;
                const auto body_width = lane_width * request.hold_body_width_ratio;
                const auto center = (bounds->left + bounds->right) / 2.0;
                scene.hold_bodies.push_back(
                    {.note = note,
                     .lane = lane,
                     .selected = selected_note,
                     .proposal_ghost = true,
                     .semantic_color = semantic_color,
                     .bounds = {center - body_width / 2.0, y_for_row(begin),
                                center + body_width / 2.0, y_for_row(end)}});
            }
        }
        const auto add_endpoint = [&](std::uint64_t row, bool tail) {
            const auto* band = visible_band_for_row(row);
            if (band == nullptr) {
                return;
            }
            const auto* bounds = lane_bounds(*band, lane);
            if (bounds == nullptr) {
                return;
            }
            const auto y = y_for_row(row);
            const auto half_height = request.note_head_height_pixels / 2.0;
            const editor_field_rect_t rect{bounds->left, y - half_height, bounds->right,
                                           y + half_height};
            if (!rect_intersects_viewport(rect, view.viewport_height_pixels)) {
                return;
            }
            if (!consume()) {
                proposal_failed = true;
                return;
            }
            scene.note_endpoints.push_back({.note = note,
                                            .lane = lane,
                                            .glyph = bounds->glyph,
                                            .kind = kind,
                                            .tail = tail,
                                            .selected = selected_note,
                                            .proposal_ghost = true,
                                            .semantic_color = semantic_color,
                                            .bounds = rect});
        };
        add_endpoint(begin_row, false);
        if (!proposal_failed && kind == note_kind_t::hold && end_row.has_value()) {
            add_endpoint(*end_row, true);
        }
    };
    for (const auto& proposal : duration_proposals) {
        append_proposal_geometry(proposal.source->note, proposal.source->lane,
                                 proposal.preview->kind, proposal.source->begin_row,
                                 proposal.end_row, selected.contains(proposal.source->note.value),
                                 proposal.source->semantic_color);
        if (proposal_failed) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "field-scene duration preview exceeds a composition cap"));
            return result;
        }
    }
    for (const auto& proposal : move_proposals) {
        append_proposal_geometry(proposal.source->note, proposal.preview->lane,
                                 proposal.source->kind, proposal.begin_row, proposal.end_row,
                                 selected.contains(proposal.source->note.value),
                                 proposal.source->semantic_color);
        if (proposal_failed) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "field-scene note move preview exceeds a composition cap"));
            return result;
        }
    }
    if (hold_proposal.has_value() && hold_proposal->adds_note) {
        const auto kind = hold_proposal->tail_row ? note_kind_t::hold : note_kind_t::tap;
        append_proposal_geometry(object_id_t{}, hold_proposal->lane, kind, hold_proposal->head_row,
                                 hold_proposal->tail_row, false);
        if (proposal_failed) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "field-scene hold preview exceeds a composition cap"));
            return result;
        }
    }

    if (request.bgm_pane_width_pixels > 0.0) {
        const auto content_width = static_cast<long double>(chart.bgm_source_lanes.size()) *
                                   static_cast<long double>(request.bgm_lane_width_pixels);
        const auto maximum_horizontal_scroll =
            std::max(0.0L, content_width - static_cast<long double>(request.bgm_pane_width_pixels));
        if (static_cast<long double>(request.bgm_horizontal_scroll_pixels) >
            maximum_horizontal_scroll) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::row_out_of_range,
                      "field-scene BGM horizontal scroll exceeds its content"));
            return result;
        }

        const auto pane_left = request.bgm_pane_left_pixels;
        const auto pane_right = pane_left + request.bgm_pane_width_pixels;
        std::unordered_map<std::uint64_t, editor_field_rect_t> visible_bgm_lanes;
        visible_bgm_lanes.reserve(chart.bgm_source_lanes.size());
        for (std::size_t lane_index = 0; lane_index < chart.bgm_source_lanes.size(); ++lane_index) {
            if (!operate()) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_resolution,
                          "field-scene BGM lanes exceed the configured operation cap"));
                return result;
            }
            const auto& lane = chart.bgm_source_lanes[lane_index];
            const auto unclipped_left =
                pane_left + static_cast<double>(lane_index) * request.bgm_lane_width_pixels -
                request.bgm_horizontal_scroll_pixels;
            const auto unclipped_right = unclipped_left + request.bgm_lane_width_pixels;
            if (unclipped_right <= pane_left || unclipped_left >= pane_right) {
                continue;
            }
            if (!consume()) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_resolution,
                          "field-scene BGM lanes exceed the configured primitive cap"));
                return result;
            }
            const editor_field_rect_t bounds{std::max(pane_left, unclipped_left), 0.0,
                                             std::min(pane_right, unclipped_right),
                                             view.viewport_height_pixels};
            visible_bgm_lanes.emplace(lane.id.value, bounds);
            scene.bgm_lanes.push_back(
                {.source_lane = lane.id, .source_order = lane.source_order, .bounds = bounds});
        }

        std::unordered_set<std::uint64_t> selected_bgm_events;
        selected_bgm_events.reserve(view.selected_objects.size());
        for (const auto& object : view.selected_objects) {
            if (object.kind == editor_selection_object_kind_t::bgm_event) {
                selected_bgm_events.insert(object.id.value);
            }
        }
        const auto first_visible_bgm_row = static_cast<std::uint64_t>(std::ceil(query_low));
        const auto last_visible_bgm_row = static_cast<std::uint64_t>(std::floor(query_high));
        const auto first_event = std::ranges::lower_bound(
            index.bgm_events, first_visible_bgm_row, {}, &editor_field_scene_bgm_source_t::row);
        const auto after_event =
            std::ranges::upper_bound(first_event, index.bgm_events.end(), last_visible_bgm_row, {},
                                     &editor_field_scene_bgm_source_t::row);
        for (auto event = first_event; event != after_event; ++event) {
            if (!operate()) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_resolution,
                          "field-scene BGM events exceed the configured operation cap"));
                return result;
            }
            const auto lane = visible_bgm_lanes.find(event->source_lane.value);
            if (lane == visible_bgm_lanes.end()) {
                continue;
            }
            const auto y = y_for_row(event->row);
            const auto half_height = request.note_head_height_pixels / 2.0;
            const editor_field_rect_t bounds{lane->second.left, y - half_height, lane->second.right,
                                             y + half_height};
            if (!rect_intersects_viewport(bounds, view.viewport_height_pixels)) {
                continue;
            }
            if (!consume()) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_resolution,
                          "field-scene BGM events exceed the configured primitive cap"));
                return result;
            }
            scene.bgm_events.push_back(
                {.event = event->event,
                 .source_lane = event->source_lane,
                 .sound = event->sound,
                 .bmson_restart = event->bmson_restart,
                 .selected = selected_bgm_events.contains(event->event.value),
                 .bounds = bounds});
        }
    }

    const auto cursor_row = view.virtual_bms_cursor_row
                                ? view.virtual_bms_cursor_row
                                : absolute_row(index.measures, view.cursor);
    if (cursor_row && static_cast<long double>(*cursor_row) >= query_low &&
        static_cast<long double>(*cursor_row) <= query_high) {
        if (const auto* band = visible_band_for_row(*cursor_row); band != nullptr) {
            if (const auto* bounds = lane_bounds(*band, view.active_lane); bounds != nullptr) {
                if (!consume()) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_resolution,
                              "field-scene cursor exceeds its configured primitive cap"));
                    return result;
                }
                const auto y = y_for_row(*cursor_row);
                scene.cursor = editor_field_cursor_geometry_t{
                    .lane = view.active_lane,
                    .bounds = {bounds->left, y - 1.0, bounds->right, y + 1.0}};
            }
        }
    }

    result.scene = std::move(scene);
    return result;
}

} // namespace wormedit

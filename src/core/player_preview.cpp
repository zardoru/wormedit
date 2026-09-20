#include <wormedit/core/player_preview.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <tuple>
#include <unordered_map>

namespace wormedit {
namespace {

auto error(diagnostic_code_t code, std::string message,
           std::optional<object_id_t> object = std::nullopt) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error,
            .code = code,
            .message = std::move(message),
            .object = object};
}

auto timing_error(const timing_diagnostic_t& source) -> diagnostic_t {
    auto code = diagnostic_code_t::invalid_rational;
    if (source.code == timing_error_code_t::position_out_of_range) {
        code = diagnostic_code_t::row_out_of_range;
    } else if (source.code == timing_error_code_t::capacity_exceeded ||
               source.code == timing_error_code_t::arithmetic_overflow) {
        code = diagnostic_code_t::invalid_resolution;
    }
    return {.severity = source.severity,
            .code = code,
            .message = source.message,
            .object = source.object};
}

auto valid_skin(player_preview_skin_t skin) -> bool {
    return skin == player_preview_skin_t::neutral || skin == player_preview_skin_t::mania ||
           skin == player_preview_skin_t::arrows;
}

auto valid_direction(player_preview_scroll_direction_t direction) -> bool {
    return direction == player_preview_scroll_direction_t::toward_top ||
           direction == player_preview_scroll_direction_t::toward_bottom;
}

auto visible_lane(const lane_definition_t& lane) -> bool {
    return lane.role != lane_role_t::autoplay && lane.role != lane_role_t::invisible;
}

auto glyph_for_skin(const lane_definition_t& lane, player_preview_skin_t skin) -> lane_glyph_t {
    if (skin == player_preview_skin_t::neutral) {
        return lane_glyph_t::square;
    }
    if (skin == player_preview_skin_t::mania) {
        return lane.role == lane_role_t::scratch ? lane_glyph_t::square : lane_glyph_t::bar;
    }
    return lane.glyph;
}

auto finite_positive(double value) -> bool {
    return std::isfinite(value) && value > 0.0;
}

auto valid_visual_layer(const std::optional<preview_visual_event_t>& event,
                        visual_event_kind_t expected_kind, std::uint64_t sample_frame) -> bool {
    return !event || (event->object && event->resource && event->kind == expected_kind &&
                      event->sample_frame <= sample_frame);
}

auto valid_visual_layers(const bga_layer_state_t& layers, std::uint64_t sample_frame) -> bool {
    return valid_visual_layer(layers.background, visual_event_kind_t::background, sample_frame) &&
           valid_visual_layer(layers.poor, visual_event_kind_t::poor, sample_frame) &&
           valid_visual_layer(layers.layer, visual_event_kind_t::layer, sample_frame) &&
           valid_visual_layer(layers.video, visual_event_kind_t::video, sample_frame) &&
           valid_visual_layer(layers.storyboard, visual_event_kind_t::storyboard, sample_frame);
}

auto append_tree(const std::vector<player_preview_note_source_t>& notes, std::size_t leaf_base)
    -> std::vector<double> {
    std::vector<double> tree(leaf_base * 2, -std::numeric_limits<double>::infinity());
    for (std::size_t index = 0; index < notes.size(); ++index) {
        tree[leaf_base + index] = notes[index].maximum_displacement_beats;
    }
    for (auto node = leaf_base; node-- > 1;) {
        tree[node] = std::max(tree[node * 2], tree[node * 2 + 1]);
    }
    return tree;
}

template <typename callback_t>
auto query_notes(const player_preview_index_t& index, double low, double high,
                 std::size_t& operations, std::size_t maximum_operations, callback_t&& callback)
    -> bool {
    const auto visit = [&](auto&& self, std::size_t node, std::size_t begin,
                           std::size_t end) -> bool {
        if (begin >= index.notes.size() || operations >= maximum_operations) {
            return operations < maximum_operations;
        }
        ++operations;
        if (index.note_tree_max_end[node] < low ||
            index.notes[begin].minimum_displacement_beats > high) {
            return true;
        }
        if (end - begin == 1) {
            const auto& note = index.notes[begin];
            if (note.minimum_displacement_beats <= high && note.maximum_displacement_beats >= low) {
                callback(note);
            }
            return true;
        }
        const auto middle = begin + (end - begin) / 2;
        return self(self, node * 2, begin, middle) && self(self, node * 2 + 1, middle, end);
    };
    return visit(visit, 1, 0, index.note_tree_leaf_base);
}

} // namespace

auto build_player_preview_index(const chart_document_t& chart, std::uint64_t document_revision,
                                player_preview_index_options_t options)
    -> player_preview_index_result_t {
    player_preview_index_result_t result;
    if (!chart.id || document_revision == 0 || options.maximum_notes == 0 ||
        options.maximum_bgm_events == 0 ||
        options.maximum_layout_segments == 0 || options.maximum_lane_definitions == 0) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution,
                  "player-preview index requires a chart, revision, and positive bounded limits"));
        return result;
    }
    auto field = build_editor_field_scene_index(
        chart, document_revision,
        {.maximum_notes = options.maximum_notes,
         .maximum_bgm_events = options.maximum_bgm_events,
         .maximum_layout_segments = options.maximum_layout_segments,
         .maximum_lane_definitions = options.maximum_lane_definitions});
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&field)) {
        result.diagnostics = *diagnostics;
        return result;
    }
    auto timing = build_timing_index(chart);
    if (!timing) {
        std::ranges::transform(timing.diagnostics, std::back_inserter(result.diagnostics),
                               timing_error);
        return result;
    }
    auto scroll_speed = build_scroll_speed_index(chart, *timing.index);
    if (!scroll_speed) {
        std::ranges::transform(scroll_speed.diagnostics, std::back_inserter(result.diagnostics),
                               timing_error);
        return result;
    }

    auto& field_index = std::get<editor_field_scene_index_t>(field);
    player_preview_index_t index{
        .chart = chart.id,
        .document_revision = document_revision,
        .timing = std::move(*timing.index),
        .scroll_speed = std::move(*scroll_speed.index),
        .layouts = std::move(field_index.layouts),
        .notes = {},
        .note_tree_leaf_base = 1,
        .note_tree_max_end = {},
    };
    index.notes.reserve(field_index.notes.size());
    for (const auto& source : field_index.notes) {
        if (source.begin_row >
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
            source.end_row > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_resolution,
                      "player-preview note row exceeds exact evaluator bounds", source.note));
            continue;
        }
        const auto begin = scroll_speed_at(index.scroll_speed, index.timing,
                                           {static_cast<std::int64_t>(source.begin_row), 1});
        const auto end = scroll_speed_at(index.scroll_speed, index.timing,
                                         {static_cast<std::int64_t>(source.end_row), 1});
        if (!begin || !end) {
            const auto* failure = !begin ? &begin.error : &end.error;
            result.diagnostics.push_back(
                failure->has_value()
                    ? timing_error(**failure)
                    : error(diagnostic_code_t::invalid_rational,
                            "player-preview note displacement could not be evaluated",
                            source.note));
            continue;
        }
        const auto begin_displacement = begin.state->render_displacement_beats;
        const auto end_displacement = end.state->render_displacement_beats;
        if (!std::isfinite(begin_displacement) || !std::isfinite(end_displacement)) {
            result.diagnostics.push_back(error(diagnostic_code_t::invalid_rational,
                                               "player-preview note displacement is not finite",
                                               source.note));
            continue;
        }
        index.notes.push_back(
            {.note = source.note,
             .lane = source.lane,
             .kind = source.kind,
             .begin_displacement_beats = begin_displacement,
             .end_displacement_beats = end_displacement,
             .minimum_displacement_beats = std::min(begin_displacement, end_displacement),
             .maximum_displacement_beats = std::max(begin_displacement, end_displacement),
             .semantic_color = source.semantic_color});
    }
    if (!result.diagnostics.empty()) {
        return result;
    }
    std::ranges::sort(index.notes, [](const auto& left, const auto& right) {
        if (left.minimum_displacement_beats != right.minimum_displacement_beats) {
            return left.minimum_displacement_beats < right.minimum_displacement_beats;
        }
        return left.note.value < right.note.value;
    });
    while (index.note_tree_leaf_base < index.notes.size()) {
        if (index.note_tree_leaf_base > std::numeric_limits<std::size_t>::max() / 2) {
            result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                               "player-preview note index size overflowed"));
            return result;
        }
        index.note_tree_leaf_base *= 2;
    }
    index.note_tree_max_end = append_tree(index.notes, index.note_tree_leaf_base);
    result.index = std::move(index);
    return result;
}

auto make_player_preview_scene(const player_preview_index_t& index,
                               const player_preview_scene_request_t& request)
    -> player_preview_scene_result_t {
    player_preview_scene_result_t result;
    if (!index.chart || index.document_revision == 0 || request.sample_rate == 0 ||
        !finite_positive(request.viewport_width_pixels) ||
        !finite_positive(request.viewport_height_pixels) ||
        !finite_positive(request.pixels_per_beat) ||
        !finite_positive(request.note_head_height_pixels) ||
        !finite_positive(request.receptor_height_pixels) ||
        !std::isfinite(request.receptor_y_pixels) || request.receptor_y_pixels < 0.0 ||
        request.receptor_y_pixels > request.viewport_height_pixels ||
        !std::isfinite(request.split_stage_gutter_pixels) ||
        request.split_stage_gutter_pixels < 0.0 || !std::isfinite(request.hold_body_width_ratio) ||
        request.hold_body_width_ratio <= 0.0 || request.hold_body_width_ratio > 1.0 ||
        request.maximum_visible_notes == 0 || request.maximum_composition_operations == 0 ||
        !valid_skin(request.skin) || !valid_direction(request.direction) || index.layouts.empty() ||
        !valid_visual_layers(request.visual_layers, request.sample_frame) ||
        index.timing.rows.empty() || index.note_tree_leaf_base == 0 ||
        index.note_tree_max_end.size() != index.note_tree_leaf_base * 2) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                           "player-preview scene request or index is invalid"));
        return result;
    }
    if (request.sample_frame >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution,
                  "player-preview sample frame exceeds exact time bounds"));
        return result;
    }
    const auto position = musical_position_at(
        index.timing, {static_cast<std::int64_t>(request.sample_frame), request.sample_rate},
        {.extrapolate_before_chart = true});
    if (!position || !position.absolute_row) {
        result.diagnostics.push_back(
            position.error ? timing_error(*position.error)
                           : error(diagnostic_code_t::row_out_of_range,
                                   "player-preview clock is outside chart timing bounds"));
        return result;
    }
    const auto current = scroll_speed_at(index.scroll_speed, index.timing, *position.absolute_row,
                                         timing_boundary_side_t::event, true);
    if (!current) {
        result.diagnostics.push_back(
            current.error ? timing_error(*current.error)
                          : error(diagnostic_code_t::invalid_rational,
                                  "player-preview scroll state could not be evaluated"));
        return result;
    }
    const auto current_row = static_cast<long double>(position.absolute_row->numerator) /
                             static_cast<long double>(position.absolute_row->denominator);
    const auto layout = std::ranges::upper_bound(
        index.layouts, current_row, {}, [](const editor_field_scene_layout_source_t& source) {
            return static_cast<long double>(source.begin_row);
        });
    const auto pre_zero = position.absolute_row->numerator < 0;
    const auto active_layout =
        pre_zero ? index.layouts.begin()
                 : (layout == index.layouts.begin() ? index.layouts.begin() : std::prev(layout));
    if (active_layout == index.layouts.end() ||
        (pre_zero ? active_layout->begin_row != 0
                  : current_row < active_layout->begin_row ||
                        current_row > active_layout->end_row)) {
        result.diagnostics.push_back(error(diagnostic_code_t::unknown_lane,
                                           "player-preview clock has no active lane layout"));
        return result;
    }
    std::vector<const lane_definition_t*> lanes;
    for (const auto& lane : active_layout->layout.lanes) {
        if (visible_lane(lane)) {
            lanes.push_back(&lane);
        }
    }
    std::ranges::sort(lanes, [](const auto* left, const auto* right) {
        return std::tie(left->display_order, left->id.value) <
               std::tie(right->display_order, right->id.value);
    });
    if (lanes.empty()) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::unknown_lane, "player-preview layout has no visible lanes"));
        return result;
    }

    player_preview_scene_t scene{.chart = index.chart,
                                 .document_revision = index.document_revision,
                                 .sample_frame = request.sample_frame,
                                 .absolute_row = *position.absolute_row,
                                 .skin = request.skin,
                                 .direction = request.direction,
                                 .pre_zero = pre_zero,
                                 .split_stage = active_layout->layout.split_stage,
                                 .visual_layers = request.visual_layers,
                                 .composition_operations = 0,
                                 .visible_note_count = 0,
                                 .lanes = {},
                                 .receptors = {},
                                 .hold_bodies = {},
                                 .note_endpoints = {}};
    const auto gutter = active_layout->layout.split_stage ? request.split_stage_gutter_pixels : 0.0;
    const auto lane_width =
        (request.viewport_width_pixels - gutter) / static_cast<double>(lanes.size());
    if (!finite_positive(lane_width)) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                           "player-preview lanes do not fit the viewport"));
        return result;
    }
    std::unordered_map<std::uint64_t, std::size_t> lane_indices;
    lane_indices.reserve(lanes.size());
    std::size_t right_stage_begin = lanes.size();
    for (std::size_t lane_index = 0; lane_index < lanes.size(); ++lane_index) {
        if (right_stage_begin == lanes.size() && lanes[lane_index]->stage == stage_group_t::right) {
            right_stage_begin = lane_index;
        }
        const auto extra = lane_index >= right_stage_begin ? gutter : 0.0;
        const auto left = static_cast<double>(lane_index) * lane_width + extra;
        const auto right = left + lane_width;
        const auto glyph = glyph_for_skin(*lanes[lane_index], request.skin);
        scene.lanes.push_back({.lane = lanes[lane_index]->id,
                               .role = lanes[lane_index]->role,
                               .stage = lanes[lane_index]->stage,
                               .glyph = glyph,
                               .display_order = lanes[lane_index]->display_order,
                               .bounds = {left, 0.0, right, request.viewport_height_pixels}});
        scene.receptors.push_back(
            {.lane = lanes[lane_index]->id,
             .glyph = glyph,
             .bounds = {
                 left + 1.0, request.receptor_y_pixels - request.receptor_height_pixels / 2.0,
                 right - 1.0, request.receptor_y_pixels + request.receptor_height_pixels / 2.0}});
        lane_indices.emplace(lanes[lane_index]->id.value, lane_index);
    }

    const auto y_for_displacement = [&](double displacement) {
        const auto distance =
            (displacement - current.state->render_displacement_beats) * request.pixels_per_beat;
        return request.direction == player_preview_scroll_direction_t::toward_top
                   ? request.receptor_y_pixels + distance
                   : request.receptor_y_pixels - distance;
    };
    const auto padding = request.note_head_height_pixels;
    const auto delta_a =
        request.direction == player_preview_scroll_direction_t::toward_top
            ? (-padding - request.receptor_y_pixels) / request.pixels_per_beat
            : (request.receptor_y_pixels - request.viewport_height_pixels - padding) /
                  request.pixels_per_beat;
    const auto delta_b =
        request.direction == player_preview_scroll_direction_t::toward_top
            ? (request.viewport_height_pixels + padding - request.receptor_y_pixels) /
                  request.pixels_per_beat
            : (request.receptor_y_pixels + padding) / request.pixels_per_beat;
    const auto visible_low = current.state->render_displacement_beats + std::min(delta_a, delta_b);
    const auto visible_high = current.state->render_displacement_beats + std::max(delta_a, delta_b);
    std::size_t operations{};
    std::size_t visible_notes{};
    bool visible_note_cap_exceeded{};
    const auto query_ok = query_notes(
        index, visible_low, visible_high, operations, request.maximum_composition_operations,
        [&](const player_preview_note_source_t& note) {
            const auto lane = lane_indices.find(note.lane.value);
            if (lane == lane_indices.end()) {
                return;
            }
            if (visible_notes >= request.maximum_visible_notes) {
                visible_note_cap_exceeded = true;
                return;
            }
            ++visible_notes;
            const auto& lane_geometry = scene.lanes[lane->second];
            const auto lane_center = (lane_geometry.bounds.left + lane_geometry.bounds.right) / 2.0;
            const auto head_width =
                std::max(1.0, lane_geometry.bounds.right - lane_geometry.bounds.left - 4.0);
            const auto begin_y = y_for_displacement(note.begin_displacement_beats);
            const auto end_y = y_for_displacement(note.end_displacement_beats);
            const auto append_endpoint = [&](double y, bool tail) {
                if (y + request.note_head_height_pixels / 2.0 < 0.0 ||
                    y - request.note_head_height_pixels / 2.0 > request.viewport_height_pixels) {
                    return;
                }
                scene.note_endpoints.push_back(
                    {.note = note.note,
                     .lane = note.lane,
                     .kind = note.kind,
                     .glyph = lane_geometry.glyph,
                     .tail = tail,
                     .semantic_color = note.semantic_color,
                     .bounds = {lane_center - head_width / 2.0,
                                y - request.note_head_height_pixels / 2.0,
                                lane_center + head_width / 2.0,
                                y + request.note_head_height_pixels / 2.0}});
            };
            append_endpoint(begin_y, false);
            if (note.kind == note_kind_t::hold || note.kind == note_kind_t::roll) {
                const auto body_half_width = head_width * request.hold_body_width_ratio / 2.0;
                const auto top = std::max(0.0, std::min(begin_y, end_y));
                const auto bottom =
                    std::min(request.viewport_height_pixels, std::max(begin_y, end_y));
                if (bottom >= top) {
                    scene.hold_bodies.push_back(
                        {.note = note.note,
                         .lane = note.lane,
                         .semantic_color = note.semantic_color,
                         .bounds = {lane_center - body_half_width, top,
                                    lane_center + body_half_width, bottom}});
                }
                append_endpoint(end_y, true);
            }
        });
    if (!query_ok) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution,
                  "player-preview scene exceeded its composition-operation cap"));
        return result;
    }
    if (visible_note_cap_exceeded) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                           "player-preview visible-note cap was reached"));
        return result;
    }
    scene.composition_operations = operations;
    scene.visible_note_count = visible_notes;
    result.scene = std::move(scene);
    return result;
}

} // namespace wormedit

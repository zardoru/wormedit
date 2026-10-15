#include <wormedit/core/rational.h>
#include <wormedit/core/player_preview.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <tuple>
#include <unordered_map>

namespace wormedit {
namespace {

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

auto is_valid_skin(player_preview_skin_t skin) -> bool {
    return skin == player_preview_skin_t::neutral || skin == player_preview_skin_t::mania ||
           skin == player_preview_skin_t::arrows;
}

auto is_valid_direction(player_preview_scroll_direction_t direction) -> bool {
    return direction == player_preview_scroll_direction_t::toward_top ||
           direction == player_preview_scroll_direction_t::toward_bottom;
}

auto is_visible_lane(const lane_definition_t& lane) -> bool {
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

auto is_finite_positive(double value) -> bool {
    return std::isfinite(value) && value > 0.0;
}

auto is_valid_visual_layer(const std::optional<preview_visual_event_t>& event,
                        visual_event_kind_t expected_kind, std::uint64_t sample_frame) -> bool {
    return !event || (event->object && event->resource && event->kind == expected_kind &&
                      event->sample_frame <= sample_frame);
}

auto is_valid_visual_layers(const bga_layer_state_t& layers, std::uint64_t sample_frame) -> bool {
    return is_valid_visual_layer(layers.background, visual_event_kind_t::background, sample_frame) &&
           is_valid_visual_layer(layers.poor, visual_event_kind_t::poor, sample_frame) &&
           is_valid_visual_layer(layers.layer, visual_event_kind_t::layer, sample_frame) &&
           is_valid_visual_layer(layers.layer2, visual_event_kind_t::layer2, sample_frame) &&
           is_valid_visual_layer(layers.video, visual_event_kind_t::video, sample_frame) &&
           is_valid_visual_layer(layers.storyboard, visual_event_kind_t::storyboard, sample_frame);
}

auto is_valid_storyboard_instance(const storyboard_draw_instance_t& instance) -> bool {
    return instance.stable_id != 0 && instance.layer_order <= 4 && !instance.layer.empty() &&
           !instance.origin.empty() && !instance.path.empty() && std::isfinite(instance.x) &&
           std::isfinite(instance.y) && std::isfinite(instance.scale_x) &&
           std::isfinite(instance.scale_y) && std::isfinite(instance.rotation_radians) &&
           std::isfinite(instance.opacity) && std::isfinite(instance.red) &&
           std::isfinite(instance.green) && std::isfinite(instance.blue);
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

auto preview_y(double displacement, double current_displacement,
               const player_preview_scene_request_t& request) -> double {
    const auto distance = (displacement - current_displacement) * request.pixels_per_beat;
    return request.direction == player_preview_scroll_direction_t::toward_top
               ? request.receptor_y_pixels + distance
               : request.receptor_y_pixels - distance;
}

auto append_preview_endpoint(player_preview_scene_t& scene,
                             const player_preview_scene_request_t& request,
                             const player_preview_note_source_t& note,
                             const player_preview_lane_geometry_t& lane_geometry, double y,
                             bool tail)
    -> void {
    if (y + request.note_head_height_pixels / 2.0 < 0.0 ||
        y - request.note_head_height_pixels / 2.0 > request.viewport_height_pixels) {
        return;
    }
    const auto lane_center = (lane_geometry.bounds.left + lane_geometry.bounds.right) / 2.0;
    const auto head_width =
        std::max(1.0, lane_geometry.bounds.right - lane_geometry.bounds.left - 4.0);
    scene.note_endpoints.push_back(
        {.note = note.note,
         .lane = note.lane,
         .kind = note.kind,
         .glyph = lane_geometry.glyph,
         .tail = tail,
         .semantic_color = note.semantic_color,
         .bounds = {lane_center - head_width / 2.0, y - request.note_head_height_pixels / 2.0,
                    lane_center + head_width / 2.0, y + request.note_head_height_pixels / 2.0}});
}

struct note_render_context_t {
    player_preview_scene_t& scene;
    const player_preview_scene_request_t& request;
    const std::unordered_map<std::uint64_t, std::size_t>& lane_indices;
    double current_displacement;
    std::size_t visible_notes{};
    bool visible_note_cap_exceeded{};
};

auto append_preview_note(note_render_context_t& context,
                         const player_preview_note_source_t& note) -> void {
    const auto lane = context.lane_indices.find(note.lane.value);
    if (lane == context.lane_indices.end()) {
        return;
    }
    if (context.visible_notes >= context.request.maximum_visible_notes) {
        context.visible_note_cap_exceeded = true;
        return;
    }
    ++context.visible_notes;
    const auto& geometry = context.scene.lanes[lane->second];
    const auto begin_y = preview_y(note.begin_displacement_beats, context.current_displacement,
                                   context.request);
    const auto end_y = preview_y(note.end_displacement_beats, context.current_displacement,
                                 context.request);
    append_preview_endpoint(context.scene, context.request, note, geometry, begin_y, false);
    if (note.kind != note_kind_t::hold && note.kind != note_kind_t::roll) {
        return;
    }
    const auto lane_center = (geometry.bounds.left + geometry.bounds.right) / 2.0;
    const auto head_width = std::max(1.0, geometry.bounds.right - geometry.bounds.left - 4.0);
    const auto body_half_width = head_width * context.request.hold_body_width_ratio / 2.0;
    const auto top = std::max(0.0, std::min(begin_y, end_y));
    const auto bottom = std::min(context.request.viewport_height_pixels, std::max(begin_y, end_y));
    if (bottom >= top) {
        context.scene.hold_bodies.push_back(
            {.note = note.note,
             .lane = note.lane,
             .semantic_color = note.semantic_color,
             .bounds = {lane_center - body_half_width, top, lane_center + body_half_width,
                        bottom}});
    }
    append_preview_endpoint(context.scene, context.request, note, geometry, end_y, true);
}

auto append_index_note(player_preview_index_t& index,
                       const editor_field_scene_note_source_t& source,
                       diagnostic_list_t& diagnostics) -> void {
    if (source.begin_row >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        source.end_row > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                    "player-preview note row exceeds exact evaluator bounds",
                                    source.note));
        return;
    }
    const auto begin = scroll_speed_at(index.scroll_speed, index.timing,
                                       {static_cast<std::int64_t>(source.begin_row), 1});
    const auto end = scroll_speed_at(index.scroll_speed, index.timing,
                                     {static_cast<std::int64_t>(source.end_row), 1});
    if (!begin || !end) {
        const auto* failure = !begin ? &begin.error : &end.error;
        diagnostics.push_back(
            failure->has_value()
                ? timing_error(**failure)
                : error(diagnostic_code_t::invalid_rational,
                        "player-preview note displacement could not be evaluated", source.note));
        return;
    }
    const auto begin_displacement = begin.state->render_displacement_beats;
    const auto end_displacement = end.state->render_displacement_beats;
    if (!std::isfinite(begin_displacement) || !std::isfinite(end_displacement)) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_rational,
                                    "player-preview note displacement is not finite", source.note));
        return;
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

auto is_valid_scene_request(const player_preview_index_t& index,
                         const player_preview_scene_request_t& request) -> bool {
    return index.chart && index.document_revision != 0 && request.sample_rate != 0 &&
           is_finite_positive(request.viewport_width_pixels) &&
           is_finite_positive(request.viewport_height_pixels) &&
           is_finite_positive(request.pixels_per_beat) &&
           is_finite_positive(request.note_head_height_pixels) &&
           is_finite_positive(request.receptor_height_pixels) &&
           std::isfinite(request.receptor_y_pixels) && request.receptor_y_pixels >= 0.0 &&
           request.receptor_y_pixels <= request.viewport_height_pixels &&
           std::isfinite(request.split_stage_gutter_pixels) &&
           request.split_stage_gutter_pixels >= 0.0 &&
           std::isfinite(request.hold_body_width_ratio) && request.hold_body_width_ratio > 0.0 &&
           request.hold_body_width_ratio <= 1.0 && request.maximum_visible_notes != 0 &&
           request.maximum_composition_operations != 0 &&
           request.maximum_storyboard_instances != 0 &&
           request.storyboard_instances.size() <= request.maximum_storyboard_instances &&
           std::ranges::all_of(request.storyboard_instances, is_valid_storyboard_instance) &&
           is_valid_skin(request.skin) && is_valid_direction(request.direction) &&
           is_valid_visual_fit_policy(request.visual_fit) && !index.layouts.empty() &&
           is_valid_visual_layers(request.visual_layers, request.sample_frame) &&
           !index.timing.rows.empty() && index.note_tree_leaf_base != 0 &&
           index.note_tree_max_end.size() == index.note_tree_leaf_base * 2;
}

auto active_preview_layout(const player_preview_index_t& index, long double current_row,
                           bool pre_zero) -> const editor_field_scene_layout_source_t* {
    const auto layout = std::ranges::upper_bound(
        index.layouts, current_row, {}, [](const editor_field_scene_layout_source_t& source) {
            return static_cast<long double>(source.begin_row);
        });
    const auto active = pre_zero ? index.layouts.begin()
                                 : (layout == index.layouts.begin() ? index.layouts.begin()
                                                                    : std::prev(layout));
    if (active == index.layouts.end() ||
        (pre_zero ? active->begin_row != 0
                  : current_row < active->begin_row || current_row > active->end_row)) {
        return nullptr;
    }
    return &*active;
}

auto visible_sorted_lanes(const editor_field_scene_layout_source_t& layout)
    -> std::vector<const lane_definition_t*> {
    std::vector<const lane_definition_t*> lanes;
    for (const auto& lane : layout.layout.lanes) {
        if (is_visible_lane(lane)) {
            lanes.push_back(&lane);
        }
    }
    std::ranges::sort(lanes, [split_stage = layout.layout.split_stage](const auto* left, const auto* right) {
        if (split_stage && left->stage != right->stage) return left->stage < right->stage;
        return std::tie(left->display_order, left->id.value) <
               std::tie(right->display_order, right->id.value);
    });
    return lanes;
}

auto build_player_video_clock(const player_preview_scene_request_t& request)
    -> result_t<std::optional<video_preview_clock_t>, video_preview_clock_status_t> {
    const auto clock = video_preview_clock_t::from(
        request.visual_layers.video, request.sample_frame, request.sample_rate);
    if (clock)
        return std::optional{*clock};
    if (clock.error() == video_preview_clock_status_t::inactive)
        return std::nullopt;
    return std::unexpected(clock.error());
}

} // namespace

auto player_preview_note_source_t::displacement_less(const player_preview_note_source_t& left,
    const player_preview_note_source_t& right) -> bool {
    if (left.minimum_displacement_beats != right.minimum_displacement_beats)
        return left.minimum_displacement_beats < right.minimum_displacement_beats;
    return left.note.value < right.note.value;
}

auto player_preview_index_result_t::from(player_preview_index_outcome_t&& outcome) -> player_preview_index_result_t {
    return {.index = std::move(outcome.value), .diagnostics = std::move(outcome.diagnostics)};
}
auto player_preview_index_t::from(const chart_document_t& chart, std::uint64_t document_revision,
                                player_preview_index_options_t options)
    -> player_preview_index_outcome_t {
    player_preview_index_outcome_t result{};
    if (!chart.id || document_revision == 0 || options.maximum_notes == 0 ||
        options.maximum_bgm_events == 0 || options.maximum_layout_segments == 0 ||
        options.maximum_lane_definitions == 0) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution,
                  "player-preview index requires a chart, revision, and positive bounded limits"));
        return result;
    }
    auto field = editor_field_scene_index_t::from(
        chart, document_revision,
        {.maximum_notes = options.maximum_notes,
         .maximum_bgm_events = options.maximum_bgm_events,
         .maximum_layout_segments = options.maximum_layout_segments,
         .maximum_lane_definitions = options.maximum_lane_definitions});
    if (!field) {
        result.diagnostics = std::move(field.error());
        return result;
    }
    auto timing = timing_index_t::from(chart, {.allow_bounded_clock = true});
    if (!timing.value) {
        std::ranges::transform(timing.diagnostics, std::back_inserter(result.diagnostics),
                               timing_error);
        return result;
    }
    auto scroll_speed = scroll_speed_index_t::from(chart, *timing.value);
    if (!scroll_speed.value) {
        std::ranges::transform(scroll_speed.diagnostics, std::back_inserter(result.diagnostics),
                               timing_error);
        return result;
    }

    auto& field_index = *field;
    player_preview_index_t index{
        .chart = chart.id,
        .document_revision = document_revision,
        .timing = std::move(*timing.value),
        .scroll_speed = std::move(*scroll_speed.value),
        .layouts = std::move(field_index.layouts),
        .notes = {},
        .note_tree_leaf_base = 1,
        .note_tree_max_end = {},
    };
    index.notes.reserve(field_index.notes.size());
    for (const auto& source : field_index.notes) {
        append_index_note(index, source, result.diagnostics);
    }
    if (!result.diagnostics.empty()) {
        return result;
    }
    std::ranges::sort(index.notes, player_preview_note_source_t::displacement_less);
    while (index.note_tree_leaf_base < index.notes.size()) {
        if (index.note_tree_leaf_base > std::numeric_limits<std::size_t>::max() / 2) {
            result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                               "player-preview note index size overflowed"));
            return result;
        }
        index.note_tree_leaf_base *= 2;
    }
    index.note_tree_max_end = append_tree(index.notes, index.note_tree_leaf_base);
    result.value = std::move(index);
    return result;
}

auto build_player_preview_index(const chart_document_t& chart, std::uint64_t document_revision,
    player_preview_index_options_t options) -> player_preview_index_result_t {
    return player_preview_index_result_t::from(player_preview_index_t::from(chart, document_revision, options));
}

auto player_preview_scene_t::from(const player_preview_index_t& index,
                                  const player_preview_scene_request_t& request)
    -> player_preview_scene_outcome_t {
    player_preview_scene_outcome_t result{};
    if (!is_valid_scene_request(index, request)) {
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
    const auto video_clock = build_player_video_clock(request);
    if (!video_clock) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                           "player-preview video clock is invalid"));
        return result;
    }
    const auto position = index.timing.row_at({static_cast<std::int64_t>(request.sample_frame), request.sample_rate},
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
    const auto current_row = to_long_double(*position.absolute_row);
    const auto pre_zero = position.absolute_row->numerator < 0;
    const auto* active_layout = active_preview_layout(index, current_row, pre_zero);
    if (active_layout == nullptr) {
        result.diagnostics.push_back(error(diagnostic_code_t::unknown_lane,
                                           "player-preview clock has no active lane layout"));
        return result;
    }
    auto lanes = visible_sorted_lanes(*active_layout);
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
                                 .visual_fit = request.visual_fit,
                                 .pre_zero = pre_zero,
                                 .split_stage = active_layout->layout.split_stage,
                                 .visual_layers = request.visual_layers,
                                 .video_clock = *video_clock,
                                 .storyboard_instances = {request.storyboard_instances.begin(),
                                                          request.storyboard_instances.end()},
                                 .composition_operations = 0,
                                 .visible_note_count = 0,
                                 .lanes = {},
                                 .receptors = {},
                                 .hold_bodies = {},
                                 .note_endpoints = {}};
    const auto gutter = active_layout->layout.split_stage ? request.split_stage_gutter_pixels : 0.0;
    const auto lane_width =
        (request.viewport_width_pixels - gutter) / static_cast<double>(lanes.size());
    if (!is_finite_positive(lane_width)) {
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
    note_render_context_t render_context{scene, request, lane_indices,
                                         current.state->render_displacement_beats};
    const auto query_ok = query_notes(
        index, visible_low, visible_high, operations, request.maximum_composition_operations,
        std::bind_front(append_preview_note, std::ref(render_context)));
    if (!query_ok) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution,
                  "player-preview scene exceeded its composition-operation cap"));
        return result;
    }
    if (render_context.visible_note_cap_exceeded) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                           "player-preview visible-note cap was reached"));
        return result;
    }
    scene.composition_operations = operations;
    scene.visible_note_count = render_context.visible_notes;
    result.value = std::move(scene);
    return result;
}

} // namespace wormedit

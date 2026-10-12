#include "sys/compose_projection_internal.h"
namespace wormedit::sys::compose_projection_detail {
auto converted_bgm_preview_count(const wormedit::editor_interaction_state_t& state) -> std::size_t {
    if (!state.pending_note_move_gesture)
        return 0;
    std::unordered_set<std::uint64_t> selected_bgm;
    for (const auto& object : state.selected_objects)
        if (object.kind == wormedit::editor_selection_object_kind_t::bgm_event)
            selected_bgm.insert(object.id.value);
    return std::ranges::count_if(
        state.pending_note_move_gesture->bgm_preview,
        [&](const auto& preview) { return !selected_bgm.contains(preview.note.value); });
}

void populate_compose_selection_preview(
    const compose_scene_cache_t& cache,
    std::optional<wormedit::editor_interaction_state_t>& retained_state,
    wormedit::editor_interaction_view_model_t& interaction_view, WormUiComposeView& view) {
    interaction_view.pending_hold = retained_state->pending_hold;
    interaction_view.pending_note_duration_gesture = retained_state->pending_note_duration_gesture;
    interaction_view.pending_note_move_gesture = retained_state->pending_note_move_gesture;
    interaction_view.pending_note_paint_gesture =
        retained_state->pending_note_paint_gesture
            ? std::optional{wormedit::note_paint_gesture_preview_t{
                  .document_revision =
                      retained_state->pending_note_paint_gesture->document_revision,
                  .targets = retained_state->pending_note_paint_gesture->targets,
              }}
            : std::nullopt;
    interaction_view.retained_selection_range = retained_state->retained_selection_range;
    interaction_view.pending_editor_area_selection =
        retained_state->pending_editor_area_selection
            ? std::optional{wormedit::editor_area_selection_preview_t{
                  .document_revision =
                      retained_state->pending_editor_area_selection->document_revision,
                  .left_pixels =
                      std::min(retained_state->pending_editor_area_selection->anchor_x_pixels,
                               retained_state->pending_editor_area_selection->current_x_pixels),
                  .top_pixels =
                      std::min(retained_state->pending_editor_area_selection->anchor_y_pixels,
                               retained_state->pending_editor_area_selection->current_y_pixels),
                  .right_pixels =
                      std::max(retained_state->pending_editor_area_selection->anchor_x_pixels,
                               retained_state->pending_editor_area_selection->current_x_pixels),
                  .bottom_pixels =
                      std::max(retained_state->pending_editor_area_selection->anchor_y_pixels,
                               retained_state->pending_editor_area_selection->current_y_pixels),
                  .notes = retained_state->pending_editor_area_selection->notes,
                  .objects = retained_state->pending_editor_area_selection->objects,
              }}
            : std::nullopt;
    interaction_view.selected_notes = retained_state->selected_notes;
    interaction_view.selected_objects = retained_state->selected_objects;
    if (retained_state->retained_selection_range) {
        const auto& retained = *retained_state->retained_selection_range;
        const auto begin_row = wormedit::absolute_row(cache.scene.index->measures, retained.begin);
        const auto end_row = wormedit::absolute_row(cache.scene.index->measures, retained.end);
        const auto begin_measure =
            std::ranges::find(cache.scene.index->measures.entries, retained.begin.measure,
                              &wormedit::measure_index_entry_t::id);
        const auto end_measure =
            std::ranges::find(cache.scene.index->measures.entries, retained.end.measure,
                              &wormedit::measure_index_entry_t::id);
        if (begin_row && end_row && *begin_row < *end_row &&
            begin_measure != cache.scene.index->measures.entries.end() &&
            end_measure != cache.scene.index->measures.entries.end()) {
            view.area_range_available = true;
            view.area_range_begin_row = *begin_row;
            view.area_range_end_row = *end_row;
            view.beat_section_selection_span_available = true;
            view.beat_section_selection_begin_measure_number = static_cast<std::uint64_t>(
                std::distance(cache.scene.index->measures.entries.begin(), begin_measure) + 1);
            view.beat_section_selection_begin_row = retained.begin.row;
            view.beat_section_selection_end_measure_number = static_cast<std::uint64_t>(
                std::distance(cache.scene.index->measures.entries.begin(), end_measure) + 1);
            view.beat_section_selection_end_row = retained.end.row;
        } else {
            retained_state->retained_selection_range.reset();
            retained_state->retained_canvas_selection_range.reset();
            interaction_view.retained_selection_range.reset();
            interaction_view.retained_canvas_selection_range.reset();
        }
    }
    view.move_conflict_count = retained_state->pending_note_move_gesture
                                   ? retained_state->pending_note_move_gesture->conflicting_notes
                                   : 0;
    view.move_to_bgm_count = converted_bgm_preview_count(*retained_state);
    view.selected_note_count = retained_state->selected_notes.size();
    view.keyboard_context_note_id =
        retained_state->selected_notes.empty() ? 0 : retained_state->selected_notes.front().value;
    view.selected_object_count = retained_state->selected_objects.size();
}

auto quantize_report_row_view(const quantize_report_row_t& row)
    -> WormUiComposeQuantizeReportRowView {
    return {
        .object_id = row.object.value,
        .lane_id = row.lane.value,
        .start_before_absolute_row = row.start_before_absolute_row,
        .start_after_absolute_row = row.start_after_absolute_row,
        .start_before = {.measure_id = row.start_before.measure.value, .row = row.start_before.row},
        .start_after = {.measure_id = row.start_after.measure.value, .row = row.start_after.row},
        .has_end_before = row.end_before.has_value(),
        .has_end_after = row.end_after.has_value(),
        .end_before_absolute_row = row.end_before_absolute_row.value_or(0),
        .end_after_absolute_row = row.end_after_absolute_row.value_or(0),
        .end_before =
            row.end_before
                ? WormUiComposeQuantizePositionView{.measure_id = row.end_before->measure.value,
                                                    .row = row.end_before->row}
                : WormUiComposeQuantizePositionView{},
        .end_after =
            row.end_after
                ? WormUiComposeQuantizePositionView{.measure_id = row.end_after->measure.value,
                                                    .row = row.end_after->row}
                : WormUiComposeQuantizePositionView{},
        .moved = row.moved,
        .collision = row.collision,
        .collision_peer_id = row.collision_peer ? row.collision_peer->value : 0,
        .has_collision_peer = row.collision_peer.has_value(),
        .selected = row.selected,
        .head_halfway = row.head_halfway,
        .tail_halfway = row.tail_halfway,
        .stationary_tail_annotation = row.stationary_tail_annotation,
        .bgm = row.bgm};
}

auto active_quantize_request(application_command_state_t& application,
                             const wormedit::song_document_t& document,
                             const wormedit::chart_document_t& chart, std::uint64_t active_snap)
    -> wormedit::quantize_request_t {
    using namespace wormedit;
    if (application.quantize.quantize_editor && application.quantize.quantize_editor->song == document.id &&
        application.quantize.quantize_editor->chart == chart.id) {
        auto request = application.quantize.quantize_editor->request;
        request.objects.clear();
        if (application.quantize.quantize_editor->preset_index &&
            *application.quantize.quantize_editor->preset_index >= document.quantization_presets.size())
            application.quantize.quantize_editor->preset_index.reset();
        if (application.quantize.quantize_editor->preset_index) {
            const auto& preset =
                document.quantization_presets[*application.quantize.quantize_editor->preset_index];
            request.divisions = preset.divisions;
            request.space = preset.space;
            request.tie = preset.tie;
            request.holds = preset.holds;
            application.quantize.quantize_editor->request = request;
        }
        return request;
    }
    return quantize_request_t{.divisions = active_snap};
}

void populate_quantize_presets(compose_scene_cache_t& cache,
                               const wormedit::song_document_t& document, WormUiComposeView& view) {
    cache.quantize.quantize_preset_views.clear();
    cache.quantize.quantize_preset_views.reserve(document.quantization_presets.size());
    for (const auto& preset : document.quantization_presets) {
        cache.quantize.quantize_preset_views.push_back({.name = preset.name.c_str(),
                                               .divisions = preset.divisions,
                                               .space = static_cast<std::uint8_t>(preset.space),
                                               .tie = static_cast<std::uint8_t>(preset.tie),
                                               .holds = static_cast<std::uint8_t>(preset.holds)});
    }
    view.quantize_presets = cache.quantize.quantize_preset_views.data();
    view.quantize_preset_count = cache.quantize.quantize_preset_views.size();
}

void populate_measure_rows(compose_scene_cache_t& cache, const wormedit::chart_document_t& chart,
                           WormUiComposeView& view) {
    cache.measures.measure_row_views.clear();
    cache.measures.measure_row_ids.clear();
    const auto addressable =
        wormedit::chart_has_virtual_tail(chart)
            ? std::max(chart.measures.size(), wormedit::virtual_tail_measure_limit(chart))
            : chart.measures.size();
    const auto count = std::min(addressable, compose_measure_row_limit);
    const auto is_bms = chart.source_family == wormedit::format_family_t::bms;
    for (std::size_t index = 0; index < count; ++index) {
        const wormedit::measure_definition_t definition =
            index < chart.measures.size()
                ? chart.measures[index].definition
                : wormedit::measure_definition_t{{1, 1}, wormedit::meter_t{4, 4}};
        cache.measures.measure_row_views.push_back(
            {.measure_id = index,
             .number = is_bms ? index : index + 1,
             .length_numerator = definition.length.numerator,
             .length_denominator = definition.length.denominator,
             .has_meter = definition.meter.has_value(),
             .meter_numerator = definition.meter ? definition.meter->numerator : 0,
             .meter_denominator = definition.meter ? definition.meter->denominator : 0});
        cache.measures.measure_row_ids.push_back(index);
    }
    view.measure_rows = cache.measures.measure_row_views.data();
    view.measure_row_ids = cache.measures.measure_row_ids.data();
    view.measure_row_count = cache.measures.measure_row_views.size();
    view.measure_rows_truncated = addressable > count;
}

void populate_quantize_applied_report(compose_scene_cache_t& cache,
                                      const wormedit::song_document_t& document,
                                      const wormedit::chart_document_t& chart,
                                      const application_command_state_t& application,
                                      WormUiComposeView& view) {
    cache.quantize.quantize_applied_row_views.clear();
    if (!application.quantize.quantize_applied_report ||
        application.quantize.quantize_applied_report->song != document.id ||
        application.quantize.quantize_applied_report->chart != chart.id) {
        view.quantize_applied_rows = nullptr;
        view.quantize_applied_row_count = view.quantize_applied_total_rows = 0;
        view.quantize_applied_rows_truncated = false;
        return;
    }
    const auto& report = *application.quantize.quantize_applied_report;
    cache.quantize.quantize_applied_row_views.reserve(report.rows.size());
    for (const auto& row : report.rows)
        cache.quantize.quantize_applied_row_views.push_back(quantize_report_row_view(row));
    view.quantize_applied_rows = cache.quantize.quantize_applied_row_views.data();
    view.quantize_applied_row_count = cache.quantize.quantize_applied_row_views.size();
    view.quantize_applied_total_rows = report.total_rows;
    view.quantize_applied_rows_truncated = report.truncated;
}

void populate_quantize_preview(compose_scene_cache_t& cache,
                               const wormedit::song_document_t& document,
                               const wormedit::chart_document_t& chart, std::uint64_t revision,
                               const wormedit::editor_interaction_state_t& state,
                               application_command_state_t& application, WormUiComposeView& view) {
    view.quantize_preview_moved = view.quantize_preview_unchanged = 0;
    view.quantize_preview_blocked = 0;
    view.quantize_preview_colliding_objects = 0;
    view.quantize_preview_halfway_notes = view.quantize_preview_holds = 0;
    view.quantize_preview_message[0] = '\0';
    view.quantize_source_resolution = view.quantize_display_resolution = 0;
    view.quantize_resolution_scale = 1;
    view.quantize_geometry_valid = view.quantize_preview_drawable = false;
    cache.quantize.quantize_ghost_views.clear();
    view.quantize_ghosts = nullptr;
    view.quantize_ghost_count = 0;
    view.quantize_ghosts_drawable = false;
    cache.quantize.quantize_preview_row_views.clear();
    populate_quantize_presets(cache, document, view);
    populate_measure_rows(cache, chart, view);
    populate_quantize_applied_report(cache, document, chart, application, view);
    auto request =
        active_quantize_request(application, document, chart, state.active_snap_denominator);
    view.supported_snap_denominator_count = std::min(state.supported_snap_denominators.size(),
                                                     std::size(view.supported_snap_denominators));
    std::copy_n(state.supported_snap_denominators.begin(), view.supported_snap_denominator_count,
                view.supported_snap_denominators);
    view.quantize_divisions = request.divisions;
    view.quantize_space = static_cast<std::uint8_t>(request.space);
    view.quantize_tie = static_cast<std::uint8_t>(request.tie);
    view.quantize_holds = static_cast<std::uint8_t>(request.holds);
    view.quantize_preview_enabled = application.quantize.quantize_editor &&
                                    application.quantize.quantize_editor->song == document.id &&
                                    application.quantize.quantize_editor->chart == chart.id &&
                                    application.quantize.quantize_editor->preview_enabled;
    view.quantize_selected_preset_valid = application.quantize.quantize_editor &&
                                          application.quantize.quantize_editor->song == document.id &&
                                          application.quantize.quantize_editor->chart == chart.id &&
                                          application.quantize.quantize_editor->preset_index.has_value();
    view.quantize_selected_preset_index =
        view.quantize_selected_preset_valid ? *application.quantize.quantize_editor->preset_index : 0;
    request.selection = wormedit::editor_quantize_selection(state);
    if (request.selection->notes.empty() && request.selection->bgm_events.empty()) {
        view.quantize_preview_rows = nullptr;
        view.quantize_preview_row_count = view.quantize_preview_total_rows = 0;
        view.quantize_preview_rows_truncated = false;
        return;
    }
    const auto& preview =
        cache.quantize.quantize_preview.get(chart, revision, request.selection->notes, request);
    view.quantize_source_resolution = preview.source_resolution;
    view.quantize_display_resolution = preview.display_resolution;
    view.quantize_resolution_scale = preview.resolution_scale;
    view.quantize_geometry_valid = preview.geometry_valid;
    view.quantize_preview_drawable = preview.drawable;
    view.quantize_preview_moved = preview.moved;
    view.quantize_preview_unchanged = preview.unchanged;
    view.quantize_preview_blocked = preview.blocked;
    view.quantize_preview_colliding_objects = preview.colliding_objects;
    view.quantize_preview_halfway_notes = preview.halfway_notes;
    view.quantize_preview_holds = preview.holds;
    cache.quantize.quantize_preview_row_views.reserve(preview.report_rows.size());
    for (const auto& row : preview.report_rows)
        cache.quantize.quantize_preview_row_views.push_back(quantize_report_row_view(row));
    view.quantize_preview_rows = cache.quantize.quantize_preview_row_views.data();
    view.quantize_preview_row_count = cache.quantize.quantize_preview_row_views.size();
    view.quantize_preview_total_rows = preview.report_total_rows;
    view.quantize_preview_rows_truncated = preview.report_truncated;
    std::snprintf(view.quantize_preview_message, sizeof(view.quantize_preview_message), "%s",
                  preview.message.c_str());
}

auto quantize_lane_geometry(const wormedit::editor_field_scene_layout_source_t& layout,
                            wormedit::lane_id_t lane_id,
                            const wormedit::editor_field_scene_request_t& request,
                            quantize_lane_order_map_t& lane_orders)
    -> std::optional<quantize_lane_geometry_t> {
    auto [ordered, inserted] = lane_orders.try_emplace(&layout);
    auto& lanes = ordered->second;
    if (inserted) {
        lanes = wormedit::editor_presented_lanes(layout.layout, request.scratch_on_right);
    }
    const auto found = std::ranges::find(lanes, lane_id, [](const auto* lane) { return lane->id; });
    if (found == lanes.end() || lanes.empty())
        return std::nullopt;
    const auto split = layout.layout.split_stage && layout.layout.split_after_column
                           ? std::min<std::size_t>(*layout.layout.split_after_column, lanes.size())
                           : lanes.size();
    const auto gutter = split > 0 && split < lanes.size() ? request.split_stage_gutter_pixels : 0.0;
    if (!(request.field_width_pixels > gutter))
        return std::nullopt;
    const auto width = (request.field_width_pixels - gutter) / static_cast<double>(lanes.size());
    const auto index = static_cast<std::size_t>(std::distance(lanes.begin(), found));
    const auto left = request.field_left_pixels + width * static_cast<double>(index) +
                      (index >= split ? gutter : 0.0);
    return quantize_lane_geometry_t{left, left + width, (*found)->glyph};
}

auto quantize_layout_at(const wormedit::editor_field_scene_index_t& index, std::uint64_t row)
    -> const wormedit::editor_field_scene_layout_source_t* {
    const auto after = std::ranges::upper_bound(
        index.layouts, row, {}, &wormedit::editor_field_scene_layout_source_t::begin_row);
    if (after == index.layouts.begin())
        return nullptr;
    const auto& found = *std::prev(after);
    return row < found.end_row ? &found : nullptr;
}

auto quantize_endpoint_bounds(const quantize_lane_geometry_t& lane, double y, double height,
                              wormedit::editor_field_scroll_direction_t direction)
    -> WormUiComposeRect {
    const auto rectangular =
        lane.glyph == wormedit::lane_glyph_t::bar || lane.glyph == wormedit::lane_glyph_t::square;
    const auto top = rectangular && direction == wormedit::editor_field_scroll_direction_t::down
                         ? y - height
                     : rectangular ? y
                                   : y - height / 2.0;
    const auto bottom = rectangular && direction == wormedit::editor_field_scroll_direction_t::up
                            ? y + height
                        : rectangular ? y
                                      : y + height / 2.0;
    return {lane.left, top, lane.right, bottom};
}

auto quantize_endpoint_anchor(const WormUiComposeRect& bounds, wormedit::lane_glyph_t glyph,
                              wormedit::editor_field_scroll_direction_t direction)
    -> std::pair<double, double> {
    const auto center_x = (bounds.left + bounds.right) / 2.0;
    const auto rectangular =
        glyph == wormedit::lane_glyph_t::bar || glyph == wormedit::lane_glyph_t::square;
    const auto y =
        rectangular ? (direction == wormedit::editor_field_scroll_direction_t::up ? bounds.top
                                                                                  : bounds.bottom)
                    : (bounds.top + bounds.bottom) / 2.0;
    return {center_x, y};
}

auto project_quantize_endpoint(const quantize_projection_scope_t& scope,
                               wormedit::lane_id_t lane_id, std::uint64_t absolute_row,
                               bool promoted) -> std::optional<quantize_endpoint_projection_t> {
    const auto& index = scope.index;
    const auto& request = scope.request;
    auto& lane_orders = scope.lane_orders;
    const auto& scale = scope.scale;
    const auto& pixels_per_row = scope.pixels_per_row;
    const auto& scroll = scope.scroll;
    const auto& height = scope.height;
    const auto& downscroll = scope.downscroll;

    const auto layout_row = promoted ? absolute_row / scale : absolute_row;
    const auto* layout = quantize_layout_at(index, layout_row);
    if (!layout)
        return std::nullopt;
    const auto lane = quantize_lane_geometry(*layout, lane_id, request, lane_orders);
    if (!lane)
        return std::nullopt;
    const auto row = promoted ? static_cast<long double>(absolute_row) / scale : absolute_row;
    const auto y = compose_project_y(row, pixels_per_row, scroll, height, downscroll);
    const auto direction = downscroll ? wormedit::editor_field_scroll_direction_t::down
                                      : wormedit::editor_field_scroll_direction_t::up;
    auto bounds = quantize_endpoint_bounds(*lane, y, request.note_head_height_pixels, direction);
    const auto [anchor_x, anchor_y] = quantize_endpoint_anchor(bounds, lane->glyph, direction);
    return quantize_endpoint_projection_t{bounds, static_cast<std::uint32_t>(lane->glyph), anchor_x,
                                          anchor_y};
}

auto make_quantize_ghost_geometry(const quantize_projection_scope_t& scope,
                                  const WormUiComposeQuantizeReportRowView& report,
                                  const wormedit::editor_field_scene_note_source_t& note)
    -> std::optional<WormUiComposeQuantizeGhostView> {
    const auto& scale = scope.scale;

    const auto before_head =
        project_quantize_endpoint(scope, note.lane, report.start_before_absolute_row, false);
    const auto after_head =
        project_quantize_endpoint(scope, note.lane, report.start_after_absolute_row, true);
    if (!before_head || !after_head)
        return std::nullopt;
    WormUiComposeQuantizeGhostView ghost{};
    ghost.object_id = report.object_id;
    ghost.lane_id = report.lane_id;
    ghost.before_head = before_head->bounds;
    ghost.after_head = after_head->bounds;
    ghost.before_head_glyph = before_head->glyph;
    ghost.after_head_glyph = after_head->glyph;
    ghost.before_head_anchor_x = before_head->anchor_x;
    ghost.before_head_anchor_y = before_head->anchor_y;
    ghost.after_head_anchor_x = after_head->anchor_x;
    ghost.after_head_anchor_y = after_head->anchor_y;
    ghost.moved_head = report.start_after_absolute_row % scale != 0 ||
                       report.start_before_absolute_row != report.start_after_absolute_row / scale;
    ghost.head_halfway = report.head_halfway;
    ghost.collision = report.collision;
    ghost.stationary_tail_annotation = report.stationary_tail_annotation;
    return ghost;
}

auto add_quantize_tail_geometry(const quantize_projection_scope_t& scope,
                                WormUiComposeQuantizeGhostView& ghost,
                                const WormUiComposeQuantizeReportRowView& report) -> bool {
    const auto& scale = scope.scale;

    if (!report.has_end_before || !report.has_end_after)
        return true;
    const auto before = project_quantize_endpoint(scope, wormedit::lane_id_t{report.lane_id},
                                                  report.end_before_absolute_row, false);
    const auto after = project_quantize_endpoint(scope, wormedit::lane_id_t{report.lane_id},
                                                 report.end_after_absolute_row, true);
    if (!before || !after)
        return false;
    ghost.before_tail = before->bounds;
    ghost.after_tail = after->bounds;
    ghost.before_tail_glyph = before->glyph;
    ghost.after_tail_glyph = after->glyph;
    ghost.before_tail_anchor_x = before->anchor_x;
    ghost.before_tail_anchor_y = before->anchor_y;
    ghost.after_tail_anchor_x = after->anchor_x;
    ghost.after_tail_anchor_y = after->anchor_y;
    ghost.has_tail = true;
    ghost.moved_tail = report.end_after_absolute_row % scale != 0 ||
                       report.end_before_absolute_row != report.end_after_absolute_row / scale;
    ghost.tail_halfway = report.tail_halfway;
    return true;
}

auto bgm_quantize_ghost(const WormUiComposeQuantizeReportRowView& report,
                        const wormedit::chart_document_t& chart,
                        const wormedit::editor_field_scene_request_t& request, std::uint64_t scale,
                        double pixels_per_row, double scroll, double height, bool downscroll)
    -> std::optional<WormUiComposeQuantizeGhostView> {
    const auto lane =
        std::ranges::find(chart.bgm_source_lanes, wormedit::object_id_t{report.lane_id},
                          &wormedit::bgm_source_lane_t::id);
    if (lane == chart.bgm_source_lanes.end())
        return std::nullopt;
    const auto lane_index = static_cast<std::size_t>(lane - chart.bgm_source_lanes.begin());
    auto left = request.bgm_pane_left_pixels - request.bgm_horizontal_scroll_pixels;
    for (std::size_t i = 0; i < lane_index; ++i)
        left += wormedit::editor_bgm_lane_width_pixels(chart, request, i);
    const auto pane_right = request.bgm_pane_left_pixels + request.bgm_pane_width_pixels;
    const auto unclipped_right =
        left + wormedit::editor_bgm_lane_width_pixels(chart, request, lane_index);
    if (unclipped_right <= request.bgm_pane_left_pixels || left >= pane_right)
        return std::nullopt;
    left = std::max(left, request.bgm_pane_left_pixels);
    const auto right = std::min(unclipped_right, pane_right);
    const quantize_lane_geometry_t bounds{left, right, wormedit::lane_glyph_t::bar};
    const auto direction = downscroll ? wormedit::editor_field_scroll_direction_t::down
                                      : wormedit::editor_field_scroll_direction_t::up;
    const auto endpoint = [&](std::uint64_t row, bool promoted) {
        const auto coordinate = promoted ? static_cast<long double>(row) / scale : row;
        const auto y = compose_project_y(coordinate, pixels_per_row, scroll, height, downscroll);
        return quantize_endpoint_bounds(bounds, y, request.note_head_height_pixels, direction);
    };
    WormUiComposeQuantizeGhostView ghost{};
    ghost.object_id = report.object_id;
    ghost.lane_id = report.lane_id;
    ghost.before_head = endpoint(report.start_before_absolute_row, false);
    ghost.after_head = endpoint(report.start_after_absolute_row, true);
    ghost.before_head_anchor_x = ghost.after_head_anchor_x = (left + right) / 2;
    ghost.before_head_anchor_y = downscroll ? ghost.before_head.bottom : ghost.before_head.top;
    ghost.after_head_anchor_y = downscroll ? ghost.after_head.bottom : ghost.after_head.top;
    ghost.moved_head = report.start_after_absolute_row % scale != 0 ||
                       report.start_before_absolute_row != report.start_after_absolute_row / scale;
    ghost.head_halfway = report.head_halfway;
    ghost.tail_halfway = report.tail_halfway;
    ghost.stationary_tail_annotation = report.stationary_tail_annotation;
    ghost.collision = report.collision;
    ghost.has_tail = report.has_end_before && report.has_end_after;
    if (ghost.has_tail) {
        ghost.before_tail = endpoint(report.end_before_absolute_row, false);
        ghost.after_tail = endpoint(report.end_after_absolute_row, true);
        ghost.before_tail_anchor_x = ghost.after_tail_anchor_x = (left + right) / 2;
        ghost.before_tail_anchor_y = downscroll ? ghost.before_tail.bottom : ghost.before_tail.top;
        ghost.after_tail_anchor_y = downscroll ? ghost.after_tail.bottom : ghost.after_tail.top;
        ghost.moved_tail = report.end_after_absolute_row % scale != 0 ||
                           report.end_before_absolute_row != report.end_after_absolute_row / scale;
    }
    return ghost;
}

void append_bgm_quantize_ghost(compose_scene_cache_t& cache,
                               const WormUiComposeQuantizeReportRowView& report,
                               const wormedit::chart_document_t& chart,
                               const wormedit::editor_field_scene_request_t& request,
                               std::uint64_t scale, double pixels_per_row, double scroll,
                               double height, bool downscroll) {
    const auto event =
        std::ranges::find(cache.scene.index->bgm_events, wormedit::object_id_t{report.object_id},
                          &wormedit::editor_field_scene_bgm_source_t::event);
    if (event == cache.scene.index->bgm_events.end() || !quantize_row_visible(*event, request))
        return;
    const auto ghost = bgm_quantize_ghost(report, chart, request, scale, pixels_per_row, scroll,
                                          height, downscroll);
    if (ghost)
        cache.quantize.quantize_ghost_views.push_back(*ghost);
}

void populate_quantize_ghost_geometry(compose_scene_cache_t& cache,
                                      const wormedit::song_document_t& document,
                                      const wormedit::chart_document_t& chart,
                                      application_command_state_t& application,
                                      const wormedit::editor_interaction_state_t& state,
                                      const wormedit::editor_interaction_view_model_t& interaction,
                                      WormUiComposeView& view, double pixels_per_row,
                                      bool downscroll) {
    cache.quantize.quantize_ghost_views.clear();
    view.quantize_ghosts = nullptr;
    view.quantize_ghost_count = 0;
    view.quantize_ghosts_drawable = false;
    if (!view.quantize_preview_enabled || !cache.scene.index || !view.scene_available ||
        cache.quantize.quantize_preview_row_views.empty())
        return;
    auto quantize_request =
        active_quantize_request(application, document, chart, state.active_snap_denominator);
    quantize_request.selection = wormedit::editor_quantize_selection(state);
    const auto& preview = cache.quantize.quantize_preview.get(
        chart, document.revision, quantize_request.selection->notes, quantize_request);
    if (!preview.drawable || !preview.geometry_valid || preview.resolution_scale == 0)
        return;
    const auto& request = cache.frame.preview_scene_request;
    quantize_lane_order_map_t lane_orders;
    const quantize_projection_scope_t scope{*cache.scene.index,
                                            request,
                                            lane_orders,
                                            preview.resolution_scale,
                                            pixels_per_row,
                                            interaction.viewport_scroll_pixels,
                                            view.viewport_height_pixels,
                                            downscroll};
    cache.quantize.quantize_ghost_views.reserve(std::min<std::size_t>(
        quantize_report_row_capacity, cache.quantize.quantize_preview_row_views.size()));
    for (const auto& report : cache.quantize.quantize_preview_row_views) {
        if (!report.selected || report.object_id == 0)
            continue;
        if (report.bgm) {
            append_bgm_quantize_ghost(cache, report, chart, request, preview.resolution_scale,
                                      pixels_per_row, interaction.viewport_scroll_pixels,
                                      view.viewport_height_pixels, downscroll);
            continue;
        }
        const auto found = cache.scene.index->note_indices_by_id.find(report.object_id);
        if (found == cache.scene.index->note_indices_by_id.end())
            continue;
        const auto& note = cache.scene.index->notes[found->second];
        if (note.lane.value != report.lane_id || !quantize_row_visible(note, request))
            continue;
        auto ghost = make_quantize_ghost_geometry(scope, report, note);
        if (!ghost || !add_quantize_tail_geometry(scope, *ghost, report))
            continue;
        cache.quantize.quantize_ghost_views.push_back(*ghost);
    }
    view.quantize_ghosts = cache.quantize.quantize_ghost_views.data();
    view.quantize_ghost_count = cache.quantize.quantize_ghost_views.size();
    view.quantize_ghosts_drawable = preview.drawable && !cache.quantize.quantize_ghost_views.empty();
}
} // namespace wormedit::sys::compose_projection_detail

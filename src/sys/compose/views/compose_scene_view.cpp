#include "sys/compose/views/compose_view_internal.h"
#include "ui/compose_draw_source_labels.h"
#include <wormedit/formats/osu_mania.h>
namespace wormedit::sys::compose_projection_detail {
namespace {
using note_hitsounds_view_t = compose_scene_cache_t::scene_state_t::note_hitsounds_t;

auto find_note_hitsound(const note_hitsounds_view_t& hitsounds, std::uint64_t id)
    -> const wormedit::ui::hitsound_note_view_t* {
    const auto found = hitsounds.find(id);
    return found == hitsounds.end() ? nullptr : &found->second;
}

auto build_chart_note_hitsounds(const wormedit::chart_document_t& chart) -> note_hitsounds_view_t {
    note_hitsounds_view_t result;
    const auto context = wormedit::osu_sample_context_index_t::from(chart);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (!note.osu_hitsample) continue;
            const auto& sample = *note.osu_hitsample;
            result.emplace(note.id.value, wormedit::ui::hitsound_note_view_t::from(sample,
                context ? context->at(note.position) : wormedit::osu_sample_context_t{}, sample.filename));
        }
    }
    return result;
}

auto build_note_hitsounds_view(const compose_scene_projection_context_t& projection)
    -> const note_hitsounds_view_t& {
    const auto& chart = projection.inputs.scope.chart_scope.chart;
    return projection.inputs.frame.cache.scene.note_hitsounds.get_or_build(
        compose_scene_source_key_t::from(projection.history().document, chart),
        std::bind_front(build_chart_note_hitsounds, std::cref(chart)));
}
} // namespace

void populate_compose_sound_labels(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.inputs.frame.flattened;
    const auto& history = projection.history();
    const auto& bgm_events = projection.scene.bgm_events;
    const auto source = std::pair{compose_scene_source_key_t::from(
        history.document, projection.inputs.scope.chart_scope.chart), flattened.query_session_generation};
    if (flattened.compose.sound_labels_source != source) {
        flattened.compose.sound_labels.clear();
        flattened.compose.stem_filenames.clear();
        flattened.compose.sound_labels_source = source;
    }
    auto& visible_sound_ids = flattened.compose.visible_sound_ids;
    visible_sound_ids.clear();
    for (const auto& endpoint : projection.scene.note_endpoints)
        if (endpoint.sound) visible_sound_ids.push_back(endpoint.sound->value);
    const auto bgm_event_count =
        bgm_events.size();
    for (std::size_t index = 0; index < bgm_event_count; ++index)
        if (bgm_events[index].sound) visible_sound_ids.push_back(bgm_events[index].sound->value);
    std::ranges::sort(visible_sound_ids);
    for (const auto& resource : history.document.resources) {
        if (!std::ranges::binary_search(visible_sound_ids, resource.id.value) ||
            flattened.compose.sound_labels.contains(resource.id.value)) continue;
        flattened.compose.sound_labels.emplace(resource.id.value,
                                               compose_resource_sound_label(resource));
    }
}

auto prepare_compose_grid_rows(frontend_workspace_view_t& flattened, std::size_t count) -> bool {
    flattened.view.compose.grid_lines.clear();
    flattened.compose.grid.row.clear();
    flattened.view.compose.grid_lines.reserve(count);
    flattened.compose.grid.row.reserve(count);
    return true;
}

auto append_compose_grid_row(frontend_workspace_view_t& flattened,
                             const wormedit::ui::compose_grid_line_view_t& line,
                             const wormedit::exact_chart_row_t& row) -> bool {
    flattened.view.compose.grid_lines.push_back(line);
    flattened.compose.grid.row.push_back(row);
    return true;
}

auto compose_grid_phase(const compose_scene_projection_context_t& projection, std::uint64_t row)
    -> wormedit::grid_phase_t {
    return wormedit::grid_phase(projection.inputs.scope.chart_scope.chart, projection.index.measures,
                                row, projection.inputs.scope.interaction_view.active_snap_space);
}

// The measure a real measure line starts; virtual BMS measures and other lines have none.
auto grid_line_measure_id(const wormedit::editor_field_grid_geometry_t& line, std::uint32_t measure_number,
                          const wormedit::measure_index_t& measures) -> wormedit::measure_id_t {
    if (line.kind != wormedit::editor_grid_line_kind_t::measure || measure_number == 0 ||
        measure_number > measures.entries.size() ||
        measures.entries[measure_number - 1].absolute_begin_row != line.chart_row.whole_rows)
        return {};
    return measures.entries[measure_number - 1].id;
}

void build_compose_grid_lines_view(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.inputs.frame.flattened;
    const auto& chart = projection.inputs.scope.chart_scope.chart;
    const auto& scene = projection.scene;
    auto& view = projection.inputs.frame.view();
    const auto& lines = scene.grid_lines;
    flattened.view.compose.grid_lines.clear();
    flattened.compose.grid.row.clear();
    if (lines.empty())
        return;
    const auto line_count =
        lines.size();
    std::optional<wormedit::virtual_canvas_t> virtual_canvas;
    if (view.bms_measure_labels) {
        auto canvas = wormedit::build_virtual_canvas(chart);
        if (auto* built = std::get_if<wormedit::virtual_canvas_t>(&canvas)) {
            virtual_canvas = std::move(*built);
        }
    }
    if (!prepare_compose_grid_rows(flattened, line_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < line_count; ++index) {
        const auto& line = lines[index];
        const auto& measures = projection.index.measures.entries;
        const auto measure = std::ranges::lower_bound(
            measures, line.chart_row.whole_rows, {},
            &wormedit::measure_index_entry_t::absolute_begin_row);
        std::uint32_t measure_number = measure != measures.end() &&
            measure->absolute_begin_row == line.chart_row.whole_rows
                ? static_cast<std::uint32_t>(measure - measures.begin() + 1) : 0;
        if (line.kind == wormedit::editor_grid_line_kind_t::measure && measure_number == 0 &&
            virtual_canvas) {
            const auto virtual_measure =
                virtual_canvas->measure_containing(line.chart_row.whole_rows);
            if (virtual_measure && virtual_measure->begin_row == line.chart_row.whole_rows) {
                measure_number = static_cast<std::uint32_t>(virtual_measure->number + 1);
            }
        }
        if (!append_compose_grid_row(
                flattened,
                {.y_pixels = line.y_pixels,
                 .kind = line.kind,
                 .emphasis = line.emphasis,
                 .measure_number =
                     line.kind == wormedit::editor_grid_line_kind_t::measure ? measure_number : 0,
                 .chart_row =
                     line.chart_row.fractional_rows.numerator == 0 ? line.chart_row.whole_rows : 0,
                 .snap_class = wormedit::ui::note_snap_class(
                     compose_grid_phase(projection, line.chart_row.whole_rows),
                     line.chart_row.fractional_rows.numerator,
                     line.chart_row.fractional_rows.denominator),
                 .measure_id = grid_line_measure_id(line, measure_number, projection.index.measures)},
                line.chart_row)) {
            view.truncated = true;
            break;
        }
    }
    view.truncated = view.truncated || line_count != lines.size();
}

void build_compose_scene_objects_view(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.inputs.frame.flattened;
    const auto& scene = projection.scene;
    auto& view = projection.inputs.frame.view();
    const auto& differences = scene.comparison_differences;
    const auto difference_count =
        differences.size();
    if (!prepare_compose_geometry_rows(flattened, flattened.view.compose.differences, difference_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < difference_count; ++index) {
        append_compose_geometry_row(flattened.view.compose.differences,
                                    {.bounds = wormedit::ui::compose_rect_t::from(differences[index].bounds)},
                                    view.truncated);
    }
    const auto& holds = scene.hold_bodies;
    const auto hold_count =
        holds.size();
    if (!prepare_compose_geometry_rows(flattened, flattened.view.compose.hold_bodies, hold_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < hold_count; ++index) {
        const auto& hold = holds[index];
        append_compose_geometry_row(
            flattened.view.compose.hold_bodies,
            {.bounds = wormedit::ui::compose_rect_t::from(hold.bounds),
             .note_id = hold.note,
             .lane_id = hold.lane,
             .display_lane_number = compose_note_display_lane_number(scene, hold),
             .key_color = compose_note_key_color(scene, hold),
             .snap_class = compose_endpoint_snap_class(projection, hold),
             .selected = hold.selected,
             .proposal_origin = hold.proposal_origin,
             .proposal_ghost = hold.proposal_ghost,
             .semantic_color_visible = hold.semantic_color.has_value(),
             .semantic_red =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->red : 0),
             .semantic_green =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->green : 0),
             .semantic_blue =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->blue : 0),
             .semantic_alpha =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->alpha : 0),
             .move_conflict = hold.move_conflict,
             .move_conflict_occupant = hold.move_conflict_occupant},
            view.truncated);
    }
    const auto& endpoints = scene.note_endpoints;
    const auto& hitsounds = build_note_hitsounds_view(projection);
    const auto endpoint_count =
        endpoints.size();
    if (!prepare_compose_geometry_rows(flattened, flattened.view.compose.note_endpoints, endpoint_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < endpoint_count; ++index) {
        const auto& endpoint = endpoints[index];
        const auto sound = compose_note_sound_text(endpoint.sound, flattened.compose.sound_labels);
        append_compose_geometry_row(
            flattened.view.compose.note_endpoints,
            {.bounds = wormedit::ui::compose_rect_t::from(endpoint.bounds),
             .note_id = endpoint.note,
             .lane_id = endpoint.lane,
             .display_lane_number = compose_note_display_lane_number(scene, endpoint),
             .key_color = compose_note_key_color(scene, endpoint),
             .glyph = endpoint.glyph,
             .sound_label = sound.label,
             .sound_detail = sound.detail,
             .sound_resource_id = endpoint.sound.value_or(wormedit::resource_id_t{}),
             .hitsound = find_note_hitsound(hitsounds, endpoint.note.value),
             .snap_class = compose_endpoint_snap_class(projection, endpoint),
             .kind = endpoint.kind,
             .tail = endpoint.tail,
             .selected = endpoint.selected,
             .proposal_origin = endpoint.proposal_origin,
             .proposal_ghost = endpoint.proposal_ghost,
             .semantic_color_visible = endpoint.semantic_color.has_value(),
             .semantic_red = static_cast<std::uint8_t>(
                 endpoint.semantic_color ? endpoint.semantic_color->red : 0),
             .semantic_green = static_cast<std::uint8_t>(
                 endpoint.semantic_color ? endpoint.semantic_color->green : 0),
             .semantic_blue = static_cast<std::uint8_t>(
                 endpoint.semantic_color ? endpoint.semantic_color->blue : 0),
             .semantic_alpha = static_cast<std::uint8_t>(
                 endpoint.semantic_color ? endpoint.semantic_color->alpha : 0),
             .paint_preview = endpoint.paint_preview,
             .paint_erase_preview = endpoint.paint_erase_preview,
             .paint_invalid_preview = endpoint.paint_invalid_preview,
             .move_conflict = endpoint.move_conflict,
             .move_conflict_occupant = endpoint.move_conflict_occupant},
            view.truncated);
    }
    view.truncated = view.truncated || difference_count != differences.size() ||
                     hold_count != holds.size() || endpoint_count != endpoints.size();
}

void build_compose_layout_and_notes_view(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.inputs.frame.flattened;
    const auto& scene = projection.scene;
    auto& view = projection.inputs.frame.view();
    const auto field_left_pixels = projection.field_left_pixels;
    const auto playable_field_width = projection.playable_field_width;
    const auto& bands = scene.layout_bands;
    const auto band_count =
        bands.size();
    if (!prepare_compose_geometry_rows(flattened, flattened.view.compose.bands, band_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < band_count; ++index) {
        const auto& band = bands[index];
        append_compose_geometry_row(
            flattened.view.compose.bands,
            {.bounds = {field_left_pixels, band.top_pixels,
                        field_left_pixels + playable_field_width, band.bottom_pixels},
             .split_stage = band.split_stage},
            view.truncated);
    }
    const auto& lanes = scene.lanes;
    const auto lane_count =
        lanes.size();
    if (!prepare_compose_geometry_rows(flattened, flattened.view.compose.lanes, lane_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < lane_count; ++index) {
        const auto& lane = lanes[index];
        append_compose_geometry_row(flattened.view.compose.lanes,
                                    {.bounds = wormedit::ui::compose_rect_t::from(lane.bounds),
                                     .lane_id = lane.lane,
                                     .display_order = lane.display_order,
                                     .role = lane.role,
                                     .stage = lane.stage,
                                     .glyph = lane.glyph,
                                     .key_color = lane.key_color},
                                    view.truncated);
    }
    build_compose_grid_lines_view(projection);
    build_compose_scene_objects_view(projection);
    view.truncated = view.truncated ||
                     band_count != bands.size() || lane_count != lanes.size();
}

void build_compose_bgm_events_view(
    frontend_workspace_view_t& flattened,
    const std::vector<wormedit::editor_field_bgm_event_geometry_t>& events,
    wormedit::ui::compose_view_t& view) {
    const auto event_count =
        events.size();
    if (!prepare_compose_geometry_rows(flattened, flattened.view.compose.bgm_events, event_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < event_count; ++index) {
        const auto& event = events[index];
        const auto sound = event.sound ? flattened.compose.sound_labels.find(event.sound->value)
                                       : flattened.compose.sound_labels.end();
        append_compose_geometry_row(
            flattened.view.compose.bgm_events,
            {.bounds = wormedit::ui::compose_rect_t::from(event.bounds),
             .event_id = event.event,
             .source_lane_id = event.source_lane,
             .sound_id = event.sound.value_or(wormedit::resource_id_t{}),
             .has_sound = event.sound.has_value(),
             .bmson_slice = event.bmson_restart.has_value(),
             .bmson_restart = event.bmson_restart.value_or(false),
             .selected = event.selected,
             .has_tail = event.has_tail,
             .proposal_ghost = event.proposal_ghost,
             .move_conflict = event.move_conflict,
             .sound_label = sound == flattened.compose.sound_labels.end()
                                ? (event.sound ? "?" : "")
                                : sound->second.first.c_str(),
             .sound_detail = sound == flattened.compose.sound_labels.end()
                                 ? (event.sound ? "Unknown sound resource" : "Unbound BGM slot")
                                 : sound->second.second.c_str(),
             .semantic_color_visible = event.semantic_color.has_value(),
             .semantic_red =
                 static_cast<std::uint8_t>(event.semantic_color ? event.semantic_color->red : 0),
             .semantic_green =
                 static_cast<std::uint8_t>(event.semantic_color ? event.semantic_color->green : 0),
             .semantic_blue =
                 static_cast<std::uint8_t>(event.semantic_color ? event.semantic_color->blue : 0),
             .semantic_alpha =
                 static_cast<std::uint8_t>(event.semantic_color ? event.semantic_color->alpha : 0),
             .head_top_pixels = static_cast<float>(wormedit::ui::compose_rect_t::from(event.head_bounds).top),
             .head_bottom_pixels = static_cast<float>(wormedit::ui::compose_rect_t::from(event.head_bounds).bottom)},
            view.truncated);
    }
    view.truncated = view.truncated || event_count != events.size();
}

auto compose_stem_filename(const compose_scene_projection_context_t& projection,
                           const wormedit::bgm_source_lane_t& lane) -> std::string_view {
    if (!lane.stem_channel) return {};
    const auto channel = std::ranges::find(projection.inputs.scope.chart_scope.chart.sound_channels,
                                           *lane.stem_channel, &wormedit::sound_channel_t::id);
    if (channel == projection.inputs.scope.chart_scope.chart.sound_channels.end()) return {};
    const auto* asset = projection.history().document.find_audio_asset(channel->source_clip.source);
    if (!asset) return {};
    auto& filenames = projection.inputs.frame.flattened.compose.stem_filenames;
    const auto found = filenames.find(asset->id.value);
    if (found != filenames.end()) return found->second;
    const auto name = asset->source_path.filename().generic_u8string();
    return filenames.emplace(asset->id.value,
        std::string{reinterpret_cast<const char*>(name.data()), name.size()}).first->second;
}

void build_compose_bgm_lane_modes_view(const compose_scene_projection_context_t& projection,
                                    const compose_scene_cache_t& cache) {
    using namespace wormedit;
    const auto& chart = projection.inputs.scope.chart_scope.chart;
    if (chart.source_family != format_family_t::bmson)
        return;
    for (auto& lane : projection.inputs.frame.flattened.view.compose.bgm_lanes) {
        const auto source = std::ranges::find(
            chart.bgm_source_lanes, lane.source_lane_id, &bgm_source_lane_t::id);
        if (source == chart.bgm_source_lanes.end() || !source->stem_channel)
            continue;
        lane.bmson_channel_id = *source->stem_channel;
        const auto continues =
            cache.stems.draw_stem_sources.value().channel_continues.find(lane.bmson_channel_id.value);
        lane.bmson_channel_continues =
            continues != cache.stems.draw_stem_sources.value().channel_continues.end() && continues->second;
    }
}

void build_compose_bgm_view(const compose_scene_projection_context_t& projection,
                         const compose_scene_cache_t& cache) {
    auto& flattened = projection.inputs.frame.flattened;
    const auto& scene = projection.scene;
    auto& view = projection.inputs.frame.view();
    const auto& bgm_lanes = scene.bgm_lanes;
    const auto bgm_lane_count =
        bgm_lanes.size();
    if (!prepare_compose_geometry_rows(flattened, flattened.view.compose.bgm_lanes, bgm_lane_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < bgm_lane_count; ++index) {
        const auto& lane = bgm_lanes[index];
        const auto source = std::ranges::find(projection.inputs.scope.chart_scope.chart.bgm_source_lanes,
                                              lane.source_lane, &wormedit::bgm_source_lane_t::id);
        append_compose_geometry_row(
            flattened.view.compose.bgm_lanes,
            {.bounds = wormedit::ui::compose_rect_t::from(lane.bounds),
             .source_lane_id = lane.source_lane,
             .source_order = lane.source_order,
             .stem = source != projection.inputs.scope.chart_scope.chart.bgm_source_lanes.end() &&
                     source->stem_channel.has_value(),
             .width_pixels = lane.unclipped_right_pixels - lane.unclipped_left_pixels,
             .resize_edge_pixels = lane.unclipped_right_pixels,
             .filename = source == projection.inputs.scope.chart_scope.chart.bgm_source_lanes.end()
                             ? std::string_view{}
                             : compose_stem_filename(projection, *source)},
            view.truncated);
    }
    build_compose_bgm_events_view(flattened, scene.bgm_events, view);
    build_compose_bgm_lane_modes_view(projection, cache);
    view.truncated = view.truncated || bgm_lane_count != bgm_lanes.size();
}

auto bga_preview_authored_row(const compose_scene_projection_context_t& projection, double source_y)
    -> std::optional<std::uint64_t> {
    const auto position = wormedit::editor_position_at_y_pixels(
        projection.inputs.scope.chart_scope.chart, projection.inputs.scope.interaction_view, source_y);
    if (!position)
        return std::nullopt;
    const auto target = wormedit::snap_position_or_exact(projection.inputs.scope.chart_scope.chart,
        projection.index.measures, *position, projection.bga_move_snap);
    return target ? (projection.index.measures).row_of(*target) : std::nullopt;
}

auto bga_preview_virtual_row(const compose_scene_projection_context_t& projection, double source_y)
    -> std::optional<std::uint64_t> {
    using namespace wormedit;
    if (projection.inputs.scope.chart_scope.chart.source_family != format_family_t::bms ||
        !has_chart_virtual_tail(projection.inputs.scope.chart_scope.chart))
        return std::nullopt;
    const auto& interaction = projection.inputs.scope.interaction_view;
    const auto raw = (static_cast<long double>(interaction.viewport_scroll_pixels) + source_y) *
                     projection.inputs.scope.chart_scope.chart.divisions_per_standard_measure /
                     interaction.pixels_per_standard_measure;
    if (!std::isfinite(raw) || raw < 0.0L ||
        raw >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
        return std::nullopt;
    const auto rounded = static_cast<std::uint64_t>(std::floor(raw + 0.5L));
    const auto built = build_virtual_canvas(projection.inputs.scope.chart_scope.chart);
    const auto* canvas = std::get_if<virtual_canvas_t>(&built);
    const auto source_measure = canvas ? canvas->measure_containing(rounded) : std::nullopt;
    if (!source_measure || source_measure->authored)
        return std::nullopt;
    const auto target_row = snap_canvas_row_or_exact(projection.inputs.scope.chart_scope.chart,
        canvas->authored_index, rounded, projection.bga_move_snap, canvas->total_rows);
    const auto target_measure =
        canvas && target_row ? canvas->measure_containing(*target_row) : std::nullopt;
    return target_measure && !target_measure->authored ? target_row : std::nullopt;
}

auto bga_preview_target_row(const compose_scene_projection_context_t& projection,
                            const compose_bga_move_preview_input_t& input)
    -> std::optional<std::uint64_t> {
    const auto height = projection.inputs.scope.interaction_view.viewport_height_pixels;
    const auto y = std::clamp(input.y_pixels, 0.0, std::max(0.0, height - 0.001));
    const auto source_y = projection.inputs.scope.downscroll ? height - y : y;
    if (const auto authored = bga_preview_authored_row(projection, source_y))
        return authored;
    return bga_preview_virtual_row(projection, source_y);
}

void build_compose_bga_move_preview_view(const compose_scene_projection_context_t& projection) {
    auto& view = projection.inputs.frame.view();
    view.bga_move_preview_valid = false;
    const auto* input = projection.bga_move_preview;
    if (!input || input->event_id == 0 || input->project_id != view.project_id.value ||
        input->chart_id != projection.inputs.scope.chart_scope.chart.id.value ||
        input->document_revision != view.document_revision || !std::isfinite(input->y_pixels))
        return;
    const auto event =
        std::ranges::find(projection.scene.bga_events, wormedit::object_id_t{input->event_id},
                          &wormedit::editor_field_bga_event_geometry_t::event);
    if (event == projection.scene.bga_events.end() ||
        !wormedit::editor_bga_lane_index(event->kind) || !event->selected)
        return;
    const auto row = bga_preview_target_row(projection, *input);
    if (!row)
        return;
    view.bga_move_preview_valid = true;
    view.bga_move_preview_event_id.value = input->event_id;
    view.bga_move_preview_y_pixels =
        compose_project_y(*row, projection.inputs.scope.pixels_per_row,
                          projection.inputs.scope.interaction_view.viewport_scroll_pixels,
                          view.viewport_height_pixels, projection.inputs.scope.downscroll);
}

void build_compose_bga_view(const compose_scene_projection_context_t& projection) {
    static_assert(wormedit::bga_lane_overlap_stack_threshold ==
                  WORM_UI_COMPOSE_BGA_OVERLAP_STACK_THRESHOLD);
    auto& flattened = projection.inputs.frame.flattened;
    auto& view = projection.inputs.frame.view();
    const auto& scene = projection.scene;
    const auto count = scene.bga_events.size();
    if (!prepare_compose_geometry_rows(flattened, view.bga_events, count)) {
        view.truncated = true;
        return;
    }
    view.bga_lane_visible = scene.bga_lane.has_value();
    view.bga_revealed_lane_mask = projection.inputs.scope.interaction_view.bga_revealed_lane_mask;
    view.bga_lane_count = std::min<std::size_t>(scene.bga_lanes.size(), 4);
    for (std::size_t index = 0; index < view.bga_lane_count; ++index) {
        const auto& lane = scene.bga_lanes[index];
        view.bga_lane_rects[index] = {lane.bounds.left, lane.bounds.top, lane.bounds.right,
                                      lane.bounds.bottom};
        view.bga_lane_kinds[index] = static_cast<std::uint8_t>(lane.kind);
    }
    view.bga_lane_bounds =
        scene.bga_lane
            ? wormedit::ui::compose_rect_t{scene.bga_lane->bounds.left, scene.bga_lane->bounds.top,
                                scene.bga_lane->bounds.right, scene.bga_lane->bounds.bottom}
            : wormedit::ui::compose_rect_t{};
    for (std::size_t index = 0; index < count; ++index) {
        const auto& event = scene.bga_events[index];
        const auto& document = projection.history().document;
        const auto resource =
            std::ranges::find(document.resources, event.resource, &wormedit::resource_entry_t::id);
        const wormedit::target_resource_identity_t* identity{};
        if (resource != document.resources.end()) {
            const auto target =
                std::ranges::find_if(resource->target_identities, [](const auto& value) {
                    return value.family == wormedit::format_family_t::bms &&
                           value.namespace_name == "BMP";
                });
            if (target != resource->target_identities.end())
                identity = &*target;
        }
        const auto* name = resource != document.resources.end() && !resource->name.empty()
                               ? resource->name.c_str()
                               : "Missing BGA resource";
        const auto* bmp_id = identity ? identity->identifier.c_str() : "";
        const auto asset = resource == document.resources.end()
                               ? document.visual_assets.end()
                               : std::ranges::find(document.visual_assets, resource->asset,
                                                   &wormedit::visual_asset_t::id);
        const auto expected_asset_kind = event.kind == wormedit::visual_event_kind_t::video
                                             ? wormedit::asset_kind_t::video
                                             : wormedit::asset_kind_t::image;
        append_compose_geometry_row(
            view.bga_events,
            {.event_id = event.event,
             .resource_id = event.resource,
             .canvas_row = event.row,
             .overlap_count = event.overlap_count,
             .kind = event.kind,
             .name = name,
             .bmp_id = bmp_id,
             .missing = resource == document.resources.end() || resource->missing ||
                        asset == document.visual_assets.end() || asset->kind != expected_asset_kind,
             .selected = event.selected,
             .bounds = {event.bounds.left, event.bounds.top, event.bounds.right,
                        event.bounds.bottom}},
            view.truncated);
    }
    view.truncated = view.truncated || count != scene.bga_events.size();
    build_compose_bga_move_preview_view(projection);
}

void build_compose_marker_draft_view(const compose_scene_projection_context_t& projection) {
    const auto& draft = projection.inputs.frame.application_state.project.compose_gesture.bmson_marker_draft;
    if (!draft || projection.inputs.scope.pixels_per_row <= 0.0)
        return;
    const auto* reference =
        projection.inputs.scope.chart_scope.chart.find_bgm_event(draft->payload.reference);
    if (!reference)
        return;
    const auto lane = std::ranges::find_if(projection.scene.bgm_lanes, [&](const auto& candidate) {
        return candidate.source_lane == reference->source_lane;
    });
    const auto* bounds = lane == projection.scene.bgm_lanes.end() ? nullptr : &lane->bounds;
    for (const auto& marker : draft->payload.markers)
        build_compose_draft_marker_view(projection, marker, bounds);
}

void build_compose_marker_previews_view(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.inputs.frame.flattened;
    flattened.view.compose.cursor.reset();
    const auto& scene = projection.scene;
    build_compose_marker_draft_view(projection);
    if (scene.cursor) {
        flattened.view.compose.cursor = wormedit::ui::compose_cursor_view_t{
            .bounds = wormedit::ui::compose_rect_t::from(scene.cursor->bounds),
            .lane_id = scene.cursor->lane,
        };
    }
}


bool initialize_compose_view_state(
    frontend_workspace_view_t& flattened, compose_scene_cache_t& cache, int window_width,
    int window_height, bool default_bgm_bar_hidden,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    const application_command_state_t& application_state) {
    auto& view = flattened.view.compose;
    if (!view.visible) {
        cache.frame.interaction_view.reset();
        cache.frame.hover.reset();
        cache.frame.paint_pointer.reset();
        cache.frame.focus_note_label_editor = false;
        cache.frame.viewport_autoscroll_active = false;
        return false;
    }
    view.viewport_width_pixels = std::max(160.0, static_cast<double>(window_width) - 48.0);
    view.bgm_bar_available = cache.frame.bgm_bar_audio_seen;
    view.bgm_bar_reserved = cache.frame.bgm_bar_audio_seen &&
                            !application_state.project.bgm.bgm_bar_hidden.value_or(default_bgm_bar_hidden);
    view.viewport_height_pixels = compose_surface_height(window_height, flattened.view);
    view.bga_lane_visible = !retained_state || retained_state->bga_lane_visible;
    view.bga_revealed_lane_mask = retained_state ? retained_state->bga_revealed_lane_mask : 0;
    view.playable_field_left_pixels = compose_measure_rail_width_pixels;
    view.playable_field_right_pixels = view.viewport_width_pixels;
    view.same_sound_selection_available = false;
    view.move_to_bgm_available = false;
    view.bms_measure_labels = false;
    view.bmson_tools_available = false;
    view.compose_measure_count = 0;
    view.selected_bms_measure_number_plus_one = 0;
    view.timing_document_begin_fraction = 0.0;
    view.timing_document_end_fraction = 1.0;
    view.truncated = false;
    view.pixels_per_standard_measure = retained_state
                                           ? retained_state->pixels_per_standard_measure
                                           : wormedit::default_pixels_per_standard_measure;
    const auto notes_editable =
        wormedit::ui::compose_notes_editable(compose_mode_of(application_state));
    view.resource_paint_available = false;
    view.resource_paint_has_sample = false;
    view.resource_paint_mode_active = notes_editable &&
        application_state.project.compose_gesture.resource_paint_mode_active;
    view.draw_source_kind = application_state.project.compose_gesture.draw_source_kind;
    view.draw_source_id.value = application_state.project.compose_gesture.draw_source_id;
    view.draw_source_label = ui::no_draw_sound_resource_label;
    view.draw_source_missing = false;
    view.semantic_layer_target_row = 0;
    view.note_paint_mode_active = notes_editable && application_state.project.compose_gesture.note_paint_mode_active;
    view.semantic_layer_paint_mode_active =
        notes_editable && application_state.project.compose_gesture.semantic_layer_paint_mode_active;
    view.semantic_layer_gesture_active =
        retained_state && retained_state->is_pending();
    view.timing_stage_active = false;
    view.timing_stage_valid = false;
    view.timing_stage_lane = wormedit::timing_lane_kind_t::bpm;
    view.timing_stage_y_pixels = 0.0;
    view.timing_lane_visibility_mask = effective_compose_timing_lane_mask(application_state);
    view.timing_focus = application_state.project.compose_view.compose_timing_focus;
    view.selected_note_count = 0;
    view.keyboard_context_note_id.value = 0;
    view.move_conflict_count = 0;
    view.selected_bgm_event_count = 0;
    view.selected_assigned_resource_id.value = 0;
    view.selected_bgm_event_id.value = 0;
    view.selected_bmson_bgm_event_id.value = 0;
    view.selected_bmson_note_event_id.value = 0;
    view.selected_bmson_source_lane_id.value = 0;

    view.staged_bmson_marker_playable_lanes.clear();
    view.staged_bmson_marker_restarts.clear();
    view.bmson_marker_drafts.clear();
    view.bmson_marker_move_preview.reset();
    view.waveform_rows.clear();
    view.channel_waveform_rows.clear();
    view.channel_waveform_source_lane_id = {};
    view.waveform_overview_rows.clear();
    view.waveform_overview_visible_begin_fraction = 0.0;
    view.waveform_overview_visible_end_fraction = 0.0;
    view.waveform_beat_zero_visible = false;
    view.waveform_beat_zero_fraction = 0.0;
    view.waveform_overview_total_frames = 0;
    view.waveform_overview_sample_rate = 0;
    view.bgm_bar_rows.clear();
    view.bgm_bar_begin_fraction = 0.0;
    view.bgm_bar_end_fraction = 1.0;
    view.bgm_bar_marks.clear();
    view.audio_offset_editable = false;
    view.audio_offset_locked = application_state.project.timing_recipe.audio_offset_locked;
    view.waveform_audio_offset_numerator = 0;
    view.waveform_audio_offset_denominator = 1;
    view.waveform_offset_drag_active = false;
    view.beat_fit_drag_active = false;
    view.offset_fit_drag_active = false;
    view.offset_fit_target_y_pixels = -1.0;
    view.proposed_grid_lines.clear();
    view.timing_preview_rects.clear();
    view.timing_preview_markers.clear();
    view.dead_spans.clear();
    view.kiai_spans.clear();
    view.recipe_areas.clear();
    view.recipe_entries.clear();
    view.speed_spans.clear();
    view.recipe_graph_points.clear();
    view.sv_graph_points.clear();
    view.sv_graph_one_fraction = -1.0;
    view.speed_graph_points.clear();
    view.speed_graph_one_fraction = -1.0;
    view.waveform_proposed_beat_zero_visible = false;
    view.waveform_proposed_beat_zero_fraction = 0.0;
    view.waveform_proposed_audio_offset_numerator = 0;
    view.waveform_proposed_audio_offset_denominator = 1;
    view.waveform_timing_locked = application_state.project.timing_recipe.timing_locked;
    view.waveform_analysis_pending = false;
    view.beat_marker_navigation_available = false;
    view.audio_marker_navigation_available = false;
    view.audio_marker_transport_available = false;




    view.beat_section_selection_span_available = false;
    view.beat_section_selection_begin_measure_number = 0;
    view.beat_section_selection_begin_row = 0;
    view.beat_section_selection_end_measure_number = 0;
    view.beat_section_selection_end_row = 0;






    view.semantic_layer_total_count = 0;
    view.semantic_layer_first_row = 0;
    view.note_menu_layers.clear();

    view.note_menu_layer_first_row = 0;
    view.note_menu_layer_total_count = 0;
    view.semantic_layer_target = {};
    view.semantic_layer_target_none = false;
    view.semantic_layer_unassigned_note_count = 0;
    view.semantic_layer_unassigned_bgm_count = 0;
    view.semantic_layer_page = 0;
    view.semantic_layer_page_count = 0;
    view.semantic_layer_target_locked = false;
    view.semantic_layer_target_visible = false;
    view.semantic_layer_target_required_filter = false;
    view.semantic_layer_target_excluded_filter = false;
    view.semantic_layer_solo_active = application_state.project.semantic_layer.semantic_layer_solo_active;
    view.status_text = {};
    view.selected_note_label.clear();
    view.selected_note_label_note_id.value = 0;
    view.focus_note_label_editor = false;
    view.hover_note_visible = false;
    view.hover_note_id.value = 0;
    view.hover_tooltip.clear();
    view.timing_markers.clear();
    view.viewport_autoscroll_active = cache.frame.viewport_autoscroll_active;
    view.viewport_autoscroll_anchor_x_pixels = cache.frame.viewport_autoscroll_anchor_x_pixels;
    view.viewport_autoscroll_anchor_y_pixels = cache.frame.viewport_autoscroll_anchor_y_pixels;
    view.viewport_autoscroll_pointer_x_pixels = cache.frame.viewport_autoscroll_pointer_x_pixels;
    view.viewport_autoscroll_pointer_y_pixels = cache.frame.viewport_autoscroll_pointer_y_pixels;
    view.hover_tooltip.clear();
    view.selected_note_label.clear();
    return true;
}
} // namespace wormedit::sys::compose_projection_detail

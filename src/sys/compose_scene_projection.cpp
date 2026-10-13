#include "sys/compose_projection_internal.h"
namespace wormedit::sys::compose_projection_detail {
void populate_compose_sound_labels(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.frame.flattened;
    const auto& history = projection.history();
    const auto& bgm_events = projection.scene.bgm_events;
    std::unordered_set<std::uint64_t> visible_sound_ids;
    visible_sound_ids.reserve(projection.scene.note_endpoints.size() + bgm_events.size());
    const auto note_count = std::min(projection.scene.note_endpoints.size(),
                                     static_cast<std::size_t>(WORM_UI_COMPOSE_NOTE_CAPACITY));
    for (std::size_t index = 0; index < note_count; ++index) {
        if (const auto sound = projection.scene.note_endpoints[index].sound)
            visible_sound_ids.insert(sound->value);
    }
    const auto bgm_event_count =
        std::min(bgm_events.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_BGM_EVENT_CAPACITY));
    for (std::size_t index = 0; index < bgm_event_count; ++index) {
        if (bgm_events[index].sound)
            visible_sound_ids.insert(bgm_events[index].sound->value);
    }
    flattened.compose.sound_labels.reserve(visible_sound_ids.size());
    for (const auto& resource : history.document.resources) {
        if (!visible_sound_ids.contains(resource.id.value))
            continue;
        flattened.compose.sound_labels.emplace(resource.id.value,
                                               compose_resource_sound_label(resource));
    }
}

auto prepare_compose_grid_rows(flattened_workspace_t& flattened, std::size_t count) -> bool {
    auto& grid = flattened.compose.grid;
    grid.line = {};
    grid.row = {};
    if (count == 0)
        return true;
    auto* arena = flattened.arena();
    if (!arena)
        return false;
    auto lines = arena_array<WormUiComposeGridLineView>(*arena, count);
    auto rows = arena_array<wormedit::exact_chart_row_t>(*arena, count);
    if (lines.capacity() != count || rows.capacity() != count)
        return false;
    grid.line = lines;
    grid.row = rows;
    return true;
}

auto append_compose_grid_row(flattened_workspace_t& flattened,
                             const WormUiComposeGridLineView& line,
                             const wormedit::exact_chart_row_t& row) -> bool {
    auto& grid = flattened.compose.grid;
    if (grid.line.size() == grid.line.capacity() || grid.row.size() == grid.row.capacity())
        return false;
    return grid.line.push_back(line) && grid.row.push_back(row);
}

auto compose_measure_local_row(const wormedit::measure_index_t& measures, std::uint64_t row,
                               std::uint64_t divisions) -> std::uint64_t {
    if (row >= measures.total_rows)
        return (row - measures.total_rows) % divisions;
    const auto after = std::ranges::upper_bound(
        measures.entries, row, {}, &wormedit::measure_index_entry_t::absolute_begin_row);
    return after == measures.entries.begin() ? row : row - std::prev(after)->absolute_begin_row;
}

void project_compose_grid_lines(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.frame.flattened;
    const auto& chart = projection.scope.chart_scope.chart;
    const auto& scene = projection.scene;
    auto& view = projection.frame.view();
    const auto& lines = scene.grid_lines;
    if (lines.empty())
        return;
    const auto line_count =
        std::min(lines.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_GRID_CAPACITY));
    std::unordered_map<std::uint64_t, std::uint32_t> measure_numbers;
    measure_numbers.reserve(projection.index.measures.entries.size());
    for (std::size_t measure = 0; measure < projection.index.measures.entries.size(); ++measure) {
        measure_numbers.emplace(projection.index.measures.entries[measure].absolute_begin_row,
                                static_cast<std::uint32_t>(measure + 1));
    }
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
        const auto measure = measure_numbers.find(line.chart_row.whole_rows);
        std::uint32_t measure_number = measure != measure_numbers.end() ? measure->second : 0;
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
                 .kind = static_cast<std::uint8_t>(line.kind),
                 .emphasis = static_cast<std::uint8_t>(line.emphasis),
                 .measure_number =
                     line.kind == wormedit::editor_grid_line_kind_t::measure ? measure_number : 0,
                 .chart_row =
                     line.chart_row.fractional_rows.numerator == 0 ? line.chart_row.whole_rows : 0,
                 .snap_class = wormedit::ui::note_snap_class(
                     compose_measure_local_row(projection.index.measures, line.chart_row.whole_rows,
                                               chart.divisions_per_standard_measure),
                     line.chart_row.fractional_rows.numerator,
                     line.chart_row.fractional_rows.denominator,
                     chart.divisions_per_standard_measure)},
                line.chart_row)) {
            view.truncated = true;
            break;
        }
    }
    view.truncated = view.truncated || line_count != lines.size();
}

void project_compose_scene_objects(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.frame.flattened;
    const auto& scene = projection.scene;
    auto& view = projection.frame.view();
    const auto& differences = scene.comparison_differences;
    const auto difference_count =
        std::min(differences.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_DIFFERENCE_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.difference, difference_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < difference_count; ++index) {
        append_compose_geometry_row(flattened.compose.difference,
                                    {.bounds = to_frontend_rect(differences[index].bounds)},
                                    view.truncated);
    }
    const auto& holds = scene.hold_bodies;
    const auto hold_count =
        std::min(holds.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_HOLD_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.hold, hold_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < hold_count; ++index) {
        const auto& hold = holds[index];
        append_compose_geometry_row(
            flattened.compose.hold,
            {.bounds = to_frontend_rect(hold.bounds),
             .note_id = hold.note.value,
             .lane_id = hold.lane.value,
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
             .move_conflict = static_cast<std::uint8_t>(hold.move_conflict),
             .move_conflict_occupant = hold.move_conflict_occupant},
            view.truncated);
    }
    const auto& endpoints = scene.note_endpoints;
    const auto endpoint_count =
        std::min(endpoints.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_NOTE_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.note, endpoint_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < endpoint_count; ++index) {
        const auto& endpoint = endpoints[index];
        const auto sound = compose_note_sound_text(endpoint.sound, flattened.compose.sound_labels);
        append_compose_geometry_row(
            flattened.compose.note,
            {.bounds = to_frontend_rect(endpoint.bounds),
             .note_id = endpoint.note.value,
             .lane_id = endpoint.lane.value,
             .display_lane_number = compose_note_display_lane_number(scene, endpoint),
             .key_color = compose_note_key_color(scene, endpoint),
             .glyph = static_cast<std::uint8_t>(endpoint.glyph),
             .sound_label = sound.label,
             .sound_detail = sound.detail,
             .sound_resource_id = endpoint.sound ? endpoint.sound->value : 0,
             .snap_class = compose_endpoint_snap_class(projection, endpoint),
             .kind = static_cast<std::uint8_t>(endpoint.kind),
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
             .move_conflict = static_cast<std::uint8_t>(endpoint.move_conflict),
             .move_conflict_occupant = endpoint.move_conflict_occupant},
            view.truncated);
    }
    view.truncated = view.truncated || difference_count != differences.size() ||
                     hold_count != holds.size() || endpoint_count != endpoints.size();
}

void project_compose_layout_and_notes(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.frame.flattened;
    const auto& scene = projection.scene;
    auto& view = projection.frame.view();
    const auto field_left_pixels = projection.field_left_pixels;
    const auto playable_field_width = projection.playable_field_width;
    const auto& bands = scene.layout_bands;
    const auto band_count =
        std::min(bands.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_BAND_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.band, band_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < band_count; ++index) {
        const auto& band = bands[index];
        append_compose_geometry_row(
            flattened.compose.band,
            {.bounds = {field_left_pixels, band.top_pixels,
                        field_left_pixels + playable_field_width, band.bottom_pixels},
             .split_stage = band.split_stage},
            view.truncated);
    }
    const auto& lanes = scene.lanes;
    const auto lane_count =
        std::min(lanes.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_LANE_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.lane, lane_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < lane_count; ++index) {
        const auto& lane = lanes[index];
        append_compose_geometry_row(flattened.compose.lane,
                                    {.bounds = to_frontend_rect(lane.bounds),
                                     .lane_id = lane.lane.value,
                                     .display_order = lane.display_order,
                                     .role = static_cast<std::uint8_t>(lane.role),
                                     .stage = static_cast<std::uint8_t>(lane.stage),
                                     .glyph = static_cast<std::uint8_t>(lane.glyph),
                                     .key_color = static_cast<std::uint8_t>(lane.key_color)},
                                    view.truncated);
    }
    project_compose_grid_lines(projection);
    project_compose_scene_objects(projection);
    view.truncated = view.truncated || projection.lane_key_bindings_truncated ||
                     band_count != bands.size() || lane_count != lanes.size();
}

void project_compose_bgm_events(
    flattened_workspace_t& flattened,
    const std::vector<wormedit::editor_field_bgm_event_geometry_t>& events,
    WormUiComposeView& view) {
    const auto event_count =
        std::min(events.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_BGM_EVENT_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.bgm.event, event_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < event_count; ++index) {
        const auto& event = events[index];
        const auto sound = event.sound ? flattened.compose.sound_labels.find(event.sound->value)
                                       : flattened.compose.sound_labels.end();
        append_compose_geometry_row(
            flattened.compose.bgm.event,
            {.bounds = to_frontend_rect(event.bounds),
             .event_id = event.event.value,
             .source_lane_id = event.source_lane.value,
             .sound_id = event.sound ? event.sound->value : 0,
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
             .head_top_pixels = static_cast<float>(to_frontend_rect(event.head_bounds).top),
             .head_bottom_pixels = static_cast<float>(to_frontend_rect(event.head_bounds).bottom)},
            view.truncated);
    }
    view.truncated = view.truncated || event_count != events.size();
}

auto compose_stem_filename(const compose_scene_projection_context_t& projection,
                           const wormedit::bgm_source_lane_t& lane) -> const char* {
    if (!lane.stem_channel)
        return nullptr;
    const auto channel = std::ranges::find(projection.scope.chart_scope.chart.sound_channels,
                                           *lane.stem_channel, &wormedit::sound_channel_t::id);
    if (channel == projection.scope.chart_scope.chart.sound_channels.end())
        return nullptr;
    const auto& assets = projection.history().document.audio_assets;
    const auto asset =
        std::ranges::find(assets, channel->source_clip.source, &wormedit::audio_asset_t::id);
    if (asset == assets.end() || !projection.frame.flattened.arena())
        return nullptr;
    const auto name = asset->source_path.filename().generic_u8string();
    return arena_string(*projection.frame.flattened.arena(),
                        {reinterpret_cast<const char*>(name.data()), name.size()});
}

void project_compose_bgm_lane_modes(const compose_scene_projection_context_t& projection,
                                    const compose_scene_cache_t& cache) {
    using namespace wormedit;
    const auto& chart = projection.scope.chart_scope.chart;
    if (chart.source_family != format_family_t::bmson)
        return;
    for (auto& lane : projection.frame.flattened.compose.bgm.lane) {
        const auto source = std::ranges::find(
            chart.bgm_source_lanes, object_id_t{lane.source_lane_id}, &bgm_source_lane_t::id);
        if (source == chart.bgm_source_lanes.end() || !source->stem_channel)
            continue;
        lane.bmson_channel_id = source->stem_channel->value;
        const auto continues =
            cache.stems.draw_stem_sources.channel_continues.find(lane.bmson_channel_id);
        lane.bmson_channel_continues =
            continues != cache.stems.draw_stem_sources.channel_continues.end() && continues->second;
    }
}

void project_compose_bgm(const compose_scene_projection_context_t& projection,
                         const compose_scene_cache_t& cache) {
    auto& flattened = projection.frame.flattened;
    const auto& scene = projection.scene;
    auto& view = projection.frame.view();
    const auto& bgm_lanes = scene.bgm_lanes;
    const auto bgm_lane_count =
        std::min(bgm_lanes.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_BGM_LANE_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.bgm.lane, bgm_lane_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < bgm_lane_count; ++index) {
        const auto& lane = bgm_lanes[index];
        const auto source = std::ranges::find(projection.scope.chart_scope.chart.bgm_source_lanes,
                                              lane.source_lane, &wormedit::bgm_source_lane_t::id);
        append_compose_geometry_row(
            flattened.compose.bgm.lane,
            {.bounds = to_frontend_rect(lane.bounds),
             .source_lane_id = lane.source_lane.value,
             .source_order = lane.source_order,
             .stem = source != projection.scope.chart_scope.chart.bgm_source_lanes.end() &&
                     source->stem_channel.has_value(),
             .width_pixels = lane.unclipped_right_pixels - lane.unclipped_left_pixels,
             .resize_edge_pixels = lane.unclipped_right_pixels,
             .filename = source == projection.scope.chart_scope.chart.bgm_source_lanes.end()
                             ? nullptr
                             : compose_stem_filename(projection, *source)},
            view.truncated);
    }
    project_compose_bgm_events(flattened, scene.bgm_events, view);
    project_compose_bgm_lane_modes(projection, cache);
    view.truncated = view.truncated || bgm_lane_count != bgm_lanes.size();
}

auto bga_preview_authored_row(const compose_scene_projection_context_t& projection, double source_y)
    -> std::optional<std::uint64_t> {
    const auto position = wormedit::editor_position_at_y_pixels(
        projection.scope.chart_scope.chart, projection.scope.interaction_view, source_y);
    if (!position)
        return std::nullopt;
    const auto target =
        projection.bga_move_snap_denominator == 0
            ? position
            : wormedit::snap_position(projection.scope.chart_scope.chart, projection.index.measures,
                                      *position, projection.bga_move_snap_denominator);
    return target ? wormedit::absolute_row(projection.index.measures, *target) : std::nullopt;
}

auto bga_preview_virtual_row(const compose_scene_projection_context_t& projection, double source_y)
    -> std::optional<std::uint64_t> {
    using namespace wormedit;
    if (projection.scope.chart_scope.chart.source_family != format_family_t::bms ||
        !chart_has_virtual_tail(projection.scope.chart_scope.chart))
        return std::nullopt;
    const auto& interaction = projection.scope.interaction_view;
    const auto raw = (static_cast<long double>(interaction.viewport_scroll_pixels) + source_y) *
                     projection.scope.chart_scope.chart.divisions_per_standard_measure /
                     interaction.pixels_per_standard_measure;
    if (!std::isfinite(raw) || raw < 0.0L ||
        raw >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
        return std::nullopt;
    const auto rounded = static_cast<std::uint64_t>(std::floor(raw + 0.5L));
    const auto built = build_virtual_canvas(projection.scope.chart_scope.chart);
    const auto* canvas = std::get_if<virtual_canvas_t>(&built);
    const auto source_measure = canvas ? canvas->measure_containing(rounded) : std::nullopt;
    if (!source_measure || source_measure->authored)
        return std::nullopt;
    const auto target_row = projection.bga_move_snap_denominator == 0
                                ? std::optional{rounded}
                                : snap_absolute_row(projection.scope.chart_scope.chart, rounded,
                                                    projection.bga_move_snap_denominator);
    const auto target_measure =
        canvas && target_row ? canvas->measure_containing(*target_row) : std::nullopt;
    return target_measure && !target_measure->authored ? target_row : std::nullopt;
}

auto bga_preview_target_row(const compose_scene_projection_context_t& projection,
                            const compose_bga_move_preview_input_t& input)
    -> std::optional<std::uint64_t> {
    const auto height = projection.scope.interaction_view.viewport_height_pixels;
    const auto y = std::clamp(input.y_pixels, 0.0, std::max(0.0, height - 0.001));
    const auto source_y = projection.scope.downscroll ? height - y : y;
    if (const auto authored = bga_preview_authored_row(projection, source_y))
        return authored;
    return bga_preview_virtual_row(projection, source_y);
}

void project_compose_bga_move_preview(const compose_scene_projection_context_t& projection) {
    auto& view = projection.frame.view();
    view.bga_move_preview_valid = false;
    const auto* input = projection.bga_move_preview;
    if (!input || input->event_id == 0 || input->project_id != view.project_id ||
        input->chart_id != projection.scope.chart_scope.chart.id.value ||
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
    view.bga_move_preview_event_id = input->event_id;
    view.bga_move_preview_y_pixels =
        compose_project_y(*row, projection.scope.pixels_per_row,
                          projection.scope.interaction_view.viewport_scroll_pixels,
                          view.viewport_height_pixels, projection.scope.downscroll);
}

void project_compose_bga(const compose_scene_projection_context_t& projection) {
    static_assert(wormedit::bga_lane_overlap_stack_threshold ==
                  WORM_UI_COMPOSE_BGA_OVERLAP_STACK_THRESHOLD);
    auto& flattened = projection.frame.flattened;
    auto& view = projection.frame.view();
    const auto& scene = projection.scene;
    const auto count = std::min(scene.bga_events.size(),
                                static_cast<std::size_t>(WORM_UI_COMPOSE_BGA_EVENT_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.bga_event, count)) {
        view.truncated = true;
        return;
    }
    view.bga_lane_visible = scene.bga_lane.has_value();
    view.bga_revealed_lane_mask = projection.scope.interaction_view.bga_revealed_lane_mask;
    view.bga_lane_count = std::min<std::size_t>(scene.bga_lanes.size(), 4);
    for (std::size_t index = 0; index < view.bga_lane_count; ++index) {
        const auto& lane = scene.bga_lanes[index];
        view.bga_lane_rects[index] = {lane.bounds.left, lane.bounds.top, lane.bounds.right,
                                      lane.bounds.bottom};
        view.bga_lane_kinds[index] = static_cast<std::uint8_t>(lane.kind);
    }
    view.bga_lane_bounds =
        scene.bga_lane
            ? WormUiComposeRect{scene.bga_lane->bounds.left, scene.bga_lane->bounds.top,
                                scene.bga_lane->bounds.right, scene.bga_lane->bounds.bottom}
            : WormUiComposeRect{};
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
            flattened.compose.bga_event,
            {.event_id = event.event.value,
             .resource_id = event.resource.value,
             .canvas_row = event.row,
             .overlap_count = event.overlap_count,
             .kind = static_cast<std::uint8_t>(event.kind),
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
    project_compose_bga_move_preview(projection);
}

void project_compose_marker_draft(const compose_scene_projection_context_t& projection) {
    const auto& draft = projection.frame.application_state.project.compose_gesture.bmson_marker_draft;
    if (!draft || projection.scope.pixels_per_row <= 0.0)
        return;
    const auto* reference =
        find_compose_bgm_event(projection.scope.chart_scope.chart, draft->reference);
    if (!reference)
        return;
    const auto lane = std::ranges::find_if(projection.scene.bgm_lanes, [&](const auto& candidate) {
        return candidate.source_lane == reference->source_lane;
    });
    const auto* bounds = lane == projection.scene.bgm_lanes.end() ? nullptr : &lane->bounds;
    for (const auto& marker : draft->markers)
        project_compose_draft_marker(projection, marker, bounds);
}

void project_compose_marker_previews(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.frame.flattened;
    const auto& scene = projection.scene;
    project_compose_marker_draft(projection);
    if (scene.cursor) {
        flattened.compose.cursor = WormUiComposeCursorView{
            .bounds = to_frontend_rect(scene.cursor->bounds),
            .lane_id = scene.cursor->lane.value,
        };
    }
}

void publish_compose_scene(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.frame.flattened;
    auto& view = projection.frame.view();
    view.bands = flattened.compose.band.data();
    view.band_count = flattened.compose.band.size();
    view.lanes = flattened.compose.lane.data();
    view.lane_count = flattened.compose.lane.size();
    view.grid_lines = flattened.compose.grid.line.data();
    view.grid_line_count = flattened.compose.grid.line.size();
    view.differences = flattened.compose.difference.data();
    view.difference_count = flattened.compose.difference.size();
    view.hold_bodies = flattened.compose.hold.data();
    view.hold_body_count = flattened.compose.hold.size();
    view.note_endpoints = flattened.compose.note.data();
    view.note_endpoint_count = flattened.compose.note.size();
    view.bgm_lanes = flattened.compose.bgm.lane.data();
    view.bgm_lane_count = flattened.compose.bgm.lane.size();
    view.bgm_events = flattened.compose.bgm.event.data();
    view.bgm_event_count = flattened.compose.bgm.event.size();
    view.bga_events = flattened.compose.bga_event.data();
    view.bga_event_count = flattened.compose.bga_event.size();
    view.bmson_marker_drafts = flattened.compose.bgm.draft.data();
    view.bmson_marker_draft_count = flattened.compose.bgm.draft.size();
    view.bmson_marker_move_preview =
        flattened.compose.bgm.move ? &*flattened.compose.bgm.move : nullptr;
    view.cursor = flattened.compose.cursor ? &*flattened.compose.cursor : nullptr;
}

bool initialize_compose_view_state(
    flattened_workspace_t& flattened, compose_scene_cache_t& cache, int window_width,
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
    view.draw_source_kind = application_state.project.compose_gesture.draw_source_kind;
    view.draw_source_id = application_state.project.compose_gesture.draw_source_id;
    view.draw_source_label = "silent note";
    view.draw_source_missing = false;
    view.semantic_layer_target_row = 0;
    view.note_paint_mode_active = notes_editable && application_state.project.compose_gesture.note_paint_mode_active;
    view.semantic_layer_paint_mode_active =
        notes_editable && application_state.project.compose_gesture.semantic_layer_paint_mode_active;
    view.semantic_layer_gesture_active =
        retained_state && editor_interaction_has_pending_gesture(*retained_state);
    view.timing_stage_active = false;
    view.timing_stage_valid = false;
    view.timing_stage_lane = WORM_UI_TIMING_LANE_BPM;
    view.timing_stage_y_pixels = 0.0;
    view.timing_lane_visibility_mask = effective_compose_timing_lane_mask(application_state);
    view.timing_focus = application_state.project.compose_view.compose_timing_focus;
    view.selected_note_count = 0;
    view.keyboard_context_note_id = 0;
    view.move_conflict_count = 0;
    view.selected_bgm_event_count = 0;
    view.selected_assigned_resource_id = 0;
    view.selected_bgm_event_id = 0;
    view.selected_bmson_bgm_event_id = 0;
    view.selected_bmson_note_event_id = 0;
    view.selected_bmson_source_lane_id = 0;
    view.staged_bmson_marker_count = 0;
    view.staged_bmson_marker_playable_lanes = nullptr;
    view.staged_bmson_marker_restarts = nullptr;
    view.bmson_marker_drafts = nullptr;
    view.bmson_marker_draft_count = 0;
    view.bmson_marker_move_preview = nullptr;
    view.waveform_rows = nullptr;
    view.waveform_row_count = 0;
    view.channel_waveform_rows = nullptr;
    view.channel_waveform_row_count = 0;
    view.channel_waveform_source_lane_id = 0;
    view.waveform_overview_rows = nullptr;
    view.waveform_overview_row_count = 0;
    view.waveform_overview_visible_begin_fraction = 0.0;
    view.waveform_overview_visible_end_fraction = 0.0;
    view.waveform_beat_zero_visible = false;
    view.waveform_beat_zero_fraction = 0.0;
    view.waveform_overview_total_frames = 0;
    view.waveform_overview_sample_rate = 0;
    view.bgm_bar_rows = nullptr;
    view.bgm_bar_row_count = 0;
    view.bgm_bar_begin_fraction = 0.0;
    view.bgm_bar_end_fraction = 1.0;
    view.bgm_bar_marks = nullptr;
    view.bgm_bar_mark_count = 0;
    view.audio_offset_editable = false;
    view.audio_offset_locked = application_state.project.timing_recipe.audio_offset_locked;
    view.waveform_audio_offset_numerator = 0;
    view.waveform_audio_offset_denominator = 1;
    view.waveform_offset_drag_active = false;
    view.beat_fit_drag_active = false;
    view.offset_fit_drag_active = false;
    view.offset_fit_target_y_pixels = -1.0;
    view.proposed_grid_lines = nullptr;
    view.proposed_grid_line_count = 0;
    view.timing_preview_rects = nullptr;
    view.timing_preview_rect_count = 0;
    view.timing_preview_markers = nullptr;
    view.timing_preview_marker_count = 0;
    view.dead_spans = nullptr;
    view.dead_span_count = 0;
    view.kiai_spans = nullptr;
    view.kiai_span_count = 0;
    view.recipe_areas = nullptr;
    view.recipe_area_count = 0;
    view.speed_spans = nullptr;
    view.speed_span_count = 0;
    view.recipe_graph_points = nullptr;
    view.recipe_graph_point_count = 0;
    view.sv_graph_points = nullptr;
    view.sv_graph_point_count = 0;
    view.sv_graph_one_fraction = -1.0;
    view.speed_graph_points = nullptr;
    view.speed_graph_point_count = 0;
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
    view.beat_bookmarks = flattened.compose.bookmark.data();
    view.beat_bookmark_count = 0;
    view.beat_bookmarks_truncated = false;
    view.beat_sections = flattened.compose.section.data();
    view.beat_section_count = 0;
    view.beat_sections_truncated = false;
    view.beat_section_selection_span_available = false;
    view.beat_section_selection_begin_measure_number = 0;
    view.beat_section_selection_begin_row = 0;
    view.beat_section_selection_end_measure_number = 0;
    view.beat_section_selection_end_row = 0;
    view.audio_bookmarks = flattened.compose.audio_marker.bookmark.data();
    view.audio_bookmark_count = 0;
    view.audio_bookmarks_truncated = false;
    view.audio_sections = flattened.compose.audio_marker.section.data();
    view.audio_section_count = 0;
    view.audio_sections_truncated = false;
    view.semantic_layers = flattened.compose.semantic_layer.view.data();
    view.semantic_layer_count = 0;
    view.semantic_layer_total_count = 0;
    view.semantic_layer_first_row = 0;
    view.note_menu_layers = nullptr;
    view.note_menu_layer_count = 0;
    view.note_menu_layer_first_row = 0;
    view.note_menu_layer_total_count = 0;
    view.semantic_layer_target = {};
    view.semantic_layer_target_none = false;
    view.semantic_layer_unassigned_note_count = 0;
    view.semantic_layer_unassigned_bgm_count = 0;
    view.semantic_layer_page = 0;
    view.semantic_layer_page_count = 0;
    view.semantic_layers_truncated = false;
    view.semantic_layer_target_locked = false;
    view.semantic_layer_target_visible = false;
    view.semantic_layer_target_required_filter = false;
    view.semantic_layer_target_excluded_filter = false;
    view.semantic_layer_solo_active = application_state.project.semantic_layer.semantic_layer_solo_active;
    view.status_text = nullptr;
    view.selected_note_label = nullptr;
    view.selected_note_label_note_id = 0;
    view.focus_note_label_editor = false;
    view.hover_note_visible = false;
    view.hover_note_id = 0;
    view.hover_tooltip = nullptr;
    view.timing_markers = nullptr;
    view.timing_marker_count = 0;
    view.viewport_autoscroll_active = cache.frame.viewport_autoscroll_active;
    view.viewport_autoscroll_anchor_x_pixels = cache.frame.viewport_autoscroll_anchor_x_pixels;
    view.viewport_autoscroll_anchor_y_pixels = cache.frame.viewport_autoscroll_anchor_y_pixels;
    view.viewport_autoscroll_pointer_x_pixels = cache.frame.viewport_autoscroll_pointer_x_pixels;
    view.viewport_autoscroll_pointer_y_pixels = cache.frame.viewport_autoscroll_pointer_y_pixels;
    flattened.compose.hover_tooltip.clear();
    flattened.compose.selected_note_label.clear();
    return true;
}
} // namespace wormedit::sys::compose_projection_detail

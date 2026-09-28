#include "sys/compose_view.h"

#include <wormedit/core/editor_scene.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/virtual_canvas.h>

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace wormedit::sys {

auto compose_surface_height(int window_height, const WormUiWorkspaceView& view) -> double {
    float recovery_height = 0.0f;
    if (view.recovery_panel_visible) {
        recovery_height = 40.0f + (view.recovery_notice && view.recovery_notice[0] ? 22.0f : 0.0f) +
                          static_cast<float>(view.recovery_candidate_count) * 48.0f +
                          static_cast<float>(view.recovery_diagnostic_count) * 18.0f;
        for (std::size_t row = 0; row < view.recovery_candidate_count; ++row) {
            const auto& candidate = view.recovery_candidates[row];
            if ((candidate.action_mask & WORM_UI_RECOVERY_ACTION_SAVE_AS) != 0) {
                recovery_height += 25.0f;
                if (candidate.path_label == nullptr || candidate.path_label[0] == '\0')
                    recovery_height += 18.0f;
            }
            if (candidate.disabled_reason && candidate.disabled_reason[0])
                recovery_height += 20.0f;
        }
    }
    const auto content_height =
        window_height > 300 ? static_cast<float>(window_height) - 165.0f - recovery_height : 80.0f;
    const auto& compose = view.compose;
    const auto rows_shown = std::min<std::size_t>(compose.branch_row_count, 4);
    const auto diagnostics_shown = std::min<std::size_t>(compose.branch_diagnostic_count, 2);
    const bool branch_status_shown =
        compose.branch_rows_truncated || compose.branch_display_text_truncated ||
        compose.branch_diagnostics_truncated || compose.branch_row_count > rows_shown ||
        compose.branch_diagnostic_count > diagnostics_shown;
    const bool branch_visible = compose.branch_outline_available || compose.branch_row_count != 0 ||
                                compose.branch_diagnostic_count != 0 || branch_status_shown;
    const auto branch_height = branch_visible
                                   ? 23.0f + static_cast<float>(rows_shown + diagnostics_shown +
                                                                (branch_status_shown ? 1 : 0)) *
                                                 18.0f
                                   : 0.0f;
    return std::max(64.0, static_cast<double>(content_height - 38.0f - 18.0f - branch_height));
}


auto compose_field_downscroll(const wormedit::chart_document_t& chart) -> bool {
    for (const auto& segment : chart.lane_layouts) {
        for (const auto& lane : segment.layout.lanes) {
            switch (lane.glyph) {
            case wormedit::lane_glyph_t::arrow_left:
            case wormedit::lane_glyph_t::arrow_down:
            case wormedit::lane_glyph_t::arrow_up:
            case wormedit::lane_glyph_t::arrow_right:
                return false;
            case wormedit::lane_glyph_t::bar:
            case wormedit::lane_glyph_t::square:
                break;
            }
        }
    }
    return true;
}

auto compose_project_y(long double row, double pixels_per_row, double scroll,
                       double viewport_height, bool downscroll) -> double {
    const auto offset = row * pixels_per_row - scroll;
    return static_cast<double>(downscroll ? viewport_height - offset : offset);
}

auto compose_source_y(double viewport_y, double viewport_height, bool downscroll) -> double {
    return downscroll ? viewport_height - viewport_y : viewport_y;
}

auto maximum_layout_lane_count(const wormedit::chart_document_t& chart) -> std::size_t {
    std::size_t result{};
    for (const auto& segment : chart.lane_layouts)
        result = std::max(result, segment.layout.lanes.size());
    return result;
}

auto compose_horizontal_layout(double viewport_width_pixels, std::size_t lane_count,
                               bool has_bgm_lanes, std::size_t visible_timing_lane_count)
    -> compose_horizontal_layout_t {
    const auto desired_left = compose_measure_rail_width_pixels +
                              compose_timing_lane_width_pixels * visible_timing_lane_count;
    const auto field_left =
        std::clamp(viewport_width_pixels - compose_minimum_playable_field_width_pixels,
                   compose_measure_rail_width_pixels, desired_left);
    const auto available = std::max(1.0, viewport_width_pixels - field_left);
    auto playable = std::clamp(static_cast<double>(std::max<std::size_t>(lane_count, 1)) *
                                   compose_playable_lane_width_pixels,
                               compose_minimum_playable_field_width_pixels,
                               compose_maximum_playable_field_width_pixels);
    playable = std::min(playable, available);
    if (!has_bgm_lanes)
        return {.field_left_pixels = field_left, .playable_field_width_pixels = playable};
    if (available > compose_minimum_bgm_pane_width_pixels)
        playable = std::min(playable, available - compose_minimum_bgm_pane_width_pixels);
    return {.field_left_pixels = field_left,
            .playable_field_width_pixels = std::max(1.0, playable),
            .bgm_pane_width_pixels = std::max(0.0, available - playable)};
}

auto has_beat_marker_navigation(const wormedit::chart_document_t& chart) -> bool {
    return std::ranges::any_of(chart.bookmarks,
                               [](const auto& bookmark) {
                                   return bookmark.anchor == wormedit::marker_anchor_t::beat;
                               }) ||
           std::ranges::any_of(chart.sections, [](const auto& section) {
               return section.anchor == wormedit::marker_anchor_t::beat;
           });
}

auto has_audio_marker_navigation(const wormedit::chart_document_t& chart) -> bool {
    return std::ranges::any_of(chart.bookmarks,
                               [](const auto& bookmark) {
                                   return bookmark.anchor == wormedit::marker_anchor_t::audio_time;
                               }) ||
           std::ranges::any_of(chart.sections, [](const auto& section) {
               return section.anchor == wormedit::marker_anchor_t::audio_time;
           });
}

auto timing_lane_kind_to_ui(wormedit::timing_lane_kind_t lane) -> WormUiTimingLaneKind {
    using kind_t = wormedit::timing_lane_kind_t;
    switch (lane) {
    case kind_t::bpm:
        return WORM_UI_TIMING_LANE_BPM;
    case kind_t::scroll:
        return WORM_UI_TIMING_LANE_SCROLL;
    case kind_t::speed:
        return WORM_UI_TIMING_LANE_SPEED;
    case kind_t::stop:
        return WORM_UI_TIMING_LANE_STOP;
    case kind_t::delay:
        return WORM_UI_TIMING_LANE_DELAY;
    case kind_t::warp:
        return WORM_UI_TIMING_LANE_WARP;
    case kind_t::kiai:
        return WORM_UI_TIMING_LANE_KIAI;
    case kind_t::anchor:
        return WORM_UI_TIMING_LANE_ANCHOR;
    }
    return WORM_UI_TIMING_LANE_BPM;
}

auto compose_lane_is_editable(const wormedit::lane_definition_t& lane) -> bool {
    using wormedit::lane_role_t;
    return lane.role != lane_role_t::autoplay && lane.role != lane_role_t::invisible &&
           lane.role != lane_role_t::mine;
}

auto compose_physical_scancode(wormedit::physical_key_t key) -> std::uint32_t {
    using wormedit::physical_key_t;
    switch (key) {
    case physical_key_t::digit_1:
        return SDL_SCANCODE_1;
    case physical_key_t::digit_2:
        return SDL_SCANCODE_2;
    case physical_key_t::digit_3:
        return SDL_SCANCODE_3;
    case physical_key_t::digit_4:
        return SDL_SCANCODE_4;
    case physical_key_t::digit_5:
        return SDL_SCANCODE_5;
    case physical_key_t::digit_6:
        return SDL_SCANCODE_6;
    case physical_key_t::digit_7:
        return SDL_SCANCODE_7;
    case physical_key_t::digit_8:
        return SDL_SCANCODE_8;
    case physical_key_t::digit_9:
        return SDL_SCANCODE_9;
    case physical_key_t::digit_0:
        return SDL_SCANCODE_0;
    case physical_key_t::key_q:
        return SDL_SCANCODE_Q;
    case physical_key_t::key_w:
        return SDL_SCANCODE_W;
    case physical_key_t::key_e:
        return SDL_SCANCODE_E;
    case physical_key_t::key_r:
        return SDL_SCANCODE_R;
    case physical_key_t::key_t:
        return SDL_SCANCODE_T;
    case physical_key_t::key_y:
        return SDL_SCANCODE_Y;
    case physical_key_t::key_u:
        return SDL_SCANCODE_U;
    case physical_key_t::key_i:
        return SDL_SCANCODE_I;
    case physical_key_t::key_o:
        return SDL_SCANCODE_O;
    case physical_key_t::key_p:
        return SDL_SCANCODE_P;
    case physical_key_t::key_a:
        return SDL_SCANCODE_A;
    case physical_key_t::key_s:
        return SDL_SCANCODE_S;
    case physical_key_t::key_d:
        return SDL_SCANCODE_D;
    case physical_key_t::key_f:
        return SDL_SCANCODE_F;
    case physical_key_t::key_g:
        return SDL_SCANCODE_G;
    case physical_key_t::key_h:
        return SDL_SCANCODE_H;
    case physical_key_t::key_j:
        return SDL_SCANCODE_J;
    case physical_key_t::key_k:
        return SDL_SCANCODE_K;
    case physical_key_t::key_l:
        return SDL_SCANCODE_L;
    case physical_key_t::key_z:
        return SDL_SCANCODE_Z;
    case physical_key_t::key_x:
        return SDL_SCANCODE_X;
    case physical_key_t::key_c:
        return SDL_SCANCODE_C;
    case physical_key_t::key_v:
        return SDL_SCANCODE_V;
    case physical_key_t::key_b:
        return SDL_SCANCODE_B;
    case physical_key_t::key_n:
        return SDL_SCANCODE_N;
    case physical_key_t::key_m:
        return SDL_SCANCODE_M;
    case physical_key_t::unknown:
        return 0;
    }
    return 0;
}

auto requested_snap_denominator(const application_command_state_t& application_state,
                                wormedit::chart_id_t chart) -> std::uint64_t {
    const auto found = application_state.requested_snap_denominators.find(chart.value);
    return found == application_state.requested_snap_denominators.end() ? 4 : found->second;
}

auto editor_interaction_has_pending_gesture(const wormedit::editor_interaction_state_t& state)
    -> bool {
    return state.pending_hold.has_value() || state.pending_virtual_lane_key.has_value() ||
           state.pending_note_duration_gesture.has_value() ||
           state.pending_note_move_gesture.has_value() ||
           state.pending_note_paint_gesture.has_value() ||
           state.pending_semantic_layer_paint_gesture.has_value() ||
           state.pending_editor_area_selection.has_value() ||
           state.pending_space_range_selection.has_value();
}


namespace {
struct compose_scene_projection_context_t {
    flattened_workspace_t& flattened;
    const wormedit::chart_document_t& chart;
    const wormedit::editor_field_scene_index_t& index;
    const wormedit::editor_field_scene_t& scene;
    const wormedit::document_history_t& history;
    application_command_state_t& application_state;
    const wormedit::editor_interaction_view_model_t& interaction_view;
    WormUiComposeView& view;
    double field_left_pixels;
    double playable_field_width;
    double pixels_per_row;
    bool downscroll;
    bool lane_key_bindings_truncated;
};

void project_compose_layout_and_notes(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.flattened;
    const auto& chart = projection.chart;
    const auto& scene = projection.scene;
    auto& view = projection.view;
    const auto field_left_pixels = projection.field_left_pixels;
    const auto playable_field_width = projection.playable_field_width;
    const auto convert_rect = [](const wormedit::editor_field_rect_t& rect) {
        return WormUiComposeRect{rect.left, rect.top, rect.right, rect.bottom};
    };
    const auto& bands = scene.layout_bands;
    const auto band_count =
        std::min(bands.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_BAND_CAPACITY));
    flattened.compose_band_storage.reserve(band_count);
    for (std::size_t index = 0; index < band_count; ++index) {
        const auto& band = bands[index];
        flattened.compose_band_storage.push_back(
            {.bounds = {field_left_pixels, band.top_pixels,
                        field_left_pixels + playable_field_width,
                        band.bottom_pixels},
             .split_stage = band.split_stage});
    }
    const auto& lanes = scene.lanes;
    const auto lane_count =
        std::min(lanes.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_LANE_CAPACITY));
    flattened.compose_lane_storage.reserve(lane_count);
    for (std::size_t index = 0; index < lane_count; ++index) {
        const auto& lane = lanes[index];
        flattened.compose_lane_storage.push_back({.bounds = convert_rect(lane.bounds),
                                                  .lane_id = lane.lane.value,
                                                  .display_order = lane.display_order,
                                                  .role = static_cast<std::uint8_t>(lane.role),
                                                  .stage = static_cast<std::uint8_t>(lane.stage),
                                                  .glyph = static_cast<std::uint8_t>(lane.glyph)});
    }
    const auto& lines = scene.grid_lines;
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
    flattened.compose_grid_storage.reserve(line_count);
    flattened.compose_grid_row_storage.reserve(line_count);
    for (std::size_t index = 0; index < line_count; ++index) {
        const auto& line = lines[index];
        flattened.compose_grid_row_storage.push_back(line.chart_row);
        const auto measure = measure_numbers.find(line.chart_row.whole_rows);
        std::uint32_t measure_number = measure != measure_numbers.end() ? measure->second : 0;
        if (line.kind == wormedit::editor_grid_line_kind_t::measure &&
            measure_number == 0 && virtual_canvas) {
            const auto virtual_measure =
                virtual_canvas->measure_containing(line.chart_row.whole_rows);
            if (virtual_measure && virtual_measure->begin_row == line.chart_row.whole_rows) {
                measure_number = static_cast<std::uint32_t>(virtual_measure->number + 1);
            }
        }
        flattened.compose_grid_storage.push_back(
            {.y_pixels = line.y_pixels,
             .kind = static_cast<std::uint8_t>(line.kind),
             .emphasis = static_cast<std::uint8_t>(line.emphasis),
             .measure_number = line.kind == wormedit::editor_grid_line_kind_t::measure
                                   ? measure_number
                                   : 0,
             .chart_row =
                 line.chart_row.fractional_rows.numerator == 0 ? line.chart_row.whole_rows : 0});
    }
    const auto& differences = scene.comparison_differences;
    const auto difference_count =
        std::min(differences.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_DIFFERENCE_CAPACITY));
    flattened.compose_difference_storage.reserve(difference_count);
    for (std::size_t index = 0; index < difference_count; ++index) {
        flattened.compose_difference_storage.push_back(
            {.bounds = convert_rect(differences[index].bounds)});
    }
    const auto& holds = scene.hold_bodies;
    const auto hold_count =
        std::min(holds.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_HOLD_CAPACITY));
    flattened.compose_hold_storage.reserve(hold_count);
    for (std::size_t index = 0; index < hold_count; ++index) {
        const auto& hold = holds[index];
        flattened.compose_hold_storage.push_back(
            {.bounds = convert_rect(hold.bounds),
             .note_id = hold.note.value,
             .lane_id = hold.lane.value,
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
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->alpha : 0)});
    }
    const auto& endpoints = scene.note_endpoints;
    const auto endpoint_count =
        std::min(endpoints.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_NOTE_CAPACITY));
    flattened.compose_note_storage.reserve(endpoint_count);
    for (std::size_t index = 0; index < endpoint_count; ++index) {
        const auto& endpoint = endpoints[index];
        flattened.compose_note_storage.push_back(
            {.bounds = convert_rect(endpoint.bounds),
             .note_id = endpoint.note.value,
             .lane_id = endpoint.lane.value,
             .glyph = static_cast<std::uint8_t>(endpoint.glyph),
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
             .paint_invalid_preview = endpoint.paint_invalid_preview});
    }

    view.truncated = view.truncated || projection.lane_key_bindings_truncated ||
                     band_count != bands.size() || lane_count != lanes.size() ||
                     line_count != lines.size() || difference_count != differences.size() ||
                     hold_count != holds.size() || endpoint_count != endpoints.size();
}

void project_compose_bgm(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.flattened;
    const auto& scene = projection.scene;
    const auto& history = projection.history;
    auto& view = projection.view;
    const auto convert_rect = [](const wormedit::editor_field_rect_t& rect) {
        return WormUiComposeRect{rect.left, rect.top, rect.right, rect.bottom};
    };
    const auto& bgm_lanes = scene.bgm_lanes;
    const auto bgm_lane_count =
        std::min(bgm_lanes.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_BGM_LANE_CAPACITY));
    flattened.compose_bgm_lane_storage.reserve(bgm_lane_count);
    for (std::size_t index = 0; index < bgm_lane_count; ++index) {
        const auto& lane = bgm_lanes[index];
        flattened.compose_bgm_lane_storage.push_back({.bounds = convert_rect(lane.bounds),
                                                      .source_lane_id = lane.source_lane.value,
                                                      .source_order = lane.source_order});
    }
    const auto& bgm_events = scene.bgm_events;
    const auto bgm_event_count =
        std::min(bgm_events.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_BGM_EVENT_CAPACITY));
    std::unordered_set<std::uint64_t> visible_sound_ids;
    visible_sound_ids.reserve(bgm_event_count);
    for (std::size_t index = 0; index < bgm_event_count; ++index) {
        if (bgm_events[index].sound)
            visible_sound_ids.insert(bgm_events[index].sound->value);
    }
    flattened.compose_bgm_sound_labels.reserve(visible_sound_ids.size());
    for (const auto& resource : history.document.resources) {
        if (!visible_sound_ids.contains(resource.id.value))
            continue;
        const auto bms_identity =
            std::ranges::find_if(resource.target_identities, [](const auto& identity) {
                return identity.family == wormedit::format_family_t::bms;
            });
        const auto name = resource.name.empty()
                              ? "Resource " + std::to_string(resource.id.value)
                              : resource.name;
        const auto label = bms_identity != resource.target_identities.end()
                               ? bms_identity->identifier
                               : std::filesystem::path{name}.stem().string();
        flattened.compose_bgm_sound_labels.emplace(
            resource.id.value,
            std::pair{label.empty() ? std::to_string(resource.id.value) : label, name});
    }
    flattened.compose_bgm_event_storage.reserve(bgm_event_count);
    for (std::size_t index = 0; index < bgm_event_count; ++index) {
        const auto& event = bgm_events[index];
        const auto sound = event.sound
                               ? flattened.compose_bgm_sound_labels.find(event.sound->value)
                               : flattened.compose_bgm_sound_labels.end();
        flattened.compose_bgm_event_storage.push_back(
            {.bounds = convert_rect(event.bounds),
             .event_id = event.event.value,
             .source_lane_id = event.source_lane.value,
             .sound_id = event.sound ? event.sound->value : 0,
             .has_sound = event.sound.has_value(),
             .bmson_slice = event.bmson_restart.has_value(),
             .bmson_restart = event.bmson_restart.value_or(false),
             .selected = event.selected,
             .sound_label = sound == flattened.compose_bgm_sound_labels.end()
                                ? (event.sound ? "?" : "")
                                : sound->second.first.c_str(),
             .sound_detail = sound == flattened.compose_bgm_sound_labels.end()
                                 ? (event.sound ? "Unknown sound resource" : "Unbound BGM slot")
                                 : sound->second.second.c_str()});
    }

    view.truncated = view.truncated || bgm_lane_count != bgm_lanes.size() ||
                     bgm_event_count != bgm_events.size();
}

void project_compose_marker_previews(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.flattened;
    const auto& chart = projection.chart;
    const auto& scene = projection.scene;
    const auto& index = projection.index;
    const auto& application_state = projection.application_state;
    const auto& interaction_view = projection.interaction_view;
    auto& view = projection.view;
    const auto pixels_per_row = projection.pixels_per_row;
    const auto downscroll = projection.downscroll;
    const auto convert_rect = [](const wormedit::editor_field_rect_t& rect) {
        return WormUiComposeRect{rect.left, rect.top, rect.right, rect.bottom};
    };
    if (application_state.bmson_marker_draft && pixels_per_row > 0.0) {
        const auto& draft = *application_state.bmson_marker_draft;
        const wormedit::bgm_event_t* reference{};
        for (const auto& measure : chart.measures) {
            const auto event = std::ranges::find(measure.bgm_events, draft.reference,
                                                  &wormedit::bgm_event_t::id);
            if (event != measure.bgm_events.end()) {
                reference = &*event;
                break;
            }
        }
        if (reference) {
            const auto lane = std::ranges::find_if(scene.bgm_lanes, [&](const auto& candidate) {
                return candidate.source_lane == reference->source_lane;
            });
            for (const auto& marker : draft.markers) {
                const auto row = wormedit::absolute_row(projection.index.measures, marker.position);
                if (!row) {
                    continue;
                }
                const auto y = compose_project_y(*row, pixels_per_row,
                                                 interaction_view.viewport_scroll_pixels,
                                                 view.viewport_height_pixels, downscroll);
                if (y < -8.0 || y > view.viewport_height_pixels + 8.0) {
                    continue;
                }
                const wormedit::editor_field_rect_t* bounds{};
                if (marker.playable_lane) {
                    const auto playable =
                        std::ranges::find_if(scene.lanes, [&](const auto& candidate) {
                            return candidate.lane == *marker.playable_lane &&
                                   y >= candidate.bounds.top - 8.0 &&
                                   y <= candidate.bounds.bottom + 8.0;
                        });
                    if (playable != scene.lanes.end()) {
                        bounds = &playable->bounds;
                    }
                } else if (lane != scene.bgm_lanes.end()) {
                    bounds = &lane->bounds;
                }
                if (!bounds) {
                    continue;
                }
                flattened.compose_bgm_draft_storage.push_back(
                    {.bounds = {bounds->left, y - 8.0, bounds->right, y + 8.0},
                     .restart = marker.restart,
                     .playable = marker.playable_lane.has_value()});
            }
        }
    }
    if (application_state.bmson_marker_move_preview && pixels_per_row > 0.0) {
        const auto& preview = *application_state.bmson_marker_move_preview;
        const auto lane = std::ranges::find_if(scene.bgm_lanes, [&](const auto& candidate) {
            return candidate.source_lane == preview.source_lane;
        });
        const auto row = preview.virtual_canvas_row
            ? preview.virtual_canvas_row
            : wormedit::absolute_row(projection.index.measures, preview.target);
        if (lane != scene.bgm_lanes.end() && row) {
            const auto y = compose_project_y(*row, pixels_per_row,
                                             interaction_view.viewport_scroll_pixels,
                                             view.viewport_height_pixels, downscroll);
            if (y >= -8.0 && y <= view.viewport_height_pixels + 8.0) {
                flattened.compose_bgm_move_storage = WormUiComposeBgmMovePreviewView{
                    .bounds = {lane->bounds.left, y - 8.0, lane->bounds.right, y + 8.0},
                    .valid = preview.valid};
            }
        }
    }
    if (scene.cursor) {
        flattened.compose_cursor_storage = WormUiComposeCursorView{
            .bounds = convert_rect(scene.cursor->bounds),
            .lane_id = scene.cursor->lane.value,
        };
    }

}

void publish_compose_scene(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.flattened;
    auto& view = projection.view;
    view.bands = flattened.compose_band_storage.data();
    view.band_count = flattened.compose_band_storage.size();
    view.lanes = flattened.compose_lane_storage.data();
    view.lane_count = flattened.compose_lane_storage.size();
    view.grid_lines = flattened.compose_grid_storage.data();
    view.grid_line_count = flattened.compose_grid_storage.size();
    view.differences = flattened.compose_difference_storage.data();
    view.difference_count = flattened.compose_difference_storage.size();
    view.hold_bodies = flattened.compose_hold_storage.data();
    view.hold_body_count = flattened.compose_hold_storage.size();
    view.note_endpoints = flattened.compose_note_storage.data();
    view.note_endpoint_count = flattened.compose_note_storage.size();
    view.bgm_lanes = flattened.compose_bgm_lane_storage.data();
    view.bgm_lane_count = flattened.compose_bgm_lane_storage.size();
    view.bgm_events = flattened.compose_bgm_event_storage.data();
    view.bgm_event_count = flattened.compose_bgm_event_storage.size();
    view.bmson_marker_drafts = flattened.compose_bgm_draft_storage.data();
    view.bmson_marker_draft_count = flattened.compose_bgm_draft_storage.size();
    view.bmson_marker_move_preview = flattened.compose_bgm_move_storage
        ? &*flattened.compose_bgm_move_storage : nullptr;
    view.cursor = flattened.compose_cursor_storage ? &*flattened.compose_cursor_storage : nullptr;
}
bool initialize_compose_view_state(
    flattened_workspace_t& flattened, compose_scene_cache_t& cache, int window_width,
    int window_height, const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    const application_command_state_t& application_state) {
    auto& view = flattened.view.compose;
    if (!view.visible) {
        cache.interaction_view.reset();
        cache.hover.reset();
        cache.focus_note_label_editor = false;
        cache.viewport_autoscroll_active = false;
        return false;
    }
    view.viewport_width_pixels = std::max(160.0, static_cast<double>(window_width) - 48.0);
    view.viewport_height_pixels = compose_surface_height(window_height, flattened.view);
    view.playable_field_left_pixels = compose_measure_rail_width_pixels;
    view.playable_field_right_pixels = view.viewport_width_pixels;
    view.bms_measure_labels = false;
    view.selected_bms_measure_number_plus_one = 0;
    view.timing_document_begin_fraction = 0.0;
    view.timing_document_end_fraction = 1.0;
    view.truncated = false;
    view.pixels_per_standard_measure = retained_state
                                           ? retained_state->pixels_per_standard_measure
                                           : wormedit::default_pixels_per_standard_measure;
    view.note_paint_mode_active = application_state.note_paint_mode_active;
    view.semantic_layer_paint_mode_active = application_state.semantic_layer_paint_mode_active;
    view.timing_stage_active = false;
    view.timing_stage_valid = false;
    view.timing_stage_lane = WORM_UI_TIMING_LANE_BPM;
    view.timing_stage_y_pixels = 0.0;
    view.timing_lane_visibility_mask = application_state.compose_timing_lane_visibility_mask;
    view.selected_note_count = 0;
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
    view.waveform_audio_offset_numerator = 0;
    view.waveform_audio_offset_denominator = 1;
    view.waveform_offset_drag_active = false;
    view.beat_fit_drag_active = false;
    view.proposed_grid_lines = nullptr;
    view.proposed_grid_line_count = 0;
    view.timing_preview_rects = nullptr;
    view.timing_preview_rect_count = 0;
    view.timing_preview_markers = nullptr;
    view.timing_preview_marker_count = 0;
    view.waveform_proposed_beat_zero_visible = false;
    view.waveform_proposed_beat_zero_fraction = 0.0;
    view.waveform_proposed_audio_offset_numerator = 0;
    view.waveform_proposed_audio_offset_denominator = 1;
    view.waveform_timing_locked = application_state.timing_locked;
    view.waveform_analysis_pending = false;
    view.beat_marker_navigation_available = false;
    view.audio_marker_navigation_available = false;
    view.audio_marker_transport_available = false;
    view.beat_bookmarks = flattened.compose_bookmark_storage.data();
    view.beat_bookmark_count = 0;
    view.beat_bookmarks_truncated = false;
    view.beat_sections = flattened.compose_section_storage.data();
    view.beat_section_count = 0;
    view.beat_sections_truncated = false;
    view.beat_section_selection_span_available = false;
    view.beat_section_selection_begin_measure_number = 0;
    view.beat_section_selection_begin_row = 0;
    view.beat_section_selection_end_measure_number = 0;
    view.beat_section_selection_end_row = 0;
    view.audio_bookmarks = flattened.compose_audio_bookmark_storage.data();
    view.audio_bookmark_count = 0;
    view.audio_bookmarks_truncated = false;
    view.audio_sections = flattened.compose_audio_section_storage.data();
    view.audio_section_count = 0;
    view.audio_sections_truncated = false;
    view.semantic_layers = flattened.compose_semantic_layer_storage.data();
    view.semantic_layer_count = 0;
    view.semantic_layer_total_count = 0;
    view.semantic_layer_page = 0;
    view.semantic_layer_page_count = 0;
    view.semantic_layers_truncated = false;
    view.semantic_layer_target_locked = false;
    view.semantic_layer_target_visible = false;
    view.semantic_layer_target_required_filter = false;
    view.semantic_layer_target_excluded_filter = false;
    view.semantic_layer_solo_active = application_state.semantic_layer_solo_active;
    view.status_text = nullptr;
    view.selected_note_label = nullptr;
    view.selected_note_label_note_id = 0;
    view.focus_note_label_editor = false;
    view.hover_note_visible = false;
    view.hover_note_id = 0;
    view.hover_tooltip = nullptr;
    view.timing_markers = nullptr;
    view.timing_marker_count = 0;
    view.viewport_autoscroll_active = cache.viewport_autoscroll_active;
    view.viewport_autoscroll_anchor_x_pixels = cache.viewport_autoscroll_anchor_x_pixels;
    view.viewport_autoscroll_anchor_y_pixels = cache.viewport_autoscroll_anchor_y_pixels;
    view.viewport_autoscroll_pointer_x_pixels = cache.viewport_autoscroll_pointer_x_pixels;
    view.viewport_autoscroll_pointer_y_pixels = cache.viewport_autoscroll_pointer_y_pixels;
    flattened.compose_hover_tooltip_storage.clear();
    flattened.compose_selected_note_label_storage.clear();
    return true;
}

bool populate_semantic_layer_palette(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    compose_scene_cache_t& cache, const wormedit::document_history_t& history,
    application_command_state_t& application_state) {
    auto& view = flattened.view.compose;
    if (!cache.attempted || cache.chart != chart.id ||
        cache.revision != history.document.revision) {
        cache.semantic_layer_membership_counts.clear();
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) {
                for (const auto layer : note.semantic_layers) {
                    auto& count = cache.semantic_layer_membership_counts[layer.value];
                    if (count != std::numeric_limits<std::size_t>::max()) {
                        ++count;
                    }
                }
            }
        }
    }
    const auto palette_context_changed = application_state.semantic_layer_palette_chart != chart.id;
    const auto palette_target_changed =
        application_state.semantic_layer_palette_target != application_state.semantic_layer_target;
    if (palette_context_changed) {
        application_state.semantic_layer_palette_chart = chart.id;
        application_state.semantic_layer_palette_page = 0;
    }
    const auto palette = wormedit::make_semantic_layer_palette_window(
        chart, application_state.semantic_layer_target,
        application_state.semantic_layer_palette_page,
        palette_context_changed || palette_target_changed, WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY);
    if (!palette) {
        flattened.compose_status_storage = "The semantic-layer palette exceeds its UI limit.";
        view.status_text = flattened.compose_status_storage.c_str();
        return false;
    }
    application_state.semantic_layer_palette_target = application_state.semantic_layer_target;
    application_state.semantic_layer_palette_page = palette->page;
    view.semantic_layer_total_count = palette->total_count;
    view.semantic_layer_page = palette->page;
    view.semantic_layer_page_count = palette->page_count;
    view.semantic_layers_truncated = palette->page_count > 1;
    for (const auto layer_index : palette->layer_indices) {
        const auto& layer = chart.semantic_layers[layer_index];
        const auto selected = application_state.semantic_layer_target == layer.id;
        const auto index = view.semantic_layer_count++;
        flattened.compose_semantic_layer_name_storage[index] = layer.name;
        flattened.compose_semantic_layer_storage[index] = {
            .layer_id = layer.id.value,
            .name = flattened.compose_semantic_layer_name_storage[index].c_str(),
            .red = layer.color.red,
            .green = layer.color.green,
            .blue = layer.color.blue,
            .alpha = layer.color.alpha,
            .assigned_note_count = cache.semantic_layer_membership_counts[layer.id.value],
            .visible = layer.visible,
            .locked = layer.locked,
            .selected = selected,
        };
        view.semantic_layer_target_locked =
            view.semantic_layer_target_locked || (selected && layer.locked);
        view.semantic_layer_target_visible =
            view.semantic_layer_target_visible || (selected && layer.visible);
        view.semantic_layer_target_required_filter =
            view.semantic_layer_target_required_filter ||
            (selected &&
             std::ranges::find(application_state.required_semantic_layer_filters, layer.id) !=
                 application_state.required_semantic_layer_filters.end());
        view.semantic_layer_target_excluded_filter =
            view.semantic_layer_target_excluded_filter ||
            (selected &&
             std::ranges::find(application_state.excluded_semantic_layer_filters, layer.id) !=
                 application_state.excluded_semantic_layer_filters.end());
    }
    return true;
}

bool populate_compose_bookmark_storage(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const wormedit::measure_index_t& measures, WormUiComposeView& view) {
    for (const auto& bookmark : chart.bookmarks) {
        if (bookmark.anchor == wormedit::marker_anchor_t::audio_time) {
            if (!bookmark.audio_milliseconds || bookmark.position) {
                flattened.compose_status_storage =
                    "An audio-time bookmark has inconsistent position data.";
                view.status_text = flattened.compose_status_storage.c_str();
                return false;
            }
            if (view.audio_bookmark_count >= WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY) {
                view.audio_bookmarks_truncated = true;
                continue;
            }
            const auto index = view.audio_bookmark_count++;
            flattened.compose_audio_bookmark_label_storage[index] = bookmark.label;
            flattened.compose_audio_bookmark_storage[index] = {
                .bookmark_id = bookmark.id.value,
                .label = flattened.compose_audio_bookmark_label_storage[index].c_str(),
                .audio_milliseconds = *bookmark.audio_milliseconds,
                .projection_visible = false,
                .projected_y_pixels = 0.0,
            };
            continue;
        }
        if (bookmark.anchor != wormedit::marker_anchor_t::beat) {
            continue;
        }
        if (!bookmark.position) {
            flattened.compose_status_storage =
                "A beat-anchored bookmark has no valid chart position.";
            view.status_text = flattened.compose_status_storage.c_str();
            return false;
        }
        if (view.beat_bookmark_count >= WORM_UI_COMPOSE_BOOKMARK_CAPACITY) {
            view.beat_bookmarks_truncated = true;
            continue;
        }
        const auto measure = std::ranges::find(measures.entries, bookmark.position->measure,
                                               &wormedit::measure_index_entry_t::id);
        if (measure == measures.entries.end() || bookmark.position->row >= measure->row_capacity) {
            flattened.compose_status_storage =
                "A beat-anchored bookmark is outside the chart measure geometry.";
            view.status_text = flattened.compose_status_storage.c_str();
            return false;
        }
        const auto index = view.beat_bookmark_count++;
        flattened.compose_bookmark_label_storage[index] = bookmark.label;
        flattened.compose_bookmark_storage[index] = {
            .bookmark_id = bookmark.id.value,
            .label = flattened.compose_bookmark_label_storage[index].c_str(),
            .measure_number =
                static_cast<std::uint64_t>(std::distance(measures.entries.begin(), measure) + 1),
            .row = bookmark.position->row,
            .chart_row = measure->absolute_begin_row + bookmark.position->row,
            .projection_visible = false,
            .projected_y_pixels = 0.0,
            .proposed_projection_visible = false,
            .proposed_y_pixels = 0.0,
        };
    }
    return true;
}

bool populate_compose_section_storage(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const wormedit::measure_index_t& measures, WormUiComposeView& view) {
    for (const auto& section : chart.sections) {
        if (section.anchor == wormedit::marker_anchor_t::audio_time) {
            if (!section.audio_millisecond_span || section.beat_span ||
                section.audio_millisecond_span->second <= section.audio_millisecond_span->first) {
                flattened.compose_status_storage =
                    "An audio-time section has inconsistent span data.";
                view.status_text = flattened.compose_status_storage.c_str();
                return false;
            }
            if (view.audio_section_count >= WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY) {
                view.audio_sections_truncated = true;
                continue;
            }
            const auto index = view.audio_section_count++;
            flattened.compose_audio_section_label_storage[index] = section.label;
            flattened.compose_audio_section_storage[index] = {
                .section_id = section.id.value,
                .label = flattened.compose_audio_section_label_storage[index].c_str(),
                .begin_audio_milliseconds = section.audio_millisecond_span->first,
                .end_audio_milliseconds = section.audio_millisecond_span->second,
                .begin_projection_visible = false,
                .end_projection_visible = false,
                .span_projection_visible = false,
                .projected_begin_y_pixels = 0.0,
                .projected_end_y_pixels = 0.0,
                .projected_span_top_y_pixels = 0.0,
                .projected_span_bottom_y_pixels = 0.0,
            };
            continue;
        }
        if (section.anchor != wormedit::marker_anchor_t::beat) {
            continue;
        }
        if (!section.beat_span) {
            flattened.compose_status_storage = "A beat-anchored section has no valid chart span.";
            view.status_text = flattened.compose_status_storage.c_str();
            return false;
        }
        if (view.beat_section_count >= WORM_UI_COMPOSE_SECTION_CAPACITY) {
            view.beat_sections_truncated = true;
            continue;
        }
        const auto begin_measure =
            std::ranges::find(measures.entries, section.beat_span->begin.measure,
                              &wormedit::measure_index_entry_t::id);
        const auto end_measure = std::ranges::find(measures.entries, section.beat_span->end.measure,
                                                   &wormedit::measure_index_entry_t::id);
        if (begin_measure == measures.entries.end() || end_measure == measures.entries.end() ||
            section.beat_span->begin.row >= begin_measure->row_capacity ||
            section.beat_span->end.row >= end_measure->row_capacity) {
            flattened.compose_status_storage =
                "A beat-anchored section is outside the chart measure geometry.";
            view.status_text = flattened.compose_status_storage.c_str();
            return false;
        }
        const auto index = view.beat_section_count++;
        flattened.compose_section_label_storage[index] = section.label;
        flattened.compose_section_storage[index] = {
            .section_id = section.id.value,
            .label = flattened.compose_section_label_storage[index].c_str(),
            .begin_measure_number = static_cast<std::uint64_t>(
                std::distance(measures.entries.begin(), begin_measure) + 1),
            .begin_row = section.beat_span->begin.row,
            .end_measure_number = static_cast<std::uint64_t>(
                std::distance(measures.entries.begin(), end_measure) + 1),
            .end_row = section.beat_span->end.row,
            .begin_chart_row = begin_measure->absolute_begin_row + section.beat_span->begin.row,
            .end_chart_row = end_measure->absolute_begin_row + section.beat_span->end.row,
            .begin_projection_visible = false,
            .end_projection_visible = false,
            .span_projection_visible = false,
            .projected_begin_y_pixels = 0.0,
            .projected_end_y_pixels = 0.0,
            .projected_span_top_y_pixels = 0.0,
            .projected_span_bottom_y_pixels = 0.0,
            .proposed_begin_projection_visible = false,
            .proposed_end_projection_visible = false,
            .proposed_span_projection_visible = false,
            .proposed_begin_y_pixels = 0.0,
            .proposed_end_y_pixels = 0.0,
            .proposed_span_top_y_pixels = 0.0,
            .proposed_span_bottom_y_pixels = 0.0,
        };
    }
    return true;
}

bool populate_compose_bookmark_and_section_storage(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const wormedit::measure_index_t& measures, WormUiComposeView& view) {
    return populate_compose_bookmark_storage(flattened, chart, measures, view) &&
           populate_compose_section_storage(flattened, chart, measures, view);
}

void project_compose_markers(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const compose_scene_cache_t& cache,
    const wormedit::editor_interaction_view_model_t& interaction_view, WormUiComposeView& view,
    bool downscroll) {
    const auto beat_pixels_per_row =
        interaction_view.pixels_per_standard_measure / chart.divisions_per_standard_measure;
    const auto visible_beat_y = [&](std::uint64_t row) -> std::optional<double> {
        const auto y = compose_project_y(row, beat_pixels_per_row,
                                         interaction_view.viewport_scroll_pixels,
                                         view.viewport_height_pixels, downscroll);
        if (!std::isfinite(y) || y < 0.0 || y > view.viewport_height_pixels)
            return std::nullopt;
        return y;
    };
    for (std::size_t index = 0; index < view.beat_bookmark_count; ++index) {
        auto& bookmark = flattened.compose_bookmark_storage[index];
        const auto y = visible_beat_y(bookmark.chart_row);
        bookmark.projection_visible = y.has_value();
        bookmark.projected_y_pixels = y.value_or(0.0);
    }
    for (std::size_t index = 0; index < view.beat_section_count; ++index) {
        auto& section = flattened.compose_section_storage[index];
        const auto begin_y = compose_project_y(section.begin_chart_row, beat_pixels_per_row,
                                               interaction_view.viewport_scroll_pixels,
                                               view.viewport_height_pixels, downscroll);
        const auto end_y = compose_project_y(section.end_chart_row, beat_pixels_per_row,
                                             interaction_view.viewport_scroll_pixels,
                                             view.viewport_height_pixels, downscroll);
        section.begin_projection_visible =
            std::isfinite(begin_y) && begin_y >= 0.0 && begin_y <= view.viewport_height_pixels;
        section.end_projection_visible =
            std::isfinite(end_y) && end_y >= 0.0 && end_y <= view.viewport_height_pixels;
        section.projected_begin_y_pixels = section.begin_projection_visible ? begin_y : 0.0;
        section.projected_end_y_pixels = section.end_projection_visible ? end_y : 0.0;
        const auto span_top = std::min(begin_y, end_y);
        const auto span_bottom = std::max(begin_y, end_y);
        section.span_projection_visible = std::isfinite(begin_y) && std::isfinite(end_y) &&
                                          span_bottom >= 0.0 &&
                                          span_top <= view.viewport_height_pixels;
        if (section.span_projection_visible) {
            section.projected_span_top_y_pixels =
                std::clamp(span_top, 0.0, view.viewport_height_pixels);
            section.projected_span_bottom_y_pixels =
                std::clamp(span_bottom, 0.0, view.viewport_height_pixels);
        }
    }
    if (cache.marker_timing_index) {
        const auto projected_y = [&](std::int64_t milliseconds) -> std::optional<double> {
            const auto projected = wormedit::musical_position_at(
                *cache.marker_timing_index, wormedit::signed_rational_t{milliseconds, 1'000});
            if (!projected || !projected.absolute_row || projected.absolute_row->denominator == 0) {
                return std::nullopt;
            }
            const auto row = static_cast<long double>(projected.absolute_row->numerator) /
                             static_cast<long double>(projected.absolute_row->denominator);
            const auto pixels_per_row =
                static_cast<long double>(interaction_view.pixels_per_standard_measure) /
                static_cast<long double>(chart.divisions_per_standard_measure);
            const auto y = compose_project_y(row, static_cast<double>(pixels_per_row),
                                             interaction_view.viewport_scroll_pixels,
                                             view.viewport_height_pixels, downscroll);
            if (!std::isfinite(y) ||
                y < static_cast<long double>(std::numeric_limits<double>::lowest()) ||
                y > static_cast<long double>(std::numeric_limits<double>::max())) {
                return std::nullopt;
            }
            return static_cast<double>(y);
        };
        const auto visible_y = [&](double y) {
            return std::isfinite(y) && y >= 0.0 && y <= view.viewport_height_pixels;
        };
        for (std::size_t index = 0; index < view.audio_bookmark_count; ++index) {
            auto& bookmark = flattened.compose_audio_bookmark_storage[index];
            const auto y = projected_y(bookmark.audio_milliseconds);
            if (y && visible_y(*y)) {
                bookmark.projection_visible = true;
                bookmark.projected_y_pixels = *y;
            }
        }
        for (std::size_t index = 0; index < view.audio_section_count; ++index) {
            auto& section = flattened.compose_audio_section_storage[index];
            const auto begin = projected_y(section.begin_audio_milliseconds);
            const auto end = projected_y(section.end_audio_milliseconds);
            if (!begin || !end) {
                continue;
            }
            section.begin_projection_visible = visible_y(*begin);
            section.end_projection_visible = visible_y(*end);
            if (section.begin_projection_visible) {
                section.projected_begin_y_pixels = *begin;
            }
            if (section.end_projection_visible) {
                section.projected_end_y_pixels = *end;
            }
            const auto top = std::min(*begin, *end);
            const auto bottom = std::max(*begin, *end);
            if (bottom >= 0.0 && top <= view.viewport_height_pixels) {
                section.span_projection_visible = true;
                section.projected_span_top_y_pixels =
                    std::clamp(top, 0.0, view.viewport_height_pixels);
                section.projected_span_bottom_y_pixels =
                    std::clamp(bottom, 0.0, view.viewport_height_pixels);
            }
        }
    }
}
void populate_compose_timing_markers(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const compose_scene_cache_t& cache,
    const wormedit::editor_interaction_view_model_t& interaction_view,
    WormUiComposeView& view, bool downscroll, double pixels_per_row,
    std::uint64_t canvas_rows,
    const wormedit::timing_lane_pseudonote_preview_t* timing_stage) {
    flattened.compose_timing_marker_storage.clear();
    if (canvas_rows != 0 && pixels_per_row > 0.0) {
        const auto total_rows = static_cast<double>(canvas_rows);
        view.timing_document_begin_fraction = std::clamp(
            interaction_view.viewport_scroll_pixels / pixels_per_row / total_rows, 0.0, 1.0);
        view.timing_document_end_fraction = std::clamp(
            (interaction_view.viewport_scroll_pixels + view.viewport_height_pixels) /
                pixels_per_row / total_rows,
            view.timing_document_begin_fraction, 1.0);
    }
    const auto append_timing_marker = [&](wormedit::local_position_t position, std::uint64_t id,
                                          std::size_t source_order, std::size_t storage_order,
                                          WormUiTimingLaneKind lane) {
        if (flattened.compose_timing_marker_storage.size() >= WORM_UI_TIMING_MARKER_CAPACITY) {
            view.truncated = true;
            return;
        }
        const auto row = wormedit::absolute_row(cache.index->measures, position);
        if (!row) {
            return;
        }
        const auto y = compose_project_y(*row, pixels_per_row,
                                         interaction_view.viewport_scroll_pixels,
                                         view.viewport_height_pixels, downscroll);
        if (!std::isfinite(y) || y < 0.0 || y > view.viewport_height_pixels) {
            return;
        }
        flattened.compose_timing_marker_storage.push_back({
            .object_id = id,
            .source_order = static_cast<std::uint64_t>(source_order),
            .storage_order = static_cast<std::uint64_t>(storage_order),
            .marker_y_pixels = y,
            .marker_left_fraction = 0.0,
            .marker_right_fraction = 1.0,
            .chart_row = *row,
            .lane = lane,
        });
    };
    for (std::size_t index = 0; index < chart.bpms.size(); ++index)
        append_timing_marker(chart.bpms[index].position, chart.bpms[index].id.value,
                             chart.bpms[index].provenance.source_order, index,
                             WORM_UI_TIMING_LANE_BPM);
    for (std::size_t index = 0; index < chart.scrolls.size(); ++index)
        append_timing_marker(chart.scrolls[index].position, chart.scrolls[index].id.value,
                             chart.scrolls[index].provenance.source_order, index,
                             WORM_UI_TIMING_LANE_SCROLL);
    for (std::size_t index = 0; index < chart.speeds.size(); ++index)
        append_timing_marker(chart.speeds[index].position, chart.speeds[index].id.value,
                             chart.speeds[index].provenance.source_order, index,
                             WORM_UI_TIMING_LANE_SPEED);
    for (std::size_t index = 0; index < chart.pauses.size(); ++index)
        append_timing_marker(chart.pauses[index].position, chart.pauses[index].id.value,
                             chart.pauses[index].provenance.source_order, index,
                             chart.pauses[index].kind == wormedit::pause_kind_t::stop
                                 ? WORM_UI_TIMING_LANE_STOP
                                 : WORM_UI_TIMING_LANE_DELAY);
    for (std::size_t index = 0; index < chart.warps.size(); ++index)
        append_timing_marker(chart.warps[index].span.begin, chart.warps[index].id.value,
                             chart.warps[index].provenance.source_order, index,
                             WORM_UI_TIMING_LANE_WARP);
    for (std::size_t index = 0; index < chart.kiai.size(); ++index)
        append_timing_marker(chart.kiai[index].position, chart.kiai[index].id.value,
                             chart.kiai[index].provenance.source_order, index,
                             WORM_UI_TIMING_LANE_KIAI);
    for (std::size_t index = 0; index < chart.timing_anchors.size(); ++index)
        append_timing_marker(chart.timing_anchors[index].position,
                             chart.timing_anchors[index].id.value, index, index,
                             WORM_UI_TIMING_LANE_ANCHOR);
    std::ranges::sort(flattened.compose_timing_marker_storage,
                      [](const auto& left, const auto& right) {
                          return std::tie(left.lane, left.marker_y_pixels, left.source_order,
                                          left.storage_order, left.object_id) <
                                 std::tie(right.lane, right.marker_y_pixels, right.source_order,
                                          right.storage_order, right.object_id);
                      });
    for (std::size_t first = 0; first < flattened.compose_timing_marker_storage.size();) {
        auto after = first + 1;
        while (after < flattened.compose_timing_marker_storage.size() &&
               flattened.compose_timing_marker_storage[after].lane ==
                   flattened.compose_timing_marker_storage[first].lane &&
               flattened.compose_timing_marker_storage[after].marker_y_pixels ==
                   flattened.compose_timing_marker_storage[first].marker_y_pixels) {
            ++after;
        }
        const auto count = static_cast<double>(after - first);
        for (auto index = first; index < after; ++index) {
            flattened.compose_timing_marker_storage[index].marker_left_fraction =
                static_cast<double>(index - first) / count;
            flattened.compose_timing_marker_storage[index].marker_right_fraction =
                static_cast<double>(index - first + 1) / count;
        }
        first = after;
    }
    view.timing_markers = flattened.compose_timing_marker_storage.data();
    view.timing_marker_count = flattened.compose_timing_marker_storage.size();
    if (timing_stage &&
        timing_stage->status == wormedit::timing_lane_pseudonote_status_t::editing &&
        timing_stage->chart == chart.id &&
        timing_stage->document_revision == view.document_revision) {
        const auto stage_row = wormedit::absolute_row(cache.index->measures, timing_stage->position);
        if (stage_row) {
            const auto y = compose_project_y(*stage_row, pixels_per_row,
                                             interaction_view.viewport_scroll_pixels,
                                             view.viewport_height_pixels, downscroll);
            if (std::isfinite(y) && y >= 0.0 && y <= view.viewport_height_pixels) {
                view.timing_stage_active = true;
                view.timing_stage_valid = timing_stage->valid;
                view.timing_stage_lane = timing_lane_kind_to_ui(timing_stage->kind);
                view.timing_stage_y_pixels = y;
            }
        }
    }
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
        const auto begin_row = wormedit::absolute_row(cache.index->measures, retained.begin);
        const auto end_row = wormedit::absolute_row(cache.index->measures, retained.end);
        const auto begin_measure =
            std::ranges::find(cache.index->measures.entries, retained.begin.measure,
                              &wormedit::measure_index_entry_t::id);
        const auto end_measure =
            std::ranges::find(cache.index->measures.entries, retained.end.measure,
                              &wormedit::measure_index_entry_t::id);
        if (begin_row && end_row && *begin_row < *end_row &&
            begin_measure != cache.index->measures.entries.end() &&
            end_measure != cache.index->measures.entries.end()) {
            view.beat_section_selection_span_available = true;
            view.beat_section_selection_begin_measure_number = static_cast<std::uint64_t>(
                std::distance(cache.index->measures.entries.begin(), begin_measure) + 1);
            view.beat_section_selection_begin_row = retained.begin.row;
            view.beat_section_selection_end_measure_number = static_cast<std::uint64_t>(
                std::distance(cache.index->measures.entries.begin(), end_measure) + 1);
            view.beat_section_selection_end_row = retained.end.row;
        } else {
            retained_state->retained_selection_range.reset();
            interaction_view.retained_selection_range.reset();
        }
    }
    view.selected_note_count = retained_state->selected_notes.size();
    view.selected_object_count = retained_state->selected_objects.size();
}

void populate_compose_selected_objects_and_drafts(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const wormedit::document_history_t& history,
    application_command_state_t& application_state,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    WormUiComposeView& view) {
    if (application_state.bmson_marker_draft &&
        (application_state.bmson_marker_draft->chart != chart.id ||
         application_state.bmson_marker_draft->revision != history.document.revision)) {
        application_state.bmson_marker_draft.reset();
        application_state.bmson_razor_stroke.reset();
    }
    if (application_state.bmson_marker_draft) {
        view.staged_bmson_marker_count = application_state.bmson_marker_draft->markers.size();
        flattened.compose_bmson_draft_playable_lane_storage.clear();
        flattened.compose_bmson_draft_restart_storage.clear();
        flattened.compose_bmson_draft_playable_lane_storage.reserve(view.staged_bmson_marker_count);
        flattened.compose_bmson_draft_restart_storage.reserve(view.staged_bmson_marker_count);
        for (const auto& marker : application_state.bmson_marker_draft->markers) {
            flattened.compose_bmson_draft_playable_lane_storage.push_back(
                marker.playable_lane.value_or(wormedit::lane_id_t{}).value);
            flattened.compose_bmson_draft_restart_storage.push_back(marker.restart ? 1 : 0);
        }
        view.staged_bmson_marker_playable_lanes =
            flattened.compose_bmson_draft_playable_lane_storage.data();
        view.staged_bmson_marker_restarts = flattened.compose_bmson_draft_restart_storage.data();
    }
    if (application_state.bmson_marker_move_preview &&
        (application_state.bmson_marker_move_preview->chart != chart.id ||
         application_state.bmson_marker_move_preview->revision != history.document.revision)) {
        application_state.bmson_marker_move_preview.reset();
    }
    if (retained_state->selected_objects.size() == 1 &&
        retained_state->selected_objects.front().kind ==
            wormedit::editor_selection_object_kind_t::bgm_event) {
        const auto selected = retained_state->selected_objects.front().id;
        for (const auto& measure : chart.measures) {
            const auto event = std::ranges::find(measure.bgm_events, selected,
                                                  &wormedit::bgm_event_t::id);
            if (event != measure.bgm_events.end()) {
                view.selected_bgm_event_id = selected.value;
                view.selected_assigned_resource_id =
                    event->sound.value_or(wormedit::resource_id_t{}).value;
                if (chart.source_family == wormedit::format_family_t::bmson &&
                    event->sound_channel_event) {
                    view.selected_bmson_bgm_event_id = selected.value;
                    view.selected_bmson_source_lane_id = event->source_lane.value;
                }
                break;
            }
        }
    }
    if (retained_state->selected_notes.size() == 1) {
        const auto note_id = retained_state->selected_notes.front();
        for (const auto& measure : chart.measures) {
            const auto note = std::ranges::find(measure.notes, note_id, &wormedit::note_t::id);
            if (note == measure.notes.end()) {
                continue;
            }
            if (note->label.has_value()) {
                flattened.compose_selected_note_label_storage = *note->label;
            }
            view.selected_note_label = flattened.compose_selected_note_label_storage.c_str();
            view.selected_note_label_note_id = note_id.value;
            view.selected_assigned_resource_id =
                note->sound.value_or(wormedit::resource_id_t{}).value;
            if (chart.source_family == wormedit::format_family_t::bmson &&
                note->sound_channel_event && note->kind == wormedit::note_kind_t::tap) {
                view.selected_bmson_note_event_id = note_id.value;
            }
            break;
        }
    }
}


} // namespace

void populate_compose_view(flattened_workspace_t& flattened,
                           wormedit::document_history_t& history,
                           const wormedit::workspace_controller_t& workspace,
                           compose_scene_cache_t& cache, int window_width, int window_height,
                           std::optional<wormedit::editor_interaction_state_t>& retained_state,
                           application_command_state_t& application_state,
                           const wormedit::timing_lane_pseudonote_preview_t* timing_stage) {
    if (!initialize_compose_view_state(flattened, cache, window_width, window_height,
                                       retained_state, application_state)) {
        return;
    }
    auto& view = flattened.view.compose;
    if (!workspace.selected_chart) {
        flattened.compose_status_storage = "Choose a difficulty to display its chart field.";
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    const auto chart_it = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                            &wormedit::chart_document_t::id);
    if (chart_it == history.document.charts.end()) {
        flattened.compose_status_storage = "The selected difficulty is no longer available.";
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    const auto& chart = *chart_it;
    const auto downscroll = compose_field_downscroll(chart);
    view.waveform_audio_offset_numerator = chart.beat_zero_audio_offset_seconds.numerator;
    view.waveform_audio_offset_denominator = chart.beat_zero_audio_offset_seconds.denominator;
    view.beat_marker_navigation_available = has_beat_marker_navigation(chart);
    view.audio_marker_navigation_available = has_audio_marker_navigation(chart);
    view.audio_marker_transport_available =
        static_cast<bool>(application_state.transport_audio_milliseconds) &&
        static_cast<bool>(application_state.transport_seek_audio_milliseconds) &&
        application_state.transport_audio_milliseconds().has_value();
    if (application_state.semantic_layer_filter_chart != chart.id) {
        application_state.semantic_layer_filter_chart = chart.id;
        application_state.required_semantic_layer_filters.clear();
        application_state.excluded_semantic_layer_filters.clear();
    }
    if (application_state.semantic_layer_target &&
        std::ranges::find(chart.semantic_layers, *application_state.semantic_layer_target,
                          &wormedit::semantic_layer_t::id) == chart.semantic_layers.end()) {
        application_state.semantic_layer_target.reset();
        application_state.semantic_layer_paint_mode_active = false;
        application_state.semantic_layer_solo_active = false;
    }
    view.semantic_layer_paint_mode_active = application_state.semantic_layer_paint_mode_active;
    view.semantic_layer_solo_active = application_state.semantic_layer_solo_active;
    view.project_id = history.document.id.value;
    view.chart_id = chart.id.value;
    view.document_revision = history.document.revision;
    if (!populate_semantic_layer_palette(flattened, chart, cache, history, application_state)) {
        return;
    }
    if (!cache.attempted || cache.chart != chart.id ||
        cache.revision != history.document.revision) {
        cache.index.reset();
        cache.bgm_rows_by_id.clear();
        cache.hit_test_index.reset();
        cache.marker_timing_index.reset();
        cache.proposed_offset_timing_index.reset();
        cache.proposed_offset_seconds.reset();
        cache.beat_fit_drag.reset();
        cache.beat_fit_asset.reset();
        cache.proposed_beat_timing_index.reset();
        cache.proposed_beat_bpm.reset();
        cache.proposed_beat_event = {};
        cache.channel_waveform_channel = {};
        cache.channel_waveform_source = nullptr;
        cache.channel_waveform_source_generation = 0;
        cache.channel_waveform_slices.reset();
        cache.channel_waveform_projection_valid = false;
        cache.channel_waveform_rows.clear();
        cache.interaction_view.reset();
        cache.hover.reset();
        cache.focus_note_label_editor = false;
        cache.viewport_autoscroll_active = false;
        cache.status.clear();
        cache.chart = chart.id;
        cache.revision = history.document.revision;
        cache.attempted = true;
        const auto built =
            wormedit::build_editor_field_scene_index(chart, history.document.revision);
        if (std::holds_alternative<wormedit::editor_field_scene_index_t>(built)) {
            cache.index = std::get<wormedit::editor_field_scene_index_t>(built);
            cache.bgm_rows_by_id.reserve(cache.index->bgm_events.size());
            for (const auto& event : cache.index->bgm_events) {
                cache.bgm_rows_by_id.emplace(event.event.value, event.row);
            }
        } else {
            const auto& diagnostics = std::get<wormedit::diagnostic_list_t>(built);
            cache.status = diagnostics.empty() ? "The field layout could not be indexed."
                                               : diagnostics.front().message;
        }
        auto timing = wormedit::build_timing_index(chart);
        if (timing) {
            cache.marker_timing_index = std::move(*timing.index);
        }
    }
    if (!cache.index) {
        flattened.compose_status_storage = cache.status;
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    if (chart.measures.empty()) {
        flattened.compose_status_storage = "This difficulty has no measures to display.";
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }

    const auto measure_index = wormedit::build_measure_index(chart);
    if (!std::holds_alternative<wormedit::measure_index_t>(measure_index)) {
        const auto& diagnostics = std::get<wormedit::diagnostic_list_t>(measure_index);
        flattened.compose_status_storage = diagnostics.empty()
                                               ? "The chart measure geometry is invalid."
                                               : diagnostics.front().message;
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    const auto& measures = std::get<wormedit::measure_index_t>(measure_index);
    if (!populate_compose_bookmark_and_section_storage(flattened, chart, measures, view)) {
        return;
    }
    const auto initial_interaction = [&]() -> wormedit::editor_interaction_setup_t {
        std::optional<wormedit::local_position_t> initial_cursor;
        std::optional<wormedit::lane_id_t> initial_lane;
        std::uint64_t earliest_layout_row = std::numeric_limits<std::uint64_t>::max();
        for (const auto& segment : chart.lane_layouts) {
            const auto row = wormedit::absolute_row(
                std::get<wormedit::measure_index_t>(measure_index), segment.position);
            if (!row || *row >= earliest_layout_row)
                continue;
            const auto lane = std::ranges::find_if(segment.layout.lanes, compose_lane_is_editable);
            if (lane == segment.layout.lanes.end())
                continue;
            earliest_layout_row = *row;
            initial_cursor = segment.position;
            initial_lane = lane->id;
        }
        if (!initial_cursor || !initial_lane) {
            return {};
        }
        return wormedit::make_editor_interaction(
            history, chart.id, *initial_lane, *initial_cursor, chart_snap_preferences,
            requested_snap_denominator(application_state, chart.id), view.viewport_height_pixels);
    };
    if (!retained_state || retained_state->chart != chart.id) {
        const auto initial = initial_interaction();
        if (!initial.state || !initial.view) {
            if (!initial.diagnostics.empty()) {
                flattened.compose_status_storage = initial.diagnostics.front().message;
            } else {
                flattened.compose_status_storage =
                    "This difficulty has no editable lanes to display.";
            }
            view.status_text = flattened.compose_status_storage.c_str();
            return;
        }
        retained_state = *initial.state;
        (void)wormedit::dispatch_editor_action(
            history, *retained_state,
            wormedit::scroll_editor_view_action_t{
                0.0, view.viewport_height_pixels * 0.5,
                downscroll ? wormedit::editor_field_scroll_direction_t::down
                           : wormedit::editor_field_scroll_direction_t::up});
    }

    // Compose and Timing consume the same chart-scoped snap preference. Do
    // not replace a gesture's captured grid mid-drag: the next idle frame
    // applies a Timing selection or a Compose snap-cycle uniformly.
    if (!editor_interaction_has_pending_gesture(*retained_state)) {
        const auto snap = wormedit::filter_snap_denominators(
            chart.divisions_per_standard_measure, chart_snap_preferences,
            requested_snap_denominator(application_state, chart.id));
        if (!snap) {
            flattened.compose_status_storage = "The chart snap configuration is invalid.";
            view.status_text = flattened.compose_status_storage.c_str();
            return;
        }
        retained_state->supported_snap_denominators = snap.supported_denominators;
        retained_state->active_snap_denominator = snap.active_denominator;
        retained_state->active_snap_was_fallback = snap.active_was_fallback;
    }

    retained_state->viewport_height_pixels = view.viewport_height_pixels;
    auto inspected = wormedit::inspect_editor_interaction(history, *retained_state);
    if (!std::holds_alternative<wormedit::editor_interaction_view_model_t>(inspected)) {
        // Undoing the first edit in a virtual measure can remove the cursor's
        // measure. Rebind it immediately and keep the virtual viewport in place.
        auto recovered = wormedit::recover_editor_interaction(history, *retained_state);
        if (!recovered.state || !recovered.view) {
            retained_state.reset();
            flattened.compose_status_storage = recovered.diagnostics.empty()
                                                   ? "The chart field view could not be recovered."
                                                   : recovered.diagnostics.front().message;
            view.status_text = flattened.compose_status_storage.c_str();
            return;
        }
        retained_state = std::move(*recovered.state);
        inspected = std::move(*recovered.view);
    }
    auto interaction_view =
        std::get<wormedit::editor_interaction_view_model_t>(std::move(inspected));
    project_compose_markers(flattened, chart, cache, interaction_view, view, downscroll);
    if (application_state.preview_playhead_absolute_row) {
        const auto pixels_per_row = interaction_view.pixels_per_standard_measure /
                                    static_cast<double>(chart.divisions_per_standard_measure);
        const auto playhead_y = compose_project_y(
            *application_state.preview_playhead_absolute_row, pixels_per_row,
            interaction_view.viewport_scroll_pixels, view.viewport_height_pixels, downscroll);
        if (std::isfinite(playhead_y) && playhead_y >= 0.0 &&
            playhead_y <= view.viewport_height_pixels) {
            view.playhead_visible = true;
            view.playhead_y_pixels = playhead_y;
        }
    }
    populate_compose_selection_preview(cache, retained_state, interaction_view, view);
    populate_compose_selected_objects_and_drafts(flattened, chart, history,
                                                 application_state, retained_state, view);
    auto canvas_rows = cache.index->measures.total_rows;
    if (chart.source_family == wormedit::format_family_t::bms) {
        const auto built_canvas = wormedit::build_virtual_canvas(chart);
        if (const auto* canvas = std::get_if<wormedit::virtual_canvas_t>(&built_canvas))
            canvas_rows = canvas->total_rows;
    }
    const auto content_height = static_cast<long double>(canvas_rows) *
                                interaction_view.pixels_per_standard_measure /
                                chart.divisions_per_standard_measure;
    const auto maximum_scroll =
        std::max(0.0L, content_height - static_cast<long double>(view.viewport_height_pixels));
    retained_state->viewport_scroll_pixels = std::clamp(retained_state->viewport_scroll_pixels, 0.0,
                                                        static_cast<double>(maximum_scroll));
    interaction_view.viewport_scroll_pixels = retained_state->viewport_scroll_pixels;
    view.pixels_per_standard_measure = interaction_view.pixels_per_standard_measure;
    cache.interaction_view = interaction_view;
    const auto hover_gesture_active =
        retained_state->pending_hold.has_value() ||
        retained_state->pending_note_duration_gesture.has_value() ||
        retained_state->pending_note_move_gesture.has_value() ||
        retained_state->pending_note_paint_gesture.has_value() ||
        retained_state->pending_semantic_layer_paint_gesture.has_value() ||
        retained_state->pending_editor_area_selection.has_value() ||
        retained_state->pending_space_range_selection.has_value();
    if (!hover_gesture_active && cache.hover &&
        cache.hover->document_revision == history.document.revision &&
        cache.hover->object.kind == wormedit::editor_selection_object_kind_t::note) {
        view.hover_note_visible = true;
        view.hover_note_id = cache.hover->object.id.value;
        flattened.compose_hover_tooltip_storage = cache.hover->tooltip;
        view.hover_tooltip = flattened.compose_hover_tooltip_storage.c_str();
    }
    if (!hover_gesture_active && cache.focus_note_label_editor &&
        retained_state->selected_notes.size() == 1) {
        view.focus_note_label_editor = true;
    }
    const auto scalar_selection_mode = [](wormedit::editor_area_selection_mode_t mode) {
        switch (mode) {
        case wormedit::editor_area_selection_mode_t::replace:
            return WORM_UI_COMPOSE_SELECTION_REPLACE;
        case wormedit::editor_area_selection_mode_t::add:
            return WORM_UI_COMPOSE_SELECTION_ADD;
        case wormedit::editor_area_selection_mode_t::toggle:
            return WORM_UI_COMPOSE_SELECTION_TOGGLE;
        }
        return WORM_UI_COMPOSE_SELECTION_REPLACE;
    };
    if (interaction_view.pending_editor_area_selection) {
        const auto& preview = *interaction_view.pending_editor_area_selection;
        view.area_selection_preview_visible = true;
        view.area_selection_preview_mode =
            scalar_selection_mode(retained_state->pending_editor_area_selection->mode);
        view.area_selection_preview_bounds = {
            std::clamp(preview.left_pixels, 0.0, view.viewport_width_pixels),
            std::clamp(downscroll ? view.viewport_height_pixels - preview.bottom_pixels
                                  : preview.top_pixels,
                       0.0, view.viewport_height_pixels),
            std::clamp(preview.right_pixels, 0.0, view.viewport_width_pixels),
            std::clamp(downscroll ? view.viewport_height_pixels - preview.top_pixels
                                  : preview.bottom_pixels,
                       0.0, view.viewport_height_pixels),
        };
    }
    if (retained_state->pending_space_range_selection) {
        const auto& pending = *retained_state->pending_space_range_selection;
        const auto cursor_row = retained_state->virtual_bms_cursor_row
                                    ? retained_state->virtual_bms_cursor_row
                                    : wormedit::absolute_row(
                                          std::get<wormedit::measure_index_t>(measure_index),
                                          retained_state->cursor);
        if (cursor_row) {
            const auto pixels_per_row = interaction_view.pixels_per_standard_measure /
                                        static_cast<double>(chart.divisions_per_standard_measure);
            const auto first_row = std::min(pending.anchor_canvas_row, *cursor_row);
            const auto last_row = std::max(pending.anchor_canvas_row, *cursor_row);
            const auto first_y = compose_project_y(first_row, pixels_per_row,
                                                   interaction_view.viewport_scroll_pixels,
                                                   view.viewport_height_pixels, downscroll);
            const auto last_y = compose_project_y(last_row + 1, pixels_per_row,
                                                  interaction_view.viewport_scroll_pixels,
                                                  view.viewport_height_pixels, downscroll);
            const auto top = std::min(first_y, last_y);
            const auto bottom = std::max(first_y, last_y);
            view.space_range_preview_visible = true;
            view.space_range_preview_mode = scalar_selection_mode(pending.mode);
            view.space_range_preview_bounds = {
                0.0,
                std::clamp(top, 0.0, view.viewport_height_pixels),
                view.viewport_width_pixels,
                std::clamp(bottom, 0.0, view.viewport_height_pixels),
            };
            const auto row_center = [&](std::uint64_t row) {
                return std::clamp(compose_project_y(
                                      static_cast<long double>(row) + 0.5L, pixels_per_row,
                                      interaction_view.viewport_scroll_pixels,
                                      view.viewport_height_pixels, downscroll),
                                  0.0, view.viewport_height_pixels);
            };
            view.space_range_anchor_y_pixels = row_center(pending.anchor_canvas_row);
            view.space_range_tail_y_pixels = row_center(*cursor_row);
        }
    }
    flattened.compose_interaction_view_storage = interaction_view;
    view.bms_measure_labels = chart.source_family == wormedit::format_family_t::bms;
    view.selected_bms_measure_number_plus_one =
        interaction_view.selected_canvas_measure
            ? static_cast<std::uint32_t>(*interaction_view.selected_canvas_measure + 1)
            : 0;
    std::size_t lane_key_binding_count{};
    bool lane_key_bindings_truncated{};
    for (const auto& binding : chart.input_profile.bindings) {
        const auto scancode = compose_physical_scancode(binding.key);
        if (scancode == 0) {
            continue;
        }
        if (lane_key_binding_count >= flattened.compose_lane_key_storage.size()) {
            lane_key_bindings_truncated = true;
            break;
        }
        flattened.compose_lane_key_storage[lane_key_binding_count++] = {
            .scancode = scancode,
            .lane_id = binding.lane.value,
        };
    }
    view.lane_key_bindings = flattened.compose_lane_key_storage.data();
    view.lane_key_binding_count = lane_key_binding_count;

    const auto horizontal_layout =
        compose_horizontal_layout(view.viewport_width_pixels, maximum_layout_lane_count(chart),
                                  !chart.bgm_source_lanes.empty(),
                                  std::popcount(
                                      application_state.compose_timing_lane_visibility_mask));
    const auto bgm_pane_width = horizontal_layout.bgm_pane_width_pixels;
    const auto playable_field_width = horizontal_layout.playable_field_width_pixels;
    const auto field_left_pixels = horizontal_layout.field_left_pixels;
    view.playable_field_left_pixels = field_left_pixels;
    view.playable_field_right_pixels = field_left_pixels + playable_field_width;

    const auto pixels_per_row = interaction_view.pixels_per_standard_measure /
                                static_cast<double>(chart.divisions_per_standard_measure);
    populate_compose_timing_markers(flattened, chart, cache, interaction_view, view,
                                    downscroll, pixels_per_row, canvas_rows, timing_stage);
    const auto maximum_bgm_scroll = std::max(
        0.0, static_cast<double>(chart.bgm_source_lanes.size()) * compose_bgm_lane_width_pixels -
                 bgm_pane_width);
    auto& bgm_horizontal_scroll = application_state.bgm_horizontal_scroll_pixels[chart.id.value];
    bgm_horizontal_scroll = std::clamp(bgm_horizontal_scroll, 0.0, maximum_bgm_scroll);
    cache.preview_scene_request = {
        .field_left_pixels = field_left_pixels,
        .field_width_pixels = playable_field_width,
        .split_stage_gutter_pixels = 14.0,
        .note_head_height_pixels = 16.0,
        .scroll_direction = downscroll
            ? wormedit::editor_field_scroll_direction_t::down
            : wormedit::editor_field_scroll_direction_t::up,
        .fixed_cursor_screen_y_pixels = retained_state->fixed_cursor_screen_y_pixels,
        .bgm_pane_left_pixels = field_left_pixels + playable_field_width,
        .bgm_pane_width_pixels = bgm_pane_width,
        .bgm_lane_width_pixels = compose_bgm_lane_width_pixels,
        .bgm_horizontal_scroll_pixels = bgm_horizontal_scroll,
        .include_virtual_bms_tail =
            chart.source_family == wormedit::format_family_t::bms,
        .solo_semantic_layer = application_state.semantic_layer_solo_active
                                   ? application_state.semantic_layer_target
                                   : std::nullopt,
        .required_semantic_layers = application_state.required_semantic_layer_filters,
        .excluded_semantic_layers = application_state.excluded_semantic_layer_filters,
        .maximum_grid_lines = WORM_UI_COMPOSE_GRID_CAPACITY,
        .maximum_scene_primitives = WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY};
    const auto composed = wormedit::make_editor_field_scene(chart, *cache.index, interaction_view,
                                                            cache.preview_scene_request);
    if (!composed.scene || composed.has_errors()) {
        flattened.compose_status_storage = composed.diagnostics.empty()
                                               ? "The chart field could not be composed."
                                               : composed.diagnostics.front().message;
        view.status_text = flattened.compose_status_storage.c_str();
        return;
    }
    const auto& scene = *composed.scene;
    view.scene_available = true;
    const compose_scene_projection_context_t projection{
        flattened, chart, *cache.index, scene, history, application_state, interaction_view, view,
        field_left_pixels, playable_field_width, pixels_per_row, downscroll,
        lane_key_bindings_truncated};
    project_compose_layout_and_notes(projection);
    project_compose_bgm(projection);
    project_compose_marker_previews(projection);
    publish_compose_scene(projection);
}


} // namespace wormedit::sys

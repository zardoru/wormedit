#include "ui/nuklear/compose_panel.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/sdl_backend.h"
#include "sys/compose_scroll_geometry.h"
#include "ui/nuklear/bms_branch_panel.h"
#include "ui/nuklear/compose_markers.h"
#include "ui/nuklear/compose_canvas.h"
#include "ui/nuklear/compose_toolbar.h"
#include "ui/nuklear/compose_inspector.h"
#include "ui/nuklear/compose_input_panel.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/compose_resources.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/timing_panel.h"

#include "wormedit/nuklear_config.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <ranges>
#include <string>

void worm_ui_nuklear_reset_compose_panel() {
    worm_ui_nuklear_reset_compose_markers();
    worm_ui_nuklear_reset_compose_state();
}

void worm_ui_nuklear_queue_compose_key(std::uint32_t keycode, std::uint32_t scancode,
                                       std::uint16_t modifiers, bool down, bool repeat) {
    queue_compose_key_input(keycode, scancode, modifiers, down, repeat);
}

void worm_ui_nuklear_queue_compose_wheel(double delta_x, double delta_y, double mouse_x,
                                         double mouse_y, std::uint16_t modifiers,
                                         bool coarse_steps) {
    queue_compose_wheel_input(delta_x, delta_y, mouse_x, mouse_y, modifiers, coarse_steps);
}

void worm_ui_nuklear_queue_compose_middle_click(float x, float y) {
    queue_compose_middle_click_input(x, y);
}

void worm_ui_nuklear_compose_handle_focus_lost() {
    compose_handle_focus_lost_input();
}

void worm_ui_nuklear_compose_discard_frame_input() {
    discard_compose_frame_input();
}

void worm_ui_nuklear_prepare_compose_panel(const WormUiComposeView* view, bool compose_tab_active,
                                           bool window_focused, WormUiWorkspaceAction* action) {
    prepare_compose_panel_input(view, compose_tab_active, window_focused, action);
}

static int compose_rect_valid(const WormUiComposeRect* rect) {
    if (!rect || !std::isfinite(rect->left) || !std::isfinite(rect->top) ||
        !std::isfinite(rect->right) || !std::isfinite(rect->bottom) || rect->right < rect->left ||
        rect->bottom < rect->top) {
        return 0;
    }
    constexpr double coordinate_limit = 10000000.0;
    return std::abs(rect->left) <= coordinate_limit && std::abs(rect->top) <= coordinate_limit &&
           std::abs(rect->right) <= coordinate_limit && std::abs(rect->bottom) <= coordinate_limit;
}

static bool compose_selection_mode_valid(WormUiComposeSelectionMode mode) {
    return mode == WORM_UI_COMPOSE_SELECTION_REPLACE || mode == WORM_UI_COMPOSE_SELECTION_ADD ||
           mode == WORM_UI_COMPOSE_SELECTION_TOGGLE;
}

static bool compose_bgm_scroll_valid(const WormUiComposeView* view) {
    return std::isfinite(view->bgm_horizontal_scroll_pixels) &&
           std::isfinite(view->bgm_maximum_horizontal_scroll_pixels) &&
           view->bgm_horizontal_scroll_pixels >= 0.0 &&
           view->bgm_maximum_horizontal_scroll_pixels >= view->bgm_horizontal_scroll_pixels;
}

static bool compose_geometry_dimensions_valid(const WormUiComposeView* view) {
    const auto valid_viewport = std::isfinite(view->viewport_width_pixels) &&
                                view->viewport_width_pixels > 0.0 &&
                                view->viewport_width_pixels <= 10000000.0 &&
                                std::isfinite(view->viewport_height_pixels) &&
                                view->viewport_height_pixels > 0.0 &&
                                view->viewport_height_pixels <= 10000000.0;
    const auto valid_measure = std::isfinite(view->pixels_per_standard_measure) &&
                               view->pixels_per_standard_measure >=
                                   WORM_UI_COMPOSE_MIN_PIXELS_PER_MEASURE &&
                               view->pixels_per_standard_measure <=
                                   WORM_UI_COMPOSE_MAX_PIXELS_PER_MEASURE;
    const auto valid_field = std::isfinite(view->playable_field_left_pixels) &&
                             std::isfinite(view->playable_field_right_pixels) &&
                             view->playable_field_left_pixels >= 0.0 &&
                             view->playable_field_left_pixels <= view->playable_field_right_pixels &&
                             view->playable_field_right_pixels <= view->viewport_width_pixels;
    const auto valid_document = std::isfinite(view->timing_document_begin_fraction) &&
                                std::isfinite(view->timing_document_end_fraction) &&
                                view->timing_document_begin_fraction >= 0.0 &&
                                view->timing_document_end_fraction >=
                                    view->timing_document_begin_fraction &&
                                view->timing_document_end_fraction <= 1.0;
    return valid_viewport && valid_measure && valid_field && valid_document &&
           compose_bgm_scroll_valid(view);
}

static bool compose_geometry_overlay_valid(const WormUiComposeView* view) {
    const auto stage_valid = !view->timing_stage_active ||
        (std::isfinite(view->timing_stage_y_pixels) && view->timing_stage_y_pixels >= 0.0 &&
         view->timing_stage_y_pixels <= view->viewport_height_pixels &&
         view->timing_stage_lane >= WORM_UI_TIMING_LANE_BPM &&
         view->timing_stage_lane <= WORM_UI_TIMING_LANE_ANCHOR);
    const auto playhead_valid = !view->playhead_visible ||
        (std::isfinite(view->playhead_y_pixels) && view->playhead_y_pixels >= 0.0 &&
         view->playhead_y_pixels <= view->viewport_height_pixels);
    const auto selection_valid = !view->area_selection_preview_visible ||
        (compose_rect_valid(&view->area_selection_preview_bounds) &&
         compose_selection_mode_valid(view->area_selection_preview_mode));
    const auto space_valid = !view->space_range_preview_visible ||
        (compose_rect_valid(&view->space_range_preview_bounds) &&
         compose_selection_mode_valid(view->space_range_preview_mode) &&
         std::isfinite(view->space_range_anchor_y_pixels) &&
         std::isfinite(view->space_range_tail_y_pixels) &&
         view->space_range_anchor_y_pixels >= 0.0 &&
         view->space_range_anchor_y_pixels <= view->viewport_height_pixels &&
         view->space_range_tail_y_pixels >= 0.0 &&
         view->space_range_tail_y_pixels <= view->viewport_height_pixels);
    return stage_valid && playhead_valid && selection_valid && space_valid;
}

static bool compose_geometry_counts_valid(const WormUiComposeView* view) {
    return view->branch_row_count <= WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY &&
           view->branch_choice_count <= WORM_UI_COMPOSE_BRANCH_CHOICE_CAPACITY &&
           view->branch_diagnostic_count <= WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY &&
           view->semantic_layer_count <= WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY &&
           view->beat_bookmark_count <= WORM_UI_COMPOSE_BOOKMARK_CAPACITY &&
           view->beat_section_count <= WORM_UI_COMPOSE_SECTION_CAPACITY &&
           view->audio_bookmark_count <= WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY &&
           view->audio_section_count <= WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY &&
           view->timing_marker_count <= WORM_UI_TIMING_MARKER_CAPACITY &&
           view->resource_count <= WORM_UI_COMPOSE_RESOURCE_CAPACITY &&
           view->output_group_count <= WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY &&
           view->resource_total_count >= view->resource_count &&
           (view->semantic_layer_page_count == 0
                ? view->semantic_layer_page == 0 && view->semantic_layer_total_count == 0 &&
                      view->semantic_layer_count == 0
                : view->semantic_layer_total_count != 0 &&
                      view->semantic_layer_page < view->semantic_layer_page_count &&
                      view->semantic_layer_page_count ==
                          1 + (view->semantic_layer_total_count - 1) /
                                  WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY &&
                      view->semantic_layer_count ==
                          std::min(static_cast<size_t>(WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY),
                                   view->semantic_layer_total_count -
                                       view->semantic_layer_page *
                                           WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY));
}

static bool compose_geometry_arrays_valid(const WormUiComposeView* view) {
    return (view->branch_row_count == 0 || view->branch_rows) &&
           (view->branch_choice_count == 0 || view->branch_choices) &&
           (view->beat_bookmark_count == 0 || view->beat_bookmarks) &&
           (view->beat_section_count == 0 || view->beat_sections) &&
           (view->audio_bookmark_count == 0 || view->audio_bookmarks) &&
           (view->audio_section_count == 0 || view->audio_sections) &&
           (view->timing_marker_count == 0 || view->timing_markers) &&
           (view->dead_span_count == 0 || view->dead_spans) &&
           (view->kiai_span_count == 0 || view->kiai_spans) &&
           (view->recipe_area_count == 0 || view->recipe_areas) &&
           (view->speed_span_count == 0 || view->speed_spans) &&
           (view->recipe_graph_point_count == 0 || view->recipe_graph_points) &&
           (view->sv_graph_point_count == 0 || view->sv_graph_points) &&
           (view->resource_count == 0 || view->resources) &&
           (view->output_group_count == 0 || view->output_groups);
}

static bool compose_geometry_scene_elements_valid(const WormUiComposeView* view);

static bool compose_geometry_valid(const WormUiComposeView* view) {
    return view && compose_geometry_dimensions_valid(view) &&
           compose_geometry_overlay_valid(view) && compose_geometry_counts_valid(view) &&
           compose_geometry_arrays_valid(view) && compose_geometry_scene_elements_valid(view);
}

static bool compose_geometry_basic_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->band_count; ++index) {
        if (!compose_rect_valid(&view->bands[index].bounds))
            return 0;
    }
    for (size_t index = 0; index < view->lane_count; ++index) {
        if (!compose_rect_valid(&view->lanes[index].bounds))
            return 0;
    }

    return true;
}

static bool compose_geometry_grid_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->grid_line_count; ++index) {
        if (!std::isfinite(view->grid_lines[index].y_pixels) ||
            std::abs(view->grid_lines[index].y_pixels) > 10000000.0)
            return 0;
    }
    for (size_t index = 0; index < view->proposed_grid_line_count; ++index) {
        if (!std::isfinite(view->proposed_grid_lines[index].y_pixels) ||
            view->proposed_grid_lines[index].y_pixels < 0.0 ||
            view->proposed_grid_lines[index].y_pixels > view->viewport_height_pixels)
            return 0;
    }

    return true;
}

static bool compose_dead_spans_valid(const WormUiComposeView* view) {
    for (size_t index = 0; index < view->dead_span_count; ++index) {
        const auto& span = view->dead_spans[index];
        if (!compose_rect_valid(&span.bounds) || span.kind > 2 || !span.label) return false;
    }
    return true;
}

static bool compose_kiai_and_sv_valid(const WormUiComposeView* view) {
    for (size_t index = 0; index < view->kiai_span_count; ++index) {
        const auto& span = view->kiai_spans[index];
        if (!compose_rect_valid(&span.bounds) || span.head_object_id == 0 ||
            span.end_row <= span.begin_row) return false;
    }
    for (size_t index = 0; index < view->sv_graph_point_count; ++index) {
        const auto& point = view->sv_graph_points[index];
        if (!std::isfinite(point.x_fraction) || point.x_fraction < 0.0 || point.x_fraction > 1.0 ||
            !std::isfinite(point.y_pixels)) return false;
    }
    return true;
}

static bool compose_speed_spans_valid(const WormUiComposeView* view) {
    for (size_t index = 0; index < view->speed_span_count; ++index) {
        const auto& span = view->speed_spans[index];
        if (!compose_rect_valid(&span.bounds) || span.object_id == 0 ||
            !std::isfinite(span.multiplier) || !std::isfinite(span.transition_length)) return false;
    }
    return true;
}

static bool compose_recipe_areas_valid(const WormUiComposeView* view) {
    for (size_t index = 0; index < view->recipe_area_count; ++index) {
        const auto& area = view->recipe_areas[index];
        if (!compose_rect_valid(&area.bounds) || area.recipe_id == 0 || !area.label ||
            (area.lane != WORM_UI_TIMING_LANE_SCROLL && area.lane != WORM_UI_TIMING_LANE_SPEED) ||
            area.graph_first > view->recipe_graph_point_count ||
            area.graph_count > view->recipe_graph_point_count - area.graph_first) return false;
    }
    for (size_t index = 0; index < view->recipe_graph_point_count; ++index) {
        const auto& point = view->recipe_graph_points[index];
        if (!std::isfinite(point.x_fraction) || point.x_fraction < 0.0 || point.x_fraction > 1.0 ||
            !std::isfinite(point.y_pixels)) return false;
    }
    return true;
}

static bool compose_geometry_timing_elements_valid(const WormUiComposeView* view) {
    if (!compose_dead_spans_valid(view) || !compose_kiai_and_sv_valid(view) ||
        !compose_recipe_areas_valid(view) || !compose_speed_spans_valid(view)) return 0;
    for (size_t index = 0; index < view->timing_preview_rect_count; ++index) {
        if (!compose_rect_valid(&view->timing_preview_rects[index].bounds) ||
            view->timing_preview_rects[index].kind > 2)
            return 0;
    }
    for (size_t index = 0; index < view->timing_preview_marker_count; ++index) {
        const auto& marker = view->timing_preview_markers[index];
        if (!std::isfinite(marker.marker_y_pixels) || marker.marker_y_pixels < 0.0 ||
            marker.marker_y_pixels > view->viewport_height_pixels ||
            marker.lane < WORM_UI_TIMING_LANE_BPM || marker.lane > WORM_UI_TIMING_LANE_ANCHOR)
            return 0;
    }
    for (size_t index = 0; index < view->timing_marker_count; ++index) {
        const auto& marker = view->timing_markers[index];
        const auto missing_initial = marker.fixed_initial &&
            marker.lane == WORM_UI_TIMING_LANE_BPM && marker.chart_row == 0;
        if ((marker.object_id == 0 && !missing_initial) || !std::isfinite(marker.marker_y_pixels) ||
            marker.marker_y_pixels < 0.0 ||
            marker.marker_y_pixels > view->viewport_height_pixels ||
            !std::isfinite(marker.marker_left_fraction) ||
            !std::isfinite(marker.marker_right_fraction) ||
            marker.marker_left_fraction < 0.0 ||
            marker.marker_right_fraction <= marker.marker_left_fraction ||
            marker.marker_right_fraction > 1.0 || marker.lane < WORM_UI_TIMING_LANE_BPM ||
            marker.lane > WORM_UI_TIMING_LANE_ANCHOR) {
            return 0;
        }
    }

    return true;
}

static bool compose_geometry_resource_elements_valid(const WormUiComposeView* view) {    constexpr std::uint8_t compose_resource_roles = WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE |
                                                    WORM_UI_COMPOSE_RESOURCE_ROLE_BGM |
                                                    WORM_UI_COMPOSE_RESOURCE_ROLE_BGA;
    for (size_t index = 0; index < view->resource_count; ++index) {
        const auto& resource = view->resources[index];
        if (resource.resource_id == 0 || resource.asset_id == 0 || resource.roles == 0 ||
            (resource.roles & ~compose_resource_roles) != 0 || !resource.target_index ||
            !resource.name || !resource.source_name) {
            return 0;
        }
    }
    for (size_t index = 0; index < view->output_group_count; ++index) {
        const auto& group = view->output_groups[index];
        if (!group.name || !group.relative_output_path || !group.relative_asset_directory ||
            (index != 0 && group.document_index <= view->output_groups[index - 1].document_index))
            return 0;
    }

    return true;
}

static bool compose_geometry_note_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->difference_count; ++index) {
        if (!compose_rect_valid(&view->differences[index].bounds))
            return 0;
    }
    for (size_t index = 0; index < view->hold_body_count; ++index) {
        if (!compose_rect_valid(&view->hold_bodies[index].bounds))
            return 0;
    }
    for (size_t index = 0; index < view->note_endpoint_count; ++index) {
        if (!compose_rect_valid(&view->note_endpoints[index].bounds) ||
            view->note_endpoints[index].glyph > 5)
            return 0;
    }
    for (size_t index = 0; index < view->bgm_lane_count; ++index) {
        if (!compose_rect_valid(&view->bgm_lanes[index].bounds) ||
            view->bgm_lanes[index].source_lane_id == 0)
            return 0;
    }
    for (size_t index = 0; index < view->bgm_event_count; ++index) {
        if (!compose_rect_valid(&view->bgm_events[index].bounds) ||
            view->bgm_events[index].event_id == 0 || view->bgm_events[index].source_lane_id == 0)
            return 0;
    }

    return true;
}

static bool compose_geometry_waveform_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->waveform_row_count; ++index) {
        const auto& row = view->waveform_rows[index];
        if (!std::isfinite(row.y_pixels) || row.y_pixels < 0.0 ||
            row.y_pixels > view->viewport_height_pixels) {
            return 0;
        }
        for (size_t channel = 0; channel < 2; ++channel) {
            if (!std::isfinite(row.absolute_peak[channel]) || !std::isfinite(row.rms[channel]) ||
                row.absolute_peak[channel] < 0.0f || row.absolute_peak[channel] > 1.0f ||
                row.rms[channel] < 0.0f || row.rms[channel] > 1.0f) {
                return 0;
            }
        }
    }
    for (size_t index = 0; index < view->channel_waveform_row_count; ++index) {
        const auto& row = view->channel_waveform_rows[index];
        if (!std::isfinite(row.y_pixels) || row.y_pixels < 0.0 ||
            row.y_pixels > view->viewport_height_pixels)
            return 0;
        for (size_t channel = 0; channel < 2; ++channel) {
            if (!std::isfinite(row.absolute_peak[channel]) || !std::isfinite(row.rms[channel]) ||
                row.absolute_peak[channel] < 0.0f || row.absolute_peak[channel] > 1.0f ||
                row.rms[channel] < 0.0f || row.rms[channel] > 1.0f)
                return 0;
        }
    }
    for (size_t index = 0; index < view->waveform_overview_row_count; ++index) {
        const auto& row = view->waveform_overview_rows[index];
        if (!std::isfinite(row.audio_fraction) || row.audio_fraction < 0.0 ||
            row.audio_fraction > 1.0 || !std::isfinite(row.absolute_peak) ||
            row.absolute_peak < 0.0f || row.absolute_peak > 1.0f) {
            return 0;
        }
    }

    return true;
}

static bool compose_geometry_bmson_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->bmson_marker_draft_count; ++index) {
        if (!compose_rect_valid(&view->bmson_marker_drafts[index].bounds)) {
            return 0;
        }
    }
    if (view->bmson_marker_move_preview &&
        !compose_rect_valid(&view->bmson_marker_move_preview->bounds)) {
        return 0;
    }
    for (size_t index = 0; index < view->semantic_layer_count; ++index) {
        if (!view->semantic_layers || view->semantic_layers[index].layer_id == 0 ||
            !view->semantic_layers[index].name) {
            return 0;
        }
    }

    return true;
}

static bool compose_geometry_bookmark_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->beat_bookmark_count; ++index) {
        const auto& bookmark = view->beat_bookmarks[index];
        if (bookmark.bookmark_id == 0 || !bookmark.label || bookmark.measure_number == 0 ||
            (bookmark.projection_visible &&
             (!std::isfinite(bookmark.projected_y_pixels) || bookmark.projected_y_pixels < 0.0 ||
              bookmark.projected_y_pixels > view->viewport_height_pixels)) ||
            (bookmark.proposed_projection_visible &&
             (!std::isfinite(bookmark.proposed_y_pixels) || bookmark.proposed_y_pixels < 0.0 ||
              bookmark.proposed_y_pixels > view->viewport_height_pixels))) {
            return 0;
        }
    }

    return true;
}

static bool compose_geometry_beat_section_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->beat_section_count; ++index) {
        const auto& section = view->beat_sections[index];
        const auto valid_y = [&](bool visible, double y) {
            return !visible || (std::isfinite(y) && y >= 0.0 && y <= view->viewport_height_pixels);
        };
        if (section.section_id == 0 || !section.label || section.begin_measure_number == 0 ||
            section.end_measure_number == 0 ||
            section.begin_measure_number > section.end_measure_number ||
            (section.begin_measure_number == section.end_measure_number &&
             section.begin_row >= section.end_row) ||
            section.begin_chart_row >= section.end_chart_row ||
            !valid_y(section.begin_projection_visible, section.projected_begin_y_pixels) ||
            !valid_y(section.end_projection_visible, section.projected_end_y_pixels) ||
            !valid_y(section.span_projection_visible, section.projected_span_top_y_pixels) ||
            !valid_y(section.span_projection_visible, section.projected_span_bottom_y_pixels) ||
            (section.span_projection_visible &&
             section.projected_span_top_y_pixels > section.projected_span_bottom_y_pixels) ||
            !valid_y(section.proposed_begin_projection_visible, section.proposed_begin_y_pixels) ||
            !valid_y(section.proposed_end_projection_visible, section.proposed_end_y_pixels) ||
            !valid_y(section.proposed_span_projection_visible,
                     section.proposed_span_top_y_pixels) ||
            !valid_y(section.proposed_span_projection_visible,
                     section.proposed_span_bottom_y_pixels) ||
            (section.proposed_span_projection_visible &&
             section.proposed_span_top_y_pixels > section.proposed_span_bottom_y_pixels)) {
            return 0;
        }
    }
    if (view->beat_section_selection_span_available &&
        (view->beat_section_selection_begin_measure_number == 0 ||
         view->beat_section_selection_end_measure_number == 0 ||
         view->beat_section_selection_begin_measure_number >
             view->beat_section_selection_end_measure_number ||
         (view->beat_section_selection_begin_measure_number ==
              view->beat_section_selection_end_measure_number &&
          view->beat_section_selection_begin_row >= view->beat_section_selection_end_row))) {
        return 0;
    }

    return true;
}

static bool compose_geometry_audio_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->audio_bookmark_count; ++index) {
        const auto& bookmark = view->audio_bookmarks[index];
        if (bookmark.bookmark_id == 0 || !bookmark.label ||
            (bookmark.projection_visible &&
             (!std::isfinite(bookmark.projected_y_pixels) || bookmark.projected_y_pixels < 0.0 ||
              bookmark.projected_y_pixels > view->viewport_height_pixels))) {
            return 0;
        }
    }
    for (size_t index = 0; index < view->audio_section_count; ++index) {
        const auto& section = view->audio_sections[index];
        if (section.section_id == 0 || !section.label ||
            section.end_audio_milliseconds <= section.begin_audio_milliseconds ||
            (section.begin_projection_visible &&
             (!std::isfinite(section.projected_begin_y_pixels) ||
              section.projected_begin_y_pixels < 0.0 ||
              section.projected_begin_y_pixels > view->viewport_height_pixels)) ||
            (section.end_projection_visible &&
             (!std::isfinite(section.projected_end_y_pixels) ||
              section.projected_end_y_pixels < 0.0 ||
              section.projected_end_y_pixels > view->viewport_height_pixels)) ||
            (section.span_projection_visible &&
             (!std::isfinite(section.projected_span_top_y_pixels) ||
              !std::isfinite(section.projected_span_bottom_y_pixels) ||
              section.projected_span_top_y_pixels < 0.0 ||
              section.projected_span_bottom_y_pixels < section.projected_span_top_y_pixels ||
              section.projected_span_bottom_y_pixels > view->viewport_height_pixels))) {
            return 0;
        }
    }

    return true;
}

static bool compose_geometry_hover_valid(const WormUiComposeView* view) {    if (view->hover_note_visible && (!view->hover_tooltip || view->hover_note_id == 0)) {
        return 0;
    }

    return true;
}

static bool compose_geometry_branch_elements_valid(const WormUiComposeView* view) {    for (size_t index = 0; index < view->branch_row_count; ++index) {
        if (!view->branch_rows[index].display_text || view->branch_rows[index].depth > 512)
            return 0;
    }
    for (size_t index = 0; index < view->branch_choice_count; ++index) {
        const auto& choice = view->branch_choices[index];
        if (choice.branch_id == 0 || choice.kind > WORM_UI_COMPOSE_BRANCH_SWITCH ||
            (choice.editable && !choice.active) ||
            (choice.can_clear && (!choice.active || !choice.has_selected_value)) ||
            (choice.has_previous_value && choice.has_selected_value &&
             choice.previous_value >= choice.selected_value) ||
            (choice.has_next_value && choice.has_selected_value &&
             choice.next_value <= choice.selected_value)) {
            return 0;
        }
    }

    return true;
}

static bool compose_geometry_cursor_valid(const WormUiComposeView* view) {
    return !view->cursor || compose_rect_valid(&view->cursor->bounds);
}

static bool compose_geometry_scene_elements_valid(const WormUiComposeView* view) {
    return compose_geometry_basic_elements_valid(view) &&
           compose_geometry_grid_elements_valid(view) &&
           compose_geometry_timing_elements_valid(view) &&
           compose_geometry_resource_elements_valid(view) &&
           compose_geometry_note_elements_valid(view) &&
           compose_geometry_waveform_elements_valid(view) &&
           compose_geometry_bmson_elements_valid(view) &&
           compose_geometry_bookmark_elements_valid(view) &&
           compose_geometry_beat_section_elements_valid(view) &&
           compose_geometry_audio_elements_valid(view) &&
           compose_geometry_hover_valid(view) &&
           compose_geometry_branch_elements_valid(view) &&
           compose_geometry_cursor_valid(view);
}

static bool compose_beat_fit_valid(const WormUiComposeView* view) {
    return !view->beat_fit_drag_active ||
           (view->beat_fit_chart_row != 0 && view->waveform_overview_sample_rate != 0 &&
            view->beat_fit_original_bpm_denominator != 0 &&
            view->beat_fit_proposed_bpm_denominator != 0 &&
            std::isfinite(view->beat_fit_target_y_pixels) &&
            std::isfinite(view->beat_fit_anchor_y_pixels) &&
            std::isfinite(view->beat_fit_beat_y_pixels) &&
            std::abs(view->beat_fit_target_y_pixels) <= 10000000.0 &&
            std::abs(view->beat_fit_anchor_y_pixels) <= 10000000.0 &&
            std::abs(view->beat_fit_beat_y_pixels) <= 10000000.0 &&
            view->beat_fit_target_sample_frame < view->waveform_overview_total_frames);
}

static bool compose_waveform_overview_valid(const WormUiComposeView* view) {
    return std::isfinite(view->waveform_overview_visible_begin_fraction) &&
           std::isfinite(view->waveform_overview_visible_end_fraction) &&
           view->waveform_overview_visible_begin_fraction >= 0.0 &&
           view->waveform_overview_visible_end_fraction <= 1.0 &&
           view->waveform_overview_visible_begin_fraction <=
               view->waveform_overview_visible_end_fraction &&
           (!view->waveform_beat_zero_visible ||
            (std::isfinite(view->waveform_beat_zero_fraction) &&
             view->waveform_beat_zero_fraction >= 0.0 &&
             view->waveform_beat_zero_fraction <= 1.0)) &&
           (!view->waveform_proposed_beat_zero_visible ||
            (view->waveform_offset_drag_active &&
             std::isfinite(view->waveform_proposed_beat_zero_fraction) &&
             view->waveform_proposed_beat_zero_fraction >= 0.0 &&
             view->waveform_proposed_beat_zero_fraction <= 1.0)) &&
           (!view->waveform_offset_drag_active ||
            view->waveform_proposed_audio_offset_denominator != 0) &&
           (view->waveform_overview_row_count == 0 ||
            (view->waveform_overview_total_frames != 0 &&
             view->waveform_overview_sample_rate != 0 &&
             view->waveform_audio_offset_denominator != 0));
}

static bool compose_bgm_bar_valid(const WormUiComposeView* view) {
    if (view->bgm_bar_row_count > WORM_UI_COMPOSE_BGM_BAR_ROW_CAPACITY ||
        view->bgm_bar_mark_count > WORM_UI_COMPOSE_BGM_BAR_MARK_CAPACITY ||
        (view->bgm_bar_row_count != 0 && !view->bgm_bar_rows) ||
        (view->bgm_bar_mark_count != 0 && !view->bgm_bar_marks))
        return false;
    if (view->bgm_bar_bookmark_count > WORM_UI_COMPOSE_BGM_BAR_BOOKMARK_CAPACITY ||
        (view->bgm_bar_bookmark_count != 0 && !view->bgm_bar_bookmarks))
        return false;
    if (view->bgm_bar_row_count == 0)
        return view->bgm_bar_mark_count == 0 && view->bgm_bar_bookmark_count == 0;
    if (!std::isfinite(view->bgm_bar_begin_fraction) || !std::isfinite(view->bgm_bar_end_fraction) ||
        view->bgm_bar_begin_fraction < 0.0 || view->bgm_bar_end_fraction > 1.0 ||
        view->bgm_bar_begin_fraction >= view->bgm_bar_end_fraction)
        return false;
    return std::all_of(view->bgm_bar_marks, view->bgm_bar_marks + view->bgm_bar_mark_count,
                       [](const WormUiComposeBgmBarMarkView& mark) {
                           return std::isfinite(mark.audio_fraction);
                       }) &&
           std::all_of(view->bgm_bar_bookmarks,
                       view->bgm_bar_bookmarks + view->bgm_bar_bookmark_count,
                       [](const WormUiComposeBgmBarBookmarkView& bookmark) {
                           return std::isfinite(bookmark.audio_fraction) && bookmark.label;
                       });
}

static bool compose_scene_arrays_present(const WormUiComposeView* view) {
    return (view->band_count == 0 || view->bands) && (view->lane_count == 0 || view->lanes) &&
           (view->grid_line_count == 0 || view->grid_lines) &&
           (view->proposed_grid_line_count == 0 || view->proposed_grid_lines) &&
           (view->timing_preview_rect_count == 0 || view->timing_preview_rects) &&
           (view->timing_preview_marker_count == 0 || view->timing_preview_markers) &&
           (view->timing_marker_count == 0 || view->timing_markers) &&
           (view->difference_count == 0 || view->differences) &&
           (view->hold_body_count == 0 || view->hold_bodies) &&
           (view->note_endpoint_count == 0 || view->note_endpoints) &&
           (view->bgm_lane_count == 0 || view->bgm_lanes) &&
           (view->bgm_event_count == 0 || view->bgm_events) &&
           (view->waveform_row_count == 0 || view->waveform_rows) &&
           (view->channel_waveform_row_count == 0 ||
            (view->channel_waveform_rows && view->channel_waveform_source_lane_id != 0 &&
             view->bgm_lane_count != 0)) &&
           (view->waveform_overview_row_count == 0 || view->waveform_overview_rows) &&
           (view->semantic_layer_count == 0 || view->semantic_layers) &&
           (view->lane_key_binding_count == 0 || view->lane_key_bindings) &&
           (view->branch_choice_count == 0 || view->branch_choices) &&
           (view->branch_row_count == 0 || view->branch_rows);
}

int worm_ui_nuklear_compose_view_valid(const WormUiComposeView* view) {
    return view && view->band_count <= WORM_UI_COMPOSE_BAND_CAPACITY &&
           (view->selected_bmson_bgm_event_id == 0 || view->selected_object_count == 1) &&
           (view->selected_bmson_note_event_id == 0 || view->selected_note_count == 1) &&
           ((view->selected_bmson_bgm_event_id == 0) ==
            (view->selected_bmson_source_lane_id == 0)) &&
           view->staged_bmson_marker_count <= 256 &&
           (view->staged_bmson_marker_count == 0 ||
            (view->staged_bmson_marker_playable_lanes != nullptr &&
             view->staged_bmson_marker_restarts != nullptr)) &&
           view->bmson_marker_draft_count <= view->staged_bmson_marker_count &&
           (view->bmson_marker_draft_count == 0 || view->bmson_marker_drafts != nullptr) &&
           view->lane_count <= WORM_UI_COMPOSE_LANE_CAPACITY &&
           view->grid_line_count <= WORM_UI_COMPOSE_GRID_CAPACITY &&
           view->proposed_grid_line_count <= WORM_UI_COMPOSE_GRID_CAPACITY &&
           view->timing_preview_rect_count <= WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY &&
           view->timing_preview_marker_count <= WORM_UI_TIMING_MARKER_CAPACITY &&
           (view->proposed_grid_line_count == 0 || view->waveform_offset_drag_active ||
            view->beat_fit_drag_active) &&
           ((view->timing_preview_rect_count == 0 && view->timing_preview_marker_count == 0) ||
            view->waveform_offset_drag_active || view->beat_fit_drag_active) &&
           compose_beat_fit_valid(view) &&
           view->timing_marker_count <= WORM_UI_TIMING_MARKER_CAPACITY &&
           view->difference_count <= WORM_UI_COMPOSE_DIFFERENCE_CAPACITY &&
           view->hold_body_count <= WORM_UI_COMPOSE_HOLD_CAPACITY &&
           view->note_endpoint_count <= WORM_UI_COMPOSE_NOTE_CAPACITY &&
           view->bgm_lane_count <= WORM_UI_COMPOSE_BGM_LANE_CAPACITY &&
           view->bgm_event_count <= WORM_UI_COMPOSE_BGM_EVENT_CAPACITY &&
           view->waveform_row_count <= WORM_UI_COMPOSE_WAVEFORM_ROW_CAPACITY &&
           view->channel_waveform_row_count <= WORM_UI_COMPOSE_CHANNEL_WAVEFORM_ROW_CAPACITY &&
           view->waveform_overview_row_count <= WORM_UI_COMPOSE_WAVEFORM_OVERVIEW_CAPACITY &&
           compose_waveform_overview_valid(view) &&
           compose_bgm_bar_valid(view) &&
           view->semantic_layer_count <= WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY &&
           view->lane_key_binding_count <= WORM_UI_COMPOSE_LANE_KEY_BINDING_CAPACITY &&
           view->branch_row_count <= WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY &&
           view->branch_choice_count <= WORM_UI_COMPOSE_BRANCH_CHOICE_CAPACITY &&
           view->branch_diagnostic_count <= WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY &&
           compose_scene_arrays_present(view) && compose_geometry_valid(view);
}

static void render_compose_waveform_overview(const ComposeOverviewContext& overview) {
    const auto& view = *overview.view;
    const auto bounds = overview.bounds;
    auto* canvas = overview.canvas;
    const auto background = wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::overview);
    if (!wormedit::ui::nuklear::begin_compose_overview(overview.context, canvas, view,
            bounds, background, nk_rgba(114, 222, 239, 190))) {
        nk_fill_rect(canvas, bounds, 0.0f,
                     wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::overview));
        const auto center = bounds.y + bounds.h * 0.5f;
        const auto half_height = bounds.h * 0.43f;
        for (size_t index = 0; index < view.waveform_overview_row_count; ++index) {
            const auto& row = view.waveform_overview_rows[index];
            const auto x = bounds.x + static_cast<float>(row.audio_fraction) * bounds.w;
            nk_stroke_line(canvas, x, center - row.absolute_peak * half_height,
                           x, center + row.absolute_peak * half_height, 1.0f,
                           nk_rgba(114, 222, 239, 190));
        }
        wormedit::ui::nuklear::end_compose_overview(overview.context, canvas, bounds);
    }
    const auto thumb_left = bounds.x +
        static_cast<float>(view.waveform_overview_visible_begin_fraction) * bounds.w;
    const auto thumb_right = bounds.x +
        static_cast<float>(view.waveform_overview_visible_end_fraction) * bounds.w;
    const auto thumb_width = std::min(bounds.w, std::max(6.0f, thumb_right - thumb_left));
    const auto thumb_x = std::min(thumb_left, bounds.x + bounds.w - thumb_width);
    const auto thumb = nk_rect(thumb_x, bounds.y + 1.0f, thumb_width, bounds.h - 2.0f);
    nk_fill_rect(canvas, thumb, 0.0f, nk_rgba(99, 173, 221, 55));
    nk_stroke_rect(canvas, thumb, 0.0f, 1.0f, nk_rgba(139, 210, 255, 180));
    if (const auto fraction = worm_ui_nuklear_overview_source_fraction(view)) {
        const auto x = std::min(bounds.x + static_cast<float>(*fraction) * bounds.w,
                                bounds.x + bounds.w - 1.0f);
        nk_stroke_line(canvas, x, bounds.y + 1.0f, x, bounds.y + bounds.h - 1.0f,
                       2.0f, nk_rgb(255, 110, 92));
    }
    if (view.waveform_beat_zero_visible) {
        const auto zero_x = std::min(bounds.x +
            static_cast<float>(view.waveform_beat_zero_fraction) * bounds.w,
            bounds.x + bounds.w - 1.0f);
        nk_stroke_line(canvas, zero_x, bounds.y, zero_x, bounds.y + bounds.h,
                       view.waveform_offset_drag_active ? 1.0f : 2.0f,
                       view.waveform_offset_drag_active ? nk_rgba(255, 207, 104, 110)
                                                        : nk_rgb(255, 207, 104));
    }
    if (view.waveform_proposed_beat_zero_visible) {
        const auto proposed_x = std::min(bounds.x +
            static_cast<float>(view.waveform_proposed_beat_zero_fraction) * bounds.w,
            bounds.x + bounds.w - 1.0f);
        nk_stroke_line(canvas, proposed_x, bounds.y, proposed_x, bounds.y + bounds.h,
                       2.0f, nk_rgb(255, 237, 143));
    }
}

void worm_ui_nuklear_render_compose_minimap(
    struct nk_context* context, const WormUiComposeView* view,
    WormUiWorkspaceAction* action, struct nk_rect bounds, bool window_focused) {
    if (!context || !view || !action) return;
    const bool available = view->visible && worm_ui_nuklear_compose_view_valid(view) &&
        view->scene_available && view->waveform_overview_row_count != 0 &&
        bounds.w > 1.0f && bounds.h > 1.0f;
    auto* canvas = nk_window_get_canvas(context);
    if (available && canvas)
        render_compose_waveform_overview({context, view, action, canvas, bounds});
    const auto& button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    const bool pressed = available && window_focused && button.clicked && button.down &&
        nk_input_is_mouse_click_down_in_rect(&context->input, NK_BUTTON_LEFT, bounds, nk_true);
    if (pressed) state.keyboard_focus = true;
    process_compose_overview_input({context, view, action, bounds, available, pressed,
        window_focused && available, state.waveform_beat_zero_drag_active});
}

namespace {

constexpr float bgm_bar_line_spacing_pixels = 5.0f;
constexpr float bgm_bar_label_spacing_pixels = 48.0f;
constexpr double bgm_bar_wheel_pan_fraction = 0.1;

struct BgmBarGeometry {
    struct nk_rect bounds;
    double begin;
    double end;
    [[nodiscard]] float x_of(double fraction) const {
        return bounds.x + static_cast<float>((fraction - begin) / (end - begin)) * bounds.w;
    }
    [[nodiscard]] double fraction_at(float x) const {
        return begin + std::clamp(static_cast<double>((x - bounds.x) / bounds.w), 0.0, 1.0) *
                           (end - begin);
    }
};

// Smallest 1-2-5 measure step whose marks keep `spacing` pixels apart.
std::uint32_t bgm_bar_measure_step(double pixels_per_measure, float spacing) {
    if (!(pixels_per_measure > 0.0)) return 1U << 30;
    const double needed = spacing / pixels_per_measure;
    std::uint32_t scale = 1;
    for (;;) {
        for (const std::uint32_t factor : {1U, 2U, 5U})
            if (static_cast<double>(factor) * scale >= needed) return factor * scale;
        if (scale > (1U << 27)) return 1U << 30;
        scale *= 10;
    }
}

// Average on-screen width of one measure, from the first and last published marks.
double bgm_bar_pixels_per_measure(const WormUiComposeView& view, const BgmBarGeometry& geometry) {
    if (view.bgm_bar_mark_count < 2) return 0.0;
    const auto& first = view.bgm_bar_marks[0];
    const auto& last = view.bgm_bar_marks[view.bgm_bar_mark_count - 1];
    const auto measures = static_cast<double>(last.measure) - first.measure;
    if (measures <= 0.0) return 0.0;
    return (last.audio_fraction - first.audio_fraction) / (geometry.end - geometry.begin) *
           geometry.bounds.w / measures;
}

void draw_bgm_bar_waveform(struct nk_command_buffer* canvas, const WormUiComposeView& view,
                           const BgmBarGeometry& geometry) {
    const auto center = geometry.bounds.y + geometry.bounds.h * 0.5f;
    const auto half_height = geometry.bounds.h * 0.43f;
    const auto width = std::max(1.0f, geometry.bounds.w / static_cast<float>(view.bgm_bar_row_count));
    const auto color = nk_rgba(114, 222, 239, 190);
    for (size_t index = 0; index < view.bgm_bar_row_count; ++index) {
        const auto& row = view.bgm_bar_rows[index];
        const auto x = geometry.x_of(row.audio_fraction);
        nk_stroke_line(canvas, x, center - row.absolute_peak * half_height,
                       x, center + row.absolute_peak * half_height, width, color);
    }
}

void draw_bgm_bar_grid(struct nk_context* context, struct nk_command_buffer* canvas,
                       const WormUiComposeView& view, const BgmBarGeometry& geometry) {
    const auto* font = context->style.font;
    const auto per_measure = bgm_bar_pixels_per_measure(view, geometry);
    const auto line_step = bgm_bar_measure_step(per_measure, bgm_bar_line_spacing_pixels);
    const auto label_step = bgm_bar_measure_step(per_measure, bgm_bar_label_spacing_pixels);
    const auto& bounds = geometry.bounds;
    for (size_t index = 0; index < view.bgm_bar_mark_count; ++index) {
        const auto& mark = view.bgm_bar_marks[index];
        if (mark.measure % line_step != 0) continue;
        const auto x = geometry.x_of(mark.audio_fraction);
        const bool labeled = font && mark.measure % label_step == 0;
        nk_stroke_line(canvas, x, bounds.y, x, bounds.y + (labeled ? bounds.h : bounds.h * 0.35f),
                       1.0f, nk_rgba(255, 255, 255, labeled ? 70 : 36));
        if (!labeled) continue;
        char text[16];
        const int length = std::snprintf(text, sizeof(text), "%u", mark.measure);
        if (length <= 0) continue;
        const auto width = font->width(font->userdata, font->height, text, length);
        if (x + 3.0f + width > bounds.x + bounds.w) continue;
        nk_draw_text(canvas, nk_rect(x + 3.0f, bounds.y + 1.0f, width, font->height), text, length,
                     font, nk_rgba(0, 0, 0, 0), nk_rgba(210, 220, 230, 200));
    }
}

// Flag on the top edge for each bookmark, with its label to the right when the next
// bookmark leaves room for it.
void draw_bgm_bar_bookmarks(struct nk_context* context, struct nk_command_buffer* canvas,
                            const WormUiComposeView& view, const BgmBarGeometry& geometry) {
    const auto& bounds = geometry.bounds;
    const auto* font = context->style.font;
    for (size_t index = 0; index < view.bgm_bar_bookmark_count; ++index) {
        const auto& bookmark = view.bgm_bar_bookmarks[index];
        const auto x = geometry.x_of(bookmark.audio_fraction);
        const auto color = bookmark.audio ? nk_rgb(124, 224, 168) : nk_rgb(255, 198, 92);
        nk_stroke_line(canvas, x, bounds.y, x, bounds.y + bounds.h, 1.0f, nk_rgba(color.r, color.g, color.b, 150));
        nk_fill_triangle(canvas, x, bounds.y + 7.0f, x + 7.0f, bounds.y, x, bounds.y, color);
        if (!font || bookmark.label[0] == '\0') continue;
        float limit = bounds.x + bounds.w;
        for (size_t other = 0; other < view.bgm_bar_bookmark_count; ++other) {
            const auto other_x = geometry.x_of(view.bgm_bar_bookmarks[other].audio_fraction);
            if (other_x > x + 1.0f) limit = std::min(limit, other_x);
        }
        const auto length = static_cast<int>(std::strlen(bookmark.label));
        const auto width = font->width(font->userdata, font->height, bookmark.label, length);
        if (x + 10.0f + width > limit - 2.0f) continue;
        nk_draw_text(canvas, nk_rect(x + 10.0f, bounds.y + bounds.h - font->height - 1.0f, width, font->height),
                     bookmark.label, length, font, nk_rgba(0, 0, 0, 0), color);
    }
}

void draw_bgm_bar_markers(struct nk_command_buffer* canvas, const WormUiComposeView& view,
                          const BgmBarGeometry& geometry) {
    const auto& bounds = geometry.bounds;
    const auto clamp_x = [&](double fraction) {
        return std::clamp(geometry.x_of(fraction), bounds.x, bounds.x + bounds.w - 1.0f);
    };
    const auto left = clamp_x(std::max(view.waveform_overview_visible_begin_fraction, geometry.begin));
    const auto right = clamp_x(std::min(view.waveform_overview_visible_end_fraction, geometry.end));
    if (view.waveform_overview_visible_end_fraction >= geometry.begin &&
        view.waveform_overview_visible_begin_fraction <= geometry.end) {
        const auto thumb = nk_rect(left, bounds.y + 1.0f, std::max(2.0f, right - left), bounds.h - 2.0f);
        nk_fill_rect(canvas, thumb, 0.0f, nk_rgba(99, 173, 221, 55));
        nk_stroke_rect(canvas, thumb, 0.0f, 1.0f, nk_rgba(139, 210, 255, 180));
    }
    if (const auto fraction = worm_ui_nuklear_overview_source_fraction(view);
        fraction && *fraction >= geometry.begin && *fraction <= geometry.end) {
        const auto x = clamp_x(*fraction);
        nk_stroke_line(canvas, x, bounds.y + 1.0f, x, bounds.y + bounds.h - 1.0f, 2.0f,
                       nk_rgb(255, 110, 92));
    }
    if (view.waveform_beat_zero_visible && view.waveform_beat_zero_fraction >= geometry.begin &&
        view.waveform_beat_zero_fraction <= geometry.end) {
        const auto x = clamp_x(view.waveform_beat_zero_fraction);
        nk_stroke_line(canvas, x, bounds.y, x, bounds.y + bounds.h,
                       view.waveform_offset_drag_active ? 1.0f : 2.0f,
                       view.waveform_offset_drag_active ? nk_rgba(255, 207, 104, 110)
                                                        : nk_rgb(255, 207, 104));
    }
}

void append_bgm_bar_input(WormUiWorkspaceAction* action, WormUiComposeInputKind kind,
                          double amount, double audio_fraction) {
    append_compose_input(action, kind, amount);
    if (action->compose_input_count == 0) return;
    auto& input = action->compose_inputs[action->compose_input_count - 1];
    if (input.kind == kind) input.audio_fraction = audio_fraction;
}

// Wheel zooms around the pointer, Shift+wheel or the horizontal wheel pans, a middle drag pans,
// and a middle click shows the whole song again.
void process_bgm_bar_view_input(struct nk_context* context, WormUiWorkspaceAction* action,
                                const BgmBarGeometry& geometry) {
    static bool middle_dragged = false;
    auto& input = context->input;
    const auto span = geometry.end - geometry.begin;
    if (nk_input_is_mouse_released(&input, NK_BUTTON_MIDDLE)) {
        if (!middle_dragged && nk_input_is_mouse_hovering_rect(&input, geometry.bounds))
            append_bgm_bar_input(action, WORM_UI_COMPOSE_INPUT_BGM_BAR_RESET, 0.0, 0.0);
        middle_dragged = false;
    }
    if (!nk_input_is_mouse_hovering_rect(&input, geometry.bounds) &&
        !(middle_dragged && input.mouse.buttons[NK_BUTTON_MIDDLE].down))
        return;
    if (input.mouse.buttons[NK_BUTTON_MIDDLE].down && input.mouse.delta.x != 0.0f) {
        middle_dragged = true;
        append_bgm_bar_input(action, WORM_UI_COMPOSE_INPUT_BGM_BAR_PAN,
                             -static_cast<double>(input.mouse.delta.x) / geometry.bounds.w * span, 0.0);
    }
    const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    const auto wheel_x = static_cast<double>(input.mouse.scroll_delta.x);
    const auto wheel_y = static_cast<double>(input.mouse.scroll_delta.y);
    if (shift || wheel_x != 0.0) {
        const auto amount = wheel_x != 0.0 ? wheel_x : wheel_y;
        if (amount != 0.0)
            append_bgm_bar_input(action, WORM_UI_COMPOSE_INPUT_BGM_BAR_PAN,
                                 -amount * span * bgm_bar_wheel_pan_fraction, 0.0);
    } else if (wheel_y != 0.0) {
        append_bgm_bar_input(action, WORM_UI_COMPOSE_INPUT_BGM_BAR_ZOOM, wheel_y,
                             geometry.fraction_at(input.mouse.pos.x));
    }
}

} // namespace

void worm_ui_nuklear_render_compose_bgm_bar(
    struct nk_context* context, const WormUiComposeView* view,
    WormUiWorkspaceAction* action, struct nk_rect bounds, bool window_focused) {
    if (!context || !view || !action) return;
    const bool available = view->visible && worm_ui_nuklear_compose_view_valid(view) &&
        view->scene_available && view->bgm_bar_row_count != 0 && bounds.w > 1.0f && bounds.h > 1.0f;
    auto* canvas = nk_window_get_canvas(context);
    const BgmBarGeometry geometry{bounds, view->bgm_bar_begin_fraction, view->bgm_bar_end_fraction};
    if (available && canvas) {
        nk_fill_rect(canvas, bounds, 0.0f,
                     wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::overview));
        draw_bgm_bar_waveform(canvas, *view, geometry);
        draw_bgm_bar_grid(context, canvas, *view, geometry);
        draw_bgm_bar_bookmarks(context, canvas, *view, geometry);
        draw_bgm_bar_markers(canvas, *view, geometry);
        if (window_focused && nk_input_is_mouse_hovering_rect(&context->input, bounds))
            nk_tooltip(context, "Wheel: zoom   Shift+wheel or middle-drag: pan   "
                                "Middle-click: whole song");
    }
    const auto& button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    const bool pressed = available && window_focused && button.clicked && button.down &&
        nk_input_is_mouse_click_down_in_rect(&context->input, NK_BUTTON_LEFT, bounds, nk_true);
    if (pressed) state.keyboard_focus = true;
    if (available && window_focused) process_bgm_bar_view_input(context, action, geometry);
    process_compose_overview_input({context, view, action, bounds, available, pressed,
        window_focused && available, state.waveform_beat_zero_drag_active,
        view->bgm_bar_begin_fraction, view->bgm_bar_end_fraction});
}

struct ComposePanelRenderContext {
    struct nk_context* context;
    const WormUiComposeView* view;
    const WormUiTimingView* timing_view;
    WormUiWorkspaceAction* action;
    bool window_focused;
    bool ime_composition_active;
    const worm_ui_nuklear_pointer_click_t* double_clicks;
    std::size_t double_click_count;
    std::size_t shortcut_event_count;
};

static auto compose_inline_timing_popup_bounds(nk_context* context, struct nk_rect viewport)
    -> struct nk_rect {
    constexpr float popup_width = 196.0f;
    constexpr float popup_height = 75.0f;
    const auto popup_x = std::clamp(state.inline_timing_anchor_screen_x,
                                    viewport.x, viewport.x + std::max(0.0f, viewport.w - popup_width));
    const auto popup_y = std::clamp(state.inline_timing_anchor_screen_y - 24.0f,
                                    viewport.y,
                                    viewport.y + std::max(0.0f, viewport.h - popup_height));
    const auto parent_clip = nk_window_get_content_region(context);
    return nk_rect(popup_x - parent_clip.x, popup_y - parent_clip.y,
                                    popup_width, popup_height);

}

static void dismiss_compose_inline_timing_popup(nk_context* context, struct nk_rect viewport) {
    wormedit::ui::chrome::dismiss_popup(
        context, "Compose timing value", compose_inline_timing_popup_bounds(context, viewport));
    state.inline_timing_popup_close_pending = false;
    state.inline_timing_open = false;
    state.inline_timing_focus_pending = false;
}

static void render_compose_inline_timing_editor(const ComposePanelRenderContext& panel,
                                                struct nk_rect viewport) {
    if (state.inline_timing_popup_close_pending) {
        dismiss_compose_inline_timing_popup(panel.context, viewport);
        return;
    }
    if (!state.inline_timing_open) return;
    if (!panel.window_focused) {
        worm_ui_nuklear_inline_timing_cancel(state, *panel.action);
        dismiss_compose_inline_timing_popup(panel.context, viewport);
        return;
    }
    const auto& timing = *panel.timing_view;
    if (!timing.stage_active) {
        // The opening double-click reaches the application after this frame.
        if (panel.action->timing_stage_action == WORM_UI_TIMING_STAGE_BEGIN) return;
        dismiss_compose_inline_timing_popup(panel.context, viewport);
        return;
    }
    if (timing.chart_id != state.inline_timing_chart_id ||
        timing.document_revision != state.inline_timing_revision ||
        timing.stage_existing_object_id != state.inline_timing_object_id ||
        timing.stage_lane != state.inline_timing_lane) {
        dismiss_compose_inline_timing_popup(panel.context, viewport);
        return;
    }
    if (state.inline_timing_focus_pending && state.inline_timing_error[0] == '\0')
        worm_ui_nuklear_inline_timing_initial_text(
            timing, state.inline_timing_text.data(), state.inline_timing_text.size());

    if (!nk_popup_begin(panel.context, NK_POPUP_STATIC, "Compose timing value",
                        NK_WINDOW_NO_SCROLLBAR,
                        compose_inline_timing_popup_bounds(panel.context, viewport))) {
        worm_ui_nuklear_inline_timing_cancel(state, *panel.action);
        return;
    }
    nk_layout_row_dynamic(panel.context, 25.0f, 1);
    const auto edit_flags = wormedit::ui::chrome::autofocus_text_field(
        panel.context, static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER |
                           NK_EDIT_AUTO_SELECT,
        state.inline_timing_text.data(), static_cast<int>(state.inline_timing_text.size()),
        nk_filter_default, state.inline_timing_focus_pending);
    worm_ui_nuklear_inline_timing_update(state, timing, *panel.action,
                                        (edit_flags & NK_EDIT_COMMITTED) != 0,
                                        (edit_flags & NK_EDIT_DEACTIVATED) != 0);
    if (!state.inline_timing_open) {
        nk_popup_close(panel.context);
        state.inline_timing_popup_close_pending = false;
    }
    nk_layout_row_dynamic(panel.context, 17.0f, 1);
    nk_label(panel.context, state.inline_timing_error[0] ? state.inline_timing_error.data()
                                                    : "Enter to apply · Esc to cancel",
             NK_TEXT_LEFT);
    nk_popup_end(panel.context);
}

static void discard_occluded_compose_input(const ComposePanelRenderContext& panel) {
    // Release previously held editor keys; Options owns new presses and wheel input.
    std::size_t releases{};
    for (std::size_t index = 0; index < state.key_event_count; ++index) {
        if (!state.key_events[index].down) state.key_events[releases++] = state.key_events[index];
    }
    state.key_event_count = releases;
    if (releases != 0)
        process_compose_key_events(panel.context, panel.view, panel.action,
            panel.window_focused, panel.ime_composition_active);
    discard_compose_frame_input();
}

static void render_compose_editor_canvas(const ComposePanelRenderContext& panel,
                                        bool inspector_drawn, float inspector_width) {
    auto* context = panel.context;
    const auto* view = panel.view;
    const auto* timing_view = panel.timing_view;
    auto* action = panel.action;
    const auto window_focused = panel.window_focused;
    const auto ime_composition_active = panel.ime_composition_active;
    const auto* double_clicks = panel.double_clicks;
    const auto double_click_count = panel.double_click_count;
    const auto shortcut_event_count = panel.shortcut_event_count;

    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, compose_editor_status_hint(view, timing_view), NK_TEXT_LEFT);

    const auto group_bounds = nk_window_get_content_region(context);
    const auto surface_height = std::max(32.0f, group_bounds.h - 24.0f);
    nk_layout_space_begin(context, NK_DYNAMIC, surface_height, 1);
    nk_layout_space_push(context, nk_rect(0.0f, 0.0f, 1.0f, 1.0f));
    struct nk_rect viewport{};
    const auto widget_state = nk_widget(&viewport, context);
    nk_layout_space_end(context);
    if (widget_state == NK_WIDGET_INVALID || viewport.w <= 1.0f || viewport.h <= 1.0f)
        return;

    auto* canvas = nk_window_get_canvas(context);
    if (!canvas)
        return;

    const auto editor_input_right = inspector_drawn
        ? viewport.x + viewport.w - inspector_width - 8.0f
        : viewport.x + viewport.w;
    if (view->input_occluded) {
        discard_occluded_compose_input(panel);
        draw_compose_scene_canvas(context, view, canvas, viewport);
        return;
    }
    const auto pointer_focus = update_compose_pointer_focus(
        {context, view, action, viewport, nk_rect(0, 0, 0, 0), editor_input_right, false,
         window_focused});
    const auto pointer_inside = pointer_focus.pointer_inside;
    const auto pointer_clicked = pointer_focus.pointer_clicked;
    const auto& left_button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    process_compose_key_events(context, view, action, window_focused, ime_composition_active);
    if (state.go_to_measure_open) {
        draw_compose_scene_canvas(context, view, canvas, viewport);
        return;
    }


    const ComposeViewportInputContext viewport_input{
        context, view, action, viewport, editor_input_right, pointer_inside, window_focused};
    const auto pointer = collect_compose_viewport_inputs(viewport_input);
    const auto pointer_x = pointer.x;
    const auto pointer_y = pointer.y;
    const auto note_field_left = pointer.note_field_left;
    const auto note_field_right = pointer.note_field_right;
    const auto pointer_in_note_field = pointer.in_note_field;
    const bool alt_down = nk_input_is_key_down(&context->input, NK_KEY_ALT) != 0;
    const bool left_down = context->input.mouse.buttons[NK_BUTTON_LEFT].down != 0;
    const bool left_released = nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT);
    const auto pointer_modifiers = static_cast<uint16_t>(SDL_GetModState());
    constexpr uint16_t any_pointer_modifier = SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT |
                                              SDL_KMOD_GUI | SDL_KMOD_MODE | SDL_KMOD_LEVEL5;
    const auto pointer_started_in_view = left_button.clicked != 0 &&
                                         left_button.clicked_pos.x >= viewport.x &&
                                         left_button.clicked_pos.x < editor_input_right &&
                                         left_button.clicked_pos.y >= viewport.y &&
                                         left_button.clicked_pos.y <= viewport.y + viewport.h;
    const auto pointer_started_x = (static_cast<double>(left_button.clicked_pos.x) - viewport.x) *
                                   view->viewport_width_pixels / viewport.w;
    const auto pointer_started_y = (static_cast<double>(left_button.clicked_pos.y) - viewport.y) *
                                   view->viewport_height_pixels / viewport.h;
    const auto pointer_started_in_note_field =
        pointer_started_in_view && pointer_started_x >= note_field_left &&
        pointer_started_x < note_field_right;
    const auto clamp_paint_x = [&](double value) {
        return std::clamp(value, note_field_left, note_field_right);
    };
    const auto clamp_paint_y = [&](double value) {
        return std::clamp(value, 0.0, view->viewport_height_pixels);
    };
    const auto paint_anchor_x =
        clamp_paint_x((static_cast<double>(left_button.clicked_pos.x) - viewport.x) *
                      view->viewport_width_pixels / viewport.w);
    const auto paint_anchor_y =
        clamp_paint_y((static_cast<double>(left_button.clicked_pos.y) - viewport.y) *
                      view->viewport_height_pixels / viewport.h);
    const auto paint_pointer_x = clamp_paint_x(pointer_x);
    const auto paint_pointer_y = clamp_paint_y(pointer_y);
    const ComposeBeatFitInput beat_fit_input{
        context, view, action, viewport, pointer_y, pointer_started_x, pointer_started_y,
        pointer_modifiers, any_pointer_modifier, shortcut_event_count, window_focused,
        pointer_started_in_view, left_down, left_released};
    const bool beat_fit_pointer_consumed = process_compose_beat_fit_pointer(beat_fit_input);
    ComposePointerEventContext pointer_event{
        context, view, timing_view, action, viewport, editor_input_right, pointer_x, pointer_y,
        note_field_left, note_field_right, pointer_started_x, pointer_started_y,
        paint_anchor_x, paint_anchor_y, paint_pointer_x, paint_pointer_y,
        left_button.clicked_pos.x, left_button.clicked_pos.y,
        pointer_modifiers, any_pointer_modifier, shortcut_event_count, window_focused,
        pointer_inside, pointer_clicked, pointer_in_note_field,
        pointer_started_in_view, pointer_started_in_note_field,
        left_down, left_released, alt_down, false, false};
    if (!state.inline_timing_open && !state.timing_marker_candidate_active)
        update_timing_stage_drag(pointer_event);
    update_kiai_span_drag(pointer_event);
    update_recipe_drag(pointer_event);
    const auto compose_pointer_captured =
        state.inline_timing_open || state.note_paint_pointer_active ||
        state.bmson_razor_pointer_active ||
        state.bmson_marker_move_active || state.semantic_layer_paint_pointer_active ||
        state.pointer_hold_active || state.lane_hold_active || state.note_move_active ||
        state.area_selection_active || state.timing_stage_drag_active || state.kiai_drag_active || state.recipe_drag_active ||
        state.timing_marker_candidate_active || state.timing_marker_drag_active ||
        state.waveform_beat_zero_drag_active || state.waveform_overview_drag_active ||
        beat_fit_pointer_consumed;
    action->compose_has_pointer_hover = !state.action_overflowed && window_focused &&
                                        view->scene_available && pointer_in_note_field &&
                                        !compose_pointer_captured;
    if (action->compose_has_pointer_hover) {
        action->compose_pointer_hover_x_pixels = pointer_x;
        action->compose_pointer_hover_y_pixels = pointer_y;
    }
    const bool compose_note_double_click_in_view = process_compose_double_clicks(
        {view, timing_view, action, double_clicks, double_click_count, viewport,
         editor_input_right, note_field_left, note_field_right},
        compose_pointer_captured, window_focused);
    pointer_event.compose_pointer_captured = compose_pointer_captured;
    pointer_event.compose_note_double_click_in_view = compose_note_double_click_in_view;
    bool paint_pointer_consumed =
        state.inline_timing_open || state.note_paint_pointer_active ||
        beat_fit_pointer_consumed ||
        state.bmson_razor_pointer_active || state.bmson_marker_move_candidate_active ||
        state.bmson_marker_move_active || state.semantic_layer_paint_pointer_active ||
        compose_action_has_cancel(action);
    paint_pointer_consumed = process_compose_timing_marker_pointer(pointer_event,
                                                                    paint_pointer_consumed) ||
                             paint_pointer_consumed;
    paint_pointer_consumed = begin_compose_resource_pointer(pointer_event, paint_pointer_consumed) ||
                             paint_pointer_consumed;
    paint_pointer_consumed = process_bmson_razor_pointer(pointer_event, paint_pointer_consumed);
    paint_pointer_consumed = process_bmson_marker_move_pointer(pointer_event, paint_pointer_consumed);
    if (!process_semantic_layer_paint_pointer(pointer_event, paint_pointer_consumed))
        process_note_paint_pointer(pointer_event, paint_pointer_consumed);

    process_compose_pointer_actions(pointer_event, paint_pointer_consumed);
    process_compose_drag_gestures(pointer_event, paint_pointer_consumed);

    draw_compose_scene_canvas(context, view, canvas, viewport);
    render_compose_inline_timing_editor(panel, viewport);
}

static void close_go_to_measure(nk_context* context) {
    nk_edit_unfocus(context);
    state.go_to_measure_open = false;
    state.go_to_measure_focus_pending = false;
    state.go_to_measure_cancel_pending = false;
    state.keyboard_focus = true;
    nk_popup_close(context);
}

static void confirm_go_to_measure(const WormUiComposeView& view,
                                  WormUiWorkspaceAction& action, nk_context* context) {
    const auto number = worm_ui_nuklear_compose_measure_number(view,
                                                              state.go_to_measure_text.data());
    if (!number) {
        std::snprintf(state.go_to_measure_error.data(), state.go_to_measure_error.size(),
                      view.bms_measure_labels ? "Enter a measure from 000 to 999"
                                              : "Enter an existing measure number");
        state.go_to_measure_focus_pending = true;
        return;
    }
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action.compose_action = WORM_UI_COMPOSE_ACTION_GO_TO_MEASURE;
    action.compose_measure_number = *number;
    action.compose_input_chart_id = view.chart_id;
    action.compose_input_document_revision = view.document_revision;
    close_go_to_measure(context);
}

static void render_go_to_measure(nk_context* context, const WormUiComposeView& view,
                                 WormUiWorkspaceAction& action, bool window_focused) {
    if (!state.go_to_measure_open) return;
    if (!window_focused || view.chart_id != state.go_to_measure_chart_id ||
        view.document_revision != state.go_to_measure_revision) {
        state.go_to_measure_cancel_pending = true;
    }
    const auto region = nk_window_get_content_region(context);
    const auto parent = nk_window_get_bounds(context);
    const auto width = std::min(320.0f, std::max(160.0f, parent.w - 24.0f));
    const auto bounds = nk_rect(parent.x + (parent.w - width) * 0.5f - region.x,
                                parent.y + (parent.h - 120.0f) * 0.5f - region.y,
                                width, 120.0f);
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "Go to measure",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, bounds)) {
        state.go_to_measure_open = false;
        state.go_to_measure_focus_pending = false;
        return;
    }
    if (state.go_to_measure_cancel_pending) {
        close_go_to_measure(context);
        nk_popup_end(context);
        return;
    }
    nk_layout_row_dynamic(context, 20, 1);
    if (view.bms_measure_labels)
        nk_label(context, "Measure #000-#999", NK_TEXT_LEFT);
    else
        nk_labelf(context, NK_TEXT_LEFT, "Measure M1-M%llu",
                  static_cast<unsigned long long>(view.compose_measure_count));
    nk_layout_row_dynamic(context, 28, 1);
    if (state.go_to_measure_focus_pending) {
        nk_edit_focus(context, NK_EDIT_ALWAYS_INSERT_MODE);
        state.go_to_measure_focus_pending = false;
    }
    const auto flags = nk_edit_string_zero_terminated(
        context, static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER,
        state.go_to_measure_text.data(), static_cast<int>(state.go_to_measure_text.size()),
        nk_filter_decimal);
    if ((flags & NK_EDIT_COMMITTED) != 0) confirm_go_to_measure(view, action, context);
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, state.go_to_measure_error[0] ? state.go_to_measure_error.data()
                                                : "Enter to go · Esc to cancel", NK_TEXT_LEFT);
    nk_popup_end(context);
}

void worm_ui_nuklear_render_compose_panel(struct nk_context* context, const WormUiComposeView* view,
                                          const WormUiTimingView* timing_view,
                                          WormUiWorkspaceAction* action, bool window_focused,
                                          bool ime_composition_active,
                                          const worm_ui_nuklear_pointer_click_t* double_clicks,
                                          std::size_t double_click_count,
                                          std::size_t shortcut_event_count) {
    if (!context || !view || !timing_view || !action || !view->visible ||
        (double_click_count != 0 && !double_clicks)) {
        return;
    }
    render_compose_toolbar({context, view, action});
    const ComposePanelRenderContext panel{context, view, timing_view, action, window_focused,
                                         ime_composition_active, double_clicks,
                                         double_click_count, shortcut_event_count};

    const auto compose_body_bounds = nk_window_get_content_region(context);
    const auto body_top = nk_widget_position(context).y;
    const auto compose_body_height = std::max(
        64.0f, compose_body_bounds.y + compose_body_bounds.h - body_top -
                   context->style.window.spacing.y);
    const auto inspector_drawn = state.inspector_visible && compose_body_bounds.w >= 540.0f &&
        wormedit::ui::compose_inspector_available(compose_mode_of(*view));
    const auto inspector_width = inspector_drawn
                                     ? std::clamp(compose_body_bounds.w * 0.24f, 280.0f, 380.0f)
                                     : 0.0f;
    const auto editor_width = compose_body_bounds.w;
    const auto palette_drawn =
        wormedit::ui::compose_note_palette_visible(compose_mode_of(*view)) &&
        compose_note_palette_has_buttons(*view);
    constexpr float palette_width = 36.0f;
    const auto editor_left = palette_drawn ? palette_width : 0.0f;
    nk_layout_space_begin(context, NK_STATIC, compose_body_height,
                          1 + (inspector_drawn ? 1 : 0) + (palette_drawn ? 1 : 0));
    if (palette_drawn) {
        nk_layout_space_push(context, nk_rect(0.0f, 0.0f, palette_width, compose_body_height));
        if (nk_group_begin(context, "Compose note palette", NK_WINDOW_NO_SCROLLBAR)) {
            render_compose_note_palette_column({context, view, action});
            nk_group_end(context);
        }
    }
    nk_layout_space_push(context, nk_rect(editor_left, 0.0f, editor_width - editor_left,
                                          compose_body_height));
    if (nk_group_begin(context, "Compose editor", NK_WINDOW_NO_SCROLLBAR)) {
    render_compose_editor_canvas(panel, inspector_drawn, inspector_width);
    nk_group_end(context);
    }
    if (inspector_drawn) {
        nk_layout_space_push(context, nk_rect(editor_width - inspector_width - 8.0f, 8.0f,
                                              inspector_width, compose_body_height - 16.0f));
        if (nk_group_begin(context, "Compose inspector", NK_WINDOW_BORDER)) {

    const ComposeInspectorContext inspector{context, view, timing_view, action};
    render_compose_note_label_editor(inspector);
    render_compose_label_search(inspector);
    render_compose_quantize_section(inspector);
    render_compose_transform_section(inspector);
    render_compose_measure_section(inspector);

    const auto visible_layer_count = view->semantic_layer_count;
    const bool semantic_target_active = semantic_layer_target_active(view);
    if (render_semantic_layer_palette_panel(inspector)) {
        render_semantic_layer_selected_panel(inspector, visible_layer_count);
        render_semantic_layer_membership_buttons(inspector, semantic_target_active);
        render_semantic_layer_target_buttons(inspector, semantic_target_active);
        nk_tree_pop(context);
    }

    render_compose_timing_inspector(inspector);

    worm_ui_nuklear_render_bms_branch_panel(context, view, action);
    nk_group_end(context);
        }
    }
    nk_layout_space_end(context);
    render_go_to_measure(context, *view, *action, window_focused);
}

bool worm_ui_nuklear_compose_lane_focused(const WormUiComposeView* view, bool compose_tab_active,
                                          bool window_focused) {
    return compose_tab_active && worm_ui_nuklear_compose_canvas_input_available(
        view, window_focused, state.inline_timing_open || state.go_to_measure_open, false) &&
        view->cursor && view->cursor->lane_id != 0;
}

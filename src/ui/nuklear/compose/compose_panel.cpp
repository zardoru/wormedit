#include "ui/nuklear/compose/compose_input_queries.h"
#include "ui/nuklear/compose/compose_gesture_registry.h"
#include "ui/nuklear/compose/compose_note_menu.h"
#include "ui/nuklear/compose/compose_area_menu.h"
#include "ui/nuklear/compose/compose_recipe_menu.h"
#include "ui/nuklear/compose/compose_panel.h"
#include "ui/nuklear/render/theme.h"
#include "ui/nuklear/app/sdl_backend.h"
#include "sys/compose/compose_scroll_geometry.h"
#include "ui/nuklear/project/bms_branch_panel.h"
#include "ui/nuklear/compose/compose_markers.h"
#include "ui/nuklear/compose/compose_note_label_inline.h"
#include "ui/nuklear/compose/compose_canvas.h"
#include "ui/nuklear/render/arrow_glyph.h"
#include "ui/nuklear/compose/compose_toolbar.h"
#include "ui/nuklear/compose/compose_inspector.h"
#include "ui/nuklear/compose/compose_input_panel.h"
#include "ui/nuklear/compose/compose_input_internal.h"
#include "ui/nuklear/compose/compose_panel_internal.h"
#include "ui/nuklear/compose/compose_resources.h"
#include "ui/nuklear/compose/compose_sounds_panel.h"
#include "ui/nuklear/compose/compose_bga_panel.h"
#include "ui/nuklear/compose/compose_measures_panel.h"
#include "ui/nuklear/compose/compose_layers_panel.h"
#include "ui/nuklear/compose/compose_state.h"
#include "ui/nuklear/compose/compose_recipe_form.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/timing/timing_stage_editor.h"

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
#include <string_view>
#include <vector>

void worm_ui_nuklear_reset_compose_panel() {
    reset_compose_quantize_popup();
    worm_ui_nuklear_reset_compose_markers();
    worm_ui_nuklear_reset_compose_state();
}

static bool compose_selection_mode_valid(WormUiComposeSelectionMode mode) {
    return mode == WORM_UI_COMPOSE_SELECTION_REPLACE || mode == WORM_UI_COMPOSE_SELECTION_ADD ||
           mode == WORM_UI_COMPOSE_SELECTION_TOGGLE;
}

static bool compose_bgm_scroll_valid(const wormedit::ui::compose_view_t* view) {
    return std::isfinite(view->bgm_horizontal_scroll_pixels) &&
           std::isfinite(view->bgm_maximum_horizontal_scroll_pixels) &&
           view->bgm_horizontal_scroll_pixels >= 0.0 &&
           view->bgm_maximum_horizontal_scroll_pixels >= view->bgm_horizontal_scroll_pixels;
}

static bool compose_geometry_dimensions_valid(const wormedit::ui::compose_view_t* view) {
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

static bool compose_geometry_overlay_valid(const wormedit::ui::compose_view_t* view) {
    const auto stage_valid = !view->timing_stage_active ||
        (std::isfinite(view->timing_stage_y_pixels) && view->timing_stage_y_pixels >= 0.0 &&
         view->timing_stage_y_pixels <= view->viewport_height_pixels &&
         view->timing_stage_lane >= wormedit::timing_lane_kind_t::bpm &&
         view->timing_stage_lane <= wormedit::timing_lane_kind_t::anchor);
    const auto playhead_valid = !view->playhead_visible ||
        (std::isfinite(view->playhead_y_pixels) && view->playhead_y_pixels >= 0.0 &&
         view->playhead_y_pixels <= view->viewport_height_pixels);
    const auto selection_valid = !view->area_selection_preview_visible ||
        (view->area_selection_preview_bounds.is_valid() &&
         compose_selection_mode_valid(view->area_selection_preview_mode));
    const auto space_valid = !view->space_range_preview_visible ||
        (view->space_range_preview_bounds.is_valid() &&
         compose_selection_mode_valid(view->space_range_preview_mode) &&
         std::isfinite(view->space_range_anchor_y_pixels) &&
         std::isfinite(view->space_range_tail_y_pixels) &&
         view->space_range_anchor_y_pixels >= 0.0 &&
         view->space_range_anchor_y_pixels <= view->viewport_height_pixels &&
         view->space_range_tail_y_pixels >= 0.0 &&
         view->space_range_tail_y_pixels <= view->viewport_height_pixels);
    return stage_valid && playhead_valid && selection_valid && space_valid;
}

static bool compose_geometry_counts_valid(const wormedit::ui::compose_view_t* view) {
    return view->resource_total_count >= view->resources.size() &&
           view->semantic_layer_first_row <= view->semantic_layer_total_count &&
           view->semantic_layers.size() <=
               view->semantic_layer_total_count - view->semantic_layer_first_row;

}


static bool compose_geometry_scene_elements_valid(const wormedit::ui::compose_view_t* view);

static bool compose_geometry_valid(const wormedit::ui::compose_view_t* view) {
    return view && compose_geometry_dimensions_valid(view) &&
           compose_geometry_overlay_valid(view) && compose_geometry_counts_valid(view) &&
           compose_geometry_scene_elements_valid(view);
}

static bool compose_geometry_basic_elements_valid(const wormedit::ui::compose_view_t* view) {    for (size_t index = 0; index < view->bands.size(); ++index) {
        if (!view->bands[index].bounds.is_valid())
            return 0;
    }
    for (size_t index = 0; index < view->lanes.size(); ++index) {
        if (!view->lanes[index].bounds.is_valid())
            return 0;
    }

    return true;
}

static bool compose_geometry_grid_elements_valid(const wormedit::ui::compose_view_t* view) {    for (size_t index = 0; index < view->grid_lines.size(); ++index) {
        if (!std::isfinite(view->grid_lines[index].y_pixels) ||
            std::abs(view->grid_lines[index].y_pixels) > 10000000.0)
            return 0;
    }
    for (size_t index = 0; index < view->proposed_grid_lines.size(); ++index) {
        if (!std::isfinite(view->proposed_grid_lines[index].y_pixels) ||
            view->proposed_grid_lines[index].y_pixels < 0.0 ||
            view->proposed_grid_lines[index].y_pixels > view->viewport_height_pixels)
            return 0;
    }

    return true;
}

static bool compose_dead_spans_valid(const wormedit::ui::compose_view_t* view) {
    for (size_t index = 0; index < view->dead_spans.size(); ++index) {
        const auto& span = view->dead_spans[index];
        if (!span.bounds.is_valid() || (span.kind != wormedit::timing_lane_kind_t::stop &&
            span.kind != wormedit::timing_lane_kind_t::delay &&
            span.kind != wormedit::timing_lane_kind_t::warp)) return false;
    }
    return true;
}

static bool compose_kiai_and_sv_valid(const wormedit::ui::compose_view_t* view) {
    for (size_t index = 0; index < view->kiai_spans.size(); ++index) {
        const auto& span = view->kiai_spans[index];
        if (!span.bounds.is_valid() || span.head_object_id.value == 0 ||
            span.end_row <= span.begin_row) return false;
    }
    for (size_t index = 0; index < view->sv_graph_points.size(); ++index) {
        const auto& point = view->sv_graph_points[index];
        if (!std::isfinite(point.x_fraction) || point.x_fraction < 0.0 || point.x_fraction > 1.0 ||
            !std::isfinite(point.y_pixels)) return false;
    }
    return true;
}

static bool compose_speed_spans_valid(const wormedit::ui::compose_view_t* view) {
    for (size_t index = 0; index < view->speed_spans.size(); ++index) {
        const auto& span = view->speed_spans[index];
        if (!span.bounds.is_valid() || span.object_id.value == 0 ||
            !std::isfinite(span.multiplier) || !std::isfinite(span.transition_length)) return false;
    }
    return true;
}

static bool compose_recipe_areas_valid(const wormedit::ui::compose_view_t* view) {
    for (size_t index = 0; index < view->recipe_areas.size(); ++index) {
        const auto& area = view->recipe_areas[index];
        if (!area.bounds.is_valid() || area.recipe_id.value == 0 ||
            (area.lane != wormedit::timing_lane_kind_t::scroll && area.lane != wormedit::timing_lane_kind_t::speed) ||
            area.graph_first > view->recipe_graph_points.size() ||
            area.graph_count > view->recipe_graph_points.size() - area.graph_first) return false;
    }
    for (size_t index = 0; index < view->recipe_graph_points.size(); ++index) {
        const auto& point = view->recipe_graph_points[index];
        if (!std::isfinite(point.x_fraction) || point.x_fraction < 0.0 || point.x_fraction > 1.0 ||
            !std::isfinite(point.y_pixels)) return false;
    }
    return true;
}

static bool compose_geometry_timing_elements_valid(const wormedit::ui::compose_view_t* view) {
    if (!compose_dead_spans_valid(view) || !compose_kiai_and_sv_valid(view) ||
        !compose_recipe_areas_valid(view) || !compose_speed_spans_valid(view)) return 0;
    for (size_t index = 0; index < view->timing_preview_rects.size(); ++index) {
        if (!view->timing_preview_rects[index].bounds.is_valid() ||
            view->timing_preview_rects[index].kind > wormedit::ui::compose_timing_preview_kind_t::hold_body)
            return 0;
    }
    for (size_t index = 0; index < view->timing_preview_markers.size(); ++index) {
        const auto& marker = view->timing_preview_markers[index];
        if (!std::isfinite(marker.marker_y_pixels) || marker.marker_y_pixels < 0.0 ||
            marker.marker_y_pixels > view->viewport_height_pixels ||
            marker.lane < wormedit::timing_lane_kind_t::bpm || marker.lane > wormedit::timing_lane_kind_t::anchor)
            return 0;
    }
    for (size_t index = 0; index < view->timing_markers.size(); ++index) {
        const auto& marker = view->timing_markers[index];
        const auto missing_initial = marker.fixed_initial &&
            marker.lane == wormedit::timing_lane_kind_t::bpm && marker.chart_row == 0;
        if ((marker.object_id.value == 0 && !missing_initial) || !std::isfinite(marker.marker_y_pixels) ||
            marker.marker_y_pixels < 0.0 ||
            marker.marker_y_pixels > view->viewport_height_pixels ||
            !std::isfinite(marker.marker_left_fraction) ||
            !std::isfinite(marker.marker_right_fraction) ||
            marker.marker_left_fraction < 0.0 ||
            marker.marker_right_fraction <= marker.marker_left_fraction ||
            marker.marker_right_fraction > 1.0 || marker.lane < wormedit::timing_lane_kind_t::bpm ||
            marker.lane > wormedit::timing_lane_kind_t::anchor) {
            return 0;
        }
    }

    return true;
}

static bool compose_geometry_resource_elements_valid(const wormedit::ui::compose_view_t* view) {    constexpr std::uint8_t compose_resource_roles = WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE |
                                                    WORM_UI_COMPOSE_RESOURCE_ROLE_BGM |
                                                    WORM_UI_COMPOSE_RESOURCE_ROLE_BGA;
    for (size_t index = 0; index < view->resources.size(); ++index) {
        const auto& resource = view->resources[index];
        if (!resource.resource_id || !resource.asset_id || resource.roles == 0 ||
            (resource.roles & ~compose_resource_roles) != 0) {
            return 0;
        }
    }
    for (size_t index = 0; index < view->output_groups.size(); ++index) {
        const auto& group = view->output_groups[index];
        if ((index != 0 && group.document_index <= view->output_groups[index - 1].document_index))
            return 0;
    }

    return true;
}

static bool compose_geometry_note_elements_valid(const wormedit::ui::compose_view_t* view) {    for (size_t index = 0; index < view->differences.size(); ++index) {
        if (!view->differences[index].bounds.is_valid())
            return 0;
    }
    for (size_t index = 0; index < view->hold_bodies.size(); ++index) {
        if (!view->hold_bodies[index].bounds.is_valid())
            return 0;
    }
    for (size_t index = 0; index < view->note_endpoints.size(); ++index) {
        if (!view->note_endpoints[index].bounds.is_valid() ||
            view->note_endpoints[index].glyph > static_cast<wormedit::lane_glyph_t>(wormedit::ui::last_arrow_glyph))
            return 0;
    }
    for (size_t index = 0; index < view->bgm_lanes.size(); ++index) {
        if (!view->bgm_lanes[index].bounds.is_valid() ||
            view->bgm_lanes[index].source_lane_id == wormedit::object_id_t{})
            return 0;
    }
    for (size_t index = 0; index < view->bgm_events.size(); ++index) {
        if (!view->bgm_events[index].bounds.is_valid() ||
            view->bgm_events[index].event_id == wormedit::object_id_t{} || view->bgm_events[index].source_lane_id == wormedit::object_id_t{})
            return 0;
    }

    return true;
}

static bool compose_geometry_waveform_elements_valid(const wormedit::ui::compose_view_t* view) {    for (size_t index = 0; index < view->waveform_rows.size(); ++index) {
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
    for (size_t index = 0; index < view->channel_waveform_rows.size(); ++index) {
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

    return true;
}

static bool compose_geometry_bmson_elements_valid(const wormedit::ui::compose_view_t* view) {    for (size_t index = 0; index < view->bmson_marker_drafts.size(); ++index) {
        if (!view->bmson_marker_drafts[index].bounds.is_valid()) {
            return 0;
        }
    }
    if (view->bmson_marker_move_preview &&
        !view->bmson_marker_move_preview->bounds.is_valid()) {
        return 0;
    }
    for (size_t index = 0; index < view->semantic_layers.size(); ++index) {
        if (view->semantic_layers.empty() || view->semantic_layers[index].layer_id.value == 0 ||
            false) {
            return 0;
        }
    }

    return true;
}

static bool compose_geometry_bookmark_elements_valid(const wormedit::ui::compose_view_t* view) {    for (size_t index = 0; index < view->beat_bookmarks.size(); ++index) {
        const auto& bookmark = view->beat_bookmarks[index];
        if (bookmark.bookmark_id.value == 0 || bookmark.measure_number == 0 ||
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

static bool compose_geometry_beat_section_elements_valid(const wormedit::ui::compose_view_t* view) {    for (size_t index = 0; index < view->beat_sections.size(); ++index) {
        const auto& section = view->beat_sections[index];
        const auto valid_y = [&](bool visible, double y) {
            return !visible || (std::isfinite(y) && y >= 0.0 && y <= view->viewport_height_pixels);
        };
        if (section.section_id.value == 0 || section.begin_measure_number == 0 ||
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

static bool compose_geometry_audio_elements_valid(const wormedit::ui::compose_view_t* view) {    for (size_t index = 0; index < view->audio_bookmarks.size(); ++index) {
        const auto& bookmark = view->audio_bookmarks[index];
        if (bookmark.bookmark_id.value == 0 ||
            (bookmark.projection_visible &&
             (!std::isfinite(bookmark.projected_y_pixels) || bookmark.projected_y_pixels < 0.0 ||
              bookmark.projected_y_pixels > view->viewport_height_pixels))) {
            return 0;
        }
    }
    for (size_t index = 0; index < view->audio_sections.size(); ++index) {
        const auto& section = view->audio_sections[index];
        if (section.section_id.value == 0 ||
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

static bool compose_geometry_hover_valid(const wormedit::ui::compose_view_t* view) {    if (view->hover_note_visible && (!view->hover_tooltip.c_str() || view->hover_note_id.value == 0)) {
        return 0;
    }

    return true;
}

static bool compose_geometry_branch_elements_valid(const wormedit::ui::compose_view_t* view) {    for (size_t index = 0; index < view->branch_rows.size(); ++index) {
        if (view->branch_rows[index].depth > 512)
            return 0;
    }
    for (size_t index = 0; index < view->branch_choices.size(); ++index) {
        const auto& choice = view->branch_choices[index];
        if (choice.branch_id.value == 0 || choice.kind > WORM_UI_COMPOSE_BRANCH_SWITCH ||
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

static bool compose_geometry_cursor_valid(const wormedit::ui::compose_view_t* view) {
    return !view->cursor || view->cursor->bounds.is_valid();
}

static bool compose_geometry_scene_elements_valid(const wormedit::ui::compose_view_t* view) {
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

static bool compose_beat_fit_valid(const wormedit::ui::compose_view_t* view) {
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

static bool compose_waveform_overview_valid(const wormedit::ui::compose_view_t* view) {
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
           (view->waveform_overview_rows.size() == 0 ||
            (view->waveform_overview_total_frames != 0 &&
             view->waveform_overview_sample_rate != 0 &&
             view->waveform_audio_offset_denominator != 0));
}

static bool compose_bgm_bar_valid(const wormedit::ui::compose_view_t* view) {
    if (view->bgm_bar_rows.size() == 0)
        return view->bgm_bar_marks.size() == 0 && view->bgm_bar_bookmarks.size() == 0;
    if (!std::isfinite(view->bgm_bar_begin_fraction) || !std::isfinite(view->bgm_bar_end_fraction) ||
        view->bgm_bar_begin_fraction < 0.0 || view->bgm_bar_end_fraction > 1.0 ||
        view->bgm_bar_begin_fraction >= view->bgm_bar_end_fraction)
        return false;
    return std::all_of(view->bgm_bar_marks.begin(), view->bgm_bar_marks.end(),
                       [](const wormedit::ui::compose_bgm_bar_mark_view_t& mark) {
                           return std::isfinite(mark.audio_fraction);
                       }) &&
           std::all_of(view->bgm_bar_bookmarks.begin(),
                       view->bgm_bar_bookmarks.end(),
                       [](const wormedit::ui::compose_bgm_bar_bookmark_view_t& bookmark) {
                           return std::isfinite(bookmark.audio_fraction);
                       });
}

static bool compose_scene_arrays_present(const wormedit::ui::compose_view_t* view) {
    return (view->channel_waveform_rows.empty() ||
            (view->channel_waveform_source_lane_id != wormedit::object_id_t{} && !view->bgm_lanes.empty()));
}

static bool compose_validation_check(bool accepted, const char* reason) {
#ifndef NDEBUG
    static std::vector<std::string_view> reported;
    if (!accepted && std::ranges::find(reported, reason) == reported.end()) {
        reported.emplace_back(reason);
        std::fprintf(stderr, "Compose view validation: %s\n", reason);
    }
#else
    (void)reason;
#endif
    return accepted;
}

static bool overview_rows_valid(std::span<const wormedit::ui::compose_waveform_overview_row_view_t> rows) {
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows[index];
        if (!std::isfinite(row.audio_fraction) || row.audio_fraction < 0 || row.audio_fraction > 1 ||
            !std::isfinite(row.absolute_peak) || row.absolute_peak < 0 || row.absolute_peak > 1)
            return false;
    }
    return true;
}

const char* worm_ui_nuklear_compose_overview_error(const wormedit::ui::compose_view_t* view) {
    if (!view) return "missing overview view";
    if (!compose_waveform_overview_valid(view)) return "overview timing metadata";
    if (!overview_rows_valid(view->waveform_overview_rows))
        return "overview row data";
    if (!compose_bgm_bar_valid(view)) return "overview window/markers";
    if (!overview_rows_valid(view->bgm_bar_rows))
        return "overview bar row data";
    return nullptr;
}

int worm_ui_nuklear_compose_overview_valid(const wormedit::ui::compose_view_t* view) {
    const auto* error = worm_ui_nuklear_compose_overview_error(view);
    return compose_validation_check(error == nullptr, error ? error : "overview");
}

int worm_ui_nuklear_compose_view_valid(const wormedit::ui::compose_view_t* view) {
    return compose_validation_check(view, "view") &&
           compose_validation_check(view->hitsounds.values.is_valid(), "hitsound palette values") &&
           compose_validation_check(worm_ui_nuklear_note_layer_window_valid(view), "worm_ui_nuklear_note_layer_window_valid(view)") &&
           compose_validation_check((view->selected_bmson_bgm_event_id.value == 0 || view->selected_object_count == 1), "(view->selected_bmson_bgm_event_id == 0 || view->selected_object_count == 1)") &&
           compose_validation_check((view->selected_bmson_note_event_id.value == 0 || view->selected_note_count == 1), "(view->selected_bmson_note_event_id == 0 || view->selected_note_count == 1)") &&
           compose_validation_check(((view->selected_bmson_bgm_event_id.value == 0) ==
            (view->selected_bmson_source_lane_id.value == 0)), "((view->selected_bmson_bgm_event_id == 0) == (view->selected_bmson_source_lane_id == 0))") &&
           compose_validation_check(view->staged_bmson_marker_restarts.size() <= 256, "view->staged_bmson_marker_restarts.size() <= 256") &&
           compose_validation_check((view->staged_bmson_marker_restarts.size() == 0 ||
            (!view->staged_bmson_marker_playable_lanes.empty() &&
             !view->staged_bmson_marker_restarts.empty())), "(view->staged_bmson_marker_restarts.size() == 0 || (!view->staged_bmson_marker_playable_lanes.empty() && !view->staged_bmson_marker_restarts.empty()))") &&
           compose_validation_check(view->bmson_marker_drafts.size() <= view->staged_bmson_marker_restarts.size(), "view->bmson_marker_drafts.size() <= view->staged_bmson_marker_restarts.size()") &&
           compose_validation_check((view->proposed_grid_lines.size() == 0 || view->waveform_offset_drag_active ||
            view->beat_fit_drag_active), "(view->proposed_grid_lines.size() == 0 || view->waveform_offset_drag_active || view->beat_fit_drag_active)") &&
           compose_validation_check(((view->timing_preview_rects.size() == 0 && view->timing_preview_markers.size() == 0) ||
            view->waveform_offset_drag_active || view->beat_fit_drag_active), "((view->timing_preview_rects.size() == 0 && view->timing_preview_markers.size() == 0) || view->waveform_offset_drag_active || view->beat_fit_drag_active)") &&
           compose_validation_check(compose_beat_fit_valid(view), "compose_beat_fit_valid(view)") &&
           compose_validation_check(view->semantic_layers.size() <= wormedit::ui::semantic_layer_window_limit, "view->semantic_layers.size() <= wormedit::ui::semantic_layer_window_limit") &&


           compose_validation_check(compose_scene_arrays_present(view), "compose_scene_arrays_present(view)") &&
           compose_validation_check(compose_geometry_valid(view), "compose_geometry_valid(view)");
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
        for (size_t index = 0; index < view.waveform_overview_rows.size(); ++index) {
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
    struct nk_context* context, const wormedit::ui::compose_view_t* view,
    wormedit::ui::workspace_action_t* action, struct nk_rect bounds, bool window_focused) {
    if (!context || !view || !action) return;
    const bool overview_valid = worm_ui_nuklear_compose_overview_valid(view);
    action->minimap_visible_begin_fraction = overview_valid ? view->waveform_overview_visible_begin_fraction : 0;
    action->minimap_visible_end_fraction = overview_valid ? view->waveform_overview_visible_end_fraction : 0;
    const bool available = view->visible && overview_valid &&
        view->scene_available && view->waveform_overview_rows.size() != 0 &&
        bounds.w > 1.0f && bounds.h > 1.0f;
    auto* canvas = nk_window_get_canvas(context);
    if (available && canvas)
        render_compose_waveform_overview({context, view, action, canvas, bounds});
    const auto& button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    const bool pressed = available && window_focused && button.clicked && button.down &&
        nk_input_is_mouse_click_down_in_rect(&context->input, NK_BUTTON_LEFT, bounds, nk_true);
    if (pressed) state.input_queue.keyboard_focus = true;
    process_compose_overview_input({context, view, action, bounds, available, pressed,
        window_focused && available, state.waveform_drag.waveform_beat_zero_drag_active});
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
    [[nodiscard]] double fraction_at(double x) const {
        return begin + std::clamp((x - bounds.x) / bounds.w, 0.0, 1.0) * (end - begin);
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
double bgm_bar_pixels_per_measure(const wormedit::ui::compose_view_t& view, const BgmBarGeometry& geometry) {
    if (view.bgm_bar_marks.size() < 2) return 0.0;
    const auto& first = view.bgm_bar_marks[0];
    const auto& last = view.bgm_bar_marks[view.bgm_bar_marks.size() - 1];
    const auto measures = static_cast<double>(last.measure) - first.measure;
    if (measures <= 0.0) return 0.0;
    return (last.audio_fraction - first.audio_fraction) / (geometry.end - geometry.begin) *
           geometry.bounds.w / measures;
}

void draw_bgm_bar_waveform(struct nk_command_buffer* canvas, const wormedit::ui::compose_view_t& view,
                           const BgmBarGeometry& geometry) {
    const auto center = geometry.bounds.y + geometry.bounds.h * 0.5f;
    const auto half_height = geometry.bounds.h * 0.43f;
    const auto width = std::max(1.0f, geometry.bounds.w / static_cast<float>(view.bgm_bar_rows.size()));
    const auto color = nk_rgba(114, 222, 239, 190);
    for (size_t index = 0; index < view.bgm_bar_rows.size(); ++index) {
        const auto& row = view.bgm_bar_rows[index];
        const auto x = geometry.x_of(row.audio_fraction);
        nk_stroke_line(canvas, x, center - row.absolute_peak * half_height,
                       x, center + row.absolute_peak * half_height, width, color);
    }
}

void draw_bgm_bar_grid(struct nk_context* context, struct nk_command_buffer* canvas,
                       const wormedit::ui::compose_view_t& view, const BgmBarGeometry& geometry) {
    const auto* font = context->style.font;
    const auto per_measure = bgm_bar_pixels_per_measure(view, geometry);
    const auto line_step = bgm_bar_measure_step(per_measure, bgm_bar_line_spacing_pixels);
    const auto label_step = bgm_bar_measure_step(per_measure, bgm_bar_label_spacing_pixels);
    const auto& bounds = geometry.bounds;
    for (size_t index = 0; index < view.bgm_bar_marks.size(); ++index) {
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
                            const wormedit::ui::compose_view_t& view, const BgmBarGeometry& geometry) {
    const auto& bounds = geometry.bounds;
    const auto* font = context->style.font;
    for (size_t index = 0; index < view.bgm_bar_bookmarks.size(); ++index) {
        const auto& bookmark = view.bgm_bar_bookmarks[index];
        const auto x = geometry.x_of(bookmark.audio_fraction);
        const auto color = bookmark.audio ? nk_rgb(124, 224, 168) : nk_rgb(255, 198, 92);
        nk_stroke_line(canvas, x, bounds.y, x, bounds.y + bounds.h, 1.0f, nk_rgba(color.r, color.g, color.b, 150));
        nk_fill_triangle(canvas, x, bounds.y + 7.0f, x + 7.0f, bounds.y, x, bounds.y, color);
        if (!font || bookmark.label.empty()) continue;
        float limit = bounds.x + bounds.w;
        for (size_t other = 0; other < view.bgm_bar_bookmarks.size(); ++other) {
            const auto other_x = geometry.x_of(view.bgm_bar_bookmarks[other].audio_fraction);
            if (other_x > x + 1.0f) limit = std::min(limit, other_x);
        }
        const auto length = static_cast<int>(bookmark.label.size());
        const auto width = font->width(font->userdata, font->height, bookmark.label.data(), length);
        if (x + 10.0f + width > limit - 2.0f) continue;
        nk_draw_text(canvas, nk_rect(x + 10.0f, bounds.y + bounds.h - font->height - 1.0f, width, font->height),
                     bookmark.label.data(), length, font, nk_rgba(0, 0, 0, 0), color);
    }
}

void draw_bgm_bar_markers(struct nk_command_buffer* canvas, const wormedit::ui::compose_view_t& view,
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

void append_bgm_bar_input(wormedit::ui::workspace_action_t* action, WormUiComposeInputKind kind,
                          double amount, double audio_fraction) {
    append_compose_input(action, kind, amount);
    if (action->compose_inputs.size() == 0) return;
    auto& input = action->compose_inputs[action->compose_inputs.size() - 1];
    if (input.kind == kind) input.audio_fraction = audio_fraction;
}

// Wheel zoom centers on the visible field span; Shift+wheel or middle-drag pans the range.
void process_bgm_bar_view_input(struct nk_context* context, const wormedit::ui::compose_view_t& view,
                                wormedit::ui::workspace_action_t* action,
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
    const bool shift = (state.input_queue.pointer_modifiers & SDL_KMOD_SHIFT) != 0;
    const auto wheel_x = static_cast<double>(input.mouse.scroll_delta.x);
    const auto wheel_y = static_cast<double>(input.mouse.scroll_delta.y);
    if (shift || wheel_x != 0.0) {
        const auto amount = wheel_x != 0.0 ? wheel_x : wheel_y;
        if (amount != 0.0)
            append_bgm_bar_input(action, WORM_UI_COMPOSE_INPUT_BGM_BAR_PAN,
                                 -amount * span * bgm_bar_wheel_pan_fraction, 0.0);
    } else if (wheel_y != 0.0) {
        append_bgm_bar_input(action, WORM_UI_COMPOSE_INPUT_BGM_BAR_ZOOM, wheel_y,
                             (view.waveform_overview_visible_begin_fraction +
                              view.waveform_overview_visible_end_fraction) * 0.5);
    }
}

void report_compose_bgm_hover(struct nk_context* context, const wormedit::ui::compose_view_t* view,
                              wormedit::ui::workspace_action_t* action, const BgmBarGeometry& geometry) {
    if (!nk_input_is_mouse_hovering_rect(&context->input, geometry.bounds)) return;
    constexpr char instructions[] =
        "Wheel: zoom   Shift+wheel or middle-drag: pan   Middle-click: whole song";
    if (view->selected_audio_asset_id.value == 0 || view->waveform_overview_total_frames == 0 ||
        view->waveform_overview_sample_rate == 0) {
        return;
    }
    const auto frame_value = static_cast<long double>(
        geometry.fraction_at(context->input.mouse.pos.x)) *
        (view->waveform_overview_total_frames - 1);
    if (frame_value > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return;
    }
    const auto source_frame = static_cast<std::int64_t>(frame_value);
    action->hover_source_frame = source_frame;
    action->hover_source_frame_valid = true;
    action->hover_source_total_frames = view->waveform_overview_total_frames;
    action->hover_source_sample_rate = view->waveform_overview_sample_rate;
    if (!view->hover_timing_valid || view->hover_timing_source_frame != source_frame ||
        view->hover_timing_total_frames != view->waveform_overview_total_frames ||
        view->hover_timing_sample_rate != view->waveform_overview_sample_rate) {
        return;
    }
    if (!wormedit::ui::nuklear::tooltip::hovered(context, geometry.bounds)) return;
    wormedit::ui::chrome::timing_tooltip(context, view->hover_timing_label.c_str(), instructions);
}

} // namespace

void worm_ui_nuklear_render_compose_bgm_bar(
    struct nk_context* context, const wormedit::ui::compose_view_t* view,
    wormedit::ui::workspace_action_t* action, struct nk_rect bounds, bool window_focused) {
    if (!context || !view || !action) return;
    const bool overview_valid = worm_ui_nuklear_compose_overview_valid(view);
    action->minimap_visible_begin_fraction = overview_valid ? view->waveform_overview_visible_begin_fraction : 0;
    action->minimap_visible_end_fraction = overview_valid ? view->waveform_overview_visible_end_fraction : 0;
    const bool available = view->visible && overview_valid &&
        view->scene_available && view->bgm_bar_rows.size() != 0 && bounds.w > 1.0f && bounds.h > 1.0f;
    auto* canvas = nk_window_get_canvas(context);
    const auto capture_valid = state.waveform_drag.waveform_overview_drag_active &&
        state.waveform_drag.waveform_overview_capture.window_capture &&
        worm_ui_nuklear_overview_capture_matches(state.waveform_drag.waveform_overview_capture, *view,
            bounds.x, bounds.w, available, window_focused);
    const auto captured_window = worm_ui_nuklear_overview_window_for_capture(
        state.waveform_drag.waveform_overview_capture, capture_valid,
        view->bgm_bar_begin_fraction, view->bgm_bar_end_fraction);
    const BgmBarGeometry geometry{bounds, captured_window.begin, captured_window.end};
    if (available && canvas) {
        nk_fill_rect(canvas, bounds, 0.0f,
                     wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::overview));
        draw_bgm_bar_waveform(canvas, *view, geometry);
        draw_bgm_bar_grid(context, canvas, *view, geometry);
        draw_bgm_bar_bookmarks(context, canvas, *view, geometry);
        draw_bgm_bar_markers(canvas, *view, geometry);
        report_compose_bgm_hover(context, view, action, geometry);
    }
    const auto& button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    const bool pressed = available && window_focused && button.clicked && button.down &&
        nk_input_is_mouse_click_down_in_rect(&context->input, NK_BUTTON_LEFT, bounds, nk_true);
    if (pressed) state.input_queue.keyboard_focus = true;
    if (available && window_focused) process_bgm_bar_view_input(context, *view, action, geometry);
    process_compose_overview_input({context, view, action, bounds, available, pressed,
        window_focused && available, state.waveform_drag.waveform_beat_zero_drag_active,
        view->bgm_bar_begin_fraction, view->bgm_bar_end_fraction, true});
    action->minimap_window_capture_active = state.waveform_drag.waveform_overview_drag_active &&
        state.waveform_drag.waveform_overview_capture.window_capture;
    action->minimap_window_capture_begin_fraction =
        state.waveform_drag.waveform_overview_capture.window_begin;
    action->minimap_window_capture_end_fraction = state.waveform_drag.waveform_overview_capture.window_end;
}

struct ComposePanelRenderContext {
    struct nk_context* context;
    const wormedit::ui::compose_view_t* view;
    const wormedit::ui::timing_view_t* timing_view;
    wormedit::ui::workspace_action_t* action;
    bool window_focused;
    bool ime_composition_active;
    const worm_ui_nuklear_pointer_click_t* double_clicks;
    std::size_t double_click_count;
    std::size_t shortcut_event_count;
};

static auto is_inline_audio_timing(wormedit::timing_lane_kind_t lane) -> bool {
    using wormedit::timing_lane_kind_t;
    return lane == timing_lane_kind_t::bpm || lane == timing_lane_kind_t::stop || lane == timing_lane_kind_t::delay;
}

static void update_inline_audio_timing(const ComposePanelRenderContext& panel,
    bool committed, bool changed) {
    const auto& timing = *panel.timing_view;
    worm_ui_nuklear_render_timing_anchoring(panel.context, timing, *panel.action);
    nk_layout_row_dynamic(panel.context, 26, 1);
    const bool apply = nk_button_label(panel.context, "Apply timing edit") != 0;
    if (committed || apply) {
        worm_ui_nuklear_inline_timing_update(state, timing, *panel.action, true, false);
        // Keep a rejected proposal editable here. Application acceptance removes the
        // stage, and the next frame dismisses this popup through the normal binding check.
        if (panel.action->timing_stage_action == wormedit::ui::timing_stage_action_t::confirm)
            state.inline_timing.inline_timing_open = true;
    } else if (changed && panel.action->timing_stage_action == wormedit::ui::timing_stage_action_t::none &&
        worm_ui_nuklear_inline_timing_confirm(timing, state.inline_timing.inline_timing_text.data(), *panel.action)) {
        panel.action->timing_stage_action = wormedit::ui::timing_stage_action_t::update;
    }
}

static auto inline_audio_timing_status(const wormedit::ui::timing_view_t& view) -> const char* {
    if (state.inline_timing.inline_timing_error[0]) return state.inline_timing.inline_timing_error.data();
    if (view.stage_rounded_positions && !view.stage_anchoring.allow_quantization)
        return "Allow quantization is required to apply.";
    if (!view.stage_status_text.empty()) return view.stage_status_text.c_str();
    return "Enter to apply · Esc to cancel";
}

static auto compose_inline_timing_popup_bounds(nk_context* context, struct nk_rect viewport)
    -> struct nk_rect {
    const bool audio_timing = is_inline_audio_timing(state.inline_timing.inline_timing_lane);
    const float popup_width = audio_timing ? 310.0f : 196.0f;
    const float popup_height = audio_timing ? 224.0f : 75.0f;
    const auto popup_x = std::clamp(state.inline_timing.inline_timing_anchor_screen_x,
                                    viewport.x, viewport.x + std::max(0.0f, viewport.w - popup_width));
    const auto popup_y = std::clamp(state.inline_timing.inline_timing_anchor_screen_y - 24.0f,
                                    viewport.y,
                                    viewport.y + std::max(0.0f, viewport.h - popup_height));
    const auto parent_clip = nk_window_get_content_region(context);
    return nk_rect(popup_x - parent_clip.x, popup_y - parent_clip.y,
                                    popup_width, popup_height);

}

static void dismiss_compose_inline_timing_popup(nk_context* context, struct nk_rect viewport) {
    wormedit::ui::chrome::dismiss_popup(
        context, "Compose timing value", compose_inline_timing_popup_bounds(context, viewport));
    state.inline_timing.inline_timing_popup_close_pending = false;
    state.inline_timing.inline_timing_open = false;
    state.inline_timing.inline_timing_focus_pending = false;
}

static void render_compose_inline_timing_editor(const ComposePanelRenderContext& panel,
                                                struct nk_rect viewport) {
    static_cast<void>(worm_ui_nuklear_inline_timing_review_new_stage(
        state, *panel.timing_view, *panel.action,
        viewport.x + viewport.w * 0.5f, viewport.y + viewport.h * 0.5f));
    if (state.inline_timing.inline_timing_popup_close_pending) {
        dismiss_compose_inline_timing_popup(panel.context, viewport);
        return;
    }
    if (!state.inline_timing.inline_timing_open) return;
    if (!panel.window_focused) {
        worm_ui_nuklear_inline_timing_cancel(state, *panel.action);
        dismiss_compose_inline_timing_popup(panel.context, viewport);
        return;
    }
    const auto& timing = *panel.timing_view;
    if (!timing.stage_active) {
        // The opening double-click reaches the application after this frame.
        if (panel.action->timing_stage_action == wormedit::ui::timing_stage_action_t::begin) return;
        dismiss_compose_inline_timing_popup(panel.context, viewport);
        return;
    }
    if (timing.chart_id.value != state.inline_timing.inline_timing_chart_id ||
        timing.document_revision != state.inline_timing.inline_timing_revision ||
        timing.stage_existing_object_id.value != state.inline_timing.inline_timing_object_id ||
        timing.stage_lane != state.inline_timing.inline_timing_lane) {
        dismiss_compose_inline_timing_popup(panel.context, viewport);
        return;
    }
    if (state.inline_timing.inline_timing_focus_pending && state.inline_timing.inline_timing_error[0] == '\0')
        worm_ui_nuklear_inline_timing_initial_text(
            timing, state.inline_timing.inline_timing_text.data(), state.inline_timing.inline_timing_text.size());

    if (!nk_popup_begin(panel.context, NK_POPUP_STATIC, "Compose timing value",
                        NK_WINDOW_NO_SCROLLBAR,
                        compose_inline_timing_popup_bounds(panel.context, viewport))) {
        worm_ui_nuklear_inline_timing_cancel(state, *panel.action);
        return;
    }
    nk_layout_row_dynamic(panel.context, 25.0f, 1);
    const auto before_text = state.inline_timing.inline_timing_text;
    const auto edit_flags = wormedit::ui::chrome::autofocus_text_field(
        panel.context, static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER |
                           NK_EDIT_AUTO_SELECT,
        state.inline_timing.inline_timing_text.data(), static_cast<int>(state.inline_timing.inline_timing_text.size()),
        nk_filter_default, state.inline_timing.inline_timing_focus_pending);
    if (is_inline_audio_timing(timing.stage_lane))
        update_inline_audio_timing(panel, (edit_flags & NK_EDIT_COMMITTED) != 0,
            before_text != state.inline_timing.inline_timing_text);
    else worm_ui_nuklear_inline_timing_update(state, timing, *panel.action,
        (edit_flags & NK_EDIT_COMMITTED) != 0, (edit_flags & NK_EDIT_DEACTIVATED) != 0);
    if (!state.inline_timing.inline_timing_open) {
        nk_popup_close(panel.context);
        state.inline_timing.inline_timing_popup_close_pending = false;
    }
    nk_layout_row_dynamic(panel.context, is_inline_audio_timing(timing.stage_lane) ? 36.0f : 17.0f, 1);
    nk_label_wrap(panel.context, inline_audio_timing_status(timing));
    nk_popup_end(panel.context);
}

static void discard_occluded_compose_input(const ComposePanelRenderContext& panel) {
    panel.action->compose_input_ownership = 1;
    cancel_compose_gestures(panel.action);
    state.input_queue.key_event_count = 0;
}

// Area and SV recipe popups occlude the rest of Compose input while they own it,
// so they must be serviced ahead of that gate to render, finish captures, and close.
static auto process_compose_owned_popups(const ComposePanelRenderContext& panel,
                                         struct nk_rect viewport, double input_right) -> bool {
    const bool area_owned = compose_area_menu_consumed();
    const bool recipe_owned = compose_recipe_menu_consumed();
    if (area_owned)
        process_compose_area_menu(panel.context, *panel.view, *panel.action, viewport,
                                  input_right, panel.window_focused);
    if (recipe_owned)
        process_compose_recipe_menu(panel.context, *panel.view, *panel.action, viewport,
                                    input_right, panel.window_focused);
    return area_owned || recipe_owned;
}

static void process_compose_draw_tools(const ComposePointerEventContext& input, bool& consumed) {
    if (process_resource_paint_pointer(input, consumed)) return;
    if (process_semantic_layer_paint_pointer(input, consumed)) return;
    const auto right_consumed = !consumed && process_draw_right_pointer(input);
    consumed |= right_consumed;
    if (!right_consumed) process_note_paint_pointer(input, consumed);
}

static void draw_compose_editor_scene(const ComposePanelRenderContext& panel,
    nk_command_buffer* canvas, struct nk_rect viewport, double input_right) {
    draw_compose_scene_canvas(panel.context, panel.view, canvas, viewport);
    draw_compose_tool_chip(panel.context, *panel.view, canvas,
        nk_rect(viewport.x, viewport.y, static_cast<float>(input_right - viewport.x), viewport.h));
}

static void process_compose_canvas_tools(const ComposePointerEventContext& pointer_event,
    bool paint_pointer_consumed) {
    const bool same_sound_pointer = !paint_pointer_consumed && handle_same_sound_pointer(pointer_event);
    paint_pointer_consumed |= same_sound_pointer;
    paint_pointer_consumed = process_compose_timing_marker_pointer(pointer_event,
                                                                    paint_pointer_consumed) ||
                             paint_pointer_consumed;
    paint_pointer_consumed = begin_compose_resource_pointer(pointer_event, paint_pointer_consumed) ||
                             paint_pointer_consumed;
    paint_pointer_consumed = process_bmson_razor_pointer(pointer_event, paint_pointer_consumed);
    paint_pointer_consumed = process_bmson_marker_move_pointer(pointer_event, paint_pointer_consumed);
    process_compose_draw_tools(pointer_event, paint_pointer_consumed);

    if (!same_sound_pointer) {
        process_compose_pointer_actions(pointer_event, paint_pointer_consumed);
        process_compose_drag_gestures(pointer_event, paint_pointer_consumed);
    }

}

static auto capture_pointer_modifiers(const nk_context& context) -> std::uint16_t {
    if (nk_input_is_mouse_pressed(&context.input, NK_BUTTON_LEFT))
        state.input_queue.pointer_press_modifiers = state.input_queue.pointer_modifiers;
    const auto modifiers = state.input_queue.pointer_press_modifiers.value_or(state.input_queue.pointer_modifiers);
    if (!context.input.mouse.buttons[NK_BUTTON_LEFT].down)
        state.input_queue.pointer_press_modifiers.reset();
    return modifiers;
}

// Canceled captures retain ownership until their physical release.
static auto consume_compose_capture_release(const ComposePanelRenderContext& panel,
                                            struct nk_rect viewport, float editor_right) -> bool {
    if (state.pointer_capture.draw_left_cancelled_until_release) {
        if (!panel.context->input.mouse.buttons[NK_BUTTON_LEFT].down)
            state.pointer_capture.draw_left_cancelled_until_release = false;
        return true;
    }
    return process_compose_tool_chip(panel.context, *panel.view,
        nk_rect(viewport.x, viewport.y, editor_right - viewport.x, viewport.h), panel.window_focused);
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
    worm_ui_nuklear_cancel_stale_stem_lane_resize(state, *view, window_focused);

    const auto editor_input_right = inspector_drawn
        ? viewport.x + viewport.w - inspector_width - 8.0f
        : viewport.x + viewport.w;
    action->compose_input_editor_right_pixels =
        std::max(1.0, static_cast<double>(editor_input_right - viewport.x) *
                      view->viewport_width_pixels / viewport.w);
    // A note double-click selected the note and asked for its label editor; open it over the note.
    if (view->focus_note_label_editor && window_focused && !view->input_occluded &&
        begin_compose_note_label_inline(state.bookmark_ui.bookmark_rename, *view, state.note_label.note_label_anchor_x,
                                        state.note_label.note_label_anchor_y))
        action->compose_note_label_focus_consumed = true;
    if (compose_note_menu_open() || compose_note_menu_consumed())
        process_compose_note_menu(context, *view, *action, viewport, editor_input_right, window_focused);
    const bool owned_popup = !compose_note_menu_consumed() &&
        process_compose_owned_popups(panel, viewport, editor_input_right);
    if (view->input_occluded || owned_popup || compose_input_queries_t{state}.is_canvas_input_blocked()) {
        discard_occluded_compose_input(panel);
        draw_compose_editor_scene(panel, canvas, viewport, editor_input_right);
        render_compose_bookmark_rename(context, *view, *action, state.bookmark_ui.bookmark_rename, viewport, window_focused);
        return;
    }
    const auto pointer_focus = update_compose_pointer_focus(
        {context, view, action, viewport, nk_rect(0, 0, 0, 0), editor_input_right, false,
         window_focused});
    const auto pointer_inside = pointer_focus.pointer_inside;
    const auto pointer_clicked = pointer_focus.pointer_clicked;
    const auto& left_button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    process_compose_key_events(context, view, action, window_focused, ime_composition_active);
    if (state.dialogs.go_to_measure_open) {
        draw_compose_editor_scene(panel, canvas, viewport, editor_input_right);
        render_compose_bookmark_rename(context, *view, *action, state.bookmark_ui.bookmark_rename, viewport, window_focused);
        return;
    }


    if (consume_compose_capture_release(panel, viewport, static_cast<float>(editor_input_right))) {
        draw_compose_editor_scene(panel, canvas, viewport, editor_input_right);
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
    const bool left_down = context->input.mouse.buttons[NK_BUTTON_LEFT].down != 0;
    const bool left_released = nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT);
    const auto pointer_modifiers = capture_pointer_modifiers(*context);
    const bool alt_down = compose_pointer_chord(pointer_modifiers) == compose_pointer_chord_t::hold;
    constexpr auto any_pointer_modifier = compose_pointer_modifier_mask;
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
    const auto semantic_paint_uses_bgm_pane = view->semantic_layer_paint_mode_active;
    const auto clamp_paint_x = [&](double value) {
        return semantic_paint_uses_bgm_pane
            ? std::clamp(value, 0.0, view->viewport_width_pixels)
            : std::clamp(value, note_field_left, note_field_right);
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
        state.input_queue.pointer_modifiers, any_pointer_modifier, shortcut_event_count, window_focused,
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
    if (!state.inline_timing.inline_timing_open && !state.timing_marker.timing_marker_candidate_active)
        update_timing_stage_drag(pointer_event);
    update_kiai_span_drag(pointer_event);
    update_recipe_drag(pointer_event);
    auto compose_pointer_captured = compose_input_queries_t{state}.is_canvas_hover_blocked() ||
        beat_fit_pointer_consumed;
    const auto mix_consumed = process_bgm_mix_controls(pointer_event, compose_pointer_captured);
    compose_pointer_captured |= mix_consumed;
    const auto resize_consumed = process_stem_lane_resize(pointer_event, double_clicks,
                                                          double_click_count, compose_pointer_captured);
    compose_pointer_captured |= resize_consumed;
    action->compose_has_pointer_hover = !state.input_queue.action_overflowed && window_focused &&
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
    update_compose_sound_hover(pointer_event);
    pointer_event.compose_note_double_click_in_view = compose_note_double_click_in_view;
    bool paint_pointer_consumed = compose_input_queries_t{state}.is_paint_blocked() ||
        beat_fit_pointer_consumed || resize_consumed || mix_consumed || compose_action_has_cancel(action);
    process_compose_canvas_tools(pointer_event, paint_pointer_consumed);

    draw_compose_editor_scene(panel, canvas, viewport, editor_input_right);
    render_compose_bookmark_rename(context, *view, *action, state.bookmark_ui.bookmark_rename, viewport, window_focused);
    render_compose_inline_timing_editor(panel, viewport);
}

static void close_go_to_measure(nk_context* context) {
    nk_edit_unfocus(context);
    state.dialogs.go_to_measure_open = false;
    state.dialogs.go_to_measure_focus_pending = false;
    state.dialogs.go_to_measure_cancel_pending = false;
    state.input_queue.keyboard_focus = true;
    nk_popup_close(context);
}

static void confirm_go_to_measure(const wormedit::ui::compose_view_t& view,
                                  wormedit::ui::workspace_action_t& action, nk_context* context) {
    const auto number = worm_ui_nuklear_compose_measure_number(view,
                                                              state.dialogs.go_to_measure_text.data());
    if (!number) {
        std::snprintf(state.dialogs.go_to_measure_error.data(), state.dialogs.go_to_measure_error.size(),
                      view.bms_measure_labels ? "Enter a measure from 000 to 999"
                                              : "Enter an existing measure number");
        state.dialogs.go_to_measure_focus_pending = true;
        return;
    }
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action.compose_action = WORM_UI_COMPOSE_ACTION_GO_TO_MEASURE;
    action.compose_measure_number = *number;
    action.compose_input_chart_id.value = view.chart_id.value;
    action.compose_input_document_revision = view.document_revision;
    close_go_to_measure(context);
}

static void render_go_to_measure(nk_context* context, const wormedit::ui::compose_view_t& view,
                                 wormedit::ui::workspace_action_t& action, bool window_focused) {
    if (!state.dialogs.go_to_measure_open) return;
    if (!window_focused || view.chart_id.value != state.dialogs.go_to_measure_chart_id ||
        view.document_revision != state.dialogs.go_to_measure_revision) {
        state.dialogs.go_to_measure_cancel_pending = true;
    }
    const auto region = nk_window_get_content_region(context);
    const auto parent = nk_window_get_bounds(context);
    const auto width = std::min(320.0f, std::max(160.0f, parent.w - 24.0f));
    const auto bounds = nk_rect(parent.x + (parent.w - width) * 0.5f - region.x,
                                parent.y + (parent.h - 120.0f) * 0.5f - region.y,
                                width, 120.0f);
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "Go to measure",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, bounds)) {
        state.dialogs.go_to_measure_open = false;
        state.dialogs.go_to_measure_focus_pending = false;
        return;
    }
    if (state.dialogs.go_to_measure_cancel_pending) {
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
    if (state.dialogs.go_to_measure_focus_pending) {
        nk_edit_focus(context, NK_EDIT_ALWAYS_INSERT_MODE);
        state.dialogs.go_to_measure_focus_pending = false;
    }
    const auto flags = wormedit::ui::chrome::worm_ui_nuklear_text_field(
        context, static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER,
        state.dialogs.go_to_measure_text.data(), static_cast<int>(state.dialogs.go_to_measure_text.size()),
        nk_filter_decimal);
    if ((flags & NK_EDIT_COMMITTED) != 0) confirm_go_to_measure(view, action, context);
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, state.dialogs.go_to_measure_error[0] ? state.dialogs.go_to_measure_error.data()
                                                : "Enter to go · Esc to cancel", NK_TEXT_LEFT);
    nk_popup_end(context);
}

static void begin_layer_menu_input_frame() {
    state.layer_editor.layers_menu_input_consumed = wormedit::ui::chrome::context_menu_owns_input(state.layer_editor.layers_menu);
}

static void close_hidden_layer_actions(bool visible) {
    if (visible) return;
    wormedit::ui::chrome::context_menu_abandon(state.layer_editor.layers_menu);
    state.layer_editor.semantic_layer_name_editor_visible = false;
    state.layer_editor.semantic_layer_name_focus_pending = false;
    state.layer_editor.semantic_layer_color_editor_visible = false;
}

struct ComposeDockLayout {
    struct nk_rect compose_body_bounds;
    float compose_body_height;
    bool inspector_drawn;
    bool branch_drawn;
    float branch_width;
    float side_width;
    std::array<bool, 6> available;
    wormedit::ui::dock_geometry_t right;
};

static void close_hidden_offset_editor(nk_context* context, bool inspector_drawn,
                                       bool timing_focus) {
    if (!state.inline_offset.inline_offset_open) return;
    const bool visible = state.inline_offset.inline_offset_in_inspector ? inspector_drawn
        : timing_focus && nk_window_get_bounds(context).w >= 900.0f;
    if (!visible) {
        worm_ui_nuklear_cancel_inline_offset(state);
        nk_edit_unfocus(context);
    }
}

static auto compose_dock_layout(nk_context* context, const wormedit::ui::compose_view_t* view)
    -> ComposeDockLayout {
    const auto bounds = nk_window_get_content_region(context);
    const auto height = std::max(64.0f, bounds.y + bounds.h - nk_widget_position(context).y -
                                        context->style.window.spacing.y);
    const auto notes = wormedit::ui::compose_notes_editable(compose_mode_of(*view));
    const std::array available{
        wormedit::ui::compose_inspector_available(compose_mode_of(*view)),
        notes && view->keysounds_available, view->scene_available, notes, notes, view->scene_available};
    const auto right = wormedit::ui::dock_geometry_t::from(bounds.w, height, state.dock_layout.layout, available);
    const auto inspector = right.is_visible(wormedit::dock_panel_t::inspector);
    close_hidden_layer_actions(right.is_visible(wormedit::dock_panel_t::layers));
    worm_ui_nuklear_dismiss_layer_popup(context);
    state.dock_layout.compact_dock_layout = bounds.w < 700.0f;
    const auto branch = state.dock_layout.branch_panel_visible &&
        worm_ui_nuklear_bms_branch_panel_visible(view) && bounds.w >= 900.0f;
    const auto branch_width = branch ? std::clamp(bounds.w * 0.2f, 260.0f, 320.0f) : 0.0f;
    state.dock_layout.branch_dock_width_drawn = branch ? branch_width + 8.0f : 0.0f;
    return {.compose_body_bounds = bounds, .compose_body_height = height, .inspector_drawn = inspector,
        .branch_drawn = branch, .branch_width = branch_width, .side_width = right.reserved_width,
        .available = available, .right = right};
}

static void render_compose_inspector_contents(nk_context* context,
                                               const wormedit::ui::compose_view_t* view,
                                               const wormedit::ui::timing_view_t* timing_view,
                                               wormedit::ui::workspace_action_t* action) {
    const ComposeInspectorContext inspector{context, view, timing_view, action};
    render_compose_recipe_form(inspector);
    render_compose_label_search(inspector);
    render_compose_quantize_section(inspector);
    render_compose_transform_section(inspector);
    render_compose_measure_section(inspector);

    render_compose_timing_inspector(inspector);
}

template <typename Render>
static void render_compose_dock(nk_context* context, struct nk_rect bounds, const char* name,
                                nk_flags flags, const Render& render) {
    nk_layout_space_push(context, bounds);
    if (nk_group_begin(context, name, flags)) {
        render();
        nk_group_end(context);
    }
}

namespace {
void render_dock_inspector(const ComposePanelRenderContext& panel) {
    nk_layout_row_dynamic(panel.context, 20, 1);
    nk_label(panel.context, wormedit::dock_panel_catalog[static_cast<std::size_t>(wormedit::dock_panel_t::inspector)].title.data(), NK_TEXT_LEFT);
    render_compose_inspector_contents(panel.context, panel.view, panel.timing_view, panel.action);
}
void render_dock_sounds(const ComposePanelRenderContext& panel) {
    worm_ui_nuklear_render_compose_sounds(panel.context, panel.view, panel.action, &state);
}
void render_dock_images(const ComposePanelRenderContext& panel) {
    worm_ui_nuklear_render_compose_bga(panel.context, panel.view, panel.action, &state);
}
void render_dock_measures(const ComposePanelRenderContext& panel) {
    worm_ui_nuklear_render_compose_measures(panel.context, panel.view, panel.action, &state);
}
void render_dock_layers(const ComposePanelRenderContext& panel) {
    worm_ui_nuklear_render_compose_layers(panel.context, panel.view, panel.action, &state);
}
void render_dock_markers(const ComposePanelRenderContext& panel) {
    if (state.bookmark_ui.bookmark_chooser.open) {
        nk_layout_row_dynamic(panel.context, 22, 1);
        nk_label(panel.context, "Bookmark chooser is open.", NK_TEXT_LEFT);
        return;
    }
    worm_ui_nuklear_render_bookmark_list(panel.context, panel.view->bookmark_chooser,
                                        panel.action, state.dock_layout.markers);
}
struct dock_panel_renderer_t {
    wormedit::dock_panel_t id;
    const char* group;
    nk_flags flags;
    void (*render)(const ComposePanelRenderContext&);
};
constexpr std::array dock_panel_renderers{
    dock_panel_renderer_t{wormedit::dock_panel_t::inspector, "Compose inspector", NK_WINDOW_BORDER, render_dock_inspector},
    dock_panel_renderer_t{wormedit::dock_panel_t::sounds, "Compose sounds", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, render_dock_sounds},
    dock_panel_renderer_t{wormedit::dock_panel_t::images, "Compose BGA", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, render_dock_images},
    dock_panel_renderer_t{wormedit::dock_panel_t::measures, "Compose measures", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, render_dock_measures},
    dock_panel_renderer_t{wormedit::dock_panel_t::layers, "Compose layers", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, render_dock_layers},
    dock_panel_renderer_t{wormedit::dock_panel_t::markers, "Compose markers", NK_WINDOW_BORDER, render_dock_markers}};
}
static void render_compose_side_docks(const ComposePanelRenderContext& panel,
                                       const ComposeDockLayout& layout) {
    for (const auto& pane : layout.right.panes) {
        if (!pane) continue;
        const auto renderer = std::ranges::find(dock_panel_renderers, pane->panel, &dock_panel_renderer_t::id);
        const auto& r = pane->bounds;
        render_compose_dock(panel.context, nk_rect(static_cast<float>(r.left), static_cast<float>(r.top),
            static_cast<float>(r.right - r.left), static_cast<float>(r.bottom - r.top)),
            renderer->group, renderer->flags, [&] { renderer->render(panel); });
    }
    wormedit::ui::render_dock_controls(panel.context, layout.right, state.dock_layout.layout,
        state.dock_layout.pointer, layout.available, panel.window_focused && !panel.view->input_occluded &&
        !compose_input_queries_t{state}.has_popup_input());
}

void worm_ui_nuklear_render_compose_panel(struct nk_context* context, const wormedit::ui::compose_view_t* view,
                                          const wormedit::ui::timing_view_t* timing_view,
                                          wormedit::ui::workspace_action_t* action, bool window_focused,
                                          bool ime_composition_active,
                                          const worm_ui_nuklear_pointer_click_t* double_clicks,
                                          std::size_t double_click_count,
                                          std::size_t shortcut_event_count) {
    if (!context || !view || !timing_view || !action || !view->visible ||
        (double_click_count != 0 && !double_clicks)) {
        return;
    }
    reset_compose_quantize_section_visibility();
    begin_layer_menu_input_frame();
    begin_compose_note_menu_frame();
    render_compose_toolbar({context, view, action});
    const ComposePanelRenderContext panel{context, view, timing_view, action, window_focused,
                                         ime_composition_active, double_clicks,
                                         double_click_count, shortcut_event_count};

    const auto dock_layout = compose_dock_layout(context, view);
    close_hidden_offset_editor(context, dock_layout.inspector_drawn, view->timing_focus);
    prepare_compose_quantize_input(context, *view, dock_layout.inspector_drawn, window_focused);
    const auto& compose_body_bounds = dock_layout.compose_body_bounds;
    const auto compose_body_height = dock_layout.compose_body_height;
    const auto inspector_drawn = dock_layout.inspector_drawn;
    const auto branch_drawn = dock_layout.branch_drawn;
    const auto branch_width = dock_layout.branch_width;
    const auto side_width = dock_layout.side_width;
    state.sounds_dock.sound_list_valid = false;
    state.bga_catalog.bga_list_valid = false;
    const auto editor_width = compose_body_bounds.w;
    const auto palette_drawn =
        wormedit::ui::compose_note_palette_visible(compose_mode_of(*view)) &&
        compose_note_palette_has_buttons(*view);
    constexpr float palette_width = 36.0f;
    const auto branch_left = palette_drawn ? palette_width : 0.0f;
    const auto editor_left = branch_left + (branch_drawn ? branch_width + 8.0f : 0.0f);
    nk_layout_space_begin(context, NK_STATIC, compose_body_height,
        1 + (branch_drawn ? 1 : 0) + (palette_drawn ? 1 : 0) + dock_layout.right.widget_count());
    if (palette_drawn) {
        nk_layout_space_push(context, nk_rect(0.0f, 0.0f, palette_width, compose_body_height));
        if (nk_group_begin(context, "Compose note palette", NK_WINDOW_NO_SCROLLBAR)) {
            render_compose_note_palette_column({context, view, action});
            nk_group_end(context);
        }
    }
    if (branch_drawn)
        render_compose_dock(context, nk_rect(branch_left, 0.0f, branch_width, compose_body_height),
            "BMS branches", NK_WINDOW_BORDER | NK_WINDOW_TITLE,
            [&] { worm_ui_nuklear_render_bms_branch_panel(context, view, action); });
    nk_layout_space_push(context, nk_rect(editor_left, 0.0f, editor_width - editor_left,
                                          compose_body_height));
    if (nk_group_begin(context, "Compose editor", NK_WINDOW_NO_SCROLLBAR)) {
    render_compose_editor_canvas(panel, true,
                                 side_width);
    nk_group_end(context);
    }
    render_compose_side_docks(panel, dock_layout);
    draw_compose_quantize_ghosts_after_docks(context, view,
        inspector_drawn && compose_quantize_section_visible());
    nk_layout_space_end(context);
    worm_ui_nuklear_render_bga_stack_picker(context, view, action, &state);
    render_compose_hitsound_menu({context, view, action});
    render_go_to_measure(context, *view, *action, window_focused);
    (void)worm_ui_nuklear_render_compose_bookmark_chooser(context, view->bookmark_chooser, action, window_focused, &state.bookmark_ui.bookmark_chooser);
    render_compose_sound_picker(context, *view, *action, state, window_focused);
}

bool worm_ui_nuklear_compose_lane_focused(const wormedit::ui::compose_view_t* view, bool compose_tab_active,
                                          bool window_focused) {
    return compose_tab_active && worm_ui_nuklear_compose_canvas_input_available(
        view, window_focused, state.inline_timing.inline_timing_open || state.dialogs.go_to_measure_open ||
            state.layer_editor.layers_menu.open || state.layer_editor.layers_menu_input_consumed || compose_quantize_owns_input() ||
            state.has_palette_context_menu() || compose_note_menu_consumed() ||
            state.bookmark_ui.bookmark_rename.active || state.bookmark_ui.bookmark_rename.release_pending ||
            (worm_ui_nuklear_bookmark_chooser_owns_input(state.bookmark_ui.bookmark_chooser) || compose_sound_picker_owns_input(state.sound_picker_ui.sound_picker)), false) &&
        view->cursor && view->cursor->lane_id.value != 0;
}

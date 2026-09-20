#include "ui/nuklear/timing_panel.h"
#include "ui/nuklear/timing_state.h"

#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>

static auto& state = worm_ui_nuklear_timing_state();

int worm_ui_nuklear_timing_view_valid(const WormUiTimingView* view) {
    if (!view || view->speed_count > WORM_UI_TIMING_SPEED_CAPACITY ||
        (view->speed_count != 0 && !view->speeds) ||
        view->marker_count > WORM_UI_TIMING_MARKER_CAPACITY ||
        (view->marker_count != 0 && !view->markers) ||
        !std::isfinite(view->viewport_width_pixels) || view->viewport_width_pixels <= 0.0 ||
        !std::isfinite(view->viewport_height_pixels) || view->viewport_height_pixels <= 0.0) {
        return 0;
    }
    if (view->scene_available &&
        (view->chart_id == 0 || view->document_revision == 0 || view->total_rows == 0)) {
        return 0;
    }
    for (size_t index = 0; index < view->speed_count; ++index) {
        const auto& speed = view->speeds[index];
        if (!std::isfinite(speed.marker_y_pixels) || !std::isfinite(speed.marker_left_fraction) ||
            !std::isfinite(speed.marker_right_fraction) || speed.marker_left_fraction < 0.0 ||
            speed.marker_right_fraction <= speed.marker_left_fraction ||
            speed.marker_right_fraction > 1.0 || !std::isfinite(speed.affected_begin_y_pixels) ||
            !std::isfinite(speed.affected_end_y_pixels) || !std::isfinite(speed.multiplier) ||
            !std::isfinite(speed.transition_duration) || speed.marker_y_pixels < 0.0 ||
            speed.marker_y_pixels > view->viewport_height_pixels ||
            speed.affected_begin_y_pixels < 0.0 ||
            speed.affected_end_y_pixels < speed.affected_begin_y_pixels ||
            speed.affected_end_y_pixels > view->viewport_height_pixels || speed.multiplier < 0.0 ||
            speed.transition_duration < 0.0) {
            return 0;
        }
    }
    for (size_t index = 0; index < view->marker_count; ++index) {
        const auto& marker = view->markers[index];
        if (!std::isfinite(marker.marker_y_pixels) || marker.marker_y_pixels < 0.0 ||
            marker.marker_y_pixels > view->viewport_height_pixels ||
            !std::isfinite(marker.marker_left_fraction) ||
            !std::isfinite(marker.marker_right_fraction) || marker.marker_left_fraction < 0.0 ||
            marker.marker_right_fraction <= marker.marker_left_fraction ||
            marker.marker_right_fraction > 1.0 || marker.lane < WORM_UI_TIMING_LANE_BPM ||
            marker.lane > WORM_UI_TIMING_LANE_ANCHOR) {
            return 0;
        }
    }
    if (view->grid_line_count > WORM_UI_TIMING_GRID_CAPACITY ||
        (view->grid_line_count != 0 && !view->grid_lines)) {
        return 0;
    }
    for (size_t index = 0; index < view->grid_line_count; ++index) {
        const auto& line = view->grid_lines[index];
        if (!std::isfinite(line.y_pixels) || line.y_pixels < 0.0 ||
            line.y_pixels > view->viewport_height_pixels || line.kind > 3 || line.emphasis > 3) {
            return 0;
        }
    }
    const auto& recipe = view->sv_recipe;
    if (recipe.candidate_point_count > WORM_UI_TIMING_RECIPE_POINT_CAPACITY ||
        (recipe.candidate_point_count != 0 && !recipe.candidate_points) ||
        recipe.composed_point_count > WORM_UI_TIMING_RECIPE_POINT_CAPACITY ||
        (recipe.composed_point_count != 0 && !recipe.composed_points)) {
        return 0;
    }
    if (recipe.selection_armed &&
        (recipe.chart_id == 0 || recipe.document_revision != view->document_revision ||
         recipe.chart_id != view->chart_id)) {
        return 0;
    }
    if (recipe.active &&
        (recipe.chart_id == 0 || recipe.document_revision != view->document_revision ||
         recipe.chart_id != view->chart_id || !std::isfinite(recipe.range_begin_fraction) ||
         !std::isfinite(recipe.range_end_fraction) || recipe.range_begin_fraction < 0.0 ||
         recipe.range_end_fraction <= recipe.range_begin_fraction ||
         recipe.range_end_fraction > 1.0 || recipe.selection_armed)) {
        return 0;
    }
    const auto form_valid = [](const WormUiTimingRecipeForm& form) {
        const auto terminated = [](const char* value, size_t size) {
            return std::memchr(value, '\0', size) != nullptr;
        };
        const WormUiTimingRecipeRationalText* rationals[] = {
            &form.value_from, &form.value_to,    &form.shape,
            &form.bump_start, &form.bump_length, &form.bump_x,
            &form.bump_y,     &form.tolerance,   &form.transition_duration,
        };
        for (const auto* rational : rationals) {
            if (!terminated(rational->numerator, sizeof(rational->numerator)) ||
                !terminated(rational->denominator, sizeof(rational->denominator))) {
                return false;
            }
        }
        return terminated(form.fixed_point_count, sizeof(form.fixed_point_count)) &&
               terminated(form.fixed_step_rows, sizeof(form.fixed_step_rows)) &&
               terminated(form.adaptive_max_points, sizeof(form.adaptive_max_points)) &&
               form.output >= WORM_UI_TIMING_RECIPE_OUTPUT_SV &&
               form.output <= WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS &&
               form.curve >= WORM_UI_TIMING_RECIPE_CURVE_CONSTANT &&
               form.curve <= WORM_UI_TIMING_RECIPE_CURVE_BUMP &&
               form.sampling_mode >= WORM_UI_TIMING_RECIPE_SAMPLING_FIXED &&
               form.sampling_mode <= WORM_UI_TIMING_RECIPE_SAMPLING_ADAPTIVE &&
               form.fixed_extent >= WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT &&
               form.fixed_extent <= WORM_UI_TIMING_RECIPE_FIXED_STEP_ROWS;
    };
    if (!form_valid(recipe.form)) {
        return 0;
    }
    const auto points_valid = [](const WormUiTimingRecipePoint* points, size_t count) {
        double previous_row_fraction = 0.0;
        for (size_t index = 0; index < count; ++index) {
            const auto& point = points[index];
            if (!std::isfinite(point.row_fraction) || !std::isfinite(point.value_fraction) ||
                point.row_fraction < 0.0 || point.row_fraction > 1.0 ||
                point.value_fraction < 0.0 || point.value_fraction > 1.0 ||
                (index != 0 && point.row_fraction < previous_row_fraction)) {
                return false;
            }
            previous_row_fraction = point.row_fraction;
        }
        return true;
    };
    if (!points_valid(recipe.candidate_points, recipe.candidate_point_count) ||
        !points_valid(recipe.composed_points, recipe.composed_point_count)) {
        return 0;
    }
    if (view->stage_active &&
        (!std::isfinite(view->stage_marker_y_pixels) || view->stage_marker_y_pixels < 0.0 ||
         view->stage_marker_y_pixels > view->viewport_height_pixels ||
         view->stage_lane < WORM_UI_TIMING_LANE_BPM ||
         view->stage_lane > WORM_UI_TIMING_LANE_ANCHOR ||
         view->stage_pause_unit < WORM_UI_TIMING_PAUSE_SECONDS ||
         view->stage_pause_unit > WORM_UI_TIMING_PAUSE_BEATS || !std::isfinite(view->stage_value) ||
         !std::isfinite(view->stage_secondary_value))) {
        return 0;
    }
    return 1;
}

static void set_timing_action(WormUiWorkspaceAction* action, const WormUiTimingView* view,
                              WormUiTimingStageAction kind, double primary = 0.0,
                              double secondary = 0.0, bool transition_is_seconds = false) {
    if (!action || !view) {
        return;
    }
    action->timing_stage_action = kind;
    action->timing_chart_id = view->chart_id;
    action->timing_document_revision = view->document_revision;
    action->timing_object_id = view->stage_existing_object_id;
    action->timing_value = primary;
    action->timing_secondary_value = secondary;
    action->timing_transition_is_seconds = transition_is_seconds;
    action->timing_toggle_primary = view->stage_toggle_primary;
    action->timing_pause_unit = view->stage_pause_unit;
}

static void copy_timing_text(char* destination, size_t capacity, const char* source) {
    if (!destination || capacity == 0) {
        return;
    }
    const auto length = source ? std::min(strlen(source), capacity - 1) : 0;
    if (length != 0) {
        std::memcpy(destination, source, length);
    }
    destination[length] = '\0';
}

static void copy_timing_form_to_action(WormUiWorkspaceAction* action) {
    if (!action) {
        return;
    }
    copy_timing_text(action->timing_bpm_decimal_text, sizeof(action->timing_bpm_decimal_text),
                     state.bpm_decimal.data());
    action->timing_bpm_decimal_edited = state.bpm_decimal_edited;
    copy_timing_text(action->timing_primary_numerator_text,
                     sizeof(action->timing_primary_numerator_text), state.primary_numerator.data());
    copy_timing_text(action->timing_primary_denominator_text,
                     sizeof(action->timing_primary_denominator_text),
                     state.primary_denominator.data());
    copy_timing_text(action->timing_secondary_numerator_text,
                     sizeof(action->timing_secondary_numerator_text),
                     state.secondary_numerator.data());
    copy_timing_text(action->timing_secondary_denominator_text,
                     sizeof(action->timing_secondary_denominator_text),
                     state.secondary_denominator.data());
}

static void set_timing_recipe_action(WormUiWorkspaceAction* action,
                                     const WormUiTimingRecipeDraftView* view,
                                     WormUiTimingRecipeActionKind kind, double range_begin = 0.0,
                                     double range_end = 0.0) {
    if (!action || !view) {
        return;
    }
    auto& recipe_action = action->timing_recipe_action;
    recipe_action.kind = kind;
    recipe_action.chart_id = view->chart_id;
    recipe_action.document_revision = view->document_revision;
    recipe_action.range_begin_fraction = range_begin;
    recipe_action.range_end_fraction = range_end;
    recipe_action.form = state.recipe_buffers_active ? state.recipe_form : view->form;
}

static void cancel_timing_recipe_range_drag() {
    state.recipe_range_drag_active = false;
    state.recipe_range_drag_mode = worm_ui_nuklear_timing_recipe_range_drag_mode_t::new_range;
    state.recipe_drag_moved = false;
}

static void cancel_timing_marker_drag() {
    state.marker_drag_active = false;
    state.marker_drag_moved = false;
    state.marker_drag_chart = 0;
    state.marker_drag_revision = 0;
    state.marker_drag_object = 0;
}

void worm_ui_nuklear_timing_handle_focus_lost() {
    state.recipe_cancel_pending = true;
    cancel_timing_recipe_range_drag();
    if (state.marker_drag_active) {
        state.marker_drag_cancel_pending = true;
    }
    cancel_timing_marker_drag();
}

void worm_ui_nuklear_reconcile_timing_gestures(const WormUiTimingView* view, bool timing_tab_active,
                                               bool window_focused) {
    const bool marker_matches =
        !state.marker_drag_active ||
        (view && timing_tab_active && window_focused && view->visible && view->scene_available &&
         view->chart_id == state.marker_drag_chart &&
         view->document_revision == state.marker_drag_revision);
    if (!marker_matches) {
        cancel_timing_marker_drag();
    }

    bool recipe_matches = !state.recipe_range_drag_active;
    if (state.recipe_range_drag_active && view && timing_tab_active && window_focused &&
        view->visible && view->scene_available && !view->stage_active &&
        view->chart_id == state.recipe_drag_chart &&
        view->document_revision == state.recipe_drag_revision) {
        recipe_matches = state.recipe_range_drag_mode ==
                                 worm_ui_nuklear_timing_recipe_range_drag_mode_t::new_range
                             ? view->sv_recipe.selection_armed
                             : view->sv_recipe.active;
    }
    if (!recipe_matches) {
        cancel_timing_recipe_range_drag();
    }
}

void worm_ui_nuklear_emit_timing_cancellations(const WormUiTimingView* view, bool timing_tab_active,
                                               WormUiWorkspaceAction* action) {
    if (!action) {
        return;
    }
    if (state.recipe_cancel_pending) {
        if (view && timing_tab_active && view->visible &&
            (view->sv_recipe.active || view->sv_recipe.selection_armed)) {
            set_timing_recipe_action(action, &view->sv_recipe, WORM_UI_TIMING_RECIPE_ACTION_CANCEL);
        }
        state.recipe_cancel_pending = false;
    }
    if (state.marker_drag_cancel_pending) {
        if (view && timing_tab_active && view->visible && view->stage_active &&
            view->stage_existing_object_id != 0) {
            set_timing_action(action, view, WORM_UI_TIMING_STAGE_CANCEL);
        }
        state.marker_drag_cancel_pending = false;
    }
}

static void ensure_timing_recipe_buffers(const WormUiTimingRecipeDraftView* view) {
    if (!view || !view->active) {
        state.recipe_buffers_active = false;
        return;
    }
    if (state.recipe_buffers_active && state.recipe_buffers_chart == view->chart_id &&
        state.recipe_buffers_revision == view->document_revision &&
        state.recipe_buffers_id == view->recipe_id) {
        return;
    }
    state.recipe_form = view->form;
    state.recipe_buffers_active = true;
    state.recipe_buffers_chart = view->chart_id;
    state.recipe_buffers_revision = view->document_revision;
    state.recipe_buffers_id = view->recipe_id;
}

static auto timing_recipe_output_label(WormUiTimingRecipeOutput output) -> const char* {
    switch (output) {
    case WORM_UI_TIMING_RECIPE_OUTPUT_SV:
        return "SV";
    case WORM_UI_TIMING_RECIPE_OUTPUT_SCROLLS:
        return "SCROLLS";
    case WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS:
        return "SPEEDS";
    }
    return "SV";
}

static auto timing_recipe_curve_label(WormUiTimingRecipeCurve curve) -> const char* {
    switch (curve) {
    case WORM_UI_TIMING_RECIPE_CURVE_CONSTANT:
        return "Constant";
    case WORM_UI_TIMING_RECIPE_CURVE_LINEAR:
        return "Linear";
    case WORM_UI_TIMING_RECIPE_CURVE_EXPONENTIAL:
        return "Exponential";
    case WORM_UI_TIMING_RECIPE_CURVE_BUMP:
        return "Bump";
    }
    return "Linear";
}

static auto timing_recipe_form_rational(struct nk_context* context, const char* label,
                                        WormUiTimingRecipeRationalText* rational) -> bool {
    if (!rational) {
        return false;
    }
    bool changed = false;
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 4);
    nk_layout_row_push(context, 0.22f);
    nk_label(context, label, NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.34f);
    changed |= nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, rational->numerator,
                                              sizeof(rational->numerator), nk_filter_float) != 0;
    nk_layout_row_push(context, 0.08f);
    nk_label(context, "/", NK_TEXT_CENTERED);
    nk_layout_row_push(context, 0.34f);
    changed |=
        nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, rational->denominator,
                                       sizeof(rational->denominator), nk_filter_decimal) != 0;
    nk_layout_row_end(context);
    return changed;
}

static void render_timing_recipe_form(struct nk_context* context, const WormUiTimingView* view,
                                      WormUiWorkspaceAction* action) {
    const auto& draft = view->sv_recipe;
    ensure_timing_recipe_buffers(&draft);
    if (!draft.active) {
        return;
    }
    bool changed = false;
    auto& form = state.recipe_form;

    nk_layout_row_dynamic(context, 22, 1);
    nk_labelf(context, NK_TEXT_LEFT, "SV recipe draft · range %.4f to %.4f",
              draft.range_begin_fraction, draft.range_end_fraction);
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, "Amber: raw candidate · Mint: composed baseline × recipe", NK_TEXT_LEFT);
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 4);
    nk_layout_row_push(context, 0.18f);
    nk_label(context, "Output", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.32f);
    if (nk_button_label(context, timing_recipe_output_label(form.output))) {
        form.output =
            static_cast<WormUiTimingRecipeOutput>((static_cast<int>(form.output) + 1) % 3);
        changed = true;
    }
    nk_layout_row_push(context, 0.18f);
    nk_label(context, "Curve", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.32f);
    if (nk_button_label(context, timing_recipe_curve_label(form.curve))) {
        form.curve = static_cast<WormUiTimingRecipeCurve>((static_cast<int>(form.curve) + 1) % 4);
        changed = true;
    }
    nk_layout_row_end(context);

    if (form.curve == WORM_UI_TIMING_RECIPE_CURVE_CONSTANT) {
        changed |= timing_recipe_form_rational(context, "Value", &form.value_from);
    } else if (form.curve == WORM_UI_TIMING_RECIPE_CURVE_LINEAR) {
        changed |= timing_recipe_form_rational(context, "From", &form.value_from);
        changed |= timing_recipe_form_rational(context, "To", &form.value_to);
    } else if (form.curve == WORM_UI_TIMING_RECIPE_CURVE_EXPONENTIAL) {
        changed |= timing_recipe_form_rational(context, "From", &form.value_from);
        changed |= timing_recipe_form_rational(context, "To", &form.value_to);
        changed |= timing_recipe_form_rational(context, "Shape", &form.shape);
    } else {
        changed |= timing_recipe_form_rational(context, "Bump start", &form.bump_start);
        changed |= timing_recipe_form_rational(context, "Bump length", &form.bump_length);
        changed |= timing_recipe_form_rational(context, "Bump X", &form.bump_x);
        changed |= timing_recipe_form_rational(context, "Bump Y", &form.bump_y);
    }
    if (form.output == WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS) {
        changed |= timing_recipe_form_rational(context, "Transition", &form.transition_duration);
        nk_layout_row_dynamic(context, 22, 1);
        nk_bool transition_is_seconds = form.transition_is_seconds ? 1 : 0;
        if (nk_checkbox_label(context, "SPEEDS transition uses seconds", &transition_is_seconds)) {
            form.transition_is_seconds = transition_is_seconds != 0;
            changed = true;
        }
    }

    nk_layout_row_begin(context, NK_DYNAMIC, 26, 4);
    nk_layout_row_push(context, 0.18f);
    nk_label(context, "Sampling", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.32f);
    if (nk_button_label(context, form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED
                                     ? "Fixed"
                                     : "Adaptive")) {
        form.sampling_mode = form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED
                                 ? WORM_UI_TIMING_RECIPE_SAMPLING_ADAPTIVE
                                 : WORM_UI_TIMING_RECIPE_SAMPLING_FIXED;
        changed = true;
    }
    nk_layout_row_push(context, 0.18f);
    if (form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED) {
        nk_label(context, "Fixed extent", NK_TEXT_LEFT);
    } else {
        nk_label(context, "Adaptive", NK_TEXT_LEFT);
    }
    nk_layout_row_push(context, 0.32f);
    if (form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED) {
        if (nk_button_label(context, form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT
                                         ? "Point count"
                                         : "Row step")) {
            form.fixed_extent = form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT
                                    ? WORM_UI_TIMING_RECIPE_FIXED_STEP_ROWS
                                    : WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT;
            changed = true;
        }
    } else {
        nk_label(context, "error tolerance", NK_TEXT_LEFT);
    }
    nk_layout_row_end(context);

    if (form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED) {
        nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
        nk_layout_row_push(context, 0.22f);
        nk_label(context,
                 form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT ? "Points"
                                                                              : "Step rows",
                 NK_TEXT_LEFT);
        nk_layout_row_push(context, 0.78f);
        if (form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT) {
            changed |= nk_edit_string_zero_terminated(
                           context, NK_EDIT_FIELD, form.fixed_point_count,
                           sizeof(form.fixed_point_count), nk_filter_decimal) != 0;
        } else {
            changed |= nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, form.fixed_step_rows,
                                                      sizeof(form.fixed_step_rows),
                                                      nk_filter_decimal) != 0;
        }
        nk_layout_row_end(context);
    } else {
        changed |= timing_recipe_form_rational(context, "Tolerance", &form.tolerance);
        nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
        nk_layout_row_push(context, 0.22f);
        nk_label(context, "Max points", NK_TEXT_LEFT);
        nk_layout_row_push(context, 0.78f);
        changed |= nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, form.adaptive_max_points,
                                                  sizeof(form.adaptive_max_points),
                                                  nk_filter_decimal) != 0;
        nk_layout_row_end(context);
    }

    if (draft.status_text && draft.status_text[0]) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, draft.status_text, NK_TEXT_LEFT);
    }
    nk_layout_row_begin(context, NK_DYNAMIC, 28, 2);
    nk_layout_row_push(context, 0.50f);
    if (!draft.valid) {
        nk_widget_disable_begin(context);
    }
    const auto apply_clicked = nk_button_label(context, "Apply SV recipe");
    if (!draft.valid) {
        nk_widget_disable_end(context);
    }
    nk_layout_row_push(context, 0.50f);
    const auto cancel_clicked = nk_button_label(context, "Cancel recipe");
    nk_layout_row_end(context);
    if (action && action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
        if (cancel_clicked) {
            set_timing_recipe_action(action, &draft, WORM_UI_TIMING_RECIPE_ACTION_CANCEL);
        } else if (apply_clicked && draft.valid) {
            set_timing_recipe_action(action, &draft, WORM_UI_TIMING_RECIPE_ACTION_APPLY);
        } else if (changed) {
            set_timing_recipe_action(action, &draft, WORM_UI_TIMING_RECIPE_ACTION_UPDATE);
        }
    }
}

void worm_ui_nuklear_render_timing_panel(struct nk_context* context, const WormUiTimingView* view,
                                         WormUiWorkspaceAction* action, bool window_focused,
                                         const worm_ui_nuklear_timing_double_click_t* double_clicks,
                                         std::size_t double_click_count) {
    if (!context || !view || !action || !view->visible ||
        (double_click_count != 0 && !double_clicks)) {
        return;
    }
    if (!view->stage_active) {
        state.stage_buffers_active = false;
    } else if (!state.stage_buffers_active || state.stage_buffers_chart != view->chart_id ||
               state.stage_buffers_revision != view->document_revision ||
               state.stage_buffers_object != view->stage_existing_object_id ||
               state.stage_buffers_lane != view->stage_lane ||
               state.stage_buffers_y != view->stage_marker_y_pixels) {
        copy_timing_text(state.bpm_decimal.data(), state.bpm_decimal.size(),
                         view->stage_primary_decimal_text);
        copy_timing_text(state.primary_numerator.data(), state.primary_numerator.size(),
                         view->stage_primary_numerator_text);
        copy_timing_text(state.primary_denominator.data(), state.primary_denominator.size(),
                         view->stage_primary_denominator_text);
        copy_timing_text(state.secondary_numerator.data(), state.secondary_numerator.size(),
                         view->stage_secondary_numerator_text);
        copy_timing_text(state.secondary_denominator.data(), state.secondary_denominator.size(),
                         view->stage_secondary_denominator_text);
        copy_timing_text(state.view_primary_numerator.data(), state.view_primary_numerator.size(),
                         view->stage_primary_numerator_text);
        copy_timing_text(state.view_primary_denominator.data(),
                         state.view_primary_denominator.size(),
                         view->stage_primary_denominator_text);
        state.bpm_decimal_edited = false;
        state.bpm_advanced_fraction = false;
        state.stage_buffers_active = true;
        state.stage_buffers_chart = view->chart_id;
        state.stage_buffers_revision = view->document_revision;
        state.stage_buffers_object = view->stage_existing_object_id;
        state.stage_buffers_lane = view->stage_lane;
        state.stage_buffers_y = view->stage_marker_y_pixels;
    }
    if (view->stage_active &&
        (strcmp(state.view_primary_numerator.data(), view->stage_primary_numerator_text) != 0 ||
         strcmp(state.view_primary_denominator.data(), view->stage_primary_denominator_text) !=
             0)) {
        copy_timing_text(state.bpm_decimal.data(), state.bpm_decimal.size(),
                         view->stage_primary_decimal_text);
        copy_timing_text(state.primary_numerator.data(), state.primary_numerator.size(),
                         view->stage_primary_numerator_text);
        copy_timing_text(state.primary_denominator.data(), state.primary_denominator.size(),
                         view->stage_primary_denominator_text);
        copy_timing_text(state.view_primary_numerator.data(), state.view_primary_numerator.size(),
                         view->stage_primary_numerator_text);
        copy_timing_text(state.view_primary_denominator.data(),
                         state.view_primary_denominator.size(),
                         view->stage_primary_denominator_text);
        state.bpm_decimal_edited = false;
    }
    const bool can_change_grid = !view->stage_active && !view->sv_recipe.active &&
                                 !view->sv_recipe.selection_armed && view->scene_available &&
                                 view->supported_snap_denominator_count > 0;
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 3);
    nk_layout_row_push(context, 0.42f);
    nk_labelf(context, NK_TEXT_LEFT, "Active grid: 1/%llu%s",
              static_cast<unsigned long long>(view->active_snap_denominator),
              view->active_snap_denominator == 0 ? " (unavailable)" : "");
    nk_layout_row_push(context, 0.29f);
    if (!can_change_grid) {
        nk_widget_disable_begin(context);
    }
    const auto previous_grid_clicked = nk_button_label(context, "Previous grid");
    if (!can_change_grid) {
        nk_widget_disable_end(context);
    }
    nk_layout_row_push(context, 0.29f);
    if (!can_change_grid) {
        nk_widget_disable_begin(context);
    }
    const auto next_grid_clicked = nk_button_label(context, "Next grid");
    if (!can_change_grid) {
        nk_widget_disable_end(context);
    }
    nk_layout_row_end(context);
    if (action && action->timing_grid_action == WORM_UI_TIMING_GRID_ACTION_NONE &&
        (previous_grid_clicked || next_grid_clicked)) {
        size_t current = 0;
        while (current < view->supported_snap_denominator_count &&
               view->supported_snap_denominators[current] != view->active_snap_denominator) {
            ++current;
        }
        if (current < view->supported_snap_denominator_count) {
            const auto count = view->supported_snap_denominator_count;
            const auto next =
                previous_grid_clicked ? (current + count - 1) % count : (current + 1) % count;
            action->timing_grid_action = WORM_UI_TIMING_GRID_ACTION_SELECT;
            action->timing_grid_chart_id = view->chart_id;
            action->timing_grid_document_revision = view->document_revision;
            action->timing_grid_denominator = view->supported_snap_denominators[next];
        }
    }
    const bool can_apply_meter = !view->stage_active && !view->sv_recipe.active &&
                                 !view->sv_recipe.selection_armed && view->scene_available &&
                                 view->measure_count != 0;
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 5);
    nk_layout_row_push(context, 0.23f);
    nk_label(context, "Meter at measure", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.18f);
    if (!can_apply_meter)
        nk_widget_disable_begin(context);
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state.meter_measure.data(),
                                         state.meter_measure.size(), nk_filter_decimal);
    if (!can_apply_meter)
        nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.16f);
    if (!can_apply_meter)
        nk_widget_disable_begin(context);
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state.meter_numerator.data(),
                                         state.meter_numerator.size(), nk_filter_decimal);
    if (!can_apply_meter)
        nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.16f);
    if (!can_apply_meter)
        nk_widget_disable_begin(context);
    (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state.meter_denominator.data(),
                                         state.meter_denominator.size(), nk_filter_decimal);
    if (!can_apply_meter)
        nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.27f);
    if (!can_apply_meter)
        nk_widget_disable_begin(context);
    const auto apply_meter_clicked = nk_button_label(context, "Apply meter");
    if (!can_apply_meter)
        nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if (action && action->timing_meter_action == WORM_UI_TIMING_METER_ACTION_NONE &&
        apply_meter_clicked) {
        action->timing_meter_action = WORM_UI_TIMING_METER_ACTION_APPLY;
        action->timing_meter_chart_id = view->chart_id;
        action->timing_meter_document_revision = view->document_revision;
        copy_timing_text(action->timing_meter_measure_text,
                         sizeof(action->timing_meter_measure_text), state.meter_measure.data());
        copy_timing_text(action->timing_meter_numerator_text,
                         sizeof(action->timing_meter_numerator_text), state.meter_numerator.data());
        copy_timing_text(action->timing_meter_denominator_text,
                         sizeof(action->timing_meter_denominator_text),
                         state.meter_denominator.data());
    }
    nk_layout_row_dynamic(context, 24, 1);
    nk_labelf(context, NK_TEXT_LEFT,
              "%zu measures · %zu BPM · %zu stop/delay · %zu warp · %zu SPEEDS",
              view->measure_count, view->bpm_count, view->pause_count, view->warp_count,
              view->speed_total_count);
    nk_layout_row_dynamic(context, 18, 1);
    nk_labelf(context, NK_TEXT_LEFT,
              "%zu bookmarks/sections · %zu anchors · Hover a SPEEDS marker for its exact span; "
              "Double-click a marker to edit its exact value, or an empty row to insert on the "
              "current active timing grid. Coincident markers split left-to-right by source order.",
              view->bookmark_count + view->section_count, view->anchor_count);

    nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
    nk_layout_row_push(context, 0.38f);
    if (view->sv_recipe.selection_armed) {
        if (nk_button_label(context, "Cancel SV recipe selection") && action &&
            action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
            set_timing_recipe_action(action, &view->sv_recipe, WORM_UI_TIMING_RECIPE_ACTION_CANCEL);
        }
    } else if (!view->sv_recipe.active) {
        const bool can_arm_recipe =
            !view->stage_active && view->scene_available && view->chart_id != 0;
        if (!can_arm_recipe) {
            nk_widget_disable_begin(context);
        }
        const auto arm_clicked = nk_button_label(context, "New SV recipe");
        if (!can_arm_recipe) {
            nk_widget_disable_end(context);
        }
        if (arm_clicked && can_arm_recipe && action &&
            action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
            set_timing_recipe_action(action, &view->sv_recipe,
                                     WORM_UI_TIMING_RECIPE_ACTION_ARM_SELECTION);
        }
    } else {
        nk_label(context,
                 view->stage_active ? "Finish or cancel marker edit before resnapping this range"
                                    : "Drag a boundary or the interval body to resnap it",
                 NK_TEXT_LEFT);
    }
    nk_layout_row_push(context, 0.62f);
    if (view->sv_recipe.selection_armed) {
        nk_label(context, "Drag vertically in Scroll / SV to choose an exact range.", NK_TEXT_LEFT);
    } else if (view->sv_recipe.active) {
        nk_label(context, "Candidate and composed curves are preview only until Apply.",
                 NK_TEXT_LEFT);
    } else {
        nk_label(context, "Parametric curves stay editable in the project.", NK_TEXT_LEFT);
    }
    nk_layout_row_end(context);

    const auto group_bounds = nk_window_get_content_region(context);
    const auto reserved_panel_height =
        120.0f + (view->stage_active ? 212.0f : 0.0f) + (view->sv_recipe.active ? 300.0f : 0.0f);
    const auto surface_height = std::max(32.0f, group_bounds.h - reserved_panel_height);
    nk_layout_space_begin(context, NK_DYNAMIC, surface_height, 1);
    nk_layout_space_push(context, nk_rect(0.0f, 0.0f, 1.0f, 1.0f));
    struct nk_rect viewport{};
    const auto widget_state = nk_widget(&viewport, context);
    nk_layout_space_end(context);
    if (widget_state == NK_WIDGET_INVALID || viewport.w <= 1.0f || viewport.h <= 1.0f) {
        cancel_timing_recipe_range_drag();
        return;
    }

    auto* canvas = nk_window_get_canvas(context);
    if (!canvas) {
        cancel_timing_recipe_range_drag();
        return;
    }
    const auto parent_clip = canvas->clip;
    nk_push_scissor(canvas, viewport);
    nk_fill_rect(canvas, viewport, 0.0f, nk_rgb(22, 25, 33));
    if (!view->scene_available) {
        cancel_timing_recipe_range_drag();
        const auto* status = view->status_text ? view->status_text : "Timing lane is unavailable.";
        nk_draw_text(canvas, viewport, status, static_cast<int>(strlen(status)),
                     context->style.font, nk_rgb(22, 25, 33), nk_rgb(190, 196, 207));
        nk_push_scissor(canvas, parent_clip);
        return;
    }

    const auto screen_y = [&](double y_pixels) {
        return viewport.y + static_cast<float>(y_pixels) * viewport.h /
                                static_cast<float>(view->viewport_height_pixels);
    };
    constexpr std::array<WormUiTimingLaneKind, 8> lane_kinds{
        WORM_UI_TIMING_LANE_BPM,  WORM_UI_TIMING_LANE_SCROLL, WORM_UI_TIMING_LANE_SPEED,
        WORM_UI_TIMING_LANE_STOP, WORM_UI_TIMING_LANE_DELAY,  WORM_UI_TIMING_LANE_WARP,
        WORM_UI_TIMING_LANE_KIAI, WORM_UI_TIMING_LANE_ANCHOR};
    constexpr std::array<const char*, 8> lane_labels{"BPM",   "Scroll / SV", "SPEEDS", "Stop",
                                                     "Delay", "Warp",        "Kiai",   "Anchor"};
    const std::array<struct nk_color, 8> lane_colors{
        nk_rgb(242, 190, 102), nk_rgb(177, 206, 129), nk_rgb(113, 184, 218), nk_rgb(224, 132, 119),
        nk_rgb(207, 153, 104), nk_rgb(176, 142, 218), nk_rgb(244, 116, 174), nk_rgb(130, 219, 206)};
    const auto lane_left = viewport.x + viewport.w * 0.025f;
    const auto lane_width = viewport.w * 0.95f / static_cast<float>(lane_kinds.size());
    std::array<struct nk_rect, 8> lanes{};
    for (size_t index = 0; index < lanes.size(); ++index) {
        lanes[index] = nk_rect(lane_left + lane_width * static_cast<float>(index) + 2.0f,
                               viewport.y, std::max(1.0f, lane_width - 4.0f), viewport.h);
        nk_fill_rect(canvas, lanes[index], 0.0f, nk_rgb(31, 37, 49));
        nk_stroke_line(canvas, lanes[index].x, lanes[index].y, lanes[index].x,
                       lanes[index].y + lanes[index].h, 1.0f, nk_rgb(66, 76, 94));
        nk_stroke_line(canvas, lanes[index].x + lanes[index].w, lanes[index].y,
                       lanes[index].x + lanes[index].w, lanes[index].y + lanes[index].h, 1.0f,
                       nk_rgb(66, 76, 94));
        const auto title = nk_rect(lanes[index].x + 3.0f, viewport.y + 3.0f,
                                   std::max(0.0f, lanes[index].w - 6.0f), 18.0f);
        nk_draw_text(canvas, title, lane_labels[index],
                     static_cast<int>(strlen(lane_labels[index])), context->style.font,
                     nk_rgba(22, 25, 33, 190), lane_colors[index]);
    }
    for (size_t index = 0; index < view->grid_line_count; ++index) {
        const auto& line = view->grid_lines[index];
        const auto y = screen_y(line.y_pixels);
        auto color = nk_rgb(42, 49, 63);
        float thickness = 0.7f;
        if (line.kind == 3) {
            color = nk_rgb(95, 107, 128);
            thickness = 1.2f;
        } else if (line.kind == 1 || line.kind == 2) {
            color = nk_rgb(60, 70, 88);
            thickness = line.emphasis >= 2 ? 1.1f : 0.9f;
        } else if (line.emphasis >= 2) {
            color = nk_rgb(52, 61, 78);
        }
        nk_stroke_line(canvas, viewport.x, y, viewport.x + viewport.w, y, thickness, color);
    }
    const auto recipe_lane_index =
        view->sv_recipe.active && view->sv_recipe.form.output == WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS
            ? std::size_t{2}
            : std::size_t{1};
    const auto& recipe_lane = lanes[recipe_lane_index];

    const bool pointer_inside = nk_input_is_mouse_hovering_rect(&context->input, viewport);
    const auto pointer_y = context->input.mouse.pos.y;
    size_t hovered = view->speed_count;
    auto nearest_distance = std::numeric_limits<float>::max();
    const auto& speed_lane = lanes[2];
    const auto marker_horizontal_bounds = [](const struct nk_rect& lane, double left_fraction,
                                             double right_fraction) {
        const auto inner_left = lane.x + 4.0f;
        const auto inner_width = std::max(0.0f, lane.w - 8.0f);
        const auto left = inner_left + inner_width * static_cast<float>(left_fraction);
        const auto right = inner_left + inner_width * static_cast<float>(right_fraction);
        return std::pair{left, right};
    };
    const auto normalized_y_fraction = [&](double y) {
        return std::clamp((y - static_cast<double>(viewport.y)) / static_cast<double>(viewport.h),
                          0.0, 1.0);
    };
    const auto current_recipe_drag_range = [&]() -> std::pair<double, double> {
        const auto current = state.recipe_drag_current_fraction;
        switch (state.recipe_range_drag_mode) {
        case worm_ui_nuklear_timing_recipe_range_drag_mode_t::new_range:
            return std::pair{std::min(state.recipe_drag_anchor_fraction, current),
                             std::max(state.recipe_drag_anchor_fraction, current)};
        case worm_ui_nuklear_timing_recipe_range_drag_mode_t::begin_handle:
            return std::pair{std::min(current, state.recipe_drag_original_end),
                             std::max(current, state.recipe_drag_original_end)};
        case worm_ui_nuklear_timing_recipe_range_drag_mode_t::end_handle:
            return std::pair{std::min(state.recipe_drag_original_begin, current),
                             std::max(state.recipe_drag_original_begin, current)};
        case worm_ui_nuklear_timing_recipe_range_drag_mode_t::whole_span: {
            const auto minimum_delta = -state.recipe_drag_original_begin;
            const auto maximum_delta = 1.0 - state.recipe_drag_original_end;
            const auto delta = std::clamp(current - state.recipe_drag_anchor_fraction,
                                          minimum_delta, maximum_delta);
            return std::pair{state.recipe_drag_original_begin + delta,
                             state.recipe_drag_original_end + delta};
        }
        }
        return std::pair{0.0, 0.0};
    };
    const auto& left_button = context->input.mouse.buttons[NK_BUTTON_LEFT];
    const auto pointer_started_in_view = left_button.down && left_button.clicked != 0 &&
                                         left_button.clicked_pos.x >= viewport.x &&
                                         left_button.clicked_pos.x <= viewport.x + viewport.w &&
                                         left_button.clicked_pos.y >= viewport.y &&
                                         left_button.clicked_pos.y <= viewport.y + viewport.h;
    const auto pointer_started_in_recipe_lane =
        pointer_started_in_view && left_button.clicked_pos.x >= recipe_lane.x &&
        left_button.clicked_pos.x <= recipe_lane.x + recipe_lane.w;
    const auto marker_at_pointer =
        [&](float x, float y) -> std::optional<std::pair<WormUiTimingLaneKind, uint64_t>> {
        for (size_t lane_index = 0; lane_index < lanes.size(); ++lane_index) {
            if (x < lanes[lane_index].x || x > lanes[lane_index].x + lanes[lane_index].w) {
                continue;
            }
            uint64_t object_id = 0;
            auto nearest = std::numeric_limits<float>::max();
            const auto consider = [&](uint64_t candidate, double left_fraction,
                                      double right_fraction, double marker_y) {
                const auto [left, right] =
                    marker_horizontal_bounds(lanes[lane_index], left_fraction, right_fraction);
                if (x < left || x > right) {
                    return;
                }
                const auto distance = std::abs(y - screen_y(marker_y));
                if (distance <= 7.0f && distance <= nearest) {
                    object_id = candidate;
                    nearest = distance;
                }
            };
            for (size_t marker = 0; marker < view->marker_count; ++marker) {
                const auto& candidate = view->markers[marker];
                if (candidate.lane == lane_kinds[lane_index]) {
                    consider(candidate.object_id, candidate.marker_left_fraction,
                             candidate.marker_right_fraction, candidate.marker_y_pixels);
                }
            }
            if (lane_kinds[lane_index] == WORM_UI_TIMING_LANE_SPEED) {
                for (size_t marker = 0; marker < view->speed_count; ++marker) {
                    const auto& candidate = view->speeds[marker];
                    consider(candidate.object_id, candidate.marker_left_fraction,
                             candidate.marker_right_fraction, candidate.marker_y_pixels);
                }
            }
            if (object_id != 0) {
                return std::pair{lane_kinds[lane_index], object_id};
            }
        }
        return std::nullopt;
    };
    if (!state.marker_drag_active && !view->stage_active && !view->sv_recipe.active &&
        !view->sv_recipe.selection_armed && window_focused && pointer_started_in_view) {
        if (const auto marker =
                marker_at_pointer(left_button.clicked_pos.x, left_button.clicked_pos.y)) {
            state.marker_drag_active = true;
            state.marker_drag_moved = false;
            state.marker_drag_chart = view->chart_id;
            state.marker_drag_revision = view->document_revision;
            state.marker_drag_lane = marker->first;
            state.marker_drag_object = marker->second;
            state.marker_drag_anchor_screen_y = left_button.clicked_pos.y;
        }
    }
    if (!state.recipe_range_drag_active && !view->stage_active && action &&
        action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE &&
        !state.marker_drag_active && window_focused && pointer_started_in_recipe_lane) {
        const bool armed_selection = view->sv_recipe.selection_armed && !view->sv_recipe.active;
        const auto clicked_fraction = normalized_y_fraction(left_button.clicked_pos.y);
        const auto selected_begin = view->sv_recipe.range_begin_fraction;
        const auto selected_end = view->sv_recipe.range_end_fraction;
        const bool hit_selected_interval = view->sv_recipe.active &&
                                           clicked_fraction >= selected_begin - 0.01 &&
                                           clicked_fraction <= selected_end + 0.01;
        if (armed_selection || hit_selected_interval) {
            state.recipe_range_drag_active = true;
            state.recipe_drag_original_begin = selected_begin;
            state.recipe_drag_original_end = selected_end;
            if (armed_selection) {
                state.recipe_range_drag_mode =
                    worm_ui_nuklear_timing_recipe_range_drag_mode_t::new_range;
            } else {
                const auto handle_tolerance =
                    std::max(0.005, 6.0 / static_cast<double>(viewport.h));
                if (std::abs(clicked_fraction - selected_begin) <= handle_tolerance) {
                    state.recipe_range_drag_mode =
                        worm_ui_nuklear_timing_recipe_range_drag_mode_t::begin_handle;
                } else if (std::abs(clicked_fraction - selected_end) <= handle_tolerance) {
                    state.recipe_range_drag_mode =
                        worm_ui_nuklear_timing_recipe_range_drag_mode_t::end_handle;
                } else {
                    state.recipe_range_drag_mode =
                        worm_ui_nuklear_timing_recipe_range_drag_mode_t::whole_span;
                }
            }
            state.recipe_drag_chart = view->chart_id;
            state.recipe_drag_revision = view->document_revision;
            state.recipe_drag_anchor_fraction = clicked_fraction;
            state.recipe_drag_current_fraction = clicked_fraction;
            state.recipe_drag_anchor_screen_y = left_button.clicked_pos.y;
            state.recipe_drag_moved = false;
        }
    }
    const bool left_down = left_button.down != 0;
    const bool left_released = nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT);
    if (state.marker_drag_active) {
        const bool matches_drag = state.marker_drag_chart == view->chart_id &&
                                  state.marker_drag_revision == view->document_revision;
        if (!window_focused || !view->scene_available || !matches_drag ||
            (!left_down && !left_released)) {
            cancel_timing_marker_drag();
        } else {
            const auto distance =
                std::abs(context->input.mouse.pos.y - state.marker_drag_anchor_screen_y);
            state.marker_drag_moved = state.marker_drag_moved || distance >= 4.0f;
            if (state.marker_drag_moved && action &&
                action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE &&
                action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
                const auto target = std::min(normalized_y_fraction(context->input.mouse.pos.y),
                                             std::nextafter(1.0, 0.0));
                set_timing_action(action, view,
                                  view->stage_active ? WORM_UI_TIMING_STAGE_MOVE
                                                     : WORM_UI_TIMING_STAGE_BEGIN);
                action->timing_chart_id = state.marker_drag_chart;
                action->timing_document_revision = state.marker_drag_revision;
                action->timing_lane = state.marker_drag_lane;
                action->timing_object_id = state.marker_drag_object;
                action->timing_has_target_y = true;
                action->timing_target_y_fraction = target;
            }
            if (left_released) {
                cancel_timing_marker_drag();
            }
        }
    }
    if (state.recipe_range_drag_active) {
        const bool view_matches_drag =
            state.recipe_drag_chart == view->chart_id &&
            state.recipe_drag_revision == view->document_revision &&
            (state.recipe_range_drag_mode ==
                     worm_ui_nuklear_timing_recipe_range_drag_mode_t::new_range
                 ? view->sv_recipe.selection_armed
                 : view->sv_recipe.active);
        if (!window_focused || !view->scene_available || view->stage_active || !view_matches_drag ||
            (!left_down && !left_released)) {
            cancel_timing_recipe_range_drag();
        } else {
            state.recipe_drag_current_fraction = normalized_y_fraction(context->input.mouse.pos.y);
            const auto distance =
                std::abs(context->input.mouse.pos.y - state.recipe_drag_anchor_screen_y);
            state.recipe_drag_moved = state.recipe_drag_moved || distance >= 4.0f;
            if (left_released) {
                if (state.recipe_drag_moved && action &&
                    action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
                    const auto [begin, end] = current_recipe_drag_range();
                    const auto kind =
                        state.recipe_range_drag_mode ==
                                worm_ui_nuklear_timing_recipe_range_drag_mode_t::new_range
                            ? WORM_UI_TIMING_RECIPE_ACTION_BEGIN
                            : WORM_UI_TIMING_RECIPE_ACTION_UPDATE_RANGE;
                    set_timing_recipe_action(action, &view->sv_recipe, kind, begin, end);
                }
                cancel_timing_recipe_range_drag();
            }
        }
    }
    if (pointer_inside && context->input.mouse.pos.x >= speed_lane.x &&
        context->input.mouse.pos.x <= speed_lane.x + speed_lane.w) {
        for (size_t index = 0; index < view->speed_count; ++index) {
            const auto [marker_left, marker_right] =
                marker_horizontal_bounds(speed_lane, view->speeds[index].marker_left_fraction,
                                         view->speeds[index].marker_right_fraction);
            if (context->input.mouse.pos.x < marker_left ||
                context->input.mouse.pos.x > marker_right) {
                continue;
            }
            const auto distance =
                std::abs(pointer_y - screen_y(view->speeds[index].marker_y_pixels));
            if (distance <= 7.0f && distance <= nearest_distance) {
                hovered = index;
                nearest_distance = distance;
            }
        }
    }

    if (hovered != view->speed_count) {
        const auto& speed = view->speeds[hovered];
        auto top = screen_y(speed.affected_begin_y_pixels);
        auto bottom = screen_y(speed.affected_end_y_pixels);
        if (bottom - top < 3.0f) {
            bottom = std::min(viewport.y + viewport.h, top + 3.0f);
        }
        nk_fill_rect(canvas, nk_rect(speed_lane.x, top, speed_lane.w, std::max(0.0f, bottom - top)),
                     0.0f, nk_rgba(136, 203, 238, 54));
        nk_stroke_line(canvas, speed_lane.x, top, speed_lane.x + speed_lane.w, top, 1.0f,
                       nk_rgb(137, 207, 239));
        nk_stroke_line(canvas, speed_lane.x, bottom, speed_lane.x + speed_lane.w, bottom, 1.0f,
                       nk_rgb(91, 145, 177));
    }

    for (size_t index = 0; index < view->marker_count; ++index) {
        const auto& marker = view->markers[index];
        for (size_t lane_index = 0; lane_index < lane_kinds.size(); ++lane_index) {
            if (marker.lane != lane_kinds[lane_index]) {
                continue;
            }
            const auto y = screen_y(marker.marker_y_pixels);
            const auto [marker_left, marker_right] = marker_horizontal_bounds(
                lanes[lane_index], marker.marker_left_fraction, marker.marker_right_fraction);
            nk_fill_rect(
                canvas,
                nk_rect(marker_left, y - 2.0f, std::max(0.0f, marker_right - marker_left), 4.0f),
                0.0f, lane_colors[lane_index]);
            break;
        }
    }

    double selected_begin = view->sv_recipe.range_begin_fraction;
    double selected_end = view->sv_recipe.range_end_fraction;
    if (state.recipe_range_drag_active) {
        std::tie(selected_begin, selected_end) = current_recipe_drag_range();
    }
    if ((view->sv_recipe.active || state.recipe_range_drag_active) &&
        selected_end > selected_begin) {
        const auto top = viewport.y + static_cast<float>(selected_begin) * viewport.h;
        const auto bottom = viewport.y + static_cast<float>(selected_end) * viewport.h;
        const struct nk_rect selected =
            nk_rect(recipe_lane.x + 1.0f, top, recipe_lane.w - 2.0f, std::max(1.0f, bottom - top));
        nk_fill_rect(canvas, selected, 0.0f, nk_rgba(180, 160, 230, 32));
        nk_stroke_line(canvas, selected.x, selected.y, selected.x + selected.w, selected.y, 1.0f,
                       nk_rgb(196, 173, 242));
        nk_stroke_line(canvas, selected.x, selected.y + selected.h, selected.x + selected.w,
                       selected.y + selected.h, 1.0f, nk_rgb(196, 173, 242));
    }
    if (view->sv_recipe.active) {
        const auto draw_recipe_curve = [&](const WormUiTimingRecipePoint* points, size_t count,
                                           struct nk_color color, float thickness) {
            if (!points || count == 0) {
                return;
            }
            const auto point_x = [&](const WormUiTimingRecipePoint& point) {
                const auto [inner_left, inner_right] = marker_horizontal_bounds(
                    recipe_lane, point.value_fraction, point.value_fraction);
                return (inner_left + inner_right) * 0.5f;
            };
            const auto point_y = [&](const WormUiTimingRecipePoint& point) {
                return viewport.y + static_cast<float>(point.row_fraction) * viewport.h;
            };
            for (size_t index = 0; index < count; ++index) {
                const auto x = point_x(points[index]);
                const auto y = point_y(points[index]);
                if (index != 0) {
                    nk_stroke_line(canvas, point_x(points[index - 1]), point_y(points[index - 1]),
                                   x, y, thickness, color);
                } else {
                    nk_fill_rect(canvas, nk_rect(x - 2.0f, y - 2.0f, 4.0f, 4.0f), 0.0f, color);
                }
                if (index + 1 == count) {
                    nk_fill_rect(canvas, nk_rect(x - 2.0f, y - 2.0f, 4.0f, 4.0f), 0.0f, color);
                }
            }
        };
        draw_recipe_curve(view->sv_recipe.candidate_points, view->sv_recipe.candidate_point_count,
                          nk_rgb(246, 183, 106), 2.0f);
        draw_recipe_curve(view->sv_recipe.composed_points, view->sv_recipe.composed_point_count,
                          nk_rgb(139, 229, 181), 1.5f);
    }
    for (size_t index = 0; index < view->speed_count; ++index) {
        const auto y = screen_y(view->speeds[index].marker_y_pixels);
        const auto color = index == hovered ? nk_rgb(191, 232, 248) : nk_rgb(113, 184, 218);
        const auto [marker_left, marker_right] =
            marker_horizontal_bounds(speed_lane, view->speeds[index].marker_left_fraction,
                                     view->speeds[index].marker_right_fraction);
        nk_fill_rect(
            canvas,
            nk_rect(marker_left, y - 2.0f, std::max(0.0f, marker_right - marker_left), 4.0f), 0.0f,
            color);
    }
    if (view->stage_active) {
        const auto stage_y = screen_y(view->stage_marker_y_pixels);
        for (size_t index = 0; index < lane_kinds.size(); ++index) {
            if (lane_kinds[index] == view->stage_lane) {
                nk_stroke_line(canvas, lanes[index].x + 2.0f, stage_y,
                               lanes[index].x + lanes[index].w - 2.0f, stage_y, 3.0f,
                               view->stage_valid ? nk_rgb(249, 238, 154) : nk_rgb(237, 116, 104));
                break;
            }
        }
    }
    if (hovered != view->speed_count) {
        const auto& speed = view->speeds[hovered];
        char label[192]{};
        const auto count =
            snprintf(label, sizeof(label), "SPEEDS #%llu  %.4gx over %.4g %s%s",
                     static_cast<unsigned long long>(speed.object_id), speed.multiplier,
                     speed.transition_duration, speed.transition_is_seconds ? "seconds" : "beats",
                     speed.transition_clipped ? " (clipped by next event or chart end)" : "");
        if (count > 0) {
            const auto text_bounds = nk_rect(viewport.x + 8.0f, viewport.y + 24.0f,
                                             std::max(0.0f, viewport.w - 16.0f), 20.0f);
            nk_draw_text(canvas, text_bounds, label,
                         static_cast<int>(std::min<size_t>(strlen(label), sizeof(label) - 1)),
                         context->style.font, nk_rgba(22, 25, 33, 220), nk_rgb(207, 232, 244));
        }
    }
    if (view->truncated) {
        const char* warning = "Additional timing markers are hidden by the frontend cap.";
        const auto text_bounds = nk_rect(viewport.x + 8.0f, viewport.y + viewport.h - 24.0f,
                                         std::max(0.0f, viewport.w - 16.0f), 18.0f);
        nk_draw_text(canvas, text_bounds, warning, static_cast<int>(strlen(warning)),
                     context->style.font, nk_rgba(22, 25, 33, 220), nk_rgb(224, 184, 103));
    }
    nk_push_scissor(canvas, parent_clip);

    if (!view->stage_active && action && action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE &&
        action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
        for (size_t click = 0; click < double_click_count; ++click) {
            const auto& pointer = double_clicks[click];
            if (pointer.x < viewport.x || pointer.x >= viewport.x + viewport.w ||
                pointer.y < viewport.y || pointer.y >= viewport.y + viewport.h) {
                continue;
            }
            for (size_t index = 0; index < lanes.size(); ++index) {
                if (pointer.x < lanes[index].x || pointer.x >= lanes[index].x + lanes[index].w) {
                    continue;
                }
                uint64_t existing_object_id = 0;
                auto existing_distance = std::numeric_limits<float>::max();
                for (size_t marker = 0; marker < view->marker_count; ++marker) {
                    if (view->markers[marker].lane != lane_kinds[index]) {
                        continue;
                    }
                    const auto [marker_left, marker_right] = marker_horizontal_bounds(
                        lanes[index], view->markers[marker].marker_left_fraction,
                        view->markers[marker].marker_right_fraction);
                    if (pointer.x < marker_left || pointer.x > marker_right) {
                        continue;
                    }
                    const auto distance =
                        std::abs(pointer.y - screen_y(view->markers[marker].marker_y_pixels));
                    if (distance <= 7.0f && distance <= existing_distance) {
                        existing_object_id = view->markers[marker].object_id;
                        existing_distance = distance;
                    }
                }
                if (lane_kinds[index] == WORM_UI_TIMING_LANE_SPEED) {
                    for (size_t marker = 0; marker < view->speed_count; ++marker) {
                        const auto [marker_left, marker_right] = marker_horizontal_bounds(
                            lanes[index], view->speeds[marker].marker_left_fraction,
                            view->speeds[marker].marker_right_fraction);
                        if (pointer.x < marker_left || pointer.x > marker_right) {
                            continue;
                        }
                        const auto distance =
                            std::abs(pointer.y - screen_y(view->speeds[marker].marker_y_pixels));
                        if (distance <= 7.0f && distance <= existing_distance) {
                            existing_object_id = view->speeds[marker].object_id;
                            existing_distance = distance;
                        }
                    }
                }
                set_timing_action(action, view, WORM_UI_TIMING_STAGE_BEGIN);
                action->timing_lane = lane_kinds[index];
                action->timing_object_id = existing_object_id;
                if (existing_object_id == 0) {
                    action->timing_has_target_y = true;
                    action->timing_target_y_fraction = static_cast<double>(pointer.y - viewport.y) /
                                                       static_cast<double>(viewport.h);
                }
                break;
            }
            if (action->timing_stage_action != WORM_UI_TIMING_STAGE_NONE) {
                break;
            }
        }
    }

    if (view->stage_active) {
        nk_bool seconds = view->stage_transition_is_seconds ? 1 : 0;
        nk_bool toggle_primary = view->stage_toggle_primary ? 1 : 0;
        bool changed = false;
        int bpm_step = 0;
        const char* primary_label = "Value";
        const char* pause_unit_label = "seconds";
        switch (view->stage_pause_unit) {
        case WORM_UI_TIMING_PAUSE_SECONDS:
            pause_unit_label = "seconds";
            break;
        case WORM_UI_TIMING_PAUSE_MILLISECONDS:
            pause_unit_label = "milliseconds";
            break;
        case WORM_UI_TIMING_PAUSE_BMS_STOP_TICKS:
            pause_unit_label = "BMS STOP ticks";
            break;
        case WORM_UI_TIMING_PAUSE_BEATS:
            pause_unit_label = "beats";
            break;
        }
        if (view->stage_lane == WORM_UI_TIMING_LANE_BPM) {
            primary_label = "BPM";
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_SCROLL) {
            primary_label = "Scroll multiplier";
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_SPEED) {
            primary_label = "SPEEDS multiplier";
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_STOP ||
                   view->stage_lane == WORM_UI_TIMING_LANE_DELAY) {
            primary_label = "Duration";
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_ANCHOR) {
            primary_label = "Source sample frame";
        }
        nk_layout_row_dynamic(context, 22, 1);
        if (view->stage_existing_object_id != 0) {
            nk_labelf(context, NK_TEXT_LEFT,
                      "Editing timing event #%llu — drag its row to move the staged preview.",
                      static_cast<unsigned long long>(view->stage_existing_object_id));
        } else {
            nk_label(context, "Staged timing event — nothing is written until Confirm.",
                     NK_TEXT_LEFT);
        }
        if (view->stage_lane == WORM_UI_TIMING_LANE_KIAI) {
            nk_layout_row_dynamic(context, 24, 1);
            changed |= nk_checkbox_label(context, "Kiai enabled", &toggle_primary) != 0;
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_ANCHOR) {
            nk_layout_row_dynamic(context, 20, 1);
            nk_label(context, "Exact source sample frame", NK_TEXT_LEFT);
            nk_layout_row_begin(context, NK_DYNAMIC, 28, 2);
            nk_layout_row_push(context, 0.38f);
            nk_label(context, primary_label, NK_TEXT_LEFT);
            nk_layout_row_push(context, 0.62f);
            changed |= nk_edit_string_zero_terminated(
                           context, NK_EDIT_FIELD, state.primary_numerator.data(),
                           state.primary_numerator.size(), nk_filter_decimal) != 0;
            nk_layout_row_end(context);
            nk_layout_row_dynamic(context, 24, 1);
            changed |= nk_checkbox_label(context, "Lock anchor", &toggle_primary) != 0;
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_BPM) {
            nk_layout_row_dynamic(context, 20, 1);
            nk_label(context,
                     view->stage_primary_decimal_exact
                         ? "Decimal BPM (exact)"
                         : "Decimal BPM (approximate display; exact fraction retained)",
                     NK_TEXT_LEFT);
            nk_layout_row_begin(context, NK_DYNAMIC, 28, 4);
            nk_layout_row_push(context, 0.18f);
            nk_label(context, primary_label, NK_TEXT_LEFT);
            nk_layout_row_push(context, 0.56f);
            const bool decimal_changed =
                nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, state.bpm_decimal.data(),
                                               state.bpm_decimal.size(), nk_filter_float) != 0;
            nk_layout_row_push(context, 0.13f);
            if (nk_button_label(context, "-1")) {
                bpm_step = -1;
            }
            nk_layout_row_push(context, 0.13f);
            if (nk_button_label(context, "+1")) {
                bpm_step = 1;
            }
            nk_layout_row_end(context);
            if (decimal_changed) {
                state.bpm_decimal_edited = true;
                changed = true;
            }
            changed = changed || bpm_step != 0;
            nk_bool advanced_fraction = state.bpm_advanced_fraction ? 1 : 0;
            nk_layout_row_dynamic(context, 24, 1);
            if (nk_checkbox_label(context, "Advanced exact fraction", &advanced_fraction) != 0) {
                state.bpm_advanced_fraction = advanced_fraction != 0;
            }
            if (state.bpm_advanced_fraction) {
                nk_layout_row_begin(context, NK_DYNAMIC, 28, 4);
                nk_layout_row_push(context, 0.24f);
                nk_label(context, "Exact BPM", NK_TEXT_LEFT);
                nk_layout_row_push(context, 0.30f);
                const bool numerator_changed =
                    nk_edit_string_zero_terminated(
                        context, NK_EDIT_FIELD, state.primary_numerator.data(),
                        state.primary_numerator.size(), nk_filter_float) != 0;
                nk_layout_row_push(context, 0.05f);
                nk_label(context, "/", NK_TEXT_CENTERED);
                nk_layout_row_push(context, 0.30f);
                const bool denominator_changed =
                    nk_edit_string_zero_terminated(
                        context, NK_EDIT_FIELD, state.primary_denominator.data(),
                        state.primary_denominator.size(), nk_filter_decimal) != 0;
                nk_layout_row_end(context);
                if (numerator_changed || denominator_changed) {
                    state.bpm_decimal_edited = false;
                    changed = true;
                }
            } else {
                nk_layout_row_dynamic(context, 18, 1);
                nk_labelf(context, NK_TEXT_LEFT, "Exact value: %s / %s",
                          state.primary_numerator.data(), state.primary_denominator.data());
            }
        } else {
            nk_layout_row_dynamic(context, 20, 1);
            nk_label(context, "Exact rational value: numerator / denominator", NK_TEXT_LEFT);
            nk_layout_row_begin(context, NK_DYNAMIC, 28, 4);
            nk_layout_row_push(context, 0.24f);
            nk_label(context, primary_label, NK_TEXT_LEFT);
            nk_layout_row_push(context, 0.30f);
            changed |= nk_edit_string_zero_terminated(
                           context, NK_EDIT_FIELD, state.primary_numerator.data(),
                           state.primary_numerator.size(), nk_filter_float) != 0;
            nk_layout_row_push(context, 0.05f);
            nk_label(context, "/", NK_TEXT_CENTERED);
            nk_layout_row_push(context, 0.30f);
            changed |= nk_edit_string_zero_terminated(
                           context, NK_EDIT_FIELD, state.primary_denominator.data(),
                           state.primary_denominator.size(), nk_filter_decimal) != 0;
            nk_layout_row_end(context);
        }
        if (view->stage_lane == WORM_UI_TIMING_LANE_SPEED) {
            nk_layout_row_begin(context, NK_DYNAMIC, 28, 4);
            nk_layout_row_push(context, 0.24f);
            nk_label(context, "Transition", NK_TEXT_LEFT);
            nk_layout_row_push(context, 0.30f);
            changed |= nk_edit_string_zero_terminated(
                           context, NK_EDIT_FIELD, state.secondary_numerator.data(),
                           state.secondary_numerator.size(), nk_filter_decimal) != 0;
            nk_layout_row_push(context, 0.05f);
            nk_label(context, "/", NK_TEXT_CENTERED);
            nk_layout_row_push(context, 0.30f);
            changed |= nk_edit_string_zero_terminated(
                           context, NK_EDIT_FIELD, state.secondary_denominator.data(),
                           state.secondary_denominator.size(), nk_filter_decimal) != 0;
            nk_layout_row_end(context);
            nk_layout_row_dynamic(context, 22, 1);
            changed |= nk_checkbox_label(context, "Seconds", &seconds) != 0;
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_WARP) {
            nk_layout_row_begin(context, NK_DYNAMIC, 28, 3);
            nk_layout_row_push(context, 0.38f);
            nk_label(context,
                     view->stage_existing_object_id != 0 ? "Length in exact chart rows"
                                                         : "Length in current grid steps",
                     NK_TEXT_LEFT);
            nk_layout_row_push(context, 0.34f);
            changed |= nk_edit_string_zero_terminated(
                           context, NK_EDIT_FIELD, state.secondary_numerator.data(),
                           state.secondary_numerator.size(), nk_filter_decimal) != 0;
            nk_layout_row_push(context, 0.28f);
            nk_label(context,
                     view->stage_existing_object_id != 0 ? "rows (denominator 1)"
                                                         : "steps (denominator 1)",
                     NK_TEXT_LEFT);
            nk_layout_row_end(context);
        } else if (view->stage_lane == WORM_UI_TIMING_LANE_STOP ||
                   view->stage_lane == WORM_UI_TIMING_LANE_DELAY) {
            nk_layout_row_dynamic(context, 18, 1);
            nk_labelf(context, NK_TEXT_LEFT, "Pause duration uses its authored unit: %s.",
                      pause_unit_label);
        }
        if (view->stage_status_text && view->stage_status_text[0]) {
            nk_layout_row_dynamic(context, 20, 1);
            nk_label(context, view->stage_status_text, NK_TEXT_LEFT);
        }
        nk_layout_row_begin(context, NK_DYNAMIC, 28, view->stage_existing_object_id != 0 ? 3 : 2);
        nk_layout_row_push(context, view->stage_existing_object_id != 0 ? 0.40f : 0.50f);
        const auto confirm_clicked =
            nk_button_label(context, view->stage_existing_object_id != 0 ? "Apply timing edit"
                                                                         : "Confirm timing event");
        nk_layout_row_push(context, view->stage_existing_object_id != 0 ? 0.30f : 0.50f);
        const auto cancel_clicked = nk_button_label(context, "Cancel");
        bool delete_clicked = false;
        if (view->stage_existing_object_id != 0) {
            nk_layout_row_push(context, 0.30f);
            delete_clicked = nk_button_label(context, "Delete timing event");
        }
        nk_layout_row_end(context);
        if (action && action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE && changed) {
            set_timing_action(action, view, WORM_UI_TIMING_STAGE_UPDATE, view->stage_value,
                              view->stage_secondary_value, seconds != 0);
        }
        if (action && action->timing_stage_action != WORM_UI_TIMING_STAGE_NONE) {
            action->timing_transition_is_seconds = seconds != 0;
            action->timing_toggle_primary = toggle_primary != 0;
            copy_timing_form_to_action(action);
            action->timing_bpm_step = bpm_step;
        }
        if (action && delete_clicked) {
            set_timing_action(action, view, WORM_UI_TIMING_STAGE_DELETE, view->stage_value,
                              view->stage_secondary_value, seconds != 0);
            copy_timing_form_to_action(action);
        } else if (action && cancel_clicked) {
            set_timing_action(action, view, WORM_UI_TIMING_STAGE_CANCEL, view->stage_value,
                              view->stage_secondary_value, seconds != 0);
            copy_timing_form_to_action(action);
        } else if (action && confirm_clicked) {
            set_timing_action(action, view, WORM_UI_TIMING_STAGE_CONFIRM, view->stage_value,
                              view->stage_secondary_value, seconds != 0);
            copy_timing_form_to_action(action);
        }
        if (action && action->timing_stage_action != WORM_UI_TIMING_STAGE_NONE) {
            action->timing_toggle_primary = toggle_primary != 0;
        }
    }
    render_timing_recipe_form(context, view, action);
}

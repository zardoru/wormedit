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

struct TimingPanel {
    nk_context* context;
    const WormUiTimingView* view;
    WormUiWorkspaceAction* action;
    bool window_focused;
    const worm_ui_nuklear_pointer_click_t* double_clicks;
    std::size_t double_click_count;
};

struct TimingCanvas {
    TimingPanel panel;
    struct nk_rect viewport{};
    struct nk_rect parent_clip{};
    struct nk_command_buffer* commands{};
    std::array<struct nk_rect, 8> lanes{};
    std::size_t hovered{};
};

static constexpr std::array<WormUiTimingLaneKind, 8> timing_lane_kinds{
    WORM_UI_TIMING_LANE_BPM, WORM_UI_TIMING_LANE_SCROLL, WORM_UI_TIMING_LANE_SPEED,
    WORM_UI_TIMING_LANE_STOP, WORM_UI_TIMING_LANE_DELAY, WORM_UI_TIMING_LANE_WARP,
    WORM_UI_TIMING_LANE_KIAI, WORM_UI_TIMING_LANE_ANCHOR};
static constexpr std::array<const char*, 8> timing_lane_labels{
    "BPM", "Scroll / SV", "SPEEDS", "Stop", "Delay", "Warp", "Kiai", "Anchor"};

static auto valid_timing_view_header(const WormUiTimingView& view) -> bool {
    return view.speed_count <= WORM_UI_TIMING_SPEED_CAPACITY &&
        (view.speed_count == 0 || view.speeds) &&
        view.marker_count <= WORM_UI_TIMING_MARKER_CAPACITY &&
        (view.marker_count == 0 || view.markers) &&
        std::isfinite(view.viewport_width_pixels) && view.viewport_width_pixels > 0.0 &&
        std::isfinite(view.viewport_height_pixels) && view.viewport_height_pixels > 0.0 &&
        (!view.scene_available ||
         (view.chart_id != 0 && view.document_revision != 0 && view.total_rows != 0));
}

static auto valid_speed_marker(const WormUiTimingView& view,
                               const WormUiTimingSpeedView& speed) -> bool {
    return std::isfinite(speed.marker_y_pixels) && speed.marker_y_pixels >= 0.0 &&
        speed.marker_y_pixels <= view.viewport_height_pixels &&
        std::isfinite(speed.marker_left_fraction) &&
        std::isfinite(speed.marker_right_fraction) && speed.marker_left_fraction >= 0.0 &&
        speed.marker_right_fraction > speed.marker_left_fraction &&
        speed.marker_right_fraction <= 1.0 && std::isfinite(speed.affected_begin_y_pixels) &&
        std::isfinite(speed.affected_end_y_pixels) &&
        speed.affected_begin_y_pixels >= 0.0 &&
        speed.affected_end_y_pixels >= speed.affected_begin_y_pixels &&
        speed.affected_end_y_pixels <= view.viewport_height_pixels &&
        std::isfinite(speed.multiplier) && speed.multiplier >= 0.0 &&
        std::isfinite(speed.transition_duration) && speed.transition_duration >= 0.0;
}

static auto valid_timing_markers(const WormUiTimingView& view) -> bool {
    for (size_t index = 0; index < view.marker_count; ++index) {
        const auto& marker = view.markers[index];
        if (!std::isfinite(marker.marker_y_pixels) || marker.marker_y_pixels < 0.0 ||
            marker.marker_y_pixels > view.viewport_height_pixels ||
            !std::isfinite(marker.marker_left_fraction) ||
            !std::isfinite(marker.marker_right_fraction) || marker.marker_left_fraction < 0.0 ||
            marker.marker_right_fraction <= marker.marker_left_fraction ||
            marker.marker_right_fraction > 1.0 || marker.lane < WORM_UI_TIMING_LANE_BPM ||
            marker.lane > WORM_UI_TIMING_LANE_ANCHOR) return false;
    }
    return true;
}

static auto valid_timing_grid(const WormUiTimingView& view) -> bool {
    if (view.grid_line_count > WORM_UI_TIMING_GRID_CAPACITY ||
        (view.grid_line_count != 0 && !view.grid_lines)) return false;
    for (size_t index = 0; index < view.grid_line_count; ++index) {
        const auto& line = view.grid_lines[index];
        if (!std::isfinite(line.y_pixels) || line.y_pixels < 0.0 ||
            line.y_pixels > view.viewport_height_pixels || line.kind > 3 || line.emphasis > 3)
            return false;
    }
    return true;
}

static auto terminated(const char* value, size_t size) -> bool {
    return std::memchr(value, '\0', size) != nullptr;
}

static auto valid_timing_recipe_form(const WormUiTimingRecipeForm& form) -> bool {
    const WormUiTimingRecipeRationalText* rationals[] = {
        &form.value_from, &form.value_to, &form.shape, &form.bump_start, &form.bump_length,
        &form.bump_x, &form.bump_y, &form.tolerance, &form.transition_duration};
    for (const auto* rational : rationals)
        if (!terminated(rational->numerator, sizeof(rational->numerator)) ||
            !terminated(rational->denominator, sizeof(rational->denominator))) return false;
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
}

static auto valid_timing_recipe_points(const WormUiTimingRecipePoint* points, size_t count)
    -> bool {
    double previous = 0.0;
    for (size_t index = 0; index < count; ++index) {
        const auto& point = points[index];
        if (!std::isfinite(point.row_fraction) || !std::isfinite(point.value_fraction) ||
            point.row_fraction < 0.0 || point.row_fraction > 1.0 ||
            point.value_fraction < 0.0 || point.value_fraction > 1.0 ||
            (index != 0 && point.row_fraction < previous)) return false;
        previous = point.row_fraction;
    }
    return true;
}

static auto valid_timing_recipe(const WormUiTimingView& view) -> bool {
    const auto& recipe = view.sv_recipe;
    if (recipe.candidate_point_count > WORM_UI_TIMING_RECIPE_POINT_CAPACITY ||
        (recipe.candidate_point_count != 0 && !recipe.candidate_points) ||
        recipe.composed_point_count > WORM_UI_TIMING_RECIPE_POINT_CAPACITY ||
        (recipe.composed_point_count != 0 && !recipe.composed_points) ||
        !valid_timing_recipe_form(recipe.form)) return false;
    const auto identity_matches = recipe.chart_id != 0 &&
        recipe.document_revision == view.document_revision && recipe.chart_id == view.chart_id;
    if (recipe.selection_armed && !identity_matches) return false;
    if (recipe.active && (!identity_matches || !std::isfinite(recipe.range_begin_fraction) ||
        !std::isfinite(recipe.range_end_fraction) || recipe.range_begin_fraction < 0.0 ||
        recipe.range_end_fraction <= recipe.range_begin_fraction ||
        recipe.range_end_fraction > 1.0 || recipe.selection_armed)) return false;
    return valid_timing_recipe_points(recipe.candidate_points, recipe.candidate_point_count) &&
        valid_timing_recipe_points(recipe.composed_points, recipe.composed_point_count);
}

static auto valid_timing_stage(const WormUiTimingView& view) -> bool {
    if (!view.stage_active) return true;
    return std::isfinite(view.stage_marker_y_pixels) && view.stage_marker_y_pixels >= 0.0 &&
        view.stage_marker_y_pixels <= view.viewport_height_pixels &&
        view.stage_lane >= WORM_UI_TIMING_LANE_BPM &&
        view.stage_lane <= WORM_UI_TIMING_LANE_ANCHOR &&
        view.stage_pause_unit >= WORM_UI_TIMING_PAUSE_SECONDS &&
        view.stage_pause_unit <= WORM_UI_TIMING_PAUSE_BEATS &&
        std::isfinite(view.stage_value) && std::isfinite(view.stage_secondary_value);
}

int worm_ui_nuklear_timing_view_valid(const WormUiTimingView* view) {
    if (!view || !valid_timing_view_header(*view) || !valid_timing_grid(*view) ||
        !valid_timing_recipe(*view) || !valid_timing_stage(*view) ||
        !valid_timing_markers(*view)) return 0;
    for (size_t index = 0; index < view->speed_count; ++index)
        if (!valid_speed_marker(*view, view->speeds[index])) return 0;
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
    action->timing_lane = view->stage_lane;
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

struct TimingRecipeEditor {
    nk_context* context;
    const WormUiTimingRecipeDraftView* draft;
    WormUiWorkspaceAction* action;
    bool changed{};
};

static void render_recipe_header(TimingRecipeEditor& editor) {
    auto* context = editor.context;
    const auto& draft = *editor.draft;
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
        form.output = static_cast<WormUiTimingRecipeOutput>((static_cast<int>(form.output) + 1) % 3);
        editor.changed = true;
    }
    nk_layout_row_push(context, 0.18f);
    nk_label(context, "Curve", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.32f);
    if (nk_button_label(context, timing_recipe_curve_label(form.curve))) {
        form.curve = static_cast<WormUiTimingRecipeCurve>((static_cast<int>(form.curve) + 1) % 4);
        editor.changed = true;
    }
    nk_layout_row_end(context);
}

static void render_recipe_curve_parameters(TimingRecipeEditor& editor) {
    auto* context = editor.context;
    auto& form = state.recipe_form;
    if (form.curve == WORM_UI_TIMING_RECIPE_CURVE_CONSTANT) {
        editor.changed |= timing_recipe_form_rational(context, "Value", &form.value_from);
    } else if (form.curve == WORM_UI_TIMING_RECIPE_CURVE_LINEAR) {
        editor.changed |= timing_recipe_form_rational(context, "From", &form.value_from);
        editor.changed |= timing_recipe_form_rational(context, "To", &form.value_to);
    } else if (form.curve == WORM_UI_TIMING_RECIPE_CURVE_EXPONENTIAL) {
        editor.changed |= timing_recipe_form_rational(context, "From", &form.value_from);
        editor.changed |= timing_recipe_form_rational(context, "To", &form.value_to);
        editor.changed |= timing_recipe_form_rational(context, "Shape", &form.shape);
    } else {
        editor.changed |= timing_recipe_form_rational(context, "Bump start", &form.bump_start);
        editor.changed |= timing_recipe_form_rational(context, "Bump length", &form.bump_length);
        editor.changed |= timing_recipe_form_rational(context, "Bump X", &form.bump_x);
        editor.changed |= timing_recipe_form_rational(context, "Bump Y", &form.bump_y);
    }
    if (form.output != WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS) return;
    editor.changed |= timing_recipe_form_rational(context, "Transition", &form.transition_duration);
    nk_layout_row_dynamic(context, 22, 1);
    nk_bool seconds = form.transition_is_seconds ? 1 : 0;
    if (nk_checkbox_label(context, "SPEEDS transition uses seconds", &seconds)) {
        form.transition_is_seconds = seconds != 0;
        editor.changed = true;
    }
}

static void render_recipe_sampling_mode(TimingRecipeEditor& editor) {
    auto* context = editor.context;
    auto& form = state.recipe_form;
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 4);
    nk_layout_row_push(context, 0.18f);
    nk_label(context, "Sampling", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.32f);
    if (nk_button_label(context, form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED
                                     ? "Fixed" : "Adaptive")) {
        form.sampling_mode = form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED
            ? WORM_UI_TIMING_RECIPE_SAMPLING_ADAPTIVE
            : WORM_UI_TIMING_RECIPE_SAMPLING_FIXED;
        editor.changed = true;
    }
    nk_layout_row_push(context, 0.18f);
    nk_label(context, form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED
                          ? "Fixed extent" : "Adaptive", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.32f);
    if (form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED) {
        const auto* label = form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT
            ? "Point count" : "Row step";
        if (nk_button_label(context, label)) {
            form.fixed_extent = form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT
                ? WORM_UI_TIMING_RECIPE_FIXED_STEP_ROWS
                : WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT;
            editor.changed = true;
        }
    } else nk_label(context, "error tolerance", NK_TEXT_LEFT);
    nk_layout_row_end(context);
}

static void render_recipe_sampling_parameters(TimingRecipeEditor& editor) {
    auto* context = editor.context;
    auto& form = state.recipe_form;
    const auto fixed = form.sampling_mode == WORM_UI_TIMING_RECIPE_SAMPLING_FIXED;
    if (fixed) {
        nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
        nk_layout_row_push(context, 0.22f);
        const auto points = form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT;
        nk_label(context, points ? "Points" : "Step rows", NK_TEXT_LEFT);
        nk_layout_row_push(context, 0.78f);
        auto* buffer = points ? form.fixed_point_count : form.fixed_step_rows;
        const auto size = points ? sizeof(form.fixed_point_count) : sizeof(form.fixed_step_rows);
        editor.changed |= nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, buffer, size,
                                                          nk_filter_decimal) != 0;
        nk_layout_row_end(context);
    } else {
        editor.changed |= timing_recipe_form_rational(context, "Tolerance", &form.tolerance);
        nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
        nk_layout_row_push(context, 0.22f);
        nk_label(context, "Max points", NK_TEXT_LEFT);
        nk_layout_row_push(context, 0.78f);
        editor.changed |= nk_edit_string_zero_terminated(context, NK_EDIT_FIELD,
            form.adaptive_max_points, sizeof(form.adaptive_max_points), nk_filter_decimal) != 0;
        nk_layout_row_end(context);
    }
}

static void render_recipe_footer(TimingRecipeEditor& editor) {
    auto* context = editor.context;
    auto* action = editor.action;
    const auto& draft = *editor.draft;
    if (draft.status_text && draft.status_text[0]) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, draft.status_text, NK_TEXT_LEFT);
    }
    nk_layout_row_begin(context, NK_DYNAMIC, 28, 2);
    nk_layout_row_push(context, 0.50f);
    if (!draft.valid) nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, "Apply SV recipe");
    if (!draft.valid) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.50f);
    const auto cancel = nk_button_label(context, "Cancel recipe");
    nk_layout_row_end(context);
    if (!action || action->timing_recipe_action.kind != WORM_UI_TIMING_RECIPE_ACTION_NONE) return;
    if (cancel) set_timing_recipe_action(action, &draft, WORM_UI_TIMING_RECIPE_ACTION_CANCEL);
    else if (apply && draft.valid)
        set_timing_recipe_action(action, &draft, WORM_UI_TIMING_RECIPE_ACTION_APPLY);
    else if (editor.changed)
        set_timing_recipe_action(action, &draft, WORM_UI_TIMING_RECIPE_ACTION_UPDATE);
}

static void render_timing_recipe_form(struct nk_context* context, const WormUiTimingView* view,
                                      WormUiWorkspaceAction* action) {
    const auto& draft = view->sv_recipe;
    ensure_timing_recipe_buffers(&draft);
    if (!draft.active) return;
    TimingRecipeEditor editor{context, &draft, action};
    render_recipe_header(editor);
    render_recipe_curve_parameters(editor);
    render_recipe_sampling_mode(editor);
    render_recipe_sampling_parameters(editor);
    render_recipe_footer(editor);
}

static void synchronize_timing_stage_editor(const WormUiTimingView* view) {
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
}

struct TimingStageEditor {
    nk_context* context;
    const WormUiTimingView* view;
    WormUiWorkspaceAction* action;
    nk_bool seconds;
    nk_bool toggle_primary;
    bool changed{};
    int bpm_step{};
};

static auto timing_stage_pause_label(WormUiTimingPauseUnit unit) -> const char* {
    switch (unit) {
    case WORM_UI_TIMING_PAUSE_SECONDS: return "seconds";
    case WORM_UI_TIMING_PAUSE_MILLISECONDS: return "milliseconds";
    case WORM_UI_TIMING_PAUSE_BMS_STOP_TICKS: return "BMS STOP ticks";
    case WORM_UI_TIMING_PAUSE_BEATS: return "beats";
    }
    return "seconds";
}

static auto timing_stage_primary_label(WormUiTimingLaneKind lane) -> const char* {
    switch (lane) {
    case WORM_UI_TIMING_LANE_BPM: return "BPM";
    case WORM_UI_TIMING_LANE_SCROLL: return "Scroll multiplier";
    case WORM_UI_TIMING_LANE_SPEED: return "SPEEDS multiplier";
    case WORM_UI_TIMING_LANE_STOP:
    case WORM_UI_TIMING_LANE_DELAY: return "Duration";
    case WORM_UI_TIMING_LANE_ANCHOR: return "Source sample frame";
    default: return "Value";
    }
}

static void render_stage_title(const TimingStageEditor& editor) {
    const auto& view = *editor.view;
    nk_layout_row_dynamic(editor.context, 22, 1);
    if (view.stage_existing_object_id != 0)
        nk_labelf(editor.context, NK_TEXT_LEFT, "Editing timing event #%llu",
                  static_cast<unsigned long long>(view.stage_existing_object_id));
    else nk_label(editor.context, "Staged timing event — confirm to write.", NK_TEXT_LEFT);
}

static void render_stage_kiai(TimingStageEditor& editor) {
    nk_layout_row_dynamic(editor.context, 24, 1);
    editor.changed |= nk_checkbox_label(editor.context, "Kiai enabled",
                                        &editor.toggle_primary) != 0;
}

static void render_stage_anchor(TimingStageEditor& editor) {
    auto* context = editor.context;
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Exact source sample frame", NK_TEXT_LEFT);
    nk_layout_row_begin(context, NK_DYNAMIC, 28, 2);
    nk_layout_row_push(context, 0.38f);
    nk_label(context, timing_stage_primary_label(editor.view->stage_lane), NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.62f);
    editor.changed |= nk_edit_string_zero_terminated(context, NK_EDIT_FIELD,
        state.primary_numerator.data(), state.primary_numerator.size(), nk_filter_decimal) != 0;
    nk_layout_row_end(context);
    nk_layout_row_dynamic(context, 24, 1);
    editor.changed |= nk_checkbox_label(context, "Lock anchor", &editor.toggle_primary) != 0;
}

static void render_bpm_fraction(TimingStageEditor& editor) {
    auto* context = editor.context;
    if (!state.bpm_advanced_fraction) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Exact value: %s / %s",
                  state.primary_numerator.data(), state.primary_denominator.data());
        return;
    }
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Exact BPM", NK_TEXT_LEFT);
    nk_layout_row_begin(context, NK_DYNAMIC, 28, 3);
    nk_layout_row_push(context, 0.46f);
    const auto numerator_changed = nk_edit_string_zero_terminated(context, NK_EDIT_FIELD,
        state.primary_numerator.data(), state.primary_numerator.size(), nk_filter_float) != 0;
    nk_layout_row_push(context, 0.08f);
    nk_label(context, "/", NK_TEXT_CENTERED);
    nk_layout_row_push(context, 0.46f);
    const auto denominator_changed = nk_edit_string_zero_terminated(context, NK_EDIT_FIELD,
        state.primary_denominator.data(), state.primary_denominator.size(), nk_filter_decimal) != 0;
    nk_layout_row_end(context);
    if (numerator_changed || denominator_changed) {
        state.bpm_decimal_edited = false;
        editor.changed = true;
    }
}

static void render_stage_bpm(TimingStageEditor& editor) {
    auto* context = editor.context;
    const auto& view = *editor.view;
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, view.stage_primary_decimal_exact ? "Decimal BPM (exact)"
                                                       : "Decimal BPM (approximate; fraction retained)",
             NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 28, 1);
    const auto decimal_changed = nk_edit_string_zero_terminated(context, NK_EDIT_FIELD,
        state.bpm_decimal.data(), state.bpm_decimal.size(), nk_filter_float) != 0;
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 2);
    nk_layout_row_push(context, 0.50f);
    if (nk_button_label(context, "-1")) editor.bpm_step = -1;
    nk_layout_row_push(context, 0.50f);
    if (nk_button_label(context, "+1")) editor.bpm_step = 1;
    nk_layout_row_end(context);
    if (decimal_changed) {
        state.bpm_decimal_edited = true;
        editor.changed = true;
    }
    editor.changed = editor.changed || editor.bpm_step != 0;
    nk_bool advanced = state.bpm_advanced_fraction ? 1 : 0;
    nk_layout_row_dynamic(context, 24, 1);
    if (nk_checkbox_label(context, "Advanced exact fraction", &advanced) != 0)
        state.bpm_advanced_fraction = advanced != 0;
    render_bpm_fraction(editor);
}

static void render_stage_rational_primary(TimingStageEditor& editor) {
    auto* context = editor.context;
    nk_layout_row_dynamic(context, 20, 1);
    nk_labelf(context, NK_TEXT_LEFT, "%s (numerator / denominator)",
              timing_stage_primary_label(editor.view->stage_lane));
    nk_layout_row_begin(context, NK_DYNAMIC, 28, 3);
    nk_layout_row_push(context, 0.46f);
    editor.changed |= nk_edit_string_zero_terminated(context, NK_EDIT_FIELD,
        state.primary_numerator.data(), state.primary_numerator.size(), nk_filter_float) != 0;
    nk_layout_row_push(context, 0.08f);
    nk_label(context, "/", NK_TEXT_CENTERED);
    nk_layout_row_push(context, 0.46f);
    editor.changed |= nk_edit_string_zero_terminated(context, NK_EDIT_FIELD,
        state.primary_denominator.data(), state.primary_denominator.size(), nk_filter_decimal) != 0;
    nk_layout_row_end(context);
}

static void render_stage_primary(TimingStageEditor& editor) {
    const auto lane = editor.view->stage_lane;
    if (lane == WORM_UI_TIMING_LANE_KIAI) render_stage_kiai(editor);
    else if (lane == WORM_UI_TIMING_LANE_ANCHOR) render_stage_anchor(editor);
    else if (lane == WORM_UI_TIMING_LANE_BPM) render_stage_bpm(editor);
    else render_stage_rational_primary(editor);
}

static void render_stage_secondary(TimingStageEditor& editor) {
    const auto& view = *editor.view;
    auto* context = editor.context;
    if (view.stage_lane == WORM_UI_TIMING_LANE_SPEED) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, "Transition", NK_TEXT_LEFT);
        nk_layout_row_begin(context, NK_DYNAMIC, 28, 3);
        nk_layout_row_push(context, 0.46f);
        editor.changed |= nk_edit_string_zero_terminated(context, NK_EDIT_FIELD,
            state.secondary_numerator.data(), state.secondary_numerator.size(),
            nk_filter_decimal) != 0;
        nk_layout_row_push(context, 0.08f);
        nk_label(context, "/", NK_TEXT_CENTERED);
        nk_layout_row_push(context, 0.46f);
        editor.changed |= nk_edit_string_zero_terminated(context, NK_EDIT_FIELD,
            state.secondary_denominator.data(), state.secondary_denominator.size(),
            nk_filter_decimal) != 0;
        nk_layout_row_end(context);
        nk_layout_row_dynamic(context, 22, 1);
        editor.changed |= nk_checkbox_label(context, "Seconds", &editor.seconds) != 0;
    } else if (view.stage_lane == WORM_UI_TIMING_LANE_WARP) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, view.stage_existing_object_id != 0 ? "Length in exact chart rows"
                                                              : "Length in current grid steps",
                 NK_TEXT_LEFT);
        nk_layout_row_dynamic(context, 28, 1);
        editor.changed |= nk_edit_string_zero_terminated(context, NK_EDIT_FIELD,
            state.secondary_numerator.data(), state.secondary_numerator.size(),
            nk_filter_decimal) != 0;
    } else if (view.stage_lane == WORM_UI_TIMING_LANE_STOP ||
               view.stage_lane == WORM_UI_TIMING_LANE_DELAY) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Pause duration uses its authored unit: %s.",
                  timing_stage_pause_label(view.stage_pause_unit));
    }
}

static void update_stage_action_fields(TimingStageEditor& editor) {
    auto* action = editor.action;
    if (!action || action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE) return;
    action->timing_transition_is_seconds = editor.seconds != 0;
    action->timing_toggle_primary = editor.toggle_primary != 0;
    copy_timing_form_to_action(action);
    action->timing_bpm_step = editor.bpm_step;
}

static void commit_stage_action(TimingStageEditor& editor, WormUiTimingStageAction kind) {
    set_timing_action(editor.action, editor.view, kind, editor.view->stage_value,
                      editor.view->stage_secondary_value, editor.seconds != 0);
    copy_timing_form_to_action(editor.action);
}

static void render_stage_actions(TimingStageEditor& editor) {
    auto* context = editor.context;
    auto* action = editor.action;
    const auto& view = *editor.view;
    if (view.stage_status_text && view.stage_status_text[0]) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label(context, view.stage_status_text, NK_TEXT_LEFT);
    }
    nk_layout_row_begin(context, NK_DYNAMIC, 28, 2);
    nk_layout_row_push(context, 0.50f);
    const auto confirm = nk_button_label(context,
        view.stage_existing_object_id != 0 ? "Apply timing edit" : "Confirm event");
    nk_layout_row_push(context, 0.50f);
    const auto cancel = nk_button_label(context, "Cancel");
    bool remove = false;
    nk_layout_row_end(context);
    if (view.stage_existing_object_id != 0) {
        nk_layout_row_dynamic(context, 28, 1);
        remove = nk_button_label(context, "Delete event");
    }
    if (action && action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE && editor.changed)
        set_timing_action(action, &view, WORM_UI_TIMING_STAGE_UPDATE, view.stage_value,
                          view.stage_secondary_value, editor.seconds != 0);
    update_stage_action_fields(editor);
    if (remove) commit_stage_action(editor, WORM_UI_TIMING_STAGE_DELETE);
    else if (cancel) commit_stage_action(editor, WORM_UI_TIMING_STAGE_CANCEL);
    else if (confirm) commit_stage_action(editor, WORM_UI_TIMING_STAGE_CONFIRM);
    if (action && action->timing_stage_action != WORM_UI_TIMING_STAGE_NONE)
        action->timing_toggle_primary = editor.toggle_primary != 0;
}

static void render_active_timing_stage(const TimingPanel& panel) {
    TimingStageEditor editor{panel.context, panel.view, panel.action,
        panel.view->stage_transition_is_seconds ? 1 : 0,
        panel.view->stage_toggle_primary ? 1 : 0};
    render_stage_title(editor);
    render_stage_primary(editor);
    render_stage_secondary(editor);
    render_stage_actions(editor);
}

void worm_ui_nuklear_render_timing_stage_editor(struct nk_context* context,
                                                const WormUiTimingView* view,
                                                WormUiWorkspaceAction* action) {
    if (!context || !view || !action) return;
    const TimingPanel panel{context, view, action, false, nullptr, 0};
    synchronize_timing_stage_editor(view);
    if (view->stage_active) render_active_timing_stage(panel);
}

static void render_grid_control(const TimingPanel& panel) {
    const auto& view = *panel.view;
    auto* action = panel.action;
    auto* context = panel.context;
    const auto enabled = !view.stage_active && !view.sv_recipe.active &&
                         !view.sv_recipe.selection_armed && view.scene_available &&
                         view.supported_snap_denominator_count > 0;
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 3);
    nk_layout_row_push(context, 0.42f);
    nk_labelf(context, NK_TEXT_LEFT, "Active grid: 1/%llu%s",
              static_cast<unsigned long long>(view.active_snap_denominator),
              view.active_snap_denominator == 0 ? " (unavailable)" : "");
    nk_layout_row_push(context, 0.29f);
    if (!enabled) nk_widget_disable_begin(context);
    const auto previous = nk_button_label(context, "Previous grid");
    if (!enabled) nk_widget_disable_end(context);
    nk_layout_row_push(context, 0.29f);
    if (!enabled) nk_widget_disable_begin(context);
    const auto next_button = nk_button_label(context, "Next grid");
    if (!enabled) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if (!action || action->timing_grid_action != WORM_UI_TIMING_GRID_ACTION_NONE ||
        (!previous && !next_button)) return;
    size_t current = 0;
    while (current < view.supported_snap_denominator_count &&
           view.supported_snap_denominators[current] != view.active_snap_denominator) ++current;
    if (current >= view.supported_snap_denominator_count) return;
    const auto count = view.supported_snap_denominator_count;
    const auto selected = previous ? (current + count - 1) % count : (current + 1) % count;
    action->timing_grid_action = WORM_UI_TIMING_GRID_ACTION_SELECT;
    action->timing_grid_chart_id = view.chart_id;
    action->timing_grid_document_revision = view.document_revision;
    action->timing_grid_denominator = view.supported_snap_denominators[selected];
}

static void render_initial_bpm_control(const TimingPanel& panel) {
    const auto& view = *panel.view;
    auto* action = panel.action;
    auto* context = panel.context;
    nk_layout_row_begin(context, NK_DYNAMIC, 25, 2);
    nk_layout_row_push(context, 0.57f);
    if (view.initial_bpm_present && std::isfinite(view.initial_bpm_value))
        nk_labelf(context, NK_TEXT_LEFT, "Initial BPM: %.6g", view.initial_bpm_value);
    else nk_label(context, "Initial BPM: unset", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.43f);
    const auto can_edit = view.initial_bpm_editable && !view.stage_active &&
        !view.sv_recipe.active && !view.sv_recipe.selection_armed &&
        action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE;
    if (!can_edit) nk_widget_disable_begin(context);
    const auto clicked = nk_button_label(context, "Set initial BPM");
    if (!can_edit) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if (!clicked || !can_edit) return;
    set_timing_action(action, &view, WORM_UI_TIMING_STAGE_BEGIN);
    action->timing_lane = WORM_UI_TIMING_LANE_BPM;
    action->timing_object_id = view.initial_bpm_event_id;
    action->timing_has_target_y = view.initial_bpm_event_id == 0;
    action->timing_target_y_fraction = 0.0;
}

static auto timing_meter_editable(const WormUiTimingView& view) -> bool {
    return !view.stage_active && !view.sv_recipe.active && !view.sv_recipe.selection_armed &&
           view.scene_available && view.measure_count != 0;
}

static void render_meter_control(const TimingPanel& panel) {
    const auto& view = *panel.view;
    auto* action = panel.action;
    auto* context = panel.context;
    const auto editable = timing_meter_editable(view);
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 5);
    nk_layout_row_push(context, 0.23f);
    nk_label(context, "Meter at measure", NK_TEXT_LEFT);
    const std::array<std::pair<std::array<char, 32>*, float>, 3> fields{{
        {&state.meter_measure, 0.18f}, {&state.meter_numerator, 0.16f},
        {&state.meter_denominator, 0.16f}}};
    for (const auto& [buffer, width] : fields) {
        nk_layout_row_push(context, width);
        if (!editable) nk_widget_disable_begin(context);
        (void)nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, buffer->data(),
                                             buffer->size(), nk_filter_decimal);
        if (!editable) nk_widget_disable_end(context);
    }
    nk_layout_row_push(context, 0.27f);
    if (!editable) nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, "Apply meter");
    if (!editable) nk_widget_disable_end(context);
    nk_layout_row_end(context);
    if (!action || !apply || action->timing_meter_action != WORM_UI_TIMING_METER_ACTION_NONE)
        return;
    action->timing_meter_action = WORM_UI_TIMING_METER_ACTION_APPLY;
    action->timing_meter_chart_id = view.chart_id;
    action->timing_meter_document_revision = view.document_revision;
    copy_timing_text(action->timing_meter_measure_text, sizeof(action->timing_meter_measure_text),
                     state.meter_measure.data());
    copy_timing_text(action->timing_meter_numerator_text,
                     sizeof(action->timing_meter_numerator_text), state.meter_numerator.data());
    copy_timing_text(action->timing_meter_denominator_text,
                     sizeof(action->timing_meter_denominator_text),
                     state.meter_denominator.data());
}

static void render_timing_summary(const TimingPanel& panel) {
    const auto& view = *panel.view;
    auto* context = panel.context;
    nk_layout_row_dynamic(context, 24, 1);
    nk_labelf(context, NK_TEXT_LEFT,
              "%zu measures · %zu BPM · %zu stop/delay · %zu warp · %zu SPEEDS",
              view.measure_count, view.bpm_count, view.pause_count, view.warp_count,
              view.speed_total_count);
    nk_layout_row_dynamic(context, 18, 1);
    nk_labelf(context, NK_TEXT_LEFT,
              "%zu bookmarks/sections · %zu anchors · Hover a SPEEDS marker for its exact span; "
              "Double-click a marker to edit its exact value, or an empty row to insert on the "
              "current active timing grid. Coincident markers split left-to-right by source order.",
              view.bookmark_count + view.section_count, view.anchor_count);
}

static void render_recipe_selection_controls(const TimingPanel& panel) {
    const auto& view = *panel.view;
    auto* action = panel.action;
    auto* context = panel.context;
    nk_layout_row_begin(context, NK_DYNAMIC, 24, 2);
    nk_layout_row_push(context, 0.38f);
    if (view.sv_recipe.selection_armed) {
        if (nk_button_label(context, "Cancel SV recipe selection") && action &&
            action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE)
            set_timing_recipe_action(action, &view.sv_recipe,
                                     WORM_UI_TIMING_RECIPE_ACTION_CANCEL);
    } else if (!view.sv_recipe.active) {
        const auto can_arm = !view.stage_active && view.scene_available && view.chart_id != 0;
        if (!can_arm) nk_widget_disable_begin(context);
        const auto clicked = nk_button_label(context, "New SV recipe");
        if (!can_arm) nk_widget_disable_end(context);
        if (clicked && can_arm && action &&
            action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE)
            set_timing_recipe_action(action, &view.sv_recipe,
                                     WORM_UI_TIMING_RECIPE_ACTION_ARM_SELECTION);
    } else {
        nk_label(context,
            view.stage_active ? "Finish or cancel marker edit before resnapping this range"
                              : "Drag a boundary or the interval body to resnap it", NK_TEXT_LEFT);
    }
    nk_layout_row_push(context, 0.62f);
    const auto* hint = view.sv_recipe.selection_armed
        ? "Drag vertically in Scroll / SV to choose an exact range."
        : view.sv_recipe.active
            ? "Candidate and composed curves are preview only until Apply."
            : "Parametric curves stay editable in the project.";
    nk_label(context, hint, NK_TEXT_LEFT);
    nk_layout_row_end(context);
}

static void render_timing_controls(const TimingPanel& panel) {
    render_grid_control(panel);
    render_initial_bpm_control(panel);
    render_meter_control(panel);
    render_timing_summary(panel);
    render_recipe_selection_controls(panel);
}

static auto begin_timing_canvas(const TimingPanel& panel) -> std::optional<TimingCanvas> {
    auto* context = panel.context;
    const auto& view = *panel.view;
    const auto bounds = nk_window_get_content_region(context);
    const auto reserved = 120.0f + (view.stage_active ? 212.0f : 0.0f) +
                          (view.sv_recipe.active ? 300.0f : 0.0f);
    const auto height = std::max(32.0f, bounds.h - reserved);
    nk_layout_space_begin(context, NK_DYNAMIC, height, 1);
    nk_layout_space_push(context, nk_rect(0.0f, 0.0f, 1.0f, 1.0f));
    TimingCanvas result{.panel = panel};
    const auto widget_state = nk_widget(&result.viewport, context);
    nk_layout_space_end(context);
    const auto& viewport = result.viewport;
    if (widget_state == NK_WIDGET_INVALID || viewport.w <= 1.0f || viewport.h <= 1.0f) {
        cancel_timing_recipe_range_drag();
        return std::nullopt;
    }
    result.commands = nk_window_get_canvas(context);
    if (!result.commands) {
        cancel_timing_recipe_range_drag();
        return std::nullopt;
    }
    result.parent_clip = result.commands->clip;
    nk_push_scissor(result.commands, viewport);
    nk_fill_rect(result.commands, viewport, 0.0f, nk_rgb(22, 25, 33));
    if (!view.scene_available) {
        cancel_timing_recipe_range_drag();
        const auto* status = view.status_text ? view.status_text : "Timing lane is unavailable.";
        nk_draw_text(result.commands, viewport, status, static_cast<int>(strlen(status)),
                     context->style.font, nk_rgb(22, 25, 33), nk_rgb(190, 196, 207));
        nk_push_scissor(result.commands, result.parent_clip);
    }
    return result;
}

static auto timing_screen_y(const TimingCanvas& canvas, double y_pixels) -> float {
    const auto& view = *canvas.panel.view;
    const auto& viewport = canvas.viewport;
    return viewport.y + static_cast<float>(y_pixels) * viewport.h /
                            static_cast<float>(view.viewport_height_pixels);
}

static auto marker_horizontal_bounds(const struct nk_rect& lane, double left_fraction,
                                     double right_fraction) -> std::pair<float, float> {
    const auto inner_left = lane.x + 4.0f;
    const auto inner_width = std::max(0.0f, lane.w - 8.0f);
    return {inner_left + inner_width * static_cast<float>(left_fraction),
            inner_left + inner_width * static_cast<float>(right_fraction)};
}

static auto normalized_y_fraction(const TimingCanvas& canvas, float y) -> double {
    const auto& viewport = canvas.viewport;
    return std::clamp((static_cast<double>(y) - static_cast<double>(viewport.y)) /
                          static_cast<double>(viewport.h), 0.0, 1.0);
}

static auto current_recipe_drag_range() -> std::pair<double, double> {
    const auto current = state.recipe_drag_current_fraction;
    switch (state.recipe_range_drag_mode) {
    case worm_ui_nuklear_timing_recipe_range_drag_mode_t::new_range:
        return {std::min(state.recipe_drag_anchor_fraction, current),
                std::max(state.recipe_drag_anchor_fraction, current)};
    case worm_ui_nuklear_timing_recipe_range_drag_mode_t::begin_handle:
        return {std::min(current, state.recipe_drag_original_end),
                std::max(current, state.recipe_drag_original_end)};
    case worm_ui_nuklear_timing_recipe_range_drag_mode_t::end_handle:
        return {std::min(state.recipe_drag_original_begin, current),
                std::max(state.recipe_drag_original_begin, current)};
    case worm_ui_nuklear_timing_recipe_range_drag_mode_t::whole_span: {
        const auto min_delta = -state.recipe_drag_original_begin;
        const auto max_delta = 1.0 - state.recipe_drag_original_end;
        const auto delta = std::clamp(current - state.recipe_drag_anchor_fraction,
                                      min_delta, max_delta);
        return {state.recipe_drag_original_begin + delta,
                state.recipe_drag_original_end + delta};
    }
    }
    return {0.0, 0.0};
}

static void initialize_timing_lanes(TimingCanvas& canvas) {
    auto* commands = canvas.commands;
    auto* context = canvas.panel.context;
    const auto& viewport = canvas.viewport;
    const std::array<nk_color, 8> colors{
        nk_rgb(242, 190, 102), nk_rgb(177, 206, 129), nk_rgb(113, 184, 218), nk_rgb(224, 132, 119),
        nk_rgb(207, 153, 104), nk_rgb(176, 142, 218), nk_rgb(244, 116, 174), nk_rgb(130, 219, 206)};
    const auto lane_left = viewport.x + viewport.w * 0.025f;
    const auto lane_width = viewport.w * 0.95f / static_cast<float>(timing_lane_kinds.size());
    for (size_t index = 0; index < canvas.lanes.size(); ++index) {
        auto& lane = canvas.lanes[index];
        lane = nk_rect(lane_left + lane_width * static_cast<float>(index) + 2.0f, viewport.y,
                       std::max(1.0f, lane_width - 4.0f), viewport.h);
        nk_fill_rect(commands, lane, 0.0f, nk_rgb(31, 37, 49));
        nk_stroke_line(commands, lane.x, lane.y, lane.x, lane.y + lane.h, 1.0f,
                       nk_rgb(66, 76, 94));
        nk_stroke_line(commands, lane.x + lane.w, lane.y, lane.x + lane.w,
                       lane.y + lane.h, 1.0f, nk_rgb(66, 76, 94));
        const auto title = nk_rect(lane.x + 3.0f, viewport.y + 3.0f,
                                   std::max(0.0f, lane.w - 6.0f), 18.0f);
        nk_draw_text(commands, title, timing_lane_labels[index],
                     static_cast<int>(strlen(timing_lane_labels[index])), context->style.font,
                     nk_rgba(22, 25, 33, 190), colors[index]);
    }
}

static void draw_timing_grid(TimingCanvas& canvas) {
    const auto& view = *canvas.panel.view;
    const auto& viewport = canvas.viewport;
    for (size_t index = 0; index < view.grid_line_count; ++index) {
        const auto& line = view.grid_lines[index];
        const auto y = timing_screen_y(canvas, line.y_pixels);
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
        nk_stroke_line(canvas.commands, viewport.x, y, viewport.x + viewport.w, y,
                       thickness, color);
    }
}

static void consider_timing_marker(const TimingCanvas& canvas, size_t lane_index, float x, float y,
                                   std::uint64_t candidate, double left_fraction,
                                   double right_fraction, double marker_y,
                                   std::uint64_t& best_id, float& nearest) {
    const auto [left, right] = marker_horizontal_bounds(
        canvas.lanes[lane_index], left_fraction, right_fraction);
    if (x < left || x > right) return;
    const auto distance = std::abs(y - timing_screen_y(canvas, marker_y));
    if (distance > 7.0f || distance > nearest) return;
    best_id = candidate;
    nearest = distance;
}

static auto marker_at_pointer(const TimingCanvas& canvas, float x, float y)
    -> std::optional<std::pair<WormUiTimingLaneKind, std::uint64_t>> {
    const auto& view = *canvas.panel.view;
    for (size_t lane_index = 0; lane_index < canvas.lanes.size(); ++lane_index) {
        const auto& lane = canvas.lanes[lane_index];
        if (x < lane.x || x > lane.x + lane.w) continue;
        std::uint64_t object_id = 0;
        auto nearest = std::numeric_limits<float>::max();
        for (size_t marker = 0; marker < view.marker_count; ++marker) {
            const auto& candidate = view.markers[marker];
            if (candidate.lane != timing_lane_kinds[lane_index]) continue;
            consider_timing_marker(canvas, lane_index, x, y, candidate.object_id,
                candidate.marker_left_fraction, candidate.marker_right_fraction,
                candidate.marker_y_pixels, object_id, nearest);
        }
        if (timing_lane_kinds[lane_index] == WORM_UI_TIMING_LANE_SPEED) {
            for (size_t marker = 0; marker < view.speed_count; ++marker) {
                const auto& candidate = view.speeds[marker];
                consider_timing_marker(canvas, lane_index, x, y, candidate.object_id,
                    candidate.marker_left_fraction, candidate.marker_right_fraction,
                    candidate.marker_y_pixels, object_id, nearest);
            }
        }
        if (object_id != 0) return std::pair{timing_lane_kinds[lane_index], object_id};
    }
    return std::nullopt;
}

static auto pointer_started_in_rect(const nk_mouse_button& button, const struct nk_rect& rect) -> bool {
    return button.down && button.clicked != 0 && button.clicked_pos.x >= rect.x &&
           button.clicked_pos.x <= rect.x + rect.w && button.clicked_pos.y >= rect.y &&
           button.clicked_pos.y <= rect.y + rect.h;
}

static void start_marker_drag(TimingCanvas& canvas, const nk_mouse_button& button) {
    const auto& view = *canvas.panel.view;
    if (state.marker_drag_active || view.stage_active || view.sv_recipe.active ||
        view.sv_recipe.selection_armed || !canvas.panel.window_focused ||
        !pointer_started_in_rect(button, canvas.viewport)) return;
    const auto marker = marker_at_pointer(canvas, button.clicked_pos.x, button.clicked_pos.y);
    if (!marker) return;
    state.marker_drag_active = true;
    state.marker_drag_moved = false;
    state.marker_drag_chart = view.chart_id;
    state.marker_drag_revision = view.document_revision;
    state.marker_drag_lane = marker->first;
    state.marker_drag_object = marker->second;
    state.marker_drag_anchor_screen_y = button.clicked_pos.y;
}

static auto recipe_drag_mode(double clicked, double begin, double end, bool armed,
                             float viewport_height)
    -> worm_ui_nuklear_timing_recipe_range_drag_mode_t {
    if (armed) return worm_ui_nuklear_timing_recipe_range_drag_mode_t::new_range;
    const auto tolerance = std::max(0.005, 6.0 / static_cast<double>(viewport_height));
    if (std::abs(clicked - begin) <= tolerance)
        return worm_ui_nuklear_timing_recipe_range_drag_mode_t::begin_handle;
    if (std::abs(clicked - end) <= tolerance)
        return worm_ui_nuklear_timing_recipe_range_drag_mode_t::end_handle;
    return worm_ui_nuklear_timing_recipe_range_drag_mode_t::whole_span;
}

static void start_recipe_range_drag(const TimingCanvas& canvas, const nk_mouse_button& button) {
    const auto& view = *canvas.panel.view;
    auto* action = canvas.panel.action;
    const auto lane_index = view.sv_recipe.active &&
        view.sv_recipe.form.output == WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS ? 2U : 1U;
    const auto& lane = canvas.lanes[lane_index];
    const auto started_in_lane = pointer_started_in_rect(button, canvas.viewport) &&
        button.clicked_pos.x >= lane.x && button.clicked_pos.x <= lane.x + lane.w;
    if (state.recipe_range_drag_active || view.stage_active || !action ||
        action->timing_recipe_action.kind != WORM_UI_TIMING_RECIPE_ACTION_NONE ||
        state.marker_drag_active || !canvas.panel.window_focused || !started_in_lane) return;
    const auto armed = view.sv_recipe.selection_armed && !view.sv_recipe.active;
    const auto clicked = normalized_y_fraction(canvas, button.clicked_pos.y);
    const auto begin = view.sv_recipe.range_begin_fraction;
    const auto end = view.sv_recipe.range_end_fraction;
    const auto hit_selected = view.sv_recipe.active && clicked >= begin - 0.01 && clicked <= end + 0.01;
    if (!armed && !hit_selected) return;
    state.recipe_range_drag_active = true;
    state.recipe_drag_original_begin = begin;
    state.recipe_drag_original_end = end;
    state.recipe_range_drag_mode = recipe_drag_mode(clicked, begin, end, armed,
                                                     canvas.viewport.h);
    state.recipe_drag_chart = view.chart_id;
    state.recipe_drag_revision = view.document_revision;
    state.recipe_drag_anchor_fraction = clicked;
    state.recipe_drag_current_fraction = clicked;
    state.recipe_drag_anchor_screen_y = button.clicked_pos.y;
    state.recipe_drag_moved = false;
}

static void advance_marker_drag(const TimingCanvas& canvas, bool left_down, bool released) {
    const auto& view = *canvas.panel.view;
    auto* action = canvas.panel.action;
    if (!state.marker_drag_active) return;
    const auto matches = state.marker_drag_chart == view.chart_id &&
                         state.marker_drag_revision == view.document_revision;
    if (!canvas.panel.window_focused || !view.scene_available || !matches ||
        (!left_down && !released)) {
        cancel_timing_marker_drag();
        return;
    }
    const auto distance = std::abs(canvas.panel.context->input.mouse.pos.y -
                                   state.marker_drag_anchor_screen_y);
    state.marker_drag_moved = state.marker_drag_moved || distance >= 4.0f;
    if (state.marker_drag_moved && action &&
        action->timing_stage_action == WORM_UI_TIMING_STAGE_NONE &&
        action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
        const auto target = std::min(normalized_y_fraction(
            canvas, canvas.panel.context->input.mouse.pos.y), std::nextafter(1.0, 0.0));
        set_timing_action(action, &view, view.stage_active ? WORM_UI_TIMING_STAGE_MOVE
                                                            : WORM_UI_TIMING_STAGE_BEGIN);
        action->timing_chart_id = state.marker_drag_chart;
        action->timing_document_revision = state.marker_drag_revision;
        action->timing_lane = state.marker_drag_lane;
        action->timing_object_id = state.marker_drag_object;
        action->timing_has_target_y = true;
        action->timing_target_y_fraction = target;
    }
    if (released) cancel_timing_marker_drag();
}

static void advance_recipe_range_drag(const TimingCanvas& canvas, bool left_down, bool released) {
    const auto& view = *canvas.panel.view;
    auto* action = canvas.panel.action;
    if (!state.recipe_range_drag_active) return;
    const auto selection_drag = state.recipe_range_drag_mode ==
        worm_ui_nuklear_timing_recipe_range_drag_mode_t::new_range;
    const auto matches = state.recipe_drag_chart == view.chart_id &&
        state.recipe_drag_revision == view.document_revision &&
        (selection_drag ? view.sv_recipe.selection_armed : view.sv_recipe.active);
    if (!canvas.panel.window_focused || !view.scene_available || view.stage_active || !matches ||
        (!left_down && !released)) {
        cancel_timing_recipe_range_drag();
        return;
    }
    state.recipe_drag_current_fraction = normalized_y_fraction(
        canvas, canvas.panel.context->input.mouse.pos.y);
    const auto distance = std::abs(canvas.panel.context->input.mouse.pos.y -
                                   state.recipe_drag_anchor_screen_y);
    state.recipe_drag_moved = state.recipe_drag_moved || distance >= 4.0f;
    if (!released) return;
    if (state.recipe_drag_moved && action &&
        action->timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
        const auto [begin, end] = current_recipe_drag_range();
        const auto kind = selection_drag ? WORM_UI_TIMING_RECIPE_ACTION_BEGIN
                                         : WORM_UI_TIMING_RECIPE_ACTION_UPDATE_RANGE;
        set_timing_recipe_action(action, &view.sv_recipe, kind, begin, end);
    }
    cancel_timing_recipe_range_drag();
}

static auto hovered_speed_marker(const TimingCanvas& canvas) -> std::size_t {
    const auto& view = *canvas.panel.view;
    const auto& context = *canvas.panel.context;
    const auto& viewport = canvas.viewport;
    const auto& lane = canvas.lanes[2];
    const auto x = context.input.mouse.pos.x;
    if (!nk_input_is_mouse_hovering_rect(&context.input, viewport) ||
        x < lane.x || x > lane.x + lane.w) return view.speed_count;
    auto hovered = view.speed_count;
    auto nearest = std::numeric_limits<float>::max();
    for (size_t index = 0; index < view.speed_count; ++index) {
        const auto& speed = view.speeds[index];
        const auto [left, right] = marker_horizontal_bounds(
            lane, speed.marker_left_fraction, speed.marker_right_fraction);
        if (x < left || x > right) continue;
        const auto distance = std::abs(context.input.mouse.pos.y - timing_screen_y(
            canvas, speed.marker_y_pixels));
        if (distance <= 7.0f && distance <= nearest) {
            hovered = index;
            nearest = distance;
        }
    }
    return hovered;
}

static void process_timing_pointer(TimingCanvas& canvas) {
    auto& input = canvas.panel.context->input;
    const auto& button = input.mouse.buttons[NK_BUTTON_LEFT];
    start_marker_drag(canvas, button);
    start_recipe_range_drag(canvas, button);
    const auto left_down = button.down != 0;
    const auto released = nk_input_is_mouse_released(&input, NK_BUTTON_LEFT);
    advance_marker_drag(canvas, left_down, released);
    advance_recipe_range_drag(canvas, left_down, released);
    canvas.hovered = hovered_speed_marker(canvas);
}

static auto timing_lane_colors() -> std::array<nk_color, 8> {
    return {nk_rgb(242, 190, 102), nk_rgb(177, 206, 129), nk_rgb(113, 184, 218),
            nk_rgb(224, 132, 119), nk_rgb(207, 153, 104), nk_rgb(176, 142, 218),
            nk_rgb(244, 116, 174), nk_rgb(130, 219, 206)};
}

static auto recipe_lane(const TimingCanvas& canvas) -> const struct nk_rect& {
    const auto& recipe = canvas.panel.view->sv_recipe;
    const auto index = recipe.active && recipe.form.output == WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS
        ? std::size_t{2} : std::size_t{1};
    return canvas.lanes[index];
}

static void draw_hovered_speed_span(const TimingCanvas& canvas) {
    const auto& view = *canvas.panel.view;
    if (canvas.hovered == view.speed_count) return;
    const auto& speed = view.speeds[canvas.hovered];
    const auto& lane = canvas.lanes[2];
    const auto& viewport = canvas.viewport;
    auto top = timing_screen_y(canvas, speed.affected_begin_y_pixels);
    auto bottom = timing_screen_y(canvas, speed.affected_end_y_pixels);
    if (bottom - top < 3.0f) bottom = std::min(viewport.y + viewport.h, top + 3.0f);
    nk_fill_rect(canvas.commands, nk_rect(lane.x, top, lane.w, std::max(0.0f, bottom - top)),
                 0.0f, nk_rgba(136, 203, 238, 54));
    nk_stroke_line(canvas.commands, lane.x, top, lane.x + lane.w, top, 1.0f,
                   nk_rgb(137, 207, 239));
    nk_stroke_line(canvas.commands, lane.x, bottom, lane.x + lane.w, bottom, 1.0f,
                   nk_rgb(91, 145, 177));
}

static void draw_timing_markers(const TimingCanvas& canvas) {
    const auto& view = *canvas.panel.view;
    const auto colors = timing_lane_colors();
    for (size_t marker_index = 0; marker_index < view.marker_count; ++marker_index) {
        const auto& marker = view.markers[marker_index];
        for (size_t lane_index = 0; lane_index < canvas.lanes.size(); ++lane_index) {
            if (marker.lane != timing_lane_kinds[lane_index]) continue;
            const auto y = timing_screen_y(canvas, marker.marker_y_pixels);
            const auto [left, right] = marker_horizontal_bounds(
                canvas.lanes[lane_index], marker.marker_left_fraction,
                marker.marker_right_fraction);
            nk_fill_rect(canvas.commands,
                nk_rect(left, y - 2.0f, std::max(0.0f, right - left), 4.0f),
                0.0f, colors[lane_index]);
            break;
        }
    }
}

static void draw_recipe_selection(const TimingCanvas& canvas) {
    const auto& view = *canvas.panel.view;
    const auto& viewport = canvas.viewport;
    const auto& lane = recipe_lane(canvas);
    auto begin = view.sv_recipe.range_begin_fraction;
    auto end = view.sv_recipe.range_end_fraction;
    if (state.recipe_range_drag_active) std::tie(begin, end) = current_recipe_drag_range();
    if (!(view.sv_recipe.active || state.recipe_range_drag_active) || end <= begin) return;
    const auto top = viewport.y + static_cast<float>(begin) * viewport.h;
    const auto bottom = viewport.y + static_cast<float>(end) * viewport.h;
    const auto rect = nk_rect(lane.x + 1.0f, top, lane.w - 2.0f, std::max(1.0f, bottom - top));
    nk_fill_rect(canvas.commands, rect, 0.0f, nk_rgba(180, 160, 230, 32));
    nk_stroke_line(canvas.commands, rect.x, rect.y, rect.x + rect.w, rect.y, 1.0f,
                   nk_rgb(196, 173, 242));
    nk_stroke_line(canvas.commands, rect.x, rect.y + rect.h, rect.x + rect.w,
                   rect.y + rect.h, 1.0f, nk_rgb(196, 173, 242));
}

static auto recipe_point_x(const TimingCanvas& canvas,
                           const WormUiTimingRecipePoint& point) -> float {
    const auto& lane = recipe_lane(canvas);
    const auto [left, right] = marker_horizontal_bounds(lane, point.value_fraction,
                                                        point.value_fraction);
    return (left + right) * 0.5f;
}

static auto recipe_point_y(const TimingCanvas& canvas,
                           const WormUiTimingRecipePoint& point) -> float {
    return canvas.viewport.y + static_cast<float>(point.row_fraction) * canvas.viewport.h;
}

static void draw_recipe_curve(const TimingCanvas& canvas,
                              const WormUiTimingRecipePoint* points, size_t count,
                              nk_color color, float thickness) {
    if (!points || count == 0) return;
    for (size_t index = 0; index < count; ++index) {
        const auto x = recipe_point_x(canvas, points[index]);
        const auto y = recipe_point_y(canvas, points[index]);
        if (index != 0)
            nk_stroke_line(canvas.commands, recipe_point_x(canvas, points[index - 1]),
                           recipe_point_y(canvas, points[index - 1]), x, y, thickness, color);
        else nk_fill_rect(canvas.commands, nk_rect(x - 2.0f, y - 2.0f, 4.0f, 4.0f), 0.0f, color);
        if (index + 1 == count)
            nk_fill_rect(canvas.commands, nk_rect(x - 2.0f, y - 2.0f, 4.0f, 4.0f), 0.0f, color);
    }
}

static void draw_recipe_curves(const TimingCanvas& canvas) {
    const auto& recipe = canvas.panel.view->sv_recipe;
    if (!recipe.active) return;
    draw_recipe_curve(canvas, recipe.candidate_points, recipe.candidate_point_count,
                      nk_rgb(246, 183, 106), 2.0f);
    draw_recipe_curve(canvas, recipe.composed_points, recipe.composed_point_count,
                      nk_rgb(139, 229, 181), 1.5f);
}

static void draw_speed_markers(const TimingCanvas& canvas) {
    const auto& view = *canvas.panel.view;
    const auto& lane = canvas.lanes[2];
    for (size_t index = 0; index < view.speed_count; ++index) {
        const auto& speed = view.speeds[index];
        const auto y = timing_screen_y(canvas, speed.marker_y_pixels);
        const auto color = index == canvas.hovered ? nk_rgb(191, 232, 248)
                                                    : nk_rgb(113, 184, 218);
        const auto [left, right] = marker_horizontal_bounds(
            lane, speed.marker_left_fraction, speed.marker_right_fraction);
        nk_fill_rect(canvas.commands, nk_rect(left, y - 2.0f,
            std::max(0.0f, right - left), 4.0f), 0.0f, color);
    }
}

static void draw_stage_marker(const TimingCanvas& canvas) {
    const auto& view = *canvas.panel.view;
    if (!view.stage_active) return;
    const auto y = timing_screen_y(canvas, view.stage_marker_y_pixels);
    for (size_t index = 0; index < canvas.lanes.size(); ++index) {
        if (timing_lane_kinds[index] != view.stage_lane) continue;
        nk_stroke_line(canvas.commands, canvas.lanes[index].x + 2.0f, y,
                       canvas.lanes[index].x + canvas.lanes[index].w - 2.0f, y, 3.0f,
                       view.stage_valid ? nk_rgb(249, 238, 154) : nk_rgb(237, 116, 104));
        return;
    }
}

static void draw_timing_tooltips(const TimingCanvas& canvas) {
    const auto& view = *canvas.panel.view;
    auto* context = canvas.panel.context;
    if (canvas.hovered != view.speed_count) {
        const auto& speed = view.speeds[canvas.hovered];
        char label[192]{};
        const auto count = snprintf(label, sizeof(label), "SPEEDS #%llu  %.4gx over %.4g %s%s",
            static_cast<unsigned long long>(speed.object_id), speed.multiplier,
            speed.transition_duration, speed.transition_is_seconds ? "seconds" : "beats",
            speed.transition_clipped ? " (clipped by next event or chart end)" : "");
        if (count > 0) {
            const auto bounds = nk_rect(canvas.viewport.x + 8.0f, canvas.viewport.y + 24.0f,
                std::max(0.0f, canvas.viewport.w - 16.0f), 20.0f);
            nk_draw_text(canvas.commands, bounds, label,
                static_cast<int>(std::min<size_t>(strlen(label), sizeof(label) - 1)),
                context->style.font, nk_rgba(22, 25, 33, 220), nk_rgb(207, 232, 244));
        }
    }
    if (!view.truncated) return;
    const auto* warning = "Additional timing markers are hidden by the frontend cap.";
    const auto bounds = nk_rect(canvas.viewport.x + 8.0f,
        canvas.viewport.y + canvas.viewport.h - 24.0f,
        std::max(0.0f, canvas.viewport.w - 16.0f), 18.0f);
    nk_draw_text(canvas.commands, bounds, warning, static_cast<int>(strlen(warning)),
        context->style.font, nk_rgba(22, 25, 33, 220), nk_rgb(224, 184, 103));
}

static void draw_timing_overlays(const TimingCanvas& canvas) {
    draw_hovered_speed_span(canvas);
    draw_timing_markers(canvas);
    draw_recipe_selection(canvas);
    draw_recipe_curves(canvas);
    draw_speed_markers(canvas);
    draw_stage_marker(canvas);
    draw_timing_tooltips(canvas);
}

static auto timing_marker_at_point(const TimingCanvas& canvas, size_t lane_index,
                                  float x, float y) -> std::uint64_t {
    const auto& view = *canvas.panel.view;
    std::uint64_t result = 0;
    auto nearest = std::numeric_limits<float>::max();
    for (size_t marker = 0; marker < view.marker_count; ++marker) {
        const auto& candidate = view.markers[marker];
        if (candidate.lane != timing_lane_kinds[lane_index]) continue;
        consider_timing_marker(canvas, lane_index, x, y, candidate.object_id,
            candidate.marker_left_fraction, candidate.marker_right_fraction,
            candidate.marker_y_pixels, result, nearest);
    }
    if (timing_lane_kinds[lane_index] == WORM_UI_TIMING_LANE_SPEED) {
        for (size_t marker = 0; marker < view.speed_count; ++marker) {
            const auto& candidate = view.speeds[marker];
            consider_timing_marker(canvas, lane_index, x, y, candidate.object_id,
                candidate.marker_left_fraction, candidate.marker_right_fraction,
                candidate.marker_y_pixels, result, nearest);
        }
    }
    return result;
}

static void handle_timing_double_clicks(const TimingCanvas& canvas) {
    const auto& panel = canvas.panel;
    const auto& view = *panel.view;
    auto* action = panel.action;
    if (view.stage_active || !action ||
        action->timing_stage_action != WORM_UI_TIMING_STAGE_NONE ||
        action->timing_recipe_action.kind != WORM_UI_TIMING_RECIPE_ACTION_NONE) return;
    for (size_t click = 0; click < panel.double_click_count; ++click) {
        const auto& pointer = panel.double_clicks[click];
        if (pointer.x < canvas.viewport.x || pointer.x >= canvas.viewport.x + canvas.viewport.w ||
            pointer.y < canvas.viewport.y || pointer.y >= canvas.viewport.y + canvas.viewport.h)
            continue;
        for (size_t lane_index = 0; lane_index < canvas.lanes.size(); ++lane_index) {
            const auto& lane = canvas.lanes[lane_index];
            if (pointer.x < lane.x || pointer.x >= lane.x + lane.w) continue;
            const auto object = timing_marker_at_point(canvas, lane_index, pointer.x, pointer.y);
            set_timing_action(action, &view, WORM_UI_TIMING_STAGE_BEGIN);
            action->timing_lane = timing_lane_kinds[lane_index];
            action->timing_object_id = object;
            if (object == 0) {
                action->timing_has_target_y = true;
                action->timing_target_y_fraction =
                    static_cast<double>(pointer.y - canvas.viewport.y) /
                    static_cast<double>(canvas.viewport.h);
            }
            break;
        }
        if (action->timing_stage_action != WORM_UI_TIMING_STAGE_NONE) break;
    }
}

void worm_ui_nuklear_render_timing_panel(struct nk_context* context, const WormUiTimingView* view,
                                         WormUiWorkspaceAction* action, bool window_focused,
                                         const worm_ui_nuklear_pointer_click_t* double_clicks,
                                         std::size_t double_click_count) {
    if (!context || !view || !action || !view->visible ||
        (double_click_count != 0 && !double_clicks)) {
        return;
    }
    const TimingPanel panel{context, view, action, window_focused, double_clicks,
                            double_click_count};
    synchronize_timing_stage_editor(view);
    render_timing_controls(panel);

    auto canvas = begin_timing_canvas(panel);
    if (!canvas || !view->scene_available) return;
    initialize_timing_lanes(*canvas);
    draw_timing_grid(*canvas);
    if (!view->input_occluded) process_timing_pointer(*canvas);
    draw_timing_overlays(*canvas);
    nk_push_scissor(canvas->commands, canvas->parent_clip);
    if (!view->input_occluded) handle_timing_double_clicks(*canvas);
    worm_ui_nuklear_render_timing_stage_editor(context, view, action);
    render_timing_recipe_form(context, view, action);
}

#include "ui/nuklear/timing_stage_editor.h"
#include "ui/nuklear/chrome.h"
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
        !valid_timing_stage(*view) ||
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
    editor.changed |= wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
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
    const auto numerator_changed = wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
        state.primary_numerator.data(), state.primary_numerator.size(), nk_filter_float) != 0;
    nk_layout_row_push(context, 0.08f);
    nk_label(context, "/", NK_TEXT_CENTERED);
    nk_layout_row_push(context, 0.46f);
    const auto denominator_changed = wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
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
    const auto decimal_changed = wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
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
    editor.changed |= wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
        state.primary_numerator.data(), state.primary_numerator.size(), nk_filter_float) != 0;
    nk_layout_row_push(context, 0.08f);
    nk_label(context, "/", NK_TEXT_CENTERED);
    nk_layout_row_push(context, 0.46f);
    editor.changed |= wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
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
        editor.changed |= wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
            state.secondary_numerator.data(), state.secondary_numerator.size(),
            nk_filter_decimal) != 0;
        nk_layout_row_push(context, 0.08f);
        nk_label(context, "/", NK_TEXT_CENTERED);
        nk_layout_row_push(context, 0.46f);
        editor.changed |= wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
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
        editor.changed |= wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
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

static void render_active_timing_stage(nk_context* context, const WormUiTimingView* view,
                                       WormUiWorkspaceAction* action) {
    TimingStageEditor editor{context, view, action,
        view->stage_transition_is_seconds ? 1 : 0,
        view->stage_toggle_primary ? 1 : 0};
    render_stage_title(editor);
    render_stage_primary(editor);
    render_stage_secondary(editor);
    render_stage_actions(editor);
}

void worm_ui_nuklear_render_timing_stage_editor(struct nk_context* context,
                                                const WormUiTimingView* view,
                                                WormUiWorkspaceAction* action) {
    if (!context || !view || !action) return;
    synchronize_timing_stage_editor(view);
    if (view->stage_active) render_active_timing_stage(context, view, action);
}

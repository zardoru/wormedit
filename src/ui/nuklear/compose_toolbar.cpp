#include <wormedit/core/catalogs.h>
#include "ui/nuklear/compose_menu_command.h"

#include <wormedit/core/rational.h>
#include "ui/nuklear/compose_toolbar.h"
#include "ui/nuklear/chrome/tooltip.h"
#include <cstdio>
#include "ui/nuklear/render/vector_icons.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/project/bms_branch_panel.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>

using wormedit::ui::lucide::Glyph;

static auto compose_inspector_icon(bool visible) -> Glyph {
    return visible ? Glyph::panel_right_close : Glyph::panel_right_open;
}

static void append_bgm_horizontal_scroll(wormedit::ui::workspace_action_t* action,
                                         const wormedit::ui::compose_view_t* view, double delta_pixels) {
    if (!action || !view || !std::isfinite(delta_pixels) || delta_pixels == 0.0 ||
        action->compose_viewport_inputs.size() >= wormedit::ui::compose_viewport_input_admission_limit ||
        view->bgm_maximum_horizontal_scroll_pixels <= 0.0) return;
    action->compose_viewport_inputs.emplace_back() = {
        .kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_SMOOTH_WHEEL,
        .zoom_modifier = false,
        .delta_x = delta_pixels,
        .delta_y = 0.0,
        .cursor_x_pixels = (view->playable_field_right_pixels + view->viewport_width_pixels) * 0.5,
        .cursor_y_pixels = view->viewport_height_pixels * 0.5,
    };
}

static void update_compose_note_label_cache(const wormedit::ui::compose_view_t* view) {
    if (state.note_label.label_project_id == view->project_id.value && state.note_label.label_chart_id == view->chart_id.value &&
        state.note_label.label_document_revision == view->document_revision &&
        state.note_label.label_note_id == view->selected_note_label_note_id.value)
        return;
    state.note_label.note_label.fill('\0');
    if (view->selected_note_label.c_str() && view->selected_note_label_note_id.value != 0) {
        const auto length = std::min(strlen(view->selected_note_label.c_str()), state.note_label.note_label.size() - 1);
        memcpy(state.note_label.note_label.data(), view->selected_note_label.c_str(), length);
        state.note_label.note_label[length] = '\0';
    }
    state.note_label.label_project_id = view->project_id.value;
    state.note_label.label_chart_id = view->chart_id.value;
    state.note_label.label_document_revision = view->document_revision;
    state.note_label.label_note_id = view->selected_note_label_note_id.value;
}

// Shows or hides the song bar under the seek bar; unavailable while the song has no audio.
static void render_compose_bgm_bar_toggle(const ComposeToolbarContext& toolbar) {
    auto* context = toolbar.context;
    const auto* view = toolbar.view;
    auto* action = toolbar.action;
    const bool clicked = wormedit::ui::chrome::IconButton{
        .glyph = Glyph::audio_lines,
        .tooltip = !view->bgm_bar_available ? "Song audio is unavailable"
            : view->bgm_bar_reserved ? "Hide song bar" : "Show song bar",
        .selected = view->bgm_bar_reserved,
        .enabled = view->bgm_bar_available}.render(context);
    if (!clicked ||
        action->compose_inputs.size() >= wormedit::ui::compose_input_admission_limit)
        return;
    auto& input = action->compose_inputs.emplace_back();
    input = {};
    input.kind = WORM_UI_COMPOSE_INPUT_BGM_BAR_TOGGLE;
    input.overview_primary = view->bgm_bar_reserved;
}

static void render_loop_selection_button(const ComposeToolbarContext& toolbar) {
    const auto& view = *toolbar.view;
    auto* action = toolbar.action;
    const bool can_toggle = view.transport_loop_selection_available &&
        (view.transport_loop_selection_enabled ||
         (view.area_range_available && !view.area_range_virtual &&
          view.area_range_begin_row < view.area_range_end_row));
    const auto* tooltip = view.transport_loop_selection_enabled
        ? "Disable selection loop" : "Loop the committed selection";
    const auto clicked = wormedit::ui::chrome::IconButton{
            .glyph = Glyph::rotate_ccw, .tooltip = tooltip,
            .selected = view.transport_loop_selection_enabled,
            .enabled = can_toggle}.render(toolbar.context) &&
        action->kind == WORM_UI_WORKSPACE_ACTION_NONE;
    if (clicked) {
        emit_compose_menu_command(*action, view, "transport.toggle_loop_selection");
    }
}

static void render_scroll_direction_toggle(const ComposeToolbarContext& toolbar) {
    const auto& view = *toolbar.view;
    const auto glyph = view.downscroll ? Glyph::chevron_down : Glyph::chevron_up;
    const bool clicked = wormedit::ui::chrome::IconButton{
        .glyph = glyph,
        .tooltip = view.downscroll ? "Downscroll · switch to upscroll" : "Upscroll · switch to downscroll",
        .selected = view.downscroll, .enabled = view.scene_available}.render(toolbar.context);
    if (clicked && toolbar.action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        toolbar.action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_SCROLL_DIRECTION;
}

static void render_navigation_group(const ComposeToolbarContext& toolbar) {
    auto* context = toolbar.context;
    auto* action = toolbar.action;
    if (wormedit::ui::chrome::IconButton{Glyph::zoom_out, "Zoom out"}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_ZOOM_OUT;
    if (wormedit::ui::chrome::IconButton{Glyph::zoom_in, "Zoom in"}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_ZOOM_IN;
    update_compose_pointer_navigation(action, std::nullopt);
    if (wormedit::ui::chrome::IconButton{Glyph::rotate_ccw, "Reset view"}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_RESET_VIEW;
    render_scroll_direction_toggle(toolbar);
}

static void render_layer_tool(const ComposeToolbarContext& toolbar, bool paint_disabled) {
    auto* context = toolbar.context;
    const auto* view = toolbar.view;
    auto* action = toolbar.action;
    const bool disabled = view->semantic_layer_target.layer_id.value == 0 ||
        !view->semantic_layer_target.visible || view->semantic_layer_target.locked;
    if (disabled && !paint_disabled) nk_widget_disable_begin(context);
    if (wormedit::ui::chrome::IconButton{Glyph::layers,
            disabled ? "Pick a layer in the Layers dock" : "Layer (Alt+3): add; Alt+drag removes",
            view->semantic_layer_paint_mode_active}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_LAYER_TOOL;
    if (disabled && !paint_disabled) nk_widget_disable_end(context);
}

// The measure lane toggle, and while the lane shows, its time-propagation toggle.
static void render_measure_lane_toggles(const ComposeToolbarContext& toolbar) {
    const auto* view = toolbar.view;
    auto* action = toolbar.action;
    const auto request = [action](WormUiComposeActionKind kind) {
        if (action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) action->compose_action = kind;
    };
    if (wormedit::ui::chrome::DrawnIconButton{wormedit::ui::vector_icons::measure_lane,
            view->measure_lane.visible ? "Hide measure lane"
                                       : "Show measure lane: select, place and move bar lines",
            view->measure_lane.visible}.render(toolbar.context))
        request(WORM_UI_COMPOSE_ACTION_TOGGLE_MEASURE_LANE);
    if (!view->measure_lane.visible) return;
    const bool propagates = view->measure_lane.bar_line_time == wormedit::bar_line_time_t::propagate;
    if (wormedit::ui::chrome::DrawnIconButton{wormedit::ui::vector_icons::bar_line_time,
            propagates
                ? "Moved bar lines carry the content after them (click to keep it in place)"
                : "Moved bar lines keep other objects in place (click to carry later content)",
            propagates}.render(toolbar.context))
        request(WORM_UI_COMPOSE_ACTION_TOGGLE_BAR_LINE_TIME);
}

static void render_tool_group(const ComposeToolbarContext& toolbar) {
    auto* context = toolbar.context;
    const auto* view = toolbar.view;
    auto* action = toolbar.action;
    const bool paint_disabled = !view->scene_available || view->timing_focus;
    if (paint_disabled) nk_widget_disable_begin(context);
    if (wormedit::ui::chrome::DrawnIconButton{wormedit::ui::vector_icons::select_cursor,
            "Select (Alt+1)", !view->note_paint_mode_active && !view->semantic_layer_paint_mode_active}.render(context) &&
        (view->note_paint_mode_active || view->semantic_layer_paint_mode_active) && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_SELECT_TOOL;
    if (wormedit::ui::chrome::IconButton{Glyph::paintbrush, paint_disabled ? "Draw unavailable: choose an editable chart outside Timing focus" : "Draw (Alt+2): place current source; right-drag erases",
            view->note_paint_mode_active}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_DRAW_TOOL;
    render_layer_tool(toolbar, paint_disabled);
    if (paint_disabled) nk_widget_disable_end(context);
    if (!view->scene_available) nk_widget_disable_begin(context);
    if (wormedit::ui::chrome::DrawnIconButton{wormedit::ui::vector_icons::grid,
            view->edit_snap_enabled ? "Snap on: edits land on the grid (click to turn off)"
                                   : "Snap off: edits use the finest row (click to turn on)",
            view->edit_snap_enabled}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_EDIT_SNAP;
    if (wormedit::ui::chrome::DrawnIconButton{wormedit::ui::vector_icons::y_lock,
            view->y_lock ? "Y-lock on: dragged notes keep their rows (click to turn off)"
                         : "Y-lock off: lock dragged notes to their rows (click to turn on)",
            view->y_lock}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_Y_LOCK;
    if (wormedit::ui::chrome::DrawnIconButton{wormedit::ui::vector_icons::timer,
            "Timing focus: edit timing only", view->timing_focus}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_FOCUS;
    if (wormedit::ui::chrome::IconButton{Glyph::eye,
            view->bga_lane_visible ? "Hide BGA event lane" : "Show BGA event lane",
            view->bga_lane_visible}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_BGA_LANE_VISIBILITY;
    render_measure_lane_toggles(toolbar);
    if (!view->scene_available) nk_widget_disable_end(context);
    render_loop_selection_button(toolbar);
}

static void render_color_view_group(const ComposeToolbarContext& toolbar) {
    using View = worm_ui_nuklear_compose_color_view_t;
    static constexpr View views[]{View::normal, View::snap, View::layer};
    static constexpr const char* tips[]{"Normal view: layout colors", "Snap view: beat snap colors",
                                         "Layer view: semantic layer colors"};
    static constexpr auto icons = std::array{wormedit::ui::vector_icons::view_keys,
        wormedit::ui::vector_icons::view_snap, wormedit::ui::vector_icons::view_layers};
    for (std::size_t index = 0; index < std::size(views); ++index)
        if (wormedit::ui::chrome::DrawnIconButton{icons[index], tips[index],
                state.input_queue.color_view == views[index]}.render(toolbar.context))
            state.input_queue.color_view = views[index];
}

static void render_panel_group(const ComposeToolbarContext& toolbar) {
    auto* context = toolbar.context;
    if (wormedit::ui::chrome::IconButton{compose_inspector_icon(state.dock_layout.inspector_visible),
            state.dock_layout.inspector_visible ? "Hide inspector" : "Show inspector",
            state.dock_layout.inspector_visible}.render(context))
        state.dock_layout.inspector_visible = !state.dock_layout.inspector_visible;
    if (wormedit::ui::chrome::DrawnIconButton{wormedit::ui::vector_icons::sound_list,
            state.dock_layout.sounds_panel_visible ? "Hide sounds list" : "Show sounds list",
            state.dock_layout.sounds_panel_visible}.render(context))
        state.dock_layout.sounds_panel_visible = !state.dock_layout.sounds_panel_visible;
    if (worm_ui_nuklear_bms_branch_panel_visible(toolbar.view) &&
        wormedit::ui::chrome::IconButton{Glyph::chevrons_right,
            state.dock_layout.branch_panel_visible ? "Hide BMS branches" : "Show BMS branches",
            state.dock_layout.branch_panel_visible}.render(context))
        state.dock_layout.branch_panel_visible = !state.dock_layout.branch_panel_visible;
    if (wormedit::ui::chrome::DrawnIconButton{wormedit::ui::vector_icons::measure_list,
            state.dock_layout.measures_panel_visible ? "Hide measures list" : "Show measures list",
            state.dock_layout.measures_panel_visible}.render(context))
        state.dock_layout.measures_panel_visible = !state.dock_layout.measures_panel_visible;
    if (wormedit::ui::chrome::IconButton{Glyph::layers,
            state.dock_layout.layers_panel_visible ? "Hide layers" : "Show layers",
            state.dock_layout.layers_panel_visible}.render(context))
        state.dock_layout.layers_panel_visible = !state.dock_layout.layers_panel_visible;
    if (wormedit::ui::chrome::IconButton{Glyph::eye,
            state.dock_layout.bga_panel_visible ? "Hide BGA list" : "Show BGA list", state.dock_layout.bga_panel_visible}.render(context))
        state.dock_layout.bga_panel_visible = !state.dock_layout.bga_panel_visible;
    render_compose_bgm_bar_toggle(toolbar);
}

static void render_compose_bgm_column_count_controls(const ComposeToolbarContext& toolbar);

static void render_compose_bgm_scroll_controls(const ComposeToolbarContext& toolbar) {
    auto* context = toolbar.context;
    const auto* view = toolbar.view;
    auto* action = toolbar.action;
    const bool scrollable = view->bgm_maximum_horizontal_scroll_pixels > 0.0;
    if (!scrollable) nk_widget_disable_begin(context);
    if (wormedit::ui::chrome::IconButton{Glyph::chevron_left,
                                         "Scroll BGM columns left"}.render(context) && scrollable)
        append_bgm_horizontal_scroll(action, view, -80.0);
    constexpr nk_size scroll_steps = 1000;
    nk_size scroll_position = scrollable
        ? static_cast<nk_size>(view->bgm_horizontal_scroll_pixels /
                               view->bgm_maximum_horizontal_scroll_pixels * scroll_steps)
        : 0;
    if (nk_progress(context, &scroll_position, scroll_steps, NK_MODIFIABLE) && scrollable) {
        const auto target = view->bgm_maximum_horizontal_scroll_pixels *
                            static_cast<double>(scroll_position) / scroll_steps;
        append_bgm_horizontal_scroll(action, view,
                                     target - view->bgm_horizontal_scroll_pixels);
    }
    if (wormedit::ui::chrome::IconButton{Glyph::chevron_right,
                                         "Scroll BGM columns right"}.render(context) && scrollable)
        append_bgm_horizontal_scroll(action, view, 80.0);
    if (!scrollable) nk_widget_disable_end(context);
    render_compose_bgm_column_count_controls(toolbar);
}

static void reset_bgm_column_count_draft(
                                         worm_ui_nuklear_compose_state_t::bgm_column_count_edit_t& draft,
                                         const wormedit::ui::compose_view_t& view,
                                         bool clear_ownership = true) {
    const auto was_owned = draft.input_owned;
    const auto release_pending = draft.release_pending;
    draft.chart_id = view.chart_id.value;
    draft.revision = view.document_revision;
    draft.text.fill('\0');
    const auto converted = std::to_chars(draft.text.data(),
                                         draft.text.data() + draft.text.size() - 1,
                                         view.bgm_column_count);
    if (converted.ec == std::errc{}) *converted.ptr = '\0';
    draft.initialized = true;
    draft.commit_pending = false;
    if (clear_ownership) {
        draft.input_owned = false;
        draft.release_pending = false;
    } else {
        draft.input_owned = was_owned;
        draft.release_pending = release_pending;
    }
}

static auto bgm_column_count_draft(const wormedit::ui::compose_view_t& view)
    -> worm_ui_nuklear_compose_state_t::bgm_column_count_edit_t& {
    auto& draft = state.inline_offset.bgm_column_count_edit;
    if (!draft.initialized || !view.bgm_column_count_editable ||
        draft.chart_id != view.chart_id.value || draft.revision != view.document_revision)
        reset_bgm_column_count_draft(draft, view);
    else if (draft.commit_pending)
        reset_bgm_column_count_draft(draft, view, false);
    return draft;
}

static auto parse_bgm_column_count(const char* text) -> std::optional<std::uint32_t> {
    if (!text || !text[0]) return std::nullopt;
    const auto* end = text + std::strlen(text);
    std::uint32_t count{};
    const auto parsed = std::from_chars(text, end, count);
    if (parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    return count;
}

static auto request_bgm_column_count(const ComposeToolbarContext& toolbar, uint32_t count)
    -> bool {
    auto* action = toolbar.action;
    if (action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return false;
    action->compose_action = WORM_UI_COMPOSE_ACTION_SET_BGM_COLUMN_COUNT;
    action->compose_bgm_column_count = count;
    action->compose_input_chart_id.value = toolbar.view->chart_id.value;
    action->compose_input_document_revision = toolbar.view->document_revision;
    bgm_column_count_draft(*toolbar.view).commit_pending = true;
    return true;
}

// "BGM columns: N" with minus, an editable count, and plus.
static void render_compose_bgm_column_count_controls(const ComposeToolbarContext& toolbar) {
    auto* context = toolbar.context;
    const auto* view = toolbar.view;
    const bool editable = view->bgm_column_count_editable;
    auto& draft = bgm_column_count_draft(*view);
    if (!editable) nk_widget_disable_begin(context);
    if (nk_window_get_bounds(context).w >= 900.0f)
        nk_label(context, "BGM columns:", NK_TEXT_RIGHT);
    if (nk_button_label(context, "-") && editable && view->bgm_column_count > 1)
        (void)request_bgm_column_count(toolbar, view->bgm_column_count - 1);
    const auto field_bounds = nk_widget_bounds(context);
    const bool field_clicked = nk_input_is_mouse_click_in_rect(
        &context->input, NK_BUTTON_LEFT, field_bounds);
    const auto flags = wormedit::ui::chrome::worm_ui_nuklear_text_field(
        context, static_cast<nk_flags>(static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER), draft.text.data(),
        static_cast<int>(draft.text.size()), nk_filter_decimal);
    const bool edit_active = (flags & NK_EDIT_ACTIVE) != 0;
    const bool deactivated = (flags & NK_EDIT_DEACTIVATED) != 0;
    const bool committed = (flags & NK_EDIT_COMMITED) != 0;
    (void)worm_ui_nuklear_update_bgm_column_count_input_owner(
        state, view->chart_id.value, view->document_revision, editable, field_clicked,
        edit_active, committed || deactivated);
    if (editable && !draft.commit_pending &&
        (committed || deactivated)) {
        const auto count = parse_bgm_column_count(draft.text.data());
        if (!count || !request_bgm_column_count(toolbar, *count))
            reset_bgm_column_count_draft(draft, *view, false);
        nk_edit_unfocus(context);
    }
    if (nk_button_label(context, "+") && editable && view->bgm_column_count < 128)
        (void)request_bgm_column_count(toolbar, view->bgm_column_count + 1);
    if (!editable) nk_widget_disable_end(context);
}

static void request_toolbar_offset_nudge(wormedit::ui::workspace_action_t* action,
                                         const wormedit::ui::compose_view_t* view, std::int64_t frames) {
    if (action->audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE) return;
    action->audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_NUDGE;
    action->audio_offset_drag_chart_id = wormedit::chart_id_t{view->chart_id.value};
    action->audio_offset_drag_document_revision = view->document_revision;
    action->audio_offset_drag_sample_rate = view->waveform_overview_sample_rate;
    action->audio_offset_drag_delta_frames = frames;
}

static void render_offset_value(nk_context* context, const wormedit::ui::compose_view_t& view) {
    if (!(view.waveform_offset_drag_active ? view.waveform_proposed_audio_offset_denominator
                                          : view.waveform_audio_offset_denominator)) {
        nk_label(context, "Unavailable", NK_TEXT_CENTERED);
        return;
    }
    nk_labelf(context, NK_TEXT_CENTERED, "%+.3f s%s",
        wormedit::to_double(wormedit::signed_rational_t{
            view.waveform_offset_drag_active ? view.waveform_proposed_audio_offset_numerator : view.waveform_audio_offset_numerator,
            view.waveform_offset_drag_active ? view.waveform_proposed_audio_offset_denominator : view.waveform_audio_offset_denominator}),
        (view.waveform_timing_locked || view.audio_offset_locked) ? " · locked" : view.waveform_offset_drag_active ? " · preview" : "");
}

static void render_inline_offset(const ComposeToolbarContext& toolbar, bool in_inspector) {
    auto* context = toolbar.context;
    const auto& view = *toolbar.view;
    const auto bounds = nk_widget_bounds(context);
    if (!state.inline_offset.inline_offset_open && nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_DOUBLE, bounds) &&
        worm_ui_nuklear_begin_inline_offset(state, view))
        state.inline_offset.inline_offset_in_inspector = in_inspector;
    if (!state.inline_offset.inline_offset_open || state.inline_offset.inline_offset_in_inspector != in_inspector) {
        render_offset_value(context, view);
        return;
    }
    worm_ui_nuklear_update_inline_offset(state, view, *toolbar.action, false, false);
    if (!state.inline_offset.inline_offset_open) {
        nk_edit_unfocus(context);
        render_offset_value(context, view);
        return;
    }
    const auto flags = wormedit::ui::chrome::autofocus_text_field(context,
        static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT,
        state.inline_offset.inline_offset_text.data(), static_cast<int>(state.inline_offset.inline_offset_text.size()),
        nk_filter_default, state.inline_offset.inline_offset_focus_pending);
    worm_ui_nuklear_update_inline_offset(state, view, *toolbar.action,
        (flags & NK_EDIT_COMMITED) != 0, (flags & NK_EDIT_DEACTIVATED) != 0);
    if (!state.inline_offset.inline_offset_open) nk_edit_unfocus(context);
    if (state.inline_offset.inline_offset_error[0] && wormedit::ui::nuklear::tooltip::hovered(context, bounds))
        wormedit::ui::nuklear::tooltip::text(context, state.inline_offset.inline_offset_error.data());
}

static void render_compose_audio_offset(const ComposeToolbarContext& toolbar, bool show_offset,
                                         bool in_inspector = false) {
    auto* context = toolbar.context;
    const auto& view = *toolbar.view;
    const bool disabled = view.waveform_timing_locked || view.audio_offset_locked || view.waveform_offset_drag_active ||
                          !view.waveform_overview_rows.size() || state.inline_offset.inline_offset_open;
    const auto ten_ms = static_cast<std::int64_t>(std::max(1U, view.waveform_overview_sample_rate / 100U));
    if (disabled) nk_widget_disable_begin(context);
    if (wormedit::ui::chrome::IconButton{Glyph::chevrons_left, "Nudge audio offset -10 ms"}.render(context))
        request_toolbar_offset_nudge(toolbar.action, &view, -ten_ms);
    if (wormedit::ui::chrome::IconButton{Glyph::chevron_left, "Nudge audio offset -1 frame"}.render(context))
        request_toolbar_offset_nudge(toolbar.action, &view, -1);
    if (disabled) nk_widget_disable_end(context);
    if (show_offset) render_inline_offset(toolbar, in_inspector);
    if (disabled) nk_widget_disable_begin(context);
    if (wormedit::ui::chrome::IconButton{Glyph::chevron_right, "Nudge audio offset +1 frame"}.render(context))
        request_toolbar_offset_nudge(toolbar.action, &view, 1);
    if (wormedit::ui::chrome::IconButton{Glyph::chevrons_right, "Nudge audio offset +10 ms"}.render(context))
        request_toolbar_offset_nudge(toolbar.action, &view, ten_ms);
    if (disabled) nk_widget_disable_end(context);
}

static void render_compose_snap_controls(nk_context* context) {
    static const char* steps[] = {"1", "0.75", "0.5", "0.33", "0.25", "0.1"};
    static const char* tooltips[] = {
        "BPM snap step 1 (Ctrl+drag beat fit; Ctrl+wheel while dragging)",
        "BPM snap step 0.75 (Ctrl+drag beat fit; Ctrl+wheel while dragging)",
        "BPM snap step 0.5 (Ctrl+drag beat fit; Ctrl+wheel while dragging)",
        "BPM snap step 0.33 (Ctrl+drag beat fit; Ctrl+wheel while dragging)",
        "BPM snap step 0.25 (Ctrl+drag beat fit; Ctrl+wheel while dragging)",
        "BPM snap step 0.1 (Ctrl+drag beat fit; Ctrl+wheel while dragging)",
    };
    const auto step_index = std::min<std::size_t>(state.waveform_drag.beat_fit_snap_step_index,
                                                  std::size(steps) - 1);
    if (wormedit::ui::chrome::IconButton{Glyph::magnet, tooltips[step_index]}.render(context))
        state.waveform_drag.beat_fit_snap_step_index = static_cast<std::uint8_t>((step_index + 1) % std::size(steps));
    state.waveform_drag.beat_fit_snap_step_index = static_cast<std::uint8_t>(std::min<int>(
        wormedit::ui::chrome::safe_combo(context, steps, static_cast<int>(std::size(steps)),
                 state.waveform_drag.beat_fit_snap_step_index, 20, nk_vec2(70, 140)),
        static_cast<int>(std::size(steps) - 1)));
}

static void render_offset_padlock(const ComposeToolbarContext& toolbar) {
    auto* context = toolbar.context;
    const auto bounds = nk_widget_bounds(context);
    const bool locked = toolbar.view->audio_offset_locked;
    if (nk_button_label(context, "")) {
        toolbar.action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_AUDIO_OFFSET_LOCK;
        toolbar.action->compose_input_chart_id.value = toolbar.view->chart_id.value;
        toolbar.action->compose_input_document_revision = toolbar.view->document_revision;
    }
    auto* canvas = nk_window_get_canvas(context);
    const auto color = context->style.text.color;
    const float center = bounds.x + bounds.w * 0.5f;
    nk_stroke_rect(canvas, nk_rect(center - 5.0f + (locked ? 0.0f : 4.0f), bounds.y + 5.0f,
                                   10.0f, 11.0f), 4.0f, 2.0f, color);
    nk_fill_rect(canvas, nk_rect(center - 7.0f, bounds.y + 13.0f, 14.0f, 10.0f), 2.0f, color);
    if (wormedit::ui::nuklear::tooltip::hovered(context, bounds))
        wormedit::ui::nuklear::tooltip::text(context, locked ? "Unlock audio offset" : "Lock audio offset");
}

static void render_compose_offset_toolbar(const ComposeToolbarContext& toolbar) {
    const bool show_offset = nk_window_get_bounds(toolbar.context).w >= 900.0f;
    render_offset_padlock(toolbar);
    render_compose_audio_offset(toolbar, show_offset);
}

void render_compose_inspector_timing_controls(const ComposeToolbarContext& toolbar) {
    auto* context = toolbar.context;
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, "Audio offset · seconds", NK_TEXT_LEFT);
    nk_layout_row_template_begin(context, 27);
    wormedit::ui::chrome::IconButton::layout_columns(context, 1, 24.0f);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_end(context);
    render_offset_padlock(toolbar);
    render_inline_offset(toolbar, true);
    nk_layout_row_dynamic(context, 27, 4);
    render_compose_audio_offset(toolbar, false, true);
    nk_layout_row_template_begin(context, 27);
    nk_layout_row_template_push_dynamic(context);
    wormedit::ui::chrome::IconButton::layout_columns(context, 1, 24.0f);
    nk_layout_row_template_push_static(context, 75.0f);
    nk_layout_row_template_end(context);
    nk_label(context, "BPM snap", NK_TEXT_LEFT);
    render_compose_snap_controls(context);
}

namespace {

auto note_palette_icon(std::uint32_t kind) -> Glyph {
    switch (static_cast<wormedit::note_kind_t>(kind)) {
    case wormedit::note_kind_t::tap: return Glyph::square;
    case wormedit::note_kind_t::hold: return Glyph::minus;
    case wormedit::note_kind_t::mine: return Glyph::x;
    case wormedit::note_kind_t::invisible: return Glyph::eye;
    case wormedit::note_kind_t::lift: return Glyph::chevron_left;
    case wormedit::note_kind_t::fake: return Glyph::layers;
    case wormedit::note_kind_t::roll: return Glyph::audio_lines;
    }
    return Glyph::square;
}

auto note_palette_kind_allowed(const wormedit::ui::compose_view_t& view, uint32_t kind) -> bool {
    return (view.allowed_note_kind_mask & (1U << kind)) != 0;
}

} // namespace

bool compose_note_palette_has_buttons(const wormedit::ui::compose_view_t& view) {
    if (view.hitsounds.visible) return true;
    if (!view.note_kind_palette_visible) return false;
    for (std::size_t index = 0; index < view.note_palette.entries.size(); ++index)
        if (note_palette_kind_allowed(view, static_cast<std::uint32_t>(view.note_palette.entries[index].id))) return true;
    return false;
}

namespace {
constexpr std::array hitsound_set_labels{"Auto", "Normal", "Soft", "Drum"};

void emit_hitsound(const ComposeToolbarContext& toolbar, wormedit::hitsound_field_t field,
                   std::optional<int> value = {}) {
    auto& action = *toolbar.action;
    action.compose_hitsound = {.field = field, .value = value};
    action.compose_input_project_id.value = toolbar.view->project_id.value;
    action.compose_input_chart_id.value = toolbar.view->chart_id.value;
    action.compose_input_document_revision = toolbar.view->document_revision;
}

void draw_hitsound_check(nk_context* context, struct nk_rect bounds, std::optional<int> value,
                         struct nk_color tint) {
    auto* canvas = nk_window_get_canvas(context);
    const auto box = nk_rect(bounds.x + bounds.w - 10, bounds.y + bounds.h - 10, 8, 8);
    nk_fill_rect(canvas, box, 1, wormedit::ui::chrome::color(wormedit::ui::theme::UiColor::surface_hover));
    nk_stroke_rect(canvas, box, 1, 1, tint);
    if (!value)
        nk_stroke_line(canvas, box.x + 2, box.y + 4, box.x + 6, box.y + 4, 1, tint);
    else if (*value == 1) {
        const float tick[]{box.x + 1, box.y + 4, box.x + 3, box.y + 6, box.x + 7, box.y + 2};
        nk_stroke_polyline(canvas, const_cast<float*>(tick), 3, 1.2f, tint);
    }
}

void render_hitsound_toggle(const ComposeToolbarContext& toolbar, std::size_t index) {
    namespace chrome = wormedit::ui::chrome;
    constexpr std::array icons{wormedit::ui::vector_icons::whistle,
        wormedit::ui::vector_icons::cymbal, wormedit::ui::vector_icons::clap};
    constexpr std::array names{"Whistle (W)", "Finish (E)", "Clap (R)"};
    const auto value = toolbar.view->hitsounds.values.fields[index];
    const auto tooltip = std::string{names[index]} + ": " +
        (!value ? "mixed" : *value == 1 ? "on" : "off") + "; click toggles";
    nk_layout_row_static(toolbar.context, 30, 30, 1);
    const chrome::NativeIconCell cell{toolbar.context, value == 1, true, tooltip.c_str()};
    icons[index](nk_window_get_canvas(toolbar.context), cell.bounds(), cell.tint());
    draw_hitsound_check(toolbar.context, cell.bounds(), value, cell.tint());
    if (cell.is_clicked()) emit_hitsound(toolbar, static_cast<wormedit::hitsound_field_t>(index));
}

void draw_hitsound_set(nk_context* context, struct nk_rect bounds, bool addition,
                       std::optional<int> value, struct nk_color tint) {
    namespace chrome = wormedit::ui::chrome;
    auto* canvas = nk_window_get_canvas(context);
    nk_stroke_rect(canvas, bounds, 3, 1, chrome::color(wormedit::ui::theme::UiColor::border));
    wormedit::ui::lucide::draw(context, nk_rect(bounds.x + 3, bounds.y, 22, 20),
        addition ? Glyph::layers : Glyph::music_2, tint);
    constexpr std::array labels{"A", "N", "S", "D"};
    const char* label = value ? labels[static_cast<std::size_t>(*value)] : "−";
    nk_draw_text(canvas, nk_rect(bounds.x + 4, bounds.y + 16, 14, 14), label,
        static_cast<int>(std::strlen(label)), context->style.font, nk_rgba(0, 0, 0, 0), tint);
    const float arrow[]{bounds.x + 20, bounds.y + 22, bounds.x + 23, bounds.y + 25,
        bounds.x + 26, bounds.y + 22};
    nk_stroke_polyline(canvas, const_cast<float*>(arrow), 3, 1.2f, tint);
}

void render_hitsound_set(const ComposeToolbarContext& toolbar, std::size_t index) {
    namespace chrome = wormedit::ui::chrome;
    auto* context = toolbar.context;
    const auto value = toolbar.view->hitsounds.values.fields[index];
    const auto field = static_cast<wormedit::hitsound_field_t>(index);
    const auto tooltip = std::string{index == 3 ? "Normal sample set: " : "Addition sample set: "} +
        (value ? hitsound_set_labels[static_cast<std::size_t>(*value)] : "mixed") +
        "; click cycles, right-click chooses";
    nk_layout_row_static(context, 30, 30, 1);
    const chrome::NativeIconCell cell{context, false, true, tooltip.c_str()};
    draw_hitsound_set(context, cell.bounds(), index == 4, value, cell.tint());
    if (cell.is_clicked()) emit_hitsound(toolbar, field);
    if (nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT) &&
        nk_input_is_mouse_hovering_rect(&context->input, cell.bounds())) {
        state.hitsound_menu.field = field;
        chrome::context_menu_open(state.hitsound_menu.menu, context->input.mouse.pos,
            chrome::PopupContextStamp::from(*toolbar.view));
    }
}

void render_hitsound_tiles(const ComposeToolbarContext& toolbar) {
    if (!toolbar.view->hitsounds.visible) return;
    wormedit::ui::chrome::section_separator(toolbar.context);
    for (std::size_t index = 0; index < 3; ++index) render_hitsound_toggle(toolbar, index);
    wormedit::ui::chrome::section_separator(toolbar.context);
    for (std::size_t index = 3; index < 5; ++index) render_hitsound_set(toolbar, index);
}
} // namespace

void render_compose_hitsound_menu(const ComposeToolbarContext& toolbar) {
    namespace chrome = wormedit::ui::chrome;
    auto& menu = state.hitsound_menu.menu;
    auto* context = toolbar.context;
    if (!toolbar.view->hitsounds.visible) chrome::context_menu_abandon(menu);
    if (!menu.opened_this_frame) (void)chrome::context_menu_update(menu, context);
    if (!chrome::context_menu_begin(menu, context, chrome::PopupContextStamp::from(*toolbar.view),
        nk_vec2(220, chrome::context_menu_height(4)))) return;
    for (std::size_t index = 0; index < hitsound_set_labels.size(); ++index) {
        if (chrome::context_menu_item(menu, context, hitsound_set_labels[index], nullptr))
            emit_hitsound(toolbar, state.hitsound_menu.field, static_cast<int>(index));
    }
    chrome::context_menu_end(menu, context);
}

void render_compose_note_palette_column(const ComposeToolbarContext& toolbar) {
    // Select tool: the pointer selects instead of painting notes.
    nk_layout_row_static(toolbar.context, 30, 30, 1);
    if (wormedit::ui::chrome::DrawnIconButton{wormedit::ui::vector_icons::select_cursor,
            "Select (Alt+1)", !toolbar.view->note_paint_mode_active &&
                !toolbar.view->semantic_layer_paint_mode_active}
            .render(toolbar.context) &&
        toolbar.action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        toolbar.action->compose_action = WORM_UI_COMPOSE_ACTION_SELECT_TOOL;
    const auto count = toolbar.view->note_kind_palette_visible ? toolbar.view->note_palette.entries.size() : 0;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& descriptor = toolbar.view->note_palette.entries[index];
        const auto kind = static_cast<std::uint32_t>(descriptor.id);
        if (!note_palette_kind_allowed(*toolbar.view, kind)) continue;
        nk_layout_row_static(toolbar.context, 30, 30, 1);
        if (wormedit::ui::chrome::IconButton{note_palette_icon(kind), descriptor.label.data(),
                toolbar.view->note_paint_mode_active &&
                    toolbar.view->armed_note_kind == static_cast<wormedit::note_kind_t>(kind)}.render(toolbar.context) &&
            toolbar.action->compose_inputs.size() < wormedit::ui::compose_input_admission_limit) {
            toolbar.action->compose_inputs.emplace_back() = {
                .kind = WORM_UI_COMPOSE_INPUT_ARM_NOTE_KIND, .note_kind = static_cast<wormedit::note_kind_t>(kind)};
            // Picking a note kind selects the paint tool, mirroring Select above.
            if (!toolbar.view->note_paint_mode_active &&
                toolbar.action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
                toolbar.action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE;
        }
    }
    render_hitsound_tiles(toolbar);
}

static void layout_toolbar_icons(nk_context* context, int count) {
    wormedit::ui::chrome::IconButton::layout_columns(context, count, 24.0f);
}

// The full toolbar needs about 1,150 px (with the measure lane toggles). Narrower than that its tail is clipped, so the dock toggles
// stay in the middle (where they are always reachable) and move to the right edge only when it fits.
static auto dock_toggles_at_end(nk_context* context) -> bool {
    return nk_window_get_bounds(context).w >= 1160.0f;
}

// `panel_icons` is the dock toggle count; the BMS branch toggle exists only for charts with branches.
// `tool_icons` grows by one while the measure lane shows its time-propagation toggle.
static void layout_compose_toolbar(nk_context* context, int tool_icons, int panel_icons, bool timing_focus) {
    const auto width = nk_window_get_bounds(context).w;
    nk_layout_row_template_begin(context, 27);
    layout_toolbar_icons(context, 4); // Navigation.
    wormedit::ui::chrome::layout_separator(context);
    layout_toolbar_icons(context, tool_icons); // Tools.
    wormedit::ui::chrome::layout_separator(context);
    layout_toolbar_icons(context, 3); // Color views.
    wormedit::ui::chrome::layout_separator(context);
    if (!dock_toggles_at_end(context)) {
        layout_toolbar_icons(context, panel_icons); // Panels.
        wormedit::ui::chrome::layout_separator(context);
    }
    if (timing_focus) {
        layout_toolbar_icons(context, 3); // Audio lock, backward nudges.
        if (width >= 900.0f) nk_layout_row_template_push_static(context, 104);
        layout_toolbar_icons(context, 2); // Forward nudges.
        wormedit::ui::chrome::layout_separator(context);
        layout_toolbar_icons(context, 1); // BPM snap.
        nk_layout_row_template_push_static(context, 60);
        wormedit::ui::chrome::layout_separator(context);
    }
    layout_toolbar_icons(context, 1); // BGM scroll back.
    nk_layout_row_template_push_dynamic(context);
    layout_toolbar_icons(context, 1); // BGM scroll forward.
    if (width >= 900.0f) nk_layout_row_template_push_static(context, 85);
    layout_toolbar_icons(context, 1);
    nk_layout_row_template_push_static(context, 42);
    layout_toolbar_icons(context, 1);
    if (dock_toggles_at_end(context)) {
        // The dock toggles close the row at the right edge, after the BGM scroll bar's spacer.
        wormedit::ui::chrome::layout_separator(context);
        layout_toolbar_icons(context, panel_icons); // Panels.
    }
    layout_toolbar_icons(context, 1); // Click audition, rendered last.
    nk_layout_row_template_end(context);
}

// Last in the row so the icons before it keep their positions. Issues the registry command.
static void render_click_audition_toggle(const ComposeToolbarContext& toolbar) {
    if (wormedit::ui::chrome::IconButton{Glyph::audio_lines, "Play sound on click",
            toolbar.view->audition_on_click}.render(toolbar.context) &&
        toolbar.action->menu_command_id[0] == '\0')
        toolbar.action->menu_command_id = "audio.toggle_click_audition";
}

void render_compose_toolbar(const ComposeToolbarContext& toolbar) {
    update_compose_note_label_cache(toolbar.view);
    auto* context = toolbar.context;
    const auto spacing = context->style.window.spacing;
    const bool spacing_pushed = nk_style_push_vec2(context, &context->style.window.spacing,
                                                  nk_vec2(3.0f, spacing.y)) != 0;
    layout_compose_toolbar(context, 9 + (toolbar.view->measure_lane.visible ? 1 : 0),
                           6 + (worm_ui_nuklear_bms_branch_panel_visible(toolbar.view) ? 1 : 0),
                           toolbar.view->timing_focus);
    render_navigation_group(toolbar);
    wormedit::ui::chrome::toolbar_separator(context);
    render_tool_group(toolbar);
    wormedit::ui::chrome::toolbar_separator(context);
    render_color_view_group(toolbar);
    wormedit::ui::chrome::toolbar_separator(context);
    if (!dock_toggles_at_end(context)) {
        render_panel_group(toolbar);
        wormedit::ui::chrome::toolbar_separator(context);
    }
    if (toolbar.view->timing_focus) {
        render_compose_offset_toolbar(toolbar);
        wormedit::ui::chrome::toolbar_separator(context);
        render_compose_snap_controls(context);
        wormedit::ui::chrome::toolbar_separator(context);
    }
    render_compose_bgm_scroll_controls(toolbar);
    if (dock_toggles_at_end(context)) {
        wormedit::ui::chrome::toolbar_separator(context);
        render_panel_group(toolbar);
    }
    render_click_audition_toggle(toolbar);
    if (spacing_pushed) (void)nk_style_pop_vec2(context);
}

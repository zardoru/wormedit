#include "ui/nuklear/compose_toolbar.h"
#include "ui/nuklear/vector_icons.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/chrome.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>

using wormedit::ui::lucide::Glyph;

static auto compose_inspector_icon(bool visible) -> Glyph {
    return visible ? Glyph::panel_right_close : Glyph::panel_right_open;
}

static void append_bgm_horizontal_scroll(WormUiWorkspaceAction* action,
                                         const WormUiComposeView* view, double delta_pixels) {
    if (!action || !view || !std::isfinite(delta_pixels) || delta_pixels == 0.0 ||
        action->compose_viewport_input_count >= WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY ||
        view->bgm_maximum_horizontal_scroll_pixels <= 0.0) return;
    action->compose_viewport_inputs[action->compose_viewport_input_count++] = {
        .kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_SMOOTH_WHEEL,
        .zoom_modifier = false,
        .delta_x = delta_pixels,
        .delta_y = 0.0,
        .cursor_x_pixels = (view->playable_field_right_pixels + view->viewport_width_pixels) * 0.5,
        .cursor_y_pixels = view->viewport_height_pixels * 0.5,
    };
}

static void update_compose_note_label_cache(const WormUiComposeView* view) {
    if (state.label_project_id == view->project_id && state.label_chart_id == view->chart_id &&
        state.label_document_revision == view->document_revision &&
        state.label_note_id == view->selected_note_label_note_id)
        return;
    state.note_label.fill('\0');
    if (view->selected_note_label && view->selected_note_label_note_id != 0) {
        const auto length = std::min(strlen(view->selected_note_label), state.note_label.size() - 1);
        memcpy(state.note_label.data(), view->selected_note_label, length);
        state.note_label[length] = '\0';
    }
    state.label_project_id = view->project_id;
    state.label_chart_id = view->chart_id;
    state.label_document_revision = view->document_revision;
    state.label_note_id = view->selected_note_label_note_id;
}

// Shows or hides the song bar under the seek bar; unavailable while the song has no audio.
static void render_compose_bgm_bar_toggle(const ComposeToolbarContext& toolbar) {
    auto* context = toolbar.context;
    const auto* view = toolbar.view;
    auto* action = toolbar.action;
    if (!view->bgm_bar_available) nk_widget_disable_begin(context);
    const bool clicked = wormedit::ui::chrome::IconButton{
        Glyph::audio_lines, view->bgm_bar_reserved ? "Hide song bar" : "Show song bar",
        view->bgm_bar_reserved}.render(context);
    if (!view->bgm_bar_available) nk_widget_disable_end(context);
    if (!clicked || !view->bgm_bar_available ||
        action->compose_input_count >= WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY)
        return;
    auto& input = action->compose_inputs[action->compose_input_count++];
    input = {};
    input.kind = WORM_UI_COMPOSE_INPUT_BGM_BAR_TOGGLE;
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
    if (!can_toggle) nk_widget_disable_begin(toolbar.context);
    const auto clicked = wormedit::ui::chrome::IconButton{Glyph::rotate_ccw, tooltip,
            view.transport_loop_selection_enabled}.render(toolbar.context) && can_toggle &&
        action->kind == WORM_UI_WORKSPACE_ACTION_NONE;
    if (!can_toggle) nk_widget_disable_end(toolbar.context);
    if (clicked) {
        action->kind = WORM_UI_WORKSPACE_ACTION_TRANSPORT_SET_LOOP_SELECTION;
        action->transport_loop_chart_id = view.chart_id;
        action->transport_loop_document_revision = view.document_revision;
        action->transport_loop_begin_row = view.area_range_available ? view.area_range_begin_row : 0;
        action->transport_loop_end_row = view.area_range_available ? view.area_range_end_row : 0;
        action->transport_loop_enabled = !view.transport_loop_selection_enabled;
    }
}

static void render_compose_view_controls(const ComposeToolbarContext& toolbar) {
    auto* context = toolbar.context;
    const auto* view = toolbar.view;
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
    render_loop_selection_button(toolbar);
    const auto paint_disabled = !view->scene_available || view->timing_focus;
    if (paint_disabled) nk_widget_disable_begin(context);
    if (wormedit::ui::chrome::IconButton{Glyph::paintbrush, "Toggle note paint mode",
                                         view->note_paint_mode_active}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE;
    if (paint_disabled) nk_widget_disable_end(context);
    if (!view->scene_available) nk_widget_disable_begin(context);
    if (wormedit::ui::chrome::DrawnIconButton{wormedit::ui::vector_icons::timer,
                                              "Timing focus: edit timing only",
                                              view->timing_focus}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_TIMING_FOCUS;
    if (wormedit::ui::chrome::DrawnIconButton{wormedit::ui::vector_icons::grid,
                                              view->edit_snap_enabled
                                                  ? "Snap on: edits land on the grid (click to turn off)"
                                                  : "Snap off: edits use the finest row (click to turn on)",
                                              view->edit_snap_enabled}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_EDIT_SNAP;
    if (!view->scene_available) nk_widget_disable_end(context);
    if (wormedit::ui::chrome::IconButton{compose_inspector_icon(state.inspector_visible),
                                         state.inspector_visible ? "Hide inspector" : "Show inspector",
                                         state.inspector_visible}.render(context))
        state.inspector_visible = !state.inspector_visible;
    if (wormedit::ui::chrome::DrawnIconButton{wormedit::ui::vector_icons::sound_list,
                                              state.sounds_panel_visible ? "Hide sounds list"
                                                                         : "Show sounds list",
                                              state.sounds_panel_visible}.render(context))
        state.sounds_panel_visible = !state.sounds_panel_visible;
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
    nk_labelf(context, NK_TEXT_RIGHT, "%.0f px/measure · %zu lanes",
              view->pixels_per_standard_measure, view->lane_count);
}

static void reset_bgm_column_count_draft(
                                         worm_ui_nuklear_compose_state_t::bgm_column_count_edit_t& draft,
                                         const WormUiComposeView& view,
                                         bool clear_ownership = true) {
    const auto was_owned = draft.input_owned;
    const auto release_pending = draft.release_pending;
    draft.chart_id = view.chart_id;
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

static auto bgm_column_count_draft(const WormUiComposeView& view)
    -> worm_ui_nuklear_compose_state_t::bgm_column_count_edit_t& {
    auto& draft = state.bgm_column_count_edit;
    if (!draft.initialized || !view.bgm_column_count_editable ||
        draft.chart_id != view.chart_id || draft.revision != view.document_revision ||
        draft.commit_pending)
        reset_bgm_column_count_draft(draft, view);
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
    action->compose_input_chart_id = toolbar.view->chart_id;
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
    nk_label(context, "BGM columns:", NK_TEXT_RIGHT);
    if (nk_button_label(context, "-") && editable && view->bgm_column_count > 1)
        (void)request_bgm_column_count(toolbar, view->bgm_column_count - 1);
    const auto field_bounds = nk_widget_bounds(context);
    const bool field_clicked = nk_input_is_mouse_click_in_rect(
        &context->input, NK_BUTTON_LEFT, field_bounds);
    const auto flags = wormedit::ui::chrome::worm_ui_nuklear_text_field(
        context, static_cast<nk_flags>(NK_EDIT_FIELD | NK_EDIT_SIG_ENTER), draft.text.data(),
        static_cast<int>(draft.text.size()), nk_filter_decimal);
    const bool edit_active = (flags & NK_EDIT_ACTIVE) != 0;
    const bool deactivated = (flags & NK_EDIT_DEACTIVATED) != 0;
    const bool committed = (flags & NK_EDIT_COMMITED) != 0;
    (void)worm_ui_nuklear_update_bgm_column_count_input_owner(
        state, view->chart_id, view->document_revision, editable, field_clicked,
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

static void request_toolbar_offset_nudge(WormUiWorkspaceAction* action,
                                         const WormUiComposeView* view, std::int64_t frames) {
    if (action->audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE) return;
    action->audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_NUDGE;
    action->audio_offset_drag_chart_id = view->chart_id;
    action->audio_offset_drag_document_revision = view->document_revision;
    action->audio_offset_drag_sample_rate = view->waveform_overview_sample_rate;
    action->audio_offset_drag_delta_frames = frames;
}

static void render_offset_value(nk_context* context, const WormUiComposeView& view) {
    nk_labelf(context, NK_TEXT_CENTERED, "%+.3f s%s",
        static_cast<double>(view.waveform_offset_drag_active
            ? view.waveform_proposed_audio_offset_numerator : view.waveform_audio_offset_numerator) /
        static_cast<double>(view.waveform_offset_drag_active
            ? view.waveform_proposed_audio_offset_denominator : view.waveform_audio_offset_denominator),
        (view.waveform_timing_locked || view.audio_offset_locked) ? " · locked" : view.waveform_offset_drag_active ? " · preview" : "");
}

static void render_inline_offset(const ComposeToolbarContext& toolbar) {
    auto* context = toolbar.context;
    const auto& view = *toolbar.view;
    const auto bounds = nk_widget_bounds(context);
    if (!state.inline_offset_open && nk_input_is_mouse_click_in_rect(&context->input, NK_BUTTON_DOUBLE, bounds))
        (void)worm_ui_nuklear_begin_inline_offset(state, view);
    if (!state.inline_offset_open) {
        render_offset_value(context, view);
        return;
    }
    worm_ui_nuklear_update_inline_offset(state, view, *toolbar.action, false, false);
    if (!state.inline_offset_open) {
        nk_edit_unfocus(context);
        render_offset_value(context, view);
        return;
    }
    const auto flags = wormedit::ui::chrome::autofocus_text_field(context,
        NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT,
        state.inline_offset_text.data(), static_cast<int>(state.inline_offset_text.size()),
        nk_filter_default, state.inline_offset_focus_pending);
    worm_ui_nuklear_update_inline_offset(state, view, *toolbar.action,
        (flags & NK_EDIT_COMMITED) != 0, (flags & NK_EDIT_DEACTIVATED) != 0);
    if (!state.inline_offset_open) nk_edit_unfocus(context);
    if (state.inline_offset_error[0] && nk_input_is_mouse_hovering_rect(&context->input, bounds))
        nk_tooltip(context, state.inline_offset_error.data());
}

static void render_compose_audio_offset(const ComposeToolbarContext& toolbar, bool show_offset) {
    auto* context = toolbar.context;
    const auto& view = *toolbar.view;
    const bool disabled = view.waveform_timing_locked || view.audio_offset_locked || view.waveform_offset_drag_active ||
                          !view.waveform_overview_row_count || state.inline_offset_open;
    const auto ten_ms = static_cast<std::int64_t>(std::max(1U, view.waveform_overview_sample_rate / 100U));
    if (disabled) nk_widget_disable_begin(context);
    if (wormedit::ui::chrome::IconButton{Glyph::chevrons_left, "Nudge audio offset -10 ms"}.render(context))
        request_toolbar_offset_nudge(toolbar.action, &view, -ten_ms);
    if (wormedit::ui::chrome::IconButton{Glyph::chevron_left, "Nudge audio offset -1 frame"}.render(context))
        request_toolbar_offset_nudge(toolbar.action, &view, -1);
    if (disabled) nk_widget_disable_end(context);
    if (show_offset) render_inline_offset(toolbar);
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
    const auto step_index = std::min<std::size_t>(state.beat_fit_snap_step_index,
                                                  std::size(steps) - 1);
    if (wormedit::ui::chrome::IconButton{Glyph::magnet, tooltips[step_index]}.render(context))
        state.beat_fit_snap_step_index = static_cast<std::uint8_t>((step_index + 1) % std::size(steps));
    state.beat_fit_snap_step_index = static_cast<std::uint8_t>(std::min<int>(
        wormedit::ui::chrome::safe_combo(context, steps, static_cast<int>(std::size(steps)),
                 state.beat_fit_snap_step_index, 20, nk_vec2(70, 140)),
        static_cast<int>(std::size(steps) - 1)));
}

static void render_offset_padlock(const ComposeToolbarContext& toolbar) {
    auto* context = toolbar.context;
    const auto bounds = nk_widget_bounds(context);
    const bool locked = toolbar.view->audio_offset_locked;
    if (nk_button_label(context, "")) {
        toolbar.action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_AUDIO_OFFSET_LOCK;
        toolbar.action->compose_input_chart_id = toolbar.view->chart_id;
        toolbar.action->compose_input_document_revision = toolbar.view->document_revision;
    }
    auto* canvas = nk_window_get_canvas(context);
    const auto color = context->style.text.color;
    const float center = bounds.x + bounds.w * 0.5f;
    nk_stroke_rect(canvas, nk_rect(center - 5.0f + (locked ? 0.0f : 4.0f), bounds.y + 5.0f,
                                   10.0f, 11.0f), 4.0f, 2.0f, color);
    nk_fill_rect(canvas, nk_rect(center - 7.0f, bounds.y + 13.0f, 14.0f, 10.0f), 2.0f, color);
    if (nk_input_is_mouse_hovering_rect(&context->input, bounds))
        nk_tooltip(context, locked ? "Unlock audio offset" : "Lock audio offset");
}

static void render_compose_offset_toolbar(const ComposeToolbarContext& toolbar) {
    const auto* view = toolbar.view;
    const bool show_offset = nk_window_get_bounds(toolbar.context).w >= 900.0f;
    render_offset_padlock(toolbar);
    nk_spacing(toolbar.context, 1);
    render_compose_audio_offset(toolbar, show_offset);
    nk_label(toolbar.context, "", NK_TEXT_LEFT);
    render_compose_snap_controls(toolbar.context);
    nk_label(toolbar.context, "", NK_TEXT_LEFT);
}

namespace {

constexpr uint32_t note_palette_kinds[]{0, 1, 4, 3, 5, 6, 2};

auto note_palette_kind_allowed(const WormUiComposeView& view, uint32_t kind) -> bool {
    return (view.allowed_note_kind_mask & (1U << kind)) != 0;
}

} // namespace

bool compose_note_palette_has_buttons(const WormUiComposeView& view) {
    return view.note_kind_palette_visible &&
           std::ranges::any_of(note_palette_kinds, [&](uint32_t kind) {
               return note_palette_kind_allowed(view, kind);
           });
}

void render_compose_note_palette_column(const ComposeToolbarContext& toolbar) {
    // Stable shortcut order differs from the document enum order.
    static constexpr Glyph icons[]{Glyph::square, Glyph::minus, Glyph::x,
        Glyph::eye, Glyph::chevron_left, Glyph::layers, Glyph::audio_lines};
    static constexpr const char* labels[]{"Tap (Alt+1)", "Hold (Alt+2; click to begin hold gesture)",
        "Mine (Alt+3)", "Invisible (Alt+4)", "Lift (Alt+5)", "Fake (Alt+6)",
        "Roll (Alt+7; click to begin hold gesture)"};
    // Select tool: the pointer selects instead of painting notes.
    nk_layout_row_static(toolbar.context, 30, 30, 1);
    if (wormedit::ui::chrome::DrawnIconButton{wormedit::ui::vector_icons::select_cursor,
            "Select (leave note paint mode)", !toolbar.view->note_paint_mode_active}
            .render(toolbar.context) &&
        toolbar.view->note_paint_mode_active &&
        toolbar.action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        toolbar.action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE;
    for (std::size_t index = 0; index < std::size(note_palette_kinds); ++index) {
        const auto kind = note_palette_kinds[index];
        if (!note_palette_kind_allowed(*toolbar.view, kind)) continue;
        nk_layout_row_static(toolbar.context, 30, 30, 1);
        if (wormedit::ui::chrome::IconButton{icons[index], labels[index],
                toolbar.view->note_paint_mode_active &&
                    toolbar.view->armed_note_kind == kind}.render(toolbar.context) &&
            toolbar.action->compose_input_count < WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) {
            toolbar.action->compose_inputs[toolbar.action->compose_input_count++] = {
                .kind = WORM_UI_COMPOSE_INPUT_ARM_NOTE_KIND, .note_kind = kind};
            // Picking a note kind selects the paint tool, mirroring Select above.
            if (!toolbar.view->note_paint_mode_active &&
                toolbar.action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
                toolbar.action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE;
        }
    }
}

void render_compose_toolbar(const ComposeToolbarContext& toolbar) {
    update_compose_note_label_cache(toolbar.view);
    auto* context = toolbar.context;
    const auto spacing = context->style.window.spacing;
    const auto compact_spacing = nk_vec2(1.0f, spacing.y);
    const bool spacing_pushed =
        nk_style_push_vec2(context, &context->style.window.spacing, compact_spacing) != 0;
    nk_layout_row_template_begin(context, 27);
    wormedit::ui::chrome::IconButton::layout_columns(context, 3);
    nk_layout_row_template_push_static(context, 34);
    wormedit::ui::chrome::IconButton::layout_columns(context, 6);
    // Offset padlock, nudges and optional readout, then the BPM snap controls.
    wormedit::ui::chrome::IconButton::layout_columns(context);
    nk_layout_row_template_push_static(context, 10);
    wormedit::ui::chrome::IconButton::layout_columns(context, 2);
    if (nk_window_get_bounds(context).w >= 900.0f)
        nk_layout_row_template_push_static(context, 104);
    wormedit::ui::chrome::IconButton::layout_columns(context, 2);
    nk_layout_row_template_push_static(context, 16);
    wormedit::ui::chrome::IconButton::layout_columns(context);
    nk_layout_row_template_push_static(context, 70);
    nk_layout_row_template_push_static(context, 12);
    // BGM column scroll: left, progress, right, then the zoom readout.
    wormedit::ui::chrome::IconButton::layout_columns(context);
    nk_layout_row_template_push_dynamic(context);
    wormedit::ui::chrome::IconButton::layout_columns(context);
    nk_layout_row_template_push_static(context, 100);
    nk_layout_row_template_push_static(context, 24);
    nk_layout_row_template_push_static(context, 42);
    nk_layout_row_template_push_static(context, 24);
    nk_layout_row_template_push_static(context, 215);
    nk_layout_row_template_end(context);
    render_compose_view_controls(toolbar);
    render_compose_offset_toolbar(toolbar);
    render_compose_bgm_scroll_controls(toolbar);
    if (spacing_pushed) (void)nk_style_pop_vec2(context);
}

#include "ui/nuklear/compose_toolbar.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/chrome.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>

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
    nk_label(context, "", NK_TEXT_LEFT);
    if (!view->scene_available) nk_widget_disable_begin(context);
    if (wormedit::ui::chrome::IconButton{Glyph::paintbrush, "Toggle note paint mode",
                                         view->note_paint_mode_active}.render(context) &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE)
        action->compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_NOTE_PAINT_MODE;
    if (!view->scene_available) nk_widget_disable_end(context);
    if (wormedit::ui::chrome::IconButton{compose_inspector_icon(state.inspector_visible),
                                         state.inspector_visible ? "Hide inspector" : "Show inspector",
                                         state.inspector_visible}.render(context))
        state.inspector_visible = !state.inspector_visible;
}

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
    nk_labelf(context, NK_TEXT_RIGHT, "%.0f px/measure · %zu lanes",
              view->pixels_per_standard_measure, view->lane_count);
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

static void render_compose_audio_offset(const ComposeToolbarContext& toolbar, bool show_offset) {
    auto* context = toolbar.context;
    const auto& view = *toolbar.view;
    const bool disabled = view.waveform_timing_locked || view.waveform_offset_drag_active;
    const auto ten_ms = static_cast<std::int64_t>(
        std::max(1U, view.waveform_overview_sample_rate / 100U));
    if (disabled) nk_widget_disable_begin(context);
    if (wormedit::ui::chrome::IconButton{Glyph::chevrons_left,
            "Nudge audio offset -10 ms"}.render(context))
        request_toolbar_offset_nudge(toolbar.action, &view, -ten_ms);
    if (wormedit::ui::chrome::IconButton{Glyph::chevron_left,
            "Nudge audio offset -1 frame"}.render(context))
        request_toolbar_offset_nudge(toolbar.action, &view, -1);
    if (show_offset)
        nk_labelf(context, NK_TEXT_CENTERED, "%+.3f s%s",
            static_cast<double>(view.waveform_offset_drag_active
                ? view.waveform_proposed_audio_offset_numerator
                : view.waveform_audio_offset_numerator) /
                static_cast<double>(view.waveform_offset_drag_active
                    ? view.waveform_proposed_audio_offset_denominator
                    : view.waveform_audio_offset_denominator),
            view.waveform_timing_locked ? " · locked"
            : view.waveform_offset_drag_active ? " · preview" : "");
    if (wormedit::ui::chrome::IconButton{Glyph::chevron_right,
            "Nudge audio offset +1 frame"}.render(context))
        request_toolbar_offset_nudge(toolbar.action, &view, 1);
    if (wormedit::ui::chrome::IconButton{Glyph::chevrons_right,
            "Nudge audio offset +10 ms"}.render(context))
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

static void render_compose_offset_toolbar(const ComposeToolbarContext& toolbar) {
    const auto* view = toolbar.view;
    const bool show_offset = nk_window_get_bounds(toolbar.context).w >= 900.0f;
    nk_label(toolbar.context, "", NK_TEXT_LEFT);
    if (view->waveform_overview_row_count == 0) {
        nk_widget_disable_begin(toolbar.context);
        for (int slot = 0; slot < 2; ++slot)
            (void)wormedit::ui::chrome::IconButton{Glyph::chevron_left, ""}.render(toolbar.context);
        if (show_offset) nk_label(toolbar.context, "", NK_TEXT_LEFT);
        for (int slot = 0; slot < 2; ++slot)
            (void)wormedit::ui::chrome::IconButton{Glyph::chevron_right, ""}.render(toolbar.context);
        nk_widget_disable_end(toolbar.context);
    } else {
        render_compose_audio_offset(toolbar, show_offset);
    }
    nk_label(toolbar.context, "", NK_TEXT_LEFT);
    render_compose_snap_controls(toolbar.context);
    nk_label(toolbar.context, "", NK_TEXT_LEFT);
}

static void render_note_kind_palette(const ComposeToolbarContext& toolbar) {
    // Stable shortcut order differs from the document enum order.
    static constexpr uint32_t kinds[]{0, 1, 4, 3, 5, 6, 2};
    static constexpr Glyph icons[]{Glyph::square, Glyph::minus, Glyph::x,
        Glyph::eye, Glyph::chevron_left, Glyph::layers, Glyph::audio_lines};
    static constexpr const char* labels[]{"Tap (Alt+1)", "Hold (Alt+2; click to begin hold gesture)",
        "Mine (Alt+3)", "Invisible (Alt+4)", "Lift (Alt+5)", "Fake (Alt+6)",
        "Roll (Alt+7; click to begin hold gesture)"};
    unsigned count{};
    for (const auto kind : kinds)
        if ((toolbar.view->allowed_note_kind_mask & (1U << kind)) != 0) ++count;
    if (count == 0) return;
    nk_layout_row_static(toolbar.context, 27, 30, static_cast<int>(count));
    for (std::size_t index = 0; index < std::size(kinds); ++index) {
        const auto kind = kinds[index];
        if ((toolbar.view->allowed_note_kind_mask & (1U << kind)) == 0) continue;
        if (wormedit::ui::chrome::IconButton{icons[index], labels[index],
                toolbar.view->armed_note_kind == kind}.render(toolbar.context) &&
            toolbar.action->compose_input_count < WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) {
            toolbar.action->compose_inputs[toolbar.action->compose_input_count++] = {
                .kind = WORM_UI_COMPOSE_INPUT_ARM_NOTE_KIND, .note_kind = kind};
        }
    }
}

void render_compose_toolbar(const ComposeToolbarContext& toolbar) {
    update_compose_note_label_cache(toolbar.view);
    auto* context = toolbar.context;
    nk_layout_row_template_begin(context, 27);
    wormedit::ui::chrome::IconButton::layout_columns(context, 3);
    nk_layout_row_template_push_static(context, 12);
    wormedit::ui::chrome::IconButton::layout_columns(context, 2);
    // Audio offset nudges, optional readout, then the BPM snap magnet and step combo.
    nk_layout_row_template_push_static(context, 12);
    wormedit::ui::chrome::IconButton::layout_columns(context, 2);
    if (nk_window_get_bounds(context).w >= 900.0f)
        nk_layout_row_template_push_static(context, 150);
    wormedit::ui::chrome::IconButton::layout_columns(context, 2);
    nk_layout_row_template_push_static(context, 12);
    wormedit::ui::chrome::IconButton::layout_columns(context);
    nk_layout_row_template_push_static(context, 70);
    nk_layout_row_template_push_static(context, 12);
    // BGM column scroll: left, progress, right, then the zoom readout.
    wormedit::ui::chrome::IconButton::layout_columns(context);
    nk_layout_row_template_push_dynamic(context);
    wormedit::ui::chrome::IconButton::layout_columns(context);
    nk_layout_row_template_push_static(context, 215);
    nk_layout_row_template_end(context);
    render_compose_view_controls(toolbar);
    render_compose_offset_toolbar(toolbar);
    render_compose_bgm_scroll_controls(toolbar);
    if (toolbar.view->note_kind_palette_visible) render_note_kind_palette(toolbar);
}

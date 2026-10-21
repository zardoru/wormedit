#include "ui/nuklear/compose_sound_picker.h"

#include "ui/nuklear/chrome.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/compose_resources.h"
#include "ui/nuklear/popup_lifecycle.h"
#include <algorithm>
#include <cstring>

namespace {
void activate(const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action,
              worm_ui_nuklear_compose_state_t& compose, std::size_t index) {
    auto& picker = compose.sound_picker_ui.sound_picker;
    const auto& result = view.sound_picker;
    if (!result.ready || index >= result.rows.size() || result.rows[index].missing ||
        std::strcmp(picker.query.data(), result.query.c_str()) != 0 ||
        action.compose_inputs.size() >= wormedit::ui::compose_input_admission_limit) return;
    const auto id = result.rows[index].resource_id.value;
    if (!id) return;
    compose.keysound_catalog.keysound_selection.selected = {id};
    compose.keysound_catalog.keysound_selection.primary = compose.keysound_catalog.keysound_selection.anchor = id;
    set_armed_resource(compose, id, compose_resource_origin_t::sounds);
    action.compose_input_project_id.value = view.project_id.value;
    action.compose_input_chart_id.value = view.chart_id.value;
    action.compose_input_document_revision = view.document_revision;
    worm_ui_nuklear_pick_draw_source(compose, action, 1, id);
    cancel_compose_sound_picker(picker);
}
void render_search(nk_context* context, sound_picker_state_t& state) {
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, "Go to sound", NK_TEXT_LEFT);
    const auto previous = state.query;
    nk_layout_row_dynamic(context, 28, 1);
    (void)wormedit::ui::chrome::autofocus_text_field(context,
        static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER, state.query.data(),
        static_cast<int>(state.query.size()), nk_filter_default, state.focus_pending);
    if (state.query != previous) state.selected = 0;
}
void render_keyboard_hint(nk_context* context) {
    nk_layout_row_template_begin(context, 22);
    nk_layout_row_template_push_static(context, 22);
    nk_layout_row_template_push_static(context, 22);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_end(context);
    for (const auto glyph : {wormedit::ui::lucide::Glyph::chevron_up,
                             wormedit::ui::lucide::Glyph::chevron_down}) {
        struct nk_rect bounds{};
        if (nk_widget(&bounds, context) != NK_WIDGET_INVALID)
            wormedit::ui::lucide::draw(context, bounds, glyph, context->style.text.color);
    }
    nk_label(context, "choose · Enter arms · Esc cancels", NK_TEXT_LEFT);
}

void render_results(nk_context* context, const wormedit::ui::compose_view_t& view,
                    wormedit::ui::workspace_action_t& action, worm_ui_nuklear_compose_state_t& compose) {
    auto& picker = compose.sound_picker_ui.sound_picker;
    const auto& results = view.sound_picker;
    nk_layout_row_dynamic(context, 22, 1);
    if (!results.ready || std::strcmp(results.query.c_str(), picker.query.data()) != 0) {
        nk_label(context, "Searching…", NK_TEXT_LEFT);
        return;
    }
    if (results.rows.size() == 0) {
        nk_label(context, "No matching sounds.", NK_TEXT_LEFT);
        return;
    }
    picker.selected = std::min(picker.selected, results.rows.size() - 1);
    if (nk_input_is_key_pressed(&context->input, NK_KEY_UP) && picker.selected > 0)
        --picker.selected;
    if (nk_input_is_key_pressed(&context->input, NK_KEY_DOWN))
        picker.selected = std::min(picker.selected + 1, results.rows.size() - 1);
    if (nk_input_is_key_pressed(&context->input, NK_KEY_ENTER))
        activate(view, action, compose, picker.selected);
    nk_labelf(context, NK_TEXT_LEFT, "%zu matches · result %zu/%zu · type to narrow",
              results.total_count, picker.selected + 1, results.rows.size());
    // A bounded page keeps keyboard-highlighted rows visible even in small windows.
    const auto available = nk_window_get_content_region(context).h - 125.0f;
    const auto page = static_cast<std::size_t>(std::clamp(available / (27.0f + context->style.window.spacing.y), 1.0f, 10.0f));
    const auto first = picker.selected / page * page;
    const auto end = std::min(results.rows.size(), first + page);
    for (auto index = first; index < end; ++index) {
        const auto& row = results.rows[index];
        nk_layout_row_dynamic(context, 27, 1);
        nk_bool selected = picker.selected == index;
        if (row.missing) nk_widget_disable_begin(context);
        if (nk_selectable_label(context, row.label.c_str() ? row.label.c_str() : "(unnamed sound)",
                                NK_TEXT_LEFT, &selected)) activate(view, action, compose, index);
        if (row.missing) nk_widget_disable_end(context);
    }
    render_keyboard_hint(context);
}
} // namespace

void open_compose_sound_picker(sound_picker_state_t& state, std::uint64_t project,
                               std::uint64_t chart, std::uint64_t revision) {
    state = {};
    state.open = state.focus_pending = true;
    state.project = project;
    state.chart = chart;
    state.revision = revision;
}
void cancel_compose_sound_picker(sound_picker_state_t& state) {
    if (!state.open) return;
    state.cancel();
    state.focus_pending = false;
}
auto compose_sound_picker_owns_input(const sound_picker_state_t& state) -> bool {
    return state.owns_input();
}
void render_compose_sound_picker(nk_context* context, const wormedit::ui::compose_view_t& view,
    wormedit::ui::workspace_action_t& action, worm_ui_nuklear_compose_state_t& compose, bool focused) {
    auto& state = compose.sound_picker_ui.sound_picker;
    if (!context) return;
    if (!state.open && !state.close_pending) {
        (void)wormedit::ui::chrome::release_retained_popup(
            context, state, wormedit::ui::chrome::PopupReleaseKeys::all);
        return;
    }
    if (!focused || state.project != view.project_id.value || state.chart != view.chart_id.value ||
        state.revision != view.document_revision)
        cancel_compose_sound_picker(state);
    const auto parent = nk_window_get_bounds(context);
    const auto region = nk_window_get_content_region(context);
    const auto width = std::min(660.0f, std::max(200.0f, parent.w - 24.0f));
    const auto height = std::min(440.0f, std::max(160.0f, parent.h - 24.0f));
    const auto bounds = nk_rect(parent.x + (parent.w - width) * 0.5f - region.x,
        parent.y + (parent.h - height) * 0.5f - region.y, width, height);
    if (state.open && wormedit::ui::chrome::popup_dismiss_requested(context,
        wormedit::ui::chrome::popup_screen_bounds(context, bounds))) cancel_compose_sound_picker(state);
    const bool was_open = state.open;
    auto popup = wormedit::ui::chrome::begin_retained_popup(context, state, "Go to sound",
        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, bounds,
        wormedit::ui::chrome::retained_popup_begin_failure_t::cancel);
    if (!popup.has_contents()) {
        if (was_open) state.focus_pending = false;
        return;
    }
    if (state.open) {
        render_search(context, state);
        render_results(context, view, action, compose);
        if (state.open && nk_input_is_key_pressed(&context->input, NK_KEY_ENTER))
            state.focus_pending = true;
    }
    wormedit::ui::chrome::end_retained_popup(context, state, popup,
        wormedit::ui::chrome::retained_popup_edit_focus_t::unfocus_on_close,
        wormedit::ui::chrome::retained_popup_close_trigger_t::closed);
}

#include "ui/nuklear/compose_sound_picker.h"

#include "ui/nuklear/chrome.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/popup_lifecycle.h"
#include <algorithm>
#include <cstring>

namespace {
auto quiet_frame(const nk_context& context) -> bool {
    for (const auto& button : context.input.mouse.buttons)
        if (button.down || button.clicked) return false;
    for (const auto& key : context.input.keyboard.keys)
        if (key.down || key.clicked) return false;
    return true;
}
void activate(const WormUiComposeView& view, WormUiWorkspaceAction& action,
              worm_ui_nuklear_compose_state_t& compose, std::size_t index) {
    auto& picker = compose.sound_picker;
    const auto& result = view.sound_picker;
    if (!result.ready || index >= result.row_count || result.rows[index].missing ||
        std::strcmp(picker.query.data(), result.query) != 0 ||
        action.compose_input_count >= WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) return;
    const auto id = result.rows[index].resource_id;
    if (!id) return;
    compose.keysound_selection.selected = {id};
    compose.keysound_selection.primary = compose.keysound_selection.anchor = id;
    compose.armed_resource_id = id;
    compose.resource_filter = worm_ui_nuklear_compose_resource_filter_t::sample;
    compose.armed_from_sounds_dock = true;
    compose.armed_from_bga_dock = false;
    action.compose_input_project_id = view.project_id;
    action.compose_input_chart_id = view.chart_id;
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

void render_results(nk_context* context, const WormUiComposeView& view,
                    WormUiWorkspaceAction& action, worm_ui_nuklear_compose_state_t& compose) {
    auto& picker = compose.sound_picker;
    const auto& results = view.sound_picker;
    nk_layout_row_dynamic(context, 22, 1);
    if (!results.ready || std::strcmp(results.query, picker.query.data()) != 0) {
        nk_label(context, "Searching…", NK_TEXT_LEFT);
        return;
    }
    if (results.row_count == 0) {
        nk_label(context, "No matching sounds.", NK_TEXT_LEFT);
        return;
    }
    picker.selected = std::min(picker.selected, results.row_count - 1);
    if (nk_input_is_key_pressed(&context->input, NK_KEY_UP) && picker.selected > 0)
        --picker.selected;
    if (nk_input_is_key_pressed(&context->input, NK_KEY_DOWN))
        picker.selected = std::min(picker.selected + 1, results.row_count - 1);
    if (nk_input_is_key_pressed(&context->input, NK_KEY_ENTER))
        activate(view, action, compose, picker.selected);
    nk_labelf(context, NK_TEXT_LEFT, "%zu matches · result %zu/%zu · type to narrow",
              results.total_count, picker.selected + 1, results.row_count);
    // A bounded page keeps keyboard-highlighted rows visible even in small windows.
    const auto available = nk_window_get_content_region(context).h - 125.0f;
    const auto page = static_cast<std::size_t>(std::clamp(available / (27.0f + context->style.window.spacing.y), 1.0f, 10.0f));
    const auto first = picker.selected / page * page;
    const auto end = std::min(results.row_count, first + page);
    for (auto index = first; index < end; ++index) {
        const auto& row = results.rows[index];
        nk_layout_row_dynamic(context, 27, 1);
        nk_bool selected = picker.selected == index;
        if (row.missing) nk_widget_disable_begin(context);
        if (nk_selectable_label(context, row.label ? row.label : "(unnamed sound)",
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
    state.open = false;
    state.release_pending = state.close_pending = true;
    state.focus_pending = false;
}
auto compose_sound_picker_owns_input(const sound_picker_state_t& state) -> bool {
    return state.open || state.release_pending;
}
void render_compose_sound_picker(nk_context* context, const WormUiComposeView& view,
    WormUiWorkspaceAction& action, worm_ui_nuklear_compose_state_t& compose, bool focused) {
    auto& state = compose.sound_picker;
    if (!context) return;
    if (!state.open && !state.close_pending) {
        if (quiet_frame(*context)) state.release_pending = false;
        return;
    }
    if (!focused || state.project != view.project_id || state.chart != view.chart_id ||
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
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "Go to sound",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, bounds)) {
        cancel_compose_sound_picker(state);
        state.close_pending = false;
        return;
    }
    if (state.open) {
        render_search(context, state);
        render_results(context, view, action, compose);
        if (state.open && nk_input_is_key_pressed(&context->input, NK_KEY_ENTER))
            state.focus_pending = true;
    }
    if (!state.open) {
        nk_edit_unfocus(context);
        nk_popup_close(context);
        state.close_pending = false;
    }
    nk_popup_end(context);
}

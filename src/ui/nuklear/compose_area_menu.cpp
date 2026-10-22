#include "ui/nuklear/compose_menu_command.h"
#include "sys/compose/compose_scroll_geometry.h"
#include "ui/nuklear/compose_area_menu.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/compose_note_menu.h"
#include "ui/nuklear/context_menu.h"
#include "wormedit/nuklear_config.h"
#include "ui/nuklear/compose_state.h"

namespace {
namespace chrome = wormedit::ui::chrome;

ComposeAreaCapture capture;
bool consumed{};
bool band_visible{};
bool sound_layers{};
chrome::ContextMenu menu{.title = "Area actions"};


void retire_sound_layer_window(wormedit::ui::workspace_action_t& action) {
    if (!sound_layers) return;
    if (action.compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action.compose_action = WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU;
        action.compose_input_project_id.value = menu.stamp.project_id;
        action.compose_input_chart_id.value = menu.stamp.chart_id;
        action.compose_input_document_revision = menu.stamp.revision;
    }
    sound_layers = false;
}

void close_menu(nk_context* context, wormedit::ui::workspace_action_t& action) {
    chrome::context_menu_close(menu, context);
    retire_sound_layer_window(action);
    consumed = true;
}

void menu_entry(nk_context* context, const char* title, const char* disabled_reason,
                WormUiComposeActionKind kind, wormedit::ui::workspace_action_t& action) {
    if (chrome::context_menu_item(menu, context, title, disabled_reason)) {
        action.compose_action = kind;
        action.compose_input_project_id.value = menu.stamp.project_id;
        action.compose_input_chart_id.value = menu.stamp.chart_id;
        action.compose_input_document_revision = menu.stamp.revision;
        consumed = true;
    }
}

auto reason_unless(bool enabled, const char* reason) -> const char* {
    return enabled ? nullptr : reason;
}

void raise_sound_layer_action(const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action,
                             WormUiComposeActionKind kind) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action.compose_action = kind;
    action.compose_input_project_id.value = view.project_id.value;
    action.compose_input_chart_id.value = view.chart_id.value;
    action.compose_input_document_revision = view.document_revision;
}

void render_sound_layer_entry(nk_context* context, const wormedit::ui::compose_view_t& view,
                              wormedit::ui::workspace_action_t& action) {
    nk_layout_row_dynamic(context, chrome::context_menu_row_height, 1);
    const auto reason = view.selected_note_count + view.selected_bgm_event_count == 0
        ? "Select notes or BGM first" : view.semantic_layer_gesture_active || view.timing_focus
        ? "Finish editing before assigning sound layers" : view.note_menu_layer_total_count == 0
        ? "Create a semantic layer first" : nullptr;
    if (!chrome::context_menu_button(context, "Add selected sounds to layer…", reason, true)) return;
    sound_layers = true;
    chrome::context_menu_reset_navigation(menu);
    raise_sound_layer_action(view, action, WORM_UI_COMPOSE_ACTION_OPEN_SELECTED_SOUND_LAYER_MENU);
}

void render_sound_layer_row(nk_context* context, const wormedit::ui::compose_view_t& view,
    wormedit::ui::workspace_action_t& action, std::size_t index) {
    if (index < view.note_menu_layer_first_row ||
        index - view.note_menu_layer_first_row >= view.note_menu_layers.size() || view.note_menu_layers.empty()) {
        nk_layout_row_dynamic(context, chrome::context_menu_row_height, 1);
        nk_label(context, "Loading layers…", NK_TEXT_LEFT);
        chrome::context_menu_placeholder(menu);
        return;
    }
    const auto& layer = view.note_menu_layers[index - view.note_menu_layer_first_row];
    if (!chrome::context_menu_item(menu, context, layer.name.data(),
                                   layer.locked ? "Layer is locked" : nullptr)) return;
    raise_sound_layer_action(view, action, WORM_UI_COMPOSE_ACTION_ADD_SELECTED_SOUNDS_TO_LAYER);
    action.compose_semantic_layer_id.value = layer.layer_id.value;
    consumed = true;
}

void render_sound_layers(nk_context* context, const wormedit::ui::compose_view_t& view,
                          wormedit::ui::workspace_action_t& action) {
    nk_layout_row_dynamic(context, chrome::context_menu_row_height, 1);
    auto back_style = context->style.contextual_button;
    back_style.text_alignment = NK_TEXT_LEFT;
    const auto back = nk_button_label_styled(context, &back_style, "‹ Area actions");
    if (back || chrome::context_menu_take_back(menu)) {
        sound_layers = false;
        chrome::context_menu_reset_navigation(menu);
        raise_sound_layer_action(view, action, WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU);
        return;
    }
    nk_layout_row_dynamic(context, chrome::context_menu_row_height, 1);
    nk_label(context, "Add selected sounds to layer", NK_TEXT_LEFT);
    const auto available = nk_window_get_content_region(context).h -
                           2 * (chrome::context_menu_row_height + context->style.window.spacing.y);
    nk_layout_row_dynamic(context, std::max(chrome::context_menu_row_height, available), 1);
    nk_list_view list{};
    const auto total = std::min<std::size_t>(view.note_menu_layer_total_count, 100'000);
    chrome::context_menu_set_row_count(menu, total);
    for (std::size_t offset = 0; offset < view.note_menu_layers.size(); ++offset)
        chrome::context_menu_set_row_enabled(menu, view.note_menu_layer_first_row + offset,
                                             !view.note_menu_layers[offset].locked);
    chrome::context_menu_scroll_to_selected(menu, context, "Selected sound layers",
                                            chrome::context_menu_row_height, available);
    if (!nk_list_view_begin(context, &list, "Selected sound layers", NK_WINDOW_BORDER,
                            static_cast<int>(chrome::context_menu_row_height), static_cast<int>(total))) return;
    chrome::context_menu_set_row_cursor(menu, list.begin);
    for (int index = list.begin; index < list.end; ++index)
        render_sound_layer_row(context, view, action, static_cast<std::size_t>(index));
    const auto first = static_cast<std::size_t>(list.begin);
    const auto requested = std::min<std::size_t>(wormedit::ui::note_layer_window_limit,
                                                static_cast<std::size_t>(list.end - list.begin));
    nk_list_view_end(&list);
    if (menu.open && total &&
        (first != view.note_menu_layer_first_row || requested > view.note_menu_layers.size())) {
        raise_sound_layer_action(view, action, WORM_UI_COMPOSE_ACTION_SET_NOTE_LAYER_MENU_WINDOW);
        action.compose_semantic_layer_first_row = first;
        action.compose_semantic_layer_row_count = std::max<std::size_t>(1, requested);
    }
}

void render_menu(nk_context* context, const wormedit::ui::compose_view_t& view,
                 wormedit::ui::workspace_action_t& action) {
    if (!menu.open) {
        retire_sound_layer_window(action);
        return;
    }
    if (!view.area_range_available) {
        close_menu(context, action);
        return;
    }
    if (!chrome::context_menu_begin(menu, context, wormedit::ui::chrome::PopupContextStamp::from(view),
                                    nk_vec2(320.0f, chrome::context_menu_height(sound_layers ? 14 : 13)))) {
        retire_sound_layer_window(action);
        consumed = true;
        return;
    }
    if (sound_layers) {
        render_sound_layers(context, view, action);
        chrome::context_menu_end(menu, context);
        return;
    }
    constexpr const char* materialize = "Materialize the selected measures first";
    if (chrome::context_menu_item(menu, context, "Quantize selection",
            reason_unless(view.selected_note_count + view.selected_bgm_event_count != 0, "Select notes or BGM first"))) {
        emit_compose_menu_command(action, view, "edit.apply_quantize");
        consumed = true;
    }
    render_sound_layer_entry(context, view, action);
    if (chrome::context_menu_item(menu, context, "Export preview audio…",
            reason_unless(view.preview_audio_range.available, "Select a nonempty materialized audio interval"))) {
        worm_ui_nuklear_compose_state().preview_audio.begin(view);
        consumed = true;
    }
    menu_entry(context, "Create section",
               reason_unless(view.beat_section_selection_span_available, materialize),
               WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION, action);
    const bool can_loop = view.transport_loop_selection_available && !view.area_range_virtual &&
        view.area_range_begin_row < view.area_range_end_row;
    if (chrome::context_menu_item(menu, context, "Loop playback",
            reason_unless(can_loop, view.area_range_virtual ? materialize :
                          "Selection looping requires audio output and a nonempty area"))) {
        emit_compose_menu_command(action, view, "transport.loop_selection");
        consumed = true;
    }
    menu_entry(context, "Delete contents",
               reason_unless(view.area_delete_supported,
                             view.area_range_virtual ? materialize
                                                     : "Area deletion supports notes and BGM only"),
               WORM_UI_COMPOSE_ACTION_AREA_DELETE, action);
    menu_entry(context, "Add SV recipe", reason_unless(!view.area_range_virtual, materialize),
               WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE, action);
    menu_entry(context, "Add SPEEDS span", reason_unless(!view.area_range_virtual, materialize),
               WORM_UI_COMPOSE_ACTION_AREA_ADD_SPEED_SPAN, action);
    menu_entry(context, "Find fitting snap",
               reason_unless(view.selected_note_count + view.selected_bgm_event_count != 0, "Select notes or BGM first"),
               WORM_UI_COMPOSE_ACTION_FIND_FITTING_SNAP, action);
    const auto conversion_reason = view.waveform_timing_locked ? "Timing is locked"
        : view.area_range_virtual ? materialize
        : view.semantic_layer_gesture_active ? "Finish the current edit first" : nullptr;
    menu_entry(context, "Convert to Stop", conversion_reason,
               WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_STOP, action);
    menu_entry(context, "Convert to Delay", conversion_reason,
               WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_DELAY, action);
    menu_entry(context, "Convert to Stop · shorten measures", conversion_reason,
               WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_STOP_RESIZE, action);
    menu_entry(context, "Convert to Delay · shorten measures", conversion_reason,
               WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_DELAY_RESIZE, action);
    chrome::context_menu_end(menu, context);
}

void update_capture(nk_context* context, const wormedit::ui::compose_view_t& view,
                    wormedit::ui::workspace_action_t& action, struct nk_rect viewport, bool focused) {
    const auto& mouse = context->input.mouse;
    if (!capture.current(view)) {
        capture.active = false;
        return;
    }
    if (!focused || (!mouse.buttons[NK_BUTTON_LEFT].down &&
                     !nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT))) {
        capture.restore(action);
        capture.active = false;
        return;
    }
    const auto y = (mouse.pos.y - viewport.y) * view.viewport_height_pixels / viewport.h;
    const auto source_y = wormedit::sys::compose_source_y(y, view.viewport_height_pixels, view.downscroll);
    const auto row = std::clamp((source_y + view.area_scroll_pixels) *
                                    static_cast<double>(view.area_rows_per_measure) /
                                    view.pixels_per_standard_measure,
                                0.0, static_cast<double>(view.area_canvas_rows - 1));
    action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET;
    action.compose_area_begin_row =
        capture.begin_edge ? static_cast<std::uint64_t>(std::llround(row)) : capture.begin;
    action.compose_area_end_row =
        capture.begin_edge ? capture.end : static_cast<std::uint64_t>(std::llround(row));
    action.compose_area_virtual = capture.virtual_range;
    action.compose_area_begin_edge = capture.begin_edge;
    if (nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT))
        capture.active = false;
}
} // namespace

struct nk_rect compose_area_band_rect(const wormedit::ui::compose_view_t& view, struct nk_rect viewport) {
    const auto scale = viewport.h / static_cast<float>(view.viewport_height_pixels);
    const auto a =
        viewport.y + static_cast<float>(compose_area_y(view, view.area_range_begin_row)) * scale;
    const auto b =
        viewport.y + static_cast<float>(compose_area_y(view, view.area_range_end_row)) * scale;
    const auto left =
        viewport.x + 42.0f * viewport.w / static_cast<float>(view.viewport_width_pixels);
    return nk_rect(left, std::min(a, b), viewport.x + viewport.w - left, std::abs(b - a));
}


auto compose_area_pointer_consumed() -> bool {
    return consumed || chrome::context_menu_owns_input(menu);
}
auto compose_area_menu_open() -> bool {
    return menu.open;
}
auto compose_area_menu_consumed() -> bool {
    return consumed || chrome::context_menu_owns_input(menu);
}
auto compose_area_band_visible() -> bool {
    return band_visible;
}
auto compose_area_capture_active() -> bool {
    return capture.active || chrome::context_menu_owns_input(menu);
}
void reset_compose_area_menu() {
    capture = {};
    chrome::context_menu_reset(menu);
    consumed = false;
    band_visible = false;
    sound_layers = false;
}

void cancel_compose_area_capture(wormedit::ui::workspace_action_t& action) {
    if (capture.active)
        capture.restore(action);
    else if (sound_layers)
        retire_sound_layer_window(action);
    else
        action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_CLEAR;
    capture.active = false;
    chrome::context_menu_close(menu);
    consumed = true;
}

void process_compose_area_menu(nk_context* context, const wormedit::ui::compose_view_t& view,
                               wormedit::ui::workspace_action_t& action, struct nk_rect viewport,
                               double input_right, bool focused) {
    (void)chrome::context_menu_update(menu, context);
    if (compose_note_menu_consumed()) { consumed = false; return; }
    band_visible = view.scene_available && view.area_range_available;
    consumed = capture.active || menu.open;
    if (!view.scene_available || view.area_rows_per_measure == 0 ||
        view.viewport_height_pixels <= 0 || view.viewport_width_pixels <= 0 ||
        !std::isfinite(view.pixels_per_standard_measure) || view.pixels_per_standard_measure <= 0 ||
        view.area_canvas_rows == 0) {
        capture.active = false;
        close_menu(context, action);
        consumed = false;
        return;
    }
    if (!focused && menu.open)
        close_menu(context, action);
    if (capture.active) {
        update_capture(context, view, action, viewport, focused);
        return;
    }
    if (!focused || !view.area_range_available || view.area_canvas_rows == 0) {
        render_menu(context, view, action); // Closes an open menu whose range went away.
        return;
    }
    const auto rect = compose_area_band_rect(view, viewport);
    const auto& mouse = context->input.mouse;
    const bool inside =
        nk_input_is_mouse_hovering_rect(&context->input, rect) && mouse.pos.x < input_right;
    if (chrome::context_menu_take_open_request(chrome::KeyboardContextTarget::compose)) {
        const auto x = std::clamp(rect.x + rect.w * 0.5f, viewport.x, std::max(viewport.x, static_cast<float>(input_right)));
        const auto y = std::clamp(rect.y + rect.h * 0.5f, viewport.y, viewport.y + viewport.h);
        sound_layers = false;
        chrome::context_menu_open(menu, nk_vec2(x, y), wormedit::ui::chrome::PopupContextStamp::from(view), true);
        consumed = true;
    }
    if (worm_ui_nuklear_compose_context_click(context, view) && inside) {
        sound_layers = false;
        chrome::context_menu_open(menu, mouse.pos, wormedit::ui::chrome::PopupContextStamp::from(view));
        consumed = true;
    }
    if (!menu.open && nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT)) {
        const auto a = viewport.y + compose_area_y(view, view.area_range_begin_row) * viewport.h /
                                        view.viewport_height_pixels;
        const auto b = viewport.y + compose_area_y(view, view.area_range_end_row) * viewport.h /
                                        view.viewport_height_pixels;
        const bool horizontal = mouse.pos.x >= rect.x && mouse.pos.x < input_right;
        if (horizontal && (std::abs(mouse.pos.y - a) <= 5 || std::abs(mouse.pos.y - b) <= 5)) {
            capture.start(view, std::abs(mouse.pos.y - a) <= std::abs(mouse.pos.y - b));
            consumed = true;
        }
    }
    render_menu(context, view, action);
}

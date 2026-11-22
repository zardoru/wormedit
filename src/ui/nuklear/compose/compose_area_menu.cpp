#include "ui/nuklear/compose/compose_menu_command.h"
#include "sys/compose/compose_scroll_geometry.h"
#include "ui/nuklear/compose/compose_area_menu.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/compose/compose_note_menu.h"
#include "ui/nuklear/chrome/context_menu.h"
#include "wormedit/nuklear_config.h"
#include "ui/nuklear/compose/compose_state.h"
#include "ui/nuklear/chrome/scroll_group.h"
#include <cstdio>
#include <wormedit/core/bpm_editor.h>

namespace {
namespace chrome = wormedit::ui::chrome;

ComposeAreaCapture capture;
bool consumed{};
bool band_visible{};
bool sound_layers{};
bool warp_timing{};
bool warp_resize_span{true};
int warp_numerator{};
int warp_denominator{};
chrome::ContextMenu menu{.title = "Area actions"};


void retire_sound_layer_window(wormedit::ui::compose_ui_window_t& action) {
    if (!sound_layers) return;
    if (action.compose.kind == WORM_UI_COMPOSE_ACTION_NONE) {
        action.compose.kind = WORM_UI_COMPOSE_ACTION_CLOSE_NOTE_LAYER_MENU;
        action.stamp = menu.stamp.request_stamp();
    }
    sound_layers = false;
}

void close_menu(nk_context* context, wormedit::ui::compose_ui_window_t& action) {
    chrome::context_menu_close(menu, context);
    retire_sound_layer_window(action);
    warp_timing = false;
    consumed = true;
}

void menu_entry(nk_context* context, const char* title, const char* disabled_reason,
                WormUiComposeActionKind kind, wormedit::ui::compose_ui_window_t& action) {
    if (chrome::context_menu_item(menu, context, title, disabled_reason)) {
        action.compose.kind = kind;
        action.stamp = menu.stamp.request_stamp();
        consumed = true;
    }
}

auto reason_unless(bool enabled, const char* reason) -> const char* {
    return enabled ? nullptr : reason;
}

void raise_sound_layer_action(const wormedit::ui::compose_view_t& view, wormedit::ui::compose_ui_window_t& action,
                             WormUiComposeActionKind kind) {
    if (action.compose.kind != WORM_UI_COMPOSE_ACTION_NONE) return;
    action.compose.kind = kind;
    action.stamp = wormedit::ui::request_stamp_t::of(view);
}

void render_sound_layer_entry(nk_context* context, const wormedit::ui::compose_view_t& view,
                              wormedit::ui::compose_ui_window_t& action) {
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
    wormedit::ui::compose_ui_window_t& action, std::size_t index) {
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
    action.compose.layer.id.value = layer.layer_id.value;
    consumed = true;
}

void render_sound_layers(nk_context* context, const wormedit::ui::compose_view_t& view,
                          wormedit::ui::compose_ui_window_t& action) {
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
    if (!wormedit::ui::chrome::scroll_list_view_begin(context, &list, "Selected sound layers", NK_WINDOW_BORDER,
                            static_cast<int>(chrome::context_menu_row_height), static_cast<int>(total))) return;
    chrome::context_menu_set_row_cursor(menu, list.begin);
    for (int index = list.begin; index < list.end; ++index)
        render_sound_layer_row(context, view, action, static_cast<std::size_t>(index));
    const auto first = static_cast<std::size_t>(list.begin);
    const auto requested = std::min<std::size_t>(wormedit::ui::note_layer_window_limit,
                                                static_cast<std::size_t>(list.end - list.begin));
    wormedit::ui::chrome::scroll_list_view_end(&list);
    if (menu.open && total &&
        (first != view.note_menu_layer_first_row || requested > view.note_menu_layers.size())) {
        raise_sound_layer_action(view, action, WORM_UI_COMPOSE_ACTION_SET_NOTE_LAYER_MENU_WINDOW);
        action.compose.layer.first_row = first;
        action.compose.layer.row_count = std::max<std::size_t>(1, requested);
    }
}

void render_warp_timing(nk_context* context, const wormedit::ui::compose_view_t& view,
                        wormedit::ui::compose_ui_window_t& action) {
    nk_layout_row_dynamic(context, chrome::context_menu_row_height, 1);
    if (chrome::context_menu_button(context, "‹ Area actions") ||
        chrome::context_menu_take_back(menu)) {
        warp_timing = false;
        chrome::context_menu_reset_navigation(menu);
        return;
    }
    constexpr auto factors = wormedit::bpm_range_warp_factors;
    constexpr int count = static_cast<int>(factors.size());
    std::array<std::array<char, 8>, count> text{};
    std::array<const char*, count> labels{};
    for (int i = 0; i < count; ++i) {
        std::snprintf(text[i].data(), text[i].size(), "%llu", static_cast<unsigned long long>(factors[i]));
        labels[i] = text[i].data();
    }
    nk_layout_row_dynamic(context, wormedit::ui::metrics::row_text, 1);
    nk_label(context, "BPM multiplier numerator", NK_TEXT_LEFT);
    if (const auto i = chrome::ChipGrid{labels.data(), count, warp_numerator, 8}.render(context).chosen; i >= 0)
        warp_numerator = i;
    nk_layout_row_dynamic(context, wormedit::ui::metrics::row_text, 1);
    nk_label(context, "BPM multiplier denominator", NK_TEXT_LEFT);
    if (const auto i = chrome::ChipGrid{labels.data(), count, warp_denominator, 8}.render(context).chosen; i >= 0)
        warp_denominator = i;
    nk_layout_row_dynamic(context, wormedit::ui::metrics::row_text, 1);
    const auto resize_state = warp_resize_span ? chrome::CheckState::on : chrome::CheckState::off;
    if (chrome::Checkbox{"Resize span to preserve audio time", resize_state}.render(context))
        warp_resize_span = !warp_resize_span;
    const auto numerator = factors[warp_numerator];
    const auto denominator = factors[warp_denominator];
    const bool available = view.area_bpm_warp_moving_seconds.has_value() && !view.waveform_timing_locked;
    if (available) {
        const auto seconds = *view.area_bpm_warp_moving_seconds;
        char preview[128];
        const auto resulting_seconds = warp_resize_span ? seconds :
            seconds * static_cast<double>(denominator) / static_cast<double>(numerator);
        std::snprintf(preview, sizeof(preview), "×%llu/%llu · %.3f s → %.3f s",
            static_cast<unsigned long long>(numerator), static_cast<unsigned long long>(denominator),
            seconds, resulting_seconds);
        nk_layout_row_dynamic(context, wormedit::ui::metrics::row_text, 1);
        nk_label(context, preview, NK_TEXT_LEFT);
    }
    if (!chrome::context_menu_item(menu, context, "Apply timing warp",
            !available ? "Select a range within one authored measure" :
            numerator == denominator ? "Choose a multiplier other than 1" : nullptr)) return;
    action.compose.kind = WORM_UI_COMPOSE_ACTION_AREA_WARP_TIMING;
    action.compose.area.begin_row = view.area_range_begin_row;
    action.compose.area.end_row = view.area_range_end_row;
    action.compose.area.bpm_multiplier_numerator = numerator;
    action.compose.area.bpm_multiplier_denominator = denominator;
    action.compose.area.bpm_warp_resize_span = warp_resize_span;
    action.stamp = menu.stamp.request_stamp();
    consumed = true;
}

void render_menu(nk_context* context, const wormedit::ui::compose_view_t& view,
                 wormedit::ui::compose_ui_window_t& action) {
    if (!menu.open) {
        retire_sound_layer_window(action);
        return;
    }
    if (!view.area_range_available) {
        close_menu(context, action);
        return;
    }
    if (!chrome::context_menu_begin(menu, context, wormedit::ui::chrome::PopupContextStamp::from(view),
                                    nk_vec2(320.0f, chrome::context_menu_height(warp_timing ? 11 : 16)))) {
        retire_sound_layer_window(action);
        consumed = true;
        return;
    }
    if (sound_layers) {
        render_sound_layers(context, view, action);
        chrome::context_menu_end(menu, context);
        return;
    }
    if (warp_timing) {
        render_warp_timing(context, view, action);
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
    menu_entry(context, "Set song preview from selection",
               reason_unless(view.preview_audio_range.available, "Select a nonempty materialized audio interval"),
               WORM_UI_COMPOSE_ACTION_SET_PREVIEW_FROM_SELECTION, action);
    menu_entry(context, "Create section",
               reason_unless(view.beat_section_selection_span_available, materialize),
               WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION, action);
    menu_entry(context, "Create export cut",
               reason_unless(view.preview_audio_range.available, "Select a nonempty materialized audio interval"),
               WORM_UI_COMPOSE_ACTION_ADD_EXPORT_CUT_FROM_SELECTION, action);
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
    if (action.compose.kind == WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE) {
        worm_ui_nuklear_compose_state().dock_layout.layout.activate(wormedit::dock_panel_t::inspector, false);
        nk_group_set_scroll(context, "Compose inspector", 0, 0);
    }
    menu_entry(context, "Add SPEEDS span", reason_unless(!view.area_range_virtual, materialize),
               WORM_UI_COMPOSE_ACTION_AREA_ADD_SPEED_SPAN, action);
    menu_entry(context, "Find fitting snap",
               reason_unless(view.selected_note_count + view.selected_bgm_event_count != 0, "Select notes or BGM first"),
               WORM_UI_COMPOSE_ACTION_FIND_FITTING_SNAP, action);
    const auto conversion_reason = view.waveform_timing_locked ? "Timing is locked"
        : view.area_range_virtual ? materialize
        : view.semantic_layer_gesture_active ? "Finish the current edit first" : nullptr;
    nk_layout_row_dynamic(context, chrome::context_menu_row_height, 1);
    if (chrome::context_menu_button(context, "Warp timing…",
            conversion_reason ? conversion_reason : reason_unless(view.area_bpm_warp_moving_seconds.has_value(),
                "Select a range within one authored measure"), true)) {
        warp_timing = true;
        warp_numerator = warp_denominator = 0;
        chrome::context_menu_reset_navigation(menu);
    }
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
                    wormedit::ui::compose_ui_window_t& action, struct nk_rect viewport, bool focused) {
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
    const wormedit::editor_axis_projection_t axis{.cut_axis = view.cut_axis.get(),
        .pixels_per_row = view.pixels_per_standard_measure / static_cast<double>(view.area_rows_per_measure),
        .viewport_scroll_pixels = view.area_scroll_pixels};
    const auto row = std::clamp(axis.row_at_y(source_y), 0.0L,
        static_cast<long double>(view.area_canvas_rows - 1));
    action.compose.kind = WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET;
    action.compose.area.begin_row =
        capture.begin_edge ? static_cast<std::uint64_t>(std::llround(row)) : capture.begin;
    action.compose.area.end_row =
        capture.begin_edge ? capture.end : static_cast<std::uint64_t>(std::llround(row));
    action.compose.area.is_virtual = capture.virtual_range;
    action.compose.area.begin_edge = capture.begin_edge;
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

void cancel_compose_area_capture(wormedit::ui::compose_ui_window_t& action) {
    if (capture.active)
        capture.restore(action);
    else if (sound_layers)
        retire_sound_layer_window(action);
    else
        action.compose.kind = WORM_UI_COMPOSE_ACTION_AREA_RANGE_CLEAR;
    capture.active = false;
    chrome::context_menu_close(menu);
    consumed = true;
}

void process_compose_area_menu(nk_context* context, const wormedit::ui::compose_view_t& view,
                               wormedit::ui::compose_ui_window_t& action, struct nk_rect viewport,
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
        warp_timing = false;
        chrome::context_menu_open(menu, nk_vec2(x, y), wormedit::ui::chrome::PopupContextStamp::from(view), true);
        consumed = true;
    }
    if (worm_ui_nuklear_compose_context_click(context, view) && inside) {
        sound_layers = false;
        warp_timing = false;
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

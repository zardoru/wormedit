#include "ui/nuklear/chrome/color.h"
#include "ui/nuklear/chrome/content_layout.h"
#include "ui/nuklear/compose/compose_layers_panel.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/compose/compose_action_stamp.h"
#include "ui/nuklear/render/lucide_icons.h"
#include "ui/nuklear/chrome/popup_lifecycle.h"
#include "ui/nuklear/render/theme.h"
#include "ui/nuklear/chrome/tooltip.h"
#include "ui/nuklear/render/vector_icons.h"

#include "wormedit/nuklear_config.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>

namespace {

using wormedit::ui::chrome::color;
using wormedit::ui::chrome::IconButton;
using wormedit::ui::lucide::Glyph;
using wormedit::ui::theme::UiColor;
constexpr float row_height = 38.0f;

struct LayersPanel {
    nk_context* context;
    const wormedit::ui::compose_view_t* view;
    wormedit::ui::workspace_action_t* action;
    worm_ui_nuklear_compose_state_t* state;
};

auto raise(const LayersPanel& panel, WormUiComposeActionKind kind, std::uint64_t id = 0) -> bool {
    if (!wormedit::ui::raise_compose_action(*panel.action, *panel.view, kind))
        return false;
    panel.action->compose_semantic_layer_id.value = id;
    return true;
}

auto layer_menu_blocks_row_input(const LayersPanel& panel) -> bool {
    return wormedit::ui::chrome::context_menu_owns_input(panel.state->layer_editor.layers_menu) ||
           panel.state->layer_editor.layers_menu_input_consumed;
}

struct LayerMenuEntry {
    const char* label;
    WormUiComposeActionKind kind;
    bool needs_unlocked;
    bool needs_idle;
};

constexpr LayerMenuEntry layer_menu_entries[]{
    {"Rename…", WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER, true, true},
    {"Color…", WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR, true, true},
    {"", WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY, false, true},
    {"Solo", WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO, false, false},
    {"", WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK, false, true},
    {"Select notes", WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER_NOTES, false, true},
    {"Add/remove selected notes", WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER, true, true},
    {"Stop targeting this layer", WORM_UI_COMPOSE_ACTION_CLEAR_SEMANTIC_LAYER, false, false},
    {"Require filter", WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER, true, false},
    {"Exclude filter", WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER, true, false},
    {"Move up", WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP, true, true},
    {"Move down", WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN, true, true},
    {"Delete", WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER, true, true},
    {"Delete + unlink", WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS, true, true},
};

auto captured_layer(const LayersPanel& panel) -> const wormedit::ui::compose_semantic_layer_view_t* {
    auto& state = *panel.state;
    if (state.layer_editor.layers_menu_layer.layer_id.value == 0) return nullptr;
    state.layer_editor.layers_menu_layer.name = state.layer_editor.layers_menu_layer_name.c_str();
    return &state.layer_editor.layers_menu_layer;
}


auto layer_capture_current(const LayersPanel& panel) -> bool {
    return wormedit::ui::chrome::popup_context_matches(panel.state->layer_editor.layers_menu.stamp,
                                                       wormedit::ui::chrome::PopupContextStamp::from(*panel.view, panel.state->layer_editor.layers_menu.stamp.object_id));
}

void draw_layer_row_text(const LayersPanel& panel,
                         const wormedit::ui::compose_semantic_layer_view_t& layer,
                         struct nk_rect bounds) {
    auto* context = panel.context;
    auto* canvas = nk_window_get_canvas(context);
    const auto text_color = color(layer.selected ? UiColor::selected_text : UiColor::text);
    char label[256];
    std::snprintf(label, sizeof(label), "%s", layer.name.data());
    nk_draw_text(canvas, nk_rect(bounds.x + 20, bounds.y + 3,
                                 std::max(1.0f, bounds.w - 24), 15), label,
                 static_cast<int>(std::strlen(label)), context->style.font,
                 nk_rgba(0, 0, 0, 0), text_color);
    std::snprintf(label, sizeof(label), "%zu objects", layer.assigned_note_count + layer.assigned_bgm_count);
    nk_draw_text(canvas, nk_rect(bounds.x + 20, bounds.y + 20,
                                 std::max(1.0f, bounds.w - 24), 15), label,
                 static_cast<int>(std::strlen(label)), context->style.font,
                 nk_rgba(0, 0, 0, 0), color(UiColor::text_muted));
}

void open_layer_menu(const LayersPanel& panel, const wormedit::ui::compose_semantic_layer_view_t& layer,
                      std::size_t index, struct nk_rect bounds) {
    auto* context = panel.context;
    if (layer.layer_id.value == 0 || layer_menu_blocks_row_input(panel) ||
        panel.view->input_occluded || !panel.view->scene_available ||
        wormedit::ui::chrome::popup_panel(context)) return;
    const bool keyboard_open = layer.layer_id.value == wormedit::ui::chrome::context_menu_focused_object() &&
        wormedit::ui::chrome::context_menu_take_open_request(wormedit::ui::chrome::KeyboardContextTarget::layers);
    const bool pointer_open = nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT) &&
        nk_input_is_mouse_hovering_rect(&context->input, bounds);
    if (!keyboard_open && !pointer_open) return;
    if (!raise(panel, WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER, layer.layer_id.value)) return;
    auto& state = *panel.state;
    wormedit::ui::chrome::context_menu_open(
        state.layer_editor.layers_menu, keyboard_open ? nk_vec2(bounds.x, bounds.y + bounds.h) : context->input.mouse.pos,
        {panel.view->project_id.value, panel.view->chart_id.value, panel.view->document_revision,
         layer.layer_id.value}, keyboard_open);
    state.layer_editor.layers_menu_input_consumed = true;
    state.layer_editor.layers_menu_layer = layer;
    state.layer_editor.layers_menu_layer_name = layer.name.data();
    state.layer_editor.layers_menu_row_index = index;
    state.layer_editor.semantic_layer_name_editor_visible = false;
    state.layer_editor.semantic_layer_color_editor_visible = false;
}

auto layer_entry_reason(const LayersPanel& panel, const wormedit::ui::compose_semantic_layer_view_t& layer,
                        const LayerMenuEntry& entry) -> const char* {
    if (!panel.view->scene_available) return "Open a Compose chart";
    if (entry.needs_idle && panel.view->semantic_layer_gesture_active)
        return "Finish or cancel the active Compose gesture";
    if (entry.needs_unlocked && layer.locked) return "Layer is locked";
    if (entry.kind == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER &&
        panel.view->selected_note_count == 0 && panel.view->selected_bgm_event_count == 0)
        return "Select notes or BGM events first";
    if (entry.kind == WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER &&
        (layer.assigned_note_count || layer.assigned_bgm_count))
        return "Layer has assigned objects; use Delete + unlink";
    if (entry.kind == WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP &&
        panel.state->layer_editor.layers_menu_row_index == 0) return "Layer is already first";
    if (entry.kind == WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN &&
        panel.state->layer_editor.layers_menu_row_index + 1 >= panel.view->semantic_layer_total_count)
        return "Layer is already last";
    return nullptr;
}

auto raise_captured(const LayersPanel& panel, WormUiComposeActionKind kind) -> bool {
    if (!layer_capture_current(panel) ||
        !raise(panel, kind, panel.state->layer_editor.layers_menu.stamp.object_id)) return false;
    panel.action->compose_input_chart_id.value = panel.state->layer_editor.layers_menu.stamp.chart_id;
    panel.action->compose_input_document_revision = panel.state->layer_editor.layers_menu.stamp.revision;
    return true;
}

void start_layer_editor(const LayersPanel& panel, const wormedit::ui::compose_semantic_layer_view_t& layer,
                         WormUiComposeActionKind kind) {
    auto& state = *panel.state;
    if (kind == WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER) {
        state.layer_editor.semantic_layer_name.fill('\0');
        std::snprintf(state.layer_editor.semantic_layer_name.data(), state.layer_editor.semantic_layer_name.size(), "%s", layer.name.data());
        state.layer_editor.semantic_layer_name_editor_visible = true;
        state.layer_editor.semantic_layer_name_focus_pending = true;
    } else {
        const auto value = nk_color_cf(nk_rgba(layer.red, layer.green, layer.blue, layer.alpha));
        state.layer_editor.semantic_layer_color = {value.r, value.g, value.b, value.a};
        state.layer_editor.semantic_layer_color_editor_visible = true;
    }
}

auto layer_entry_label(const LayersPanel& panel, const wormedit::ui::compose_semantic_layer_view_t& layer,
                       const LayerMenuEntry& entry) -> const char* {
    if (entry.kind == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY)
        return layer.visible ? "Hide" : "Show";
    if (entry.kind == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK)
        return layer.locked ? "Unlock" : "Lock";
    if (entry.kind == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO &&
        panel.view->semantic_layer_target.layer_id.value == layer.layer_id.value &&
        panel.view->semantic_layer_solo_active) return "Solo: On";
    return entry.label;
}

void render_layer_menu_entry(const LayersPanel& panel, const wormedit::ui::compose_semantic_layer_view_t& layer,
                             const LayerMenuEntry& entry) {
    if (!wormedit::ui::chrome::context_menu_item(panel.state->layer_editor.layers_menu, panel.context,
                                                 layer_entry_label(panel, layer, entry),
                                                 layer_entry_reason(panel, layer, entry)))
        return;
    if (entry.kind == WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER ||
        entry.kind == WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR)
        start_layer_editor(panel, layer, entry.kind);
    else
        (void)raise_captured(panel, entry.kind);
}

void render_layer_menu(const LayersPanel& panel) {
    auto* context = panel.context;
    auto& menu = panel.state->layer_editor.layers_menu;
    if (!menu.open || wormedit::ui::chrome::popup_panel(context)) return;
    const auto* layer = captured_layer(panel);
    if (!layer) {
        wormedit::ui::chrome::context_menu_close(menu, context);
        return;
    }
    const auto rows = static_cast<float>(std::size(layer_menu_entries));
    if (!wormedit::ui::chrome::context_menu_begin(
            menu, context, wormedit::ui::chrome::PopupContextStamp::from(*panel.view, panel.state->layer_editor.layers_menu.stamp.object_id),
            nk_vec2(wormedit::ui::chrome::context_menu_width,
                    wormedit::ui::chrome::context_menu_height(rows))))
        return;
    for (const auto& entry : layer_menu_entries) render_layer_menu_entry(panel, *layer, entry);
    wormedit::ui::chrome::context_menu_end(menu, context);
}

void close_layer_editors(worm_ui_nuklear_compose_state_t& state) {
    state.layer_editor.semantic_layer_name_editor_visible = false;
    state.layer_editor.semantic_layer_name_focus_pending = false;
    state.layer_editor.semantic_layer_color_editor_visible = false;
}

auto layer_editor_allowed(const LayersPanel& panel, const wormedit::ui::compose_semantic_layer_view_t& layer)
    -> bool {
    return panel.view->scene_available && !layer.locked && !panel.view->semantic_layer_gesture_active;
}

void render_layer_name_editor(const LayersPanel& panel, const wormedit::ui::compose_semantic_layer_view_t& layer) {
    auto* context = panel.context;
    auto& state = *panel.state;
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Rename layer", NK_TEXT_LEFT);
    const auto allowed = layer_editor_allowed(panel, layer);
    if (!allowed) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 24, 1);
    (void)wormedit::ui::chrome::autofocus_text_field(context, static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_AUTO_SELECT,
        state.layer_editor.semantic_layer_name.data(), static_cast<int>(state.layer_editor.semantic_layer_name.size()),
        nk_filter_default, state.layer_editor.semantic_layer_name_focus_pending);
    if (!allowed) nk_widget_disable_end(context);
    nk_layout_row_dynamic(context, 24, 2);
    const auto valid = state.layer_editor.semantic_layer_name[0] != '\0';
    if (!allowed || !valid) nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, "Apply");
    if (!allowed || !valid) nk_widget_disable_end(context);
    const auto cancel = nk_button_label(context, "Cancel");
    if (apply && allowed && valid && raise_captured(panel, WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER)) {
        panel.action->compose_semantic_layer_name = state.layer_editor.semantic_layer_name.data();
        close_layer_editors(state);
    } else if (cancel) close_layer_editors(state);
}


void render_layer_color_editor(const LayersPanel& panel, const wormedit::ui::compose_semantic_layer_view_t& layer) {
    auto* context = panel.context;
    auto& state = *panel.state;
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Layer color", NK_TEXT_LEFT);
    const auto allowed = layer_editor_allowed(panel, layer);
    if (!allowed) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 110, 1);
    const auto value = nk_color_picker(context, state.layer_editor.semantic_layer_color, NK_RGBA);
    state.layer_editor.semantic_layer_color = {value.r, value.g, value.b, value.a};
    nk_layout_row_dynamic(context, 24, 2);
    const auto apply = nk_button_label(context, "Apply");
    if (!allowed) nk_widget_disable_end(context);
    const auto cancel = nk_button_label(context, "Cancel");
    if (apply && allowed && raise_captured(panel, WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR)) {
        panel.action->compose_semantic_layer_red = wormedit::ui::chrome::unit_to_byte(value.r);
        panel.action->compose_semantic_layer_green = wormedit::ui::chrome::unit_to_byte(value.g);
        panel.action->compose_semantic_layer_blue = wormedit::ui::chrome::unit_to_byte(value.b);
        panel.action->compose_semantic_layer_alpha = wormedit::ui::chrome::unit_to_byte(value.a);
        close_layer_editors(state);
    } else if (cancel) close_layer_editors(state);
}

void render_layer_editors(const LayersPanel& panel) {
    auto& state = *panel.state;
    if (!state.layer_editor.semantic_layer_name_editor_visible && !state.layer_editor.semantic_layer_color_editor_visible) return;
    const auto* layer = captured_layer(panel);
    if (!layer_capture_current(panel) || !layer ||
        nk_input_is_key_pressed(&panel.context->input, NK_KEY_TEXT_RESET_MODE)) {
        close_layer_editors(state);
        return;
    }
    if (state.layer_editor.semantic_layer_name_editor_visible) render_layer_name_editor(panel, *layer);
    if (state.layer_editor.semantic_layer_color_editor_visible) render_layer_color_editor(panel, *layer);
}

void render_header(const LayersPanel& panel) {
    auto* context = panel.context;
    nk_layout_row_begin(context, NK_DYNAMIC, 26, 2);
    nk_layout_row_push(context, 0.84f);
    nk_labelf(context, NK_TEXT_LEFT, "Layers  %zu", panel.view->semantic_layer_total_count);
    nk_layout_row_push(context, 0.16f);
    if (!panel.view->scene_available) nk_widget_disable_begin(context);
    if (wormedit::ui::chrome::DrawnIconButton{wormedit::ui::vector_icons::plus, "Add layer"}
            .render(context) && panel.view->scene_available)
        panel.state->layer_editor.layer_creation_request = {
            panel.view->project_id.value, panel.view->chart_id.value, panel.view->document_revision};
    if (!panel.view->scene_available) nk_widget_disable_end(context);
    nk_layout_row_end(context);
}

void render_row_name(const LayersPanel& panel, const wormedit::ui::compose_semantic_layer_view_t& layer,
                      std::size_t index) {
    auto* context = panel.context;
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT) &&
                         !panel.view->input_occluded && !layer_menu_blocks_row_input(panel);
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    if (clicked) {
        panel.state->input_queue.keyboard_focus = false;
        wormedit::ui::chrome::context_menu_focus(wormedit::ui::chrome::KeyboardContextTarget::layers, layer.layer_id.value);
    }
    open_layer_menu(panel, layer, index, bounds);
    auto* canvas = nk_window_get_canvas(context);
    if (layer.selected) nk_fill_rect(canvas, bounds, 3.0f, color(UiColor::selected));
    else if (hovered) nk_fill_rect(canvas, bounds, 3.0f, color(UiColor::surface_hover));
    nk_fill_circle(canvas, nk_rect(bounds.x + 5, bounds.y + (bounds.h - 8) * 0.5f, 8, 8),
                   nk_rgba(layer.red, layer.green, layer.blue, layer.alpha));
    draw_layer_row_text(panel, layer, bounds);
    if (hovered) wormedit::ui::nuklear::tooltip::text(context, layer.locked ? "Layer is locked; click to choose its target"
                              : !panel.view->semantic_layer_paint_mode_active &&
                                    (panel.view->selected_note_count || panel.view->selected_bgm_event_count)
                               ? "Assign selected notes and BGM events to this layer"
                               : layer.name.data());
    if (clicked && panel.view->scene_available && layer.layer_id.value == 0) {
        (void)raise(panel, WORM_UI_COMPOSE_ACTION_SELECT_NO_SEMANTIC_LAYER);
    } else if (clicked && panel.view->scene_available)
        (void)raise(panel, (panel.view->selected_note_count ||
                            panel.view->selected_bgm_event_count) && !layer.locked &&
                            !panel.view->semantic_layer_paint_mode_active
                              ? WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER
                              : WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER, layer.layer_id.value);
}

void render_row(const LayersPanel& panel, const wormedit::ui::compose_semantic_layer_view_t& layer,
                std::size_t index) {
    auto* context = panel.context;
    nk_layout_row_template_begin(context, row_height);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 24);
    nk_layout_row_template_push_static(context, 24);
    nk_layout_row_template_push_static(context, 24);
    nk_layout_row_template_end(context);
    render_row_name(panel, layer, index);
    const auto row_input_blocked = !panel.view->scene_available || panel.view->input_occluded ||
                                   layer_menu_blocks_row_input(panel);
    if (row_input_blocked) nk_widget_disable_begin(context);
    if (!panel.view->semantic_layer_assist_available) nk_widget_disable_begin(context);
    if (IconButton{Glyph::metronome, layer.assist_tick_enabled ? "Disable layer assist ticks"
                       : "Enable layer assist ticks", layer.assist_tick_enabled}.render(context))
        (void)raise(panel, WORM_UI_COMPOSE_ACTION_TOGGLE_LAYER_ASSIST_TICK, layer.layer_id.value);
    if (!panel.view->semantic_layer_assist_available) nk_widget_disable_end(context);
    if (IconButton{layer.visible ? Glyph::eye : Glyph::eye_off,
                   layer.visible ? "Hide layer" : "Show layer"}.render(context))
        (void)raise(panel, WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY, layer.layer_id.value);
    if (IconButton{layer.locked ? Glyph::lock : Glyph::lock_open,
                   layer.locked ? "Unlock layer" : "Lock layer"}.render(context))
        (void)raise(panel, WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK, layer.layer_id.value);
    if (row_input_blocked) nk_widget_disable_end(context);
}

void render_no_layer_row(const LayersPanel& panel) {
    const auto neutral = wormedit::ui::theme::compose(
        wormedit::ui::theme::ComposeColor::semantic_layer_none);
    const wormedit::ui::compose_semantic_layer_view_t layer{
        .layer_id = {0}, .name = "No layer", .red = neutral.r, .green = neutral.g,
        .blue = neutral.b, .alpha = neutral.a,
        .assigned_note_count = panel.view->semantic_layer_unassigned_note_count,
        .visible = true, .locked = false, .selected = panel.view->semantic_layer_target_none,
        .assigned_bgm_count = panel.view->semantic_layer_unassigned_bgm_count};
    nk_layout_row_dynamic(panel.context, row_height, 1);
    render_row_name(panel, layer, 0);
}

void render_list_row(const LayersPanel& panel, int index) {
    if (index == 0) { render_no_layer_row(panel); return; }
    const auto offset = static_cast<std::size_t>(index - 1);
    if (offset < panel.view->semantic_layer_first_row ||
        offset - panel.view->semantic_layer_first_row >= panel.view->semantic_layers.size()) {
        nk_layout_row_dynamic(panel.context, row_height, 1);
        nk_label(panel.context, "", NK_TEXT_LEFT);
        return;
    }
    render_row(panel, panel.view->semantic_layers[offset - panel.view->semantic_layer_first_row], offset);
}

void render_list(const LayersPanel& panel, float height) {
    auto* context = panel.context;
    const auto total = static_cast<int>(panel.view->semantic_layer_total_count) + 1;
    nk_layout_row_dynamic(context, height, 1);
    struct nk_list_view list{};
    if (!nk_list_view_begin(context, &list, "Compose layer list", NK_WINDOW_BORDER,
                            static_cast<int>(row_height), total)) return;
    for (int index = list.begin; index < std::min(list.end, total); ++index)
        render_list_row(panel, index);
    const auto first = static_cast<std::size_t>(std::max(0, list.begin - 1));
    const auto end = static_cast<std::size_t>(std::max(0, std::min(list.end, total) - 1));
    const auto count = std::max<std::size_t>(1, end - first);
    if (!panel.state->layer_editor.layers_menu.open && panel.view->semantic_layer_total_count &&
        (first != panel.view->semantic_layer_first_row || count != panel.view->semantic_layers.size()) &&
        raise(panel, WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_WINDOW)) {
        panel.action->compose_semantic_layer_first_row = first;
        panel.action->compose_semantic_layer_row_count = count;
    }
    nk_list_view_end(&list);
}

void reveal_added_layer(const LayersPanel& panel) {
    auto& state = *panel.state;
    const auto& view = *panel.view;
    if (view.semantic_layer_total_count > state.layer_editor.layers_panel_total_count &&
        view.semantic_layer_target.layer_id.value &&
        view.semantic_layer_target_row >= state.layer_editor.layers_panel_total_count) {
        state.pointer_capture.layer_chip_reveal_id = view.semantic_layer_target.layer_id.value;
        state.pointer_capture.chip_reveal_project = view.project_id.value;
        state.pointer_capture.chip_reveal_chart = view.chart_id.value;
    }
    state.layer_editor.layers_panel_total_count = view.semantic_layer_total_count;
}

} // namespace

void worm_ui_nuklear_dismiss_layer_popup(struct nk_context* context) {
    wormedit::ui::chrome::context_menu_retire(worm_ui_nuklear_compose_state().layer_editor.layers_menu, context);
}

void worm_ui_nuklear_render_compose_layers(struct nk_context* context,
                                           const wormedit::ui::compose_view_t* view,
                                           wormedit::ui::workspace_action_t* action,
                                           worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) return;
    if (state->layer_editor.layers_panel_chart_id != view->chart_id.value ||
        state->layer_editor.layers_panel_project_id != view->project_id.value) {
        state->layer_editor.layers_panel_total_count = view->semantic_layer_total_count;
        state->layer_editor.layers_panel_project_id = view->project_id.value;
        state->layer_editor.layers_panel_chart_id = view->chart_id.value;
        nk_group_set_scroll(context, "Compose layer list", 0, 0);
    }
    // Before rows: they open the menu, and the opening press must survive to its first begin.
    (void)wormedit::ui::chrome::context_menu_update(state->layer_editor.layers_menu, context);
    const LayersPanel panel{context, view, action, state};
    reveal_added_layer(panel);
    render_header(panel);
    render_layer_editors(panel);
    if (state->pointer_capture.layer_chip_reveal_id &&
        state->pointer_capture.chip_reveal_project == view->project_id.value && state->pointer_capture.chip_reveal_chart == view->chart_id.value &&
        state->pointer_capture.layer_chip_reveal_id == view->semantic_layer_target.layer_id.value) {
        nk_group_set_scroll(context, "Compose layer list", 0,
            static_cast<nk_uint>((view->semantic_layer_target_row + 1) *
                (row_height + context->style.window.spacing.y)));
    }
    state->pointer_capture.layer_chip_reveal_id = 0;
    const auto height = wormedit::ui::layout::remaining_content_height(context,
        {.minimum = 60.0f, .include_spacing = true,
         .clamp_order = wormedit::ui::layout::content_height_clamp_order_t::available_first});
    render_list(panel, height);
    render_layer_menu(panel);
}

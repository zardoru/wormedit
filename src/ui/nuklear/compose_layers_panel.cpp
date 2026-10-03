#include "ui/nuklear/compose_layers_panel.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/lucide_icons.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/vector_icons.h"

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
constexpr float row_height = 26.0f;

struct LayersPanel {
    nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    worm_ui_nuklear_compose_state_t* state;
};

auto raise(const LayersPanel& panel, WormUiComposeActionKind kind, std::uint64_t id = 0) -> bool {
    if (panel.action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return false;
    panel.action->compose_action = kind;
    panel.action->compose_input_chart_id = panel.view->chart_id;
    panel.action->compose_input_document_revision = panel.view->document_revision;
    panel.action->compose_semantic_layer_id = id;
    return true;
}

constexpr const char* layer_menu_title = "Layer actions";

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
    {"Require filter", WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_REQUIRE_FILTER, true, false},
    {"Exclude filter", WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_EXCLUDE_FILTER, true, false},
    {"Move up", WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP, true, true},
    {"Move down", WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN, true, true},
    {"Delete", WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER, true, true},
    {"Delete + unlink", WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER_WITH_MEMBERSHIPS, true, true},
};

auto captured_layer(const LayersPanel& panel) -> const WormUiComposeSemanticLayerView* {
    auto& state = *panel.state;
    if (state.layers_menu_layer.layer_id == 0) return nullptr;
    state.layers_menu_layer.name = state.layers_menu_layer_name.c_str();
    return &state.layers_menu_layer;
}

auto layer_capture_current(const LayersPanel& panel) -> bool {
    const auto& state = *panel.state;
    return state.layers_menu_project_id == panel.view->project_id &&
           state.layers_menu_chart_id == panel.view->chart_id &&
           state.layers_menu_revision == panel.view->document_revision;
}

auto layer_popup_bounds(const LayersPanel& panel) -> struct nk_rect {
    const auto region = nk_window_get_content_region(panel.context);
    return nk_rect(panel.state->layers_menu_x, panel.state->layers_menu_y,
                   std::min(240.0f, region.w), std::min(376.0f, region.h));
}

void open_layer_menu(const LayersPanel& panel, const WormUiComposeSemanticLayerView& layer,
                      std::size_t index, struct nk_rect bounds) {
    auto* context = panel.context;
    if (layer.layer_id == 0 || panel.state->layers_menu_open ||
        panel.view->input_occluded || !panel.view->scene_available ||
        wormedit::ui::chrome::popup_panel(context)) return;
    if (!nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT) ||
        !nk_input_is_mouse_hovering_rect(&context->input, bounds)) return;
    if (!raise(panel, WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER, layer.layer_id)) return;
    auto& state = *panel.state;
    state.layers_menu_open = true;
    state.layers_menu_input_consumed = true;
    state.layers_menu_project_id = panel.view->project_id;
    state.layers_menu_chart_id = panel.view->chart_id;
    state.layers_menu_revision = panel.view->document_revision;
    state.layers_menu_layer_id = layer.layer_id;
    state.layers_menu_layer = layer;
    state.layers_menu_layer_name = layer.name;
    state.layers_menu_row_index = index;
    state.layers_menu_anchor_x = context->input.mouse.pos.x;
    state.layers_menu_anchor_y = context->input.mouse.pos.y;
    state.semantic_layer_name_editor_visible = false;
    state.semantic_layer_color_editor_visible = false;
}

auto layer_entry_reason(const LayersPanel& panel, const WormUiComposeSemanticLayerView& layer,
                        const LayerMenuEntry& entry) -> const char* {
    if (!panel.view->scene_available) return "Open a Compose chart";
    if (entry.needs_idle && panel.view->semantic_layer_gesture_active)
        return "Finish or cancel the active Compose gesture";
    if (entry.needs_unlocked && layer.locked) return "Layer is locked";
    if (entry.kind == WORM_UI_COMPOSE_ACTION_DELETE_SEMANTIC_LAYER && layer.assigned_note_count)
        return "Layer has notes; use Delete + unlink";
    if (entry.kind == WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_UP &&
        panel.state->layers_menu_row_index == 0) return "Layer is already first";
    if (entry.kind == WORM_UI_COMPOSE_ACTION_MOVE_SEMANTIC_LAYER_DOWN &&
        panel.state->layers_menu_row_index + 1 >= panel.view->semantic_layer_total_count)
        return "Layer is already last";
    return nullptr;
}

auto raise_captured(const LayersPanel& panel, WormUiComposeActionKind kind) -> bool {
    if (!layer_capture_current(panel) ||
        !raise(panel, kind, panel.state->layers_menu_layer_id)) return false;
    panel.action->compose_input_chart_id = panel.state->layers_menu_chart_id;
    panel.action->compose_input_document_revision = panel.state->layers_menu_revision;
    return true;
}

void start_layer_editor(const LayersPanel& panel, const WormUiComposeSemanticLayerView& layer,
                         WormUiComposeActionKind kind) {
    auto& state = *panel.state;
    if (kind == WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER) {
        state.semantic_layer_name.fill('\0');
        std::snprintf(state.semantic_layer_name.data(), state.semantic_layer_name.size(), "%s", layer.name);
        state.semantic_layer_name_editor_visible = true;
        state.semantic_layer_name_focus_pending = true;
    } else {
        const auto value = nk_color_cf(nk_rgba(layer.red, layer.green, layer.blue, layer.alpha));
        state.semantic_layer_color = {value.r, value.g, value.b, value.a};
        state.semantic_layer_color_editor_visible = true;
    }
}

auto layer_entry_label(const LayersPanel& panel, const WormUiComposeSemanticLayerView& layer,
                       const LayerMenuEntry& entry) -> const char* {
    if (entry.kind == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY)
        return layer.visible ? "Hide" : "Show";
    if (entry.kind == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK)
        return layer.locked ? "Unlock" : "Lock";
    if (entry.kind == WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_SOLO &&
        panel.view->semantic_layer_target.layer_id == layer.layer_id &&
        panel.view->semantic_layer_solo_active) return "Solo: On";
    return entry.label;
}

void render_layer_menu_entry(const LayersPanel& panel, const WormUiComposeSemanticLayerView& layer,
                             const LayerMenuEntry& entry) {
    auto* context = panel.context;
    const auto* reason = layer_entry_reason(panel, layer, entry);
    nk_layout_row_dynamic(context, 24, 1);
    if (reason) nk_widget_disable_begin(context);
    const auto clicked = nk_button_label(context, layer_entry_label(panel, layer, entry));
    if (reason) nk_widget_disable_end(context);
    if (clicked && !reason) {
        if (entry.kind == WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER ||
            entry.kind == WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR)
            start_layer_editor(panel, layer, entry.kind);
        else
            (void)raise_captured(panel, entry.kind);
        panel.state->layers_menu_open = false;
        panel.state->layers_menu_release_pending =
            context->input.mouse.buttons[NK_BUTTON_LEFT].down ||
            context->input.mouse.buttons[NK_BUTTON_RIGHT].down;
        nk_popup_close(context);
    }
    if (reason) {
        nk_layout_row_dynamic(context, 32, 1);
        nk_label_wrap(context, reason);
    }
}

void render_layer_menu(const LayersPanel& panel) {
    auto* context = panel.context;
    auto& state = *panel.state;
    if (wormedit::ui::chrome::popup_panel(context)) return;
    if (state.layers_menu_close_pending) {
        wormedit::ui::chrome::dismiss_popup(context, layer_menu_title, layer_popup_bounds(panel));
        state.layers_menu_close_pending = false;
    }
    if (!state.layers_menu_open) return;
    const auto* layer = captured_layer(panel);
    if (!layer_capture_current(panel) || !layer ||
        nk_input_is_key_pressed(&context->input, NK_KEY_TEXT_RESET_MODE)) {
        state.layers_menu_open = false;
        wormedit::ui::chrome::dismiss_popup(context, layer_menu_title, layer_popup_bounds(panel));
        return;
    }
    const auto region = nk_window_get_content_region(context);
    const auto width = std::min(240.0f, region.w);
    const auto height = std::min(376.0f, region.h);
    state.layers_menu_x = std::clamp(state.layers_menu_anchor_x - region.x, 0.0f,
                                     std::max(0.0f, region.w - width));
    state.layers_menu_y = std::clamp(state.layers_menu_anchor_y - region.y, 0.0f,
                                     std::max(0.0f, region.h - height));
    const auto bounds = layer_popup_bounds(panel);
    const auto screen_bounds = nk_rect(region.x + bounds.x, region.y + bounds.y, bounds.w, bounds.h);
    const auto outside_press = nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT) ||
                               nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT);
    if (outside_press && !nk_input_is_mouse_hovering_rect(&context->input, screen_bounds)) {
        state.layers_menu_open = false;
        state.layers_menu_release_pending = true;
        wormedit::ui::chrome::dismiss_popup(context, layer_menu_title, bounds);
        return;
    }
    if (!nk_popup_begin(context, NK_POPUP_STATIC, layer_menu_title, NK_WINDOW_BORDER,
                        layer_popup_bounds(panel))) {
        state.layers_menu_open = false;
        state.layers_menu_release_pending = context->input.mouse.buttons[NK_BUTTON_LEFT].down ||
                                             context->input.mouse.buttons[NK_BUTTON_RIGHT].down;
        return;
    }
    for (const auto& entry : layer_menu_entries) render_layer_menu_entry(panel, *layer, entry);
    nk_popup_end(context);
}

void close_layer_editors(worm_ui_nuklear_compose_state_t& state) {
    state.semantic_layer_name_editor_visible = false;
    state.semantic_layer_name_focus_pending = false;
    state.semantic_layer_color_editor_visible = false;
}

auto layer_editor_allowed(const LayersPanel& panel, const WormUiComposeSemanticLayerView& layer)
    -> bool {
    return panel.view->scene_available && !layer.locked && !panel.view->semantic_layer_gesture_active;
}

void render_layer_name_editor(const LayersPanel& panel, const WormUiComposeSemanticLayerView& layer) {
    auto* context = panel.context;
    auto& state = *panel.state;
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Rename layer", NK_TEXT_LEFT);
    const auto allowed = layer_editor_allowed(panel, layer);
    if (!allowed) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 24, 1);
    (void)wormedit::ui::chrome::autofocus_text_field(context, NK_EDIT_FIELD | NK_EDIT_AUTO_SELECT,
        state.semantic_layer_name.data(), static_cast<int>(state.semantic_layer_name.size()),
        nk_filter_default, state.semantic_layer_name_focus_pending);
    if (!allowed) nk_widget_disable_end(context);
    nk_layout_row_dynamic(context, 24, 2);
    const auto valid = state.semantic_layer_name[0] != '\0';
    if (!allowed || !valid) nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, "Apply");
    if (!allowed || !valid) nk_widget_disable_end(context);
    const auto cancel = nk_button_label(context, "Cancel");
    if (apply && allowed && valid && raise_captured(panel, WORM_UI_COMPOSE_ACTION_RENAME_SEMANTIC_LAYER)) {
        std::memcpy(panel.action->compose_semantic_layer_name, state.semantic_layer_name.data(),
                    sizeof(panel.action->compose_semantic_layer_name));
        close_layer_editors(state);
    } else if (cancel) close_layer_editors(state);
}

auto layer_color_byte(float component) -> std::uint8_t {
    return static_cast<std::uint8_t>(std::lround(std::clamp(component, 0.0f, 1.0f) * 255.0f));
}

void render_layer_color_editor(const LayersPanel& panel, const WormUiComposeSemanticLayerView& layer) {
    auto* context = panel.context;
    auto& state = *panel.state;
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Layer color", NK_TEXT_LEFT);
    const auto allowed = layer_editor_allowed(panel, layer);
    if (!allowed) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 110, 1);
    const auto value = nk_color_picker(context,
        nk_colorf{state.semantic_layer_color.r, state.semantic_layer_color.g,
                  state.semantic_layer_color.b, state.semantic_layer_color.a}, NK_RGBA);
    state.semantic_layer_color = {value.r, value.g, value.b, value.a};
    nk_layout_row_dynamic(context, 24, 2);
    const auto apply = nk_button_label(context, "Apply");
    if (!allowed) nk_widget_disable_end(context);
    const auto cancel = nk_button_label(context, "Cancel");
    if (apply && allowed && raise_captured(panel, WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_COLOR)) {
        panel.action->compose_semantic_layer_red = layer_color_byte(value.r);
        panel.action->compose_semantic_layer_green = layer_color_byte(value.g);
        panel.action->compose_semantic_layer_blue = layer_color_byte(value.b);
        panel.action->compose_semantic_layer_alpha = layer_color_byte(value.a);
        close_layer_editors(state);
    } else if (cancel) close_layer_editors(state);
}

void render_layer_editors(const LayersPanel& panel) {
    auto& state = *panel.state;
    if (!state.semantic_layer_name_editor_visible && !state.semantic_layer_color_editor_visible) return;
    const auto* layer = captured_layer(panel);
    if (!layer_capture_current(panel) || !layer ||
        nk_input_is_key_pressed(&panel.context->input, NK_KEY_TEXT_RESET_MODE)) {
        close_layer_editors(state);
        return;
    }
    if (state.semantic_layer_name_editor_visible) render_layer_name_editor(panel, *layer);
    if (state.semantic_layer_color_editor_visible) render_layer_color_editor(panel, *layer);
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
        (void)raise(panel, WORM_UI_COMPOSE_ACTION_ADD_SEMANTIC_LAYER);
    if (!panel.view->scene_available) nk_widget_disable_end(context);
    nk_layout_row_end(context);
}

void render_row_name(const LayersPanel& panel, const WormUiComposeSemanticLayerView& layer,
                      std::size_t index) {
    auto* context = panel.context;
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT) &&
                         !panel.view->input_occluded;
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    open_layer_menu(panel, layer, index, bounds);
    auto* canvas = nk_window_get_canvas(context);
    if (layer.selected) nk_fill_rect(canvas, bounds, 3.0f, color(UiColor::selected));
    else if (hovered) nk_fill_rect(canvas, bounds, 3.0f, color(UiColor::surface_hover));
    nk_fill_circle(canvas, nk_rect(bounds.x + 5, bounds.y + 9, 8, 8),
                   nk_rgba(layer.red, layer.green, layer.blue, layer.alpha));
    char label[256];
    std::snprintf(label, sizeof(label), "%s  (%zu)", layer.name, layer.assigned_note_count);
    nk_draw_text(canvas, nk_rect(bounds.x + 20, bounds.y + 4, std::max(1.0f, bounds.w - 24),
                                 bounds.h - 8), label, static_cast<int>(std::strlen(label)),
                 context->style.font, nk_rgba(0, 0, 0, 0),
                 color(layer.selected ? UiColor::selected_text : UiColor::text));
    if (hovered) nk_tooltip(context, layer.locked ? "Layer is locked; click to choose its target"
                         : panel.view->selected_note_count ? "Assign selected notes to this layer"
                                                           : layer.name);
    if (clicked && panel.view->scene_available && layer.layer_id == 0) {
        (void)raise(panel, WORM_UI_COMPOSE_ACTION_SELECT_NO_SEMANTIC_LAYER);
    } else if (clicked && panel.view->scene_available)
        (void)raise(panel, panel.view->selected_note_count && !layer.locked
                              ? WORM_UI_COMPOSE_ACTION_APPLY_SEMANTIC_LAYER
                              : WORM_UI_COMPOSE_ACTION_SELECT_SEMANTIC_LAYER, layer.layer_id);
}

void render_row(const LayersPanel& panel, const WormUiComposeSemanticLayerView& layer,
                std::size_t index) {
    auto* context = panel.context;
    nk_layout_row_template_begin(context, row_height);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 24);
    nk_layout_row_template_push_static(context, 24);
    nk_layout_row_template_end(context);
    render_row_name(panel, layer, index);
    if (!panel.view->scene_available || panel.view->input_occluded) nk_widget_disable_begin(context);
    if (IconButton{layer.visible ? Glyph::eye : Glyph::eye_off,
                   layer.visible ? "Hide layer" : "Show layer"}.render(context))
        (void)raise(panel, WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_VISIBILITY, layer.layer_id);
    if (IconButton{layer.locked ? Glyph::lock : Glyph::lock_open,
                   layer.locked ? "Unlock layer" : "Lock layer"}.render(context))
        (void)raise(panel, WORM_UI_COMPOSE_ACTION_TOGGLE_SEMANTIC_LAYER_LOCK, layer.layer_id);
    if (!panel.view->scene_available || panel.view->input_occluded) nk_widget_disable_end(context);
}

void render_no_layer_row(const LayersPanel& panel) {
    const auto neutral = wormedit::ui::theme::compose(
        wormedit::ui::theme::ComposeColor::semantic_layer_none);
    const WormUiComposeSemanticLayerView layer{
        .layer_id = 0, .name = "No layer", .red = neutral.r, .green = neutral.g,
        .blue = neutral.b, .alpha = neutral.a,
        .assigned_note_count = panel.view->semantic_layer_unassigned_note_count,
        .visible = true, .locked = false, .selected = panel.view->semantic_layer_target_none};
    nk_layout_row_dynamic(panel.context, row_height, 1);
    render_row_name(panel, layer, 0);
}

void render_list_row(const LayersPanel& panel, int index) {
    if (index == 0) { render_no_layer_row(panel); return; }
    const auto offset = static_cast<std::size_t>(index - 1);
    if (offset < panel.view->semantic_layer_first_row ||
        offset - panel.view->semantic_layer_first_row >= panel.view->semantic_layer_count) {
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
    if (!panel.state->layers_menu_open && panel.view->semantic_layer_total_count &&
        (first != panel.view->semantic_layer_first_row || count != panel.view->semantic_layer_count) &&
        raise(panel, WORM_UI_COMPOSE_ACTION_SET_SEMANTIC_LAYER_WINDOW)) {
        panel.action->compose_semantic_layer_first_row = first;
        panel.action->compose_semantic_layer_row_count = count;
    }
    nk_list_view_end(&list);
}

} // namespace

void worm_ui_nuklear_dismiss_layer_popup(struct nk_context* context) {
    auto& state = worm_ui_nuklear_compose_state();
    if (!context || !state.layers_menu_close_pending ||
        wormedit::ui::chrome::popup_panel(context)) return;
    wormedit::ui::chrome::dismiss_popup(context, layer_menu_title,
        nk_rect(state.layers_menu_x, state.layers_menu_y, 240, 376));
    state.layers_menu_close_pending = false;
}

void worm_ui_nuklear_render_compose_layers(struct nk_context* context,
                                           const WormUiComposeView* view,
                                           WormUiWorkspaceAction* action,
                                           worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) return;
    if (state->layers_panel_chart_id != view->chart_id ||
        state->layers_panel_project_id != view->project_id) {
        state->layers_panel_project_id = view->project_id;
        state->layers_panel_chart_id = view->chart_id;
        nk_group_set_scroll(context, "Compose layer list", 0, 0);
    }
    const LayersPanel panel{context, view, action, state};
    render_header(panel);
    render_layer_editors(panel);
    const auto region = nk_window_get_content_region(context);
    const auto remaining = region.y + region.h - nk_widget_position(context).y -
                           context->style.window.spacing.y;
    render_list(panel, std::max(remaining, 60.0f));
    render_layer_menu(panel);
    if (!context->input.mouse.buttons[NK_BUTTON_LEFT].down &&
        !context->input.mouse.buttons[NK_BUTTON_RIGHT].down)
        state->layers_menu_release_pending = false;
}

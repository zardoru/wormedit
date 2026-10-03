#include "ui/nuklear/compose_sounds_panel.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/lucide_icons.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/vector_icons.h"

#include "wormedit/nuklear_config.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace {

using wormedit::ui::chrome::color;
using wormedit::ui::chrome::DrawnIconButton;
using wormedit::ui::chrome::IconButton;
using wormedit::ui::lucide::Glyph;
using wormedit::ui::theme::UiColor;
namespace icons = wormedit::ui::vector_icons;

constexpr float row_height = 22.0f;
constexpr float layer_row_height = 22.0f;
constexpr float chip_size = 8.0f;

struct SoundsPanel {
    nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    worm_ui_nuklear_compose_state_t* state;
};

// ---- Multi-file picker. SDL may call back from another thread. ----

struct PickerState {
    std::mutex mutex;
    bool open{};
    std::vector<std::string> paths;
};

auto picker() -> PickerState& {
    static PickerState state;
    return state;
}

void SDLCALL picker_callback(void* /*userdata*/, const char* const* filelist, int /*filter*/) {
    auto& state = picker();
    std::lock_guard lock{state.mutex};
    state.open = false;
    for (std::size_t index = 0; filelist && filelist[index] &&
                                state.paths.size() < WORM_UI_SOUND_DROP_CAPACITY; ++index)
        state.paths.emplace_back(filelist[index]);
}

auto picker_open() -> bool {
    auto& state = picker();
    std::lock_guard lock{state.mutex};
    return state.open;
}

void show_picker() {
    {
        auto& state = picker();
        std::lock_guard lock{state.mutex};
        if (state.open) return;
        state.open = true;
        state.paths.clear();
    }
    static const SDL_DialogFileFilter filters[]{{"Audio files", "wav;ogg;mp3;flac"}};
    SDL_ShowOpenFileDialog(picker_callback, nullptr, SDL_GetKeyboardFocus(), filters, 1, nullptr,
                           true);
}

auto take_picked_paths(std::vector<std::string>& destination) -> bool {
    auto& state = picker();
    std::lock_guard lock{state.mutex};
    if (state.paths.empty()) return false;
    destination = std::move(state.paths);
    state.paths.clear();
    return true;
}

// ---- Actions ----

auto raise(const SoundsPanel& panel, WormUiComposeActionKind kind) -> bool {
    if (panel.action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return false;
    panel.action->compose_action = kind;
    panel.action->compose_input_chart_id = panel.view->chart_id;
    panel.action->compose_input_document_revision = panel.view->document_revision;
    return true;
}

void stage_selection(const SoundsPanel& panel) {
    auto& state = *panel.state;
    state.keysound_staged_ids = state.keysound_selection.selected;
    panel.action->compose_sound_resource_ids = state.keysound_staged_ids.data();
    panel.action->compose_sound_resource_count = state.keysound_staged_ids.size();
}

void raise_on_selection(const SoundsPanel& panel, WormUiComposeActionKind kind,
                        std::uint64_t layer_id = 0) {
    if (!raise(panel, kind)) return;
    stage_selection(panel);
    panel.action->compose_semantic_layer_id = layer_id;
}

void import_picked_files(const SoundsPanel& panel) {
    auto& state = *panel.state;
    if (panel.action->compose_action != WORM_UI_COMPOSE_ACTION_NONE ||
        !take_picked_paths(state.keysound_staged_paths))
        return;
    state.keysound_staged_path_pointers.clear();
    for (const auto& path : state.keysound_staged_paths)
        state.keysound_staged_path_pointers.push_back(path.c_str());
    (void)raise(panel, WORM_UI_COMPOSE_ACTION_IMPORT_SOUND_FILES);
    panel.action->compose_import_paths = state.keysound_staged_path_pointers.data();
    panel.action->compose_import_path_count = state.keysound_staged_path_pointers.size();
}

// ---- Selection state kept in step with the document ----

auto find_row(const WormUiComposeView& view, std::uint64_t id)
    -> const WormUiComposeKeysoundRowView* {
    for (std::size_t index = 0; index < view.keysound_count; ++index)
        if (view.keysounds[index].resource_id == id) return &view.keysounds[index];
    return nullptr;
}

void sync_with_document(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto& view = *panel.view;
    if (state.keysound_chart_id != view.chart_id) {
        worm_ui_keysound_clear(state.keysound_selection);
        state.keysound_chart_id = view.chart_id;
        state.keysound_revision = view.document_revision;
        return;
    }
    if (state.keysound_revision == view.document_revision) return;
    state.keysound_revision = view.document_revision;
    state.keysound_all_ids.clear();
    for (std::size_t index = 0; index < view.keysound_count; ++index)
        state.keysound_all_ids.push_back(view.keysounds[index].resource_id);
    worm_ui_keysound_prune(state.keysound_selection, state.keysound_all_ids);
}

void collect_shown_and_masks(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto& view = *panel.view;
    const std::string_view query{state.keysound_search.data()};
    state.keysound_shown.clear();
    state.keysound_masks.clear();
    for (std::size_t index = 0; index < view.keysound_count; ++index) {
        const auto& row = view.keysounds[index];
        state.keysound_masks.push_back({row.resource_id, row.layer_mask});
        if (worm_ui_keysound_matches(row.slot, row.name, query))
            state.keysound_shown.push_back(row.resource_id);
    }
}

// ---- Toolbar ----

auto paint_allowed(const SoundsPanel& panel) -> bool {
    const auto& view = *panel.view;
    const auto* armed = find_row(view, panel.state->armed_resource_id);
    return view.scene_available && armed && !armed->missing && !view.note_paint_mode_active &&
           !view.semantic_layer_paint_mode_active && !view.timing_stage_active &&
           !view.timing_focus;
}

void toggle_paint(const SoundsPanel& panel) {
    auto& state = *panel.state;
    const auto active = state.resource_pointer_mode ==
                        worm_ui_nuklear_compose_resource_pointer_mode_t::place;
    state.resource_pointer_mode = active ? worm_ui_nuklear_compose_resource_pointer_mode_t::off
                                         : worm_ui_nuklear_compose_resource_pointer_mode_t::place;
    state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::sample;
    state.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
}

template <typename Button>
auto toolbar_button(nk_context* context, bool enabled, const Button& button) -> bool {
    if (!enabled) nk_widget_disable_begin(context);
    const auto clicked = button.render(context);
    if (!enabled) nk_widget_disable_end(context);
    return clicked && enabled;
}

void render_toolbar(const SoundsPanel& panel) {
    auto* context = panel.context;
    auto& state = *panel.state;
    const auto has_primary = find_row(*panel.view, state.keysound_selection.primary) != nullptr;
    const auto has_selection = !state.keysound_selection.selected.empty();
    const auto painting = state.resource_pointer_mode ==
                          worm_ui_nuklear_compose_resource_pointer_mode_t::place;
    const auto spacing = context->style.window.spacing;
    const auto pushed = nk_style_push_vec2(context, &context->style.window.spacing,
                                           nk_vec2(1.0f, spacing.y)) != 0;
    nk_layout_row_template_begin(context, 27);
    IconButton::layout_columns(context, 8);
    nk_layout_row_template_end(context);

    const auto can_paint = paint_allowed(panel);
    if (toolbar_button(context, can_paint || painting,
            IconButton{Glyph::paintbrush,
                       can_paint || painting
                           ? "Paint: place the selected sound as a note by clicking the chart"
                           : "Paint: select a sound first (and leave note paint mode)",
                       painting}))
        toggle_paint(panel);
    if (toolbar_button(context, has_primary,
            IconButton{Glyph::play, "Play the selected sound (Space on the list)"}) &&
        raise(panel, WORM_UI_COMPOSE_ACTION_AUDITION_SOUND))
        panel.action->compose_resource_id = state.keysound_selection.primary;
    if (IconButton{Glyph::audio_lines, state.sounds_preview_on_click
                                            ? "Preview on click: on (click to turn off)"
                                            : "Preview on click: off (click to turn on)",
                   state.sounds_preview_on_click}.render(context))
        state.sounds_preview_on_click = !state.sounds_preview_on_click;
    if (toolbar_button(context, !picker_open(),
            DrawnIconButton{icons::plus,
                            "Add sounds from files (you can also drop audio files on this list)"}))
        show_picker();
    if (toolbar_button(context, has_selection,
            DrawnIconButton{icons::trash,
                            "Remove the selected sounds from the list (only sounds no note uses)"}))
        raise_on_selection(panel, WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS);
    if (toolbar_button(context, !state.keysound_shown.empty(),
            DrawnIconButton{icons::select_all, "Select every sound shown"}))
        worm_ui_keysound_select_all(state.keysound_selection, state.keysound_shown);
    if (toolbar_button(context, has_selection,
            IconButton{Glyph::x, "Clear the selection"}))
        worm_ui_keysound_clear(state.keysound_selection);
    if (IconButton{Glyph::layers, "Layers: give the selected sounds a semantic layer, so notes "
                                  "painted from them join it", state.sounds_layers_open}
            .render(context))
        state.sounds_layers_open = !state.sounds_layers_open;
    if (pushed) (void)nk_style_pop_vec2(context);
}

void render_search(const SoundsPanel& panel) {
    auto* context = panel.context;
    nk_layout_row_dynamic(context, 25, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(
        context, NK_EDIT_FIELD, panel.state->keysound_search.data(),
        static_cast<int>(panel.state->keysound_search.size()), nk_filter_default);
}

// ---- Layer strip ----

void draw_tri_state(struct nk_command_buffer* canvas, struct nk_rect box, worm_ui_keysound_layer_state_t state,
                    nk_color tint) {
    nk_stroke_rect(canvas, box, 2.0f, 1.0f, tint);
    if (state == worm_ui_keysound_layer_state_t::all)
        nk_fill_rect(canvas, nk_rect(box.x + 3, box.y + 3, box.w - 6, box.h - 6), 1.0f, tint);
    else if (state == worm_ui_keysound_layer_state_t::some)
        nk_fill_rect(canvas, nk_rect(box.x + 3, box.y + box.h * 0.5f - 1.0f, box.w - 6, 2.0f),
                     0.0f, tint);
}

void assign_sound_layer(const SoundsPanel& panel, std::size_t index) {
    const auto& layer = panel.view->keysound_layers[index];
    const auto mask = std::uint64_t{1} << index;
    const auto conflicts = std::ranges::any_of(panel.state->keysound_selection.selected,
        [&](auto id) {
            const auto* row = find_row(*panel.view, id);
            return row && (row->note_layer_conflict_mask & mask) != 0;
        });
    if (!conflicts) {
        raise_on_selection(panel, WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER, layer.layer_id);
        return;
    }
    auto& state = *panel.state;
    state.sound_layer_confirmation_open = true;
    state.sound_layer_confirmation_project = panel.view->project_id;
    state.sound_layer_confirmation_chart = panel.view->chart_id;
    state.sound_layer_confirmation_revision = panel.view->document_revision;
    state.sound_layer_confirmation_layer = layer.layer_id;
    state.sound_layer_confirmation_resources = state.keysound_selection.selected;
}

void confirm_sound_layer(const SoundsPanel& panel, bool overwrite) {
    if (!raise(panel, WORM_UI_COMPOSE_ACTION_ADD_SOUND_LAYER)) return;
    auto& state = *panel.state;
    state.keysound_staged_ids = state.sound_layer_confirmation_resources;
    panel.action->compose_sound_resource_ids = state.keysound_staged_ids.data();
    panel.action->compose_sound_resource_count = state.keysound_staged_ids.size();
    panel.action->compose_semantic_layer_id = state.sound_layer_confirmation_layer;
    panel.action->compose_sound_overwrite_note_layers = overwrite;
    state.sound_layer_confirmation_open = false;
}

void render_sound_layer_confirmation(const SoundsPanel& panel) {
    auto& state = *panel.state;
    if (!state.sound_layer_confirmation_open) return;
    const auto& view = *panel.view;
    auto* context = panel.context;
    const auto region = nk_window_get_content_region(context);
    const auto bounds = nk_rect(0, 60, std::min(360.0f, region.w), 204);
    if (state.sound_layer_confirmation_project != view.project_id ||
        state.sound_layer_confirmation_chart != view.chart_id ||
        state.sound_layer_confirmation_revision != view.document_revision) {
        state.sound_layer_confirmation_open = false;
        wormedit::ui::chrome::dismiss_popup(context, "Sound note layers", bounds);
        return;
    }
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "Sound note layers",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, bounds)) {
        state.sound_layer_confirmation_open = false;
        return;
    }
    nk_layout_row_dynamic(context, 66, 1);
    nk_label_wrap(context, "Some notes using these sounds have different layers. Keep their "
                           "layers, or replace them with the sounds' layers?");
    nk_layout_row_dynamic(context, 28, 1);
    if (nk_button_label(context, "Keep existing layers")) confirm_sound_layer(panel, false);
    if (nk_button_label(context, "Overwrite")) confirm_sound_layer(panel, true);
    if (nk_button_label(context, "Cancel")) state.sound_layer_confirmation_open = false;
    if (!state.sound_layer_confirmation_open) nk_popup_close(context);
    nk_popup_end(context);
}

void render_layer_row(const SoundsPanel& panel, std::size_t index) {
    auto* context = panel.context;
    const auto& layer = panel.view->keysound_layers[index];
    const auto layer_state = worm_ui_keysound_layer_state(
        panel.state->keysound_selection, panel.state->keysound_masks,
        static_cast<unsigned>(index));
    const auto has_selection = !panel.state->keysound_selection.selected.empty();
    const auto adding = layer_state != worm_ui_keysound_layer_state_t::all;
    const auto enabled = has_selection && !(adding && layer.locked);
    nk_layout_row_dynamic(context, layer_row_height, 1);
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT);
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    auto* canvas = nk_window_get_canvas(context);
    if (hovered && enabled) nk_fill_rect(canvas, bounds, 3.0f, color(UiColor::surface_hover));
    const auto tint = enabled ? color(UiColor::text) : color(UiColor::text_muted);
    draw_tri_state(canvas, nk_rect(bounds.x + 4, bounds.y + 4, 14, 14), layer_state, tint);
    nk_fill_circle(canvas, nk_rect(bounds.x + 24, bounds.y + 7, 8, 8),
                   nk_rgba(layer.red, layer.green, layer.blue, 255));
    std::string label = layer.name;
    if (layer.locked) label += " (locked)";
    nk_draw_text(canvas, nk_rect(bounds.x + 38, bounds.y + 2, bounds.w - 42, bounds.h - 4),
                 label.c_str(), static_cast<int>(label.size()), context->style.font,
                 nk_rgba(0, 0, 0, 0), tint);
    if (hovered)
        nk_tooltip(context, !has_selection ? "Select sounds first"
                            : layer.locked && adding ? "Locked layers cannot gain sounds"
                            : adding ? "Add this layer to the selected sounds"
                                     : "Remove this layer from the selected sounds");
    if (clicked && enabled) {
        if (adding) assign_sound_layer(panel, index);
        else raise_on_selection(panel, WORM_UI_COMPOSE_ACTION_REMOVE_SOUND_LAYER, layer.layer_id);
    }
}

void render_layer_section(const SoundsPanel& panel) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    if (view.keysound_layer_count == 0) {
        nk_layout_row_dynamic(context, 34, 1);
        nk_label_wrap(context, "No layers yet. Create them in the Semantic layers section of "
                               "the inspector.");
        return;
    }
    const auto visible = std::min<std::size_t>(view.keysound_layer_count, 5);
    const auto height = static_cast<float>(visible) * (layer_row_height +
                        context->style.window.spacing.y) + 6.0f;
    nk_layout_row_dynamic(context, height, 1);
    if (nk_group_begin(context, "Sound layers", NK_WINDOW_BORDER)) {
        for (std::size_t index = 0; index < view.keysound_layer_count; ++index)
            render_layer_row(panel, index);
        nk_group_end(context);
    }
    nk_layout_row_dynamic(context, 24, 1);
    const auto can_clear = !panel.state->keysound_selection.selected.empty();
    if (!can_clear) nk_widget_disable_begin(context);
    const auto clear = nk_button_label(context, "Clear layers of selected sounds");
    if (!can_clear) nk_widget_disable_end(context);
    if (clear && can_clear)
        raise_on_selection(panel, WORM_UI_COMPOSE_ACTION_CLEAR_SOUND_LAYERS);
}

// ---- List ----

auto modifier_state() -> std::pair<bool, bool> {
    const auto modifiers = SDL_GetModState();
    return {(modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, (modifiers & SDL_KMOD_SHIFT) != 0};
}

void draw_row(const SoundsPanel& panel, struct nk_rect bounds, const WormUiComposeKeysoundRowView& row,
              bool selected, bool hovered) {
    auto* context = panel.context;
    auto* canvas = nk_window_get_canvas(context);
    const auto armed = panel.state->armed_resource_id == row.resource_id;
    if (selected) nk_fill_rect(canvas, bounds, 3.0f, color(UiColor::selected));
    else if (hovered) nk_fill_rect(canvas, bounds, 3.0f, color(UiColor::surface_hover));
    if (armed) nk_stroke_rect(canvas, bounds, 3.0f, 1.0f, color(UiColor::accent));
    const auto text_color = row.missing ? color(UiColor::text_muted)
                            : selected  ? color(UiColor::selected_text) : color(UiColor::text);
    const auto clear = nk_rgba(0, 0, 0, 0);
    constexpr float slot_width = 30.0f;
    const auto slot_len = static_cast<int>(std::strlen(row.slot));
    nk_draw_text(canvas, nk_rect(bounds.x + 6, bounds.y + 3, slot_width, bounds.h - 6), row.slot,
                 slot_len, context->style.font, clear, color(UiColor::text_muted));
    const auto chips = static_cast<float>(
        std::min<int>(std::popcount(row.layer_mask), 3)) * (chip_size + 2.0f);
    const auto name_width = bounds.w - slot_width - 12.0f - chips;
    nk_draw_text(canvas, nk_rect(bounds.x + 6 + slot_width, bounds.y + 3, std::max(name_width, 8.0f),
                                 bounds.h - 6),
                 row.name, static_cast<int>(std::strlen(row.name)), context->style.font, clear,
                 text_color);
    float chip_x = bounds.x + bounds.w - chips - 2.0f;
    int drawn = 0;
    for (std::size_t bit = 0; bit < panel.view->keysound_layer_count && drawn < 3; ++bit) {
        if (((row.layer_mask >> bit) & 1U) == 0) continue;
        const auto& layer = panel.view->keysound_layers[bit];
        nk_fill_circle(canvas, nk_rect(chip_x, bounds.y + (bounds.h - chip_size) * 0.5f,
                                       chip_size, chip_size),
                       nk_rgba(layer.red, layer.green, layer.blue, 255));
        chip_x += chip_size + 2.0f;
        ++drawn;
    }
}

auto layer_tooltip(const SoundsPanel& panel, const WormUiComposeKeysoundRowView& row)
    -> std::string {
    std::string text = row.missing ? "File missing. " : "";
    std::string layers;
    for (std::size_t bit = 0; bit < panel.view->keysound_layer_count; ++bit) {
        if (((row.layer_mask >> bit) & 1U) == 0) continue;
        if (!layers.empty()) layers += ", ";
        layers += panel.view->keysound_layers[bit].name;
    }
    if (!layers.empty()) text += "Layers: " + layers;
    return text;
}

void click_row(const SoundsPanel& panel, std::size_t shown_index, std::uint64_t id) {
    auto& state = *panel.state;
    const auto [ctrl, shift] = modifier_state();
    worm_ui_keysound_click(state.keysound_selection, state.keysound_shown, shown_index, ctrl,
                           shift);
    const auto* primary = find_row(*panel.view, state.keysound_selection.primary);
    state.armed_resource_id = primary && !primary->missing ? primary->resource_id : 0;
    state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::sample;
    if (state.sounds_preview_on_click && !ctrl && !shift && id == state.armed_resource_id &&
        raise(panel, WORM_UI_COMPOSE_ACTION_AUDITION_SOUND))
        panel.action->compose_resource_id = id;
}

void render_row(const SoundsPanel& panel, std::size_t shown_index) {
    auto* context = panel.context;
    const auto id = panel.state->keysound_shown[shown_index];
    const auto* row = find_row(*panel.view, id);
    nk_layout_row_dynamic(context, row_height, 1);
    if (!row) return;
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT) &&
                         !panel.view->input_occluded;
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    draw_row(panel, bounds, *row, worm_ui_keysound_is_selected(panel.state->keysound_selection, id),
             hovered);
    if (hovered) {
        const auto tip = layer_tooltip(panel, *row);
        if (!tip.empty()) nk_tooltip(context, tip.c_str());
    }
    if (clicked) click_row(panel, shown_index, id);
}

void render_list_hint(const SoundsPanel& panel) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    nk_layout_row_dynamic(context, 52, 1);
    if (view.keysound_total_count == 0)
        nk_label_wrap(context, "No sounds yet. Drop audio files here, or use the add button.");
    else
        nk_label_wrap(context, "No sound matches the search.");
}

void render_list(const SoundsPanel& panel, float height) {
    auto* context = panel.context;
    const auto count = static_cast<int>(panel.state->keysound_shown.size());
    nk_layout_row_dynamic(context, height, 1);
    struct nk_list_view list{};
    if (!nk_list_view_begin(context, &list, "Compose sound list", NK_WINDOW_BORDER,
                            static_cast<int>(row_height), std::max(count, 1))) return;
    if (count == 0) {
        render_list_hint(panel);
    } else {
        for (int index = list.begin; index < std::min(list.end, count); ++index)
            render_row(panel, static_cast<std::size_t>(index));
    }
    nk_list_view_end(&list);
}

void render_title(const SoundsPanel& panel) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    const auto& state = *panel.state;
    nk_layout_row_dynamic(context, 20, 1);
    char text[96];
    std::snprintf(text, sizeof(text), "Sounds  %zu%s  ·  %zu selected", view.keysound_total_count,
                  view.keysounds_truncated ? "+" : "", state.keysound_selection.selected.size());
    nk_label(context, text, NK_TEXT_LEFT);
}

void publish_drop_bounds(const SoundsPanel& panel) {
    const auto bounds = nk_window_get_bounds(panel.context);
    auto& state = *panel.state;
    state.sound_list_valid = true;
    state.sound_list_x = bounds.x;
    state.sound_list_y = bounds.y;
    state.sound_list_width = bounds.w;
    state.sound_list_height = bounds.h;
}

} // namespace

void worm_ui_nuklear_render_compose_sounds(struct nk_context* context,
                                           const WormUiComposeView* view,
                                           WormUiWorkspaceAction* action,
                                           worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) return;
    const SoundsPanel panel{context, view, action, state};
    publish_drop_bounds(panel);
    sync_with_document(panel);
    collect_shown_and_masks(panel);
    import_picked_files(panel);
    render_title(panel);
    render_toolbar(panel);
    render_search(panel);
    if (state->sounds_layers_open) render_layer_section(panel);
    const auto region = nk_window_get_content_region(context);
    const auto remaining = region.y + region.h - nk_widget_position(context).y -
                           context->style.window.spacing.y;
    render_list(panel, std::max(remaining, 60.0f));
    render_sound_layer_confirmation(panel);
}

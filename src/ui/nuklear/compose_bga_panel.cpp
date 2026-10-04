#include "ui/nuklear/compose_bga_panel.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/vector_icons.h"
#include <SDL3/SDL.h>
#include <nuklear.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
namespace {
using wormedit::ui::chrome::IconButton;
using wormedit::ui::chrome::DrawnIconButton;
using wormedit::ui::lucide::Glyph;
namespace icons = wormedit::ui::vector_icons;
struct Texture { SDL_Texture* value{}; std::uint64_t content{}, project{}; };
SDL_Renderer* renderer{};
std::unordered_map<std::uint64_t, Texture> textures;
struct Picker { std::mutex mutex; bool open{}; std::vector<std::string> paths; };
Picker picker;
void SDLCALL picked(void*, const char* const* paths, int) {
    std::lock_guard lock{picker.mutex};
    picker.open = false;
    for (std::size_t index = 0; paths && paths[index] && index < WORM_UI_SOUND_DROP_CAPACITY; ++index)
        picker.paths.emplace_back(paths[index]);
}
void open_picker() {
    { std::lock_guard lock{picker.mutex}; if (picker.open) return; picker.open = true; }
    static const SDL_DialogFileFilter filter{"Images", "png;jpg;jpeg;bmp;gif;tga;ppm"};
    SDL_ShowOpenFileDialog(picked, nullptr, SDL_GetKeyboardFocus(), &filter, 1, nullptr, true);
}
auto raise(WormUiWorkspaceAction& action, const WormUiComposeView& view,
           WormUiComposeActionKind kind) -> bool {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return false;
    action.compose_action = kind;
    action.compose_input_chart_id = view.chart_id;
    action.compose_input_document_revision = view.document_revision;
    return true;
}
void import_picked(WormUiWorkspaceAction& action, const WormUiComposeView& view,
                   worm_ui_nuklear_compose_state_t& state) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    { std::lock_guard lock{picker.mutex};
      if (picker.paths.empty()) return;
      state.bga_staged_paths = std::move(picker.paths); picker.paths.clear(); }
    state.bga_staged_path_pointers.clear();
    for (const auto& path : state.bga_staged_paths) state.bga_staged_path_pointers.push_back(path.c_str());
    if (!raise(action, view, WORM_UI_COMPOSE_ACTION_IMPORT_BGA_FILES)) return;
    action.compose_import_paths = state.bga_staged_path_pointers.data();
    action.compose_import_path_count = state.bga_staged_path_pointers.size();
}
auto thumbnail(const WormUiComposeView& view, const WormUiComposeBgaRowView& row) -> SDL_Texture* {
    if (!renderer || !row.rgba_pixels || !row.image_width || !row.image_height ||
        row.image_width > 32 || row.image_height > 32 ||
        row.rgba_byte_count != static_cast<std::size_t>(row.image_width) * row.image_height * 4)
        return nullptr;
    if (!textures.contains(row.resource_id) && textures.size() >= 128) {
        if (textures.begin()->second.value) SDL_DestroyTexture(textures.begin()->second.value);
        textures.erase(textures.begin());
    }
    auto& entry = textures[row.resource_id];
    if (entry.content == row.image_content_id && entry.project == view.project_id) return entry.value;
    if (entry.value) SDL_DestroyTexture(entry.value);
    entry = {};
    entry.value = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                     static_cast<int>(row.image_width), static_cast<int>(row.image_height));
    if (!entry.value) return nullptr;
    if (!SDL_UpdateTexture(entry.value, nullptr, row.rgba_pixels, static_cast<int>(row.image_width * 4))) {
        SDL_DestroyTexture(entry.value); entry.value = nullptr; return nullptr;
    }
    SDL_SetTextureBlendMode(entry.value, SDL_BLENDMODE_BLEND);
    entry.content = row.image_content_id;
    entry.project = view.project_id;
    return entry.value;
}
void toolbar(nk_context* context, const WormUiComposeView& view, worm_ui_nuklear_compose_state_t& state,
              WormUiWorkspaceAction& action) {
    nk_layout_row_template_begin(context, 27);
    IconButton::layout_columns(context, 5);
    nk_layout_row_template_end(context);
    const auto active = state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::place &&
        state.resource_filter == worm_ui_nuklear_compose_resource_filter_t::bga;
    const auto matching = std::span(view.bga_matching_ids, view.bga_total_matching);
    const auto can_paint = state.armed_resource_id && state.armed_resource_id == state.bga_selection.primary &&
        std::ranges::find(matching, state.armed_resource_id) != matching.end() &&
        !view.note_paint_mode_active && !view.semantic_layer_paint_mode_active && !view.timing_focus;
    if (!can_paint && !active) nk_widget_disable_begin(context);
    if (IconButton{Glyph::paintbrush, "Paint selected BGA on the chart", active}.render(context) && (can_paint || active)) {
        state.resource_pointer_mode = active ? worm_ui_nuklear_compose_resource_pointer_mode_t::off
                                            : worm_ui_nuklear_compose_resource_pointer_mode_t::place;
        state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::bga;
        state.armed_from_sounds_dock = false;
    }
    if (!can_paint && !active) nk_widget_disable_end(context);
    if (DrawnIconButton{icons::plus, "Add BGA images (or drop images here)"}.render(context)) open_picker();
    if (DrawnIconButton{icons::trash, "Remove unused BGA resources"}.render(context))
        (void)raise(action, view, WORM_UI_COMPOSE_ACTION_REMOVE_UNUSED_BGA);
    if (DrawnIconButton{icons::select_all, "Select every BGA shown"}.render(context))
        worm_ui_keysound_select_all(state.bga_selection, {view.bga_matching_ids, view.bga_total_matching});
    if (IconButton{Glyph::x, "Clear BGA selection"}.render(context)) worm_ui_keysound_clear(state.bga_selection);
}
void row(nk_context* context, const WormUiComposeView& view, worm_ui_nuklear_compose_state_t& state,
          std::size_t index) {
    nk_layout_row_dynamic(context, 36, 1);
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT) && !view.input_occluded;
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID || index < view.bga_first_row ||
        index - view.bga_first_row >= view.bga_row_count) return;
    const auto& value = view.bga_rows[index - view.bga_first_row];
    const auto selected = worm_ui_keysound_is_selected(state.bga_selection, value.resource_id);
    auto* canvas = nk_window_get_canvas(context);
    if (selected || hovered) nk_fill_rect(canvas, bounds, 3, nk_rgb(44, 72, 109));
    const auto image_bounds = nk_rect(bounds.x + 2, bounds.y + 2, 32, 32);
    if (auto* image = thumbnail(view, value)) {
        const auto handle = nk_image_ptr(image);
        const auto fitted = nk_rect(image_bounds.x + (32 - value.image_width) * .5f,
            image_bounds.y + (32 - value.image_height) * .5f,
            static_cast<float>(value.image_width), static_cast<float>(value.image_height));
        nk_draw_image(canvas, fitted, &handle, nk_rgb(255, 255, 255));
    }
    else {
        nk_fill_rect(canvas, image_bounds, 2, nk_rgb(45, 48, 59));
        const auto label = value.video ? "VID" : value.missing ? "!" : "…";
        nk_draw_text(canvas, image_bounds, label, static_cast<int>(std::strlen(label)), context->style.font,
                      nk_rgba(0, 0, 0, 0), nk_rgb(174, 183, 197));
    }
    char label[512];
    std::snprintf(label, sizeof(label), "%s  %s  · %u%s", value.slot, value.name, value.use_count,
                  value.missing ? " · missing" : "");
    nk_draw_text(canvas, nk_rect(bounds.x + 40, bounds.y + 8, bounds.w - 42, 20), label,
        static_cast<int>(std::strlen(label)), context->style.font, nk_rgba(0, 0, 0, 0), nk_rgb(222, 228, 238));
    if (hovered) nk_tooltip(context, label);
    if (!clicked) return;
    const auto modifiers = SDL_GetModState();
    worm_ui_keysound_click(state.bga_selection, {view.bga_matching_ids, view.bga_total_matching}, index,
        (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, (modifiers & SDL_KMOD_SHIFT) != 0);
    state.armed_resource_id = value.missing ? 0 : state.bga_selection.primary;
    state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::bga;
    state.armed_from_sounds_dock = false;
}
}
void worm_ui_nuklear_bga_set_renderer(SDL_Renderer* value) {
    for (const auto& [id, texture] : textures) if (texture.value) SDL_DestroyTexture(texture.value);
    textures.clear(); renderer = value;
}
void worm_ui_nuklear_render_compose_bga(nk_context* context, const WormUiComposeView* view,
    WormUiWorkspaceAction* action, worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) return;
    if (state->bga_project_id != view->project_id || state->bga_chart_id != view->chart_id) {
        state->bga_selection = {}; state->bga_search.fill(0); state->bga_first_row = 0;
        state->bga_project_id = view->project_id; state->bga_chart_id = view->chart_id;
        worm_ui_nuklear_bga_set_renderer(renderer);
    }
    const auto bounds = nk_window_get_content_region(context);
    state->bga_list_valid = true;
    state->bga_list_x = bounds.x; state->bga_list_y = bounds.y;
    state->bga_list_width = bounds.w; state->bga_list_height = bounds.h;
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "BGA", NK_TEXT_LEFT);
    import_picked(*action, *view, *state);
    toolbar(context, *view, *state, *action);
    nk_layout_row_dynamic(context, 25, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state->bga_search.data(),
        static_cast<int>(state->bga_search.size()), nk_filter_default);
    const auto region = nk_window_get_content_region(context);
    const auto height = std::max(60.0f, region.y + region.h - nk_widget_position(context).y - 5);
    nk_layout_row_dynamic(context, height, 1);
    struct nk_list_view list{};
    const auto matching = view->bga_search && std::strcmp(view->bga_search, state->bga_search.data()) == 0;
    if (nk_list_view_begin(context, &list, "Compose BGA list", NK_WINDOW_BORDER, 36,
                          static_cast<int>(std::max<std::size_t>(view->bga_total_matching, 1)))) {
        state->bga_first_row = static_cast<std::size_t>(list.begin);
        state->bga_row_limit = static_cast<std::size_t>(std::max(1, list.end - list.begin + 1));
        if (matching) {
            worm_ui_keysound_prune(state->bga_selection, {view->bga_matching_ids, view->bga_total_matching});
            for (auto index = list.begin; index < std::min(list.end, static_cast<int>(view->bga_total_matching)); ++index)
                row(context, *view, *state, static_cast<std::size_t>(index));
            if (!view->bga_total_matching) { nk_layout_row_dynamic(context, 36, 1); nk_label(context, "No BGA matches. Add or drop images here.", NK_TEXT_LEFT); }
        }
        nk_list_view_end(&list);
    }
}

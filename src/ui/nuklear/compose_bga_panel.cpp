#include "ui/nuklear/compose_bga_panel.h"
#include "ui/nuklear/tooltip.h"
#include "wormedit/core/document.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/popup_lifecycle.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/vector_icons.h"
#include <SDL3/SDL.h>
#include <nuklear.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <span>
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
auto grouped_count(std::size_t value) -> std::string {
    auto text = std::to_string(value);
    for (auto position = text.size(); position > 3; position -= 3)
        text.insert(position - 3, ",");
    return text;
}
struct Picker { std::mutex mutex; bool open{}; std::vector<std::string> paths; };
Picker picker;
auto advance_eligible(const WormUiComposeView& view, std::uint64_t resource_id) -> bool {
    return view.bga_advance_ids &&
           std::ranges::find(view.bga_advance_ids,
                             view.bga_advance_ids + view.bga_advance_count,
                             resource_id) != view.bga_advance_ids + view.bga_advance_count;
}
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
    IconButton::layout_columns(context, view.bga_ids_available ? 8 : 6);
    nk_layout_row_template_end(context);
    const auto active = state.resource_pointer_mode == worm_ui_nuklear_compose_resource_pointer_mode_t::place &&
        state.resource_filter == worm_ui_nuklear_compose_resource_filter_t::bga;
    const auto can_paint = state.armed_resource_id &&
        state.armed_resource_id == state.bga_selection.primary &&
        advance_eligible(view, state.armed_resource_id) &&
        !view.note_paint_mode_active && !view.semantic_layer_paint_mode_active && !view.timing_focus;
    if (!can_paint && !active) nk_widget_disable_begin(context);
    if (IconButton{Glyph::paintbrush, "Paint selected BGA on the chart", active}.render(context) && (can_paint || active)) {
        state.resource_pointer_mode = active ? worm_ui_nuklear_compose_resource_pointer_mode_t::off
                                            : worm_ui_nuklear_compose_resource_pointer_mode_t::place;
        state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::bga;
        state.armed_from_sounds_dock = false;
        state.armed_from_bga_dock = !active && can_paint;
    }
    if (!can_paint && !active) nk_widget_disable_end(context);
    if (IconButton{Glyph::chevron_right, "Advance after place: arm the next available BGA shown",
                   state.bga_auto_increment_on_place}.render(context))
        state.bga_auto_increment_on_place = !state.bga_auto_increment_on_place;
    if (DrawnIconButton{icons::plus, "Add BGA images (or drop images here)"}.render(context)) open_picker();
    if (DrawnIconButton{icons::trash, "Remove unused BGA resources"}.render(context))
        (void)raise(action, view, WORM_UI_COMPOSE_ACTION_REMOVE_UNUSED_BGA);
    if (DrawnIconButton{icons::select_all, "Select every BGA shown"}.render(context))
        worm_ui_keysound_select_all(state.bga_selection, {view.bga_matching_ids, view.bga_total_matching});
    if (IconButton{Glyph::x, "Clear BGA selection"}.render(context)) worm_ui_keysound_clear(state.bga_selection);
    if (view.bga_ids_available) {
        if (IconButton{Glyph::chevrons_left, state.bga_sort_by_id ? "Sort by name" : "Sort by BMS ID",
                       state.bga_sort_by_id}.render(context)) state.bga_sort_by_id = !state.bga_sort_by_id;
        if (IconButton{Glyph::rotate_ccw, "Renumber BMS IDs by name"}.render(context))
        {
            state.bga_renumber_confirmation_open = true;
            state.bga_renumber_project = view.project_id;
            state.bga_renumber_chart = view.chart_id;
            state.bga_renumber_revision = view.document_revision;
            state.bga_renumber_resources = state.bga_selection.selected;
        }
    }
}
void raise_bga_renumber(const WormUiComposeView& view, worm_ui_nuklear_compose_state_t& state,
                        WormUiWorkspaceAction& action, bool all) {
    if (!raise(action, view, WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS)) return;
    state.bga_staged_ids = all ? std::vector<std::uint64_t>{}
                               : state.bga_renumber_resources;
    action.compose_sound_resource_ids = state.bga_staged_ids.data();
    action.compose_sound_resource_count = state.bga_staged_ids.size();
    action.compose_renumber_all = all;
    action.compose_renumber_bga = true;
    state.bga_renumber_confirmation_open = false;
    state.bga_renumber_release_pending = true;
    state.bga_renumber_resources.clear();
}
void renumber_confirmation(nk_context* context, const WormUiComposeView& view,
                           worm_ui_nuklear_compose_state_t& state,
                           WormUiWorkspaceAction& action) {
    if (!state.bga_renumber_confirmation_open) return;
    const auto bounds = nk_rect(0, 60, std::min(360.0f, nk_window_get_content_region(context).w), 204);
    if (!view.bga_ids_available || state.bga_renumber_project != view.project_id ||
        state.bga_renumber_chart != view.chart_id ||
        state.bga_renumber_revision != view.document_revision) {
        state.bga_renumber_confirmation_open = false;
        state.bga_renumber_release_pending = true;
        state.bga_renumber_resources.clear();
        wormedit::ui::chrome::dismiss_popup(context, "Renumber BMP IDs", bounds);
        return;
    }
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "Renumber BMP IDs",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR,
                        bounds)) {
        state.bga_renumber_confirmation_open = false;
        state.bga_renumber_release_pending = true;
        state.bga_renumber_resources.clear();
        return;
    }
    nk_layout_row_dynamic(context, 66, 1);
    nk_label_wrap(context, "Renumber BMP IDs in natural name order? Choose selected images or all BGA.");
    nk_layout_row_dynamic(context, 28, 2);
    if (!state.bga_renumber_resources.empty() && nk_button_label(context, "Selected"))
        raise_bga_renumber(view, state, action, false);
    if (nk_button_label(context, "All BGA")) raise_bga_renumber(view, state, action, true);
    if (nk_button_label(context, "Cancel")) {
        state.bga_renumber_confirmation_open = false;
        state.bga_renumber_release_pending = true;
        state.bga_renumber_resources.clear();
    }
    if (!state.bga_renumber_confirmation_open) nk_popup_close(context);
    nk_popup_end(context);
}
void row(nk_context* context, const WormUiComposeView& view, worm_ui_nuklear_compose_state_t& state,
          std::size_t index) {
    nk_layout_row_dynamic(context, 36, 1);
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT) && !view.input_occluded &&
                         !state.bga_renumber_confirmation_open && !state.bga_renumber_release_pending;
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
    const auto slot = view.bga_ids_available ? value.slot : "";
    std::snprintf(label, sizeof(label), "%s%s%s  · %u%s", slot,
                  view.bga_ids_available ? "  " : "", value.name, value.use_count,
                  value.missing ? " · missing" : "");
    nk_draw_text(canvas, nk_rect(bounds.x + 40, bounds.y + 8, bounds.w - 42, 20), label,
        static_cast<int>(std::strlen(label)), context->style.font, nk_rgba(0, 0, 0, 0), nk_rgb(222, 228, 238));
    if (hovered) wormedit::ui::nuklear::tooltip::text(context, label);
    if (!clicked) return;
    const auto modifiers = SDL_GetModState();
    worm_ui_keysound_click(state.bga_selection, {view.bga_matching_ids, view.bga_total_matching}, index,
        (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, (modifiers & SDL_KMOD_SHIFT) != 0);
    const auto eligible = advance_eligible(view, state.bga_selection.primary);
    state.armed_resource_id = eligible ? state.bga_selection.primary : 0;
    state.resource_filter = worm_ui_nuklear_compose_resource_filter_t::bga;
    state.armed_from_sounds_dock = false;
    state.armed_from_bga_dock = eligible;
    if (!eligible) state.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
}

void sync_advance_ids(const WormUiComposeView& view, worm_ui_nuklear_compose_state_t& state) {
    if (view.bga_advance_count && view.bga_advance_ids)
        state.bga_advance_ids.assign(view.bga_advance_ids,
                                     view.bga_advance_ids + view.bga_advance_count);
    else
        state.bga_advance_ids.clear();
}

void reveal_armed_bga(nk_context* context, const WormUiComposeView& view,
                      worm_ui_nuklear_compose_state_t& state) {
    if (!state.bga_reveal_primary) return;
    const auto matches = std::span(view.bga_matching_ids, view.bga_total_matching);
    const auto found = std::ranges::find(matches, state.bga_selection.primary);
    if (found != matches.end()) {
        const auto index = static_cast<std::size_t>(found - matches.begin());
        const auto y = index * static_cast<std::size_t>(36.0f + context->style.window.spacing.y);
        nk_group_set_scroll(context, "Compose BGA list", 0, static_cast<nk_uint>(y));
    }
    state.bga_reveal_primary = false;
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
        state->bga_advance_ids.clear(); state->armed_from_bga_dock = false;
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
    sync_advance_ids(*view, *state);
    toolbar(context, *view, *state, *action);
    nk_layout_row_dynamic(context, 25, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state->bga_search.data(),
        static_cast<int>(state->bga_search.size()), nk_filter_default);
    const auto region = nk_window_get_content_region(context);
    const auto height = std::max(60.0f, region.y + region.h - nk_widget_position(context).y -
        (view->bga_ids_available ? 45.0f : 5.0f));
    const auto block_list_pointer = state->bga_renumber_confirmation_open ||
                                    state->bga_renumber_release_pending;
    if (block_list_pointer) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, height, 1);
    reveal_armed_bga(context, *view, *state);
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
    if (block_list_pointer) nk_widget_disable_end(context);
    if (view->bga_ids_available) {
        nk_layout_row_dynamic(context, 20, 1);
        char capacity[64];
        const auto used = grouped_count(view->bga_ids_used);
        const auto total = grouped_count(view->bga_ids_total);
        std::snprintf(capacity, sizeof(capacity), "%s / %s IDs", used.c_str(), total.c_str());
        const auto amber = view->bga_ids_total != 0 && view->bga_ids_used * 10 > view->bga_ids_total * 9;
        nk_label_colored(context, capacity, NK_TEXT_LEFT,
                         amber ? nk_rgb(226, 166, 64) : context->style.text.color);
        nk_layout_row_dynamic(context, 20, 1);
        char free_run[64];
        std::snprintf(free_run, sizeof(free_run), "Largest free run: %zu",
                      view->bga_largest_free_run);
        nk_label(context, free_run, NK_TEXT_LEFT);
    }
    renumber_confirmation(context, *view, *state, *action);
    if (state->bga_renumber_release_pending &&
        !context->input.mouse.buttons[NK_BUTTON_LEFT].down)
        state->bga_renumber_release_pending = false;
}

static bool bga_stack_is_valid(const WormUiComposeView& view,
                               const worm_ui_nuklear_compose_state_t& state) {
    const auto expected = wormedit::ui::chrome::PopupContextStamp{
        state.bga_stack_project_id, state.bga_stack_chart_id, state.bga_stack_revision, 0};
    const auto current = wormedit::ui::chrome::PopupContextStamp{
        view.project_id, view.chart_id, view.document_revision, 0};
    return view.scene_available && view.bga_lane_visible &&
        wormedit::ui::chrome::popup_context_matches(expected, current) &&
        std::ranges::any_of(std::span(view.bga_events, view.bga_event_count), [&](const auto& event) {
            return event.canvas_row == state.bga_stack_row &&
                   event.overlap_count > WORM_UI_COMPOSE_BGA_OVERLAP_STACK_THRESHOLD;
        });
}

static bool draw_bga_stack_row(nk_context* context, const WormUiComposeBgaEventView& event,
                               std::uint64_t chart_id, std::uint64_t revision,
                               WormUiWorkspaceAction& action,
                               worm_ui_nuklear_compose_state_t& state) {
    char label[256];
    const auto kind = event.kind == static_cast<std::uint8_t>(wormedit::visual_event_kind_t::video)
        ? "VIDEO" : "BACKGROUND";
    std::snprintf(label, sizeof(label), "#%llu · %s · %s · %s",
        static_cast<unsigned long long>(event.event_id),
        kind,
        event.bmp_id && event.bmp_id[0] ? event.bmp_id : "No BMP ID",
        event.name ? event.name : "Missing resource");
    const auto clicked = nk_button_label(context, label);
    if (nk_widget_is_hovered(context)) wormedit::ui::nuklear::tooltip::text(context, label);
    if (!clicked) return false;
    if (action.compose_input_count < WORM_UI_COMPOSE_INPUT_ACTION_CAPACITY) {
        auto& input = action.compose_inputs[action.compose_input_count++];
        input = {};
        input.kind = WORM_UI_COMPOSE_INPUT_BGA_EVENT_CLICK;
        input.object_id = event.event_id;
        input.timing_chart_id = chart_id;
        input.timing_document_revision = revision;
    }
    state.bga_stack_open = false;
    return true;
}

void worm_ui_nuklear_render_bga_stack_picker(nk_context* context, const WormUiComposeView* view,
    WormUiWorkspaceAction* action, worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) return;
    if (wormedit::ui::chrome::popup_update_release_pending(
            context, state->bga_stack_release_pending)) return;
    if (!state->bga_stack_open) return;
    const auto local_bounds = nk_rect(24, 48, 300, 360);
    const auto screen_bounds = wormedit::ui::chrome::popup_screen_bounds(context, local_bounds);
    if (!bga_stack_is_valid(*view, *state)) {
        state->bga_stack_open = false;
        wormedit::ui::chrome::popup_arm_release(state->bga_stack_release_pending);
        wormedit::ui::chrome::dismiss_popup(context, "BGA events at measure", local_bounds);
        return;
    }
    if (wormedit::ui::chrome::popup_dismiss_requested(context, screen_bounds)) {
        state->bga_stack_open = false;
        wormedit::ui::chrome::popup_arm_release(state->bga_stack_release_pending);
        wormedit::ui::chrome::dismiss_popup(context, "BGA events at measure", local_bounds);
        return;
    }
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "BGA events at measure",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR,
                        local_bounds)) {
        state->bga_stack_open = false;
        wormedit::ui::chrome::popup_arm_release(state->bga_stack_release_pending);
        return;
    }
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Choose the event to select or edit", NK_TEXT_LEFT);
    struct nk_list_view list{};
    const auto count = static_cast<int>(std::ranges::count_if(
        std::span(view->bga_events, view->bga_event_count), [&](const auto& event) {
            return event.canvas_row == state->bga_stack_row;
        }));
    nk_layout_row_dynamic(context, 300, 1);
    bool selected_event = false;
    if (nk_list_view_begin(context, &list, "BGA overlap events", NK_WINDOW_BORDER, 30,
                           std::max(1, count))) {
        const auto events = std::span(view->bga_events, view->bga_event_count);
        int row_index = 0;
        for (const auto& event : events) {
            if (event.canvas_row != state->bga_stack_row) continue;
            if (row_index >= list.begin && row_index < list.end) {
                nk_layout_row_dynamic(context, 28, 1);
                selected_event = draw_bga_stack_row(context, event, view->chart_id,
                    view->document_revision, *action, *state) || selected_event;
            }
            ++row_index;
        }
        nk_list_view_end(&list);
    }
    if (selected_event) {
        nk_popup_close(context);
        wormedit::ui::chrome::popup_arm_release(state->bga_stack_release_pending);
    }
    nk_layout_row_dynamic(context, 28, 1);
    if (nk_button_label(context, "Close")) {
        state->bga_stack_open = false;
        nk_popup_close(context);
        wormedit::ui::chrome::popup_arm_release(state->bga_stack_release_pending);
    }
    nk_popup_end(context);
}

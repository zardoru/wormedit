#include "ui/nuklear/catalog_chrome.h"
#include "ui/nuklear/content_layout.h"
#include "ui/nuklear/compose_bga_panel.h"
#include "ui/nuklear/catalog_confirmation.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/compose_action_stamp.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/compose_resources.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/popup_lifecycle.h"
#include "ui/nuklear/theme.h"
#include "ui/nuklear/tooltip.h"
#include "ui/nuklear/vector_icons.h"
#include "wormedit/core/document.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <nuklear.h>
#include <span>
#include <string>
#include <unordered_map>
#include <wormedit/core/vocabulary.h>
namespace {
using wormedit::ui::chrome::IconButton;
using wormedit::ui::chrome::DrawnIconButton;
using wormedit::ui::lucide::Glyph;
using wormedit::ui::theme::UiColor;
namespace icons = wormedit::ui::vector_icons;
struct Texture { SDL_Texture* value{}; std::uint64_t content{}, project{}; };
SDL_Renderer* renderer{};
std::unordered_map<std::uint64_t, Texture> textures;

struct Picker { std::mutex mutex; bool open{}; wormedit::ui::chrome::PopupContextStamp origin; std::vector<std::string> paths; };
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
void open_picker(const WormUiComposeView& view) {
    { std::lock_guard lock{picker.mutex};
      if (picker.open) return;
      picker.open = true;
      picker.origin = wormedit::ui::chrome::PopupContextStamp::from(view);
      picker.paths.clear(); }
    static const SDL_DialogFileFilter filter{"Images", "png;jpg;jpeg;bmp;gif;tga;ppm"};
    SDL_ShowOpenFileDialog(picked, nullptr, SDL_GetKeyboardFocus(), &filter, 1, nullptr, true);
}
auto raise(WormUiWorkspaceAction& action, const WormUiComposeView& view,
           WormUiComposeActionKind kind) -> bool {
    return wormedit::ui::raise_compose_action(action, view, kind);
}
void import_picked(WormUiWorkspaceAction& action, const WormUiComposeView& view,
                   worm_ui_nuklear_compose_state_t& state) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    wormedit::ui::chrome::PopupContextStamp origin;
    { std::lock_guard lock{picker.mutex};
      if (picker.paths.empty()) return;
      state.bga_catalog.bga_staged_paths = std::move(picker.paths); picker.paths.clear();
      origin = picker.origin; }
    if (!wormedit::ui::chrome::popup_context_matches(origin,
            wormedit::ui::chrome::PopupContextStamp::from(view))) {
        state.bga_catalog.bga_staged_paths.clear();
        return;
    }
    state.bga_catalog.bga_staged_path_pointers.clear();
    for (const auto& path : state.bga_catalog.bga_staged_paths) state.bga_catalog.bga_staged_path_pointers.push_back(path.c_str());
    if (!raise(action, view, WORM_UI_COMPOSE_ACTION_IMPORT_BGA_FILES)) return;
    action.compose_import_paths = state.bga_catalog.bga_staged_path_pointers.data();
    action.compose_import_path_count = state.bga_catalog.bga_staged_path_pointers.size();
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
void reveal_bga_lane(WormUiWorkspaceAction& action, std::uint8_t kind) {
    append_compose_input(&action, WORM_UI_COMPOSE_INPUT_BGA_LANE_REVEAL);
    if (action.compose_input_count == 0) return;
    auto& output = action.compose_inputs[action.compose_input_count - 1];
    output.bga_lane_kind = kind;
    output.bga_lane_kind_valid = true;
}

void reveal_empty_base_lane(WormUiWorkspaceAction& action, const WormUiComposeView& view, bool was_active) {
    if (!was_active && view.bga_lane_count == 0) reveal_bga_lane(action, 0);
}

void bga_lane_picker(nk_context* context, const WormUiComposeView& view,
                     WormUiWorkspaceAction& action, bool blocked) {
    if (blocked) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Show BGA lanes", NK_TEXT_LEFT);
    constexpr std::array labels{"BGA", "Layer", "Layer 2", "Poor (miss)"};
    constexpr std::array kinds{wormedit::visual_event_kind_t::background, wormedit::visual_event_kind_t::layer,
        wormedit::visual_event_kind_t::layer2, wormedit::visual_event_kind_t::poor};
    constexpr std::array descriptions{
        "Show the background image/video lane",
        "Show the first overlay lane",
        "Show the second overlay lane (BMS channel 0A)",
        "Show the miss/Poor image lane; normal preview keeps its miss-only policy"};
    nk_layout_row_dynamic(context, 25, 2);
    for (size_t index = 0; index < labels.size(); ++index) {
        const auto kind = static_cast<std::uint8_t>(kinds[index]);
        const auto count = std::min<std::size_t>(view.bga_lane_count, 4);
        const auto visible = std::ranges::find(std::span(view.bga_lane_kinds, count), kind) !=
                             std::span(view.bga_lane_kinds, count).end();
        if (nk_select_label(context, labels[index], NK_TEXT_CENTERED, visible))
            reveal_bga_lane(action, kind);
        if (!blocked && nk_widget_is_hovered(context))
            wormedit::ui::nuklear::tooltip::text(context, descriptions[index]);
    }
    if (blocked) nk_widget_disable_end(context);
}

void toolbar(nk_context* context, const WormUiComposeView& view, worm_ui_nuklear_compose_state_t& state,
              WormUiWorkspaceAction& action) {
    nk_layout_row_template_begin(context, 27);
    IconButton::layout_columns(context, view.bga_ids_available ? 7 : 5);
    nk_layout_row_template_end(context);
    if (IconButton{Glyph::chevron_right, "Advance after place: arm the next available BGA shown",
                   state.bga_catalog.bga_auto_increment_on_place}.render(context))
        state.bga_catalog.bga_auto_increment_on_place = !state.bga_catalog.bga_auto_increment_on_place;
    if (DrawnIconButton{icons::plus, "Add BGA images (or drop images here)"}.render(context)) open_picker(view);
    if (DrawnIconButton{icons::trash, "Remove unused BGA resources"}.render(context))
        (void)raise(action, view, WORM_UI_COMPOSE_ACTION_REMOVE_UNUSED_BGA);
    if (DrawnIconButton{icons::select_all, "Select every BGA shown"}.render(context))
        worm_ui_keysound_select_all(state.bga_catalog.bga_selection, {view.bga_matching_ids, view.bga_total_matching});
    if (IconButton{Glyph::x, "Clear BGA selection"}.render(context)) worm_ui_keysound_clear(state.bga_catalog.bga_selection);
    if (view.bga_ids_available) {
        if (IconButton{Glyph::chevrons_left, state.bga_catalog.bga_sort_by_id ? "Sort by name" : "Sort by BMS ID",
                       state.bga_catalog.bga_sort_by_id}.render(context)) state.bga_catalog.bga_sort_by_id = !state.bga_catalog.bga_sort_by_id;
        if (IconButton{Glyph::rotate_ccw, "Renumber BMS IDs by name"}.render(context))
        {
            wormedit::ui::nuklear::open_catalog_confirmation(
                wormedit::ui::nuklear::catalog_confirmation(state, true), view, state.bga_catalog.bga_selection.selected);
        }
    }
}
void raise_bga_renumber(const WormUiComposeView& view, worm_ui_nuklear_compose_state_t& state,
                        WormUiWorkspaceAction& action, bool all) {
    if (!raise(action, view, WORM_UI_COMPOSE_ACTION_RENUMBER_BMS_IDS)) return;
    state.bga_catalog.bga_staged_ids = all ? std::vector<std::uint64_t>{}
                               : state.bga_catalog.bga_renumber_resources;
    action.compose_sound_resource_ids = state.bga_catalog.bga_staged_ids.data();
    action.compose_sound_resource_count = state.bga_catalog.bga_staged_ids.size();
    action.compose_renumber_all = all;
    action.compose_renumber_bga = true;
    wormedit::ui::nuklear::close_catalog_confirmation(
        wormedit::ui::nuklear::catalog_confirmation(state, true));
}
void renumber_confirmation(nk_context* context, const WormUiComposeView& view,
                           worm_ui_nuklear_compose_state_t& state, WormUiWorkspaceAction& action) {
    const auto choice = wormedit::ui::nuklear::render_catalog_confirmation(context, view,
        wormedit::ui::nuklear::catalog_confirmation(state, true), true);
    if (choice) raise_bga_renumber(view, state, action, *choice);
}

constexpr float bga_row_height = 22.0f;

void draw_bga_resource_row(nk_context* context, const WormUiComposeView& view,
                           const WormUiComposeBgaRowView& value, struct nk_rect bounds,
                           bool selected, bool hovered) {
    auto* canvas = nk_window_get_canvas(context);
    if (selected || hovered) nk_fill_rect(canvas, bounds, 3, wormedit::ui::chrome::color(UiColor::selected));
    const auto tint = wormedit::ui::chrome::color(value.missing ? UiColor::text_muted : UiColor::text);
    const auto clear = nk_rgba(0, 0, 0, 0);
    const auto text = [&](float x, float width, const char* label) {
        nk_draw_text(canvas, nk_rect(bounds.x + x, bounds.y + 3, width, bounds.h - 6),
            label, static_cast<int>(std::strlen(label)), context->style.font, clear, tint);
    };
    text(4, 30, view.bga_ids_available && value.slot ? value.slot : "");
    const auto image_bounds = nk_rect(bounds.x + 40, bounds.y + 2, 18, 18);
    if (auto* image = thumbnail(view, value)) {
        const auto handle = nk_image_ptr(image);
        nk_draw_image(canvas, image_bounds, &handle, nk_rgb(255, 255, 255));
    } else nk_fill_rect(canvas, image_bounds, 2, nk_rgb(45, 48, 59));
    text(64, std::max(12.0f, bounds.w - 158), value.name);
    char uses[24];
    std::snprintf(uses, sizeof(uses), "%llu", static_cast<unsigned long long>(value.use_count));
    text(bounds.w - 86, 32, uses);
    text(bounds.w - 48, 48, value.missing ? "missing" : value.use_count ? "ok" : "unused");
}

void select_bga_row(nk_context* context, const WormUiComposeView& view,
                     worm_ui_nuklear_compose_state_t& state, WormUiWorkspaceAction& action,
                     std::size_t index, const WormUiComposeBgaRowView& value) {
    const auto modifiers = SDL_GetModState();
    worm_ui_keysound_click(state.bga_catalog.bga_selection, {view.bga_matching_ids, view.bga_total_matching}, index,
        (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, (modifiers & SDL_KMOD_SHIFT) != 0);
    if (!nk_input_is_mouse_released(&context->input, NK_BUTTON_DOUBLE) || value.missing ||
        !advance_eligible(view, state.bga_catalog.bga_selection.primary)) return;
    set_armed_resource(state, state.bga_catalog.bga_selection.primary, compose_resource_origin_t::bga);
    worm_ui_nuklear_pick_draw_source(state, action, 2, state.bga_catalog.bga_selection.primary);
    reveal_empty_base_lane(action, view, false);
}

void row(nk_context* context, const WormUiComposeView& view, worm_ui_nuklear_compose_state_t& state,
         WormUiWorkspaceAction& action, std::size_t index) {
    if (index < view.bga_first_row || index - view.bga_first_row >= view.bga_row_count) return;
    const auto& value = view.bga_rows[index - view.bga_first_row];
    const auto width = nk_window_get_content_region(context).w;
    nk_layout_row_begin(context, NK_STATIC, bga_row_height, 2);
    nk_layout_row_push(context, std::max(32.0f, width - 22 - context->style.window.spacing.x));
    const bool available = !view.input_occluded && !state.bga_catalog.bga_renumber_confirmation_open &&
                           !state.bga_catalog.bga_renumber_release_pending;
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT) && available;
    struct nk_rect bounds{};
    const auto widget = nk_widget(&bounds, context);
    if (widget != NK_WIDGET_INVALID) {
        draw_bga_resource_row(context, view, value, bounds,
            worm_ui_keysound_is_selected(state.bga_catalog.bga_selection, value.resource_id), hovered);
        if (hovered && available) {
            const auto tip = std::string{value.source_path ? value.source_path : value.name} + "\n" +
                std::to_string(value.use_count) + " stored references. " +
                (value.missing ? "File missing; cannot arm." : "Double-click to arm.");
            wormedit::ui::nuklear::tooltip::text(context, tip);
        }
        if (clicked) select_bga_row(context, view, state, action, index, value);
    }
    nk_layout_row_push(context, 22);
    const auto tip = value.use_count == 0 ? std::string{"Delete unused resource; keep its file"}
        : "Cannot delete: " + std::to_string(value.use_count) + " stored references";
    if (!available || value.use_count != 0) nk_widget_disable_begin(context);
    const bool deleted = DrawnIconButton{icons::trash, available ? tip.c_str() : nullptr}.render(context);
    if (!available || value.use_count != 0) nk_widget_disable_end(context);
    if (deleted && available && value.use_count == 0 && raise(action, view, WORM_UI_COMPOSE_ACTION_DELETE_SOUNDS)) {
        state.bga_catalog.bga_staged_ids = {value.resource_id};
        action.compose_sound_resource_ids = state.bga_catalog.bga_staged_ids.data();
        action.compose_sound_resource_count = 1;
    }
    nk_layout_row_end(context);
}

void bga_use_filter(nk_context* context, worm_ui_nuklear_compose_state_t& state, bool blocked) {
    const auto chosen = wormedit::ui::nuklear::render_catalog_use_filter(context,
        state.bga_catalog.bga_use_filter, blocked);
    if (chosen) {
        state.bga_catalog.bga_use_filter = *chosen;
        state.bga_catalog.bga_first_row = 0;
        nk_group_set_scroll(context, "Compose BGA list", 0, 0);
    }
}

void sync_advance_ids(const WormUiComposeView& view, worm_ui_nuklear_compose_state_t& state) {
    if (view.bga_advance_count && view.bga_advance_ids)
        state.bga_catalog.bga_advance_ids.assign(view.bga_advance_ids,
                                     view.bga_advance_ids + view.bga_advance_count);
    else
        state.bga_catalog.bga_advance_ids.clear();
}

void reveal_armed_bga(nk_context* context, const WormUiComposeView& view,
                      worm_ui_nuklear_compose_state_t& state) {
    if (!state.bga_catalog.bga_reveal_primary) return;
    if (!view.bga_search || std::strcmp(view.bga_search, state.bga_catalog.bga_search.data()) != 0) return;
    const auto matches = std::span(view.bga_matching_ids, view.bga_total_matching);
    const auto found = std::ranges::find(matches, state.bga_catalog.bga_selection.primary);
    if (found != matches.end()) {
        const auto index = static_cast<std::size_t>(found - matches.begin());
        const auto y = index * static_cast<std::size_t>(bga_row_height + context->style.window.spacing.y);
        nk_group_set_scroll(context, "Compose BGA list", 0, static_cast<nk_uint>(y));
        if (state.pointer_capture.source_chip_reveal_kind == 2) {
            if (index < view.bga_first_row || index - view.bga_first_row >= view.bga_row_count) return;
            if (view.bga_rows[index - view.bga_first_row].resource_id != state.pointer_capture.source_chip_reveal_id) return;
            state.pointer_capture.source_chip_reveal_kind = 0;
            state.pointer_capture.source_chip_reveal_id = 0;
        }
    }
    if (found != matches.end()) state.bga_catalog.bga_reveal_primary = false;
}

void restore_chip_bga_selection(const WormUiComposeView& view,
                               worm_ui_nuklear_compose_state_t& state) {
    if (state.pointer_capture.source_chip_reveal_kind != 2) return;
    if (state.pointer_capture.chip_reveal_project != view.project_id || state.pointer_capture.chip_reveal_chart != view.chart_id ||
        view.draw_source_kind != 2 || view.draw_source_id != state.pointer_capture.source_chip_reveal_id || view.draw_source_missing) {
        state.pointer_capture.source_chip_reveal_kind = 0;
        state.pointer_capture.source_chip_reveal_id = 0;
        state.bga_catalog.bga_reveal_primary = false;
        return;
    }
    state.bga_catalog.bga_selection.primary = state.pointer_capture.source_chip_reveal_id;
    state.bga_catalog.bga_selection.selected = {state.pointer_capture.source_chip_reveal_id};
    state.bga_catalog.bga_reveal_primary = true;
}
}
void worm_ui_nuklear_bga_set_renderer(SDL_Renderer* value) {
    for (const auto& [id, texture] : textures) if (texture.value) SDL_DestroyTexture(texture.value);
    textures.clear(); renderer = value;
}
static void sync_bga_document(const WormUiComposeView* view,
                              worm_ui_nuklear_compose_state_t* state, WormUiWorkspaceAction* action) {
    if (state->bga_catalog.bga_project_id != view->project_id || state->bga_catalog.bga_chart_id != view->chart_id) {
        state->bga_catalog.bga_selection = {}; state->bga_catalog.bga_search.fill(0); state->bga_catalog.bga_first_row = 0;
        state->bga_catalog.bga_advance_ids.clear();
        state->bga_catalog.bga_project_id = view->project_id; state->bga_catalog.bga_chart_id = view->chart_id;
        state->bga_catalog.bga_revision = view->document_revision;
        state->bga_catalog.bga_use_filter = 0;
        worm_ui_nuklear_bga_set_renderer(renderer);
    }
    if (state->bga_catalog.bga_revision != view->document_revision) {
        state->bga_catalog.bga_revision = view->document_revision;
        const auto all = std::span(view->bga_all_resource_ids, view->bga_all_resource_count);
        worm_ui_keysound_prune(state->bga_catalog.bga_selection, all);
        if (state->bga_catalog.armed_from_bga_dock && std::ranges::find(all, state->resource_picker.armed_resource_id) == all.end())
            worm_ui_nuklear_pick_draw_source(*state, *action, 0, 0);
    }
}

void worm_ui_nuklear_render_compose_bga(nk_context* context, const WormUiComposeView* view,
    WormUiWorkspaceAction* action, worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) return;
    sync_bga_document(view, state, action);
    const auto bounds = nk_window_get_content_region(context);
    restore_chip_bga_selection(*view, *state);
    state->bga_catalog.bga_list_valid = true;
    state->bga_catalog.bga_list_x = bounds.x; state->bga_catalog.bga_list_y = bounds.y;
    state->bga_catalog.bga_list_width = bounds.w; state->bga_catalog.bga_list_height = bounds.h;
    state->bga_catalog.bga_shown.assign(view->bga_matching_ids, view->bga_matching_ids + view->bga_total_matching);
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "BGA", NK_TEXT_LEFT);
    import_picked(*action, *view, *state);
    sync_advance_ids(*view, *state);
    toolbar(context, *view, *state, *action);
    bga_lane_picker(context, *view, *action, state->bga_catalog.bga_renumber_confirmation_open ||
                    state->bga_catalog.bga_renumber_release_pending);
    nk_layout_row_dynamic(context, 25, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state->bga_catalog.bga_search.data(),
        static_cast<int>(state->bga_catalog.bga_search.size()), nk_filter_default);
    bga_use_filter(context, *state, view->input_occluded || state->bga_catalog.bga_renumber_confirmation_open ||
                   state->bga_catalog.bga_renumber_release_pending);
    wormedit::ui::nuklear::render_catalog_header(context,
        wormedit::ui::nuklear::catalog_layout_t::bga);
    const auto height = wormedit::ui::layout::remaining_content_height(context,
        {.minimum = 60.0f, .reserved = view->bga_ids_available ? 45.0f : 5.0f});
    const auto block_list_pointer = state->bga_catalog.bga_renumber_confirmation_open ||
                                    state->bga_catalog.bga_renumber_release_pending;
    if (block_list_pointer) nk_widget_disable_begin(context);
    nk_layout_row_dynamic(context, height, 1);
    reveal_armed_bga(context, *view, *state);
    struct nk_list_view list{};
    const auto matching = view->bga_search && std::strcmp(view->bga_search, state->bga_catalog.bga_search.data()) == 0;
    if (nk_list_view_begin(context, &list, "Compose BGA list", NK_WINDOW_BORDER, static_cast<int>(bga_row_height),
                          static_cast<int>(std::max<std::size_t>(view->bga_total_matching, 1)))) {
        state->bga_catalog.bga_first_row = static_cast<std::size_t>(list.begin);
        state->bga_catalog.bga_row_limit = static_cast<std::size_t>(std::max(1, list.end - list.begin + 1));
        if (matching) {
            for (auto index = list.begin; index < std::min(list.end, static_cast<int>(view->bga_total_matching)); ++index)
                row(context, *view, *state, *action, static_cast<std::size_t>(index));
            if (!view->bga_total_matching) { nk_layout_row_dynamic(context, 36, 1); nk_label(context, "No BGA matches. Add or drop images here.", NK_TEXT_LEFT); }
        }
        nk_list_view_end(&list);
    }
    if (block_list_pointer) nk_widget_disable_end(context);
    if (view->bga_ids_available)
        wormedit::ui::nuklear::render_catalog_capacity(context,
            {.used = view->bga_ids_used, .total = view->bga_ids_total,
             .largest_free_run = view->bga_largest_free_run});
    renumber_confirmation(context, *view, *state, *action);
}

static bool bga_stack_is_valid(const WormUiComposeView& view,
                               const worm_ui_nuklear_compose_state_t& state) {
    const auto expected = wormedit::ui::chrome::PopupContextStamp{
        state.bga_interaction.bga_stack_project_id, state.bga_interaction.bga_stack_chart_id, state.bga_interaction.bga_stack_revision, 0};
    const auto current = wormedit::ui::chrome::PopupContextStamp::from(view);
    return view.scene_available && view.bga_lane_visible &&
        wormedit::ui::chrome::popup_context_matches(expected, current) &&
        std::ranges::any_of(std::span(view.bga_events, view.bga_event_count), [&](const auto& event) {
            return event.canvas_row == state.bga_interaction.bga_stack_row &&
                   wormedit::editor_bga_lane_index(static_cast<wormedit::visual_event_kind_t>(event.kind)) ==
                   wormedit::editor_bga_lane_index(static_cast<wormedit::visual_event_kind_t>(state.bga_interaction.bga_stack_kind)) &&
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
    state.bga_interaction.bga_stack_open = false;
    return true;
}

void worm_ui_nuklear_render_bga_stack_picker(nk_context* context, const WormUiComposeView* view,
    WormUiWorkspaceAction* action, worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) return;
    if (wormedit::ui::chrome::popup_update_release_pending(
            context, state->bga_interaction.bga_stack_release_pending)) return;
    if (!state->bga_interaction.bga_stack_open) return;
    const auto local_bounds = nk_rect(24, 48, 300, 360);
    const auto screen_bounds = wormedit::ui::chrome::popup_screen_bounds(context, local_bounds);
    if (!bga_stack_is_valid(*view, *state)) {
        state->bga_interaction.bga_stack_open = false;
        wormedit::ui::chrome::popup_arm_release(state->bga_interaction.bga_stack_release_pending);
        wormedit::ui::chrome::dismiss_popup(context, "BGA events at measure", local_bounds);
        return;
    }
    if (wormedit::ui::chrome::popup_dismiss_requested(context, screen_bounds)) {
        state->bga_interaction.bga_stack_open = false;
        wormedit::ui::chrome::popup_arm_release(state->bga_interaction.bga_stack_release_pending);
        wormedit::ui::chrome::dismiss_popup(context, "BGA events at measure", local_bounds);
        return;
    }
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "BGA events at measure",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR,
                        local_bounds)) {
        state->bga_interaction.bga_stack_open = false;
        wormedit::ui::chrome::popup_arm_release(state->bga_interaction.bga_stack_release_pending);
        return;
    }
    nk_layout_row_dynamic(context, 24, 1);
    nk_label(context, "Choose the event to select or edit", NK_TEXT_LEFT);
    struct nk_list_view list{};
    const auto count = static_cast<int>(std::ranges::count_if(
        std::span(view->bga_events, view->bga_event_count), [&](const auto& event) {
            return event.canvas_row == state->bga_interaction.bga_stack_row &&
                wormedit::editor_bga_lane_index(static_cast<wormedit::visual_event_kind_t>(event.kind)) ==
                wormedit::editor_bga_lane_index(static_cast<wormedit::visual_event_kind_t>(state->bga_interaction.bga_stack_kind));
        }));
    nk_layout_row_dynamic(context, 300, 1);
    bool selected_event = false;
    if (nk_list_view_begin(context, &list, "BGA overlap events", NK_WINDOW_BORDER, 30,
                           std::max(1, count))) {
        const auto events = std::span(view->bga_events, view->bga_event_count);
        int row_index = 0;
        for (const auto& event : events) {
            if (event.canvas_row != state->bga_interaction.bga_stack_row ||
                wormedit::editor_bga_lane_index(static_cast<wormedit::visual_event_kind_t>(event.kind)) !=
                wormedit::editor_bga_lane_index(static_cast<wormedit::visual_event_kind_t>(state->bga_interaction.bga_stack_kind))) continue;
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
        wormedit::ui::chrome::popup_arm_release(state->bga_interaction.bga_stack_release_pending);
    }
    nk_layout_row_dynamic(context, 28, 1);
    if (nk_button_label(context, "Close")) {
        state->bga_interaction.bga_stack_open = false;
        nk_popup_close(context);
        wormedit::ui::chrome::popup_arm_release(state->bga_interaction.bga_stack_release_pending);
    }
    nk_popup_end(context);
}

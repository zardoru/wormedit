#include "ui/nuklear/compose_recipe_menu.h"
#include "ui/nuklear/compose_canvas.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/popup_lifecycle.h"
#include "wormedit/nuklear_config.h"

#include <algorithm>

namespace {
bool menu_open{};
bool release_pending{};
bool opened_this_frame{};
wormedit::ui::chrome::PopupContextStamp menu_context{};
std::uint64_t menu_recipe{};
struct nk_rect menu_bounds{};

void close_menu(nk_context* context, bool dismiss_existing_popup = true) {
    menu_open = false;
    wormedit::ui::chrome::popup_arm_release(release_pending);
    if (context && dismiss_existing_popup)
        wormedit::ui::chrome::dismiss_popup(context, "SV recipe actions", menu_bounds);
}

// Command ids carried in WormUiComposeInputAction::span_edge by RECIPE_COMMAND.
enum recipe_command_t : std::uint8_t { toggle_enabled = 0, materialize = 1, remove = 2 };

void queue_command(const WormUiComposeView& view, WormUiWorkspaceAction& action,
                   recipe_command_t command) {
    append_compose_input(&action, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 0.0, 0.0, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, menu_recipe, 0, false,
                         WORM_UI_TIMING_LANE_SCROLL, view.chart_id, view.document_revision);
    if (action.compose_input_count != 0)
        action.compose_inputs[action.compose_input_count - 1].span_edge = command;
    close_menu(nullptr, false);
}

void menu_entry(nk_context* context, const char* title, bool enabled, const char* reason,
                const WormUiComposeView& view, WormUiWorkspaceAction& action,
                recipe_command_t command) {
    nk_layout_row_dynamic(context, 24, 1);
    if (!enabled) nk_widget_disable_begin(context);
    if (nk_button_label(context, title)) {
        queue_command(view, action, command);
        nk_popup_close(context);
    }
    if (!enabled) {
        nk_widget_disable_end(context);
        nk_layout_row_dynamic(context, 32, 1);
        nk_label_wrap(context, reason);
    }
}

auto find_area(const WormUiComposeView& view, std::uint64_t id) -> const WormUiComposeRecipeAreaView* {
    for (size_t index = 0; index < view.recipe_area_count; ++index)
        if (view.recipe_areas[index].recipe_id == id) return &view.recipe_areas[index];
    return nullptr;
}

void render_menu(nk_context* context, const WormUiComposeView& view, WormUiWorkspaceAction& action) {
    if (!menu_open) return;
    const auto* area = find_area(view, menu_recipe);
    const auto current = wormedit::ui::chrome::PopupContextStamp{
        view.project_id, view.chart_id, view.document_revision, menu_recipe};
    const auto bounds = wormedit::ui::chrome::popup_screen_bounds(context, menu_bounds);
    if (!wormedit::ui::chrome::popup_context_matches(menu_context, current) || !area ||
        wormedit::ui::chrome::popup_dismiss_requested(context, bounds, !opened_this_frame)) {
        close_menu(context);
        opened_this_frame = false;
        return;
    }
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "SV recipe actions",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, menu_bounds)) {
        close_menu(context);
        return;
    }
    menu_entry(context, area->enabled ? "Disable recipe" : "Enable recipe", true, "", view, action,
               toggle_enabled);
    menu_entry(context, "Materialize into events", area->evaluable,
               "This recipe cannot be evaluated yet", view, action, materialize);
    menu_entry(context, "Delete recipe", true, "", view, action, remove);
    nk_popup_end(context);
}

// The recipe area under a screen point, using the same lane geometry the canvas draws with.
auto area_at(const WormUiComposeView& view, struct nk_rect viewport, float x, float y)
    -> const WormUiComposeRecipeAreaView* {
    const auto x_scale = viewport.w / static_cast<float>(view.viewport_width_pixels);
    const auto y_scale = viewport.h / static_cast<float>(view.viewport_height_pixels);
    const WormUiComposeRecipeAreaView* hit = nullptr;
    for (size_t index = 0; index < view.recipe_area_count; ++index) {
        const auto& area = view.recipe_areas[index];
        const auto extent = compose_timing_lane_inner_extent(
            view, static_cast<WormUiTimingLaneKind>(area.lane));
        if (!extent) continue;
        const auto left = viewport.x + static_cast<float>(extent->first) * x_scale;
        const auto right = viewport.x + static_cast<float>(extent->second) * x_scale;
        const auto top = viewport.y + static_cast<float>(area.bounds.top) * y_scale;
        const auto bottom = viewport.y + static_cast<float>(area.bounds.bottom) * y_scale;
        if (x >= left && x < right && y >= top && y <= bottom) hit = &area;
    }
    return hit;
}
} // namespace

auto compose_recipe_menu_open() -> bool {
    return menu_open;
}
auto compose_recipe_menu_consumed() -> bool {
    return menu_open || release_pending;
}

void reset_compose_recipe_menu() {
    menu_open = false;
    release_pending = false;
    opened_this_frame = false;
}

void process_compose_recipe_menu(nk_context* context, const WormUiComposeView& view,
                                 WormUiWorkspaceAction& action, struct nk_rect viewport,
                                 double input_right, bool focused) {
    opened_this_frame = false;
    (void)wormedit::ui::chrome::popup_update_release_pending(context, release_pending);
    if (!view.scene_available || view.viewport_width_pixels <= 0 || view.viewport_height_pixels <= 0 ||
        !focused) {
        if (menu_open) close_menu(context);
        return;
    }
    const auto& mouse = context->input.mouse;
    if (nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT) && mouse.pos.x < input_right) {
        if (const auto* area = area_at(view, viewport, mouse.pos.x, mouse.pos.y)) {
            menu_open = true;
            menu_context = {view.project_id, view.chart_id, view.document_revision,
                            area->recipe_id};
            menu_recipe = area->recipe_id;
            const auto content = nk_window_get_content_region(context);
            menu_bounds = nk_rect(
                std::clamp(mouse.pos.x - content.x, 0.0f, std::max(0.0f, content.w - 240)),
                std::clamp(mouse.pos.y - content.y, 0.0f, std::max(0.0f, content.h - 180)), 240, 180);
            opened_this_frame = true;
        }
    }
    render_menu(context, view, action);
}

#include "ui/nuklear/compose_recipe_menu.h"
#include "ui/nuklear/compose_canvas.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/context_menu.h"
#include "wormedit/nuklear_config.h"
#include "ui/nuklear/compose_state.h"

#include <algorithm>

namespace {
namespace chrome = wormedit::ui::chrome;

chrome::ContextMenu menu{.title = "SV recipe actions"};
std::uint64_t menu_recipe{};

// Command ids carried in ui::compose_input_action_t::span_edge by RECIPE_COMMAND.
enum recipe_command_t : std::uint8_t { toggle_enabled = 0, materialize = 1, remove = 2 };

void queue_command(const WormUiComposeView& view, wormedit::ui::workspace_action_t& action,
                   recipe_command_t command) {
    append_compose_input(&action, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 0.0, 0.0, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, menu_recipe, 0, false,
                         wormedit::timing_lane_kind_t::scroll, view.chart_id, view.document_revision);
    if (action.compose_input_count != 0)
        action.compose_inputs[action.compose_input_count - 1].span_edge = command;
}

void menu_entry(nk_context* context, const char* title, const char* disabled_reason,
                const WormUiComposeView& view, wormedit::ui::workspace_action_t& action,
                recipe_command_t command) {
    if (chrome::context_menu_item(menu, context, title, disabled_reason))
        queue_command(view, action, command);
}

auto find_area(const WormUiComposeView& view, std::uint64_t id) -> const WormUiComposeRecipeAreaView* {
    for (size_t index = 0; index < view.recipe_area_count; ++index)
        if (view.recipe_areas[index].recipe_id == id) return &view.recipe_areas[index];
    return nullptr;
}

void render_menu(nk_context* context, const WormUiComposeView& view, wormedit::ui::workspace_action_t& action) {
    if (!menu.open) return;
    const auto* area = find_area(view, menu_recipe);
    if (!area) {
        chrome::context_menu_close(menu, context);
        return;
    }
    const auto current = wormedit::ui::chrome::PopupContextStamp::from(view, menu_recipe);
    if (!chrome::context_menu_begin(menu, context, current,
                                    nk_vec2(chrome::context_menu_width,
                                            chrome::context_menu_height(3))))
        return;
    menu_entry(context, area->enabled ? "Disable recipe" : "Enable recipe", nullptr, view, action,
               toggle_enabled);
    menu_entry(context, "Materialize into events",
               area->evaluable ? nullptr : "This recipe cannot be evaluated yet", view, action,
               materialize);
    menu_entry(context, "Delete recipe", nullptr, view, action, remove);
    chrome::context_menu_end(menu, context);
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
            view, static_cast<wormedit::timing_lane_kind_t>(area.lane));
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
    return menu.open;
}

auto compose_recipe_menu_consumed() -> bool {
    return chrome::context_menu_owns_input(menu);
}

void reset_compose_recipe_menu() {
    chrome::context_menu_reset(menu);
}

void process_compose_recipe_menu(nk_context* context, const WormUiComposeView& view,
                                 wormedit::ui::workspace_action_t& action, struct nk_rect viewport,
                                 double input_right, bool focused) {
    (void)chrome::context_menu_update(menu, context);
    if (!view.scene_available || view.viewport_width_pixels <= 0 || view.viewport_height_pixels <= 0 ||
        !focused) {
        chrome::context_menu_close(menu, context);
        return;
    }
    const auto& mouse = context->input.mouse;
    if (worm_ui_nuklear_compose_context_click(context, view) && mouse.pos.x < input_right) {
        if (const auto* area = area_at(view, viewport, mouse.pos.x, mouse.pos.y)) {
            chrome::context_menu_open(menu, mouse.pos,
                                      {view.project_id, view.chart_id, view.document_revision,
                                       area->recipe_id});
            menu_recipe = area->recipe_id;
        }
    }
    render_menu(context, view, action);
}

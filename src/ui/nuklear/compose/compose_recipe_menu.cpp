#include "ui/nuklear/compose/compose_recipe_menu.h"
#include "ui/nuklear/compose/compose_canvas.h"
#include "ui/nuklear/compose/compose_panel_internal.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/context_menu.h"
#include "wormedit/nuklear_config.h"
#include "ui/nuklear/compose/compose_state.h"

#include <algorithm>

namespace {
namespace chrome = wormedit::ui::chrome;

chrome::ContextMenu menu{.title = "SV recipe actions"};
std::uint64_t menu_recipe{};

// Command ids carried in ui::compose_input_action_t::span_edge by RECIPE_COMMAND.
using recipe_command_t = wormedit::ui::compose_recipe_command_t;

void queue_command(const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action,
                   recipe_command_t command) {
    append_compose_input(&action, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 0.0, 0.0, 0, false,
                         WORM_UI_COMPOSE_SELECTION_REPLACE,
                         WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, menu_recipe, 0, false,
                         wormedit::timing_lane_kind_t::scroll, view.chart_id.value, view.document_revision);
    if (action.compose_inputs.size() != 0)
        action.compose_inputs[action.compose_inputs.size() - 1].span_edge = static_cast<std::uint8_t>(command);
}

void menu_entry(nk_context* context, const char* title, const char* disabled_reason,
                const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action,
                recipe_command_t command) {
    if (chrome::context_menu_item(menu, context, title, disabled_reason))
        queue_command(view, action, command);
}

auto find_area(const wormedit::ui::compose_view_t& view, std::uint64_t id) -> const wormedit::ui::compose_recipe_area_view_t* {
    for (size_t index = 0; index < view.recipe_areas.size(); ++index)
        if (view.recipe_areas[index].recipe_id.value == id) return &view.recipe_areas[index];
    return nullptr;
}

void render_menu(nk_context* context, const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action) {
    if (!menu.open) return;
    const auto* area = find_area(view, menu_recipe);
    if (!area) {
        chrome::context_menu_close(menu, context);
        return;
    }
    const auto current = wormedit::ui::chrome::PopupContextStamp::from(view, menu_recipe);
    if (!chrome::context_menu_begin(menu, context, current,
                                    nk_vec2(chrome::context_menu_width,
                                            chrome::context_menu_height(4))))
        return;
    menu_entry(context, area->enabled ? "Disable recipe" : "Enable recipe", nullptr, view, action,
               recipe_command_t::toggle_enabled);
    menu_entry(context, "Materialize into events",
               area->evaluable ? nullptr : "This recipe cannot be evaluated yet", view, action,
               recipe_command_t::materialize);
    menu_entry(context, "Duplicate recipe", nullptr, view, action, recipe_command_t::duplicate);
    menu_entry(context, "Delete recipe", nullptr, view, action, recipe_command_t::remove);
    chrome::context_menu_end(menu, context);
}

// The recipe area under a screen point, using the same lane geometry the canvas draws with.
auto area_at(const wormedit::ui::compose_view_t& view, struct nk_rect viewport, float x, float y)
    -> const wormedit::ui::compose_recipe_area_view_t* {
    const auto x_scale = viewport.w / static_cast<float>(view.viewport_width_pixels);
    const auto y_scale = viewport.h / static_cast<float>(view.viewport_height_pixels);
    const wormedit::ui::compose_recipe_area_view_t* hit = nullptr;
    for (size_t index = 0; index < view.recipe_areas.size(); ++index) {
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

void process_compose_recipe_menu(nk_context* context, const wormedit::ui::compose_view_t& view,
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
                                      {view.project_id.value, view.chart_id.value, view.document_revision,
                                       area->recipe_id.value});
            menu_recipe = area->recipe_id.value;
        }
    }
    render_menu(context, view, action);
}

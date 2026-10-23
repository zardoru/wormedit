#include "sys/resources/bga_usage_actions.h"
#include "sys/resources/bga_usage_view.h"
#include "sys/project/workspace_navigation.h"
#include <algorithm>

namespace wormedit::sys {
auto dispatch_bga_usage_action(const ui::workspace_action_t& action,
    const ui::bga_dock_request_t& request, document_history_t& history,
    workspace_controller_t& workspace, std::optional<editor_interaction_state_t>& interaction)
    -> std::optional<std::string> {
    const auto& use = action.bga_usage;
    if (action.kind != WORM_UI_WORKSPACE_ACTION_BGA_USAGE_ACTIVATE ||
        workspace.active_tab != workspace_tab_t::compose || !request.visible ||
        !request.find_uses_open || request.selected_project != history.document.id ||
        request.selected_resource != use.resource || use.song != history.document.id ||
        use.revision != history.document.revision)
        return "BGA usage results changed; refresh Find uses.";
    const auto view = bga_usage_view_t::from(history.document, use.resource);
    if (!view.valid || view.asset != use.asset ||
        std::ranges::find(view.rows, use.target, &ui::bga_usage_row_view_t::target) == view.rows.end())
        return "The BGA usage is no longer available.";
    auto navigation = workspace_navigation_t::from(history, workspace, interaction,
                                                   use.target.chart, use.target.position);
    if (!navigation) return "The BGA usage cannot be opened in Compose.";
    const auto selected = dispatch_editor_action(history, navigation->interaction,
        select_editor_objects_action_t{{{editor_selection_object_kind_t::bga_event, use.target.event}},
                                       editor_area_selection_mode_t::replace});
    if (!selected.accepted) return "The BGA event cannot be selected.";
    navigation->interaction.bga_lane_visible = true;
    std::move(*navigation).apply(workspace, interaction);
    return std::nullopt;
}
} // namespace wormedit::sys

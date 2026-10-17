#pragma once
#include "sys/compose_action_outcome.h"
#include "ui/frontend.h"
#include <optional>
#include <string>
#include <wormedit/core/interaction_state.h>
#include <wormedit/core/notifications.h>
namespace wormedit::sys {
constexpr auto compose_area_pause_conversion(WormUiComposeActionKind kind) -> bool {
    return kind >= WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_STOP &&
           kind <= WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_DELAY_RESIZE;
}
auto compose_area_delete_supported(const wormedit::chart_document_t&,
                                   const wormedit::measure_index_t&,
                                   const wormedit::editor_interaction_state_t&) -> bool;
auto compose_area_delete_supported(wormedit::document_history_t&,
                                   const wormedit::editor_interaction_state_t&) -> bool;
struct application_command_state_t;
auto can_apply_compose_quantize(const application_command_state_t& application,
                                const wormedit::editor_interaction_state_t& state) -> bool;
auto apply_compose_quantize_selection(wormedit::document_history_t& history,
                                      wormedit::editor_interaction_state_t& state,
                                      application_command_state_t& application,
                                      const WormUiWorkspaceAction& action)
    -> compose_action_outcome_t;
} // namespace wormedit::sys

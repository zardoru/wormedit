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
} // namespace wormedit::sys

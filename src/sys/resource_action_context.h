#pragma once

#include "sys/compose_action_outcome.h"
#include "ui/frontend.h"

namespace wormedit::sys {
struct document_edit_inputs_t;

// The sound and BGA editors also have entry points outside Compose dispatch.
// Borrow the existing document owner for this synchronous resource operation.
struct resource_action_context_t {
    using outcome_type = compose_action_outcome_t;
    const WormUiWorkspaceAction& action;
    const document_edit_inputs_t& document;
    chart_id_t chart;
};
} // namespace wormedit::sys

#include "sys/timing_edit_runtime.h"

#include "sys/compose_view.h"

namespace wormedit::sys {
namespace runtime {

void timing_edit_t::cancel_if_stale(
    const document_history_t& history, const workspace_controller_t& workspace,
    const application_command_state_t&) {
    if (stage &&
        (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
         !stage->captured_stamp().is_current(history.document, *workspace.selected_chart,
                                 document_identity_policy_t::revision_only))) {
        cancel_proposal(*stage);
        stage.reset();
    }
}

} // namespace runtime
} // namespace wormedit::sys

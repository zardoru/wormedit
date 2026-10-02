#include "sys/timing_edit_runtime.h"

#include "sys/compose_view.h"

namespace wormedit::sys {
namespace runtime {

void timing_edit_t::cancel_if_stale(
    const document_history_t& history, const workspace_controller_t& workspace,
    const application_command_state_t&) {
    if (stage &&
        (stage->document_revision != history.document.revision ||
         workspace.active_tab != workspace_tab_t::compose ||
         !workspace.selected_chart || *workspace.selected_chart != stage->chart)) {
        cancel_timing_lane_pseudonote(*stage);
        stage.reset();
    }
}

} // namespace runtime
} // namespace wormedit::sys

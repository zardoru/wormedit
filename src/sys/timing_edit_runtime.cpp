#include "sys/timing_edit_runtime.h"

#include "sys/compose_view.h"

namespace wormedit::sys {
namespace runtime {

void timing_edit_t::cancel_if_stale(
    const document_history_t& history, const workspace_controller_t& workspace,
    const application_command_state_t& application_state) {
    if (stage &&
        (stage->document_revision != history.document.revision ||
         (workspace.active_tab != workspace_tab_t::timing &&
          workspace.active_tab != workspace_tab_t::compose) ||
         !workspace.selected_chart || *workspace.selected_chart != stage->chart)) {
        cancel_timing_lane_pseudonote(*stage);
        stage.reset();
    }
    if (recipe_draft &&
        (recipe_draft->document_revision != history.document.revision ||
         workspace.active_tab != workspace_tab_t::timing ||
         !workspace.selected_chart || *workspace.selected_chart != recipe_draft->chart ||
         application_state.timing_locked)) {
        cancel_sv_recipe_draft(*recipe_draft);
        recipe_draft.reset();
        recipe_preview.reset();
        recipe_selection_binding.reset();
    }
    if (recipe_selection_binding &&
        (workspace.active_tab != workspace_tab_t::timing ||
         application_state.timing_locked || !workspace.selected_chart ||
         *workspace.selected_chart != recipe_selection_binding->first ||
         history.document.revision != recipe_selection_binding->second)) {
        recipe_selection_binding.reset();
    }
}

} // namespace runtime
} // namespace wormedit::sys

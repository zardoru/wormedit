#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/workspace.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace wormedit::sys {

struct application_command_state_t;

namespace runtime {

struct timing_edit_t {
    std::optional<timing_lane_pseudonote_preview_t> stage;
    std::optional<std::pair<chart_id_t, std::uint64_t>> recipe_selection_binding;
    std::optional<sv_recipe_draft_t> recipe_draft;
    std::optional<sv_recipe_draft_preview_t> recipe_preview;

    [[nodiscard]] bool has_pending_edit() const {
        return stage || recipe_draft || recipe_selection_binding;
    }
    [[nodiscard]] bool has_pending_recipe_edit() const {
        return recipe_draft || recipe_selection_binding;
    }
    [[nodiscard]] const timing_lane_pseudonote_preview_t* stage_view() const {
        return stage ? &*stage : nullptr;
    }
    [[nodiscard]] const sv_recipe_draft_t* recipe_draft_view() const {
        return recipe_draft ? &*recipe_draft : nullptr;
    }
    [[nodiscard]] const sv_recipe_draft_preview_t* recipe_preview_view() const {
        return recipe_preview ? &*recipe_preview : nullptr;
    }
    void cancel_if_stale(const document_history_t& history,
                         const workspace_controller_t& workspace,
                         const application_command_state_t& application_state);
};

} // namespace runtime
} // namespace wormedit::sys

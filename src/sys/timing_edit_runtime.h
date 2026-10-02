#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/workspace.h>

#include <cstdint>
#include <optional>

namespace wormedit::sys {

struct application_command_state_t;

namespace runtime {

struct timing_edit_t {
    std::optional<timing_lane_pseudonote_preview_t> stage;

    [[nodiscard]] bool has_pending_edit() const { return stage.has_value(); }
    [[nodiscard]] const timing_lane_pseudonote_preview_t* stage_view() const {
        return stage ? &*stage : nullptr;
    }
    void cancel_if_stale(const document_history_t& history,
                         const workspace_controller_t& workspace,
                         const application_command_state_t& application_state);
};

} // namespace runtime
} // namespace wormedit::sys

#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/workspace.h>

#include <cstdint>
#include <optional>

namespace wormedit::ui {
struct workspace_action_t;
}

namespace wormedit::sys {

struct application_command_state_t;
struct document_edit_inputs_t;

namespace runtime {

struct timing_edit_t {
    std::optional<timing_lane_pseudonote_outcome_t> stage;

    [[nodiscard]] bool has_pending_edit() const {
        return stage.has_value();
    }
    [[nodiscard]] const timing_lane_pseudonote_outcome_t* stage_view() const {
        return stage ? &*stage : nullptr;
    }
    void dispatch(const ui::workspace_action_t& action, const document_edit_inputs_t& inputs,
                  bool timing_locked, std::uint64_t active_snap_denominator);
    void cancel_if_stale(const document_history_t& history, const workspace_controller_t& workspace,
                         const application_command_state_t& application_state);

  private:
    void begin(const ui::workspace_action_t& action, const document_edit_inputs_t& inputs,
               bool timing_locked, std::uint64_t active_snap_denominator);
    void quick_insert(const ui::workspace_action_t& action, const document_edit_inputs_t& inputs,
                      bool timing_locked, std::uint64_t active_snap_denominator);
    void cancel();
    void dispatch_active(const ui::workspace_action_t& action, const document_edit_inputs_t& inputs,
                         bool timing_locked, std::uint64_t active_snap_denominator);
    void revise_value(const ui::workspace_action_t& action, const document_edit_inputs_t& inputs,
                      bool timing_locked, std::uint64_t active_snap_denominator,
                      const chart_document_t* chart);
    void confirm(const document_edit_inputs_t& inputs, bool timing_locked);
    void move(const ui::workspace_action_t& action, const document_edit_inputs_t& inputs,
              bool timing_locked, std::uint64_t active_snap_denominator,
              const chart_document_t* chart);
    void remove(const ui::workspace_action_t& action, const document_edit_inputs_t& inputs,
                bool timing_locked);
};

} // namespace runtime
} // namespace wormedit::sys

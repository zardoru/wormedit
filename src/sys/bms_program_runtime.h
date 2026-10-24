#pragma once
#include "ui/frontend/bms_program.h"
#include <wormedit/core/bms_branch_editor.h>
#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/interaction_state.h>

namespace wormedit::sys {
// Project-lifetime owner of scope selection, recoverable drafts and confirmations.
class bms_program_runtime_t {
  public:
    void synchronize(document_history_t& history, std::optional<chart_id_t> difficulty);
    void refill(ui::bms_program_view_t& view, const document_history_t& history) const;
    [[nodiscard]] auto apply(document_history_t& history, editor_interaction_state_t& state,
        const ui::bms_program_action_t& action) -> document_command_result_t;
    [[nodiscard]] auto content_chart() const -> std::optional<branch_id_t> { return content_chart_; }
  private:
    void build_tree_cache(const bms_program_document_t& owner, std::uint64_t revision) const;
    void load_draft(const bms_program_t& program, std::uint64_t revision);
    void select(document_history_t& history, const bms_program_t& program, branch_id_t node);
    [[nodiscard]] auto edit(document_history_t& history, const ui::bms_program_action_t& action)
        -> document_command_result_t;
    [[nodiscard]] auto save(document_history_t& history) -> document_command_result_t;
    [[nodiscard]] auto prepare(document_history_t& history, bool unwrap) -> document_command_result_t;
    [[nodiscard]] auto confirm(document_history_t& history, ui::bms_program_operation_t operation) -> document_command_result_t;
    song_id_t project_;
    chart_id_t difficulty_;
    branch_id_t selected_;
    std::optional<branch_id_t> content_chart_;
    std::uint64_t draft_revision_{};
    std::string parameter_, text_, preview_;
    bool dirty_{};
    std::variant<std::monostate, bms_program_delete_proposal_t, bms_program_unwrap_proposal_t> pending_;
    diagnostic_list_t feedback_;
    mutable std::optional<std::pair<std::uint64_t, branch_id_t>> tree_key_;
    mutable ui::bms_program_view_t tree_view_;
};
}

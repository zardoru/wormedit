#pragma once

#include "sys/app/application_edit_inputs.h"
#include "sys/compose/actions/compose_action_outcome.h"
#include "ui/frontend/storyboard.h"

namespace wormedit { struct osbx_edit_created_ids_t; struct osbx_place_image_t; struct osbx_document_t; struct osbx_edit_t; struct lua_source_descriptor_t; }

namespace wormedit::sys {
struct application_command_state_t;

// Project lifetime: the existing application command-state project reset owns
// this complete value. Views only project it; actions are the mutation boundary.
class storyboard_runtime_t {
  public:
    void refill(ui::storyboard_view_t&, const song_document_t&, std::uint64_t session_generation);
    [[nodiscard]] auto complete_assets_import(const ui::storyboard_action_t&, const osbx_place_image_t&,
        const document_edit_inputs_t&, const application_command_state_t&, editor_interaction_state_t*, bool file_job_busy) -> bool;
    [[nodiscard]] auto complete_lua_source(const ui::storyboard_action_t&,
        const result_t<lua_source_descriptor_t, std::string>&, const document_edit_inputs_t&,
        const application_command_state_t&, editor_interaction_state_t*, bool) -> bool;
    [[nodiscard]] auto state() const -> const ui::storyboard_view_state_t& { return state_; }
    [[nodiscard]] auto dispatch(const ui::storyboard_action_t&, const document_edit_inputs_t&,
        const application_command_state_t&, editor_interaction_state_t* compose,
        bool file_job_busy) -> bool;
  private:
    // Binding copies of the application's existing identity/session values;
    // this runtime never allocates or advances a generation.
    song_id_t project_;
    std::uint64_t session_generation_{};
    ui::storyboard_view_state_t state_;
    std::vector<std::string> thumbnail_images_;
    std::vector<resource_id_t> thumbnail_resources_;
    [[nodiscard]] auto lua_link(const ui::storyboard_action_t&, const document_edit_inputs_t&,
        const application_command_state_t&, editor_interaction_state_t*, bool) -> compose_action_outcome_t;
    [[nodiscard]] auto lua_capture_reason(const ui::storyboard_action_t&, const document_edit_inputs_t&,
        const application_command_state_t&, const editor_interaction_state_t*, bool) const -> std::string;
    [[nodiscard]] auto assets(const ui::storyboard_action_t&, const document_edit_inputs_t&,
        const application_command_state_t&, editor_interaction_state_t*, bool) -> compose_action_outcome_t;
    [[nodiscard]] auto apply_assets(const ui::storyboard_action_t&, const osbx_edit_t&,
        const document_edit_inputs_t&) -> compose_action_outcome_t;
    void update_thumbnail_demand(const ui::storyboard_action_t&, const song_document_t&, const osbx_document_t&);
    bool timeline_grid_enabled_{};
    // Derived gesture feedback for the dock; the shared editor owns the proposal.
    std::optional<std::int64_t> timeline_preview_delta_;
    std::string timeline_preview_reason_;
    void reconcile(const song_document_t&);
    [[nodiscard]] auto update_view(const ui::storyboard_action_t&, const song_document_t&) -> compose_action_outcome_t;
    [[nodiscard]] auto execute(const ui::storyboard_action_t&, const document_edit_inputs_t&,
        const application_command_state_t&, editor_interaction_state_t*, bool file_job_busy)
        -> compose_action_outcome_t;
    [[nodiscard]] auto transport(const ui::storyboard_action_t&, const document_edit_inputs_t&,
        const application_command_state_t&, editor_interaction_state_t*, bool file_job_busy)
        -> compose_action_outcome_t;
    [[nodiscard]] auto edit(const ui::storyboard_action_t&, const document_edit_inputs_t&,
                            const application_command_state_t&) -> compose_action_outcome_t;
    [[nodiscard]] auto edit_hierarchy(const ui::storyboard_action_t&, const document_edit_inputs_t&,
        const application_command_state_t&, editor_interaction_state_t*, bool file_job_busy) -> compose_action_outcome_t;
    void reconcile_hierarchy_edit(const ui::storyboard_action_t&, const osbx_edit_created_ids_t&,
                                  const song_document_t&);
    [[nodiscard]] auto inspect(const ui::storyboard_action_t&, const document_edit_inputs_t&,
        const application_command_state_t&, editor_interaction_state_t*, bool file_job_busy) -> compose_action_outcome_t;
    [[nodiscard]] auto cancel_gesture(const ui::storyboard_action_t&, const document_edit_inputs_t&,
        editor_interaction_state_t*, const diagnostic_t* rejection = nullptr) -> std::optional<compose_action_outcome_t>;
    [[nodiscard]] auto is_matching_gesture(const ui::storyboard_action_t&, const document_edit_inputs_t&,
        const editor_interaction_state_t*) const -> bool;
    [[nodiscard]] auto reject_edit(const ui::storyboard_action_t&, const document_edit_inputs_t&,
        editor_interaction_state_t*, const diagnostic_t&, notification_source_t) -> compose_action_outcome_t;
    [[nodiscard]] auto timeline(const ui::storyboard_action_t&, const document_edit_inputs_t&,
        const application_command_state_t&, editor_interaction_state_t*, bool file_job_busy) -> compose_action_outcome_t;
    [[nodiscard]] auto select_timeline_keys(const ui::storyboard_action_t&, const song_document_t&) -> compose_action_outcome_t;
    [[nodiscard]] auto edit_timeline(const ui::storyboard_action_t&, const document_edit_inputs_t&,
        const application_command_state_t&, editor_interaction_state_t*) -> compose_action_outcome_t;
    [[nodiscard]] auto apply_inspector(const ui::storyboard_action_t&, const osbx_edit_t&,
        const document_edit_inputs_t&) -> compose_action_outcome_t;
    [[nodiscard]] auto set_conditional_layer(const ui::storyboard_action_t&,
        const application_command_state_t&, const editor_interaction_state_t*, bool file_job_busy)
        -> compose_action_outcome_t;
    [[nodiscard]] auto is_current(const ui::storyboard_action_t&, const document_edit_inputs_t&,
        bool allow_player_preview = false) const -> bool;
    [[nodiscard]] auto is_inspector_target_current(const ui::storyboard_action_t&) const -> bool;
    [[nodiscard]] auto is_timeline_target_current(const ui::storyboard_action_t&) const -> bool;
};
} // namespace wormedit::sys

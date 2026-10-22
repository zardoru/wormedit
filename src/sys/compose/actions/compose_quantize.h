#pragma once

#include "sys/compose/views/compose_view.h"

namespace wormedit::sys {

inline constexpr auto compose_quantize_idle_message =
    "Quantize requires an idle active Compose chart.";

[[nodiscard]] auto is_compose_quantize_request_valid(const wormedit::quantize_request_t& request)
    -> bool;

auto compose_quantize_editor_for(wormedit::document_history_t& history,
                                 wormedit::editor_interaction_state_t& state,
                                 application_command_state_t& application)
    -> application_command_state_t::quantize_editor_t&;

auto can_apply_compose_quantize(const application_command_state_t& application,
                                const wormedit::editor_interaction_state_t& state) -> bool;

auto toggle_compose_quantize_preview(wormedit::document_history_t& history,
                                     wormedit::editor_interaction_state_t& state,
                                     application_command_state_t& application) -> bool;

auto apply_compose_quantize_selection(wormedit::document_history_t& history,
                                      wormedit::editor_interaction_state_t& state,
                                      application_command_state_t& application,
                                      wormedit::quantize_request_t request)
    -> compose_action_outcome_t;

} // namespace wormedit::sys

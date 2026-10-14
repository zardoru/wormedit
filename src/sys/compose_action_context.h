#pragma once
#include "sys/application_edit_inputs.h"
#include "sys/compose_action_outcome.h"
#include "ui/frontend.h"

namespace wormedit::sys::compose_action_detail {
struct compose_action_context_t {
    const compose_action_inputs_t& environment;
    const WormUiWorkspaceAction& action;
    wormedit::editor_interaction_state_t& state;
    double viewport_height;
    bool metadata_import_busy;
    wormedit::editor_field_scroll_direction_t scroll_direction;
    notification_sink_t notifications{notification_sink_t::muted()};

    [[nodiscard]] auto require_resources() const -> std::optional<compose_action_outcome_t>;
    [[nodiscard]] auto require_no_gesture(std::string_view message) const
        -> std::optional<compose_action_outcome_t>;
};

auto finish_compose_action(compose_action_context_t& context, compose_action_outcome_t outcome)
    -> std::string;

} // namespace wormedit::sys::compose_action_detail

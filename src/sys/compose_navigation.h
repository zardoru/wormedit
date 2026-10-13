#pragma once

#include "sys/bookmark_commands.h"

namespace wormedit::sys {

// A position request retains exact audio anchors independently of its editable row.
struct compose_navigation_target_t {
    local_position_t position;
    std::optional<std::size_t> canvas_measure;
    std::optional<std::uint64_t> preview_frame;
    std::optional<std::int64_t> audio_milliseconds;
    bool minimap{};
    bool primary{};
};

enum class compose_navigation_transport_t { keep, seek, stop };

[[nodiscard]] auto compose_navigation_transport_policy(const application_command_state_t& application,
    bool minimap, bool primary) -> compose_navigation_transport_t;

[[nodiscard]] auto apply_compose_navigation_transport(application_command_state_t& application,
    compose_navigation_transport_t policy, std::optional<std::uint64_t> frame,
    std::optional<std::int64_t> audio_milliseconds = std::nullopt) -> bookmark_command_outcome_t;

[[nodiscard]] auto move_compose_navigation_cursor(document_history_t& history,
    editor_interaction_state_t& state, const compose_navigation_target_t& target,
    std::uint64_t expected_revision) -> editor_interaction_result_t;

[[nodiscard]] auto scroll_compose_navigation_view(document_history_t& history,
    editor_interaction_state_t& state, double target_scroll,
    std::optional<editor_field_scroll_direction_t> direction = std::nullopt) -> editor_interaction_result_t;

[[nodiscard]] auto reset_compose_navigation_view(document_history_t& history,
    editor_interaction_state_t& state) -> editor_interaction_result_t;

[[nodiscard]] auto seek_compose_navigation_position(const chart_document_t& chart,
    application_command_state_t& application, const compose_navigation_target_t& target)
    -> bookmark_command_outcome_t;

[[nodiscard]] auto navigate_compose_position(document_history_t& history,
    editor_interaction_state_t& state, application_command_state_t& application,
    const compose_navigation_target_t& target, std::uint64_t expected_revision)
    -> bookmark_command_outcome_t;

[[nodiscard]] auto navigate_compose_measure(document_history_t& history,
    editor_interaction_state_t& state, application_command_state_t& application,
    std::uint64_t number) -> bookmark_command_outcome_t;

} // namespace wormedit::sys

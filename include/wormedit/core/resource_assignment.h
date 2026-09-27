#pragma once

#include <wormedit/core/interaction.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wormedit {

struct resource_assignment_limits_t {
    std::size_t maximum_selected_objects{50'000};
    std::size_t maximum_chart_objects{1'000'000};
};

struct assign_resource_action_t {
    chart_id_t chart;
    resource_id_t resource;
    std::vector<editor_selection_object_t> objects;
};

// Assigns one catalog resource to an explicit typed note/BGM/BGA selection.
// BMSON objects linked to retained sound-channel events cannot be rebound to
// another resource without editing their source channel. A no-op adds no history.
[[nodiscard]] auto apply_resource_assignment(document_history_t& history,
                                             std::uint64_t expected_revision,
                                             const assign_resource_action_t& action,
                                             resource_assignment_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit

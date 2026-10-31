#pragma once

#include "sys/compose/views/compose_view.h"

namespace wormedit::sys {

// Clipboard selection spans the interaction's objects and the project-owned
// timing-area selection. Menus and keyboard commands use the same availability.
// A latched Space range is a selection aid, not clipboard content or an unfinished edit.
inline auto can_copy_compose_selection(const wormedit::editor_interaction_state_t& interaction,
    const application_command_state_t& application) -> bool {
    const auto& timing = application.project.timing_recipe;
    return (!interaction.is_pending() || interaction.has_latched_range()) &&
        (!interaction.selected_objects.empty() || !interaction.selected_notes.empty() ||
         timing.kiai_selected_head != 0 || timing.recipe_selected != 0);
}

} // namespace wormedit::sys

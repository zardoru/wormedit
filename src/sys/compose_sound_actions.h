#pragma once

#include "sys/compose_view.h"
#include "ui/frontend.h"

#include <wormedit/core/command.h>
#include <wormedit/core/workspace.h>

#include <optional>
#include <span>
#include <string>

namespace wormedit::sys {

// Registers dropped or picked audio files as keysounds in one undo step. Files
// that are not decodable audio, or that already back a sample, are skipped and
// counted in the returned message. BMS charts give each new sound the next
// free #WAV slot. The input order is sorted by file name for stable slots.
[[nodiscard]] auto import_keysound_files(document_history_t& history,
                                         workspace_controller_t& workspace, chart_id_t chart,
                                         std::span<const std::string> paths) -> std::string;

// Keysound listbox actions. Returns nullopt for any other action. The status
// message is always non-empty on success or failure.
[[nodiscard]] auto dispatch_compose_sound_action(const WormUiWorkspaceAction& action,
                                                 document_history_t& history,
                                                 workspace_controller_t& workspace,
                                                 chart_id_t chart,
                                                 application_command_state_t& application_state)
    -> std::optional<std::string>;

} // namespace wormedit::sys

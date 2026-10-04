#pragma once
#include "sys/preferences_runtime.h"
#include "ui/frontend.h"
#include <wormedit/core/notifications.h>

namespace wormedit::sys {
struct options_diagnostic_t {
    WormUiOptionsField field;
    std::string message;
};
// Program settings only: no document, workspace, or history references.
struct options_runtime_t {
    bool visible{};
    std::uint64_t revision{};
    WormUiOptionsCategory category{WORM_UI_OPTIONS_GENERAL};
    program_preferences_t draft;
    program_preferences_t baseline;
    std::vector<options_diagnostic_t> diagnostics;
    std::vector<WormUiOptionsDiagnostic> diagnostic_views;
    std::vector<WormUiExternalPlayerDefinitionView> player_views;
    std::string status;
    // One producer result per action; projection must never repost this value.
    std::optional<notification_post_t> notification;
};
void open_options(options_runtime_t&, const program_preferences_t&);
// Later registered pages submit typed candidates; only the active category is copied.
[[nodiscard]] auto edit_options_draft(options_runtime_t&, std::uint64_t revision,
    const program_preferences_t& candidate) -> bool;
[[nodiscard]] auto options_view(options_runtime_t&) -> WormUiOptionsView;
// Returns true only when an active category has been accepted into program preferences.
[[nodiscard]] auto apply_options_action(options_runtime_t&, program_preferences_runtime_t&,
    const WormUiOptionsAction&) -> bool;
} // namespace wormedit::sys

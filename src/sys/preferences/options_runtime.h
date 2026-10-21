#pragma once
#include "sys/preferences/preferences_runtime.h"
#include "ui/frontend.h"
#include <wormedit/core/notifications.h>
#include <wormedit/core/keyed_cache.h>
#include <tuple>

namespace wormedit::sys {
struct options_diagnostic_t {
    WormUiOptionsField field;
    std::string message;
};
// Program settings only: no document, workspace, or history references.
struct options_runtime_t {
    bool visible{};
    std::uint32_t default_lane_count{4};
    std::uint64_t revision{};
    WormUiOptionsCategory category{WORM_UI_OPTIONS_GENERAL};
    program_preferences_t draft;
    program_preferences_t baseline;
    std::vector<options_diagnostic_t> diagnostics;
    keyed_cache_t<std::tuple<program_preferences_t, program_preferences_t, WormUiOptionsCategory>,
                  bool> validation;
    std::string status;
    std::string current_field_layout_key;
    std::string current_field_layout_name;
    std::vector<std::string> audio_devices;
    bool audio_output_pending{};
    // One producer result per action; projection must never repost this value.
    std::optional<notification_post_t> notification;
};
// Resolves presentation only: null preserves the authored chart side.
[[nodiscard]] auto resolve_scratch_positions(const display_preferences_t&,
    const std::array<std::optional<bool>, 2>& skin_defaults)
    -> std::array<std::optional<bool>, 2>;
void set_options_field_layout(options_runtime_t&, std::string_view key, std::string_view name);
void open_options(options_runtime_t&, const program_preferences_t&);
void open_options_category(options_runtime_t&, const program_preferences_t&, WormUiOptionsCategory);
// Later registered pages submit typed candidates; only the active category is copied.
[[nodiscard]] auto edit_options_draft(options_runtime_t&, std::uint64_t revision,
    const program_preferences_t& candidate) -> bool;
// Returns true only when an active category has been accepted into program preferences.
[[nodiscard]] auto apply_options_action(options_runtime_t&, program_preferences_runtime_t&,
    const wormedit::ui::options_action_t&) -> bool;
// Flips the click-audition preference through the same save path as the Options page and
// mirrors the accepted value into `enabled`. Returns the save error, or empty on success.
[[nodiscard]] auto toggle_audition_on_click(program_preferences_runtime_t&, bool& enabled)
    -> std::string;
[[nodiscard]] auto toggle_audition_on_playback(program_preferences_runtime_t&, bool& enabled)
    -> std::string;
} // namespace wormedit::sys

#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace wormedit::sys {

// Typed application-level commands handled by the system frontend. The
// registry keeps stable textual IDs for shortcut profiles and UI integration;
// this value is the validated form used by the application dispatcher.
enum class application_command_kind_t {
    document_new_project,
    document_save_project,
    document_new_chart,
    history_undo,
    history_redo,
    transport_play_stop,
    transport_play_pause,
    transport_play_from_start,
    transport_stop,
    transport_toggle_metronome,
    transport_external_play_start,
    transport_external_play_here,
    transport_external_stop,
    transport_rate_slower,
    transport_rate_faster,
    edit_delete,
    edit_copy,
    edit_cut,
    edit_paste,
    edit_note_kind,
    edit_tool_select,
    selection_clear,
    selection_select_all,
    selection_invert,
    navigation_previous_snap,
    navigation_next_snap,
    navigation_previous_marker,
    navigation_next_marker,
    navigation_previous_bookmark,
    navigation_next_bookmark,
    markers_add_bookmark,
    view_notifications,
    workspace_options,
    workspace_open_setup,
    workspace_open_metadata,
    workspace_open_timing,
    workspace_open_compose,
    workspace_open_storyboard,
    workspace_open_audio_slices,
    workspace_open_compare_validate,
    timing_tap_assistant,
    timing_tap,
    timing_toggle_lock,
    timing_insert_bpm,
    timing_insert_sv,
    timing_open_settings,
    semantic_layer_select_slot,
    semantic_layer_clear_target,
    semantic_layer_apply,
    semantic_layer_remove,
    semantic_layer_toggle,
    audio_paint_hitsound,
};

struct application_command_t {
    application_command_kind_t kind{};
    // Set only for semantic_layer_select_slot. Slots in the external ID format
    // are one-based and must be positive.
    std::optional<std::size_t> semantic_layer_slot;
    std::optional<unsigned> note_kind;

    auto operator==(const application_command_t&) const -> bool = default;
};

[[nodiscard]] auto parse_application_command(std::string_view id) noexcept
    -> std::optional<application_command_t>;

// Focus cleanup emits navigation cancellation even on otherwise idle frames.
// Keep the last command result until input produces a meaningful new status.
[[nodiscard]] auto compose_input_status_with_precedence(std::string_view current,
                                                       std::string_view input) noexcept
    -> std::string_view;

} // namespace wormedit::sys

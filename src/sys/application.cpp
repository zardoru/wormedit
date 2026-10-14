#include "sys/application.h"
#include "sys/application_edit_inputs.h"
#include "sys/external_player_runtime.h"
#include "sys/application_timing_stage.h"
#include "sys/audio_offset_actions.h"
#include "sys/compose_note_to_bgm.h"
#include "sys/compose_sound_selection.h"
#if defined(WORMEDIT_HAS_MACOS_MENU)
#include "sys/macos_menu.h"
#endif
#include "sys/frame_timing.h"
#include "sys/thread_priority.h"
#include "wormedit/audio.h"
#include "wormedit/window.h"

#include "sys/application_commands.h"
#include "sys/application_startup.h"
#include "sys/application_lifetime.h"
#include "sys/application_startup_feedback.h"
#include "sys/frontend_notification_sink.h"
#include "sys/application_frame_services.h"
#include "sys/application_frame_projection.h"
#include "sys/application_command_dispatch.h"
#include "sys/application_defaults.h"
#include "sys/audio_project_import.h"
#include "sys/compose_resource_presenter.h"
#include "sys/compose_view.h"
#include "sys/compare_view.h"
#include "sys/compose_timing_target.h"
#include "sys/compose_input.h"
#include "sys/compose_actions.h"
#include "sys/application_edit_helpers.h"
#include "sys/recipe_form.h"
#include "sys/workspace_projection.h"
#include "sys/workspace_frame_view.h"
#include "sys/transport_player_actions.h"
#include "sys/compose_minimap_navigation.h"
#include "sys/timing_view.h"
#include "sys/timing_edit_runtime.h"
#include "sys/compose_sound_actions.h"
#include "sys/sound_usage_actions.h"
#include "sys/compose_bga_actions.h"
#include "sys/bga_dock_runtime.h"
#include "sys/metadata_drop_state.h"
#include "sys/metadata_asset_runtime.h"
#include "sys/metadata_actions.h"
#include "sys/metadata_view_projection.h"
#include "sys/platform_recovery.h"
#include "sys/recovery_notification_feedback.h"
#include "sys/platform_shortcut_profile.h"
#include "sys/external_player.h"
#include "sys/preferences_runtime.h"
#include "sys/options_runtime.h"
#include "sys/shortcut_rows.h"
#include "sys/menu_command_projection.h"
#include <set>
#include "sys/notification_history_runtime.h"
#include <SDL3/SDL_clipboard.h>
#include "sys/project_view.h"
#include "sys/chart_import_session.h"
#include <wormedit/core/snap.h>
#include <wormedit/core/chart_editor.h>
#include "sys/tap_timing_runtime.h"
#include <SDL3/SDL_filesystem.h>
#include <wormedit/core/virtual_canvas.h>
#include "sys/project_open.h"
#include "sys/project_import_feedback.h"
#include "sys/project_save_paths.h"
#include "sys/player_preview_presenter.h"
#include "sys/waveform_view_projection.h"
#include "sys/bmson_waveform_projection.h"
#include "sys/compose_audition_requests.h"
#include "sys/crash_backtrace.h"
#include "sys/inline_preview_cursor.h"
#include "sys/audio_source_registration.h"
#include "sys/compose_waveform_overlays.h"
#include "ui/frontend.h"
#include "wormedit/config.h"

#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/autosave_loop.h>
#include <wormedit/core/bms_branch_editor.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/command_registry.h>
#include <wormedit/core/editor_navigation_input.h>
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/marker_navigation.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/metadata_editor.h>
#include <wormedit/core/note_label_editor.h>
#include <wormedit/core/player_preview.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/recovery_controller.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/sound_slice.h>
#include <wormedit/core/startup_session.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/task_graph.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/viewport_input.h>
#include <wormedit/core/virtual_canvas.h>
#include <wormedit/core/workspace.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/formats/conversion_publication.h>
#include <wormedit/formats/native_project_storage.h>
#include <wormedit/formats/stepmania.h>

#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_decoder.h>
#include <wormedit/visual/image_job.h>
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
#include <wormedit/visual/video_job.h>
#endif

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include "sys/chart_preview_state.h"
#include "sys/compose_playhead_cursor.h"
#include "sys/audio_transport_frame.h"
#include "sys/preview_cursor_frame.h"
#include "sys/waveform_analysis_runtime.h"
#include "sys/sdl_audition_output.h"
#include "sys/sdl_preview_output.h"
#include <wormedit/audio/aligned_waveform.h>
#include <wormedit/audio/channel_waveform.h>
#include <wormedit/audio/decoder.h>

#include <wormedit/audio/object_audition.h>
#include <wormedit/audio/preview_session.h>
#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/waveform_job.h>
#endif
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace {
using wormedit::sys::begin_timing_stage;
using wormedit::sys::timing_lane_kind_from_ui;
using wormedit::sys::timing_position_from_fraction;
using wormedit::sys::timing_stage_value;



using wormedit::sys::flatten_workspace;
using wormedit::sys::flattened_workspace_t;
using wormedit::sys::initial_document;
using wormedit::sys::new_session_project_id;
using wormedit::sys::parse_application_command;
using wormedit::sys::timing_grid_state_t;
using wormedit::sys::timing_scene_cache_t;
using wormedit::sys::runtime::timing_edit_t;
using wormedit::sys::metadata_drop_state_t;
using wormedit::sys::dispatch_compose_view_action;
using wormedit::sys::runtime::output_group_export_t;
using wormedit::sys::output_group_export_task_result_t;
using wormedit::sys::pending_output_group_export_t;
using wormedit::sys::runtime::resource_copy_t;
using wormedit::sys::parse_timing_signed;
using wormedit::sys::timing_position_at_row;
using wormedit::sys::application_command_state_t;
using wormedit::sys::chart_snap_preferences;
using wormedit::sys::compose_bgm_lane_width_pixels;
using wormedit::sys::compose_measure_rail_width_pixels;
using wormedit::sys::compose_timing_lane_count;
using wormedit::sys::bmson_draft_audition_request_t;
using wormedit::sys::compose_scene_cache_t;
using wormedit::sys::compare_scene_cache_t;
using wormedit::sys::compose_field_downscroll;
using wormedit::sys::compose_horizontal_layout;
using wormedit::sys::compose_lane_is_editable;
using wormedit::sys::compose_physical_scancode;
using wormedit::sys::compose_project_y;
using wormedit::sys::compose_source_y;
using wormedit::sys::compose_surface_height;
using wormedit::sys::editor_interaction_has_pending_gesture;
using wormedit::sys::maximum_layout_lane_count;
using wormedit::sys::populate_compose_view;
using wormedit::sys::requested_snap_denominator;
using wormedit::sys::timing_lane_kind_to_ui;
using wormedit::sys::compose_result_status;
using wormedit::sys::dispatch_compose_input_action;
using wormedit::sys::dispatch_compose_navigation_actions;
using wormedit::sys::selected_bmson_waveform_source;
using wormedit::sys::selected_chart;
using wormedit::sys::visible_semantic_layer_by_id;

auto notification_severity_for(wormedit::diagnostic_severity_t severity)
    -> wormedit::notification_severity_t {
    switch (severity) {
    case wormedit::diagnostic_severity_t::info: return wormedit::notification_severity_t::info;
    case wormedit::diagnostic_severity_t::warning: return wormedit::notification_severity_t::warning;
    case wormedit::diagnostic_severity_t::error: return wormedit::notification_severity_t::error;
    }
    return wormedit::notification_severity_t::error;
}

void post_edit_rejection(wormedit::notification_sink_t& notifications,
                         const wormedit::diagnostic_list_t& diagnostics,
                         std::string_view fallback) {
    if (diagnostics.empty())
        notifications.post(wormedit::notification_severity_t::warning,
                           wormedit::notification_source_t::edit, fallback);
    else
        notifications.post(notification_severity_for(diagnostics.front().severity),
                           wormedit::notification_source_t::edit, diagnostics.front().message);
}

auto post_callback_feedback(wormedit::notification_sink_t& notifications,
                            wormedit::notification_severity_t severity,
                            wormedit::notification_source_t source, std::string message) -> std::string {
    if (severity == wormedit::notification_severity_t::hint) notifications.post_transient_hint(source, message);
    else notifications.post(severity, source, message);
    return message;
}

auto routine_transport_severity(wormedit::notification_severity_t severity, bool accepted)
    -> wormedit::notification_severity_t {
    if (!accepted || severity == wormedit::notification_severity_t::warning ||
        severity == wormedit::notification_severity_t::error) return severity;
    return wormedit::notification_severity_t::hint;
}

void remember_snap_denominator(application_command_state_t& application_state,
                               wormedit::chart_id_t chart, std::uint64_t denominator) {
    if (chart.value != 0 && denominator != 0) {
        application_state.project.compose_view.requested_snap_denominators.insert_or_assign(chart.value, denominator);
    }
}

template <typename Kind>
void remember_created_chart_snap(application_command_state_t& state, bool handled,
                                 Kind kind,
                                 std::optional<wormedit::chart_id_t> previous,
                                 std::optional<wormedit::chart_id_t> selected) {
    if (handled && kind == WORM_UI_PROJECT_CHART_NEW && selected && selected != previous) {
        remember_snap_denominator(state, *selected, state.preferences.editor_preferences.default_snap);
    }
}

auto refresh_timing_grid(const wormedit::chart_document_t& chart,
                         application_command_state_t& application_state) -> timing_grid_state_t {
    const auto snap = wormedit::filter_snap_denominators(
        chart.divisions_per_standard_measure, wormedit::sys::compose_snap_preferences(application_state, chart.id),
        requested_snap_denominator(application_state, chart.id));
    timing_grid_state_t state{.chart = chart.id,
                              .supported_denominators = snap.supported_denominators,
                              .active_denominator = snap.active_denominator,
                              .active_was_fallback = snap.active_was_fallback};
    // Preserve a selected valid denominator across timing gestures, while an
    // incompatible chart safely uses the exact one-row fallback.
    if (snap && !snap.inserted_resolution_fallback) {
        remember_snap_denominator(application_state, chart.id, snap.active_denominator);
    }
    return state;
}





















auto inline_preview_active(const application_command_state_t& state) -> bool {
    return state.services.transport_inline_active && state.services.transport_inline_active();
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
auto loop_region_for_selection(const wormedit::chart_document_t& chart,
                               wormedit::position_span_t selection,
                               std::uint64_t requested_begin_row,
                               std::uint64_t requested_end_row)
    -> std::optional<wormedit::audio::preview_loop_region_t> {
    using namespace wormedit;
    const auto built_measures = build_measure_index(chart);
    const auto* measures = std::get_if<measure_index_t>(&built_measures);
    if (!measures) return std::nullopt;
    const auto begin_row = absolute_row(*measures, selection.begin);
    const auto end_row = absolute_row(*measures, selection.end);
    if (!begin_row || !end_row || *begin_row != requested_begin_row ||
        *end_row != requested_end_row || *begin_row >= *end_row) return std::nullopt;
    const auto built_timing = build_timing_index(chart, {.allow_bounded_clock = true});
    if (!built_timing) return std::nullopt;
    const auto begin = sys::preview_cursor_frame(chart, *built_timing.index, selection.begin);
    const auto end = sys::preview_cursor_frame(chart, *built_timing.index, selection.end);
    const auto* begin_frame = std::get_if<std::uint64_t>(&begin);
    const auto* end_frame = std::get_if<std::uint64_t>(&end);
    if (!begin_frame || !end_frame || *begin_frame >= *end_frame) return std::nullopt;
    return audio::preview_loop_region_t{*begin_frame, *end_frame};
}














#endif

void refresh_transport_rate_commands(wormedit::command_registry_t& registry,
                                     const application_command_state_t& state) {
    (void)wormedit::update_command_state(registry, {"transport.toggle_metronome"},
                                       state.preferences.transport_metronome_available,
                                       state.preferences.transport_metronome_enabled);
    const bool available = static_cast<bool>(state.services.transport_set_playback_rate);
    (void)wormedit::update_command_state(registry, {"transport.rate_slower"},
                                       available && state.preferences.transport_rate_index > 0, std::nullopt);
    (void)wormedit::update_command_state(registry, {"transport.rate_faster"},
                                       available && state.preferences.transport_rate_index < 8, std::nullopt);
}

using wormedit::sys::external_player_runtime_t;

void initialize_program_preferences(wormedit::sys::program_preferences_runtime_t& preferences,
    external_player_runtime_t& player, application_command_state_t& state,
    wormedit::notification_sink_t& notifications) {
    player.program_preferences = &preferences;
    player.preferences = preferences.preferences.players;
    state.preferences.minimap_click_moves_paused_playhead = preferences.preferences.minimap.click_moves_paused_playhead;
    state.preferences.editor_preferences = preferences.preferences.editor;
    for (const auto& warning : preferences.warnings)
        notifications.post(wormedit::notification_severity_t::warning,
                           wormedit::notification_source_t::system, warning);
}

void initialize_preferred_transport(const wormedit::program_preferences_t& preferences,
    application_command_state_t& state) {
    const auto& rates = wormedit::audio::playback_rates;
    const auto found = std::find(rates.begin(), rates.end(), preferences.audio.playback_rate);
    if (found != rates.end())
        (void)wormedit::sys::set_transport_rate(state, static_cast<std::size_t>(found - rates.begin()));
    if (state.services.transport_set_keep_pitch && state.services.transport_set_keep_pitch(preferences.audio.keep_pitch))
        state.preferences.transport_keep_pitch = preferences.audio.keep_pitch;
}

constexpr auto application_primary_modifier() -> wormedit::shortcut_primary_modifier_t {
#if defined(__APPLE__)
    return wormedit::shortcut_primary_modifier_t::meta;
#else
    return wormedit::shortcut_primary_modifier_t::control;
#endif
}

auto shortcut_profile_path(const wormedit::sys::program_preferences_runtime_t& preferences)
    -> std::filesystem::path {
    auto path = std::filesystem::path{preferences.preferences.general.shortcut_profile_path};
    if (path.is_relative()) path = preferences.path.parent_path() / path;
    return path;
}

auto load_preferred_shortcuts(const wormedit::sys::program_preferences_runtime_t& preferences,
    wormedit::command_registry_t& registry) -> wormedit::shortcut_profile_startup_result_t {
    if (preferences.path.empty()) return {};
    return wormedit::load_shortcut_profile_for_startup(shortcut_profile_path(preferences), registry);
}

// Input & shortcuts editing: the projection feeds the Options page, and `capture` holds
// the command waiting for its next key press.
struct shortcut_editor_runtime_t {
    wormedit::sys::shortcut_rows_projection_t rows;
    std::optional<wormedit::command_id_t> capture;
};

// Fills the page rows only while Input & shortcuts is the visible category.
void attach_shortcut_rows(WormUiOptionsView& view, const wormedit::sys::options_runtime_t& options,
    shortcut_editor_runtime_t& editor, const wormedit::command_registry_t& registry) {
    if (!options.visible || options.category != WORM_UI_OPTIONS_INPUT) {
        editor.capture.reset();
        return;
    }
    wormedit::sys::project_shortcut_rows(registry, application_primary_modifier(), editor.rows);
    view.shortcut_rows = editor.rows.rows.data();
    view.shortcut_row_count = editor.rows.rows.size();
    const auto capturing = editor.capture ? std::ranges::find(editor.rows.commands, *editor.capture)
                                          : editor.rows.commands.end();
    view.shortcut_capturing = capturing != editor.rows.commands.end();
    view.shortcut_capture_row = view.shortcut_capturing
        ? static_cast<std::size_t>(capturing - editor.rows.commands.begin()) : 0;
    if (!view.shortcut_capturing) editor.capture.reset();
}

// Returns true when the action belonged to the shortcut editor and was consumed here.
auto handle_shortcut_editor_action(shortcut_editor_runtime_t& editor, const WormUiOptionsAction& action,
    const wormedit::sys::options_runtime_t& options, wormedit::notification_sink_t& notifications) -> bool {
    if (action.kind != WORM_UI_OPTIONS_SHORTCUT_CAPTURE && action.kind != WORM_UI_OPTIONS_SHORTCUT_CANCEL)
        return false;
    if (action.revision != options.revision) return true;
    if (action.kind == WORM_UI_OPTIONS_SHORTCUT_CANCEL) {
        editor.capture.reset();
        return true;
    }
    if (action.shortcut_row >= editor.rows.commands.size()) {
        notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::input,
                           "That shortcut row is no longer available.");
        return true;
    }
    editor.capture = editor.rows.commands[action.shortcut_row];
    return true;
}

// Remaps and persists one captured chord. The in-memory mapping stays even when the save fails.
void remap_and_save_shortcut(wormedit::command_registry_t& registry, const wormedit::command_id_t& id,
    const wormedit::shortcut_chord_t& chord, const wormedit::sys::program_preferences_runtime_t& preferences,
    wormedit::notification_sink_t& notifications) {
    const auto remapped = wormedit::remap_command(registry, id, {chord});
    if (!remapped.applied) {
        notifications.post(wormedit::notification_severity_t::error, wormedit::notification_source_t::input,
                           remapped.diagnostics.empty() ? "Shortcut could not be changed."
                                                        : remapped.diagnostics.front().message);
        return;
    }
    const auto saved = wormedit::save_shortcut_profile_atomic(shortcut_profile_path(preferences),
                                                              wormedit::make_shortcut_profile(registry, "user"));
    if (saved)
        notifications.post(wormedit::notification_severity_t::success, wormedit::notification_source_t::input,
                           "Shortcut saved: " + id.value);
    else
        notifications.post(wormedit::notification_severity_t::error, wormedit::notification_source_t::system,
                           "Shortcut kept for this session, but the profile was not saved: " + saved.error);
    if (!remapped.diagnostics.empty())
        notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::input,
                           "Shortcut conflict: " + remapped.diagnostics.front().message);
}

// Consumes the frame's key batch while a capture is waiting, so no command runs from it.
void consume_shortcut_capture(shortcut_editor_runtime_t& editor, WormUiWorkspaceAction& action,
    wormedit::command_registry_t& registry, const wormedit::sys::program_preferences_runtime_t& preferences,
    wormedit::notification_sink_t& notifications) {
    if (!editor.capture) return;
    const auto captured = wormedit::sys::capture_shortcut_batch({action.shortcut_events, action.shortcut_event_count});
    if (!captured.cancel && !captured.chord) return;
    action.shortcut_event_count = 0;
    const auto id = *editor.capture;
    editor.capture.reset();
    if (captured.cancel) {
        notifications.post(wormedit::notification_severity_t::info, wormedit::notification_source_t::input,
                           "Shortcut change canceled.");
        return;
    }
    remap_and_save_shortcut(registry, id, *captured.chord, preferences, notifications);
}

void persist_session_preferences(wormedit::sys::program_preferences_runtime_t& runtime,
    const application_command_state_t& state, wormedit::notification_sink_t& notifications) {
    auto candidate = runtime.preferences;
    const bool minimap_changed = candidate.minimap.click_moves_paused_playhead !=
                                state.preferences.minimap_click_moves_paused_playhead;
    candidate.minimap.click_moves_paused_playhead = state.preferences.minimap_click_moves_paused_playhead;
    candidate.editor.scroll_directions = state.preferences.editor_preferences.scroll_directions;
    if (state.preferences.transport_keep_pitch_available && state.services.transport_set_playback_rate)
        candidate.audio.playback_rate = wormedit::audio::playback_rates[
            std::min(state.preferences.transport_rate_index, wormedit::audio::playback_rates.size() - 1)];
    if (state.preferences.transport_keep_pitch_available) candidate.audio.keep_pitch = state.preferences.transport_keep_pitch;
    if (candidate == runtime.preferences) return;
    const auto error = wormedit::sys::apply_program_preferences(runtime, std::move(candidate));
    if (!error.empty())
        notifications.post(wormedit::notification_severity_t::error,
                           wormedit::notification_source_t::system, error);
    else if (minimap_changed)
        notifications.post(wormedit::notification_severity_t::success,
                           wormedit::notification_source_t::system,
                           "Minimap paused-click preference saved.");
}

void update_scratch_presentation(const wormedit::display_preferences_t& display,
                                 application_command_state_t& state) {
    std::array<std::optional<bool>, 2> skin_defaults;
    for (std::uint8_t index = 0; index < skin_defaults.size(); ++index) {
        const auto side = worm_ui_frontend_field_scratch_side(index + 1);
        if (side >= 0) skin_defaults[index] = side != 0;
    }
    state.scratch_on_right = wormedit::sys::resolve_scratch_positions(display, skin_defaults);
}

auto active_field_skin_layout(const wormedit::song_document_t& document,
    const wormedit::workspace_controller_t& workspace,
    const std::optional<wormedit::editor_interaction_state_t>& interaction)
    -> const wormedit::lane_layout_t* {
    if (!workspace.selected_chart) return nullptr;
    const auto chart = std::ranges::find(document.charts, *workspace.selected_chart,
        &wormedit::chart_document_t::id);
    if (chart == document.charts.end() || chart->lane_layouts.empty()) return nullptr;
    if (interaction && interaction->chart == chart->id) {
        if (const auto* segment = wormedit::active_lane_layout_segment(*chart, interaction->cursor))
            return &segment->layout;
    }
    return &chart->lane_layouts.front().layout;
}

auto configure_field_skin(const wormedit::lane_layout_t* layout,
    wormedit::sys::options_runtime_t& options, const wormedit::display_preferences_t& saved,
    application_command_state_t& state) -> std::vector<WormUiComposeLaneView> {
    const auto key = layout ? wormedit::field_skin_layout_key(*layout) : std::string{};
    wormedit::sys::set_options_field_layout(options, key, layout ? layout->display_name : "");
    const auto& display = options.visible ? options.draft.display : saved;
    std::vector<WormUiComposeLaneView> lanes;
    if (layout) {
        lanes.reserve(layout->lanes.size());
        for (const auto& lane : layout->lanes)
            lanes.push_back({.lane_id = lane.id.value, .display_order = lane.display_order,
                .role = static_cast<std::uint8_t>(lane.role), .stage = static_cast<std::uint8_t>(lane.stage),
                .glyph = static_cast<std::uint8_t>(lane.glyph), .key_color = static_cast<std::uint8_t>(lane.key_color)});
    }
    const auto requested = wormedit::resolve_field_scheme(display, key);
    // Preferences own their strings through the immediate frontend call.
    worm_ui_frontend_configure_field_scheme(std::string(requested).c_str(), display.field_scheme.c_str(),
        lanes.data(), lanes.size());
    update_scratch_presentation(display, state);
    return lanes;
}



// Shortcut-editor actions never reach the program-preference runtime.


auto external_source_measure(const wormedit::chart_document_t& chart,
    const std::optional<wormedit::editor_interaction_state_t>& interaction) -> std::optional<std::uint64_t> {
    if (!interaction || interaction->chart != chart.id) return std::nullopt;
    const auto built = wormedit::build_virtual_canvas(chart);
    const auto* canvas = std::get_if<wormedit::virtual_canvas_t>(&built);
    if (!canvas) return std::nullopt;
    const auto row = interaction->virtual_bms_cursor_row
        ? interaction->virtual_bms_cursor_row : wormedit::absolute_row(canvas->authored_index, interaction->cursor);
    const auto measure = row ? canvas->measure_containing(*row) : std::nullopt;
    return measure ? std::optional<std::uint64_t>{measure->number} : std::nullopt;
}



void project_external_player_view(external_player_runtime_t& runtime, WormUiWorkspaceView& view) {
    runtime.views.clear();
    for (const auto& player : runtime.preferences.players)
        runtime.views.push_back({player.name.c_str(), player.path.c_str(), player.start_arguments.c_str(),
                                 player.here_arguments.c_str(), player.stop_arguments.c_str()});
    view.external_player = {runtime.visible, runtime.revision, runtime.views.data(),
                           runtime.views.size(), runtime.preferences.selected};
}

auto edit_external_player_preferences(external_player_runtime_t& runtime,
    const WormUiExternalPlayerAction& action, wormedit::notification_sink_t& notifications) -> std::string {
    auto candidate = runtime.preferences;
    switch (action.kind) {
    case WORM_UI_EXTERNAL_PLAYER_SELECT:
        if (action.selected >= candidate.players.size()) return post_callback_feedback(notifications,
            wormedit::notification_severity_t::warning, wormedit::notification_source_t::system, "External player selection is invalid.");
        candidate.selected = action.selected;
        break;
    case WORM_UI_EXTERNAL_PLAYER_SAVE:
        if (action.selected >= candidate.players.size()) return post_callback_feedback(notifications,
            wormedit::notification_severity_t::warning, wormedit::notification_source_t::system, "External player selection is invalid.");
        candidate.players[action.selected] = {action.name, action.path, action.start_arguments,
                                              action.here_arguments, action.stop_arguments};
        break;
    case WORM_UI_EXTERNAL_PLAYER_ADD:
        candidate.players.push_back({"New player", "", "\"<filename>\"", "-s <measure> \"<filename>\"", "-t"});
        candidate.selected = candidate.players.size() - 1;
        break;
    case WORM_UI_EXTERNAL_PLAYER_REMOVE:
        if (candidate.players.size() <= 1) return post_callback_feedback(notifications,
            wormedit::notification_severity_t::warning, wormedit::notification_source_t::system, "Keep at least one external player definition.");
        candidate.players.erase(candidate.players.begin() + static_cast<std::ptrdiff_t>(candidate.selected));
        candidate.selected = 0;
        break;
    default: return {};
    }
    if (!wormedit::valid_external_player_preferences(candidate)) return post_callback_feedback(notifications,
        wormedit::notification_severity_t::warning, wormedit::notification_source_t::system, "External-player preferences are invalid.");
    auto program = runtime.program_preferences->preferences;
    program.players = candidate;
    const auto error = wormedit::sys::apply_program_preferences(*runtime.program_preferences, std::move(program));
    runtime.preferences = std::move(candidate);
    ++runtime.revision;
    return post_callback_feedback(notifications,
        error.empty() ? wormedit::notification_severity_t::success : wormedit::notification_severity_t::error,
        wormedit::notification_source_t::system, error.empty() ? "External-player preferences saved." : error);
}

auto dispatch_external_player_action(external_player_runtime_t& runtime,
    const WormUiExternalPlayerAction& action, const application_command_state_t& state,
    wormedit::notification_sink_t& notifications) -> bool {
    using wormedit::sys::external_player_action_t;
    if (action.kind == WORM_UI_EXTERNAL_PLAYER_NONE) return false;
    if (action.kind == WORM_UI_EXTERNAL_PLAYER_OPEN) runtime.visible = true;
    else if (action.kind == WORM_UI_EXTERNAL_PLAYER_CLOSE) runtime.visible = false;
    else if (action.kind == WORM_UI_EXTERNAL_PLAYER_PLAY_START && state.services.external_player)
        (void)state.services.external_player(external_player_action_t::play_start);
    else if (action.kind == WORM_UI_EXTERNAL_PLAYER_PLAY_HERE && state.services.external_player)
        (void)state.services.external_player(external_player_action_t::play_here);
    else if (action.kind == WORM_UI_EXTERNAL_PLAYER_STOP && state.services.external_player)
        (void)state.services.external_player(external_player_action_t::stop);
    else (void)edit_external_player_preferences(runtime, action, notifications);
    return true;
}

auto open_project_chart(wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& interaction, wormedit::chart_id_t chart)
    -> std::string {
    const auto selected = wormedit::apply_workspace_action(
        workspace, history.document, wormedit::workspace_select_chart_action_t{chart});
    if (!selected) return selected.diagnostics.empty() ? "Chart selection failed." : selected.diagnostics.front().message;
    const auto opened = wormedit::apply_workspace_action(
        workspace, history.document,
        wormedit::workspace_switch_tab_action_t{wormedit::workspace_tab_t::compose});
    if (!opened) return opened.diagnostics.empty() ? "Compose could not be opened." : opened.diagnostics.front().message;
    interaction.reset();
    return {};
}

void remember_project_file(wormedit::sys::program_preferences_runtime_t& preferences,
                           const std::filesystem::path& path, wormedit::notification_sink_t& notifications) {
    const auto warning = wormedit::sys::remember_recent_file(preferences, path);
    if (!warning.empty()) notifications.post(wormedit::notification_severity_t::warning,
                                            wormedit::notification_source_t::system, warning);
}

void remember_startup_file(wormedit::sys::program_preferences_runtime_t& preferences,
                          bool loaded, const std::filesystem::path& path) {
    if (!loaded) return;
    const auto warning = wormedit::sys::remember_recent_file(preferences, path);
    if (!warning.empty()) preferences.warnings.push_back(warning);
}

// Project tab "Import chart…": begin, read a chart file, tick difficulties, import or cancel.
// Reading and planning never change the document; Import commits one undoable command.




auto layer_tool_target_editable(const wormedit::chart_document_t* chart,
    std::optional<wormedit::layer_id_t> target) -> bool {
    if (!chart || !target) return false;
    const auto layer = std::ranges::find(chart->semantic_layers, *target,
                                        &wormedit::semantic_layer_t::id);
    return layer != chart->semantic_layers.end() && layer->visible && !layer->locked;
}

void refresh_note_kind_commands(wormedit::command_registry_t& registry,
    const wormedit::chart_document_t* chart,
    const std::optional<wormedit::editor_interaction_state_t>& compose_state,
    bool compose_ready, bool pending, const application_command_state_t& application) {
    using namespace wormedit;
    const auto paint_active = application.project.compose_gesture.note_paint_mode_active;
    const auto layer_active = application.project.compose_gesture.semantic_layer_paint_mode_active;
    const auto tools_ready = compose_ready && !application.project.compose_view.compose_timing_focus;
    const auto layer_ready = tools_ready &&
        layer_tool_target_editable(chart, application.project.semantic_layer.semantic_layer_target);
    (void)update_command_state(registry, command_id_t{"edit.tool_select"}, compose_ready,
                                compose_ready && !paint_active && !layer_active);
    (void)update_command_state(registry, command_id_t{"edit.tool_draw"}, tools_ready,
                              tools_ready && paint_active);
    (void)update_command_state(registry, command_id_t{"edit.tool_layer"}, layer_ready,
                              layer_ready && layer_active);
    constexpr std::pair<std::string_view, note_kind_t> note_kinds[]{
        {"tap", note_kind_t::tap}, {"hold", note_kind_t::hold}, {"mine", note_kind_t::mine},
        {"invisible", note_kind_t::invisible}, {"lift", note_kind_t::lift},
        {"fake", note_kind_t::fake}, {"roll", note_kind_t::roll}};
    for (const auto& [name, kind] : note_kinds)
        (void)update_command_state(registry, command_id_t{"edit.note_kind_" + std::string(name)},
            compose_ready && note_kind_allowed(*chart, kind) && !pending,
            compose_ready && paint_active && compose_state->armed_note_kind == kind);
}

auto has_clipboard_selection(bool compose_ready, bool pending,
    const std::optional<wormedit::editor_interaction_state_t>& state,
    std::uint64_t kiai_selected_head) -> bool {
    return compose_ready && !pending &&
        (!state->selected_objects.empty() || !state->selected_notes.empty() ||
         kiai_selected_head != 0);
}

auto play_stop_available(const wormedit::chart_document_t* chart,
    const application_command_state_t& state) -> bool {
    return chart && state.services.transport_preview_playing && state.services.transport_play_pause &&
           state.services.transport_stop;
}

auto paste_target_available(bool compose_ready, bool pending,
                            const application_command_state_t& state) -> bool {
    return compose_ready && !pending && state.project.clipboard.chart_clipboard.has_value();
}

void refresh_semantic_membership_commands(wormedit::command_registry_t& registry,
    bool compose_ready, bool target_valid, const application_command_state_t& app,
    const std::optional<wormedit::editor_interaction_state_t>& state) {
    using namespace wormedit;
    const auto any_target = target_valid || app.project.semantic_layer.semantic_layer_target_none;
    const auto can_apply = compose_ready && any_target && !state->selected_notes.empty();
    (void)update_command_state(registry, command_id_t{"semantic_layer.clear_target"},
                               compose_ready && any_target, !any_target);
    (void)update_command_state(registry, command_id_t{"semantic_layer.apply"}, can_apply, std::nullopt);
    (void)update_command_state(registry, command_id_t{"semantic_layer.remove"}, can_apply && target_valid, std::nullopt);
    (void)update_command_state(registry, command_id_t{"semantic_layer.toggle"}, can_apply && target_valid, std::nullopt);
}

auto bgm_move_enabled(const wormedit::chart_document_t* chart,
                       const std::optional<wormedit::editor_interaction_state_t>& state,
                       bool ready, bool pending, const application_command_state_t& application) -> bool {
    return ready && !pending && chart && state &&
        wormedit::sys::compose_move_to_bgm_available(*chart, *state, application);
}

void attach_bgm_shortcut(WormUiComposeView& view, const wormedit::command_registry_t& registry) {
    const auto* command = wormedit::find_command(registry, {"edit.move_to_bgm"});
    const auto shortcut = command && !command->current_shortcuts.empty()
        ? wormedit::sys::shortcut_chord_text(command->current_shortcuts.front()) : std::string{};
    std::snprintf(view.move_to_bgm_shortcut, sizeof(view.move_to_bgm_shortcut), "%s", shortcut.c_str());
}

void refresh_same_sound_command(wormedit::command_registry_t& registry,
    const wormedit::document_history_t& history, const wormedit::chart_document_t* chart,
    const std::optional<wormedit::editor_interaction_state_t>& state, bool ready,
    const application_command_state_t& application) {
    const bool enabled = ready && wormedit::sys::compose_sound_selection_available(history.document, *chart, *state, application);
    (void)wormedit::update_command_state(registry, {"selection.select_same_sound_chart"}, enabled, std::nullopt);
}

void refresh_quantize_commands(wormedit::command_registry_t& registry, bool ready, bool pending,
    const std::optional<wormedit::editor_interaction_state_t>& state,
    const wormedit::document_history_t& history, const application_command_state_t& application) {
    const bool enabled = ready && !pending && !application.project.compose_view.compose_timing_focus && !application.project.clipboard.paste_session;
    const bool checked = enabled && application.project.quantize.quantize_editor &&
        application.project.quantize.quantize_editor->stamp.is_identity_current(history.document, state->chart) && application.project.quantize.quantize_editor->preview_enabled;
    (void)wormedit::update_command_state(registry, {"edit.toggle_quantize_preview"}, enabled, checked);
    const bool selected = enabled && (!state->selected_notes.empty() || !state->selected_objects.empty());
    (void)wormedit::update_command_state(registry, {"edit.apply_quantize"}, selected, std::nullopt);
}

void refresh_compose_dock_commands(wormedit::command_registry_t& registry, bool compose_ready,
                                    bool pending, const application_command_state_t& application) {
    const bool enabled = compose_ready && !pending && !application.project.compose_view.compose_timing_focus &&
                         !application.project.clipboard.paste_session;
    for (const auto id : {"view.open_layers", "semantic_layer.create", "view.toggle_sounds"})
        (void)wormedit::update_command_state(registry, {id}, enabled, std::nullopt);
}

auto sound_picker_command_available(bool compose_ready, bool pending) -> bool {
    return compose_ready && !pending;
}

auto project_export_available(const wormedit::chart_document_t* chart,
    const std::optional<wormedit::editor_interaction_state_t>& compose,
    const application_command_state_t& application) -> bool {
    return chart && !application.project.clipboard.paste_session &&
        (!compose || !editor_interaction_has_pending_gesture(*compose));
}

void refresh_application_command_states(
    wormedit::command_registry_t& registry, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    const std::optional<wormedit::editor_interaction_state_t>& compose_state,
    application_command_state_t& application_state) {
    using namespace wormedit;
    const auto* chart = selected_chart(history, workspace);
    const bool compose_ready = workspace.active_tab == workspace_tab_t::compose && chart &&
                               compose_state && compose_state->chart == chart->id;
    const bool pending = compose_ready && editor_interaction_has_pending_gesture(*compose_state);
    bool target_valid{};
    if (chart && application_state.project.semantic_layer.semantic_layer_target) {
        const auto target_layer =
            std::ranges::find(chart->semantic_layers, *application_state.project.semantic_layer.semantic_layer_target,
                              &semantic_layer_t::id);
        target_valid = target_layer != chart->semantic_layers.end();
        if (!target_valid) {
            application_state.project.semantic_layer.semantic_layer_target.reset();
            application_state.project.semantic_layer.semantic_layer_target_none = false;
            application_state.project.compose_gesture.semantic_layer_paint_mode_active = false;
            application_state.project.semantic_layer.semantic_layer_solo_active = false;
        } else if (target_layer->locked || !target_layer->visible) {
            application_state.project.compose_gesture.semantic_layer_paint_mode_active = false;
        }
    }
    const auto set = [&](std::string id, bool enabled, std::optional<bool> checked = std::nullopt) {
        (void)update_command_state(registry, command_id_t{std::move(id)}, enabled, checked);
    };
    set("document.new_project", true);
    set("document.new_chart", chart != nullptr);
    set("history.undo", !history.undo.empty());
    set("history.redo", !history.redo.empty());
    set("edit.delete", compose_ready);
    const bool can_move_bgm = bgm_move_enabled(chart, compose_state, compose_ready, pending, application_state);
    set("edit.move_to_bgm", can_move_bgm);
    set("edit.move_next_to_bgm", can_move_bgm);
    const bool clipboard_selection = has_clipboard_selection(compose_ready, pending, compose_state,
                                                          application_state.project.timing_recipe.kiai_selected_head);
    set("edit.copy", clipboard_selection);
    set("edit.cut", clipboard_selection);
    set("edit.paste", paste_target_available(compose_ready, pending, application_state));
    refresh_note_kind_commands(registry, chart, compose_state, compose_ready, pending,
                                application_state);

    set("selection.clear", compose_ready && (inline_preview_active(application_state) ||
                                            application_state.project.clipboard.paste_session.has_value() ||
                                            pending || !compose_state->selected_objects.empty() ||
                                             application_state.project.compose_gesture.note_paint_mode_active ||
                                             application_state.project.compose_gesture.semantic_layer_paint_mode_active));
    refresh_same_sound_command(registry, history, chart, compose_state, compose_ready, application_state);
    set("selection.select_all", compose_ready);
    set("selection.select_all_objects", compose_ready);
    set("selection.invert", compose_ready);
    const bool marker_navigation_available =
        compose_ready && wormedit::has_marker_navigation_targets(*chart);
    set("navigation.previous_marker", marker_navigation_available);
    set("navigation.next_marker", marker_navigation_available);
    set("navigation.go_to_sound", sound_picker_command_available(compose_ready, pending));
    refresh_compose_dock_commands(registry, compose_ready, pending, application_state);
    set("document.export_output_group", project_export_available(chart, compose_state, application_state));
    refresh_quantize_commands(registry, compose_ready, pending, compose_state, history, application_state);
    set("navigation.previous_bookmark", compose_ready);
    set("navigation.next_bookmark", compose_ready);
    set("markers.add_bookmark", compose_ready);
    set("navigation.previous_snap", compose_ready);
    set("navigation.next_snap", compose_ready);
    set("transport.play_stop", play_stop_available(chart, application_state));
    set("transport.play_pause",
        chart != nullptr && static_cast<bool>(application_state.services.transport_play_pause));
    set("transport.play_from_start",
        chart != nullptr && static_cast<bool>(application_state.services.transport_play_from_start));
    refresh_transport_rate_commands(registry, application_state);
    set("transport.stop",
        application_state.services.transport_active && application_state.services.transport_active());

    constexpr std::array<std::pair<std::string_view, workspace_tab_t>, 7> tabs{{
        {"workspace.open_setup", workspace_tab_t::setup},
        {"workspace.open_metadata", workspace_tab_t::metadata},
        {"workspace.open_timing", workspace_tab_t::compose},
        {"workspace.open_compose", workspace_tab_t::compose},
        {"workspace.open_storyboard", workspace_tab_t::storyboard},
        {"workspace.open_audio_slices", workspace_tab_t::audio_slices},
        {"workspace.open_compare_validate", workspace_tab_t::compare_validate},
    }};
    for (const auto& [id, tab] : tabs) {
        const bool timing_alias = id == "workspace.open_timing";
        set(std::string{id}, tab != workspace_tab_t::storyboard,
            workspace.active_tab == tab &&
                timing_alias == application_state.project.compose_view.compose_timing_focus);
    }
    set("timing.toggle_lock",
        workspace.active_tab == workspace_tab_t::compose &&
            application_state.project.compose_view.compose_timing_focus,
        application_state.project.timing_recipe.timing_locked);
    set("timing.insert_bpm", compose_ready && !application_state.project.timing_recipe.timing_locked);
    set("timing.insert_sv", compose_ready && !application_state.project.timing_recipe.timing_locked);
    set("timing.open_settings", true);

    const auto palette = compose_ready
                             ? make_semantic_layer_palette_window(
                                   *chart, application_state.project.semantic_layer.semantic_layer_target,
                                   application_state.project.semantic_layer.semantic_layer_palette_chart == chart->id
                                       ? application_state.project.semantic_layer.semantic_layer_palette_page
                                       : 0,
                                   false, WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY)
                             : std::nullopt;
    for (std::size_t index = 0; index < WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY; ++index) {
        const auto* layer = palette && index < palette->layer_indices.size()
                                ? &chart->semantic_layers[palette->layer_indices[index]]
                                : nullptr;
        const bool available = layer != nullptr;
        const bool checked = available && application_state.project.semantic_layer.semantic_layer_target &&
                             layer->id == *application_state.project.semantic_layer.semantic_layer_target;
        set("semantic_layer.select_slot_" + std::to_string(index + 1), available, checked);
    }
    refresh_semantic_membership_commands(registry, compose_ready, target_valid,
                                          application_state, compose_state);
    set("audio.paint_hitsound", false);
    set("audio.toggle_click_audition", true, application_state.preferences.editor_preferences.audition_on_click);
}

auto application_command_focus(const WormUiWorkspaceAction& action,
                               const wormedit::workspace_controller_t& workspace)
    -> wormedit::command_focus_t {
    using namespace wormedit;
    command_focus_t focus{
        .text_entry_active = action.shortcut_text_entry_active,
        .ime_composition_active = action.shortcut_ime_composition_active,
        .workspace_active = action.shortcut_window_focused,
        .active_tab = action.shortcut_notification_history_focused
            ? std::optional<workspace_tab_t>{} : std::optional{workspace.active_tab},
        .visible_tabs = {workspace_tab_t::setup, workspace_tab_t::metadata,
                         workspace_tab_t::compose, workspace_tab_t::storyboard,
                         workspace_tab_t::audio_slices, workspace_tab_t::compare_validate},
    };
    if (!action.shortcut_notification_history_focused &&
        action.shortcut_compose_lane_focused && action.shortcut_focused_lane_id != 0) {
        focus.focused_lane = lane_id_t{action.shortcut_focused_lane_id};
    }
    return focus;
}


void post_shortcut_rejection(wormedit::notification_sink_t& notifications,
                            const wormedit::command_registry_t& registry,
                            const wormedit::command_dispatch_result_t& result) {
    // A controller posts its own rejection once. Registry-level failures have
    // no controller, so the input boundary owns their notification.
    const auto* command = result.command ? wormedit::find_command(registry, *result.command) : nullptr;
    if (result.status == wormedit::command_dispatch_status_t::rejected && command &&
        command->definition.invoke)
        return;
    if (result.diagnostics.empty())
        notifications.post(wormedit::notification_severity_t::warning,
                           wormedit::notification_source_t::input, "Shortcut command was rejected.");
    else
        notifications.post(notification_severity_for(result.diagnostics.front().severity),
                           wormedit::notification_source_t::input, result.diagnostics.front().message);
}

void consume_menu_shortcut(WormUiWorkspaceAction& action,
                                  const wormedit::command_registry_t& registry) {
    const auto* command = wormedit::find_command(registry, {action.menu_command_id});
    if (!command || command->current_shortcuts.empty()) return;
    const auto& chord = command->current_shortcuts.front();
    auto modifiers = chord.modifiers;
    const auto primary = static_cast<wormedit::shortcut_modifiers_t>(wormedit::shortcut_modifier_t::primary);
    if (modifiers & primary) {
        modifiers &= ~primary;
#if defined(__APPLE__)
        modifiers |= WORM_UI_SHORTCUT_MODIFIER_META;
#else
        modifiers |= WORM_UI_SHORTCUT_MODIFIER_CONTROL;
#endif
    }
    std::size_t retained = 0;
    for (std::size_t i = 0; i < action.shortcut_event_count; ++i) {
        const auto& event = action.shortcut_events[i];
        if (event.hid_usage == static_cast<std::uint16_t>(chord.key) && event.modifiers == modifiers) continue;
        action.shortcut_events[retained++] = event;
    }
    action.shortcut_event_count = retained;
}

void project_command_menu(const wormedit::command_registry_t& registry, WormUiWorkspaceView& view) {
    view.command_menu_count = std::min(registry.commands.size(),
        static_cast<std::size_t>(WORM_UI_COMMAND_MENU_CAPACITY));
    for (std::size_t i = 0; i < view.command_menu_count; ++i) {
        const auto& command = registry.commands[i];
        view.command_menu_rows[i] = {command.definition.id.value.c_str(),
                                     command.definition.display_name.c_str(),
                                     command.definition.enabled};
    }
}

auto command_menu_invoker(wormedit::command_invoker_t invoke) -> wormedit::command_invoker_t {
    return [invoke = std::move(invoke)](const wormedit::command_invocation_context_t& invocation) {
        if (invocation.command.value == "workspace.command_menu")
            return wormedit::command_invocation_result_t{.accepted = worm_ui_frontend_open_command_menu()};
        return invoke(invocation);
    };
}








void delete_staged_timing_event(const WormUiWorkspaceAction& action,
                                wormedit::document_history_t& history, bool timing_locked,
                                wormedit::timing_lane_pseudonote_preview_t& stage,
                                wormedit::notification_sink_t& notifications) {
    const auto kind = timing_lane_kind_from_ui(action.timing_lane);
    if (!stage.existing_event || !kind || *kind != stage.kind) {
        post_edit_rejection(notifications, {}, "The selected timing event cannot be deleted.");
    } else {
        const auto result = wormedit::delete_existing_timing_lane_event(
            history, stage.stamp.chart, *kind, *stage.existing_event, timing_locked);
        if (result)
            notifications.post(wormedit::notification_severity_t::success,
                               wormedit::notification_source_t::edit, "Timing event deleted.");
        else
            post_edit_rejection(notifications, result.diagnostics, "Timing event deletion was rejected.");
    }
    wormedit::cancel_timing_lane_pseudonote(stage);
}





// Timing focus: begin a new event with its default value and confirm it at once.
void quick_insert_timing_event(const WormUiWorkspaceAction& action,
                               wormedit::document_history_t& history,
                               const wormedit::workspace_controller_t& workspace,
                               bool timing_locked, std::uint64_t active_snap_denominator,
                               wormedit::notification_sink_t& notifications) {
    auto candidate = begin_timing_stage(action, history, workspace, timing_locked,
                                        active_snap_denominator);
    if (candidate.status != wormedit::timing_lane_pseudonote_status_t::editing) {
        post_edit_rejection(notifications, candidate.diagnostics, "Timing event could not be inserted at that row.");
        return;
    }
    const auto result = wormedit::apply_timing_lane_pseudonote(history, candidate, timing_locked);
    if (result)
        notifications.post(wormedit::notification_severity_t::success,
                           wormedit::notification_source_t::edit, "Timing event inserted.");
    else
        post_edit_rejection(notifications, result.diagnostics, "Timing event insertion was rejected.");
}



auto post_workspace_selection_feedback(wormedit::notification_sink_t& notifications,
    const wormedit::workspace_action_result_t& result, std::string accepted, std::string rejected) -> std::string {
    if (result) return post_callback_feedback(notifications, wormedit::notification_severity_t::hint,
                                             wormedit::notification_source_t::input, std::move(accepted));
    if (!result.diagnostics.empty())
        return post_callback_feedback(notifications, notification_severity_for(result.diagnostics.front().severity),
                                      wormedit::notification_source_t::input, result.diagnostics.front().message);
    return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
                                  wormedit::notification_source_t::input, std::move(rejected));
}

auto dispatch_comparison_selection(const WormUiWorkspaceAction& action,
                                   const wormedit::document_history_t& history,
                                   wormedit::workspace_controller_t& workspace,
                                   wormedit::notification_sink_t& notifications) -> std::string {
    const auto selected = wormedit::chart_id_t{action.compare_chart_id};
    const auto first = workspace.selected_chart.value_or(selected);
    auto second = selected;
    if (first == second) {
        const auto other = std::ranges::find_if(history.document.charts,
            [first](const wormedit::chart_document_t& chart) { return chart.id != first; });
        if (other == history.document.charts.end()) return post_callback_feedback(notifications, wormedit::notification_severity_t::hint,
            wormedit::notification_source_t::input, "Add another difficulty to compare.");
        second = other->id;
    }
    const auto applied = wormedit::apply_workspace_action(
        workspace, history.document,
        wormedit::workspace_set_comparison_charts_action_t{{first, second}});
    return post_workspace_selection_feedback(notifications, applied,
        "Comparison difficulty selected.", "Comparison selection rejected.");
}

auto dispatch_sound_resource_selection(const WormUiWorkspaceAction& action,
    const wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    wormedit::notification_sink_t& notifications)
    -> std::string {
    auto asset = wormedit::asset_id_t{action.audio_asset_id};
    if (action.audio_resource_id != 0) {
        const auto resource = std::ranges::find(history.document.resources,
            wormedit::resource_id_t{action.audio_resource_id}, &wormedit::resource_entry_t::id);
        if (resource == history.document.resources.end() ||
            (action.kind == WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET && resource->asset != asset))
            return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
            wormedit::notification_source_t::input, "The selected sound changed; select it again.");
        asset = resource->asset;
    } else if (action.kind == WORM_UI_WORKSPACE_ACTION_SELECT_SOUND_RESOURCE) {
        return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
            wormedit::notification_source_t::input, "No sound resource was selected.");
    }
    if (!asset) return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
            wormedit::notification_source_t::input, "Sound resource selected; no audio asset is available.");
    const auto applied = wormedit::apply_workspace_action(
        workspace, history.document,
        wormedit::workspace_select_asset_action_t{.asset = asset,
                                                  .slot = wormedit::workspace_asset_slot_t::audio});
    return post_workspace_selection_feedback(notifications, applied,
        "Audio asset selected; waveform analysis scheduled.", "Audio asset selection rejected.");
}

auto dispatch_workspace_selection_action(const WormUiWorkspaceAction& action,
                                         const wormedit::document_history_t& history,
                                         wormedit::workspace_controller_t& workspace,
                                         wormedit::notification_sink_t& notifications)
    -> std::optional<std::string> {
    switch (action.kind) {
    case WORM_UI_WORKSPACE_ACTION_SELECT_SOUND_RESOURCE:
    case WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET:
        return dispatch_sound_resource_selection(action, history, workspace, notifications);
    case WORM_UI_WORKSPACE_ACTION_SELECT_VISUAL_ASSET: {
        const auto applied = wormedit::apply_workspace_action(
            workspace, history.document,
            wormedit::workspace_select_asset_action_t{
                .asset = wormedit::asset_id_t{action.visual_asset_id},
                .slot = wormedit::workspace_asset_slot_t::visual});
        return post_workspace_selection_feedback(notifications, applied,
            "Image asset selected.", "Image asset selection rejected.");
    }
    case WORM_UI_WORKSPACE_ACTION_SWITCH_TAB: {
        const auto applied = wormedit::apply_workspace_action(
            workspace, history.document,
            wormedit::workspace_switch_tab_action_t{
                static_cast<wormedit::workspace_tab_t>(action.tab)});
        return post_workspace_selection_feedback(notifications, applied,
            "Workspace tab changed", "Workspace action rejected");
    }
    case WORM_UI_WORKSPACE_ACTION_COMPARE_SELECT_CHART: {
        return dispatch_comparison_selection(action, history, workspace, notifications);
    }
    default:
        return std::nullopt;
    }
}



void cancel_stale_compose_interaction(
    std::optional<wormedit::editor_interaction_state_t>& compose_interaction_state,
    wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace) {
    if (compose_interaction_state) {
        const auto& state = *compose_interaction_state;
        const bool selected_chart_changed =
            !workspace.selected_chart || *workspace.selected_chart != state.chart;
        const bool pending_is_stale =
            (state.pending_hold &&
             state.pending_hold->document_revision != history.document.revision) ||
            (state.pending_note_duration_gesture &&
             state.pending_note_duration_gesture->document_revision !=
                 history.document.revision) ||
            (state.pending_note_move_gesture &&
             state.pending_note_move_gesture->document_revision != history.document.revision) ||
            (state.pending_note_paint_gesture &&
             state.pending_note_paint_gesture->document_revision !=
                 history.document.revision) ||
            (state.pending_semantic_layer_paint_gesture &&
             state.pending_semantic_layer_paint_gesture->document_revision !=
                 history.document.revision) ||
            (state.pending_editor_area_selection &&
             state.pending_editor_area_selection->document_revision !=
                 history.document.revision) ||
            (state.pending_space_range_selection &&
             state.pending_space_range_selection->document_revision !=
                 history.document.revision);
        const bool compose_inactive =
            workspace.active_tab != wormedit::workspace_tab_t::compose;
        if (editor_interaction_has_pending_gesture(state) &&
            (selected_chart_changed || pending_is_stale || compose_inactive)) {
            (void)wormedit::dispatch_editor_action(history, *compose_interaction_state,
                                                   wormedit::cancel_editor_gesture_action_t{});
        }
        if (selected_chart_changed) {
            compose_interaction_state.reset();
        }
    }
}



void refresh_sound_selection_context(const WormUiWorkspaceAction& action,
    application_command_state_t& sound_context) {
    sound_context.project.sound_selection.sound_selection_chart = wormedit::chart_id_t{action.compose_input_chart_id};
    sound_context.project.sound_selection.sound_selection_revision = action.compose_input_document_revision;
    sound_context.project.sound_selection.sound_selection_hover = action.compose_sound_hover_object_id
        ? std::optional{wormedit::object_id_t{action.compose_sound_hover_object_id}} : std::nullopt;
    WormUiKeysoundDockRequest sound_dock{};
    worm_ui_frontend_keysound_dock_request(&sound_dock);
    sound_context.project.sound_selection.sound_selection_dock.clear();
    for (size_t i = 0; i < sound_dock.selected_resource_count; ++i)
        sound_context.project.sound_selection.sound_selection_dock.push_back(wormedit::resource_id_t{sound_dock.selected_resource_ids[i]});
}

auto has_fresh_shortcut_key(const WormUiWorkspaceAction& action) -> bool {
    const auto count = std::min(action.shortcut_event_count, std::size(action.shortcut_events));
    for (std::size_t index = 0; index < count; ++index)
        if (!action.shortcut_events[index].repeat) return true;
    return false;
}





auto can_offer_audio_project_drop(bool session_loaded, bool fresh_blank_project,
                                  const wormedit::document_history_t& history) -> bool {
    if (!session_loaded) return true;
    return fresh_blank_project && history.undo.empty() && history.redo.empty() &&
           !wormedit::is_document_dirty(history);
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
void post_audio_inventory_feedback(std::optional<std::size_t> unavailable,
    const wormedit::notification_sink_t& notifications) {
    if (unavailable && *unavailable != 0)
        notifications.post(wormedit::notification_severity_t::warning,
            wormedit::notification_source_t::audio,
            std::to_string(*unavailable) + " audio asset(s) are unavailable for preview.");
}

// Registers audio assets added or removed by document edits (linking, undo, redo) so
// playback and the waveform see them without a save or reload.
void reconcile_audition_sources_after_edit(
    const wormedit::sys::audition_source_reconciliation_context_t& reconciliation,
    std::uint64_t revision, std::uint64_t& reconciled_revision,
    const wormedit::notification_sink_t& notifications) {
    if (revision == reconciled_revision) return;
    reconciled_revision = revision;
    post_audio_inventory_feedback(
        wormedit::sys::reconcile_audition_sources(reconciliation, true), notifications);
}
#endif

auto requested_project_lane_count(std::uint8_t index) -> std::optional<std::uint32_t> {
    constexpr std::array<std::uint32_t, 8> choices{4, 5, 6, 7, 8, 9, 10, 14};
    if (index >= choices.size()) return std::nullopt;
    return choices[index];
}

void install_startup_recovery_scan(
    wormedit::native_project_recovery_controller_t& controller,
    wormedit::native_project_startup_result_t& startup) {
    if (startup.recovery_scan_configured)
        (void)controller.replace_scan(std::move(startup.recovery_scan));
}

void post_initial_application_feedback(wormedit::notification_sink_t& notifications,
    bool loaded_primary, const std::filesystem::path& requested_primary_path,
    const wormedit::platform_recovery_directory_result_t& recovery) {
    notifications.post(wormedit::sys::initial_application_feedback(loaded_primary, requested_primary_path, recovery));
}

void post_shortcut_startup_diagnostic(
    wormedit::notification_sink_t& sink, const wormedit::shortcut_profile_startup_result_t& result) {
    if (!result.diagnostics.empty())
        sink.post(notification_severity_for(result.diagnostics.front().severity),
            wormedit::notification_source_t::input, result.diagnostics.front().message);
}

void validate_minimap_start_frame(application_command_state_t& application,
    const wormedit::document_history_t& history,
    const std::optional<wormedit::editor_interaction_state_t>& compose_state) {
    (void)wormedit::sys::compose_minimap_start_frame(application, history,
        compose_state ? &*compose_state : nullptr);
}












enum class close_target_t { none, application, project, new_project, open_file };
struct pending_close_t {
    close_target_t target{close_target_t::none};
    wormedit::song_id_t project;
    std::uint64_t revision{};
    bool save_requested{};
    std::string project_path;
    std::uint8_t layout_index{};
    std::uint8_t encoding_index{};
    bool awaiting_encoding{};
    // Shown in the confirmation only when a save started from it failed.
    std::string save_failure;
};












auto requested_project_transition(WormUiWorkspaceActionKind kind) -> std::optional<close_target_t> {
    switch (kind) {
        case WORM_UI_WORKSPACE_ACTION_QUIT: return close_target_t::application;
        case WORM_UI_WORKSPACE_ACTION_CLOSE_PROJECT: return close_target_t::project;
        case WORM_UI_WORKSPACE_ACTION_NEW_PROJECT: return close_target_t::new_project;
        case WORM_UI_WORKSPACE_ACTION_OPEN_FILE: return close_target_t::open_file;
        default: return std::nullopt;
    }
}






















void project_bga_dock(wormedit::sys::bga_dock_runtime_t& runtime,
    const wormedit::document_history_t& history, const wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& base, WormUiComposeView& view) {
    WormUiBgaDockRequest request{};
    worm_ui_frontend_bga_dock_request(&request);
    request.visible = request.visible && workspace.active_tab == wormedit::workspace_tab_t::compose;
    const auto chart = workspace.selected_chart
        ? std::ranges::find(history.document.charts, *workspace.selected_chart,
                            &wormedit::chart_document_t::id)
        : history.document.charts.end();
    request.ids_available = chart != history.document.charts.end() &&
        chart->source_family == wormedit::format_family_t::bms;
    runtime.project(history.document, base, request, view);
}

void apply_compare_scroll_action(compare_scene_cache_t& cache,
                                 const WormUiWorkspaceAction& action) {
    if (action.kind != WORM_UI_WORKSPACE_ACTION_COMPARE_SCROLL ||
        cache.pixels_per_measure <= 0.0) return;
    cache.shared_scroll_measures = std::max(
        0.0, cache.shared_scroll_measures -
                 action.compare_scroll_delta / cache.pixels_per_measure);
}

auto stage_sound_import(const wormedit::song_document_t& document, wormedit::chart_id_t chart,
                        std::span<const std::string> paths, wormedit::notification_sink_t& notifications,
                        std::uint8_t fixed_kind = 0) -> std::string {
    const auto target = std::ranges::find(document.charts, chart, &wormedit::chart_document_t::id);
    if (target == document.charts.end()) return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
        wormedit::notification_source_t::import_, "Select a difficulty before adding sounds.");
    auto prepared = wormedit::sys::prepare_keysound_import(document, chart, paths, fixed_kind);
    if (prepared.entries.empty()) return post_callback_feedback(notifications, prepared.skipped_unreadable
        ? wormedit::notification_severity_t::warning : wormedit::notification_severity_t::info,
        wormedit::notification_source_t::import_, prepared.message);
    std::vector<WormUiSoundImportItem> items;
    std::vector<std::string> source_paths;
    source_paths.reserve(prepared.entries.size());
    for (const auto& entry : prepared.entries)
        source_paths.push_back(entry.asset.source_path.generic_string());
    for (std::size_t index = 0; index < prepared.entries.size(); ++index) {
        const auto& entry = prepared.entries[index];
        items.push_back({source_paths[index].c_str(), entry.asset.name.c_str(),
                         static_cast<std::uint8_t>(entry.kind), fixed_kind != 0});
    }
    std::vector<WormUiSoundImportGroup> groups;
    for (const auto& group : document.sound_groups)
        groups.push_back({group.name.c_str(), group.id.value, nullptr, 0});
    const bool has_bgm = !target->sound_channels.empty() ||
        std::ranges::any_of(target->measures, [](const wormedit::measure_chunk_t& measure) { return !measure.bgm_events.empty(); });
    const auto staged = worm_ui_frontend_stage_sound_import(document.id.value, chart.value,
        document.revision, items.data(), items.size(), groups.data(), groups.size(), has_bgm,
        prepared.message.c_str());
    return post_callback_feedback(notifications, staged ? wormedit::notification_severity_t::hint
        : wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_,
        staged ? "Review the sounds before importing." : "The sound import could not be staged.");
}

auto stage_sound_slot_batch(const wormedit::song_document_t& document,
    wormedit::chart_id_t chart, std::uint16_t first_slot,
    const std::vector<std::string>& paths, wormedit::notification_sink_t& notifications) -> std::string {
    const auto prepared = wormedit::sys::prepare_sound_slot_batch(document, chart, first_slot, paths);
    std::vector<WormUiSoundSlotImportItem> items;
    items.reserve(prepared.items.size());
    for (const auto& item : prepared.items)
        items.push_back({item.path.c_str(), item.name.c_str(), item.occupied_resource_id,
                         item.occupied_name.c_str(), item.ambiguous});
    const auto staged = worm_ui_frontend_stage_sound_slot_batch(document.id.value,
        chart.value, document.revision, first_slot, items.data(), items.size(),
        prepared.reason.c_str());
    return post_callback_feedback(notifications, staged ? wormedit::notification_severity_t::hint
        : wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_,
        staged ? "Review the WAV slot assignments before importing." : "The WAV slots target is no longer active.");
}

auto stage_dropped_sound_slots(const wormedit::song_document_t& document,
    wormedit::chart_id_t chart, std::uint16_t first_slot,
    const std::vector<std::string>& paths, wormedit::notification_sink_t& notifications) -> std::string {
    if (paths.size() != 1) return stage_sound_slot_batch(document, chart, first_slot, paths, notifications);
    const auto prepared = wormedit::sys::prepare_sound_slot_batch(document, chart, first_slot, paths);
    if (!prepared.reason.empty() || prepared.items.size() != 1)
        return stage_sound_slot_batch(document, chart, first_slot, paths, notifications);
    const auto& item = prepared.items.front();
    const auto staged = worm_ui_frontend_stage_sound_slot_file_at(document.id.value,
        chart.value, document.revision, first_slot, item.occupied_resource_id, item.path.c_str());
    return post_callback_feedback(notifications, staged ? wormedit::notification_severity_t::hint
        : wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_,
        staged ? "Sound staged for the selected WAV slot." : "The WAV slots target is no longer active.");
}

void import_dropped_resources(bool session_loaded, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::vector<wormedit::sys::sound_drop_batch_t>& batches, wormedit::notification_sink_t& notifications) {
    WormUiKeysoundDockRequest dock{};
    worm_ui_frontend_keysound_dock_request(&dock);
    for (const auto& batch : batches) {
        const auto& origin = batch.origin;
        if (!batch.accepted()) {
            notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_, batch.reason);
            continue;
        }
        const bool slots_mode = workspace.active_tab == wormedit::workspace_tab_t::compose && dock.slots_mode;
        if (!session_loaded || !workspace.selected_chart ||
            origin.project_id != history.document.id.value ||
            origin.chart_id != workspace.selected_chart->value || origin.revision != history.document.revision ||
            origin.tab != static_cast<WormUiWorkspaceTab>(workspace.active_tab) ||
            (origin.target != wormedit::sys::resource_drop_target_t::bga && origin.slots_mode != slots_mode)) {
            notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_,
                "The resource drop target changed; drop the files again.");
            continue;
        }
        if (origin.target == wormedit::sys::resource_drop_target_t::bga)
            static_cast<void>(wormedit::sys::import_bga_files(history, workspace, *workspace.selected_chart, batch.paths, notifications));
        else if (origin.slots_mode)
            static_cast<void>(stage_dropped_sound_slots(history.document, *workspace.selected_chart, origin.first_slot, batch.paths, notifications));
        else static_cast<void>(stage_sound_import(history.document, *workspace.selected_chart, batch.paths, notifications));
    }
    batches.clear();
}

auto dispatch_reviewed_sound_slot_batch(const WormUiWorkspaceAction& action,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    wormedit::sys::application_command_state_t& application,
    const std::optional<wormedit::editor_interaction_state_t>& interaction,
    wormedit::notification_sink_t& notifications) -> bool {
    const bool request = action.compose_action == WORM_UI_COMPOSE_ACTION_STAGE_SOUND_SLOT_BATCH;
    const bool confirm = action.compose_action == WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_SLOT_BATCH;
    if (!request && !confirm) return false;
    std::string feedback;
    WormUiKeysoundDockRequest dock{};
    worm_ui_frontend_keysound_dock_request(&dock);
    const auto chart = wormedit::chart_id_t{action.compose_input_chart_id};
    if (!dock.slots_mode || action.sound_import_project_id != history.document.id.value ||
        !workspace.selected_chart || chart != *workspace.selected_chart ||
        workspace.active_tab != wormedit::workspace_tab_t::compose ||
        action.compose_input_document_revision != history.document.revision) {
        feedback = "The WAV slots target changed. Choose the files again.";
        notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_, feedback);
        if (confirm) worm_ui_frontend_sound_slot_batch_result(false, feedback.c_str());
        return true;
    }
    if (request) {
        std::vector<std::string> paths;
        for (std::size_t i = 0; action.compose_import_paths && i < action.compose_import_path_count; ++i)
            paths.emplace_back(action.compose_import_paths[i] ? action.compose_import_paths[i] : "");
        feedback = stage_sound_slot_batch(history.document, chart, action.compose_sound_slot, paths, notifications);
        return true;
    }
    if (interaction && editor_interaction_has_pending_gesture(*interaction)) {
        feedback = "Finish or cancel the Compose gesture before importing sounds.";
        notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_, feedback);
        worm_ui_frontend_sound_slot_batch_result(false, feedback.c_str());
        return true;
    }
    const auto before = history.document.revision;
    const auto result = wormedit::sys::dispatch_compose_sound_action(action, history, workspace, chart, application, notifications);
    feedback = result.value_or("The WAV slot import was not handled.");
    if (!result) notifications.post(wormedit::notification_severity_t::error, wormedit::notification_source_t::import_, feedback);
    worm_ui_frontend_sound_slot_batch_result(history.document.revision != before, feedback.c_str());
    return true;
}

auto dispatch_reviewed_sound_import(const WormUiWorkspaceAction& action,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    wormedit::sys::application_command_state_t& application,
    const std::optional<wormedit::editor_interaction_state_t>& interaction,
    wormedit::notification_sink_t& notifications) -> bool {
    const auto request = action.compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_SOUND_FILES;
    const auto confirm = action.compose_action == WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT;
    if (!request && !confirm) return false;
    std::string feedback;
    const auto chart = wormedit::chart_id_t{action.compose_input_chart_id};
    if ((action.sound_import_project_id && action.sound_import_project_id != history.document.id.value) ||
        !workspace.selected_chart || chart != *workspace.selected_chart ||
        action.compose_input_document_revision != history.document.revision) {
        feedback = "The import target changed. Cancel and choose the files again.";
        notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_, feedback);
        if (confirm) worm_ui_frontend_sound_import_result(false, feedback.c_str());
        return true;
    }
    if (request) {
        std::vector<std::string> paths;
        for (std::size_t index = 0; action.compose_import_paths &&
                                   index < action.compose_import_path_count; ++index)
            if (action.compose_import_paths[index]) paths.emplace_back(action.compose_import_paths[index]);
        feedback = stage_sound_import(history.document, chart, paths, notifications, action.sound_import_kind);
        return true;
    }
    if (interaction && editor_interaction_has_pending_gesture(*interaction)) {
        feedback = "Finish or cancel the Compose gesture before importing sounds.";
        notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_, feedback);
        worm_ui_frontend_sound_import_result(false, feedback.c_str());
        return true;
    }
    const auto before = history.document.revision;
    const auto result = wormedit::sys::dispatch_compose_sound_action(action, history, workspace, chart, application, notifications);
    feedback = result.value_or("The sound import was not handled.");
    if (!result) notifications.post(wormedit::notification_severity_t::error, wormedit::notification_source_t::import_, feedback);
    worm_ui_frontend_sound_import_result(history.document.revision != before, feedback.c_str());
    return true;
}

auto compare_jump_interaction(const wormedit::document_history_t& history,
                              const wormedit::chart_document_t& chart,
                              wormedit::local_position_t position,
                              const std::optional<wormedit::editor_interaction_state_t>& previous)
    -> std::optional<wormedit::editor_interaction_state_t> {
    if (previous && previous->chart == chart.id) return previous;
    const auto built = wormedit::build_measure_index(chart);
    const auto* index = std::get_if<wormedit::measure_index_t>(&built);
    const auto row = index ? wormedit::absolute_row(*index, position) : std::nullopt;
    const wormedit::lane_layout_segment_t* active{};
    std::uint64_t active_row{};
    if (row) {
        for (const auto& segment : chart.lane_layouts) {
            const auto start = wormedit::absolute_row(*index, segment.position);
            if (start && *start <= *row && (!active || *start >= active_row)) {
                active = &segment;
                active_row = *start;
            }
        }
    }
    if (!active || active->layout.lanes.empty()) return std::nullopt;
    constexpr std::array<std::uint64_t, 6> default_snaps{4, 8, 16, 32, 48, 64};
    const auto setup = wormedit::make_editor_interaction(history, chart.id,
        active->layout.lanes.front().id, position,
        previous ? std::span<const std::uint64_t>{previous->supported_snap_denominators}
                 : std::span<const std::uint64_t>{default_snaps},
        previous ? previous->active_snap_denominator : 16,
        previous ? previous->viewport_height_pixels : 1.0);
    return setup.state;
}

auto dispatch_sound_usage_jump(const WormUiWorkspaceAction& action,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& interaction,
    wormedit::notification_sink_t& notifications) -> std::string {
    WormUiSoundUsageRequest request{};
    worm_ui_frontend_sound_usage_request(&request);
    const auto result = wormedit::sys::dispatch_sound_usage_action(
        action, request, history, workspace, interaction);
    if (result.source_selection) {
        const auto& source = *result.source_selection;
        worm_ui_frontend_select_sound_usage_source(source.project_id, source.resource_id,
                                                   source.channel_id, source.clip_id);
    }
    return post_callback_feedback(notifications, result.accepted ? wormedit::notification_severity_t::hint
                                                              : wormedit::notification_severity_t::warning,
                                  wormedit::notification_source_t::input, result.message);
}

auto dispatch_compare_diagnostic_jump(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& interaction,
    wormedit::notification_sink_t& notifications) -> std::string {
    const auto chart_id = wormedit::chart_id_t{action.compare_diagnostic_chart};
    const auto chart = std::ranges::find(history.document.charts, chart_id,
                                         &wormedit::chart_document_t::id);
    if (chart == history.document.charts.end()) return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
        wormedit::notification_source_t::input, "The validation target is no longer available.");
    const auto measure = std::ranges::find(chart->measures,
        wormedit::measure_id_t{action.compare_diagnostic_measure}, &wormedit::measure_chunk_t::id);
    if (measure == chart->measures.end()) return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
        wormedit::notification_source_t::input, "The validation target is no longer available.");
    const auto capacity = wormedit::validate_measure(chart->divisions_per_standard_measure,
                                                      measure->definition).row_capacity;
    if (!capacity || action.compare_diagnostic_row >= *capacity)
        return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
        wormedit::notification_source_t::input, "The validation target is no longer available.");
    auto candidate = compare_jump_interaction(history, *chart,
        {measure->id, action.compare_diagnostic_row}, interaction);
    if (!candidate) return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
        wormedit::notification_source_t::input, "The validation target cannot be opened in Compose.");
    const auto jumped = wormedit::dispatch_editor_action(history, *candidate,
        wormedit::jump_edit_cursor_action_t{
            {measure->id, action.compare_diagnostic_row}, history.document.revision});
    if (!jumped.accepted)
        return post_callback_feedback(notifications, jumped.diagnostics.empty() ? wormedit::notification_severity_t::warning
            : notification_severity_for(jumped.diagnostics.front().severity), wormedit::notification_source_t::input,
            jumped.diagnostics.empty() ? "Could not move to validation diagnostic." : jumped.diagnostics.front().message);
    auto next_workspace = workspace;
    if (!wormedit::apply_workspace_action(next_workspace, history.document,
                                          wormedit::workspace_select_chart_action_t{chart_id}) ||
        !wormedit::apply_workspace_action(
            next_workspace, history.document,
            wormedit::workspace_switch_tab_action_t{wormedit::workspace_tab_t::compose}))
        return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
        wormedit::notification_source_t::input, "The validation target cannot be opened in Compose.");
    workspace = std::move(next_workspace);
    interaction = std::move(candidate);
    return post_callback_feedback(notifications, wormedit::notification_severity_t::hint,
        wormedit::notification_source_t::input, "Moved to validation diagnostic.");
}



auto dispatch_audio_sound_audition(const WormUiWorkspaceAction& action,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    wormedit::sys::application_command_state_t& application,
    wormedit::notification_sink_t& notifications) -> bool {
    if (workspace.active_tab != wormedit::workspace_tab_t::audio_slices ||
        action.compose_action != WORM_UI_COMPOSE_ACTION_AUDITION_SOUND) return false;
    if (!workspace.selected_chart || action.compose_input_project_id != history.document.id.value ||
        action.compose_input_chart_id != workspace.selected_chart->value ||
        action.compose_input_document_revision != history.document.revision) {
        notifications.post(wormedit::notification_severity_t::warning,
            wormedit::notification_source_t::input, "Sound audition ignored because its target changed.");
        return true;
    }
    if (action.kind == WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET ||
        action.kind == WORM_UI_WORKSPACE_ACTION_SELECT_SOUND_RESOURCE)
        static_cast<void>(dispatch_workspace_selection_action(action, history, workspace, notifications));
    static_cast<void>(wormedit::sys::dispatch_compose_sound_action(action, history, workspace,
        *workspace.selected_chart, application, notifications));
    return true;
}

auto dispatch_reviewed_sound_edit(const WormUiWorkspaceAction& action,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    wormedit::sys::application_command_state_t& application,
    const std::optional<wormedit::editor_interaction_state_t>& interaction,
    wormedit::notification_sink_t& notifications) -> bool {
    return dispatch_audio_sound_audition(action, history, workspace, application, notifications) ||
        wormedit::sys::dispatch_sound_properties_action(action, history, workspace, notifications) ||
        dispatch_reviewed_sound_slot_batch(action, history, workspace, application, interaction, notifications) ||
        dispatch_reviewed_sound_import(action, history, workspace, application, interaction, notifications);
}



auto notification_frame_time() -> wormedit::notification_time_t {
    return std::chrono::duration_cast<wormedit::notification_time_t>(
        std::chrono::steady_clock::now().time_since_epoch());
}

auto project_application_notifications(const wormedit::notification_center_t& center,
    wormedit::notification_time_t now,
    const std::optional<wormedit::editor_interaction_state_t>& interaction,
    wormedit::sys::notification_history_runtime_t& runtime,
    const wormedit::song_document_t& document, std::uint64_t document_generation)
    -> WormUiNotificationView {
    auto view = wormedit::sys::project_notification_view(center, now);
    view.history_available = true;
    view.history = runtime.project(center, document, now, document_generation);
    view.edit_gesture_active = interaction && editor_interaction_has_pending_gesture(*interaction);
    return view;
}

void seed_notification_smoke(wormedit::notification_center_t& center) {
    const auto* enabled = std::getenv("WORMEDIT_NOTIFICATION_SMOKE");
    if (!enabled || std::strcmp(enabled, "1") != 0) return;
    center.post(wormedit::notification_severity_t::warning,
                wormedit::notification_source_t::system, "Example notification warning");
    const auto* long_fixture = std::getenv("WORMEDIT_NOTIFICATION_SMOKE_LONG");
    const char* error_message = long_fixture && std::strcmp(long_fixture, "1") == 0
        ? "Example notification error: This bounded 200-byte message checks wrapping near the bottom-right edge. Every word stays visible in the hover detail while typing and editor Escape keep their same owner."
        : "Example notification error";
    center.post(wormedit::notification_severity_t::error,
                wormedit::notification_source_t::system, error_message);
}

auto notification_history_smoke_detail() -> std::string {
    std::string detail{"Detalles de notificación: café · música 🎵\n"};
    constexpr std::string_view line{"Línea visible de detalle para comprobar el desplazamiento y el portapapeles.\n"};
    constexpr std::string_view ending{"\nÚltima línea: fin del detalle.\n"};
    while (detail.size() + line.size() + ending.size() <= wormedit::notification_detail_max_bytes)
        detail += line;
    detail.append(wormedit::notification_detail_max_bytes - ending.size() - detail.size(), ' ');
    detail += ending;
    return detail;
}

void seed_notification_history_smoke(wormedit::notification_center_t& center,
                                    const wormedit::song_document_t& document) {
    const auto* enabled = std::getenv("WORMEDIT_NOTIFICATION_HISTORY_SMOKE");
    if (!enabled || std::strcmp(enabled, "1") != 0) return;
    const auto detail = notification_history_smoke_detail();
    for (int repeat = 0; repeat < 3; ++repeat)
        center.post({wormedit::notification_severity_t::info, wormedit::notification_source_t::system,
                     "Unicode detail example", detail, {}});
    center.post(wormedit::notification_severity_t::hint, wormedit::notification_source_t::input, "History hint");
    center.post(wormedit::notification_severity_t::success, wormedit::notification_source_t::edit, "History success");
    for (std::size_t index = 0; index < std::min<std::size_t>(document.charts.size(), 2); ++index) {
        const auto& chart = document.charts[index];
        if (chart.measures.empty()) continue;
        const auto& measure = chart.measures.front();
        const wormedit::notification_target_t target = measure.notes.empty()
            ? wormedit::notification_target_t{wormedit::notification_chart_position_t{chart.id, {measure.id, 0}}}
            : wormedit::notification_target_t{measure.notes.front().id};
        center.post({wormedit::notification_severity_t::info, wormedit::notification_source_t::document,
            index == 0 ? "First chart target" : "Second chart target", "Exact chart destination", target});
    }
}









auto close_confirmation_view(const pending_close_t& pending, bool has_native_path)
    -> WormUiCloseConfirmationView {
    return {.visible = pending.target != close_target_t::none && !pending.awaiting_encoding,
        .application = pending.target == close_target_t::application, .has_native_path = has_native_path,
        .project_id = pending.project.value, .revision = pending.revision, .status_text = pending.save_failure.c_str()};
}



auto retained_compose_geometry(const compose_scene_cache_t& cache)
    -> wormedit::editor_measure_geometry_t {
    return {cache.scene.chart, cache.scene.revision,
            cache.scene.cached_measure_index ? &*cache.scene.cached_measure_index : nullptr};
}



auto dispatch_tap_panel_action(wormedit::sys::tap_timing_runtime_t& runtime,
                               const WormUiTapTimingAction& action,
                               wormedit::document_history_t& history,
                               const wormedit::sys::tap_timing_clock_t& clock,
                               bool timing_locked)
    -> std::optional<wormedit::document_command_result_t> {
    if (action.toggle) runtime.toggle();
    if (action.close) runtime.set_visible(false);
    if (action.reset) runtime.reset();
    if (action.choose_bpm && action.bpm_choice <= 2)
        runtime.set_bpm_choice(static_cast<wormedit::tap_bpm_choice_t>(action.bpm_choice));
    if (action.apply && !action.choose_bpm) {
        return runtime.apply(history, clock, action.apply_revision, timing_locked);
    }
    return std::nullopt;
}

auto tap_music_available(const wormedit::song_document_t& document, const WormUiWorkspaceView& view) -> bool {
    if (view.tap_timing.source_available || view.metadata.selected_music_asset_id != 0) return true;
    return std::ranges::any_of(document.resources, [&](const wormedit::resource_entry_t& resource) {
        return std::ranges::find(resource.roles, wormedit::resource_role_t::music) != resource.roles.end() &&
            std::ranges::find(document.audio_assets, resource.asset, &wormedit::audio_asset_t::id) != document.audio_assets.end();
    });
}

void project_tap_timing(wormedit::sys::tap_timing_runtime_t& runtime,
                        const wormedit::song_document_t& document,
                        const std::function<wormedit::sys::tap_timing_clock_t()>& read_clock,
                        wormedit::command_registry_t& registry, WormUiWorkspaceView& view) {
    const auto clock = runtime.visible() ? read_clock() : wormedit::sys::tap_timing_clock_t{};
    if (runtime.visible()) (void)runtime.observe(clock);
    const auto state = runtime.state();
    (void)wormedit::update_command_state(registry, {"timing.tap_assistant"}, true, state.visible);
    (void)wormedit::update_command_state(registry, {"timing.tap"}, state.visible, std::nullopt);
    auto& target = view.tap_timing;
    target.visible = state.visible;
    target.playing = state.playing;
    target.source_available = state.source_available;
    target.beats_per_measure = state.beats_per_measure;
    target.active_segment = state.active_segment;
    target.downbeat_flash = state.downbeat_flash;
    target.tap_flash = state.tap_flash;
    target.tap_segment = state.tap_segment;
    target.timing_locked = state.timing_locked;
    target.music_available = state.visible && tap_music_available(document, view);
    target.tap_count = state.tap_count;
    target.tap_capacity = state.tap_capacity;
    target.status = state.status;
    target.bpm_choice = static_cast<uint8_t>(runtime.bpm_choice());
    const auto& proposal = runtime.apply_proposal();
    target.apply_available = proposal.has_value();
    if (proposal) {
        target.apply_revision = proposal->stamp.document.revision;
        target.proposed_bpm = static_cast<double>(proposal->beats_per_minute.numerator) /
                             proposal->beats_per_minute.denominator;
        target.proposed_offset_ms = 1000.0 * proposal->beat_zero_audio_offset_seconds.numerator /
                                   proposal->beat_zero_audio_offset_seconds.denominator;
    }
    if (!state.estimate) return;
    target.estimate_available = true;
    target.stable = state.estimate->stable;
    target.bpm = state.estimate->bpm;
    target.ms_per_beat = 60'000 / target.bpm;
    target.offset_ms = state.estimate->phase_frame * 1000 / clock.sample_rate;
    target.deviation_ms = state.estimate->residual_standard_deviation_ms;
    target.confidence = state.estimate->confidence;
}

void prepare_tap_proposal(wormedit::sys::tap_timing_runtime_t& runtime,
    const std::function<wormedit::sys::tap_timing_clock_t()>& read_clock,
    const wormedit::song_document_t& document, bool locked,
    wormedit::sys::compose_scene_cache_t& cache) {
    if (runtime.visible()) (void)runtime.observe(read_clock());
    runtime.prepare_apply(document, locked);
    cache.timing.tap_apply_proposal = runtime.apply_proposal();
}

void post_tap_apply_feedback(const std::optional<wormedit::document_command_result_t>& result,
                             wormedit::notification_sink_t& notifications) {
    if (!result) return;
    if (*result)
        notifications.post(wormedit::notification_severity_t::success,
                           wormedit::notification_source_t::edit,
                           "Applied initial BPM and audio offset.");
    else
        post_edit_rejection(notifications, result->diagnostics, "Tap timing Apply rejected.");
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
void publish_tap_assist_grid(const wormedit::sys::tap_timing_runtime_t& runtime,
                            wormedit::audio::preview_session_t& session,
                            std::optional<wormedit::sys::tap_timing_assist_publication_t>& published) {
    const auto publication = runtime.assist_publication();
    if (published == publication) return;
    if (session.set_assist_grid(publication.active, publication.grid)) published = publication;
}

auto tap_meter_at(const wormedit::song_document_t& document,
                  const wormedit::sys::runtime::chart_preview_t& preview,
                  std::uint64_t frame) -> std::uint16_t {
    if (!preview.chart || !preview.timing ||
        frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return 4;
    const auto chart = std::ranges::find(document.charts, *preview.chart, &wormedit::chart_document_t::id);
    if (chart == document.charts.end()) return 4;
    const auto position = wormedit::musical_position_at(*preview.timing,
        {static_cast<std::int64_t>(frame), 44'100});
    if (!position.absolute_row) return 4;
    const auto row = position.absolute_row->numerator / static_cast<std::int64_t>(position.absolute_row->denominator);
    struct meter_cache_t {
        std::uint64_t revision{};
        wormedit::song_id_t project;
        std::uint64_t fingerprint{};
        std::optional<wormedit::chart_id_t> chart;
        std::vector<std::pair<std::uint64_t, std::uint32_t>> entries;
    };
    static meter_cache_t cache;
    if (cache.project != document.id || cache.chart != preview.chart ||
        cache.revision != document.revision || cache.fingerprint != preview.timing->source_fingerprint) {
        cache.fingerprint = preview.timing->source_fingerprint;
        cache.project = document.id;
        cache.chart = preview.chart;
        cache.revision = document.revision;
        cache.entries.clear();
        std::uint64_t begin{};
        std::uint32_t beats = 4;
        for (const auto& measure : chart->measures) {
            if (measure.definition.meter) beats = measure.definition.meter->numerator;
            cache.entries.emplace_back(begin, beats);
            const auto validation = wormedit::validate_measure(chart->divisions_per_standard_measure, measure.definition);
            if (!validation.row_capacity || *validation.row_capacity > UINT64_MAX - begin) break;
            begin += *validation.row_capacity;
        }
    }
    const auto found = std::upper_bound(cache.entries.begin(), cache.entries.end(),
        row < 0 ? 0 : static_cast<std::uint64_t>(row),
        [](std::uint64_t target, const std::pair<std::uint64_t, std::uint32_t>& entry) { return target < entry.first; });
    const auto beats = found == cache.entries.begin() ? 4 : std::prev(found)->second;
    return beats >= 1 && beats <= 32 ? static_cast<std::uint16_t>(beats) : 4;
}
#endif

struct tap_clock_context_t {
    std::uint64_t& epoch;
    std::uint64_t& epoch_begin_ns;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    const wormedit::song_document_t& document;
    const wormedit::sys::runtime::chart_preview_t& preview;
    const wormedit::audio::preview_transport_bridge_t& bridge;
    const wormedit::sys::sdl_preview_output_t& output;
#endif
};

auto read_tap_clock(tap_clock_context_t& context) -> wormedit::sys::tap_timing_clock_t {
        wormedit::sys::tap_timing_clock_t clock;
        clock.observed_ns = SDL_GetTicksNS();
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        const auto token = context.bridge.session_token();
        const auto rate = context.output.playback_rate();
        const auto keep_pitch = context.output.keep_pitch();
        const auto playing = context.output.playing();
        const auto frame = context.output.timeline_frame();
        clock.observed_ns = SDL_GetTicksNS();
        if (context.epoch != (token ? token->generation : 0)) {
            context.epoch = token ? token->generation : 0;
            context.epoch_begin_ns = clock.observed_ns;
        }
        clock.chart = context.preview.chart ? context.preview.chart->value : 0;
        clock.revision = context.preview.revision;
        clock.asset = context.preview.music_asset ? context.preview.music_asset->value : 0;
        clock.epoch = context.epoch;
        clock.epoch_begin_ns = context.epoch_begin_ns;
        clock.playback_rate = rate;
        clock.keep_pitch = keep_pitch;
        clock.playing = playing && context.output.playing();
        if (token == context.bridge.session_token() && rate == context.output.playback_rate() &&
            keep_pitch == context.output.keep_pitch() && context.preview.schedule &&
            context.preview.schedule->music) {
            clock.source_frame = wormedit::preview_music_source_frame_at(*context.preview.schedule->music, frame);
            clock.beats_per_measure = tap_meter_at(context.document, context.preview, frame);
        }
#endif
        return clock;
}

auto dispatch_click_audition_toggle(const wormedit::command_invocation_context_t& invocation,
    wormedit::sys::program_preferences_runtime_t& program, application_command_state_t& state,
    wormedit::notification_sink_t& notifications)
    -> std::optional<wormedit::command_invocation_result_t> {
    const auto command = parse_application_command(invocation.command.value);
    if (!command || command->kind != wormedit::sys::application_command_kind_t::audio_toggle_click_audition)
        return std::nullopt;
    const auto error = wormedit::sys::toggle_audition_on_click(program, state.preferences.editor_preferences.audition_on_click);
    if (!error.empty())
        return wormedit::sys::reject_application_command(notifications, error,
            wormedit::notification_severity_t::error, wormedit::notification_source_t::system);
    notifications.post(wormedit::notification_severity_t::success, wormedit::notification_source_t::input,
        state.preferences.editor_preferences.audition_on_click ? "Play sound on click enabled." : "Play sound on click disabled.");
    return wormedit::command_invocation_result_t{.accepted = true};
}

template<class InvokeEditor>
auto dispatch_options_or_editor_command(const wormedit::command_invocation_context_t& invocation,
    wormedit::sys::options_runtime_t& options, const wormedit::program_preferences_t& preferences,
    wormedit::sys::notification_history_runtime_t& notifications,
    wormedit::notification_center_t& center, InvokeEditor&& invoke_editor) -> wormedit::command_invocation_result_t {
    const auto command = parse_application_command(invocation.command.value);
    if (command) {
        const auto notification = wormedit::sys::invoke_notification_history_command(
            *command, invocation.auto_repeat, notifications, center);
        if (notification) return *notification;
    }
    if (command && command->kind == wormedit::sys::application_command_kind_t::workspace_options) {
        wormedit::sys::open_options(options, preferences);
        return {.accepted = true};
    }
    return invoke_editor();
}

auto create_chart_from_registered_command(wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& compose_interaction_state,
    application_command_state_t& application_command_state, wormedit::notification_sink_t& notifications)
    -> wormedit::command_invocation_result_t {
    using severity_t = wormedit::notification_severity_t;
    using source_t = wormedit::notification_source_t;
    const auto created = wormedit::apply_add_chart_command(
        history, workspace.selected_chart.value_or(wormedit::chart_id_t{}));
    if (!created) return wormedit::sys::reject_application_command(notifications,
        created.command.diagnostics.empty() ? "New chart could not be created." : created.command.diagnostics.front().message,
        created.command.diagnostics.empty() ? severity_t::warning
            : notification_severity_for(created.command.diagnostics.front().severity), source_t::document);
    const auto rebound = wormedit::rebind_workspace(workspace, history);
    if (!rebound) return wormedit::sys::reject_application_command(notifications,
        "New chart created, but workspace rebinding failed.", severity_t::error, source_t::document);
    if (application_command_state.services.transport_stop) application_command_state.services.transport_stop();
    const auto error = open_project_chart(history, workspace, compose_interaction_state, *created.payload.chart);
    if (!error.empty()) return wormedit::sys::reject_application_command(notifications, error,
                                                                       severity_t::error, source_t::document);
    remember_snap_denominator(application_command_state, *created.payload.chart,
        application_command_state.preferences.editor_preferences.default_snap);
    notifications.post(severity_t::success, source_t::document, "New chart created.");
    return wormedit::command_invocation_result_t{.accepted = true};
}





#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#endif

void retain_compose_hover_timing(
    wormedit::sys::compose_scene_cache_t& cache, const WormUiWorkspaceView& view,
    const WormUiWorkspaceAction& action) {
    auto& hover = cache.waveform.hover_timing;
    hover.valid = action.hover_source_frame_valid && view.project_id != 0 &&
        view.selected_chart_id != 0 && view.compose.selected_audio_asset_id != 0 &&
        action.hover_source_total_frames != 0 && action.hover_source_sample_rate != 0;
    if (!hover.valid) return;
    hover.source_frame = action.hover_source_frame;
    hover.project_id = view.project_id;
    hover.chart_id = view.selected_chart_id;
    hover.document_revision = view.document_revision;
    hover.audio_asset_id = view.compose.selected_audio_asset_id;
    hover.total_frames = action.hover_source_total_frames;
    hover.sample_rate = action.hover_source_sample_rate;
}

void retain_compose_bgm_window_capture(
    wormedit::sys::application_command_state_t& application, const WormUiWorkspaceView& view,
    const WormUiWorkspaceAction& action) {
    application.project.bgm.bgm_bar_window_capture.reset();
    if (!action.minimap_window_capture_active || view.project_id == 0 ||
        view.selected_chart_id == 0 ||
        view.compose.selected_audio_asset_id == 0 ||
        view.compose.waveform_overview_total_frames == 0 ||
        view.compose.waveform_overview_sample_rate == 0 ||
        !std::isfinite(action.minimap_window_capture_begin_fraction) ||
        !std::isfinite(action.minimap_window_capture_end_fraction) ||
        action.minimap_window_capture_begin_fraction < 0.0 ||
        action.minimap_window_capture_begin_fraction >= action.minimap_window_capture_end_fraction ||
        action.minimap_window_capture_end_fraction > 1.0)
        return;
    application.project.bgm.bgm_bar_window_capture = wormedit::sys::bgm_bar_window_capture_t{
        .range = {action.minimap_window_capture_begin_fraction,
                  action.minimap_window_capture_end_fraction},
        .project = wormedit::song_id_t{view.project_id},
        .chart = wormedit::chart_id_t{view.selected_chart_id},
        .document_revision = view.document_revision,
        .asset = wormedit::asset_id_t{view.compose.selected_audio_asset_id},
        .total_frames = view.compose.waveform_overview_total_frames,
        .sample_rate = view.compose.waveform_overview_sample_rate};
}

void publish_project_portability_notice(wormedit::sys::project_portability_notice_t& notice,
                                        const wormedit::document_history_t& history,
                                        std::uint64_t session,
                                        wormedit::notification_sink_t& sink) {
    if (notice.update(history.document, session))
        sink.post(wormedit::notification_severity_t::warning,
                  wormedit::notification_source_t::document,
                  "Some assets are linked by absolute paths or paths outside the project folder. "
                  "Moving this project to another environment may break those links. Copy the "
                  "assets into the project to make it portable.");
}

} // namespace

namespace wormedit::sys {
struct application_t::implementation_t {
    explicit implementation_t(bool& running_value) : running(running_value) {}
    struct platform_t {
        notification_center_t notifications{};
        notification_sink_t notification_sink{notifications};
        application_platform_t lifetime{};
        std::optional<frontend_notification_sink_t> frontend_notifications{};
        platform_recovery_directory_result_t recovery_directory{};
        application_startup_arguments_t arguments{};
    } platform;

    struct document_session_t {
        notification_sink_t& notifications;
        document_history_t history{};
        workspace_controller_t workspace{};
        project_identity_t project_identity{};
        task_scheduler_t autosave_scheduler{1, 1, background_scheduler_options()};
        native_project_autosave_loop_t autosave_loop{native_project_autosave_loop_t::clock_t::now(),
                                                     autosave_scheduler,
                                                     {},
                                                     0,
                                                     std::chrono::minutes{5},
                                                     std::chrono::seconds{30},
                                                     notifications};
        native_project_recovery_controller_t recovery_controller{};
        recovery_scan_notification_publisher_t recovery_scan_feedback{};
        bool recovery_scan_configured{};
    } session{platform.notification_sink};

    struct background_jobs_t {
        notification_sink_t& notifications;
        task_scheduler_t resource_copy_scheduler{1, 1, background_scheduler_options()};
        runtime::resource_copy_t resource_copy_runtime{notifications};
        runtime::metadata_asset_t metadata_asset_runtime{notifications};
        runtime::output_group_export_t export_runtime{notifications};
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        visual::image_cache_t player_image_cache{};
        task_scheduler_t player_image_scheduler{1, 4, background_scheduler_options()};
        visual::image_job_t player_image_job{player_image_cache, player_image_scheduler,
                                             WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER_CAPACITY +
                                                 WORM_UI_PLAYER_PREVIEW_STORYBOARD_CAPACITY};
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
        visual::video_frame_cache_t player_video_cache{};
        task_scheduler_t player_video_scheduler{1, 2, background_scheduler_options()};
        visual::video_frame_job_t player_video_job{player_video_cache, player_video_scheduler};
#endif
    } jobs{platform.notification_sink};

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    struct audio_runtime_t {
        notification_sink_t& notifications;
        audio::decoded_source_registry_t audition_sources{};
        audio::decoded_block_cache_t audition_cache{};
        audio::transport_t audition_transport{audition_cache};
        audio::audition_transport_bridge_t audition_bridge{audition_transport};
        audio::object_audition_controller_t object_audition{audition_bridge};
        audio::audition_session_t audition_session{audition_sources, audition_transport,
                                                   audition_bridge};
        sdl_audition_output_t audition_output{audition_session};
        audio::decoded_block_cache_t preview_cache{audio::preview_block_cache_limits()};
        runtime::waveform_analysis_t waveform_runtime{audition_sources, &preview_cache,
                                                      notifications};
        audio::transport_t preview_transport{preview_cache};
        audio::preview_transport_bridge_t preview_bridge{preview_transport};
        audio::preview_session_t preview_session{audition_sources, preview_transport,
                                                 preview_bridge};
        audio::resident_sample_store_t resident_samples{};
        sdl_preview_output_t preview_output{preview_session};
        runtime::chart_preview_t preview_state{};
        std::vector<audio::preview_metronome_source_binding_t> preview_metronome_bindings{};
        std::optional<song_id_t> audition_source_project{};
        std::vector<audition_asset_entry_t> audition_asset_inventory{};
        std::uint64_t audition_source_generation{};
        task_scheduler_t audition_probe_scheduler{2, 4, background_scheduler_options()};
        resident_sample_loader_t resident_loader{resident_samples, audition_probe_scheduler};
        std::optional<audition_source_reconciliation_context_t> reconciliation{};
        std::uint64_t audition_reconciled_revision{};
    } audio_runtime{platform.notification_sink};
#endif

    struct preferences_t {
        program_preferences_runtime_t program_preferences{};
        external_player_runtime_t external_player_runtime{};
        options_runtime_t options_runtime{};
        shortcut_editor_runtime_t shortcut_editor{};
    } preferences;
    application_command_state_t commands{};
    command_registry_t command_registry{};

    struct editor_views_t {
        std::string close_feedback{};
        workspace_edit_feedback_t workspace_edit_feedback{};
        projection_notification_feedback_t projection_feedback{}, workspace_view_feedback{};
        notification_history_runtime_t notification_history{};
        WormUiNotificationAction pending_notification{};
        sound_drop_transaction_t sound_drop_transaction{};
        std::vector<sound_drop_batch_t> pending_resource_batches{};
        bga_dock_runtime_t bga_dock_runtime{};
        std::optional<audio_offset_drag_t> audio_offset_drag{};
        std::optional<workspace_tab_t> audio_offset_drag_tab{};
        timing_scene_cache_t timing_scene_cache{};
        runtime::timing_edit_t timing_edit{};
        compose_scene_cache_t compose_scene_cache{};
        compare_scene_cache_t compare_scene_cache{};
        frame_arena_t frame_projection_arena{
            {.chunk_bytes = 1024 * 1024, .maximum_bytes = 16 * 1024 * 1024}};
        player_preview_presenter_t player_preview_presenter{};
        std::optional<editor_interaction_state_t> compose_interaction_state{};
        viewport_input_controller_t compose_viewport_input{};
        editor_navigation_input_controller_t compose_navigation_input{};
        project_chart_runtime_t project_chart_runtime{};
        chart_import_session_t chart_import{};
        pending_close_t pending_close{};
        inline_preview_cursor_t inline_preview_cursor{};
        stepmania_export_policy_t stepmania_export_policy{};
        tap_timing_runtime_t tap_runtime{};
        std::optional<tap_timing_assist_publication_t> published_assist{};
        std::optional<std::uint64_t> last_tap_event_ns{};
        std::uint64_t tap_epoch{}, tap_epoch_begin_ns{};
        std::function<tap_timing_clock_t()> tap_clock;
        timing_grid_state_t timing_grid{};
        metadata_drop_state_t metadata_drop{};
        std::uint64_t compose_viewport_last_ticks{};
        project_portability_notice_t portability_notice{};
    } views;

    tap_clock_context_t tap_clock_context{
        views.tap_epoch,
        views.tap_epoch_begin_ns,
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        session.history.document,
        audio_runtime.preview_state,
        audio_runtime.preview_bridge,
        audio_runtime.preview_output,
#endif
    };
    std::optional<std::uint64_t>* current_tap_event_{};
    static constexpr std::size_t worker_count = 3
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
                                                + 1
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
                                                + 1
#endif
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
                                                + 2
#endif
        ;
    std::array<task_scheduler_t*, worker_count> worker_schedulers{
        &session.autosave_scheduler,
        &jobs.resource_copy_scheduler,
        &jobs.export_runtime.scheduler,
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        &jobs.player_image_scheduler,
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
        &jobs.player_video_scheduler,
#endif
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        &audio_runtime.waveform_runtime.worker_scheduler(),
        &audio_runtime.audition_probe_scheduler,
#endif
    };
    application_worker_set_t workers{platform.lifetime, worker_schedulers};
    bool& running;
    bool started_{}, shutdown_started_{};

    struct AudioOffsetDragContext {
        const WormUiWorkspaceAction& action;
        bool timing_locked;
        std::optional<std::uint32_t> selected_sample_rate;
    };
    auto document_inputs() -> document_edit_inputs_t {
        return {session.history, session.workspace, platform.notification_sink};
    }
    auto command_inputs() -> command_dispatch_inputs_t {
        return {document_inputs(),
                {commands, views.compose_interaction_state, views.timing_edit.stage}};
    }
    auto compose_actions() -> compose_action_inputs_t {
        return {document_inputs(),
                {commands, views.compose_interaction_state},
                {session.project_identity.asset_base,
                 {jobs.resource_copy_scheduler, jobs.resource_copy_runtime, jobs.export_runtime},
                 {views.metadata_drop, jobs.metadata_asset_runtime}}};
    }
    auto compose_inputs() -> compose_input_inputs_t {
        return {document_inputs(),
                commands,
                {views.compose_scene_cache, views.compose_viewport_input,
                 views.compose_navigation_input}};
    }
    auto metadata_inputs() -> metadata_action_inputs_t {
        return {document_inputs(),
                {views.metadata_drop, jobs.metadata_asset_runtime, jobs.resource_copy_scheduler},
                session.project_identity.asset_base};
    }
    auto frontend_inputs() -> frontend_event_inputs_t {
        return {document_inputs(), views.metadata_drop, session.project_identity};
    }

    void post_project_feedback(wormedit::notification_severity_t severity,
                               wormedit::notification_source_t source, std::string message,
                               std::string_view detail = {}) {
        // The close confirmation retains its own scoped feedback; it is not a
        // global notification producer or a replacement status-line channel.
        views.close_feedback = std::move(message);
        if (severity == wormedit::notification_severity_t::hint)
            platform.notification_sink.post_transient_hint(source, views.close_feedback);
        else
            platform.notification_sink.post(
                wormedit::notification_post_t{.severity = severity,
                                              .source = source,
                                              .message = views.close_feedback,
                                              .detail = std::string(detail),
                                              .target = {}});
    }

    auto can_transition_project() -> bool {
        if (session.autosave_loop.busy() || jobs.resource_copy_runtime.busy() ||
            jobs.export_runtime.busy() || jobs.metadata_asset_runtime.import_pending()) {
            post_project_feedback(wormedit::notification_severity_t::warning,
                                  wormedit::notification_source_t::document,
                                  "Wait for the current file operation before changing projects.");
            return false;
        }
        if (session.project_identity.session_loaded &&
            wormedit::is_document_dirty(session.history)) {
            post_project_feedback(wormedit::notification_severity_t::warning,
                                  wormedit::notification_source_t::document,
                                  "Save the current project before replacing or closing it.");
            return false;
        }
        return true;
    }

    void tick_loaded_project_autosave() {
        if (!session.project_identity.session_loaded)
            return;
        wormedit::sys::tick_application_autosave(
            session.autosave_loop, session.history, session.project_identity.native_project_loaded,
            session.project_identity.active_project_path, platform.recovery_directory.path,
            platform.notification_sink, session.project_identity.asset_base);
    }

    void apply_notification_action(const WormUiNotificationAction& action) {
        if (views.notification_history.apply(action, platform.notifications))
            return;
        if (action.kind == WORM_UI_NOTIFICATION_ACTION_COPY_DETAILS)
            copy_notification_details(action.id);
        else if (action.kind == WORM_UI_NOTIFICATION_ACTION_GO_TO) {
            const auto error = wormedit::sys::go_to_notification(platform.notifications, action.id,
                                                                 session.history, session.workspace,
                                                                 views.compose_interaction_state);
            const auto message = error ? *error : "Moved to notification target.";
            platform.notification_sink.post(error ? wormedit::notification_severity_t::error
                                                  : wormedit::notification_severity_t::success,
                                            wormedit::notification_source_t::document, message);
        }
    }

    void move_staged_timing_event(const WormUiWorkspaceAction& action, bool timing_locked,
                                  std::uint64_t active_snap_denominator,
                                  wormedit::chart_document_t* chart,
                                  wormedit::timing_lane_pseudonote_preview_t& stage) {
        const auto target =
            chart == nullptr || !stage.existing_event || !action.timing_has_target_y
                ? std::optional<wormedit::local_position_t>{}
                : timing_position_from_fraction(
                      *chart, wormedit::sys::timing_target_fraction(action.timing_target),
                      active_snap_denominator);
        if (!target || !wormedit::revise_existing_timing_lane_pseudonote_position(session.history, stage,
                                                                       *target, timing_locked)) {
            post_edit_rejection(platform.notification_sink, stage.diagnostics,
                                "The timing marker cannot move to that row.");
        } else {
            platform.notification_sink.post_transient_hint(
                wormedit::notification_source_t::edit,
                "Timing marker moved in the staged preview; apply to commit.");
        }
    }

    auto finish_confirmed_close(pending_close_t& pending) -> bool {
        if (pending.target == close_target_t::application) {
            running = false;
            pending = {};
            return true;
        }
        if (is_project_replacement_busy())
            return false;
        if (pending.target == close_target_t::new_project ||
            pending.target == close_target_t::open_file) {
            finish_confirmed_project_replacement(pending);
            return true;
        }
        if (!replace_project_session(
                wormedit::sys::empty_session_document(wormedit::sys::new_session_project_id()),
                false, true, false, {}, {}))
            return false;
        pending = {};
        post_project_feedback(wormedit::notification_severity_t::info,
                              wormedit::notification_source_t::document, "No project loaded.");
        return true;
    }

    auto save_project_session(const WormUiWorkspaceAction& action) -> bool {
        using namespace wormedit;
        if (!session.project_identity.session_loaded) {
            post_project_feedback(notification_severity_t::warning, notification_source_t::document,
                                  "Create or open a project before saving.");
            return false;
        }
        if (jobs.resource_copy_runtime.busy() || jobs.export_runtime.busy() ||
            jobs.metadata_asset_runtime.import_pending()) {
            post_project_feedback(notification_severity_t::warning, notification_source_t::document,
                                  "Wait for the current file operation before saving.");
            return false;
        }
        auto destination = action.project_path[0] != '\0'
                               ? std::filesystem::path{action.project_path}
                               : session.project_identity.active_project_path;
        if (destination.empty()) {
            post_project_feedback(notification_severity_t::hint, notification_source_t::document,
                                  "Choose a native project path to save.");
            return false;
        }
        auto extension = destination.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (extension.empty())
            destination += ".wormedit";
        else if (extension != ".wormedit") {
            post_project_feedback(notification_severity_t::warning, notification_source_t::document,
                                  "Choose a .wormedit project filename.");
            return false;
        }
        std::error_code path_error;
        const auto destination_base =
            std::filesystem::absolute(destination, path_error).parent_path();
        if (path_error) {
            post_project_feedback(notification_severity_t::error, notification_source_t::document,
                                  "Could not resolve the project destination: " +
                                      path_error.message());
            return false;
        }
        auto saved_document = session.history.document;
        wormedit::sys::rebase_project_paths(saved_document, session.project_identity.asset_base,
                                            destination_base);
        const auto result = publish_native_project_atomic(destination, saved_document);
        if (!result.published) {
            post_project_feedback(
                result.diagnostics.empty()
                    ? notification_severity_t::error
                    : notification_severity_for(result.diagnostics.front().severity),
                notification_source_t::document,
                result.diagnostics.empty() ? "Could not save the project."
                                           : result.diagnostics.front().message);
            return false;
        }
        wormedit::sys::rebase_project_paths(session.history.document,
                                            session.project_identity.asset_base, destination_base);
        for (auto& entry : session.history.undo) {
            wormedit::sys::rebase_project_paths(entry.before, session.project_identity.asset_base,
                                                destination_base);
            wormedit::sys::rebase_project_paths(entry.after, session.project_identity.asset_base,
                                                destination_base);
        }
        for (auto& entry : session.history.redo) {
            wormedit::sys::rebase_project_paths(entry.before, session.project_identity.asset_base,
                                                destination_base);
            wormedit::sys::rebase_project_paths(entry.after, session.project_identity.asset_base,
                                                destination_base);
        }
        mark_document_saved(session.history);
        const auto version =
            native_project_document_version_t{.project_id = session.history.document.id,
                                              .revision = session.history.document.revision};
        session.autosave_loop.mark_explicit_save(version);
        session.project_identity.scanned_primary_version = version;
        session.project_identity.active_project_path = destination;
        remember_project_file(preferences.program_preferences, destination,
                              platform.notification_sink);
        session.project_identity.native_project_loaded = true;
        session.project_identity.asset_base = destination_base;
        session.project_identity.external_player_source_path.clear();
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        post_audio_inventory_feedback(
            wormedit::sys::reconcile_audition_sources((*audio_runtime.reconciliation), true),
            platform.notification_sink);
#endif
        auto message = "Project saved to " + destination.string();
        auto severity = notification_severity_t::success;
        if (!result.diagnostics.empty()) {
            message += " " + result.diagnostics.front().message;
            severity =
                std::max(severity, notification_severity_for(result.diagnostics.front().severity));
        }
        post_project_feedback(severity, notification_source_t::document, std::move(message));
        return true;
    }

    void create_audio_project_session(const char* path, std::uint32_t lane_count) {
        const auto audio = wormedit::sys::inspect_audio_for_project(path);
        if (!audio) {
            post_project_feedback(wormedit::notification_severity_t::error,
                                  wormedit::notification_source_t::import_, audio.error);
            session.project_identity.pending_import.audio_project_path.clear();
            return;
        }
        auto document = wormedit::sys::audio_project_document(
            wormedit::sys::new_session_project_id(), audio.source_path, audio.title, audio.artist,
            lane_count, audio.stream);
        if (!document) {
            post_project_feedback(wormedit::notification_severity_t::error,
                                  wormedit::notification_source_t::import_,
                                  "Could not create the selected audio chart layout.");
            return;
        }
        if (!replace_project_session(std::move(*document), true, true, false, {},
                                     audio.source_path.parent_path()))
            return;
        for (const auto& chart : session.history.document.charts)
            remember_snap_denominator(commands, chart.id,
                                      commands.preferences.editor_preferences.default_snap);
        const auto selected = wormedit::apply_workspace_action(
            session.workspace, session.history.document,
            wormedit::workspace_select_asset_action_t{
                .asset = session.history.document.audio_assets.front().id,
                .slot = wormedit::workspace_asset_slot_t::audio});
        post_project_feedback(
            selected ? wormedit::notification_severity_t::success
                     : wormedit::notification_severity_t::warning,
            wormedit::notification_source_t::import_,
            selected ? "Audio imported as a new " + std::to_string(lane_count) + "K project."
                     : "Audio project created; select the music asset to preview it.");
    }

    void dispatch_audio_offset_lock_feedback(const WormUiWorkspaceAction& action) {
        const auto was_locked = commands.project.timing_recipe.audio_offset_locked;
        const auto feedback = toggle_compose_audio_offset_lock(
            action, session.history, session.workspace, commands, views.compose_scene_cache,
            views.audio_offset_drag, views.audio_offset_drag_tab);
        platform.notification_sink.post(was_locked !=
                                                commands.project.timing_recipe.audio_offset_locked
                                            ? wormedit::notification_severity_t::success
                                            : wormedit::notification_severity_t::warning,
                                        wormedit::notification_source_t::edit, feedback);
    }

    void begin_notification_frame(WormUiNotificationAction& pending,
                                  wormedit::notification_time_t now) {
        apply_notification_action(pending);
        pending = {};
        platform.notifications.drain_inbox(now);
    }

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    void sync_layer_assist_selection() {
        if (commands.project.semantic_layer.layer_assist_chart ==
            session.workspace.selected_chart) {
            (void)audio_runtime.preview_state.set_layer_assist(
                session.workspace.selected_chart.value_or(wormedit::chart_id_t{}),
                commands.project.semantic_layer.layer_assist_layers, audio_runtime.preview_session);
        } else {
            (void)audio_runtime.preview_state.set_layer_assist(
                session.workspace.selected_chart.value_or(wormedit::chart_id_t{}), {},
                audio_runtime.preview_session);
        }
    }
#endif

    void dispatch_dropped_chart_project(std::string& pending_path, pending_close_t& pending,
                                        bool confirm) {
        if (pending_path.empty())
            return;
        const auto path = std::exchange(pending_path, {});
        if (path.size() >= WORM_UI_PROJECT_PATH_CAPACITY) {
            post_project_feedback(wormedit::notification_severity_t::warning,
                                  wormedit::notification_source_t::import_,
                                  "The dropped chart or project path is too long.");
            return;
        }
        WormUiWorkspaceAction action{};
        action.kind = WORM_UI_WORKSPACE_ACTION_OPEN_FILE;
        std::memcpy(action.project_path, path.c_str(), path.size() + 1);
        (void)dispatch_close_confirmation(pending, action, confirm);
    }

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    auto selected_waveform_rate() -> std::optional<std::uint32_t> {
        if (audio_runtime.waveform_runtime.waveform_analysis() &&
            audio_runtime.waveform_runtime.waveform_analysis()->waveform &&
            audio_runtime.waveform_runtime.waveform_asset() &&
            session.workspace.transport.audio_asset ==
                audio_runtime.waveform_runtime.waveform_asset()) {
            return audio_runtime.waveform_runtime.waveform_analysis()->waveform->sample_rate;
        }
        return std::nullopt;
    }
#endif

    auto dispatch_close_confirmation(pending_close_t& pending, const WormUiWorkspaceAction& action,
                                     bool confirm) -> bool {
        if (pending.awaiting_encoding &&
            action.kind == WORM_UI_WORKSPACE_ACTION_CANCEL_BMS_ENCODING) {
            pending = {};
            return false;
        }
        if (resume_confirmed_encoding(pending, action))
            return true;
        if (const auto target = requested_project_transition(action.kind)) {
            const bool replacing =
                *target == close_target_t::new_project || *target == close_target_t::open_file;
            confirm_unsaved_changes(pending, *target, confirm || replacing, &action);
            return true;
        }
        if (pending.target == close_target_t::none || pending.awaiting_encoding)
            return false;
        if (action.close_decision != WORM_UI_CLOSE_NONE) {
            if (action.close_project_id != pending.project.value ||
                action.close_revision != pending.revision)
                return true;
            if (action.close_decision == WORM_UI_CLOSE_CANCEL) {
                pending = {};
                return true;
            }
            if (session.history.document.id != pending.project ||
                session.history.document.revision != pending.revision) {
                pending.project = session.history.document.id;
                pending.revision = session.history.document.revision;
                post_project_feedback(wormedit::notification_severity_t::warning,
                                      wormedit::notification_source_t::document,
                                      "The project changed. Review it before confirming close.");
                return true;
            }
            if (action.close_decision == WORM_UI_CLOSE_DISCARD) {
                (void)finish_confirmed_close(pending);
                return true;
            }
            if (action.close_decision == WORM_UI_CLOSE_SAVE)
                pending.save_requested = true;
        }
        if (pending.save_requested && action.kind == WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT)
            save_before_confirmed_close(pending, action);
        return true; // Confirmation blocks authoring and shortcut dispatch.
    }

    auto continue_audio_offset_drag(AudioOffsetDragContext& context) -> std::string {
        using namespace wormedit;
        const auto& action = context.action;
        auto& history = session.history;
        auto& workspace = session.workspace;
        auto& drag = views.audio_offset_drag;
        auto& drag_tab = views.audio_offset_drag_tab;
        const auto timing_locked = context.timing_locked;
        if (!drag || !drag_tab || *drag_tab != workspace.active_tab ||
            !drag->stamp.is_current(*workspace.selected_chart, action.audio_offset_drag_document_revision) ||
            drag->sample_rate != *context.selected_sample_rate) {
            return clear_audio_offset_drag(
                context, "Audio offset drag has no matching captured transaction.");
        }
        const auto updated = revise_audio_offset_drag(
            history.document, *drag, action.audio_offset_drag_delta_frames, timing_locked);
        if (!updated.is_valid()) {
            drag.reset();
            drag_tab.reset();
            return post_audio_offset_diagnostic(context, updated.diagnostics,
                                                "Audio offset preview was rejected.",
                                                notification_severity_t::warning);
        }
        drag = updated;
        if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_UPDATE)
            return post_audio_offset_feedback(context, notification_severity_t::hint,
                                              "Previewing exact waveform offset.");
        if (action.audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_COMMIT) {
            drag.reset();
            drag_tab.reset();
            return post_audio_offset_feedback(context, notification_severity_t::warning,
                                              "Invalid audio offset drag action.");
        }
        const auto committed = apply_audio_offset_drag(history, *drag, timing_locked);
        drag.reset();
        drag_tab.reset();
        if (!committed.applied) {
            return post_audio_offset_diagnostic(context, committed.diagnostics,
                                                "Audio offset was unchanged.",
                                                notification_severity_t::info);
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (rebound)
            return post_audio_offset_feedback(context, notification_severity_t::success,
                                              "Audio offset updated from waveform drag.");
        return post_audio_offset_diagnostic(context, rebound.diagnostics,
                                            "Audio offset changed, but workspace rebinding failed.",
                                            notification_severity_t::error);
    }

    auto launch_external_player(wormedit::sys::external_player_action_t action) -> std::string {
        using namespace wormedit::sys;
        const auto& asset_base = session.project_identity.asset_base;
        if (preferences.external_player_runtime.preferences.selected >=
            preferences.external_player_runtime.preferences.players.size()) {
            preferences.external_player_runtime.visible = true;
            return post_callback_feedback(platform.notification_sink,
                                          wormedit::notification_severity_t::warning,
                                          wormedit::notification_source_t::audio,
                                          "Choose an external player in Preferences.");
        }
        const auto& player = preferences.external_player_runtime.preferences
                                 .players[preferences.external_player_runtime.preferences.selected];
        if (player.path.empty()) {
            preferences.external_player_runtime.visible = true;
            return post_callback_feedback(platform.notification_sink,
                                          wormedit::notification_severity_t::warning,
                                          wormedit::notification_source_t::audio,
                                          "Choose an external player in Preferences.");
        }
        const auto* chart = selected_chart(session.history, session.workspace);
        if (action != external_player_action_t::stop &&
            (!chart || chart->source_family != wormedit::format_family_t::bms))
            return post_callback_feedback(platform.notification_sink,
                                          wormedit::notification_severity_t::warning,
                                          wormedit::notification_source_t::audio,
                                          "External preview requires a selected BMS chart.");
        std::uint64_t measure{};
        if (action == external_player_action_t::play_here) {
            const auto source_measure =
                external_source_measure(*chart, views.compose_interaction_state);
            if (!source_measure)
                return post_callback_feedback(
                    platform.notification_sink, wormedit::notification_severity_t::hint,
                    wormedit::notification_source_t::audio,
                    "Place the Compose cursor before playing externally from here.");
            measure = *source_measure;
        }
        auto source = session.project_identity.external_player_source_path;
        if (source.empty() && chart && !asset_base.empty()) {
            // Native projects retain the authoritative resource directory, but no imported
            // basename.
            bool popn{};
            for (const auto& layout : chart->lane_layouts)
                popn = popn || layout.layout.profile_id.starts_with("pms-");
            source = asset_base / (popn ? "chart.pms" : "chart.bms");
        }
        const char* application_path = SDL_GetBasePath();
        const auto result = preferences.external_player_runtime.player.launch(
            player, action, session.history.document, chart ? chart->id : wormedit::chart_id_t{},
            source, measure, application_path ? application_path : "");
        if (result.open_preferences)
            preferences.external_player_runtime.visible = true;
        return post_callback_feedback(
            platform.notification_sink,
            result.launched ? wormedit::notification_severity_t::success
                            : (result.open_preferences ? wormedit::notification_severity_t::warning
                                                       : wormedit::notification_severity_t::error),
            wormedit::notification_source_t::audio,
            result.launched ? "External player command launched." : result.error);
    }

    auto dispatch_application_shortcuts(const WormUiWorkspaceAction& action,
                                        std::optional<std::uint64_t>& tap_event_ns,
                                        bool& preserves_editor_gesture) -> bool {
        using namespace wormedit;
        preserves_editor_gesture = true;
        if (action.shortcut_overflowed ||
            action.shortcut_event_count > WORM_UI_SHORTCUT_EVENT_CAPACITY) {
            platform.notification_sink.post(
                notification_severity_t::warning, notification_source_t::input,
                "Shortcut input exceeded its per-frame capacity and was discarded.");
            preserves_editor_gesture = false;
            return true;
        }
        bool handled{};
        for (std::size_t index = 0; index < action.shortcut_event_count; ++index) {
            refresh_application_command_states(command_registry, session.history, session.workspace,
                                               views.compose_interaction_state, commands);
            const auto& input = action.shortcut_events[index];
            tap_event_ns = input.timestamp_ns;
            const auto dispatched =
                input.tap_timing_space
                    ? invoke_command(command_registry, {"timing.tap"},
                                     application_command_focus(action, session.workspace))
                    : dispatch_shortcut(
                          command_registry,
                          {.chord = {.key = static_cast<shortcut_key_t>(input.hid_usage),
                                     .modifiers = input.modifiers},
                           .auto_repeat = input.repeat},
                          application_command_focus(action, session.workspace),
                          application_primary_modifier());
            tap_event_ns.reset();
            if (!dispatched.command || (dispatched.command->value != "timing.tap" &&
                                        dispatched.command->value != "workspace.options" &&
                                        dispatched.command->value != "view.notifications"))
                preserves_editor_gesture = false;
            if (dispatched.status == command_dispatch_status_t::invoked) {
                handled = true;
            } else if (dispatched.status == command_dispatch_status_t::disabled) {
                handled = true;
                platform.notification_sink.post(
                    notification_severity_t::warning, notification_source_t::input,
                    dispatched.command
                        ? "Command is currently unavailable: " + dispatched.command->value
                        : "Command is currently unavailable.");
            } else if (dispatched.status == command_dispatch_status_t::conflict ||
                       dispatched.status == command_dispatch_status_t::invalid_event ||
                       dispatched.status == command_dispatch_status_t::rejected) {
                handled = true;
                post_shortcut_rejection(platform.notification_sink, command_registry, dispatched);
            } else if (dispatched.status == command_dispatch_status_t::repeat_suppressed) {
                handled = true;
            }
        }
        return handled;
    }

    void begin_project_session(wormedit::sys::project_identity_t replacement) {
        wormedit::sys::begin_project_session(session.project_identity, views.project_chart_runtime,
                                             std::move(replacement));
        worm_ui_frontend_begin_project_session();
        views.audio_offset_drag.reset();
        views.audio_offset_drag_tab.reset();
        views.compose_interaction_state.reset();
        views.timing_edit = {};
        views.timing_scene_cache = {};
        views.compose_scene_cache = {};
        views.timing_grid = {};
        views.compose_viewport_input.escape();
        views.compose_navigation_input.cancel();
        views.metadata_drop = {};
        wormedit::sys::reset_application_project_edit_state(commands);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        post_audio_inventory_feedback(
            wormedit::sys::reconcile_audition_sources((*audio_runtime.reconciliation), true),
            platform.notification_sink);
#endif
    }

    auto begin_audio_offset_drag_action(AudioOffsetDragContext& context) -> std::string {
        using namespace wormedit;
        const auto begun =
            begin_audio_offset_drag(session.history.document, *session.workspace.selected_chart,
                                    *context.selected_sample_rate, context.timing_locked);
        views.audio_offset_drag = begun.is_valid() ? std::optional{begun} : std::nullopt;
        views.audio_offset_drag_tab =
            begun.is_valid() ? std::optional{session.workspace.active_tab} : std::nullopt;
        if (begun.is_valid())
            return post_audio_offset_feedback(context, notification_severity_t::hint,
                                              "Dragging waveform offset; release to apply.");
        return post_audio_offset_diagnostic(context, begun.diagnostics,
                                            "Audio offset drag could not start.",
                                            notification_severity_t::warning);
    }

    void save_before_confirmed_close(pending_close_t& pending,
                                     const WormUiWorkspaceAction& action) {
        const auto id = session.history.document.id;
        const auto revision = session.history.document.revision;
        if (id != pending.project) {
            post_project_feedback(
                wormedit::notification_severity_t::warning,
                wormedit::notification_source_t::document,
                "The project changed while closing. Cancel and review the current project.");
            return;
        }
        pending.save_failure.clear();
        const bool saved = save_project_session(action);
        if (!saved)
            pending.save_failure = "Save failed: " + views.close_feedback;
        if (saved && session.history.document.id == id &&
            session.history.document.revision == revision &&
            !wormedit::is_document_dirty(session.history))
            (void)finish_confirmed_close(pending);
    }

    auto validate_audio_offset_drag_context(const AudioOffsetDragContext& context)
        -> std::optional<std::string> {
        return wormedit::sys::validate_audio_offset_drag_binding(
            context.action, session.history.document, session.workspace, context.selected_sample_rate);
    }

    auto replace_project_session(wormedit::song_document_t document, bool loaded, bool saved,
                                 bool native_project, std::filesystem::path native_path,
                                 std::filesystem::path asset_base) -> bool {
        using namespace wormedit;
        wormedit::sys::refresh_project_audio_availability(document, asset_base);
        auto replacement_history = make_document_history(std::move(document), 128, saved);
        auto replacement_binding = make_workspace_controller(replacement_history);
        if (!replacement_binding) {
            post_project_feedback(notification_severity_t::error, notification_source_t::document,
                                  "The selected document could not be bound to a workspace.");
            return false;
        }
        const auto saved_version =
            saved && native_project
                ? std::optional<native_project_document_version_t>{{.project_id =
                                                                        replacement_history.document
                                                                            .id,
                                                                    .revision =
                                                                        replacement_history.document
                                                                            .revision}}
                : std::nullopt;
        if (!session.autosave_loop.reset_session(native_project_autosave_loop_t::clock_t::now(),
                                                 saved_version, 0)) {
            post_project_feedback(
                notification_severity_t::warning, notification_source_t::document,
                "Wait for the current autosave to finish before changing projects.");
            return false;
        }
        session.history = std::move(replacement_history);
        session.workspace = std::move(*replacement_binding.workspace);
        begin_project_session({.session_loaded = loaded,
                               .native_project_loaded = native_project,
                               .active_project_path = std::move(native_path),
                               .asset_base = std::move(asset_base),
                               .scanned_primary_version = saved_version});
        return true;
    }

    auto is_project_replacement_busy() -> bool {
        if (!session.autosave_loop.busy() && !jobs.resource_copy_runtime.busy() &&
            !jobs.export_runtime.busy() && !jobs.metadata_asset_runtime.import_pending())
            return false;
        post_project_feedback(wormedit::notification_severity_t::warning,
                              wormedit::notification_source_t::document,
                              "Wait for the current file operation before closing the project.");
        return true;
    }

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    void sync_bgm_mix_selection() {
        const auto* chart = wormedit::sys::selected_chart(session.history, session.workspace);
        const auto found = chart ? commands.project.bgm.bgm_lane_mix.find(chart->id.value)
                                 : commands.project.bgm.bgm_lane_mix.end();
        const auto muted = chart && found != commands.project.bgm.bgm_lane_mix.end()
                               ? wormedit::sys::compose_bgm_muted_lanes(*chart, found->second)
                               : std::vector<std::uint64_t>{};
        audio_runtime.preview_state.set_bgm_mix(chart ? chart->id : wormedit::chart_id_t{}, muted,
                                                audio_runtime.preview_session);
    }
#endif

    auto dispatch_audio_offset_drag_action(const WormUiWorkspaceAction& action, bool timing_locked,
                                           std::optional<std::uint32_t> selected_sample_rate)
        -> std::string {

        AudioOffsetDragContext context{action, timing_locked, selected_sample_rate};
        if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_CANCEL)
            return clear_audio_offset_drag(context, "Audio offset drag cancelled.",
                                           wormedit::notification_severity_t::hint);
        if (auto failure = validate_audio_offset_drag_context(context))
            return clear_audio_offset_drag(context, std::move(*failure));
        if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_NUDGE)
            return nudge_audio_offset_drag(context);
        if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_BEGIN)
            return begin_audio_offset_drag_action(context);
        return continue_audio_offset_drag(context);
    }

    auto resume_confirmed_encoding(pending_close_t& pending, const WormUiWorkspaceAction& action)
        -> bool {
        if (!pending.awaiting_encoding || action.kind != WORM_UI_WORKSPACE_ACTION_OPEN_FILE ||
            pending.project_path != action.project_path ||
            pending.project != session.history.document.id ||
            pending.revision != session.history.document.revision)
            return false;
        pending.encoding_index = action.bms_encoding_index;
        pending.awaiting_encoding = false;
        (void)finish_confirmed_close(pending);
        return true;
    }

    auto dispatch_clipboard_menu(const WormUiWorkspaceAction& action) -> bool {
        if (action.clipboard == WORM_UI_CLIPBOARD_NONE)
            return false;
        const auto id = action.clipboard == WORM_UI_CLIPBOARD_COPY  ? "edit.copy"
                        : action.clipboard == WORM_UI_CLIPBOARD_CUT ? "edit.cut"
                                                                    : "edit.paste";
        auto& registry = command_registry;
        refresh_application_command_states(registry, session.history, session.workspace,
                                           views.compose_interaction_state, commands);
        const auto result = wormedit::invoke_command(
            registry, {id}, application_command_focus(action, session.workspace));
        if (!result.diagnostics.empty())
            post_shortcut_rejection(platform.notification_sink, registry, result);
        return true;
    }

    auto clear_audio_offset_drag(AudioOffsetDragContext& context, std::string message,
                                 wormedit::notification_severity_t severity =
                                     wormedit::notification_severity_t::warning) -> std::string {
        views.audio_offset_drag.reset();
        views.audio_offset_drag_tab.reset();
        return post_audio_offset_feedback(context, severity, std::move(message));
    }

    auto dispatch_recovery_workspace_action(const WormUiWorkspaceAction& action) -> bool {
        if (action.kind < WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT ||
            action.kind > WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE)
            return false;
        if (session.autosave_loop.busy()) {
            platform.notification_sink.post(
                wormedit::notification_severity_t::warning,
                wormedit::notification_source_t::recovery,
                "Wait for the current autosave to finish before recovering a session.");
            return true;
        }
        bool recovered = false;
        (void)wormedit::sys::dispatch_recovery_ui_action(
            action, session.recovery_controller, session.history, session.workspace,
            session.project_identity.scanned_primary_version, session.autosave_loop, recovered,
            platform.notification_sink);
        if (recovered) {
            begin_project_session({.session_loaded = true});
        }
        return true;
    }

    void open_project_session(const WormUiWorkspaceAction& action) {
        constexpr std::array<wormedit::text_encoding_t, 5> encodings{
            wormedit::text_encoding_t::utf8, wormedit::text_encoding_t::cp932,
            wormedit::text_encoding_t::euc_kr, wormedit::text_encoding_t::cp949,
            wormedit::text_encoding_t::windows_1252};
        if (action.bms_encoding_index > encodings.size()) {
            post_project_feedback(wormedit::notification_severity_t::warning,
                                  wormedit::notification_source_t::import_,
                                  "Choose a supported BMS text encoding.");
            return;
        }
        const auto encoding = action.bms_encoding_index == 0
                                  ? std::optional<wormedit::text_encoding_t>{}
                                  : std::optional{encodings[action.bms_encoding_index - 1]};
        auto opened = wormedit::sys::open_project_file(action.project_path, encoding);
        if (!opened) {
            post_project_feedback(opened.needs_encoding_choice
                                      ? wormedit::notification_severity_t::warning
                                      : wormedit::notification_severity_t::error,
                                  wormedit::notification_source_t::import_, opened.error);
            if (opened.needs_encoding_choice)
                session.project_identity.pending_import.bms_encoding_path = action.project_path;
            else if (encoding)
                session.project_identity.pending_import.bms_encoding_path.clear();
            return;
        }
        auto document = std::move(*opened.document);
        if (!opened.native_project)
            document.id = wormedit::sys::new_session_project_id();
        if (replace_project_session(std::move(document), true, true, opened.native_project,
                                    opened.native_project ? opened.source_path
                                                          : std::filesystem::path{},
                                    opened.asset_base)) {
            remember_project_file(preferences.program_preferences, opened.source_path,
                                  platform.notification_sink);
            session.project_identity.external_player_source_path =
                opened.native_project ? std::filesystem::path{} : opened.source_path;
            post_project_feedback(wormedit::notification_severity_t::success,
                                  opened.native_project ? wormedit::notification_source_t::document
                                                        : wormedit::notification_source_t::import_,
                                  opened.native_project
                                      ? "Project opened: " + opened.source_path.string()
                                      : "Chart imported from " + opened.source_path.string() +
                                            ". Save it as a project.");
            if (!opened.warnings.empty()) {
                auto warning = wormedit::sys::project_import_warning(opened.warnings);
                post_project_feedback(wormedit::notification_severity_t::warning,
                                      wormedit::notification_source_t::import_,
                                      std::move(warning.message), warning.detail);
            }
        }
    }

    void dispatch_workspace_frame_action(const WormUiWorkspaceAction& action, bool shortcut_handled,
                                         std::optional<std::uint32_t> sample_rate,
                                         int compose_viewport_height_pixels) {
        auto& running = this->running;
        auto& history = session.history;
        auto& workspace = session.workspace;
        auto& application_command_state = commands;
        auto& stepmania_export_policy = views.stepmania_export_policy;
        auto& command_registry = this->command_registry;
        auto& compose_interaction_state = views.compose_interaction_state;
        auto& metadata_asset_runtime = jobs.metadata_asset_runtime;
        auto& timing_grid = views.timing_grid;
        auto& timing_edit = views.timing_edit;
        if (action.kind == WORM_UI_WORKSPACE_ACTION_QUIT) {
            running = false;
        } else if (dispatch_project_session_action(action)) {
            // Project transitions are handled before editor actions.
        } else if (dispatch_recovery_workspace_action(action)) {
            // Recovery can open a document from the empty landing state.
        } else if (!session.project_identity.session_loaded) {
            // The landing view only accepts project and recovery actions.
        } else if (shortcut_handled) {
            // Registry commands take precedence over widget actions emitted
            // by the same input frame, preventing duplicate mutations.
        } else if (dispatch_clipboard_menu(action)) {
            // Clipboard menu actions use the same typed registry as shortcuts.
        } else if (dispatch_reviewed_sound_edit(
                       action, history, workspace, application_command_state,
                       compose_interaction_state, platform.notification_sink)) {
            // Properties and reviewed imports share a revision-checked sound edit boundary.
        } else if (action.compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_AUDIO_OFFSET_LOCK) {
            dispatch_audio_offset_lock_feedback(action);
        } else if (action.audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE) {
            static_cast<void>(dispatch_audio_offset_drag_action(
                action, application_command_state.audio_offset_edits_locked(), sample_rate));
        } else if (const auto common_status = wormedit::sys::dispatch_transport_player_action(
                       action, application_command_state, stepmania_export_policy,
                       platform.notification_sink)) {
            // Typed producer callbacks already publish this transport outcome.
        } else if (action.kind == WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE) {
            static_cast<void>(dispatch_sound_usage_jump(
                action, history, workspace, compose_interaction_state, platform.notification_sink));
        } else if (action.kind == WORM_UI_WORKSPACE_ACTION_COMPARE_DIAGNOSTIC_JUMP) {
            static_cast<void>(dispatch_compare_diagnostic_jump(
                action, history, workspace, compose_interaction_state, platform.notification_sink));
        } else if (action.kind == WORM_UI_WORKSPACE_ACTION_NAVIGATE_PREVIOUS_MARKER ||
                   action.kind == WORM_UI_WORKSPACE_ACTION_NAVIGATE_NEXT_MARKER) {
            const auto command = wormedit::command_id_t{
                action.kind == WORM_UI_WORKSPACE_ACTION_NAVIGATE_PREVIOUS_MARKER
                    ? "navigation.previous_marker"
                    : "navigation.next_marker"};
            refresh_application_command_states(command_registry, history, workspace,
                                               compose_interaction_state,
                                               application_command_state);
            const auto dispatched = wormedit::invoke_command(
                command_registry, command, application_command_focus(action, workspace));
            if (dispatched.status == wormedit::command_dispatch_status_t::disabled) {
                platform.notification_sink.post_transient_hint(
                    wormedit::notification_source_t::input,
                    "No beat-anchored bookmarks or section boundaries are available.");
            } else if (dispatched.status != wormedit::command_dispatch_status_t::invoked &&
                       !dispatched.diagnostics.empty()) {
                post_shortcut_rejection(platform.notification_sink, command_registry, dispatched);
            }
        } else if (const auto selection_status = dispatch_workspace_selection_action(
                       action, history, workspace, platform.notification_sink)) {
            static_cast<void>(selection_status);
        } else if (const auto metadata_status = wormedit::sys::dispatch_metadata_action(
                       action, metadata_inputs(),
                       application_command_state.audio_offset_edits_locked(),
                       jobs.resource_copy_runtime.busy(), jobs.export_runtime.busy(), true)) {
            static_cast<void>(metadata_status);
        } else if (dispatch_timing_workspace_action(action, application_command_state)) {
            // The timing producer publishes feedback and consumes this action.
        } else if (wormedit::sys::dispatch_compose_workspace_action(
                       action, compose_actions(), compose_viewport_height_pixels,
                       metadata_asset_runtime.import_pending())) {
            // The Compose producer publishes feedback and consumes this action.
        }
    }

    void copy_notification_details(std::uint64_t id) {
        const auto* entry = wormedit::sys::find_notification(platform.notifications, id);
        if (!entry) {
            platform.notification_sink.post(wormedit::notification_severity_t::error,
                                            wormedit::notification_source_t::input,
                                            "Notification no longer exists");
            return;
        }
        const auto& text = entry->detail.empty() ? entry->message : entry->detail;
        const bool copied = SDL_SetClipboardText(text.c_str());
        const auto message = copied ? "Notification details copied."
                                    : "Could not copy notification details to the clipboard.";
        platform.notification_sink.post(copied ? wormedit::notification_severity_t::success
                                               : wormedit::notification_severity_t::error,
                                        wormedit::notification_source_t::input, message);
    }

    auto dispatch_project_session_action(const WormUiWorkspaceAction& action) -> bool {
        if (action.kind == WORM_UI_WORKSPACE_ACTION_CANCEL_BMS_ENCODING) {
            session.project_identity.pending_import.bms_encoding_path.clear();
            post_project_feedback(wormedit::notification_severity_t::hint,
                                  wormedit::notification_source_t::import_, "BMS import canceled.");
            return true;
        }
        if (action.kind == WORM_UI_WORKSPACE_ACTION_CANCEL_AUDIO_PROJECT) {
            session.project_identity.pending_import.audio_project_path.clear();
            post_project_feedback(wormedit::notification_severity_t::hint,
                                  wormedit::notification_source_t::import_,
                                  "Audio import canceled.");
            return true;
        }
        if (action.kind == WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT) {
            save_project_session(action);
            return true;
        }
        if (action.kind != WORM_UI_WORKSPACE_ACTION_NEW_PROJECT &&
            action.kind != WORM_UI_WORKSPACE_ACTION_OPEN_FILE &&
            action.kind != WORM_UI_WORKSPACE_ACTION_CLOSE_PROJECT)
            return false;
        if (!can_transition_project())
            return true;
        if (action.kind == WORM_UI_WORKSPACE_ACTION_CLOSE_PROJECT) {
            if (replace_project_session(
                    wormedit::sys::empty_session_document(wormedit::sys::new_session_project_id()),
                    false, true, false, {}, {}))
                post_project_feedback(wormedit::notification_severity_t::info,
                                      wormedit::notification_source_t::document,
                                      "No project loaded.");
            return true;
        }
        if (action.kind == WORM_UI_WORKSPACE_ACTION_NEW_PROJECT) {
            create_new_project_session(action);
            return true;
        }
        open_project_session(action);
        return true;
    }

    bool dispatch_chart_import_action(const WormUiWorkspaceAction& action) {
        using namespace wormedit;
        const auto& request = action.chart_import;
        const bool picked = action.kind == WORM_UI_WORKSPACE_ACTION_IMPORT_CHART_FILE;
        if (!session.project_identity.session_loaded ||
            (request.kind == WORM_UI_CHART_IMPORT_NONE && !picked))
            return false;
        if (request.kind == WORM_UI_CHART_IMPORT_BEGIN)
            sys::chart_import_begin(views.chart_import);
        else if (request.kind == WORM_UI_CHART_IMPORT_CANCEL)
            sys::chart_import_cancel(views.chart_import);
        else if (request.kind == WORM_UI_CHART_IMPORT_LOAD || picked) {
            sys::chart_import_begin(views.chart_import);
            sys::chart_import_load(views.chart_import,
                                   std::filesystem::path{std::string{action.project_path}},
                                   session.history.document, session.project_identity.asset_base);
        } else if (request.kind == WORM_UI_CHART_IMPORT_TOGGLE) {
            sys::chart_import_toggle(views.chart_import, chart_id_t{request.chart_id},
                                     session.history.document, session.project_identity.asset_base);
        } else if (request.kind == WORM_UI_CHART_IMPORT_CONFIRM) {
            const auto source_path = views.chart_import.path_text;
            const auto imported = sys::chart_import_confirm(views.chart_import, session.history,
                                                            request.document_revision,
                                                            session.project_identity.asset_base);
            if (imported.command.applied) {
                remember_project_file(preferences.program_preferences, source_path,
                                      platform.notification_sink);
                platform.notification_sink.post(
                    notification_severity_t::success, notification_source_t::document,
                    imported.payload.charts.size() == 1 ? "Chart imported." : "Charts imported.");
                const auto rebound = rebind_workspace(session.workspace, session.history);
                if (!rebound)
                    platform.notification_sink.post(
                        notification_severity_t::error, notification_source_t::document,
                        rebound.diagnostics.empty()
                            ? "Charts imported, but workspace rebinding failed."
                            : rebound.diagnostics.front().message);
            } else {
                platform.notification_sink.post(notification_severity_t::warning,
                                                notification_source_t::document,
                                                views.chart_import.error);
            }
        }
        return true;
    }

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    auto apply_loop_selection(wormedit::chart_id_t chart_id, std::uint64_t expected_revision,
                              std::uint64_t begin_row, std::uint64_t end_row, bool enabled)
        -> bool {
        if (!session.workspace.selected_chart || *session.workspace.selected_chart != chart_id ||
            session.history.document.revision != expected_revision)
            return false;
        if (!enabled)
            return audio_runtime.preview_output.set_loop_region(std::nullopt);
        const auto* chart = selected_chart(session.history, session.workspace);
        if (session.workspace.active_tab != wormedit::workspace_tab_t::compose || !chart ||
            chart->id != chart_id || !views.compose_interaction_state ||
            views.compose_interaction_state->chart != chart_id ||
            !views.compose_interaction_state->retained_selection_range)
            return false;
        const auto region = loop_region_for_selection(
            *chart, *views.compose_interaction_state->retained_selection_range, begin_row, end_row);
        if (!region || (audio_runtime.preview_output.playing() &&
                        !audio_runtime.preview_state.matches_selection(session.history.document,
                                                                       session.workspace)))
            return false;
        if (!audio_runtime.preview_output.set_loop_region(region))
            return false;
        if (audio_runtime.preview_output.playing()) {
            const auto sought = audio_runtime.preview_output.seek(region->begin_frame);
            if (!sought) {
                static_cast<void>(audio_runtime.preview_output.set_loop_region(std::nullopt));
                return false;
            }
            audio_runtime.preview_state.resume_frame = region->begin_frame;
            audio_runtime.preview_state.paused = false;
        } else {
            const auto previous_frame = audio_runtime.preview_state.resume_frame;
            const auto was_paused = audio_runtime.preview_state.paused;
            audio_runtime.preview_state.resume_frame = region->begin_frame;
            audio_runtime.preview_state.paused = false;
            static_cast<void>(audio_runtime.preview_state.play_pause(
                chart, session.history.document, audio_runtime.audition_sources,
                audio_runtime.preview_metronome_bindings, audio_runtime.audition_output,
                audio_runtime.preview_session, audio_runtime.preview_output, std::nullopt, false,
                region->begin_frame));
            if (!audio_runtime.preview_output.playing()) {
                audio_runtime.preview_state.resume_frame = previous_frame;
                audio_runtime.preview_state.paused = was_paused;
                static_cast<void>(audio_runtime.preview_output.set_loop_region(std::nullopt));
                return false;
            }
        }
        views.inline_preview_cursor.capture(
            views.compose_interaction_state ? &*views.compose_interaction_state : nullptr);
        views.inline_preview_cursor.capture_follow_anchor(
            session.history,
            views.compose_interaction_state ? &*views.compose_interaction_state : nullptr);
        return true;
    }
#endif

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    auto audition_inputs() -> wormedit::sys::compose_audition_inputs_t {
        return {{audio_runtime.preview_session, audio_runtime.preview_output,
                 audio_runtime.preview_state},
                {audio_runtime.audition_sources, audio_runtime.object_audition,
                 audio_runtime.audition_output},
                platform.notification_sink};
    }
#endif

    auto process_compose_frame_action(
        WormUiWorkspaceAction& action, double viewport_delta_seconds,
        const std::optional<wormedit::document_command_result_t>& tap_applied) -> bool {
        auto& tap_event_ns = current_tap_event_ ? *current_tap_event_ : views.last_tap_event_ns;

        // Apply consumed this frame's document snapshot. Never replay its shortcuts,
        // pointer input or navigation against the newly committed timing revision.
        if (tap_applied && tap_applied->applied)
            return true;
        // Complete a prior measure move before a new keyboard command can edit its destination.
        if (has_fresh_shortcut_key(action) && views.compose_interaction_state)
            views.compose_scene_cache.navigation_travel.finish(*views.compose_interaction_state,
                                                               session.history.document.revision);
        bool preserves_editor_gesture{};
        const bool shortcut_handled =
            action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
            dispatch_application_shortcuts(action, tap_event_ns, preserves_editor_gesture);
        if (!session.project_identity.session_loaded)
            return shortcut_handled;
        if (shortcut_handled && !preserves_editor_gesture && views.compose_interaction_state &&
            editor_interaction_has_pending_gesture(*views.compose_interaction_state)) {
            (void)wormedit::dispatch_editor_action(session.history,
                                                   *views.compose_interaction_state,
                                                   wormedit::cancel_editor_gesture_action_t{});
        }
        if (shortcut_handled && !preserves_editor_gesture) {
            views.audio_offset_drag.reset();
            views.audio_offset_drag_tab.reset();
            views.compose_scene_cache.clear_beat_fit_capture();
            if (views.compose_interaction_state)
                views.compose_scene_cache.navigation_travel.finish(
                    *views.compose_interaction_state, session.history.document.revision);
            views.compose_scene_cache.timing.offset_fit_drag.reset();
        }
        dispatch_bgm_mix_frame_action(action, shortcut_handled);
        if (!shortcut_handled && action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
            (action.compose_input_count != 0 || action.compose_viewport_input_count != 0 ||
             action.compose_has_pointer_hover ||
             (session.workspace.active_tab == wormedit::workspace_tab_t::compose &&
              action.compose_input_chart_id != 0)) &&
            views.compose_interaction_state) {
            (void)dispatch_compose_input_action(action, compose_inputs(),
                                                *views.compose_interaction_state,
                                                viewport_delta_seconds, true);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            wormedit::sys::process_compose_audition_requests(views.compose_scene_cache,
                                                             session.history, audition_inputs());
#else
            wormedit::sys::process_compose_audition_requests(
                views.compose_scene_cache, session.history, platform.notification_sink);
#endif
        }
        refresh_compose_editor_width(action, session.history, session.workspace, commands);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        wormedit::sys::process_sound_audition_request(
            commands.project.sound_selection.pending_sound_audition, session.history,
            audition_inputs());
#else
        wormedit::sys::process_sound_audition_request(
            commands.project.sound_selection.pending_sound_audition, platform.notification_sink);
#endif
        if (session.workspace.active_tab != wormedit::workspace_tab_t::compose) {
            commands.project.semantic_layer.note_layer_menu_row_count = 0;
            views.compose_viewport_input.escape();
            views.compose_scene_cache.frame.viewport_autoscroll_active = false;
            if (views.compose_interaction_state)
                views.compose_scene_cache.navigation_travel.finish(
                    *views.compose_interaction_state, session.history.document.revision);
        }
        if (!shortcut_handled && action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
            views.compose_interaction_state &&
            session.workspace.active_tab == wormedit::workspace_tab_t::compose) {
            (void)dispatch_compose_navigation_actions(
                views.compose_navigation_input.advance(static_cast<std::uint64_t>(SDL_GetTicks())),
                session.history, *views.compose_interaction_state, &commands,
                &platform.notification_sink);
        } else if (session.workspace.active_tab != wormedit::workspace_tab_t::compose) {
            views.compose_navigation_input.cancel();
        }
        return shortcut_handled;
    }

    void finish_confirmed_project_replacement(pending_close_t& pending) {
        WormUiWorkspaceAction action{};
        action.new_project_layout_index = pending.layout_index;
        action.bms_encoding_index = pending.encoding_index;
        std::snprintf(action.project_path, sizeof(action.project_path), "%s",
                      pending.project_path.c_str());
        if (pending.target == close_target_t::new_project)
            create_new_project_session(action);
        else
            open_project_session(action);
        // Encoding choice continues the same approved replacement of this document version.
        if (pending.target == close_target_t::open_file && !pending.project_path.empty() &&
            session.project_identity.pending_import.bms_encoding_path == pending.project_path) {
            pending.awaiting_encoding = true;
            return;
        }
        pending = {};
    }

    auto nudge_audio_offset_drag(AudioOffsetDragContext& context) -> std::string {
        using namespace wormedit;
        if (views.audio_offset_drag) {
            return post_audio_offset_feedback(
                context, notification_severity_t::warning,
                "Finish or cancel the active audio offset drag before nudging.");
        }
        const auto begun =
            begin_audio_offset_drag(session.history.document, *session.workspace.selected_chart,
                                    *context.selected_sample_rate, context.timing_locked);
        if (!begun.is_valid()) {
            return post_audio_offset_diagnostic(context, begun.diagnostics,
                                                "Audio offset nudge could not start.",
                                                notification_severity_t::warning);
        }
        const auto updated = revise_audio_offset_drag(session.history.document, begun,
                                                      context.action.audio_offset_drag_delta_frames,
                                                      context.timing_locked);
        if (!updated.is_valid()) {
            return post_audio_offset_diagnostic(context, updated.diagnostics,
                                                "Audio offset nudge was rejected.",
                                                notification_severity_t::warning);
        }
        const auto committed =
            apply_audio_offset_drag(session.history, updated, context.timing_locked);
        if (!committed.applied) {
            return post_audio_offset_diagnostic(context, committed.diagnostics,
                                                "Audio offset was unchanged.",
                                                notification_severity_t::info);
        }
        const auto rebound = rebind_workspace(session.workspace, session.history);
        return post_audio_offset_feedback(
            context, rebound ? notification_severity_t::success : notification_severity_t::error,
            rebound ? "Audio offset nudged by an exact frame count."
                    : "Audio offset changed, but workspace rebinding failed.");
    }

    auto post_audio_offset_diagnostic(AudioOffsetDragContext& context,
                                      const diagnostic_list_t& diagnostics, std::string fallback,
                                      wormedit::notification_severity_t fallback_severity)
        -> std::string {
        if (!diagnostics.empty())
            return post_audio_offset_feedback(
                context, notification_severity_for(diagnostics.front().severity),
                diagnostics.front().message);
        return post_audio_offset_feedback(context, fallback_severity, std::move(fallback));
    }

    bool dispatch_timing_workspace_action(const WormUiWorkspaceAction& action,
                                          application_command_state_t& application_state) {
        if (action.timing_stage_action != WORM_UI_TIMING_STAGE_NONE) {
            const auto chart = std::ranges::find(session.history.document.charts,
                                                 wormedit::chart_id_t{action.timing_chart_id},
                                                 &wormedit::chart_document_t::id);
            const auto denominator =
                chart == session.history.document.charts.end()
                    ? views.timing_grid.active_denominator
                    : wormedit::effective_edit_snap_denominator(
                          *chart, views.timing_grid.active_denominator,
                          application_state.project.compose_view.edit_snap_enabled);
            dispatch_timing_stage_action(
                action, application_state.project.timing_recipe.timing_locked, denominator);
            return true;
        }
        return false;
    }

    void prepare_bound_workspace_frame() {
        wormedit::sys::frame_phase_scope_t preparation(
            wormedit::sys::frame_phase_t::workspace_preparation);
        if (session.workspace.selected_chart) {
            const auto chart = std::ranges::find(session.history.document.charts,
                                                 *session.workspace.selected_chart,
                                                 &wormedit::chart_document_t::id);
            if (chart != session.history.document.charts.end()) {
                views.timing_grid = refresh_timing_grid(*chart, commands);
            }
        }
        if (views.audio_offset_drag &&
            (!views.audio_offset_drag_tab ||
             session.workspace.active_tab != *views.audio_offset_drag_tab ||
             !session.workspace.selected_chart ||
             !views.audio_offset_drag->stamp.is_current(session.history.document,
                 *session.workspace.selected_chart, document_identity_policy_t::revision_only) ||
             commands.audio_offset_edits_locked())) {
            views.audio_offset_drag.reset();
            views.audio_offset_drag_tab.reset();
        }
        if (views.compose_scene_cache.timing.beat_fit_drag &&
            (session.workspace.active_tab != wormedit::workspace_tab_t::compose ||
             !session.workspace.selected_chart ||
             !views.compose_scene_cache.timing.beat_fit_drag->stamp.is_current(
                 session.history.document, *session.workspace.selected_chart,
                 document_identity_policy_t::revision_only) ||
             session.workspace.transport.audio_asset !=
                 views.compose_scene_cache.timing.beat_fit_asset ||
             commands.project.timing_recipe.timing_locked)) {
            views.compose_scene_cache.clear_beat_fit_capture();
        }
        if (views.compose_scene_cache.timing.offset_fit_drag &&
            (session.workspace.active_tab != wormedit::workspace_tab_t::compose ||
             !session.workspace.selected_chart ||
             !views.compose_scene_cache.timing.offset_fit_drag->beat.stamp.is_current(
                 session.history.document, *session.workspace.selected_chart,
                 document_identity_policy_t::revision_only) ||
             session.workspace.transport.audio_asset !=
                 views.compose_scene_cache.timing.offset_fit_drag->asset ||
             commands.audio_offset_edits_locked())) {
            views.compose_scene_cache.timing.offset_fit_drag.reset();
        }
    }

    void confirm_unsaved_changes(pending_close_t& pending, close_target_t target, bool confirm,
                                 const WormUiWorkspaceAction* replacement = nullptr) {
        if (target != close_target_t::application && is_project_replacement_busy())
            return;
        // A close request ends transient gestures; cancellation returns a usable editor.
        views.compose_navigation_input.cancel();
        views.compose_viewport_input.escape();
        if (views.compose_interaction_state)
            (void)wormedit::dispatch_editor_action(session.history,
                                                   *views.compose_interaction_state,
                                                   wormedit::cancel_editor_gesture_action_t{});
        pending = {.target = target,
                   .project = session.history.document.id,
                   .revision = session.history.document.revision};
        if (replacement) {
            pending.project_path = replacement->project_path;
            pending.layout_index = replacement->new_project_layout_index;
            pending.encoding_index = replacement->bms_encoding_index;
        }
        if (!confirm || !session.project_identity.session_loaded ||
            !wormedit::is_document_dirty(session.history))
            (void)finish_confirmed_close(pending);
    }

    void dispatch_bgm_mix_frame_action(WormUiWorkspaceAction& action, bool shortcut_handled) {
        if (!shortcut_handled && action.kind != WORM_UI_WORKSPACE_ACTION_QUIT)
            (void)dispatch_compose_bgm_mix_inputs(action, session.history, session.workspace,
                                                  commands, &platform.notification_sink);
    }

    bool dispatch_project_chart_action(const WormUiWorkspaceAction& frame_action, bool handled) {
        auto& runtime = views.project_chart_runtime;
        auto& history = session.history;
        auto& workspace = session.workspace;
        auto& interaction = views.compose_interaction_state;
        auto& application = commands;
        auto& notifications = platform.notification_sink;
        auto& loaded = session.project_identity.session_loaded;
        auto& scheduler = jobs.resource_copy_scheduler;

        if (handled)
            return true;
        const auto& action = frame_action.project_chart;
        const auto& group = frame_action.project_output_group;
        const bool group_action = group.kind != WORM_UI_PROJECT_OUTPUT_GROUP_NONE;
        if (!loaded || (!group_action && action.kind == WORM_UI_PROJECT_CHART_NONE))
            return false;
        if (group_action &&
            (application.project.clipboard.paste_session ||
             (interaction && editor_interaction_has_pending_gesture(*interaction)))) {
            notifications.post(
                wormedit::notification_severity_t::warning,
                wormedit::notification_source_t::document,
                "Finish or cancel the editor gesture before changing output groups.");
            return true;
        }
        const auto previous_chart = workspace.selected_chart;
        auto result = group_action
                          ? wormedit::sys::apply_project_output_group_ui_action(runtime, history,
                                                                                group, scheduler)
                          : wormedit::sys::apply_project_chart_ui_action(runtime, history, action,
                                                                         workspace.selected_chart);
        notifications.post(result.severity, wormedit::notification_source_t::document,
                           result.status);
        if (result.applied) {
            const auto rebound = wormedit::rebind_workspace(workspace, history);
            if (!rebound) {
                if (rebound.diagnostics.empty())
                    notifications.post(wormedit::notification_severity_t::error,
                                       wormedit::notification_source_t::document,
                                       "Project changed, but workspace rebinding failed.");
                else
                    notifications.post(
                        notification_severity_for(rebound.diagnostics.front().severity),
                        wormedit::notification_source_t::document,
                        rebound.diagnostics.front().message);
                return true;
            }
        }
        if (result.open_chart) {
            if (application.services.transport_stop)
                application.services.transport_stop();
            const auto error =
                open_project_chart(history, workspace, interaction, *result.open_chart);
            if (!error.empty())
                notifications.post(wormedit::notification_severity_t::error,
                                   wormedit::notification_source_t::document, error);
        } else if (previous_chart != workspace.selected_chart) {
            if (application.services.transport_stop)
                application.services.transport_stop();
            interaction.reset();
        }
        return true;
    }

    void create_new_project_session(const WormUiWorkspaceAction& action) {
        const auto lane_count = requested_project_lane_count(action.new_project_layout_index);
        if (!lane_count) {
            post_project_feedback(wormedit::notification_severity_t::warning,
                                  wormedit::notification_source_t::document,
                                  "Choose a supported chart layout.");
            return;
        }
        if (action.project_path[0] != '\0') {
            create_audio_project_session(action.project_path, *lane_count);
            return;
        }
        auto document =
            wormedit::sys::initial_document(wormedit::sys::new_session_project_id(), *lane_count);
        if (!document) {
            post_project_feedback(wormedit::notification_severity_t::error,
                                  wormedit::notification_source_t::document,
                                  "Could not create the selected chart layout.");
            return;
        }
        if (replace_project_session(std::move(*document), true, true, false, {}, {})) {
            for (const auto& chart : session.history.document.charts)
                remember_snap_denominator(commands, chart.id,
                                          commands.preferences.editor_preferences.default_snap);
            session.project_identity.fresh_blank_project = true;
            post_project_feedback(wormedit::notification_severity_t::success,
                                  wormedit::notification_source_t::document,
                                  "New " + std::to_string(*lane_count) +
                                      "K project created. Save it to keep your work.");
        }
    }

    void dispatch_menu_command(WormUiWorkspaceAction& action) {
        const auto* command_id =
            action.command_menu_invoked ? action.command_menu_command_id : action.menu_command_id;
        if (!command_id[0])
            return;
        if (action.command_menu_invoked &&
            (!action.shortcut_window_focused || action.shortcut_ime_composition_active ||
             action.compose_input_project_id != session.history.document.id.value ||
             action.compose_input_chart_id !=
                 (session.workspace.selected_chart ? session.workspace.selected_chart->value : 0) ||
             action.compose_input_document_revision != session.history.document.revision ||
             static_cast<int>(action.tab) != static_cast<int>(session.workspace.active_tab)))
            return;
        if (!wormedit::sys::compose_menu_command_is_current(action, session.history,
                                                            session.workspace))
            return;
        // SDL forwards key events before AppKit invokes key equivalents. Retire that
        // same chord so a menu shortcut cannot run the registry command twice.
        if (!action.command_menu_invoked)
            consume_menu_shortcut(action, command_registry);
        refresh_application_command_states(command_registry, session.history, session.workspace,
                                           views.compose_interaction_state, commands);
        auto focus = application_command_focus(action, session.workspace);
        // Activation closes the palette; its own search field must not consume the command.
        if (action.command_menu_invoked || action.compose_menu_command)
            focus.text_entry_active = false;
        const auto result = wormedit::invoke_command(command_registry, {command_id}, focus);
        if (!result.diagnostics.empty())
            post_shortcut_rejection(platform.notification_sink, command_registry, result);
    }

    auto dispatch_registered_application_command(
        const wormedit::command_invocation_context_t& invocation,
        const std::optional<std::uint64_t>& tap_event_ns) -> command_invocation_result_t {
        auto& program = preferences.program_preferences;
        auto& history = session.history;
        auto& workspace = session.workspace;
        auto& compose_interaction_state = views.compose_interaction_state;
        auto& application_command_state = commands;
        auto& notifications = platform.notification_sink;

        using severity_t = wormedit::notification_severity_t;
        using source_t = wormedit::notification_source_t;
        if (auto toggled = dispatch_click_audition_toggle(invocation, program,
                                                          application_command_state, notifications))
            return *toggled;
        const auto command = parse_application_command(invocation.command.value);
        if (!command) {
            return wormedit::sys::reject_application_command(
                notifications, "Command is not implemented by the application.", severity_t::error,
                source_t::system);
        }
        if (command->kind == wormedit::sys::application_command_kind_t::document_new_project) {
            if (!worm_ui_frontend_request_new_project())
                return wormedit::sys::reject_application_command(
                    notifications, "Finish the open project dialog first.", severity_t::warning,
                    source_t::document);
            notifications.post_transient_hint(source_t::document,
                                              "Choose the layout for the new project.");
            return wormedit::command_invocation_result_t{.accepted = true};
        }
        if (command->kind == wormedit::sys::application_command_kind_t::document_save_project) {
            if (!worm_ui_frontend_request_save_project(history.document.id.value))
                return wormedit::sys::reject_application_command(
                    notifications, "Finish the open dialog first.", severity_t::warning,
                    source_t::document);
            return wormedit::command_invocation_result_t{.accepted = true};
        }
        if (command->kind == wormedit::sys::application_command_kind_t::document_new_chart)
            return create_chart_from_registered_command(history, workspace,
                                                        compose_interaction_state,
                                                        application_command_state, notifications);
        if (command->kind == wormedit::sys::application_command_kind_t::timing_tap_assistant) {
            views.tap_runtime.toggle();
            (void)views.tap_runtime.observe(views.tap_clock());
            return wormedit::command_invocation_result_t{.accepted = true};
        }
        if (command->kind == wormedit::sys::application_command_kind_t::timing_tap) {
            const auto clock = views.tap_clock();
            const auto result =
                views.tap_runtime.tap(tap_event_ns.value_or(clock.observed_ns), clock);
            const auto state = views.tap_runtime.state();
            if (state.status) {
                const bool guidance = result == wormedit::sys::tap_timing_result_t::ignored ||
                                      result == wormedit::sys::tap_timing_result_t::not_playing;
                static_cast<void>(post_callback_feedback(
                    notifications, guidance ? severity_t::hint : severity_t::warning,
                    source_t::input, state.status));
            }
            return wormedit::command_invocation_result_t{.accepted = true};
        }
        return wormedit::sys::invoke_application_command(*command, command_inputs());
    }

    auto post_audio_offset_feedback(AudioOffsetDragContext& context,
                                    wormedit::notification_severity_t severity, std::string message)
        -> std::string {
        return post_callback_feedback(platform.notification_sink, severity,
                                      wormedit::notification_source_t::edit, std::move(message));
    }

    void dispatch_timing_stage_value_action(
        const WormUiWorkspaceAction& action, bool timing_locked,
        std::uint64_t active_snap_denominator, wormedit::chart_document_t* chart,
        std::optional<wormedit::timing_lane_pseudonote_preview_t>& timing_stage) {
        const auto value = chart == nullptr ? std::optional<wormedit::timing_lane_value_t>{}
                                            : timing_stage_value(*timing_stage, *chart, action,
                                                                 active_snap_denominator);
        if (!value) {
            timing_stage->diagnostics = {{
                .severity = wormedit::diagnostic_severity_t::error,
                .code = wormedit::diagnostic_code_t::invalid_rational,
                .message = "Enter a bounded exact rational for this timing value.",
            }};
            platform.notification_sink.post(wormedit::notification_severity_t::warning,
                                            wormedit::notification_source_t::edit,
                                            "Timing value is outside its supported bounds.");
            return;
        }

        const bool valid = wormedit::revise_timing_lane_pseudonote(session.history, *timing_stage,
                                                                   *value, timing_locked);
        if (action.timing_stage_action == WORM_UI_TIMING_STAGE_CONFIRM && valid) {
            const auto result = wormedit::apply_timing_lane_pseudonote(
                session.history, *timing_stage, timing_locked);
            const auto edited_existing = timing_stage->existing_event.has_value();
            if (result)
                platform.notification_sink.post(wormedit::notification_severity_t::success,
                                                wormedit::notification_source_t::edit,
                                                edited_existing ? "Timing event updated."
                                                                : "Timing event inserted.");
            else if (result.diagnostics.empty() && edited_existing)
                platform.notification_sink.post(wormedit::notification_severity_t::info,
                                                wormedit::notification_source_t::edit,
                                                "Timing event was unchanged.");
            else
                post_edit_rejection(platform.notification_sink, result.diagnostics,
                                    "Timing event insertion was rejected.");
            timing_stage.reset();
        } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_CONFIRM) {
            post_edit_rejection(platform.notification_sink, timing_stage->diagnostics,
                                "Timing event values are invalid.");
        }
    }

    void dispatch_timing_stage_action(const WormUiWorkspaceAction& action, bool timing_locked,
                                      std::uint64_t active_snap_denominator) {
        using status_t = wormedit::timing_lane_pseudonote_status_t;
        auto& timing_stage = views.timing_edit.stage;
        if (action.timing_stage_action == WORM_UI_TIMING_STAGE_BEGIN) {
            if (!timing_stage) {
                auto candidate = begin_timing_stage(action, session.history, session.workspace,
                                                    timing_locked, active_snap_denominator);
                if (candidate.status == status_t::editing) {
                    timing_stage = std::move(candidate);
                    platform.notification_sink.post_transient_hint(
                        wormedit::notification_source_t::edit,
                        timing_stage->existing_event.has_value()
                            ? "Timing event loaded; apply to update it."
                            : "Timing event staged; confirm to insert it.");
                } else {
                    post_edit_rejection(platform.notification_sink, candidate.diagnostics,
                                        "Timing event could not be staged at that row.");
                }
            }
        } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_QUICK_INSERT) {
            if (timing_stage) {
                post_edit_rejection(
                    platform.notification_sink, {},
                    "Confirm or cancel the open timing edit before inserting another event.");
            } else {
                quick_insert_timing_event(action, session.history, session.workspace, timing_locked,
                                          active_snap_denominator, platform.notification_sink);
            }
        } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_CANCEL) {
            if (timing_stage) {
                wormedit::cancel_timing_lane_pseudonote(*timing_stage);
                timing_stage.reset();
                platform.notification_sink.post_transient_hint(
                    wormedit::notification_source_t::edit, "Timing event staging cancelled.");
            }
        } else if (timing_stage) {
            dispatch_active_timing_stage_action(action, timing_locked, active_snap_denominator);
        }
    }

    void dispatch_options_page_action(const WormUiOptionsAction& action) {
        if (handle_shortcut_editor_action(preferences.shortcut_editor, action,
                                          preferences.options_runtime, platform.notification_sink))
            return;
        dispatch_options_frame_action(action);
    }

    auto handle_workspace_view_error(const workspace_view_result_t& model,
                                     const WormUiNotificationView& notifications, bool confirm)
        -> bool {
        const wormedit::sys::projection_identity_t identity{
            session.history.document.id, session.history.document.revision,
            session.workspace.selected_chart.value_or(wormedit::chart_id_t{}),
            views.project_chart_runtime.session_generation};
        if (model) {
            views.workspace_view_feedback.publish(identity, {}, platform.notification_sink);
            return false;
        }
        const auto diagnostic = model.diagnostics.empty()
                                    ? std::string{"Workspace view unavailable"}
                                    : model.diagnostics.front().message;
        views.workspace_view_feedback.publish(
            identity,
            {{wormedit::sys::projection_feedback_kind_t::workspace_view, 0,
              wormedit::sys::projection_diagnostic_severity(model.diagnostics),
              wormedit::notification_source_t::document, diagnostic}},
            platform.notification_sink);
        const auto close = close_confirmation_view(
            views.pending_close, session.project_identity.native_project_loaded &&
                                     !session.project_identity.active_project_path.empty());
        const auto action =
            worm_ui_frontend_workspace_error(diagnostic.c_str(), &notifications, &close);
        views.pending_notification = action.notifications;
        if (dispatch_close_confirmation(views.pending_close, action, confirm))
            return true;
        if (action.kind == WORM_UI_WORKSPACE_ACTION_SWITCH_TAB) {
            const auto changed = wormedit::apply_workspace_action(
                session.workspace, session.history.document,
                wormedit::workspace_switch_tab_action_t{wormedit::workspace_tab_t::setup});
            if (!changed && !changed.diagnostics.empty())
                platform.notification_sink.post(
                    wormedit::sys::projection_diagnostic_severity(changed.diagnostics),
                    wormedit::notification_source_t::document, changed.diagnostics.front().message);
        }
        return true;
    }

    void dispatch_options_frame_action(const WormUiOptionsAction& action) {
        const bool accepted = wormedit::sys::apply_options_action(
            preferences.options_runtime, preferences.program_preferences, action);
        worm_ui_frontend_set_snap_grid_opacity(
            preferences.options_runtime.visible
                ? preferences.options_runtime.draft.display.snap_grid_opacity_percent
                : preferences.program_preferences.preferences.display.snap_grid_opacity_percent);
        worm_ui_frontend_set_field_waveform_opacity(
            preferences.options_runtime.visible
                ? preferences.options_runtime.draft.display.field_waveform_opacity_percent
                : preferences.program_preferences.preferences.display
                      .field_waveform_opacity_percent);
        update_scratch_presentation(preferences.options_runtime.visible
                                        ? preferences.options_runtime.draft.display
                                        : preferences.program_preferences.preferences.display,
                                    commands);
        if (preferences.options_runtime.notification)
            platform.notification_sink.post(*preferences.options_runtime.notification);
        if (!accepted)
            return;
        if (preferences.options_runtime.category == WORM_UI_OPTIONS_GENERAL)
            (void)session.autosave_loop.set_interval(std::chrono::minutes{
                preferences.program_preferences.preferences.general.autosave_minutes});
        if (preferences.options_runtime.category == WORM_UI_OPTIONS_EDITOR) {
            commands.preferences.editor_preferences =
                preferences.program_preferences.preferences.editor;
            commands.preferences.minimap_click_moves_paused_playhead =
                preferences.program_preferences.preferences.minimap.click_moves_paused_playhead;
        }
        if (preferences.options_runtime.category == WORM_UI_OPTIONS_PLAYERS) {
            preferences.external_player_runtime.preferences =
                preferences.program_preferences.preferences.players;
            ++preferences.external_player_runtime.revision;
        }
        if (preferences.options_runtime.category == WORM_UI_OPTIONS_AUDIO)
            initialize_preferred_transport(preferences.program_preferences.preferences, commands);
        if (preferences.options_runtime.category == WORM_UI_OPTIONS_DISPLAY) {
            worm_ui_frontend_set_snap_colored_grid(
                preferences.program_preferences.preferences.display.snap_colored_grid);
        }
    }

    void dispatch_active_timing_stage_action(const WormUiWorkspaceAction& action,
                                             bool timing_locked,
                                             std::uint64_t active_snap_denominator) {
        auto& stage = views.timing_edit.stage;
        if (!stage)
            return;
        if (auto failure = wormedit::sys::validate_timing_stage_binding(
                action, session.history, session.workspace, *stage)) {
            wormedit::cancel_timing_lane_pseudonote(*stage);
            stage.reset();
            post_edit_rejection(
                platform.notification_sink, {},
                *failure);
            return;
        }
        const auto chart = std::ranges::find(session.history.document.charts, stage->stamp.chart,
                                             &wormedit::chart_document_t::id);
        auto* chart_ptr = chart == session.history.document.charts.end() ? nullptr : &*chart;
        if (action.timing_stage_action == WORM_UI_TIMING_STAGE_DELETE) {
            delete_staged_timing_event(action, session.history, timing_locked, *stage,
                                       platform.notification_sink);
            stage.reset();
        } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_MOVE) {
            move_staged_timing_event(action, timing_locked, active_snap_denominator, chart_ptr,
                                     *stage);
        } else {
            dispatch_timing_stage_value_action(action, timing_locked, active_snap_denominator,
                                               chart_ptr, stage);
        }
    }

    auto start() -> bool {
        if (shutdown_started_)
            return false;
        if (started_)
            return true;
        if (!initialize_sdl_runtime(platform.lifetime.runtime))
            return false;
        const auto& requested_primary_path = platform.arguments.requested_primary_path;
        auto startup_request = std::move(platform.arguments.request);
        platform.recovery_directory =
            prepare_platform_recovery_directory(platform.notification_sink);
        if (platform.recovery_directory) {
            startup_request.recovery_directories.push_back(platform.recovery_directory.path);
            platform.lifetime.crash_marker = crash_marker_path(platform.recovery_directory.path);
            startup_request.crash_marker_path = platform.lifetime.crash_marker;
        }
        auto startup = prepare_native_project_startup(
            empty_session_document(new_session_project_id()), std::move(startup_request));
        if (!platform.lifetime.crash_marker.empty()) {
            if (const auto error =
                    write_crash_marker(platform.lifetime.crash_marker, startup.document.id);
                !error.empty())
                platform.notification_sink.post(notification_severity_t::warning,
                                                notification_source_t::recovery, error);
        }
        if (!initialize_application_runtime(platform.lifetime.runtime))
            return false;
        platform.frontend_notifications.emplace(platform.notification_sink);
        session.project_identity.asset_base =
            startup.loaded_primary ? std::filesystem::absolute(requested_primary_path).parent_path()
                                   : std::filesystem::path{};
        refresh_project_audio_availability(startup.document, session.project_identity.asset_base);
        session.history = make_document_history(std::move(startup.document), 128, true);
        auto binding = make_workspace_controller(session.history);
        if (!binding) {
            std::fprintf(stderr, "Initial workspace binding failed\n");
            return false;
        }
        session.workspace = std::move(*binding.workspace);
        const auto sequence = native_project_recovery_session_sequence(startup.recovery_scan,
                                                                       session.history.document.id);
        (void)session.autosave_loop.reset_session(native_project_autosave_loop_t::clock_t::now(),
                                                  startup.primary_version, sequence);
        post_initial_application_feedback(platform.notification_sink, startup.loaded_primary,
                                          requested_primary_path, platform.recovery_directory);
        seed_notification_smoke(platform.notifications);
        seed_notification_history_smoke(platform.notifications, session.history.document);
        session.project_identity.session_loaded = startup.loaded_primary;
        session.project_identity.native_project_loaded = startup.loaded_primary;
        session.project_identity.active_project_path =
            startup.loaded_primary ? requested_primary_path : std::filesystem::path{};
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        audio_runtime.resident_samples.set_listener(
            [this](audio::resident_snapshot_ptr_t snapshot) {
                audio_runtime.audition_transport.set_resident_snapshot(snapshot);
                audio_runtime.preview_transport.set_resident_snapshot(std::move(snapshot));
            });
        audio_runtime.resident_loader.set_head_cache(&audio_runtime.preview_cache);
        audio_runtime.reconciliation.emplace(audition_source_reconciliation_context_t{
            session.history.document, session.project_identity.asset_base,
            audio_runtime.audition_source_project, audio_runtime.audition_asset_inventory,
            audio_runtime.audition_source_generation, audio_runtime.audition_sources,
            audio_runtime.preview_metronome_bindings,
            [this] {
                (void)audio_runtime.audition_output.stop();
                (void)audio_runtime.preview_output.stop();
            },
            [this] { audio_runtime.waveform_runtime.reset_sources(); },
            [this] { audio_runtime.preview_state.invalidate(); },
            &audio_runtime.audition_probe_scheduler, &audio_runtime.resident_loader});
        post_audio_inventory_feedback(
            reconcile_audition_sources((*audio_runtime.reconciliation), false),
            platform.notification_sink);
        audio_runtime.audition_reconciled_revision = session.history.document.revision;
        std::string audition_output_error;
        if (!audio_runtime.audition_output.open(&audition_output_error))
            platform.notification_sink.post(notification_severity_t::error,
                                            notification_source_t::audio,
                                            "Audio output unavailable: " + audition_output_error);
        std::string preview_output_error;
        if (!audio_runtime.preview_output.open(&preview_output_error))
            platform.notification_sink.post(
                notification_severity_t::error, notification_source_t::audio,
                "Chart preview output unavailable: " + preview_output_error);
#endif
        install_startup_recovery_scan(session.recovery_controller, startup);
        session.recovery_scan_feedback.publish(session.recovery_controller.view(),
                                               platform.notification_sink);
        session.recovery_scan_configured = startup.recovery_scan_configured;
        session.project_identity.scanned_primary_version = startup.primary_version;
        commands.services.open_bookmark_chooser = worm_ui_frontend_open_bookmark_chooser;
        commands.services.open_sound_picker = worm_ui_frontend_open_sound_picker;
        commands.services.compose_dock_command = worm_ui_frontend_compose_dock_command;
        commands.services.open_project_export = worm_ui_frontend_open_project_export;
        commands.services.compose_left_dock_pixels = worm_ui_frontend_compose_left_dock_pixels;
        commands.services.compose_bgm_lane_width = worm_ui_frontend_compose_bgm_lane_width;
        preferences.program_preferences = load_program_preferences_runtime();
        remember_startup_file(preferences.program_preferences, startup.loaded_primary,
                              requested_primary_path);
        initialize_program_preferences(preferences.program_preferences,
                                       preferences.external_player_runtime, commands,
                                       platform.notification_sink);
        worm_ui_frontend_select_field_scheme(
            preferences.program_preferences.preferences.display.field_scheme.c_str());
        update_scratch_presentation(preferences.program_preferences.preferences.display, commands);
        worm_ui_frontend_set_snap_colored_grid(
            preferences.program_preferences.preferences.display.snap_colored_grid);
        worm_ui_frontend_set_snap_grid_opacity(
            preferences.program_preferences.preferences.display.snap_grid_opacity_percent);
        worm_ui_frontend_set_field_waveform_opacity(
            preferences.program_preferences.preferences.display.field_waveform_opacity_percent);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        audio_runtime.resident_loader.set_budget_bytes(
            static_cast<std::size_t>(
                preferences.program_preferences.preferences.audio.keysound_budget_mib) *
            1024ULL * 1024ULL);
#endif
        (void)session.autosave_loop.set_interval(std::chrono::minutes{
            preferences.program_preferences.preferences.general.autosave_minutes});
        commands.services.external_player = [this](external_player_action_t action) -> std::string {
            return launch_external_player(action);
        };
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        configure_transport_commands();
#endif
        initialize_preferred_transport(preferences.program_preferences.preferences, commands);
        views.tap_clock = [this]() -> tap_timing_clock_t {
            return read_tap_clock(tap_clock_context);
        };
        command_registry = make_default_editor_command_registry(
            command_menu_invoker([this](const command_invocation_context_t& invocation) {
                return invoke_registry_command(invocation);
            }));
        const auto shortcut_startup =
            load_preferred_shortcuts(preferences.program_preferences, command_registry);
        post_shortcut_startup_diagnostic(platform.notification_sink, shortcut_startup);
        views.compose_viewport_last_ticks = static_cast<std::uint64_t>(SDL_GetTicks());
        started_ = true;
        return true;
    }

    void shutdown() noexcept {
        if (shutdown_started_)
            return;
        shutdown_started_ = true;
        jobs.resource_copy_runtime.cancel();
        jobs.metadata_asset_runtime.cancel();
        jobs.export_runtime.cancel();
        jobs.resource_copy_scheduler.shutdown();
        jobs.metadata_asset_runtime.cleanup(session.project_identity.asset_base);
        jobs.export_runtime.shutdown();
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        audio_runtime.preview_output.close();
        audio_runtime.audition_output.close();
#endif
        preferences.external_player_runtime.player.cleanup();
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        jobs.player_image_job.cancel();
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
        jobs.player_video_job.cancel();
#endif
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        audio_runtime.resident_loader.cancel();
        audio_runtime.waveform_runtime.shutdown();
#endif
        workers.shutdown();
        platform.lifetime.shutdown();
    }

    void publish_tap_assist() {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        publish_tap_assist_grid(views.tap_runtime, audio_runtime.preview_session,
                                views.published_assist);
#endif
    }

    auto invoke_registry_command(const command_invocation_context_t& invocation)
        -> command_invocation_result_t {
        return dispatch_options_or_editor_command(
            invocation, preferences.options_runtime, preferences.program_preferences.preferences,
            views.notification_history, platform.notifications, [this, &invocation] {
                const auto& tap_event_ns =
                    current_tap_event_ ? *current_tap_event_ : views.last_tap_event_ns;
                return dispatch_registered_application_command(invocation, tap_event_ns);
            });
    }

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    auto on_transport_play_pause() -> std::string {
        commands.invalidate_playing_seek_records();
        const auto compose_cursor =
            session.workspace.active_tab == wormedit::workspace_tab_t::compose &&
                    views.compose_interaction_state && session.workspace.selected_chart &&
                    views.compose_interaction_state->chart == *session.workspace.selected_chart
                ? &*views.compose_interaction_state
                : nullptr;
        if (!audio_runtime.preview_output.playing() && !audio_runtime.preview_state.paused &&
            compose_cursor && compose_cursor->virtual_bms_cursor_row)
            return post_callback_feedback(
                platform.notification_sink, wormedit::notification_severity_t::warning,
                wormedit::notification_source_t::audio,
                "Materialize the cursor measure before starting chart preview.");
        const bool starting =
            !audio_runtime.preview_output.playing() && !audio_runtime.preview_state.paused;
        const auto exact_start = starting ? wormedit::sys::compose_minimap_start_frame(
                                                commands, session.history, compose_cursor)
                                          : std::nullopt;
        const auto result = audio_runtime.preview_state.play_pause(
            selected_chart(session.history, session.workspace), session.history.document,
            audio_runtime.audition_sources, audio_runtime.preview_metronome_bindings,
            audio_runtime.audition_output, audio_runtime.preview_session,
            audio_runtime.preview_output,
            compose_cursor ? std::optional{compose_cursor->cursor} : std::nullopt, false,
            exact_start, compose_cursor ? compose_cursor->cursor_subrow : 0.0);
        if (starting && audio_runtime.preview_output.playing()) {
            commands.project.transport.minimap_start_frame.reset();
            views.inline_preview_cursor.capture(compose_cursor);
        }
        if (audio_runtime.preview_output.playing())
            views.inline_preview_cursor.capture_follow_anchor(session.history, compose_cursor);
        return post_callback_feedback(
            platform.notification_sink,
            routine_transport_severity(audio_runtime.preview_state.feedback.severity,
                                       audio_runtime.preview_state.feedback.accepted),
            wormedit::notification_source_t::audio, result);
    }

    auto on_transport_seek(std::uint64_t frame) -> std::string {
        if (!audio_runtime.preview_output.playing() && !audio_runtime.preview_state.paused &&
            session.workspace.active_tab == wormedit::workspace_tab_t::compose &&
            views.compose_interaction_state && session.workspace.selected_chart &&
            views.compose_interaction_state->chart == *session.workspace.selected_chart) {
            if (wormedit::sys::editor_interaction_has_pending_gesture(
                    *views.compose_interaction_state))
                return post_callback_feedback(
                    platform.notification_sink, wormedit::notification_severity_t::warning,
                    wormedit::notification_source_t::input,
                    "Finish or cancel the active Compose gesture before seeking.");
            return wormedit::sys::seek_stopped_compose_transport(
                session.history, *views.compose_interaction_state, views.compose_scene_cache, frame,
                platform.notification_sink);
        }
        const auto result = audio_runtime.preview_state.seek(
            frame, session.history.document, session.workspace, audio_runtime.audition_output,
            audio_runtime.preview_session, audio_runtime.preview_output);
        return post_callback_feedback(platform.notification_sink,
                                      audio_runtime.preview_state.feedback.severity,
                                      wormedit::notification_source_t::audio, result);
    }

    auto on_transport_audio_milliseconds() -> std::optional<std::int64_t> {
        return audio_runtime.preview_state.audio_milliseconds(
            session.history.document, session.workspace, audio_runtime.preview_session);
    }

    auto on_transport_seek_audio_milliseconds(std::int64_t milliseconds) -> std::string {
        const auto result = audio_runtime.preview_state.seek_audio_milliseconds(
            milliseconds, session.history.document, session.workspace,
            audio_runtime.audition_output, audio_runtime.preview_session,
            audio_runtime.preview_output);
        return post_callback_feedback(platform.notification_sink,
                                      audio_runtime.preview_state.feedback.severity,
                                      wormedit::notification_source_t::audio, result);
    }

    auto on_transport_set_bgm_mix(wormedit::chart_id_t chart, std::span<const std::uint64_t> muted)
        -> bool {
        if (session.workspace.selected_chart != chart)
            return false;
        audio_runtime.preview_state.set_bgm_mix(chart, muted, audio_runtime.preview_session);
        return true;
    }

    auto on_transport_set_layer_assist(wormedit::chart_id_t chart,
                                       std::span<const wormedit::layer_id_t> layers) -> bool {
        return session.workspace.selected_chart == chart &&
               audio_runtime.preview_state.set_layer_assist(chart, layers,
                                                            audio_runtime.preview_session);
    }

    auto on_transport_set_metronome(bool enabled) -> void {
        audio_runtime.preview_session.set_metronome_enabled(enabled);
    }

    auto on_transport_set_metronome_volume(std::uint8_t volume) -> void {
        (void)audio_runtime.preview_session.set_metronome_volume(volume);
    }

    auto on_transport_set_metronome_count_in(std::uint8_t measures) -> void {
        if (audio_runtime.preview_session.set_metronome_count_in(measures))
            audio_runtime.preview_state.count_in_measures = measures;
    }

    auto on_transport_set_keep_pitch(bool enabled) -> bool {
        return audio_runtime.preview_output.set_keep_pitch(enabled);
    }

    auto on_transport_set_playback_rate(wormedit::rational_t rate) -> bool {
        return audio_runtime.preview_output.set_playback_rate(rate);
    }

    auto is_transport_active() -> bool {
        return audio_runtime.audition_session.active() || audio_runtime.preview_output.playing() ||
               audio_runtime.preview_state.paused;
    }

    auto is_transport_preview_playing() -> bool {
        return audio_runtime.preview_output.playing();
    }

    auto is_transport_preview_paused() -> bool {
        return audio_runtime.preview_state.paused;
    }

    auto on_transport_preserve_bookmark_revision(wormedit::chart_id_t chart,
                                                 std::uint64_t previous_revision,
                                                 std::uint64_t next_revision) -> bool {
        if (!audio_runtime.preview_output.playing() || !session.workspace.selected_chart ||
            *session.workspace.selected_chart != chart ||
            audio_runtime.preview_state.chart != chart ||
            audio_runtime.preview_state.revision != previous_revision)
            return false;
        return audio_runtime.preview_state.preserve_bookmark_revision(chart, previous_revision,
                                                                      next_revision);
    }

    auto on_transport_preview_end_frame() -> std::optional<std::uint64_t> {
        return audio_runtime.preview_state.matches_selection(session.history.document,
                                                             session.workspace)
                   ? std::optional{audio_runtime.preview_state.end_frame}
                   : std::nullopt;
    }

    auto is_transport_inline_active() -> bool {
        return audio_runtime.preview_output.playing() || audio_runtime.preview_state.paused;
    }

    auto on_transport_play_from_start() -> std::string {
        commands.invalidate_playing_seek_records();
        commands.project.transport.minimap_start_frame.reset();
        const bool starting =
            !audio_runtime.preview_output.playing() && !audio_runtime.preview_state.paused;
        const auto* compose_cursor =
            session.workspace.active_tab == wormedit::workspace_tab_t::compose &&
                    views.compose_interaction_state && session.workspace.selected_chart &&
                    views.compose_interaction_state->chart == *session.workspace.selected_chart
                ? &*views.compose_interaction_state
                : nullptr;
        const auto result = audio_runtime.preview_state.play_pause(
            selected_chart(session.history, session.workspace), session.history.document,
            audio_runtime.audition_sources, audio_runtime.preview_metronome_bindings,
            audio_runtime.audition_output, audio_runtime.preview_session,
            audio_runtime.preview_output, std::nullopt, true);
        if (starting && audio_runtime.preview_output.playing())
            views.inline_preview_cursor.capture(compose_cursor);
        if (audio_runtime.preview_output.playing())
            views.inline_preview_cursor.capture_follow_anchor(session.history, compose_cursor);
        return post_callback_feedback(
            platform.notification_sink,
            routine_transport_severity(audio_runtime.preview_state.feedback.severity,
                                       audio_runtime.preview_state.feedback.accepted),
            wormedit::notification_source_t::audio, result);
    }

    auto on_transport_stop() -> void {
        commands.invalidate_playing_seek_records();
        commands.project.transport.minimap_start_frame.reset();
        audio_runtime.preview_state.stop(audio_runtime.audition_output,
                                         audio_runtime.preview_output);
        views.inline_preview_cursor.anchor.reset();
    }

    auto has_transport_loop_selection() -> bool {
        return audio_runtime.preview_session.has_loop_region();
    }

    auto on_transport_set_loop_selection(wormedit::chart_id_t chart_id,
                                         std::uint64_t expected_revision, std::uint64_t begin_row,
                                         std::uint64_t end_row, bool enabled) -> bool {
        return apply_loop_selection(chart_id, expected_revision, begin_row, end_row, enabled);
    }

    auto on_transport_cancel_restore() -> std::string {
        audio_runtime.preview_state.stop(audio_runtime.audition_output,
                                         audio_runtime.preview_output);
        const bool restored = views.inline_preview_cursor.restore(
            session.history,
            views.compose_interaction_state ? &*views.compose_interaction_state : nullptr);
        return post_callback_feedback(
            platform.notification_sink, wormedit::notification_severity_t::hint,
            wormedit::notification_source_t::audio,
            restored ? "Chart preview stopped; cursor restored." : "Chart preview stopped.");
    }

    void configure_transport_play_pause() {
        commands.services.transport_play_pause = [this]() -> std::string {
            return on_transport_play_pause();
        };
    }

    void configure_transport_seek_commands() {
        commands.preferences.transport_seek_posts_feedback = true;
        commands.services.transport_seek = [this](std::uint64_t frame) -> std::string {
            return on_transport_seek(frame);
        };
        commands.services.transport_audio_milliseconds = [this]() {
            return on_transport_audio_milliseconds();
        };
        commands.services.transport_seek_audio_milliseconds =
            [this](std::int64_t milliseconds) -> std::string {
            return on_transport_seek_audio_milliseconds(milliseconds);
        };
    }

    void configure_bgm_mix_command() {
        commands.services.transport_set_bgm_mix = [this](wormedit::chart_id_t chart,
                                                         std::span<const std::uint64_t> muted) {
            return on_transport_set_bgm_mix(chart, muted);
        };
    }

    void configure_transport_commands() {
        commands.preferences.transport_metronome_available =
            audio_runtime.preview_output.available() &&
            !audio_runtime.preview_metronome_bindings.empty();
        commands.services.transport_set_layer_assist =
            [this](wormedit::chart_id_t chart, std::span<const wormedit::layer_id_t> layers) {
                return on_transport_set_layer_assist(chart, layers);
            };
        configure_bgm_mix_command();
        commands.services.transport_set_metronome = [this](bool enabled) {
            return on_transport_set_metronome(enabled);
        };
        audio_runtime.preview_session.set_metronome_enabled(
            commands.preferences.transport_metronome_enabled);
        commands.services.transport_set_metronome_volume = [this](std::uint8_t volume) {
            return on_transport_set_metronome_volume(volume);
        };
        commands.services.transport_set_metronome_count_in = [this](std::uint8_t measures) {
            return on_transport_set_metronome_count_in(measures);
        };
        (void)audio_runtime.preview_session.set_metronome_volume(
            commands.preferences.metronome_volume);
        (void)audio_runtime.preview_session.set_metronome_count_in(
            commands.preferences.metronome_count_in);
        audio_runtime.preview_state.count_in_measures = commands.preferences.metronome_count_in;
        commands.preferences.transport_keep_pitch_available =
            audio_runtime.preview_output.available();
        commands.services.transport_set_keep_pitch = [this](bool enabled) {
            return on_transport_set_keep_pitch(enabled);
        };
        commands.services.transport_set_playback_rate = [this](wormedit::rational_t rate) {
            return on_transport_set_playback_rate(rate);
        };
        commands.services.transport_active = [this] { return is_transport_active(); };
        commands.services.transport_preview_playing = [this] {
            return is_transport_preview_playing();
        };
        commands.services.transport_preview_paused = [this] {
            return is_transport_preview_paused();
        };
        commands.services.transport_preserve_bookmark_revision =
            [this](wormedit::chart_id_t chart, std::uint64_t previous_revision,
                   std::uint64_t next_revision) {
                return on_transport_preserve_bookmark_revision(chart, previous_revision,
                                                               next_revision);
            };
        commands.services.transport_preview_end_frame = [this]() -> std::optional<std::uint64_t> {
            return on_transport_preview_end_frame();
        };
        commands.services.transport_inline_active = [this] { return is_transport_inline_active(); };
        configure_transport_play_pause();
        commands.services.transport_play_from_start = [this]() -> std::string {
            return on_transport_play_from_start();
        };
        configure_transport_seek_commands();
        commands.services.transport_stop = [this] { return on_transport_stop(); };
        commands.services.transport_loop_selection_active = [this] {
            return has_transport_loop_selection();
        };
        commands.services.transport_set_loop_selection =
            [this](wormedit::chart_id_t chart_id, std::uint64_t expected_revision,
                   std::uint64_t begin_row, std::uint64_t end_row, bool enabled) {
                return on_transport_set_loop_selection(chart_id, expected_revision, begin_row,
                                                       end_row, enabled);
            };
        commands.services.transport_cancel_restore = [this]() -> std::string {
            return on_transport_cancel_restore();
        };
    }

#endif
    struct composed_frame_inputs_t {
        const workspace_view_t& model;
        const WormUiNotificationView& notifications;
        std::span<const WormUiComposeLaneView> field_skin_lanes;
        double viewport_delta_seconds{};
    };

    void publish_frame_notice() {
        publish_project_portability_notice(views.portability_notice, session.history,
                                           views.project_chart_runtime.session_generation,
                                           platform.notification_sink);
    }

    void begin_frame(notification_time_t now) {
        session.recovery_scan_feedback.publish(session.recovery_controller.view(),
                                               platform.notification_sink);
        begin_notification_frame(views.pending_notification, now);
        wormedit::sys::frame_timing().begin_frame();
        views.frame_projection_arena.reset();
        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::events);
    }

    void poll_frame_events() {
        {
            wormedit::sys::frame_phase_scope_t input(wormedit::sys::frame_phase_t::events_input);
            if (!wormedit::sys::poll_frontend_events(
                    platform.lifetime.runtime.window, frontend_inputs(),
                    can_offer_audio_project_drop(session.project_identity.session_loaded,
                                                 session.project_identity.fresh_blank_project,
                                                 session.history),
                    session.project_identity.pending_import.chart_project_path,
                    views.sound_drop_transaction, views.pending_resource_batches))
                confirm_unsaved_changes(
                    views.pending_close, close_target_t::application,
                    preferences.program_preferences.preferences.general.confirm_unsaved_close);
        }
    }

    void import_frame_drops() {
        {
            wormedit::sys::frame_phase_scope_t drops(wormedit::sys::frame_phase_t::events_drops);
            import_dropped_resources(session.project_identity.session_loaded, session.history,
                                     session.workspace, views.pending_resource_batches,
                                     platform.notification_sink);
            dispatch_dropped_chart_project(
                session.project_identity.pending_import.chart_project_path, views.pending_close,
                preferences.program_preferences.preferences.general.confirm_unsaved_close);
        }
    }

    auto poll_frame_jobs() -> double {
        double compose_viewport_delta_seconds{};
        {
            wormedit::sys::frame_phase_scope_t jobs_phase(
                wormedit::sys::frame_phase_t::events_jobs);
            wormedit::sys::poll_application_jobs(
                jobs.metadata_asset_runtime, jobs.resource_copy_runtime, jobs.export_runtime,
                session.history, session.workspace, session.project_identity.asset_base);
            compose_viewport_delta_seconds =
                wormedit::sys::compose_frame_delta_seconds(views.compose_viewport_last_ticks);
            tick_loaded_project_autosave();
        }
        return compose_viewport_delta_seconds;
    }

    void service_frame_audio() {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::audio_service);
        reconcile_audition_sources_after_edit(
            (*audio_runtime.reconciliation), session.history.document.revision,
            audio_runtime.audition_reconciled_revision, platform.notification_sink);
        if (const auto failed =
                wormedit::sys::drain_audition_probe_failures(audio_runtime.audition_sources)) {
            platform.notification_sink.post(*failed);
        }
        if (const auto streaming = audio_runtime.resident_loader.drain_report()) {
            platform.notification_sink.post(*streaming);
        }
        if (const auto loading = audio_runtime.resident_loader.drain_progress()) {
            platform.notification_sink.post(*loading);
        }
        sync_layer_assist_selection();
        sync_bgm_mix_selection();
        wormedit::sys::service_audio_transport_frame(
            session.history.document, session.workspace, audio_runtime.preview_state,
            audio_runtime.preview_session, audio_runtime.audition_output,
            audio_runtime.preview_output, commands.project.transport.preview_playhead_absolute_row,
            platform.notification_sink);
        wormedit::sys::frame_timing().restore(wormedit::sys::frame_phase_t::count);
        audio_runtime.waveform_runtime.service(
            session.history.document, session.workspace, views.compose_scene_cache,
            views.compose_interaction_state ? &*views.compose_interaction_state : nullptr);
        views.compose_scene_cache.timing.fit_waveform =
            audio_runtime.waveform_runtime.waveform_analysis();
        views.compose_scene_cache.timing.fit_waveform_asset =
            audio_runtime.waveform_runtime.waveform_asset();
#endif
    }

    auto configure_frame_field_skin() -> std::vector<WormUiComposeLaneView> {
        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::workspace);
        return configure_field_skin(active_field_skin_layout(session.history.document,
                                                             session.workspace,
                                                             views.compose_interaction_state),
                                    preferences.options_runtime,
                                    preferences.program_preferences.preferences.display, commands);
    }

    void prepare_frame_model(workspace_view_result_t& model) {
        {
            wormedit::sys::frame_phase_scope_t preparation(
                wormedit::sys::frame_phase_t::workspace_preparation);
            cancel_stale_compose_interaction(views.compose_interaction_state, session.history,
                                             session.workspace);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            if (session.workspace.active_tab == wormedit::workspace_tab_t::compose &&
                views.compose_interaction_state &&
                commands.project.transport.preview_playhead_absolute_row &&
                (audio_runtime.preview_output.playing() || audio_runtime.preview_state.paused)) {
                wormedit::sys::frame_phase_scope_t follow(
                    wormedit::sys::frame_phase_t::workspace_follow);
                (void)wormedit::sys::follow_compose_playhead(
                    session.history, *views.compose_interaction_state,
                    *commands.project.transport.preview_playhead_absolute_row,
                    audio_runtime.preview_output.playing(),
                    retained_compose_geometry(views.compose_scene_cache),
                    views.inline_preview_cursor.follow_anchor_fraction.value_or(
                        commands.preferences.editor_preferences.playhead_anchor_percent / 100.0));
            }
#endif

            wormedit::sys::rebind_workspace_after_edit(session.workspace, session.history,
                                                       platform.notification_sink,
                                                       views.workspace_edit_feedback);
            model = wormedit::sys::build_workspace_frame_model(
                views.compare_scene_cache, session.workspace, session.history.document,
                views.project_chart_runtime.session_generation);
        }
        views.timing_edit.cancel_if_stale(session.history, session.workspace, commands);
    }

    auto build_frame_notifications(notification_time_t now) -> WormUiNotificationView {
        return project_application_notifications(
            platform.notifications, now, views.compose_interaction_state,
            views.notification_history, session.history.document,
            views.project_chart_runtime.session_generation);
    }

    auto prepare_bound_frame_view(const workspace_view_t& model) -> flattened_workspace_t {
        prepare_bound_workspace_frame();
        prepare_tap_proposal(views.tap_runtime, views.tap_clock, session.history.document,
                             commands.audio_offset_edits_locked(), views.compose_scene_cache);
        wormedit::sys::compose_bga_move_preview_input_t bga_move_preview{};
        bga_move_preview.project_id = session.history.document.id.value;
        worm_ui_frontend_bga_move_preview(&bga_move_preview.chart_id,
                                          &bga_move_preview.document_revision,
                                          &bga_move_preview.event_id, &bga_move_preview.y_pixels);
        return wormedit::sys::project_application_frame({
            .model = model,
            .history = session.history,
            .workspace = session.workspace,
            .frame_arena = &views.frame_projection_arena,
            .recovery_controller = session.recovery_controller,
            .recovery_scan_configured = session.recovery_scan_configured,
            .stepmania_export_policy = views.stepmania_export_policy,
            .asset_base = session.project_identity.asset_base,
            .timing_grid = views.timing_grid,
            .timing_scene_cache = views.timing_scene_cache,
            .timing_edit = views.timing_edit,
            .compose_scene_cache = views.compose_scene_cache,
            .player_preview_presenter = views.player_preview_presenter,
            .application_command_state = commands,
            .compose_interaction_state = views.compose_interaction_state,
            .bga_move_preview = bga_move_preview,
            .audio_offset_drag = views.audio_offset_drag,
            .audio_offset_drag_tab = views.audio_offset_drag_tab,
            .metadata_drop = views.metadata_drop,
            .metadata_asset_runtime = jobs.metadata_asset_runtime,
            .window = platform.lifetime.runtime.window,
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
            .player_image_job = jobs.player_image_job,
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
            .player_video_job = jobs.player_video_job,
#endif
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            .waveform_runtime = audio_runtime.waveform_runtime,
            .audition_sources = audio_runtime.audition_sources,
            .audition_source_generation = audio_runtime.audition_source_generation,
            .audition_session = audio_runtime.audition_session,
            .preview_session = audio_runtime.preview_session,
            .preview_state = audio_runtime.preview_state,
            .preview_output_available = audio_runtime.preview_output.available(),
#endif
        });
    }

    void complete_frame_view(flattened_workspace_t& flattened,
                             const composed_frame_inputs_t& frame) {
        validate_minimap_start_frame(commands, session.history, views.compose_interaction_state);
        flattened.view.minimap_click_moves_paused_playhead =
            commands.preferences.minimap_click_moves_paused_playhead;
        flattened.view.transport_rate_index =
            static_cast<uint8_t>(commands.preferences.transport_rate_index);
        flattened.view.transport_rate_available =
            static_cast<bool>(commands.services.transport_set_playback_rate);
        flattened.view.transport_keep_pitch = commands.preferences.transport_keep_pitch;
        flattened.view.transport_keep_pitch_available =
            commands.preferences.transport_keep_pitch_available;
        flattened.view.notifications = frame.notifications;
        flattened.view.options = wormedit::sys::options_view(preferences.options_runtime);
        flattened.view.options.current_field_layout_lanes = frame.field_skin_lanes.data();
        flattened.view.options.current_field_layout_lane_count = frame.field_skin_lanes.size();
        attach_shortcut_rows(flattened.view.options, preferences.options_runtime,
                             preferences.shortcut_editor, command_registry);
        attach_bgm_shortcut(flattened.view.compose, command_registry);
        flattened.view.timing_settings_visible = commands.preferences.timing_settings_visible;
        flattened.view.metronome_volume = commands.preferences.metronome_volume;
        flattened.view.metronome_count_in = commands.preferences.metronome_count_in;
        flattened.view.chart_import =
            wormedit::sys::chart_import_view(views.chart_import, session.history.document.revision);
        flattened.view.close_confirmation = close_confirmation_view(
            views.pending_close, session.project_identity.native_project_loaded &&
                                     !session.project_identity.active_project_path.empty());
        project_external_player_view(preferences.external_player_runtime, flattened.view);
        wormedit::sys::project_chart_view(views.project_chart_runtime, session.history.document,
                                          flattened.view.setup);
        flattened.view.setup.selected_chart_id = flattened.view.selected_chart_id;
        project_tap_timing(views.tap_runtime, session.history.document, views.tap_clock,
                           command_registry, flattened.view);
        publish_tap_assist();
        wormedit::sys::project_recent_files(preferences.program_preferences, flattened.view);
        flattened.view.project_loaded = session.project_identity.session_loaded;
        flattened.view.project_has_native_path = session.project_identity.native_project_loaded;
        flattened.view.pending_new_project_audio_path =
            session.project_identity.pending_import.audio_project_path.c_str();
        flattened.view.pending_bms_encoding_path =
            session.project_identity.pending_import.bms_encoding_path.c_str();
        // Projection returns by value; refresh view pointers after any move.
        flattened.rebind_view_pointers();
        project_bga_dock(views.bga_dock_runtime, session.history, session.workspace,
                         session.project_identity.asset_base, flattened.view.compose);
    }

    void dispatch_confirmed_workspace_actions(
        double viewport_delta_seconds, WormUiWorkspaceAction& action,
        const std::optional<std::uint32_t>& selected_waveform_sample_rate, double viewport_height) {
        dispatch_options_page_action(action.options);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        audio_runtime.resident_loader.set_budget_bytes(
            static_cast<std::size_t>(
                preferences.program_preferences.preferences.audio.keysound_budget_mib) *
            1024ULL * 1024ULL);
#endif
        const auto tap_applied =
            dispatch_tap_panel_action(views.tap_runtime, action.tap_timing, session.history,
                                      views.tap_clock(), commands.audio_offset_edits_locked());
        post_tap_apply_feedback(tap_applied, platform.notification_sink);
        wormedit::sys::apply_timing_settings_action(commands, action.timing_settings);
        publish_tap_assist();
        consume_shortcut_capture(preferences.shortcut_editor, action, command_registry,
                                 preferences.program_preferences, platform.notification_sink);
        const bool shortcut_handled =
            process_compose_frame_action(action, viewport_delta_seconds, tap_applied);
        const bool external_handled =
            dispatch_external_player_action(preferences.external_player_runtime,
                                            action.external_player, commands,
                                            platform.notification_sink) ||
            dispatch_chart_import_action(action);
        const auto previous_selected_chart = session.workspace.selected_chart;
        const bool project_handled =
            dispatch_project_chart_action(action, shortcut_handled || external_handled);
        remember_created_chart_snap(commands, project_handled, action.project_chart.kind,
                                    previous_selected_chart, session.workspace.selected_chart);
        apply_compare_scroll_action(views.compare_scene_cache, action);
        dispatch_workspace_frame_action(action, project_handled, selected_waveform_sample_rate,
                                        viewport_height);
    }

    void dispatch_frame_action(double viewport_delta_seconds, flattened_workspace_t& flattened,
                               WormUiWorkspaceAction& action) {
        {

            wormedit::sys::frame_phase_scope_t actions(
                wormedit::sys::frame_phase_t::workspace_actions);
            std::optional<std::uint32_t> selected_waveform_sample_rate;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            selected_waveform_sample_rate = selected_waveform_rate();
#endif

            if (dispatch_close_confirmation(
                    views.pending_close, action,
                    preferences.program_preferences.preferences.general.confirm_unsaved_close))
                return;
            dispatch_confirmed_workspace_actions(viewport_delta_seconds, action,
                                                 selected_waveform_sample_rate,
                                                 flattened.view.compose.viewport_height_pixels);
        }
    }

    void compose_frame_ui(flattened_workspace_t& flattened, double viewport_delta_seconds) {
        const auto frame_nuklear_previous =
            wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::nuklear);
        refresh_application_command_states(command_registry, session.history, session.workspace,
                                           views.compose_interaction_state, commands);
        wormedit::sys::project_menu_commands(command_registry, flattened.view);
        project_command_menu(command_registry, flattened.view);
#if defined(WORMEDIT_HAS_MACOS_MENU)
        wormedit::sys::refresh_macos_menu_shortcuts(command_registry);
#endif
        auto action = worm_ui_frontend_compose(&flattened.view);
        refresh_sound_selection_context(action, commands);
        dispatch_menu_command(action);
        retain_compose_hover_timing(views.compose_scene_cache, flattened.view, action);
        retain_compose_bgm_window_capture(commands, flattened.view, action);
        views.pending_notification = action.notifications;
        wormedit::sys::frame_timing().restore(frame_nuklear_previous);

        dispatch_frame_action(viewport_delta_seconds, flattened, action);
    }

    void compose_bound_frame(const composed_frame_inputs_t& frame) {
        auto flattened = prepare_bound_frame_view(frame.model);
        wormedit::sys::flattened_workspace_t compare_first_pane(&views.frame_projection_arena);
        views.projection_feedback.publish(
            {session.history.document.id, session.history.document.revision,
             session.workspace.selected_chart.value_or(wormedit::chart_id_t{}),
             views.project_chart_runtime.session_generation},
            flattened.projection_feedback, platform.notification_sink);
        wormedit::sys::flattened_workspace_t compare_second_pane(&views.frame_projection_arena);
        int compare_width = 0;
        int compare_height = 0;
        worm_window_get_size(&platform.lifetime.runtime.window, &compare_width, &compare_height);
        compare_height += 36 - worm_ui_frontend_title_bar_height();
        wormedit::sys::populate_compare_view(
            flattened, compare_first_pane, compare_second_pane, session.history, session.workspace,
            views.compare_scene_cache, commands, compare_width, compare_height);

        complete_frame_view(flattened, frame);
        compose_frame_ui(flattened, frame.viewport_delta_seconds);
    }

    void finish_frame(std::uint64_t frame_start_ns) {
        persist_session_preferences(preferences.program_preferences, commands,
                                    platform.notification_sink);
        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::render);
        wormedit::sys::render_application_frame(platform.lifetime.runtime.window, frame_start_ns);
        wormedit::sys::frame_timing().end_frame();
    }

    void run_frame() {
        if (!started_ || !running)
            return;
        auto tap_event_ns = views.last_tap_event_ns;
        current_tap_event_ = &tap_event_ns;
        struct tap_event_frame_t {
            implementation_t& owner;
            const std::optional<std::uint64_t>& timestamp;
            ~tap_event_frame_t() {
                owner.views.last_tap_event_ns = timestamp;
                owner.current_tap_event_ = nullptr;
            }
        } tap_event_frame{*this, tap_event_ns};

        publish_frame_notice();
        const auto frame_start_ns = SDL_GetTicksNS();
        const auto notification_now = notification_frame_time();
        begin_frame(notification_now);
        poll_frame_events();
        import_frame_drops();
        const auto viewport_delta = poll_frame_jobs();
        wormedit::sys::frame_timing().restore(wormedit::sys::frame_phase_t::count);
        service_frame_audio();
        const auto field_skin_lanes = configure_frame_field_skin();
        workspace_view_result_t model;
        prepare_frame_model(model);
        const auto notification_view = build_frame_notifications(notification_now);
        if (!handle_workspace_view_error(
                model, notification_view,
                preferences.program_preferences.preferences.general.confirm_unsaved_close)) {
            compose_bound_frame({*model.view, notification_view, field_skin_lanes, viewport_delta});
        }
        finish_frame(frame_start_ns);
    }
};

application_t::application_t(application_startup_arguments_t arguments)
    : implementation_(std::make_unique<implementation_t>(running)) {
    implementation_->platform.arguments = std::move(arguments);
}
application_t::~application_t() {
    shutdown();
}
auto application_t::start() -> bool {
    return implementation_->start();
}
void application_t::run_frame() {
    implementation_->run_frame();
}
void application_t::shutdown() noexcept {
    running = false;
    implementation_->shutdown();
}
} // namespace wormedit::sys

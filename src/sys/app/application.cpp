#include "common/window_presentation.h"
#include "sys/compose/generator_runtime.h"
#include "sys/compose/compose_clipboard_selection.h"
#include <wormedit/core/timing_lane_catalog.h>
#include "core/text_util.h"
#include "sys/resources/catalog_view.h"
#include "sys/app/application.h"
#include <wormedit/core/hitsound_editor.h>
#include "sys/app/application_edit_inputs.h"
#include "sys/preferences/external_player_runtime.h"
#include "sys/app/application_timing_stage.h"
#include "sys/audio/audio_offset_actions.h"
#include "sys/audio/audio_slicer_runtime.h"
#include "sys/compose/actions/compose_note_to_bgm.h"
#include "sys/compose/compose_sound_selection.h"
#if defined(WORMEDIT_HAS_MACOS_MENU)
#include "sys/app/macos_menu.h"
#endif
#include "sys/app/frame_timing.h"
#include "sys/app/display_watchdog.h"
#include "sys/app/thread_priority.h"
#include "wormedit/audio.h"
#include "wormedit/window.h"
#include <SDL3/SDL_hints.h>

#include "sys/commands/application_commands.h"
#include "sys/app/application_startup.h"
#include "sys/app/application_lifetime.h"
#include "sys/app/application_startup_feedback.h"
#include "sys/app/frontend_notification_sink.h"
#include "sys/app/application_frame_services.h"
#include "sys/app/application_frame_view.h"
#include "sys/commands/application_command_dispatch.h"
#include "sys/app/application_defaults.h"
#include "sys/audio/audio_project_import.h"
#include "sys/compose/views/compose_resource_presenter.h"
#include "sys/compose/views/compose_view.h"
#include "sys/project/compare_view.h"
#include "sys/compose/compose_timing_target.h"
#include "sys/compose/input/compose_input.h"
#include "sys/compose/actions/compose_actions.h"
#include "sys/compose/views/recipe_form.h"
#include "sys/project/workspace_view.h"
#include "sys/project/workspace_frame_view.h"
#include "sys/audio/transport_player_actions.h"
#include "sys/compose/input/compose_minimap_navigation.h"
#include "sys/timing/timing_view.h"
#include "sys/timing/timing_edit_runtime.h"
#include "sys/compose/actions/compose_sound_actions.h"
#include "sys/resources/sound_import_staging.h"
#include "sys/resources/sound_usage_actions.h"
#include "sys/resources/bga_usage_actions.h"
#include "sys/compose/actions/compose_bga_actions.h"
#include "sys/resources/bga_dock_runtime.h"
#include "sys/metadata/metadata_drop_state.h"
#include "sys/metadata/metadata_asset_runtime.h"
#include "sys/metadata/metadata_actions.h"
#include "sys/metadata/metadata_view.h"
#include "sys/app/platform_recovery.h"
#include "sys/project/recovery_notification_feedback.h"
#include "sys/commands/platform_shortcut_profile.h"
#include "sys/preferences/external_player.h"
#include "sys/preferences/preferences_runtime.h"
#include "sys/preferences/options_runtime.h"
#include "sys/audio/audio_options_runtime.h"
#include "sys/preferences/lane_keys_runtime.h"
#include "sys/commands/shortcut_rows.h"
#include "sys/commands/menu_command_view.h"
#include <set>
#include "sys/notifications/notification_history_runtime.h"
#include <SDL3/SDL_clipboard.h>
#include "sys/project/project_view.h"
#include "sys/project/chart_import_session.h"
#include <wormedit/core/snap.h>
#include <wormedit/core/chart_editor.h>
#include "sys/audio/tap_timing_runtime.h"
#include <SDL3/SDL_filesystem.h>
#include <wormedit/core/virtual_canvas.h>
#include "sys/project/project_open.h"
#include "sys/project/project_import_feedback.h"
#include "sys/project/project_save_paths.h"
#include "sys/audio/player_preview_presenter.h"
#include "sys/audio/waveform_view.h"
#include "sys/audio/bmson_waveform_view.h"
#include "sys/compose/actions/compose_audition_requests.h"
#include "sys/app/crash_backtrace.h"
#include "sys/audio/inline_preview_cursor.h"
#include "sys/audio/audio_source_registration.h"
#include "sys/audio/project_audio_sources.h"
#include "sys/compose/views/compose_waveform_overlays.h"
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
#include <wormedit/core/interaction_dispatch.h>
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
#include <wormedit/core/keyed_cache.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/viewport_input.h>
#include <wormedit/core/virtual_canvas.h>
#include <wormedit/core/workspace.h>
#include <wormedit/formats/bms_clipboard.h>
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
#include "sys/audio/chart_preview_state.h"
#include "sys/compose/compose_playhead_cursor.h"
#include "sys/audio/audio_transport_frame.h"
#include "sys/audio/preview_cursor_frame.h"
#include "sys/audio/waveform_analysis_runtime.h"
#include "sys/audio/sdl_audition_output.h"
#include "sys/audio/sdl_preview_output.h"
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
bool open_bookmark_chooser_backend(void*, std::uint64_t project, std::uint64_t chart,
                                  std::uint64_t revision) {
    return worm_ui_frontend_open_bookmark_chooser(project, chart, revision);
}

using wormedit::sys::begin_timing_stage;
using wormedit::sys::compose_action_outcome_t;
using wormedit::sys::timing_position_from_fraction;
using wormedit::sys::timing_stage_value;



using wormedit::sys::frontend_workspace_view_t;
using wormedit::sys::create_session_project_id;
using wormedit::sys::parse_application_command;
using wormedit::sys::timing_grid_state_t;
using wormedit::sys::timing_scene_cache_t;
using wormedit::sys::runtime::timing_edit_t;
using wormedit::sys::metadata_drop_state_t;
using wormedit::sys::dispatch_compose_view_action;
using wormedit::sys::runtime::resource_copy_t;
using wormedit::sys::application_command_state_t;
using wormedit::sys::chart_snap_preferences;
using wormedit::sys::compose_bgm_lane_width_pixels;
using wormedit::sys::compose_measure_rail_width_pixels;
using wormedit::sys::compose_timing_lane_count;
using wormedit::sys::compose_scene_cache_t;
using wormedit::sys::compare_scene_cache_t;
using wormedit::sys::compose_field_downscroll;
using wormedit::sys::compose_horizontal_layout;
using wormedit::sys::compose_physical_scancode;
using wormedit::sys::compose_project_y;
using wormedit::sys::compose_source_y;
using wormedit::sys::compose_surface_height;
using wormedit::sys::maximum_layout_lane_count;
using wormedit::sys::populate_compose_view;
using wormedit::sys::requested_snap_denominator;
using wormedit::sys::compose_result_status;
using wormedit::sys::dispatch_compose_input_action;
using wormedit::sys::dispatch_compose_navigation_actions;
using wormedit::sys::selected_bmson_waveform_source;
using wormedit::sys::selected_chart;
using wormedit::sys::visible_semantic_layer_by_id;

void post_edit_rejection(wormedit::notification_sink_t& notifications,
                         const wormedit::diagnostic_list_t& diagnostics,
                         std::string_view fallback) {
    if (diagnostics.empty())
        notifications.post(wormedit::notification_severity_t::warning,
                           wormedit::notification_source_t::edit, fallback);
    else
        notifications.post(to_notification_severity(diagnostics.front().severity),
                           wormedit::notification_source_t::edit, diagnostics.front().message);
}

auto post_callback_feedback(wormedit::notification_sink_t& notifications,
                            wormedit::notification_severity_t severity,
                            wormedit::notification_source_t source, std::string message) -> std::string {
    notifications.post_feedback(severity, source, message);
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
                                 std::optional<wormedit::chart_id_t> selected,
                                 const wormedit::project_settings_t& settings) {
    if (handled && kind == WORM_UI_PROJECT_CHART_NEW && selected && selected != previous) {
        remember_snap_denominator(state, *selected, settings.snap_for(state.preferences.editor_preferences.default_snap).value);
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





















#if defined(WORMEDIT_HAS_AUDIO_DECODER)
auto loop_region_for_selection(const wormedit::document_history_t& history,
                               const wormedit::chart_document_t& chart,
                               wormedit::position_span_t selection,
                               std::uint64_t requested_begin_row,
                               std::uint64_t requested_end_row)
    -> std::optional<wormedit::audio::preview_loop_region_t> {
    using namespace wormedit;
    const auto* indices = history.indices(chart.id);
    const auto* measures = indices && indices->measures ? &*indices->measures : nullptr;
    if (!measures) return std::nullopt;
    const auto begin_row = (*measures).row_of(selection.begin);
    const auto end_row = (*measures).row_of(selection.end);
    if (!begin_row || !end_row || *begin_row != requested_begin_row ||
        *end_row != requested_end_row || *begin_row >= *end_row) return std::nullopt;
    const auto built_timing = timing_index_t::from(chart, {.allow_bounded_clock = true});
    if (!built_timing.value) return std::nullopt;
    const auto begin = sys::preview_cursor_frame(chart, *built_timing.value, selection.begin, 0.0, &indices->measures);
    const auto end = sys::preview_cursor_frame(chart, *built_timing.value, selection.end, 0.0, &indices->measures);
    const auto* begin_frame = std::get_if<std::uint64_t>(&begin);
    const auto* end_frame = std::get_if<std::uint64_t>(&end);
    if (!begin_frame || !end_frame || *begin_frame >= *end_frame) return std::nullopt;
    return audio::preview_loop_region_t{*begin_frame, *end_frame};
}














#endif

void refresh_transport_rate_commands(wormedit::command_registry_t& registry,
                                     const application_command_state_t& state) {
    (void)wormedit::update_command_state(registry, {"transport.toggle_metronome"},
                                       state.metronome_settings().available,
                                       state.metronome_settings().enabled);
    const bool available = static_cast<bool>(state.services.transport_set_playback_rate);
    (void)wormedit::update_command_state(registry, {"transport.rate_slower"},
                                       available && state.preferences.transport_rate_index > 0, std::nullopt);
    (void)wormedit::update_command_state(registry, {"transport.rate_faster"},
                                       available && state.preferences.transport_rate_index < wormedit::audio::playback_rate_catalog.size() - 1, std::nullopt);
}

using wormedit::sys::external_player_runtime_t;

void initialize_program_preferences(wormedit::sys::program_preferences_runtime_t& preferences,
    application_command_state_t& state,
    wormedit::notification_sink_t& notifications) {
    worm_ui_frontend_set_dock_layout(preferences.preferences.layout.dock);
    state.preferences.minimap_click_moves_paused_playhead = preferences.preferences.minimap.click_moves_paused_playhead;
    state.preferences.editor_preferences = preferences.preferences.editor;
    state.preferences.default_lane_keys = preferences.preferences.default_lane_keys;
    state.preferences.lane_keys = preferences.preferences.lane_keys;
    for (const auto& warning : preferences.warnings)
        notifications.post(wormedit::notification_severity_t::warning,
                           wormedit::notification_source_t::system, warning);
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
    wormedit::command_registry_t& registry) -> wormedit::shortcut_profile_startup_outcome_t {
    if (preferences.path.empty())
        return {.value = wormedit::failure_code_t::unavailable,
                .diagnostics = {}};
    return wormedit::apply_startup_shortcut_profile(shortcut_profile_path(preferences), registry);
}

// Input & shortcuts editing: the projection feeds the Options page, and `capture` holds
// the command waiting for its next key press.
struct shortcut_editor_runtime_t {
    wormedit::sys::shortcut_rows_projection_t rows;
    std::optional<wormedit::command_id_t> capture;
};

// Fills the page rows only while Input & shortcuts is the visible category.
void attach_shortcut_rows(wormedit::ui::options_view_t& view, const wormedit::sys::options_runtime_t& options,
    shortcut_editor_runtime_t& editor, const wormedit::command_registry_t& registry) {
    if (!options.visible || options.category != WORM_UI_OPTIONS_INPUT) {
        editor.capture.reset();
        return;
    }
    editor.rows.refill(registry, application_primary_modifier());
    view.shortcut_rows.assign(editor.rows.rows().begin(), editor.rows.rows().end());
    const auto capturing = editor.capture ? std::ranges::find(editor.rows.commands(), *editor.capture)
                                          : editor.rows.commands().end();
    view.shortcut_capturing = capturing != editor.rows.commands().end();
    view.shortcut_capture_command_id = view.shortcut_capturing
        ? std::string_view{capturing->value} : std::string_view{};
    if (!view.shortcut_capturing) editor.capture.reset();
}

// Returns true when the action belonged to the shortcut editor and was consumed here.
auto handle_shortcut_editor_action(shortcut_editor_runtime_t& editor, const wormedit::ui::options_action_t& action,
    const wormedit::sys::options_runtime_t& options, wormedit::notification_sink_t& notifications) -> bool {
    if (action.kind != WORM_UI_OPTIONS_SHORTCUT_CAPTURE && action.kind != WORM_UI_OPTIONS_SHORTCUT_CANCEL)
        return false;
    if (action.revision != options.revision) return true;
    if (action.kind == WORM_UI_OPTIONS_SHORTCUT_CANCEL) {
        editor.capture.reset();
        return true;
    }
    const auto command = std::ranges::find(editor.rows.commands(), action.shortcut_command_id,
                                           &wormedit::command_id_t::value);
    if (command == editor.rows.commands().end()) {
        notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::input,
                           "That shortcut row is no longer available.");
        return true;
    }
    editor.capture = *command;
    return true;
}

// Remaps and persists one captured chord. The in-memory mapping stays even when the save fails.
void remap_and_save_shortcut(wormedit::command_registry_t& registry, const wormedit::command_id_t& id,
    const wormedit::shortcut_chord_t& chord, const wormedit::sys::program_preferences_runtime_t& preferences,
    wormedit::notification_sink_t& notifications) {
    const auto remapped = wormedit::remap_command(registry, id, {chord});
    if (!remapped.value) {
        notifications.post(wormedit::notification_severity_t::error, wormedit::notification_source_t::input,
                           remapped.diagnostics.empty() ? "Shortcut could not be changed."
                                                        : remapped.diagnostics.front().message);
        return;
    }
    const auto saved = wormedit::create_shortcut_profile_file(shortcut_profile_path(preferences),
                                                              wormedit::shortcut_profile_t::from(registry, "user"));
    if (saved)
        notifications.post(wormedit::notification_severity_t::success, wormedit::notification_source_t::input,
                           "Shortcut saved: " + id.value);
    else
        notifications.post(wormedit::notification_severity_t::error, wormedit::notification_source_t::system,
                           "Shortcut kept for this session, but the profile was not saved: " + saved.error().message);
    if (!remapped.diagnostics.empty())
        notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::input,
                           "Shortcut conflict: " + remapped.diagnostics.front().message);
}

// Consumes the frame's key batch while a capture is waiting, so no command runs from it.
void consume_shortcut_capture(shortcut_editor_runtime_t& editor, wormedit::ui::workspace_action_t& action,
    wormedit::command_registry_t& registry, const wormedit::sys::program_preferences_runtime_t& preferences,
    wormedit::notification_sink_t& notifications) {
    if (!editor.capture) return;
    const auto captured = wormedit::sys::capture_shortcut_batch(action.shortcut_events);
    if (!captured.cancel && !captured.chord) return;
    action.shortcut_events.resize(0);
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
    if (!worm_ui_frontend_dock_layout_is_editing()) candidate.layout.dock = worm_ui_frontend_dock_layout();
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
    const auto chart = document.read_chart(*workspace.selected_chart);
    if (chart == nullptr || chart->lane_layouts.empty()) return nullptr;
    if (interaction && interaction->chart == chart->id) {
        if (const auto* segment = wormedit::active_lane_layout_segment(*chart, interaction->cursor))
            return &segment->layout;
    }
    return &chart->lane_layouts.front().layout;
}

auto configure_field_skin(const wormedit::lane_layout_t* layout,
    wormedit::sys::options_runtime_t& options, const wormedit::display_preferences_t& saved,
    application_command_state_t& state) -> std::vector<wormedit::ui::compose_lane_view_t> {
    const auto key = layout ? wormedit::field_skin_layout_key(*layout) : std::string{};
    wormedit::sys::set_options_field_layout(options, key, layout ? layout->display_name : "");
    const auto& display = options.visible ? options.draft.display : saved;
    std::vector<wormedit::ui::compose_lane_view_t> lanes;
    if (layout) {
        lanes.reserve(layout->lanes.size());
        for (const auto& lane : layout->lanes)
            lanes.push_back({.lane_id = lane.id, .display_order = lane.display_order,
                .role = lane.role, .stage = lane.stage,
                .glyph = lane.glyph, .key_color = lane.key_color});
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
        ? interaction->virtual_bms_cursor_row : (canvas->authored_index).row_of(interaction->cursor);
    const auto measure = row ? canvas->measure_containing(*row) : std::nullopt;
    return measure ? std::optional<std::uint64_t>{measure->number} : std::nullopt;
}



auto dispatch_external_player_action(wormedit::sys::options_runtime_t& options,
    const wormedit::ui::external_player_action_t& action, const application_command_state_t& state,
    const wormedit::program_preferences_t& preferences) -> bool {
    using wormedit::sys::external_player_action_t;
    if (action.kind == wormedit::ui::external_player_action_kind_t::none) return false;
    if (action.kind == wormedit::ui::external_player_action_kind_t::open)
        wormedit::sys::open_options_category(options, preferences, WORM_UI_OPTIONS_PLAYERS);
    else if (action.kind == wormedit::ui::external_player_action_kind_t::play_start && state.services.external_player)
        (void)state.services.external_player(external_player_action_t::play_start);
    else if (action.kind == wormedit::ui::external_player_action_kind_t::play_here && state.services.external_player)
        (void)state.services.external_player(external_player_action_t::play_here);
    else if (action.kind == wormedit::ui::external_player_action_kind_t::stop && state.services.external_player)
        (void)state.services.external_player(external_player_action_t::stop);
    else return false;
    return true;
}

auto open_project_chart(wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& interaction, wormedit::chart_id_t chart)
    -> std::string {
    const auto selected = wormedit::apply_workspace_action(
        workspace, history.document, wormedit::workspace_select_chart_action_t{chart});
    if (!(selected.value && selected.diagnostics.empty())) return selected.diagnostics.empty() ? "Chart selection failed." : selected.diagnostics.front().message;
    const auto opened = wormedit::apply_workspace_action(
        workspace, history.document,
        wormedit::workspace_switch_tab_action_t{wormedit::workspace_tab_t::compose});
    if (!(opened.value && opened.diagnostics.empty())) return opened.diagnostics.empty() ? "Compose could not be opened." : opened.diagnostics.front().message;
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
    const auto hitsounds_ready = compose_ready && !pending &&
        compose_state->has_hitsound_palette(chart->source_family);
    for (const auto& entry : hitsound_commands())
        (void)update_command_state(registry, command_id_t{std::string{entry.id}}, hitsounds_ready, std::nullopt);
    (void)update_command_state(registry, command_id_t{"hitsound.toggle_palette"},
        compose_ready && !pending && chart->source_family != format_family_t::osu_mania &&
            chart->source_family != format_family_t::quaver,
        compose_ready && compose_state->has_hitsound_palette(chart->source_family));
    const auto tools_ready = compose_ready;
    const auto layer_ready = tools_ready &&
        layer_tool_target_editable(chart, application.project.semantic_layer.semantic_layer_target);
    (void)update_command_state(registry, command_id_t{"edit.tool_select"}, compose_ready,
                                compose_ready && !paint_active && !layer_active && !application.project.compose_gesture.resource_paint_mode_active);
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
            compose_ready && is_note_kind_allowed(*chart, kind) && !pending,
            compose_ready && paint_active && compose_state->armed_note_kind == kind);
}

auto play_stop_available(const wormedit::chart_document_t* chart,
    const application_command_state_t& state) -> bool {
    return chart && state.services.transport_preview_playing && state.services.transport_play_pause &&
           state.services.transport_stop;
}

auto paste_target_available(bool compose_ready, bool pending,
                            const application_command_state_t& state) -> bool {
    return compose_ready && !pending &&
           (state.project.clipboard.chart_clipboard.has_value() ||
            state.system_clipboard_has_bms_editor_text);
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

void attach_bgm_shortcut(wormedit::ui::compose_view_t& view, const wormedit::command_registry_t& registry) {
    const auto* command = wormedit::find_command(registry, {"edit.move_to_bgm"});
    const auto shortcut = command && !command->current_shortcuts.empty()
        ? wormedit::sys::shortcut_chord_text(command->current_shortcuts.front()) : std::string{};
    view.move_to_bgm_shortcut = shortcut;
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
    const bool enabled = ready && !pending && !application.project.clipboard.paste_session;
    const bool checked = enabled && application.project.quantize.quantize_editor &&
        application.project.quantize.quantize_editor->stamp.is_identity_current(history.document, state->chart) && application.project.quantize.quantize_editor->preview_enabled;
    (void)wormedit::update_command_state(registry, {"edit.toggle_quantize_preview"}, enabled, checked);
    const bool selected = enabled && (!state->selected_notes.empty() || !state->selected_objects.empty());
    (void)wormedit::update_command_state(registry, {"edit.apply_quantize"}, selected, std::nullopt);
}

void refresh_compose_dock_commands(wormedit::command_registry_t& registry, bool compose_ready,
                                    bool pending, const application_command_state_t& application) {
    const bool enabled = compose_ready && !pending && !application.project.clipboard.paste_session;
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
        (!compose || !compose->is_pending());
}

void refresh_resource_paint_command(wormedit::command_registry_t& registry,
    const wormedit::document_history_t& history,
    const std::optional<wormedit::editor_interaction_state_t>& state,
    const application_command_state_t& application, bool compose_ready) {
    (void)wormedit::update_command_state(registry, {"audio.paint_hitsound"},
        compose_ready && application.project.compose_gesture.can_paint(history.document, *state),
        application.project.compose_gesture.resource_paint_mode_active);
}

auto refresh_semantic_layer_target(const wormedit::chart_document_t* chart,
    application_command_state_t& application_state) -> bool {
    using namespace wormedit;
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
    return target_valid;
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
    const bool pending = compose_ready && compose_state->is_pending();
    const bool target_valid = refresh_semantic_layer_target(chart, application_state);
    const auto set = [&](std::string id, bool enabled, std::optional<bool> checked = std::nullopt) {
        (void)update_command_state(registry, command_id_t{std::move(id)}, enabled, checked);
    };
    set("document.new_project", true);
    set("document.open", true);
    set("document.save_as", !history.document.read_charts().empty());
    set("document.close", !history.document.read_charts().empty());
    set("app.quit", true);
    set("workspace.lane_keys", wormedit::sys::lane_keys_runtime_t::can_open(chart));
    set("document.new_chart", chart != nullptr);
    set("history.undo", history.can_undo());
    set("history.redo", history.can_redo());
    set("edit.delete", compose_ready);
    const bool can_move_bgm = bgm_move_enabled(chart, compose_state, compose_ready, pending, application_state);
    set("edit.move_to_bgm", can_move_bgm);
    set("edit.move_next_to_bgm", can_move_bgm);
    const bool clipboard_selection = compose_ready &&
        can_copy_compose_selection(*compose_state, application_state);
    set("edit.copy", clipboard_selection);
    set("edit.cut", clipboard_selection);
    set("edit.paste", paste_target_available(compose_ready, pending, application_state));
    refresh_note_kind_commands(registry, chart, compose_state, compose_ready, pending,
                                application_state);

    set("selection.clear", compose_ready);
    refresh_same_sound_command(registry, history, chart, compose_state, compose_ready, application_state);
    set("selection.select_all", compose_ready);
    set("selection.select_all_objects", compose_ready);
    set("selection.select_all_after_cursor", compose_ready);
    set("selection.select_all_objects_after_cursor", compose_ready);
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
                             ? semantic_layer_palette_window_t::from(
                                   *chart, application_state.project.semantic_layer.semantic_layer_target,
                                   application_state.project.semantic_layer.semantic_layer_palette_chart == chart->id
                                       ? application_state.project.semantic_layer.semantic_layer_palette_page
                                       : 0,
                                   false, wormedit::ui::semantic_layer_shortcut_limit)
                             : std::nullopt;
    for (std::size_t index = 0; index < wormedit::ui::semantic_layer_shortcut_limit; ++index) {
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
    refresh_resource_paint_command(registry, history, compose_state, application_state, compose_ready);
    set("audio.toggle_click_audition", true, application_state.preferences.editor_preferences.audition_on_click);
    set("audio.toggle_playback_audition", true, application_state.preferences.editor_preferences.audition_on_playback);
    wormedit::sys::apply_timing_focus_command_policy(
        registry, !application_state.project.compose_view.compose_timing_focus);
}

auto application_command_focus(const wormedit::ui::workspace_action_t& action,
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
        action.shortcut_compose_lane_focused && action.shortcut_focused_lane_id.value != 0) {
        focus.focused_lane = action.shortcut_focused_lane_id;
    }
    return focus;
}


void post_shortcut_rejection(wormedit::notification_sink_t& notifications,
                            const wormedit::command_registry_t& registry,
                            const wormedit::command_dispatch_outcome_t& result) {
    // A controller posts its own rejection once. Registry-level failures have
    // no controller, so the input boundary owns their notification.
    const auto* command = result.value.command ? wormedit::find_command(registry, *result.value.command) : nullptr;
    if (result.value.status == wormedit::command_dispatch_status_t::rejected && command &&
        command->definition.invoke)
        return;
    if (result.diagnostics.empty())
        notifications.post(wormedit::notification_severity_t::warning,
                           wormedit::notification_source_t::input, "Shortcut command was rejected.");
    else
        notifications.post(to_notification_severity(result.diagnostics.front().severity),
                           wormedit::notification_source_t::input,
                           result.diagnostics.front().message);
}

void consume_menu_shortcut(wormedit::ui::workspace_action_t& action,
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
    for (std::size_t i = 0; i < action.shortcut_events.size(); ++i) {
        const auto& event = action.shortcut_events[i];
        if (event.hid_usage == static_cast<wormedit::shortcut_key_t>(static_cast<std::uint16_t>(chord.key)) && event.modifiers == modifiers) continue;
        action.shortcut_events[retained++] = event;
    }
    action.shortcut_events.resize(retained);
}

void build_command_menu_view(const wormedit::command_registry_t& registry,
                             wormedit::sys::command_shortcut_text_t& shortcuts,
                             wormedit::ui::workspace_view_t& view) {
    shortcuts.refresh(registry, application_primary_modifier());
    view.command_menu_rows.resize(registry.commands.size());
    for (std::size_t i = 0; i < view.command_menu_rows.size(); ++i) {
        const auto& command = registry.commands[i];
        view.command_menu_rows[i] = {command.definition.id.value.c_str(),
                                     command.definition.display_name.c_str(),
                                     shortcuts.text(i),
                                     command.definition.enabled};
    }
}

auto command_menu_invoker(wormedit::command_invoker_t invoke) -> wormedit::command_invoker_t {
    return [invoke = std::move(invoke)](const wormedit::command_invocation_context_t& invocation) {
        if (invocation.command.value == "workspace.command_menu")
            return wormedit::dispatch_outcome_t{.value = worm_ui_frontend_open_command_menu()};
        return invoke(invocation);
    };
}








void delete_staged_timing_event(const wormedit::ui::workspace_action_t& action,
                                wormedit::document_history_t& history, bool timing_locked,
                                wormedit::timing_lane_pseudonote_outcome_t& stage,
                                wormedit::notification_sink_t& notifications) {
    const auto* kind = wormedit::find_catalog_entry(wormedit::timing_lanes, action.timing_lane);
    if (!stage.report()->existing_event || !kind || kind->kind != stage.report()->kind) {
        post_edit_rejection(notifications, {}, "The selected timing event cannot be deleted.");
    } else {
        const auto result = wormedit::delete_existing_timing_lane_event(
            history, stage.captured_stamp().chart, kind->kind, *stage.report()->existing_event, timing_locked, stage.report()->anchoring);
        if (result)
            notifications.post(wormedit::notification_severity_t::success,
                               wormedit::notification_source_t::edit, "Timing event deleted.");
        else
            post_edit_rejection(notifications, result.diagnostics, "Timing event deletion was rejected.");
    }
    wormedit::cancel_proposal(stage);
}





// Timing focus: begin a new event with its default value and confirm it at once.
void quick_insert_timing_event(const wormedit::ui::workspace_action_t& action,
                               wormedit::document_history_t& history,
                               const wormedit::workspace_controller_t& workspace,
                               bool timing_locked, std::uint64_t active_snap_denominator,
                               wormedit::notification_sink_t& notifications) {
    auto candidate = begin_timing_stage(action, history, workspace, timing_locked,
                                        active_snap_denominator);
    if (!candidate.value) {
        post_edit_rejection(notifications, candidate.diagnostics, "Timing event could not be inserted at that row.");
        return;
    }
    const auto result = wormedit::apply_proposal(history, candidate, timing_locked);
    if (result)
        notifications.post(wormedit::notification_severity_t::success,
                           wormedit::notification_source_t::edit, "Timing event inserted.");
    else
        post_edit_rejection(notifications, result.diagnostics, "Timing event insertion was rejected.");
}



auto post_workspace_selection_feedback(wormedit::notification_sink_t& notifications,
    const wormedit::dispatch_outcome_t& result, std::string accepted, std::string rejected) -> std::string {
    if ((result.value && result.diagnostics.empty())) return post_callback_feedback(notifications, wormedit::notification_severity_t::hint,
                                             wormedit::notification_source_t::input, std::move(accepted));
    if (!result.diagnostics.empty())
        return post_callback_feedback(
            notifications, to_notification_severity(result.diagnostics.front().severity),
            wormedit::notification_source_t::input, result.diagnostics.front().message);
    return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
                                  wormedit::notification_source_t::input, std::move(rejected));
}

auto dispatch_comparison_selection(const wormedit::ui::workspace_action_t& action,
                                   const wormedit::document_history_t& history,
                                   wormedit::workspace_controller_t& workspace,
                                   wormedit::notification_sink_t& notifications) -> std::string {
    const auto selected = action.compare_chart_id;
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

auto dispatch_sound_resource_selection(const wormedit::ui::workspace_action_t& action,
    const wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    wormedit::notification_sink_t& notifications)
    -> std::string {
    auto asset = action.audio_asset_id;
    if (action.audio_resource_id.value != 0) {
        const auto resource = std::ranges::find(history.document.resources,
            action.audio_resource_id, &wormedit::resource_entry_t::id);
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

auto dispatch_workspace_selection_action(const wormedit::ui::workspace_action_t& action,
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
                .asset = action.visual_asset_id,
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
        const bool pending_is_stale = state.is_stale(history.document.revision);
        const bool compose_inactive =
            workspace.active_tab != wormedit::workspace_tab_t::compose;
        if (state.is_pending() &&
            (selected_chart_changed || pending_is_stale || compose_inactive)) {
            (void)wormedit::dispatch_editor_action(history, *compose_interaction_state,
                                                   wormedit::cancel_editor_gesture_action_t{});
        }
        if (selected_chart_changed) {
            compose_interaction_state.reset();
        }
    }
}



void refresh_sound_selection_context(const wormedit::ui::workspace_action_t& action,
    application_command_state_t& sound_context) {
    sound_context.project.sound_selection.sound_selection_chart = action.compose_input_chart_id;
    sound_context.project.sound_selection.sound_selection_revision = action.compose_input_document_revision;
    sound_context.project.sound_selection.sound_selection_hover = action.compose_sound_hover_object_id.value
        ? std::optional{action.compose_sound_hover_object_id} : std::nullopt;
    wormedit::ui::keysound_dock_request_t sound_dock{};
    worm_ui_frontend_keysound_dock_request(&sound_dock);
    sound_context.project.sound_selection.sound_selection_dock.clear();
    for (size_t i = 0; i < sound_dock.selected_resource_ids.size(); ++i)
        sound_context.project.sound_selection.sound_selection_dock.push_back(sound_dock.selected_resource_ids[i]);
}

auto has_fresh_shortcut_key(const wormedit::ui::workspace_action_t& action) -> bool {
    const auto count = std::min(action.shortcut_events.size(), std::size(action.shortcut_events));
    for (std::size_t index = 0; index < count; ++index)
        if (!action.shortcut_events[index].repeat) return true;
    return false;
}





auto read_system_clipboard_text() -> std::string {
    if (!SDL_HasClipboardText()) return {};
    char* text = SDL_GetClipboardText();
    std::string copied = text ? text : "";
    SDL_free(text);
    return copied;
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

#endif


void install_startup_recovery_scan(
    wormedit::native_project_recovery_controller_t& controller,
    wormedit::native_project_startup_result_t& startup) {
    if (startup.recovery_scan_configured)
        (void)controller.replace_scan(std::move(startup.recovery_scan));
}

void post_initial_application_feedback(wormedit::notification_sink_t& notifications,
    bool loaded_primary, const std::filesystem::path& requested_primary_path,
    const wormedit::result_t<std::filesystem::path, std::string>& recovery) {
    notifications.post(wormedit::sys::make_initial_application_feedback(loaded_primary, requested_primary_path, recovery));
}

void post_shortcut_startup_diagnostic(
    wormedit::notification_sink_t& sink, const wormedit::shortcut_profile_startup_outcome_t& result) {
    if (!result.diagnostics.empty())
        sink.post(to_notification_severity(result.diagnostics.front().severity),
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
    std::string layout_id;
    wormedit::text_encoding_t encoding_id{};
    std::string bms_lane_profile_id;
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






















void build_bga_dock_view(wormedit::sys::bga_dock_runtime_t& runtime,
    const wormedit::document_history_t& history, const wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& base, wormedit::ui::compose_view_t& view, std::uint64_t session_generation) {
    wormedit::ui::bga_dock_request_t request{};
    worm_ui_frontend_bga_dock_request(&request);
    request.visible = request.visible && workspace.active_tab == wormedit::workspace_tab_t::compose;
    const auto chart = workspace.selected_chart
        ? history.read_chart(*workspace.selected_chart) : nullptr;
    request.ids_available = chart != nullptr &&
        chart->source_family == wormedit::format_family_t::bms;
    runtime.project(history.document, base, request, view, session_generation, chart);
}

void apply_compare_scroll_action(compare_scene_cache_t& cache,
                                 const wormedit::ui::workspace_action_t& action) {
    if (action.kind != WORM_UI_WORKSPACE_ACTION_COMPARE_SCROLL ||
        cache.pixels_per_measure <= 0.0) return;
    cache.shared_scroll_measures = std::max(
        0.0, cache.shared_scroll_measures -
                 action.compare_scroll_delta / cache.pixels_per_measure);
}

void import_dropped_resources(bool session_loaded, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::vector<wormedit::sys::sound_drop_batch_t>& batches, wormedit::notification_sink_t& notifications) {
    if (batches.empty()) return;
    wormedit::ui::keysound_dock_request_t dock{};
    worm_ui_frontend_keysound_dock_request(&dock);
    for (const auto& batch : batches) {
        if (!batch.is_success()) {
            notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_, batch.reason);
            continue;
        }
        const auto& origin = batch.origin;
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
        else
            static_cast<void>(wormedit::sys::finish_compose_action(notifications,
                wormedit::sys::stage_dropped_sound_import(history.document, batch, history.read_chart(wormedit::chart_id_t{batch.origin.chart_id}))));
    }
    batches.clear();
}

auto dispatch_reviewed_sound_slot_batch(const wormedit::ui::workspace_action_t& action,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    wormedit::sys::application_command_state_t& application,
    const std::optional<wormedit::editor_interaction_state_t>& interaction,
    wormedit::notification_sink_t& notifications) -> bool {
    const bool request = action.compose_action == WORM_UI_COMPOSE_ACTION_STAGE_SOUND_SLOT_BATCH;
    const bool confirm = action.compose_action == WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_SLOT_BATCH;
    if (!request && !confirm) return false;
    std::string feedback;
    wormedit::ui::keysound_dock_request_t dock{};
    worm_ui_frontend_keysound_dock_request(&dock);
    const auto chart = action.compose_input_chart_id;
    if (!dock.slots_mode || action.sound_import_project_id != history.document.id ||
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
        for (std::size_t i = 0; !action.compose_import_paths.empty() && i < action.compose_import_paths.size(); ++i)
            paths.emplace_back(action.compose_import_paths[i]);
        feedback = wormedit::sys::finish_compose_action(notifications,
            wormedit::sys::stage_sound_slot_batch(history.document, chart, action.compose_sound_slot, paths, history.read_chart(chart))).message;
        return true;
    }
    if (interaction && interaction->is_pending()) {
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

auto dispatch_reviewed_sound_import(const wormedit::ui::workspace_action_t& action,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    wormedit::sys::application_command_state_t& application,
    const std::optional<wormedit::editor_interaction_state_t>& interaction,
    wormedit::notification_sink_t& notifications) -> bool {
    const auto request = action.compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_SOUND_FILES;
    const auto confirm = action.compose_action == WORM_UI_COMPOSE_ACTION_CONFIRM_SOUND_IMPORT;
    if (!request && !confirm) return false;
    std::string feedback;
    const auto chart = action.compose_input_chart_id;
    if ((action.sound_import_project_id.value && action.sound_import_project_id != history.document.id) ||
        !workspace.selected_chart || chart != *workspace.selected_chart ||
        action.compose_input_document_revision != history.document.revision) {
        feedback = "The import target changed. Cancel and choose the files again.";
        notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_, feedback);
        if (confirm) worm_ui_frontend_sound_import_result(false, feedback.c_str());
        return true;
    }
    if (request) {
        std::vector<std::string> paths;
        for (std::size_t index = 0; !action.compose_import_paths.empty() &&
                                   index < action.compose_import_paths.size(); ++index)
            if (!action.compose_import_paths[index].empty()) paths.emplace_back(action.compose_import_paths[index]);
        feedback = wormedit::sys::finish_compose_action(notifications,
            wormedit::sys::stage_sound_import(history.document, workspace, paths, action.sound_import_kind,
                workspace.selected_chart ? history.read_chart(*workspace.selected_chart) : nullptr)).message;
        return true;
    }
    if (interaction && interaction->is_pending()) {
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
    const auto& measures = history.indices(chart.id)->measures;
    const auto* index = measures ? &*measures : nullptr;
    const auto row = index ? (*index).row_of(position) : std::nullopt;
    const wormedit::lane_layout_segment_t* active{};
    std::uint64_t active_row{};
    if (row) {
        for (const auto& segment : chart.lane_layouts) {
            const auto start = (*index).row_of(segment.position);
            if (start && *start <= *row && (!active || *start >= active_row)) {
                active = &segment;
                active_row = *start;
            }
        }
    }
    if (!active || active->layout.lanes.empty()) return std::nullopt;
    const auto setup = wormedit::editor_interaction_state_t::from(history, chart.id,
        active->layout.lanes.front().id, position,
        previous ? std::span<const std::uint64_t>{previous->supported_snap_denominators}
                 : std::span<const std::uint64_t>{wormedit::default_snap_denominators},
        previous ? previous->active_snap_denominator : 16,
        previous ? previous->viewport_height_pixels : 1.0);
    return setup.state;
}

void dispatch_bga_usage_jump(const wormedit::ui::workspace_action_t& action,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& interaction,
    wormedit::notification_sink_t& notifications) {
    wormedit::ui::bga_dock_request_t request{};
    worm_ui_frontend_bga_dock_request(&request);
    const auto error = wormedit::sys::dispatch_bga_usage_action(
        action, request, history, workspace, interaction);
    post_callback_feedback(notifications,
        error ? wormedit::notification_severity_t::warning : wormedit::notification_severity_t::hint,
        wormedit::notification_source_t::input, error ? *error : "Moved to BGA usage.");
}

auto dispatch_sound_usage_jump(const wormedit::ui::workspace_action_t& action,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& interaction,
    wormedit::notification_sink_t& notifications) -> std::string {
    wormedit::ui::sound_usage_request_t request{};
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
    const wormedit::ui::workspace_action_t& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& interaction,
    wormedit::notification_sink_t& notifications) -> std::string {
    const auto chart_id = action.compare_diagnostic_chart;
    const auto chart = history.read_chart(chart_id);
    if (chart == nullptr) return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
        wormedit::notification_source_t::input, "The validation target is no longer available.");
    const auto measure = chart->find_measure(action.compare_diagnostic_measure);
    if (measure == nullptr) return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
        wormedit::notification_source_t::input, "The validation target is no longer available.");
    const auto capacity = wormedit::measure_capacity_t::from(chart->divisions_per_standard_measure,
                                                      measure->definition);
    if (!capacity || action.compare_diagnostic_row >= capacity->row_capacity)
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
        return post_callback_feedback(
            notifications,
            jumped.diagnostics.empty()
                ? wormedit::notification_severity_t::warning
                : to_notification_severity(jumped.diagnostics.front().severity),
            wormedit::notification_source_t::input,
            jumped.diagnostics.empty() ? "Could not move to validation diagnostic."
                                       : jumped.diagnostics.front().message);
    auto next_workspace = workspace;
    auto opened = wormedit::apply_workspace_action(next_workspace, history.document,
                                          wormedit::workspace_select_chart_action_t{chart_id});
    if (opened.value && opened.diagnostics.empty())
        opened = wormedit::apply_workspace_action(
            next_workspace, history.document,
            wormedit::workspace_switch_tab_action_t{wormedit::workspace_tab_t::compose});
    if (!opened.value || !opened.diagnostics.empty())
        return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
        wormedit::notification_source_t::input, "The validation target cannot be opened in Compose.");
    workspace = std::move(next_workspace);
    interaction = std::move(candidate);
    return post_callback_feedback(notifications, wormedit::notification_severity_t::hint,
        wormedit::notification_source_t::input, "Moved to validation diagnostic.");
}



auto dispatch_audio_sound_audition(const wormedit::ui::workspace_action_t& action,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    wormedit::sys::application_command_state_t& application,
    wormedit::notification_sink_t& notifications) -> bool {
    if (workspace.active_tab != wormedit::workspace_tab_t::audio_slices ||
        action.compose_action != WORM_UI_COMPOSE_ACTION_AUDITION_SOUND) return false;
    if (!workspace.selected_chart || action.compose_input_project_id.value != history.document.id.value ||
        action.compose_input_chart_id.value != workspace.selected_chart->value ||
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

auto dispatch_reviewed_sound_edit(const wormedit::ui::workspace_action_t& action,
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

void build_application_notifications_view(wormedit::ui::notification_view_t& view,
    const wormedit::notification_center_t& center,
    wormedit::notification_time_t now,
    const std::optional<wormedit::editor_interaction_state_t>& interaction,
    wormedit::sys::notification_history_runtime_t& runtime,
    const wormedit::song_document_t& document, std::uint64_t document_generation) {
    view.refill(center, now);
    view.history_available = true;
    view.history.refill(runtime, center, document, now, document_generation);
    view.edit_gesture_active = interaction && interaction->is_pending();
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
    const auto charts = document.read_charts();
    for (std::size_t index = 0; index < std::min<std::size_t>(charts.size(), 2); ++index) {
        const auto& chart = *charts[index];
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
    -> wormedit::ui::close_confirmation_view_t {
    return {.visible = pending.target != close_target_t::none && !pending.awaiting_encoding,
        .application = pending.target == close_target_t::application, .has_native_path = has_native_path,
        .project_id = pending.project, .revision = pending.revision, .status_text = pending.save_failure};
}



auto retained_compose_geometry(const compose_scene_cache_t& cache)
    -> wormedit::editor_measure_geometry_t {
    return {cache.stamp().chart, cache.stamp().document.revision,
            cache.scene.cached_measure_index ? &*cache.scene.cached_measure_index : nullptr};
}



auto dispatch_tap_panel_action(wormedit::sys::tap_timing_runtime_t& runtime,
                               const wormedit::ui::tap_timing_action_t& action,
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

auto tap_music_available(const wormedit::song_document_t& document, const wormedit::ui::workspace_view_t& view) -> bool {
    if (view.tap_timing.source_available || view.metadata.selected_music_asset_id) return true;
    return std::ranges::any_of(document.resources, [&](const wormedit::resource_entry_t& resource) {
        return std::ranges::find(resource.roles, wormedit::resource_role_t::music) != resource.roles.end() &&
            document.find_audio_asset(resource.asset) != nullptr;
    });
}

void build_tap_timing_view(wormedit::sys::tap_timing_runtime_t& runtime,
                        const wormedit::song_document_t& document,
                        const std::function<wormedit::sys::tap_timing_clock_t()>& read_clock,
                        wormedit::command_registry_t& registry, wormedit::ui::workspace_view_t& view) {
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
        target.proposed_bpm = wormedit::to_double(proposal->payload.beats_per_minute);
        target.proposed_offset_ms = 1000.0 * proposal->payload.beat_zero_audio_offset_seconds.numerator /
                                   proposal->payload.beat_zero_audio_offset_seconds.denominator;
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

struct tap_meter_cache_key_t {
    wormedit::document_stamp_t document;
    wormedit::chart_id_t chart;
    std::uint64_t fingerprint{};
    auto operator==(const tap_meter_cache_key_t&) const -> bool = default;
};

auto build_tap_meter_entries(const wormedit::chart_document_t& chart,
                             std::vector<std::pair<std::uint64_t, std::uint32_t>>& entries) -> void {
    entries.clear();
    std::uint64_t begin{};
    std::uint32_t beats = 4;
    for (const auto& measure : chart.measures) {
        if (measure.definition.meter) beats = measure.definition.meter->numerator;
        entries.emplace_back(begin, beats);
        const auto validation = wormedit::measure_capacity_t::from(
            chart.divisions_per_standard_measure, measure.definition);
        if (!validation || validation->row_capacity > UINT64_MAX - begin) break;
        begin += validation->row_capacity;
    }
}

auto tap_meter_at(const wormedit::song_document_t& document,
                  const wormedit::sys::runtime::chart_preview_t& preview,
                  std::uint64_t frame) -> std::uint16_t {
    if (!preview.chart() || !preview.compiled.value().timing ||
        frame > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return 4;
    const auto chart = document.read_chart(*preview.chart());
    if (chart == nullptr) return 4;
    const auto position = preview.compiled.value().timing->query_position({static_cast<std::int64_t>(frame), 44'100});
    if (!position) return 4;
    const auto row = position->value.numerator / static_cast<std::int64_t>(position->value.denominator);
    static wormedit::keyed_cache_t<tap_meter_cache_key_t,
        std::vector<std::pair<std::uint64_t, std::uint32_t>>> cache;
    const tap_meter_cache_key_t key{.document = wormedit::document_stamp_t::from(document),
                                   .chart = *preview.chart(),
                                   .fingerprint = preview.compiled.value().timing->source_fingerprint};
    const auto& entries = cache.get_or_build(key,
        std::bind_front(build_tap_meter_entries, std::cref(*chart)));
    const auto found = std::upper_bound(entries.begin(), entries.end(),
        row < 0 ? 0 : static_cast<std::uint64_t>(row),
        [](std::uint64_t target, const std::pair<std::uint64_t, std::uint32_t>& entry) { return target < entry.first; });
    const auto beats = found == entries.begin() ? 4 : std::prev(found)->second;
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

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
auto preview_music_frame(const wormedit::preview_schedule_t& schedule, std::uint64_t frame)
    -> std::optional<std::uint64_t> {
    for (const auto& music : schedule.music_events())
        if (const auto source = wormedit::preview_music_source_frame_at(music, frame)) return source;
    return std::nullopt;
}
#endif

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
        clock.chart = context.preview.chart() ? context.preview.chart()->value : 0;
        clock.revision = context.preview.revision();
        clock.asset = context.preview.music_asset() ? context.preview.music_asset()->value : 0;
        clock.epoch = context.epoch;
        clock.epoch_begin_ns = context.epoch_begin_ns;
        clock.playback_rate = rate;
        clock.keep_pitch = keep_pitch;
        clock.playing = playing && context.output.playing();
        if (token == context.bridge.session_token() && rate == context.output.playback_rate() &&
            keep_pitch == context.output.keep_pitch() && context.preview.compiled.value().schedule) {
            clock.source_frame = preview_music_frame(*context.preview.compiled.value().schedule, frame);
            clock.beats_per_measure = tap_meter_at(context.document, context.preview, context.preview.source_frame(frame));
        }
#endif
        return clock;
}

auto dispatch_audition_toggle(const wormedit::command_invocation_context_t& invocation,
    wormedit::sys::program_preferences_runtime_t& program, application_command_state_t& state,
    wormedit::notification_sink_t& notifications)
    -> std::optional<wormedit::dispatch_outcome_t> {
    const auto command = parse_application_command(invocation.command.value);
    if (!command)
        return std::nullopt;
    auto& editor = state.preferences.editor_preferences;
    std::string error;
    bool enabled{};
    std::string label;
    if (command->kind == wormedit::sys::application_command_kind_t::audio_toggle_click_audition) {
        error = wormedit::sys::toggle_audition_on_click(program, editor.audition_on_click);
        enabled = editor.audition_on_click;
        label = "Play sound on click";
    } else if (command->kind == wormedit::sys::application_command_kind_t::audio_toggle_playback_audition) {
        error = wormedit::sys::toggle_audition_on_playback(program, editor.audition_on_playback);
        enabled = editor.audition_on_playback;
        label = "Play hitsounds on playback";
    } else return std::nullopt;
    if (!error.empty())
        return wormedit::sys::reject_application_command(notifications, error,
            wormedit::notification_severity_t::error, wormedit::notification_source_t::system);
    notifications.post(wormedit::notification_severity_t::success, wormedit::notification_source_t::input,
        label + (enabled ? " enabled." : " disabled."));
    return wormedit::dispatch_outcome_t{.value = true};
}

template<class InvokeEditor>
auto dispatch_options_or_editor_command(const wormedit::command_invocation_context_t& invocation,
    wormedit::sys::options_runtime_t& options, const wormedit::program_preferences_t& preferences,
    wormedit::sys::notification_history_runtime_t& notifications,
    wormedit::notification_center_t& center, InvokeEditor&& invoke_editor) -> wormedit::dispatch_outcome_t {
    const auto command = parse_application_command(invocation.command.value);
    if (command) {
        const auto notification = wormedit::sys::invoke_notification_history_command(
            *command, invocation.auto_repeat, notifications, center);
        if (notification) return *notification;
    }
    if (command && command->kind == wormedit::sys::application_command_kind_t::workspace_options) {
        wormedit::sys::open_options(options, preferences);
        return {.value = true};
    }
    return invoke_editor();
}

auto create_chart_from_registered_command(wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& compose_interaction_state,
    application_command_state_t& application_command_state, wormedit::notification_sink_t& notifications)
    -> wormedit::dispatch_outcome_t {
    using severity_t = wormedit::notification_severity_t;
    using source_t = wormedit::notification_source_t;
    const auto created = wormedit::apply_add_chart_command(
        history, workspace.selected_chart.value_or(wormedit::chart_id_t{}),
        &application_command_state.preferences.default_lane_keys);
    if (!created)
        return wormedit::sys::reject_application_command(
            notifications,
            created.command.diagnostics.empty() ? "New chart could not be created."
                                                : created.command.diagnostics.front().message,
            created.command.diagnostics.empty()
                ? severity_t::warning
                : to_notification_severity(created.command.diagnostics.front().severity),
            source_t::document);
    auto outcome = wormedit::sys::compose_action_outcome_t::from({}, severity_t::success,
                                                                 source_t::document, true);
    outcome.delivery = wormedit::sys::compose_feedback_delivery_t::none;
    outcome.rebind_failure = "New chart created, but workspace rebinding failed.";
    outcome.rebind_failure_policy = wormedit::sys::compose_rebind_failure_policy_t::message_only;
    outcome.rebind_feedback_policy = wormedit::sys::compose_rebind_feedback_policy_t::document_only;
    auto finished = wormedit::sys::finish_compose_action({history, workspace, notifications},
                                                         notifications, std::move(outcome));
    if (!finished.accepted)
        return {.diagnostics = {{.severity = wormedit::diagnostic_severity_t::error,
                                 .code = wormedit::diagnostic_code_t::unsupported_feature,
                                 .message = std::move(finished.message)}}};
    if (application_command_state.services.transport_stop) application_command_state.services.transport_stop();
    const auto error = open_project_chart(history, workspace, compose_interaction_state, *created.payload.chart);
    if (!error.empty()) return wormedit::sys::reject_application_command(notifications, error,
                                                                       severity_t::error, source_t::document);
    remember_snap_denominator(application_command_state, *created.payload.chart,
        history.document.settings.snap_for(application_command_state.preferences.editor_preferences.default_snap).value);
    notifications.post(severity_t::success, source_t::document, "New chart created.");
    return wormedit::dispatch_outcome_t{.value = true};
}





#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#endif

void retain_compose_hover_timing(
    wormedit::sys::compose_scene_cache_t& cache, const wormedit::ui::workspace_view_t& view,
    const wormedit::ui::workspace_action_t& action) {
    auto& hover = cache.waveform.hover_timing;
    hover.valid = action.hover_source_frame_valid && view.project_id.value != 0 &&
        view.selected_chart_id.value != 0 && view.compose.selected_audio_asset_id.value != 0 &&
        action.hover_source_total_frames != 0 && action.hover_source_sample_rate != 0;
    if (!hover.valid) return;
    hover.source_frame = action.hover_source_frame;
    hover.project_id = view.project_id.value;
    hover.chart_id = view.selected_chart_id.value;
    hover.document_revision = view.document_revision;
    hover.audio_asset_id = view.compose.selected_audio_asset_id.value;
    hover.total_frames = action.hover_source_total_frames;
    hover.sample_rate = action.hover_source_sample_rate;
}

void retain_compose_bgm_window_capture(
    wormedit::sys::application_command_state_t& application, const wormedit::ui::workspace_view_t& view,
    const wormedit::ui::workspace_action_t& action) {
    application.project.bgm.bgm_bar_window_capture.reset();
    if (!action.minimap_window_capture_active || view.project_id.value == 0 ||
        view.selected_chart_id.value == 0 ||
        view.compose.selected_audio_asset_id.value == 0 ||
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
        .project = wormedit::song_id_t{view.project_id.value},
        .chart = wormedit::chart_id_t{view.selected_chart_id.value},
        .document_revision = view.document_revision,
        .asset = wormedit::asset_id_t{view.compose.selected_audio_asset_id.value},
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
                  wormedit::sys::project_portability_notice_t::warning_message());
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
        result_t<std::filesystem::path, std::string> recovery_directory{};
        application_startup_arguments_t arguments{};
        frontend_event_batch_t event_batch;
        std::optional<display_watchdog_t> display_watchdog;
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
        task_scheduler_t resource_copy_scheduler{1, 4, background_scheduler_options()};
        runtime::resource_copy_t resource_copy_runtime{resource_copy_scheduler, notifications};
        runtime::preview_audio_export_t preview_audio_runtime{resource_copy_scheduler, notifications};
        runtime::audio_slicer_t audio_slicer_runtime{resource_copy_scheduler, notifications};
        [[nodiscard]] auto is_resource_worker_busy() const -> bool {
            return resource_copy_runtime.is_busy() || preview_audio_runtime.is_busy() || audio_slicer_runtime.is_busy();
        }
        runtime::metadata_asset_t metadata_asset_runtime{notifications};
        runtime::output_group_export_t export_runtime{notifications};
        task_scheduler_t project_open_scheduler{1, 1, background_scheduler_options()};
        project_open_runtime_t project_open_runtime{project_open_scheduler};
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        visual::image_cache_t player_image_cache{};
        task_scheduler_t player_image_scheduler{1, 4, background_scheduler_options()};
        visual::image_job_t player_image_job{player_image_cache, player_image_scheduler,
                                             wormedit::ui::player_preview_visual_layer_limit +
                                                 wormedit::ui::maximum_preview_storyboard_instances};
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
        audio_options_runtime_t options{};
        metronome_t metronome{};
        project_audio_sources_t project_sources{.metronome = &metronome};
        audio::decoded_block_cache_t audition_cache{};
        audio::transport_t audition_transport{audition_cache};
        audio::audition_transport_bridge_t audition_bridge{audition_transport};
        audio::object_audition_controller_t object_audition{audition_bridge};
        audio::audition_session_t audition_session{project_sources.registry(), audition_transport,
                                                   audition_bridge};
        sdl_audition_output_t audition_output{audition_session};
        audio::decoded_block_cache_t preview_cache{audio::preview_block_cache_limits()};
        runtime::waveform_analysis_t waveform_runtime{project_sources.registry(), &preview_cache,
                                                      notifications};
        audio::transport_t preview_transport{preview_cache};
        audio::preview_transport_bridge_t preview_bridge{preview_transport};
        audio::preview_session_t preview_session{project_sources.registry(), preview_transport,
                                                 preview_bridge};
        audio::resident_sample_store_t resident_samples{};
        sdl_preview_output_t preview_output{preview_session};
        runtime::chart_preview_t preview_state{};
        task_scheduler_t audition_probe_scheduler{2, 4, background_scheduler_options()};
        resident_sample_loader_t resident_loader{resident_samples, audition_probe_scheduler};
    } audio_runtime{platform.notification_sink};
#endif

    struct preferences_t {
        program_preferences_runtime_t program_preferences{};
        external_player_runtime_t external_player_runtime{};
        options_runtime_t options_runtime{};
        lane_keys_runtime_t lane_keys{};
        shortcut_editor_runtime_t shortcut_editor{};
    } preferences;
    application_command_state_t commands{};
    command_registry_t command_registry{};
    // Shortcut text the command palette shows beside each command.
    wormedit::sys::command_shortcut_text_t command_menu_shortcuts{};

    struct editor_views_t {
        std::string close_feedback{};
        workspace_edit_feedback_t workspace_edit_feedback{};
        projection_notification_feedback_t projection_feedback{}, workspace_view_feedback{};
        notification_history_runtime_t notification_history{};
        wormedit::ui::notification_action_t pending_notification{};
        sound_drop_transaction_t sound_drop_transaction{};
        std::vector<sound_drop_batch_t> pending_resource_batches{};
        bga_dock_runtime_t bga_dock_runtime{};
        std::optional<proposal_t<audio_offset_drag_t>> audio_offset_drag{};
        std::optional<workspace_tab_t> audio_offset_drag_tab{};
        timing_scene_cache_t timing_scene_cache{};
        runtime::timing_edit_t timing_edit{};
        compose_scene_cache_t compose_scene_cache{};
        compare_scene_cache_t compare_scene_cache{};
        frontend_workspace_view_t workspace_frame{};
        frontend_workspace_view_t compare_first_frame{};
        frontend_workspace_view_t compare_second_frame{};
        player_preview_presenter_t player_preview_presenter{};
        std::optional<editor_interaction_state_t> compose_interaction_state{};
        viewport_input_controller_t compose_viewport_input{};
        editor_navigation_input_controller_t compose_navigation_input{};
        project_chart_runtime_t project_chart_runtime{};
        chart_import_session_t chart_import{};
        pending_close_t pending_close{};
        inline_preview_cursor_t inline_preview_cursor{};
        tap_timing_runtime_t tap_runtime{};
        std::optional<tap_timing_assist_publication_t> published_assist{};
        std::optional<std::uint64_t> last_tap_event_ns{};
        std::uint64_t tap_epoch{}, tap_epoch_begin_ns{};
        std::function<tap_timing_clock_t()> tap_clock;
        timing_grid_state_t timing_grid{};
        metadata_drop_state_t metadata_drop{};
        // Starts true so the first frame reads the clipboard the application started with.
        bool system_clipboard_changed{true};
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
    static constexpr std::size_t worker_count = 4
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
        &jobs.export_runtime.scheduler(),
        &jobs.project_open_scheduler,
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
        const wormedit::ui::workspace_action_t& action;
        bool timing_locked;
        std::optional<std::uint32_t> selected_sample_rate;
    };
    auto document_inputs() -> document_edit_inputs_t {
        return {.history = session.history, .workspace = session.workspace,
                .notifications = platform.notification_sink, .auditions = &commands.project.auditions};
    }
    auto command_inputs() -> command_dispatch_inputs_t {
        return {document_inputs(),
                {commands, views.compose_interaction_state, views.timing_edit.stage}};
    }
    auto compose_actions() -> compose_action_inputs_t {
        return {document_inputs(),
                {commands, views.compose_interaction_state},
                compose_resource_inputs_t{
                    session.project_identity.asset_base,
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
                session.project_identity.asset_base, views.project_chart_runtime.session_generation};
    }
    auto frontend_inputs() -> frontend_event_inputs_t {
        return {document_inputs(), views.metadata_drop, session.project_identity,
                views.system_clipboard_changed};
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
        if (session.autosave_loop.is_busy() || jobs.is_resource_worker_busy() ||
            jobs.export_runtime.is_busy() || jobs.metadata_asset_runtime.import_pending()) {
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

    void bind_project_journal() {
        if (!session.project_identity.session_loaded) return;
        auto target = native_project_autosave_target_t::from(session.history.document.id,
            session.project_identity.native_project_loaded, session.project_identity.active_project_path,
            platform.recovery_directory ? *platform.recovery_directory : std::filesystem::path{});
        target.asset_base = session.project_identity.asset_base;
        session.autosave_loop.observe(session.history, target);
    }

    void tick_loaded_project_autosave() {
        if (!session.project_identity.session_loaded || jobs.project_open_runtime.is_busy())
            return;
        static const std::filesystem::path unavailable_directory;
        wormedit::sys::tick_application_autosave(
            session.autosave_loop, session.history, session.project_identity.native_project_loaded,
            session.project_identity.active_project_path,
            platform.recovery_directory ? *platform.recovery_directory : unavailable_directory,
            platform.notification_sink, session.project_identity.asset_base);
    }

    void apply_notification_action(const wormedit::ui::notification_action_t& action) {
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

    void move_staged_timing_event(const wormedit::ui::workspace_action_t& action, bool timing_locked,
                                  std::uint64_t active_snap_denominator,
                                  const wormedit::chart_document_t* chart,
                                  wormedit::timing_lane_pseudonote_outcome_t& stage) {
        const auto target =
            chart == nullptr || !stage.report()->existing_event || !action.timing_has_target_y
                ? std::optional<wormedit::local_position_t>{}
                : timing_position_from_fraction(
                      *chart, *session.history.indices(chart->id),
                      wormedit::sys::timing_target_fraction(action.timing_target),
                      active_snap_denominator);
        if (!target || !(stage = wormedit::timing_lane_pseudonote_preview_t::revise(std::move(stage), session.history, *target, timing_locked)).value.has_value()) {
            post_edit_rejection(platform.notification_sink, stage.diagnostics,
                                "The timing marker cannot move to that row.");
        } else {
            platform.notification_sink.post_transient_hint(
                wormedit::notification_source_t::edit,
                "Timing marker moved in the staged preview; apply to commit.");
        }
    }

    auto finish_confirmed_close(pending_close_t& pending) -> bool {
        if (jobs.resource_copy_runtime.is_project_save_busy())
            return false;
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
                wormedit::sys::make_empty_session_document(wormedit::sys::create_session_project_id()),
                false, true, false, {}, {}))
            return false;
        pending = {};
        post_project_feedback(wormedit::notification_severity_t::info,
                              wormedit::notification_source_t::document, "No project loaded.");
        return true;
    }

    auto save_project_session(const wormedit::ui::workspace_action_t& action) -> bool {
        using namespace wormedit;
        if (!session.project_identity.session_loaded) {
            post_project_feedback(notification_severity_t::warning, notification_source_t::document,
                                  "Create or open a project before saving.");
            return false;
        }
        if (jobs.is_resource_worker_busy() || jobs.export_runtime.is_busy() ||
            jobs.metadata_asset_runtime.import_pending() || jobs.project_open_runtime.is_busy()) {
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
        const auto extension = text_util::lowercase_extension(destination);
        if (extension.empty())
            destination += ".wormedit";
        else if (extension != ".wormedit") {
            post_project_feedback(notification_severity_t::warning, notification_source_t::document,
                                  "Choose a .wormedit project filename.");
            return false;
        }
        std::error_code path_error;
        destination = std::filesystem::absolute(destination, path_error);
        if (path_error) {
            post_project_feedback(notification_severity_t::error, notification_source_t::document,
                                  "Could not resolve the project destination: " +
                                      path_error.message());
            return false;
        }
        session.autosave_loop.flush_journal();
        const auto started = jobs.resource_copy_runtime.start_project_save(
            session.history.document, session.project_identity.asset_base, destination);
        if (!started) {
            post_project_feedback(notification_severity_t::error, notification_source_t::document,
                                  started.error().message);
            return false;
        }
        return true;
    }

    void finish_project_save(const wormedit::sys::project_save_report_t& report,
                              wormedit::document_stamp_t stamp) {
        using namespace wormedit;
        report.publish_paths(session.history, session.project_identity.asset_base);
        for (auto* frame : {&views.workspace_frame, &views.compare_first_frame, &views.compare_second_frame})
            frame->metadata_projection_cache.invalidate_asset_paths();
        session.history.saved_revision = stamp.revision;
        const auto version =
            native_project_document_version_t{.project_id = stamp.project, .revision = stamp.revision};
        session.project_identity.scanned_primary_version = version;
        session.project_identity.active_project_path = report.destination;
        remember_project_file(preferences.program_preferences, report.destination,
                              platform.notification_sink);
        session.project_identity.native_project_loaded = true;
        session.project_identity.asset_base = report.destination.parent_path();
        session.project_identity.external_player_source_path.clear();
        bind_project_journal();
        session.autosave_loop.mark_explicit_save(version, session.history.document);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        post_audio_inventory_feedback(
            audio_runtime.project_sources.reconcile(session.history.document, session.project_identity.asset_base),
            platform.notification_sink);
#endif
        auto severity = report.copy_errors.empty() ? notification_severity_t::success
                                                   : notification_severity_t::warning;
        if (!report.storage.diagnostics.empty())
            severity = std::max(severity, to_notification_severity(report.storage.diagnostics.front().severity));
        post_project_feedback(severity, notification_source_t::document, report.status_message());
        auto& pending = views.pending_close;
        if (pending.target != close_target_t::none && pending.project == stamp.project &&
            pending.revision == stamp.revision && !is_document_dirty(session.history))
            (void)finish_confirmed_close(pending);
    }

    void poll_project_save() {
        const auto completed = jobs.resource_copy_runtime.poll_project_save();
        if (!completed) return;
        std::string failure;
        if (!*completed) failure = completed->error().cause.message;
        else if (!completed->value().payload->storage.value)
            failure = completed->value().payload->status_message();
        if (!failure.empty()) {
            post_project_feedback(wormedit::notification_severity_t::error,
                                  wormedit::notification_source_t::document, failure);
            if (views.pending_close.save_requested) views.pending_close.save_failure = "Save failed: " + failure;
            return;
        }
        if (completed->value().identity.project != session.history.document.id) return;
        finish_project_save(*completed->value().payload, completed->value().identity);
    }

    void create_audio_project_session(const char* path, std::string_view layout_id) {
        const auto audio = wormedit::sys::inspect_audio_for_project(path);
        if (!audio.is_success()) {
            post_project_feedback(wormedit::notification_severity_t::error,
                                  wormedit::notification_source_t::import_, audio.error);
            session.project_identity.pending_import.audio_project_path.clear();
            return;
        }
        auto document = wormedit::sys::make_audio_project_document(
            wormedit::sys::create_session_project_id(), audio.source_path, audio.title, audio.artist,
            layout_id, audio.stream);
        if (!document) {
            post_project_feedback(wormedit::notification_severity_t::error,
                                  wormedit::notification_source_t::import_,
                                  "Could not create the selected audio chart layout.");
            return;
        }
        if (!replace_project_session(std::move(*document), true, true, false, {},
                                     audio.source_path.parent_path()))
            return;
        for (const auto* chart : session.history.document.read_charts())
            remember_snap_denominator(commands, chart->id,
                session.history.document.settings.snap_for(commands.preferences.editor_preferences.default_snap).value);
        session.workspace.active_tab = workspace_tab_t::compose;
        const auto selected = wormedit::apply_workspace_action(
            session.workspace, session.history.document,
            wormedit::workspace_select_asset_action_t{
                .asset = session.history.document.audio_assets.front().id,
                .slot = wormedit::workspace_asset_slot_t::audio});
        post_project_feedback(
            (selected.value && selected.diagnostics.empty()) ? wormedit::notification_severity_t::success
                     : wormedit::notification_severity_t::warning,
            wormedit::notification_source_t::import_,
            (selected.value && selected.diagnostics.empty()) ? "Audio imported as a new " + std::string(wormedit::find_new_project_layout(layout_id)->label) + " project."
                     : "Audio project created; select the music asset to preview it.");
    }

    void dispatch_audio_offset_lock_feedback(const wormedit::ui::workspace_action_t& action) {
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

    void begin_notification_frame(wormedit::ui::notification_action_t& pending,
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
        if (path.size() > wormedit::maximum_project_path_bytes) {
            post_project_feedback(wormedit::notification_severity_t::warning,
                                  wormedit::notification_source_t::import_,
                                  "The dropped chart or project path is too long.");
            return;
        }
        wormedit::ui::workspace_action_t action{};
        action.kind = WORM_UI_WORKSPACE_ACTION_OPEN_FILE;
        action.project_path = path.c_str();
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

    auto dispatch_close_confirmation(pending_close_t& pending, const wormedit::ui::workspace_action_t& action,
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
            if (action.close_project_id != pending.project ||
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
            drag->payload.sample_rate != *context.selected_sample_rate) {
            return clear_audio_offset_drag(
                "Audio offset drag has no matching captured transaction.");
        }
        const auto updated = drag->revise(
            history.document, {.is_timing_locked = timing_locked}, action.audio_offset_drag_delta_frames);
        if (!updated.ok() || !updated.value) {
            drag.reset();
            drag_tab.reset();
            return post_audio_offset_diagnostic(updated.diagnostics,
                                                "Audio offset preview was rejected.",
                                                notification_severity_t::warning);
        }
        drag = updated.value;
        if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_UPDATE)
            return post_audio_offset_feedback(notification_severity_t::hint,
                                              "Previewing exact waveform offset.");
        if (action.audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_COMMIT) {
            drag.reset();
            drag_tab.reset();
            return post_audio_offset_feedback(notification_severity_t::warning,
                                              "Invalid audio offset drag action.");
        }
        const auto committed = apply_proposal(history, *drag, timing_locked);
        drag.reset();
        drag_tab.reset();
        if (!committed.applied) {
            return post_audio_offset_diagnostic(committed.diagnostics,
                                                "Audio offset was unchanged.",
                                                notification_severity_t::info);
        }
        auto outcome = compose_action_outcome_t::from("Audio offset updated from waveform drag.",
                                                      notification_severity_t::success,
                                                      notification_source_t::edit, true);
        outcome.rebind_failure = "Audio offset changed, but workspace rebinding failed.";
        outcome.rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only;
        auto finished = finish_compose_action(document_inputs(), platform.notification_sink,
                                              std::move(outcome));
        return std::move(finished.message);
    }

    auto launch_external_player(wormedit::sys::external_player_action_t action) -> std::string {
        using namespace wormedit::sys;
        const auto& asset_base = session.project_identity.asset_base;
        const auto& configured = preferences.program_preferences.preferences.players;
        if (configured.selected >= configured.players.size()) {
            wormedit::sys::open_options_category(preferences.options_runtime,
                preferences.program_preferences.preferences, WORM_UI_OPTIONS_PLAYERS);
            return post_callback_feedback(platform.notification_sink,
                                          wormedit::notification_severity_t::warning,
                                          wormedit::notification_source_t::audio,
                                          "Choose a player in Options → External players.");
        }
        const auto& player = configured.players[configured.selected];
        if (player.path.empty()) {
            wormedit::sys::open_options_category(preferences.options_runtime,
                preferences.program_preferences.preferences, WORM_UI_OPTIONS_PLAYERS);
            return post_callback_feedback(platform.notification_sink,
                                          wormedit::notification_severity_t::warning,
                                          wormedit::notification_source_t::audio,
                                          "Choose a player in Options → External players.");
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
        const auto launch = preferences.external_player_runtime.player.launch(
            player, action, session.history.document, chart ? chart->id : wormedit::chart_id_t{},
            source, measure, application_path ? application_path : "");
        if (launch.open_preferences)
            wormedit::sys::open_options_category(preferences.options_runtime,
                preferences.program_preferences.preferences, WORM_UI_OPTIONS_PLAYERS);
        return post_callback_feedback(
            platform.notification_sink,
            launch.is_success() ? wormedit::notification_severity_t::success
                            : (launch.open_preferences ? wormedit::notification_severity_t::warning
                                                       : wormedit::notification_severity_t::error),
            wormedit::notification_source_t::audio,
            launch.is_success() ? "External player command launched." : launch.error);
    }

    auto dispatch_application_shortcuts(const wormedit::ui::workspace_action_t& action,
                                        std::optional<std::uint64_t>& tap_event_ns,
                                        bool& preserves_editor_gesture) -> bool {
        using namespace wormedit;
        preserves_editor_gesture = true;
        if (action.shortcut_overflowed ||
            action.shortcut_events.size() > wormedit::ui::shortcut_event_admission_limit) {
            platform.notification_sink.post(
                notification_severity_t::warning, notification_source_t::input,
                "Shortcut input exceeded its per-frame capacity and was discarded.");
            preserves_editor_gesture = false;
            return true;
        }
        bool handled{};
        for (std::size_t index = 0; index < action.shortcut_events.size(); ++index) {
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
            if (!application_command_preserves_editor_gesture(dispatched.value.command,
                    views.compose_interaction_state ? &*views.compose_interaction_state : nullptr))
                preserves_editor_gesture = false;
            if (dispatched.value.status == command_dispatch_status_t::invoked) {
                handled = true;
            } else if (dispatched.value.status == command_dispatch_status_t::disabled) {
                handled = true;
                platform.notification_sink.post(
                    notification_severity_t::warning, notification_source_t::input,
                    wormedit::sys::unavailable_application_command_message(
                        command_registry, dispatched.value.command, commands));
            } else if (dispatched.value.status == command_dispatch_status_t::conflict ||
                       dispatched.value.status == command_dispatch_status_t::invalid_event ||
                       dispatched.value.status == command_dispatch_status_t::rejected) {
                handled = true;
                post_shortcut_rejection(platform.notification_sink, command_registry, dispatched);
            } else if (dispatched.value.status == command_dispatch_status_t::repeat_suppressed) {
                handled = true;
            }
        }
        return handled;
    }

    void begin_project_session(wormedit::sys::project_identity_t replacement) {
        jobs.project_open_runtime.cancel();
        wormedit::sys::begin_project_session(session.project_identity, views.project_chart_runtime,
                                             std::move(replacement));
        bind_project_journal();
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
            audio_runtime.project_sources.reconcile(session.history.document, session.project_identity.asset_base),
            platform.notification_sink);
#endif
    }

    auto begin_audio_offset_drag_action(AudioOffsetDragContext& context) -> std::string {
        using namespace wormedit;
        const auto begun =
            audio_offset_drag_t::propose(session.history.document, *session.workspace.selected_chart,
                                    *context.selected_sample_rate, context.timing_locked);
        views.audio_offset_drag = begun.ok() ? begun.value : std::nullopt;
        views.audio_offset_drag_tab =
            views.audio_offset_drag ? std::optional{session.workspace.active_tab} : std::nullopt;
        if (views.audio_offset_drag)
            return post_audio_offset_feedback(notification_severity_t::hint,
                                              "Dragging waveform offset; release to apply.");
        return post_audio_offset_diagnostic(begun.diagnostics,
                                            "Audio offset drag could not start.",
                                            notification_severity_t::warning);
    }

    void save_before_confirmed_close(pending_close_t& pending,
                                     const wormedit::ui::workspace_action_t& action) {
        const auto id = session.history.document.id;
        if (id != pending.project) {
            post_project_feedback(
                wormedit::notification_severity_t::warning,
                wormedit::notification_source_t::document,
                "The project changed while closing. Cancel and review the current project.");
            return;
        }
        pending.save_failure.clear();
        if (!save_project_session(action))
            pending.save_failure = "Save failed: " + views.close_feedback;
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
        if (loaded && !native_project)
            preferences.program_preferences.preferences.default_lane_keys.apply_to(document);
        wormedit::sys::refresh_project_audio_availability(document, asset_base);
        auto replacement_history = document_history_t::from(std::move(document), 128, saved);
        auto replacement_binding = workspace_controller_t::from(replacement_history);
        if (!replacement_binding.value || !replacement_binding.diagnostics.empty()) {
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
        session.workspace = std::move(*replacement_binding.value);
        begin_project_session({.session_loaded = loaded,
                               .native_project_loaded = native_project,
                               .active_project_path = std::move(native_path),
                               .asset_base = std::move(asset_base),
                               .scanned_primary_version = saved_version});
        return true;
    }

    auto is_project_replacement_busy() -> bool {
        if (!session.autosave_loop.is_busy() && !jobs.is_resource_worker_busy() &&
            !jobs.export_runtime.is_busy() && !jobs.metadata_asset_runtime.import_pending())
            return false;
        post_project_feedback(wormedit::notification_severity_t::warning,
                              wormedit::notification_source_t::document,
                              "Wait for the current file operation before closing the project.");
        return true;
    }

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    void sync_hitsound_monitoring() {
        const auto enabled = preferences.program_preferences.preferences.editor.audition_on_playback;
        auto& preview = audio_runtime.preview_session;
        if (preview.is_hitsounds_enabled() == enabled) return;
        preview.set_hitsounds_enabled(enabled);
        if (!audio_runtime.preview_output.playing()) return;
        const auto refreshed = audio_runtime.preview_output.seek(audio_runtime.preview_output.timeline_frame());
        if (!refreshed)
            post_project_feedback(wormedit::notification_severity_t::error,
                wormedit::notification_source_t::audio,
                refreshed.message().empty() ? "Could not refresh hitsound playback monitoring."
                                            : std::string{refreshed.message()});
    }

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

    auto dispatch_audio_offset_drag_action(const wormedit::ui::workspace_action_t& action, bool timing_locked,
                                           std::optional<std::uint32_t> selected_sample_rate)
        -> std::string {

        AudioOffsetDragContext context{action, timing_locked, selected_sample_rate};
        if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_CANCEL)
            return clear_audio_offset_drag("Audio offset drag cancelled.",
                                           wormedit::notification_severity_t::hint);
        if (auto failure = validate_audio_offset_drag_context(context))
            return clear_audio_offset_drag(std::move(*failure));
        if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_NUDGE)
            return nudge_audio_offset_drag(context);
        if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_BEGIN)
            return begin_audio_offset_drag_action(context);
        return continue_audio_offset_drag(context);
    }

    auto resume_confirmed_encoding(pending_close_t& pending, const wormedit::ui::workspace_action_t& action)
        -> bool {
        if (!pending.awaiting_encoding || action.kind != WORM_UI_WORKSPACE_ACTION_OPEN_FILE ||
            pending.project_path != action.project_path ||
            pending.project != session.history.document.id ||
            pending.revision != session.history.document.revision)
            return false;
        pending.encoding_id = action.bms_encoding_id;
        pending.bms_lane_profile_id = action.bms_lane_profile_id;
        pending.awaiting_encoding = false;
        (void)finish_confirmed_close(pending);
        return true;
    }

    auto dispatch_clipboard_menu(const wormedit::ui::workspace_action_t& action) -> bool {
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

    auto clear_audio_offset_drag(std::string message,
                                 wormedit::notification_severity_t severity =
                                     wormedit::notification_severity_t::warning) -> std::string {
        views.audio_offset_drag.reset();
        views.audio_offset_drag_tab.reset();
        return post_audio_offset_feedback(severity, std::move(message));
    }

    auto dispatch_recovery_workspace_action(const wormedit::ui::workspace_action_t& action) -> bool {
        if (action.kind < WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT ||
            action.kind > WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE)
            return false;
        if (session.autosave_loop.is_busy()) {
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

    void open_project_session(const wormedit::ui::workspace_action_t& action) {
        if (action.bms_encoding_id != wormedit::text_encoding_t::unknown &&
            !wormedit::sys::is_bms_encoding_offered(action.bms_encoding_id)) {
            post_project_feedback(wormedit::notification_severity_t::warning,
                                  wormedit::notification_source_t::import_,
                                  "Choose a supported BMS text encoding.");
            return;
        }
        const auto encoding = action.bms_encoding_id == wormedit::text_encoding_t::unknown
                                  ? std::optional<wormedit::text_encoding_t>{}
                                  : std::optional{action.bms_encoding_id};
        const auto started = jobs.project_open_runtime.start(action.project_path, encoding,
                                                              project_open_identity(), project_open_runtime_t::open,
            action.bms_lane_profile_id.empty() ? std::nullopt : std::optional{action.bms_lane_profile_id});
        if (!started)
            post_project_feedback(notification_severity_t::warning, notification_source_t::import_,
                                  started.error().message);
        else
            session.project_identity.pending_import.bms_encoding_path.clear();
    }

    auto project_open_identity() const -> project_open_identity_t {
        return {.document = document_stamp_t::from(session.history.document),
                .session_generation = views.project_chart_runtime.session_generation};
    }

    void poll_project_open() {
        auto completion = jobs.project_open_runtime.poll();
        if (!completion) return;
        if (!*completion) {
            const auto& error = completion->error();
            views.pending_close = {};
            if (error.identity != project_open_identity()) return;
            post_project_feedback(error.cause.code == failure_code_t::cancelled
                                      ? notification_severity_t::info : notification_severity_t::error,
                                  notification_source_t::import_, error.cause.message);
            return;
        }
        auto& value = **completion;
        if (value.identity != project_open_identity()) {
            views.pending_close = {};
            post_project_feedback(notification_severity_t::warning, notification_source_t::import_,
                                  "Opening discarded because the current project changed.");
            return;
        }
        // The immutable payload owns a detached result; transfer its document without copying.
        auto opened = std::move(**value.payload);
        const auto path = opened.source_path;
        finish_project_open(std::move(opened), path, jobs.project_open_runtime.is_encoding_retry());
    }

    void finish_project_open(wormedit::sys::project_open_result_t opened,
                             const std::filesystem::path& requested_path, bool encoding_retry) {
        if (!opened.is_success()) {
            post_project_feedback((opened.needs_encoding_choice || opened.needs_profile_choice)
                                      ? wormedit::notification_severity_t::warning
                                      : wormedit::notification_severity_t::error,
                                  wormedit::notification_source_t::import_, opened.error);
            if (opened.needs_encoding_choice || opened.needs_profile_choice) {
                auto& pending = session.project_identity.pending_import;
                pending.bms_encoding_path = requested_path.string();
                pending.bms_profile_required = opened.needs_profile_choice;
                pending.bms_encoding = opened.bms_encoding.value_or(text_encoding_t::unknown);
            }
            else {
                if (encoding_retry) session.project_identity.pending_import.bms_encoding_path.clear();
                views.pending_close = {};
            }
            return;
        }
        views.pending_close = {};
        publish_opened_project(std::move(opened));
    }

    // UI-thread publication boundary: only current, uncancelled jobs reach here.
    void publish_opened_project(wormedit::sys::project_open_result_t opened) {
        auto document = std::move(*opened.document);
        if (!opened.native_project)
            document.id = wormedit::sys::create_session_project_id();
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

    void dispatch_workspace_frame_action(const wormedit::ui::workspace_action_t& action, bool shortcut_handled,
                                         std::optional<std::uint32_t> sample_rate,
                                         int compose_viewport_height_pixels) {
        auto& running = this->running;
        auto& history = session.history;
        auto& workspace = session.workspace;
        auto& application_command_state = commands;
        auto& command_registry = this->command_registry;
        auto& compose_interaction_state = views.compose_interaction_state;
        auto& metadata_asset_runtime = jobs.metadata_asset_runtime;
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
                       action, application_command_state, platform.notification_sink)) {
            // Typed producer callbacks already publish this transport outcome.
        } else if (action.kind == WORM_UI_WORKSPACE_ACTION_BGA_USAGE_ACTIVATE) {
            dispatch_bga_usage_jump(
                action, history, workspace, compose_interaction_state, platform.notification_sink);
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
            if (dispatched.value.status == wormedit::command_dispatch_status_t::disabled) {
                platform.notification_sink.post_transient_hint(
                    wormedit::notification_source_t::input,
                    "No beat-anchored bookmarks or section boundaries are available.");
            } else if (dispatched.value.status != wormedit::command_dispatch_status_t::invoked &&
                       !dispatched.diagnostics.empty()) {
                post_shortcut_rejection(platform.notification_sink, command_registry, dispatched);
            }
        } else if (const auto selection_status = dispatch_workspace_selection_action(
                       action, history, workspace, platform.notification_sink)) {
            static_cast<void>(selection_status);
        } else if (const auto metadata_status = wormedit::sys::dispatch_metadata_action(
                       action, metadata_inputs(),
                       application_command_state.audio_offset_edits_locked(),
                       jobs.is_resource_worker_busy(), jobs.export_runtime.is_busy(), true)) {
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

    auto dispatch_project_session_action(const wormedit::ui::workspace_action_t& action) -> bool {
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
                    wormedit::sys::make_empty_session_document(wormedit::sys::create_session_project_id()),
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

    bool dispatch_chart_import_action(const wormedit::ui::workspace_action_t& action) {
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
                                   session.history.document, session.project_identity.asset_base,
                                   &preferences.program_preferences.preferences.default_lane_keys);
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
                auto outcome = compose_action_outcome_t::from(
                    imported.payload.charts.size() == 1 ? "Chart imported." : "Charts imported.",
                    notification_severity_t::success, notification_source_t::document, true);
                outcome.rebind_failure = "Charts imported, but workspace rebinding failed.";
                outcome.is_rebind_severity_diagnostic = false;
                static_cast<void>(finish_compose_action(
                    document_inputs(), platform.notification_sink, std::move(outcome)));
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
        if (audio_runtime.preview_state.compile_if_needed(*chart, session.history.document, audio_runtime.project_sources))
            return false;
        auto region = loop_region_for_selection(
            session.history, *chart, *views.compose_interaction_state->retained_selection_range, begin_row, end_row);
        if (region) {
            region->begin_frame = audio_runtime.preview_state.collapsed_frame(region->begin_frame);
            region->end_frame = audio_runtime.preview_state.collapsed_frame(region->end_frame);
            if (region->begin_frame >= region->end_frame) return false;
        }
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
                chart, session.history.document, audio_runtime.project_sources,
                audio_runtime.audition_output,
                audio_runtime.preview_session, audio_runtime.preview_output, std::nullopt, false,
                audio_runtime.preview_state.source_frame(region->begin_frame)));
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

    auto audition_inputs() -> compose_audition_inputs_t {
        return {.history = session.history, .notifications = platform.notification_sink,
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
                .preview = {audio_runtime.preview_session, audio_runtime.preview_output,
                            audio_runtime.preview_state},
                .audition = {audio_runtime.project_sources, audio_runtime.object_audition,
                             audio_runtime.audition_output},
#endif
        };
    }

    auto process_compose_frame_action(
        wormedit::ui::workspace_action_t& action, double viewport_delta_seconds,
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
            views.compose_interaction_state->is_pending()) {
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
            (action.compose_inputs.size() != 0 || action.compose_viewport_inputs.size() != 0 ||
             action.compose_has_pointer_hover ||
             (session.workspace.active_tab == wormedit::workspace_tab_t::compose &&
              action.compose_input_chart_id.value != 0)) &&
            views.compose_interaction_state) {
            auto audition_scope = commands.project.auditions.input_scope(audition_inputs());
            (void)dispatch_compose_input_action(action, compose_inputs(),
                                                *views.compose_interaction_state,
                                                viewport_delta_seconds, true);
        }
        {
            auto audition_scope = commands.project.auditions.catalog_scope(audition_inputs());
            refresh_compose_editor_width(action, session.history, session.workspace, commands);
        }
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
        wormedit::ui::workspace_action_t action{};
        action.new_project_layout_id = pending.layout_id;
        action.bms_encoding_id = pending.encoding_id;
        action.bms_lane_profile_id = pending.bms_lane_profile_id;
        action.project_path = pending.project_path.c_str();
        if (pending.target == close_target_t::new_project)
            create_new_project_session(action);
        else
            open_project_session(action);
        // BMS option retries continue the same approved replacement of this document version.
        if (pending.target == close_target_t::open_file && jobs.project_open_runtime.is_busy()) {
            pending.awaiting_encoding = true;
            return;
        }
        pending = {};
    }

    auto nudge_audio_offset_drag(AudioOffsetDragContext& context) -> std::string {
        using namespace wormedit;
        if (views.audio_offset_drag) {
            return post_audio_offset_feedback(
                notification_severity_t::warning,
                "Finish or cancel the active audio offset drag before nudging.");
        }
        const auto begun =
            audio_offset_drag_t::propose(session.history.document, *session.workspace.selected_chart,
                                    *context.selected_sample_rate, context.timing_locked);
        if (!begun.ok() || !begun.value) {
            return post_audio_offset_diagnostic(begun.diagnostics,
                                                "Audio offset nudge could not start.",
                                                notification_severity_t::warning);
        }
        const auto updated = begun.value->revise(session.history.document,
            {.is_timing_locked = context.timing_locked}, context.action.audio_offset_drag_delta_frames);
        if (!updated.ok() || !updated.value) {
            return post_audio_offset_diagnostic(updated.diagnostics,
                                                "Audio offset nudge was rejected.",
                                                notification_severity_t::warning);
        }
        const auto committed =
            apply_proposal(session.history, *updated.value, context.timing_locked);
        if (!committed.applied) {
            return post_audio_offset_diagnostic(committed.diagnostics,
                                                "Audio offset was unchanged.",
                                                notification_severity_t::info);
        }
        auto outcome = compose_action_outcome_t::from(
            "Audio offset nudged by an exact frame count.", notification_severity_t::success,
            notification_source_t::edit, true);
        outcome.rebind_failure = "Audio offset changed, but workspace rebinding failed.";
        outcome.rebind_failure_policy = compose_rebind_failure_policy_t::message_only;
        outcome.rebind_feedback_policy = compose_rebind_feedback_policy_t::action_only;
        auto finished = finish_compose_action(document_inputs(), platform.notification_sink,
                                              std::move(outcome));
        return std::move(finished.message);
    }

    auto post_audio_offset_diagnostic(const diagnostic_list_t& diagnostics, std::string fallback,
                                      wormedit::notification_severity_t fallback_severity)
        -> std::string {
        if (!diagnostics.empty())
            return post_audio_offset_feedback(
                to_notification_severity(diagnostics.front().severity),
                diagnostics.front().message);
        return post_audio_offset_feedback(fallback_severity, std::move(fallback));
    }

    bool dispatch_timing_workspace_action(const wormedit::ui::workspace_action_t& action,
                                          application_command_state_t& application_state) {
        if (action.timing_stage_action != wormedit::ui::timing_stage_action_t::none) {
            const auto chart = session.history.read_chart(action.timing_chart_id);
            const auto denominator =
                chart == nullptr
                    ? views.timing_grid.active_denominator
                    : wormedit::effective_edit_snap_grid(
                          *chart, {.denominator = views.timing_grid.active_denominator},
                          application_state.project.compose_view.edit_snap_enabled).denominator;
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
            const auto chart = session.history.read_chart(*session.workspace.selected_chart);
            if (chart != nullptr) {
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
                                 const wormedit::ui::workspace_action_t* replacement = nullptr) {
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
            pending.layout_id = replacement->new_project_layout_id;
            pending.encoding_id = replacement->bms_encoding_id;
            pending.bms_lane_profile_id = replacement->bms_lane_profile_id;
        }
        if (!confirm || !session.project_identity.session_loaded ||
            !wormedit::is_document_dirty(session.history))
            (void)finish_confirmed_close(pending);
    }

    void dispatch_bgm_mix_frame_action(wormedit::ui::workspace_action_t& action, bool shortcut_handled) {
        if (!shortcut_handled && action.kind != WORM_UI_WORKSPACE_ACTION_QUIT)
            (void)dispatch_compose_bgm_mix_inputs(action, session.history, session.workspace,
                                                  commands, &platform.notification_sink);
    }

    bool dispatch_project_settings_action(const ui::project_settings_action_t& action) {
        if (!action.requested) return false;
        if (action.session_generation != views.project_chart_runtime.session_generation) {
            platform.notification_sink.post(notification_severity_t::warning, notification_source_t::document,
                stale_action_message("Project settings"));
            return true;
        }
        auto outcome = compose_action_outcome_t::from(
            wormedit::sys::apply_project_settings_ui_action(session.history, action),
            {.success = "Project settings updated.", .unchanged = "Project settings unchanged.",
             .rebind_failure = "Project settings changed, but workspace rebinding failed.",
             .source = notification_source_t::document});
        (void)finish_compose_action(document_inputs(), platform.notification_sink, std::move(outcome));
        return true;
    }

    bool dispatch_project_chart_action(const wormedit::ui::workspace_action_t& frame_action, bool handled) {
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
             (interaction && interaction->is_pending()))) {
            notifications.post(
                wormedit::notification_severity_t::warning,
                wormedit::notification_source_t::document,
                "Finish or cancel the editor gesture before changing output groups.");
            return true;
        }
        const auto previous_chart = workspace.selected_chart;
        auto result =
            group_action
                ? wormedit::sys::apply_project_output_group_ui_action(
                      runtime, history, group, scheduler, session.project_identity.asset_base)
                : wormedit::sys::apply_project_chart_ui_action(runtime, history, action,
                    workspace.selected_chart, &preferences.program_preferences.preferences.default_lane_keys);
        auto outcome =
            compose_action_outcome_t::from(std::move(result.status), result.severity,
                                           notification_source_t::document, result.applied);
        outcome.rebind_failure = "Project changed, but workspace rebinding failed.";
        const auto finished = finish_compose_action({history, workspace, notifications},
                                                    notifications, std::move(outcome));
        if (result.applied && !finished.accepted)
            return true;
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

    void create_new_project_session(const wormedit::ui::workspace_action_t& action) {
        const std::string_view layout_id{action.new_project_layout_id};
        const auto* layout = wormedit::find_new_project_layout(layout_id);
        if (!layout) {
            post_project_feedback(wormedit::notification_severity_t::warning,
                                  wormedit::notification_source_t::document,
                                  "Choose a supported chart layout.");
            return;
        }
        if (action.project_path[0] != '\0') {
            create_audio_project_session(action.project_path.c_str(), layout->id);
            return;
        }
        auto document =
            wormedit::sys::make_initial_document(wormedit::sys::create_session_project_id(), layout->id);
        if (!document) {
            post_project_feedback(wormedit::notification_severity_t::error,
                                  wormedit::notification_source_t::document,
                                  "Could not create the selected chart layout.");
            return;
        }
        if (replace_project_session(std::move(*document), true, true, false, {}, {})) {
            for (const auto* chart : session.history.document.read_charts())
                remember_snap_denominator(commands, chart->id,
                    session.history.document.settings.snap_for(commands.preferences.editor_preferences.default_snap).value);
            session.project_identity.fresh_blank_project = true;
            session.workspace.active_tab = workspace_tab_t::compose;
            post_project_feedback(wormedit::notification_severity_t::success,
                                  wormedit::notification_source_t::document,
                                  "New " + std::string(layout->label) +
                                      " project created. Save it to keep your work.");
        }
    }

    void dispatch_menu_command(wormedit::ui::workspace_action_t& action) {
        const auto& command_id =
            action.command_menu_invoked ? action.command_menu_command_id : action.menu_command_id;
        if (!command_id[0])
            return;
        if (action.command_menu_invoked &&
            (!action.shortcut_window_focused || action.shortcut_ime_composition_active ||
             action.compose_input_project_id.value != session.history.document.id.value ||
             action.compose_input_chart_id.value !=
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

    auto dispatch_file_command(application_command_kind_t command)
        -> std::optional<dispatch_outcome_t> {
        WormUiWorkspaceActionKind action;
        bool save_as = false;
        switch (command) {
        case application_command_kind_t::document_open: action = WORM_UI_WORKSPACE_ACTION_OPEN_FILE; break;
        case application_command_kind_t::document_save_project: action = WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT; break;
        case application_command_kind_t::document_save_as:
            action = WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT;
            save_as = true;
            break;
        case application_command_kind_t::document_close: action = WORM_UI_WORKSPACE_ACTION_CLOSE_PROJECT; break;
        case application_command_kind_t::app_quit: action = WORM_UI_WORKSPACE_ACTION_QUIT; break;
        default: return std::nullopt;
        }
        if (!worm_ui_frontend_request_file_action(action, session.history.document.id.value, save_as))
            return wormedit::sys::reject_application_command(platform.notification_sink,
                "Finish the open dialog first.", notification_severity_t::warning,
                notification_source_t::document);
        return dispatch_outcome_t{.value = true};
    }

    auto dispatch_registered_application_command(
        const wormedit::command_invocation_context_t& invocation,
        const std::optional<std::uint64_t>& tap_event_ns) -> dispatch_outcome_t {
        auto& program = preferences.program_preferences;
        auto& history = session.history;
        auto& workspace = session.workspace;
        auto& compose_interaction_state = views.compose_interaction_state;
        auto& application_command_state = commands;
        auto& notifications = platform.notification_sink;

        using severity_t = wormedit::notification_severity_t;
        using source_t = wormedit::notification_source_t;
        if (auto toggled = dispatch_audition_toggle(invocation, program,
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
            return wormedit::dispatch_outcome_t{.value = true};
        }
        if (auto file = dispatch_file_command(command->kind)) return *file;
        if (command->kind == wormedit::sys::application_command_kind_t::document_new_chart)
            return create_chart_from_registered_command(history, workspace,
                                                        compose_interaction_state,
                                                        application_command_state, notifications);
        if (command->kind == wormedit::sys::application_command_kind_t::timing_tap_assistant) {
            views.tap_runtime.toggle();
            (void)views.tap_runtime.observe(views.tap_clock());
            return wormedit::dispatch_outcome_t{.value = true};
        }
        if (command->kind == wormedit::sys::application_command_kind_t::timing_tap) {
            const auto clock = views.tap_clock();
            const auto result =
                views.tap_runtime.tap(tap_event_ns.value_or(clock.observed_ns), clock);
            const auto state = views.tap_runtime.state();
            if (!state.status.empty()) {
                const bool guidance = result == wormedit::sys::tap_timing_result_t::ignored ||
                                      result == wormedit::sys::tap_timing_result_t::not_playing;
                static_cast<void>(post_callback_feedback(
                    notifications, guidance ? severity_t::hint : severity_t::warning,
                    source_t::input, std::string{state.status}));
            }
            return wormedit::dispatch_outcome_t{.value = true};
        }
        return wormedit::sys::invoke_application_command(*command, command_inputs());
    }

    auto post_audio_offset_feedback(wormedit::notification_severity_t severity, std::string message)
        -> std::string {
        return post_callback_feedback(platform.notification_sink, severity,
                                      wormedit::notification_source_t::edit, std::move(message));
    }

    void dispatch_timing_stage_value_action(
        const wormedit::ui::workspace_action_t& action, bool timing_locked,
        std::uint64_t active_snap_denominator, const wormedit::chart_document_t* chart,
        std::optional<wormedit::timing_lane_pseudonote_outcome_t>& timing_stage) {
        if (action.timing_anchoring) {
            *timing_stage = wormedit::timing_lane_pseudonote_preview_t::revise(std::move(*timing_stage),
                session.history, *action.timing_anchoring, timing_locked);
            return;
        }
        const auto value = chart == nullptr ? std::optional<wormedit::timing_lane_value_t>{}
                                            : timing_stage_value(*timing_stage->report(), *chart,
                                                                 *session.history.indices(chart->id), action,
                                                                 active_snap_denominator);
        if (!value) {
            timing_stage->diagnostics = {{
                .severity = wormedit::diagnostic_severity_t::error,
                .code = wormedit::diagnostic_code_t::invalid_rational,
                .message = "Enter a bounded exact rational for this timing value.",
            }};
            timing_stage->complete(false);
            platform.notification_sink.post(wormedit::notification_severity_t::warning,
                                            wormedit::notification_source_t::edit,
                                            "Timing value is outside its supported bounds.");
            return;
        }

        const bool valid = (*timing_stage = wormedit::timing_lane_pseudonote_preview_t::revise(std::move(*timing_stage), session.history, *value, timing_locked)).value.has_value();
        if (action.timing_stage_action == wormedit::ui::timing_stage_action_t::confirm && valid) {
            const auto result = wormedit::apply_proposal(
                session.history, *timing_stage, timing_locked);
            const auto edited_existing = timing_stage->report()->existing_event.has_value();
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
        } else if (action.timing_stage_action == wormedit::ui::timing_stage_action_t::confirm) {
            post_edit_rejection(platform.notification_sink, timing_stage->diagnostics,
                                "Timing event values are invalid.");
        }
    }

    void dispatch_timing_stage_action(const wormedit::ui::workspace_action_t& action, bool timing_locked,
                                      std::uint64_t active_snap_denominator) {
        auto& timing_stage = views.timing_edit.stage;
        if (action.timing_stage_action == wormedit::ui::timing_stage_action_t::begin) {
            if (!timing_stage) {
                auto candidate = begin_timing_stage(action, session.history, session.workspace,
                                                    timing_locked, active_snap_denominator);
                if (candidate.value) {
                    timing_stage = std::move(candidate);
                    platform.notification_sink.post_transient_hint(
                        wormedit::notification_source_t::edit,
                        timing_stage->report()->existing_event.has_value()
                            ? "Timing event loaded; apply to update it."
                            : "Timing event staged; confirm to insert it.");
                } else {
                    post_edit_rejection(platform.notification_sink, candidate.diagnostics,
                                        "Timing event could not be staged at that row.");
                }
            }
        } else if (action.timing_stage_action == wormedit::ui::timing_stage_action_t::quick_insert) {
            if (timing_stage) {
                post_edit_rejection(
                    platform.notification_sink, {},
                    "Confirm or cancel the open timing edit before inserting another event.");
            } else {
                quick_insert_timing_event(action, session.history, session.workspace, timing_locked,
                                          active_snap_denominator, platform.notification_sink);
            }
        } else if (action.timing_stage_action == wormedit::ui::timing_stage_action_t::cancel) {
            if (timing_stage) {
                wormedit::cancel_proposal(*timing_stage);
                timing_stage.reset();
                platform.notification_sink.post_transient_hint(
                    wormedit::notification_source_t::edit, "Timing event staging cancelled.");
            }
        } else if (timing_stage) {
            dispatch_active_timing_stage_action(action, timing_locked, active_snap_denominator);
        }
    }

    void dispatch_options_page_action(const wormedit::ui::options_action_t& action) {
        if (handle_shortcut_editor_action(preferences.shortcut_editor, action,
                                          preferences.options_runtime, platform.notification_sink))
            return;
        dispatch_options_frame_action(action);
    }

    auto handle_workspace_view_error(const workspace_view_outcome_t& model,
                                     notification_time_t now, bool confirm)
        -> bool {
        const wormedit::sys::projection_identity_t identity{
            session.history.document.id, session.history.document.revision,
            session.workspace.selected_chart.value_or(wormedit::chart_id_t{}),
            views.project_chart_runtime.session_generation};
        if (model.value && model.diagnostics.empty()) {
            views.workspace_view_feedback.publish(identity, {}, platform.notification_sink);
            return false;
        }
        auto& notifications = views.workspace_frame.view.notifications;
        build_frame_notifications(notifications, now);
        const auto diagnostic = model.diagnostics.empty()
                                    ? std::string{"Workspace view unavailable"}
                                    : model.diagnostics.front().message;
        views.workspace_view_feedback.publish(
            identity,
            {{wormedit::sys::projection_feedback_kind_t::workspace_view, 0,
              wormedit::to_notification_severity(model.diagnostics),
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
            if (!(changed.value && changed.diagnostics.empty()) && !changed.diagnostics.empty())
                platform.notification_sink.post(
                    wormedit::to_notification_severity(changed.diagnostics),
                    wormedit::notification_source_t::document, changed.diagnostics.front().message);
        }
        return true;
    }

    auto prepare_display_options(const wormedit::ui::options_action_t& action) -> bool {
        auto& options = preferences.options_runtime;
        if (action.kind != WORM_UI_OPTIONS_APPLY || !options.visible ||
            action.revision != options.revision || !options.diagnostics.empty() ||
            options.draft.display == options.baseline.display) return true;
        const auto candidate = prepare_options_apply(options, preferences.program_preferences.preferences);
        if (!candidate) {
            if (options.notification) platform.notification_sink.post(*options.notification);
            return false;
        }
        const auto& display = candidate->display;
        auto message = worm_ui_frontend_apply_display_settings(display.theme, display.ui_scale_percent,
                                                              display.frame_timing_overlay);
        if (message.empty()) return true;
        options.status = std::move(message);
        platform.notification_sink.post(notification_severity_t::warning, notification_source_t::system, options.status);
        return false;
    }

    void apply_saved_display_settings() {
        const auto& display = preferences.program_preferences.preferences.display;
        const auto message = worm_ui_frontend_apply_display_settings(display.theme, display.ui_scale_percent,
                                                                    display.frame_timing_overlay);
        if (!message.empty())
            platform.notification_sink.post(notification_severity_t::warning, notification_source_t::system, message);
    }

    void apply_changed_saved_options(const program_preferences_t& previous) {
        const auto& saved = preferences.program_preferences.preferences;
        if (saved.layout.dock != previous.layout.dock)
            worm_ui_frontend_set_dock_layout(saved.layout.dock);
        if (saved.default_lane_keys != previous.default_lane_keys)
            commands.preferences.default_lane_keys = saved.default_lane_keys;
        if (saved.lane_keys != previous.lane_keys)
            commands.preferences.lane_keys = saved.lane_keys;
        if (saved.general.autosave_minutes != previous.general.autosave_minutes)
            (void)session.autosave_loop.set_interval(std::chrono::minutes{saved.general.autosave_minutes});
        if (saved.editor != previous.editor)
            commands.preferences.editor_preferences = saved.editor;
        if (saved.minimap != previous.minimap)
            commands.preferences.minimap_click_moves_paused_playhead = saved.minimap.click_moves_paused_playhead;
        initialize_preferred_transport(saved, commands, &previous.audio);
        if (saved.display.snap_colored_grid != previous.display.snap_colored_grid)
            worm_ui_frontend_set_snap_colored_grid(saved.display.snap_colored_grid);
    }

    void dispatch_options_frame_action(const wormedit::ui::options_action_t& action) {
        if (!prepare_display_options(action)) return;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        refresh_audio_options_request(action);
#endif
        const auto previous = action.kind == WORM_UI_OPTIONS_APPLY
            ? std::optional{preferences.program_preferences.preferences} : std::nullopt;
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
        if (previous) apply_changed_saved_options(*previous);
    }

    void dispatch_active_timing_stage_action(const wormedit::ui::workspace_action_t& action,
                                             bool timing_locked,
                                             std::uint64_t active_snap_denominator) {
        auto& stage = views.timing_edit.stage;
        if (!stage)
            return;
        if (auto failure = wormedit::sys::validate_timing_stage_binding(
                action, session.history, session.workspace, *stage)) {
            wormedit::cancel_proposal(*stage);
            stage.reset();
            post_edit_rejection(
                platform.notification_sink, {},
                *failure);
            return;
        }
        auto* chart_ptr = session.history.read_chart(stage->captured_stamp().chart);
        if (action.timing_stage_action == wormedit::ui::timing_stage_action_t::remove) {
            delete_staged_timing_event(action, session.history, timing_locked, *stage,
                                       platform.notification_sink);
            stage.reset();
        } else if (action.timing_stage_action == wormedit::ui::timing_stage_action_t::move) {
            move_staged_timing_event(action, timing_locked, active_snap_denominator, chart_ptr,
                                     *stage);
        } else {
            dispatch_timing_stage_value_action(action, timing_locked, active_snap_denominator,
                                               chart_ptr, stage);
        }
    }

    void start_requested_chart_import() {
        if (platform.arguments.chart_import_path.empty()) return;
        wormedit::ui::workspace_action_t action{};
        action.kind = WORM_UI_WORKSPACE_ACTION_OPEN_FILE;
        action.project_path = platform.arguments.chart_import_path.string();
        open_project_session(action);
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
        if (platform.recovery_directory && !platform.recovery_directory->empty()) {
            startup_request.recovery_directories.push_back(*platform.recovery_directory);
            platform.lifetime.crash_marker = crash_marker_path(*platform.recovery_directory);
            startup_request.crash_marker_path = platform.lifetime.crash_marker;
        }
        auto startup = prepare_native_project_startup(
            make_empty_session_document(create_session_project_id()), std::move(startup_request));
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
        session.history = document_history_t::from(std::move(startup.document), 128, true);
        auto binding = workspace_controller_t::from(session.history);
        if (!binding.value || !binding.diagnostics.empty()) {
            std::fprintf(stderr, "Initial workspace binding failed\n");
            return false;
        }
        session.workspace = std::move(*binding.value);
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
        bind_project_journal();
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        audio_runtime.resident_samples.set_listener(
            [this](audio::resident_snapshot_ptr_t snapshot) {
                audio_runtime.audition_transport.set_resident_snapshot(snapshot);
                audio_runtime.preview_transport.set_resident_snapshot(std::move(snapshot));
            });
        audio_runtime.resident_loader.set_head_cache(&audio_runtime.preview_cache);
        audio_runtime.project_sources.probe_scheduler = &audio_runtime.audition_probe_scheduler;
        audio_runtime.project_sources.resident_loader = &audio_runtime.resident_loader;
        audio_runtime.project_sources.notifications = &platform.notification_sink;
        audio_runtime.project_sources.hitsounds.environment.bundle_directory =
            std::filesystem::path{SDL_GetBasePath()} / "assets/hitsounds";
        audio_runtime.project_sources.audition_output = &audio_runtime.audition_output;
        audio_runtime.project_sources.preview_output = &audio_runtime.preview_output;
        audio_runtime.project_sources.waveform = &audio_runtime.waveform_runtime;
        audio_runtime.project_sources.preview = &audio_runtime.preview_state;
        post_audio_inventory_feedback(
            audio_runtime.project_sources.reconcile(session.history.document,
                session.project_identity.asset_base, false), platform.notification_sink);
        audio_runtime.options.refresh_devices(preferences.options_runtime);
        service_audio_options();
#endif
        install_startup_recovery_scan(session.recovery_controller, startup);
        session.recovery_scan_feedback.publish(session.recovery_controller.view(),
                                               platform.notification_sink);
        session.recovery_scan_configured = startup.recovery_scan_configured;
        session.project_identity.scanned_primary_version = startup.primary_version;
        commands.bookmarks.chooser_backend = {.open = open_bookmark_chooser_backend};
        commands.services.open_sound_picker = worm_ui_frontend_open_sound_picker;
        commands.services.compose_dock_command = worm_ui_frontend_compose_dock_command;
        commands.services.open_project_export = worm_ui_frontend_open_project_export;
        commands.services.system_clipboard_text = read_system_clipboard_text;
        commands.services.compose_left_dock_pixels = worm_ui_frontend_compose_left_dock_pixels;
        commands.services.compose_bgm_lane_width = worm_ui_frontend_compose_bgm_lane_width;
        preferences.program_preferences = load_program_preferences_runtime();
        remember_startup_file(preferences.program_preferences, startup.loaded_primary,
                              requested_primary_path);
        initialize_program_preferences(preferences.program_preferences,
                                       commands,
                                       platform.notification_sink);
        apply_saved_display_settings();
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
        command_registry = command_registry_t::from(
            command_menu_invoker([this](const command_invocation_context_t& invocation) {
                return invoke_registry_command(invocation);
            }));
        const auto shortcut_startup =
            load_preferred_shortcuts(preferences.program_preferences, command_registry);
        post_shortcut_startup_diagnostic(platform.notification_sink, shortcut_startup);
        views.compose_viewport_last_ticks = static_cast<std::uint64_t>(SDL_GetTicks());
        started_ = true;
        start_requested_chart_import();
        start_display_watchdog();
        return true;
    }

    void start_display_watchdog() {
        auto log = platform.recovery_directory ? *platform.recovery_directory / "display-stalls.log" : std::filesystem::path{};
        const auto* actual = SDL_GetCurrentVideoDriver();
        platform.display_watchdog.emplace(frame_timing(), platform.notification_sink,
            actual ? actual : "unknown", std::move(log));
        const auto* requested = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
        if (requested && actual && std::string_view{requested} == "wayland,x11" && std::string_view{actual} == "x11")
            platform.notification_sink.post(notification_severity_t::warning, notification_source_t::system,
                "Native Wayland initialization failed; using X11.");
    }

    void shutdown() noexcept {
        if (shutdown_started_)
            return;
        shutdown_started_ = true;
        platform.display_watchdog.reset();
        jobs.project_open_runtime.shutdown();
        jobs.resource_copy_runtime.cancel();
        jobs.preview_audio_runtime.cancel();
        jobs.audio_slicer_runtime.cancel();
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

    auto open_lane_keys() -> dispatch_outcome_t {
        preferences.shortcut_editor.capture.reset();
        const auto chart = session.workspace.selected_chart.value_or(chart_id_t{});
        const auto cursor = views.compose_interaction_state
            ? views.compose_interaction_state->cursor : local_position_t{};
        if (preferences.lane_keys.open(session.history.document, chart, cursor,
                                       preferences.program_preferences.preferences))
            return {.value = true};
        return reject_application_command(platform.notification_sink,
            "Select a chart with a named lane layout before editing lane keys.",
            notification_severity_t::warning, notification_source_t::input);
    }

    void dispatch_lane_keys(wormedit::ui::workspace_action_t& action) {
        if (action.lane_keys.kind == ui::lane_keys_action_kind_t::open) (void)open_lane_keys();
        preferences.lane_keys.refresh(session.history.document);
        if (!preferences.lane_keys.view().visible) return;
        preferences.lane_keys.capture(action.shortcut_events);
        action.shortcut_events.resize(0);
        action.compose_inputs.resize(0);
        action.compose_viewport_inputs.resize(0);
        action.compose_action = WORM_UI_COMPOSE_ACTION_NONE;
        action.clipboard = WORM_UI_CLIPBOARD_NONE;
        action.menu_command_id.clear();
        action.command_menu_command_id.clear();
        if (!preferences.lane_keys.dispatch(action.lane_keys, preferences.program_preferences)) return;
        commands.preferences.lane_keys = preferences.program_preferences.preferences.lane_keys;
        const auto& error = preferences.lane_keys.status();
        platform.notification_sink.post(error.empty() ? notification_severity_t::success : notification_severity_t::error,
            notification_source_t::system, error.empty() ? "Lane keys saved." : error);
    }

    auto invoke_registry_command(const command_invocation_context_t& invocation)
        -> dispatch_outcome_t {
        const auto command = parse_application_command(invocation.command.value);
        if (command && command->kind == application_command_kind_t::workspace_lane_keys)
            return open_lane_keys();
        return dispatch_options_or_editor_command(
            invocation, preferences.options_runtime, preferences.program_preferences.preferences,
            views.notification_history, platform.notifications, [this, &invocation] {
                const auto& tap_event_ns =
                    current_tap_event_ ? *current_tap_event_ : views.last_tap_event_ns;
                return dispatch_registered_application_command(invocation, tap_event_ns);
            });
    }

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    void refresh_audio_options_request(const wormedit::ui::options_action_t& action) {
        const auto& options = preferences.options_runtime;
        if (action.kind == WORM_UI_OPTIONS_REFRESH_AUDIO_DEVICES && options.visible &&
            action.revision == options.revision && options.category == WORM_UI_OPTIONS_AUDIO)
            audio_runtime.options.refresh_devices(preferences.options_runtime);
    }

    void service_audio_options() {
        const auto message = audio_runtime.options.service(
            preferences.program_preferences.preferences.audio, preferences.options_runtime,
            audio_runtime.audition_output, audio_runtime.preview_output, audio_runtime.metronome,
            audio_runtime.preview_state, audio_runtime.audition_session.is_active());
        if (!message.empty())
            platform.notification_sink.post(notification_severity_t::warning, notification_source_t::audio, message);
    }

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
        const auto* indices = starting && !exact_start && compose_cursor
            ? session.history.indices(compose_cursor->chart) : nullptr;
        const auto* geometry = indices ? &indices->measures : nullptr;
        const auto result = audio_runtime.preview_state.play_pause(
            selected_chart(session.history, session.workspace), session.history.document,
            audio_runtime.project_sources,             audio_runtime.audition_output, audio_runtime.preview_session,
            audio_runtime.preview_output,
            compose_cursor ? std::optional{compose_cursor->cursor} : std::nullopt, false,
            exact_start, compose_cursor ? compose_cursor->cursor_subrow : 0.0, geometry);
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

    auto on_transport_seek(std::uint64_t frame) -> compose_action_outcome_t {
        if (!audio_runtime.preview_output.playing() && !audio_runtime.preview_state.paused &&
            session.workspace.active_tab == wormedit::workspace_tab_t::compose &&
            views.compose_interaction_state && session.workspace.selected_chart &&
            views.compose_interaction_state->chart == *session.workspace.selected_chart) {
            if (views.compose_interaction_state->is_pending())
                return compose_action_outcome_t::blocked(
                    "Finish or cancel the active Compose gesture before seeking.",
                    wormedit::notification_source_t::input);
            return wormedit::sys::seek_stopped_compose_transport(session.history,
                                                                 *views.compose_interaction_state,
                                                                 views.compose_scene_cache, audio_runtime.preview_state.source_frame(frame));
        }
        const auto result = audio_runtime.preview_state.seek(
            frame, session.history.document, session.workspace, audio_runtime.audition_output,
            audio_runtime.preview_session, audio_runtime.preview_output);
        return compose_action_outcome_t::from(result, audio_runtime.preview_state.feedback.severity,
                                              wormedit::notification_source_t::audio, false,
                                              audio_runtime.preview_state.feedback.accepted);
    }

    auto on_transport_audio_milliseconds() -> std::optional<std::int64_t> {
        return audio_runtime.preview_state.audio_milliseconds(
            session.history.document, session.workspace, audio_runtime.preview_session);
    }

    auto on_transport_seek_audio_milliseconds(std::int64_t milliseconds)
        -> compose_action_outcome_t {
        const auto result = audio_runtime.preview_state.seek_audio_milliseconds(
            milliseconds, session.history.document, session.workspace,
            audio_runtime.audition_output, audio_runtime.preview_session,
            audio_runtime.preview_output);
        return compose_action_outcome_t::from(result, audio_runtime.preview_state.feedback.severity,
                                              wormedit::notification_source_t::audio, false,
                                              audio_runtime.preview_state.feedback.accepted);
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

    auto on_transport_set_keep_pitch(bool enabled) -> bool {
        return audio_runtime.preview_output.set_keep_pitch(enabled);
    }

    auto on_transport_set_playback_rate(wormedit::rational_t rate) -> bool {
        return audio_runtime.preview_output.set_playback_rate(rate);
    }

    auto is_transport_active() -> bool {
        return audio_runtime.audition_session.is_active() || audio_runtime.preview_output.playing() ||
               audio_runtime.preview_state.paused;
    }

    auto is_transport_preview_playing() -> bool {
        return audio_runtime.preview_output.playing();
    }

    auto is_transport_preview_paused() -> bool {
        return audio_runtime.preview_state.paused;
    }

    auto on_transport_preview_end_frame() -> std::optional<std::uint64_t> {
        return audio_runtime.preview_state.matches_selection(session.history.document,
                                                             session.workspace)
                   ? std::optional{audio_runtime.preview_state.compiled.value().end_frame}
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
            audio_runtime.project_sources,             audio_runtime.audition_output, audio_runtime.preview_session,
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
        commands.services.transport_seek = [this](std::uint64_t frame) -> compose_action_outcome_t {
            return on_transport_seek(frame);
        };
        commands.services.transport_audio_milliseconds = [this]() {
            return on_transport_audio_milliseconds();
        };
        commands.services.transport_seek_audio_milliseconds =
            [this](std::int64_t milliseconds) -> compose_action_outcome_t {
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
        commands.metronome = &audio_runtime.metronome;
        audio_runtime.metronome.attach_session(audio_runtime.preview_session,
                                               audio_runtime.preview_output.available());
        commands.services.transport_set_layer_assist =
            [this](wormedit::chart_id_t chart, std::span<const wormedit::layer_id_t> layers) {
                return on_transport_set_layer_assist(chart, layers);
            };
        configure_bgm_mix_command();
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
        commands.bookmarks.workspace = &session.workspace;
        commands.bookmarks.preview_output = &audio_runtime.preview_output;
        commands.bookmarks.preview = &audio_runtime.preview_state;
        commands.services.transport_preview_frame_from_source = [this](std::uint64_t frame) {
            return audio_runtime.preview_state.collapsed_frame(frame);
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
        notification_time_t notification_now;
        std::span<const wormedit::ui::compose_lane_view_t> field_skin_lanes;
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
        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::events);
    }

    void poll_frame_events() {
        {
            wormedit::sys::frame_phase_scope_t input(wormedit::sys::frame_phase_t::events_input);
            if (!wormedit::sys::poll_frontend_events(
                    platform.lifetime.runtime.window, platform.event_batch, frontend_inputs(),
                    can_offer_audio_project_drop(session.project_identity.session_loaded,
                                                 session.project_identity.fresh_blank_project,
                                                 session.history),
                    session.project_identity.pending_import.chart_project_path,
                    views.sound_drop_transaction, views.pending_resource_batches))
                confirm_unsaved_changes(
                    views.pending_close, close_target_t::application,
                    preferences.program_preferences.preferences.general.confirm_unsaved_close);
        }
        refresh_system_clipboard_state();
    }

    void refresh_system_clipboard_state() {
        if (!views.system_clipboard_changed) return;
        views.system_clipboard_changed = false;
        commands.system_clipboard_has_bms_editor_text =
            wormedit::is_bms_editor_clipboard_text(read_system_clipboard_text());
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

    void poll_recipe_evaluation() {
        auto& state = commands.project.timing_recipe;
        if (!views.compose_interaction_state) {
            if (state.evaluation) state.evaluation->cancel();
            return;
        }
        const auto* chart = session.history.read_chart(views.compose_interaction_state->chart);
        if (!chart) {
            if (state.evaluation) state.evaluation->cancel();
            return;
        }
        const recipe_id_t selected{state.recipe_selected};
        const auto recipe = std::ranges::find(chart->sv_recipes, selected, &sv_recipe_t::id);
        if (recipe == chart->sv_recipes.end()) {
            if (state.evaluation) state.evaluation->cancel();
            return;
        }
        const bool has_lua = std::ranges::any_of(chart->sv_recipes, [](const auto& value) {
            return value.enabled && value.curve == sv_recipe_curve_t::lua_expression;
        });
        const bool has_impulses = std::ranges::any_of(chart->sv_recipes, [](const auto& value) {
            return value.enabled && !value.impulses.empty();
        });
        if (!state.evaluation && !has_lua && !has_impulses) return;
        if (!state.evaluation)
            state.evaluation = std::make_shared<recipe_evaluation_runtime_t>(jobs.resource_copy_scheduler);
        state.evaluation->poll(session.history.document, chart->id, selected, state.recipe_preview);
        if (state.evaluation->requested_stamp() != chart_stamp_t::from(session.history.document, chart->id) ||
            state.evaluation->requested_recipe() != selected) {
            static_cast<void>(state.evaluation->request(session.history.document, *chart, *recipe,
                ui::timing_recipe_form_t::from(*recipe), session.project_identity.asset_base));
        }
    }

    void poll_generator_jobs() {
        auto& runtime = commands.project.generator.runtime;
        if (!runtime) return;
        if (!views.compose_interaction_state) { runtime->cancel(); return; }
        runtime->poll(session.history, views.compose_interaction_state->chart);
    }

    auto poll_frame_jobs() -> double {
        double compose_viewport_delta_seconds{};
        {
            wormedit::sys::frame_phase_scope_t jobs_phase(
                wormedit::sys::frame_phase_t::events_jobs);
            wormedit::sys::poll_application_jobs(
                jobs.metadata_asset_runtime, jobs.resource_copy_runtime, jobs.export_runtime,
                jobs.preview_audio_runtime,
                session.history, session.workspace, session.project_identity.asset_base);
            poll_recipe_evaluation();
            poll_generator_jobs();
            jobs.audio_slicer_runtime.poll(session.history, session.workspace);
            poll_project_save();
            poll_project_open();
            compose_viewport_delta_seconds =
                wormedit::sys::compose_frame_delta_seconds(views.compose_viewport_last_ticks);
            tick_loaded_project_autosave();
        }
        return compose_viewport_delta_seconds;
    }

    void service_frame_audio([[maybe_unused]] std::uint64_t expected_presentation_ns) {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::audio_service);
        post_audio_inventory_feedback(
            audio_runtime.project_sources.reconcile_if_needed(session.history.document),
            platform.notification_sink);
        if (const auto failed =
                wormedit::sys::drain_audition_probe_failures(audio_runtime.project_sources.registry())) {
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
        service_audio_options();
        if (const auto changed = audio_runtime.preview_state.synchronize_timing_focus(
                commands.project.compose_view.compose_timing_focus,
                selected_chart(session.history, session.workspace), session.history.document,
                audio_runtime.project_sources, audio_runtime.audition_output,
                audio_runtime.preview_session, audio_runtime.preview_output)) {
            platform.notification_sink.post(audio_runtime.preview_state.feedback.severity,
                wormedit::notification_source_t::audio, *changed);
        }
        wormedit::sys::service_audio_transport_frame(
            session.history.document, session.workspace, audio_runtime.preview_state,
            audio_runtime.preview_session, audio_runtime.audition_output,
            audio_runtime.preview_output, commands.project.transport.preview_playhead_absolute_row,
            platform.notification_sink, expected_presentation_ns);
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

    auto configure_frame_field_skin() -> std::vector<wormedit::ui::compose_lane_view_t> {
        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::workspace);
        return configure_field_skin(active_field_skin_layout(session.history.document,
                                                             session.workspace,
                                                             views.compose_interaction_state),
                                    preferences.options_runtime,
                                    preferences.program_preferences.preferences.display, commands);
    }

    void prepare_frame_model(workspace_view_outcome_t& model) {
        {
            wormedit::sys::frame_phase_scope_t preparation(
                wormedit::sys::frame_phase_t::workspace_preparation);
            const auto previous_authoring_chart = commands.project.bms_program.content_chart();
            commands.project.bms_program.synchronize(session.history, session.workspace.selected_chart);
            if (previous_authoring_chart != commands.project.bms_program.content_chart()) {
                if (views.compose_interaction_state) views.compose_interaction_state->clear_authoring_selection();
                session.history.selections = {};
            }
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
                views.project_chart_runtime.session_generation,
                commands.project.bms_program.content_chart());
        }
        views.timing_edit.cancel_if_stale(session.history, session.workspace, commands);
    }

    void build_frame_notifications(wormedit::ui::notification_view_t& view, notification_time_t now) {
        build_application_notifications_view(
            view, platform.notifications, now, views.compose_interaction_state,
            views.notification_history, session.history.document,
            views.project_chart_runtime.session_generation);
    }

    auto prepare_bound_frame_view(const workspace_view_t& model) -> frontend_workspace_view_t& {
        prepare_bound_workspace_frame();
        prepare_tap_proposal(views.tap_runtime, views.tap_clock, session.history.document,
                             commands.audio_offset_edits_locked(), views.compose_scene_cache);
        wormedit::sys::compose_bga_move_preview_input_t bga_move_preview{};
        bga_move_preview.project_id = session.history.document.id.value;
        worm_ui_frontend_bga_move_preview(&bga_move_preview.chart_id,
                                          &bga_move_preview.document_revision,
                                          &bga_move_preview.event_id, &bga_move_preview.y_pixels);
        views.workspace_frame.refill({
            .model = model,
            .history = session.history,
            .workspace = session.workspace,
            .recovery_controller = session.recovery_controller,
            .recovery_scan_configured = session.recovery_scan_configured,
            .project_session_generation = views.project_chart_runtime.session_generation,
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
            .audition_sources = audio_runtime.project_sources,
            .audition_source_generation = audio_runtime.project_sources.generation,
            .audition_session = audio_runtime.audition_session,
            .preview_session = audio_runtime.preview_session,
            .preview_state = audio_runtime.preview_state,
            .preview_output_available = audio_runtime.preview_output.available(),
#endif
        });
        return views.workspace_frame;
    }

    void complete_frame_view(frontend_workspace_view_t& flattened,
                             const composed_frame_inputs_t& frame) {
        flattened.view.file_jobs.opening = jobs.project_open_runtime.view();
        const auto preview_audio = jobs.preview_audio_runtime.view();
        flattened.view.file_jobs.exporting = preview_audio.active ? preview_audio : jobs.export_runtime.view();
        const auto slicer = jobs.audio_slicer_runtime.view();
        if (slicer.active) flattened.view.file_jobs.exporting = slicer;
        auto& audio = flattened.view.audio_slices;
        jobs.audio_slicer_runtime.populate(audio.slicer, worm_ui_frontend_audio_slicer_settings(),
            audio.sample_rate, audio.total_frames, session.project_identity.native_project_loaded &&
            audio.has_chart && !jobs.is_resource_worker_busy() && !jobs.export_runtime.is_busy() &&
            !jobs.metadata_asset_runtime.import_pending());
        flattened.view.file_jobs.preview = views.project_chart_runtime.preview_view();
        const auto export_confirmation = jobs.export_runtime.confirmation_view();
        flattened.view.compose.export_confirmation_required = export_confirmation.required;
        flattened.view.compose.export_confirmation_project_id.value = export_confirmation.project_id;
        flattened.view.compose.export_confirmation_revision = export_confirmation.document_revision;
        flattened.view.compose.export_confirmation_message = export_confirmation.message;
        flattened.view.compose.export_confirmation_detail = export_confirmation.detail;
        validate_minimap_start_frame(commands, session.history, views.compose_interaction_state);
        flattened.view.minimap_click_moves_paused_playhead =
            commands.preferences.minimap_click_moves_paused_playhead;
        flattened.view.transport_rate_index =
            static_cast<uint8_t>(commands.preferences.transport_rate_index);
        flattened.view.transport_rate_id =
            wormedit::audio::playback_rate_catalog[std::min(commands.preferences.transport_rate_index,
                wormedit::audio::playback_rate_catalog.size() - 1)].id;
        flattened.view.transport_rate_available =
            static_cast<bool>(commands.services.transport_set_playback_rate);
        flattened.view.transport_keep_pitch = commands.preferences.transport_keep_pitch;
        flattened.view.transport_keep_pitch_available =
            commands.preferences.transport_keep_pitch_available;
        build_frame_notifications(flattened.view.notifications, frame.notification_now);
        flattened.view.options.refill(preferences.options_runtime);
        preferences.lane_keys.refresh(session.history.document);
        flattened.view.lane_keys = &preferences.lane_keys.view();
        flattened.view.options.current_field_layout_lanes.assign(
            frame.field_skin_lanes.begin(), frame.field_skin_lanes.end());
        attach_shortcut_rows(flattened.view.options, preferences.options_runtime,
                             preferences.shortcut_editor, command_registry);
        attach_bgm_shortcut(flattened.view.compose, command_registry);
        flattened.view.timing_settings_visible = commands.preferences.timing_settings_visible;
        flattened.view.metronome_volume = commands.metronome_settings().volume;
        flattened.view.metronome_count_in = commands.metronome_settings().count_in;
        flattened.view.chart_import.refill(views.chart_import, session.history.document.revision);
        flattened.view.close_confirmation = close_confirmation_view(
            views.pending_close, session.project_identity.native_project_loaded &&
                                     !session.project_identity.active_project_path.empty());
        flattened.view.setup.selected_chart_id = flattened.view.selected_chart_id;
        flattened.view.setup.refill(views.project_chart_runtime, session.history.document);
        flattened.view.setup.project_has_native_path = session.project_identity.native_project_loaded &&
            !session.project_identity.active_project_path.empty();
        if (flattened.view.setup.visible)
            flattened.view.setup.settings.refill(session.history.document, preferences.program_preferences.preferences);
        build_tap_timing_view(views.tap_runtime, session.history.document, views.tap_clock,
                           command_registry, flattened.view);
        publish_tap_assist();
        wormedit::sys::build_recent_files_view(preferences.program_preferences, flattened.view);
        flattened.view.project_loaded = session.project_identity.session_loaded;
        flattened.view.project_has_native_path = session.project_identity.native_project_loaded;
        flattened.view.pending_new_project_audio_path =
            session.project_identity.pending_import.audio_project_path.c_str();
        flattened.view.pending_bms_encoding_path =
            session.project_identity.pending_import.bms_encoding_path.c_str();
        flattened.view.pending_bms_profile_required = session.project_identity.pending_import.bms_profile_required;
        flattened.view.pending_bms_encoding = session.project_identity.pending_import.bms_encoding;
        build_bga_dock_view(views.bga_dock_runtime, session.history, session.workspace,
                         session.project_identity.asset_base, flattened.view.compose,
                         views.project_chart_runtime.session_generation);
    }

    void dispatch_audio_slicer(const wormedit::ui::workspace_action_t& action) {
        if (!action.audio_slicer) return;
        if (!session.project_identity.native_project_loaded || jobs.is_resource_worker_busy() ||
            jobs.export_runtime.is_busy() || jobs.metadata_asset_runtime.import_pending() ||
            session.workspace.selected_chart != action.audio_slicer->chart) {
            platform.notification_sink.post(notification_severity_t::warning, notification_source_t::import_,
                "Save the project and wait for current file jobs before slicing.");
            return;
        }
        const auto started = jobs.audio_slicer_runtime.start(session.history.document,
            *action.audio_slicer, session.project_identity.asset_base);
        platform.notification_sink.post(started ? notification_severity_t::info : notification_severity_t::warning,
            notification_source_t::import_, started ? "Audio slicing started." : started.error().message);
    }

    void dispatch_preview_audio_export(const wormedit::ui::workspace_action_t& action) {
        if (!action.preview_audio_export) return;
        if (jobs.is_resource_worker_busy() || jobs.export_runtime.is_busy() || jobs.metadata_asset_runtime.import_pending()) {
            platform.notification_sink.post(notification_severity_t::warning, notification_source_t::export_,
                "Wait for the current import or export to finish.");
            return;
        }
        const auto* base = SDL_GetBasePath();
        const auto started = jobs.preview_audio_runtime.start(session.history.document,
            *action.preview_audio_export, session.project_identity.asset_base,
            preferences.program_preferences.preferences.audio.osu_skin_directory,
            std::filesystem::path{base ? base : ""} / "assets/hitsounds");
        platform.notification_sink.post(started ? notification_severity_t::info : notification_severity_t::warning,
            notification_source_t::export_, started ? "Preview audio export started." : started.error().message);
    }

    void dispatch_confirmed_workspace_actions(
        double viewport_delta_seconds, wormedit::ui::workspace_action_t& action,
        const std::optional<std::uint32_t>& selected_waveform_sample_rate, double viewport_height) {
        dispatch_lane_keys(action);
        dispatch_options_page_action(action.options);
        dispatch_preview_audio_export(action);
        dispatch_audio_slicer(action);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        const auto& skin = preferences.program_preferences.preferences.audio.osu_skin_directory;
        sync_hitsound_monitoring();
        if (audio_runtime.project_sources.hitsounds.environment.skin_directory != skin) {
            audio_runtime.project_sources.hitsounds.environment.skin_directory = skin;
            audio_runtime.preview_state.invalidate();
        }
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
            dispatch_external_player_action(preferences.options_runtime,
                                            action.external_player, commands,
                                            preferences.program_preferences.preferences) ||
            dispatch_chart_import_action(action);
        const auto previous_selected_chart = session.workspace.selected_chart;
        const bool project_handled =
            (!shortcut_handled && !external_handled && dispatch_project_settings_action(action.project_settings)) ||
            dispatch_project_chart_action(action, shortcut_handled || external_handled);
        remember_created_chart_snap(commands, project_handled, action.project_chart.kind,
                                    previous_selected_chart, session.workspace.selected_chart, session.history.document.settings);
        apply_compare_scroll_action(views.compare_scene_cache, action);
        dispatch_workspace_frame_action(action, project_handled, selected_waveform_sample_rate,
                                        viewport_height);
    }

    void dispatch_frame_action(double viewport_delta_seconds, frontend_workspace_view_t& flattened,
                               wormedit::ui::workspace_action_t& action) {
        {

            wormedit::sys::frame_phase_scope_t actions(
                wormedit::sys::frame_phase_t::workspace_actions);
            std::optional<std::uint32_t> selected_waveform_sample_rate;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            selected_waveform_sample_rate = selected_waveform_rate();
#endif

            if (action.file_jobs.cancel_open) jobs.project_open_runtime.cancel();
            if (action.file_jobs.cancel_export) {
                if (jobs.audio_slicer_runtime.is_busy()) jobs.audio_slicer_runtime.cancel();
                else if (jobs.preview_audio_runtime.is_busy()) jobs.preview_audio_runtime.cancel();
                else jobs.export_runtime.cancel();
            }
            if (action.file_jobs.cancel_preview) views.project_chart_runtime.request_cancel_preview();
            if (jobs.project_open_runtime.is_busy()) return;
            if (dispatch_close_confirmation(
                    views.pending_close, action,
                    preferences.program_preferences.preferences.general.confirm_unsaved_close))
                return;
            dispatch_confirmed_workspace_actions(viewport_delta_seconds, action,
                                                 selected_waveform_sample_rate,
                                                 flattened.view.compose.viewport_height_pixels);
        }
    }

    void compose_frame_ui(frontend_workspace_view_t& flattened, double viewport_delta_seconds) {
        const auto frame_nuklear_previous =
            wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::nuklear);
        refresh_application_command_states(command_registry, session.history, session.workspace,
                                           views.compose_interaction_state, commands);
        wormedit::sys::build_menu_commands_view(command_registry, flattened.view, command_menu_shortcuts);
        build_command_menu_view(command_registry, command_menu_shortcuts, flattened.view);
#if defined(WORMEDIT_HAS_MACOS_MENU)
        wormedit::sys::refresh_macos_menu_shortcuts(command_registry);
#endif
        const auto project_directory = session.project_identity.asset_base.generic_string();
        worm_ui_frontend_set_project_directory(project_directory.c_str());
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
        auto& flattened = prepare_bound_frame_view(frame.model);
        auto& compare_first_pane = views.compare_first_frame;
        views.projection_feedback.publish(
            {session.history.document.id, session.history.document.revision,
             session.workspace.selected_chart.value_or(wormedit::chart_id_t{}),
             views.project_chart_runtime.session_generation},
            flattened.projection_feedback, platform.notification_sink);
        auto& compare_second_pane = views.compare_second_frame;
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
        if (platform.display_watchdog) {
            auto* window = platform.lifetime.runtime.window.handle;
            const auto presentation = wormedit::common::window_presentation_t::from(window);
            const auto pending = !presentation.is_inactive() && presentation.is_pending();
            platform.display_watchdog->record_frame(SDL_GetTicksNS() - frame_start_ns, pending);
        }
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
        if (platform.display_watchdog) {
            auto* window = platform.lifetime.runtime.window.handle;
            platform.display_watchdog->publish_window(SDL_GetWindowFlags(window),
                SDL_GetKeyboardFocus() == window);
        }
        poll_frame_events();
        import_frame_drops();
        const auto viewport_delta = poll_frame_jobs();
        wormedit::sys::frame_timing().restore(wormedit::sys::frame_phase_t::count);
        service_frame_audio(wormedit::sys::frame_timing().expected_presentation_ns(frame_start_ns));
        const auto field_skin_lanes = configure_frame_field_skin();
        workspace_view_outcome_t model;
        prepare_frame_model(model);
        if (!handle_workspace_view_error(
                model, notification_now,
                preferences.program_preferences.preferences.general.confirm_unsaved_close)) {
            compose_bound_frame({*model.value, notification_now, field_skin_lanes, viewport_delta});
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

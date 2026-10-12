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
using wormedit::sys::stage_bmson_bgm_marker;
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
        application_state.requested_snap_denominators.insert_or_assign(chart.value, denominator);
    }
}

template <typename Kind>
void remember_created_chart_snap(application_command_state_t& state, bool handled,
                                 Kind kind,
                                 std::optional<wormedit::chart_id_t> previous,
                                 std::optional<wormedit::chart_id_t> selected) {
    if (handled && kind == WORM_UI_PROJECT_CHART_NEW && selected && selected != previous) {
        remember_snap_denominator(state, *selected, state.editor_preferences.default_snap);
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
auto audio_frame_to_milliseconds(std::uint64_t frame, std::uint32_t sample_rate)
    -> std::optional<std::int64_t> {
    if (sample_rate == 0) {
        return std::nullopt;
    }
    const auto whole_seconds = frame / sample_rate;
    const auto remaining_frames = frame % sample_rate;
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (whole_seconds > maximum / 1'000) {
        return std::nullopt;
    }
    const auto milliseconds = whole_seconds * 1'000 + remaining_frames * 1'000 / sample_rate;
    if (milliseconds > maximum) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(milliseconds);
}

auto audio_milliseconds_to_frame(std::int64_t milliseconds, std::uint32_t sample_rate)
    -> std::uint64_t {
    if (milliseconds <= 0 || sample_rate == 0) {
        return 0;
    }
    const auto unsigned_milliseconds = static_cast<std::uint64_t>(milliseconds);
    const auto whole_seconds = unsigned_milliseconds / 1'000;
    const auto remainder = unsigned_milliseconds % 1'000;
    if (whole_seconds > std::numeric_limits<std::uint64_t>::max() / sample_rate) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const auto base = whole_seconds * sample_rate;
    const auto fractional_frames = (remainder * sample_rate + 999) / 1'000;
    if (fractional_frames > std::numeric_limits<std::uint64_t>::max() - base) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return base + fractional_frames;
}
#endif

auto timing_stage_grid_step(const wormedit::chart_document_t& chart,
                            std::uint64_t active_denominator) -> std::uint64_t {
    // A snap the chart's resolution cannot represent falls back to one standard measure.
    if (active_denominator == 0 || chart.divisions_per_standard_measure % active_denominator != 0) {
        return chart.divisions_per_standard_measure;
    }
    return chart.divisions_per_standard_measure / active_denominator;
}

auto timing_position_from_fraction(const wormedit::chart_document_t& chart, double fraction,
                                   std::uint64_t active_denominator)
    -> std::optional<wormedit::local_position_t> {
    if (!std::isfinite(fraction) || fraction < 0.0 || fraction >= 1.0) {
        return std::nullopt;
    }
    const auto measures_result = wormedit::build_measure_index(chart);
    const auto* measures = std::get_if<wormedit::measure_index_t>(&measures_result);
    if (!measures || measures->total_rows == 0) {
        return std::nullopt;
    }
    const auto step = timing_stage_grid_step(chart, active_denominator);
    if (step == 0) {
        return std::nullopt;
    }
    const auto raw_row_value =
        std::min(std::floor(static_cast<long double>(fraction) * measures->total_rows),
                 static_cast<long double>(measures->total_rows - 1));
    const auto raw_row = static_cast<std::uint64_t>(raw_row_value);
    const auto usable_denominator =
        step == chart.divisions_per_standard_measure ? std::uint64_t{1} : active_denominator;
    const auto snapped_row = wormedit::snap_canvas_row(chart, *measures, raw_row,
        usable_denominator, measures->total_rows).value_or(raw_row);
    return timing_position_at_row(*measures, snapped_row);
}

auto timing_lane_kind_from_ui(WormUiTimingLaneKind lane)
    -> std::optional<wormedit::timing_lane_kind_t> {
    using kind_t = wormedit::timing_lane_kind_t;
    switch (lane) {
    case WORM_UI_TIMING_LANE_BPM:
        return kind_t::bpm;
    case WORM_UI_TIMING_LANE_SCROLL:
        return kind_t::scroll;
    case WORM_UI_TIMING_LANE_SPEED:
        return kind_t::speed;
    case WORM_UI_TIMING_LANE_STOP:
        return kind_t::stop;
    case WORM_UI_TIMING_LANE_DELAY:
        return kind_t::delay;
    case WORM_UI_TIMING_LANE_WARP:
        return kind_t::warp;
    case WORM_UI_TIMING_LANE_KIAI:
        return kind_t::kiai;
    case WORM_UI_TIMING_LANE_ANCHOR:
        return kind_t::anchor;
    case WORM_UI_TIMING_LANE_COUNT:
        break;
    }
    return std::nullopt;
}


auto timing_pause_unit_from_ui(WormUiTimingPauseUnit unit)
    -> std::optional<wormedit::pause_unit_t> {
    using unit_t = wormedit::pause_unit_t;
    switch (unit) {
    case WORM_UI_TIMING_PAUSE_SECONDS:
        return unit_t::seconds;
    case WORM_UI_TIMING_PAUSE_MILLISECONDS:
        return unit_t::milliseconds;
    case WORM_UI_TIMING_PAUSE_BMS_STOP_TICKS:
        return unit_t::bms_stop_ticks;
    case WORM_UI_TIMING_PAUSE_BEATS:
        return unit_t::beats;
    }
    return std::nullopt;
}

auto initial_timing_value_for_stage(const wormedit::chart_document_t& chart,
                                    wormedit::timing_lane_kind_t kind,
                                    wormedit::local_position_t position,
                                    std::uint64_t active_snap_denominator)
    -> std::optional<wormedit::timing_lane_value_t> {
    using namespace wormedit;
    switch (kind) {
    case timing_lane_kind_t::bpm:
        return timing_bpm_value_t{
            effective_bpm_at(chart, position).value_or(signed_rational_t{120, 1})};
    case timing_lane_kind_t::scroll:
        return timing_scroll_value_t{{1, 1}};
    case timing_lane_kind_t::speed:
        return timing_speed_value_t{{1, 1}, {0, 1}, false};
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay:
        return timing_pause_value_t{{1, 1}, pause_unit_t::beats};
    case timing_lane_kind_t::warp: {
        const auto measures_result = build_measure_index(chart);
        const auto* measures = std::get_if<measure_index_t>(&measures_result);
        const auto step = timing_stage_grid_step(chart, active_snap_denominator);
        const auto start = measures ? absolute_row(*measures, position) : std::nullopt;
        if (!measures || !start || step == 0 ||
            step > std::numeric_limits<std::uint64_t>::max() - *start ||
            *start + step >= measures->total_rows) {
            return std::nullopt;
        }
        const auto end = timing_position_at_row(*measures, *start + step);
        if (!end) {
            return std::nullopt;
        }
        return timing_warp_value_t{*end, warp_provenance_t::native};
    }
    case timing_lane_kind_t::kiai:
        return timing_kiai_value_t{true, 0};
    case timing_lane_kind_t::anchor:
        return timing_anchor_value_t{0, false};
    }
    return std::nullopt;
}

auto begin_timing_stage(const WormUiWorkspaceAction& action,
                        const wormedit::document_history_t& history,
                        const wormedit::workspace_controller_t& workspace, bool timing_locked,
                        std::uint64_t active_snap_denominator)
    -> wormedit::timing_lane_pseudonote_preview_t {
    using namespace wormedit;
    timing_lane_pseudonote_preview_t rejected{};
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart || action.timing_chart_id == 0 ||
        workspace.selected_chart->value != action.timing_chart_id ||
        history.document.revision != action.timing_document_revision) {
        return rejected;
    }
    const auto requested_kind = timing_lane_kind_from_ui(action.timing_lane);
    if (!requested_kind) {
        return rejected;
    }
    if (action.timing_object_id != 0) {
        auto preview = begin_existing_timing_lane_pseudonote(
            history, chart_id_t{action.timing_chart_id}, *requested_kind,
            object_id_t{action.timing_object_id}, timing_locked);
        if (preview.status == timing_lane_pseudonote_status_t::editing &&
            action.timing_has_target_y) {
            const auto chart =
                std::ranges::find(history.document.charts, preview.chart, &chart_document_t::id);
            const auto target =
                chart == history.document.charts.end()
                    ? std::optional<local_position_t>{}
                    : timing_position_from_fraction(
                          *chart, wormedit::sys::timing_target_fraction(action.timing_target),
                          active_snap_denominator);
            if (!target ||
                !move_existing_timing_lane_pseudonote(history, preview, *target, timing_locked)) {
                return rejected;
            }
        }
        return preview;
    }
    if (!action.timing_has_target_y) {
        return rejected;
    }
    const auto found = std::ranges::find(history.document.charts,
                                         chart_id_t{action.timing_chart_id}, &chart_document_t::id);
    if (found == history.document.charts.end()) {
        return rejected;
    }
    const auto position = timing_position_from_fraction(
        *found, wormedit::sys::timing_target_fraction(action.timing_target),
        active_snap_denominator);
    if (!position) {
        return rejected;
    }

    const auto kind = *requested_kind;
    auto initial = initial_timing_value_for_stage(*found, kind, *position, active_snap_denominator);
    if (!initial) {
        return rejected;
    }
    return begin_timing_lane_pseudonote(
        history, chart_id_t{action.timing_chart_id}, kind, *position, std::move(*initial),
        timing_lane_initial_value_source_t::explicit_default, timing_locked);
}

auto parse_timing_unsigned(const char* text) -> std::optional<std::uint64_t> {
    if (!text || text[0] == '\0') {
        return std::nullopt;
    }
    const auto* end = text + std::strlen(text);
    std::uint64_t value{};
    const auto parsed = std::from_chars(text, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end ? std::optional{value} : std::nullopt;
}

auto timing_ratio_in_bounds(std::int64_t numerator, std::uint64_t denominator, long double minimum,
                            long double maximum) -> bool {
    if (denominator == 0) {
        return false;
    }
    const auto value = static_cast<long double>(numerator) / denominator;
    return value >= minimum && value <= maximum;
}

auto timing_unsigned_ratio_in_bounds(std::uint64_t numerator, std::uint64_t denominator,
                                     long double maximum) -> bool {
    if (denominator == 0) {
        return false;
    }
    return static_cast<long double>(numerator) / denominator <= maximum;
}

struct TimingStageInput {
    const wormedit::timing_lane_pseudonote_preview_t& preview;
    const WormUiWorkspaceAction& action;
    std::optional<std::int64_t> primary_numerator;
    std::optional<std::uint64_t> primary_unsigned;
    std::optional<std::uint64_t> primary_denominator;
    std::optional<std::int64_t> secondary_numerator;
    std::optional<std::uint64_t> secondary_denominator;
};

auto timing_stage_bpm_value(const TimingStageInput& input)
    -> std::optional<wormedit::timing_lane_value_t> {
    using namespace wormedit;
    const auto& preview = input.preview;
    const auto& action = input.action;
    if (action.timing_bpm_step != 0) {
        const auto* current = std::get_if<timing_bpm_value_t>(&preview.value);
        const auto direction = action.timing_bpm_step == -1
                                   ? std::optional{bpm_step_direction_t::decrement}
                               : action.timing_bpm_step == 1
                                   ? std::optional{bpm_step_direction_t::increment}
                                   : std::nullopt;
        const auto stepped = current && direction
                                 ? step_bpm_by_one(current->beats_per_minute, *direction)
                                 : std::nullopt;
        if (!stepped || (!preview.existing_event &&
                         !timing_ratio_in_bounds(stepped->numerator, stepped->denominator,
                                                 1.0L, 1000.0L))) {
            return std::nullopt;
        }
        return timing_lane_value_t{timing_bpm_value_t{*stepped}};
    }
    if (action.timing_bpm_decimal_edited) {
        const auto bpm = parse_bpm_text(action.timing_bpm_decimal_text);
        if (!bpm || (!preview.existing_event &&
                     !timing_ratio_in_bounds(bpm->numerator, bpm->denominator, 1.0L, 1000.0L))) {
            return std::nullopt;
        }
        return timing_lane_value_t{timing_bpm_value_t{*bpm}};
    }
    if (!input.primary_numerator || !input.primary_denominator || *input.primary_numerator <= 0 ||
        (!preview.existing_event &&
         !timing_ratio_in_bounds(*input.primary_numerator, *input.primary_denominator,
                                 1.0L, 1000.0L))) {
        return std::nullopt;
    }
    return timing_lane_value_t{
        timing_bpm_value_t{{*input.primary_numerator, *input.primary_denominator}}};
}

auto timing_stage_scroll_value(const TimingStageInput& input)
    -> std::optional<wormedit::timing_lane_value_t> {
    using namespace wormedit;
    if (!input.primary_numerator || !input.primary_denominator ||
        (!input.preview.existing_event &&
         !timing_ratio_in_bounds(*input.primary_numerator, *input.primary_denominator,
                                 -1000.0L, 1000.0L))) {
        return std::nullopt;
    }
    return timing_lane_value_t{
        timing_scroll_value_t{{*input.primary_numerator, *input.primary_denominator}}};
}

auto timing_stage_speed_value(const TimingStageInput& input)
    -> std::optional<wormedit::timing_lane_value_t> {
    using namespace wormedit;
    if (!input.primary_numerator || !input.primary_denominator || !input.secondary_numerator ||
        *input.secondary_numerator < 0 || !input.secondary_denominator ||
        *input.primary_numerator < 0 ||
        (!input.preview.existing_event &&
         (!timing_ratio_in_bounds(*input.primary_numerator, *input.primary_denominator,
                                  0.0L, 1000.0L) ||
          !timing_unsigned_ratio_in_bounds(
              static_cast<std::uint64_t>(*input.secondary_numerator),
              *input.secondary_denominator, 10000.0L)))) {
        return std::nullopt;
    }
    return timing_lane_value_t{timing_speed_value_t{
        {*input.primary_numerator, *input.primary_denominator},
        {static_cast<std::uint64_t>(*input.secondary_numerator), *input.secondary_denominator},
        input.action.timing_transition_is_seconds}};
}

auto timing_pause_maximum_duration(std::optional<wormedit::pause_unit_t> unit) -> long double {
    if (!unit) {
        return 0.0L;
    }
    switch (*unit) {
    case wormedit::pause_unit_t::seconds:
        return 86'400.0L;
    case wormedit::pause_unit_t::milliseconds:
        return 86'400'000.0L;
    case wormedit::pause_unit_t::bms_stop_ticks:
    case wormedit::pause_unit_t::beats:
        return 1'000'000.0L;
    }
    return 0.0L;
}

auto timing_stage_pause_value(const TimingStageInput& input)
    -> std::optional<wormedit::timing_lane_value_t> {
    using namespace wormedit;
    const auto unit = timing_pause_unit_from_ui(input.action.timing_pause_unit);
    const auto maximum = timing_pause_maximum_duration(unit);
    if (!input.primary_numerator || !input.primary_denominator || *input.primary_numerator < 0 ||
        !unit || (!input.preview.existing_event &&
                  !timing_unsigned_ratio_in_bounds(
                      static_cast<std::uint64_t>(*input.primary_numerator),
                      *input.primary_denominator, maximum))) {
        return std::nullopt;
    }
    return timing_lane_value_t{
        timing_pause_value_t{{*input.primary_numerator, *input.primary_denominator}, *unit}};
}

auto timing_stage_warp_value(const TimingStageInput& input,
                             const wormedit::chart_document_t& chart,
                             std::uint64_t active_snap_denominator)
    -> std::optional<wormedit::timing_lane_value_t> {
    using namespace wormedit;
    if (!input.secondary_numerator || *input.secondary_numerator < 1 ||
        !input.secondary_denominator || *input.secondary_denominator != 1 ||
        (!input.preview.existing_event && *input.secondary_numerator > 1'000'000)) {
        return std::nullopt;
    }
    const auto units = static_cast<std::uint64_t>(*input.secondary_numerator);
    const auto measures = build_measure_index(chart);
    const auto* index = std::get_if<measure_index_t>(&measures);
    const auto start = index ? absolute_row(*index, input.preview.position) : std::nullopt;
    const auto step = input.preview.existing_event ? std::uint64_t{1}
                                                   : timing_stage_grid_step(chart,
                                                                            active_snap_denominator);
    if (!index || !start || step == 0 ||
        units > (std::numeric_limits<std::uint64_t>::max() - *start) / step) {
        return std::nullopt;
    }
    const auto end_row = *start + units * step;
    if (end_row >= index->total_rows) {
        return std::nullopt;
    }
    const auto end = timing_position_at_row(*index, end_row);
    return end ? std::optional<timing_lane_value_t>{timing_warp_value_t{
                     *end, warp_provenance_t::native}}
               : std::nullopt;
}

auto timing_stage_toggle_value(const TimingStageInput& input)
    -> std::optional<wormedit::timing_lane_value_t> {
    return wormedit::timing_lane_value_t{wormedit::timing_kiai_value_t{
        input.action.timing_toggle_primary,
        std::get<wormedit::timing_kiai_value_t>(input.preview.value).retained_effect_bits}};
}

auto timing_stage_anchor_value(const TimingStageInput& input)
    -> std::optional<wormedit::timing_lane_value_t> {
    using namespace wormedit;
    if (!input.primary_unsigned || !input.primary_denominator || *input.primary_denominator != 1) {
        return std::nullopt;
    }
    return timing_lane_value_t{
        timing_anchor_value_t{*input.primary_unsigned, input.action.timing_toggle_primary}};
}

auto timing_stage_value(const wormedit::timing_lane_pseudonote_preview_t& preview,
                        const wormedit::chart_document_t& chart,
                        const WormUiWorkspaceAction& action, std::uint64_t active_snap_denominator)
    -> std::optional<wormedit::timing_lane_value_t> {
    using namespace wormedit;
    const TimingStageInput input{
        preview,
        action,
        parse_timing_signed(action.timing_primary_numerator_text),
        parse_timing_unsigned(action.timing_primary_numerator_text),
        parse_timing_unsigned(action.timing_primary_denominator_text),
        parse_timing_signed(action.timing_secondary_numerator_text),
        parse_timing_unsigned(action.timing_secondary_denominator_text),
    };
    switch (preview.kind) {
    case timing_lane_kind_t::bpm:
        return timing_stage_bpm_value(input);
    case timing_lane_kind_t::scroll:
        return timing_stage_scroll_value(input);
    case timing_lane_kind_t::speed:
        return timing_stage_speed_value(input);
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay:
        return timing_stage_pause_value(input);
    case timing_lane_kind_t::warp:
        return timing_stage_warp_value(input, chart, active_snap_denominator);
    case timing_lane_kind_t::kiai:
        return timing_stage_toggle_value(input);
    case timing_lane_kind_t::anchor:
        return timing_stage_anchor_value(input);
    }
    return std::nullopt;
}

struct AudioOffsetDragContext {
    const WormUiWorkspaceAction& action;
    wormedit::document_history_t& history;
    wormedit::workspace_controller_t& workspace;
    bool timing_locked;
    std::optional<std::uint32_t> selected_sample_rate;
    std::optional<wormedit::audio_offset_drag_t>& drag;
    std::optional<wormedit::workspace_tab_t>& drag_tab;
    wormedit::notification_sink_t& notifications;
};

auto post_audio_offset_feedback(AudioOffsetDragContext& context,
    wormedit::notification_severity_t severity, std::string message) -> std::string {
    return post_callback_feedback(context.notifications, severity,
                                  wormedit::notification_source_t::edit, std::move(message));
}

auto post_audio_offset_diagnostic(AudioOffsetDragContext& context, const auto& diagnostics,
    std::string fallback, wormedit::notification_severity_t fallback_severity) -> std::string {
    if (!diagnostics.empty())
        return post_audio_offset_feedback(context, notification_severity_for(diagnostics.front().severity),
                                          diagnostics.front().message);
    return post_audio_offset_feedback(context, fallback_severity, std::move(fallback));
}

auto clear_audio_offset_drag(AudioOffsetDragContext& context, std::string message,
    wormedit::notification_severity_t severity = wormedit::notification_severity_t::warning) -> std::string {
    context.drag.reset();
    context.drag_tab.reset();
    return post_audio_offset_feedback(context, severity, std::move(message));
}

auto audio_offset_drag_context_matches(const AudioOffsetDragContext& context) -> bool {
    using namespace wormedit;
    const auto supported_tab = context.workspace.active_tab == workspace_tab_t::audio_slices ||
                               context.workspace.active_tab == workspace_tab_t::metadata ||
                               context.workspace.active_tab == workspace_tab_t::compose;
    const auto context_matches =
        supported_tab && context.workspace.selected_chart &&
        context.workspace.selected_chart->value == context.action.audio_offset_drag_chart_id &&
        context.history.document.revision == context.action.audio_offset_drag_document_revision &&
        context.selected_sample_rate &&
        *context.selected_sample_rate == context.action.audio_offset_drag_sample_rate;
    return context_matches;
}

auto nudge_audio_offset_drag(AudioOffsetDragContext& context) -> std::string {
    using namespace wormedit;
    if (context.drag) {
            return post_audio_offset_feedback(context, notification_severity_t::warning,
                "Finish or cancel the active audio offset drag before nudging.");
    }
    const auto begun = begin_audio_offset_drag(context.history.document,
                                               *context.workspace.selected_chart,
                                               *context.selected_sample_rate,
                                               context.timing_locked);
        if (!begun) {
            return post_audio_offset_diagnostic(context, begun.diagnostics, "Audio offset nudge could not start.",
                notification_severity_t::warning);
        }
        const auto updated = update_audio_offset_drag(
            context.history.document, *begun.drag,
            context.action.audio_offset_drag_delta_frames, context.timing_locked);
        if (!updated) {
            return post_audio_offset_diagnostic(context, updated.diagnostics, "Audio offset nudge was rejected.",
                notification_severity_t::warning);
        }
        const auto committed = commit_audio_offset_drag(context.history, *updated.drag,
                                                        context.timing_locked);
        if (!committed.applied) {
            return post_audio_offset_diagnostic(context, committed.diagnostics, "Audio offset was unchanged.",
                notification_severity_t::info);
        }
        const auto rebound = rebind_workspace(context.workspace, context.history);
        return post_audio_offset_feedback(context, rebound ? notification_severity_t::success
                                                          : notification_severity_t::error,
            rebound ? "Audio offset nudged by an exact frame count."
                    : "Audio offset changed, but workspace rebinding failed.");
}

auto begin_audio_offset_drag_action(AudioOffsetDragContext& context) -> std::string {
    using namespace wormedit;
    const auto begun = begin_audio_offset_drag(context.history.document,
                                               *context.workspace.selected_chart,
                                               *context.selected_sample_rate,
                                               context.timing_locked);
    context.drag = begun.drag;
    context.drag_tab = begun ? std::optional{context.workspace.active_tab} : std::nullopt;
        if (begun) return post_audio_offset_feedback(context, notification_severity_t::hint,
        "Dragging waveform offset; release to apply.");
    return post_audio_offset_diagnostic(context, begun.diagnostics, "Audio offset drag could not start.",
                                       notification_severity_t::warning);
}

auto continue_audio_offset_drag(AudioOffsetDragContext& context) -> std::string {
    using namespace wormedit;
    const auto& action = context.action;
    auto& history = context.history;
    auto& workspace = context.workspace;
    auto& drag = context.drag;
    auto& drag_tab = context.drag_tab;
    const auto timing_locked = context.timing_locked;
    if (!drag || !drag_tab || *drag_tab != workspace.active_tab ||
        drag->chart != *workspace.selected_chart ||
        drag->document_revision != action.audio_offset_drag_document_revision ||
        drag->sample_rate != *context.selected_sample_rate) {
        return clear_audio_offset_drag(context,
                                       "Audio offset drag has no matching captured transaction.");
    }
    const auto updated = update_audio_offset_drag(
        history.document, *drag, action.audio_offset_drag_delta_frames, timing_locked);
    if (!updated) {
        drag.reset();
        drag_tab.reset();
        return post_audio_offset_diagnostic(context, updated.diagnostics, "Audio offset preview was rejected.",
                notification_severity_t::warning);
    }
    drag = updated.drag;
    if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_UPDATE)
        return post_audio_offset_feedback(context, notification_severity_t::hint, "Previewing exact waveform offset.");
    if (action.audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_COMMIT) {
        drag.reset();
        drag_tab.reset();
        return post_audio_offset_feedback(context, notification_severity_t::warning, "Invalid audio offset drag action.");
    }
    const auto committed = commit_audio_offset_drag(history, *drag, timing_locked);
    drag.reset();
    drag_tab.reset();
    if (!committed.applied) {
        return post_audio_offset_diagnostic(context, committed.diagnostics, "Audio offset was unchanged.",
                notification_severity_t::info);
    }
    const auto rebound = rebind_workspace(workspace, history);
    if (rebound) return post_audio_offset_feedback(context, notification_severity_t::success,
        "Audio offset updated from waveform drag.");
    return post_audio_offset_diagnostic(context, rebound.diagnostics,
        "Audio offset changed, but workspace rebinding failed.", notification_severity_t::error);
}

auto dispatch_audio_offset_drag_action(const WormUiWorkspaceAction& action,
                                       wormedit::document_history_t& history,
                                       wormedit::workspace_controller_t& workspace,
                                       bool timing_locked,
                                       std::optional<std::uint32_t> selected_sample_rate,
                                       std::optional<wormedit::audio_offset_drag_t>& drag,
                                       std::optional<wormedit::workspace_tab_t>& drag_tab,
                                       wormedit::notification_sink_t& notifications)
    -> std::string {
    AudioOffsetDragContext context{action, history, workspace, timing_locked,
                                   selected_sample_rate, drag, drag_tab, notifications};
    if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_CANCEL)
        return clear_audio_offset_drag(context, "Audio offset drag cancelled.",
                                      wormedit::notification_severity_t::hint);
    if (!audio_offset_drag_context_matches(context))
        return clear_audio_offset_drag(context,
                                       "Audio offset drag expired after its chart or waveform changed.");
    if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_NUDGE)
        return nudge_audio_offset_drag(context);
    if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_BEGIN)
        return begin_audio_offset_drag_action(context);
    return continue_audio_offset_drag(context);
}

auto inline_preview_active(const application_command_state_t& state) -> bool {
    return state.transport_inline_active && state.transport_inline_active();
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

auto apply_loop_selection(auto& history, auto& workspace, auto& interaction_state,
    auto& inline_preview_cursor, auto& preview_session,
    auto& preview_state, auto& audition_sources, auto& metronome_bindings,
    auto& audition_output, auto& preview_output, wormedit::chart_id_t chart_id,
    std::uint64_t expected_revision, std::uint64_t begin_row,
    std::uint64_t end_row, bool enabled) -> bool {
    if (!workspace.selected_chart || *workspace.selected_chart != chart_id ||
        history.document.revision != expected_revision) return false;
    if (!enabled) return preview_output.set_loop_region(std::nullopt);
    const auto* chart = selected_chart(history, workspace);
    if (workspace.active_tab != wormedit::workspace_tab_t::compose || !chart ||
        chart->id != chart_id || !interaction_state || interaction_state->chart != chart_id ||
        !interaction_state->retained_selection_range) return false;
    const auto region = loop_region_for_selection(*chart,
        *interaction_state->retained_selection_range, begin_row, end_row);
    if (!region || (preview_output.playing() &&
                    !preview_state.matches_selection(history.document, workspace)))
        return false;
    if (!preview_output.set_loop_region(region)) return false;
    if (preview_output.playing()) {
        const auto sought = preview_output.seek(region->begin_frame);
        if (!sought) {
            static_cast<void>(preview_output.set_loop_region(std::nullopt));
            return false;
        }
        preview_state.resume_frame = region->begin_frame;
        preview_state.paused = false;
    } else {
        const auto previous_frame = preview_state.resume_frame;
        const auto was_paused = preview_state.paused;
        preview_state.resume_frame = region->begin_frame;
        preview_state.paused = false;
        static_cast<void>(preview_state.play_pause(chart, history.document, audition_sources,
            metronome_bindings, audition_output, preview_session, preview_output,
            std::nullopt, false, region->begin_frame));
        if (!preview_output.playing()) {
            preview_state.resume_frame = previous_frame;
            preview_state.paused = was_paused;
            static_cast<void>(preview_output.set_loop_region(std::nullopt));
            return false;
        }
    }
    inline_preview_cursor.capture(interaction_state ? &*interaction_state : nullptr);
    inline_preview_cursor.capture_follow_anchor(history,
        interaction_state ? &*interaction_state : nullptr);
    return true;
}

void configure_transport_play_pause(auto& state, auto& history, auto& workspace,
                                 auto& compose_interaction_state, auto& inline_preview_cursor,
                                 auto& audition_session, auto& preview_session, auto& preview_state,
                                 auto& audition_sources, auto& preview_metronome_bindings,
                                 auto& audition_output, auto& preview_output, auto& notifications) {
    state.transport_play_pause = [&]() -> std::string {
        state.invalidate_playing_seek_records();
        const auto compose_cursor = workspace.active_tab == wormedit::workspace_tab_t::compose &&
                                            compose_interaction_state && workspace.selected_chart &&
                                            compose_interaction_state->chart == *workspace.selected_chart
                                        ? &*compose_interaction_state
                                        : nullptr;
        if (!preview_output.playing() && !preview_state.paused && compose_cursor &&
            compose_cursor->virtual_bms_cursor_row)
            return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
                                          wormedit::notification_source_t::audio,
                                          "Materialize the cursor measure before starting chart preview.");
        const bool starting = !preview_output.playing() && !preview_state.paused;
        const auto exact_start = starting ? wormedit::sys::compose_minimap_start_frame(state, history, compose_cursor) : std::nullopt;
        const auto result = preview_state.play_pause(
            selected_chart(history, workspace), history.document, audition_sources,
            preview_metronome_bindings, audition_output,
            preview_session, preview_output,
            compose_cursor ? std::optional{compose_cursor->cursor} : std::nullopt, false, exact_start,
            compose_cursor ? compose_cursor->cursor_subrow : 0.0);
        if (starting && preview_output.playing()) {
            state.minimap_start_frame.reset();
            inline_preview_cursor.capture(compose_cursor);
        }
        if (preview_output.playing()) inline_preview_cursor.capture_follow_anchor(history, compose_cursor);
        return post_callback_feedback(notifications,
            routine_transport_severity(preview_state.feedback.severity, preview_state.feedback.accepted),
            wormedit::notification_source_t::audio, result);
    };
}

void configure_transport_seek_commands(auto& state, auto& history, auto& workspace,
    auto& compose_interaction_state, auto& compose_scene_cache, auto& audition_output,
    auto& preview_session, auto& preview_state, auto& preview_output, auto& notifications) {
    state.transport_seek_posts_feedback = true;
    state.transport_seek = [&](std::uint64_t frame) -> std::string {
        if (!preview_output.playing() && !preview_state.paused &&
            workspace.active_tab == wormedit::workspace_tab_t::compose &&
            compose_interaction_state && workspace.selected_chart &&
            compose_interaction_state->chart == *workspace.selected_chart) {
            if (wormedit::sys::editor_interaction_has_pending_gesture(*compose_interaction_state))
                return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
                    wormedit::notification_source_t::input, "Finish or cancel the active Compose gesture before seeking.");
            return wormedit::sys::seek_stopped_compose_transport(
                history, *compose_interaction_state, compose_scene_cache, frame, notifications);
        }
        const auto result = preview_state.seek(frame, history.document, workspace, audition_output,
                                               preview_session, preview_output);
        return post_callback_feedback(notifications, preview_state.feedback.severity,
                                      wormedit::notification_source_t::audio, result);
    };
    state.transport_audio_milliseconds = [&]() {
        return preview_state.audio_milliseconds(history.document, workspace, preview_session);
    };
    state.transport_seek_audio_milliseconds = [&](std::int64_t milliseconds) -> std::string {
        const auto result = preview_state.seek_audio_milliseconds(
            milliseconds, history.document, workspace, audition_output,
            preview_session, preview_output);
        return post_callback_feedback(notifications, preview_state.feedback.severity,
                                      wormedit::notification_source_t::audio, result);
    };
}

void sync_bgm_mix_selection(const auto& state, const auto& history, const auto& workspace,
                             auto& preview_state, auto& preview_session) {
    const auto* chart = wormedit::sys::selected_chart(history, workspace);
    const auto found = chart ? state.bgm_lane_mix.find(chart->id.value) : state.bgm_lane_mix.end();
    const auto muted = chart && found != state.bgm_lane_mix.end()
        ? wormedit::sys::compose_bgm_muted_lanes(*chart, found->second) : std::vector<std::uint64_t>{};
    preview_state.set_bgm_mix(chart ? chart->id : wormedit::chart_id_t{}, muted, preview_session);
}

void sync_layer_assist_selection(const auto& state, const auto& workspace,
                                 auto& preview_state, auto& preview_session) {
    if (state.layer_assist_chart == workspace.selected_chart) {
        (void)preview_state.set_layer_assist(workspace.selected_chart.value_or(wormedit::chart_id_t{}),
                                            state.layer_assist_layers, preview_session);
    } else {
        (void)preview_state.set_layer_assist(workspace.selected_chart.value_or(wormedit::chart_id_t{}),
                                            {}, preview_session);
    }
}

void configure_bgm_mix_command(auto& state, auto& workspace, auto& preview_state,
                               auto& preview_session) {
    state.transport_set_bgm_mix = [&](wormedit::chart_id_t chart, std::span<const std::uint64_t> muted) {
        if (workspace.selected_chart != chart) return false;
        preview_state.set_bgm_mix(chart, muted, preview_session);
        return true;
    };
}

void configure_transport_commands(auto& state, auto& history, auto& workspace,
                                 auto& compose_interaction_state, auto& compose_scene_cache,
                                 auto& inline_preview_cursor,
                                 auto& audition_session, auto& preview_session, auto& preview_state,
                                 auto& audition_sources, auto& preview_metronome_bindings,
                                 auto& audition_output, auto& preview_output, auto& notifications) {
    state.transport_metronome_available = preview_output.available() && !preview_metronome_bindings.empty();
    state.transport_set_layer_assist = [&](wormedit::chart_id_t chart, std::span<const wormedit::layer_id_t> layers) {
        return workspace.selected_chart == chart && preview_state.set_layer_assist(chart, layers, preview_session);
    };
    configure_bgm_mix_command(state, workspace, preview_state, preview_session);
    state.transport_set_metronome = [&](bool enabled) { preview_session.set_metronome_enabled(enabled); };
    preview_session.set_metronome_enabled(state.transport_metronome_enabled);
    state.transport_set_metronome_volume = [&](std::uint8_t volume) {
        (void)preview_session.set_metronome_volume(volume);
    };
    state.transport_set_metronome_count_in = [&](std::uint8_t measures) {
        if (preview_session.set_metronome_count_in(measures))
            preview_state.count_in_measures = measures;
    };
    (void)preview_session.set_metronome_volume(state.metronome_volume);
    (void)preview_session.set_metronome_count_in(state.metronome_count_in);
    preview_state.count_in_measures = state.metronome_count_in;
    state.transport_keep_pitch_available = preview_output.available();
    state.transport_set_keep_pitch = [&](bool enabled) { return preview_output.set_keep_pitch(enabled); };
    state.transport_set_playback_rate = [&](wormedit::rational_t rate) {
        return preview_output.set_playback_rate(rate);
    };
    state.transport_active = [&] {
        return audition_session.active() || preview_output.playing() || preview_state.paused;
    };
    state.transport_preview_playing = [&] { return preview_output.playing(); };
    state.transport_preview_paused = [&] { return preview_state.paused; };
    state.transport_preserve_bookmark_revision =
        [&](wormedit::chart_id_t chart, std::uint64_t previous_revision,
            std::uint64_t next_revision) {
            if (!preview_output.playing() || !workspace.selected_chart ||
                *workspace.selected_chart != chart || preview_state.chart != chart ||
                preview_state.revision != previous_revision)
                return false;
            return preview_state.preserve_bookmark_revision(
                chart, previous_revision, next_revision);
        };
    state.transport_preview_end_frame = [&]() -> std::optional<std::uint64_t> {
        return preview_state.matches_selection(history.document, workspace)
            ? std::optional{preview_state.end_frame} : std::nullopt;
    };
    state.transport_inline_active = [&] {
        return preview_output.playing() || preview_state.paused;
    };
    configure_transport_play_pause(state, history, workspace, compose_interaction_state,
        inline_preview_cursor, audition_session, preview_session, preview_state,
        audition_sources, preview_metronome_bindings, audition_output, preview_output, notifications);
    state.transport_play_from_start = [&]() -> std::string {
        state.invalidate_playing_seek_records();
        state.minimap_start_frame.reset();
        const bool starting = !preview_output.playing() && !preview_state.paused;
        const auto* compose_cursor = workspace.active_tab == wormedit::workspace_tab_t::compose &&
                                             compose_interaction_state && workspace.selected_chart &&
                                             compose_interaction_state->chart == *workspace.selected_chart
                                         ? &*compose_interaction_state
                                         : nullptr;
        const auto result = preview_state.play_pause(
            selected_chart(history, workspace), history.document, audition_sources,
            preview_metronome_bindings, audition_output,
            preview_session, preview_output,
            std::nullopt, true);
        if (starting && preview_output.playing()) inline_preview_cursor.capture(compose_cursor);
        if (preview_output.playing()) inline_preview_cursor.capture_follow_anchor(history, compose_cursor);
        return post_callback_feedback(notifications,
            routine_transport_severity(preview_state.feedback.severity, preview_state.feedback.accepted),
            wormedit::notification_source_t::audio, result);
    };
    configure_transport_seek_commands(state, history, workspace, compose_interaction_state,
        compose_scene_cache, audition_output, preview_session, preview_state, preview_output, notifications);
    state.transport_stop = [&] {
        state.invalidate_playing_seek_records();
        state.minimap_start_frame.reset();
        preview_state.stop(audition_output, preview_output);
        inline_preview_cursor.anchor.reset();
    };
    state.transport_loop_selection_active = [&] {
        return preview_session.has_loop_region();
    };
    state.transport_set_loop_selection = [&](wormedit::chart_id_t chart_id,
        std::uint64_t expected_revision, std::uint64_t begin_row,
        std::uint64_t end_row, bool enabled) {
        return apply_loop_selection(history, workspace, compose_interaction_state,
            inline_preview_cursor, preview_session, preview_state,
            audition_sources, preview_metronome_bindings, audition_output, preview_output,
            chart_id, expected_revision, begin_row, end_row, enabled);
    };
    state.transport_cancel_restore = [&]() -> std::string {
        preview_state.stop(audition_output, preview_output);
        const bool restored = inline_preview_cursor.restore(
            history, compose_interaction_state ? &*compose_interaction_state : nullptr);
        return post_callback_feedback(notifications, wormedit::notification_severity_t::hint,
                                      wormedit::notification_source_t::audio,
                                      restored ? "Chart preview stopped; cursor restored." : "Chart preview stopped.");
    };
}
#endif

void refresh_transport_rate_commands(wormedit::command_registry_t& registry,
                                     const application_command_state_t& state) {
    (void)wormedit::update_command_state(registry, {"transport.toggle_metronome"},
                                       state.transport_metronome_available,
                                       state.transport_metronome_enabled);
    const bool available = static_cast<bool>(state.transport_set_playback_rate);
    (void)wormedit::update_command_state(registry, {"transport.rate_slower"},
                                       available && state.transport_rate_index > 0, std::nullopt);
    (void)wormedit::update_command_state(registry, {"transport.rate_faster"},
                                       available && state.transport_rate_index < 8, std::nullopt);
}

struct external_player_runtime_t {
    wormedit::sys::external_player_t player;
    wormedit::sys::external_player_preferences_t preferences = wormedit::sys::default_external_player_preferences();
    wormedit::sys::program_preferences_runtime_t* program_preferences{};
    std::filesystem::path source_path;
    bool visible{};
    std::uint64_t revision{};
    std::vector<WormUiExternalPlayerDefinitionView> views;
};

void initialize_program_preferences(wormedit::sys::program_preferences_runtime_t& preferences,
    external_player_runtime_t& player, application_command_state_t& state,
    wormedit::notification_sink_t& notifications) {
    player.program_preferences = &preferences;
    player.preferences = preferences.preferences.players;
    state.minimap_click_moves_paused_playhead = preferences.preferences.minimap.click_moves_paused_playhead;
    state.editor_preferences = preferences.preferences.editor;
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
    if (state.transport_set_keep_pitch && state.transport_set_keep_pitch(preferences.audio.keep_pitch))
        state.transport_keep_pitch = preferences.audio.keep_pitch;
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
                                state.minimap_click_moves_paused_playhead;
    candidate.minimap.click_moves_paused_playhead = state.minimap_click_moves_paused_playhead;
    candidate.editor.scroll_directions = state.editor_preferences.scroll_directions;
    if (state.transport_keep_pitch_available && state.transport_set_playback_rate)
        candidate.audio.playback_rate = wormedit::audio::playback_rates[
            std::min(state.transport_rate_index, wormedit::audio::playback_rates.size() - 1)];
    if (state.transport_keep_pitch_available) candidate.audio.keep_pitch = state.transport_keep_pitch;
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

void dispatch_options_frame_action(wormedit::sys::options_runtime_t& options,
    wormedit::sys::program_preferences_runtime_t& program, const WormUiOptionsAction& action,
    external_player_runtime_t& players, application_command_state_t& state,
    wormedit::native_project_autosave_loop_t& autosave, wormedit::notification_sink_t& notifications) {
    const bool accepted = wormedit::sys::apply_options_action(options, program, action);
    worm_ui_frontend_set_snap_grid_opacity(options.visible
        ? options.draft.display.snap_grid_opacity_percent
        : program.preferences.display.snap_grid_opacity_percent);
    worm_ui_frontend_set_field_waveform_opacity(options.visible
        ? options.draft.display.field_waveform_opacity_percent
        : program.preferences.display.field_waveform_opacity_percent);
    update_scratch_presentation(options.visible ? options.draft.display
        : program.preferences.display, state);
    if (options.notification) notifications.post(*options.notification);
    if (!accepted) return;
    if (options.category == WORM_UI_OPTIONS_GENERAL)
        (void)autosave.set_interval(std::chrono::minutes{program.preferences.general.autosave_minutes});
    if (options.category == WORM_UI_OPTIONS_EDITOR) {
        state.editor_preferences = program.preferences.editor;
        state.minimap_click_moves_paused_playhead = program.preferences.minimap.click_moves_paused_playhead;
    }
    if (options.category == WORM_UI_OPTIONS_PLAYERS) {
        players.preferences = program.preferences.players;
        ++players.revision;
    }
    if (options.category == WORM_UI_OPTIONS_AUDIO) initialize_preferred_transport(program.preferences, state);
    if (options.category == WORM_UI_OPTIONS_DISPLAY) {
        worm_ui_frontend_set_snap_colored_grid(program.preferences.display.snap_colored_grid);
    }
}

// Shortcut-editor actions never reach the program-preference runtime.
void dispatch_options_page_action(shortcut_editor_runtime_t& editor, wormedit::sys::options_runtime_t& options,
    wormedit::sys::program_preferences_runtime_t& program, const WormUiOptionsAction& action,
    external_player_runtime_t& players, application_command_state_t& state,
    wormedit::native_project_autosave_loop_t& autosave, wormedit::notification_sink_t& notifications) {
    if (handle_shortcut_editor_action(editor, action, options, notifications)) return;
    dispatch_options_frame_action(options, program, action, players, state, autosave, notifications);
}

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

auto launch_external_player(external_player_runtime_t& runtime,
    wormedit::sys::external_player_action_t action, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    const std::optional<wormedit::editor_interaction_state_t>& interaction,
    const std::filesystem::path& asset_base, wormedit::notification_sink_t& notifications) -> std::string {
    using namespace wormedit::sys;
    if (runtime.preferences.selected >= runtime.preferences.players.size()) {
        runtime.visible = true;
        return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
                                      wormedit::notification_source_t::audio, "Choose an external player in Preferences.");
    }
    const auto& player = runtime.preferences.players[runtime.preferences.selected];
    if (player.path.empty()) {
        runtime.visible = true;
        return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
                                      wormedit::notification_source_t::audio, "Choose an external player in Preferences.");
    }
    const auto* chart = selected_chart(history, workspace);
    if (action != external_player_action_t::stop && (!chart || chart->source_family != wormedit::format_family_t::bms))
        return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
                                      wormedit::notification_source_t::audio, "External preview requires a selected BMS chart.");
    std::uint64_t measure{};
    if (action == external_player_action_t::play_here) {
        const auto source_measure = external_source_measure(*chart, interaction);
        if (!source_measure) return post_callback_feedback(notifications, wormedit::notification_severity_t::hint,
            wormedit::notification_source_t::audio, "Place the Compose cursor before playing externally from here.");
        measure = *source_measure;
    }
    auto source = runtime.source_path;
    if (source.empty() && chart && !asset_base.empty()) {
        // Native projects retain the authoritative resource directory, but no imported basename.
        bool popn{};
        for (const auto& layout : chart->lane_layouts)
            popn = popn || layout.layout.profile_id.starts_with("pms-");
        source = asset_base / (popn ? "chart.pms" : "chart.bms");
    }
    const char* application_path = SDL_GetBasePath();
    const auto result = runtime.player.launch(player, action, history.document,
        chart ? chart->id : wormedit::chart_id_t{}, source, measure,
        application_path ? application_path : "");
    if (result.open_preferences) runtime.visible = true;
    return post_callback_feedback(notifications,
        result.launched ? wormedit::notification_severity_t::success
                       : (result.open_preferences ? wormedit::notification_severity_t::warning
                                                  : wormedit::notification_severity_t::error),
        wormedit::notification_source_t::audio,
        result.launched ? "External player command launched." : result.error);
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
    else if (action.kind == WORM_UI_EXTERNAL_PLAYER_PLAY_START && state.external_player)
        (void)state.external_player(external_player_action_t::play_start);
    else if (action.kind == WORM_UI_EXTERNAL_PLAYER_PLAY_HERE && state.external_player)
        (void)state.external_player(external_player_action_t::play_here);
    else if (action.kind == WORM_UI_EXTERNAL_PLAYER_STOP && state.external_player)
        (void)state.external_player(external_player_action_t::stop);
    else (void)edit_external_player_preferences(runtime, action, notifications);
    return true;
}

auto open_project_chart(wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& interaction, wormedit::chart_id_t chart)
    -> std::string {
    const auto selected = wormedit::apply_workspace_action(workspace, history.document,
        history.document.revision, wormedit::workspace_select_chart_action_t{chart});
    if (!selected) return selected.diagnostics.empty() ? "Chart selection failed." : selected.diagnostics.front().message;
    const auto opened = wormedit::apply_workspace_action(workspace, history.document,
        history.document.revision, wormedit::workspace_switch_tab_action_t{wormedit::workspace_tab_t::compose});
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
bool dispatch_chart_import_action(wormedit::sys::chart_import_session_t& session,
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, const std::filesystem::path& asset_base,
    wormedit::notification_sink_t& notifications, bool loaded,
    wormedit::sys::program_preferences_runtime_t& preferences) {
    using namespace wormedit;
    const auto& request = action.chart_import;
    const bool picked = action.kind == WORM_UI_WORKSPACE_ACTION_IMPORT_CHART_FILE;
    if (!loaded || (request.kind == WORM_UI_CHART_IMPORT_NONE && !picked)) return false;
    if (request.kind == WORM_UI_CHART_IMPORT_BEGIN) sys::chart_import_begin(session);
    else if (request.kind == WORM_UI_CHART_IMPORT_CANCEL) sys::chart_import_cancel(session);
    else if (request.kind == WORM_UI_CHART_IMPORT_LOAD || picked) {
        sys::chart_import_begin(session);
        sys::chart_import_load(session, std::filesystem::path{std::string{action.project_path}},
                               history.document, asset_base);
    } else if (request.kind == WORM_UI_CHART_IMPORT_TOGGLE) {
        sys::chart_import_toggle(session, chart_id_t{request.chart_id}, history.document, asset_base);
    } else if (request.kind == WORM_UI_CHART_IMPORT_CONFIRM) {
        const auto source_path = session.path_text;
        const auto imported = sys::chart_import_confirm(session, history, request.document_revision, asset_base);
        if (imported.command.applied) {
            remember_project_file(preferences, source_path, notifications);
            notifications.post(notification_severity_t::success, notification_source_t::document,
                               imported.charts.size() == 1 ? "Chart imported." : "Charts imported.");
            const auto rebound = rebind_workspace(workspace, history);
            if (!rebound)
                notifications.post(notification_severity_t::error, notification_source_t::document,
                                   rebound.diagnostics.empty() ? "Charts imported, but workspace rebinding failed."
                                                               : rebound.diagnostics.front().message);
        } else {
            notifications.post(notification_severity_t::warning, notification_source_t::document, session.error);
        }
    }
    return true;
}

bool dispatch_project_chart_action(wormedit::sys::project_chart_runtime_t& runtime,
    const WormUiWorkspaceAction& frame_action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& interaction,
    application_command_state_t& application, wormedit::notification_sink_t& notifications,
    bool handled, bool loaded, wormedit::task_scheduler_t& scheduler) {
    if (handled) return true;
    const auto& action = frame_action.project_chart;
    const auto& group = frame_action.project_output_group;
    const bool group_action = group.kind != WORM_UI_PROJECT_OUTPUT_GROUP_NONE;
    if (!loaded || (!group_action && action.kind == WORM_UI_PROJECT_CHART_NONE)) return false;
    if (group_action && (application.paste_session ||
        (interaction && editor_interaction_has_pending_gesture(*interaction)))) {
        notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::document,
            "Finish or cancel the editor gesture before changing output groups.");
        return true;
    }
    const auto previous_chart = workspace.selected_chart;
    auto result = group_action
        ? wormedit::sys::apply_project_output_group_ui_action(runtime, history, group, scheduler)
        : wormedit::sys::apply_project_chart_ui_action(runtime, history, action, workspace.selected_chart);
    notifications.post(result.severity, wormedit::notification_source_t::document, result.status);
    if (result.applied) {
        const auto rebound = wormedit::rebind_workspace(workspace, history);
        if (!rebound) {
            if (rebound.diagnostics.empty())
                notifications.post(wormedit::notification_severity_t::error,
                                   wormedit::notification_source_t::document,
                                   "Project changed, but workspace rebinding failed.");
            else
                notifications.post(notification_severity_for(rebound.diagnostics.front().severity),
                                   wormedit::notification_source_t::document,
                                   rebound.diagnostics.front().message);
            return true;
        }
    }
    if (result.open_chart) {
        if (application.transport_stop) application.transport_stop();
        const auto error = open_project_chart(history, workspace, interaction, *result.open_chart);
        if (!error.empty()) notifications.post(wormedit::notification_severity_t::error,
                                              wormedit::notification_source_t::document, error);
    } else if (previous_chart != workspace.selected_chart) {
        if (application.transport_stop) application.transport_stop();
        interaction.reset();
    }
    return true;
}

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
    const auto paint_active = application.note_paint_mode_active;
    const auto layer_active = application.semantic_layer_paint_mode_active;
    const auto tools_ready = compose_ready && !application.compose_timing_focus;
    const auto layer_ready = tools_ready &&
        layer_tool_target_editable(chart, application.semantic_layer_target);
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
    return chart && state.transport_preview_playing && state.transport_play_pause &&
           state.transport_stop;
}

auto paste_target_available(bool compose_ready, bool pending,
                            const application_command_state_t& state) -> bool {
    return compose_ready && !pending && state.chart_clipboard.has_value();
}

void refresh_semantic_membership_commands(wormedit::command_registry_t& registry,
    bool compose_ready, bool target_valid, const application_command_state_t& app,
    const std::optional<wormedit::editor_interaction_state_t>& state) {
    using namespace wormedit;
    const auto any_target = target_valid || app.semantic_layer_target_none;
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
    const bool enabled = ready && !pending && !application.compose_timing_focus && !application.paste_session;
    const bool checked = enabled && application.quantize_editor &&
        application.quantize_editor->song == history.document.id &&
        application.quantize_editor->chart == state->chart && application.quantize_editor->preview_enabled;
    (void)wormedit::update_command_state(registry, {"edit.toggle_quantize_preview"}, enabled, checked);
    const bool selected = enabled && (!state->selected_notes.empty() || !state->selected_objects.empty());
    (void)wormedit::update_command_state(registry, {"edit.apply_quantize"}, selected, std::nullopt);
}

void refresh_compose_dock_commands(wormedit::command_registry_t& registry, bool compose_ready,
                                    bool pending, const application_command_state_t& application) {
    const bool enabled = compose_ready && !pending && !application.compose_timing_focus &&
                         !application.paste_session;
    for (const auto id : {"view.open_layers", "semantic_layer.create", "view.toggle_sounds"})
        (void)wormedit::update_command_state(registry, {id}, enabled, std::nullopt);
}

auto sound_picker_command_available(bool compose_ready, bool pending) -> bool {
    return compose_ready && !pending;
}

auto project_export_available(const wormedit::chart_document_t* chart,
    const std::optional<wormedit::editor_interaction_state_t>& compose,
    const application_command_state_t& application) -> bool {
    return chart && !application.paste_session &&
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
    if (chart && application_state.semantic_layer_target) {
        const auto target_layer =
            std::ranges::find(chart->semantic_layers, *application_state.semantic_layer_target,
                              &semantic_layer_t::id);
        target_valid = target_layer != chart->semantic_layers.end();
        if (!target_valid) {
            application_state.semantic_layer_target.reset();
            application_state.semantic_layer_target_none = false;
            application_state.semantic_layer_paint_mode_active = false;
            application_state.semantic_layer_solo_active = false;
        } else if (target_layer->locked || !target_layer->visible) {
            application_state.semantic_layer_paint_mode_active = false;
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
                                                          application_state.kiai_selected_head);
    set("edit.copy", clipboard_selection);
    set("edit.cut", clipboard_selection);
    set("edit.paste", paste_target_available(compose_ready, pending, application_state));
    refresh_note_kind_commands(registry, chart, compose_state, compose_ready, pending,
                                application_state);

    set("selection.clear", compose_ready && (inline_preview_active(application_state) ||
                                            application_state.paste_session.has_value() ||
                                            pending || !compose_state->selected_objects.empty() ||
                                             application_state.note_paint_mode_active ||
                                             application_state.semantic_layer_paint_mode_active));
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
        chart != nullptr && static_cast<bool>(application_state.transport_play_pause));
    set("transport.play_from_start",
        chart != nullptr && static_cast<bool>(application_state.transport_play_from_start));
    refresh_transport_rate_commands(registry, application_state);
    set("transport.stop",
        application_state.transport_active && application_state.transport_active());

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
                timing_alias == application_state.compose_timing_focus);
    }
    set("timing.toggle_lock",
        workspace.active_tab == workspace_tab_t::compose &&
            application_state.compose_timing_focus,
        application_state.timing_locked);
    set("timing.insert_bpm", compose_ready && !application_state.timing_locked);
    set("timing.insert_sv", compose_ready && !application_state.timing_locked);
    set("timing.open_settings", true);

    const auto palette = compose_ready
                             ? make_semantic_layer_palette_window(
                                   *chart, application_state.semantic_layer_target,
                                   application_state.semantic_layer_palette_chart == chart->id
                                       ? application_state.semantic_layer_palette_page
                                       : 0,
                                   false, WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY)
                             : std::nullopt;
    for (std::size_t index = 0; index < WORM_UI_COMPOSE_SEMANTIC_LAYER_CAPACITY; ++index) {
        const auto* layer = palette && index < palette->layer_indices.size()
                                ? &chart->semantic_layers[palette->layer_indices[index]]
                                : nullptr;
        const bool available = layer != nullptr;
        const bool checked = available && application_state.semantic_layer_target &&
                             layer->id == *application_state.semantic_layer_target;
        set("semantic_layer.select_slot_" + std::to_string(index + 1), available, checked);
    }
    refresh_semantic_membership_commands(registry, compose_ready, target_valid,
                                          application_state, compose_state);
    set("audio.paint_hitsound", false);
    set("audio.toggle_click_audition", true, application_state.editor_preferences.audition_on_click);
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
    if (result.status == wormedit::command_dispatch_status_t::rejected && command && command->invoke)
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
        view.command_menu_rows[i] = {command.id.value.c_str(), command.display_name.c_str(), command.enabled};
    }
}

auto command_menu_invoker(wormedit::command_invoker_t invoke) -> wormedit::command_invoker_t {
    return [invoke = std::move(invoke)](const wormedit::command_invocation_context_t& invocation) {
        if (invocation.command.value == "workspace.command_menu")
            return wormedit::command_invocation_result_t{.accepted = worm_ui_frontend_open_command_menu()};
        return invoke(invocation);
    };
}

void dispatch_menu_command(WormUiWorkspaceAction& action,
                                  wormedit::command_registry_t& registry,
                                  wormedit::document_history_t& history,
                                  wormedit::workspace_controller_t& workspace,
                                  const std::optional<wormedit::editor_interaction_state_t>& compose,
                                  application_command_state_t& application,
                                  wormedit::notification_sink_t& notifications) {
    const auto* command_id = action.command_menu_invoked
        ? action.command_menu_command_id : action.menu_command_id;
    if (!command_id[0]) return;
    if (action.command_menu_invoked &&
        (!action.shortcut_window_focused || action.shortcut_ime_composition_active ||
         action.compose_input_project_id != history.document.id.value ||
         action.compose_input_chart_id != (workspace.selected_chart ? workspace.selected_chart->value : 0) ||
         action.compose_input_document_revision != history.document.revision ||
         static_cast<int>(action.tab) != static_cast<int>(workspace.active_tab))) return;
    if (!wormedit::sys::compose_menu_command_is_current(action, history, workspace)) return;
    // SDL forwards key events before AppKit invokes key equivalents. Retire that
    // same chord so a menu shortcut cannot run the registry command twice.
    if (!action.command_menu_invoked) consume_menu_shortcut(action, registry);
    refresh_application_command_states(registry, history, workspace, compose, application);
    auto focus = application_command_focus(action, workspace);
    // Activation closes the palette; its own search field must not consume the command.
    if (action.command_menu_invoked || action.compose_menu_command) focus.text_entry_active = false;
    const auto result = wormedit::invoke_command(registry, {command_id}, focus);
    if (!result.diagnostics.empty()) post_shortcut_rejection(notifications, registry, result);
}

auto dispatch_application_shortcuts(
    const WormUiWorkspaceAction& action, wormedit::command_registry_t& registry,
    const wormedit::document_history_t& history, const wormedit::workspace_controller_t& workspace,
    const std::optional<wormedit::editor_interaction_state_t>& compose_state,
    application_command_state_t& application_state, wormedit::notification_sink_t& notifications,
    std::optional<std::uint64_t>& tap_event_ns, bool& preserves_editor_gesture) -> bool {
    using namespace wormedit;
    preserves_editor_gesture = true;
    if (action.shortcut_overflowed ||
        action.shortcut_event_count > WORM_UI_SHORTCUT_EVENT_CAPACITY) {
        notifications.post(notification_severity_t::warning, notification_source_t::input,
                           "Shortcut input exceeded its per-frame capacity and was discarded.");
        preserves_editor_gesture = false;
        return true;
    }
    bool handled{};
    for (std::size_t index = 0; index < action.shortcut_event_count; ++index) {
        refresh_application_command_states(registry, history, workspace, compose_state,
                                           application_state);
        const auto& input = action.shortcut_events[index];
        tap_event_ns = input.timestamp_ns;
        const auto dispatched = input.tap_timing_space
            ? invoke_command(registry, {"timing.tap"}, application_command_focus(action, workspace))
            : dispatch_shortcut(
            registry,
            {.chord = {.key = static_cast<shortcut_key_t>(input.hid_usage),
                       .modifiers = input.modifiers},
             .auto_repeat = input.repeat},
            application_command_focus(action, workspace), application_primary_modifier());
        tap_event_ns.reset();
        if (!dispatched.command || (dispatched.command->value != "timing.tap" &&
            dispatched.command->value != "workspace.options" &&
            dispatched.command->value != "view.notifications")) preserves_editor_gesture = false;
        if (dispatched.status == command_dispatch_status_t::invoked) {
            handled = true;
        } else if (dispatched.status == command_dispatch_status_t::disabled) {
            handled = true;
            notifications.post(notification_severity_t::warning, notification_source_t::input,
                               dispatched.command
                                   ? "Command is currently unavailable: " + dispatched.command->value
                                   : "Command is currently unavailable.");
        } else if (dispatched.status == command_dispatch_status_t::conflict ||
                   dispatched.status == command_dispatch_status_t::invalid_event ||
                   dispatched.status == command_dispatch_status_t::rejected) {
            handled = true;
            post_shortcut_rejection(notifications, registry, dispatched);
        } else if (dispatched.status == command_dispatch_status_t::repeat_suppressed) {
            handled = true;
        }
    }
    return handled;
}

void dispatch_timing_stage_value_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    bool timing_locked, std::uint64_t active_snap_denominator,
    wormedit::chart_document_t* chart,
    std::optional<wormedit::timing_lane_pseudonote_preview_t>& timing_stage,
    wormedit::notification_sink_t& notifications) {
    const auto value = chart == nullptr
                           ? std::optional<wormedit::timing_lane_value_t>{}
                           : timing_stage_value(*timing_stage, *chart, action,
                                                active_snap_denominator);
    if (!value) {
        timing_stage->valid = false;
        timing_stage->diagnostics = {{
            .severity = wormedit::diagnostic_severity_t::error,
            .code = wormedit::diagnostic_code_t::invalid_rational,
            .message = "Enter a bounded exact rational for this timing value.",
        }};
        notifications.post(wormedit::notification_severity_t::warning,
                           wormedit::notification_source_t::edit,
                           "Timing value is outside its supported bounds.");
        return;
    }

    const bool valid = wormedit::update_timing_lane_pseudonote(
        history, *timing_stage, *value, timing_locked);
    if (action.timing_stage_action == WORM_UI_TIMING_STAGE_CONFIRM && valid) {
        const auto result = wormedit::confirm_timing_lane_pseudonote(
            history, *timing_stage, timing_locked);
        const auto edited_existing = timing_stage->existing_event.has_value();
        if (result)
            notifications.post(wormedit::notification_severity_t::success,
                               wormedit::notification_source_t::edit,
                               edited_existing ? "Timing event updated." : "Timing event inserted.");
        else if (result.diagnostics.empty() && edited_existing)
            notifications.post(wormedit::notification_severity_t::info,
                               wormedit::notification_source_t::edit, "Timing event was unchanged.");
        else
            post_edit_rejection(notifications, result.diagnostics, "Timing event insertion was rejected.");
        timing_stage.reset();
    } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_CONFIRM) {
        post_edit_rejection(notifications, timing_stage->diagnostics, "Timing event values are invalid.");
    }
}

auto timing_stage_binding_matches(const WormUiWorkspaceAction& action,
                                 const wormedit::document_history_t& history,
                                 const wormedit::workspace_controller_t& workspace,
                                 const wormedit::timing_lane_pseudonote_preview_t& stage) -> bool {
    return workspace.active_tab == wormedit::workspace_tab_t::compose &&
           workspace.selected_chart && workspace.selected_chart->value == action.timing_chart_id &&
           stage.chart.value == action.timing_chart_id &&
           (stage.existing_event.has_value() ? stage.existing_event->value : std::uint64_t{}) ==
               action.timing_object_id &&
           stage.document_revision == action.timing_document_revision &&
           history.document.revision == action.timing_document_revision;
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
            history, stage.chart, *kind, *stage.existing_event,
            action.timing_document_revision, timing_locked);
        if (result)
            notifications.post(wormedit::notification_severity_t::success,
                               wormedit::notification_source_t::edit, "Timing event deleted.");
        else
            post_edit_rejection(notifications, result.diagnostics, "Timing event deletion was rejected.");
    }
    wormedit::cancel_timing_lane_pseudonote(stage);
}

void move_staged_timing_event(const WormUiWorkspaceAction& action,
                              wormedit::document_history_t& history, bool timing_locked,
                              std::uint64_t active_snap_denominator,
                              wormedit::chart_document_t* chart,
                              wormedit::timing_lane_pseudonote_preview_t& stage,
                              wormedit::notification_sink_t& notifications) {
    const auto target =
        chart == nullptr || !stage.existing_event || !action.timing_has_target_y
            ? std::optional<wormedit::local_position_t>{}
            : timing_position_from_fraction(
                  *chart, wormedit::sys::timing_target_fraction(action.timing_target),
                  active_snap_denominator);
    if (!target ||
        !wormedit::move_existing_timing_lane_pseudonote(history, stage, *target, timing_locked)) {
        post_edit_rejection(notifications, stage.diagnostics, "The timing marker cannot move to that row.");
    } else {
        notifications.post_transient_hint(
                           wormedit::notification_source_t::edit,
                           "Timing marker moved in the staged preview; apply to commit.");
    }
}

void dispatch_active_timing_stage_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, bool timing_locked,
    std::uint64_t active_snap_denominator, timing_edit_t& timing_edit,
    wormedit::notification_sink_t& notifications) {
    auto& stage = timing_edit.stage;
    if (!stage) return;
    if (!timing_stage_binding_matches(action, history, workspace, *stage)) {
        wormedit::cancel_timing_lane_pseudonote(*stage);
        stage.reset();
        post_edit_rejection(notifications, {}, "Timing event staging expired because its chart or revision changed.");
        return;
    }
    const auto chart = std::ranges::find(history.document.charts, stage->chart,
                                         &wormedit::chart_document_t::id);
    auto* chart_ptr = chart == history.document.charts.end() ? nullptr : &*chart;
    if (action.timing_stage_action == WORM_UI_TIMING_STAGE_DELETE) {
        delete_staged_timing_event(action, history, timing_locked, *stage, notifications);
        stage.reset();
    } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_MOVE) {
        move_staged_timing_event(action, history, timing_locked, active_snap_denominator,
                                 chart_ptr, *stage, notifications);
    } else {
        dispatch_timing_stage_value_action(action, history, timing_locked,
                                           active_snap_denominator, chart_ptr, stage, notifications);
    }
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
    const auto result = wormedit::confirm_timing_lane_pseudonote(history, candidate, timing_locked);
    if (result)
        notifications.post(wormedit::notification_severity_t::success,
                           wormedit::notification_source_t::edit, "Timing event inserted.");
    else
        post_edit_rejection(notifications, result.diagnostics, "Timing event insertion was rejected.");
}

void dispatch_timing_stage_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, bool timing_locked,
    std::uint64_t active_snap_denominator, timing_edit_t& timing_edit,
    wormedit::notification_sink_t& notifications) {
    using status_t = wormedit::timing_lane_pseudonote_status_t;
    auto& timing_stage = timing_edit.stage;
    if (action.timing_stage_action == WORM_UI_TIMING_STAGE_BEGIN) {
        if (!timing_stage) {
            auto candidate = begin_timing_stage(action, history, workspace, timing_locked,
                                                active_snap_denominator);
            if (candidate.status == status_t::editing) {
                timing_stage = std::move(candidate);
                notifications.post_transient_hint(
                                   wormedit::notification_source_t::edit,
                                   timing_stage->existing_event.has_value()
                                       ? "Timing event loaded; apply to update it."
                                       : "Timing event staged; confirm to insert it.");
            } else {
                post_edit_rejection(notifications, candidate.diagnostics, "Timing event could not be staged at that row.");
            }
        }
    } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_QUICK_INSERT) {
        if (timing_stage) {
            post_edit_rejection(notifications, {}, "Confirm or cancel the open timing edit before inserting another event.");
        } else {
            quick_insert_timing_event(action, history, workspace, timing_locked,
                                      active_snap_denominator, notifications);
        }
    } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_CANCEL) {
        if (timing_stage) {
            wormedit::cancel_timing_lane_pseudonote(*timing_stage);
            timing_stage.reset();
            notifications.post_transient_hint(
                               wormedit::notification_source_t::edit, "Timing event staging cancelled.");
        }
    } else if (timing_stage) {
        dispatch_active_timing_stage_action(action, history, workspace, timing_locked,
                                            active_snap_denominator, timing_edit, notifications);
    }
}

auto post_workspace_selection_feedback(wormedit::notification_sink_t& notifications,
    const auto& result, std::string accepted, std::string rejected) -> std::string {
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
            [first](const auto& chart) { return chart.id != first; });
        if (other == history.document.charts.end()) return post_callback_feedback(notifications, wormedit::notification_severity_t::hint,
            wormedit::notification_source_t::input, "Add another difficulty to compare.");
        second = other->id;
    }
    const auto applied = wormedit::apply_workspace_action(
        workspace, history.document, history.document.revision,
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
        workspace, history.document, history.document.revision,
        wormedit::workspace_select_audio_asset_action_t{
            asset});
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
            workspace, history.document, history.document.revision,
            wormedit::workspace_select_visual_asset_action_t{
                wormedit::asset_id_t{action.visual_asset_id}});
        return post_workspace_selection_feedback(notifications, applied,
            "Image asset selected.", "Image asset selection rejected.");
    }
    case WORM_UI_WORKSPACE_ACTION_SWITCH_TAB: {
        const auto applied = wormedit::apply_workspace_action(
            workspace, history.document, history.document.revision,
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

bool dispatch_timing_workspace_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    application_command_state_t& application_state, timing_grid_state_t& timing_grid,
    timing_edit_t& timing_edit, wormedit::notification_sink_t& notifications) {
    if (action.timing_stage_action != WORM_UI_TIMING_STAGE_NONE) {
        const auto chart = std::ranges::find(history.document.charts,
                                             wormedit::chart_id_t{action.timing_chart_id},
                                             &wormedit::chart_document_t::id);
        const auto denominator =
            chart == history.document.charts.end()
                ? timing_grid.active_denominator
                : wormedit::effective_edit_snap_denominator(
                      *chart, timing_grid.active_denominator, application_state.edit_snap_enabled);
        dispatch_timing_stage_action(action, history, workspace,
                                     application_state.timing_locked, denominator, timing_edit,
                                     notifications);
        return true;
    }
    return false;
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

void dispatch_bgm_mix_frame_action(auto& action, auto& context, bool shortcut_handled) {
    if (!shortcut_handled && action.kind != WORM_UI_WORKSPACE_ACTION_QUIT)
        (void)dispatch_compose_bgm_mix_inputs(action, context.history_ref, context.workspace_ref,
            context.application_command_state_ref, &context.notification_sink_ref);
}

void refresh_sound_selection_context(const WormUiWorkspaceAction& action,
    application_command_state_t& sound_context) {
    sound_context.sound_selection_chart = wormedit::chart_id_t{action.compose_input_chart_id};
    sound_context.sound_selection_revision = action.compose_input_document_revision;
    sound_context.sound_selection_hover = action.compose_sound_hover_object_id
        ? std::optional{wormedit::object_id_t{action.compose_sound_hover_object_id}} : std::nullopt;
    WormUiKeysoundDockRequest sound_dock{};
    worm_ui_frontend_keysound_dock_request(&sound_dock);
    sound_context.sound_selection_dock.clear();
    for (size_t i = 0; i < sound_dock.selected_resource_count; ++i)
        sound_context.sound_selection_dock.push_back(wormedit::resource_id_t{sound_dock.selected_resource_ids[i]});
}

auto has_fresh_shortcut_key(const WormUiWorkspaceAction& action) -> bool {
    const auto count = std::min(action.shortcut_event_count, std::size(action.shortcut_events));
    for (std::size_t index = 0; index < count; ++index)
        if (!action.shortcut_events[index].repeat) return true;
    return false;
}

template <typename Context>
auto process_compose_frame_action(Context& context, WormUiWorkspaceAction& action,
                                  double viewport_delta_seconds,
                                  const std::optional<wormedit::document_command_result_t>& tap_applied) -> bool {
    // Apply consumed this frame's document snapshot. Never replay its shortcuts,
    // pointer input or navigation against the newly committed timing revision.
    if (tap_applied && tap_applied->applied) return true;
    // Complete a prior measure move before a new keyboard command can edit its destination.
    if (has_fresh_shortcut_key(action) && context.compose_interaction_state_ref)
        context.compose_scene_cache_ref.navigation_travel.finish(
            *context.compose_interaction_state_ref, context.history_ref.document.revision);
    bool preserves_editor_gesture{};
    const bool shortcut_handled =
        action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
        dispatch_application_shortcuts(action, context.command_registry_ref, context.history_ref,
                                       context.workspace_ref, context.compose_interaction_state_ref,
                                       context.application_command_state_ref, context.notification_sink_ref,
                                       context.tap_event_ns_ref, preserves_editor_gesture);
    if (!context.session_loaded_ref) return shortcut_handled;
    if (shortcut_handled && !preserves_editor_gesture && context.compose_interaction_state_ref &&
        editor_interaction_has_pending_gesture(*context.compose_interaction_state_ref)) {
        (void)wormedit::dispatch_editor_action(
            context.history_ref, *context.compose_interaction_state_ref,
            wormedit::cancel_editor_gesture_action_t{});
    }
    if (shortcut_handled && !preserves_editor_gesture) {
        context.audio_offset_drag_ref.reset();
        context.audio_offset_drag_tab_ref.reset();
        context.compose_scene_cache_ref.clear_beat_fit_capture();
        if (context.compose_interaction_state_ref)
            context.compose_scene_cache_ref.navigation_travel.finish(
                *context.compose_interaction_state_ref, context.history_ref.document.revision);
        context.compose_scene_cache_ref.offset_fit_drag.reset();
    }
    dispatch_bgm_mix_frame_action(action, context, shortcut_handled);
    if (!shortcut_handled && action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
        (action.compose_input_count != 0 || action.compose_viewport_input_count != 0 ||
         action.compose_has_pointer_hover ||
         (context.workspace_ref.active_tab == wormedit::workspace_tab_t::compose &&
          action.compose_input_chart_id != 0)) &&
        context.compose_interaction_state_ref) {
        (void)dispatch_compose_input_action(
            action, context.history_ref, context.workspace_ref, *context.compose_interaction_state_ref,
            context.compose_viewport_input_ref, context.compose_navigation_input_ref,
            context.application_command_state_ref, context.compose_scene_cache_ref,
            viewport_delta_seconds, &context.notification_sink_ref);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        wormedit::sys::process_compose_audition_requests(
            context.compose_scene_cache_ref, context.history_ref, context.audition_sources_ref,
            context.object_audition_ref, context.preview_session_ref, context.preview_output_ref,
            context.preview_state_ref, context.audition_output_ref, context.notification_sink_ref);
#else
        wormedit::sys::process_compose_audition_requests(
            context.compose_scene_cache_ref, context.history_ref, context.notification_sink_ref);
#endif
    }
    refresh_compose_editor_width(action, context.history_ref, context.workspace_ref,
        context.application_command_state_ref);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    wormedit::sys::process_sound_audition_request(
        context.application_command_state_ref.pending_sound_audition, context.history_ref,
        context.audition_sources_ref, context.preview_session_ref, context.preview_output_ref,
        context.preview_state_ref, context.audition_output_ref, context.notification_sink_ref);
#else
    wormedit::sys::process_sound_audition_request(
        context.application_command_state_ref.pending_sound_audition, context.notification_sink_ref);
#endif
    if (context.workspace_ref.active_tab != wormedit::workspace_tab_t::compose) {
        context.application_command_state_ref.note_layer_menu_row_count = 0;
        context.compose_viewport_input_ref.escape();
        context.compose_scene_cache_ref.viewport_autoscroll_active = false;
        if (context.compose_interaction_state_ref)
            context.compose_scene_cache_ref.navigation_travel.finish(
                *context.compose_interaction_state_ref, context.history_ref.document.revision);
    }
    if (!shortcut_handled && action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
        context.compose_interaction_state_ref &&
        context.workspace_ref.active_tab == wormedit::workspace_tab_t::compose) {
        (void)dispatch_compose_navigation_actions(
            context.compose_navigation_input_ref.advance(
                static_cast<std::uint64_t>(SDL_GetTicks())),
            context.history_ref, *context.compose_interaction_state_ref,
            &context.application_command_state_ref, &context.notification_sink_ref);
    } else if (context.workspace_ref.active_tab != wormedit::workspace_tab_t::compose) {
        context.compose_navigation_input_ref.cancel();
    }
    return shortcut_handled;
}

void tick_loaded_project_autosave(
    bool session_loaded, wormedit::native_project_autosave_loop_t& autosave_loop,
    const wormedit::document_history_t& history, bool native_project_loaded,
    const std::filesystem::path& active_project_path,
    const std::filesystem::path& recovery_path, wormedit::notification_sink_t& notifications,
    const std::filesystem::path& asset_base) {
    if (!session_loaded) return;
    wormedit::sys::tick_application_autosave(
        autosave_loop, history, native_project_loaded, active_project_path,
        recovery_path, notifications, asset_base);
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


template <typename Context>
void post_project_feedback(Context& context, wormedit::notification_severity_t severity,
                           wormedit::notification_source_t source, std::string message, std::string_view detail = {}) {
    // The close confirmation retains its own scoped feedback; it is not a
    // global notification producer or a replacement status-line channel.
    context.close_feedback_ref = std::move(message);
    if (severity == wormedit::notification_severity_t::hint)
        context.notification_sink_ref.post_transient_hint(source, context.close_feedback_ref);
    else context.notification_sink_ref.post(wormedit::notification_post_t{.severity = severity, .source = source,
        .message = context.close_feedback_ref, .detail = std::string(detail), .target = {}});
}

template <typename Context>
void reset_project_runtime_state(Context& context) {
    const auto generation = context.project_chart_runtime_ref.session_generation + 1;
    context.project_chart_runtime_ref = {.session_generation = generation};
    context.audio_offset_drag_ref.reset();
    context.audio_offset_drag_tab_ref.reset();
    context.compose_interaction_state_ref.reset();
    context.timing_edit_ref = {};
    context.timing_scene_cache_ref = {};
    context.compose_scene_cache_ref = {};
    context.timing_grid_ref = {};
    context.compose_viewport_input_ref.escape();
    context.compose_navigation_input_ref.cancel();
    context.metadata_drop_ref.clear_paths();
    wormedit::sys::reset_application_project_edit_state(context.application_command_state_ref);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    post_audio_inventory_feedback(
        wormedit::sys::reconcile_audition_sources(context.audition_reconciliation_ref, true),
        context.notification_sink_ref);
#endif
}

template <typename Context>
auto replace_project_session(Context& context, wormedit::song_document_t document,
                             bool loaded, bool saved, bool native_project,
                             std::filesystem::path native_path,
                             std::filesystem::path asset_base) -> bool {
    using namespace wormedit;
    wormedit::sys::refresh_project_audio_availability(document, asset_base);
    auto replacement_history = make_document_history(std::move(document), 128, saved);
    auto replacement_binding = make_workspace_controller(replacement_history);
    if (!replacement_binding) {
        post_project_feedback(context, notification_severity_t::error, notification_source_t::document,
                              "The selected document could not be bound to a workspace.");
        return false;
    }
    const auto saved_version = saved && native_project
        ? std::optional<native_project_document_version_t>{
              {.project_id = replacement_history.document.id,
               .revision = replacement_history.document.revision}}
        : std::nullopt;
    if (!context.autosave_loop_ref.reset_session(
            native_project_autosave_loop_t::clock_t::now(), saved_version, 0)) {
        post_project_feedback(context, notification_severity_t::warning, notification_source_t::document,
                              "Wait for the current autosave to finish before changing projects.");
        return false;
    }
    context.history_ref = std::move(replacement_history);
    context.workspace_ref = std::move(*replacement_binding.workspace);
    context.session_loaded_ref = loaded;
    context.native_project_loaded_ref = native_project;
    context.fresh_blank_project_ref = false;
    context.pending_audio_project_path_ref.clear();
    context.pending_bms_encoding_path_ref.clear();
    context.active_project_path_ref = std::move(native_path);
    context.asset_base_ref = std::move(asset_base);
    context.external_player_runtime_ref.source_path.clear();
    context.scanned_primary_version_ref = saved_version;
    reset_project_runtime_state(context);
    return true;
}

template <typename Context>
auto project_transition_ready(Context& context) -> bool {
    if (context.autosave_loop_ref.busy() || context.resource_copy_runtime_ref.busy() ||
        context.export_runtime_ref.busy() || context.metadata_asset_runtime_ref.import_pending()) {
        post_project_feedback(context, wormedit::notification_severity_t::warning, wormedit::notification_source_t::document,
                              "Wait for the current file operation before changing projects.");
        return false;
    }
    if (context.session_loaded_ref && wormedit::is_document_dirty(context.history_ref)) {
        post_project_feedback(context, wormedit::notification_severity_t::warning, wormedit::notification_source_t::document,
                              "Save the current project before replacing or closing it.");
        return false;
    }
    return true;
}

template <typename Context>
auto save_project_session(Context& context, const WormUiWorkspaceAction& action) -> bool {
    using namespace wormedit;
    if (!context.session_loaded_ref) {
        post_project_feedback(context, notification_severity_t::warning, notification_source_t::document,
                              "Create or open a project before saving.");
        return false;
    }
    if (context.resource_copy_runtime_ref.busy() || context.export_runtime_ref.busy() ||
        context.metadata_asset_runtime_ref.import_pending()) {
        post_project_feedback(context, notification_severity_t::warning, notification_source_t::document,
                              "Wait for the current file operation before saving.");
        return false;
    }
    auto destination = action.project_path[0] != '\0'
        ? std::filesystem::path{action.project_path}
        : context.active_project_path_ref;
    if (destination.empty()) {
        post_project_feedback(context, notification_severity_t::hint, notification_source_t::document,
                              "Choose a native project path to save.");
        return false;
    }
    auto extension = destination.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension.empty()) destination += ".wormedit";
    else if (extension != ".wormedit") {
        post_project_feedback(context, notification_severity_t::warning, notification_source_t::document,
                              "Choose a .wormedit project filename.");
        return false;
    }
    std::error_code path_error;
    const auto destination_base = std::filesystem::absolute(destination, path_error).parent_path();
    if (path_error) {
        post_project_feedback(context, notification_severity_t::error, notification_source_t::document,
                              "Could not resolve the project destination: " + path_error.message());
        return false;
    }
    auto saved_document = context.history_ref.document;
    wormedit::sys::rebase_project_paths(saved_document, context.asset_base_ref, destination_base);
    const auto result = publish_native_project_atomic(destination, saved_document);
    if (!result.published) {
        post_project_feedback(context,
            result.diagnostics.empty() ? notification_severity_t::error
                : notification_severity_for(result.diagnostics.front().severity), notification_source_t::document,
            result.diagnostics.empty() ? "Could not save the project." : result.diagnostics.front().message);
        return false;
    }
    wormedit::sys::rebase_project_paths(context.history_ref.document,
                                        context.asset_base_ref, destination_base);
    for (auto& entry : context.history_ref.undo) {
        wormedit::sys::rebase_project_paths(entry.before, context.asset_base_ref, destination_base);
        wormedit::sys::rebase_project_paths(entry.after, context.asset_base_ref, destination_base);
    }
    for (auto& entry : context.history_ref.redo) {
        wormedit::sys::rebase_project_paths(entry.before, context.asset_base_ref, destination_base);
        wormedit::sys::rebase_project_paths(entry.after, context.asset_base_ref, destination_base);
    }
    mark_document_saved(context.history_ref);
    const auto version = native_project_document_version_t{
        .project_id = context.history_ref.document.id,
        .revision = context.history_ref.document.revision};
    context.autosave_loop_ref.mark_explicit_save(version);
    context.scanned_primary_version_ref = version;
    context.active_project_path_ref = destination;
    remember_project_file(context.program_preferences_ref, destination, context.notification_sink_ref);
    context.native_project_loaded_ref = true;
    context.asset_base_ref = destination_base;
    context.external_player_runtime_ref.source_path.clear();
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    post_audio_inventory_feedback(
        wormedit::sys::reconcile_audition_sources(context.audition_reconciliation_ref, true),
        context.notification_sink_ref);
#endif
    auto message = "Project saved to " + destination.string();
    auto severity = notification_severity_t::success;
    if (!result.diagnostics.empty()) {
        message += " " + result.diagnostics.front().message;
        severity = std::max(severity, notification_severity_for(result.diagnostics.front().severity));
    }
    post_project_feedback(context, severity, notification_source_t::document, std::move(message));
    return true;
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

template <typename Context>
void create_new_project_session(Context& context, const WormUiWorkspaceAction& action);
template <typename Context>
void open_project_session(Context& context, const WormUiWorkspaceAction& action);

template <typename Context>
void finish_confirmed_project_replacement(pending_close_t& pending, Context& context) {
    WormUiWorkspaceAction action{};
    action.new_project_layout_index = pending.layout_index;
    action.bms_encoding_index = pending.encoding_index;
    std::snprintf(action.project_path, sizeof(action.project_path), "%s", pending.project_path.c_str());
    if (pending.target == close_target_t::new_project)
        create_new_project_session(context, action);
    else
        open_project_session(context, action);
    // Encoding choice continues the same approved replacement of this document version.
    if (pending.target == close_target_t::open_file && !pending.project_path.empty() &&
        context.pending_bms_encoding_path_ref == pending.project_path) {
        pending.awaiting_encoding = true;
        return;
    }
    pending = {};
}

template <typename Context>
auto project_replacement_busy(Context& context) -> bool {
    if (!context.autosave_loop_ref.busy() && !context.resource_copy_runtime_ref.busy() &&
        !context.export_runtime_ref.busy() && !context.metadata_asset_runtime_ref.import_pending())
        return false;
    post_project_feedback(context, wormedit::notification_severity_t::warning, wormedit::notification_source_t::document,
                          "Wait for the current file operation before closing the project.");
    return true;
}

template <typename Context>
auto finish_confirmed_close(pending_close_t& pending, Context& context) -> bool {
    if (pending.target == close_target_t::application) {
        context.running_ref = false;
        pending = {};
        return true;
    }
    if (project_replacement_busy(context)) return false;
    if (pending.target == close_target_t::new_project || pending.target == close_target_t::open_file) {
        finish_confirmed_project_replacement(pending, context);
        return true;
    }
    if (!replace_project_session(context, wormedit::sys::empty_session_document(
        wormedit::sys::new_session_project_id()), false, true, false, {}, {})) return false;
    pending = {};
    post_project_feedback(context, wormedit::notification_severity_t::info, wormedit::notification_source_t::document,
                          "No project loaded.");
    return true;
}

template <typename Context>
void confirm_unsaved_changes(pending_close_t& pending, close_target_t target, Context& context,
                             bool confirm, const WormUiWorkspaceAction* replacement = nullptr) {
    if (target != close_target_t::application && project_replacement_busy(context)) return;
    // A close request ends transient gestures; cancellation returns a usable editor.
    context.compose_navigation_input_ref.cancel();
    context.compose_viewport_input_ref.escape();
    if (context.compose_interaction_state_ref)
        (void)wormedit::dispatch_editor_action(context.history_ref, *context.compose_interaction_state_ref,
            wormedit::cancel_editor_gesture_action_t{});
    pending = {.target = target, .project = context.history_ref.document.id,
        .revision = context.history_ref.document.revision};
    if (replacement) {
        pending.project_path = replacement->project_path;
        pending.layout_index = replacement->new_project_layout_index;
        pending.encoding_index = replacement->bms_encoding_index;
    }
    if (!confirm || !context.session_loaded_ref || !wormedit::is_document_dirty(context.history_ref))
        (void)finish_confirmed_close(pending, context);
}

auto requested_project_transition(WormUiWorkspaceActionKind kind) -> std::optional<close_target_t> {
    switch (kind) {
        case WORM_UI_WORKSPACE_ACTION_QUIT: return close_target_t::application;
        case WORM_UI_WORKSPACE_ACTION_CLOSE_PROJECT: return close_target_t::project;
        case WORM_UI_WORKSPACE_ACTION_NEW_PROJECT: return close_target_t::new_project;
        case WORM_UI_WORKSPACE_ACTION_OPEN_FILE: return close_target_t::open_file;
        default: return std::nullopt;
    }
}

template <typename Context>
auto resume_confirmed_encoding(pending_close_t& pending, Context& context,
                               const WormUiWorkspaceAction& action) -> bool {
    if (!pending.awaiting_encoding || action.kind != WORM_UI_WORKSPACE_ACTION_OPEN_FILE ||
        pending.project_path != action.project_path || pending.project != context.history_ref.document.id ||
        pending.revision != context.history_ref.document.revision)
        return false;
    pending.encoding_index = action.bms_encoding_index;
    pending.awaiting_encoding = false;
    (void)finish_confirmed_close(pending, context);
    return true;
}

template <typename Context>
void save_before_confirmed_close(pending_close_t& pending, Context& context, const WormUiWorkspaceAction& action) {
    const auto id = context.history_ref.document.id;
    const auto revision = context.history_ref.document.revision;
    if (id != pending.project) {
        post_project_feedback(context, wormedit::notification_severity_t::warning, wormedit::notification_source_t::document,
                              "The project changed while closing. Cancel and review the current project.");
        return;
    }
    pending.save_failure.clear();
    const bool saved = save_project_session(context, action);
    if (!saved) pending.save_failure = "Save failed: " + context.close_feedback_ref;
    if (saved && context.history_ref.document.id == id && context.history_ref.document.revision == revision &&
        !wormedit::is_document_dirty(context.history_ref))
        (void)finish_confirmed_close(pending, context);
}

template <typename Context>
auto dispatch_close_confirmation(pending_close_t& pending, Context& context,
    const WormUiWorkspaceAction& action, bool confirm) -> bool {
    if (pending.awaiting_encoding && action.kind == WORM_UI_WORKSPACE_ACTION_CANCEL_BMS_ENCODING) {
        pending = {};
        return false;
    }
    if (resume_confirmed_encoding(pending, context, action)) return true;
    if (const auto target = requested_project_transition(action.kind)) {
        const bool replacing = *target == close_target_t::new_project || *target == close_target_t::open_file;
        confirm_unsaved_changes(pending, *target, context, confirm || replacing, &action);
        return true;
    }
    if (pending.target == close_target_t::none || pending.awaiting_encoding) return false;
    if (action.close_decision != WORM_UI_CLOSE_NONE) {
        if (action.close_project_id != pending.project.value || action.close_revision != pending.revision) return true;
        if (action.close_decision == WORM_UI_CLOSE_CANCEL) { pending = {}; return true; }
        if (context.history_ref.document.id != pending.project || context.history_ref.document.revision != pending.revision) {
            pending.project = context.history_ref.document.id;
            pending.revision = context.history_ref.document.revision;
            post_project_feedback(context, wormedit::notification_severity_t::warning, wormedit::notification_source_t::document,
                                  "The project changed. Review it before confirming close.");
            return true;
        }
        if (action.close_decision == WORM_UI_CLOSE_DISCARD) { (void)finish_confirmed_close(pending, context); return true; }
        if (action.close_decision == WORM_UI_CLOSE_SAVE) pending.save_requested = true;
    }
    if (pending.save_requested && action.kind == WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT)
        save_before_confirmed_close(pending, context, action);
    return true; // Confirmation blocks authoring and shortcut dispatch.
}

template <typename Context>
void open_project_session(Context& context, const WormUiWorkspaceAction& action) {
    constexpr std::array<wormedit::text_encoding_t, 5> encodings{
        wormedit::text_encoding_t::utf8, wormedit::text_encoding_t::cp932,
        wormedit::text_encoding_t::euc_kr, wormedit::text_encoding_t::cp949,
        wormedit::text_encoding_t::windows_1252};
    if (action.bms_encoding_index > encodings.size()) {
        post_project_feedback(context, wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_,
                              "Choose a supported BMS text encoding.");
        return;
    }
    const auto encoding = action.bms_encoding_index == 0
        ? std::optional<wormedit::text_encoding_t>{}
        : std::optional{encodings[action.bms_encoding_index - 1]};
    auto opened = wormedit::sys::open_project_file(action.project_path, encoding);
    if (!opened) {
        post_project_feedback(context,
            opened.needs_encoding_choice ? wormedit::notification_severity_t::warning : wormedit::notification_severity_t::error,
            wormedit::notification_source_t::import_, opened.error);
        if (opened.needs_encoding_choice)
            context.pending_bms_encoding_path_ref = action.project_path;
        else if (encoding)
            context.pending_bms_encoding_path_ref.clear();
        return;
    }
    auto document = std::move(*opened.document);
    if (!opened.native_project) document.id = wormedit::sys::new_session_project_id();
    if (replace_project_session(context, std::move(document), true, true,
                                opened.native_project,
                                opened.native_project ? opened.source_path : std::filesystem::path{},
                                opened.asset_base)) {
        remember_project_file(context.program_preferences_ref, opened.source_path, context.notification_sink_ref);
        context.external_player_runtime_ref.source_path = opened.native_project ? std::filesystem::path{} : opened.source_path;
        post_project_feedback(context, wormedit::notification_severity_t::success,
            opened.native_project ? wormedit::notification_source_t::document : wormedit::notification_source_t::import_,
            opened.native_project
            ? "Project opened: " + opened.source_path.string()
            : "Chart imported from " + opened.source_path.string() + ". Save it as a project.");
        if (!opened.warnings.empty()) {
            auto warning = wormedit::sys::project_import_warning(opened.warnings);
            post_project_feedback(context, wormedit::notification_severity_t::warning,
                wormedit::notification_source_t::import_, std::move(warning.message), warning.detail);
        }
    }
}

template <typename Context>
void create_audio_project_session(Context& context, const char* path,
                                  std::uint32_t lane_count) {
    const auto audio = wormedit::sys::inspect_audio_for_project(path);
    if (!audio) {
        post_project_feedback(context, wormedit::notification_severity_t::error, wormedit::notification_source_t::import_, audio.error);
        context.pending_audio_project_path_ref.clear();
        return;
    }
    auto document = wormedit::sys::audio_project_document(
        wormedit::sys::new_session_project_id(), audio.source_path,
        audio.title, audio.artist, lane_count, audio.stream);
    if (!document) {
        post_project_feedback(context, wormedit::notification_severity_t::error, wormedit::notification_source_t::import_,
                              "Could not create the selected audio chart layout.");
        return;
    }
    if (!replace_project_session(context, std::move(*document), true, true, false,
                                 {}, audio.source_path.parent_path())) return;
    for (const auto& chart : context.history_ref.document.charts)
        remember_snap_denominator(context.application_command_state_ref, chart.id,
            context.application_command_state_ref.editor_preferences.default_snap);
    const auto selected = wormedit::apply_workspace_action(
        context.workspace_ref, context.history_ref.document,
        context.history_ref.document.revision,
        wormedit::workspace_select_audio_asset_action_t{
            context.history_ref.document.audio_assets.front().id});
    post_project_feedback(context,
        selected ? wormedit::notification_severity_t::success : wormedit::notification_severity_t::warning,
        wormedit::notification_source_t::import_, selected
        ? "Audio imported as a new " + std::to_string(lane_count) + "K project."
        : "Audio project created; select the music asset to preview it.");
}

template <typename Context>
void create_new_project_session(Context& context, const WormUiWorkspaceAction& action) {
    const auto lane_count = requested_project_lane_count(action.new_project_layout_index);
    if (!lane_count) {
        post_project_feedback(context, wormedit::notification_severity_t::warning, wormedit::notification_source_t::document,
                              "Choose a supported chart layout.");
        return;
    }
    if (action.project_path[0] != '\0') {
        create_audio_project_session(context, action.project_path, *lane_count);
        return;
    }
    auto document = wormedit::sys::initial_document(
        wormedit::sys::new_session_project_id(), *lane_count);
    if (!document) {
        post_project_feedback(context, wormedit::notification_severity_t::error, wormedit::notification_source_t::document,
                              "Could not create the selected chart layout.");
        return;
    }
    if (replace_project_session(context, std::move(*document), true, true, false, {}, {})) {
        for (const auto& chart : context.history_ref.document.charts)
            remember_snap_denominator(context.application_command_state_ref, chart.id,
                context.application_command_state_ref.editor_preferences.default_snap);
        context.fresh_blank_project_ref = true;
        post_project_feedback(context, wormedit::notification_severity_t::success, wormedit::notification_source_t::document,
                              "New " + std::to_string(*lane_count) + "K project created. Save it to keep your work.");
    }
}

template <typename Context>
auto dispatch_project_session_action(Context& context,
                                     const WormUiWorkspaceAction& action) -> bool {
    if (action.kind == WORM_UI_WORKSPACE_ACTION_CANCEL_BMS_ENCODING) {
        context.pending_bms_encoding_path_ref.clear();
        post_project_feedback(context, wormedit::notification_severity_t::hint, wormedit::notification_source_t::import_, "BMS import canceled.");
        return true;
    }
    if (action.kind == WORM_UI_WORKSPACE_ACTION_CANCEL_AUDIO_PROJECT) {
        context.pending_audio_project_path_ref.clear();
        post_project_feedback(context, wormedit::notification_severity_t::hint, wormedit::notification_source_t::import_, "Audio import canceled.");
        return true;
    }
    if (action.kind == WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT) {
        save_project_session(context, action);
        return true;
    }
    if (action.kind != WORM_UI_WORKSPACE_ACTION_NEW_PROJECT &&
        action.kind != WORM_UI_WORKSPACE_ACTION_OPEN_FILE &&
        action.kind != WORM_UI_WORKSPACE_ACTION_CLOSE_PROJECT) return false;
    if (!project_transition_ready(context)) return true;
    if (action.kind == WORM_UI_WORKSPACE_ACTION_CLOSE_PROJECT) {
        if (replace_project_session(context, wormedit::sys::empty_session_document(
                wormedit::sys::new_session_project_id()), false, true, false, {}, {}))
            post_project_feedback(context, wormedit::notification_severity_t::info, wormedit::notification_source_t::document, "No project loaded.");
        return true;
    }
    if (action.kind == WORM_UI_WORKSPACE_ACTION_NEW_PROJECT) {
        create_new_project_session(context, action);
        return true;
    }
    open_project_session(context, action);
    return true;
}

template <typename Context>
void dispatch_dropped_chart_project(Context& context, std::string& pending_path,
                                    pending_close_t& pending, bool confirm) {
    if (pending_path.empty()) return;
    const auto path = std::exchange(pending_path, {});
    if (path.size() >= WORM_UI_PROJECT_PATH_CAPACITY) {
        post_project_feedback(context, wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_,
                              "The dropped chart or project path is too long.");
        return;
    }
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_OPEN_FILE;
    std::memcpy(action.project_path, path.c_str(), path.size() + 1);
    (void)dispatch_close_confirmation(pending, context, action, confirm);
}

template <typename Context>
auto dispatch_recovery_workspace_action(Context& context,
                                        const WormUiWorkspaceAction& action) -> bool {
    if (action.kind < WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT ||
        action.kind > WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE) return false;
    if (context.autosave_loop_ref.busy()) {
        context.notification_sink_ref.post(wormedit::notification_severity_t::warning,
            wormedit::notification_source_t::recovery,
            "Wait for the current autosave to finish before recovering a session.");
        return true;
    }
    bool recovered = false;
    (void)wormedit::sys::dispatch_recovery_ui_action(
        action, context.recovery_controller_ref, context.history_ref, context.workspace_ref,
        context.scanned_primary_version_ref, context.autosave_loop_ref, recovered, context.notification_sink_ref);
    if (recovered) {
        context.session_loaded_ref = true;
        context.native_project_loaded_ref = false;
        context.fresh_blank_project_ref = false;
        context.pending_audio_project_path_ref.clear();
        context.pending_bms_encoding_path_ref.clear();
        context.active_project_path_ref.clear();
        context.asset_base_ref.clear();
        context.scanned_primary_version_ref.reset();
        reset_project_runtime_state(context);
    }
    return true;
}

template <typename Context>
auto dispatch_clipboard_menu(Context& context, const WormUiWorkspaceAction& action) -> bool {
    if (action.clipboard == WORM_UI_CLIPBOARD_NONE) return false;
    const auto id = action.clipboard == WORM_UI_CLIPBOARD_COPY ? "edit.copy"
                  : action.clipboard == WORM_UI_CLIPBOARD_CUT ? "edit.cut" : "edit.paste";
    auto& registry = context.command_registry_ref;
    refresh_application_command_states(registry, context.history_ref, context.workspace_ref,
        context.compose_interaction_state_ref, context.application_command_state_ref);
    const auto result = wormedit::invoke_command(registry, {id},
        application_command_focus(action, context.workspace_ref));
    if (!result.diagnostics.empty())
        post_shortcut_rejection(context.notification_sink_ref, registry, result);
    return true;
}

void import_dropped_bga(bool loaded, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, std::vector<std::string>& paths, wormedit::notification_sink_t& notifications) {
    if (paths.empty()) return;
    if (loaded && workspace.selected_chart)
        static_cast<void>(wormedit::sys::import_bga_files(history, workspace, *workspace.selected_chart, paths, notifications));
    else notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_,
        "Open a project and select a difficulty before adding images.");
    paths.clear();
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
        std::ranges::any_of(target->measures, [](const auto& measure) { return !measure.bgm_events.empty(); });
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

void import_dropped_keysounds(bool session_loaded, wormedit::document_history_t& history,
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
            origin.slots_mode != slots_mode) {
            notifications.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::import_,
                "The sound drop target changed; drop the files again.");
            continue;
        }
        if (origin.slots_mode)
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
    const auto revision = history.document.revision;
    if (!wormedit::apply_workspace_action(next_workspace, history.document, revision,
            wormedit::workspace_select_chart_action_t{chart_id}) ||
        !wormedit::apply_workspace_action(next_workspace, history.document, revision,
            wormedit::workspace_switch_tab_action_t{wormedit::workspace_tab_t::compose}))
        return post_callback_feedback(notifications, wormedit::notification_severity_t::warning,
        wormedit::notification_source_t::input, "The validation target cannot be opened in Compose.");
    workspace = std::move(next_workspace);
    interaction = std::move(candidate);
    return post_callback_feedback(notifications, wormedit::notification_severity_t::hint,
        wormedit::notification_source_t::input, "Moved to validation diagnostic.");
}

template <typename Context>
void dispatch_audio_offset_lock_feedback(Context& context, const WormUiWorkspaceAction& action) {
    const auto was_locked = context.application_command_state_ref.audio_offset_locked;
    const auto feedback = toggle_compose_audio_offset_lock(action, context.history_ref, context.workspace_ref,
        context.application_command_state_ref, context.compose_scene_cache_ref,
        context.audio_offset_drag_ref, context.audio_offset_drag_tab_ref);
    context.notification_sink_ref.post(was_locked != context.application_command_state_ref.audio_offset_locked
        ? wormedit::notification_severity_t::success : wormedit::notification_severity_t::warning,
        wormedit::notification_source_t::edit, feedback);
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

template <typename Context>
void dispatch_workspace_frame_action(Context& context, const WormUiWorkspaceAction& action,
                                     bool shortcut_handled,
                                     std::optional<std::uint32_t> sample_rate,
                                     int compose_viewport_height_pixels) {
    auto& running = context.running_ref;
    auto& history = context.history_ref;
    auto& workspace = context.workspace_ref;
    auto& application_command_state = context.application_command_state_ref;
    auto& stepmania_export_policy = context.stepmania_export_policy_ref;
    auto& audio_offset_drag = context.audio_offset_drag_ref;
    auto& audio_offset_drag_tab = context.audio_offset_drag_tab_ref;
    auto& command_registry = context.command_registry_ref;
    auto& compose_interaction_state = context.compose_interaction_state_ref;
    auto& metadata_drop = context.metadata_drop_ref;
    auto& metadata_asset_runtime = context.metadata_asset_runtime_ref;
    auto& asset_base = context.asset_base_ref;
    auto& resource_copy_scheduler = context.resource_copy_scheduler_ref;
    auto& resource_copy_runtime = context.resource_copy_runtime_ref;
    auto& export_runtime = context.export_runtime_ref;
    auto& timing_grid = context.timing_grid_ref;
    auto& timing_edit = context.timing_edit_ref;
        if (action.kind == WORM_UI_WORKSPACE_ACTION_QUIT) {
            running = false;
        } else if (dispatch_project_session_action(context, action)) {
            // Project transitions are handled before editor actions.
        } else if (dispatch_recovery_workspace_action(context, action)) {
            // Recovery can open a document from the empty landing state.
        } else if (!context.session_loaded_ref) {
            // The landing view only accepts project and recovery actions.
        } else if (shortcut_handled) {
            // Registry commands take precedence over widget actions emitted
            // by the same input frame, preventing duplicate mutations.
        } else if (dispatch_clipboard_menu(context, action)) {
            // Clipboard menu actions use the same typed registry as shortcuts.
        } else if (dispatch_reviewed_sound_edit(action, history, workspace,
                       application_command_state, compose_interaction_state, context.notification_sink_ref)) {
            // Properties and reviewed imports share a revision-checked sound edit boundary.
        } else if (action.compose_action == WORM_UI_COMPOSE_ACTION_TOGGLE_AUDIO_OFFSET_LOCK) {
            dispatch_audio_offset_lock_feedback(context, action);
        } else if (action.audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE) {
            static_cast<void>(dispatch_audio_offset_drag_action(
                action, history, workspace, application_command_state.audio_offset_edits_locked(),
                sample_rate, audio_offset_drag, audio_offset_drag_tab, context.notification_sink_ref));
        } else if (const auto common_status = wormedit::sys::dispatch_transport_player_action(
                       action, application_command_state, stepmania_export_policy, context.notification_sink_ref)) {
            // Typed producer callbacks already publish this transport outcome.
        } else if (action.kind == WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE) {
            static_cast<void>(dispatch_sound_usage_jump(action, history, workspace, compose_interaction_state,
                                                        context.notification_sink_ref));
        } else if (action.kind == WORM_UI_WORKSPACE_ACTION_COMPARE_DIAGNOSTIC_JUMP) {
            static_cast<void>(dispatch_compare_diagnostic_jump(action, history, workspace,
                compose_interaction_state, context.notification_sink_ref));
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
                context.notification_sink_ref.post_transient_hint(
                    wormedit::notification_source_t::input, "No beat-anchored bookmarks or section boundaries are available.");
            } else if (dispatched.status != wormedit::command_dispatch_status_t::invoked &&
                       !dispatched.diagnostics.empty()) {
                post_shortcut_rejection(context.notification_sink_ref, command_registry, dispatched);
            }
        } else if (const auto selection_status =
                       dispatch_workspace_selection_action(action, history, workspace, context.notification_sink_ref)) {
            static_cast<void>(selection_status);
        } else if (const auto metadata_status = wormedit::sys::dispatch_metadata_action(
                       action, history, workspace, application_command_state.audio_offset_edits_locked(),
                       metadata_drop, metadata_asset_runtime, asset_base,
                       resource_copy_scheduler, resource_copy_runtime.busy(),
                       export_runtime.busy(), context.notification_sink_ref)) {
            static_cast<void>(metadata_status);
        } else if (dispatch_timing_workspace_action(action, history, workspace,
                                                   application_command_state, timing_grid,
                                                   timing_edit, context.notification_sink_ref)) {
            // The timing producer publishes feedback and consumes this action.
        } else if (wormedit::sys::dispatch_compose_workspace_action(
                       action, history, workspace, compose_interaction_state,
                       compose_viewport_height_pixels, application_command_state, asset_base,
                       resource_copy_scheduler, resource_copy_runtime, export_runtime,
                       metadata_asset_runtime.import_pending(), metadata_drop,
                       metadata_asset_runtime, context.notification_sink_ref)) {
            // The Compose producer publishes feedback and consumes this action.
        }
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

struct notification_frame_context_t {
    wormedit::notification_center_t& center;
    wormedit::sys::notification_history_runtime_t& runtime;
    wormedit::document_history_t& history;
    wormedit::workspace_controller_t& workspace;
    std::optional<wormedit::editor_interaction_state_t>& interaction;
    const wormedit::notification_sink_t& notifications;
};

void copy_notification_details(notification_frame_context_t& context, std::uint64_t id) {
    const auto* entry = wormedit::sys::find_notification(context.center, id);
    if (!entry) {
        context.notifications.post(wormedit::notification_severity_t::error,
            wormedit::notification_source_t::input, "Notification no longer exists");
        return;
    }
    const auto& text = entry->detail.empty() ? entry->message : entry->detail;
    const bool copied = SDL_SetClipboardText(text.c_str());
    const auto message = copied ? "Notification details copied."
                            : "Could not copy notification details to the clipboard.";
    context.notifications.post(copied ? wormedit::notification_severity_t::success
                              : wormedit::notification_severity_t::error,
        wormedit::notification_source_t::input, message);
}

void apply_notification_action(notification_frame_context_t& context,
                                const WormUiNotificationAction& action) {
    if (context.runtime.apply(action, context.center)) return;
    if (action.kind == WORM_UI_NOTIFICATION_ACTION_COPY_DETAILS)
        copy_notification_details(context, action.id);
    else if (action.kind == WORM_UI_NOTIFICATION_ACTION_GO_TO) {
        const auto error = wormedit::sys::go_to_notification(context.center, action.id,
            context.history, context.workspace, context.interaction);
        const auto message = error ? *error : "Moved to notification target.";
        context.notifications.post(error ? wormedit::notification_severity_t::error
                                 : wormedit::notification_severity_t::success,
            wormedit::notification_source_t::document, message);
    }
}

void begin_notification_frame(notification_frame_context_t& context,
                              WormUiNotificationAction& pending, wormedit::notification_time_t now) {
    apply_notification_action(context, pending);
    pending = {};
    context.center.drain_inbox(now);
}

auto close_confirmation_view(const pending_close_t& pending, bool has_native_path)
    -> WormUiCloseConfirmationView {
    return {.visible = pending.target != close_target_t::none && !pending.awaiting_encoding,
        .application = pending.target == close_target_t::application, .has_native_path = has_native_path,
        .project_id = pending.project.value, .revision = pending.revision, .status_text = pending.save_failure.c_str()};
}

template <typename Model, typename Context>
auto handle_workspace_view_error(const Model& model, Context& context,
    const WormUiNotificationView& notifications, WormUiNotificationAction& pending_notification,
    pending_close_t& pending_close, bool confirm,
    wormedit::sys::projection_notification_feedback_t& feedback) -> bool {
    const wormedit::sys::projection_identity_t identity{context.history_ref.document.id,
        context.history_ref.document.revision,
        context.workspace_ref.selected_chart.value_or(wormedit::chart_id_t{}),
        context.project_chart_runtime_ref.session_generation};
    if (model) {
        feedback.publish(identity, {}, context.notification_sink_ref);
        return false;
    }
    const auto diagnostic = model.diagnostics.empty() ? std::string{"Workspace view unavailable"}
        : model.diagnostics.front().message;
    feedback.publish(identity, {{wormedit::sys::projection_feedback_kind_t::workspace_view, 0,
        wormedit::sys::projection_diagnostic_severity(model.diagnostics),
        wormedit::notification_source_t::document, diagnostic}}, context.notification_sink_ref);
    const auto close = close_confirmation_view(pending_close, context.native_project_loaded_ref &&
        !context.active_project_path_ref.empty());
    const auto action = worm_ui_frontend_workspace_error(diagnostic.c_str(), &notifications, &close);
    pending_notification = action.notifications;
    if (dispatch_close_confirmation(pending_close, context, action, confirm)) return true;
    if (action.kind == WORM_UI_WORKSPACE_ACTION_SWITCH_TAB) {
        const auto changed = wormedit::apply_workspace_action(context.workspace_ref,
            context.history_ref.document, context.history_ref.document.revision,
            wormedit::workspace_switch_tab_action_t{wormedit::workspace_tab_t::setup});
        if (!changed && !changed.diagnostics.empty())
            context.notification_sink_ref.post(wormedit::sys::projection_diagnostic_severity(changed.diagnostics),
                wormedit::notification_source_t::document, changed.diagnostics.front().message);
    }
    return true;
}

auto retained_compose_geometry(const compose_scene_cache_t& cache)
    -> wormedit::editor_measure_geometry_t {
    return {cache.chart, cache.revision,
            cache.cached_measure_index ? &*cache.cached_measure_index : nullptr};
}

void prepare_bound_workspace_frame(
    const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    application_command_state_t& application_command_state, timing_grid_state_t& timing_grid,
    std::optional<wormedit::audio_offset_drag_t>& audio_offset_drag,
    std::optional<wormedit::workspace_tab_t>& audio_offset_drag_tab,
    compose_scene_cache_t& compose_scene_cache) {
    wormedit::sys::frame_phase_scope_t preparation(
        wormedit::sys::frame_phase_t::workspace_preparation);
    if (workspace.selected_chart) {
        const auto chart =
            std::ranges::find(history.document.charts, *workspace.selected_chart,
                              &wormedit::chart_document_t::id);
        if (chart != history.document.charts.end()) {
            timing_grid = refresh_timing_grid(*chart, application_command_state);
        }
    }
    if (audio_offset_drag &&
        (!audio_offset_drag_tab || workspace.active_tab != *audio_offset_drag_tab ||
         !workspace.selected_chart || *workspace.selected_chart != audio_offset_drag->chart ||
         history.document.revision != audio_offset_drag->document_revision ||
         application_command_state.audio_offset_edits_locked())) {
        audio_offset_drag.reset();
        audio_offset_drag_tab.reset();
    }
    if (compose_scene_cache.beat_fit_drag &&
        (workspace.active_tab != wormedit::workspace_tab_t::compose ||
         !workspace.selected_chart ||
         *workspace.selected_chart != compose_scene_cache.beat_fit_drag->chart ||
         history.document.revision != compose_scene_cache.beat_fit_drag->document_revision ||
         workspace.transport.audio_asset != compose_scene_cache.beat_fit_asset ||
         application_command_state.timing_locked)) {
        compose_scene_cache.clear_beat_fit_capture();
    }
    if (compose_scene_cache.offset_fit_drag &&
        (workspace.active_tab != wormedit::workspace_tab_t::compose ||
         !workspace.selected_chart ||
         *workspace.selected_chart != compose_scene_cache.offset_fit_drag->beat.chart ||
         history.document.revision !=
             compose_scene_cache.offset_fit_drag->beat.document_revision ||
         workspace.transport.audio_asset != compose_scene_cache.offset_fit_drag->asset ||
         application_command_state.audio_offset_edits_locked())) {
        compose_scene_cache.offset_fit_drag.reset();
    }
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
    return std::ranges::any_of(document.resources, [&](const auto& resource) {
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
        target.apply_revision = proposal->document_revision;
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
    cache.tap_apply_proposal = runtime.apply_proposal();
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
        [](std::uint64_t target, const auto& entry) { return target < entry.first; });
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
    const auto error = wormedit::sys::toggle_audition_on_click(program, state.editor_preferences.audition_on_click);
    if (!error.empty())
        return wormedit::sys::reject_application_command(notifications, error,
            wormedit::notification_severity_t::error, wormedit::notification_source_t::system);
    notifications.post(wormedit::notification_severity_t::success, wormedit::notification_source_t::input,
        state.editor_preferences.audition_on_click ? "Play sound on click enabled." : "Play sound on click disabled.");
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
    const auto created = wormedit::apply_add_chart_command(history, history.document.revision,
        workspace.selected_chart.value_or(wormedit::chart_id_t{}));
    if (!created) return wormedit::sys::reject_application_command(notifications,
        created.command.diagnostics.empty() ? "New chart could not be created." : created.command.diagnostics.front().message,
        created.command.diagnostics.empty() ? severity_t::warning
            : notification_severity_for(created.command.diagnostics.front().severity), source_t::document);
    const auto rebound = wormedit::rebind_workspace(workspace, history);
    if (!rebound) return wormedit::sys::reject_application_command(notifications,
        "New chart created, but workspace rebinding failed.", severity_t::error, source_t::document);
    if (application_command_state.transport_stop) application_command_state.transport_stop();
    const auto error = open_project_chart(history, workspace, compose_interaction_state, *created.chart);
    if (!error.empty()) return wormedit::sys::reject_application_command(notifications, error,
                                                                       severity_t::error, source_t::document);
    remember_snap_denominator(application_command_state, *created.chart,
        application_command_state.editor_preferences.default_snap);
    notifications.post(severity_t::success, source_t::document, "New chart created.");
    return wormedit::command_invocation_result_t{.accepted = true};
}

auto dispatch_registered_application_command(
    const wormedit::command_invocation_context_t& invocation,
    wormedit::sys::program_preferences_runtime_t& program,
    wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& compose_interaction_state,
    std::optional<wormedit::timing_lane_pseudonote_preview_t>& timing_stage,
    application_command_state_t& application_command_state, wormedit::notification_sink_t& notifications,
    wormedit::sys::tap_timing_runtime_t& tap_runtime,
    const std::function<wormedit::sys::tap_timing_clock_t()>& tap_clock,
    const std::optional<std::uint64_t>& tap_event_ns) {
    using severity_t = wormedit::notification_severity_t;
    using source_t = wormedit::notification_source_t;
    if (auto toggled = dispatch_click_audition_toggle(invocation, program, application_command_state,
            notifications)) return *toggled;
    const auto command = parse_application_command(invocation.command.value);
    if (!command) {
        return wormedit::sys::reject_application_command(
            notifications, "Command is not implemented by the application.", severity_t::error, source_t::system);
    }
    if (command->kind == wormedit::sys::application_command_kind_t::document_new_project) {
        if (!worm_ui_frontend_request_new_project())
            return wormedit::sys::reject_application_command(notifications, "Finish the open project dialog first.",
                                                             severity_t::warning, source_t::document);
        notifications.post_transient_hint(source_t::document, "Choose the layout for the new project.");
        return wormedit::command_invocation_result_t{.accepted = true};
    }
    if (command->kind == wormedit::sys::application_command_kind_t::document_save_project) {
        if (!worm_ui_frontend_request_save_project(history.document.id.value))
            return wormedit::sys::reject_application_command(notifications, "Finish the open dialog first.",
                                                             severity_t::warning, source_t::document);
        return wormedit::command_invocation_result_t{.accepted = true};
    }
    if (command->kind == wormedit::sys::application_command_kind_t::document_new_chart)
        return create_chart_from_registered_command(history, workspace, compose_interaction_state,
                                                    application_command_state, notifications);
    if (command->kind == wormedit::sys::application_command_kind_t::timing_tap_assistant) {
        tap_runtime.toggle();
        (void)tap_runtime.observe(tap_clock());
        return wormedit::command_invocation_result_t{.accepted = true};
    }
    if (command->kind == wormedit::sys::application_command_kind_t::timing_tap) {
        const auto clock = tap_clock();
        const auto result = tap_runtime.tap(tap_event_ns.value_or(clock.observed_ns), clock);
        const auto state = tap_runtime.state();
        if (state.status) {
            const bool guidance = result == wormedit::sys::tap_timing_result_t::ignored ||
                                  result == wormedit::sys::tap_timing_result_t::not_playing;
            static_cast<void>(post_callback_feedback(notifications,
                guidance ? severity_t::hint : severity_t::warning, source_t::input, state.status));
        }
        return wormedit::command_invocation_result_t{.accepted = true};
    }
    return wormedit::sys::invoke_application_command(
        *command, history, workspace, compose_interaction_state, timing_stage,
        application_command_state, notifications);
}

template <typename Context, typename Callback>
void dispatch_open_workspace_actions(pending_close_t& pending, Context& context,
                                     const WormUiWorkspaceAction& action, bool confirm,
                                     Callback&& callback) {
    if (!dispatch_close_confirmation(pending, context, action, confirm)) callback();
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
template <typename WaveformRuntime, typename Workspace>
auto selected_waveform_rate(const WaveformRuntime& waveform, const Workspace& workspace)
    -> std::optional<std::uint32_t> {
    if (waveform.waveform_analysis() && waveform.waveform_analysis()->waveform &&
        waveform.waveform_asset() && workspace.transport.audio_asset == waveform.waveform_asset()) {
        return waveform.waveform_analysis()->waveform->sample_rate;
    }
    return std::nullopt;
}
#endif

void retain_compose_hover_timing(
    wormedit::sys::compose_scene_cache_t& cache, const WormUiWorkspaceView& view,
    const WormUiWorkspaceAction& action) {
    auto& hover = cache.hover_timing;
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
    application.bgm_bar_window_capture.reset();
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
    application.bgm_bar_window_capture = wormedit::sys::bgm_bar_window_capture_t{
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
    const wormedit::document_history_t& history, std::uint64_t session,
    wormedit::notification_sink_t& sink) {
    if (notice.update(history.document, session))
        sink.post(wormedit::notification_severity_t::warning, wormedit::notification_source_t::document,
            "Some assets are linked by absolute paths or paths outside the project folder. "
            "Moving this project to another environment may break those links. Copy the assets into the project to make it portable.");
}

} // namespace

int main(int argc, char** argv) {
    wormedit::sys::install_crash_backtrace_handlers();
    // Posting workers and their schedulers must be destroyed (and joined) before
    // this center on normal returns and during exception unwinding.
    wormedit::notification_center_t notifications;
    wormedit::notification_sink_t notification_sink{notifications};
    wormedit::sys::application_startup_arguments_t arguments;
    wormedit::sys::application_runtime_t runtime;
    if (const int startup_error = wormedit::sys::initialize_application_startup(
            argc, argv, arguments, runtime); startup_error != 0)
        return startup_error;
    auto startup_request = std::move(arguments.request);
    const auto requested_primary_path = arguments.requested_primary_path;

    const auto platform_recovery = wormedit::prepare_platform_recovery_directory(notification_sink);
    std::filesystem::path crash_marker;
    if (platform_recovery) {
        startup_request.recovery_directories.push_back(platform_recovery.path);
        crash_marker = wormedit::crash_marker_path(platform_recovery.path);
        startup_request.crash_marker_path = crash_marker;
    }
    auto startup = wormedit::prepare_native_project_startup(
        wormedit::sys::empty_session_document(new_session_project_id()),
        std::move(startup_request));
    if (!crash_marker.empty()) {
        if (const auto marker_error = wormedit::write_crash_marker(crash_marker, startup.document.id);
            !marker_error.empty())
            notification_sink.post(wormedit::notification_severity_t::warning,
                                   wormedit::notification_source_t::recovery, marker_error);
    }

    if (!wormedit::sys::initialize_application_runtime(runtime)) {
        if (!crash_marker.empty()) wormedit::remove_crash_marker(crash_marker);
        return 1;
    }
    wormedit::sys::frontend_notification_sink_t frontend_notifications{notification_sink};
    auto& window = runtime.window;

    auto asset_base =
        startup.loaded_primary ? std::filesystem::absolute(requested_primary_path).parent_path()
                               : std::filesystem::path{};
    wormedit::sys::refresh_project_audio_availability(startup.document, asset_base);
    auto history = wormedit::make_document_history(std::move(startup.document), 128, true);
    auto binding = wormedit::make_workspace_controller(history);
    if (!binding) {
        std::fprintf(stderr, "Initial workspace binding failed\n");
        wormedit::sys::shutdown_application_runtime(runtime);
        return 1;
    }
    auto workspace = std::move(*binding.workspace);
    const auto initial_autosave_sequence = wormedit::native_project_recovery_session_sequence(
        startup.recovery_scan, history.document.id);
    wormedit::task_scheduler_t autosave_scheduler(1, 1, wormedit::background_scheduler_options());
    wormedit::native_project_autosave_loop_t autosave_loop(
        wormedit::native_project_autosave_loop_t::clock_t::now(), autosave_scheduler,
        startup.primary_version, initial_autosave_sequence,
        std::chrono::minutes{5}, std::chrono::seconds{30}, notification_sink);
    post_initial_application_feedback(notification_sink, startup.loaded_primary, requested_primary_path, platform_recovery);
    std::string close_feedback;
    wormedit::sys::workspace_edit_feedback_t workspace_edit_feedback;
    wormedit::sys::projection_notification_feedback_t projection_feedback;
    wormedit::sys::projection_notification_feedback_t workspace_view_feedback;
    wormedit::sys::notification_history_runtime_t notification_history;
    WormUiNotificationAction pending_notification{};
    seed_notification_smoke(notifications);
    seed_notification_history_smoke(notifications, history.document);
    bool session_loaded = startup.loaded_primary;
    bool native_project_loaded = startup.loaded_primary;
    bool fresh_blank_project = false;
    std::string pending_audio_project_path;
    std::string pending_chart_project_path;
    wormedit::sys::sound_drop_transaction_t sound_drop_transaction;
    std::vector<wormedit::sys::sound_drop_batch_t> pending_sound_batches;
    std::vector<std::string> pending_bga_paths;
    wormedit::sys::bga_dock_runtime_t bga_dock_runtime;
    std::string pending_bms_encoding_path;
    std::filesystem::path active_project_path =
        startup.loaded_primary ? requested_primary_path : std::filesystem::path{};
    std::optional<wormedit::audio_offset_drag_t> audio_offset_drag;
    std::optional<wormedit::workspace_tab_t> audio_offset_drag_tab;
    wormedit::task_scheduler_t resource_copy_scheduler(1, 1, wormedit::background_scheduler_options());
    resource_copy_t resource_copy_runtime{notification_sink};
    wormedit::sys::runtime::metadata_asset_t metadata_asset_runtime{notification_sink};
    output_group_export_t export_runtime{notification_sink};
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    wormedit::visual::image_cache_t player_image_cache;
    wormedit::task_scheduler_t player_image_scheduler(1, 4, wormedit::background_scheduler_options());
    wormedit::visual::image_job_t player_image_job{player_image_cache, player_image_scheduler,
                                                   WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER_CAPACITY +
                                                       WORM_UI_PLAYER_PREVIEW_STORYBOARD_CAPACITY};
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    wormedit::visual::video_frame_cache_t player_video_cache;
    wormedit::task_scheduler_t player_video_scheduler(1, 2, wormedit::background_scheduler_options());
    wormedit::visual::video_frame_job_t player_video_job{player_video_cache,
                                                         player_video_scheduler};
#endif
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    wormedit::audio::decoded_source_registry_t audition_sources;
    wormedit::audio::decoded_block_cache_t audition_cache;
    wormedit::audio::transport_t audition_transport{audition_cache};
    wormedit::audio::audition_transport_bridge_t audition_bridge{audition_transport};
    wormedit::audio::object_audition_controller_t object_audition{audition_bridge};
    wormedit::audio::audition_session_t audition_session{audition_sources, audition_transport,
                                                         audition_bridge};
    wormedit::sys::sdl_audition_output_t audition_output{audition_session};
    wormedit::audio::decoded_block_cache_t preview_cache{
        wormedit::audio::preview_block_cache_limits()};
    wormedit::sys::runtime::waveform_analysis_t waveform_runtime{audition_sources, &preview_cache, notification_sink};
    wormedit::audio::transport_t preview_transport{preview_cache};
    wormedit::audio::preview_transport_bridge_t preview_bridge{preview_transport};
    wormedit::audio::preview_session_t preview_session{audition_sources, preview_transport,
                                                       preview_bridge};
    wormedit::audio::resident_sample_store_t resident_samples;
    resident_samples.set_listener([&](wormedit::audio::resident_snapshot_ptr_t snapshot) {
        audition_transport.set_resident_snapshot(snapshot);
        preview_transport.set_resident_snapshot(std::move(snapshot));
    });
    wormedit::sys::sdl_preview_output_t preview_output{preview_session};
    wormedit::sys::runtime::chart_preview_t preview_state;
    std::vector<wormedit::audio::preview_metronome_source_binding_t> preview_metronome_bindings;
    std::optional<wormedit::song_id_t> audition_source_project;
    std::vector<wormedit::sys::audition_asset_entry_t> audition_asset_inventory;
    std::uint64_t audition_source_generation{};
    wormedit::task_scheduler_t audition_probe_scheduler(2, 4, wormedit::background_scheduler_options());
    wormedit::sys::resident_sample_loader_t resident_loader{resident_samples,
                                                           audition_probe_scheduler};
    resident_loader.set_head_cache(&preview_cache);
    const wormedit::sys::audition_source_reconciliation_context_t audition_reconciliation{
        history.document,
        asset_base,
        audition_source_project,
        audition_asset_inventory,
        audition_source_generation,
        audition_sources,
        preview_metronome_bindings,
        [&] {
            static_cast<void>(audition_output.stop());
            static_cast<void>(preview_output.stop());
        },
        [&] { waveform_runtime.reset_sources(); },
        [&] { preview_state.invalidate(); },
        &audition_probe_scheduler,
        &resident_loader,
    };
    post_audio_inventory_feedback(
        wormedit::sys::reconcile_audition_sources(audition_reconciliation, false), notification_sink);
    auto audition_reconciled_revision = history.document.revision;
    std::string audition_output_error;
    if (!audition_output.open(&audition_output_error)) {
        notification_sink.post(wormedit::notification_severity_t::error,
            wormedit::notification_source_t::audio, "Audio output unavailable: " + audition_output_error);
    }
    std::string preview_output_error;
    if (!preview_output.open(&preview_output_error)) {
        notification_sink.post(wormedit::notification_severity_t::error,
            wormedit::notification_source_t::audio, "Chart preview output unavailable: " + preview_output_error);
    }
#endif
    wormedit::native_project_recovery_controller_t recovery_controller;
    install_startup_recovery_scan(recovery_controller, startup);
    wormedit::sys::recovery_scan_notification_publisher_t recovery_scan_feedback;
    recovery_scan_feedback.publish(recovery_controller.view(), notification_sink);
    const auto recovery_scan_configured = startup.recovery_scan_configured;
    auto scanned_primary_version = startup.primary_version;

    timing_scene_cache_t timing_scene_cache;
    timing_edit_t timing_edit;
    compose_scene_cache_t compose_scene_cache;
    compare_scene_cache_t compare_scene_cache;
    wormedit::frame_arena_t frame_projection_arena({
        .chunk_bytes = 1024 * 1024, .maximum_bytes = 16 * 1024 * 1024});
    wormedit::sys::player_preview_presenter_t player_preview_presenter;
    std::optional<wormedit::editor_interaction_state_t> compose_interaction_state;
    wormedit::viewport_input_controller_t compose_viewport_input;
    wormedit::editor_navigation_input_controller_t compose_navigation_input;
    application_command_state_t application_command_state;
    application_command_state.open_bookmark_chooser = worm_ui_frontend_open_bookmark_chooser;
    application_command_state.open_sound_picker = worm_ui_frontend_open_sound_picker;
    application_command_state.compose_dock_command = worm_ui_frontend_compose_dock_command;
    application_command_state.open_project_export = worm_ui_frontend_open_project_export;
    application_command_state.compose_left_dock_pixels = worm_ui_frontend_compose_left_dock_pixels;
    application_command_state.compose_bgm_lane_width = worm_ui_frontend_compose_bgm_lane_width;
    external_player_runtime_t external_player_runtime;
    wormedit::sys::project_chart_runtime_t project_chart_runtime;
    wormedit::sys::chart_import_session_t chart_import;
    wormedit::sys::options_runtime_t options_runtime;
    shortcut_editor_runtime_t shortcut_editor;
    pending_close_t pending_close;
    auto program_preferences = wormedit::sys::load_program_preferences_runtime();
    remember_startup_file(program_preferences, startup.loaded_primary, requested_primary_path);
    initialize_program_preferences(program_preferences, external_player_runtime, application_command_state, notification_sink);
    worm_ui_frontend_select_field_scheme(program_preferences.preferences.display.field_scheme.c_str());
    update_scratch_presentation(program_preferences.preferences.display, application_command_state);
    worm_ui_frontend_set_snap_colored_grid(program_preferences.preferences.display.snap_colored_grid);
    worm_ui_frontend_set_snap_grid_opacity(program_preferences.preferences.display.snap_grid_opacity_percent);
    worm_ui_frontend_set_field_waveform_opacity(program_preferences.preferences.display.field_waveform_opacity_percent);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    resident_loader.set_budget_bytes(static_cast<std::size_t>(program_preferences.preferences.audio.keysound_budget_mib) * 1024ULL * 1024ULL);
#endif
    (void)autosave_loop.set_interval(std::chrono::minutes{program_preferences.preferences.general.autosave_minutes});
    application_command_state.external_player = [&](wormedit::sys::external_player_action_t action) {
        return launch_external_player(external_player_runtime, action, history, workspace,
                                      compose_interaction_state, asset_base, notification_sink);
    };
    wormedit::sys::inline_preview_cursor_t inline_preview_cursor;
    wormedit::stepmania_export_policy_t stepmania_export_policy;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    configure_transport_commands(application_command_state, history, workspace,
                                 compose_interaction_state, compose_scene_cache, inline_preview_cursor,
                                 audition_session, preview_session, preview_state, audition_sources,
                                 preview_metronome_bindings, audition_output, preview_output, notification_sink);
#endif
    initialize_preferred_transport(program_preferences.preferences, application_command_state);
    wormedit::sys::tap_timing_runtime_t tap_runtime;
    std::optional<wormedit::sys::tap_timing_assist_publication_t> published_assist;
    const auto publish_tap_assist = [&] {
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        publish_tap_assist_grid(tap_runtime, preview_session, published_assist);
#endif
    };
    std::optional<std::uint64_t> tap_event_ns;
    std::uint64_t tap_epoch{};
    std::uint64_t tap_epoch_begin_ns{};
    tap_clock_context_t tap_clock_context{tap_epoch, tap_epoch_begin_ns,
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        history.document, preview_state, preview_bridge, preview_output,
#endif
    };
    const std::function<wormedit::sys::tap_timing_clock_t()> tap_clock = [&] {
        return read_tap_clock(tap_clock_context);
    };
    timing_grid_state_t timing_grid;
    auto command_registry = wormedit::make_default_editor_command_registry(command_menu_invoker(
        [&](const wormedit::command_invocation_context_t& invocation) {
            return dispatch_options_or_editor_command(invocation, options_runtime,
                program_preferences.preferences, notification_history, notifications, [&] {
                    return dispatch_registered_application_command(
                        invocation, program_preferences, history, workspace, compose_interaction_state, timing_edit.stage,
                        application_command_state, notification_sink, tap_runtime, tap_clock, tap_event_ns);
                });
        }));
    const auto shortcut_startup = load_preferred_shortcuts(program_preferences, command_registry);
    post_shortcut_startup_diagnostic(notification_sink, shortcut_startup);
    bool running = true;
    metadata_drop_state_t metadata_drop;
    auto compose_viewport_last_ticks = static_cast<std::uint64_t>(SDL_GetTicks());
    struct compose_frame_action_context_t {
        decltype(history)& history_ref;
        decltype(workspace)& workspace_ref;
        decltype(session_loaded)& session_loaded_ref;
        decltype(command_registry)& command_registry_ref;
        decltype(compose_interaction_state)& compose_interaction_state_ref;
        decltype(application_command_state)& application_command_state_ref;
        decltype(notification_sink)& notification_sink_ref;
        decltype(tap_event_ns)& tap_event_ns_ref;
        decltype(audio_offset_drag)& audio_offset_drag_ref;
        decltype(audio_offset_drag_tab)& audio_offset_drag_tab_ref;
        decltype(compose_scene_cache)& compose_scene_cache_ref;
        decltype(compose_viewport_input)& compose_viewport_input_ref;
        decltype(compose_navigation_input)& compose_navigation_input_ref;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        decltype(audition_sources)& audition_sources_ref;
        decltype(object_audition)& object_audition_ref;
        decltype(preview_session)& preview_session_ref;
        decltype(preview_output)& preview_output_ref;
        decltype(preview_state)& preview_state_ref;
        decltype(audition_output)& audition_output_ref;
#endif
    };
    compose_frame_action_context_t compose_frame_action_context{
        history,
        workspace,
        session_loaded,
        command_registry,
        compose_interaction_state,
        application_command_state,
        notification_sink,
        tap_event_ns,
        audio_offset_drag,
        audio_offset_drag_tab,
        compose_scene_cache,
        compose_viewport_input,
        compose_navigation_input,
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        audition_sources,
        object_audition,
        preview_session,
        preview_output,
        preview_state,
        audition_output,
#endif
    };
    struct workspace_frame_action_context_t {
        decltype(program_preferences)& program_preferences_ref;
        decltype(running)& running_ref;
        decltype(notification_sink)& notification_sink_ref;
        decltype(close_feedback)& close_feedback_ref;
        decltype(history)& history_ref;
        decltype(workspace)& workspace_ref;
        decltype(session_loaded)& session_loaded_ref;
        decltype(native_project_loaded)& native_project_loaded_ref;
        decltype(fresh_blank_project)& fresh_blank_project_ref;
        decltype(pending_audio_project_path)& pending_audio_project_path_ref;
        decltype(pending_bms_encoding_path)& pending_bms_encoding_path_ref;
        decltype(active_project_path)& active_project_path_ref;
        decltype(application_command_state)& application_command_state_ref;
        decltype(stepmania_export_policy)& stepmania_export_policy_ref;
        decltype(audio_offset_drag)& audio_offset_drag_ref;
        decltype(audio_offset_drag_tab)& audio_offset_drag_tab_ref;
        decltype(command_registry)& command_registry_ref;
        decltype(compose_interaction_state)& compose_interaction_state_ref;
        decltype(metadata_drop)& metadata_drop_ref;
        decltype(metadata_asset_runtime)& metadata_asset_runtime_ref;
        decltype(asset_base)& asset_base_ref;
        decltype(resource_copy_scheduler)& resource_copy_scheduler_ref;
        decltype(resource_copy_runtime)& resource_copy_runtime_ref;
        decltype(export_runtime)& export_runtime_ref;
        decltype(timing_grid)& timing_grid_ref;
        decltype(timing_edit)& timing_edit_ref;
        decltype(timing_scene_cache)& timing_scene_cache_ref;
        decltype(compose_scene_cache)& compose_scene_cache_ref;
        decltype(compose_viewport_input)& compose_viewport_input_ref;
        decltype(compose_navigation_input)& compose_navigation_input_ref;
        decltype(autosave_loop)& autosave_loop_ref;
        decltype(recovery_controller)& recovery_controller_ref;
        decltype(scanned_primary_version)& scanned_primary_version_ref;
        decltype(external_player_runtime)& external_player_runtime_ref;
        decltype(project_chart_runtime)& project_chart_runtime_ref;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        decltype(audition_reconciliation)& audition_reconciliation_ref;
#endif
    };
    workspace_frame_action_context_t workspace_frame_action_context{
        program_preferences,
        running,
        notification_sink,
        close_feedback,
        history,
        workspace,
        session_loaded,
        native_project_loaded,
        fresh_blank_project,
        pending_audio_project_path,
        pending_bms_encoding_path,
        active_project_path,
        application_command_state,
        stepmania_export_policy,
        audio_offset_drag,
        audio_offset_drag_tab,
        command_registry,
        compose_interaction_state,
        metadata_drop,
        metadata_asset_runtime,
        asset_base,
        resource_copy_scheduler,
        resource_copy_runtime,
        export_runtime,
        timing_grid,
        timing_edit,
        timing_scene_cache,
        compose_scene_cache,
        compose_viewport_input,
        compose_navigation_input,
        autosave_loop,
        recovery_controller,
        scanned_primary_version,
        external_player_runtime,
        project_chart_runtime,
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        audition_reconciliation,
#endif
    };
    notification_frame_context_t notification_context{notifications, notification_history,
        history, workspace, compose_interaction_state, notification_sink};
    wormedit::sys::project_portability_notice_t portability_notice;
    while (running) {
        publish_project_portability_notice(portability_notice, history,
            project_chart_runtime.session_generation, notification_sink);
        const auto frame_start_ns = SDL_GetTicksNS();
        const auto notification_now = notification_frame_time();
        recovery_scan_feedback.publish(recovery_controller.view(), notification_sink);
        begin_notification_frame(notification_context, pending_notification, notification_now);
        wormedit::sys::frame_timing().begin_frame();
        frame_projection_arena.reset();
        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::events);
        {
            wormedit::sys::frame_phase_scope_t input(wormedit::sys::frame_phase_t::events_input);
            if (!wormedit::sys::poll_frontend_events(
                    window, metadata_drop, workspace, history, asset_base,
                    can_offer_audio_project_drop(session_loaded, fresh_blank_project, history),
                    pending_audio_project_path, pending_chart_project_path, sound_drop_transaction, pending_sound_batches, pending_bga_paths,
                    notification_sink))
                confirm_unsaved_changes(pending_close, close_target_t::application, workspace_frame_action_context,
                    program_preferences.preferences.general.confirm_unsaved_close);
        }
        {
            wormedit::sys::frame_phase_scope_t drops(wormedit::sys::frame_phase_t::events_drops);
            import_dropped_keysounds(session_loaded, history, workspace, pending_sound_batches, notification_sink);
            import_dropped_bga(session_loaded, history, workspace, pending_bga_paths, notification_sink);
            dispatch_dropped_chart_project(workspace_frame_action_context,
                pending_chart_project_path, pending_close,
                program_preferences.preferences.general.confirm_unsaved_close);
        }
        double compose_viewport_delta_seconds{};
        {
            wormedit::sys::frame_phase_scope_t jobs(wormedit::sys::frame_phase_t::events_jobs);
            wormedit::sys::poll_application_jobs(metadata_asset_runtime, resource_copy_runtime,
                                                 export_runtime, history, workspace, asset_base);
            compose_viewport_delta_seconds =
                wormedit::sys::compose_frame_delta_seconds(compose_viewport_last_ticks);
            tick_loaded_project_autosave(session_loaded, autosave_loop, history,
                                         native_project_loaded, active_project_path,
                                         platform_recovery.path, notification_sink, asset_base);
        }
        wormedit::sys::frame_timing().restore(wormedit::sys::frame_phase_t::count);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::audio_service);
        reconcile_audition_sources_after_edit(audition_reconciliation, history.document.revision,
                                              audition_reconciled_revision, notification_sink);
        if (const auto failed = wormedit::sys::drain_audition_probe_failures(audition_sources)) {
            notification_sink.post(*failed);
        }
        if (const auto streaming = resident_loader.drain_report()) {
            notification_sink.post(*streaming);
        }
        if (const auto loading = resident_loader.drain_progress()) {
            notification_sink.post(*loading);
        }
        sync_layer_assist_selection(application_command_state, workspace, preview_state, preview_session);
        sync_bgm_mix_selection(application_command_state, history, workspace, preview_state, preview_session);
        wormedit::sys::service_audio_transport_frame(
            history.document, workspace, preview_state, preview_session,
            audition_output, preview_output,
            application_command_state.preview_playhead_absolute_row, notification_sink);
        wormedit::sys::frame_timing().restore(wormedit::sys::frame_phase_t::count);
        waveform_runtime.service(
            history.document, workspace, compose_scene_cache,
            compose_interaction_state ? &*compose_interaction_state : nullptr);
        compose_scene_cache.fit_waveform = waveform_runtime.waveform_analysis();
        compose_scene_cache.fit_waveform_asset = waveform_runtime.waveform_asset();
#endif

        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::workspace);
        const auto field_skin_lanes = configure_field_skin(
            active_field_skin_layout(history.document, workspace, compose_interaction_state),
            options_runtime, program_preferences.preferences.display, application_command_state);
        wormedit::workspace_view_result_t model;
        {
            wormedit::sys::frame_phase_scope_t preparation(
                wormedit::sys::frame_phase_t::workspace_preparation);
            cancel_stale_compose_interaction(compose_interaction_state, history, workspace);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            if (workspace.active_tab == wormedit::workspace_tab_t::compose &&
                compose_interaction_state &&
                application_command_state.preview_playhead_absolute_row &&
                (preview_output.playing() || preview_state.paused)) {
                wormedit::sys::frame_phase_scope_t follow(
                    wormedit::sys::frame_phase_t::workspace_follow);
                (void)wormedit::sys::follow_compose_playhead(
                    history, *compose_interaction_state,
                    *application_command_state.preview_playhead_absolute_row,
                    preview_output.playing(),
                    retained_compose_geometry(compose_scene_cache),
                    inline_preview_cursor.follow_anchor_fraction.value_or(
                        application_command_state.editor_preferences.playhead_anchor_percent / 100.0));
            }
#endif

            wormedit::sys::rebind_workspace_after_edit(workspace, history, notification_sink, workspace_edit_feedback);
            model = wormedit::sys::build_workspace_frame_model(
                compare_scene_cache, workspace, history.document, project_chart_runtime.session_generation);
        }
        timing_edit.cancel_if_stale(history, workspace, application_command_state);
        const auto notification_view = project_application_notifications(
            notifications, notification_now, compose_interaction_state, notification_history,
            history.document, project_chart_runtime.session_generation);
        if (!handle_workspace_view_error(model, workspace_frame_action_context, notification_view,
                pending_notification, pending_close, program_preferences.preferences.general.confirm_unsaved_close, workspace_view_feedback)) {
            prepare_bound_workspace_frame(history, workspace, application_command_state,
                                          timing_grid, audio_offset_drag, audio_offset_drag_tab,
                                          compose_scene_cache);
            prepare_tap_proposal(tap_runtime, tap_clock, history.document,
                application_command_state.audio_offset_edits_locked(), compose_scene_cache);
            wormedit::sys::compose_bga_move_preview_input_t bga_move_preview{};
            bga_move_preview.project_id = history.document.id.value;
            worm_ui_frontend_bga_move_preview(&bga_move_preview.chart_id,
                &bga_move_preview.document_revision, &bga_move_preview.event_id,
                &bga_move_preview.y_pixels);
            auto flattened = wormedit::sys::project_application_frame(
                {.model = *model.view,
                 .history = history,
                 .workspace = workspace,
                 .frame_arena = &frame_projection_arena,
                 .recovery_controller = recovery_controller,
                 .recovery_scan_configured = recovery_scan_configured,
                 .stepmania_export_policy = stepmania_export_policy,
                 .asset_base = asset_base,
                 .timing_grid = timing_grid,
                 .timing_scene_cache = timing_scene_cache,
                 .timing_edit = timing_edit,
                 .compose_scene_cache = compose_scene_cache,
                 .player_preview_presenter = player_preview_presenter,
                 .application_command_state = application_command_state,
                 .compose_interaction_state = compose_interaction_state,
                 .bga_move_preview = bga_move_preview,
                 .audio_offset_drag = audio_offset_drag,
                 .audio_offset_drag_tab = audio_offset_drag_tab,
                 .metadata_drop = metadata_drop,
                 .metadata_asset_runtime = metadata_asset_runtime,
                 .window = window,
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
                 .player_image_job = player_image_job,
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
                 .player_video_job = player_video_job,
#endif
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
                 .waveform_runtime = waveform_runtime,
                 .audition_sources = audition_sources,
                 .audition_source_generation = audition_source_generation,
                 .audition_session = audition_session,
                 .preview_session = preview_session,
                 .preview_state = preview_state,
                 .preview_output_available = preview_output.available(),
#endif
                });
            wormedit::sys::flattened_workspace_t compare_first_pane(&frame_projection_arena);
            projection_feedback.publish({history.document.id, history.document.revision,
                workspace.selected_chart.value_or(wormedit::chart_id_t{}), project_chart_runtime.session_generation},
                flattened.projection_feedback, notification_sink);
            wormedit::sys::flattened_workspace_t compare_second_pane(&frame_projection_arena);
            int compare_width = 0;
            int compare_height = 0;
            worm_window_get_size(&window, &compare_width, &compare_height);
            compare_height += 36 - worm_ui_frontend_title_bar_height();
            wormedit::sys::populate_compare_view(
                flattened, compare_first_pane, compare_second_pane, history, workspace,
                compare_scene_cache, application_command_state, compare_width, compare_height);
            validate_minimap_start_frame(application_command_state, history, compose_interaction_state);
            flattened.view.minimap_click_moves_paused_playhead = application_command_state.minimap_click_moves_paused_playhead;
            flattened.view.transport_rate_index = static_cast<uint8_t>(application_command_state.transport_rate_index);
            flattened.view.transport_rate_available = static_cast<bool>(application_command_state.transport_set_playback_rate);
            flattened.view.transport_keep_pitch = application_command_state.transport_keep_pitch;
            flattened.view.transport_keep_pitch_available = application_command_state.transport_keep_pitch_available;
            flattened.view.notifications = notification_view;
            flattened.view.options = wormedit::sys::options_view(options_runtime);
            flattened.view.options.current_field_layout_lanes = field_skin_lanes.data();
            flattened.view.options.current_field_layout_lane_count = field_skin_lanes.size();
            attach_shortcut_rows(flattened.view.options, options_runtime, shortcut_editor, command_registry);
            attach_bgm_shortcut(flattened.view.compose, command_registry);
            flattened.view.timing_settings_visible = application_command_state.timing_settings_visible;
            flattened.view.metronome_volume = application_command_state.metronome_volume;
            flattened.view.metronome_count_in = application_command_state.metronome_count_in;
            flattened.view.chart_import = wormedit::sys::chart_import_view(chart_import, history.document.revision);
            flattened.view.close_confirmation = close_confirmation_view(pending_close,
                native_project_loaded && !active_project_path.empty());
            project_external_player_view(external_player_runtime, flattened.view);
            wormedit::sys::project_chart_view(project_chart_runtime, history.document, flattened.view.setup);
            flattened.view.setup.selected_chart_id = flattened.view.selected_chart_id;
            project_tap_timing(tap_runtime, history.document, tap_clock, command_registry, flattened.view);
            publish_tap_assist();
            wormedit::sys::project_recent_files(program_preferences, flattened.view);
            flattened.view.project_loaded = session_loaded;
            flattened.view.project_has_native_path = native_project_loaded;
            flattened.view.pending_new_project_audio_path = pending_audio_project_path.c_str();
            flattened.view.pending_bms_encoding_path = pending_bms_encoding_path.c_str();
            // Projection returns by value; refresh view pointers after any move.
            flattened.rebind_view_pointers();
            project_bga_dock(bga_dock_runtime, history, workspace, asset_base, flattened.view.compose);
            const auto frame_nuklear_previous = wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::nuklear);
            refresh_application_command_states(command_registry, history, workspace, compose_interaction_state, application_command_state);
            wormedit::sys::project_menu_commands(command_registry, flattened.view);
            project_command_menu(command_registry, flattened.view);
#if defined(WORMEDIT_HAS_MACOS_MENU)
            wormedit::sys::refresh_macos_menu_shortcuts(command_registry);
#endif
            auto action = worm_ui_frontend_compose(&flattened.view);
            refresh_sound_selection_context(action, application_command_state);
            dispatch_menu_command(action, command_registry, history, workspace,
                compose_interaction_state, application_command_state, notification_sink);
            retain_compose_hover_timing(compose_scene_cache, flattened.view, action);
            retain_compose_bgm_window_capture(application_command_state, flattened.view, action);
            pending_notification = action.notifications;
            wormedit::sys::frame_timing().restore(frame_nuklear_previous);
            {
                wormedit::sys::frame_phase_scope_t actions(
                    wormedit::sys::frame_phase_t::workspace_actions);
                std::optional<std::uint32_t> selected_waveform_sample_rate;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
                selected_waveform_sample_rate = selected_waveform_rate(waveform_runtime, workspace);
#endif
                dispatch_open_workspace_actions(pending_close, workspace_frame_action_context, action,
                    program_preferences.preferences.general.confirm_unsaved_close, [&] {
                dispatch_options_page_action(shortcut_editor, options_runtime, program_preferences, action.options,
                    external_player_runtime, application_command_state, autosave_loop, notification_sink);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
                resident_loader.set_budget_bytes(static_cast<std::size_t>(program_preferences.preferences.audio.keysound_budget_mib) * 1024ULL * 1024ULL);
#endif
                const auto tap_applied = dispatch_tap_panel_action(tap_runtime, action.tap_timing,
                    history, tap_clock(), application_command_state.audio_offset_edits_locked());
                post_tap_apply_feedback(tap_applied, notification_sink);
                wormedit::sys::apply_timing_settings_action(application_command_state, action.timing_settings);
                publish_tap_assist();
                consume_shortcut_capture(shortcut_editor, action, command_registry, program_preferences,
                    notification_sink);
                const bool shortcut_handled = process_compose_frame_action(
                    compose_frame_action_context, action, compose_viewport_delta_seconds, tap_applied);
                const bool external_handled =
                    dispatch_external_player_action(external_player_runtime, action.external_player,
                                                    application_command_state, notification_sink) ||
                    dispatch_chart_import_action(chart_import, action, history, workspace, asset_base,
                                                 notification_sink, session_loaded, program_preferences);
                const auto previous_selected_chart = workspace.selected_chart;
                const bool project_handled = dispatch_project_chart_action(project_chart_runtime, action,
                    history, workspace, compose_interaction_state, application_command_state, notification_sink,
                    shortcut_handled || external_handled, session_loaded, resource_copy_scheduler);
                remember_created_chart_snap(application_command_state, project_handled,
                    action.project_chart.kind, previous_selected_chart, workspace.selected_chart);
                apply_compare_scroll_action(compare_scene_cache, action);
                dispatch_workspace_frame_action(
                    workspace_frame_action_context, action, project_handled,
                    selected_waveform_sample_rate, flattened.view.compose.viewport_height_pixels);
                });
            }
        }

        persist_session_preferences(program_preferences, application_command_state, notification_sink);
        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::render);
        wormedit::sys::render_application_frame(window, frame_start_ns);
        wormedit::sys::frame_timing().end_frame();
    }

    resource_copy_runtime.cancel();
    metadata_asset_runtime.cancel();
    export_runtime.cancel();
    resource_copy_scheduler.shutdown();
    metadata_asset_runtime.cleanup(asset_base);
    export_runtime.shutdown();
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    preview_output.close();
    audition_output.close();
#endif
    external_player_runtime.player.cleanup();
    wormedit::sys::shutdown_application_runtime(runtime);
    if (!crash_marker.empty()) wormedit::remove_crash_marker(crash_marker);
    return 0;
}

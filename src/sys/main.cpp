#include "sys/frame_timing.h"
#include "wormedit/audio.h"
#include "wormedit/window.h"

#include "sys/application_commands.h"
#include "sys/application_startup.h"
#include "sys/application_frame_services.h"
#include "sys/application_frame_projection.h"
#include "sys/application_command_dispatch.h"
#include "sys/application_defaults.h"
#include "sys/audio_project_import.h"
#include "sys/compose_resource_presenter.h"
#include "sys/compose_view.h"
#include "sys/compose_input.h"
#include "sys/compose_actions.h"
#include "sys/application_edit_helpers.h"
#include "sys/workspace_projection.h"
#include "sys/workspace_frame_view.h"
#include "sys/transport_player_actions.h"
#include "sys/compose_minimap_navigation.h"
#include "sys/timing_view.h"
#include "sys/timing_edit_runtime.h"
#include "sys/metadata_drop_state.h"
#include "sys/metadata_asset_runtime.h"
#include "sys/metadata_actions.h"
#include "sys/metadata_view_projection.h"
#include "sys/platform_recovery.h"
#include "sys/platform_shortcut_profile.h"
#include "sys/external_player.h"
#include <SDL3/SDL_filesystem.h>
#include <wormedit/core/virtual_canvas.h>
#include "sys/project_open.h"
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
using wormedit::sys::populate_timing_view;
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
using wormedit::sys::compose_field_downscroll;
using wormedit::sys::compose_horizontal_layout;
using wormedit::sys::compose_lane_is_editable;
using wormedit::sys::compose_physical_scancode;
using wormedit::sys::compose_project_y;
using wormedit::sys::compose_source_y;
using wormedit::sys::compose_surface_height;
using wormedit::sys::editor_interaction_has_pending_gesture;
using wormedit::sys::has_audio_marker_navigation;
using wormedit::sys::has_beat_marker_navigation;
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

auto rational_as_long_double(wormedit::signed_rational_t value) -> long double {
    if (value.denominator == 0) {
        return 0.0L;
    }
    return static_cast<long double>(value.numerator) / static_cast<long double>(value.denominator);
}

void remember_snap_denominator(application_command_state_t& application_state,
                               wormedit::chart_id_t chart, std::uint64_t denominator) {
    if (chart.value != 0 && denominator != 0) {
        application_state.requested_snap_denominators.insert_or_assign(chart.value, denominator);
    }
}

auto refresh_timing_grid(const wormedit::chart_document_t& chart,
                         application_command_state_t& application_state) -> timing_grid_state_t {
    const auto snap = wormedit::filter_snap_denominators(
        chart.divisions_per_standard_measure, chart_snap_preferences,
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
    if (active_denominator == 0 || chart.divisions_per_standard_measure % active_denominator != 0) {
        return 0;
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
    const auto remainder = raw_row % step;
    auto snapped_row = raw_row - remainder;
    if (remainder >= step - remainder &&
        snapped_row <= std::numeric_limits<std::uint64_t>::max() - step) {
        snapped_row += step;
    }
    const auto last_valid_row = measures->total_rows - 1;
    if (snapped_row > last_valid_row) {
        snapped_row = last_valid_row - last_valid_row % step;
    }
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
        return timing_bpm_value_t{{120, 1}};
    case timing_lane_kind_t::scroll:
        return timing_scroll_value_t{{1, 1}};
    case timing_lane_kind_t::speed:
        return timing_speed_value_t{{1, 1}, {0, 1}, false};
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay:
        return timing_pause_value_t{{1, 1}, pause_unit_t::seconds};
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
    if ((workspace.active_tab != workspace_tab_t::timing &&
         workspace.active_tab != workspace_tab_t::compose) ||
        !workspace.selected_chart || action.timing_chart_id == 0 ||
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
                    : timing_position_from_fraction(*chart, action.timing_target_y_fraction,
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
    const auto position = timing_position_from_fraction(*found, action.timing_target_y_fraction,
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

auto parse_recipe_rational(const WormUiTimingRecipeRationalText& text)
    -> std::optional<wormedit::signed_rational_t> {
    const auto numerator = parse_timing_signed(text.numerator);
    const auto denominator = parse_timing_unsigned(text.denominator);
    if (!numerator || !denominator || *denominator == 0) {
        return std::nullopt;
    }
    return wormedit::signed_rational_t{*numerator, *denominator};
}

auto parse_recipe_unsigned_rational(const WormUiTimingRecipeRationalText& text)
    -> std::optional<wormedit::rational_t> {
    const auto numerator = parse_timing_unsigned(text.numerator);
    const auto denominator = parse_timing_unsigned(text.denominator);
    if (!numerator || !denominator || *denominator == 0) {
        return std::nullopt;
    }
    return wormedit::rational_t{*numerator, *denominator};
}

auto timing_recipe_form_valid(const WormUiTimingRecipeForm& form) -> bool {
    return form.output >= WORM_UI_TIMING_RECIPE_OUTPUT_SV &&
           form.output <= WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS &&
           form.curve >= WORM_UI_TIMING_RECIPE_CURVE_CONSTANT &&
           form.curve <= WORM_UI_TIMING_RECIPE_CURVE_BUMP &&
           form.sampling_mode >= WORM_UI_TIMING_RECIPE_SAMPLING_FIXED &&
           form.sampling_mode <= WORM_UI_TIMING_RECIPE_SAMPLING_ADAPTIVE &&
           form.fixed_extent >= WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT &&
           form.fixed_extent <= WORM_UI_TIMING_RECIPE_FIXED_STEP_ROWS;
}

auto set_timing_recipe_curve_parameters(wormedit::sv_recipe_t& candidate,
                                        const WormUiTimingRecipeForm& form) -> bool {
    using namespace wormedit;
    const auto set_parameter = [](signed_rational_t& target,
                                  const WormUiTimingRecipeRationalText& source) {
        const auto parsed = parse_recipe_rational(source);
        if (parsed) {
            target = *parsed;
        }
        return parsed.has_value();
    };
    switch (candidate.curve) {
    case sv_recipe_curve_t::constant:
        return set_parameter(candidate.parameters.value_from, form.value_from);
    case sv_recipe_curve_t::linear:
        return set_parameter(candidate.parameters.value_from, form.value_from) &&
               set_parameter(candidate.parameters.value_to, form.value_to);
    case sv_recipe_curve_t::exponential:
        return set_parameter(candidate.parameters.value_from, form.value_from) &&
               set_parameter(candidate.parameters.value_to, form.value_to) &&
               set_parameter(candidate.parameters.shape, form.shape);
    case sv_recipe_curve_t::bump:
        return set_parameter(candidate.parameters.bump_start, form.bump_start) &&
               set_parameter(candidate.parameters.bump_length, form.bump_length) &&
               set_parameter(candidate.parameters.bump_x, form.bump_x) &&
               set_parameter(candidate.parameters.bump_y, form.bump_y);
    }
    return false;
}

auto set_fixed_timing_recipe_sampling(wormedit::sv_recipe_t& candidate,
                                      const WormUiTimingRecipeForm& form) -> bool {
    if (form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT) {
        const auto count = parse_timing_unsigned(form.fixed_point_count);
        if (!count || *count > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        candidate.sampling.fixed_point_count = static_cast<std::uint32_t>(*count);
        candidate.sampling.fixed_step_rows.reset();
        return true;
    }
    const auto rows = parse_timing_unsigned(form.fixed_step_rows);
    if (!rows) {
        return false;
    }
    candidate.sampling.fixed_point_count.reset();
    candidate.sampling.fixed_step_rows = *rows;
    return true;
}

auto set_adaptive_timing_recipe_sampling(wormedit::sv_recipe_t& candidate,
                                         const WormUiTimingRecipeForm& form) -> bool {
    const auto tolerance = parse_recipe_rational(form.tolerance);
    const auto max_points = parse_timing_unsigned(form.adaptive_max_points);
    if (!tolerance || !max_points || *max_points > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    candidate.sampling.fixed_point_count.reset();
    candidate.sampling.fixed_step_rows.reset();
    candidate.sampling.tolerance = *tolerance;
    candidate.sampling.max_points = static_cast<std::uint32_t>(*max_points);
    return true;
}

auto set_timing_recipe_sampling(wormedit::sv_recipe_t& candidate,
                                const WormUiTimingRecipeForm& form) -> bool {
    candidate.sampling.mode =
        static_cast<wormedit::sv_recipe_sampling_mode_t>(form.sampling_mode);
    return candidate.sampling.mode == wormedit::sv_recipe_sampling_mode_t::fixed
               ? set_fixed_timing_recipe_sampling(candidate, form)
               : set_adaptive_timing_recipe_sampling(candidate, form);
}

auto set_timing_recipe_transition(wormedit::sv_recipe_t& candidate,
                                  const WormUiTimingRecipeForm& form) -> bool {
    if (candidate.output == wormedit::sv_recipe_output_t::speeds) {
        const auto transition = parse_recipe_unsigned_rational(form.transition_duration);
        if (!transition) {
            return false;
        }
        candidate.transition_duration = *transition;
        candidate.transition_is_seconds = form.transition_is_seconds;
    } else {
        candidate.transition_duration.reset();
        candidate.transition_is_seconds.reset();
    }
    return true;
}

auto recipe_candidate_from_ui(const WormUiTimingRecipeForm& form,
                              const wormedit::sv_recipe_t& current)
    -> std::optional<wormedit::sv_recipe_t> {
    using namespace wormedit;
    if (!timing_recipe_form_valid(form)) {
        return std::nullopt;
    }
    auto candidate = current;
    candidate.output = static_cast<sv_recipe_output_t>(form.output);
    candidate.curve = static_cast<sv_recipe_curve_t>(form.curve);
    if (!set_timing_recipe_curve_parameters(candidate, form) ||
        !set_timing_recipe_sampling(candidate, form) ||
        !set_timing_recipe_transition(candidate, form)) {
        return std::nullopt;
    }
    return candidate;
}

auto timing_recipe_range_hits(const wormedit::chart_document_t& chart, double begin_fraction,
                              double end_fraction)
    -> std::optional<std::pair<wormedit::local_position_t, wormedit::local_position_t>> {
    if (!std::isfinite(begin_fraction) || !std::isfinite(end_fraction) || begin_fraction < 0.0 ||
        begin_fraction > 1.0 || end_fraction < 0.0 || end_fraction > 1.0) {
        return std::nullopt;
    }
    const auto built = wormedit::build_measure_index(chart);
    const auto* index = std::get_if<wormedit::measure_index_t>(&built);
    if (!index || index->total_rows < 2) {
        return std::nullopt;
    }
    const auto row_at = [&](double fraction) {
        if (fraction >= 1.0) {
            return index->total_rows;
        }
        const auto scaled = std::floor(static_cast<long double>(fraction) *
                                       static_cast<long double>(index->total_rows));
        return std::min(static_cast<std::uint64_t>(std::max(0.0L, scaled)), index->total_rows - 1);
    };
    const auto position_at = [&](std::uint64_t row) -> std::optional<wormedit::local_position_t> {
        if (row == index->total_rows && !index->entries.empty()) {
            const auto& last = index->entries.back();
            return wormedit::local_position_t{last.id, last.row_capacity};
        }
        return timing_position_at_row(*index, row);
    };
    const auto begin = position_at(row_at(begin_fraction));
    const auto end = position_at(row_at(end_fraction));
    return begin && end ? std::optional{std::pair{*begin, *end}} : std::nullopt;
}

auto refresh_timing_recipe_preview(const wormedit::document_history_t& history,
                                   const wormedit::sv_recipe_draft_t& draft,
                                   std::optional<wormedit::sv_recipe_draft_preview_t>& preview,
                                   std::string& status) -> bool {
    preview = wormedit::preview_sv_recipe_draft(history, draft);
    if (!preview->diagnostics.empty()) {
        status = preview->diagnostics.front().message;
        return false;
    }
    status = "SV recipe preview updated; Apply is the only durable step.";
    return true;
}

struct TimingRecipeDispatchContext {
    const WormUiTimingRecipeAction& action;
    wormedit::document_history_t& history;
    wormedit::workspace_controller_t& workspace;
    bool timing_locked;
    std::uint64_t active_snap_denominator;
    timing_edit_t& edit;
    std::string& status;
};

auto arm_timing_recipe_selection(const TimingRecipeDispatchContext& context) -> bool {
    using namespace wormedit;
    auto& selection = context.edit.recipe_selection_binding;
    if (context.timing_locked || context.workspace.active_tab != workspace_tab_t::timing ||
        !context.workspace.selected_chart) {
        context.status = context.timing_locked
                             ? "Unlock timing before selecting an SV recipe range."
                             : "SV recipe selection requires the active Timing chart.";
        return true;
    }
    if (context.edit.recipe_draft) {
        context.status = "Apply or cancel the current SV recipe before selecting another interval.";
        return true;
    }
    selection = std::pair{*context.workspace.selected_chart,
                          context.history.document.revision};
    context.status = "Drag over the Scroll / SV lane to select the recipe interval.";
    return true;
}

auto expire_timing_recipe_binding(const TimingRecipeDispatchContext& context) -> void {
    auto& edit = context.edit;
    if (edit.recipe_draft) {
        wormedit::cancel_sv_recipe_draft(*edit.recipe_draft);
    }
    edit.recipe_draft.reset();
    edit.recipe_preview.reset();
    edit.recipe_selection_binding.reset();
    context.status = "SV recipe editing expired because its chart or revision changed.";
}

auto begin_timing_recipe_draft(const TimingRecipeDispatchContext& context,
                               const wormedit::chart_document_t& chart) -> void {
    using namespace wormedit;
    auto& selection = context.edit.recipe_selection_binding;
    if (context.timing_locked) {
        context.status = "Unlock timing before adding an SV recipe.";
        return;
    }
    if (context.edit.recipe_draft) {
        context.status = "Apply or cancel the current SV recipe before starting another.";
        return;
    }
    if (!selection || selection->first != chart.id ||
        selection->second != context.action.document_revision) {
        selection.reset();
        context.status = "SV recipe range selection expired before the drag began.";
        return;
    }
    const auto hits = timing_recipe_range_hits(chart, context.action.range_begin_fraction,
                                               context.action.range_end_fraction);
    if (!hits) {
        context.status = "The selected SV recipe interval is outside the chart grid.";
        selection.reset();
        return;
    }
    auto candidate = sv_recipe_t{.name = "SV function",
                                 .range = {hits->first, hits->second},
                                 .output = sv_recipe_output_t::sv,
                                 .curve = sv_recipe_curve_t::linear};
    auto next = begin_sv_recipe_draft(context.history, chart.id, std::move(candidate),
                                      context.timing_locked);
    if (next.status != sv_recipe_draft_status_t::editing ||
        !set_sv_recipe_draft_range(context.history, next, hits->first, hits->second,
                                   context.active_snap_denominator, context.timing_locked)) {
        context.status = next.diagnostics.empty() ? "The selected interval cannot form an SV recipe."
                                                  : next.diagnostics.front().message;
        selection.reset();
        return;
    }
    context.edit.recipe_draft = std::move(next);
    selection.reset();
    (void)refresh_timing_recipe_preview(context.history, *context.edit.recipe_draft,
                                        context.edit.recipe_preview, context.status);
}

auto reject_invalid_timing_recipe_form(const TimingRecipeDispatchContext& context) -> void {
    using namespace wormedit;
    context.edit.recipe_preview = sv_recipe_draft_preview_t{};
    context.edit.recipe_preview->diagnostics.push_back({
        .severity = diagnostic_severity_t::error,
        .code = diagnostic_code_t::invalid_rational,
        .message = "Enter bounded exact recipe parameters and sampling values.",
    });
    context.status = context.edit.recipe_preview->diagnostics.front().message;
}

auto update_timing_recipe_draft(const TimingRecipeDispatchContext& context) -> bool {
    auto& draft = *context.edit.recipe_draft;
    const auto candidate = recipe_candidate_from_ui(context.action.form, draft.candidate);
    if (!candidate) {
        reject_invalid_timing_recipe_form(context);
        return false;
    }
    if (!wormedit::update_sv_recipe_draft(context.history, draft, *candidate,
                                           context.timing_locked)) {
        context.status = draft.diagnostics.empty() ? "The SV recipe draft could not be updated."
                                                   : draft.diagnostics.front().message;
        return false;
    }
    return refresh_timing_recipe_preview(context.history, draft, context.edit.recipe_preview,
                                         context.status);
}

auto update_timing_recipe_range(const TimingRecipeDispatchContext& context,
                                const wormedit::chart_document_t& chart) -> bool {
    using namespace wormedit;
    auto& draft = *context.edit.recipe_draft;
    const auto hits = timing_recipe_range_hits(chart, context.action.range_begin_fraction,
                                               context.action.range_end_fraction);
    if (!hits || !set_sv_recipe_draft_range(context.history, draft,
                                             hits ? hits->first : local_position_t{},
                                             hits ? hits->second : local_position_t{},
                                             context.active_snap_denominator,
                                             context.timing_locked)) {
        context.status = !hits || draft.diagnostics.empty()
                             ? "The selected interval cannot form an SV recipe."
                             : draft.diagnostics.front().message;
        return false;
    }
    return update_timing_recipe_draft(context);
}

auto cancel_timing_recipe_draft(const TimingRecipeDispatchContext& context) -> void {
    auto& edit = context.edit;
    if (edit.recipe_draft) {
        wormedit::cancel_sv_recipe_draft(*edit.recipe_draft);
    }
    edit.recipe_draft.reset();
    edit.recipe_preview.reset();
    edit.recipe_selection_binding.reset();
    context.status = "SV recipe editing cancelled; the chart was not changed.";
}

auto apply_timing_recipe_draft(const TimingRecipeDispatchContext& context) -> void {
    using namespace wormedit;
    auto& draft = *context.edit.recipe_draft;
    const auto result = apply_sv_recipe_draft(context.history, draft, context.timing_locked);
    if (!result.diagnostics.empty()) {
        context.status = result.diagnostics.front().message;
        return;
    }
    if (!result.applied) {
        context.status = "SV recipe did not change the chart.";
        return;
    }
    const auto rebound = rebind_workspace(context.workspace, context.history);
    if (!rebound) {
        context.status = rebound.diagnostics.empty()
                             ? "SV recipe applied, but workspace rebinding failed."
                             : rebound.diagnostics.front().message;
        return;
    }
    const bool edited_existing = draft.existing_recipe.has_value();
    context.status = edited_existing ? "SV recipe updated." : "SV recipe added.";
    context.edit.recipe_draft.reset();
    context.edit.recipe_preview.reset();
    context.edit.recipe_selection_binding.reset();
}

auto dispatch_timing_recipe_action(
    const WormUiTimingRecipeAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, bool timing_locked,
    std::uint64_t active_snap_denominator, timing_edit_t& timing_edit,
    std::string& status) -> bool {
    using namespace wormedit;
    const TimingRecipeDispatchContext context{action, history, workspace, timing_locked,
                                               active_snap_denominator, timing_edit, status};
    auto& selection = timing_edit.recipe_selection_binding;
    auto& draft = timing_edit.recipe_draft;
    auto& preview = timing_edit.recipe_preview;
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
        return false;
    }
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_ARM_SELECTION) {
        return arm_timing_recipe_selection(context);
    }
    const bool binding_matches = workspace.active_tab == workspace_tab_t::timing &&
                                 workspace.selected_chart &&
                                 workspace.selected_chart->value == action.chart_id &&
                                 history.document.revision == action.document_revision;
    if (!binding_matches) {
        expire_timing_recipe_binding(context);
        return true;
    }
    const auto chart = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                         &chart_document_t::id);
    if (chart == history.document.charts.end()) {
        status = "The selected chart is no longer available.";
        return true;
    }
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_BEGIN) {
        begin_timing_recipe_draft(context, *chart);
        return true;
    }
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_CANCEL && !draft) {
        selection.reset();
        preview.reset();
        status = "SV recipe range selection cancelled.";
        return true;
    }
    if (!draft || draft->chart != chart->id ||
        draft->document_revision != action.document_revision) {
        status = "There is no matching SV recipe draft to update.";
        return true;
    }
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_CANCEL) {
        cancel_timing_recipe_draft(context);
        return true;
    }
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_UPDATE_RANGE) {
        (void)update_timing_recipe_range(context, *chart);
        return true;
    }
    const auto preview_valid = update_timing_recipe_draft(context);
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_APPLY && preview_valid) {
        apply_timing_recipe_draft(context);
    }
    return true;
}



auto first_recovery_diagnostic(
    const wormedit::native_project_recovery_controller_dispatch_result_t& result) -> std::string {
    if (!result.diagnostics.empty()) {
        const auto& diagnostic = result.diagnostics.front();
        return diagnostic.path.empty()
                   ? diagnostic.message
                   : diagnostic.message + " (" + diagnostic.path.generic_string() + ")";
    }
    if (result.action_result && !result.action_result->diagnostics.empty()) {
        const auto& diagnostic = result.action_result->diagnostics.front();
        return diagnostic.path.empty()
                   ? diagnostic.message
                   : diagnostic.message + " (" + diagnostic.path.generic_string() + ")";
    }
    return {};
}

auto recovery_action_request(
    const WormUiWorkspaceAction& action, const wormedit::document_history_t& history,
    const std::optional<wormedit::native_project_document_version_t>& scanned_primary_version,
    const wormedit::native_project_recovery_action_target_t& target,
    std::string& error) -> wormedit::native_project_recovery_controller_action_t {
    using namespace wormedit;
    switch (action.kind) {
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT:
        return inspect_native_project_recovery_action_t{.target = target};
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER:
        if (!history.undo.empty() || !history.redo.empty()) {
            error = "The current workspace changed after recovery discovery; save or restart "
                    "before replacing it with a recovery candidate.";
            return inspect_native_project_recovery_action_t{.target = target};
        }
        return recover_native_project_recovery_action_t{
            .target = target,
            .expected_session = {
                .is_set = true,
                .version = scanned_primary_version
                    ? std::optional<native_project_document_version_t>{
                          native_project_document_version_t{.project_id = history.document.id,
                                                            .revision = history.document.revision}}
                    : std::nullopt,
            },
        };
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS: {
        const auto end = std::find(std::begin(action.recovery_save_as_destination),
                                   std::end(action.recovery_save_as_destination), '\0');
        const std::string destination(action.recovery_save_as_destination, end);
        if (destination.empty()) {
            error = "Choose a new project path before using Save as.";
            return inspect_native_project_recovery_action_t{.target = target};
        }
        return save_as_native_project_recovery_action_t{
            .target = target, .destination = std::filesystem::path{destination}};
    }
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD:
        return discard_native_project_recovery_action_t{.target = target};
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE:
        return quarantine_native_project_recovery_action_t{.target = target};
    default:
        error = "Invalid recovery action.";
        return inspect_native_project_recovery_action_t{.target = target};
    }
}

auto load_recovered_document(
    const wormedit::native_project_recovery_action_result_t& result,
    const WormUiWorkspaceAction& action, wormedit::native_project_recovery_controller_t& controller,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    wormedit::native_project_autosave_loop_t& autosave_loop, bool& recovered) -> std::string {
    using namespace wormedit;
    if (!result.document)
        return "Recovery action completed without a replacement document.";
    auto replacement_history = make_document_history(*result.document, 128, false);
    auto replacement_binding = make_workspace_controller(replacement_history);
    if (!replacement_binding) {
        return "Recovered data validated, but the workspace could not be rebound; "
               "the current workspace was kept.";
    }
    const auto& recovery_view = controller.view();
    std::optional<native_project_document_version_t> recovered_save_baseline;
    auto recovered_sequence = std::uint64_t{};
    if (action.recovery_row_index < recovery_view.rows.size()) {
        const auto& row = recovery_view.rows[action.recovery_row_index];
        if (row.last_explicit_save_revision) {
            recovered_save_baseline = native_project_document_version_t{
                .project_id = result.document->id,
                .revision = *row.last_explicit_save_revision};
        }
        if (row.recovery_session_sequence)
            recovered_sequence = *row.recovery_session_sequence;
    }
    if (!autosave_loop.reset_session(native_project_autosave_loop_t::clock_t::now(),
                                     recovered_save_baseline, recovered_sequence)) {
        return "Wait for the current autosave to finish before recovering a session.";
    }
    history = std::move(replacement_history);
    workspace = std::move(*replacement_binding.workspace);
    recovered = true;
    (void)controller.replace_scan({});
    return "Recovery loaded into the current session. Review it and save explicitly.";
}

auto dispatch_recovery_ui_action(
    const WormUiWorkspaceAction& action, wormedit::native_project_recovery_controller_t& controller,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::native_project_document_version_t> scanned_primary_version,
    wormedit::native_project_autosave_loop_t& autosave_loop, bool& recovered) -> std::string {
    using namespace wormedit;
    const native_project_recovery_action_target_t target{
        .scan_generation = action.recovery_scan_generation,
        .row_index = action.recovery_row_index,
        .candidate_index = action.recovery_candidate_index,
    };
    std::string request_error;
    const auto request = recovery_action_request(action, history, scanned_primary_version,
                                                 target, request_error);
    if (!request_error.empty()) return request_error;
    const auto dispatched = controller.dispatch(request);
    const auto diagnostic = first_recovery_diagnostic(dispatched);
    if (!dispatched.accepted) {
        return diagnostic.empty() ? "Recovery action was rejected." : diagnostic;
    }
    if (!dispatched.action_result) {
        return diagnostic.empty() ? "Recovery action did not produce a result." : diagnostic;
    }
    const auto& result = *dispatched.action_result;
    if (result.has_errors() || !result.applied) {
        return diagnostic.empty() ? "Recovery action did not complete." : diagnostic;
    }

    switch (action.kind) {
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT:
        if (result.document) {
            return "Inspected recovery candidate read-only: " + result.document->metadata.title +
                   " (revision " + std::to_string(result.document->revision) + ").";
        }
        return "Recovery candidate inspected read-only.";
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER:
        return load_recovered_document(result, action, controller, history, workspace,
                                       autosave_loop, recovered);
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS:
        return "Recovery candidate saved as a new project; the current workspace was unchanged.";
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD:
        return "Recovery artifact moved to its recoverable discarded location.";
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE:
        return result.quarantined_path
                   ? "Recovery artifact quarantined at " + result.quarantined_path->generic_string()
                   : "Recovery artifact quarantined.";
    default:
        return "Invalid recovery action.";
    }
}

struct AudioOffsetDragContext {
    const WormUiWorkspaceAction& action;
    wormedit::document_history_t& history;
    wormedit::workspace_controller_t& workspace;
    bool timing_locked;
    std::optional<std::uint32_t> selected_sample_rate;
    std::optional<wormedit::audio_offset_drag_t>& drag;
    std::optional<wormedit::workspace_tab_t>& drag_tab;
};

auto clear_audio_offset_drag(AudioOffsetDragContext& context, std::string message) -> std::string {
    context.drag.reset();
    context.drag_tab.reset();
    return message;
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
            return "Finish or cancel the active audio offset drag before nudging.";
    }
    const auto begun = begin_audio_offset_drag(context.history.document,
                                               *context.workspace.selected_chart,
                                               *context.selected_sample_rate,
                                               context.timing_locked);
        if (!begun) {
            return begun.diagnostics.empty() ? "Audio offset nudge could not start."
                                             : begun.diagnostics.front().message;
        }
        const auto updated = update_audio_offset_drag(
            context.history.document, *begun.drag,
            context.action.audio_offset_drag_delta_frames, context.timing_locked);
        if (!updated) {
            return updated.diagnostics.empty() ? "Audio offset nudge was rejected."
                                               : updated.diagnostics.front().message;
        }
        const auto committed = commit_audio_offset_drag(context.history, *updated.drag,
                                                        context.timing_locked);
        if (!committed.applied) {
            return committed.diagnostics.empty() ? "Audio offset was unchanged."
                                                 : committed.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(context.workspace, context.history);
        return rebound ? "Audio offset nudged by an exact frame count."
                       : "Audio offset changed, but workspace rebinding failed.";
}

auto begin_audio_offset_drag_action(AudioOffsetDragContext& context) -> std::string {
    using namespace wormedit;
    const auto begun = begin_audio_offset_drag(context.history.document,
                                               *context.workspace.selected_chart,
                                               *context.selected_sample_rate,
                                               context.timing_locked);
    context.drag = begun.drag;
    context.drag_tab = begun ? std::optional{context.workspace.active_tab} : std::nullopt;
        return begun                       ? "Dragging waveform offset; release to apply."
               : begun.diagnostics.empty() ? "Audio offset drag could not start."
                                           : begun.diagnostics.front().message;
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
        return updated.diagnostics.empty() ? "Audio offset preview was rejected."
                                           : updated.diagnostics.front().message;
    }
    drag = updated.drag;
    if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_UPDATE)
        return "Previewing exact waveform offset.";
    if (action.audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_COMMIT) {
        drag.reset();
        drag_tab.reset();
        return "Invalid audio offset drag action.";
    }
    const auto committed = commit_audio_offset_drag(history, *drag, timing_locked);
    drag.reset();
    drag_tab.reset();
    if (!committed.applied) {
        return committed.diagnostics.empty() ? "Audio offset was unchanged."
                                             : committed.diagnostics.front().message;
    }
    const auto rebound = rebind_workspace(workspace, history);
    return rebound                       ? "Audio offset updated from waveform drag."
           : rebound.diagnostics.empty() ? "Audio offset changed, but workspace rebinding failed."
                                         : rebound.diagnostics.front().message;
}

auto dispatch_audio_offset_drag_action(const WormUiWorkspaceAction& action,
                                       wormedit::document_history_t& history,
                                       wormedit::workspace_controller_t& workspace,
                                       bool timing_locked,
                                       std::optional<std::uint32_t> selected_sample_rate,
                                       std::optional<wormedit::audio_offset_drag_t>& drag,
                                       std::optional<wormedit::workspace_tab_t>& drag_tab)
    -> std::string {
    AudioOffsetDragContext context{action, history, workspace, timing_locked,
                                   selected_sample_rate, drag, drag_tab};
    if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_CANCEL)
        return clear_audio_offset_drag(context, "Audio offset drag cancelled.");
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
void configure_transport_play_pause(auto& state, auto& history, auto& workspace,
                                 auto& compose_interaction_state, auto& inline_preview_cursor,
                                 auto& audition_session, auto& preview_session, auto& preview_state,
                                 auto& audition_sources, auto& preview_metronome_bindings,
                                 auto& audition_output, auto& preview_output) {
    state.transport_play_pause = [&]() -> std::string {
        const auto compose_cursor = workspace.active_tab == wormedit::workspace_tab_t::compose &&
                                            compose_interaction_state && workspace.selected_chart &&
                                            compose_interaction_state->chart == *workspace.selected_chart
                                        ? &*compose_interaction_state
                                        : nullptr;
        if (!preview_session.active() && !preview_state.paused && compose_cursor &&
            compose_cursor->virtual_bms_cursor_row)
            return "Materialize the cursor measure before starting chart preview.";
        const bool starting = !preview_session.active() && !preview_state.paused;
        const auto exact_start = starting ? wormedit::sys::compose_minimap_start_frame(state, history, compose_cursor) : std::nullopt;
        const auto result = preview_state.play_pause(
            selected_chart(history, workspace), history.document, audition_sources,
            preview_metronome_bindings, audition_output,
            preview_session, preview_output,
            compose_cursor ? std::optional{compose_cursor->cursor} : std::nullopt, false, exact_start);
        if (starting && preview_session.active()) {
            state.minimap_start_frame.reset();
            inline_preview_cursor.capture(compose_cursor);
        }
        return result;
    };
}

void configure_transport_commands(auto& state, auto& history, auto& workspace,
                                 auto& compose_interaction_state, auto& inline_preview_cursor,
                                 auto& audition_session, auto& preview_session, auto& preview_state,
                                 auto& audition_sources, auto& preview_metronome_bindings,
                                 auto& audition_output, auto& preview_output) {
    state.transport_set_playback_rate = [&](wormedit::rational_t rate) {
        return preview_output.set_playback_rate(rate);
    };
    state.transport_active = [&] {
        return audition_session.active() || preview_session.active() || preview_state.paused;
    };
    state.transport_preview_playing = [&] { return preview_session.active(); };
    state.transport_preview_paused = [&] { return preview_state.paused; };
    state.transport_preview_end_frame = [&]() -> std::optional<std::uint64_t> {
        return preview_state.matches_selection(history.document, workspace)
            ? std::optional{preview_state.end_frame} : std::nullopt;
    };
    state.transport_inline_active = [&] {
        return preview_session.active() || preview_state.paused;
    };
    configure_transport_play_pause(state, history, workspace, compose_interaction_state,
        inline_preview_cursor, audition_session, preview_session, preview_state,
        audition_sources, preview_metronome_bindings, audition_output, preview_output);
    state.transport_play_from_start = [&]() -> std::string {
        state.minimap_start_frame.reset();
        const bool starting = !preview_session.active() && !preview_state.paused;
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
        if (starting && preview_session.active()) inline_preview_cursor.capture(compose_cursor);
        return result;
    };
    state.transport_seek = [&](std::uint64_t frame) -> std::string {
        return preview_state.seek(frame, history.document, workspace, audition_output,
                                  preview_session, preview_output);
    };
    state.transport_audio_milliseconds = [&]() {
        return preview_state.audio_milliseconds(history.document, workspace, preview_session);
    };
    state.transport_seek_audio_milliseconds = [&](std::int64_t milliseconds) -> std::string {
        return preview_state.seek_audio_milliseconds(
            milliseconds, history.document, workspace, audition_output,
            preview_session, preview_output);
    };
    state.transport_stop = [&] {
        state.minimap_start_frame.reset();
        preview_state.stop(audition_output, preview_output);
        inline_preview_cursor.anchor.reset();
    };
    state.transport_cancel_restore = [&]() -> std::string {
        preview_state.stop(audition_output, preview_output);
        const bool restored = inline_preview_cursor.restore(
            history, compose_interaction_state ? &*compose_interaction_state : nullptr);
        return restored ? "Chart preview stopped; cursor restored."
                        : "Chart preview stopped.";
    };
}
#endif

void refresh_transport_rate_commands(wormedit::command_registry_t& registry,
                                     const application_command_state_t& state) {
    const bool available = static_cast<bool>(state.transport_set_playback_rate);
    (void)wormedit::update_command_state(registry, {"transport.rate_slower"},
                                       available && state.transport_rate_index > 0, std::nullopt);
    (void)wormedit::update_command_state(registry, {"transport.rate_faster"},
                                       available && state.transport_rate_index < 8, std::nullopt);
}

struct external_player_runtime_t {
    wormedit::sys::external_player_t player;
    wormedit::sys::external_player_preferences_t preferences = wormedit::sys::default_external_player_preferences();
    std::filesystem::path preference_path;
    std::filesystem::path source_path;
    bool visible{};
    std::uint64_t revision{};
    std::vector<WormUiExternalPlayerDefinitionView> views;
};

void initialize_external_player(external_player_runtime_t& runtime, std::string& status) {
    const auto path = wormedit::prepare_platform_shortcut_profile_path();
    if (!path) { status += " External-player preferences directory unavailable."; return; }
    runtime.preference_path = path.path.parent_path() / "external-players.conf";
    const auto error = wormedit::sys::load_external_player_preferences(runtime.preference_path, runtime.preferences);
    if (!error.empty()) status += " " + error;
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
    const std::filesystem::path& asset_base) -> std::string {
    using namespace wormedit::sys;
    if (runtime.preferences.selected >= runtime.preferences.players.size()) {
        runtime.visible = true;
        return "Choose an external player in Preferences.";
    }
    const auto& player = runtime.preferences.players[runtime.preferences.selected];
    if (player.path.empty()) { runtime.visible = true; return "Choose an external player in Preferences."; }
    const auto* chart = selected_chart(history, workspace);
    if (action != external_player_action_t::stop && (!chart || chart->source_family != wormedit::format_family_t::bms))
        return "External preview requires a selected BMS chart.";
    std::uint64_t measure{};
    if (action == external_player_action_t::play_here) {
        const auto source_measure = external_source_measure(*chart, interaction);
        if (!source_measure) return "Place the Compose cursor before playing externally from here.";
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
    return result.launched ? "External player command launched." : result.error;
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
    const WormUiExternalPlayerAction& action) -> std::string {
    auto candidate = runtime.preferences;
    switch (action.kind) {
    case WORM_UI_EXTERNAL_PLAYER_SELECT:
        if (action.selected >= candidate.players.size()) return "External player selection is invalid.";
        candidate.selected = action.selected;
        break;
    case WORM_UI_EXTERNAL_PLAYER_SAVE:
        if (action.selected >= candidate.players.size()) return "External player selection is invalid.";
        candidate.players[action.selected] = {action.name, action.path, action.start_arguments,
                                              action.here_arguments, action.stop_arguments};
        break;
    case WORM_UI_EXTERNAL_PLAYER_ADD:
        candidate.players.push_back({"New player", "", "\"<filename>\"", "-s <measure> \"<filename>\"", "-t"});
        candidate.selected = candidate.players.size() - 1;
        break;
    case WORM_UI_EXTERNAL_PLAYER_REMOVE:
        if (candidate.players.size() <= 1) return "Keep at least one external player definition.";
        candidate.players.erase(candidate.players.begin() + static_cast<std::ptrdiff_t>(candidate.selected));
        candidate.selected = 0;
        break;
    default: return {};
    }
    if (runtime.preference_path.empty()) return "External-player preferences directory unavailable.";
    const auto error = wormedit::sys::save_external_player_preferences(runtime.preference_path, candidate);
    if (!error.empty()) return error;
    runtime.preferences = std::move(candidate);
    ++runtime.revision;
    return "External-player preferences saved.";
}

auto dispatch_external_player_action(external_player_runtime_t& runtime,
    const WormUiExternalPlayerAction& action, const application_command_state_t& state,
    std::string& status) -> bool {
    using wormedit::sys::external_player_action_t;
    if (action.kind == WORM_UI_EXTERNAL_PLAYER_NONE) return false;
    if (action.kind == WORM_UI_EXTERNAL_PLAYER_OPEN) runtime.visible = true;
    else if (action.kind == WORM_UI_EXTERNAL_PLAYER_CLOSE) runtime.visible = false;
    else if (action.kind == WORM_UI_EXTERNAL_PLAYER_PLAY_START && state.external_player)
        status = state.external_player(external_player_action_t::play_start);
    else if (action.kind == WORM_UI_EXTERNAL_PLAYER_PLAY_HERE && state.external_player)
        status = state.external_player(external_player_action_t::play_here);
    else if (action.kind == WORM_UI_EXTERNAL_PLAYER_STOP && state.external_player)
        status = state.external_player(external_player_action_t::stop);
    else status = edit_external_player_preferences(runtime, action);
    return true;
}

void refresh_note_kind_commands(wormedit::command_registry_t& registry,
    const wormedit::chart_document_t* chart,
    const std::optional<wormedit::editor_interaction_state_t>& compose_state,
    bool compose_ready, bool pending) {
    using namespace wormedit;
    constexpr std::pair<std::string_view, note_kind_t> note_kinds[]{
        {"tap", note_kind_t::tap}, {"hold", note_kind_t::hold}, {"mine", note_kind_t::mine},
        {"invisible", note_kind_t::invisible}, {"lift", note_kind_t::lift},
        {"fake", note_kind_t::fake}, {"roll", note_kind_t::roll}};
    for (const auto& [name, kind] : note_kinds)
        (void)update_command_state(registry, command_id_t{"edit.note_kind_" + std::string(name)},
            compose_ready && note_kind_allowed(*chart, kind) && !pending,
            compose_ready && compose_state->armed_note_kind == kind);
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
            application_state.semantic_layer_paint_mode_active = false;
            application_state.semantic_layer_solo_active = false;
        } else if (target_layer->locked || !target_layer->visible) {
            application_state.semantic_layer_paint_mode_active = false;
        }
    }
    const auto set = [&](std::string id, bool enabled, std::optional<bool> checked = std::nullopt) {
        (void)update_command_state(registry, command_id_t{std::move(id)}, enabled, checked);
    };
    set("document.new_project", false);
    set("document.new_chart", false);
    set("history.undo", !history.undo.empty());
    set("history.redo", !history.redo.empty());
    set("edit.delete", compose_ready);
    refresh_note_kind_commands(registry, chart, compose_state, compose_ready, pending);

    set("selection.clear", compose_ready && (inline_preview_active(application_state) ||
                                            pending || !compose_state->selected_objects.empty() ||
                                             application_state.note_paint_mode_active ||
                                             application_state.semantic_layer_paint_mode_active));
    set("selection.select_all", compose_ready);
    set("selection.invert", compose_ready);
    const bool beat_marker_navigation_available =
        compose_ready && has_beat_marker_navigation(*chart);
    set("navigation.previous_marker", beat_marker_navigation_available);
    set("navigation.next_marker", beat_marker_navigation_available);
    set("navigation.previous_snap", compose_ready);
    set("navigation.next_snap", compose_ready);
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
        {"workspace.open_timing", workspace_tab_t::timing},
        {"workspace.open_compose", workspace_tab_t::compose},
        {"workspace.open_storyboard", workspace_tab_t::storyboard},
        {"workspace.open_audio_slices", workspace_tab_t::audio_slices},
        {"workspace.open_compare_validate", workspace_tab_t::compare_validate},
    }};
    for (const auto& [id, tab] : tabs) {
        set(std::string{id}, tab != workspace_tab_t::storyboard, workspace.active_tab == tab);
    }
    set("timing.toggle_lock", workspace.active_tab == workspace_tab_t::timing,
        application_state.timing_locked);
    set("timing.insert_bpm", false);
    set("timing.insert_sv", false);
    set("timing.open_settings", false);

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
    set("semantic_layer.clear_target", compose_ready && target_valid, !target_valid);
    const bool can_apply_layer =
        compose_ready && target_valid && !compose_state->selected_notes.empty();
    set("semantic_layer.apply", can_apply_layer);
    set("semantic_layer.remove", can_apply_layer);
    set("semantic_layer.toggle", can_apply_layer);
    set("audio.paint_hitsound", false);
}

auto application_command_focus(const WormUiWorkspaceAction& action,
                               const wormedit::workspace_controller_t& workspace)
    -> wormedit::command_focus_t {
    using namespace wormedit;
    command_focus_t focus{
        .text_entry_active = action.shortcut_text_entry_active,
        .ime_composition_active = action.shortcut_ime_composition_active,
        .workspace_active = action.shortcut_window_focused,
        .active_tab = workspace.active_tab,
        .visible_tabs = {workspace_tab_t::setup, workspace_tab_t::metadata, workspace_tab_t::timing,
                         workspace_tab_t::compose, workspace_tab_t::storyboard,
                         workspace_tab_t::audio_slices, workspace_tab_t::compare_validate},
    };
    if (action.shortcut_compose_lane_focused && action.shortcut_focused_lane_id != 0) {
        focus.focused_lane = lane_id_t{action.shortcut_focused_lane_id};
    }
    return focus;
}

constexpr auto application_primary_modifier() -> wormedit::shortcut_primary_modifier_t {
#if defined(__APPLE__)
    return wormedit::shortcut_primary_modifier_t::meta;
#else
    return wormedit::shortcut_primary_modifier_t::control;
#endif
}

auto dispatch_application_shortcuts(
    const WormUiWorkspaceAction& action, wormedit::command_registry_t& registry,
    const wormedit::document_history_t& history, const wormedit::workspace_controller_t& workspace,
    const std::optional<wormedit::editor_interaction_state_t>& compose_state,
    application_command_state_t& application_state, std::string& status) -> bool {
    using namespace wormedit;
    if (action.shortcut_overflowed ||
        action.shortcut_event_count > WORM_UI_SHORTCUT_EVENT_CAPACITY) {
        status = "Shortcut input exceeded its per-frame capacity and was discarded.";
        return true;
    }
    bool handled{};
    for (std::size_t index = 0; index < action.shortcut_event_count; ++index) {
        refresh_application_command_states(registry, history, workspace, compose_state,
                                           application_state);
        const auto& input = action.shortcut_events[index];
        const auto dispatched = dispatch_shortcut(
            registry,
            {.chord = {.key = static_cast<shortcut_key_t>(input.hid_usage),
                       .modifiers = input.modifiers},
             .auto_repeat = input.repeat},
            application_command_focus(action, workspace), application_primary_modifier());
        if (dispatched.status == command_dispatch_status_t::invoked) {
            handled = true;
        } else if (dispatched.status == command_dispatch_status_t::disabled) {
            handled = true;
            status = dispatched.command
                         ? "Command is currently unavailable: " + dispatched.command->value
                         : "Command is currently unavailable.";
        } else if (dispatched.status == command_dispatch_status_t::conflict ||
                   dispatched.status == command_dispatch_status_t::invalid_event ||
                   dispatched.status == command_dispatch_status_t::rejected) {
            handled = true;
            status = dispatched.diagnostics.empty() ? "Shortcut command was rejected."
                                                    : dispatched.diagnostics.front().message;
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
    std::string& status) {
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
        status = "Timing value is outside its supported bounds.";
        return;
    }

    const bool valid = wormedit::update_timing_lane_pseudonote(
        history, *timing_stage, *value, timing_locked);
    if (action.timing_stage_action == WORM_UI_TIMING_STAGE_CONFIRM && valid) {
        const auto result = wormedit::confirm_timing_lane_pseudonote(
            history, *timing_stage, timing_locked);
        const auto edited_existing = timing_stage->existing_event.has_value();
        status = result ? (edited_existing ? "Timing event updated."
                                           : "Timing event inserted.")
                 : result.diagnostics.empty() && edited_existing
                     ? "Timing event was unchanged."
                 : result.diagnostics.empty()
                     ? "Timing event insertion was rejected."
                     : result.diagnostics.front().message;
        timing_stage.reset();
    } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_CONFIRM) {
        status = timing_stage->diagnostics.empty()
                     ? "Timing event values are invalid."
                     : timing_stage->diagnostics.front().message;
    }
}

auto timing_stage_binding_matches(const WormUiWorkspaceAction& action,
                                 const wormedit::document_history_t& history,
                                 const wormedit::workspace_controller_t& workspace,
                                 const wormedit::timing_lane_pseudonote_preview_t& stage) -> bool {
    return (workspace.active_tab == wormedit::workspace_tab_t::timing ||
            workspace.active_tab == wormedit::workspace_tab_t::compose) &&
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
                                std::string& status) {
    const auto kind = timing_lane_kind_from_ui(action.timing_lane);
    if (!stage.existing_event || !kind || *kind != stage.kind) {
        status = "The selected timing event cannot be deleted.";
    } else {
        const auto result = wormedit::delete_existing_timing_lane_event(
            history, stage.chart, *kind, *stage.existing_event,
            action.timing_document_revision, timing_locked);
        status = result                       ? "Timing event deleted."
                 : result.diagnostics.empty() ? "Timing event deletion was rejected."
                                              : result.diagnostics.front().message;
    }
    wormedit::cancel_timing_lane_pseudonote(stage);
}

void move_staged_timing_event(const WormUiWorkspaceAction& action,
                              wormedit::document_history_t& history, bool timing_locked,
                              std::uint64_t active_snap_denominator,
                              wormedit::chart_document_t* chart,
                              wormedit::timing_lane_pseudonote_preview_t& stage,
                              std::string& status) {
    const auto target = chart == nullptr || !stage.existing_event || !action.timing_has_target_y
                            ? std::optional<wormedit::local_position_t>{}
                            : timing_position_from_fraction(*chart, action.timing_target_y_fraction,
                                                            active_snap_denominator);
    if (!target || !wormedit::move_existing_timing_lane_pseudonote(
                       history, stage, *target, timing_locked)) {
        status = stage.diagnostics.empty() ? "The timing marker cannot move to that row."
                                           : stage.diagnostics.front().message;
    } else {
        status = "Timing marker moved in the staged preview; apply to commit.";
    }
}

void dispatch_active_timing_stage_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, bool timing_locked,
    std::uint64_t active_snap_denominator, timing_edit_t& timing_edit, std::string& status) {
    auto& stage = timing_edit.stage;
    if (!stage) return;
    if (!timing_stage_binding_matches(action, history, workspace, *stage)) {
        wormedit::cancel_timing_lane_pseudonote(*stage);
        stage.reset();
        status = "Timing event staging expired because its chart or revision changed.";
        return;
    }
    const auto chart = std::ranges::find(history.document.charts, stage->chart,
                                         &wormedit::chart_document_t::id);
    auto* chart_ptr = chart == history.document.charts.end() ? nullptr : &*chart;
    if (action.timing_stage_action == WORM_UI_TIMING_STAGE_DELETE) {
        delete_staged_timing_event(action, history, timing_locked, *stage, status);
        stage.reset();
    } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_MOVE) {
        move_staged_timing_event(action, history, timing_locked, active_snap_denominator,
                                 chart_ptr, *stage, status);
    } else {
        dispatch_timing_stage_value_action(action, history, timing_locked,
                                           active_snap_denominator, chart_ptr, stage, status);
    }
}

void dispatch_timing_stage_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, bool timing_locked,
    std::uint64_t active_snap_denominator, timing_edit_t& timing_edit,
    std::string& status) {
    using status_t = wormedit::timing_lane_pseudonote_status_t;
    const auto recipe_edit_pending = timing_edit.has_pending_recipe_edit();
    auto& timing_stage = timing_edit.stage;
    if (action.timing_stage_action == WORM_UI_TIMING_STAGE_BEGIN) {
        if (recipe_edit_pending) {
            status = "Apply or cancel the SV recipe before editing a timing event.";
        } else if (!timing_stage) {
            auto candidate = begin_timing_stage(action, history, workspace, timing_locked,
                                                active_snap_denominator);
            if (candidate.status == status_t::editing) {
                timing_stage = std::move(candidate);
                status = timing_stage->existing_event.has_value()
                             ? "Timing event loaded; apply to update it."
                             : "Timing event staged; confirm to insert it.";
            } else if (!candidate.diagnostics.empty()) {
                status = candidate.diagnostics.front().message;
            } else {
                status = "Timing event could not be staged at that row.";
            }
        }
    } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_CANCEL) {
        if (timing_stage) {
            wormedit::cancel_timing_lane_pseudonote(*timing_stage);
            timing_stage.reset();
            status = "Timing event staging cancelled.";
        }
    } else if (timing_stage) {
        dispatch_active_timing_stage_action(action, history, workspace, timing_locked,
                                            active_snap_denominator, timing_edit, status);
    }
}

void dispatch_timing_meter_action(const WormUiWorkspaceAction& action,
                                  wormedit::document_history_t& history,
                                  wormedit::workspace_controller_t& workspace, bool timing_locked,
                                  bool timing_edit_pending, std::string& status) {
    const bool binding_matches = action.timing_meter_action == WORM_UI_TIMING_METER_ACTION_APPLY &&
                                 workspace.active_tab == wormedit::workspace_tab_t::timing &&
                                 workspace.selected_chart &&
                                 workspace.selected_chart->value == action.timing_meter_chart_id &&
                                 history.document.revision == action.timing_meter_document_revision;
    const auto measure_number = parse_timing_unsigned(action.timing_meter_measure_text);
    const auto numerator = parse_timing_unsigned(action.timing_meter_numerator_text);
    const auto denominator = parse_timing_unsigned(action.timing_meter_denominator_text);
    const auto chart = workspace.selected_chart
                           ? std::ranges::find(history.document.charts, *workspace.selected_chart,
                                               &wormedit::chart_document_t::id)
                           : history.document.charts.end();
    if (!binding_matches || timing_locked || timing_edit_pending || !measure_number ||
        *measure_number == 0 || !numerator || !denominator || *numerator == 0 ||
        *denominator == 0 || *numerator > std::numeric_limits<std::uint32_t>::max() ||
        *denominator > std::numeric_limits<std::uint32_t>::max() ||
        chart == history.document.charts.end() || *measure_number > chart->measures.size()) {
        status = "Meter change needs a current Timing chart, measure number, and "
                 "bounded signature.";
    } else {
        const auto& target = chart->measures[static_cast<std::size_t>(*measure_number - 1)];
        const auto result = wormedit::apply_measure_edit(
            history, action.timing_meter_document_revision,
            wormedit::update_measure_definition_action_t{
                .chart = chart->id,
                .measure = target.id,
                .definition = {.length = {*numerator, *denominator},
                               .meter =
                                   wormedit::meter_t{static_cast<std::uint32_t>(*numerator),
                                                     static_cast<std::uint32_t>(*denominator)}}});
        status = result                       ? "Meter updated."
                 : result.diagnostics.empty() ? "Meter was unchanged."
                                              : result.diagnostics.front().message;
        if (result) {
            const auto rebound = wormedit::rebind_workspace(workspace, history);
            if (!rebound) {
                status = rebound.diagnostics.empty()
                             ? "Meter updated, but workspace rebinding failed."
                             : rebound.diagnostics.front().message;
            }
        }
    }
}

auto dispatch_workspace_selection_action(const WormUiWorkspaceAction& action,
                                         const wormedit::document_history_t& history,
                                         wormedit::workspace_controller_t& workspace)
    -> std::optional<std::string> {
    switch (action.kind) {
    case WORM_UI_WORKSPACE_ACTION_SELECT_AUDIO_ASSET: {
        const auto applied = wormedit::apply_workspace_action(
            workspace, history.document, history.document.revision,
            wormedit::workspace_select_audio_asset_action_t{
                wormedit::asset_id_t{action.audio_asset_id}});
        return applied ? "Audio asset selected; waveform analysis scheduled."
                       : (applied.diagnostics.empty() ? "Audio asset selection rejected."
                                                      : applied.diagnostics.front().message);
    }
    case WORM_UI_WORKSPACE_ACTION_SELECT_VISUAL_ASSET: {
        const auto applied = wormedit::apply_workspace_action(
            workspace, history.document, history.document.revision,
            wormedit::workspace_select_visual_asset_action_t{
                wormedit::asset_id_t{action.visual_asset_id}});
        return applied ? "Image asset selected."
                       : (applied.diagnostics.empty() ? "Image asset selection rejected."
                                                      : applied.diagnostics.front().message);
    }
    case WORM_UI_WORKSPACE_ACTION_SWITCH_TAB: {
        const auto applied = wormedit::apply_workspace_action(
            workspace, history.document, history.document.revision,
            wormedit::workspace_switch_tab_action_t{
                static_cast<wormedit::workspace_tab_t>(action.tab)});
        return applied ? "Workspace tab changed"
                       : (applied.diagnostics.empty() ? "Workspace action rejected"
                                                      : applied.diagnostics.front().message);
    }
    default:
        return std::nullopt;
    }
}

void dispatch_timing_grid_action(const WormUiWorkspaceAction& action,
                                 const wormedit::document_history_t& history,
                                 const wormedit::workspace_controller_t& workspace,
                                 bool timing_edit_pending, timing_grid_state_t& timing_grid,
                                 application_command_state_t& application_state,
                                 std::string& status) {
    const bool binding_matches =
        action.timing_grid_action == WORM_UI_TIMING_GRID_ACTION_SELECT &&
        workspace.active_tab == wormedit::workspace_tab_t::timing && workspace.selected_chart &&
        workspace.selected_chart->value == action.timing_grid_chart_id &&
        history.document.revision == action.timing_grid_document_revision &&
        timing_grid.chart.value == action.timing_grid_chart_id;
    const auto supported = std::ranges::find(timing_grid.supported_denominators,
                                             action.timing_grid_denominator);
    if (!binding_matches || timing_edit_pending ||
        supported == timing_grid.supported_denominators.end()) {
        status = "Timing grid selection expired or cannot change during a staged edit.";
        return;
    }

    remember_snap_denominator(application_state,
                              wormedit::chart_id_t{action.timing_grid_chart_id},
                              action.timing_grid_denominator);
    const auto chart = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                         &wormedit::chart_document_t::id);
    if (chart == history.document.charts.end()) {
        status = "The selected Timing chart is no longer available.";
        return;
    }
    timing_grid = refresh_timing_grid(*chart, application_state);
    status = "Timing grid set to 1/" + std::to_string(timing_grid.active_denominator) + ".";
}

bool dispatch_timing_workspace_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    application_command_state_t& application_state, timing_grid_state_t& timing_grid,
    timing_edit_t& timing_edit, std::string& status) {
    if (action.timing_meter_action != WORM_UI_TIMING_METER_ACTION_NONE) {
        dispatch_timing_meter_action(action, history, workspace,
                                     application_state.timing_locked,
                                     timing_edit.has_pending_edit(), status);
        return true;
    }
    if (action.timing_grid_action != WORM_UI_TIMING_GRID_ACTION_NONE) {
        dispatch_timing_grid_action(action, history, workspace,
                                    timing_edit.has_pending_edit(), timing_grid,
                                    application_state, status);
        return true;
    }
    if (action.timing_recipe_action.kind != WORM_UI_TIMING_RECIPE_ACTION_NONE) {
        const bool starting_recipe =
            action.timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_ARM_SELECTION ||
            action.timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_BEGIN;
        if (starting_recipe && timing_edit.stage) {
            status = "Apply or cancel the timing event before adding an SV recipe.";
        } else {
            (void)dispatch_timing_recipe_action(
                action.timing_recipe_action, history, workspace,
                application_state.timing_locked, timing_grid.active_denominator,
                timing_edit, status);
        }
        return true;
    }
    if (action.timing_stage_action != WORM_UI_TIMING_STAGE_NONE) {
        dispatch_timing_stage_action(action, history, workspace,
                                     application_state.timing_locked,
                                     timing_grid.active_denominator, timing_edit, status);
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

template <typename Context>
auto process_compose_frame_action(Context& context, WormUiWorkspaceAction& action,
                                  double viewport_delta_seconds) -> bool {
    if (!context.session_loaded_ref) return false;
    const bool shortcut_handled =
        action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
        dispatch_application_shortcuts(action, context.command_registry_ref, context.history_ref,
                                       context.workspace_ref, context.compose_interaction_state_ref,
                                       context.application_command_state_ref, context.status_ref);
    if (shortcut_handled && context.compose_interaction_state_ref &&
        editor_interaction_has_pending_gesture(*context.compose_interaction_state_ref)) {
        (void)wormedit::dispatch_editor_action(
            context.history_ref, *context.compose_interaction_state_ref,
            wormedit::cancel_editor_gesture_action_t{});
    }
    if (shortcut_handled) {
        context.audio_offset_drag_ref.reset();
        context.audio_offset_drag_tab_ref.reset();
        context.compose_scene_cache_ref.beat_fit_drag.reset();
        context.compose_scene_cache_ref.beat_fit_asset.reset();
        context.compose_scene_cache_ref.offset_fit_drag.reset();
    }
    if (!shortcut_handled && action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
        (action.compose_input_count != 0 || action.compose_viewport_input_count != 0 ||
         action.compose_has_pointer_hover ||
         (context.workspace_ref.active_tab == wormedit::workspace_tab_t::compose &&
          action.compose_input_chart_id != 0)) &&
        context.compose_interaction_state_ref) {
        const auto compose_status = dispatch_compose_input_action(
            action, context.history_ref, context.workspace_ref, *context.compose_interaction_state_ref,
            context.compose_viewport_input_ref, context.compose_navigation_input_ref,
            context.application_command_state_ref, context.compose_scene_cache_ref,
            viewport_delta_seconds);
        if (!compose_status.empty()) {
            context.status_ref = compose_status;
        }
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        wormedit::sys::process_compose_audition_requests(
            context.compose_scene_cache_ref, context.history_ref, context.audition_sources_ref,
            context.object_audition_ref, context.preview_session_ref, context.preview_output_ref,
            context.preview_state_ref, context.audition_output_ref, context.status_ref);
#else
        wormedit::sys::process_compose_audition_requests(
            context.compose_scene_cache_ref, context.history_ref, context.status_ref);
#endif
    }
    if (context.workspace_ref.active_tab != wormedit::workspace_tab_t::compose) {
        context.compose_viewport_input_ref.escape();
        context.compose_scene_cache_ref.viewport_autoscroll_active = false;
    }
    if (!shortcut_handled && action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
        context.compose_interaction_state_ref &&
        context.workspace_ref.active_tab == wormedit::workspace_tab_t::compose) {
        const auto repeated = dispatch_compose_navigation_actions(
            context.compose_navigation_input_ref.advance(
                static_cast<std::uint64_t>(SDL_GetTicks())),
            context.history_ref, *context.compose_interaction_state_ref);
        if (!repeated.empty()) {
            context.status_ref = repeated;
        }
    } else if (context.workspace_ref.active_tab != wormedit::workspace_tab_t::compose) {
        context.compose_navigation_input_ref.cancel();
    }
    return shortcut_handled;
}

void tick_loaded_project_autosave(
    bool session_loaded, wormedit::native_project_autosave_loop_t& autosave_loop,
    const wormedit::document_history_t& history, bool native_project_loaded,
    const std::filesystem::path& active_project_path,
    const std::filesystem::path& recovery_path, std::string& status) {
    if (!session_loaded) return;
    wormedit::sys::tick_application_autosave(
        autosave_loop, history, native_project_loaded, active_project_path,
        recovery_path, status);
}

auto can_offer_audio_project_drop(bool session_loaded, bool fresh_blank_project,
                                  const wormedit::document_history_t& history) -> bool {
    if (!session_loaded) return true;
    return fresh_blank_project && history.undo.empty() && history.redo.empty() &&
           !wormedit::is_document_dirty(history);
}

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

void append_shortcut_startup_diagnostic(
    std::string& status, const wormedit::shortcut_profile_startup_result_t& result) {
    if (!result.diagnostics.empty())
        status += " " + result.diagnostics.front().message;
}

void validate_minimap_start_frame(application_command_state_t& application,
    const wormedit::document_history_t& history,
    const std::optional<wormedit::editor_interaction_state_t>& compose_state) {
    (void)wormedit::sys::compose_minimap_start_frame(application, history,
        compose_state ? &*compose_state : nullptr);
}

void reset_project_edit_state(application_command_state_t& current) {
    application_command_state_t next;
    next.transport_rate_index = current.transport_rate_index;
    next.transport_keep_pitch = current.transport_keep_pitch;
    next.transport_keep_pitch_available = current.transport_keep_pitch_available;
    next.transport_set_playback_rate = std::move(current.transport_set_playback_rate);
    next.transport_active = std::move(current.transport_active);
    next.transport_inline_active = std::move(current.transport_inline_active);
    next.transport_preview_playing = std::move(current.transport_preview_playing);
    next.transport_preview_paused = std::move(current.transport_preview_paused);
    next.transport_preview_end_frame = std::move(current.transport_preview_end_frame);
    next.minimap_click_moves_paused_playhead = current.minimap_click_moves_paused_playhead;
    next.transport_play_pause = std::move(current.transport_play_pause);
    next.transport_play_from_start = std::move(current.transport_play_from_start);
    next.transport_seek = std::move(current.transport_seek);
    next.transport_audio_milliseconds = std::move(current.transport_audio_milliseconds);
    next.transport_seek_audio_milliseconds = std::move(current.transport_seek_audio_milliseconds);
    next.transport_stop = std::move(current.transport_stop);
    next.external_player = std::move(current.external_player);
    next.transport_cancel_restore = std::move(current.transport_cancel_restore);
    current = std::move(next);
}

template <typename Context>
void reset_project_runtime_state(Context& context) {
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
    reset_project_edit_state(context.application_command_state_ref);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    (void)wormedit::sys::reconcile_audition_sources(context.audition_reconciliation_ref, true);
#endif
}

template <typename Context>
auto replace_project_session(Context& context, wormedit::song_document_t document,
                             bool loaded, bool saved, bool native_project,
                             std::filesystem::path native_path,
                             std::filesystem::path asset_base) -> bool {
    using namespace wormedit;
    auto replacement_history = make_document_history(std::move(document), 128, saved);
    auto replacement_binding = make_workspace_controller(replacement_history);
    if (!replacement_binding) {
        context.status_ref = "The selected document could not be bound to a workspace.";
        return false;
    }
    const auto saved_version = saved && native_project
        ? std::optional<native_project_document_version_t>{
              {.project_id = replacement_history.document.id,
               .revision = replacement_history.document.revision}}
        : std::nullopt;
    if (!context.autosave_loop_ref.reset_session(
            native_project_autosave_loop_t::clock_t::now(), saved_version, 0)) {
        context.status_ref = "Wait for the current autosave to finish before changing projects.";
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
        context.status_ref = "Wait for the current file operation before changing projects.";
        return false;
    }
    if (context.session_loaded_ref && wormedit::is_document_dirty(context.history_ref)) {
        context.status_ref = "Save the current project before replacing or closing it.";
        return false;
    }
    return true;
}

template <typename Context>
void save_project_session(Context& context, const WormUiWorkspaceAction& action) {
    using namespace wormedit;
    if (!context.session_loaded_ref) {
        context.status_ref = "Create or open a project before saving.";
        return;
    }
    if (context.resource_copy_runtime_ref.busy() || context.export_runtime_ref.busy() ||
        context.metadata_asset_runtime_ref.import_pending()) {
        context.status_ref = "Wait for the current file operation before saving.";
        return;
    }
    auto destination = action.project_path[0] != '\0'
        ? std::filesystem::path{action.project_path}
        : context.active_project_path_ref;
    if (destination.empty()) {
        context.status_ref = "Choose a native project path to save.";
        return;
    }
    auto extension = destination.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (extension.empty()) destination += ".wormedit";
    else if (extension != ".wormedit") {
        context.status_ref = "Choose a .wormedit project filename.";
        return;
    }
    std::error_code path_error;
    const auto destination_base = std::filesystem::absolute(destination, path_error).parent_path();
    if (path_error) {
        context.status_ref = "Could not resolve the project destination: " + path_error.message();
        return;
    }
    auto saved_document = context.history_ref.document;
    wormedit::sys::rebase_project_paths(saved_document, context.asset_base_ref, destination_base);
    const auto result = publish_native_project_atomic(destination, saved_document);
    if (!result.published) {
        context.status_ref = result.diagnostics.empty()
            ? "Could not save the project."
            : result.diagnostics.front().message;
        return;
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
    context.native_project_loaded_ref = true;
    context.asset_base_ref = destination_base;
    context.external_player_runtime_ref.source_path.clear();
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    (void)wormedit::sys::reconcile_audition_sources(context.audition_reconciliation_ref, true);
#endif
    context.status_ref = "Project saved to " + destination.string();
    if (!result.diagnostics.empty())
        context.status_ref += " " + result.diagnostics.front().message;
}

template <typename Context>
void open_project_session(Context& context, const WormUiWorkspaceAction& action) {
    constexpr std::array<wormedit::text_encoding_t, 5> encodings{
        wormedit::text_encoding_t::utf8, wormedit::text_encoding_t::cp932,
        wormedit::text_encoding_t::euc_kr, wormedit::text_encoding_t::cp949,
        wormedit::text_encoding_t::windows_1252};
    if (action.bms_encoding_index > encodings.size()) {
        context.status_ref = "Choose a supported BMS text encoding.";
        return;
    }
    const auto encoding = action.bms_encoding_index == 0
        ? std::optional<wormedit::text_encoding_t>{}
        : std::optional{encodings[action.bms_encoding_index - 1]};
    auto opened = wormedit::sys::open_project_file(action.project_path, encoding);
    if (!opened) {
        context.status_ref = opened.error;
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
        context.external_player_runtime_ref.source_path = opened.native_project ? std::filesystem::path{} : opened.source_path;
        context.status_ref = opened.native_project
            ? "Project opened: " + opened.source_path.string()
            : "Chart imported from " + opened.source_path.string() + ". Save it as a project.";
        if (!opened.warnings.empty()) {
            context.status_ref = "Chart imported with audio warning: " + opened.warnings.front();
            if (opened.warnings.size() > 1)
                context.status_ref += " (" + std::to_string(opened.warnings.size()) +
                                      " resolution warnings total)";
        }
    }
}

template <typename Context>
void create_audio_project_session(Context& context, const char* path,
                                  std::uint32_t lane_count) {
    const auto audio = wormedit::sys::inspect_audio_for_project(path);
    if (!audio) {
        context.status_ref = audio.error;
        context.pending_audio_project_path_ref.clear();
        return;
    }
    auto document = wormedit::sys::audio_project_document(
        wormedit::sys::new_session_project_id(), audio.source_path,
        audio.title, audio.artist, lane_count);
    if (!document) {
        context.status_ref = "Could not create the selected audio chart layout.";
        return;
    }
    document->audio_assets.front().stream = audio.stream;
    if (!replace_project_session(context, std::move(*document), true, true, false,
                                 {}, audio.source_path.parent_path())) return;
    const auto selected = wormedit::apply_workspace_action(
        context.workspace_ref, context.history_ref.document,
        context.history_ref.document.revision,
        wormedit::workspace_select_audio_asset_action_t{
            context.history_ref.document.audio_assets.front().id});
    context.status_ref = selected
        ? "Audio imported as a new " + std::to_string(lane_count) + "K project."
        : "Audio project created; select the music asset to preview it.";
}

template <typename Context>
void create_new_project_session(Context& context, const WormUiWorkspaceAction& action) {
    const auto lane_count = requested_project_lane_count(action.new_project_layout_index);
    if (!lane_count) {
        context.status_ref = "Choose a supported chart layout.";
        return;
    }
    if (action.project_path[0] != '\0') {
        create_audio_project_session(context, action.project_path, *lane_count);
        return;
    }
    auto document = wormedit::sys::initial_document(
        wormedit::sys::new_session_project_id(), *lane_count);
    if (!document) {
        context.status_ref = "Could not create the selected chart layout.";
        return;
    }
    if (replace_project_session(context, std::move(*document), true, true, false, {}, {})) {
        context.fresh_blank_project_ref = true;
        context.status_ref = "New " + std::to_string(*lane_count) +
                             "K project created. Save it to keep your work.";
    }
}

template <typename Context>
auto dispatch_project_session_action(Context& context,
                                     const WormUiWorkspaceAction& action) -> bool {
    if (action.kind == WORM_UI_WORKSPACE_ACTION_CANCEL_BMS_ENCODING) {
        context.pending_bms_encoding_path_ref.clear();
        context.status_ref = "BMS import canceled.";
        return true;
    }
    if (action.kind == WORM_UI_WORKSPACE_ACTION_CANCEL_AUDIO_PROJECT) {
        context.pending_audio_project_path_ref.clear();
        context.status_ref = "Audio import canceled.";
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
            context.status_ref = "No project loaded.";
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
void dispatch_dropped_chart_project(Context& context, std::string& pending_path) {
    if (pending_path.empty()) return;
    const auto path = std::exchange(pending_path, {});
    if (path.size() >= WORM_UI_PROJECT_PATH_CAPACITY) {
        context.status_ref = "The dropped chart or project path is too long.";
        return;
    }
    WormUiWorkspaceAction action{};
    action.kind = WORM_UI_WORKSPACE_ACTION_OPEN_FILE;
    std::memcpy(action.project_path, path.c_str(), path.size() + 1);
    (void)dispatch_project_session_action(context, action);
}

template <typename Context>
auto dispatch_recovery_workspace_action(Context& context,
                                        const WormUiWorkspaceAction& action) -> bool {
    if (action.kind < WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT ||
        action.kind > WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE) return false;
    if (context.autosave_loop_ref.busy()) {
        context.status_ref = "Wait for the current autosave to finish before recovering a session.";
        return true;
    }
    bool recovered = false;
    context.status_ref = dispatch_recovery_ui_action(
        action, context.recovery_controller_ref, context.history_ref, context.workspace_ref,
        context.scanned_primary_version_ref, context.autosave_loop_ref, recovered);
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
void dispatch_workspace_frame_action(Context& context, const WormUiWorkspaceAction& action,
                                     bool shortcut_handled,
                                     std::optional<std::uint32_t> sample_rate,
                                     int compose_viewport_height_pixels) {
    auto& running = context.running_ref;
    auto& status = context.status_ref;
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
        } else if (action.audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE) {
            status = dispatch_audio_offset_drag_action(
                action, history, workspace, application_command_state.timing_locked,
                sample_rate, audio_offset_drag, audio_offset_drag_tab);
        } else if (const auto common_status = wormedit::sys::dispatch_transport_player_action(
                       action, application_command_state, stepmania_export_policy)) {
            status = *common_status;
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
                status = "No beat-anchored bookmarks or section boundaries are available.";
            } else if (dispatched.status != wormedit::command_dispatch_status_t::invoked &&
                       !dispatched.diagnostics.empty()) {
                status = dispatched.diagnostics.front().message;
            }
        } else if (const auto selection_status =
                       dispatch_workspace_selection_action(action, history, workspace)) {
            status = *selection_status;
        } else if (const auto metadata_status = wormedit::sys::dispatch_metadata_action(
                       action, history, workspace, application_command_state.timing_locked,
                       metadata_drop, metadata_asset_runtime, asset_base,
                       resource_copy_scheduler, resource_copy_runtime.busy(),
                       export_runtime.busy())) {
            status = *metadata_status;
        } else if (dispatch_timing_workspace_action(action, history, workspace,
                                                   application_command_state, timing_grid,
                                                   timing_edit, status)) {
            // The timing dispatcher updates status and consumes this action.
        } else if (wormedit::sys::dispatch_compose_workspace_action(
                       action, history, workspace, compose_interaction_state,
                       compose_viewport_height_pixels, application_command_state, asset_base,
                       resource_copy_scheduler, resource_copy_runtime, export_runtime,
                       metadata_asset_runtime.import_pending(), metadata_drop,
                       metadata_asset_runtime, status)) {
            // The compose dispatcher updates status and consumes this action.
        }
}

template <typename Model>
auto handle_workspace_view_error(const Model& model, std::string& status, bool& running,
                                 wormedit::document_history_t& history,
                                 wormedit::workspace_controller_t& workspace) -> bool {
    if (model) {
        return false;
    }
    status = model.diagnostics.empty() ? "Workspace view unavailable"
                                       : model.diagnostics.front().message;
    const auto recovery_action = worm_ui_frontend_workspace_error(status.c_str());
    if (recovery_action.kind == WORM_UI_WORKSPACE_ACTION_QUIT) {
        running = false;
    } else if (recovery_action.kind == WORM_UI_WORKSPACE_ACTION_SWITCH_TAB) {
        const auto changed = wormedit::apply_workspace_action(
            workspace, history.document, history.document.revision,
            wormedit::workspace_switch_tab_action_t{wormedit::workspace_tab_t::setup});
        if (!changed && !changed.diagnostics.empty()) {
            status = changed.diagnostics.front().message;
        }
    }
    return true;
}

void prepare_bound_workspace_frame(
    const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    application_command_state_t& application_command_state, timing_grid_state_t& timing_grid,
    std::optional<wormedit::audio_offset_drag_t>& audio_offset_drag,
    std::optional<wormedit::workspace_tab_t>& audio_offset_drag_tab,
    compose_scene_cache_t& compose_scene_cache) {
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
         application_command_state.timing_locked)) {
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
        compose_scene_cache.beat_fit_drag.reset();
        compose_scene_cache.beat_fit_asset.reset();
    }
    if (compose_scene_cache.offset_fit_drag &&
        (workspace.active_tab != wormedit::workspace_tab_t::compose ||
         !workspace.selected_chart ||
         *workspace.selected_chart != compose_scene_cache.offset_fit_drag->beat.chart ||
         history.document.revision !=
             compose_scene_cache.offset_fit_drag->beat.document_revision ||
         workspace.transport.audio_asset != compose_scene_cache.offset_fit_drag->asset ||
         application_command_state.timing_locked)) {
        compose_scene_cache.offset_fit_drag.reset();
    }
}

auto dispatch_registered_application_command(
    const wormedit::command_invocation_context_t& invocation,
    wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::editor_interaction_state_t>& compose_interaction_state,
    std::optional<wormedit::timing_lane_pseudonote_preview_t>& timing_stage,
    application_command_state_t& application_command_state, std::string& status) {
    const auto command = parse_application_command(invocation.command.value);
    if (!command) {
        return wormedit::sys::reject_application_command(
            status, "Command is not implemented by the application.");
    }
    return wormedit::sys::invoke_application_command(
        *command, history, workspace, compose_interaction_state, timing_stage,
        application_command_state, status);
}

} // namespace

int main(int argc, char** argv) {
    wormedit::sys::install_crash_backtrace_handlers();
    wormedit::sys::application_startup_arguments_t arguments;
    wormedit::sys::application_runtime_t runtime;
    if (const int startup_error = wormedit::sys::initialize_application_startup(
            argc, argv, arguments, runtime); startup_error != 0)
        return startup_error;
    auto startup_request = std::move(arguments.request);
    const auto requested_primary_path = arguments.requested_primary_path;

    const auto platform_recovery = wormedit::prepare_platform_recovery_directory();
    if (platform_recovery) {
        startup_request.recovery_directories.push_back(platform_recovery.path);
    }
    auto startup = wormedit::prepare_native_project_startup(
        wormedit::sys::empty_session_document(new_session_project_id()),
        std::move(startup_request));

    if (!wormedit::sys::initialize_application_runtime(runtime))
        return 1;
    auto& window = runtime.window;

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
    wormedit::task_scheduler_t autosave_scheduler(1, 1);
    wormedit::native_project_autosave_loop_t autosave_loop(
        wormedit::native_project_autosave_loop_t::clock_t::now(), autosave_scheduler,
        startup.primary_version, initial_autosave_sequence);
    std::string status = wormedit::sys::select_initial_application_status(
        startup.loaded_primary, requested_primary_path, platform_recovery);
    bool session_loaded = startup.loaded_primary;
    bool native_project_loaded = startup.loaded_primary;
    bool fresh_blank_project = false;
    std::string pending_audio_project_path;
    std::string pending_chart_project_path;
    std::string pending_bms_encoding_path;
    std::filesystem::path active_project_path =
        startup.loaded_primary ? requested_primary_path : std::filesystem::path{};
    std::optional<wormedit::audio_offset_drag_t> audio_offset_drag;
    std::optional<wormedit::workspace_tab_t> audio_offset_drag_tab;
    auto asset_base =
        startup.loaded_primary ? std::filesystem::absolute(requested_primary_path).parent_path()
                               : std::filesystem::path{};
    wormedit::task_scheduler_t resource_copy_scheduler(1, 1);
    resource_copy_t resource_copy_runtime;
    wormedit::sys::runtime::metadata_asset_t metadata_asset_runtime;
    output_group_export_t export_runtime;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    wormedit::visual::image_cache_t player_image_cache;
    wormedit::task_scheduler_t player_image_scheduler(1, 4);
    wormedit::visual::image_job_t player_image_job{player_image_cache, player_image_scheduler,
                                                   WORM_UI_PLAYER_PREVIEW_VISUAL_LAYER_CAPACITY +
                                                       WORM_UI_PLAYER_PREVIEW_STORYBOARD_CAPACITY};
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    wormedit::visual::video_frame_cache_t player_video_cache;
    wormedit::task_scheduler_t player_video_scheduler(1, 2);
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
    wormedit::sys::runtime::waveform_analysis_t waveform_runtime{audition_sources, &preview_cache};
    wormedit::audio::transport_t preview_transport{preview_cache};
    wormedit::audio::preview_transport_bridge_t preview_bridge{preview_transport};
    wormedit::audio::preview_session_t preview_session{audition_sources, preview_transport,
                                                       preview_bridge};
    wormedit::sys::sdl_preview_output_t preview_output{preview_session};
    wormedit::sys::runtime::chart_preview_t preview_state;
    std::vector<wormedit::audio::preview_metronome_source_binding_t> preview_metronome_bindings;
    std::optional<wormedit::song_id_t> audition_source_project;
    std::vector<wormedit::sys::audition_asset_entry_t> audition_asset_inventory;
    std::uint64_t audition_source_generation{};
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
    };
    const auto initial_unavailable_audio_assets = wormedit::sys::reconcile_audition_sources(audition_reconciliation, false).value_or(0);
    if (initial_unavailable_audio_assets != 0) {
        status += " " + std::to_string(initial_unavailable_audio_assets) +
                  " audio asset(s) are unavailable for preview.";
    }
    std::string audition_output_error;
    if (!audition_output.open(&audition_output_error)) {
        status += " Audio output unavailable: " + audition_output_error;
    }
    std::string preview_output_error;
    if (!preview_output.open(&preview_output_error)) {
        status += " Chart preview output unavailable: " + preview_output_error;
    }
#endif
    wormedit::native_project_recovery_controller_t recovery_controller;
    install_startup_recovery_scan(recovery_controller, startup);
    const auto recovery_scan_configured = startup.recovery_scan_configured;
    auto scanned_primary_version = startup.primary_version;

    timing_scene_cache_t timing_scene_cache;
    timing_edit_t timing_edit;
    compose_scene_cache_t compose_scene_cache;
    wormedit::sys::player_preview_presenter_t player_preview_presenter;
    std::optional<wormedit::editor_interaction_state_t> compose_interaction_state;
    wormedit::viewport_input_controller_t compose_viewport_input;
    wormedit::editor_navigation_input_controller_t compose_navigation_input;
    application_command_state_t application_command_state;
    external_player_runtime_t external_player_runtime;
    initialize_external_player(external_player_runtime, status);
    application_command_state.external_player = [&](wormedit::sys::external_player_action_t action) {
        return launch_external_player(external_player_runtime, action, history, workspace,
                                      compose_interaction_state, asset_base);
    };
    wormedit::sys::inline_preview_cursor_t inline_preview_cursor;
    wormedit::stepmania_export_policy_t stepmania_export_policy;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    configure_transport_commands(application_command_state, history, workspace,
                                 compose_interaction_state, inline_preview_cursor,
                                 audition_session, preview_session, preview_state, audition_sources,
                                 preview_metronome_bindings, audition_output, preview_output);
#endif
    timing_grid_state_t timing_grid;
    auto command_registry = wormedit::make_default_editor_command_registry(
        [&](const wormedit::command_invocation_context_t& invocation) {
            return dispatch_registered_application_command(
                invocation, history, workspace, compose_interaction_state, timing_edit.stage,
                application_command_state, status);
        });
    const auto shortcut_startup = wormedit::load_platform_shortcut_profile(command_registry);
    append_shortcut_startup_diagnostic(status, shortcut_startup);
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
        decltype(status)& status_ref;
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
        status,
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
        decltype(running)& running_ref;
        decltype(status)& status_ref;
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
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        decltype(audition_reconciliation)& audition_reconciliation_ref;
#endif
    };
    workspace_frame_action_context_t workspace_frame_action_context{
        running,
        status,
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
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        audition_reconciliation,
#endif
    };
    while (running) {
        wormedit::sys::frame_timing().begin_frame();
        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::events);
        if (!wormedit::sys::poll_frontend_events(
                window, metadata_drop, workspace, history, asset_base,
                can_offer_audio_project_drop(session_loaded, fresh_blank_project, history),
                pending_audio_project_path, pending_chart_project_path, status))
            running = false;
        dispatch_dropped_chart_project(workspace_frame_action_context,
                                       pending_chart_project_path);
        wormedit::sys::poll_application_jobs(metadata_asset_runtime, resource_copy_runtime,
                                             export_runtime, history, workspace, asset_base,
                                             status);
        const auto compose_viewport_delta_seconds =
            wormedit::sys::compose_frame_delta_seconds(compose_viewport_last_ticks);
        tick_loaded_project_autosave(session_loaded, autosave_loop, history,
                                     native_project_loaded, active_project_path,
                                     platform_recovery.path, status);
        wormedit::sys::frame_timing().restore(wormedit::sys::frame_phase_t::count);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::audio_service);
        wormedit::sys::service_audio_transport_frame(
            history.document, workspace, preview_state, preview_session,
            audition_output, preview_output,
            application_command_state.preview_playhead_absolute_row, status);
        wormedit::sys::frame_timing().restore(wormedit::sys::frame_phase_t::count);
        waveform_runtime.service(
            history.document, workspace, compose_scene_cache,
            compose_interaction_state ? &*compose_interaction_state : nullptr, status);
#endif

        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::workspace);
        cancel_stale_compose_interaction(compose_interaction_state, history, workspace);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        if (workspace.active_tab == wormedit::workspace_tab_t::compose &&
            compose_interaction_state && application_command_state.preview_playhead_absolute_row &&
            (preview_session.active() || preview_state.paused)) {
            (void)wormedit::sys::follow_compose_playhead(
                history, *compose_interaction_state,
                *application_command_state.preview_playhead_absolute_row,
                preview_session.active());
        }
#endif

        wormedit::sys::rebind_workspace_after_edit(workspace, history, status);
        const auto model = wormedit::make_workspace_view(workspace, history.document);
        timing_edit.cancel_if_stale(history, workspace, application_command_state);
        if (!handle_workspace_view_error(model, status, running, history, workspace)) {
            prepare_bound_workspace_frame(history, workspace, application_command_state,
                                          timing_grid, audio_offset_drag, audio_offset_drag_tab,
                                          compose_scene_cache);
            auto flattened = wormedit::sys::project_application_frame(
                {.model = *model.view,
                 .history = history,
                 .workspace = workspace,
                 .status = status,
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
            validate_minimap_start_frame(application_command_state, history, compose_interaction_state);
            flattened.view.minimap_click_moves_paused_playhead = application_command_state.minimap_click_moves_paused_playhead;
            flattened.view.transport_rate_index = static_cast<uint8_t>(application_command_state.transport_rate_index);
            flattened.view.transport_rate_available = static_cast<bool>(application_command_state.transport_set_playback_rate);
            flattened.view.transport_keep_pitch = application_command_state.transport_keep_pitch;
            flattened.view.transport_keep_pitch_available = application_command_state.transport_keep_pitch_available;
            project_external_player_view(external_player_runtime, flattened.view);
            flattened.view.project_loaded = session_loaded;
            flattened.view.project_has_native_path = native_project_loaded;
            flattened.view.pending_new_project_audio_path = pending_audio_project_path.c_str();
            flattened.view.pending_bms_encoding_path = pending_bms_encoding_path.c_str();
            // Projection returns by value; refresh view pointers after any move.
            flattened.rebind_view_pointers();
            const auto frame_nuklear_previous = wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::nuklear);
            auto action = worm_ui_frontend_compose(&flattened.view);
            wormedit::sys::frame_timing().restore(frame_nuklear_previous);
            std::optional<std::uint32_t> selected_waveform_sample_rate;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            if (waveform_runtime.waveform_analysis() &&
                waveform_runtime.waveform_analysis()->waveform &&
                waveform_runtime.waveform_asset() &&
                workspace.transport.audio_asset == waveform_runtime.waveform_asset()) {
                selected_waveform_sample_rate =
                    waveform_runtime.waveform_analysis()->waveform->sample_rate;
            }
#endif
            const bool shortcut_handled = process_compose_frame_action(
                compose_frame_action_context, action, compose_viewport_delta_seconds);
            const bool external_handled = dispatch_external_player_action(external_player_runtime, action.external_player,
                application_command_state, status);
            dispatch_workspace_frame_action(
                workspace_frame_action_context, action, shortcut_handled || external_handled, selected_waveform_sample_rate,
                flattened.view.compose.viewport_height_pixels);
        }

        wormedit::sys::frame_timing().enter(wormedit::sys::frame_phase_t::render);
        wormedit::sys::render_application_frame(window);
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
    return 0;
}

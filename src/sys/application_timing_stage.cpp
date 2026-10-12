#include "sys/application_dispatch_context.h"
#include "sys/compose_note_to_bgm.h"
#include "sys/compose_sound_selection.h"
#if defined(WORMEDIT_HAS_MACOS_MENU)
#include "sys/macos_menu.h"
#endif
#include "sys/frame_timing.h"
#include "sys/thread_priority.h"
#include "wormedit/audio.h"
#include "wormedit/window.h"

#include "sys/application_command_dispatch.h"
#include "sys/application_commands.h"
#include "sys/application_defaults.h"
#include "sys/application_edit_helpers.h"
#include "sys/application_frame_projection.h"
#include "sys/application_frame_services.h"
#include "sys/application_startup.h"
#include "sys/application_startup_feedback.h"
#include "sys/audio_project_import.h"
#include "sys/audio_source_registration.h"
#include "sys/bga_dock_runtime.h"
#include "sys/bmson_waveform_projection.h"
#include "sys/chart_import_session.h"
#include "sys/compare_view.h"
#include "sys/compose_actions.h"
#include "sys/compose_audition_requests.h"
#include "sys/compose_bga_actions.h"
#include "sys/compose_input.h"
#include "sys/compose_minimap_navigation.h"
#include "sys/compose_resource_presenter.h"
#include "sys/compose_sound_actions.h"
#include "sys/compose_timing_target.h"
#include "sys/compose_view.h"
#include "sys/compose_waveform_overlays.h"
#include "sys/crash_backtrace.h"
#include "sys/external_player.h"
#include "sys/frontend_notification_sink.h"
#include "sys/inline_preview_cursor.h"
#include "sys/menu_command_projection.h"
#include "sys/metadata_actions.h"
#include "sys/metadata_asset_runtime.h"
#include "sys/metadata_drop_state.h"
#include "sys/metadata_view_projection.h"
#include "sys/notification_history_runtime.h"
#include "sys/options_runtime.h"
#include "sys/platform_recovery.h"
#include "sys/platform_shortcut_profile.h"
#include "sys/player_preview_presenter.h"
#include "sys/preferences_runtime.h"
#include "sys/project_import_feedback.h"
#include "sys/project_open.h"
#include "sys/project_save_paths.h"
#include "sys/project_view.h"
#include "sys/recipe_form.h"
#include "sys/recovery_notification_feedback.h"
#include "sys/shortcut_rows.h"
#include "sys/sound_usage_actions.h"
#include "sys/tap_timing_runtime.h"
#include "sys/timing_edit_runtime.h"
#include "sys/timing_view.h"
#include "sys/transport_player_actions.h"
#include "sys/waveform_view_projection.h"
#include "sys/workspace_frame_view.h"
#include "sys/workspace_projection.h"
#include "ui/frontend.h"
#include "wormedit/config.h"
#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_filesystem.h>
#include <set>
#include <wormedit/core/chart_editor.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/virtual_canvas.h>

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
#include <wormedit/core/lane_profile_switch.h>
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
#include "sys/audio_transport_frame.h"
#include "sys/chart_preview_state.h"
#include "sys/compose_playhead_cursor.h"
#include "sys/preview_cursor_frame.h"
#include "sys/sdl_audition_output.h"
#include "sys/sdl_preview_output.h"
#include "sys/waveform_analysis_runtime.h"
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
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

namespace wormedit::sys {
auto timing_stage_grid_step(const wormedit::chart_document_t& chart,
                            std::uint64_t active_denominator) -> std::uint64_t {
    // A snap the chart's resolution cannot represent falls back to one standard
    // measure.
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
                                                       usable_denominator, measures->total_rows)
                                 .value_or(raw_row);
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
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        action.timing_chart_id == 0 || workspace.selected_chart->value != action.timing_chart_id ||
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
        const auto direction =
            action.timing_bpm_step == -1  ? std::optional{bpm_step_direction_t::decrement}
            : action.timing_bpm_step == 1 ? std::optional{bpm_step_direction_t::increment}
                                          : std::nullopt;
        const auto stepped = current && direction
                                 ? step_bpm_by_one(current->beats_per_minute, *direction)
                                 : std::nullopt;
        if (!stepped ||
            (!preview.existing_event &&
             !timing_ratio_in_bounds(stepped->numerator, stepped->denominator, 1.0L, 1000.0L))) {
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
         !timing_ratio_in_bounds(*input.primary_numerator, *input.primary_denominator, 1.0L,
                                 1000.0L))) {
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
         !timing_ratio_in_bounds(*input.primary_numerator, *input.primary_denominator, -1000.0L,
                                 1000.0L))) {
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
         (!timing_ratio_in_bounds(*input.primary_numerator, *input.primary_denominator, 0.0L,
                                  1000.0L) ||
          !timing_unsigned_ratio_in_bounds(static_cast<std::uint64_t>(*input.secondary_numerator),
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
        !unit ||
        (!input.preview.existing_event &&
         !timing_unsigned_ratio_in_bounds(static_cast<std::uint64_t>(*input.primary_numerator),
                                          *input.primary_denominator, maximum))) {
        return std::nullopt;
    }
    return timing_lane_value_t{
        timing_pause_value_t{{*input.primary_numerator, *input.primary_denominator}, *unit}};
}

auto timing_stage_warp_value(const TimingStageInput& input, const wormedit::chart_document_t& chart,
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
    const auto step = input.preview.existing_event
                          ? std::uint64_t{1}
                          : timing_stage_grid_step(chart, active_snap_denominator);
    if (!index || !start || step == 0 ||
        units > (std::numeric_limits<std::uint64_t>::max() - *start) / step) {
        return std::nullopt;
    }
    const auto end_row = *start + units * step;
    if (end_row >= index->total_rows) {
        return std::nullopt;
    }
    const auto end = timing_position_at_row(*index, end_row);
    return end ? std::optional<timing_lane_value_t>{timing_warp_value_t{*end,
                                                                        warp_provenance_t::native}}
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

} // namespace wormedit::sys

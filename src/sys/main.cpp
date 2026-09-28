#include "wormedit/audio.h"
#include "wormedit/window.h"

#include "sys/application_commands.h"
#include "sys/application_command_dispatch.h"
#include "sys/application_defaults.h"
#include "sys/compose_resource_presenter.h"
#include "sys/compose_view.h"
#include "sys/compose_input.h"
#include "sys/compose_actions.h"
#include "sys/application_edit_helpers.h"
#include "sys/workspace_projection.h"
#include "sys/timing_view.h"
#include "sys/timing_edit_runtime.h"
#include "sys/metadata_drop_state.h"
#include "sys/metadata_asset_runtime.h"
#include "sys/metadata_view_projection.h"
#include "sys/platform_recovery.h"
#include "sys/platform_shortcut_profile.h"
#include "sys/player_preview_presenter.h"
#include "sys/waveform_view_projection.h"
#include "sys/bmson_waveform_projection.h"
#include "sys/compose_audition_requests.h"
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

auto stepmania_policy_from_ui(const WormUiWorkspaceAction& action)
    -> std::optional<wormedit::stepmania_export_policy_t> {
    wormedit::stepmania_export_policy_t policy;
    switch (action.stepmania_file_profile) {
    case WORM_UI_STEPMANIA_FILE_SM:
        policy.file_profile = wormedit::stepmania_profile_t::sm;
        break;
    case WORM_UI_STEPMANIA_FILE_SSC:
        policy.file_profile = wormedit::stepmania_profile_t::ssc;
        break;
    default:
        return std::nullopt;
    }
    switch (action.stepmania_timing_syntax) {
    case WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS:
        policy.timing_syntax = wormedit::stepmania_timing_syntax_t::native_warps;
        break;
    case WORM_UI_STEPMANIA_TIMING_LEGACY_NEGATIVE_BPM:
        policy.timing_syntax = wormedit::stepmania_timing_syntax_t::legacy_negative_bpm;
        break;
    case WORM_UI_STEPMANIA_TIMING_LEGACY_NEGATIVE_STOP:
        policy.timing_syntax = wormedit::stepmania_timing_syntax_t::legacy_negative_stop;
        break;
    default:
        return std::nullopt;
    }
    switch (action.stepmania_implementation) {
    case WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY:
        policy.implementation_profile =
            wormedit::stepmania_implementation_profile_t::unspecified;
        break;
    case WORM_UI_STEPMANIA_IMPLEMENTATION_STEP_MANIA_5_1:
        policy.implementation_profile =
            wormedit::stepmania_implementation_profile_t::stepmania_5_1;
        break;
    case WORM_UI_STEPMANIA_IMPLEMENTATION_ETTERNA_0_75_1:
        policy.implementation_profile =
            wormedit::stepmania_implementation_profile_t::etterna_0_75_1;
        break;
    default:
        return std::nullopt;
    }
    return policy;
}



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

auto begin_timing_stage(const WormUiWorkspaceAction& action,
                        const wormedit::document_history_t& history,
                        const wormedit::workspace_controller_t& workspace, bool timing_locked,
                        std::uint64_t active_snap_denominator)
    -> wormedit::timing_lane_pseudonote_preview_t {
    using namespace wormedit;
    timing_lane_pseudonote_preview_t rejected{};
    if ((workspace.active_tab != workspace_tab_t::timing &&
         workspace.active_tab != workspace_tab_t::compose) ||
        !workspace.selected_chart ||
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
    timing_lane_value_t initial{timing_bpm_value_t{}};
    switch (kind) {
    case timing_lane_kind_t::bpm:
        initial = timing_bpm_value_t{{120, 1}};
        break;
    case timing_lane_kind_t::scroll:
        initial = timing_scroll_value_t{{1, 1}};
        break;
    case timing_lane_kind_t::speed:
        initial = timing_speed_value_t{{1, 1}, {0, 1}, false};
        break;
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay:
        initial = timing_pause_value_t{{1, 1}, pause_unit_t::seconds};
        break;
    case timing_lane_kind_t::warp: {
        const auto measures_result = build_measure_index(*found);
        const auto* measures = std::get_if<measure_index_t>(&measures_result);
        const auto step = timing_stage_grid_step(*found, active_snap_denominator);
        const auto start = measures ? absolute_row(*measures, *position) : std::nullopt;
        if (!measures || !start || step == 0 ||
            step > std::numeric_limits<std::uint64_t>::max() - *start ||
            *start + step >= measures->total_rows) {
            return rejected;
        }
        const auto end = timing_position_at_row(*measures, *start + step);
        if (!end) {
            return rejected;
        }
        initial = timing_warp_value_t{*end, warp_provenance_t::native};
        break;
    }
    case timing_lane_kind_t::kiai:
        initial = timing_kiai_value_t{true, 0};
        break;
    case timing_lane_kind_t::anchor:
        initial = timing_anchor_value_t{0, false};
        break;
    }
    return begin_timing_lane_pseudonote(
        history, chart_id_t{action.timing_chart_id}, kind, *position, std::move(initial),
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

auto timing_stage_value(const wormedit::timing_lane_pseudonote_preview_t& preview,
                        const wormedit::chart_document_t& chart,
                        const WormUiWorkspaceAction& action, std::uint64_t active_snap_denominator)
    -> std::optional<wormedit::timing_lane_value_t> {
    using namespace wormedit;
    const auto primary_numerator = parse_timing_signed(action.timing_primary_numerator_text);
    const auto primary_unsigned = parse_timing_unsigned(action.timing_primary_numerator_text);
    const auto primary_denominator = parse_timing_unsigned(action.timing_primary_denominator_text);
    const auto secondary_numerator = parse_timing_signed(action.timing_secondary_numerator_text);
    const auto secondary_denominator =
        parse_timing_unsigned(action.timing_secondary_denominator_text);
    switch (preview.kind) {
    case timing_lane_kind_t::bpm: {
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
                 !timing_ratio_in_bounds(stepped->numerator, stepped->denominator, 1.0L, 1000.0L)))
                return std::nullopt;
            return timing_lane_value_t{timing_bpm_value_t{*stepped}};
        }
        if (action.timing_bpm_decimal_edited) {
            const auto bpm = parse_bpm_text(action.timing_bpm_decimal_text);
            if (!bpm || (!preview.existing_event &&
                         !timing_ratio_in_bounds(bpm->numerator, bpm->denominator, 1.0L, 1000.0L)))
                return std::nullopt;
            return timing_lane_value_t{timing_bpm_value_t{*bpm}};
        }
        if (!primary_numerator || !primary_denominator || *primary_numerator <= 0 ||
            (!preview.existing_event &&
             !timing_ratio_in_bounds(*primary_numerator, *primary_denominator, 1.0L, 1000.0L)))
            return std::nullopt;
        return timing_lane_value_t{timing_bpm_value_t{{*primary_numerator, *primary_denominator}}};
    }
    case timing_lane_kind_t::scroll: {
        if (!primary_numerator || !primary_denominator ||
            (!preview.existing_event &&
             !timing_ratio_in_bounds(*primary_numerator, *primary_denominator, -1000.0L, 1000.0L)))
            return std::nullopt;
        return timing_lane_value_t{
            timing_scroll_value_t{{*primary_numerator, *primary_denominator}}};
    }
    case timing_lane_kind_t::speed: {
        if (!primary_numerator || !primary_denominator || !secondary_numerator ||
            *secondary_numerator < 0 || !secondary_denominator || *primary_numerator < 0 ||
            (!preview.existing_event &&
             (!timing_ratio_in_bounds(*primary_numerator, *primary_denominator, 0.0L, 1000.0L) ||
              !timing_unsigned_ratio_in_bounds(static_cast<std::uint64_t>(*secondary_numerator),
                                               *secondary_denominator, 10000.0L)))) {
            return std::nullopt;
        }
        return timing_lane_value_t{timing_speed_value_t{
            {*primary_numerator, *primary_denominator},
            {static_cast<std::uint64_t>(*secondary_numerator), *secondary_denominator},
            action.timing_transition_is_seconds}};
    }
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay: {
        const auto source_unit = timing_pause_unit_from_ui(action.timing_pause_unit);
        const auto maximum_duration = [&]() -> long double {
            if (!source_unit) {
                return 0.0L;
            }
            switch (*source_unit) {
            case pause_unit_t::seconds:
                return 86'400.0L;
            case pause_unit_t::milliseconds:
                return 86'400'000.0L;
            case pause_unit_t::bms_stop_ticks:
            case pause_unit_t::beats:
                return 1'000'000.0L;
            }
            return 0.0L;
        }();
        if (!primary_numerator || !primary_denominator || *primary_numerator < 0 || !source_unit ||
            (!preview.existing_event &&
             !timing_unsigned_ratio_in_bounds(static_cast<std::uint64_t>(*primary_numerator),
                                              *primary_denominator, maximum_duration))) {
            return std::nullopt;
        }
        return timing_lane_value_t{
            timing_pause_value_t{{*primary_numerator, *primary_denominator}, *source_unit}};
    }
    case timing_lane_kind_t::warp: {
        if (!secondary_numerator || *secondary_numerator < 1 || !secondary_denominator ||
            *secondary_denominator != 1 ||
            (!preview.existing_event && *secondary_numerator > 1'000'000)) {
            return std::nullopt;
        }
        const auto units = static_cast<std::uint64_t>(*secondary_numerator);
        const auto measures = build_measure_index(chart);
        const auto* index = std::get_if<measure_index_t>(&measures);
        const auto start = index ? absolute_row(*index, preview.position) : std::nullopt;
        const auto step = preview.existing_event.has_value()
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
        return end ? std::optional<timing_lane_value_t>{timing_warp_value_t{
                         *end, warp_provenance_t::native}}
                   : std::nullopt;
    }
    case timing_lane_kind_t::kiai:
        return timing_lane_value_t{
            timing_kiai_value_t{action.timing_toggle_primary,
                                std::get<timing_kiai_value_t>(preview.value).retained_effect_bits}};
    case timing_lane_kind_t::anchor:
        if (!primary_unsigned || !primary_denominator || *primary_denominator != 1) {
            return std::nullopt;
        }
        return timing_lane_value_t{
            timing_anchor_value_t{*primary_unsigned, action.timing_toggle_primary}};
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

auto recipe_candidate_from_ui(const WormUiTimingRecipeForm& form,
                              const wormedit::sv_recipe_t& current)
    -> std::optional<wormedit::sv_recipe_t> {
    using namespace wormedit;
    if (form.output < WORM_UI_TIMING_RECIPE_OUTPUT_SV ||
        form.output > WORM_UI_TIMING_RECIPE_OUTPUT_SPEEDS ||
        form.curve < WORM_UI_TIMING_RECIPE_CURVE_CONSTANT ||
        form.curve > WORM_UI_TIMING_RECIPE_CURVE_BUMP ||
        form.sampling_mode < WORM_UI_TIMING_RECIPE_SAMPLING_FIXED ||
        form.sampling_mode > WORM_UI_TIMING_RECIPE_SAMPLING_ADAPTIVE ||
        form.fixed_extent < WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT ||
        form.fixed_extent > WORM_UI_TIMING_RECIPE_FIXED_STEP_ROWS) {
        return std::nullopt;
    }
    auto candidate = current;
    candidate.output = static_cast<sv_recipe_output_t>(form.output);
    candidate.curve = static_cast<sv_recipe_curve_t>(form.curve);
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
        if (!set_parameter(candidate.parameters.value_from, form.value_from)) {
            return std::nullopt;
        }
        break;
    case sv_recipe_curve_t::linear:
        if (!set_parameter(candidate.parameters.value_from, form.value_from) ||
            !set_parameter(candidate.parameters.value_to, form.value_to)) {
            return std::nullopt;
        }
        break;
    case sv_recipe_curve_t::exponential:
        if (!set_parameter(candidate.parameters.value_from, form.value_from) ||
            !set_parameter(candidate.parameters.value_to, form.value_to) ||
            !set_parameter(candidate.parameters.shape, form.shape)) {
            return std::nullopt;
        }
        break;
    case sv_recipe_curve_t::bump:
        if (!set_parameter(candidate.parameters.bump_start, form.bump_start) ||
            !set_parameter(candidate.parameters.bump_length, form.bump_length) ||
            !set_parameter(candidate.parameters.bump_x, form.bump_x) ||
            !set_parameter(candidate.parameters.bump_y, form.bump_y)) {
            return std::nullopt;
        }
        break;
    }
    candidate.sampling.mode = static_cast<sv_recipe_sampling_mode_t>(form.sampling_mode);
    if (candidate.sampling.mode == sv_recipe_sampling_mode_t::fixed) {
        if (form.fixed_extent == WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT) {
            const auto count = parse_timing_unsigned(form.fixed_point_count);
            if (!count || *count > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            candidate.sampling.fixed_point_count = static_cast<std::uint32_t>(*count);
            candidate.sampling.fixed_step_rows.reset();
        } else {
            const auto rows = parse_timing_unsigned(form.fixed_step_rows);
            if (!rows) {
                return std::nullopt;
            }
            candidate.sampling.fixed_point_count.reset();
            candidate.sampling.fixed_step_rows = *rows;
        }
    } else {
        const auto tolerance = parse_recipe_rational(form.tolerance);
        const auto max_points = parse_timing_unsigned(form.adaptive_max_points);
        if (!tolerance || !max_points || *max_points > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        candidate.sampling.fixed_point_count.reset();
        candidate.sampling.fixed_step_rows.reset();
        candidate.sampling.tolerance = *tolerance;
        candidate.sampling.max_points = static_cast<std::uint32_t>(*max_points);
    }
    if (candidate.output == sv_recipe_output_t::speeds) {
        const auto transition = parse_recipe_unsigned_rational(form.transition_duration);
        if (!transition) {
            return std::nullopt;
        }
        candidate.transition_duration = *transition;
        candidate.transition_is_seconds = form.transition_is_seconds;
    } else {
        candidate.transition_duration.reset();
        candidate.transition_is_seconds.reset();
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

auto dispatch_timing_recipe_action(
    const WormUiTimingRecipeAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace, bool timing_locked,
    std::uint64_t active_snap_denominator, timing_edit_t& timing_edit,
    std::string& status) -> bool {
    using namespace wormedit;
    auto& selection_binding = timing_edit.recipe_selection_binding;
    auto& draft = timing_edit.recipe_draft;
    auto& preview = timing_edit.recipe_preview;
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_NONE) {
        return false;
    }
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_ARM_SELECTION) {
        if (timing_locked || workspace.active_tab != workspace_tab_t::timing ||
            !workspace.selected_chart) {
            status = timing_locked ? "Unlock timing before selecting an SV recipe range."
                                   : "SV recipe selection requires the active Timing chart.";
            return true;
        }
        if (draft) {
            status = "Apply or cancel the current SV recipe before selecting another interval.";
            return true;
        }
        selection_binding = std::pair{*workspace.selected_chart, history.document.revision};
        status = "Drag over the Scroll / SV lane to select the recipe interval.";
        return true;
    }

    const bool binding_matches = workspace.active_tab == workspace_tab_t::timing &&
                                 workspace.selected_chart &&
                                 workspace.selected_chart->value == action.chart_id &&
                                 history.document.revision == action.document_revision;
    if (!binding_matches) {
        if (draft) {
            cancel_sv_recipe_draft(*draft);
        }
        draft.reset();
        preview.reset();
        selection_binding.reset();
        status = "SV recipe editing expired because its chart or revision changed.";
        return true;
    }
    const auto chart = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                         &chart_document_t::id);
    if (chart == history.document.charts.end()) {
        status = "The selected chart is no longer available.";
        return true;
    }
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_BEGIN) {
        if (timing_locked) {
            status = "Unlock timing before adding an SV recipe.";
            return true;
        }
        if (draft) {
            status = "Apply or cancel the current SV recipe before starting another.";
            return true;
        }
        if (!selection_binding || selection_binding->first != chart->id ||
            selection_binding->second != action.document_revision) {
            selection_binding.reset();
            status = "SV recipe range selection expired before the drag began.";
            return true;
        }
        const auto hits = timing_recipe_range_hits(*chart, action.range_begin_fraction,
                                                   action.range_end_fraction);
        if (!hits) {
            status = "The selected SV recipe interval is outside the chart grid.";
            selection_binding.reset();
            return true;
        }
        auto candidate = sv_recipe_t{
            .name = "SV function",
            .range = {hits->first, hits->second},
            .output = sv_recipe_output_t::sv,
            .curve = sv_recipe_curve_t::linear,
        };
        auto next = begin_sv_recipe_draft(history, chart->id, std::move(candidate), timing_locked);
        if (next.status != sv_recipe_draft_status_t::editing ||
            !set_sv_recipe_draft_range(history, next, hits->first, hits->second,
                                       active_snap_denominator, timing_locked)) {
            status = next.diagnostics.empty() ? "The selected interval cannot form an SV recipe."
                                              : next.diagnostics.front().message;
            selection_binding.reset();
            return true;
        }
        draft = std::move(next);
        selection_binding.reset();
        (void)refresh_timing_recipe_preview(history, *draft, preview, status);
        return true;
    }
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_CANCEL && !draft) {
        selection_binding.reset();
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
        cancel_sv_recipe_draft(*draft);
        draft.reset();
        preview.reset();
        selection_binding.reset();
        status = "SV recipe editing cancelled; the chart was not changed.";
        return true;
    }
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_UPDATE_RANGE) {
        const auto hits = timing_recipe_range_hits(*chart, action.range_begin_fraction,
                                                   action.range_end_fraction);
        if (!hits ||
            !set_sv_recipe_draft_range(history, *draft, hits ? hits->first : local_position_t{},
                                       hits ? hits->second : local_position_t{},
                                       active_snap_denominator, timing_locked)) {
            status = !hits || draft->diagnostics.empty()
                         ? "The selected interval cannot form an SV recipe."
                         : draft->diagnostics.front().message;
            return true;
        }
        const auto candidate = recipe_candidate_from_ui(action.form, draft->candidate);
        if (!candidate) {
            preview = sv_recipe_draft_preview_t{};
            preview->diagnostics.push_back({
                .severity = diagnostic_severity_t::error,
                .code = diagnostic_code_t::invalid_rational,
                .message = "Enter bounded exact recipe parameters and sampling values.",
            });
            status = preview->diagnostics.front().message;
            return true;
        }
        if (!update_sv_recipe_draft(history, *draft, *candidate, timing_locked)) {
            status = draft->diagnostics.empty() ? "The SV recipe draft could not be updated."
                                                : draft->diagnostics.front().message;
            return true;
        }
        (void)refresh_timing_recipe_preview(history, *draft, preview, status);
        return true;
    }

    const auto candidate = recipe_candidate_from_ui(action.form, draft->candidate);
    if (!candidate) {
        preview = sv_recipe_draft_preview_t{};
        preview->diagnostics.push_back({
            .severity = diagnostic_severity_t::error,
            .code = diagnostic_code_t::invalid_rational,
            .message = "Enter bounded exact recipe parameters and sampling values.",
        });
        status = preview->diagnostics.front().message;
        return true;
    }
    if (!update_sv_recipe_draft(history, *draft, *candidate, timing_locked)) {
        status = draft->diagnostics.empty() ? "The SV recipe draft could not be updated."
                                            : draft->diagnostics.front().message;
        return true;
    }
    const bool preview_valid = refresh_timing_recipe_preview(history, *draft, preview, status);
    if (action.kind == WORM_UI_TIMING_RECIPE_ACTION_APPLY) {
        if (!preview_valid) {
            return true;
        }
        const auto result = apply_sv_recipe_draft(history, *draft, timing_locked);
        if (!result.diagnostics.empty()) {
            status = result.diagnostics.front().message;
            return true;
        }
        if (!result.applied) {
            status = "SV recipe did not change the chart.";
            return true;
        }
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            status = rebound.diagnostics.empty()
                         ? "SV recipe applied, but workspace rebinding failed."
                         : rebound.diagnostics.front().message;
            return true;
        }
        const bool edited_existing = draft->existing_recipe.has_value();
        status = edited_existing ? "SV recipe updated." : "SV recipe added.";
        draft.reset();
        preview.reset();
        selection_binding.reset();
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

auto autosave_status_text(const wormedit::native_project_autosave_result_t& result) -> std::string {
    using status_t = wormedit::native_project_autosave_status_t;
    if (result.status == status_t::published)
        return "Autosaved revision " + std::to_string(result.revision) + ".";
    if (result.status != status_t::failed)
        return {};
    if (result.diagnostics.empty())
        return "Autosave failed; the previous recovery backup was retained.";
    const auto& diagnostic = result.diagnostics.front();
    auto message = "Autosave failed: " + diagnostic.message;
    if (!diagnostic.path.empty())
        message += " (" + diagnostic.path.generic_string() + ")";
    return message;
}

auto dispatch_recovery_ui_action(
    const WormUiWorkspaceAction& action, wormedit::native_project_recovery_controller_t& controller,
    wormedit::document_history_t& history, wormedit::workspace_controller_t& workspace,
    std::optional<wormedit::native_project_document_version_t> scanned_primary_version,
    wormedit::native_project_autosave_loop_t& autosave_loop) -> std::string {
    using namespace wormedit;
    const native_project_recovery_action_target_t target{
        .scan_generation = action.recovery_scan_generation,
        .row_index = action.recovery_row_index,
        .candidate_index = action.recovery_candidate_index,
    };
    native_project_recovery_controller_action_t request =
        inspect_native_project_recovery_action_t{.target = target};
    switch (action.kind) {
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT:
        request = inspect_native_project_recovery_action_t{.target = target};
        break;
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_RECOVER:
        if (!history.undo.empty() || !history.redo.empty()) {
            return "The current workspace changed after recovery discovery; save or restart "
                   "before replacing it with a recovery candidate.";
        }
        request = recover_native_project_recovery_action_t{
            .target = target,
            .expected_session =
                {
                    .is_set = true,
                    .version =
                        scanned_primary_version.has_value()
                            ? std::optional<
                                  native_project_document_version_t>{native_project_document_version_t{
                                  .project_id = history.document.id,
                                  .revision = history.document.revision}}
                            : std::nullopt,
                },
        };
        break;
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_SAVE_AS: {
        const auto end = std::find(std::begin(action.recovery_save_as_destination),
                                   std::end(action.recovery_save_as_destination), '\0');
        const std::string destination(action.recovery_save_as_destination, end);
        if (destination.empty()) {
            return "Choose a new project path before using Save as.";
        }
        request = save_as_native_project_recovery_action_t{
            .target = target, .destination = std::filesystem::path{destination}};
        break;
    }
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_DISCARD:
        request = discard_native_project_recovery_action_t{.target = target};
        break;
    case WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE:
        request = quarantine_native_project_recovery_action_t{.target = target};
        break;
    default:
        return "Invalid recovery action.";
    }

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
        if (!result.document) {
            return "Recovery action completed without a replacement document.";
        }
        {
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
        }
        // A recovered document is no longer the primary baseline captured by
        // this scan. Invalidate every remaining row token before returning to
        // the event loop so another click cannot replace the recovered session.
        (void)controller.replace_scan({});
        return "Recovery loaded into the current session. Review it and save explicitly.";
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

auto dispatch_metadata_ui_action(const WormUiWorkspaceAction& action,
                                 wormedit::document_history_t& history,
                                 wormedit::workspace_controller_t& workspace, bool timing_locked)
    -> std::string {
    using namespace wormedit;
    const auto failed = [](const document_command_result_t& result) {
        if (!result.diagnostics.empty())
            return result.diagnostics.front().message;
        return std::string{"Metadata edit was not applied."};
    };
    const auto publish = [&](document_command_result_t result) {
        if (!result.applied)
            return result.diagnostics.empty() ? std::string{"No metadata changes."}
                                              : failed(result);
        const auto rebound = rebind_workspace(workspace, history);
        if (!rebound) {
            return rebound.diagnostics.empty()
                       ? std::string{"Metadata changed, but workspace rebinding failed."}
                       : rebound.diagnostics.front().message;
        }
        return std::string{"Metadata updated."};
    };

    switch (action.metadata_edit) {
    case WORM_UI_METADATA_EDIT_SONG: {
        if (!action.metadata_title || !action.metadata_artist ||
            action.metadata_search_tag_count > WORM_UI_METADATA_SEARCH_TAG_CAPACITY ||
            (action.metadata_search_tag_count != 0 && !action.metadata_search_tags)) {
            return "Metadata edit payload is incomplete or exceeds the tag-count cap.";
        }
        std::vector<std::string> tags;
        tags.reserve(action.metadata_search_tag_count);
        for (std::size_t index = 0; index < action.metadata_search_tag_count; ++index) {
            if (!action.metadata_search_tags[index])
                return "Metadata edit contains an empty tag pointer.";
            tags.emplace_back(action.metadata_search_tags[index]);
        }
        return publish(apply_metadata_edit(history, action.metadata_document_revision,
                                           set_song_metadata_action_t{action.metadata_title,
                                                                      action.metadata_artist,
                                                                      std::move(tags)}));
    }
    case WORM_UI_METADATA_EDIT_CHART: {
        if (action.metadata_chart_id == 0 || !action.metadata_chart_name ||
            !action.metadata_chart_author ||
            (action.metadata_has_chart_rating &&
             (!action.metadata_chart_rating_numerator_text ||
              !action.metadata_chart_rating_denominator_text))) {
            return "Chart metadata edit payload is incomplete.";
        }
        const auto rating = make_chart_rating_metadata_edit(
            chart_id_t{action.metadata_chart_id}, !action.metadata_has_chart_rating,
            action.metadata_chart_rating_numerator_text
                ? action.metadata_chart_rating_numerator_text
                : "",
            action.metadata_chart_rating_denominator_text
                ? action.metadata_chart_rating_denominator_text
                : "");
        if (!rating) {
            return "Rating must use a signed 64-bit numerator and positive 64-bit denominator.";
        }
        return publish(apply_metadata_edit(
            history, action.metadata_document_revision,
            set_chart_metadata_action_t{chart_id_t{action.metadata_chart_id},
                                        action.metadata_chart_name, action.metadata_chart_author,
                                        rating->rating}));
    }
    case WORM_UI_METADATA_EDIT_AUDIO_OFFSET: {
        if (action.metadata_chart_id == 0 || !action.metadata_audio_offset_numerator_text ||
            !action.metadata_audio_offset_denominator_text) {
            return "Audio offset edit payload is incomplete.";
        }
        const auto numerator = parse_timing_signed(action.metadata_audio_offset_numerator_text);
        const auto denominator =
            parse_timing_unsigned(action.metadata_audio_offset_denominator_text);
        if (!numerator || !denominator || *denominator == 0) {
            return "Audio offset must use a signed 64-bit numerator and positive 64-bit "
                   "denominator.";
        }
        return publish(apply_audio_offset_edit(history, action.metadata_document_revision,
                                               chart_id_t{action.metadata_chart_id},
                                               {*numerator, *denominator}, timing_locked));
    }
    case WORM_UI_METADATA_EDIT_OUTPUT_PATH: {
        if (!action.metadata_output_path ||
            std::strlen(action.metadata_output_path) > WORM_UI_METADATA_OUTPUT_PATH_BYTES)
            return "Output path edit payload is incomplete or too long.";
        return publish(apply_metadata_edit(
            history, action.metadata_document_revision,
            set_output_group_path_action_t{action.metadata_output_group_index,
                                           action.metadata_output_path}));
    }
    case WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD:
    case WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD:
    case WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD: {
        if (action.metadata_format_family > static_cast<std::uint8_t>(format_family_t::quaver)) {
            return "Format metadata edit refers to an unknown format family.";
        }
        const format_metadata_target_t target{
            action.metadata_format_chart_scoped
                ? std::optional<chart_id_t>{chart_id_t{action.metadata_chart_id}}
                : std::nullopt,
            static_cast<format_family_t>(action.metadata_format_family)};
        metadata_edit_action_t edit;
        if (action.metadata_edit == WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD) {
            if (!action.metadata_format_name || !action.metadata_format_value)
                return "Format metadata replacement payload is incomplete.";
            edit = replace_format_metadata_field_action_t{target, action.metadata_format_index,
                                                          action.metadata_format_name,
                                                          action.metadata_format_value};
        } else if (action.metadata_edit == WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD) {
            edit = erase_format_metadata_field_action_t{target, action.metadata_format_index};
        } else {
            if (!action.metadata_format_name || !action.metadata_format_value)
                return "Format metadata insertion payload is incomplete.";
            edit = insert_format_metadata_field_action_t{target, action.metadata_format_index,
                                                         action.metadata_format_name,
                                                         action.metadata_format_value};
        }
        return publish(apply_metadata_edit(history, action.metadata_document_revision, edit));
    }
    case WORM_UI_METADATA_EDIT_NONE:
        break;
    }
    return "Invalid metadata edit action.";
}

auto dispatch_audio_offset_drag_action(const WormUiWorkspaceAction& action,
                                       wormedit::document_history_t& history,
                                       wormedit::workspace_controller_t& workspace,
                                       bool timing_locked,
                                       std::optional<std::uint32_t> selected_sample_rate,
                                       std::optional<wormedit::audio_offset_drag_t>& drag,
                                       std::optional<wormedit::workspace_tab_t>& drag_tab)
    -> std::string {
    using namespace wormedit;
    const auto supported_tab = workspace.active_tab == workspace_tab_t::audio_slices ||
                               workspace.active_tab == workspace_tab_t::metadata ||
                               workspace.active_tab == workspace_tab_t::compose;
    const auto context_matches =
        supported_tab && workspace.selected_chart &&
        workspace.selected_chart->value == action.audio_offset_drag_chart_id &&
        history.document.revision == action.audio_offset_drag_document_revision &&
        selected_sample_rate && *selected_sample_rate == action.audio_offset_drag_sample_rate;
    if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_CANCEL) {
        drag.reset();
        drag_tab.reset();
        return "Audio offset drag cancelled.";
    }
    if (!context_matches) {
        drag.reset();
        drag_tab.reset();
        return "Audio offset drag expired after its chart or waveform changed.";
    }
    if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_NUDGE) {
        if (drag) {
            return "Finish or cancel the active audio offset drag before nudging.";
        }
        const auto begun = begin_audio_offset_drag(history.document, *workspace.selected_chart,
                                                   *selected_sample_rate, timing_locked);
        if (!begun) {
            return begun.diagnostics.empty() ? "Audio offset nudge could not start."
                                             : begun.diagnostics.front().message;
        }
        const auto updated = update_audio_offset_drag(
            history.document, *begun.drag, action.audio_offset_drag_delta_frames, timing_locked);
        if (!updated) {
            return updated.diagnostics.empty() ? "Audio offset nudge was rejected."
                                               : updated.diagnostics.front().message;
        }
        const auto committed = commit_audio_offset_drag(history, *updated.drag, timing_locked);
        if (!committed.applied) {
            return committed.diagnostics.empty() ? "Audio offset was unchanged."
                                                 : committed.diagnostics.front().message;
        }
        const auto rebound = rebind_workspace(workspace, history);
        return rebound ? "Audio offset nudged by an exact frame count."
                       : "Audio offset changed, but workspace rebinding failed.";
    }
    if (action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_BEGIN) {
        const auto begun = begin_audio_offset_drag(history.document, *workspace.selected_chart,
                                                   *selected_sample_rate, timing_locked);
        drag = begun.drag;
        drag_tab = begun ? std::optional{workspace.active_tab} : std::nullopt;
        return begun                       ? "Dragging waveform offset; release to apply."
               : begun.diagnostics.empty() ? "Audio offset drag could not start."
                                           : begun.diagnostics.front().message;
    }
    if (!drag || !drag_tab || *drag_tab != workspace.active_tab ||
        drag->chart != *workspace.selected_chart ||
        drag->document_revision != action.audio_offset_drag_document_revision ||
        drag->sample_rate != *selected_sample_rate) {
        drag.reset();
        drag_tab.reset();
        return "Audio offset drag has no matching captured transaction.";
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

auto dispatch_bms_branch_choice_action(
    const WormUiWorkspaceAction& action, wormedit::document_history_t& history,
    wormedit::workspace_controller_t& workspace,
    const std::optional<wormedit::editor_interaction_state_t>& interaction_state) -> std::string {
    using namespace wormedit;
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id ||
        history.document.revision != action.compose_input_document_revision) {
        return "BMS branch action ignored because the chart or document revision changed.";
    }
    if (interaction_state && editor_interaction_has_pending_gesture(*interaction_state)) {
        return "Finish or cancel the active Compose gesture before changing a BMS branch.";
    }
    if (action.compose_branch_id == 0) {
        return "Choose a current BMS RANDOM or SWITCH control first.";
    }
    const auto applied = apply_bms_branch_choice_command(
        history, action.compose_input_document_revision,
        {.chart = *workspace.selected_chart,
         .branch = branch_id_t{action.compose_branch_id},
         .selected_value = action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE
                               ? std::optional<std::int64_t>{action.compose_branch_value}
                               : std::nullopt});
    if (!applied) {
        if (!applied.command.diagnostics.empty()) {
            return applied.command.diagnostics.front().message;
        }
        if (!applied.format_diagnostics.empty()) {
            return applied.format_diagnostics.front().message;
        }
        if (!applied.write_diagnostics.empty()) {
            return applied.write_diagnostics.front().message;
        }
        if (!applied.parse_diagnostics.empty()) {
            return applied.parse_diagnostics.front().message;
        }
        return "BMS branch choice was rejected without changing the document.";
    }
    const auto rebound = rebind_workspace(workspace, history);
    if (!rebound) {
        return rebound.diagnostics.empty() ? "BMS branch changed, but workspace rebinding failed."
                                           : rebound.diagnostics.front().message;
    }
    return action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE
               ? "BMS branch choice changed and its semantics were rematerialized."
               : "BMS branch choice cleared and its semantics were rematerialized.";
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
    set("selection.clear", compose_ready && (pending || !compose_state->selected_objects.empty() ||
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
        const bool stage_matches =
            (workspace.active_tab == wormedit::workspace_tab_t::timing ||
             workspace.active_tab == wormedit::workspace_tab_t::compose) &&
            workspace.selected_chart && workspace.selected_chart->value == action.timing_chart_id &&
            timing_stage->chart.value == action.timing_chart_id &&
            (timing_stage->existing_event.has_value()
                 ? timing_stage->existing_event->value
                 : std::uint64_t{}) == action.timing_object_id &&
            timing_stage->document_revision == action.timing_document_revision &&
            history.document.revision == action.timing_document_revision;
        if (!stage_matches) {
            wormedit::cancel_timing_lane_pseudonote(*timing_stage);
            timing_stage.reset();
            status = "Timing event staging expired because its chart or revision changed.";
        } else {
            const auto chart = std::ranges::find(history.document.charts, timing_stage->chart,
                                                 &wormedit::chart_document_t::id);
            if (action.timing_stage_action == WORM_UI_TIMING_STAGE_DELETE) {
                const auto kind = timing_lane_kind_from_ui(action.timing_lane);
                if (!timing_stage->existing_event || !kind || *kind != timing_stage->kind) {
                    status = "The selected timing event cannot be deleted.";
                } else {
                    const auto result = wormedit::delete_existing_timing_lane_event(
                        history, timing_stage->chart, *kind, *timing_stage->existing_event,
                        action.timing_document_revision, timing_locked);
                    status = result                       ? "Timing event deleted."
                             : result.diagnostics.empty() ? "Timing event deletion was rejected."
                                                          : result.diagnostics.front().message;
                }
                wormedit::cancel_timing_lane_pseudonote(*timing_stage);
                timing_stage.reset();
            } else if (action.timing_stage_action == WORM_UI_TIMING_STAGE_MOVE) {
                const auto target =
                    chart == history.document.charts.end() || !timing_stage->existing_event ||
                            !action.timing_has_target_y
                        ? std::optional<wormedit::local_position_t>{}
                        : timing_position_from_fraction(*chart, action.timing_target_y_fraction,
                                                        active_snap_denominator);
                if (!target || !wormedit::move_existing_timing_lane_pseudonote(
                                   history, *timing_stage, *target, timing_locked)) {
                    status = timing_stage->diagnostics.empty()
                                 ? "The timing marker cannot move to that row."
                                 : timing_stage->diagnostics.front().message;
                } else {
                    status = "Timing marker moved in the staged preview; apply to commit.";
                }
            } else {
                dispatch_timing_stage_value_action(
                    action, history, timing_locked, active_snap_denominator,
                    chart == history.document.charts.end() ? nullptr : &*chart,
                    timing_stage, status);
            }
        }
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

} // namespace

int main(int argc, char** argv) {
    constexpr int maximum_startup_paths = 30;
    if (argc > maximum_startup_paths + 1) {
        std::fprintf(stderr, "usage: wormedit [project.wormedit [recovery-artifact ...]]\n");
        return 2;
    }
    wormedit::native_project_startup_request_t startup_request;
    if (argc > 1)
        startup_request.primary_path = std::filesystem::path{argv[1]};
    for (int index = 2; index < argc; ++index)
        startup_request.recovery_entries.emplace_back(argv[index]);
    const auto requested_primary_path = startup_request.primary_path;

    if (!SDL_Init(0)) {
        std::fprintf(stderr, "SDL init failed\n");
        return 1;
    }
    const auto platform_recovery = wormedit::prepare_platform_recovery_directory();
    if (platform_recovery) {
        startup_request.recovery_directories.push_back(platform_recovery.path);
    }
    auto startup = wormedit::prepare_native_project_startup(
        initial_document(new_session_project_id()), std::move(startup_request));

    WormAudioSystem audio{};
    if (!worm_audio_init(&audio)) {
        std::fprintf(stderr, "Audio subsystem init failed\n");
        SDL_Quit();
        return 1;
    }

    WormWindow window{};
    const WormWindowDesc window_desc{"wormedit", 1280, 720};
    if (!worm_window_create(&window, &window_desc)) {
        worm_audio_shutdown(&audio);
        SDL_Quit();
        return 1;
    }

    if (!worm_ui_frontend_init(window.handle, window.renderer)) {
        worm_window_destroy(&window);
        worm_audio_shutdown(&audio);
        SDL_Quit();
        return 1;
    }

    auto history =
        wormedit::make_document_history(std::move(startup.document), 128, startup.loaded_primary);
    auto binding = wormedit::make_workspace_controller(history);
    if (!binding) {
        std::fprintf(stderr, "Initial workspace binding failed\n");
        worm_ui_frontend_shutdown();
        worm_window_destroy(&window);
        worm_audio_shutdown(&audio);
        SDL_Quit();
        return 1;
    }
    auto workspace = std::move(*binding.workspace);
    const auto initial_autosave_sequence = wormedit::native_project_recovery_session_sequence(
        startup.recovery_scan, history.document.id);
    wormedit::task_scheduler_t autosave_scheduler(1, 1);
    wormedit::native_project_autosave_loop_t autosave_loop(
        wormedit::native_project_autosave_loop_t::clock_t::now(), autosave_scheduler,
        startup.primary_version, initial_autosave_sequence);
    std::string status;
    if (startup.loaded_primary) {
        status = "Opened native project: " + requested_primary_path.generic_string();
    } else if (!requested_primary_path.empty()) {
        status = "Primary project was not loaded; review the completed recovery scan.";
    } else if (platform_recovery) {
        status = "New unsaved workspace ready; platform recovery scan complete.";
    } else {
        status = "New unsaved workspace ready; recovery discovery unavailable.";
    }
    if (!platform_recovery && !platform_recovery.error.empty()) {
        status += " " + platform_recovery.error;
    }
    std::optional<wormedit::audio_offset_drag_t> audio_offset_drag;
    std::optional<wormedit::workspace_tab_t> audio_offset_drag_tab;
    const auto asset_base =
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
    wormedit::audio::decoded_block_cache_t waveform_blocks;
    wormedit::audio::waveform_cache_t waveform_cache;
    wormedit::task_scheduler_t waveform_scheduler(2, 4);
    wormedit::audio::waveform_job_t waveform_job{audition_sources, waveform_blocks, waveform_cache,
                                                 waveform_scheduler};
    wormedit::audio::waveform_job_t channel_waveform_job{audition_sources, waveform_blocks,
                                                         waveform_cache, waveform_scheduler};
    std::optional<wormedit::asset_id_t> waveform_job_asset;
    std::shared_ptr<const wormedit::audio::waveform_analysis_result_t> waveform_analysis;
    std::optional<wormedit::asset_id_t> channel_waveform_job_asset;
    std::shared_ptr<const wormedit::audio::waveform_analysis_result_t> channel_waveform_analysis;
    wormedit::audio::decoded_block_cache_t audition_cache;
    wormedit::audio::transport_t audition_transport{audition_cache};
    wormedit::audio::audition_transport_bridge_t audition_bridge{audition_transport};
    wormedit::audio::object_audition_controller_t object_audition{audition_bridge};
    wormedit::audio::audition_session_t audition_session{audition_sources, audition_transport,
                                                         audition_bridge};
    wormedit::sys::sdl_audition_output_t audition_output{audition_session};
    wormedit::audio::decoded_block_cache_t preview_cache;
    wormedit::audio::transport_t preview_transport{preview_cache};
    wormedit::audio::preview_transport_bridge_t preview_bridge{preview_transport};
    wormedit::audio::preview_session_t preview_session{audition_sources, preview_transport,
                                                       preview_bridge};
    wormedit::sys::sdl_preview_output_t preview_output{preview_session};
    wormedit::sys::runtime::chart_preview_t preview_state;
    std::vector<wormedit::audio::preview_metronome_source_binding_t> preview_metronome_bindings;
    using audition_asset_entry_t = std::pair<wormedit::asset_id_t, std::filesystem::path>;
    std::optional<wormedit::song_id_t> audition_source_project;
    std::vector<audition_asset_entry_t> audition_asset_inventory;
    std::uint64_t audition_source_generation{};
    const auto reconcile_audition_sources = [&](bool stop_playback) -> std::optional<std::size_t> {
        std::vector<audition_asset_entry_t> next_inventory;
        next_inventory.reserve(history.document.audio_assets.size());
        for (const auto& asset : history.document.audio_assets) {
            next_inventory.emplace_back(asset.id, asset.source_path);
        }
        if (audition_source_project == history.document.id &&
            audition_asset_inventory == next_inventory) {
            return std::nullopt;
        }
        if (stop_playback) {
            static_cast<void>(audition_output.stop());
            static_cast<void>(preview_output.stop());
            preview_state.invalidate();
        }
        waveform_job.cancel();
        waveform_job_asset.reset();
        waveform_analysis.reset();
        channel_waveform_job.cancel();
        channel_waveform_job_asset.reset();
        channel_waveform_analysis.reset();
        audition_sources.clear();
        waveform_blocks.clear();
        waveform_cache.clear();
        preview_metronome_bindings.clear();
        ++audition_source_generation;
        const auto unavailable = wormedit::sys::register_audition_sources(
            history.document, asset_base, audition_source_generation,
            audition_sources, preview_metronome_bindings);
        audition_source_project = history.document.id;
        audition_asset_inventory = std::move(next_inventory);
        return unavailable;
    };
    const auto initial_unavailable_audio_assets = reconcile_audition_sources(false).value_or(0);
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
    if (startup.recovery_scan_configured)
        (void)recovery_controller.replace_scan(std::move(startup.recovery_scan));
    const auto recovery_scan_configured = startup.recovery_scan_configured;
    const auto scanned_primary_version = startup.primary_version;

    timing_scene_cache_t timing_scene_cache;
    timing_edit_t timing_edit;
    compose_scene_cache_t compose_scene_cache;
    wormedit::sys::player_preview_presenter_t player_preview_presenter;
    std::optional<wormedit::editor_interaction_state_t> compose_interaction_state;
    wormedit::viewport_input_controller_t compose_viewport_input;
    wormedit::editor_navigation_input_controller_t compose_navigation_input;
    application_command_state_t application_command_state;
    wormedit::stepmania_export_policy_t stepmania_export_policy;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    application_command_state.transport_active = [&] {
        return audition_session.active() || preview_session.active() || preview_state.paused;
    };
    application_command_state.transport_play_pause = [&]() -> std::string {
        return preview_state.play_pause(
            selected_chart(history, workspace), history.document, audition_sources,
            preview_metronome_bindings, audition_output, preview_session, preview_output);
    };
    application_command_state.transport_seek = [&](std::uint64_t frame) -> std::string {
        return preview_state.seek(frame, history.document, workspace, audition_output,
                                  preview_session, preview_output);
    };
    application_command_state.transport_audio_milliseconds = [&]() {
        return preview_state.audio_milliseconds(history.document, workspace, preview_session);
    };
    application_command_state.transport_seek_audio_milliseconds =
        [&](std::int64_t milliseconds) -> std::string {
        return preview_state.seek_audio_milliseconds(
            milliseconds, history.document, workspace, audition_output,
            preview_session, preview_output);
    };
    application_command_state.transport_stop = [&] {
        preview_state.stop(audition_output, preview_output);
    };
#endif
    timing_grid_state_t timing_grid;
    auto command_registry = wormedit::make_default_editor_command_registry(
        [&](const wormedit::command_invocation_context_t& invocation) {
            const auto command = parse_application_command(invocation.command.value);
            if (!command) {
                return wormedit::sys::reject_application_command(
                    status, "Command is not implemented by the application.");
            }
            return wormedit::sys::invoke_application_command(*command, history, workspace,
                                              compose_interaction_state, timing_edit.stage,
                                              application_command_state, status);
        });
    const auto shortcut_startup = wormedit::load_platform_shortcut_profile(command_registry);
    if (!shortcut_startup.diagnostics.empty()) {
        status += " " + shortcut_startup.diagnostics.front().message;
    }
    bool running = true;
    metadata_drop_state_t metadata_drop;
    auto compose_viewport_last_ticks = static_cast<std::uint64_t>(SDL_GetTicks());
    while (running) {
        SDL_Event event;
        worm_ui_frontend_begin_input();
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                running = false;
            }
            metadata_drop.handle_file_drop(event, window.handle, workspace, history,
                                           asset_base, status);
            worm_ui_frontend_handle_event(&event);
        }
        worm_ui_frontend_end_input();
        if (workspace.active_tab != wormedit::workspace_tab_t::metadata) {
            metadata_drop.clear_paths();
        }
        if (auto completed = metadata_asset_runtime.poll(history, workspace, asset_base))
            status = std::move(*completed);
        if (auto completed = resource_copy_runtime.poll(history, workspace))
            status = std::move(*completed);
        if (auto completed = export_runtime.poll(history))
            status = std::move(*completed);
        const auto compose_viewport_now_ticks = static_cast<std::uint64_t>(SDL_GetTicks());
        const auto compose_viewport_delta_seconds =
            compose_viewport_now_ticks >= compose_viewport_last_ticks
                ? static_cast<double>(compose_viewport_now_ticks - compose_viewport_last_ticks) /
                      1000.0
                : 0.0;
        compose_viewport_last_ticks = compose_viewport_now_ticks;

        const auto autosave_target = wormedit::make_native_project_autosave_target(
            history.document.id, startup.loaded_primary, requested_primary_path,
            platform_recovery.path);
        if (const auto autosave_result = autosave_loop.tick(
                history.document, wormedit::is_document_dirty(history),
                wormedit::native_project_autosave_loop_t::clock_t::now(), autosave_target)) {
            const auto autosave_text = autosave_status_text(*autosave_result);
            if (!autosave_text.empty())
                status = autosave_text;
        }
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        const bool preview_binding_current =
            preview_state.matches_selection(history.document, workspace);
        if (preview_state.chart && !preview_binding_current) {
            const bool interrupted = preview_session.active() || preview_state.paused;
            if (preview_session.active())
                static_cast<void>(preview_output.stop());
            preview_state.invalidate();
            if (interrupted)
                status = "Chart preview stopped because its document or selected chart changed.";
        }
        if (const auto serviced = audition_output.service(); serviced && !*serviced) {
            static_cast<void>(audition_output.stop());
            if (serviced->status == wormedit::sys::sdl_audition_service_status_t::device_failed) {
                status = "Audio preview stopped after an output-device failure.";
            } else if (serviced->status ==
                       wormedit::sys::sdl_audition_service_status_t::render_failed) {
                status = "Audio preview stopped after a realtime cache underrun.";
            } else if (serviced->pump &&
                       serviced->pump->status ==
                           wormedit::audio::audition_pump_status_t::source_unavailable) {
                status = "Audio preview stopped because its decoded source is unavailable.";
            } else {
                status = "Audio preview stopped after a bounded lookahead failure.";
            }
        }
        if (const auto serviced = preview_output.service(); serviced && !*serviced) {
            static_cast<void>(preview_output.stop());
            preview_state.reset_transport();
            if (serviced->status == wormedit::sys::sdl_preview_service_status_t::device_failed) {
                status = "Chart preview stopped after an output-device failure.";
            } else if (serviced->status ==
                       wormedit::sys::sdl_preview_service_status_t::render_failed) {
                status = "Chart preview stopped after a realtime cache underrun.";
            } else if (serviced->pump && serviced->pump->decode &&
                       serviced->pump->decode->status ==
                           wormedit::audio::preview_pump_status_t::source_unavailable) {
                status = "Chart preview stopped because a decoded source is unavailable.";
            } else {
                status = "Chart preview stopped after a bounded refill failure.";
            }
        }
        if (!preview_session.active() && !preview_state.paused)
            preview_state.resume_frame = 0;
        application_command_state.preview_playhead_absolute_row.reset();
        if (preview_state.timing && (preview_session.active() || preview_state.paused)) {
            const auto frame = preview_state.current_frame(preview_session);
            if (frame <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                const auto position = wormedit::musical_position_at(
                    *preview_state.timing,
                    {static_cast<std::int64_t>(frame), wormedit::audio::output_sample_rate});
                if (position && position.absolute_row->denominator != 0) {
                    const auto row = static_cast<double>(position.absolute_row->numerator) /
                                     static_cast<double>(position.absolute_row->denominator);
                    if (std::isfinite(row))
                        application_command_state.preview_playhead_absolute_row = row;
                }
            }
        }
        if (workspace.transport.audio_asset != waveform_job_asset) {
            waveform_job.cancel();
            waveform_job_asset.reset();
            waveform_analysis.reset();
            if (workspace.transport.audio_asset) {
                const auto started = waveform_job.request(*workspace.transport.audio_asset, 256);
                if (started) {
                    waveform_job_asset = workspace.transport.audio_asset;
                } else if (started.status !=
                           wormedit::audio::waveform_job_start_status_t::capacity_exceeded) {
                    status = started.message.empty() ? "Waveform analysis could not start."
                                                     : started.message;
                }
            }
        }
        if (const auto completed = waveform_job.poll();
            completed && waveform_job_asset == completed->asset) {
            if (*completed) {
                waveform_analysis = completed->analysis;
            } else if (completed->task_error) {
                status = completed->task_error->message;
            }
        }
        std::optional<wormedit::asset_id_t> selected_channel_waveform_asset;
        if (workspace.active_tab == wormedit::workspace_tab_t::compose &&
            workspace.selected_chart && compose_interaction_state &&
            compose_interaction_state->chart == *workspace.selected_chart &&
            compose_interaction_state->selected_objects.size() == 1 &&
            compose_interaction_state->selected_objects.front().kind ==
                wormedit::editor_selection_object_kind_t::bgm_event) {
            const auto chart = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                                 &wormedit::chart_document_t::id);
            if (chart != history.document.charts.end()) {
                const auto selection = selected_bmson_waveform_source(
                    compose_scene_cache, *chart, history.document.revision,
                    compose_interaction_state->selected_objects.front().id);
                if (selection)
                    selected_channel_waveform_asset = selection->asset;
            }
        }
        if (selected_channel_waveform_asset == workspace.transport.audio_asset)
            selected_channel_waveform_asset.reset();
        if (selected_channel_waveform_asset != channel_waveform_job_asset) {
            channel_waveform_job.cancel();
            channel_waveform_job_asset.reset();
            channel_waveform_analysis.reset();
            if (selected_channel_waveform_asset) {
                const auto started =
                    channel_waveform_job.request(*selected_channel_waveform_asset, 256);
                if (started)
                    channel_waveform_job_asset = selected_channel_waveform_asset;
                else if (started.status !=
                         wormedit::audio::waveform_job_start_status_t::capacity_exceeded)
                    status = started.message.empty() ? "Channel waveform analysis could not start."
                                                     : started.message;
            }
        }
        if (const auto completed = channel_waveform_job.poll();
            completed && channel_waveform_job_asset == completed->asset) {
            if (*completed) {
                channel_waveform_analysis = completed->analysis;
                if (!channel_waveform_analysis->waveform)
                    status = "Selected BMSON channel waveform could not be analyzed.";
            } else if (completed->task_error) {
                status = completed->task_error->message;
            }
        }
#endif

        cancel_stale_compose_interaction(compose_interaction_state, history, workspace);

        // Every document mutation must leave the next frame bound to its new
        // revision. Keep this boundary authoritative even when a command path
        // forgets its local rebind after committing.
        if (workspace.song == history.document.id &&
            (workspace.document_revision != history.document.revision ||
             workspace.history.undo_depth != history.undo.size() ||
             workspace.history.redo_depth != history.redo.size() ||
             workspace.history.dirty != wormedit::is_document_dirty(history))) {
            const auto rebound = wormedit::rebind_workspace(workspace, history);
            if (!rebound)
                status = rebound.diagnostics.empty()
                             ? "Workspace could not follow the latest document edit."
                             : rebound.diagnostics.front().message;
        }
        const auto model = wormedit::make_workspace_view(workspace, history.document);
        timing_edit.cancel_if_stale(history, workspace, application_command_state);
        if (!model) {
            status = model.diagnostics.empty() ? "Workspace view unavailable"
                                               : model.diagnostics.front().message;
            const auto recovery_action = worm_ui_frontend_workspace_error(status.c_str());
            if (recovery_action.kind == WORM_UI_WORKSPACE_ACTION_QUIT) {
                running = false;
            } else if (recovery_action.kind == WORM_UI_WORKSPACE_ACTION_SWITCH_TAB) {
                const auto changed = wormedit::apply_workspace_action(
                    workspace, history.document, history.document.revision,
                    wormedit::workspace_switch_tab_action_t{wormedit::workspace_tab_t::setup});
                if (!changed && !changed.diagnostics.empty())
                    status = changed.diagnostics.front().message;
            }
        } else {
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
                 !workspace.selected_chart ||
                 *workspace.selected_chart != audio_offset_drag->chart ||
                 history.document.revision != audio_offset_drag->document_revision ||
                 application_command_state.timing_locked)) {
                audio_offset_drag.reset();
                audio_offset_drag_tab.reset();
            }
            if (compose_scene_cache.beat_fit_drag &&
                (workspace.active_tab != wormedit::workspace_tab_t::compose ||
                 !workspace.selected_chart ||
                 *workspace.selected_chart != compose_scene_cache.beat_fit_drag->chart ||
                 history.document.revision !=
                     compose_scene_cache.beat_fit_drag->document_revision ||
                 workspace.transport.audio_asset != compose_scene_cache.beat_fit_asset ||
                 application_command_state.timing_locked)) {
                compose_scene_cache.beat_fit_drag.reset();
                compose_scene_cache.beat_fit_asset.reset();
            }
            auto flattened =
                flatten_workspace(*model.view, history.document, status, recovery_controller.view(),
                                  recovery_scan_configured, stepmania_export_policy);
            if (flattened.view.setup.visible) {
                flattened.view.setup.stepmania_policy_status =
                    flattened.setup_policy_status_storage.c_str();
            }
            flattened.view.metadata.timing_locked = application_command_state.timing_locked;
            flattened.view.metadata.audio_waveform_has_selected_asset =
                workspace.transport.audio_asset.has_value();
            flattened.view.metadata.audio_waveform_selected_asset_id =
                workspace.transport.audio_asset ? workspace.transport.audio_asset->value : 0;
            auto& audio_view = flattened.view.audio_slices;
            audio_view.document_revision = history.document.revision;
            audio_view.timing_locked = application_command_state.timing_locked;
            if (workspace.selected_chart) {
                const auto selected =
                    std::ranges::find(history.document.charts, *workspace.selected_chart,
                                      &wormedit::chart_document_t::id);
                if (selected != history.document.charts.end()) {
                    audio_view.has_chart = true;
                    audio_view.chart_id = selected->id.value;
                    audio_view.offset_numerator =
                        selected->beat_zero_audio_offset_seconds.numerator;
                    audio_view.offset_denominator =
                        selected->beat_zero_audio_offset_seconds.denominator;
                }
            }
            if (audio_offset_drag) {
                audio_view.offset_drag_active = true;
                audio_view.offset_drag_delta_frames = audio_offset_drag->delta_frames;
                audio_view.proposed_offset_numerator =
                    audio_offset_drag->proposed_offset_seconds.numerator;
                audio_view.proposed_offset_denominator =
                    audio_offset_drag->proposed_offset_seconds.denominator;
                flattened.view.metadata.audio_offset_drag_active = true;
                flattened.view.metadata.audio_offset_drag_delta_frames =
                    audio_offset_drag->delta_frames;
                flattened.view.metadata.proposed_audio_offset_numerator =
                    audio_offset_drag->proposed_offset_seconds.numerator;
                flattened.view.metadata.proposed_audio_offset_denominator =
                    audio_offset_drag->proposed_offset_seconds.denominator;
            }
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            flattened.view.transport_available =
                workspace.selected_chart.has_value() && preview_output.available();
            flattened.view.transport_playing =
                preview_session.active() || audition_session.active();
            flattened.view.transport_paused = preview_state.paused;
            flattened.view.transport_frame =
                preview_session.active()    ? preview_session.timeline_frame()
                : audition_session.active() ? audition_session.timeline_frame()
                                            : preview_state.resume_frame;
            flattened.view.transport_end_frame = preview_state.end_frame;
            flattened.view.transport_sample_rate = wormedit::audio::output_sample_rate;
#else
            flattened.view.transport_available = false;
            flattened.view.transport_playing = false;
            flattened.view.transport_paused = false;
            flattened.view.transport_frame = 0;
            flattened.view.transport_end_frame = 0;
            flattened.view.transport_sample_rate = 44'100;
#endif
            player_preview_presenter.populate(
                flattened.view.player_preview, history, workspace.selected_chart,
                application_command_state.player_preview_visible,
                application_command_state.player_preview_skin, flattened.view.transport_frame,
                flattened.view.transport_sample_rate, asset_base
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
                ,
                player_image_job
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
                ,
                player_video_job
#endif
            );
            for (std::size_t line = 0; line < flattened.view.summary_line_count; ++line) {
                flattened.view.summary_lines[line] = flattened.summary_storage[line].c_str();
            }
            for (std::size_t row = 0; row < flattened.view.recovery_candidate_count; ++row) {
                flattened.view.recovery_candidates[row].path_label =
                    flattened.recovery_path_storage[row].c_str();
                flattened.view.recovery_candidates[row].summary =
                    flattened.recovery_summary_storage[row].c_str();
                flattened.view.recovery_candidates[row].disabled_reason =
                    flattened.recovery_disabled_storage[row].empty()
                        ? nullptr
                        : flattened.recovery_disabled_storage[row].c_str();
            }
            for (std::size_t diagnostic = 0; diagnostic < flattened.view.recovery_diagnostic_count;
                 ++diagnostic) {
                flattened.view.recovery_diagnostics[diagnostic] =
                    flattened.recovery_diagnostic_storage[diagnostic].c_str();
            }
            if (flattened.view.audio_slices.visible) {
                flattened.view.audio_slices.asset_ids = flattened.audio_asset_storage.data();
            }
            flattened.view.compose.branch_rows = flattened.compose_branch_row_storage.data();
            flattened.view.compose.branch_choices = flattened.compose_branch_choice_storage.data();
            flattened.view.compose.resources = flattened.compose_resource_storage.data();
            for (std::size_t resource = 0;
                 resource < flattened.view.compose.resource_count; ++resource) {
                flattened.compose_resource_storage[resource].target_index =
                    flattened.compose_resource_index_storage[resource].c_str();
                flattened.compose_resource_storage[resource].name =
                    flattened.compose_resource_name_storage[resource].c_str();
                flattened.compose_resource_storage[resource].source_name =
                    flattened.compose_resource_path_storage[resource].c_str();
            }
            for (std::size_t row = 0; row < flattened.view.compose.branch_row_count; ++row) {
                flattened.compose_branch_row_storage[row].display_text =
                    flattened.compose_branch_text_storage[row].c_str();
            }
            for (std::size_t diagnostic = 0;
                 diagnostic < flattened.view.compose.branch_diagnostic_count; ++diagnostic) {
                flattened.view.compose.branch_diagnostics[diagnostic] =
                    flattened.compose_branch_diagnostic_storage[diagnostic].c_str();
            }
            wormedit::sys::populate_metadata_view_projection(
                flattened, history, workspace, asset_base, metadata_drop, metadata_asset_runtime);
            int compose_window_width = 0;
            int compose_window_height = 0;
            worm_window_get_size(&window, &compose_window_width, &compose_window_height);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            wormedit::sys::populate_audio_waveform_views(
                flattened, waveform_analysis.get(), waveform_job.busy(), compose_window_width);
#endif
            const bool timing_for_compose = flattened.view.compose.visible;
            if (timing_for_compose) {
                flattened.view.timing.visible = true;
            }
            populate_timing_view(flattened, history, workspace, timing_scene_cache,
                                 compose_window_width, compose_window_height, timing_grid,
                                 timing_edit.stage_view(), timing_edit.recipe_draft_view(),
                                 timing_edit.recipe_preview_view(),
                                 timing_edit.recipe_selection_binding.has_value());
            flattened.view.timing.initial_bpm_editable =
                flattened.view.timing.scene_available &&
                !application_command_state.timing_locked;
            if (timing_for_compose) {
                flattened.view.timing.visible = false;
            }
            populate_compose_view(flattened, history, workspace, compose_scene_cache,
                                  compose_window_width, compose_window_height,
                                  compose_interaction_state, application_command_state,
                                  timing_edit.stage_view());
            compose_scene_cache.waveform_overview_total_frames = 0;
            compose_scene_cache.waveform_overview_sample_rate = 0;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            const auto* compose_waveform_analysis =
                waveform_job_asset == workspace.transport.audio_asset
                    ? waveform_analysis.get()
                    : nullptr;
            const bool compose_waveform_overview_available =
                wormedit::sys::populate_compose_waveform_projection(
                    flattened, compose_scene_cache, compose_waveform_analysis,
                    waveform_job.busy(), workspace.transport.audio_asset.has_value(),
                    compose_interaction_state ? &*compose_interaction_state : nullptr);
            wormedit::sys::populate_compose_waveform_overlays(
                compose_waveform_overview_available, flattened, history, workspace,
                compose_scene_cache,
                compose_interaction_state ? &*compose_interaction_state : nullptr,
                audio_offset_drag, audio_offset_drag_tab, compose_waveform_analysis);
#endif
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            wormedit::sys::populate_bmson_waveform_projection(
                flattened, history, workspace,
                compose_interaction_state ? &*compose_interaction_state : nullptr,
                compose_scene_cache, audition_sources, waveform_job_asset,
                waveform_analysis, channel_waveform_job_asset,
                channel_waveform_analysis, audition_source_generation);
#endif
            const auto action = worm_ui_frontend_compose(&flattened.view);
            std::optional<std::uint32_t> selected_waveform_sample_rate;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
            if (waveform_analysis && waveform_analysis->waveform && waveform_job_asset &&
                workspace.transport.audio_asset == waveform_job_asset) {
                selected_waveform_sample_rate = waveform_analysis->waveform->sample_rate;
            }
#endif
            const bool shortcut_handled =
                action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
                dispatch_application_shortcuts(action, command_registry, history, workspace,
                                               compose_interaction_state, application_command_state,
                                               status);
            if (shortcut_handled && compose_interaction_state &&
                editor_interaction_has_pending_gesture(*compose_interaction_state)) {
                (void)wormedit::dispatch_editor_action(history, *compose_interaction_state,
                                                       wormedit::cancel_editor_gesture_action_t{});
            }
            if (shortcut_handled) {
                audio_offset_drag.reset();
                audio_offset_drag_tab.reset();
                compose_scene_cache.beat_fit_drag.reset();
                compose_scene_cache.beat_fit_asset.reset();
            }
            if (!shortcut_handled && action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
                (action.compose_input_count != 0 || action.compose_viewport_input_count != 0 ||
                 action.compose_has_pointer_hover ||
                 (workspace.active_tab == wormedit::workspace_tab_t::compose &&
                  action.compose_input_chart_id != 0)) &&
                compose_interaction_state) {
                status = dispatch_compose_input_action(
                    action, history, workspace, *compose_interaction_state, compose_viewport_input,
                    compose_navigation_input, application_command_state, compose_scene_cache,
                    compose_viewport_delta_seconds);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
                wormedit::sys::process_compose_audition_requests(
                    compose_scene_cache, history, audition_sources, object_audition,
                    preview_session, preview_output, preview_state, audition_output, status);
#else
                wormedit::sys::process_compose_audition_requests(
                    compose_scene_cache, history, status);
#endif
            }
            if (workspace.active_tab != wormedit::workspace_tab_t::compose) {
                compose_viewport_input.escape();
                compose_scene_cache.viewport_autoscroll_active = false;
            }
            if (!shortcut_handled && action.kind != WORM_UI_WORKSPACE_ACTION_QUIT &&
                compose_interaction_state &&
                workspace.active_tab == wormedit::workspace_tab_t::compose) {
                const auto repeated = dispatch_compose_navigation_actions(
                    compose_navigation_input.advance(static_cast<std::uint64_t>(SDL_GetTicks())),
                    history, *compose_interaction_state);
                if (!repeated.empty()) {
                    status = repeated;
                }
            } else if (workspace.active_tab != wormedit::workspace_tab_t::compose) {
                compose_navigation_input.cancel();
            }
            if (action.kind == WORM_UI_WORKSPACE_ACTION_QUIT) {
                running = false;
            } else if (shortcut_handled) {
                // Registry commands take precedence over widget actions emitted
                // by the same input frame, preventing duplicate mutations.
            } else if (action.audio_offset_drag_action != WORM_UI_AUDIO_OFFSET_DRAG_NONE) {
                status = dispatch_audio_offset_drag_action(
                    action, history, workspace, application_command_state.timing_locked,
                    selected_waveform_sample_rate, audio_offset_drag, audio_offset_drag_tab);
            } else if (action.kind == WORM_UI_WORKSPACE_ACTION_TRANSPORT_PLAY_PAUSE) {
                status = application_command_state.transport_play_pause
                             ? application_command_state.transport_play_pause()
                             : "Chart preview is unavailable.";
            } else if (action.kind == WORM_UI_WORKSPACE_ACTION_TRANSPORT_STOP) {
                if (application_command_state.transport_stop) {
                    application_command_state.transport_stop();
                    status = "Audio preview stopped.";
                } else {
                    status = "Audio preview is unavailable.";
                }
            } else if (action.kind == WORM_UI_WORKSPACE_ACTION_TRANSPORT_SEEK) {
                status = application_command_state.transport_seek
                             ? application_command_state.transport_seek(action.transport_seek_frame)
                             : "Chart preview seek is unavailable.";
            } else if (action.kind == WORM_UI_WORKSPACE_ACTION_TOGGLE_PLAYER_PREVIEW) {
                application_command_state.player_preview_visible =
                    !application_command_state.player_preview_visible;
                status = application_command_state.player_preview_visible
                             ? "Player preview shown."
                             : "Player preview hidden.";
            } else if (action.kind == WORM_UI_WORKSPACE_ACTION_SELECT_PLAYER_PREVIEW_SKIN) {
                if (action.player_preview_skin > WORM_UI_PLAYER_PREVIEW_SKIN_ARROWS) {
                    status = "Unknown player-preview skin ignored.";
                } else {
                    application_command_state.player_preview_skin =
                        static_cast<wormedit::player_preview_skin_t>(action.player_preview_skin);
                    status = "Player-preview skin changed.";
                }
            } else if (action.kind ==
                       WORM_UI_WORKSPACE_ACTION_SET_STEPMANIA_EXPORT_POLICY) {
                const auto candidate = stepmania_policy_from_ui(action);
                if (!candidate) {
                    status = "Unknown StepMania export policy ignored.";
                } else {
                    stepmania_export_policy = *candidate;
                    const auto resolved =
                        wormedit::resolve_stepmania_export_policy(stepmania_export_policy);
                    status = resolved ? "StepMania export policy updated."
                                      : "StepMania policy needs attention: " + resolved.message;
                }
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
            } else if (action.metadata_edit != WORM_UI_METADATA_EDIT_NONE) {
                status = dispatch_metadata_ui_action(action, history, workspace,
                                                     application_command_state.timing_locked);
                if (action.metadata_edit == WORM_UI_METADATA_EDIT_OUTPUT_PATH)
                    metadata_drop.clear_output();
            } else if (action.metadata_import_copy_project || action.metadata_import_copy_export) {
                status = metadata_asset_runtime.start_import_copy(
                    action, history, workspace, asset_base, resource_copy_scheduler,
                    resource_copy_runtime.busy(), export_runtime.busy());
                if (metadata_asset_runtime.import_pending()) {
                    metadata_drop.clear_music();
                }
            } else if (action.metadata_inspect_copy) {
                status = metadata_asset_runtime.start_copy_preview(
                    action, history, workspace, asset_base);
            } else if (action.timing_meter_action != WORM_UI_TIMING_METER_ACTION_NONE) {
                dispatch_timing_meter_action(
                    action, history, workspace, application_command_state.timing_locked,
                    timing_edit.has_pending_edit(),
                    status);
            } else if (action.timing_grid_action != WORM_UI_TIMING_GRID_ACTION_NONE) {
                dispatch_timing_grid_action(
                    action, history, workspace,
                    timing_edit.has_pending_edit(),
                    timing_grid, application_command_state, status);
            } else if (action.timing_recipe_action.kind != WORM_UI_TIMING_RECIPE_ACTION_NONE) {
                const bool starting_recipe =
                    action.timing_recipe_action.kind ==
                        WORM_UI_TIMING_RECIPE_ACTION_ARM_SELECTION ||
                    action.timing_recipe_action.kind == WORM_UI_TIMING_RECIPE_ACTION_BEGIN;
                if (starting_recipe && timing_edit.stage) {
                    status = "Apply or cancel the timing event before adding an SV recipe.";
                } else {
                    (void)dispatch_timing_recipe_action(
                        action.timing_recipe_action, history, workspace,
                        application_command_state.timing_locked, timing_grid.active_denominator,
                        timing_edit, status);
                }
            } else if (action.timing_stage_action != WORM_UI_TIMING_STAGE_NONE) {
                dispatch_timing_stage_action(
                    action, history, workspace, application_command_state.timing_locked,
                    timing_grid.active_denominator, timing_edit, status);
            } else if (action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BMS_BRANCH_CHOICE ||
                       action.compose_action == WORM_UI_COMPOSE_ACTION_CLEAR_BMS_BRANCH_CHOICE) {
                status = dispatch_bms_branch_choice_action(action, history, workspace,
                                                           compose_interaction_state);
            } else if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) {
                const bool metadata_path_action =
                    workspace.active_tab == wormedit::workspace_tab_t::metadata &&
                    (action.compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET ||
                     action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT ||
                     action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT);
                wormedit::editor_interaction_state_t metadata_interaction{};
                if (metadata_path_action && workspace.selected_chart)
                    metadata_interaction.chart = *workspace.selected_chart;
                if (compose_interaction_state || metadata_path_action) {
                    if (metadata_path_action)
                        metadata_asset_runtime.clear_copy_preview_key();
                    status = dispatch_compose_view_action(
                        action, history, workspace,
                        metadata_path_action ? metadata_interaction : *compose_interaction_state,
                        flattened.view.compose.viewport_height_pixels, application_command_state,
                        asset_base, resource_copy_scheduler, resource_copy_runtime, export_runtime,
                        metadata_asset_runtime.import_pending());
                    if (workspace.active_tab == wormedit::workspace_tab_t::metadata &&
                        (action.compose_action == WORM_UI_COMPOSE_ACTION_IMPORT_RESOURCE_ASSET ||
                         action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_PROJECT ||
                         action.compose_action == WORM_UI_COMPOSE_ACTION_COPY_RESOURCE_TO_EXPORT)) {
                        metadata_drop.clear_music();
                    }
                }
            } else if (action.kind >= WORM_UI_WORKSPACE_ACTION_RECOVERY_INSPECT &&
                       action.kind <= WORM_UI_WORKSPACE_ACTION_RECOVERY_QUARANTINE) {
                if (autosave_loop.busy()) {
                    status = "Wait for the current autosave to finish before recovering a session.";
                } else {
                    status =
                        dispatch_recovery_ui_action(action, recovery_controller, history, workspace,
                                                    scanned_primary_version, autosave_loop);
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
                    if (const auto unavailable = reconcile_audition_sources(true);
                        unavailable && *unavailable != 0) {
                        status += " " + std::to_string(*unavailable) +
                                  " recovered audio asset(s) are unavailable for preview.";
                    }
#endif
                }
            }
        }

        int width = 0;
        int height = 0;
        worm_window_get_size(&window, &width, &height);
        worm_ui_frontend_render(width, height);
        worm_window_swap(&window);
        SDL_Delay(1);
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
    worm_ui_frontend_shutdown();
    worm_window_destroy(&window);
    worm_audio_shutdown(&audio);
    SDL_Quit();
    return 0;
}

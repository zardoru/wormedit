#include <wormedit/core/timing_lane_catalog.h>
#include "sys/application_timing_stage.h"
#include "sys/application_edit_inputs.h"
#include "core/timing_event_internal.h"
#include "sys/timing/timing_edit_runtime.h"
#include "sys/timing/timing_view.h"
#if defined(WORMEDIT_HAS_MACOS_MENU)
#include "sys/macos_menu.h"
#endif

#include "sys/application_defaults.h"
#include "sys/compose/compose_timing_target.h"
#include "sys/timing/timing_view.h"
#include "ui/frontend.h"
#include <SDL3/SDL_clipboard.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/numeric_text.h>

#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/workspace.h>

#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
#endif

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include "sys/compose/compose_playhead_cursor.h"
#include "sys/audio/sdl_preview_output.h"

#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace wormedit::sys {
auto timing_position_from_fraction(const wormedit::chart_document_t& chart,
                                   const wormedit::chart_indices_t& indices, double fraction,
                                   std::uint64_t active_denominator)
    -> std::optional<wormedit::local_position_t> {
    if (!std::isfinite(fraction) || fraction < 0.0 || fraction >= 1.0) {
        return std::nullopt;
    }
    const auto* measures = indices.measures ? &*indices.measures : nullptr;
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
                                                       {.denominator = usable_denominator},
                                                       measures->total_rows)
                                 .value_or(raw_row);
    return measures->position_at(snapped_row);
}

auto make_initial_timing_value_for_stage(const wormedit::chart_document_t& chart,
                                    const wormedit::chart_indices_t& indices,
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
        const auto* measures = indices.measures ? &*indices.measures : nullptr;
        const auto step = timing_stage_grid_step(chart, active_snap_denominator);
        const auto start = measures ? (*measures).row_of(position) : std::nullopt;
        if (!measures || !start || step == 0 ||
            step > std::numeric_limits<std::uint64_t>::max() - *start ||
            *start + step >= measures->total_rows) {
            return std::nullopt;
        }
        const auto end = measures->position_at(*start + step);
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

auto begin_timing_stage(const wormedit::ui::workspace_action_t& action,
                        const wormedit::document_history_t& history,
                        const wormedit::workspace_controller_t& workspace, bool timing_locked,
                        std::uint64_t active_snap_denominator)
    -> wormedit::timing_lane_pseudonote_outcome_t {
    using namespace wormedit;
    timing_lane_pseudonote_outcome_t rejected{};
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart ||
        action.timing_chart_id.value == 0 || workspace.selected_chart->value != action.timing_chart_id.value ||
        history.document.revision != action.timing_document_revision) {
        return rejected;
    }
    const auto requested_kind = wormedit::find_catalog_entry(wormedit::timing_lanes, action.timing_lane);
    if (!requested_kind) {
        return rejected;
    }
    if (action.timing_object_id.value != 0) {
        auto preview = timing_lane_pseudonote_preview_t::propose(history, action.timing_chart_id, wormedit::timing_lane_event_ref_t{requested_kind->kind, action.timing_object_id}, timing_locked);
        if (preview.report()->lifecycle.status() == proposal_status_t::active &&
            action.timing_has_target_y) {
            const auto chart =
                history.document.find_chart(preview.captured_stamp().chart);
            const auto target =
                chart == nullptr
                    ? std::optional<local_position_t>{}
                    : timing_position_from_fraction(
                          *chart, *history.indices(chart->id),
                          wormedit::sys::timing_target_fraction(action.timing_target),
                          active_snap_denominator);
            if (!target ||
                !(preview = timing_lane_pseudonote_preview_t::revise(std::move(preview), history, *target, timing_locked)).value.has_value()) {
                return rejected;
            }
        }
        return preview;
    }
    if (!action.timing_has_target_y) {
        return rejected;
    }
    const auto found = history.document.find_chart(action.timing_chart_id);
    if (found == nullptr) {
        return rejected;
    }
    const auto position = timing_position_from_fraction(
        *found, *history.indices(found->id),
        wormedit::sys::timing_target_fraction(action.timing_target),
        active_snap_denominator);
    if (!position) {
        return rejected;
    }

    const auto kind = requested_kind->kind;
    auto initial = make_initial_timing_value_for_stage(
        *found, *history.indices(found->id), kind, *position, active_snap_denominator);
    if (!initial) {
        return rejected;
    }
    return timing_lane_pseudonote_preview_t::propose(history, action.timing_chart_id, wormedit::timing_lane_pseudonote_state_t{.kind = kind, .position = *position, .value = std::move(*initial), .initial_value_source = timing_lane_initial_value_source_t::explicit_default}, timing_locked);
}

auto parse_timing_unsigned(std::string_view text) -> std::optional<std::uint64_t> {
    if (text.empty()) {
        return std::nullopt;
    }
    const auto* end = text.data() + text.size();
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), end, value);
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
    const wormedit::timing_lane_pseudonote_state_t& preview;
    const wormedit::ui::workspace_action_t& action;
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

auto timing_pause_maximum_duration(wormedit::pause_unit_t unit) -> long double {
    switch (unit) {
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
    const auto unit = input.action.timing_pause_unit;
    const auto maximum = timing_pause_maximum_duration(unit);
    if (!input.primary_numerator || !input.primary_denominator || *input.primary_numerator < 0 ||
        !wormedit::is_valid_pause_unit(unit) ||
        (!input.preview.existing_event &&
         !timing_unsigned_ratio_in_bounds(static_cast<std::uint64_t>(*input.primary_numerator),
                                          *input.primary_denominator, maximum))) {
        return std::nullopt;
    }
    return timing_lane_value_t{
        timing_pause_value_t{{*input.primary_numerator, *input.primary_denominator}, unit}};
}

auto timing_stage_warp_value(const TimingStageInput& input, const wormedit::chart_document_t& chart,
                             const wormedit::chart_indices_t& indices,
                             std::uint64_t active_snap_denominator)
    -> std::optional<wormedit::timing_lane_value_t> {
    using namespace wormedit;
    if (!input.secondary_numerator || *input.secondary_numerator < 1 ||
        !input.secondary_denominator || *input.secondary_denominator != 1 ||
        (!input.preview.existing_event && *input.secondary_numerator > 1'000'000)) {
        return std::nullopt;
    }
    const auto units = static_cast<std::uint64_t>(*input.secondary_numerator);
    const auto* index = indices.measures ? &*indices.measures : nullptr;
    const auto start = index ? (*index).row_of(input.preview.position) : std::nullopt;
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
    const auto end = index->position_at(end_row);
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

auto timing_stage_value(const wormedit::timing_lane_pseudonote_state_t& preview,
                        const wormedit::chart_document_t& chart,
                        const wormedit::chart_indices_t& indices,
                        const wormedit::ui::workspace_action_t& action, std::uint64_t active_snap_denominator)
    -> std::optional<wormedit::timing_lane_value_t> {
    using namespace wormedit;
    const TimingStageInput input{
        preview,
        action,
        parse_integer<std::int64_t>(action.timing_primary_numerator_text.c_str()),
        parse_timing_unsigned(action.timing_primary_numerator_text),
        parse_timing_unsigned(action.timing_primary_denominator_text),
        parse_integer<std::int64_t>(action.timing_secondary_numerator_text.c_str()),
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
        return timing_stage_warp_value(input, chart, indices, active_snap_denominator);
    case timing_lane_kind_t::kiai:
        return timing_stage_toggle_value(input);
    case timing_lane_kind_t::anchor:
        return timing_stage_anchor_value(input);
    }
    return std::nullopt;
}

} // namespace wormedit::sys

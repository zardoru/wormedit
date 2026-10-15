#include <wormedit/core/rational.h>
#include "sys/timing_view.h"
#include "sys/recipe_form.h"

#include <wormedit/core/grid.h>
#include <wormedit/core/measure.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/bpm_editor.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ranges>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace wormedit::sys {
namespace {

template<class T>
auto allocate_timing_rows(flattened_workspace_t& flattened, frame_span<T>& rows,
                          std::size_t count, std::size_t limit) -> bool {
    auto* arena = flattened.arena();
    rows = arena ? arena_array<T>(*arena, std::min(count, limit)) : frame_span<T>{};
    if (count > limit || rows.capacity() != std::min(count, limit)) {
        flattened.view.timing.truncated = true;
        return false;
    }
    return true;
}

auto timing_display_text(flattened_workspace_t& flattened, std::string_view text)
    -> const char* {
    auto* arena = flattened.arena();
    if (const auto* owned = arena ? arena_string(*arena, text) : nullptr) return owned;
    flattened.view.timing.truncated = true;
    return "Timing display text truncated (frame storage limit).";
}

auto compose_surface_height(int window_height, const WormUiWorkspaceView& view) -> double {
    float recovery_height = 0.0f;
    if (view.recovery_panel_visible) {
        recovery_height = 40.0f + (view.recovery_notice && view.recovery_notice[0] ? 22.0f : 0.0f) +
                          static_cast<float>(view.recovery_candidate_count) * 48.0f +
                          static_cast<float>(view.recovery_diagnostic_count) * 18.0f;
        for (std::size_t row = 0; row < view.recovery_candidate_count; ++row) {
            const auto& candidate = view.recovery_candidates[row];
            if ((candidate.action_mask & WORM_UI_RECOVERY_ACTION_SAVE_AS) != 0) {
                recovery_height += 25.0f;
                if (candidate.path_label == nullptr || candidate.path_label[0] == '\0')
                    recovery_height += 18.0f;
            }
            if (candidate.disabled_reason && candidate.disabled_reason[0])
                recovery_height += 20.0f;
        }
    }
    const auto content_height =
        window_height > 300 ? static_cast<float>(window_height) - 165.0f -
                                  (view.compose.bgm_bar_reserved ? WORM_UI_BGM_BAR_CHROME_HEIGHT : 0.0f) -
                                  recovery_height : 80.0f;
    const auto& compose = view.compose;
    const auto rows_shown = std::min<std::size_t>(compose.branch_row_count, 4);
    const auto diagnostics_shown = std::min<std::size_t>(compose.branch_diagnostic_count, 2);
    const bool branch_status_shown =
        compose.branch_rows_truncated || compose.branch_display_text_truncated ||
        compose.branch_diagnostics_truncated || compose.branch_row_count > rows_shown ||
        compose.branch_diagnostic_count > diagnostics_shown;
    const bool branch_visible = compose.branch_outline_available || compose.branch_row_count != 0 ||
                                compose.branch_diagnostic_count != 0 || branch_status_shown;
    const auto branch_height = branch_visible
                                   ? 23.0f + static_cast<float>(rows_shown + diagnostics_shown +
                                                                (branch_status_shown ? 1 : 0)) *
                                                 18.0f
                                   : 0.0f;
    return std::max(64.0, static_cast<double>(content_height - 38.0f - 18.0f - branch_height));
}



auto timing_stage_grid_step(const wormedit::chart_document_t& chart,
                            std::uint64_t active_denominator) -> std::uint64_t {
    // A snap the chart's resolution cannot represent falls back to one standard measure.
    if (active_denominator == 0 || chart.divisions_per_standard_measure % active_denominator != 0) {
        return chart.divisions_per_standard_measure;
    }
    return chart.divisions_per_standard_measure / active_denominator;
}

auto timing_lane_kind_to_ui(wormedit::timing_lane_kind_t lane) -> WormUiTimingLaneKind {
    using kind_t = wormedit::timing_lane_kind_t;
    switch (lane) {
    case kind_t::bpm:
        return WORM_UI_TIMING_LANE_BPM;
    case kind_t::scroll:
        return WORM_UI_TIMING_LANE_SCROLL;
    case kind_t::speed:
        return WORM_UI_TIMING_LANE_SPEED;
    case kind_t::stop:
        return WORM_UI_TIMING_LANE_STOP;
    case kind_t::delay:
        return WORM_UI_TIMING_LANE_DELAY;
    case kind_t::warp:
        return WORM_UI_TIMING_LANE_WARP;
    case kind_t::kiai:
        return WORM_UI_TIMING_LANE_KIAI;
    case kind_t::anchor:
        return WORM_UI_TIMING_LANE_ANCHOR;
    }
    return WORM_UI_TIMING_LANE_BPM;
}

auto timing_pause_unit_to_ui(wormedit::pause_unit_t unit) -> WormUiTimingPauseUnit {
    using unit_t = wormedit::pause_unit_t;
    switch (unit) {
    case unit_t::seconds:
        return WORM_UI_TIMING_PAUSE_SECONDS;
    case unit_t::milliseconds:
        return WORM_UI_TIMING_PAUSE_MILLISECONDS;
    case unit_t::bms_stop_ticks:
        return WORM_UI_TIMING_PAUSE_BMS_STOP_TICKS;
    case unit_t::beats:
        return WORM_UI_TIMING_PAUSE_BEATS;
    }
    return WORM_UI_TIMING_PAUSE_SECONDS;
}

struct timing_stage_value_context_t {
    WormUiTimingView& view;
    const wormedit::chart_document_t& chart;
    const wormedit::measure_index_t* measures;
    const wormedit::timing_lane_pseudonote_state_t& stage;
    const timing_grid_state_t& timing_grid;
    std::optional<std::uint64_t> stage_row;
};

template <typename Value>
void build_timing_stage_value_view(timing_stage_value_context_t& context, const Value& value) {
    auto& view = context.view;
    const auto set_primary = [&](std::int64_t numerator, std::uint64_t denominator) {
        (void)std::snprintf(view.stage_primary_numerator_text,
                            sizeof(view.stage_primary_numerator_text), "%lld",
                            static_cast<long long>(numerator));
        (void)std::snprintf(view.stage_primary_denominator_text,
                            sizeof(view.stage_primary_denominator_text), "%llu",
                            static_cast<unsigned long long>(denominator));
    };
    const auto set_secondary = [&](std::uint64_t numerator, std::uint64_t denominator) {
        (void)std::snprintf(view.stage_secondary_numerator_text,
                            sizeof(view.stage_secondary_numerator_text), "%llu",
                            static_cast<unsigned long long>(numerator));
        (void)std::snprintf(view.stage_secondary_denominator_text,
                            sizeof(view.stage_secondary_denominator_text), "%llu",
                            static_cast<unsigned long long>(denominator));
    };
    if constexpr (std::is_same_v<Value, wormedit::timing_bpm_value_t>) {
        view.stage_value = static_cast<double>(wormedit::to_long_double(value.beats_per_minute));
        set_primary(value.beats_per_minute.numerator, value.beats_per_minute.denominator);
        if (const auto formatted = wormedit::format_bpm_text(value.beats_per_minute)) {
            (void)std::snprintf(view.stage_primary_decimal_text,
                                sizeof(view.stage_primary_decimal_text), "%s",
                                formatted->decimal.c_str());
            view.stage_primary_decimal_exact = formatted->decimal_exact;
        }
    } else if constexpr (std::is_same_v<Value, wormedit::timing_scroll_value_t>) {
        view.stage_value = static_cast<double>(wormedit::to_long_double(value.multiplier));
        set_primary(value.multiplier.numerator, value.multiplier.denominator);
    } else if constexpr (std::is_same_v<Value, wormedit::timing_speed_value_t>) {
        view.stage_value = static_cast<double>(wormedit::to_long_double(value.multiplier));
        view.stage_secondary_value = wormedit::to_double(value.transition_beats);
        view.stage_transition_is_seconds = value.transition_is_seconds;
        set_primary(value.multiplier.numerator, value.multiplier.denominator);
        set_secondary(value.transition_beats.numerator, value.transition_beats.denominator);
    } else if constexpr (std::is_same_v<Value, wormedit::timing_pause_value_t>) {
        view.stage_value = static_cast<double>(wormedit::to_long_double(value.duration));
        view.stage_pause_unit = timing_pause_unit_to_ui(value.source_unit);
        set_primary(value.duration.numerator, value.duration.denominator);
    } else if constexpr (std::is_same_v<Value, wormedit::timing_warp_value_t>) {
        const auto end_row = context.measures
                                 ? (*context.measures).row_of(value.end)
                                 : std::nullopt;
        const auto step = context.stage.existing_event.has_value()
                              ? std::uint64_t{1}
                              : timing_stage_grid_step(context.chart,
                                                       context.timing_grid.active_denominator);
        const auto length_steps = end_row && context.stage_row &&
                                          *end_row > *context.stage_row
                                      ? static_cast<long double>(*end_row - *context.stage_row) /
                                            static_cast<long double>(step)
                                      : 1.0L;
        view.stage_secondary_value = static_cast<double>(length_steps);
        (void)std::snprintf(view.stage_secondary_numerator_text,
                            sizeof(view.stage_secondary_numerator_text), "%.0Lf", length_steps);
        (void)std::snprintf(view.stage_secondary_denominator_text,
                            sizeof(view.stage_secondary_denominator_text), "1");
    } else if constexpr (std::is_same_v<Value, wormedit::timing_kiai_value_t>) {
        view.stage_toggle_primary = value.enabled;
    } else if constexpr (std::is_same_v<Value, wormedit::timing_anchor_value_t>) {
        view.stage_toggle_primary = value.locked;
        (void)std::snprintf(view.stage_primary_numerator_text,
                            sizeof(view.stage_primary_numerator_text), "%llu",
                            static_cast<unsigned long long>(value.source_sample_frame));
        (void)std::snprintf(view.stage_primary_denominator_text,
                            sizeof(view.stage_primary_denominator_text), "1");
    }
}

template <typename RowToPixels>
void populate_timing_stage_view(
    flattened_workspace_t& flattened, WormUiTimingView& view,
    const wormedit::chart_document_t& chart, const wormedit::measure_index_t* measures,
    const wormedit::timing_lane_pseudonote_outcome_t* stage,
    const timing_grid_state_t& timing_grid, std::uint64_t revision, RowToPixels row_to_pixels) {
    const auto* report = stage ? wormedit::timing_lane_pseudonote_proposal_t::report(*stage) : nullptr;
    if (report && report->status == wormedit::timing_lane_pseudonote_status_t::editing &&
        wormedit::timing_lane_pseudonote_proposal_t::captured_stamp(*stage).is_current(chart.id, revision)) {
        view.stage_active = true;
        view.stage_valid = stage->value.has_value();
        view.stage_existing_object_id =
            report->existing_event.has_value() ? report->existing_event->value : 0;
        view.stage_lane = timing_lane_kind_to_ui(report->kind);
        const auto stage_row =
            measures ? (*measures).row_of(report->position) : std::nullopt;
        view.stage_marker_y_pixels =
            stage_row ? row_to_pixels(static_cast<long double>(*stage_row)) : 0.0;
        view.stage_transition_is_seconds = false;
        view.stage_toggle_primary = false;
        view.stage_pause_unit = WORM_UI_TIMING_PAUSE_SECONDS;
        timing_stage_value_context_t value_context{view, chart, measures, *report, timing_grid,
                                                   stage_row};
        std::visit(
            [&](const auto& value) {
                build_timing_stage_value_view(value_context, value);
            },
            report->value);
        if (!stage->diagnostics.empty()) {
            view.stage_status_text = timing_display_text(flattened,
                stage->diagnostics.front().message);
        }
    }
}

auto compose_total_rows(timing_scene_cache_t& cache, const wormedit::chart_document_t& chart,
                        std::uint64_t revision) -> std::uint64_t {
    if (cache.chart != chart.id || cache.revision != revision)
        cache = {.chart = chart.id, .revision = revision};
    if (cache.compose_rows_attempted) return cache.compose_total_rows;
    cache.compose_rows_attempted = true;
    for (const auto& measure : chart.measures) {
        const auto valid = wormedit::validate_measure(chart.divisions_per_standard_measure,
                                                     measure.definition);
        if (!valid || *valid.row_capacity > std::numeric_limits<std::uint64_t>::max() - cache.compose_total_rows) {
            cache.compose_total_rows = 0;
            return 0;
        }
        cache.compose_total_rows += *valid.row_capacity;
    }
    return cache.compose_total_rows;
}

auto cached_measures(timing_scene_cache_t& cache, const wormedit::chart_document_t& chart,
                     std::uint64_t revision) -> const wormedit::measure_index_t* {
    if (cache.chart != chart.id || cache.revision != revision)
        cache = {.chart = chart.id, .revision = revision};
    if (!cache.measure_attempted) {
        cache.measure_attempted = true;
        auto result = wormedit::build_measure_index(chart);
        if (auto* measures = std::get_if<wormedit::measure_index_t>(&result))
            cache.measures = std::move(*measures);
    }
    return cache.measures ? &*cache.measures : nullptr;
}

void populate_initial_bpm(WormUiTimingView& view, const wormedit::chart_document_t& chart) {
    if (!chart.measures.empty()) {
        for (const auto& bpm : chart.bpms) {
            if (bpm.position.measure == chart.measures.front().id && bpm.position.row == 0) {
                view.initial_bpm_present = true;
                view.initial_bpm_event_id = bpm.id.value;
                view.initial_bpm_value =
                    static_cast<double>(to_long_double(bpm.beats_per_minute));
            }
        }
    }
}

} // namespace

void populate_timing_view_for_compose(
    flattened_workspace_t& flattened, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace, timing_scene_cache_t& cache,
    int window_width, int window_height,
    const timing_grid_state_t& timing_grid,
    const wormedit::timing_lane_pseudonote_outcome_t* stage) {
    if (!workspace.selected_chart) return;
    const auto selected = history.document.find_chart(*workspace.selected_chart);
    if (selected == nullptr) return;
    const auto& chart = *selected;
    auto& view = flattened.view.timing;
    view.chart_id = chart.id.value;
    view.document_revision = history.document.revision;
    view.viewport_width_pixels = std::max(160.0, static_cast<double>(window_width) - 48.0);
    view.viewport_height_pixels = compose_surface_height(window_height, flattened.view);
    view.total_rows = compose_total_rows(cache, chart, history.document.revision);
    view.scene_available = view.total_rows != 0;
    populate_initial_bpm(view, chart);
    view.active_snap_denominator = timing_grid.active_denominator;
    view.supported_snap_denominators = timing_grid.supported_denominators.data();
    view.supported_snap_denominator_count = timing_grid.supported_denominators.size();
    const auto* report = stage ? wormedit::timing_lane_pseudonote_proposal_t::report(*stage) : nullptr;
    const bool editing_stage = report && report->status == wormedit::timing_lane_pseudonote_status_t::editing;
    if (!editing_stage) return;
    const auto* measures = cached_measures(cache, chart, history.document.revision);
    const auto row_to_pixels = [&](long double row) {
        return measures && measures->total_rows > 0
            ? static_cast<double>(row / measures->total_rows * view.viewport_height_pixels) : 0.0;
    };
    populate_timing_stage_view(flattened, view, chart, measures, stage, timing_grid,
                              history.document.revision, row_to_pixels);
}

} // namespace wormedit::sys

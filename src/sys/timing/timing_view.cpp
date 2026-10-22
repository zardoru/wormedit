#include <wormedit/core/timing_lane_catalog.h>
#include <wormedit/core/rational.h>
#include "sys/timing/timing_view.h"
#include "sys/compose/views/recipe_form.h"

#include <wormedit/core/grid.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/measure.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/bpm_editor.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <format>
#include <iterator>
#include <ranges>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace wormedit::sys {
namespace {

auto compose_surface_height(int window_height, const wormedit::ui::workspace_view_t& view) -> double {
    const auto recovery_height = view.recovery_panel_visible ? view.recovery_panel_height() : 0.0f;
    const auto content_height =
        window_height > 300 ? static_cast<float>(window_height) - 165.0f -
                                  (view.compose.bgm_bar_reserved ? WORM_UI_BGM_BAR_CHROME_HEIGHT : 0.0f) -
                                  recovery_height : 80.0f;
    const auto& compose = view.compose;
    const auto rows_shown = std::min<std::size_t>(compose.branch_rows.size(), 4);
    const auto diagnostics_shown = std::min<std::size_t>(compose.branch_diagnostic_count(), 2);
    const bool branch_status_shown =
        compose.branch_rows_truncated || compose.branch_display_text_truncated ||
        compose.branch_diagnostics_truncated || compose.branch_rows.size() > rows_shown ||
        compose.branch_diagnostic_count() > diagnostics_shown;
    const bool branch_visible = compose.branch_outline_available || compose.branch_rows.size() != 0 ||
                                compose.branch_diagnostic_count() != 0 || branch_status_shown;
    const auto branch_height = branch_visible
                                   ? 23.0f + static_cast<float>(rows_shown + diagnostics_shown +
                                                                (branch_status_shown ? 1 : 0)) *
                                                 18.0f
                                   : 0.0f;
    return std::max(64.0, static_cast<double>(content_height - 38.0f - 18.0f - branch_height));
}





struct timing_stage_value_context_t {
    wormedit::ui::timing_view_t& view;
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
        view.stage_primary_numerator_text.clear();
        std::format_to(std::back_inserter(view.stage_primary_numerator_text), "{}", numerator);
        view.stage_primary_denominator_text.clear();
        std::format_to(std::back_inserter(view.stage_primary_denominator_text), "{}", denominator);
    };
    const auto set_secondary = [&](std::uint64_t numerator, std::uint64_t denominator) {
        view.stage_secondary_numerator_text.clear();
        std::format_to(std::back_inserter(view.stage_secondary_numerator_text), "{}", numerator);
        view.stage_secondary_denominator_text.clear();
        std::format_to(std::back_inserter(view.stage_secondary_denominator_text), "{}", denominator);
    };
    if constexpr (std::is_same_v<Value, wormedit::timing_bpm_value_t>) {
        view.stage_value = static_cast<double>(wormedit::to_long_double(value.beats_per_minute));
        set_primary(value.beats_per_minute.numerator, value.beats_per_minute.denominator);
        if (const auto formatted = wormedit::format_bpm_text(value.beats_per_minute)) {
            view.stage_primary_decimal_text = formatted->decimal;
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
        view.stage_pause_unit = value.source_unit;
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
        view.stage_secondary_numerator_text.clear();
        std::format_to(std::back_inserter(view.stage_secondary_numerator_text), "{:.0f}", length_steps);
        view.stage_secondary_denominator_text = "1";
    } else if constexpr (std::is_same_v<Value, wormedit::timing_kiai_value_t>) {
        view.stage_toggle_primary = value.enabled;
    } else if constexpr (std::is_same_v<Value, wormedit::timing_anchor_value_t>) {
        view.stage_toggle_primary = value.locked;
        view.stage_primary_numerator_text.clear();
        std::format_to(std::back_inserter(view.stage_primary_numerator_text), "{}", value.source_sample_frame);
        view.stage_primary_denominator_text = "1";
    }
}

template <typename RowToPixels>
void populate_timing_stage_view(
    wormedit::ui::timing_view_t& view,
    const wormedit::chart_document_t& chart, const wormedit::measure_index_t* measures,
    const wormedit::timing_lane_pseudonote_outcome_t* stage,
    const timing_grid_state_t& timing_grid, std::uint64_t revision, RowToPixels row_to_pixels) {
    const auto* report = stage ? stage->report() : nullptr;
    if (report && report->lifecycle.status() == wormedit::proposal_status_t::active &&
        stage->captured_stamp().is_current(chart.id, revision)) {
        view.stage_active = true;
        view.stage_valid = stage->value.has_value();
        view.stage_existing_object_id =
            report->existing_event.value_or(wormedit::object_id_t{});
        view.stage_lane = report->kind;
        const auto stage_row =
            measures ? (*measures).row_of(report->position) : std::nullopt;
        view.stage_marker_y_pixels =
            stage_row ? row_to_pixels(static_cast<long double>(*stage_row)) : 0.0;
        view.stage_transition_is_seconds = false;
        view.stage_toggle_primary = false;
        view.stage_pause_unit = wormedit::pause_unit_t::seconds;
        timing_stage_value_context_t value_context{view, chart, measures, *report, timing_grid,
                                                   stage_row};
        std::visit(
            [&](const auto& value) {
                build_timing_stage_value_view(value_context, value);
            },
            report->value);
        if (!stage->diagnostics.empty()) {
            view.stage_status_text = stage->diagnostics.front().message;
        }
    }
}

auto build_compose_total_rows(const wormedit::chart_document_t& chart) -> std::uint64_t {
    std::uint64_t total_rows{};
    for (const auto& measure : chart.measures) {
        const auto valid = wormedit::measure_capacity_t::from(chart.divisions_per_standard_measure,
                                                             measure.definition);
        if (!valid || valid->row_capacity > std::numeric_limits<std::uint64_t>::max() - total_rows)
            return 0;
        total_rows += valid->row_capacity;
    }
    return total_rows;
}

void populate_initial_bpm(wormedit::ui::timing_view_t& view, const wormedit::chart_document_t& chart) {
    if (!chart.measures.empty()) {
        for (const auto& bpm : chart.bpms) {
            if (bpm.position.measure == chart.measures.front().id && bpm.position.row == 0) {
                view.initial_bpm_present = true;
                view.initial_bpm_event_id = bpm.id;
                view.initial_bpm_value =
                    static_cast<double>(to_long_double(bpm.beats_per_minute));
            }
        }
    }
}

} // namespace

auto timing_stage_grid_step(const wormedit::chart_document_t& chart,
                            std::uint64_t active_denominator) -> std::uint64_t {
    return wormedit::snap_step(chart, {.denominator = active_denominator}, 0)
        .value_or(chart.divisions_per_standard_measure);
}

void populate_timing_view_for_compose(
    frontend_workspace_view_t& flattened, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace, timing_scene_cache_t& cache,
    int window_width, int window_height,
    const timing_grid_state_t& timing_grid,
    const wormedit::timing_lane_pseudonote_outcome_t* stage) {
    if (!workspace.selected_chart) return;
    const auto selected = history.document.find_chart(*workspace.selected_chart);
    if (selected == nullptr) return;
    const auto& chart = *selected;
    auto& view = flattened.view.timing;
    view.chart_id = chart.id;
    view.document_revision = history.document.revision;
    view.viewport_width_pixels = std::max(160.0, static_cast<double>(window_width) - 48.0);
    view.viewport_height_pixels = compose_surface_height(window_height, flattened.view);
    view.total_rows = cache.get_or_build(std::pair{chart.id, history.document.revision},
        std::bind_front(build_compose_total_rows, std::cref(chart)));
    view.scene_available = view.total_rows != 0;
    populate_initial_bpm(view, chart);
    view.active_snap_denominator = timing_grid.active_denominator;
    view.supported_snap_denominators.assign(timing_grid.supported_denominators.begin(),
                                            timing_grid.supported_denominators.end());
    const auto* report = stage ? stage->report() : nullptr;
    const bool editing_stage = report && report->lifecycle.status() == wormedit::proposal_status_t::active;
    if (!editing_stage) return;
    const auto& indexed = history.indices(chart.id)->measures;
    const auto* measures = indexed ? &*indexed : nullptr;
    const auto row_to_pixels = [&](long double row) {
        return measures && measures->total_rows > 0
            ? static_cast<double>(row / measures->total_rows * view.viewport_height_pixels) : 0.0;
    };
    populate_timing_stage_view(view, chart, measures, stage, timing_grid,
                              history.document.revision, row_to_pixels);
}

} // namespace wormedit::sys

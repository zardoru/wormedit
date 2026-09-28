#include "sys/timing_view.h"

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
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace wormedit::sys {
namespace {

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
        window_height > 300 ? static_cast<float>(window_height) - 165.0f - recovery_height : 80.0f;
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

auto rational_as_long_double(wormedit::signed_rational_t value) -> long double {
    if (value.denominator == 0) {
        return 0.0L;
    }
    return static_cast<long double>(value.numerator) / static_cast<long double>(value.denominator);
}

auto rational_as_double(wormedit::rational_t value) -> double {
    if (value.denominator == 0) {
        return 0.0;
    }
    return static_cast<double>(value.numerator) / static_cast<double>(value.denominator);
}

auto timing_stage_grid_step(const wormedit::chart_document_t& chart,
                            std::uint64_t active_denominator) -> std::uint64_t {
    if (active_denominator == 0 || chart.divisions_per_standard_measure % active_denominator != 0) {
        return 0;
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

void set_recipe_rational_text(WormUiTimingRecipeRationalText& target,
                              wormedit::signed_rational_t value) {
    (void)std::snprintf(target.numerator, sizeof(target.numerator), "%lld",
                        static_cast<long long>(value.numerator));
    (void)std::snprintf(target.denominator, sizeof(target.denominator), "%llu",
                        static_cast<unsigned long long>(value.denominator));
}

void set_recipe_rational_text(WormUiTimingRecipeRationalText& target, wormedit::rational_t value) {
    (void)std::snprintf(target.numerator, sizeof(target.numerator), "%llu",
                        static_cast<unsigned long long>(value.numerator));
    (void)std::snprintf(target.denominator, sizeof(target.denominator), "%llu",
                        static_cast<unsigned long long>(value.denominator));
}

void populate_timing_recipe_view(flattened_workspace_t& flattened,
                                 const wormedit::chart_document_t& chart,
                                 const wormedit::measure_index_t* measures,
                                 const wormedit::sv_recipe_draft_t* draft,
                                 const wormedit::sv_recipe_draft_preview_t* preview,
                                 bool selection_armed) {
    auto& view = flattened.view.timing.sv_recipe;
    view.selection_armed = selection_armed;
    view.chart_id = chart.id.value;
    view.document_revision = flattened.view.timing.document_revision;
    if (!draft || draft->status != wormedit::sv_recipe_draft_status_t::editing || !measures ||
        measures->total_rows == 0) {
        return;
    }
    view.active = true;
    view.chart_id = draft->chart.value;
    view.document_revision = draft->document_revision;
    view.recipe_id = draft->existing_recipe ? draft->existing_recipe->value : 0;
    const auto begin_row = wormedit::absolute_row(*measures, draft->candidate.range.begin);
    auto end_row = wormedit::absolute_row(*measures, draft->candidate.range.end);
    if (!end_row && !measures->entries.empty()) {
        const auto& last = measures->entries.back();
        if (draft->candidate.range.end.measure == last.id &&
            draft->candidate.range.end.row == last.row_capacity) {
            end_row = measures->total_rows;
        }
    }
    if (begin_row && end_row) {
        view.range_begin_fraction = static_cast<double>(
            static_cast<long double>(*begin_row) / static_cast<long double>(measures->total_rows));
        view.range_end_fraction = static_cast<double>(
            static_cast<long double>(*end_row) / static_cast<long double>(measures->total_rows));
    }

    const auto& candidate = draft->candidate;
    auto& form = view.form;
    form.output = static_cast<WormUiTimingRecipeOutput>(candidate.output);
    form.curve = static_cast<WormUiTimingRecipeCurve>(candidate.curve);
    form.sampling_mode = static_cast<WormUiTimingRecipeSamplingMode>(candidate.sampling.mode);
    form.fixed_extent = candidate.sampling.fixed_step_rows
                            ? WORM_UI_TIMING_RECIPE_FIXED_STEP_ROWS
                            : WORM_UI_TIMING_RECIPE_FIXED_POINT_COUNT;
    set_recipe_rational_text(form.value_from, candidate.parameters.value_from);
    set_recipe_rational_text(form.value_to, candidate.parameters.value_to);
    set_recipe_rational_text(form.shape, candidate.parameters.shape);
    set_recipe_rational_text(form.bump_start, candidate.parameters.bump_start);
    set_recipe_rational_text(form.bump_length, candidate.parameters.bump_length);
    set_recipe_rational_text(form.bump_x, candidate.parameters.bump_x);
    set_recipe_rational_text(form.bump_y, candidate.parameters.bump_y);
    set_recipe_rational_text(form.tolerance, candidate.sampling.tolerance);
    set_recipe_rational_text(form.transition_duration,
                             candidate.transition_duration.value_or(wormedit::rational_t{}));
    form.transition_is_seconds = candidate.transition_is_seconds.value_or(false);
    (void)std::snprintf(form.fixed_point_count, sizeof(form.fixed_point_count), "%u",
                        candidate.sampling.fixed_point_count.value_or(2));
    (void)std::snprintf(
        form.fixed_step_rows, sizeof(form.fixed_step_rows), "%llu",
        static_cast<unsigned long long>(candidate.sampling.fixed_step_rows.value_or(1)));
    (void)std::snprintf(form.adaptive_max_points, sizeof(form.adaptive_max_points), "%u",
                        candidate.sampling.max_points);

    if (!preview) {
        flattened.timing_recipe_status_storage = "The transient recipe preview is unavailable.";
        view.status_text = flattened.timing_recipe_status_storage.c_str();
        return;
    }
    view.valid = preview->diagnostics.empty();
    if (!preview->diagnostics.empty()) {
        flattened.timing_recipe_status_storage = preview->diagnostics.front().message;
    } else if (!preview->candidate_points.tolerance_met) {
        flattened.timing_recipe_status_storage =
            "Adaptive sampling reached its point cap before meeting tolerance.";
    } else {
        flattened.timing_recipe_status_storage =
            "Preview is transient; Apply stores only the editable recipe.";
    }
    view.status_text = flattened.timing_recipe_status_storage.c_str();

    long double minimum = 0.0L;
    long double maximum = 1.0L;
    const auto include_values = [&](const auto& points) {
        for (const auto& point : points) {
            if (std::isfinite(point.value)) {
                minimum = std::min(minimum, static_cast<long double>(point.value));
                maximum = std::max(maximum, static_cast<long double>(point.value));
            }
        }
    };
    include_values(preview->candidate_points.points);
    include_values(preview->effective_output.points);
    const auto scale = maximum > minimum ? maximum - minimum : 1.0L;
    const auto append_points = [&](const auto& source, auto& destination) {
        destination.reserve(
            std::min<std::size_t>(source.size(), WORM_UI_TIMING_RECIPE_POINT_CAPACITY));
        const auto append = [&](std::size_t source_index) {
            const auto& point = source[source_index];
            const auto row_fraction = static_cast<long double>(point.absolute_row) /
                                      static_cast<long double>(measures->total_rows);
            const auto value_fraction = (static_cast<long double>(point.value) - minimum) / scale;
            destination.push_back({
                .row_fraction = static_cast<double>(std::clamp(row_fraction, 0.0L, 1.0L)),
                .value_fraction = static_cast<double>(std::clamp(value_fraction, 0.0L, 1.0L)),
            });
        };
        if (source.size() <= WORM_UI_TIMING_RECIPE_POINT_CAPACITY) {
            for (std::size_t index = 0; index < source.size(); ++index) {
                append(index);
            }
            return;
        }

        // Keep the first/last point plus the first, last, minimum, and maximum
        // value in each vertical row bucket. This bounded screen-oriented
        // decimator preserves narrow extrema and structural boundaries that
        // uniform index skipping can make disappear.
        constexpr std::size_t bucket_count = (WORM_UI_TIMING_RECIPE_POINT_CAPACITY - 2) / 4;
        const auto bucket_for = [&](std::size_t index) {
            const auto fraction = static_cast<long double>(source[index].absolute_row) /
                                  static_cast<long double>(measures->total_rows);
            return std::min(static_cast<std::size_t>(fraction * bucket_count), bucket_count - 1);
        };
        append(0);
        for (std::size_t first = 1; first + 1 < source.size();) {
            const auto bucket = bucket_for(first);
            auto after = first + 1;
            auto minimum_index = first;
            auto maximum_index = first;
            while (after + 1 < source.size() && bucket_for(after) == bucket) {
                if (source[after].value < source[minimum_index].value) {
                    minimum_index = after;
                }
                if (source[after].value > source[maximum_index].value) {
                    maximum_index = after;
                }
                ++after;
            }
            std::array candidates{first, minimum_index, maximum_index, after - 1};
            std::ranges::sort(candidates);
            const auto unique_end = std::ranges::unique(candidates).begin();
            for (auto candidate = candidates.begin(); candidate != unique_end; ++candidate) {
                append(*candidate);
            }
            first = after;
        }
        append(source.size() - 1);
    };
    append_points(preview->candidate_points.points, flattened.timing_recipe_candidate_storage);
    append_points(preview->effective_output.points, flattened.timing_recipe_composed_storage);
    view.candidate_points = flattened.timing_recipe_candidate_storage.data();
    view.candidate_point_count = flattened.timing_recipe_candidate_storage.size();
    view.composed_points = flattened.timing_recipe_composed_storage.data();
    view.composed_point_count = flattened.timing_recipe_composed_storage.size();
}

void populate_speed_cache(timing_scene_cache_t& cache, const wormedit::chart_document_t& chart,
                          std::uint64_t revision) {
    if (!cache.attempted || cache.chart != chart.id ||
        cache.revision != revision) {
        cache = {.chart = chart.id, .revision = revision, .attempted = true};
        const auto timing = wormedit::build_timing_index(chart);
        if (!timing.index) {
            cache.status = timing.diagnostics.empty() ? "The timing index could not be built."
                                                      : timing.diagnostics.front().message;
        } else {
            const auto scroll_speed = wormedit::build_scroll_speed_index(chart, *timing.index);
            if (!scroll_speed.index) {
                cache.status = scroll_speed.diagnostics.empty()
                                   ? "The SPEEDS evaluator could not be built."
                                   : scroll_speed.diagnostics.front().message;
            } else {
                cache.total_rows = scroll_speed.index->total_rows;
                const auto spans = wormedit::scroll_speed_transition_spans(
                    *scroll_speed.index, *timing.index,
                    {.maximum_spans = WORM_UI_TIMING_SPEED_CAPACITY});
                if (!spans) {
                    cache.status = spans.diagnostics.empty()
                                       ? "The SPEEDS transition spans could not be built."
                                       : spans.diagnostics.front().message;
                }
                cache.truncated = spans.truncated;
                cache.speeds.reserve(spans.spans.size());
                std::unordered_map<std::uint64_t, std::pair<std::uint64_t, std::uint64_t>>
                    speed_orders;
                speed_orders.reserve(chart.speeds.size());
                for (std::size_t storage_order = 0; storage_order < chart.speeds.size();
                     ++storage_order) {
                    const auto& event = chart.speeds[storage_order];
                    speed_orders.emplace(
                        event.id.value,
                        std::pair{static_cast<std::uint64_t>(event.provenance.source_order),
                                  static_cast<std::uint64_t>(storage_order)});
                }
                for (std::size_t index = 0; index < spans.spans.size(); ++index) {
                    const auto& speed = scroll_speed.index->speed_segments[index];
                    const auto& span = spans.spans[index];
                    cache.speeds.push_back({
                        .object_id = speed.source_event.value,
                        .source_order = speed_orders[speed.source_event.value].first,
                        .storage_order = speed_orders[speed.source_event.value].second,
                        .marker_row = static_cast<long double>(speed.begin_row),
                        .affected_begin_row = rational_as_long_double(span.begin_row),
                        .affected_end_row = rational_as_long_double(span.end_row),
                        .multiplier =
                            static_cast<double>(rational_as_long_double(speed.target_multiplier)),
                        .transition_duration = rational_as_double(speed.transition_duration),
                        .transition_is_seconds = speed.transition_is_seconds,
                        .transition_clipped = span.clipped,
                    });
                }
            }
        }
    }
}

template <typename RowToPixels>
void populate_timing_stage_view(
    flattened_workspace_t& flattened, WormUiTimingView& view,
    const wormedit::chart_document_t& chart, const wormedit::measure_index_t* measures,
    const wormedit::timing_lane_pseudonote_preview_t* stage,
    const timing_grid_state_t& timing_grid, std::uint64_t revision, RowToPixels row_to_pixels) {
    if (stage && stage->status == wormedit::timing_lane_pseudonote_status_t::editing &&
        stage->chart == chart.id && stage->document_revision == revision) {
        view.stage_active = true;
        view.stage_valid = stage->valid;
        view.stage_existing_object_id =
            stage->existing_event.has_value() ? stage->existing_event->value : 0;
        view.stage_lane = timing_lane_kind_to_ui(stage->kind);
        const auto stage_row =
            measures ? wormedit::absolute_row(*measures, stage->position) : std::nullopt;
        view.stage_marker_y_pixels =
            stage_row ? row_to_pixels(static_cast<long double>(*stage_row)) : 0.0;
        view.stage_transition_is_seconds = false;
        view.stage_toggle_primary = false;
        view.stage_pause_unit = WORM_UI_TIMING_PAUSE_SECONDS;
        std::visit(
            [&](const auto& value) {
                using value_t = std::decay_t<decltype(value)>;
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
                if constexpr (std::is_same_v<value_t, wormedit::timing_bpm_value_t>) {
                    view.stage_value =
                        static_cast<double>(rational_as_long_double(value.beats_per_minute));
                    set_primary(value.beats_per_minute.numerator,
                                value.beats_per_minute.denominator);
                    if (const auto formatted = wormedit::format_bpm_text(value.beats_per_minute)) {
                        (void)std::snprintf(view.stage_primary_decimal_text,
                                            sizeof(view.stage_primary_decimal_text), "%s",
                                            formatted->decimal.c_str());
                        view.stage_primary_decimal_exact = formatted->decimal_exact;
                    }
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_scroll_value_t>) {
                    view.stage_value =
                        static_cast<double>(rational_as_long_double(value.multiplier));
                    set_primary(value.multiplier.numerator, value.multiplier.denominator);
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_speed_value_t>) {
                    view.stage_value =
                        static_cast<double>(rational_as_long_double(value.multiplier));
                    view.stage_secondary_value = rational_as_double(value.transition_beats);
                    view.stage_transition_is_seconds = value.transition_is_seconds;
                    set_primary(value.multiplier.numerator, value.multiplier.denominator);
                    set_secondary(value.transition_beats.numerator,
                                  value.transition_beats.denominator);
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_pause_value_t>) {
                    view.stage_value = static_cast<double>(rational_as_long_double(value.duration));
                    view.stage_pause_unit = timing_pause_unit_to_ui(value.source_unit);
                    set_primary(value.duration.numerator, value.duration.denominator);
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_warp_value_t>) {
                    const auto end_row =
                        measures ? wormedit::absolute_row(*measures, value.end) : std::nullopt;
                    const auto start_row = stage_row;
                    const auto step =
                        stage->existing_event.has_value()
                            ? std::uint64_t{1}
                            : timing_stage_grid_step(chart, timing_grid.active_denominator);
                    const auto length_steps =
                        end_row && start_row && *end_row > *start_row
                            ? static_cast<long double>(*end_row - *start_row) /
                                  static_cast<long double>(step)
                            : 1.0L;
                    view.stage_secondary_value = static_cast<double>(length_steps);
                    (void)std::snprintf(view.stage_secondary_numerator_text,
                                        sizeof(view.stage_secondary_numerator_text), "%.0Lf",
                                        length_steps);
                    (void)std::snprintf(view.stage_secondary_denominator_text,
                                        sizeof(view.stage_secondary_denominator_text), "1");
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_kiai_value_t>) {
                    view.stage_toggle_primary = value.enabled;
                } else if constexpr (std::is_same_v<value_t, wormedit::timing_anchor_value_t>) {
                    view.stage_toggle_primary = value.locked;
                    (void)std::snprintf(view.stage_primary_numerator_text,
                                        sizeof(view.stage_primary_numerator_text), "%llu",
                                        static_cast<unsigned long long>(value.source_sample_frame));
                    (void)std::snprintf(view.stage_primary_denominator_text,
                                        sizeof(view.stage_primary_denominator_text), "1");
                }
            },
            stage->value);
        if (!stage->diagnostics.empty()) {
            flattened.timing_status_storage = stage->diagnostics.front().message;
            view.stage_status_text = flattened.timing_status_storage.c_str();
        }
    }
}

template <typename RowToPixels>
auto populate_timing_grid_and_speeds(flattened_workspace_t& flattened,
                                    const wormedit::chart_document_t& chart,
                                    const timing_scene_cache_t& cache,
                                    const timing_grid_state_t& timing_grid,
                                    WormUiTimingView& view, RowToPixels row_to_pixels) -> bool {
    flattened.timing_grid_storage.clear();
    const auto grid = wormedit::make_editor_grid_lines(
        chart,
        {.visible_begin_row = 0,
         .visible_end_row = cache.total_rows,
         .active_snap_denominator = timing_grid.active_denominator,
         .pixels_per_standard_measure = view.viewport_height_pixels *
                                        static_cast<double>(chart.divisions_per_standard_measure) /
                                        static_cast<double>(cache.total_rows),
         .maximum_line_count = WORM_UI_TIMING_GRID_CAPACITY});
    if (!grid) {
        flattened.timing_status_storage = grid.diagnostics.empty()
                                              ? "The timing grid could not be composed."
                                              : grid.diagnostics.front().message;
        view.status_text = flattened.timing_status_storage.c_str();
        return false;
    }
    flattened.timing_grid_storage.reserve(grid.lines.size());
    for (const auto& line : grid.lines) {
        const auto row = static_cast<long double>(line.chart_row.whole_rows) +
                         static_cast<long double>(line.chart_row.fractional_rows.numerator) /
                             line.chart_row.fractional_rows.denominator;
        flattened.timing_grid_storage.push_back(
            {.y_pixels = row_to_pixels(row),
             .kind = static_cast<std::uint8_t>(line.kind),
             .emphasis = static_cast<std::uint8_t>(line.emphasis)});
    }
    flattened.timing_speed_storage.reserve(cache.speeds.size());
    for (const auto& speed : cache.speeds) {
        flattened.timing_speed_storage.push_back({
            .object_id = speed.object_id,
            .source_order = speed.source_order,
            .storage_order = speed.storage_order,
            .marker_y_pixels = row_to_pixels(speed.marker_row),
            .affected_begin_y_pixels = row_to_pixels(speed.affected_begin_row),
            .affected_end_y_pixels = row_to_pixels(speed.affected_end_row),
            .multiplier = speed.multiplier,
            .transition_duration = speed.transition_duration,
            .transition_is_seconds = speed.transition_is_seconds,
            .transition_clipped = speed.transition_clipped,
        });
    }
    std::ranges::sort(flattened.timing_speed_storage, [](const auto& left, const auto& right) {
        if (left.marker_y_pixels != right.marker_y_pixels) {
            return left.marker_y_pixels < right.marker_y_pixels;
        }
        if (left.source_order != right.source_order) {
            return left.source_order < right.source_order;
        }
        return left.storage_order != right.storage_order ? left.storage_order < right.storage_order
                                                         : left.object_id < right.object_id;
    });
    for (std::size_t first = 0; first < flattened.timing_speed_storage.size();) {
        auto after = first + 1;
        while (after < flattened.timing_speed_storage.size() &&
               flattened.timing_speed_storage[after].marker_y_pixels ==
                   flattened.timing_speed_storage[first].marker_y_pixels) {
            ++after;
        }
        const auto count = static_cast<double>(after - first);
        for (auto index = first; index < after; ++index) {
            flattened.timing_speed_storage[index].marker_left_fraction =
                static_cast<double>(index - first) / count;
            flattened.timing_speed_storage[index].marker_right_fraction =
                static_cast<double>(index - first + 1) / count;
        }
        first = after;
    }
    return true;
}

template <typename RowToPixels>
void populate_timing_markers(flattened_workspace_t& flattened,
                             const wormedit::chart_document_t& chart,
                             const wormedit::measure_index_t* measures,
                             WormUiTimingView& view, RowToPixels row_to_pixels) {
    flattened.timing_marker_storage.clear();
    std::size_t marker_reserve = 0;
    for (const auto count : {chart.bpms.size(), chart.scrolls.size(), chart.pauses.size(),
                             chart.warps.size(), chart.kiai.size(), chart.timing_anchors.size()}) {
        marker_reserve +=
            std::min<std::size_t>(count, WORM_UI_TIMING_MARKER_CAPACITY - marker_reserve);
    }
    flattened.timing_marker_storage.reserve(marker_reserve);
    if (measures != nullptr) {
        const auto append_marker = [&](wormedit::local_position_t position, std::uint64_t id,
                                       std::size_t source_order, std::size_t storage_order,
                                       WormUiTimingLaneKind lane) {
            if (flattened.timing_marker_storage.size() >= WORM_UI_TIMING_MARKER_CAPACITY) {
                view.truncated = true;
                return;
            }
            const auto row = wormedit::absolute_row(*measures, position);
            if (!row) {
                return;
            }
            flattened.timing_marker_storage.push_back({
                .object_id = id,
                .source_order = source_order,
                .storage_order = storage_order,
                .marker_y_pixels = row_to_pixels(static_cast<long double>(*row)),
                .chart_row = *row,
                .lane = lane,
            });
        };
        for (std::size_t index = 0; index < chart.bpms.size(); ++index)
            append_marker(chart.bpms[index].position, chart.bpms[index].id.value,
                          chart.bpms[index].provenance.source_order, index,
                          WORM_UI_TIMING_LANE_BPM);
        for (std::size_t index = 0; index < chart.scrolls.size(); ++index)
            append_marker(chart.scrolls[index].position, chart.scrolls[index].id.value,
                          chart.scrolls[index].provenance.source_order, index,
                          WORM_UI_TIMING_LANE_SCROLL);
        for (std::size_t index = 0; index < chart.pauses.size(); ++index)
            append_marker(chart.pauses[index].position, chart.pauses[index].id.value,
                          chart.pauses[index].provenance.source_order, index,
                          chart.pauses[index].kind == wormedit::pause_kind_t::stop
                              ? WORM_UI_TIMING_LANE_STOP
                              : WORM_UI_TIMING_LANE_DELAY);
        for (std::size_t index = 0; index < chart.warps.size(); ++index)
            append_marker(chart.warps[index].span.begin, chart.warps[index].id.value,
                          chart.warps[index].provenance.source_order, index,
                          WORM_UI_TIMING_LANE_WARP);
        for (std::size_t index = 0; index < chart.kiai.size(); ++index)
            append_marker(chart.kiai[index].position, chart.kiai[index].id.value,
                          chart.kiai[index].provenance.source_order, index,
                          WORM_UI_TIMING_LANE_KIAI);
        for (std::size_t index = 0; index < chart.timing_anchors.size(); ++index)
            append_marker(chart.timing_anchors[index].position,
                          chart.timing_anchors[index].id.value, index, index,
                          WORM_UI_TIMING_LANE_ANCHOR);
        std::ranges::sort(flattened.timing_marker_storage, [](const auto& left, const auto& right) {
            if (left.lane != right.lane) {
                return left.lane < right.lane;
            }
            if (left.marker_y_pixels != right.marker_y_pixels) {
                return left.marker_y_pixels < right.marker_y_pixels;
            }
            if (left.source_order != right.source_order) {
                return left.source_order < right.source_order;
            }
            return left.storage_order != right.storage_order
                       ? left.storage_order < right.storage_order
                       : left.object_id < right.object_id;
        });
        for (std::size_t first = 0; first < flattened.timing_marker_storage.size();) {
            auto after = first + 1;
            while (after < flattened.timing_marker_storage.size() &&
                   flattened.timing_marker_storage[after].lane ==
                       flattened.timing_marker_storage[first].lane &&
                   flattened.timing_marker_storage[after].marker_y_pixels ==
                       flattened.timing_marker_storage[first].marker_y_pixels) {
                ++after;
            }
            const auto count = static_cast<double>(after - first);
            for (auto index = first; index < after; ++index) {
                flattened.timing_marker_storage[index].marker_left_fraction =
                    static_cast<double>(index - first) / count;
                flattened.timing_marker_storage[index].marker_right_fraction =
                    static_cast<double>(index - first + 1) / count;
            }
            first = after;
        }
    }
}

} // namespace

void populate_timing_view(flattened_workspace_t& flattened,
                          const wormedit::document_history_t& history,
                          const wormedit::workspace_controller_t& workspace,
                          timing_scene_cache_t& cache, int window_width, int window_height,
                          const timing_grid_state_t& timing_grid,
                          const wormedit::timing_lane_pseudonote_preview_t* stage,
                          const wormedit::sv_recipe_draft_t* recipe_draft,
                          const wormedit::sv_recipe_draft_preview_t* recipe_preview,
                          bool recipe_selection_armed) {
    auto& view = flattened.view.timing;
    if (!view.visible) {
        return;
    }
    view.viewport_width_pixels = std::max(160.0, static_cast<double>(window_width) - 48.0);
    view.viewport_height_pixels = compose_surface_height(window_height, flattened.view);
    if (!workspace.selected_chart) {
        flattened.timing_status_storage = "Choose a difficulty to display its timing lanes.";
        view.status_text = flattened.timing_status_storage.c_str();
        return;
    }
    const auto chart_it = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                            &wormedit::chart_document_t::id);
    if (chart_it == history.document.charts.end()) {
        flattened.timing_status_storage = "The selected difficulty is no longer available.";
        view.status_text = flattened.timing_status_storage.c_str();
        return;
    }
    const auto& chart = *chart_it;
    view.chart_id = chart.id.value;
    view.document_revision = history.document.revision;
    if (!chart.measures.empty()) {
        for (const auto& bpm : chart.bpms) {
            if (bpm.position.measure == chart.measures.front().id && bpm.position.row == 0) {
                view.initial_bpm_present = true;
                view.initial_bpm_event_id = bpm.id.value;
                view.initial_bpm_value =
                    static_cast<double>(rational_as_long_double(bpm.beats_per_minute));
            }
        }
    }
    view.active_snap_denominator = timing_grid.active_denominator;
    view.supported_snap_denominators = timing_grid.supported_denominators.data();
    view.supported_snap_denominator_count = timing_grid.supported_denominators.size();
    view.speed_total_count = chart.speeds.size();
    populate_speed_cache(cache, chart, history.document.revision);
    if (!cache.status.empty()) {
        flattened.timing_status_storage = cache.status;
        view.status_text = flattened.timing_status_storage.c_str();
        return;
    }
    if (cache.total_rows == 0) {
        flattened.timing_status_storage = "This difficulty has no timing rows to display.";
        view.status_text = flattened.timing_status_storage.c_str();
        return;
    }
    view.total_rows = cache.total_rows;

    const auto row_to_pixels = [&](long double row) {
        const auto bounded = std::clamp(row, 0.0L, static_cast<long double>(cache.total_rows));
        return static_cast<double>(bounded / static_cast<long double>(cache.total_rows) *
                                   view.viewport_height_pixels);
    };
    if (!populate_timing_grid_and_speeds(flattened, chart, cache, timing_grid, view,
                                         row_to_pixels)) {
        return;
    }
    const auto measure_result = wormedit::build_measure_index(chart);
    const auto* measures = std::get_if<wormedit::measure_index_t>(&measure_result);
    populate_timing_markers(flattened, chart, measures, view, row_to_pixels);
    view.scene_available = true;
    view.truncated = view.truncated || cache.truncated;
    view.speeds = flattened.timing_speed_storage.data();
    view.speed_count = flattened.timing_speed_storage.size();
    view.markers = flattened.timing_marker_storage.data();
    view.marker_count = flattened.timing_marker_storage.size();
    view.grid_lines = flattened.timing_grid_storage.data();
    view.grid_line_count = flattened.timing_grid_storage.size();
    populate_timing_recipe_view(flattened, chart, measures, recipe_draft, recipe_preview,
                                recipe_selection_armed);
    populate_timing_stage_view(flattened, view, chart, measures, stage, timing_grid,
                               history.document.revision, row_to_pixels);
}

} // namespace wormedit::sys

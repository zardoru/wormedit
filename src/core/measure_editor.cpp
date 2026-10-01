#include <wormedit/core/measure_editor.h>
#include <wormedit/core/timing.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace wormedit {
namespace {

auto error(diagnostic_code_t code, std::string message) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error, .code = code, .message = std::move(message)};
}

void reject(document_command_result_t& result, diagnostic_code_t code, std::string message) {
    result.diagnostics.push_back(error(code, std::move(message)));
}

auto validate_definition(const chart_document_t& chart, const measure_definition_t& definition,
                         const measure_edit_limits_t& limits, document_command_result_t& result)
    -> bool {
    const auto validation = validate_measure(chart.divisions_per_standard_measure, definition);
    if (!validation) {
        reject(result, diagnostic_code_t::invalid_measure,
               "measure definition is not exactly representable by the chart resolution");
        return false;
    }
    if (*validation.row_capacity > limits.maximum_rows_per_measure) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "measure definition exceeds the configured row-capacity bound");
        return false;
    }
    return true;
}

auto next_measure_id(const chart_document_t& chart) -> std::optional<measure_id_t> {
    std::uint64_t maximum{};
    for (const auto& measure : chart.measures) {
        maximum = std::max(maximum, measure.id.value);
    }
    if (maximum == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return measure_id_t{maximum + 1};
}

auto unique_measure(chart_document_t& chart, measure_id_t id, document_command_result_t& result)
    -> std::vector<measure_chunk_t>::iterator {
    const auto found = std::ranges::find(chart.measures, id, &measure_chunk_t::id);
    if (!id || found == chart.measures.end()) {
        reject(result, diagnostic_code_t::invalid_id, "measure edit refers to an unknown measure");
        return chart.measures.end();
    }
    if (std::ranges::count(chart.measures, id, &measure_chunk_t::id) != 1) {
        reject(result, diagnostic_code_t::duplicate_id,
               "measure ID is ambiguous because it appears more than once");
        return chart.measures.end();
    }
    return found;
}

auto same_definition(const measure_definition_t& left, const measure_definition_t& right) -> bool {
    return left.length == right.length && left.meter == right.meter;
}

void shift_projections_for_insert(chart_document_t& chart, std::size_t before_index) {
    for (auto& projection : chart.bms_measure_length_projections) {
        if (projection.measure_index >= before_index &&
            projection.measure_index != std::numeric_limits<std::size_t>::max()) {
            ++projection.measure_index;
        }
    }
}

void shift_projections_for_delete(chart_document_t& chart, std::size_t removed_index) {
    std::erase_if(chart.bms_measure_length_projections, [removed_index](const auto& projection) {
        return projection.measure_index == removed_index;
    });
    for (auto& projection : chart.bms_measure_length_projections) {
        if (projection.measure_index > removed_index) {
            --projection.measure_index;
        }
    }
}

struct measure_edit_context_t {
    song_document_t& proposed;
    measure_edit_limits_t limits;
    document_command_result_t& result;
    std::string label;
    bool changed{};
};

auto target_measure_chart(measure_edit_context_t& context, chart_id_t id) -> chart_document_t* {
    const auto chart = std::ranges::find(context.proposed.charts, id, &chart_document_t::id);
    if (!id || chart == context.proposed.charts.end()) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "measure edit refers to an unknown chart");
        return nullptr;
    }
    if (chart->measures.size() > context.limits.maximum_measures) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "selected chart exceeds the configured measure cap");
        return nullptr;
    }
    return &*chart;
}

auto apply_measure_action(measure_edit_context_t& context,
                          const insert_measure_action_t& edit) -> void {
    auto* chart = target_measure_chart(context, edit.chart);
    if (chart == nullptr) {
        return;
    }
    if (chart->measures.size() >= context.limits.maximum_measures) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "measure insertion would exceed the configured measure cap");
        return;
    }
    if (edit.before_index > chart->measures.size()) {
        reject(context.result, diagnostic_code_t::invalid_measure,
               "measure insertion index lies beyond the chart");
        return;
    }
    if (!validate_definition(*chart, edit.definition, context.limits, context.result)) {
        return;
    }
    const auto id = next_measure_id(*chart);
    if (!id) {
        reject(context.result, diagnostic_code_t::invalid_id, "measure ID sequence is exhausted");
        return;
    }
    shift_projections_for_insert(*chart, edit.before_index);
    chart->measures.insert(chart->measures.begin() + static_cast<std::ptrdiff_t>(edit.before_index),
                           measure_chunk_t{.id = *id, .definition = edit.definition});
    context.label = "Insert measure";
    context.changed = true;
}

auto apply_measure_action(measure_edit_context_t& context,
                          const update_measure_definition_action_t& edit) -> void {
    auto* chart = target_measure_chart(context, edit.chart);
    if (chart == nullptr) {
        return;
    }
    const auto measure = unique_measure(*chart, edit.measure, context.result);
    if (measure == chart->measures.end() ||
        !validate_definition(*chart, edit.definition, context.limits, context.result)) {
        return;
    }
    if (same_definition(measure->definition, edit.definition)) {
        return;
    }
    measure->definition = edit.definition;
    context.label = "Update measure definition";
    context.changed = true;
}

auto apply_measure_action(measure_edit_context_t& context,
                          const delete_measure_action_t& edit) -> void {
    auto* chart = target_measure_chart(context, edit.chart);
    if (chart == nullptr) {
        return;
    }
    const auto measure = unique_measure(*chart, edit.measure, context.result);
    if (measure == chart->measures.end()) {
        return;
    }
    if (chart->measures.size() == 1) {
        reject(context.result, diagnostic_code_t::invalid_measure,
               "a chart must retain at least one measure");
        return;
    }
    if (!measure->notes.empty() || !measure->bgm_events.empty()) {
        reject(context.result, diagnostic_code_t::invalid_measure,
               "measure deletion requires an empty note and BGM bucket");
        return;
    }
    const auto removed_index =
        static_cast<std::size_t>(std::distance(chart->measures.begin(), measure));
    chart->measures.erase(measure);
    shift_projections_for_delete(*chart, removed_index);
    context.label = "Delete measure";
    context.changed = true;
}

} // namespace

auto apply_measure_edit(document_history_t& history, std::uint64_t expected_revision,
                        const measure_edit_action_t& action, measure_edit_limits_t limits)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id, "measure editor document revision is stale");
        return result;
    }
    if (limits.maximum_measures == 0 || limits.maximum_rows_per_measure == 0) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "measure editor bounds must be positive");
        return result;
    }

    auto proposed = history.document;
    measure_edit_context_t context{.proposed = proposed,
                                   .limits = limits,
                                   .result = result,
                                   .label = {},
                                   .changed = false};
    std::visit([&context](const auto& edit) { apply_measure_action(context, edit); }, action);

    if (!result.diagnostics.empty() || !context.changed) {
        return result;
    }
    return commit_document_command(history, std::move(proposed), std::move(context.label));
}

auto extend_chart_to_audio_end(chart_document_t& chart, const audio_stream_info_t& stream,
                               measure_edit_limits_t limits) -> bool {
    if (!stream.frame_count || stream.sample_rate == 0) return true;
    const auto timing = build_timing_index(chart);
    if (!timing || timing.index->rows.empty()) return false;
    const auto& end = timing.index->rows.back();
    const auto end_seconds = static_cast<long double>(end.after_stops_seconds.numerator) /
                             end.after_stops_seconds.denominator;
    const auto audio_seconds = static_cast<long double>(*stream.frame_count) / stream.sample_rate;
    if (audio_seconds <= end_seconds) return true;
    const auto bpm = static_cast<long double>(end.effective_bpm.numerator) /
                     end.effective_bpm.denominator;
    const auto extra = std::ceil((audio_seconds - end_seconds) * bpm / 240.0L);
    if (!std::isfinite(extra) || extra < 1 || chart.measures.size() > limits.maximum_measures ||
        extra > limits.maximum_measures - chart.measures.size() ||
        chart.divisions_per_standard_measure > limits.maximum_rows_per_measure) return false;
    const auto first = next_measure_id(chart);
    const auto count = static_cast<std::size_t>(extra);
    if (!first || count - 1 > std::numeric_limits<std::uint64_t>::max() - first->value)
        return false;
    chart.measures.reserve(chart.measures.size() + count);
    for (std::size_t index = 0; index < count; ++index)
        chart.measures.push_back({.id = measure_id_t{first->value + index}});
    return true;
}

auto extend_native_charts_to_audio_end(song_document_t& document) -> bool {
    const audio_stream_info_t* longest{};
    long double duration{};
    for (const auto& resource : document.resources) {
        if (std::ranges::find(resource.roles, resource_role_t::music) == resource.roles.end() &&
            std::ranges::find(resource.roles, resource_role_t::bgm) == resource.roles.end()) continue;
        const auto asset = std::ranges::find(document.audio_assets, resource.asset, &audio_asset_t::id);
        if (asset == document.audio_assets.end() || !asset->stream.frame_count ||
            asset->stream.sample_rate == 0) continue;
        const auto seconds = static_cast<long double>(*asset->stream.frame_count) /
                             asset->stream.sample_rate;
        if (seconds > duration) {
            duration = seconds;
            longest = &asset->stream;
        }
    }
    if (!longest) return true;
    for (auto& chart : document.charts) {
        if (chart.source_family == format_family_t::native_project &&
            !extend_chart_to_audio_end(chart, *longest)) return false;
    }
    return true;
}

} // namespace wormedit

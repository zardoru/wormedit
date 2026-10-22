#include <wormedit/core/timing_reposition.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/quantize.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/snap.h>
#include "chart_position_storage.h"
#include <algorithm>
#include <limits>
#include <unordered_map>

namespace wormedit {
namespace {
using reposition_result_t = diagnosed_t<timing_reposition_report_t>;

template<class Events, class Visit>
auto visit_audio_events(Events& events, const Visit& visit) -> bool {
    for (auto& event : events) if (!visit(event)) return false;
    return true;
}
template<class Chart, class Visit>
auto visit_audio_objects(Chart& chart, const Visit& visit) -> bool {
    for (auto& measure : chart.measures) {
        if (!visit_audio_events(measure.notes, visit) || !visit_audio_events(measure.bgm_events, visit)) return false;
    }
    for (auto& channel : chart.sound_channels) if (!visit_audio_events(channel.events, visit)) return false;
    return true;
}
struct repositioned_object_t {
    std::uint64_t row{};
    std::optional<std::uint64_t> end;
};
struct audio_position_mapper_t {
    const chart_document_t& chart;
    const measure_index_t& measures;
    const timing_index_t& before;
    const timing_index_t& after;
    reposition_result_t& result;

    auto row_at(local_position_t position, object_id_t object) const -> std::optional<std::uint64_t> {
        const auto old_row = measures.row_of(position, measure_chart_end_policy_t::allow);
        const auto old = old_row ? before.seconds_at(*old_row) : timing_query_result_t{};
        if (old.inside_warp) {
            result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
                "Warped sound objects cannot be anchored to audio time.", object));
            return {};
        }
        const auto inverse = old.seconds ? after.row_at(*old.seconds, chart_end_policy_t::extrapolate_tail,
            {.warp_side = timing_warp_inverse_side_t::exit})
                                         : timing_position_query_result_t{};
        if (!inverse.absolute_row || inverse.absolute_row->numerator < 0) {
            result.diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
                "Audio-time anchoring cannot map this position through the edited timing.", object));
            return {};
        }
        const auto exact = *inverse.absolute_row;
        const auto row = exact.numerator % exact.denominator == 0
            ? snap_canvas_row_or_exact(chart, measures, static_cast<std::uint64_t>(exact.numerator) / exact.denominator,
                {}, std::numeric_limits<std::uint64_t>::max())
            : snap_canvas_row(chart, measures, to_long_double(exact),
                {.denominator = chart.divisions_per_standard_measure}, std::numeric_limits<std::uint64_t>::max());
        // The exact inverse extrapolates beyond the stored tail. Interior rows additionally
        // need a forward check because stops/warps can make their audio time unrepresentable.
        const auto mapped = row && *row <= after.total_rows ? after.seconds_at(*row)
            : timing_query_result_t{.seconds = row ? old.seconds : std::nullopt};
        if (!mapped.seconds) {
            result.diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
                "Audio-time anchoring exceeds the supported chart extent.", object));
            return {};
        }
        if (exact.numerator % exact.denominator != 0 || *mapped.seconds != *old.seconds)
            ++result.value.rounded_positions;
        return row;
    }
};
auto plan_audio_positions(const chart_document_t& chart, const audio_position_mapper_t& mapper,
    std::unordered_map<std::uint64_t, repositioned_object_t>& moves, std::uint64_t& extent) -> bool {
    return visit_audio_objects(chart, [&](const auto& object) {
        const auto row = mapper.row_at(object.position, object.id);
        const auto end = object.end ? mapper.row_at(*object.end, object.id) : std::nullopt;
        if (!row || (object.end && !end)) return false;
        if (!moves.emplace(object.id.value, repositioned_object_t{*row, end}).second) {
            mapper.result.diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                "Audio-time anchoring needs unique sound-object identities.", object.id));
            return false;
        }
        extent = std::max(extent, *row);
        if (end && *end) extent = std::max(extent, *end - 1);
        const auto old = mapper.measures.row_of(object.position);
        const auto old_end = object.end ? mapper.measures.row_of(*object.end, measure_chart_end_policy_t::allow) : std::nullopt;
        if (old != row || old_end != end) ++mapper.result.value.moved_objects;
        return true;
    });
}
auto install_audio_positions(chart_document_t& chart,
    const std::unordered_map<std::uint64_t, repositioned_object_t>& moves) -> bool {
    const auto measures = measure_index_t::from(chart);
    if (!measures) return false;
    return visit_audio_objects(chart, [&](auto& object) {
        const auto& move = moves.at(object.id.value);
        const auto head = measures->position_at(move.row);
        const auto end = move.end ? measures->position_at(*move.end, measure_chart_end_policy_t::allow) : std::nullopt;
        if (!head || (move.end && !end)) return false;
        object.position = *head;
        object.end = end;
        return true;
    });
}
}
auto preserve_audio_positions(const chart_document_t& before, chart_document_t& after,
    timing_anchoring_t options) -> diagnosed_t<timing_reposition_report_t> {
    reposition_result_t result;
    if (options.positions == timing_position_policy_t::musical) return result;
    if (before.bms_control_tree && before.bms_control_tree->has_control_flow()) {
        result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
            "Audio-time anchoring cannot rewrite retained BMS control flow."));
        return result;
    }
    const auto old_timing = timing_index_t::from(before);
    const auto new_timing = timing_index_t::from(after);
    const auto measures = measure_index_t::from(before);
    if (!old_timing.value || !new_timing.value || !measures) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_rational,
            "Audio-time anchoring requires valid exact timing before and after the edit."));
        return result;
    }
    std::unordered_map<std::uint64_t, repositioned_object_t> moves;
    std::uint64_t extent{};
    const audio_position_mapper_t mapper{after, *measures, *old_timing.value, *new_timing.value, result};
    if (!plan_audio_positions(before, mapper, moves, extent)) return result;
    if (result.value.rounded_positions && !options.allow_quantization) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
            std::to_string(result.value.rounded_positions) + " positions need rounding; enable Allow quantization to apply."));
        return result;
    }
    if (moves.empty()) return result;
    if (!extend_chart_to_row(after, extent) || !install_audio_positions(after, moves) ||
        !rehome_measure_objects(after, result.diagnostics)) {
        if (result.diagnostics.empty()) result.diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
            "Audio-time anchoring exceeds the supported chart extent."));
        return result;
    }
    result.diagnostics = validate_repositioned_audio(before, after);
    return result;
}
auto stage_timing_reposition(document_command_transaction_t& transaction, chart_id_t chart,
    timing_anchoring_t options, attempt_t<document_edit_effect_t> edit, timing_reposition_report_t* report)
    -> attempt_t<document_edit_effect_t> {
    if (report) *report = {};
    if (!edit.value || !edit.value->changed || options.positions == timing_position_policy_t::musical) return edit;
    return transaction.stage(chart, "Timing edit refers to an unknown chart.",
        [&](chart_document_t& after, song_document_t&) -> attempt_t<document_edit_effect_t> {
            auto mapped = preserve_audio_positions(*transaction.source_chart(chart), after, options);
            if (report) *report = mapped.value;
            return {.value = std::move(edit.value), .diagnostics = std::move(mapped.diagnostics)};
        });
}

}

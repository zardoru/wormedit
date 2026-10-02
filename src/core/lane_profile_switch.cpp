#include <wormedit/core/lane_profile_switch.h>

#include <wormedit/core/lane_layout_editor.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace wormedit {
namespace {

constexpr std::array<std::string_view, 3> step_types{"pump-single", "pump-double", "dance-solo"};

auto reject(document_command_result_t& result, std::string message) {
    result.diagnostics.push_back({.severity = diagnostic_severity_t::error,
                                  .code = diagnostic_code_t::invalid_id,
                                  .message = std::move(message),
                                  .object = std::nullopt});
}

auto measure_order(const chart_document_t& chart, measure_id_t id) -> std::optional<std::size_t> {
    const auto found = std::ranges::find(chart.measures, id, &measure_chunk_t::id);
    if (found == chart.measures.end()) return std::nullopt;
    return static_cast<std::size_t>(found - chart.measures.begin());
}

} // namespace

auto lane_profile_choices(std::size_t lane_count) -> std::vector<lane_input_profile_t> {
    std::vector<lane_input_profile_t> choices;
    if (const auto osu = make_osu_mania_profile(static_cast<std::uint32_t>(lane_count)))
        choices.push_back(*osu);
    for (const auto step_type : step_types) {
        auto profile = make_stepmania_profile_for(step_type);
        if (profile && profile->layout.lanes.size() == lane_count)
            choices.push_back(std::move(*profile));
    }
    return choices;
}

auto active_lane_layout_segment(const chart_document_t& chart, local_position_t position)
    -> const lane_layout_segment_t* {
    const auto cursor = measure_order(chart, position.measure);
    const lane_layout_segment_t* best{};
    std::optional<std::size_t> best_order;
    std::uint64_t best_row{};
    for (const auto& segment : chart.lane_layouts) {
        const auto order = measure_order(chart, segment.position.measure);
        if (!order || !cursor || *order > *cursor) continue;
        if (*order == *cursor && segment.position.row > position.row) continue;
        if (!best || *order > *best_order ||
            (*order == *best_order && segment.position.row >= best_row)) {
            best = &segment;
            best_order = order;
            best_row = segment.position.row;
        }
    }
    return best;
}

auto retarget_lane_layout(const lane_layout_t& current, const lane_layout_t& target)
    -> std::optional<lane_layout_t> {
    if (current.lanes.size() != target.lanes.size()) return std::nullopt;
    auto ordered_current = current.lanes;
    auto ordered_target = target.lanes;
    std::ranges::stable_sort(ordered_current, {}, &lane_definition_t::display_order);
    std::ranges::stable_sort(ordered_target, {}, &lane_definition_t::display_order);
    auto result = current;
    result.profile_id = target.profile_id;
    result.display_name = target.display_name;
    for (auto& lane : result.lanes) {
        const auto rank = static_cast<std::size_t>(std::ranges::find(
            ordered_current, lane.id, &lane_definition_t::id) - ordered_current.begin());
        const auto& source = ordered_target[rank];
        lane.name = source.name;
        lane.glyph = source.glyph;
        lane.key_color = source.key_color;
    }
    return result;
}

auto apply_lane_profile_switch(document_history_t& history, std::uint64_t expected_revision,
                               chart_id_t chart, local_position_t position,
                               const lane_layout_t& target) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    const auto found = std::ranges::find(history.document.charts, chart, &chart_document_t::id);
    if (found == history.document.charts.end()) {
        reject(result, "The selected difficulty is no longer available.");
        return result;
    }
    const auto* segment = active_lane_layout_segment(*found, position);
    if (!segment) {
        reject(result, "No lane layout is active at the cursor.");
        return result;
    }
    auto layout = retarget_lane_layout(segment->layout, target);
    if (!layout) {
        reject(result, "That profile has a different number of lanes.");
        return result;
    }
    return apply_lane_layout_edit(history, expected_revision,
                                  update_lane_layout_action_t{chart, segment->id, std::move(*layout)});
}

} // namespace wormedit

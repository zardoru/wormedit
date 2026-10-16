#include "chart_object_ids.h"
#include <wormedit/core/lane_layout_editor.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace wormedit {
namespace {

struct located_segment_t {
    std::uint64_t row{};
    const lane_layout_segment_t* segment{};
};

// Segments ordered by absolute start row; nullopt when a position is invalid.
auto ordered_segments(const chart_document_t& chart, const measure_index_t& index)
    -> std::optional<std::vector<located_segment_t>> {
    std::vector<located_segment_t> ordered;
    for (const auto& segment : chart.lane_layouts) {
        const auto row = index.row_of(segment.position);
        if (!row) return std::nullopt;
        ordered.push_back({*row, &segment});
    }
    std::ranges::sort(ordered, {}, &located_segment_t::row);
    return ordered;
}

auto has_lane(const lane_layout_t& layout, lane_id_t lane) -> bool { return layout.has_lane(lane); }

// Notes whose lane is missing from the layout active at their position.
auto count_unmapped_notes(const chart_document_t& chart) -> std::optional<std::size_t> {
    const auto built = measure_index_t::from(chart);
    if (!built) return std::nullopt;
    const auto segments = ordered_segments(chart, *built);
    if (!segments) return std::nullopt;
    std::size_t unmapped{};
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto row = built->row_of(note.position);
            if (!row) return std::nullopt;
            const located_segment_t* active{};
            for (const auto& entry : *segments) {
                if (entry.row <= *row) active = &entry;
            }
            if (active == nullptr || !has_lane(active->segment->layout, note.lane)) ++unmapped;
        }
    }
    return unmapped;
}

struct layout_edit_context_t {
    chart_document_t& chart;
    lane_layout_change_policy_t policy;
    document_command_result_t& result;
    std::string label;
};

auto is_position_allowed(const layout_edit_context_t& context, local_position_t position) -> bool {
    if (context.policy == lane_layout_change_policy_t::measure_boundaries_only &&
        position.row != 0) {
        reject(context.result, diagnostic_code_t::invalid_measure,
               "lane layout changes must start on a measure boundary");
        return false;
    }
    const auto built = measure_index_t::from(context.chart);
    if (!built || !built->row_of(position)) {
        reject(context.result, diagnostic_code_t::row_out_of_range,
               "lane layout change lies outside the chart");
        return false;
    }
    return true;
}

auto find_segment(layout_edit_context_t& context, object_id_t id)
    -> std::vector<lane_layout_segment_t>::iterator {
    const auto found = std::ranges::find(context.chart.lane_layouts, id, &lane_layout_segment_t::id);
    if (!id || found == context.chart.lane_layouts.end()) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "lane layout edit refers to an unknown segment");
        return context.chart.lane_layouts.end();
    }
    return found;
}

void apply_edit(layout_edit_context_t& context,
                const wormedit::insert_edit_t<wormedit::lane_layout_edit_value_t>& edit) {
    if (!is_position_allowed(context, edit.value.position))
        return;
    const auto occupied = std::ranges::any_of(context.chart.lane_layouts, [&](const auto& segment) {
        return segment.position == edit.value.position;
    });
    if (occupied) {
        reject(context.result, diagnostic_code_t::duplicate_id,
               "a lane layout segment already starts at this position");
        return;
    }
    const auto id = next_chart_object_id(context.chart).id;
    if (!id) {
        reject(context.result, diagnostic_code_t::invalid_id, "object ID sequence is exhausted");
        return;
    }
    if (edit.value.input && context.chart.input_profile.bindings.empty())
        context.chart.input_profile = *edit.value.input;
    context.chart.lane_layouts.push_back(
        {.id = *id, .position = edit.value.position, .layout = edit.value.layout});
    context.label = "Insert lane layout";
}

void apply_edit(layout_edit_context_t& context,
                const wormedit::update_edit_t<wormedit::lane_layout_edit_value_t>& edit) {
    const auto found = find_segment(context, edit.id);
    if (found == context.chart.lane_layouts.end()) return;
    found->layout = edit.value.layout;
    context.label = "Update lane layout";
}

void apply_edit(layout_edit_context_t& context,
                const wormedit::delete_edit_t<wormedit::lane_layout_edit_value_t>& edit) {
    const auto found = find_segment(context, edit.id);
    if (found == context.chart.lane_layouts.end()) return;
    const auto built = measure_index_t::from(context.chart);
    const auto row = !built ? std::nullopt : built->row_of(found->position);
    if (!row || *row == 0) {
        reject(context.result, diagnostic_code_t::invalid_measure,
               "the lane layout at the chart start cannot be removed");
        return;
    }
    context.chart.lane_layouts.erase(found);
    context.label = "Delete lane layout";
}

void sort_segments(chart_document_t& chart) {
    const auto built = measure_index_t::from(chart);
    if (!built) return;
    std::ranges::stable_sort(chart.lane_layouts, {}, [&](const auto& segment) {
        return built->row_of(segment.position).value_or(0);
    });
}

} // namespace

auto apply_lane_layout_edit(document_history_t& history,
                            const lane_layout_action_t& action, lane_layout_change_policy_t policy)
    -> document_command_result_t {
    auto transaction = document_command_transaction_t(history);
    auto& result = transaction.outcome();
    const auto chart_id = std::visit([](const auto& edit) { return edit.chart; }, action);
    auto& proposed = transaction.document();
    const auto chart = proposed.find_chart(chart_id);
    if (!chart_id || chart == nullptr) {
        reject(result, diagnostic_code_t::invalid_id, "lane layout edit refers to an unknown chart");
        return result;
    }
    layout_edit_context_t context{*chart, policy, result, {}};
    std::visit([&context](const auto& edit) { apply_edit(context, edit); }, action);
    if (!result.diagnostics.empty()) return result;

    sort_segments(*chart);
    const auto unmapped = count_unmapped_notes(*chart);
    if (unmapped && *unmapped > 0) {
        reject(result, diagnostic_code_t::invalid_id,
               std::to_string(*unmapped) +
                   " note(s) would sit on a lane the new layout does not define");
        return result;
    }
    return transaction.commit(std::move(context.label));
}

} // namespace wormedit

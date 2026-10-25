#include "chart_object_ids.h"
#include "chart_position_storage.h"
#include "timing_arithmetic.h"
#include <wormedit/core/rational.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/virtual_canvas.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>
#include <limits>
#include <optional>
#include <numeric>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace wormedit {
auto resize_measure_keep_rows_action_t::definition_for(const measure_definition_t& current) const
    -> measure_definition_t {
    auto result = definition;
    if (preserve_meter && (!current.meter || reduce_fraction(current.length) == reduce_fraction(definition.length)))
        result.meter = current.meter;
    return result;
}

namespace {

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

constexpr std::string_view unknown_measure_message = "measure edit refers to an unknown measure";
constexpr target_query_policy_t measure_target_policy{
    .messages = {unknown_measure_message,
                 "measure ID is ambiguous because it appears more than once",
                 unknown_measure_message}};

auto measure_definition_equal(const measure_definition_t& left, const measure_definition_t& right) -> bool {
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

auto has_bms_control_flow(const chart_document_t& chart) -> bool {
    return chart.bms_control_tree.has_value() || !chart.bms_bpm_projections.empty() ||
           !chart.bms_pause_projections.empty();
}

// Rejects removals that would silently drop linked or branch-owned content.
auto removal_blocker(const chart_document_t& chart, const measure_chunk_t& measure)
    -> const char* {
    if (has_bms_control_flow(chart))
        return "measure removal is not supported on a chart with retained BMS control flow";
    const auto linked = [](const auto& value) {
        return value.branch.has_value();
    };
    if (std::ranges::any_of(measure.notes, linked) ||
        std::ranges::any_of(measure.bgm_events, linked))
        return "measure removal cannot delete branch-owned objects";
    const auto sound_linked = [](const bgm_event_t& event) {
        return event.sound_channel_event.has_value();
    };
    if (std::ranges::any_of(measure.bgm_events, sound_linked))
        return "measure removal cannot delete events linked to a sound channel";
    return nullptr;
}

// Moves a segment off the removed measure; a following segment wins a clash.
void retarget_layouts(chart_document_t& chart, measure_id_t removed,
                      std::optional<measure_id_t> next, measure_removal_proposal_t& proposal) {
    auto& layouts = chart.lane_layouts;
    const auto at_removed = [&](const auto& segment) { return segment.position.measure == removed; };
    if (!next) {
        if (std::ranges::any_of(layouts, at_removed))
            proposal.diagnostics.push_back(error(
                diagnostic_code_t::invalid_measure,
                "the last measure holds a lane layout change that has no later measure to move to"));
        return;
    }
    const auto clash = std::ranges::any_of(layouts, [&](const auto& segment) {
        return segment.position.measure == *next && segment.position.row == 0;
    });
    if (clash) {
        std::erase_if(layouts, at_removed);
        return;
    }
    for (auto& segment : layouts) {
        if (at_removed(segment)) {
            segment.position = {*next, 0};
            ++proposal.moved_positions;
        }
    }
}

// BGA events are contained content like BGM events: they are deleted with the measure.
void delete_contained_bga(chart_document_t& chart, measure_id_t removed,
                          measure_removal_proposal_t& proposal) {
    const auto contained = [&](const bga_event_t& event) {
        return event.position.measure == removed;
    };
    proposal.deleted_bga_events =
        static_cast<std::size_t>(std::ranges::count_if(chart.bga_events, contained));
    std::erase_if(chart.bga_events, contained);
}

void retarget_free_positions(chart_document_t& chart, measure_id_t removed,
                             std::optional<measure_id_t> next,
                             measure_removal_proposal_t& proposal) {
    for_each_free_position(chart, [&](local_position_t& position, position_role_t kind) {
        if (position.measure != removed) return;
        if (!next) {
            proposal.diagnostics.push_back(error(
                diagnostic_code_t::invalid_measure,
                "the last measure holds positioned objects that have no later measure to move to"));
            return;
        }
        if (kind == position_role_t::note_end && position.row > 0)
            ++proposal.shortened_holds;
        else
            ++proposal.moved_positions;
        position = {*next, 0};
    });
}

// Applies the removal to `chart` in place; the proposal says what happened or
// why the chart must be discarded.
auto remove_measure_contents(chart_document_t& chart, measure_id_t id)
    -> measure_removal_proposal_t {
    measure_removal_proposal_t proposal;
    const auto found = chart.find_measure(id);
    if (!id || found == nullptr) {
        proposal.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "measure edit refers to an unknown measure"));
        return proposal;
    }
    if (chart.measures.size() == 1) {
        proposal.diagnostics.push_back(error(diagnostic_code_t::invalid_measure,
                                             "a chart must retain at least one measure"));
        return proposal;
    }
    if (const auto* blocker = removal_blocker(chart, *found)) {
        proposal.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature, blocker));
        return proposal;
    }
    const auto index = static_cast<std::size_t>(found - chart.measures.data());
    std::optional<measure_id_t> next;
    if (index + 1 < chart.measures.size()) next = chart.measures[index + 1].id;
    proposal.deleted_notes = found->notes.size();
    proposal.deleted_bgm_events = found->bgm_events.size();
    retarget_layouts(chart, id, next, proposal);
    delete_contained_bga(chart, id, proposal);
    retarget_free_positions(chart, id, next, proposal);
    if (!proposal.diagnostics.empty()) return proposal;
    chart.measures.erase(chart.measures.begin() + static_cast<std::ptrdiff_t>(index));
    shift_projections_for_delete(chart, index);
    return proposal;
}

struct measure_edit_context_t {
    song_document_t& proposed;
    measure_edit_limits_t limits;
    document_command_result_t& result;
    std::string label;
    bool changed{};
};

auto target_measure_chart(measure_edit_context_t& context, chart_id_t id) -> chart_document_t* {
    const auto chart = context.proposed.find_chart(id);
    if (!id || chart == nullptr) {
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
    const auto id = id_ceiling_t<measure_id_t>::from(*chart).next();
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
    const auto measure = chart->unique_target(&chart_document_t::measures, edit.measure,
                                               context.result.diagnostics, measure_target_policy);
    if (measure == nullptr ||
        !validate_definition(*chart, edit.definition, context.limits, context.result)) {
        return;
    }
    if (measure_definition_equal(measure->definition, edit.definition)) {
        return;
    }
    measure->definition = edit.definition;
    context.label = "Update measure definition";
    context.changed = true;
}

auto apply_measure_action(measure_edit_context_t& context,
                          const update_measures_definition_action_t& edit) -> void {
    auto* chart = target_measure_chart(context, edit.chart);
    if (chart == nullptr) {
        return;
    }
    if (edit.measures.empty()) {
        reject(context.result, diagnostic_code_t::invalid_measure, "no measures were selected");
        return;
    }
    if (!validate_definition(*chart, edit.definition, context.limits, context.result)) {
        return;
    }
    auto seen = std::vector<measure_id_t>{};
    for (const auto id : edit.measures) {
        if (std::ranges::find(seen, id) != seen.end()) {
            reject(context.result, diagnostic_code_t::invalid_measure,
                   "measure selection names a measure twice");
            return;
        }
        seen.push_back(id);
        const auto measure = chart->unique_target(&chart_document_t::measures, id,
                                                   context.result.diagnostics, measure_target_policy);
        if (measure == nullptr) {
            return;
        }
        if (!measure_definition_equal(measure->definition, edit.definition)) {
            measure->definition = edit.definition;
            context.changed = true;
        }
    }
    context.label = "Update measure definitions";
}

struct rowed_note_t {
    note_t note;
    std::uint64_t row{};
    std::optional<std::uint64_t> end_row;
};

struct rowed_bgm_t {
    bgm_event_t event;
    std::uint64_t row{};
    std::optional<std::uint64_t> end_row;
};

struct rowed_free_t {
    local_position_t* position{};
    std::uint64_t row{};
    bool exclusive_end{};
};

// Creates default measures at the end until the chart has `count` of them.
auto append_default_measures(chart_document_t& chart, std::size_t count,
                             const measure_edit_limits_t& limits) -> bool {
    if (count > limits.maximum_measures) return false;
    auto ids = id_ceiling_t<measure_id_t>::from(chart);
    while (chart.measures.size() < count) {
        const auto id = ids.next();
        if (!id) return false;
        ids.reserve(*id);
        chart.measures.push_back(measure_chunk_t{.id = *id});
    }
    return true;
}

// Takes every bucketed object out of measures from `first`, recording the absolute row it holds.
auto lift_bucketed_objects(chart_document_t& chart, const measure_index_t& index, std::size_t first,
                           std::vector<rowed_note_t>& notes, std::vector<rowed_bgm_t>& bgm) -> bool {
    for (auto at = first; at < chart.measures.size(); ++at) {
        auto& measure = chart.measures[at];
        for (auto& note : measure.notes) {
            const auto row = index.row_of(note.position);
            const auto end = note.end ? index.row_of(*note.end) : std::nullopt;
            if (!row || (note.end && !end)) return false;
            notes.push_back({std::move(note), *row, end});
        }
        for (auto& event : measure.bgm_events) {
            const auto row = index.row_of(event.position);
            const auto end = event.end ? index.row_of(*event.end) : std::nullopt;
            if (!row || (event.end && !end)) return false;
            bgm.push_back({std::move(event), *row, end});
        }
        measure.notes.clear();
        measure.bgm_events.clear();
    }
    return true;
}

auto collect_free_rows(chart_document_t& chart, const measure_index_t& index,
                       std::vector<rowed_free_t>& free_rows) -> bool {
    auto ok = true;
    for_each_free_position(chart, [&](local_position_t& position, position_role_t) {
        const auto row = index.row_of(position);
        if (!row) ok = false;
        else free_rows.push_back({&position, *row});
    });
    return ok;
}

auto place_free_position(const measure_index_t& index, const rowed_free_t& item)
    -> std::optional<local_position_t> {
    if (item.exclusive_end && item.row == index.total_rows && !index.entries.empty())
        return local_position_t{index.entries.back().id, index.entries.back().row_capacity};
    return index.position_at(item.row);
}

// Writes each lifted object back at the position its row now has.
auto place_lifted_objects(chart_document_t& chart, const measure_index_t& after,
                          std::size_t first_measure, std::vector<rowed_note_t>& notes,
                          std::vector<rowed_bgm_t>& bgm, std::vector<rowed_free_t>& free_rows) -> bool {
    const auto place = [&](std::uint64_t row) { return after.position_at(row); };
    for (auto& item : free_rows) {
        const auto position = place_free_position(after, item);
        if (!position) return false;
        *item.position = *position;
    }
    const auto bucket_of = [&](measure_id_t id) -> measure_chunk_t& {
        return *chart.find_measure(id);
    };
    for (auto& item : notes) {
        const auto position = place(item.row);
        const auto end = item.end_row ? place(*item.end_row) : std::nullopt;
        if (!position || (item.end_row && !end)) return false;
        item.note.position = *position;
        item.note.end = end;
        bucket_of(position->measure).notes.push_back(std::move(item.note));
    }
    for (auto& item : bgm) {
        const auto position = place(item.row);
        const auto end = item.end_row ? place(*item.end_row) : std::nullopt;
        if (!position || (item.end_row && !end)) return false;
        item.event.position = *position;
        item.event.end = end;
        bucket_of(position->measure).bgm_events.push_back(std::move(item.event));
    }
    for (auto at = first_measure; at < chart.measures.size(); ++at) {
        std::ranges::stable_sort(chart.measures[at].notes, {}, [](const note_t& note) { return note.position.row; });
        std::ranges::stable_sort(chart.measures[at].bgm_events, {}, [](const bgm_event_t& event) { return event.position.row; });
    }
    return true;
}

struct measure_assignment_t {
    std::size_t index{};
    measure_definition_t definition;
};

// Gives each listed measure its definition, re-bucketing everything from the first one on so
// objects keep their rows from the start of the song.
auto resize_keeping_rows(chart_document_t& chart, std::span<const measure_assignment_t> assignments,
                         const measure_edit_limits_t& limits) -> bool {
    std::size_t measure_index = assignments.front().index;
    for (const auto& assignment : assignments) measure_index = std::min(measure_index, assignment.index);
    const auto old_index = measure_index_t::from(chart);
    if (!old_index) return false;
    std::vector<rowed_note_t> notes;
    std::vector<rowed_bgm_t> bgm;
    std::vector<rowed_free_t> free_rows;
    const auto& before = *old_index;
    if (!lift_bucketed_objects(chart, before, measure_index, notes, bgm) ||
        !collect_free_rows(chart, before, free_rows)) return false;
    for (const auto& assignment : assignments) chart.measures[assignment.index].definition = assignment.definition;
    std::uint64_t last_row = 0;
    for (const auto& item : notes) last_row = std::max({last_row, item.row, item.end_row.value_or(0)});
    for (const auto& item : bgm) last_row = std::max({last_row, item.row, item.end_row.value_or(0)});
    for (const auto& item : free_rows) last_row = std::max(last_row, item.row);
    // Appended measures are 4/4 at the chart resolution, so this terminates after at most
    // last_row / rows-per-measure steps, and the measure cap bounds it.
    auto index = measure_index_t::from(chart);
    while (index && index->total_rows <= last_row) {
        if (!append_default_measures(chart, chart.measures.size() + 1, limits)) return false;
        index = measure_index_t::from(chart);
    }
    if (!index) return false;
    return place_lifted_objects(chart, *index, measure_index, notes, bgm,
                               free_rows);
}

auto definition_for_rows(std::uint64_t rows, std::uint64_t resolution) -> measure_definition_t {
    const auto length = rows_to_fraction(rows, resolution);
    const auto meters = suggest_meters(length);
    return {length, meters.empty() ? std::nullopt : std::optional{meters.front()}};
}

auto measure_rows(const chart_document_t& chart, std::size_t index) -> std::uint64_t {
    return validate_measure(chart.divisions_per_standard_measure, chart.measures[index].definition)
        .row_capacity.value_or(0);
}

auto bar_line_index(const chart_document_t& chart, measure_id_t id)
    -> std::variant<std::size_t, diagnostic_t> {
    const auto* measure = chart.find_measure(id);
    if (measure == nullptr)
        return error(diagnostic_code_t::invalid_id, "bar line refers to an unknown measure");
    const auto index = static_cast<std::size_t>(measure - chart.measures.data());
    if (index == 0)
        return error(diagnostic_code_t::invalid_measure, "the chart's first bar line is fixed");
    return index;
}

// A lane layout change belongs to the measure it starts, so removing that bar line would leave it
// mid-measure. A moved bar line takes its layout change along instead.
auto removable_bar_line_index(const chart_document_t& chart, measure_id_t id)
    -> std::variant<std::size_t, diagnostic_t> {
    const local_position_t start{id, 0};
    if (std::ranges::any_of(chart.lane_layouts, [&](const auto& segment) { return segment.position == start; }))
        return error(diagnostic_code_t::invalid_measure, "a lane layout change starts at this bar line");
    return bar_line_index(chart, id);
}

// The chart positions of the listed bar lines in ascending order; duplicates reject.
template <typename index_of_t>
auto bar_line_indices(const chart_document_t& chart, std::span<const measure_id_t> measures,
                      index_of_t index_of) -> std::variant<std::vector<std::size_t>, diagnostic_list_t> {
    if (chart.bms_control_tree && chart.bms_control_tree->has_control_flow())
        return diagnostic_list_t{error(diagnostic_code_t::unsupported_feature,
                                       "editing a bar line requires resolved BMS conditional branches")};
    std::vector<std::size_t> indices;
    for (const auto id : measures) {
        auto index = index_of(chart, id);
        if (const auto* failure = std::get_if<diagnostic_t>(&index)) return diagnostic_list_t{*failure};
        indices.push_back(std::get<std::size_t>(index));
    }
    std::ranges::sort(indices);
    if (std::ranges::adjacent_find(indices) != indices.end())
        return diagnostic_list_t{error(diagnostic_code_t::duplicate_id, "bar line selection names a measure twice")};
    return indices;
}

// Each run of merged measures must still fit one measure's row bound.
auto has_oversized_merge(const chart_document_t& chart, std::span<const std::size_t> ascending,
                         const measure_edit_limits_t& limits) -> bool {
    std::uint64_t rows{};
    for (std::size_t at = 0; at < ascending.size(); ++at) {
        const auto index = ascending[at];
        const bool continues = at != 0 && ascending[at - 1] + 1 == index;
        if (!continues) rows = measure_rows(chart, index - 1);
        const auto added = measure_rows(chart, index);
        if (added > limits.maximum_rows_per_measure - std::min(rows, limits.maximum_rows_per_measure))
            return true;
        rows += added;
    }
    return false;
}

// The chart positions of the measures whose bar lines go, highest first so merging one never
// shifts the index of another.
auto plan_bar_line_removal(const chart_document_t& chart, std::span<const measure_id_t> measures,
                           const measure_edit_limits_t& limits)
    -> std::variant<std::vector<std::size_t>, diagnostic_list_t> {
    auto planned = bar_line_indices(chart, measures, removable_bar_line_index);
    if (std::holds_alternative<diagnostic_list_t>(planned)) return planned;
    auto& indices = std::get<std::vector<std::size_t>>(planned);
    if (has_oversized_merge(chart, indices, limits))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_measure,
                                       "the merged measure would exceed the measure row limit")};
    std::ranges::reverse(indices);
    return planned;
}

// Measure-start rows (and the chart end) after the listed starts move by `rows`.
struct measure_starts_t {
    std::vector<std::uint64_t> begins;
    std::uint64_t total{};
};

auto moved_row(std::uint64_t row, std::int64_t rows) -> std::optional<std::uint64_t> {
    if (rows >= 0) {
        const auto delta = static_cast<std::uint64_t>(rows);
        return row > std::numeric_limits<std::uint64_t>::max() - delta ? std::nullopt : std::optional{row + delta};
    }
    const auto magnitude = static_cast<std::uint64_t>(-(rows + 1)) + 1U;
    return magnitude > row ? std::nullopt : std::optional{row - magnitude};
}

auto plan_measure_starts(const measure_index_t& index, std::span<const std::size_t> moved,
                         std::int64_t rows, bool move_end) -> std::optional<measure_starts_t> {
    measure_starts_t starts{.begins = {}, .total = index.total_rows};
    for (const auto& entry : index.entries) starts.begins.push_back(entry.absolute_begin_row);
    for (const auto at : moved) {
        const auto row = moved_row(starts.begins[at], rows);
        if (!row) return std::nullopt;
        starts.begins[at] = *row;
    }
    if (move_end) {
        const auto total = moved_row(starts.total, rows);
        if (!total) return std::nullopt;
        starts.total = *total;
    }
    for (std::size_t at = 1; at < starts.begins.size(); ++at)
        if (starts.begins[at] <= starts.begins[at - 1]) return std::nullopt;
    if (starts.begins.empty() || starts.begins.back() >= starts.total) return std::nullopt;
    return starts;
}

auto starts_exceed_limit(const measure_starts_t& starts, const measure_edit_limits_t& limits) -> bool {
    for (std::size_t at = 0; at < starts.begins.size(); ++at) {
        const auto end = at + 1 < starts.begins.size() ? starts.begins[at + 1] : starts.total;
        if (end - starts.begins[at] > limits.maximum_rows_per_measure) return true;
    }
    return false;
}

// Gives every measure the length its new start rows imply, keeping each object's row.
auto set_measure_starts(chart_document_t& chart, const measure_index_t& before,
                        const measure_starts_t& starts) -> bool {
    std::vector<rowed_note_t> notes;
    std::vector<rowed_bgm_t> bgm;
    std::vector<rowed_free_t> free_rows;
    if (!lift_bucketed_objects(chart, before, 0, notes, bgm) ||
        !collect_free_rows(chart, before, free_rows)) return false;
    for (std::size_t at = 0; at < chart.measures.size(); ++at) {
        const auto end = at + 1 < starts.begins.size() ? starts.begins[at + 1] : starts.total;
        const auto rows = end - starts.begins[at];
        if (rows != before.entries[at].row_capacity)
            chart.measures[at].definition = definition_for_rows(rows, chart.divisions_per_standard_measure);
    }
    const auto after = measure_index_t::from(chart);
    return after && place_lifted_objects(chart, *after, 0, notes, bgm, free_rows);
}

// The positions where new bar lines go, latest first so splitting one never moves another. Rows
// that already start a measure, or end the chart, need no split.
auto plan_bar_line_splits(const measure_index_t& index, std::span<const std::uint64_t> rows)
    -> std::optional<std::vector<local_position_t>> {
    std::vector<local_position_t> splits;
    for (const auto row : rows) {
        if (row == index.total_rows) continue;
        const auto position = index.position_at(row);
        if (!position) return std::nullopt;
        if (position->row != 0) splits.push_back(*position);
    }
    std::ranges::sort(splits, std::ranges::greater{}, [&](const auto& position) { return *index.row_of(position); });
    const auto repeated = std::ranges::unique(splits);
    splits.erase(repeated.begin(), repeated.end());
    return splits;
}

// Splits each listed measure at its row, keeping every object's row.
auto split_measures_at(chart_document_t& chart, const measure_index_t& before,
                       std::span<const local_position_t> descending) -> bool {
    std::vector<rowed_note_t> notes;
    std::vector<rowed_bgm_t> bgm;
    std::vector<rowed_free_t> free_rows;
    if (!lift_bucketed_objects(chart, before, 0, notes, bgm) ||
        !collect_free_rows(chart, before, free_rows)) return false;
    for (const auto& position : descending) {
        const auto id = id_ceiling_t<measure_id_t>::from(chart).next();
        const auto* measure = chart.find_measure(position.measure);
        if (!id || measure == nullptr) return false;
        const auto at = static_cast<std::size_t>(measure - chart.measures.data());
        const auto rows = measure_rows(chart, at);
        chart.measures[at].definition = definition_for_rows(position.row, chart.divisions_per_standard_measure);
        chart.measures.insert(chart.measures.begin() + static_cast<std::ptrdiff_t>(at) + 1, measure_chunk_t{
            .id = *id, .definition = definition_for_rows(rows - position.row, chart.divisions_per_standard_measure)});
        shift_projections_for_insert(chart, at + 1);
    }
    const auto after = measure_index_t::from(chart);
    return after && place_lifted_objects(chart, *after, 0, notes, bgm, free_rows);
}

auto split_measure_at(chart_document_t& chart, local_position_t position,
                      const measure_edit_limits_t& limits) -> bool {
    const auto old_index = measure_index_t::from(chart);
    const auto target = chart.find_measure(position.measure);
    if (!old_index || target == nullptr || !old_index->row_of(position)) return false;
    const auto capacity = validate_measure(chart.divisions_per_standard_measure, target->definition);
    if (!capacity || position.row == 0 || position.row >= *capacity.row_capacity ||
        chart.measures.size() >= limits.maximum_measures ||
        std::max(position.row, *capacity.row_capacity - position.row) > limits.maximum_rows_per_measure) return false;
    return split_measures_at(chart, *old_index, std::span{&position, 1});
}

auto merge_measures_at(chart_document_t& chart, std::span<const std::size_t> descending) -> bool {
    const auto before = measure_index_t::from(chart);
    std::vector<rowed_note_t> notes;
    std::vector<rowed_bgm_t> bgm;
    std::vector<rowed_free_t> free_rows;
    if (!before || !lift_bucketed_objects(chart, *before, 0, notes, bgm) ||
        !collect_free_rows(chart, *before, free_rows)) return false;
    for (const auto at : descending) {
        const auto rows = measure_rows(chart, at - 1) + measure_rows(chart, at);
        chart.measures[at - 1].definition = definition_for_rows(rows, chart.divisions_per_standard_measure);
        chart.measures.erase(chart.measures.begin() + static_cast<std::ptrdiff_t>(at));
        shift_projections_for_delete(chart, at);
    }
    const auto after = measure_index_t::from(chart);
    return after && place_lifted_objects(chart, *after, 0, notes, bgm, free_rows);
}

void apply_measure_action(measure_edit_context_t& context, const split_measure_action_t& edit) {
    auto* chart = target_measure_chart(context, edit.chart);
    if (!chart) return;
    if (edit.position.row == 0) return;
    if (chart->bms_control_tree && chart->bms_control_tree->has_control_flow()) {
        reject(context.result, diagnostic_code_t::unsupported_feature,
               "measure splitting requires resolved BMS conditional branches");
        return;
    }
    if (!split_measure_at(*chart, edit.position, context.limits)) {
        reject(context.result, diagnostic_code_t::invalid_measure, "the measure could not be split at this row");
        return;
    }
    context.changed = true;
    context.label = "Split measure at cursor";
}

void apply_measure_action(measure_edit_context_t& context, const remove_bar_line_action_t& edit) {
    auto* chart = target_measure_chart(context, edit.chart);
    if (!chart) return;
    const std::array measures{edit.measure};
    auto diagnostics = remove_bar_lines(*chart, measures, context.limits);
    if (!diagnostics.empty()) {
        context.result.diagnostics.insert(context.result.diagnostics.end(),
                                          std::make_move_iterator(diagnostics.begin()),
                                          std::make_move_iterator(diagnostics.end()));
        return;
    }
    context.changed = true;
    context.label = "Remove bar line at cursor";
}

// A Stop follows events at its row; a Delay precedes them. The removed
// interval is (begin,end] for Stop and [begin,end) for Delay, so an object
// never silently changes sides of the pause when the two boundaries meet.
struct pause_range_t {
    std::uint64_t begin{}, end{};
    pause_kind_t kind{};
    auto has_row(std::uint64_t row) const -> bool {
        return kind == pause_kind_t::stop ? row > begin && row <= end
                                         : row >= begin && row < end;
    }
    auto shifted(std::uint64_t row) const -> std::uint64_t {
        return row >= end ? row - (end - begin) : row;
    }
};

auto conversion_row(const measure_index_t& index, local_position_t position,
                    bool exclusive_end = false) -> std::optional<std::uint64_t> {
    if (exclusive_end && !index.entries.empty()) {
        const auto& last = index.entries.back();
        if (position.measure == last.id && position.row == last.row_capacity) return index.total_rows;
    }
    return index.row_of(position);
}

auto is_conversion_configuration(const chart_document_t& chart, const local_position_t* position) -> bool {
    return std::ranges::any_of(chart.bpms, [position](const auto& event) { return &event.position == position; });
}

auto is_conversion_timing_blocked(const chart_document_t& chart, const measure_index_t& index,
                               const pause_range_t& range) -> bool {
    const auto closed = [&](local_position_t position) {
        const auto row = index.row_of(position);
        return !row || (*row >= range.begin && *row <= range.end);
    };
    const auto bpm = std::ranges::any_of(chart.bpms, [&](const auto& event) {
        const auto row = index.row_of(event.position);
        return !row || (*row > range.begin && *row <= range.end);
    });
    const auto warp = std::ranges::any_of(chart.warps, [&](const auto& event) {
        const auto first = index.row_of(event.span.begin);
        const auto last = index.row_of(event.span.end);
        return !first || !last || (*first <= range.end && *last >= range.begin);
    });
    return bpm || warp || std::ranges::any_of(chart.pauses, [&](const auto& event) { return closed(event.position); });
}

auto is_conversion_points_clear(const chart_document_t& chart, const measure_index_t& index,
                             const pause_range_t& range) -> bool {
    bool clear = true;
    const auto visit = [&](const local_position_t& position, position_role_t) {
        const auto row = index.row_of(position);
        if (!row || (range.has_row(*row) && !is_conversion_configuration(chart, &position))) clear = false;
    };
    for_each_free_position(chart, visit);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) visit(note.position, position_role_t::point);
        for (const auto& bgm : measure.bgm_events) visit(bgm.position, position_role_t::point);
    }
    for (const auto& layout : chart.lane_layouts) {
        const auto row = index.row_of(layout.position);
        if (!row || (*row > range.begin && *row < range.end)) clear = false;
    }
    for (const auto& recipe : chart.sv_recipes) {
        const auto first = conversion_row(index, recipe.range.begin);
        const auto last = conversion_row(index, recipe.range.end, true);
        if (!first || !last || range.has_row(*first) || range.has_row(*last)) clear = false;
    }
    return clear;
}

using timing_arithmetic::rows_to_beat_fraction;

// Geometry and object movement use the same lift/place machinery as measure
// resizing. Every later endpoint moves by the same absolute-row displacement.
auto shorten_conversion_measures(chart_document_t& chart, const measure_index_t& before,
                                 const pause_range_t& range) -> bool {
    for (std::size_t at = before.entries.size(); at-- > 0;) {
        const auto& entry = before.entries[at];
        const auto first = std::max(range.begin, entry.absolute_begin_row);
        const auto last = std::min(range.end, entry.absolute_begin_row + entry.row_capacity);
        if (first >= last) continue;
        const auto capacity = entry.row_capacity - (last - first);
        if (capacity == 0) {
            chart.measures.erase(chart.measures.begin() + static_cast<std::ptrdiff_t>(at));
            shift_projections_for_delete(chart, at);
        } else {
            chart.measures[at].definition = definition_for_rows(capacity, chart.divisions_per_standard_measure);
        }
    }
    return !chart.measures.empty();
}

auto collect_conversion_extras(chart_document_t& chart, const measure_index_t& before,
                               std::vector<rowed_free_t>& positions) -> bool {
    const auto add = [&](local_position_t& position, bool boundary = false) {
        const auto row = conversion_row(before, position, boundary);
        if (!row) return false;
        positions.push_back({&position, *row, boundary});
        return true;
    };
    for (auto& layout : chart.lane_layouts) if (!add(layout.position)) return false;
    for (auto& recipe : chart.sv_recipes)
        if (!add(recipe.range.begin) || !add(recipe.range.end, true)) return false;
    return true;
}

auto is_conversion_times_match(const timing_index_t& before, const timing_index_t& after,
                            std::span<const std::uint64_t> rows, const pause_range_t& range) -> bool {
    for (const auto row : rows) {
        if (row == range.begin && range.kind == pause_kind_t::delay) continue; // configuration only
        const auto old_time = before.seconds_at(row);
        const auto new_time = after.seconds_at(range.shifted(row));
        if (!old_time || !new_time || old_time.seconds != new_time.seconds) return false;
    }
    return true;
}

auto conversion_transition_rows(rational_t beats, std::uint64_t resolution)
    -> std::optional<std::uint64_t> {
    auto first = beats.numerator, second = resolution, divisor = beats.denominator;
    std::uint64_t four = 4;
    if (divisor == 0 || second == 0) return std::nullopt;
    const auto cancel = [](auto& numerator, auto& denominator) {
        const auto gcd = std::gcd(numerator, denominator);
        numerator /= gcd; denominator /= gcd;
    };
    cancel(first, divisor); cancel(first, four); cancel(second, divisor); cancel(second, four);
    if (divisor != 1 || four != 1 || (second != 0 && first > std::numeric_limits<std::uint64_t>::max() / second))
        return std::nullopt;
    return first * second;
}

auto adjust_conversion_transitions(chart_document_t& chart, const measure_index_t& index,
    const pause_range_t& range, std::vector<std::uint64_t>& timed_rows) -> bool {
    for (auto& speed : chart.speeds) {
        if (speed.transition_is_seconds || speed.transition_beats.numerator == 0) continue;
        const auto start = index.row_of(speed.position);
        const auto length = conversion_transition_rows(speed.transition_beats, chart.divisions_per_standard_measure);
        if (!start || !length || *length > std::numeric_limits<std::uint64_t>::max() - *start) return false;
        const auto end = *start + *length;
        if (range.has_row(end)) return false;
        if (*start >= range.end || end < range.begin) continue;
        const auto beats = rows_to_beat_fraction(range.shifted(end) - range.shifted(*start),
                                             chart.divisions_per_standard_measure);
        if (!beats) return false;
        speed.transition_beats = {static_cast<std::uint64_t>(beats->numerator), beats->denominator};
        if (end <= index.total_rows) timed_rows.push_back(end);
    }
    return true;
}

auto convert_pause_geometry(chart_document_t& chart, const measure_index_t& before,
                            const pause_range_t& range, bool resize, pause_event_t pause) -> bool {
    const auto timing = timing_index_t::from(chart);
    if (!timing.value) return false;
    std::vector<rowed_note_t> notes;
    std::vector<rowed_bgm_t> bgm;
    std::vector<rowed_free_t> positions;
    std::vector<std::uint64_t> rows;
    if (!adjust_conversion_transitions(chart, before, range, rows)) return false;
    if (!lift_bucketed_objects(chart, before, 0, notes, bgm) ||
        !collect_free_rows(chart, before, positions) || !collect_conversion_extras(chart, before, positions)) return false;
    const auto move = [&](auto& objects) {
        for (auto& object : objects) {
            rows.push_back(object.row);
            object.row = range.shifted(object.row);
            if (object.end_row) {
                rows.push_back(*object.end_row);
                object.end_row = range.shifted(*object.end_row);
            }
        }
    };
    move(notes); move(bgm);
    for (auto& position : positions) { rows.push_back(position.row); position.row = range.shifted(position.row); }
    if (resize && !shorten_conversion_measures(chart, before, range)) return false;
    const auto built = measure_index_t::from(chart);
    if (!built || !place_lifted_objects(chart, *built, 0, notes, bgm, positions)) return false;
    const auto start = built->position_at(range.begin);
    if (!start) return false;
    pause.position = *start;
    chart.pauses.push_back(std::move(pause));
    const auto new_timing = timing_index_t::from(chart);
    return new_timing.value && is_conversion_times_match(*timing.value, *new_timing.value, rows, range);
}

void apply_measure_action(measure_edit_context_t& context, const convert_range_to_pause_action_t& edit) {
    auto* chart = target_measure_chart(context, edit.chart);
    if (!chart) return;
    const auto built = measure_index_t::from(*chart);
    const auto first = built ? built->row_of(edit.range.begin) : std::nullopt;
    const auto last = built ? conversion_row(*built, edit.range.end, true) : std::nullopt;
    if (!first || !last || *first >= *last ||
        (edit.kind != pause_kind_t::stop && edit.kind != pause_kind_t::delay)) {
        reject(context.result, diagnostic_code_t::invalid_measure, "Select a nonempty authored row range.");
        return;
    }
    const pause_range_t range{*first, *last, edit.kind};
    if ((chart->bms_control_tree && chart->bms_control_tree->has_control_flow()) ||
        is_conversion_timing_blocked(*chart, *built, range)) {
        reject(context.result, diagnostic_code_t::unsupported_feature,
               "The range crosses BPM, pause, warp or retained BMS branch timing; choose an uninterrupted interval.");
        return;
    }
    if (!is_conversion_points_clear(*chart, *built, range)) {
        reject(context.result, diagnostic_code_t::invalid_measure,
               edit.kind == pause_kind_t::stop
                   ? "Stop conversion needs an empty interval after its start, including its end; use Delay for an object at the end."
                   : "Delay conversion needs an empty interval including its start; use Stop for an object at the start.");
        return;
    }
    const bool bms_ticks = chart->source_family == format_family_t::bms && edit.kind == pause_kind_t::stop;
    const auto duration = rows_to_beat_fraction(*last - *first, chart->divisions_per_standard_measure, bms_ticks ? 192 : 4);
    const auto id = next_chart_object_id(*chart).id;
    if (!duration || !id || !convert_pause_geometry(*chart, *built, range, edit.resize_measures,
            {.id = id.value_or(object_id_t{}), .kind = edit.kind,
             .duration = duration.value_or(signed_rational_t{}),
             .source_unit = bms_ticks ? pause_unit_t::bms_stop_ticks : pause_unit_t::beats})) {
        reject(context.result, diagnostic_code_t::invalid_measure,
               "The range cannot be converted without changing an object's song time or losing measure geometry.");
        return;
    }
    context.changed = true;
    context.label = edit.kind == pause_kind_t::stop ? "Convert range to Stop" : "Convert range to Delay";
}

auto apply_measure_action(measure_edit_context_t& context,
                          const resize_measure_keep_rows_action_t& edit) -> void {
    auto* chart = target_measure_chart(context, edit.chart);
    if (chart == nullptr) {
        return;
    }
    if (edit.measure_indices.empty()) {
        reject(context.result, diagnostic_code_t::invalid_measure, "no measures were selected");
        return;
    }
    if (!validate_definition(*chart, edit.definition, context.limits, context.result)) {
        return;
    }
    const auto highest = *std::ranges::max_element(edit.measure_indices);
    if (highest >= chart->measures.size()) {
        if (!has_chart_virtual_tail(*chart) || highest >= virtual_tail_measure_limit(*chart) ||
            !append_default_measures(*chart, highest + 1, context.limits)) {
            reject(context.result, diagnostic_code_t::invalid_measure,
                   "the measure lies beyond the chart and its virtual canvas");
            return;
        }
        context.changed = true;
    }
    auto sorted = edit.measure_indices;
    std::ranges::sort(sorted);
    if (std::ranges::adjacent_find(sorted) != sorted.end()) {
        reject(context.result, diagnostic_code_t::invalid_measure,
               "measure selection names a measure twice");
        return;
    }
    for (const auto at : sorted) {
        const auto definition = edit.definition_for(chart->measures[at].definition);
        if (measure_definition_equal(chart->measures[at].definition, definition)) continue;
        const measure_assignment_t assignment{at, definition};
        if (!resize_keeping_rows(*chart, std::span{&assignment, 1}, context.limits)) {
            reject(context.result, diagnostic_code_t::invalid_measure,
                   "the measure cannot be resized without losing objects beyond the chart");
            return;
        }
        context.changed = true;
    }
    // Shortening measures can leave the chart ending before the music does; native charts grow
    // back to cover the longest linked audio, as after a timing edit.
    if (!extend_native_charts_to_audio_end(context.proposed)) {
        reject(context.result, diagnostic_code_t::invalid_resolution,
               "the chart cannot be extended to cover the audio after this resize");
        return;
    }
    context.label = "Resize measures";
}

auto apply_measure_action(measure_edit_context_t& context,
                          const move_bar_line_action_t& edit) -> void {
    auto* chart = target_measure_chart(context, edit.chart);
    if (chart == nullptr) {
        return;
    }
    const auto second = edit.measure_index + 1;
    if (second >= chart->measures.size()) {
        if (!has_chart_virtual_tail(*chart) || second >= virtual_tail_measure_limit(*chart) ||
            !append_default_measures(*chart, second + 1, context.limits)) {
            reject(context.result, diagnostic_code_t::invalid_measure,
                   "the bar line lies beyond the chart and its virtual canvas");
            return;
        }
        context.changed = true;
    }
    if (!validate_definition(*chart, edit.before, context.limits, context.result) ||
        !validate_definition(*chart, edit.after, context.limits, context.result)) {
        return;
    }
    const auto old_rows = validate_measure(chart->divisions_per_standard_measure,
                                           chart->measures[edit.measure_index].definition).row_capacity.value_or(0) +
                          validate_measure(chart->divisions_per_standard_measure,
                                           chart->measures[second].definition).row_capacity.value_or(0);
    const auto new_rows = validate_measure(chart->divisions_per_standard_measure, edit.before).row_capacity.value_or(0) +
                          validate_measure(chart->divisions_per_standard_measure, edit.after).row_capacity.value_or(0);
    if (old_rows != new_rows) {
        reject(context.result, diagnostic_code_t::invalid_measure,
               "moving a bar line must keep the two measures' combined length");
        return;
    }
    if (measure_definition_equal(chart->measures[edit.measure_index].definition, edit.before) &&
        measure_definition_equal(chart->measures[second].definition, edit.after)) {
        return;
    }
    const std::array assignments{measure_assignment_t{edit.measure_index, edit.before},
                                 measure_assignment_t{second, edit.after}};
    if (!resize_keeping_rows(*chart, assignments, context.limits)) {
        reject(context.result, diagnostic_code_t::invalid_measure,
               "the bar line cannot be moved without losing objects");
        return;
    }
    context.label = "Move bar line";
    context.changed = true;
}

auto apply_measure_action(measure_edit_context_t& context,
                          const materialize_measures_action_t& edit) -> void {
    auto* chart = target_measure_chart(context, edit.chart);
    if (chart == nullptr || edit.count <= chart->measures.size()) {
        return;
    }
    if (!has_chart_virtual_tail(*chart) || edit.count > virtual_tail_measure_limit(*chart) ||
        !append_default_measures(*chart, edit.count, context.limits)) {
        reject(context.result, diagnostic_code_t::invalid_measure,
               "the measure lies beyond the chart and its virtual canvas");
        return;
    }
    context.label = "Create measures";
    context.changed = true;
}

auto apply_measure_action(measure_edit_context_t& context,
                          const delete_measure_action_t& edit) -> void {
    auto* chart = target_measure_chart(context, edit.chart);
    if (chart == nullptr) {
        return;
    }
    const auto measure = chart->unique_target(&chart_document_t::measures, edit.measure,
                                               context.result.diagnostics, measure_target_policy);
    if (measure == nullptr) {
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
        static_cast<std::size_t>(measure - chart->measures.data());
    chart->measures.erase(chart->measures.begin() + (measure - chart->measures.data()));
    shift_projections_for_delete(*chart, removed_index);
    context.label = "Delete measure";
    context.changed = true;
}

auto apply_measure_action(measure_edit_context_t& context,
                          const remove_measure_with_contents_action_t& edit) -> void {
    auto* chart = target_measure_chart(context, edit.chart);
    if (chart == nullptr) {
        return;
    }
    auto proposal = remove_measure_contents(*chart, edit.measure);
    if (!proposal) {
        for (auto& diagnostic : proposal.diagnostics) {
            context.result.diagnostics.push_back(std::move(diagnostic));
        }
        return;
    }
    context.label = "Remove measure with contents";
    context.changed = true;
}

} // namespace

auto has_measure_positioned_objects(const chart_document_t& chart, measure_id_t measure) -> bool {
    if (std::ranges::any_of(chart.lane_layouts, [&](const auto& segment) {
            return segment.position.measure == measure;
        }))
        return true;
    bool found = false;
    for_each_free_position(chart, [&](const local_position_t& position, position_role_t) {
        found = found || position.measure == measure;
    });
    return found;
}

auto rebar_standard_measures(chart_document_t& chart, measure_edit_limits_t limits) -> diagnostic_list_t {
    const auto before = measure_index_t::from(chart);
    if (!before) return before.error();
    if (!before->total_rows || !chart.divisions_per_standard_measure ||
        chart.divisions_per_standard_measure > limits.maximum_rows_per_measure)
        return {error(diagnostic_code_t::invalid_measure, "Standard export bars exceed the exact row capacity.")};
    const auto count = (before->total_rows - 1) / chart.divisions_per_standard_measure + 1;
    if (count > limits.maximum_measures)
        return {error(diagnostic_code_t::invalid_measure, "Standard export bars exceed the measure capacity.")};
    auto working = chart;
    std::vector<measure_assignment_t> assignments;
    for (std::size_t index = 0; index < working.measures.size(); ++index)
        assignments.push_back({index, {{1, 1}, meter_t{4, 4}}});
    if (!resize_keeping_rows(working, assignments, limits) || !append_default_measures(working, count, limits))
        return {error(diagnostic_code_t::invalid_measure, "Standard export bars cannot preserve all exact object rows.")};
    working.measures.resize(count);
    working.bms_measure_length_projections.clear();
    working.indices_slot = {};
    chart = std::move(working);
    return {};
}

auto remove_bar_lines(chart_document_t& chart, std::span<const measure_id_t> measures,
                      measure_edit_limits_t limits) -> diagnostic_list_t {
    auto planned = plan_bar_line_removal(chart, measures, limits);
    if (auto* diagnostics = std::get_if<diagnostic_list_t>(&planned)) return std::move(*diagnostics);
    if (!merge_measures_at(chart, std::get<std::vector<std::size_t>>(planned)))
        return {error(diagnostic_code_t::invalid_measure,
                      "the bar lines could not be removed without moving an object")};
    return {};
}

auto add_bar_lines(chart_document_t& chart, std::span<const std::uint64_t> rows,
                   measure_edit_limits_t limits) -> diagnostic_list_t {
    if (rows.empty()) return {};
    if (chart.bms_control_tree && chart.bms_control_tree->has_control_flow())
        return {error(diagnostic_code_t::unsupported_feature,
                      "adding a bar line requires resolved BMS conditional branches")};
    const auto before = measure_index_t::from(chart);
    if (!before) return before.error();
    const auto splits = plan_bar_line_splits(*before, rows);
    if (!splits)
        return {error(diagnostic_code_t::row_out_of_range, "a bar line would fall past the chart's end")};
    if (splits->empty()) return {};
    if (splits->size() > limits.maximum_measures - std::min(limits.maximum_measures, chart.measures.size()))
        return {error(diagnostic_code_t::invalid_measure, "the chart would exceed the measure limit")};
    auto working = chart;
    if (!split_measures_at(working, *before, *splits))
        return {error(diagnostic_code_t::invalid_measure,
                      "the bar lines could not be added without moving an object")};
    chart = std::move(working);
    return {};
}

auto move_bar_lines(chart_document_t& chart, std::span<const measure_id_t> measures, std::int64_t rows,
                    bool move_end, measure_edit_limits_t limits) -> diagnostic_list_t {
    if (rows == 0 || (measures.empty() && !move_end)) return {};
    auto planned = bar_line_indices(chart, measures, bar_line_index);
    if (auto* diagnostics = std::get_if<diagnostic_list_t>(&planned)) return std::move(*diagnostics);
    const auto before = measure_index_t::from(chart);
    if (!before) return before.error();
    const auto starts = plan_measure_starts(*before, std::get<std::vector<std::size_t>>(planned), rows, move_end);
    if (!starts)
        return {error(diagnostic_code_t::invalid_measure,
                      "a bar line cannot cross another bar line or the chart's start or end")};
    if (starts_exceed_limit(*starts, limits))
        return {error(diagnostic_code_t::invalid_measure, "a measure would exceed the measure row limit")};
    if (!set_measure_starts(chart, *before, *starts))
        return {error(diagnostic_code_t::row_out_of_range,
                      "moving the bar lines would leave an object outside the chart")};
    return {};
}

auto propose_measure_removal(const chart_document_t& chart, measure_id_t measure)
    -> measure_removal_proposal_t {
    auto scratch = chart;
    return remove_measure_contents(scratch, measure);
}

auto stage_scoped_measure_edit(document_command_transaction_t& transaction, chart_id_t id,
    const measure_edit_action_t& action, measure_edit_limits_t limits) -> attempt_t<document_edit_effect_t> {
    return transaction.stage_bms_structure_edit(id, [&](chart_document_t& chart) -> attempt_t<document_edit_effect_t> {
        auto algorithm_document = transaction.current_document();
        algorithm_document.charts = {chart};
        document_command_result_t result;
        measure_edit_context_t context{.proposed = algorithm_document, .limits = limits, .result = result};
        std::visit([&](const auto& edit) { apply_measure_action(context, edit); }, action);
        if (!result.diagnostics.empty()) return {.diagnostics = std::move(result.diagnostics)};
        chart = std::move(algorithm_document.charts.front());
        return {.value = document_edit_effect_t{.changed = context.changed, .command_label = std::move(context.label)}};
    });
}

auto apply_measure_edit(document_history_t& history,
                        const measure_edit_action_t& action, measure_edit_limits_t limits)
    -> document_command_result_t {
    auto transaction = document_command_transaction_t(history);
    auto& result = transaction.outcome();
    if (limits.maximum_measures == 0 || limits.maximum_rows_per_measure == 0) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "measure editor bounds must be positive");
        return result;
    }

    const auto id = std::visit([](const auto& edit) { return edit.chart; }, action);
    if (transaction.has_bms_program(id)) return transaction.finish_execution(
        stage_scoped_measure_edit(transaction, id, action, limits), command_noop_policy_t::accept_without_diagnostics);

    auto& proposed = transaction.document();
    measure_edit_context_t context{.proposed = proposed,
                                   .limits = limits,
                                   .result = result,
                                   .label = {},
                                   .changed = false};
    std::visit([&context](const auto& edit) { apply_measure_action(context, edit); }, action);

    if (!result.diagnostics.empty() || !context.changed) {
        return result;
    }
    return transaction.commit(std::move(context.label));
}

auto extend_chart_to_row(chart_document_t& chart, std::uint64_t row, measure_edit_limits_t limits) -> bool {
    const auto index = measure_index_t::from(chart);
    if (!index || chart.divisions_per_standard_measure == 0) return false;
    if (row < index->total_rows) return true;
    if (chart.divisions_per_standard_measure > limits.maximum_rows_per_measure) return false;
    const auto extra = (row - index->total_rows) / chart.divisions_per_standard_measure + 1;
    if (chart.measures.size() > limits.maximum_measures || extra > limits.maximum_measures - chart.measures.size()) return false;
    return append_default_measures(chart, chart.measures.size() + static_cast<std::size_t>(extra), limits);
}

auto extend_chart_to_audio_end(chart_document_t& chart, const audio_stream_info_t& stream,
                               measure_edit_limits_t limits) -> bool {
    if (!stream.frame_count || stream.sample_rate == 0) return true;
    const auto timing = timing_index_t::from(chart);
    if (!timing.value || timing.value->rows.empty()) return false;
    const auto& end = timing.value->rows.back();
    const auto end_seconds = to_long_double(end.after_stops_seconds);
    const auto audio_seconds = static_cast<long double>(*stream.frame_count) / stream.sample_rate;
    if (audio_seconds <= end_seconds) return true;
    const auto bpm = to_long_double(end.effective_bpm);
    const auto extra = std::ceil((audio_seconds - end_seconds) * bpm / 240.0L);
    if (!std::isfinite(extra) || extra < 1 || chart.measures.size() > limits.maximum_measures ||
        extra > limits.maximum_measures - chart.measures.size() ||
        chart.divisions_per_standard_measure > limits.maximum_rows_per_measure) return false;
    const auto count = static_cast<std::size_t>(extra);
    const auto first = id_ceiling_t<measure_id_t>::from(chart).next(count);
    if (!first) return false;
    chart.measures.reserve(chart.measures.size() + count);
    for (std::size_t index = 0; index < count; ++index)
        chart.measures.push_back({.id = measure_id_t{first->value + index}});
    return true;
}

auto extend_native_charts_to_audio_end(song_document_t& document) -> bool {
    const auto* longest = document.longest_music_or_bgm_stream();
    if (!longest) return true;
    for (auto& chart : document.charts) {
        if (chart.source_family == format_family_t::native_project &&
            !extend_chart_to_audio_end(chart, *longest)) return false;
    }
    return true;
}

} // namespace wormedit

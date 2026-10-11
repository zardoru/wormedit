#include <wormedit/core/measure_editor.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/virtual_canvas.h>

#include <algorithm>
#include <array>
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

enum class free_position_kind_t { hold_end, other };

// Visits every positioned object that is stored outside the per-measure note and
// BGM buckets, plus the hold ends of the bucketed notes.
template <typename Chart, typename Visit>
void for_each_free_position(Chart& chart, Visit&& visit) {
    const auto ends = [&](auto& objects) {
        for (auto& object : objects)
            if (object.end) visit(*object.end, free_position_kind_t::hold_end);
    };
    for (auto& measure : chart.measures) {
        ends(measure.notes);
        ends(measure.bgm_events);
    }
    const auto other = [&](auto& values) {
        for (auto& value : values) visit(value.position, free_position_kind_t::other);
    };
    other(chart.bpms);
    other(chart.pauses);
    other(chart.scrolls);
    other(chart.speeds);
    other(chart.kiai);
    other(chart.timing_anchors);
    other(chart.bga_events);
    for (auto& warp : chart.warps) {
        visit(warp.span.begin, free_position_kind_t::other);
        visit(warp.span.end, free_position_kind_t::other);
    }
    for (auto& bookmark : chart.bookmarks) {
        if (bookmark.position) visit(*bookmark.position, free_position_kind_t::other);
    }
    for (auto& section : chart.sections) {
        if (!section.beat_span) continue;
        visit(section.beat_span->begin, free_position_kind_t::other);
        visit(section.beat_span->end, free_position_kind_t::other);
    }
    for (auto& channel : chart.sound_channels) {
        for (auto& event : channel.events) {
            visit(event.position, free_position_kind_t::other);
            if (event.end) visit(*event.end, free_position_kind_t::other);
        }
    }
}

auto retains_bms_control_flow(const chart_document_t& chart) -> bool {
    return chart.bms_control_tree.has_value() || !chart.bms_bpm_projections.empty() ||
           !chart.bms_pause_projections.empty();
}

// Rejects removals that would silently drop linked or branch-owned content.
auto removal_blocker(const chart_document_t& chart, const measure_chunk_t& measure)
    -> const char* {
    if (retains_bms_control_flow(chart))
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
    for_each_free_position(chart, [&](local_position_t& position, free_position_kind_t kind) {
        if (position.measure != removed) return;
        if (!next) {
            proposal.diagnostics.push_back(error(
                diagnostic_code_t::invalid_measure,
                "the last measure holds positioned objects that have no later measure to move to"));
            return;
        }
        if (kind == free_position_kind_t::hold_end && position.row > 0)
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
    const auto found = std::ranges::find(chart.measures, id, &measure_chunk_t::id);
    if (!id || found == chart.measures.end()) {
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
    const auto index = static_cast<std::size_t>(std::distance(chart.measures.begin(), found));
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
        const auto measure = unique_measure(*chart, id, context.result);
        if (measure == chart->measures.end()) {
            return;
        }
        if (!same_definition(measure->definition, edit.definition)) {
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
    while (chart.measures.size() < count) {
        const auto id = next_measure_id(chart);
        if (!id) return false;
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
            const auto row = absolute_row(index, note.position);
            const auto end = note.end ? absolute_row(index, *note.end) : std::nullopt;
            if (!row || (note.end && !end)) return false;
            notes.push_back({std::move(note), *row, end});
        }
        for (auto& event : measure.bgm_events) {
            const auto row = absolute_row(index, event.position);
            const auto end = event.end ? absolute_row(index, *event.end) : std::nullopt;
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
    for_each_free_position(chart, [&](local_position_t& position, free_position_kind_t) {
        const auto row = absolute_row(index, position);
        if (!row) ok = false;
        else free_rows.push_back({&position, *row});
    });
    return ok;
}

auto place_free_position(const measure_index_t& index, const rowed_free_t& item)
    -> std::optional<local_position_t> {
    if (item.exclusive_end && item.row == index.total_rows && !index.entries.empty())
        return local_position_t{index.entries.back().id, index.entries.back().row_capacity};
    return position_at_absolute_row(index, item.row);
}

// Writes each lifted object back at the position its row now has.
auto place_lifted_objects(chart_document_t& chart, const measure_index_t& after,
                          std::size_t first_measure, std::vector<rowed_note_t>& notes,
                          std::vector<rowed_bgm_t>& bgm, std::vector<rowed_free_t>& free_rows) -> bool {
    const auto place = [&](std::uint64_t row) { return position_at_absolute_row(after, row); };
    for (auto& item : free_rows) {
        const auto position = place_free_position(after, item);
        if (!position) return false;
        *item.position = *position;
    }
    const auto bucket_of = [&](measure_id_t id) -> measure_chunk_t& {
        return *std::ranges::find(chart.measures, id, &measure_chunk_t::id);
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
    const auto old_index = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(old_index)) return false;
    std::vector<rowed_note_t> notes;
    std::vector<rowed_bgm_t> bgm;
    std::vector<rowed_free_t> free_rows;
    const auto& before = std::get<measure_index_t>(old_index);
    if (!lift_bucketed_objects(chart, before, measure_index, notes, bgm) ||
        !collect_free_rows(chart, before, free_rows)) return false;
    for (const auto& assignment : assignments) chart.measures[assignment.index].definition = assignment.definition;
    std::uint64_t last_row = 0;
    for (const auto& item : notes) last_row = std::max({last_row, item.row, item.end_row.value_or(0)});
    for (const auto& item : bgm) last_row = std::max({last_row, item.row, item.end_row.value_or(0)});
    for (const auto& item : free_rows) last_row = std::max(last_row, item.row);
    // Appended measures are 4/4 at the chart resolution, so this terminates after at most
    // last_row / rows-per-measure steps, and the measure cap bounds it.
    auto index = build_measure_index(chart);
    while (std::holds_alternative<measure_index_t>(index) &&
           std::get<measure_index_t>(index).total_rows <= last_row) {
        if (!append_default_measures(chart, chart.measures.size() + 1, limits)) return false;
        index = build_measure_index(chart);
    }
    if (!std::holds_alternative<measure_index_t>(index)) return false;
    return place_lifted_objects(chart, std::get<measure_index_t>(index), measure_index, notes, bgm,
                               free_rows);
}

auto definition_for_rows(std::uint64_t rows, std::uint64_t resolution) -> measure_definition_t {
    const auto divisor = std::gcd(rows, resolution);
    const rational_t length{rows / divisor, resolution / divisor};
    const auto meters = suggest_meters(length);
    return {length, meters.empty() ? std::nullopt : std::optional{meters.front()}};
}

auto split_measure_at(chart_document_t& chart, local_position_t position,
                      const measure_edit_limits_t& limits) -> bool {
    const auto old_index = build_measure_index(chart);
    const auto* before = std::get_if<measure_index_t>(&old_index);
    const auto target = std::ranges::find(chart.measures, position.measure, &measure_chunk_t::id);
    if (!before || target == chart.measures.end() || !absolute_row(*before, position)) return false;
    const auto capacity = validate_measure(chart.divisions_per_standard_measure, target->definition);
    if (!capacity || position.row == 0 || position.row >= *capacity.row_capacity ||
        chart.measures.size() >= limits.maximum_measures ||
        std::max(position.row, *capacity.row_capacity - position.row) > limits.maximum_rows_per_measure) return false;
    const auto id = next_measure_id(chart);
    if (!id) return false;
    const auto at = static_cast<std::size_t>(target - chart.measures.begin());
    std::vector<rowed_note_t> notes;
    std::vector<rowed_bgm_t> bgm;
    std::vector<rowed_free_t> free_rows;
    if (!lift_bucketed_objects(chart, *before, 0, notes, bgm) ||
        !collect_free_rows(chart, *before, free_rows)) return false;
    chart.measures[at].definition = definition_for_rows(position.row, chart.divisions_per_standard_measure);
    chart.measures.insert(chart.measures.begin() + at + 1, measure_chunk_t{
        .id = *id, .definition = definition_for_rows(*capacity.row_capacity - position.row,
                                                    chart.divisions_per_standard_measure)});
    shift_projections_for_insert(chart, at + 1);
    const auto new_index = build_measure_index(chart);
    const auto* after = std::get_if<measure_index_t>(&new_index);
    return after && place_lifted_objects(chart, *after, 0, notes, bgm, free_rows);
}

auto has_conditional_measures(const bms_control_node_t& node) -> bool {
    if (node.kind != bms_control_kind_t::root && node.kind != bms_control_kind_t::raw_statement) return true;
    return std::ranges::any_of(node.children, has_conditional_measures);
}

void apply_measure_action(measure_edit_context_t& context, const split_measure_action_t& edit) {
    auto* chart = target_measure_chart(context, edit.chart);
    if (!chart) return;
    if (edit.position.row == 0) return;
    if (chart->bms_control_tree && has_conditional_measures(*chart->bms_control_tree)) {
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

// A Stop follows events at its row; a Delay precedes them. The removed
// interval is (begin,end] for Stop and [begin,end) for Delay, so an object
// never silently changes sides of the pause when the two boundaries meet.
struct pause_range_t {
    std::uint64_t begin{}, end{};
    pause_kind_t kind{};
    auto contains(std::uint64_t row) const -> bool {
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
    return absolute_row(index, position);
}

auto conversion_configuration(const chart_document_t& chart, const local_position_t* position) -> bool {
    return std::ranges::any_of(chart.bpms, [position](const auto& event) { return &event.position == position; });
}

auto conversion_timing_blocked(const chart_document_t& chart, const measure_index_t& index,
                               const pause_range_t& range) -> bool {
    const auto closed = [&](local_position_t position) {
        const auto row = absolute_row(index, position);
        return !row || (*row >= range.begin && *row <= range.end);
    };
    const auto bpm = std::ranges::any_of(chart.bpms, [&](const auto& event) {
        const auto row = absolute_row(index, event.position);
        return !row || (*row > range.begin && *row <= range.end);
    });
    const auto warp = std::ranges::any_of(chart.warps, [&](const auto& event) {
        const auto first = absolute_row(index, event.span.begin);
        const auto last = absolute_row(index, event.span.end);
        return !first || !last || (*first <= range.end && *last >= range.begin);
    });
    return bpm || warp || std::ranges::any_of(chart.pauses, [&](const auto& event) { return closed(event.position); });
}

auto conversion_points_clear(const chart_document_t& chart, const measure_index_t& index,
                             const pause_range_t& range) -> bool {
    bool clear = true;
    const auto visit = [&](const local_position_t& position, free_position_kind_t) {
        const auto row = absolute_row(index, position);
        if (!row || (range.contains(*row) && !conversion_configuration(chart, &position))) clear = false;
    };
    for_each_free_position(chart, visit);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) visit(note.position, free_position_kind_t::other);
        for (const auto& bgm : measure.bgm_events) visit(bgm.position, free_position_kind_t::other);
    }
    for (const auto& layout : chart.lane_layouts) {
        const auto row = absolute_row(index, layout.position);
        if (!row || (*row > range.begin && *row < range.end)) clear = false;
    }
    for (const auto& recipe : chart.sv_recipes) {
        const auto first = conversion_row(index, recipe.range.begin);
        const auto last = conversion_row(index, recipe.range.end, true);
        if (!first || !last || range.contains(*first) || range.contains(*last)) clear = false;
    }
    return clear;
}

auto conversion_beats(std::uint64_t rows, std::uint64_t resolution, std::uint64_t units = 4) -> std::optional<signed_rational_t> {
    if (resolution == 0) return std::nullopt;
    const auto divisor = std::gcd(rows, resolution);
    auto numerator = rows / divisor;
    auto denominator = resolution / divisor;
    const auto four = units / std::gcd(denominator, units);
    denominator /= units / four;
    if (numerator > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) / four)
        return std::nullopt;
    numerator *= four;
    return signed_rational_t{static_cast<std::int64_t>(numerator), denominator};
}

auto conversion_object_id(const chart_document_t& chart) -> std::optional<object_id_t> {
    std::uint64_t maximum{};
    const auto scan = [&](const auto& values) {
        for (const auto& value : values) maximum = std::max(maximum, value.id.value);
    };
    scan(chart.lane_layouts); scan(chart.bgm_source_lanes); scan(chart.bpms); scan(chart.pauses);
    scan(chart.warps); scan(chart.scrolls); scan(chart.speeds); scan(chart.kiai);
    scan(chart.timing_anchors); scan(chart.bookmarks); scan(chart.sections); scan(chart.bga_events);
    scan(chart.sound_channels);
    for (const auto& channel : chart.sound_channels) scan(channel.events);
    for (const auto& measure : chart.measures) { scan(measure.notes); scan(measure.bgm_events); }
    if (maximum == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
    return object_id_t{maximum + 1};
}

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

auto conversion_times_match(const timing_index_t& before, const timing_index_t& after,
                            std::span<const std::uint64_t> rows, const pause_range_t& range) -> bool {
    for (const auto row : rows) {
        if (row == range.begin && range.kind == pause_kind_t::delay) continue; // configuration only
        const auto old_time = timing_at(before, row);
        const auto new_time = timing_at(after, range.shifted(row));
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
        const auto start = absolute_row(index, speed.position);
        const auto length = conversion_transition_rows(speed.transition_beats, chart.divisions_per_standard_measure);
        if (!start || !length || *length > std::numeric_limits<std::uint64_t>::max() - *start) return false;
        const auto end = *start + *length;
        if (range.contains(end)) return false;
        if (*start >= range.end || end < range.begin) continue;
        const auto beats = conversion_beats(range.shifted(end) - range.shifted(*start),
                                             chart.divisions_per_standard_measure);
        if (!beats) return false;
        speed.transition_beats = {static_cast<std::uint64_t>(beats->numerator), beats->denominator};
        if (end <= index.total_rows) timed_rows.push_back(end);
    }
    return true;
}

auto convert_pause_geometry(chart_document_t& chart, const measure_index_t& before,
                            const pause_range_t& range, bool resize, pause_event_t pause) -> bool {
    const auto timing = build_timing_index(chart);
    if (!timing) return false;
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
    const auto built = build_measure_index(chart);
    const auto* after = std::get_if<measure_index_t>(&built);
    if (!after || !place_lifted_objects(chart, *after, 0, notes, bgm, positions)) return false;
    const auto start = position_at_absolute_row(*after, range.begin);
    if (!start) return false;
    pause.position = *start;
    chart.pauses.push_back(std::move(pause));
    const auto new_timing = build_timing_index(chart);
    return new_timing && conversion_times_match(*timing.index, *new_timing.index, rows, range);
}

void apply_measure_action(measure_edit_context_t& context, const convert_range_to_pause_action_t& edit) {
    auto* chart = target_measure_chart(context, edit.chart);
    if (!chart) return;
    const auto built = build_measure_index(*chart);
    const auto* before = std::get_if<measure_index_t>(&built);
    const auto first = before ? absolute_row(*before, edit.range.begin) : std::nullopt;
    const auto last = before ? conversion_row(*before, edit.range.end, true) : std::nullopt;
    if (!first || !last || *first >= *last ||
        (edit.kind != pause_kind_t::stop && edit.kind != pause_kind_t::delay)) {
        reject(context.result, diagnostic_code_t::invalid_measure, "Select a nonempty authored row range.");
        return;
    }
    const pause_range_t range{*first, *last, edit.kind};
    if ((chart->bms_control_tree && has_conditional_measures(*chart->bms_control_tree)) ||
        conversion_timing_blocked(*chart, *before, range)) {
        reject(context.result, diagnostic_code_t::unsupported_feature,
               "The range crosses BPM, pause, warp or retained BMS branch timing; choose an uninterrupted interval.");
        return;
    }
    if (!conversion_points_clear(*chart, *before, range)) {
        reject(context.result, diagnostic_code_t::invalid_measure,
               edit.kind == pause_kind_t::stop
                   ? "Stop conversion needs an empty interval after its start, including its end; use Delay for an object at the end."
                   : "Delay conversion needs an empty interval including its start; use Stop for an object at the start.");
        return;
    }
    const bool bms_ticks = chart->source_family == format_family_t::bms && edit.kind == pause_kind_t::stop;
    const auto duration = conversion_beats(*last - *first, chart->divisions_per_standard_measure, bms_ticks ? 192 : 4);
    const auto id = conversion_object_id(*chart);
    if (!duration || !id || !convert_pause_geometry(*chart, *before, range, edit.resize_measures,
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
        if (!chart_has_virtual_tail(*chart) || highest >= virtual_tail_measure_limit(*chart) ||
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
        if (same_definition(chart->measures[at].definition, edit.definition)) continue;
        const measure_assignment_t assignment{at, edit.definition};
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
        if (!chart_has_virtual_tail(*chart) || second >= virtual_tail_measure_limit(*chart) ||
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
    if (same_definition(chart->measures[edit.measure_index].definition, edit.before) &&
        same_definition(chart->measures[second].definition, edit.after)) {
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
    if (!chart_has_virtual_tail(*chart) || edit.count > virtual_tail_measure_limit(*chart) ||
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

auto measure_holds_positioned_objects(const chart_document_t& chart, measure_id_t measure) -> bool {
    if (std::ranges::any_of(chart.lane_layouts, [&](const auto& segment) {
            return segment.position.measure == measure;
        }))
        return true;
    bool found = false;
    for_each_free_position(chart, [&](const local_position_t& position, free_position_kind_t) {
        found = found || position.measure == measure;
    });
    return found;
}

auto propose_measure_removal(const chart_document_t& chart, measure_id_t measure)
    -> measure_removal_proposal_t {
    auto scratch = chart;
    return remove_measure_contents(scratch, measure);
}

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

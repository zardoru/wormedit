#include <wormedit/core/kiai_spans.h>

#include <algorithm>
#include <tuple>

namespace wormedit {
namespace {

struct placed_event_t {
    std::uint64_t row{};
    std::size_t source_order{};
    object_id_t id;
    bool enabled{false};
};

auto place_events(const chart_document_t& chart, const measure_index_t& measures)
    -> std::vector<placed_event_t> {
    std::vector<placed_event_t> placed;
    placed.reserve(chart.kiai.size());
    for (const auto& event : chart.kiai) {
        if (const auto row = absolute_row(measures, event.position))
            placed.push_back({*row, event.provenance.source_order, event.id, event.enabled});
    }
    std::ranges::sort(placed, [](const auto& left, const auto& right) {
        return std::tie(left.row, left.source_order, left.id.value) <
               std::tie(right.row, right.source_order, right.id.value);
    });
    return placed;
}

struct boundary_t {
    std::uint64_t row{};
    bool enabled{false};
};

// Merges overlapping or touching intervals; the result is sorted and disjoint.
auto merge_intervals(std::vector<std::pair<std::uint64_t, std::uint64_t>> intervals)
    -> std::vector<std::pair<std::uint64_t, std::uint64_t>> {
    std::ranges::sort(intervals);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> merged;
    for (const auto& interval : intervals) {
        if (!merged.empty() && interval.first <= merged.back().second)
            merged.back().second = std::max(merged.back().second, interval.second);
        else
            merged.push_back(interval);
    }
    return merged;
}

auto desired_boundaries(const std::vector<std::pair<std::uint64_t, std::uint64_t>>& merged,
                        std::uint64_t total_rows) -> std::vector<boundary_t> {
    std::vector<boundary_t> boundaries;
    for (const auto& [begin, end] : merged) {
        boundaries.push_back({begin, true});
        if (end < total_rows) boundaries.push_back({end, false});
    }
    return boundaries;
}

auto local_position_at(const measure_index_t& measures, std::uint64_t row)
    -> std::optional<local_position_t> {
    for (const auto& entry : measures.entries) {
        if (row >= entry.absolute_begin_row && row < entry.absolute_begin_row + entry.row_capacity)
            return local_position_t{entry.id, row - entry.absolute_begin_row};
    }
    return std::nullopt;
}

auto validate_rows(const kiai_span_edit_t& edit, std::uint64_t total_rows)
    -> std::optional<std::string> {
    if (!edit.head && !edit.rows) return "a kiai edit needs an area, new rows, or both";
    if (!edit.rows) return std::nullopt;
    if (edit.rows->first >= edit.rows->second || edit.rows->second > total_rows)
        return "kiai area rows must be a non-empty range inside the chart";
    return std::nullopt;
}

struct boundary_plan_t {
    std::vector<object_id_t> kept;
    std::vector<boundary_t> created;
    std::vector<std::pair<object_id_t, local_position_t>> moves;
};

// Reuses an existing event for each wanted boundary so identity and effect bits survive; a dragged
// head or tail keeps its event by moving instead of being replaced.
auto plan_boundaries(const std::vector<boundary_t>& wanted, const std::vector<placed_event_t>& placed,
                     const measure_index_t& measures, std::optional<object_id_t> edited_head,
                     std::optional<object_id_t> edited_tail) -> boundary_plan_t {
    boundary_plan_t plan;
    std::vector<boundary_t> unmatched;
    for (const auto& boundary : wanted) {
        const auto match = std::ranges::find_if(placed, [&](const placed_event_t& event) {
            return event.row == boundary.row && event.enabled == boundary.enabled &&
                   std::ranges::find(plan.kept, event.id) == plan.kept.end();
        });
        if (match == placed.end()) unmatched.push_back(boundary);
        else plan.kept.push_back(match->id);
    }
    for (const auto& boundary : unmatched) {
        const auto reusable = boundary.enabled ? edited_head : edited_tail;
        const auto position = local_position_at(measures, boundary.row);
        if (reusable && position && std::ranges::find(plan.kept, *reusable) == plan.kept.end()) {
            plan.kept.push_back(*reusable);
            plan.moves.emplace_back(*reusable, *position);
        } else {
            plan.created.push_back(boundary);
        }
    }
    return plan;
}

// Applies the plan to the chart's events. On failure the events are restored.
auto write_boundary_plan(chart_document_t& chart, const measure_index_t& measures,
                         const boundary_plan_t& plan,
                         const std::function<std::optional<object_id_t>()>& allocate_id)
    -> std::optional<std::string> {
    auto original = chart.kiai;
    // Events outside the measure geometry are not part of any area and stay as they are.
    std::erase_if(chart.kiai, [&](const kiai_event_t& event) {
        return absolute_row(measures, event.position) &&
               std::ranges::find(plan.kept, event.id) == plan.kept.end();
    });
    for (const auto& [id, position] : plan.moves) {
        const auto event = std::ranges::find(chart.kiai, id, &kiai_event_t::id);
        if (event != chart.kiai.end()) event->position = position;
    }
    std::size_t source_order = 0;
    for (const auto& event : original)
        source_order = std::max(source_order, event.provenance.source_order + 1);
    for (const auto& boundary : plan.created) {
        const auto position = local_position_at(measures, boundary.row);
        // Each event is published at once: id allocation scans the chart's objects.
        const auto id = allocate_id ? allocate_id() : std::nullopt;
        if (!position || !id) {
            chart.kiai = std::move(original);
            return "a kiai boundary could not be placed in the chart";
        }
        chart.kiai.push_back({.id = *id, .position = *position, .enabled = boundary.enabled,
                              .provenance = {.source_order = source_order++}});
    }
    return std::nullopt;
}

} // namespace

auto kiai_spans_of(const chart_document_t& chart, const measure_index_t& measures)
    -> std::vector<kiai_span_t> {
    std::vector<kiai_span_t> spans;
    bool on = false;
    kiai_span_t current;
    for (const auto& event : place_events(chart, measures)) {
        if (event.enabled && !on) {
            on = true;
            current = {.begin_row = event.row, .head = event.id};
        } else if (!event.enabled && on) {
            on = false;
            current.end_row = event.row;
            current.tail = event.id;
            if (current.end_row > current.begin_row) spans.push_back(current);
        }
    }
    if (on && measures.total_rows > current.begin_row) {
        current.end_row = measures.total_rows;
        current.open_ended = true;
        spans.push_back(current);
    }
    return spans;
}

auto apply_kiai_span_edit(chart_document_t& chart, const measure_index_t& measures,
                          const kiai_span_edit_t& edit,
                          const std::function<std::optional<object_id_t>()>& allocate_id,
                          std::size_t maximum_events) -> std::optional<std::string> {
    if (const auto error = validate_rows(edit, measures.total_rows)) return error;
    auto spans = kiai_spans_of(chart, measures);
    std::optional<object_id_t> edited_head, edited_tail;
    if (edit.head) {
        const auto found = std::ranges::find(spans, *edit.head, &kiai_span_t::head);
        if (found == spans.end()) return "the kiai area to edit no longer exists";
        edited_head = found->head;
        if (found->tail) edited_tail = found->tail;
        spans.erase(found);
    }
    std::vector<std::pair<std::uint64_t, std::uint64_t>> intervals;
    for (const auto& span : spans) intervals.emplace_back(span.begin_row, span.end_row);
    if (edit.rows) intervals.push_back(*edit.rows);
    const auto wanted = desired_boundaries(merge_intervals(std::move(intervals)),
                                           measures.total_rows);
    const auto plan = plan_boundaries(wanted, place_events(chart, measures), measures,
                                      edited_head, edited_tail);
    if (plan.kept.size() + plan.created.size() > maximum_events)
        return "the kiai edit would exceed the configured event cap";
    return write_boundary_plan(chart, measures, plan, allocate_id);
}

} // namespace wormedit

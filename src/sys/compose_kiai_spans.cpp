#include "sys/compose_kiai_spans.h"

#include <algorithm>
#include <tuple>

namespace wormedit::sys {
namespace {

struct placed_kiai_event_t {
    std::uint64_t row{};
    std::size_t source_order{};
    object_id_t id;
    bool enabled{false};
};

auto place_kiai_events(const chart_document_t& chart, const measure_index_t& measures,
                       kiai_projection_t& result) -> std::vector<placed_kiai_event_t> {
    std::vector<placed_kiai_event_t> placed;
    placed.reserve(chart.kiai.size());
    for (const auto& event : chart.kiai) {
        const auto row = absolute_row(measures, event.position);
        if (!row) {
            result.truncated = true;
            continue;
        }
        placed.push_back({*row, event.provenance.source_order, event.id, event.enabled});
    }
    std::ranges::sort(placed, [](const auto& left, const auto& right) {
        return std::tie(left.row, left.source_order, left.id.value) <
               std::tie(right.row, right.source_order, right.id.value);
    });
    return placed;
}

} // namespace

auto project_kiai_spans(const chart_document_t& chart, const measure_index_t& measures,
                        std::size_t limit) -> kiai_projection_t {
    kiai_projection_t result;
    const auto events = place_kiai_events(chart, measures, result);
    bool on = false;
    kiai_span_t current;
    for (const auto& event : events) {
        if (event.enabled && !on) {
            on = true;
            current = {.begin_row = event.row, .head = event.id};
        } else if (!event.enabled && on) {
            on = false;
            current.end_row = event.row;
            current.tail = event.id;
            // A zero-length area is an on and off at one row: nothing to draw.
            if (current.end_row > current.begin_row) result.spans.push_back(current);
        }
    }
    if (on && measures.total_rows > current.begin_row) {
        current.end_row = measures.total_rows;
        current.open_ended = true;
        result.spans.push_back(current);
    }
    if (result.spans.size() > limit) {
        result.spans.resize(limit);
        result.truncated = true;
    }
    return result;
}

} // namespace wormedit::sys

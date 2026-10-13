#include <wormedit/core/bgm_columns.h>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace wormedit {
namespace {

auto largest_object_id(const chart_document_t& chart) -> std::uint64_t {
    std::uint64_t largest{};
    const auto scan = [&](const auto& values) {
        for (const auto& value : values) largest = std::max(largest, value.id.value);
    };
    scan(chart.bgm_source_lanes);
    scan(chart.lane_layouts);
    scan(chart.bpms);
    scan(chart.pauses);
    scan(chart.warps);
    scan(chart.scrolls);
    scan(chart.speeds);
    scan(chart.kiai);
    scan(chart.timing_anchors);
    scan(chart.bookmarks);
    scan(chart.sections);
    scan(chart.bga_events);
    scan(chart.sound_channels);
    for (const auto& channel : chart.sound_channels) scan(channel.events);
    for (const auto& measure : chart.measures) {
        scan(measure.notes);
        scan(measure.bgm_events);
    }
    return largest;
}

auto events_in_column(const chart_document_t& chart, object_id_t column) -> std::size_t {
    std::size_t total{};
    for (const auto& measure : chart.measures) {
        total += static_cast<std::size_t>(std::ranges::count(
            measure.bgm_events, column, &bgm_event_t::source_lane));
    }
    return total;
}

auto grow_columns(chart_document_t& chart, std::size_t count, document_command_result_t& result)
    -> bool {
    auto next_id = largest_object_id(chart);
    std::size_t order{};
    for (const auto& lane : chart.bgm_source_lanes) order = std::max(order, lane.source_order + 1);
    const auto missing = count - chart.bgm_source_lanes.size();
    if (next_id > std::numeric_limits<std::uint64_t>::max() - missing) {
        reject(result, diagnostic_code_t::invalid_id, "BGM source lane ID space is exhausted");
        return false;
    }
    while (chart.bgm_source_lanes.size() < count) {
        chart.bgm_source_lanes.push_back(
            {.id = object_id_t{++next_id},
             .name = "BGM " + std::to_string(chart.bgm_source_lanes.size() + 1),
             .source_order = order++});
    }
    return true;
}

auto shrink_columns(chart_document_t& chart, std::size_t count, document_command_result_t& result)
    -> bool {
    while (chart.bgm_source_lanes.size() > count) {
        const auto& last = chart.bgm_source_lanes.back();
        if (const auto events = events_in_column(chart, last.id); events != 0) {
            reject(result, diagnostic_code_t::invalid_id,
                   "Cannot remove " + last.name + ": it holds " + std::to_string(events) +
                       (events == 1 ? " BGM event" : " BGM events"));
            return false;
        }
        chart.bgm_source_lanes.pop_back();
    }
    return true;
}

} // namespace

auto apply_bgm_column_count(document_history_t& history, std::uint64_t expected_revision,
                            chart_id_t chart, std::size_t count) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id, "BGM column document revision is stale");
        return result;
    }
    if (count < minimum_bgm_column_count || count > maximum_bgm_column_count) {
        reject(result, diagnostic_code_t::invalid_id, "BGM column count must be 1 to 128");
        return result;
    }
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    const auto target = std::ranges::find(proposed.charts, chart, &chart_document_t::id);
    if (!chart || target == proposed.charts.end()) {
        reject(result, diagnostic_code_t::invalid_id, "BGM column chart is missing");
        return result;
    }
    if (target->source_family != format_family_t::bms &&
        target->source_family != format_family_t::osu_mania) {
        reject(result, diagnostic_code_t::invalid_id,
               "BGM column counts are available only for BMS and osu!mania charts");
        return result;
    }
    const auto current = target->bgm_source_lanes.size();
    if (count == current) return result;
    const auto changed = count > current ? grow_columns(*target, count, result)
                                         : shrink_columns(*target, count, result);
    if (!changed) return result;
    return transaction.commit("Set BGM column count");
}

} // namespace wormedit

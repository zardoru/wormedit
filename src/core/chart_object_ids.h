#pragma once
#include <wormedit/core/document.h>
#include <algorithm>
#include <limits>
namespace wormedit {
template<class Values, class Visitor>
auto visit_object_id_values(const Values& values, Visitor& visitor) -> bool {
    for (const auto& value : values) if (!visitor(value.id)) return false;
    return true;
}
template<class Visitor>
auto visit_chart_object_ids(const chart_document_t& chart, Visitor&& visitor) -> bool {
    if (!visit_object_id_values(chart.bgm_source_lanes, visitor) ||
        !visit_object_id_values(chart.lane_layouts, visitor) || !visit_object_id_values(chart.bpms, visitor) ||
        !visit_object_id_values(chart.pauses, visitor) || !visit_object_id_values(chart.warps, visitor) ||
        !visit_object_id_values(chart.scrolls, visitor) || !visit_object_id_values(chart.speeds, visitor) ||
        !visit_object_id_values(chart.kiai, visitor) || !visit_object_id_values(chart.timing_anchors, visitor) ||
        !visit_object_id_values(chart.bookmarks, visitor) || !visit_object_id_values(chart.sections, visitor) ||
        !visit_object_id_values(chart.bga_events, visitor) || !visit_object_id_values(chart.sound_channels, visitor)) return false;
    for (const auto& measure : chart.measures)
        if (!visit_object_id_values(measure.notes, visitor) || !visit_object_id_values(measure.bgm_events, visitor)) return false;
    for (const auto& channel : chart.sound_channels)
        if (!visit_object_id_values(channel.events, visitor)) return false;
    return true;
}
struct object_id_allocation_t {
    std::optional<object_id_t> id;
    bool over_limit{};
    bool exhausted{};
};
struct object_id_ceiling_t {
    std::size_t limit{std::numeric_limits<std::size_t>::max()};
    std::size_t count{};
    std::uint64_t maximum{};
    static auto from(const chart_document_t& chart,
                     std::size_t limit = std::numeric_limits<std::size_t>::max())
        -> object_id_ceiling_t {
        object_id_ceiling_t scan{limit};
        (void)visit_chart_object_ids(chart, scan);
        return scan;
    }
    auto operator()(object_id_t id) -> bool {
        if (count >= limit) return false;
        ++count;
        maximum = std::max(maximum, id.value);
        return true;
    }
};
inline auto next_chart_object_id(const chart_document_t& chart,
    std::size_t limit = std::numeric_limits<std::size_t>::max()) -> object_id_allocation_t {
    const auto scan = object_id_ceiling_t::from(chart, limit);
    if (scan.count >= limit) return {.over_limit = true};
    if (scan.maximum == std::numeric_limits<std::uint64_t>::max()) return {.exhausted = true};
    return {.id = object_id_t{scan.maximum + 1}};
}
struct object_id_counter_t {
    object_id_t sought;
    std::size_t limit;
    std::size_t count{};
    auto operator()(object_id_t id) -> bool {
        if (id == sought) ++count;
        return count < limit;
    }
};
inline auto chart_object_id_count(const chart_document_t& chart, object_id_t sought,
    std::size_t limit = std::numeric_limits<std::size_t>::max()) -> std::size_t {
    object_id_counter_t count{sought, limit};
    (void)visit_chart_object_ids(chart, count);
    return count.count;
}
} // namespace wormedit

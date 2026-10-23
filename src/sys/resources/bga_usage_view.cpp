#include "sys/resources/bga_usage_view.h"
#include "sys/resources/resource_usage_collector.h"
#include <wormedit/core/interaction_types.h>
#include <algorithm>
#include <tuple>
#include <unordered_map>

namespace wormedit::sys {
namespace {
using order_t = std::tuple<std::size_t, std::size_t, std::uint64_t, std::size_t, unsigned, std::uint64_t>;
using collector_t = resource_usage_collector_t<ui::bga_usage_row_view_t, order_t>;

auto kind_name(visual_event_kind_t kind) -> std::string_view {
    switch (kind) {
    case visual_event_kind_t::background: return "Base";
    case visual_event_kind_t::video: return "Base video";
    case visual_event_kind_t::layer: return "Layer";
    case visual_event_kind_t::layer2: return "Layer 2";
    case visual_event_kind_t::poor: return "Poor";
    case visual_event_kind_t::storyboard: return "Storyboard";
    }
    return "BGA";
}
void append_chart_uses(const chart_document_t& chart, std::size_t chart_index,
                       resource_id_t selected, asset_id_t asset,
                       const std::unordered_map<std::uint64_t, asset_id_t>& resources,
                       collector_t& collector) {
    std::unordered_map<std::uint64_t, std::size_t> measures;
    for (std::size_t i = 0; i < chart.measures.size(); ++i) measures.emplace(chart.measures[i].id.value, i);
    for (const auto& event : chart.bga_events) {
        const auto lane = editor_bga_lane_index(event.kind);
        const auto resource = resources.find(event.resource.value);
        if (!lane || resource == resources.end()) continue;
        if (event.resource != selected && (!asset || resource->second != asset)) continue;
        const auto measure = measures.find(event.position.measure.value);
        if (measure == measures.end()) continue;
        ui::bga_usage_row_view_t row{
            .target = {chart.id, event.id, event.position, event.kind, event.resource},
            .label = chart.metadata.name + " · M" + std::to_string(measure->second + 1) +
                     ":" + std::to_string(event.position.row) + " · " + std::string{kind_name(event.kind)} +
                     " · #" + std::to_string(event.id.value)};
        if (event.resource != selected) row.label += " · shared asset";
        collector.add(std::move(row), {chart_index, measure->second, event.position.row, *lane,
                                      static_cast<unsigned>(event.kind), event.id.value});
    }
}
}
auto bga_usage_view_t::from(const song_document_t& document, resource_id_t resource)
    -> ui::bga_usage_view_t {
    ui::bga_usage_view_t view{.song = document.id, .revision = document.revision, .resource = resource};
    const auto selected = std::ranges::find(document.resources, resource, &resource_entry_t::id);
    if (!resource || selected == document.resources.end()) return view;
    if (std::ranges::find(selected->roles, resource_role_t::bga) == selected->roles.end()) return view;
    view.asset = selected->asset;
    view.valid = true;
    std::unordered_map<std::uint64_t, asset_id_t> resources;
    for (const auto& entry : document.resources) resources.emplace(entry.id.value, entry.asset);
    collector_t collector;
    for (std::size_t i = 0; i < document.charts.size(); ++i)
        append_chart_uses(document.charts[i], i, resource, view.asset, resources, collector);
    view.total_matching = collector.total;
    view.truncated = collector.total > resource_usage_projection_limit;
    view.rows = collector.ordered_rows();
    return view;
}
} // namespace wormedit::sys

#include "sys/sound_usage_view.h"

#include <algorithm>
#include <queue>
#include <tuple>
#include <unordered_map>

namespace wormedit::sys {
namespace {

using usage_order_t = std::tuple<std::size_t, bool, std::size_t, std::uint64_t, unsigned,
                                 std::uint64_t, std::uint64_t, std::uint64_t>;

struct usage_candidate_t {
    usage_order_t order;
    sound_usage_row_t row;
};

struct latest_first_t {
    auto operator()(const usage_candidate_t& left, const usage_candidate_t& right) const -> bool {
        return left.order < right.order;
    }
};

struct usage_collector_t {
    std::priority_queue<usage_candidate_t, std::vector<usage_candidate_t>, latest_first_t> latest;
    std::size_t total{};

    void add(sound_usage_row_t row, usage_order_t order) {
        ++total;
        if (latest.size() < sound_usage_projection_limit) {
            latest.push({std::move(order), std::move(row)});
        } else if (order < latest.top().order) {
            latest.pop();
            latest.push({std::move(order), std::move(row)});
        }
    }

    auto ordered_rows() -> std::vector<sound_usage_row_t> {
        std::vector<usage_candidate_t> candidates;
        candidates.reserve(latest.size());
        while (!latest.empty()) {
            candidates.push_back(latest.top());
            latest.pop();
        }
        std::ranges::sort(candidates, {}, &usage_candidate_t::order);
        std::vector<sound_usage_row_t> rows;
        rows.reserve(candidates.size());
        for (auto& candidate : candidates)
            rows.push_back(std::move(candidate.row));
        return rows;
    }
};

auto resource_assets(const song_document_t& document)
    -> std::unordered_map<std::uint64_t, std::uint64_t> {
    std::unordered_map<std::uint64_t, std::uint64_t> assets;
    assets.reserve(document.resources.size());
    for (const auto& resource : document.resources)
        assets.emplace(resource.id.value, resource.asset.value);
    return assets;
}

auto order_key(std::size_t chart_index, const sound_usage_row_t& row, std::size_t measure_index)
    -> usage_order_t {
    return {chart_index,
            row.position.has_value(),
            row.position ? measure_index : 0,
            row.position ? row.position->row : 0,
            static_cast<unsigned>(row.kind),
            row.object.value,
            row.channel.value,
            row.clip.value};
}

auto measure_orders(const chart_document_t& chart)
    -> std::unordered_map<std::uint64_t, std::size_t> {
    std::unordered_map<std::uint64_t, std::size_t> orders;
    orders.reserve(chart.measures.size());
    for (std::size_t index = 0; index < chart.measures.size(); ++index)
        orders.emplace(chart.measures[index].id.value, index);
    return orders;
}

auto position_measure_order(const std::unordered_map<std::uint64_t, std::size_t>& orders,
                            const local_position_t& position) -> std::size_t {
    const auto found = orders.find(position.measure.value);
    return found == orders.end() ? orders.size() : found->second;
}

auto use_scope(const std::unordered_map<std::uint64_t, std::uint64_t>& assets, resource_id_t used,
               resource_id_t selected, asset_id_t selected_asset)
    -> std::optional<sound_usage_scope_t> {
    if (used == selected)
        return sound_usage_scope_t::resource_reference;
    if (!selected_asset)
        return std::nullopt;
    const auto found = assets.find(used.value);
    if (found != assets.end() && found->second == selected_asset.value)
        return sound_usage_scope_t::shared_asset_reference;
    return std::nullopt;
}

void append_resource_uses(const chart_document_t& chart, std::size_t chart_index,
                          resource_id_t resource, asset_id_t asset,
                          const std::unordered_map<std::uint64_t, std::uint64_t>& assets,
                          usage_collector_t& uses) {
    for (std::size_t measure_index = 0; measure_index < chart.measures.size(); ++measure_index) {
        const auto& measure = chart.measures[measure_index];
        for (const auto& note : measure.notes) {
            if (!note.sound)
                continue;
            const auto scope = use_scope(assets, *note.sound, resource, asset);
            if (!scope)
                continue;
            sound_usage_row_t row{.chart = chart.id,
                                  .chart_name = chart.metadata.name,
                                  .kind = sound_usage_kind_t::note,
                                  .scope = *scope,
                                  .position = note.position,
                                  .object = note.id,
                                  .referenced_resource = *note.sound,
                                  .referenced_asset = asset};
            uses.add(row, order_key(chart_index, row, measure_index));
        }
        for (const auto& event : measure.bgm_events) {
            if (!event.sound)
                continue;
            const auto scope = use_scope(assets, *event.sound, resource, asset);
            if (!scope)
                continue;
            sound_usage_row_t row{.chart = chart.id,
                                  .chart_name = chart.metadata.name,
                                  .kind = sound_usage_kind_t::bgm,
                                  .scope = *scope,
                                  .position = event.position,
                                  .object = event.id,
                                  .referenced_resource = *event.sound,
                                  .referenced_asset = asset};
            uses.add(row, order_key(chart_index, row, measure_index));
        }
    }
}

void append_asset_uses(const chart_document_t& chart, std::size_t chart_index, asset_id_t asset,
                       const std::unordered_map<std::uint64_t, std::size_t>& measure_order,
                       usage_collector_t& uses) {
    for (const auto& channel : chart.sound_channels) {
        if (channel.source_clip.source != asset)
            continue;
        sound_usage_row_t source{.chart = chart.id,
                                 .chart_name = chart.metadata.name,
                                 .kind = sound_usage_kind_t::sound_channel_source,
                                 .scope = sound_usage_scope_t::shared_asset_reference,
                                 .channel = channel.id,
                                 .clip = channel.source_clip.id,
                                 .referenced_asset = asset};
        uses.add(source, order_key(chart_index, source, 0));
        for (const auto& event : channel.events) {
            sound_usage_row_t row{.chart = chart.id,
                                  .chart_name = chart.metadata.name,
                                  .kind = sound_usage_kind_t::sound_channel_event,
                                  .scope = sound_usage_scope_t::shared_asset_reference,
                                  .position = event.position,
                                  .object = event.id,
                                  .channel = channel.id,
                                  .clip = channel.source_clip.id,
                                  .referenced_asset = asset};
            uses.add(row, order_key(chart_index, row,
                                    position_measure_order(measure_order, event.position)));
        }
    }
}

} // namespace

auto sound_usage_view_t::from(const song_document_t& document, resource_id_t resource)
    -> sound_usage_view_t {
    sound_usage_view_t projection{
        .song = document.id, .revision = document.revision, .resource = resource};
    const auto selected = std::ranges::find(document.resources, resource, &resource_entry_t::id);
    if (!resource || selected == document.resources.end())
        return projection;
    projection.asset = selected->asset;
    projection.valid = true;
    usage_collector_t uses;
    const auto assets = resource_assets(document);
    for (std::size_t chart_index = 0; chart_index < document.charts.size(); ++chart_index) {
        const auto& chart = document.charts[chart_index];
        append_resource_uses(chart, chart_index, resource, selected->asset, assets, uses);
        if (selected->asset) {
            const auto measure_order = measure_orders(chart);
            append_asset_uses(chart, chart_index, selected->asset, measure_order, uses);
        }
    }
    projection.total_matching = uses.total;
    projection.truncated = uses.total > sound_usage_projection_limit;
    projection.rows = uses.ordered_rows();
    return projection;
}

auto sound_usage_cache_t::view_for(const song_document_t& document, resource_id_t resource,
                                    std::uint64_t session_generation)
    -> const sound_usage_view_t& {
    return projection_.get_or_build(std::tuple{document_stamp_t::from(document), resource, session_generation}, [&] {
        return sound_usage_view_t::from(document, resource);
    });
}

} // namespace wormedit::sys

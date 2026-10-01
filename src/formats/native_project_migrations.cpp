#include "native_project_migrations.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wormedit::native_project_internal {

auto default_migrations() -> std::span<const migration_t> {
    return {};
}

namespace {

auto find_step(std::span<const migration_t> table, std::uint32_t from_version)
    -> const migration_t* {
    const auto found = std::ranges::find(table, from_version, &migration_t::from_version);
    return found == table.end() ? nullptr : &*found;
}

} // namespace

auto migrate_document(nlohmann::json& document, std::uint32_t target_version,
                      std::span<const migration_t> table) -> migration_outcome_t {
    if (!document.is_object() || !document.contains("schema_version") ||
        !document.at("schema_version").is_number_unsigned()) {
        return {migration_status_t::malformed_version,
                "document schema version must be an unsigned integer"};
    }
    const auto start = document.at("schema_version").get<std::uint64_t>();
    if (start > target_version) {
        return {migration_status_t::unsupported_version,
                "document schema version is newer than this build supports"};
    }
    if (start == target_version) {
        return {};
    }
    // Work on a copy so a failing step never leaves a half-migrated document.
    auto working = document;
    for (auto version = static_cast<std::uint32_t>(start); version < target_version; ++version) {
        const auto* step = find_step(table, version);
        if (step == nullptr || step->migrate == nullptr) {
            return {migration_status_t::missing_step,
                    "no migration from document schema version " + std::to_string(version)};
        }
        if (auto message = step->migrate(working); !message.empty()) {
            return {migration_status_t::failed, std::move(message)};
        }
        working["schema_version"] = version + 1;
    }
    document = std::move(working);
    return {migration_status_t::migrated, {}};
}

namespace {

constexpr std::string_view legacy_lane_prefix = "BGM source #";

auto is_bms_zero_slot(const bgm_event_t& event) -> bool {
    constexpr std::string_view suffix = " object 00";
    const auto& field = event.provenance.source_field;
    return !event.sound && event.provenance.family == format_family_t::bms &&
           field.starts_with("01 slot ") && field.ends_with(suffix);
}

auto drop_zero_slot_events(chart_document_t& chart) -> bool {
    bool changed = false;
    for (auto& measure : chart.measures) {
        changed |= std::erase_if(measure.bgm_events, is_bms_zero_slot) > 0;
    }
    return changed;
}

// Legacy lanes (one per source line) ranked by their source line.
auto ranked_legacy_lanes(const chart_document_t& chart)
    -> std::map<std::uint64_t, std::size_t> {
    std::vector<const bgm_source_lane_t*> legacy;
    for (const auto& lane : chart.bgm_source_lanes) {
        if (lane.name.starts_with(legacy_lane_prefix)) legacy.push_back(&lane);
    }
    std::ranges::stable_sort(legacy, {}, &bgm_source_lane_t::source_order);
    std::map<std::uint64_t, std::size_t> rank;
    for (std::size_t index = 0; index < legacy.size(); ++index)
        rank.emplace(legacy[index]->id.value, index);
    return rank;
}

// Column of every legacy lane in the measure that uses it: the k-th distinct
// legacy lane (by source order) of a measure becomes column k.
auto measure_columns(const measure_chunk_t& measure,
                     const std::map<std::uint64_t, std::size_t>& rank)
    -> std::map<std::uint64_t, std::size_t> {
    std::set<std::size_t> used_ranks;
    for (const auto& event : measure.bgm_events) {
        if (const auto found = rank.find(event.source_lane.value); found != rank.end())
            used_ranks.insert(found->second);
    }
    std::map<std::size_t, std::size_t> column_of_rank;
    for (const auto value : used_ranks) column_of_rank.emplace(value, column_of_rank.size());
    std::map<std::uint64_t, std::size_t> columns;
    for (const auto& [lane, lane_rank] : rank) {
        if (const auto found = column_of_rank.find(lane_rank); found != column_of_rank.end())
            columns.emplace(lane, found->second);
    }
    return columns;
}

void rebuild_bgm_columns(chart_document_t& chart) {
    const auto rank = ranked_legacy_lanes(chart);
    if (rank.empty()) return;
    std::vector<std::map<std::uint64_t, std::size_t>> per_measure;
    std::size_t column_count = 0;
    std::vector<std::uint64_t> column_lane;
    std::map<std::size_t, std::size_t> column_first_order;
    for (const auto& measure : chart.measures) {
        per_measure.push_back(measure_columns(measure, rank));
        for (const auto& [lane, column] : per_measure.back()) {
            column_count = std::max(column_count, column + 1);
            const auto order = std::ranges::find(chart.bgm_source_lanes, lane,
                                                 [](const auto& item) { return item.id.value; })
                                   ->source_order;
            const auto [slot, inserted] = column_first_order.try_emplace(column, order);
            if (!inserted) slot->second = std::min(slot->second, order);
        }
    }
    // Column k keeps the id of the k-th legacy lane, so ids stay stable.
    column_lane.resize(column_count);
    for (const auto& [lane, lane_rank] : rank) {
        if (lane_rank < column_count) column_lane[lane_rank] = lane;
    }
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        for (auto& event : chart.measures[index].bgm_events) {
            const auto found = per_measure[index].find(event.source_lane.value);
            if (found != per_measure[index].end())
                event.source_lane = object_id_t{column_lane[found->second]};
        }
    }
    std::vector<bgm_source_lane_t> lanes;
    for (const auto& lane : chart.bgm_source_lanes) {
        const auto found = rank.find(lane.id.value);
        if (found == rank.end()) {
            lanes.push_back(lane);
        } else if (found->second < column_count) {
            lanes.push_back({.id = lane.id,
                             .name = "BGM " + std::to_string(found->second + 1),
                             .source_order = column_first_order.at(found->second)});
        }
    }
    chart.bgm_source_lanes = std::move(lanes);
}

} // namespace

auto normalize_legacy_bms_bgm(song_document_t& song) -> bool {
    bool changed = false;
    for (auto& chart : song.charts) {
        const auto before = chart.bgm_source_lanes.size();
        const auto dropped = drop_zero_slot_events(chart);
        const auto legacy = !ranked_legacy_lanes(chart).empty();
        rebuild_bgm_columns(chart);
        changed |= dropped || legacy || chart.bgm_source_lanes.size() != before;
    }
    return changed;
}

} // namespace wormedit::native_project_internal

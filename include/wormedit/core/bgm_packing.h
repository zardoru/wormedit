#pragma once

#include <wormedit/core/result.h>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace wormedit {

struct bgm_packing_key_t {
    bool group{};
    std::uint64_t value{};
    auto operator<=>(const bgm_packing_key_t&) const = default;
};

struct bgm_packing_request_t {
    std::optional<bgm_packing_key_t> key;
    std::uint64_t row{};
};

struct bgm_packing_existing_t {
    std::optional<bgm_packing_key_t> key;
    std::uint64_t row{};
    std::size_t lane{};
};

struct bgm_packing_error_t {
    std::size_t initial_lane_count{};
    std::string message;
    auto operator==(const bgm_packing_error_t&) const -> bool = default;
};

struct bgm_column_assignment_t {
    [[nodiscard]] static auto
    from(std::span<const bgm_packing_request_t> requests,
         std::span<const bgm_packing_existing_t> existing = {}, std::size_t existing_lane_count = 0,
         std::size_t maximum_lanes = std::numeric_limits<std::size_t>::max())
        -> result_t<bgm_column_assignment_t, bgm_packing_error_t>;
    std::vector<std::size_t> lanes;
    std::size_t lane_count{};
    auto operator==(const bgm_column_assignment_t&) const -> bool = default;
};
using bgm_packing_outcome_t = result_t<bgm_column_assignment_t, bgm_packing_error_t>;

struct bgm_packing_result_t {
    [[nodiscard]] static auto from(bgm_packing_outcome_t&& outcome) -> bgm_packing_result_t {
        if (!outcome)
            return {.lanes = {},
                    .lane_count = outcome.error().initial_lane_count,
                    .error = std::move(outcome.error().message)};
        return {.lanes = std::move(outcome->lanes), .lane_count = outcome->lane_count, .error = {}};
    }

    std::vector<std::size_t> lanes;
    std::size_t lane_count{};
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept {
        return error.empty();
    }
};

namespace bgm_packing_detail {

struct group_t {
    bgm_packing_key_t key;
    std::vector<std::size_t> requests;
    std::vector<std::size_t> preferred;
};

struct state_t {
    bgm_column_assignment_t result;
    std::size_t initial_lane_count{};
    std::size_t maximum_lanes{};
    std::vector<std::set<std::uint64_t>> occupied;

    auto load_existing(std::span<const bgm_packing_existing_t> existing)
        -> result_t<void, std::string> {
        for (const auto& event : existing) {
            if (event.lane >= result.lane_count)
                return std::unexpected(std::string{"existing event lane is out of range"});
            if (event.key && event.key->value == 0)
                return std::unexpected(std::string{"existing event key is invalid"});
            occupied[event.lane].insert(event.row);
        }
        return {};
    }

    auto lowest_free(std::uint64_t row) const -> std::optional<std::size_t> {
        for (std::size_t lane = 0; lane < occupied.size(); ++lane)
            if (!occupied[lane].contains(row))
                return lane;
        return std::nullopt;
    }

    auto create_lane(std::uint64_t row) -> std::optional<std::size_t> {
        if (result.lane_count >= maximum_lanes)
            return std::nullopt;
        const auto lane = result.lane_count++;
        occupied.emplace_back();
        occupied.back().insert(row);
        return lane;
    }

    auto assign(std::size_t request, std::uint64_t row, std::size_t lane) -> void {
        occupied[lane].insert(row);
        result.lanes[request] = lane;
    }

    auto pick(std::uint64_t row, std::span<const std::size_t> preferred)
        -> std::optional<std::size_t> {
        for (const auto lane : preferred)
            if (!occupied[lane].contains(row))
                return lane;
        if (const auto lane = lowest_free(row))
            return lane;
        return create_lane(row);
    }
};

inline auto process_group(state_t& state, group_t& group,
                          std::span<const bgm_packing_request_t> requests)
    -> result_t<void, std::string> {
    std::ranges::stable_sort(group.requests, {},
                             [&](std::size_t index) { return requests[index].row; });
    for (const auto index : group.requests) {
        const auto lane = state.pick(requests[index].row, group.preferred);
        if (!lane)
            return std::unexpected(std::string{"maximum BGM column count exceeded"});
        state.assign(index, requests[index].row, *lane);
        if (std::ranges::find(group.preferred, *lane) == group.preferred.end())
            group.preferred.push_back(*lane);
    }
    return {};
}

inline auto group_requests(std::span<const bgm_packing_request_t> requests,
                           std::vector<group_t>& groups, std::vector<std::size_t>& silent)
    -> result_t<void, std::string> {
    std::map<bgm_packing_key_t, std::size_t> group_indexes;
    for (std::size_t index = 0; index < requests.size(); ++index) {
        const auto& key = requests[index].key;
        if (!key) {
            silent.push_back(index);
            continue;
        }
        if (key->value == 0)
            return std::unexpected(std::string{"request key is invalid"});
        auto found = group_indexes.find(*key);
        if (found == group_indexes.end()) {
            const auto group_index = groups.size();
            groups.push_back({.key = *key, .requests = {}, .preferred = {}});
            found = group_indexes.emplace(*key, group_index).first;
        }
        groups[found->second].requests.push_back(index);
    }
    return {};
}

inline auto process_silent(state_t& state, std::span<const bgm_packing_request_t> requests,
                           std::span<const std::size_t> silent) -> result_t<void, std::string> {
    for (const auto index : silent) {
        const auto lane = state.lowest_free(requests[index].row);
        const auto selected = lane ? lane : state.create_lane(requests[index].row);
        if (!selected)
            return std::unexpected(std::string{"maximum BGM column count exceeded"});
        state.assign(index, requests[index].row, *selected);
    }
    return {};
}

inline auto seed_preferences(std::vector<group_t>& groups,
                             std::span<const bgm_packing_existing_t> existing) -> void {
    std::map<bgm_packing_key_t, std::map<std::size_t, std::size_t>> counts;
    for (const auto& event : existing)
        if (event.key)
            ++counts[*event.key][event.lane];
    for (auto& group : groups) {
        const auto found = counts.find(group.key);
        if (found == counts.end())
            continue;
        std::vector<std::pair<std::size_t, std::size_t>> lanes(found->second.begin(),
                                                               found->second.end());
        std::ranges::sort(lanes, [](const auto& left, const auto& right) {
            if (left.second != right.second)
                return left.second > right.second;
            return left.first < right.first;
        });
        for (const auto& [lane, count] : lanes) {
            (void)count;
            group.preferred.push_back(lane);
        }
    }
}

} // namespace bgm_packing_detail

inline auto bgm_column_assignment_t::from(std::span<const bgm_packing_request_t> requests,
                                          std::span<const bgm_packing_existing_t> existing,
                                          std::size_t existing_lane_count,
                                          std::size_t maximum_lanes) -> bgm_packing_outcome_t {
    using namespace bgm_packing_detail;
    state_t state{.result = {.lanes = {}, .lane_count = existing_lane_count},
                  .initial_lane_count = existing_lane_count,
                  .maximum_lanes = maximum_lanes,
                  .occupied = {}};
    if (existing_lane_count > maximum_lanes) {
        return std::unexpected(bgm_packing_error_t{
            state.initial_lane_count, "existing BGM column count exceeds the configured maximum"});
    }
    state.occupied.resize(existing_lane_count);
    if (auto loaded = state.load_existing(existing); !loaded)
        return std::unexpected(
            bgm_packing_error_t{state.initial_lane_count, std::move(loaded.error())});

    std::vector<group_t> groups;
    std::vector<std::size_t> silent;
    state.result.lanes.resize(requests.size());
    if (auto grouped = group_requests(requests, groups, silent); !grouped)
        return std::unexpected(
            bgm_packing_error_t{state.initial_lane_count, std::move(grouped.error())});
    seed_preferences(groups, existing);
    for (auto& group : groups) {
        if (auto processed = process_group(state, group, requests); !processed)
            return std::unexpected(
                bgm_packing_error_t{state.initial_lane_count, std::move(processed.error())});
    }
    if (auto processed = process_silent(state, requests, silent); !processed)
        return std::unexpected(
            bgm_packing_error_t{state.initial_lane_count, std::move(processed.error())});
    return std::move(state.result);
}

[[nodiscard]] inline auto allocate_bgm_columns(
    std::span<const bgm_packing_request_t> requests,
    std::span<const bgm_packing_existing_t> existing = {}, std::size_t existing_lane_count = 0,
    std::size_t maximum_lanes = std::numeric_limits<std::size_t>::max()) -> bgm_packing_result_t {
    return bgm_packing_result_t::from(
        bgm_column_assignment_t::from(requests, existing, existing_lane_count, maximum_lanes));
}

} // namespace wormedit

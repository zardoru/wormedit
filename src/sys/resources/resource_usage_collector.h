#pragma once
#include <algorithm>
#include <cstddef>
#include <queue>
#include <utility>
#include <vector>

namespace wormedit::sys {
inline constexpr std::size_t resource_usage_projection_limit = 1024;

// Retain the earliest bounded uses even when authored storage is out of position order.
template <typename Row, typename Order> struct resource_usage_collector_t {
    struct candidate_t { Order order; Row row; };
    struct latest_first_t {
        auto operator()(const candidate_t& a, const candidate_t& b) const -> bool {
            return a.order < b.order;
        }
    };
    std::priority_queue<candidate_t, std::vector<candidate_t>, latest_first_t> latest;
    std::size_t total{};
    void add(Row row, Order order) {
        ++total;
        if (latest.size() < resource_usage_projection_limit) {
            latest.push({std::move(order), std::move(row)});
        } else if (order < latest.top().order) {
            latest.pop();
            latest.push({std::move(order), std::move(row)});
        }
    }
    auto ordered_rows() -> std::vector<Row> {
        std::vector<candidate_t> candidates;
        candidates.reserve(latest.size());
        while (!latest.empty()) {
            candidates.push_back(latest.top());
            latest.pop();
        }
        std::ranges::sort(candidates, {}, &candidate_t::order);
        std::vector<Row> rows;
        rows.reserve(candidates.size());
        for (auto& candidate : candidates) rows.push_back(std::move(candidate.row));
        return rows;
    }
};
} // namespace wormedit::sys

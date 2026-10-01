#include "common/static_interval_index.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace wormedit::common {
void static_interval_index_t::rebuild(std::span<const static_interval_t> intervals) {
    nodes_.clear();
    nodes_.reserve(intervals.size());
    for (const auto& interval : intervals) {
        if (std::isfinite(interval.begin) && std::isfinite(interval.end) &&
            interval.begin <= interval.end)
            nodes_.push_back({interval, interval.end});
    }
    std::ranges::sort(nodes_, {}, [](const auto& node) { return node.interval.begin; });
    augment(0, nodes_.size());
}
auto static_interval_index_t::augment(std::size_t begin, std::size_t end) -> double {
    if (begin == end)
        return -std::numeric_limits<double>::infinity();
    const auto middle = begin + (end - begin) / 2;
    auto& node = nodes_[middle];
    node.maximum_end =
        std::max({node.interval.end, augment(begin, middle), augment(middle + 1, end)});
    return node.maximum_end;
}
auto static_interval_index_t::query_into(double begin, double end,
                                         std::span<std::size_t> output) const
    -> bounded_interval_query_t {
    bounded_interval_query_t result;
    if (!std::isfinite(begin) || !std::isfinite(end) || begin > end)
        return result;
    auto emit = [&](std::size_t index) {
        if (result.count < output.size())
            output[result.count++] = index;
        else
            result.overflow = true;
    };
    intersect(0, nodes_.size(), begin, end, emit, result.scanned);
    std::ranges::sort(output.first(result.count));
    return result;
}
auto static_interval_index_t::query(double begin, double end) const -> interval_query_t {
    interval_query_t result;
    if (!std::isfinite(begin) || !std::isfinite(end) || begin > end)
        return result;
    auto emit = [&](std::size_t index) { result.indices.push_back(index); };
    intersect(0, nodes_.size(), begin, end, emit, result.scanned);
    std::ranges::sort(result.indices);
    return result;
}
auto static_interval_index_t::accounted_bytes() const noexcept -> std::size_t {
    return nodes_.capacity() * sizeof(node_t);
}
} // namespace wormedit::common

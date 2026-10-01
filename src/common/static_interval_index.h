#pragma once
#include <cstddef>
#include <span>
#include <vector>
namespace wormedit::common {
// Augmented interval tree keeps lookup proportional to intersecting records.
// Results retain source painter order, including overlapping long holds.
struct static_interval_t {
    double begin{}, end{};
    std::size_t source_index{};
};
struct bounded_interval_query_t {
    std::size_t count{}, scanned{};
    bool overflow{};
};
struct interval_query_t {
    std::vector<std::size_t> indices;
    std::size_t scanned{};
};
class static_interval_index_t {
  public:
    void rebuild(std::span<const static_interval_t> intervals);
    auto query(double begin, double end) const -> interval_query_t;
    auto query_into(double begin, double end, std::span<std::size_t> output) const
        -> bounded_interval_query_t;
    auto accounted_bytes() const noexcept -> std::size_t;

  private:
    struct node_t {
        static_interval_t interval;
        double maximum_end{};
    };
    std::vector<node_t> nodes_;
    auto augment(std::size_t begin, std::size_t end) -> double;
    template <class Emit>
    void intersect(std::size_t first, std::size_t last, double begin, double end, Emit& emit,
                   std::size_t& scanned) const {
        if (first == last)
            return;
        const auto middle = first + (last - first) / 2;
        const auto& node = nodes_[middle];
        ++scanned;
        if (node.maximum_end < begin)
            return;
        intersect(first, middle, begin, end, emit, scanned);
        if (node.interval.begin > end)
            return;
        if (node.interval.end >= begin)
            emit(node.interval.source_index);
        intersect(middle + 1, last, begin, end, emit, scanned);
    }
};
} // namespace wormedit::common

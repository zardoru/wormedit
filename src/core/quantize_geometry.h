#pragma once

#include <wormedit/core/document.h>

#include <algorithm>
#include <iterator>
#include <limits>
#include <unordered_map>

namespace wormedit::detail {

// Call-local lookup borrowing an index produced by build_measure_index. Entries
// remain in authored order; duplicate-ID lookup retains the first entry.
class quantize_geometry_t {
public:
    explicit quantize_geometry_t(const measure_index_t& index) : index_(index) {
        entries_.reserve(index.entries.size());
        for (const auto& entry : index.entries) entries_.emplace(entry.id.value, &entry);
    }

    [[nodiscard]] auto entry(measure_id_t id) const -> const measure_index_entry_t* {
        const auto found = entries_.find(id.value);
        return found == entries_.end() ? nullptr : found->second;
    }

    [[nodiscard]] auto absolute(local_position_t position) const
        -> std::optional<std::uint64_t> {
        const auto* found = entry(position.measure);
        if (found == nullptr || position.row >= found->row_capacity ||
            position.row > std::numeric_limits<std::uint64_t>::max() - found->absolute_begin_row)
            return std::nullopt;
        return found->absolute_begin_row + position.row;
    }

    [[nodiscard]] auto position(std::uint64_t row) const -> std::optional<local_position_t> {
        if (row >= index_.total_rows) return std::nullopt;
        const auto found = std::upper_bound(index_.entries.begin(), index_.entries.end(), row,
            [](auto value, const auto& candidate) { return value < candidate.absolute_begin_row; });
        if (found == index_.entries.begin()) return std::nullopt;
        const auto& previous = *std::prev(found);
        return local_position_t{.measure = previous.id, .row = row - previous.absolute_begin_row};
    }

private:
    const measure_index_t& index_;
    std::unordered_map<std::uint64_t, const measure_index_entry_t*> entries_;
};

} // namespace wormedit::detail

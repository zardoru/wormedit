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
    explicit quantize_geometry_t(const measure_index_t& index)
        : index_(index), entries_(measure_lookup_t::from(index)) {}

    [[nodiscard]] auto entry(measure_id_t id) const -> const measure_index_entry_t* {
        return entries_.entry_for(id);
    }

    [[nodiscard]] auto absolute(local_position_t position) const
        -> std::optional<std::uint64_t> {
        return entries_.row_of(position);
    }

    [[nodiscard]] auto position(std::uint64_t row) const -> std::optional<local_position_t> {
        return index_.position_at(row);
    }

private:
    const measure_index_t& index_;
    measure_lookup_t entries_;
};

} // namespace wormedit::detail

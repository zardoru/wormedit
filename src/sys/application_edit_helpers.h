#pragma once

#include <wormedit/core/grid.h>

#include <cstdint>
#include <optional>

namespace wormedit::sys {

auto timing_position_at_row(const wormedit::measure_index_t& measures, std::uint64_t row)
    -> std::optional<wormedit::local_position_t>;
auto parse_timing_signed(const char* text) -> std::optional<std::int64_t>;

} // namespace wormedit::sys

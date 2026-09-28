#include "sys/application_edit_helpers.h"

#include <charconv>
#include <cstring>

#include <algorithm>

namespace wormedit::sys {

auto timing_position_at_row(const wormedit::measure_index_t& measures, std::uint64_t row)
    -> std::optional<wormedit::local_position_t> {
    const auto found = std::ranges::find_if(measures.entries, [row](const auto& entry) {
        return row >= entry.absolute_begin_row &&
               row - entry.absolute_begin_row < entry.row_capacity;
    });
    if (found == measures.entries.end()) {
        return std::nullopt;
    }
    return wormedit::local_position_t{.measure = found->id, .row = row - found->absolute_begin_row};
}


auto parse_timing_signed(const char* text) -> std::optional<std::int64_t> {
    if (!text || text[0] == '\0') {
        return std::nullopt;
    }
    const auto* end = text + std::strlen(text);
    std::int64_t value{};
    const auto parsed = std::from_chars(text, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end ? std::optional{value} : std::nullopt;
}


} // namespace wormedit::sys

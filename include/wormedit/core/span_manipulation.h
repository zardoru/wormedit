#pragma once

#include <cstdint>
#include <optional>

namespace wormedit {

// The one geometry of dragging a span (a Kiai area, a Speed transition, an SV recipe): grab the
// begin edge, the end edge or the body. Rows are absolute chart rows. Callers snap the rows first
// (see snap.h) and apply the result in their own object model.
enum class span_handle_t {
    begin,
    end,
    body,
};

struct span_rows_t {
    std::uint64_t begin{};
    std::uint64_t end{};

    auto operator==(const span_rows_t&) const -> bool = default;
};

struct span_manipulation_options_t {
    // A Speed may be instant (begin == end); Kiai areas and recipes may not be empty.
    bool allow_empty{false};
};

// The span after the pointer moves to `pointer_row`. An edge goes to the pointer and the other edge
// stays, swapping when they cross. The body shifts by the travel from `anchor_row` to
// `pointer_row`, clamped so it keeps its length inside [0, total_rows]. Nullopt when the result
// would be empty (unless allowed) or the geometry is invalid.
[[nodiscard]] auto manipulate_span(span_rows_t original, span_handle_t handle,
                                   std::uint64_t anchor_row, std::uint64_t pointer_row,
                                   std::uint64_t total_rows,
                                   span_manipulation_options_t options = {})
    -> std::optional<span_rows_t>;

} // namespace wormedit

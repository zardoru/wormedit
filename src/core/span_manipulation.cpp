#include <wormedit/core/span_manipulation.h>

#include <algorithm>

namespace wormedit {

auto manipulate_span(span_rows_t original, span_handle_t handle, std::uint64_t anchor_row,
                     std::uint64_t pointer_row, std::uint64_t total_rows,
                     span_manipulation_options_t options) -> std::optional<span_rows_t> {
    if (original.begin > original.end || original.end > total_rows) return std::nullopt;
    span_rows_t result = original;
    switch (handle) {
    case span_handle_t::begin:
        result = {std::min(pointer_row, original.end), std::max(pointer_row, original.end)};
        break;
    case span_handle_t::end:
        result = {std::min(original.begin, pointer_row), std::max(original.begin, pointer_row)};
        break;
    case span_handle_t::body: {
        const auto length = original.end - original.begin;
        const auto travel = static_cast<std::int64_t>(pointer_row) - static_cast<std::int64_t>(anchor_row);
        auto begin = static_cast<std::int64_t>(original.begin) + travel;
        begin = std::clamp<std::int64_t>(begin, 0, static_cast<std::int64_t>(total_rows - length));
        result = {static_cast<std::uint64_t>(begin), static_cast<std::uint64_t>(begin) + length};
        break;
    }
    }
    if (result.begin == result.end && !options.allow_empty) return std::nullopt;
    return result;
}

} // namespace wormedit

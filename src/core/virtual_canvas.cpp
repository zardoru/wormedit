#include <wormedit/core/virtual_canvas.h>

#include <algorithm>
#include <iterator>
#include <limits>
#include <ranges>

namespace wormedit {

auto virtual_canvas_t::measure_at(std::size_t number) const
    -> std::optional<virtual_canvas_measure_t> {
    if (number >= measure_count)
        return std::nullopt;
    if (number < authored_index.entries.size()) {
        const auto& entry = authored_index.entries[number];
        return virtual_canvas_measure_t{number, entry.absolute_begin_row, entry.row_capacity, true};
    }
    const auto offset = number - authored_index.entries.size();
    return virtual_canvas_measure_t{number,
                                    authored_index.total_rows + offset * default_measure_rows,
                                    default_measure_rows, false};
}

auto virtual_canvas_t::measure_containing(std::uint64_t row) const
    -> std::optional<virtual_canvas_measure_t> {
    if (row >= total_rows)
        return std::nullopt;
    if (row < authored_index.total_rows) {
        const auto after = std::ranges::upper_bound(authored_index.entries, row, {},
                                                    &measure_index_entry_t::absolute_begin_row);
        if (after == authored_index.entries.begin())
            return std::nullopt;
        return measure_at(static_cast<std::size_t>(
            std::distance(authored_index.entries.begin(), std::prev(after))));
    }
    if (default_measure_rows == 0)
        return std::nullopt;
    const auto number =
        authored_index.entries.size() + (row - authored_index.total_rows) / default_measure_rows;
    return measure_at(number);
}

auto build_virtual_canvas(const chart_document_t& chart)
    -> std::variant<virtual_canvas_t, diagnostic_list_t> {
    auto built = build_measure_index(chart);
    if (std::holds_alternative<diagnostic_list_t>(built))
        return std::get<diagnostic_list_t>(std::move(built));
    auto index = std::get<measure_index_t>(std::move(built));
    const auto count = chart.source_family == format_family_t::bms
                           ? std::max(chart.measures.size(), bms_addressable_measure_count)
                           : chart.measures.size();
    const auto tail = count - index.entries.size();
    const auto default_rows = chart.divisions_per_standard_measure;
    if (default_rows == 0 ||
        tail > (std::numeric_limits<std::uint64_t>::max() - index.total_rows) / default_rows) {
        return diagnostic_list_t{{.severity = diagnostic_severity_t::error,
                                  .code = diagnostic_code_t::invalid_measure,
                                  .message = "virtual canvas row extent overflows",
                                  .object = std::nullopt}};
    }
    const auto authored_rows = index.total_rows;
    return virtual_canvas_t{.authored_index = std::move(index),
                            .measure_count = count,
                            .total_rows = authored_rows + tail * default_rows,
                            .default_measure_rows = default_rows};
}

} // namespace wormedit

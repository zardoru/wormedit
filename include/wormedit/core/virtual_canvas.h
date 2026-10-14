#pragma once

#include <wormedit/core/document.h>

#include <optional>
#include <variant>

namespace wormedit {

inline constexpr std::size_t bms_addressable_measure_count = 1'000;

struct virtual_canvas_measure_t {
    std::size_t number{};
    std::uint64_t begin_row{};
    std::uint64_t row_capacity{};
    bool authored{};
};

// The authored index contains only document measures. The BMS tail uses one
// arithmetic default span, so opening an empty #000-#999 canvas does not add
// buckets or IDs to the chart.
struct virtual_canvas_t {
    measure_index_t authored_index;
    std::size_t measure_count{};
    std::uint64_t total_rows{};
    std::uint64_t default_measure_rows{};

    [[nodiscard]] auto measure_at(std::size_t number) const
        -> std::optional<virtual_canvas_measure_t>;
    [[nodiscard]] auto measure_containing(std::uint64_t row) const
        -> std::optional<virtual_canvas_measure_t>;
};

inline constexpr std::size_t native_unanchored_virtual_measure_count = 64;

// A chart exposes a virtual tail when its format fixes an addressable range (BMS)
// or when it has no time anchor to bound it (native chart without timed BGM).
[[nodiscard]] auto virtual_tail_measure_limit(const chart_document_t& chart) -> std::size_t;
[[nodiscard]] auto has_chart_virtual_tail(const chart_document_t& chart) -> bool;

// Recomputes chart.virtual_tail_measures for every chart from the document's BGM.
void sync_virtual_tails(song_document_t& document);

[[nodiscard]] auto build_virtual_canvas(const chart_document_t& chart)
    -> std::variant<virtual_canvas_t, diagnostic_list_t>;

} // namespace wormedit

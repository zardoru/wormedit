#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace wormedit {

// A chart-row coordinate whose fractional component is canonical and in [0, 1).
// Keeping whole and fractional parts separate lets grid lines stay exact even
// when a beat does not land on an editable note row or the chart is very long.
struct exact_chart_row_t {
    std::uint64_t whole_rows{};
    rational_t fractional_rows{};

    auto operator==(const exact_chart_row_t&) const -> bool = default;
};

struct editor_grid_position_t {
    // Zero identifies a virtual, unmaterialized BMS measure.
    measure_id_t measure;
    std::uint64_t whole_rows{};
    rational_t fractional_rows{};

    auto operator==(const editor_grid_position_t&) const -> bool = default;
};

enum class editor_grid_line_kind_t {
    active_snap,
    beat,
    group,
    measure,
};

enum class editor_grid_emphasis_t {
    subtle,
    normal,
    strong,
    strongest,
};

struct editor_grid_line_t {
    editor_grid_position_t position;
    exact_chart_row_t chart_row;
    editor_grid_line_kind_t kind{editor_grid_line_kind_t::active_snap};
    editor_grid_emphasis_t emphasis{editor_grid_emphasis_t::subtle};
    auto operator==(const editor_grid_line_t&) const -> bool = default;
};

// Optional authored grouping for one measure. Each entry is a count of
// denominator-note beats (for example, {3, 3} for 6/8 grouped as 3+3).
struct editor_grid_grouping_t {
    measure_id_t measure;
    std::vector<std::uint32_t> beat_counts;
};

struct editor_grid_density_t {
    // Measure boundaries are retained as structural anchors. Zero disables
    // density suppression for the corresponding subdivisions.
    double minimum_group_spacing_pixels{8.0};
    double minimum_beat_spacing_pixels{7.0};
    double minimum_active_snap_spacing_pixels{4.0};
};

struct editor_grid_request_t {
    // Half-open global integer-row interval [begin, end). Fractional grid
    // lines are visible when their whole_rows component lies in this interval.
    std::uint64_t visible_begin_row{};
    std::uint64_t visible_end_row{};
    std::optional<std::uint64_t> active_snap_denominator;
    bool include_virtual_bms_tail{};
    double pixels_per_standard_measure{192.0};
    editor_grid_density_t density;
    std::vector<editor_grid_grouping_t> authored_groupings;
    std::size_t maximum_line_count{100'000};
};

struct editor_grid_t {
    // Builds exact, renderer-neutral measure, beat, group, and active-snap lines
    // for a visible chart-row range. Zoom affects subdivision visibility only;
    // returned positions always remain on their exact authored coordinates.
    [[nodiscard]] static auto from(const chart_document_t& chart, const editor_grid_request_t& request)
        -> diagnosed_t<editor_grid_t>;

    std::vector<editor_grid_line_t> lines;
    auto operator==(const editor_grid_t&) const -> bool = default;
};

} // namespace wormedit

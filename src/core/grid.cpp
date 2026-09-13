#include <wormedit/core/grid.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace wormedit {
namespace {

constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

struct row_offset_t {
    std::uint64_t whole{};
    rational_t fraction{0, 1};
};

auto error(diagnostic_code_t code, std::string message) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error, .code = code, .message = std::move(message)};
}

auto add_checked(std::uint64_t left, std::uint64_t right, std::uint64_t& result) -> bool {
    if (right > maximum - left) {
        return false;
    }
    result = left + right;
    return true;
}

auto multiply_checked(std::uint64_t left, std::uint64_t right, std::uint64_t& result) -> bool {
    if (left != 0 && right > maximum / left) {
        return false;
    }
    result = left * right;
    return true;
}

auto normalized_fraction(std::uint64_t numerator, std::uint64_t denominator) -> rational_t {
    if (numerator == 0) {
        return {0, 1};
    }
    const auto divisor = std::gcd(numerator, denominator);
    return {numerator / divisor, denominator / divisor};
}

// Exact comparison of proper fractions without a potentially overflowing
// cross-product.
auto compare_fraction(rational_t left, rational_t right) -> int {
    auto direction = 1;
    for (;;) {
        const auto left_integer = left.numerator / left.denominator;
        const auto right_integer = right.numerator / right.denominator;
        if (left_integer != right_integer) {
            return direction * (left_integer < right_integer ? -1 : 1);
        }
        const auto left_remainder = left.numerator % left.denominator;
        const auto right_remainder = right.numerator % right.denominator;
        if (left_remainder == 0 || right_remainder == 0) {
            if (left_remainder == 0 && right_remainder == 0) {
                return 0;
            }
            return left_remainder == 0 ? -direction : direction;
        }
        left = {left.denominator, left_remainder};
        right = {right.denominator, right_remainder};
        direction = -direction;
    }
}

auto compare_row(const exact_chart_row_t& left, const exact_chart_row_t& right) -> int {
    if (left.whole_rows != right.whole_rows) {
        return left.whole_rows < right.whole_rows ? -1 : 1;
    }
    return compare_fraction(left.fractional_rows, right.fractional_rows);
}

auto same_row(const exact_chart_row_t& left, const exact_chart_row_t& right) -> bool {
    return compare_row(left, right) == 0;
}

// Returns the exact row coordinate divisions * beat_index / denominator.
// Splitting both operands before multiplication keeps intermediate products
// bounded while retaining a fractional row when the grid is finer than the
// editable note resolution.
auto offset_for_beat(std::uint64_t beat_index, std::uint64_t divisions, std::uint64_t denominator)
    -> std::optional<row_offset_t> {
    if (denominator == 0) {
        return std::nullopt;
    }
    const auto beat_quotient = beat_index / denominator;
    const auto beat_remainder = beat_index % denominator;
    std::uint64_t whole{};
    if (!multiply_checked(beat_quotient, divisions, whole)) {
        return std::nullopt;
    }
    const auto divisions_quotient = divisions / denominator;
    const auto divisions_remainder = divisions % denominator;
    std::uint64_t partial{};
    std::uint64_t remainder_product{};
    if (!multiply_checked(beat_remainder, divisions_quotient, partial) ||
        !multiply_checked(beat_remainder, divisions_remainder, remainder_product) ||
        !add_checked(partial, remainder_product / denominator, partial) ||
        !add_checked(whole, partial, whole)) {
        return std::nullopt;
    }
    return row_offset_t{
        .whole = whole,
        .fraction = normalized_fraction(remainder_product % denominator, denominator),
    };
}

auto absolute_position(std::uint64_t measure_begin, row_offset_t offset)
    -> std::optional<exact_chart_row_t> {
    std::uint64_t whole{};
    if (!add_checked(measure_begin, offset.whole, whole)) {
        return std::nullopt;
    }
    return exact_chart_row_t{
        .whole_rows = whole,
        .fractional_rows =
            normalized_fraction(offset.fraction.numerator, offset.fraction.denominator),
    };
}

auto is_interior(row_offset_t offset, std::uint64_t capacity) -> bool {
    return offset.whole < capacity;
}

auto at_or_after(std::uint64_t measure_begin, row_offset_t offset, std::uint64_t row) -> bool {
    const auto absolute = absolute_position(measure_begin, offset);
    return absolute.has_value() && absolute->whole_rows >= row;
}

auto last_interior_beat(std::uint64_t divisions, std::uint64_t denominator, std::uint64_t capacity)
    -> std::uint64_t {
    auto low = std::uint64_t{};
    auto high = maximum;
    while (low < high) {
        const auto distance = high - low;
        const auto middle = low + distance / 2 + distance % 2;
        const auto offset = offset_for_beat(middle, divisions, denominator);
        if (offset.has_value() && is_interior(*offset, capacity)) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    return low;
}

auto first_visible_beat(std::uint64_t divisions, std::uint64_t denominator,
                        const measure_index_entry_t& entry, std::uint64_t visible_begin,
                        std::uint64_t last_beat) -> std::optional<std::uint64_t> {
    if (last_beat == 0) {
        return std::nullopt;
    }
    const auto last = offset_for_beat(last_beat, divisions, denominator);
    if (!last.has_value() || !at_or_after(entry.absolute_begin_row, *last, visible_begin)) {
        return std::nullopt;
    }
    auto low = std::uint64_t{1};
    auto high = last_beat;
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        const auto offset = offset_for_beat(middle, divisions, denominator);
        if (offset.has_value() && at_or_after(entry.absolute_begin_row, *offset, visible_begin)) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    const auto offset = offset_for_beat(low, divisions, denominator);
    if (!offset.has_value() || !is_interior(*offset, entry.row_capacity) ||
        !at_or_after(entry.absolute_begin_row, *offset, visible_begin)) {
        return std::nullopt;
    }
    return low;
}

auto default_group_boundaries(meter_t meter) -> std::vector<std::uint32_t> {
    std::vector<std::uint32_t> result;
    if (meter.denominator == 8 &&
        (meter.numerator == 6 || meter.numerator == 9 || meter.numerator == 12)) {
        for (std::uint64_t beat = 3; beat < meter.numerator; beat += 3) {
            result.push_back(static_cast<std::uint32_t>(beat));
        }
    } else if (meter.numerator == 5) {
        result.push_back(3); // conventional 3+2
    } else if (meter.numerator == 7) {
        result = {2, 4}; // conventional 2+2+3
    } else if (meter.numerator == 4) {
        result.push_back(2); // conventional 2+2
    }
    return result;
}

auto emphasis_for(editor_grid_line_kind_t kind) -> editor_grid_emphasis_t {
    switch (kind) {
    case editor_grid_line_kind_t::active_snap:
        return editor_grid_emphasis_t::subtle;
    case editor_grid_line_kind_t::beat:
        return editor_grid_emphasis_t::normal;
    case editor_grid_line_kind_t::group:
        return editor_grid_emphasis_t::strong;
    case editor_grid_line_kind_t::measure:
        return editor_grid_emphasis_t::strongest;
    }
    return editor_grid_emphasis_t::subtle;
}

auto priority(editor_grid_line_kind_t kind) -> int {
    return static_cast<int>(kind);
}

auto error_if_invalid(const chart_document_t& chart, const editor_grid_request_t& request,
                      std::uint64_t total_rows) -> diagnostic_list_t {
    diagnostic_list_t result;
    if (request.visible_begin_row >= request.visible_end_row ||
        request.visible_end_row > total_rows) {
        result.push_back(error(diagnostic_code_t::row_out_of_range,
                               "visible editor grid range is outside chart geometry"));
    }
    if (!std::isfinite(request.pixels_per_standard_measure) ||
        request.pixels_per_standard_measure <= 0.0 || request.maximum_line_count == 0 ||
        (request.active_snap_denominator.has_value() &&
         (*request.active_snap_denominator == 0 ||
          chart.divisions_per_standard_measure % *request.active_snap_denominator != 0))) {
        result.push_back(error(diagnostic_code_t::invalid_resolution,
                               "editor grid zoom, snap, or line limit is invalid"));
    }
    const auto spacing_valid = [](double value) { return std::isfinite(value) && value >= 0.0; };
    if (!spacing_valid(request.density.minimum_group_spacing_pixels) ||
        !spacing_valid(request.density.minimum_beat_spacing_pixels) ||
        !spacing_valid(request.density.minimum_active_snap_spacing_pixels)) {
        result.push_back(error(diagnostic_code_t::invalid_resolution,
                               "editor grid density thresholds must be finite and nonnegative"));
    }
    return result;
}

auto add_candidate(std::vector<editor_grid_line_t>& lines, const measure_index_entry_t& entry,
                   row_offset_t offset, editor_grid_line_kind_t kind, std::size_t maximum_lines,
                   diagnostic_list_t& diagnostics) -> bool {
    const auto absolute = absolute_position(entry.absolute_begin_row, offset);
    if (!absolute.has_value()) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_measure,
                                    "editor grid position overflows chart coordinates"));
        return false;
    }
    if (lines.size() >= maximum_lines) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                    "visible editor grid exceeds its configured line limit"));
        return false;
    }
    lines.push_back({
        .position =
            {
                .measure = entry.id,
                .whole_rows = offset.whole,
                .fractional_rows =
                    normalized_fraction(offset.fraction.numerator, offset.fraction.denominator),
            },
        .chart_row = *absolute,
        .kind = kind,
        .emphasis = emphasis_for(kind),
    });
    return true;
}

auto locate_measure(const measure_index_t& index, std::uint64_t absolute_row)
    -> const measure_index_entry_t* {
    const auto after = std::ranges::upper_bound(index.entries, absolute_row, {},
                                                &measure_index_entry_t::absolute_begin_row);
    if (after == index.entries.begin()) {
        return nullptr;
    }
    const auto& entry = *std::prev(after);
    return absolute_row - entry.absolute_begin_row < entry.row_capacity ? &entry : nullptr;
}

auto spacing_pixels(const exact_chart_row_t& earlier, const exact_chart_row_t& later,
                    double pixels_per_standard_measure, std::uint64_t divisions) -> long double {
    const auto rows = static_cast<long double>(later.whole_rows - earlier.whole_rows) +
                      static_cast<long double>(later.fractional_rows.numerator) /
                          later.fractional_rows.denominator -
                      static_cast<long double>(earlier.fractional_rows.numerator) /
                          earlier.fractional_rows.denominator;
    return rows * static_cast<long double>(pixels_per_standard_measure) /
           static_cast<long double>(divisions);
}

} // namespace

auto editor_grid_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto make_editor_grid_lines(const chart_document_t& chart, const editor_grid_request_t& request)
    -> editor_grid_result_t {
    editor_grid_result_t result;
    const auto built_index = build_measure_index(chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built_index)) {
        result.diagnostics = *diagnostics;
        return result;
    }
    const auto& measure_index = std::get<measure_index_t>(built_index);
    result.diagnostics = error_if_invalid(chart, request, measure_index.total_rows);
    if (!result.diagnostics.empty()) {
        return result;
    }

    std::unordered_map<std::uint64_t, const editor_grid_grouping_t*> authored_groupings;
    for (const auto& grouping : request.authored_groupings) {
        if (!grouping.measure ||
            !authored_groupings.emplace(grouping.measure.value, &grouping).second) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_measure,
                      "editor grid grouping has a missing or duplicate measure ID"));
            continue;
        }
        const auto measure =
            std::ranges::find(chart.measures, grouping.measure, &measure_chunk_t::id);
        if (measure == chart.measures.end() || !measure->definition.meter.has_value()) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::unknown_measure,
                      "editor grid grouping requires a metered chart measure"));
            continue;
        }
        std::uint64_t sum{};
        auto valid = !grouping.beat_counts.empty();
        for (const auto beat_count : grouping.beat_counts) {
            valid = valid && beat_count != 0 && add_checked(sum, beat_count, sum);
        }
        if (!valid || sum != measure->definition.meter->numerator) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_measure,
                      "authored beat grouping must be positive and cover its meter"));
        }
    }
    if (!result.diagnostics.empty()) {
        return result;
    }

    const auto maximum_spacing = std::max({
        request.density.minimum_group_spacing_pixels,
        request.density.minimum_beat_spacing_pixels,
        request.density.minimum_active_snap_spacing_pixels,
    });
    const auto overscan = std::ceil(static_cast<long double>(maximum_spacing) *
                                    static_cast<long double>(chart.divisions_per_standard_measure) /
                                    static_cast<long double>(request.pixels_per_standard_measure));
    std::uint64_t candidate_begin{};
    std::uint64_t candidate_end{};
    if (!std::isfinite(overscan) || overscan >= measure_index.total_rows) {
        candidate_begin = 0;
        candidate_end = measure_index.total_rows;
    } else {
        const auto margin = static_cast<std::uint64_t>(std::max(0.0L, overscan));
        candidate_begin =
            request.visible_begin_row > margin ? request.visible_begin_row - margin : 0;
        candidate_end = request.visible_end_row > measure_index.total_rows - margin
                            ? measure_index.total_rows
                            : request.visible_end_row + margin;
    }

    std::vector<editor_grid_line_t> candidates;
    candidates.reserve(std::min(request.maximum_line_count, std::size_t{4096}));
    for (const auto& entry : measure_index.entries) {
        if (entry.absolute_begin_row >= candidate_end ||
            entry.absolute_begin_row + entry.row_capacity <= candidate_begin) {
            continue;
        }
        if (!add_candidate(candidates, entry, {}, editor_grid_line_kind_t::measure,
                           request.maximum_line_count, result.diagnostics)) {
            return result;
        }
    }

    const auto divisions = chart.divisions_per_standard_measure;
    for (const auto& measure : chart.measures) {
        const auto entry =
            std::ranges::find(measure_index.entries, measure.id, &measure_index_entry_t::id);
        if (entry == measure_index.entries.end() || entry->absolute_begin_row >= candidate_end ||
            entry->absolute_begin_row + entry->row_capacity <= candidate_begin) {
            continue;
        }

        const auto denominator =
            measure.definition.meter.has_value() ? measure.definition.meter->denominator : 4U;
        const auto last_beat =
            measure.definition.meter.has_value()
                ? static_cast<std::uint64_t>(measure.definition.meter->numerator - 1U)
                : last_interior_beat(divisions, denominator, entry->row_capacity);
        const auto beat_spacing =
            request.pixels_per_standard_measure / static_cast<double>(denominator);
        if (beat_spacing >= request.density.minimum_beat_spacing_pixels) {
            const auto first =
                first_visible_beat(divisions, denominator, *entry, candidate_begin, last_beat);
            if (first.has_value()) {
                for (auto beat = *first; beat <= last_beat; ++beat) {
                    const auto offset = offset_for_beat(beat, divisions, denominator);
                    if (!offset.has_value()) {
                        result.diagnostics.push_back(
                            error(diagnostic_code_t::invalid_measure,
                                  "authored beat position overflows exact chart coordinates"));
                        return result;
                    }
                    const auto absolute = absolute_position(entry->absolute_begin_row, *offset);
                    if (!absolute.has_value() || absolute->whole_rows >= candidate_end) {
                        break;
                    }
                    if (absolute->whole_rows >= candidate_begin &&
                        !add_candidate(candidates, *entry, *offset, editor_grid_line_kind_t::beat,
                                       request.maximum_line_count, result.diagnostics)) {
                        return result;
                    }
                    if (beat == last_beat) {
                        break;
                    }
                }
            }
        }

        if (measure.definition.meter.has_value()) {
            std::vector<std::uint32_t> boundaries;
            const auto authored = authored_groupings.find(measure.id.value);
            if (authored != authored_groupings.end()) {
                std::uint32_t cumulative{};
                const auto& counts = authored->second->beat_counts;
                for (std::size_t group = 0; group + 1 < counts.size(); ++group) {
                    cumulative += counts[group];
                    boundaries.push_back(cumulative);
                }
            } else {
                boundaries = default_group_boundaries(*measure.definition.meter);
            }
            for (const auto boundary : boundaries) {
                const auto offset = offset_for_beat(boundary, divisions, denominator);
                if (!offset.has_value()) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_measure,
                              "authored group position overflows exact chart coordinates"));
                    return result;
                }
                const auto absolute = absolute_position(entry->absolute_begin_row, *offset);
                if (!absolute.has_value() || absolute->whole_rows >= candidate_end) {
                    break;
                }
                if (absolute->whole_rows >= candidate_begin &&
                    !add_candidate(candidates, *entry, *offset, editor_grid_line_kind_t::group,
                                   request.maximum_line_count, result.diagnostics)) {
                    return result;
                }
            }
        } else {
            // For an unmetered measure, use quarter-note beats and pair them
            // into groups; the exact measure length still controls the cutoff.
            const auto last_group = last_beat / 2;
            const auto first_beat =
                first_visible_beat(divisions, 4, *entry, candidate_begin, last_beat);
            auto group = first_beat.has_value() ? *first_beat / 2 : last_group + 1;
            if (first_beat.has_value() && *first_beat % 2 != 0) {
                ++group;
            }
            for (; group <= last_group; ++group) {
                const auto offset = offset_for_beat(group * 2, divisions, 4);
                if (!offset.has_value()) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_measure,
                              "default group position overflows exact chart coordinates"));
                    return result;
                }
                const auto absolute = absolute_position(entry->absolute_begin_row, *offset);
                if (!absolute.has_value() || absolute->whole_rows >= candidate_end) {
                    break;
                }
                if (absolute->whole_rows >= candidate_begin &&
                    !add_candidate(candidates, *entry, *offset, editor_grid_line_kind_t::group,
                                   request.maximum_line_count, result.diagnostics)) {
                    return result;
                }
                if (group == last_group || group == maximum) {
                    break;
                }
            }
        }
    }

    if (request.active_snap_denominator.has_value() &&
        request.pixels_per_standard_measure /
                static_cast<double>(*request.active_snap_denominator) >=
            request.density.minimum_active_snap_spacing_pixels) {
        const auto step = divisions / *request.active_snap_denominator;
        auto multiple = candidate_begin / step;
        if (candidate_begin % step != 0) {
            ++multiple;
        }
        if (multiple <= maximum / step) {
            auto absolute_row = multiple * step;
            while (absolute_row < candidate_end) {
                if (const auto* entry = locate_measure(measure_index, absolute_row);
                    entry != nullptr) {
                    if (!add_candidate(candidates, *entry,
                                       {.whole = absolute_row - entry->absolute_begin_row},
                                       editor_grid_line_kind_t::active_snap,
                                       request.maximum_line_count, result.diagnostics)) {
                        return result;
                    }
                }
                if (step > maximum - absolute_row) {
                    break;
                }
                absolute_row += step;
            }
        }
    }

    std::ranges::sort(candidates, [](const auto& left, const auto& right) {
        return compare_row(left.chart_row, right.chart_row) < 0;
    });
    std::vector<editor_grid_line_t> unique_lines;
    unique_lines.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        if (!unique_lines.empty() && same_row(unique_lines.back().chart_row, candidate.chart_row)) {
            if (priority(candidate.kind) > priority(unique_lines.back().kind)) {
                unique_lines.back() = candidate;
            }
        } else {
            unique_lines.push_back(candidate);
        }
    }

    // Thin from strongest to weakest. Previously each member of a close pair
    // could hide the other; retaining the first eligible representative makes
    // density reduction stable while stronger anchors always win.
    std::vector<bool> visible(unique_lines.size(), false);
    for (std::size_t i = 0; i < unique_lines.size(); ++i) {
        visible[i] = unique_lines[i].kind == editor_grid_line_kind_t::measure;
    }
    const auto retain_kind = [&](editor_grid_line_kind_t kind, double threshold) {
        std::optional<std::size_t> previous_same;
        for (std::size_t i = 0; i < unique_lines.size(); ++i) {
            if (unique_lines[i].kind != kind)
                continue;
            auto dense = false;
            for (auto neighbor = i; neighbor > 0;) {
                --neighbor;
                if (!visible[neighbor] || priority(unique_lines[neighbor].kind) <= priority(kind)) {
                    continue;
                }
                dense = spacing_pixels(unique_lines[neighbor].chart_row, unique_lines[i].chart_row,
                                       request.pixels_per_standard_measure,
                                       divisions) < static_cast<long double>(threshold);
                break;
            }
            if (!dense) {
                for (auto neighbor = i + 1; neighbor < unique_lines.size(); ++neighbor) {
                    if (!visible[neighbor] ||
                        priority(unique_lines[neighbor].kind) <= priority(kind)) {
                        continue;
                    }
                    dense =
                        spacing_pixels(unique_lines[i].chart_row, unique_lines[neighbor].chart_row,
                                       request.pixels_per_standard_measure,
                                       divisions) < static_cast<long double>(threshold);
                    break;
                }
            }
            if (!dense && previous_same.has_value() && threshold != 0.0) {
                dense =
                    spacing_pixels(unique_lines[*previous_same].chart_row,
                                   unique_lines[i].chart_row, request.pixels_per_standard_measure,
                                   divisions) < static_cast<long double>(threshold);
            }
            if (!dense || threshold == 0.0) {
                visible[i] = true;
                previous_same = i;
            }
        }
    };
    retain_kind(editor_grid_line_kind_t::group, request.density.minimum_group_spacing_pixels);
    retain_kind(editor_grid_line_kind_t::beat, request.density.minimum_beat_spacing_pixels);
    retain_kind(editor_grid_line_kind_t::active_snap,
                request.density.minimum_active_snap_spacing_pixels);

    for (std::size_t i = 0; i < unique_lines.size(); ++i) {
        const auto& line = unique_lines[i];
        if (visible[i] && line.chart_row.whole_rows >= request.visible_begin_row &&
            line.chart_row.whole_rows < request.visible_end_row) {
            result.lines.push_back(line);
        }
    }
    return result;
}

} // namespace wormedit

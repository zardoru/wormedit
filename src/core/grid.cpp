#include <wormedit/core/grid.h>
#include <wormedit/core/virtual_canvas.h>

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

struct editor_grid_build_context_t {
    const chart_document_t& chart;
    const editor_grid_request_t& request;
    const measure_index_t& measure_index;
    const std::optional<virtual_canvas_t>& virtual_canvas;
    const std::unordered_map<std::uint64_t, const editor_grid_grouping_t*>& authored_groupings;
    editor_grid_result_t& result;
    std::uint64_t candidate_begin{};
    std::uint64_t candidate_end{};
    std::uint64_t divisions{};
    std::vector<editor_grid_line_t> candidates;
};

auto validate_authored_groupings(
    const chart_document_t& chart, const editor_grid_request_t& request,
    std::unordered_map<std::uint64_t, const editor_grid_grouping_t*>& groupings,
    diagnostic_list_t& diagnostics) -> bool {
    for (const auto& grouping : request.authored_groupings) {
        if (!grouping.measure ||
            !groupings.emplace(grouping.measure.value, &grouping).second) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_measure,
                                        "editor grid grouping has a missing or duplicate measure ID"));
            continue;
        }
        const auto measure = std::ranges::find(chart.measures, grouping.measure,
                                               &measure_chunk_t::id);
        if (measure == chart.measures.end() || !measure->definition.meter) {
            diagnostics.push_back(error(diagnostic_code_t::unknown_measure,
                                        "editor grid grouping requires a metered chart measure"));
            continue;
        }
        std::uint64_t sum{};
        auto valid = !grouping.beat_counts.empty();
        for (const auto beat_count : grouping.beat_counts)
            valid = valid && beat_count != 0 && add_checked(sum, beat_count, sum);
        if (!valid || sum != measure->definition.meter->numerator)
            diagnostics.push_back(error(diagnostic_code_t::invalid_measure,
                                        "authored beat grouping must be positive and cover its meter"));
    }
    return diagnostics.empty();
}

auto candidate_interval(const editor_grid_request_t& request, const chart_document_t& chart,
                        std::uint64_t total_rows)
    -> std::pair<std::uint64_t, std::uint64_t> {
    const auto maximum_spacing = std::max({
        request.density.minimum_group_spacing_pixels,
        request.density.minimum_beat_spacing_pixels,
        request.density.minimum_active_snap_spacing_pixels,
    });
    const auto overscan = std::ceil(static_cast<long double>(maximum_spacing) *
                                    static_cast<long double>(chart.divisions_per_standard_measure) /
                                    static_cast<long double>(request.pixels_per_standard_measure));
    if (!std::isfinite(overscan) || overscan >= total_rows)
        return {0, total_rows};
    const auto margin = static_cast<std::uint64_t>(std::max(0.0L, overscan));
    const auto begin = request.visible_begin_row > margin
        ? request.visible_begin_row - margin
        : 0;
    const auto end = request.visible_end_row > total_rows - margin
        ? total_rows
        : request.visible_end_row + margin;
    return {begin, end};
}

auto append_measure_candidates(editor_grid_build_context_t& context) -> bool {
    for (const auto& entry : context.measure_index.entries) {
        if (entry.absolute_begin_row >= context.candidate_end ||
            entry.absolute_begin_row + entry.row_capacity <= context.candidate_begin)
            continue;
        if (!add_candidate(context.candidates, entry, {}, editor_grid_line_kind_t::measure,
                           context.request.maximum_line_count, context.result.diagnostics))
            return false;
    }
    return true;
}

auto append_authored_beats(editor_grid_build_context_t& context,
                           const measure_index_entry_t& entry, std::uint64_t denominator,
                           std::uint64_t last_beat) -> bool {
    const auto spacing = context.request.pixels_per_standard_measure /
                         static_cast<double>(denominator);
    if (spacing < context.request.density.minimum_beat_spacing_pixels)
        return true;
    const auto first = first_visible_beat(context.divisions, denominator, entry,
                                          context.candidate_begin, last_beat);
    if (!first)
        return true;
    for (auto beat = *first; beat <= last_beat; ++beat) {
        const auto offset = offset_for_beat(beat, context.divisions, denominator);
        if (!offset) {
            context.result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_measure,
                      "authored beat position overflows exact chart coordinates"));
            return false;
        }
        const auto absolute = absolute_position(entry.absolute_begin_row, *offset);
        if (!absolute || absolute->whole_rows >= context.candidate_end)
            break;
        if (absolute->whole_rows >= context.candidate_begin &&
            !add_candidate(context.candidates, entry, *offset, editor_grid_line_kind_t::beat,
                           context.request.maximum_line_count, context.result.diagnostics))
            return false;
        if (beat == last_beat)
            break;
    }
    return true;
}

auto append_group_boundaries(editor_grid_build_context_t& context,
                             const measure_index_entry_t& entry,
                             const std::vector<std::uint32_t>& boundaries,
                             std::uint64_t denominator) -> bool {
    for (const auto boundary : boundaries) {
        const auto offset = offset_for_beat(boundary, context.divisions, denominator);
        if (!offset) {
            context.result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_measure,
                      "authored group position overflows exact chart coordinates"));
            return false;
        }
        const auto absolute = absolute_position(entry.absolute_begin_row, *offset);
        if (!absolute || absolute->whole_rows >= context.candidate_end)
            break;
        if (absolute->whole_rows >= context.candidate_begin &&
            !add_candidate(context.candidates, entry, *offset, editor_grid_line_kind_t::group,
                           context.request.maximum_line_count, context.result.diagnostics))
            return false;
    }
    return true;
}

auto append_unmetered_groups(editor_grid_build_context_t& context,
                             const measure_index_entry_t& entry,
                             std::uint64_t last_beat) -> bool {
    const auto last_group = last_beat / 2;
    const auto first_beat = first_visible_beat(context.divisions, 4, entry,
                                               context.candidate_begin, last_beat);
    auto group = first_beat ? *first_beat / 2 : last_group + 1;
    if (first_beat && *first_beat % 2 != 0)
        ++group;
    for (; group <= last_group; ++group) {
        const auto offset = offset_for_beat(group * 2, context.divisions, 4);
        if (!offset) {
            context.result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_measure,
                      "default group position overflows exact chart coordinates"));
            return false;
        }
        const auto absolute = absolute_position(entry.absolute_begin_row, *offset);
        if (!absolute || absolute->whole_rows >= context.candidate_end)
            break;
        if (absolute->whole_rows >= context.candidate_begin &&
            !add_candidate(context.candidates, entry, *offset, editor_grid_line_kind_t::group,
                           context.request.maximum_line_count, context.result.diagnostics))
            return false;
        if (group == last_group || group == maximum)
            break;
    }
    return true;
}

auto append_authored_groups(editor_grid_build_context_t& context,
                            const measure_chunk_t& measure,
                            const measure_index_entry_t& entry,
                            std::uint64_t denominator,
                            std::uint64_t last_beat) -> bool {
    if (!measure.definition.meter)
        return append_unmetered_groups(context, entry, last_beat);
    std::vector<std::uint32_t> boundaries;
    const auto authored = context.authored_groupings.find(measure.id.value);
    if (authored != context.authored_groupings.end()) {
        std::uint32_t cumulative{};
        const auto& counts = authored->second->beat_counts;
        for (std::size_t group = 0; group + 1 < counts.size(); ++group) {
            cumulative += counts[group];
            boundaries.push_back(cumulative);
        }
    } else {
        boundaries = default_group_boundaries(*measure.definition.meter);
    }
    return append_group_boundaries(context, entry, boundaries, denominator);
}

auto append_measure_snap_candidates(editor_grid_build_context_t& context,
                                    const measure_index_entry_t& entry) -> bool {
    if (!context.request.active_snap_denominator ||
        context.request.pixels_per_standard_measure /
                static_cast<double>(*context.request.active_snap_denominator) <
            context.request.density.minimum_active_snap_spacing_pixels)
        return true;
    const auto step = context.divisions / *context.request.active_snap_denominator;
    const auto begin = context.candidate_begin > entry.absolute_begin_row
        ? context.candidate_begin - entry.absolute_begin_row : 0;
    auto multiple = begin / step + (begin % step != 0);
    // The measure candidate already supplies offset zero.
    multiple = std::max(multiple, std::uint64_t{1});
    if (multiple > maximum / step) return true;
    auto offset = multiple * step;
    while (offset < entry.row_capacity &&
           offset < context.candidate_end - entry.absolute_begin_row) {
        if (!add_candidate(context.candidates, entry, {.whole = offset},
                           editor_grid_line_kind_t::active_snap,
                           context.request.maximum_line_count, context.result.diagnostics))
            return false;
        if (step > maximum - offset) break;
        offset += step;
    }
    return true;
}

auto append_authored_measure_candidates(editor_grid_build_context_t& context) -> bool {
    for (const auto& measure : context.chart.measures) {
        const auto entry = std::ranges::find(context.measure_index.entries, measure.id,
                                             &measure_index_entry_t::id);
        if (entry == context.measure_index.entries.end() ||
            entry->absolute_begin_row >= context.candidate_end ||
            entry->absolute_begin_row + entry->row_capacity <= context.candidate_begin)
            continue;
        const auto denominator = measure.definition.meter
            ? measure.definition.meter->denominator
            : 4U;
        const auto last_beat = measure.definition.meter
            ? static_cast<std::uint64_t>(measure.definition.meter->numerator - 1U)
            : last_interior_beat(context.divisions, denominator, entry->row_capacity);
        if (!append_authored_beats(context, *entry, denominator, last_beat) ||
            !append_authored_groups(context, measure, *entry, denominator, last_beat) ||
            !append_measure_snap_candidates(context, *entry))
            return false;
    }
    return true;
}

auto append_virtual_measure_candidates(editor_grid_build_context_t& context,
                                       const virtual_canvas_measure_t& measure) -> bool {
    const measure_index_entry_t entry{.id = {}, .absolute_begin_row = measure.begin_row,
                                      .row_capacity = measure.row_capacity};
    if (!append_measure_snap_candidates(context, entry) ||
        !add_candidate(context.candidates, entry, {}, editor_grid_line_kind_t::measure,
                       context.request.maximum_line_count, context.result.diagnostics))
        return false;
    const auto beat_spacing = context.request.pixels_per_standard_measure / 4.0;
    if (beat_spacing >= context.request.density.minimum_beat_spacing_pixels) {
        const auto first = first_visible_beat(context.divisions, 4, entry,
                                              context.candidate_begin, 3);
        if (first) {
            for (auto beat = *first; beat <= 3; ++beat) {
                const auto offset = offset_for_beat(beat, context.divisions, 4);
                if (!offset) {
                    context.result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_measure,
                              "virtual BMS beat overflows exact chart coordinates"));
                    return false;
                }
                const auto absolute = absolute_position(entry.absolute_begin_row, *offset);
                if (!absolute || absolute->whole_rows >= context.candidate_end)
                    break;
                if (!add_candidate(context.candidates, entry, *offset,
                                   editor_grid_line_kind_t::beat,
                                   context.request.maximum_line_count,
                                   context.result.diagnostics))
                    return false;
            }
        }
    }
    const auto group = offset_for_beat(2, context.divisions, 4);
    if (!group) {
        context.result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_measure,
                  "virtual BMS group overflows exact chart coordinates"));
        return false;
    }
    const auto group_row = absolute_position(entry.absolute_begin_row, *group);
    return !(group_row && group_row->whole_rows >= context.candidate_begin &&
             group_row->whole_rows < context.candidate_end) ||
           add_candidate(context.candidates, entry, *group, editor_grid_line_kind_t::group,
                         context.request.maximum_line_count, context.result.diagnostics);
}

auto append_virtual_tail_candidates(editor_grid_build_context_t& context) -> bool {
    if (!context.virtual_canvas || context.candidate_end <= context.measure_index.total_rows)
        return true;
    const auto first_number = context.candidate_begin < context.measure_index.total_rows
        ? context.measure_index.entries.size()
        : context.virtual_canvas->measure_containing(context.candidate_begin)->number;
    for (auto number = first_number; number < context.virtual_canvas->measure_count; ++number) {
        const auto measure = context.virtual_canvas->measure_at(number);
        if (!measure || measure->begin_row >= context.candidate_end)
            break;
        if (!append_virtual_measure_candidates(context, *measure))
            return false;
    }
    return true;
}

auto has_close_stronger_neighbor(const std::vector<editor_grid_line_t>& lines,
                                 const std::vector<bool>& visible, std::size_t index,
                                 editor_grid_line_kind_t kind, double threshold,
                                 double pixels_per_standard_measure,
                                 std::uint64_t divisions, bool backward) -> bool {
    if (backward) {
        for (auto neighbor = index; neighbor > 0;) {
            --neighbor;
            if (visible[neighbor] && priority(lines[neighbor].kind) > priority(kind))
                return spacing_pixels(lines[neighbor].chart_row, lines[index].chart_row,
                                      pixels_per_standard_measure, divisions) <
                       static_cast<long double>(threshold);
        }
    } else {
        for (auto neighbor = index + 1; neighbor < lines.size(); ++neighbor) {
            if (visible[neighbor] && priority(lines[neighbor].kind) > priority(kind))
                return spacing_pixels(lines[index].chart_row, lines[neighbor].chart_row,
                                      pixels_per_standard_measure, divisions) <
                       static_cast<long double>(threshold);
        }
    }
    return false;
}

auto retain_grid_kind(const std::vector<editor_grid_line_t>& lines,
                      std::vector<bool>& visible, editor_grid_line_kind_t kind,
                      double threshold, double pixels_per_standard_measure,
                      std::uint64_t divisions) -> void {
    std::optional<std::size_t> previous_same;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (lines[index].kind != kind)
            continue;
        auto dense = has_close_stronger_neighbor(lines, visible, index, kind, threshold,
                                                 pixels_per_standard_measure, divisions, true) ||
                     has_close_stronger_neighbor(lines, visible, index, kind, threshold,
                                                 pixels_per_standard_measure, divisions, false);
        if (!dense && previous_same && threshold != 0.0)
            dense = spacing_pixels(lines[*previous_same].chart_row, lines[index].chart_row,
                                   pixels_per_standard_measure, divisions) <
                    static_cast<long double>(threshold);
        if (!dense || threshold == 0.0) {
            visible[index] = true;
            previous_same = index;
        }
    }
}

auto thin_grid_candidates(editor_grid_build_context_t& context) -> void {
    auto& candidates = context.candidates;
    std::ranges::sort(candidates, [](const auto& left, const auto& right) {
        return compare_row(left.chart_row, right.chart_row) < 0;
    });
    std::vector<editor_grid_line_t> unique_lines;
    unique_lines.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        if (!unique_lines.empty() && same_row(unique_lines.back().chart_row, candidate.chart_row)) {
            if (priority(candidate.kind) > priority(unique_lines.back().kind))
                unique_lines.back() = candidate;
        } else {
            unique_lines.push_back(candidate);
        }
    }
    std::vector<bool> visible(unique_lines.size());
    for (std::size_t index = 0; index < unique_lines.size(); ++index)
        visible[index] = unique_lines[index].kind == editor_grid_line_kind_t::measure;
    retain_grid_kind(unique_lines, visible, editor_grid_line_kind_t::group,
                     context.request.density.minimum_group_spacing_pixels,
                     context.request.pixels_per_standard_measure, context.divisions);
    retain_grid_kind(unique_lines, visible, editor_grid_line_kind_t::beat,
                     context.request.density.minimum_beat_spacing_pixels,
                     context.request.pixels_per_standard_measure, context.divisions);
    retain_grid_kind(unique_lines, visible, editor_grid_line_kind_t::active_snap,
                     context.request.density.minimum_active_snap_spacing_pixels,
                     context.request.pixels_per_standard_measure, context.divisions);
    for (std::size_t index = 0; index < unique_lines.size(); ++index) {
        const auto& line = unique_lines[index];
        if (visible[index] && line.chart_row.whole_rows >= context.request.visible_begin_row &&
            line.chart_row.whole_rows < context.request.visible_end_row)
            context.result.lines.push_back(line);
    }
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
    std::optional<virtual_canvas_t> virtual_canvas;
    if (request.include_virtual_bms_tail && chart_has_virtual_tail(chart)) {
        auto built_canvas = build_virtual_canvas(chart);
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built_canvas)) {
            result.diagnostics = *diagnostics;
            return result;
        }
        virtual_canvas = std::get<virtual_canvas_t>(std::move(built_canvas));
    }
    const auto total_rows = virtual_canvas ? virtual_canvas->total_rows : measure_index.total_rows;
    result.diagnostics = error_if_invalid(chart, request, total_rows);
    if (!result.diagnostics.empty())
        return result;

    std::unordered_map<std::uint64_t, const editor_grid_grouping_t*> authored_groupings;
    if (!validate_authored_groupings(chart, request, authored_groupings, result.diagnostics))
        return result;
    const auto [candidate_begin, candidate_end] =
        candidate_interval(request, chart, total_rows);
    editor_grid_build_context_t context{
        chart, request, measure_index, virtual_canvas, authored_groupings, result,
        candidate_begin, candidate_end, chart.divisions_per_standard_measure, {}};
    context.candidates.reserve(std::min(request.maximum_line_count, std::size_t{4096}));
    if (!append_measure_candidates(context) ||
        !append_authored_measure_candidates(context) ||
        !append_virtual_tail_candidates(context))
        return result;
    thin_grid_candidates(context);
    return result;
}

} // namespace wormedit

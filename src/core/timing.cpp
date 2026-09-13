#include <wormedit/core/timing.h>

#include <algorithm>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <set>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace wormedit {
namespace {

constexpr auto maximum_i64_magnitude =
    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
constexpr auto minimum_i64_magnitude = maximum_i64_magnitude + 1;

auto magnitude(std::int64_t value) noexcept -> std::uint64_t {
    if (value >= 0) {
        return static_cast<std::uint64_t>(value);
    }
    return static_cast<std::uint64_t>(-(value + 1)) + 1;
}

auto checked_multiply(std::uint64_t left, std::uint64_t right) -> std::optional<std::uint64_t> {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return std::nullopt;
    }
    return left * right;
}

auto checked_signed_multiply(std::int64_t value, std::uint64_t factor)
    -> std::optional<std::int64_t> {
    const auto absolute = magnitude(value);
    const auto limit = value < 0 ? minimum_i64_magnitude : maximum_i64_magnitude;
    if (absolute != 0 && factor > limit / absolute) {
        return std::nullopt;
    }
    const auto product = absolute * factor;
    if (value < 0 && product == minimum_i64_magnitude) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return value < 0 ? -static_cast<std::int64_t>(product) : static_cast<std::int64_t>(product);
}

auto normalized(signed_rational_t value) -> std::optional<signed_rational_t> {
    if (value.denominator == 0) {
        return std::nullopt;
    }
    const auto divisor = std::gcd(magnitude(value.numerator), value.denominator);
    const auto reduced_magnitude = magnitude(value.numerator) / divisor;
    if (value.numerator < 0 && reduced_magnitude == minimum_i64_magnitude) {
        value.numerator = std::numeric_limits<std::int64_t>::min();
    } else {
        value.numerator = value.numerator < 0 ? -static_cast<std::int64_t>(reduced_magnitude)
                                              : static_cast<std::int64_t>(reduced_magnitude);
    }
    value.denominator /= divisor;
    return value;
}

auto add_exact(signed_rational_t left, signed_rational_t right)
    -> std::optional<signed_rational_t> {
    const auto left_normal = normalized(left);
    const auto right_normal = normalized(right);
    if (!left_normal || !right_normal) {
        return std::nullopt;
    }
    const auto common = std::gcd(left_normal->denominator, right_normal->denominator);
    const auto left_scale = right_normal->denominator / common;
    const auto right_scale = left_normal->denominator / common;
    const auto scaled_left = checked_signed_multiply(left_normal->numerator, left_scale);
    const auto scaled_right = checked_signed_multiply(right_normal->numerator, right_scale);
    const auto denominator = checked_multiply(left_normal->denominator, left_scale);
    if (!scaled_left || !scaled_right || !denominator) {
        return std::nullopt;
    }
    if ((*scaled_right > 0 &&
         *scaled_left > std::numeric_limits<std::int64_t>::max() - *scaled_right) ||
        (*scaled_right < 0 &&
         *scaled_left < std::numeric_limits<std::int64_t>::min() - *scaled_right)) {
        return std::nullopt;
    }
    return normalized({*scaled_left + *scaled_right, *denominator});
}

auto positive_fraction(std::vector<std::uint64_t> numerators,
                       std::vector<std::uint64_t> denominators)
    -> std::optional<signed_rational_t> {
    if (std::ranges::find(denominators, std::uint64_t{}) != denominators.end()) {
        return std::nullopt;
    }
    if (std::ranges::find(numerators, std::uint64_t{}) != numerators.end()) {
        return signed_rational_t{};
    }
    for (auto& numerator : numerators) {
        for (auto& denominator : denominators) {
            const auto divisor = std::gcd(numerator, denominator);
            numerator /= divisor;
            denominator /= divisor;
        }
    }
    auto numerator = std::uint64_t{1};
    auto denominator = std::uint64_t{1};
    for (const auto factor : numerators) {
        const auto product = checked_multiply(numerator, factor);
        if (!product || *product > maximum_i64_magnitude) {
            return std::nullopt;
        }
        numerator = *product;
    }
    for (const auto factor : denominators) {
        const auto product = checked_multiply(denominator, factor);
        if (!product) {
            return std::nullopt;
        }
        denominator = *product;
    }
    return normalized({static_cast<std::int64_t>(numerator), denominator});
}

auto row_duration(std::uint64_t rows, std::uint64_t resolution, signed_rational_t bpm)
    -> std::optional<signed_rational_t> {
    if (bpm.numerator <= 0 || bpm.denominator == 0 || resolution == 0) {
        return std::nullopt;
    }
    return positive_fraction({rows, 240, bpm.denominator},
                             {resolution, static_cast<std::uint64_t>(bpm.numerator)});
}

auto pause_duration(const pause_event_t& pause, signed_rational_t bpm)
    -> std::optional<signed_rational_t> {
    if (pause.duration.numerator < 0 || pause.duration.denominator == 0 || bpm.numerator <= 0 ||
        bpm.denominator == 0) {
        return std::nullopt;
    }
    const auto duration = static_cast<std::uint64_t>(pause.duration.numerator);
    switch (pause.source_unit) {
    case pause_unit_t::seconds:
        return normalized(pause.duration);
    case pause_unit_t::milliseconds:
        return positive_fraction({duration}, {pause.duration.denominator, 1'000});
    case pause_unit_t::beats:
        return positive_fraction(
            {duration, 60, bpm.denominator},
            {pause.duration.denominator, static_cast<std::uint64_t>(bpm.numerator)});
    case pause_unit_t::bms_stop_ticks:
        // One BMS STOP tick is 1/192 of a whole note, or 1/48 quarter beat.
        return positive_fraction(
            {duration, 5, bpm.denominator},
            {pause.duration.denominator, 4, static_cast<std::uint64_t>(bpm.numerator)});
    }
    return std::nullopt;
}

auto error(timing_error_code_t code, std::string message,
           std::optional<object_id_t> object = std::nullopt) -> timing_diagnostic_t {
    return {.severity = diagnostic_severity_t::error,
            .code = code,
            .message = std::move(message),
            .object = object};
}

auto warning(timing_error_code_t code, std::string message,
             std::optional<object_id_t> object = std::nullopt) -> timing_diagnostic_t {
    return {.severity = diagnostic_severity_t::warning,
            .code = code,
            .message = std::move(message),
            .object = object};
}

auto has_errors(const std::vector<timing_diagnostic_t>& diagnostics) -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

struct positioned_bpm_t {
    std::uint64_t row{};
    const bpm_event_t* event{};
    std::size_t insertion_order{};
};

struct positioned_pause_t {
    std::uint64_t row{};
    const pause_event_t* event{};
    std::size_t insertion_order{};
};

auto order_key(const source_provenance_t& provenance, std::size_t insertion_order,
               object_id_t object) {
    return std::tuple{provenance.source_order, insertion_order, object.value};
}

auto position_frame(const timing_index_t& timing, std::optional<std::uint64_t> row,
                    const preview_schedule_options_t& options,
                    std::vector<timing_diagnostic_t>& diagnostics,
                    std::optional<object_id_t> object) -> std::optional<std::uint64_t> {
    if (!row) {
        diagnostics.push_back(error(timing_error_code_t::position_out_of_range,
                                    "preview event lies outside chart geometry", object));
        return std::nullopt;
    }
    const auto query = timing_at(timing, *row, timing_boundary_side_t::event);
    if (!query) {
        diagnostics.push_back(*query.error);
        diagnostics.back().object = object;
        return std::nullopt;
    }
    if (query.inside_warp) {
        return std::nullopt;
    }
    const auto frame = sample_frame_at(*query.seconds, options.sample_rate, options.rounding);
    if (!frame) {
        auto diagnostic = *frame.error;
        diagnostic.object = object;
        if (diagnostic.code == timing_error_code_t::negative_sample_frame) {
            diagnostic.severity = diagnostic_severity_t::warning;
        }
        diagnostics.push_back(std::move(diagnostic));
        return std::nullopt;
    }
    return frame.frame;
}

auto visual_draw_order(visual_event_kind_t kind) -> std::size_t {
    switch (kind) {
    case visual_event_kind_t::background:
        return 0;
    case visual_event_kind_t::poor:
        return 1;
    case visual_event_kind_t::layer:
        return 2;
    case visual_event_kind_t::video:
        return 3;
    case visual_event_kind_t::storyboard:
        return 4;
    }
    return 5;
}

void fingerprint_mix(std::uint64_t& fingerprint, std::uint64_t value) {
    // A deterministic non-cryptographic fingerprint used only as a stale-cache guard.
    fingerprint ^= value + 0x9e3779b97f4a7c15ULL + (fingerprint << 6U) + (fingerprint >> 2U);
}

void fingerprint_position(std::uint64_t& fingerprint, local_position_t position) {
    fingerprint_mix(fingerprint, position.measure.value);
    fingerprint_mix(fingerprint, position.row);
}

auto timing_fingerprint(const chart_document_t& chart) -> std::uint64_t {
    auto fingerprint = std::uint64_t{0x574f524d54494d45ULL};
    fingerprint_mix(fingerprint, chart.divisions_per_standard_measure);
    fingerprint_mix(fingerprint, magnitude(chart.beat_zero_audio_offset_seconds.numerator));
    fingerprint_mix(fingerprint, chart.beat_zero_audio_offset_seconds.numerator < 0);
    fingerprint_mix(fingerprint, chart.beat_zero_audio_offset_seconds.denominator);
    for (const auto& measure : chart.measures) {
        fingerprint_mix(fingerprint, measure.id.value);
        fingerprint_mix(fingerprint, measure.definition.length.numerator);
        fingerprint_mix(fingerprint, measure.definition.length.denominator);
        fingerprint_mix(fingerprint, measure.definition.meter.has_value());
        if (measure.definition.meter) {
            fingerprint_mix(fingerprint, measure.definition.meter->numerator);
            fingerprint_mix(fingerprint, measure.definition.meter->denominator);
        }
    }
    for (const auto& bpm : chart.bpms) {
        fingerprint_mix(fingerprint, bpm.id.value);
        fingerprint_position(fingerprint, bpm.position);
        fingerprint_mix(fingerprint, magnitude(bpm.beats_per_minute.numerator));
        fingerprint_mix(fingerprint, bpm.beats_per_minute.numerator < 0);
        fingerprint_mix(fingerprint, bpm.beats_per_minute.denominator);
        fingerprint_mix(fingerprint, bpm.provenance.source_order);
    }
    for (const auto& pause : chart.pauses) {
        fingerprint_mix(fingerprint, pause.id.value);
        fingerprint_position(fingerprint, pause.position);
        fingerprint_mix(fingerprint, static_cast<std::uint64_t>(pause.kind));
        fingerprint_mix(fingerprint, magnitude(pause.duration.numerator));
        fingerprint_mix(fingerprint, pause.duration.numerator < 0);
        fingerprint_mix(fingerprint, pause.duration.denominator);
        fingerprint_mix(fingerprint, static_cast<std::uint64_t>(pause.source_unit));
        fingerprint_mix(fingerprint, pause.provenance.source_order);
    }
    for (const auto& warp : chart.warps) {
        fingerprint_mix(fingerprint, warp.id.value);
        fingerprint_position(fingerprint, warp.span.begin);
        fingerprint_position(fingerprint, warp.span.end);
        fingerprint_mix(fingerprint, static_cast<std::uint64_t>(warp.source_encoding));
        fingerprint_mix(fingerprint, warp.provenance.source_order);
    }
    return fingerprint;
}

} // namespace

auto build_timing_index(const chart_document_t& chart, timing_build_options_t options)
    -> timing_build_result_t {
    timing_build_result_t result;
    const auto built_measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built_measures)) {
        result.diagnostics.push_back(
            error(timing_error_code_t::invalid_document, "chart measure geometry is invalid"));
        return result;
    }
    const auto& measures = std::get<measure_index_t>(built_measures);
    if (chart.divisions_per_standard_measure == 0 || measures.total_rows == 0) {
        result.diagnostics.push_back(error(timing_error_code_t::invalid_document,
                                           "timing requires a nonempty positive-resolution chart"));
        return result;
    }
    if (options.maximum_boundaries == 0) {
        result.diagnostics.push_back(
            error(timing_error_code_t::capacity_exceeded, "timing boundary capacity is zero"));
        return result;
    }
    if (chart.bpms.size() > options.maximum_timing_events ||
        chart.pauses.size() > options.maximum_timing_events - chart.bpms.size() ||
        chart.warps.size() >
            options.maximum_timing_events - chart.bpms.size() - chart.pauses.size()) {
        result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                           "timing event count exceeds the configured cap"));
        return result;
    }
    const auto offset = normalized(chart.beat_zero_audio_offset_seconds);
    if (!offset) {
        result.diagnostics.push_back(error(timing_error_code_t::invalid_document,
                                           "beat-zero audio offset is not a valid rational"));
        return result;
    }

    std::set<std::uint64_t> boundary_set{0, measures.total_rows};
    if (boundary_set.size() > options.maximum_boundaries) {
        result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                           "timing boundary count exceeds the configured cap"));
        return result;
    }
    const auto insert_boundary = [&](std::uint64_t row) -> bool {
        boundary_set.insert(row);
        return boundary_set.size() <= options.maximum_boundaries;
    };
    std::vector<positioned_bpm_t> bpms;
    bpms.reserve(chart.bpms.size());
    for (std::size_t order = 0; order < chart.bpms.size(); ++order) {
        const auto& bpm = chart.bpms[order];
        const auto row = absolute_row(measures, bpm.position);
        if (!row) {
            result.diagnostics.push_back(error(timing_error_code_t::position_out_of_range,
                                               "BPM position is outside chart geometry", bpm.id));
            continue;
        }
        if (bpm.beats_per_minute.numerator <= 0 || bpm.beats_per_minute.denominator == 0) {
            result.diagnostics.push_back(
                error(timing_error_code_t::invalid_bpm, "BPM must be a positive rational", bpm.id));
            continue;
        }
        if (!insert_boundary(*row)) {
            result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                               "timing boundary count exceeds the configured cap"));
            return result;
        }
        bpms.push_back({*row, &bpm, order});
    }
    std::ranges::stable_sort(bpms, [](const auto& left, const auto& right) {
        return std::tuple{left.row,
                          order_key(left.event->provenance, left.insertion_order, left.event->id)} <
               std::tuple{right.row, order_key(right.event->provenance, right.insertion_order,
                                               right.event->id)};
    });
    if (bpms.empty() || bpms.front().row != 0) {
        result.diagnostics.push_back(error(timing_error_code_t::missing_initial_bpm,
                                           "timing requires a positive BPM at chart row zero"));
    }

    std::vector<positioned_pause_t> pauses;
    pauses.reserve(chart.pauses.size());
    for (std::size_t order = 0; order < chart.pauses.size(); ++order) {
        const auto& pause = chart.pauses[order];
        const auto row = absolute_row(measures, pause.position);
        if (!row) {
            result.diagnostics.push_back(error(timing_error_code_t::position_out_of_range,
                                               "pause position is outside chart geometry",
                                               pause.id));
            continue;
        }
        if (pause.duration.numerator < 0 || pause.duration.denominator == 0) {
            result.diagnostics.push_back(error(timing_error_code_t::invalid_pause,
                                               "pause duration must be a nonnegative rational",
                                               pause.id));
            continue;
        }
        if (!insert_boundary(*row)) {
            result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                               "timing boundary count exceeds the configured cap"));
            return result;
        }
        pauses.push_back({*row, &pause, order});
    }
    std::ranges::stable_sort(pauses, [](const auto& left, const auto& right) {
        return std::tuple{left.row,
                          order_key(left.event->provenance, left.insertion_order, left.event->id)} <
               std::tuple{right.row, order_key(right.event->provenance, right.insertion_order,
                                               right.event->id)};
    });

    std::map<std::uint64_t, std::pair<std::size_t, std::size_t>> warp_edges;
    for (const auto& warp : chart.warps) {
        const auto begin = absolute_row(measures, warp.span.begin);
        const auto end = absolute_row(measures, warp.span.end);
        if (!begin || !end || *end <= *begin) {
            result.diagnostics.push_back(error(timing_error_code_t::invalid_warp,
                                               "warp must be a positive in-chart musical interval",
                                               warp.id));
            continue;
        }
        if (!insert_boundary(*begin) || !insert_boundary(*end)) {
            result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                               "timing boundary count exceeds the configured cap"));
            return result;
        }
        ++warp_edges[*begin].first;
        ++warp_edges[*end].second;
    }

    if (boundary_set.size() > options.maximum_boundaries) {
        result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                           "timing boundary count exceeds the configured cap"));
    }
    if (has_errors(result.diagnostics)) {
        return result;
    }

    timing_index_t index{.divisions_per_standard_measure = chart.divisions_per_standard_measure,
                         .total_rows = measures.total_rows,
                         .source_fingerprint = timing_fingerprint(chart)};
    index.rows.reserve(boundary_set.size());
    index.segments.reserve(boundary_set.size() - 1);
    std::vector<std::uint64_t> boundaries(boundary_set.begin(), boundary_set.end());
    auto current_time = *offset;
    auto current_bpm = signed_rational_t{};
    auto bpm_cursor = std::size_t{};
    auto pause_cursor = std::size_t{};
    auto active_warps = std::size_t{};

    for (std::size_t boundary_index = 0; boundary_index < boundaries.size(); ++boundary_index) {
        const auto row = boundaries[boundary_index];
        if (const auto edge = warp_edges.find(row); edge != warp_edges.end()) {
            if (edge->second.second > active_warps) {
                result.diagnostics.push_back(error(timing_error_code_t::invalid_warp,
                                                   "warp edge accounting is inconsistent"));
                return result;
            }
            active_warps -= edge->second.second;
            active_warps += edge->second.first;
        }
        while (bpm_cursor < bpms.size() && bpms[bpm_cursor].row == row) {
            current_bpm = *normalized(bpms[bpm_cursor].event->beats_per_minute);
            ++bpm_cursor;
        }
        if (current_bpm.numerator <= 0) {
            result.diagnostics.push_back(error(timing_error_code_t::missing_initial_bpm,
                                               "no effective BPM exists at this timing boundary"));
            return result;
        }

        const auto row_pause_begin = pause_cursor;
        while (pause_cursor < pauses.size() && pauses[pause_cursor].row == row) {
            ++pause_cursor;
        }
        const auto row_pause_end = pause_cursor;
        const auto apply_pause = [&](const pause_event_t& pause, const char* description) -> bool {
            const auto duration = pause_duration(pause, current_bpm);
            if (!duration) {
                result.diagnostics.push_back(
                    error(timing_error_code_t::arithmetic_overflow,
                          std::string(description) + " conversion exceeds exact timing bounds",
                          pause.id));
                return false;
            }
            if (active_warps != 0 && duration->numerator != 0) {
                result.diagnostics.push_back(
                    warning(timing_error_code_t::invalid_warp,
                            std::string(description) +
                                " inside a warp is suppressed by the canonical timing map",
                            pause.id));
                return true;
            }
            const auto added = add_exact(current_time, *duration);
            if (!added) {
                result.diagnostics.push_back(
                    error(timing_error_code_t::arithmetic_overflow,
                          std::string(description) + " accumulation exceeds exact timing bounds",
                          pause.id));
                return false;
            }
            current_time = *added;
            return true;
        };

        const auto before_delays = current_time;
        for (auto index = row_pause_begin; index < row_pause_end; ++index) {
            if (pauses[index].event->kind == pause_kind_t::delay &&
                !apply_pause(*pauses[index].event, "delay")) {
                return result;
            }
        }
        const auto event_time = current_time;
        for (auto index = row_pause_begin; index < row_pause_end; ++index) {
            if (pauses[index].event->kind == pause_kind_t::stop &&
                !apply_pause(*pauses[index].event, "stop")) {
                return result;
            }
        }
        index.rows.push_back({.absolute_row = row,
                              .before_delays_seconds = before_delays,
                              .event_seconds = event_time,
                              .after_stops_seconds = current_time,
                              .effective_bpm = current_bpm,
                              .inside_warp = active_warps != 0});

        if (boundary_index + 1 == boundaries.size()) {
            continue;
        }
        const auto next_row = boundaries[boundary_index + 1];
        auto segment_end = current_time;
        if (active_warps == 0) {
            const auto duration =
                row_duration(next_row - row, chart.divisions_per_standard_measure, current_bpm);
            if (!duration) {
                result.diagnostics.push_back(
                    error(timing_error_code_t::arithmetic_overflow,
                          "BPM segment exceeds exact timing arithmetic bounds"));
                return result;
            }
            const auto added = add_exact(current_time, *duration);
            if (!added) {
                result.diagnostics.push_back(
                    error(timing_error_code_t::arithmetic_overflow,
                          "timing segment accumulation exceeds exact arithmetic bounds"));
                return result;
            }
            segment_end = *added;
        }
        index.segments.push_back({.begin_row = row,
                                  .end_row = next_row,
                                  .begin_seconds = current_time,
                                  .end_seconds = segment_end,
                                  .effective_bpm = current_bpm,
                                  .warped = active_warps != 0});
        current_time = segment_end;
    }

    result.index = std::move(index);
    return result;
}

auto timing_at(const timing_index_t& index, std::uint64_t absolute_row, timing_boundary_side_t side)
    -> timing_query_result_t {
    if (absolute_row > index.total_rows || index.rows.empty()) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "timing query lies outside chart geometry")};
    }
    const auto exact =
        std::ranges::lower_bound(index.rows, absolute_row, {}, &timing_row_state_t::absolute_row);
    if (exact != index.rows.end() && exact->absolute_row == absolute_row) {
        const auto seconds =
            side == timing_boundary_side_t::before_delays ? exact->before_delays_seconds
            : side == timing_boundary_side_t::after_stops ? exact->after_stops_seconds
                                                          : exact->event_seconds;
        return {.seconds = seconds, .inside_warp = exact->inside_warp};
    }
    const auto segment =
        std::ranges::upper_bound(index.segments, absolute_row, {}, &timing_segment_t::begin_row);
    if (segment == index.segments.begin()) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "timing query has no containing segment")};
    }
    const auto& containing = *std::prev(segment);
    if (absolute_row >= containing.end_row) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "timing query has no containing segment")};
    }
    if (containing.warped) {
        return {.seconds = containing.begin_seconds, .inside_warp = true};
    }
    const auto duration =
        row_duration(absolute_row - containing.begin_row, index.divisions_per_standard_measure,
                     containing.effective_bpm);
    const auto seconds = duration ? add_exact(containing.begin_seconds, *duration) : std::nullopt;
    if (!seconds) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "timing query exceeds exact arithmetic bounds")};
    }
    return {.seconds = *seconds};
}

auto timing_at(const timing_index_t& index, const measure_index_t& measures,
               local_position_t position, timing_boundary_side_t side) -> timing_query_result_t {
    const auto row = absolute_row(measures, position);
    if (!row) {
        return {.error = error(timing_error_code_t::position_out_of_range,
                               "timing query position is outside chart geometry")};
    }
    return timing_at(index, *row, side);
}

auto sample_frame_at(signed_rational_t seconds, std::uint32_t sample_rate,
                     sample_rounding_t rounding) -> sample_frame_result_t {
    const auto value = normalized(seconds);
    if (!value || sample_rate == 0) {
        return {.error = error(timing_error_code_t::invalid_document,
                               "sample-frame conversion requires valid seconds and sample rate")};
    }
    if (value->numerator < 0) {
        return {.error = error(timing_error_code_t::negative_sample_frame,
                               "event occurs before audio frame zero")};
    }
    auto numerator = magnitude(value->numerator);
    auto denominator = value->denominator;
    const auto divisor = std::gcd<std::uint64_t>(sample_rate, denominator);
    const auto reduced_rate = sample_rate / divisor;
    denominator /= divisor;
    const auto product = checked_multiply(numerator, reduced_rate);
    if (!product) {
        return {.error = error(timing_error_code_t::arithmetic_overflow,
                               "sample-frame conversion overflows")};
    }
    const auto whole = *product / denominator;
    const auto remainder = *product % denominator;
    auto frame = whole;
    if (rounding == sample_rounding_t::ceil && remainder != 0) {
        if (frame == std::numeric_limits<std::uint64_t>::max()) {
            return {.error = error(timing_error_code_t::arithmetic_overflow,
                                   "rounded sample frame overflows")};
        }
        ++frame;
    } else if (rounding == sample_rounding_t::nearest_ties_to_even && remainder != 0) {
        const auto complement = denominator - remainder;
        const bool round_up = remainder > complement || (remainder == complement && frame % 2 != 0);
        if (round_up) {
            if (frame == std::numeric_limits<std::uint64_t>::max()) {
                return {.error = error(timing_error_code_t::arithmetic_overflow,
                                       "rounded sample frame overflows")};
            }
            ++frame;
        }
    }
    return {.frame = frame};
}

auto build_preview_schedule(const chart_document_t& chart, const timing_index_t& timing,
                            preview_schedule_options_t options) -> preview_schedule_result_t {
    preview_schedule_result_t result;
    if (options.sample_rate == 0 ||
        timing.divisions_per_standard_measure != chart.divisions_per_standard_measure) {
        result.diagnostics.push_back(
            error(timing_error_code_t::invalid_document,
                  "preview schedule and timing index must use the same positive chart resolution"));
        return result;
    }
    const auto built_measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built_measures)) {
        result.diagnostics.push_back(error(timing_error_code_t::invalid_document,
                                           "preview chart measure geometry is invalid"));
        return result;
    }
    const auto& measures = std::get<measure_index_t>(built_measures);
    if (measures.total_rows != timing.total_rows) {
        result.diagnostics.push_back(error(timing_error_code_t::invalid_document,
                                           "preview timing index is stale for this chart"));
        return result;
    }
    if (timing.source_fingerprint != timing_fingerprint(chart)) {
        result.diagnostics.push_back(error(timing_error_code_t::invalid_document,
                                           "preview timing index is stale for this chart"));
        return result;
    }
    preview_schedule_t schedule{.sample_rate = options.sample_rate};
    auto playable_candidates = std::size_t{};
    auto bgm_candidates = std::size_t{};
    auto playable_overflow = false;
    auto bgm_overflow = false;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.sound && (note.kind == note_kind_t::tap || note.kind == note_kind_t::hold ||
                               note.kind == note_kind_t::roll)) {
                if (playable_candidates < options.maximum_playable_events) {
                    ++playable_candidates;
                } else {
                    playable_overflow = true;
                }
            }
        }
        for (const auto& bgm : measure.bgm_events) {
            if (bgm.sound) {
                if (bgm_candidates < options.maximum_bgm_events) {
                    ++bgm_candidates;
                } else {
                    bgm_overflow = true;
                }
            }
        }
    }
    if (playable_overflow || bgm_overflow || chart.bga_events.size() > options.maximum_bga_events ||
        (options.include_metronome && chart.measures.size() > options.maximum_metronome_events)) {
        result.diagnostics.push_back(
            error(timing_error_code_t::capacity_exceeded,
                  "preview input count exceeds a configured queue construction cap"));
        return result;
    }
    schedule.playable.reserve(playable_candidates);
    schedule.bgm.reserve(bgm_candidates);
    schedule.bga.reserve(std::min(chart.bga_events.size(), options.maximum_bga_events));
    std::unordered_map<std::uint64_t, const measure_index_entry_t*> measure_entries;
    measure_entries.reserve(measures.entries.size());
    for (const auto& entry : measures.entries) {
        measure_entries.emplace(entry.id.value, &entry);
    }
    const auto row_for = [&](local_position_t position) -> std::optional<std::uint64_t> {
        const auto entry = measure_entries.find(position.measure.value);
        if (entry == measure_entries.end() || position.row >= entry->second->row_capacity ||
            position.row >
                std::numeric_limits<std::uint64_t>::max() - entry->second->absolute_begin_row) {
            return std::nullopt;
        }
        return entry->second->absolute_begin_row + position.row;
    };

    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (!note.sound || (note.kind != note_kind_t::tap && note.kind != note_kind_t::hold &&
                                note.kind != note_kind_t::roll)) {
                continue;
            }
            const auto frame = position_frame(timing, row_for(note.position), options,
                                              result.diagnostics, note.id);
            if (frame) {
                if (schedule.playable.size() >= options.maximum_playable_events) {
                    result.diagnostics.push_back(
                        error(timing_error_code_t::capacity_exceeded,
                              "playable preview queue exceeds its configured cap", note.id));
                    return result;
                }
                schedule.playable.push_back({.sample_frame = *frame,
                                             .object = note.id,
                                             .resource = *note.sound,
                                             .lane = note.lane,
                                             .source_order = note.provenance.source_order});
            }
        }
        for (const auto& bgm : measure.bgm_events) {
            if (!bgm.sound) {
                continue;
            }
            const auto frame =
                position_frame(timing, row_for(bgm.position), options, result.diagnostics, bgm.id);
            if (!frame) {
                continue;
            }
            if (schedule.bgm.size() >= options.maximum_bgm_events) {
                result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                                   "BGM preview queue exceeds its configured cap",
                                                   bgm.id));
                return result;
            }
            const auto lane =
                std::ranges::find(chart.bgm_source_lanes, bgm.source_lane, &bgm_source_lane_t::id);
            schedule.bgm.push_back({.sample_frame = *frame,
                                    .object = bgm.id,
                                    .resource = *bgm.sound,
                                    .source_order = lane == chart.bgm_source_lanes.end()
                                                        ? std::numeric_limits<std::size_t>::max()
                                                        : lane->source_order,
                                    .secondary_order = bgm.provenance.source_order});
        }
    }
    for (const auto& bga : chart.bga_events) {
        const auto frame =
            position_frame(timing, row_for(bga.position), options, result.diagnostics, bga.id);
        if (frame) {
            if (schedule.bga.size() >= options.maximum_bga_events) {
                result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                                   "BGA preview queue exceeds its configured cap",
                                                   bga.id));
                return result;
            }
            schedule.bga.push_back({.sample_frame = *frame,
                                    .object = bga.id,
                                    .resource = bga.resource,
                                    .kind = bga.kind,
                                    .draw_order = visual_draw_order(bga.kind),
                                    .source_order = bga.provenance.source_order});
        }
    }

    if (options.include_metronome) {
        for (std::size_t measure_index = 0; measure_index < chart.measures.size();
             ++measure_index) {
            const auto& measure = chart.measures[measure_index];
            const auto& entry = measures.entries[measure_index];
            auto pulses = std::uint64_t{4};
            if (measure.definition.meter) {
                pulses = measure.definition.meter->numerator;
            } else {
                const auto quarter_rows = chart.divisions_per_standard_measure / 4;
                pulses = quarter_rows == 0
                             ? 1
                             : std::max<std::uint64_t>(1, entry.row_capacity / quarter_rows);
            }
            if (pulses == 0 || entry.row_capacity % pulses != 0) {
                result.diagnostics.push_back(
                    error(timing_error_code_t::invalid_document,
                          "measure meter cannot produce exact metronome pulse rows"));
                continue;
            }
            const auto step = entry.row_capacity / pulses;
            const bool compound = measure.definition.meter && pulses > 3 && pulses % 3 == 0;
            for (std::uint64_t pulse = 0; pulse < pulses; ++pulse) {
                const auto row = entry.absolute_begin_row + pulse * step;
                const auto query = timing_at(timing, row, timing_boundary_side_t::event);
                if (!query) {
                    result.diagnostics.push_back(*query.error);
                    continue;
                }
                if (query.inside_warp) {
                    continue;
                }
                const auto frame =
                    sample_frame_at(*query.seconds, options.sample_rate, options.rounding);
                if (!frame) {
                    auto diagnostic = *frame.error;
                    if (diagnostic.code == timing_error_code_t::negative_sample_frame) {
                        diagnostic.severity = diagnostic_severity_t::warning;
                    }
                    result.diagnostics.push_back(std::move(diagnostic));
                    continue;
                }
                if (schedule.metronome.size() >= options.maximum_metronome_events) {
                    result.diagnostics.push_back(
                        error(timing_error_code_t::capacity_exceeded,
                              "metronome preview queue exceeds its configured cap"));
                    return result;
                }
                schedule.metronome.push_back({
                    .sample_frame = *frame.frame,
                    .measure = measure.id,
                    .pulse = static_cast<std::uint32_t>(pulse),
                    .accent = pulse == 0                   ? metronome_accent_t::measure
                              : compound && pulse % 3 == 0 ? metronome_accent_t::group
                                                           : metronome_accent_t::regular,
                });
            }
        }
    }

    if (schedule.playable.size() > options.maximum_playable_events ||
        schedule.bgm.size() > options.maximum_bgm_events ||
        schedule.bga.size() > options.maximum_bga_events ||
        schedule.metronome.size() > options.maximum_metronome_events) {
        result.diagnostics.push_back(error(timing_error_code_t::capacity_exceeded,
                                           "preview event count exceeds a configured queue cap"));
    }
    if (has_errors(result.diagnostics)) {
        return result;
    }

    std::ranges::stable_sort(schedule.playable, [](const auto& left, const auto& right) {
        return std::tie(left.sample_frame, left.source_order, left.object.value) <
               std::tie(right.sample_frame, right.source_order, right.object.value);
    });
    std::ranges::stable_sort(schedule.bgm, [](const auto& left, const auto& right) {
        return std::tie(left.sample_frame, left.source_order, left.secondary_order,
                        left.object.value) < std::tie(right.sample_frame, right.source_order,
                                                      right.secondary_order, right.object.value);
    });
    std::ranges::stable_sort(schedule.bga, [](const auto& left, const auto& right) {
        return std::tie(left.sample_frame, left.draw_order, left.source_order, left.object.value) <
               std::tie(right.sample_frame, right.draw_order, right.source_order,
                        right.object.value);
    });
    result.schedule = std::move(schedule);
    return result;
}

} // namespace wormedit

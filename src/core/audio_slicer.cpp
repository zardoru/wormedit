#include "checked_math.h"
#include <wormedit/core/audio_slicer.h>

#include "wide_math.h"

#include <algorithm>
#include <optional>

namespace wormedit {
namespace {

using wide_math::u128_t;

constexpr std::uint64_t maximum_bpm_numerator = 1ULL << 40;
constexpr std::uint64_t maximum_denominator = 1ULL << 20;
constexpr std::uint64_t maximum_offset_numerator = 1ULL << 50;
constexpr std::uint64_t maximum_source_frames = 1ULL << 36;
constexpr std::uint32_t maximum_division = 1024;
constexpr std::uint32_t maximum_sample_rate = 1U << 19;

struct grid_t {
    [[nodiscard]] static auto from(const audio_slicer_request_t& request) -> std::optional<grid_t>;

    // frame(k) = max(0, round((step * k - lead) / denominator)), all exact.
    u128_t step;
    u128_t lead;
    u128_t denominator;
};

using checked_math::checked_multiply;

auto product(std::initializer_list<std::uint64_t> factors) -> std::optional<u128_t> {
    u128_t result{0, 1};
    for (const auto factor : factors) {
        const auto next = checked_multiply(result, factor);
        if (!next) return std::nullopt;
        result = *next;
    }
    return result;
}

auto validate(const audio_slicer_request_t& request) -> std::vector<audio_slicer_diagnostic_t> {
    using code_t = audio_slicer_error_code_t;
    std::vector<audio_slicer_diagnostic_t> out;
    const auto add = [&](code_t code, const char* message) { out.push_back({code, message}); };
    const auto& bpm = request.beats_per_minute;
    if (bpm.numerator == 0 || bpm.numerator > maximum_bpm_numerator || bpm.denominator == 0 ||
        bpm.denominator > maximum_denominator)
        add(code_t::invalid_tempo, "tempo must be a positive rational within the supported range");
    if (request.division == 0 || request.division > maximum_division)
        add(code_t::invalid_division, "division must be between 1 and 1024");
    const auto& offset = request.offset_seconds;
    const auto magnitude = offset.numerator < 0 ? static_cast<std::uint64_t>(-(offset.numerator + 1)) + 1U
                                                : static_cast<std::uint64_t>(offset.numerator);
    if (offset.denominator == 0 || offset.denominator > maximum_denominator ||
        magnitude > maximum_offset_numerator)
        add(code_t::invalid_offset, "offset is out of range");
    if (request.sample_rate == 0 || request.sample_rate > maximum_sample_rate)
        add(code_t::invalid_sample_rate, "sample rate is out of range");
    if (request.source_frames == 0 || request.source_frames > maximum_source_frames)
        add(code_t::invalid_source_length, "source length is zero or too long");
    return out;
}

// Slice length in seconds is 60 * bpm_den / (bpm_num * division); it must cover
// at least one frame so every slice is non-empty and the skip count is bounded.
auto is_slice_covering_frame(const audio_slicer_request_t& request) -> bool {
    const auto& bpm = request.beats_per_minute;
    return 60ULL * bpm.denominator * request.sample_rate >=
               bpm.numerator * static_cast<std::uint64_t>(request.division);
}

auto grid_t::from(const audio_slicer_request_t& request) -> std::optional<grid_t> {
    const auto& bpm = request.beats_per_minute;
    const auto& offset = request.offset_seconds;
    const auto step = product({request.sample_rate, 60, bpm.denominator, offset.denominator});
    const auto denominator = product({offset.denominator, bpm.numerator, request.division});
    if (!step || !denominator) return std::nullopt;
    // Positive offsets move boundary 0 forward; negative ones move it back.
    const auto magnitude = offset.numerator < 0
                               ? static_cast<std::uint64_t>(-(offset.numerator + 1)) + 1U
                               : static_cast<std::uint64_t>(offset.numerator);
    const auto offset_part = product({request.sample_rate, magnitude, bpm.numerator, request.division});
    if (!offset_part) return std::nullopt;
    return grid_t{*step, offset.numerator < 0 ? *offset_part : u128_t{}, *denominator};
}


auto round_quotient(const wide_math::divmod_t& division, const u128_t& denominator,
                    sample_rounding_t rounding) -> std::uint64_t {
    auto quotient = division.quotient.low;
    const auto has_remainder = division.remainder != u128_t{};
    if (rounding == sample_rounding_t::ceil) {
        quotient += has_remainder ? 1U : 0U;
    } else if (rounding == sample_rounding_t::nearest_ties_to_even && has_remainder) {
        const auto doubled = u128_t{(division.remainder.high << 1) | (division.remainder.low >> 63),
                                    division.remainder.low << 1};
        const bool above = wide_math::u128_less(denominator, doubled);
        const bool tie = doubled == denominator;
        quotient += (above || (tie && (quotient & 1U) != 0)) ? 1U : 0U;
    }
    return quotient;
}

// `offset` is the signed offset in frames*denominator: positive offsets add to
// the boundary, negative ones subtract. Boundaries at or below zero clamp to 0.
auto boundary_frame(const grid_t& grid, const audio_slicer_request_t& request,
                    std::uint64_t index, const u128_t& positive_offset) -> std::uint64_t {
    const auto steps = checked_multiply(grid.step, index);
    if (!steps) return request.source_frames;
    u128_t total = *steps;
    if (grid.lead != u128_t{}) {
        if (!wide_math::u128_less(grid.lead, total)) return 0;
        total = wide_math::subtract(total, grid.lead);
    } else {
        total = u128_t{total.high + (total.low + positive_offset.low < total.low ? 1U : 0U) +
                           positive_offset.high,
                       total.low + positive_offset.low};
    }
    const auto division = wide_math::divide(total, grid.denominator);
    if (!wide_math::can_fit_u64(division.quotient)) return request.source_frames;
    return std::min(round_quotient(division, grid.denominator, request.rounding),
                    request.source_frames);
}

} // namespace

auto compute_audio_slices(const audio_slicer_request_t& request) -> audio_slicer_result_t {
    audio_slicer_result_t result;
    result.diagnostics = validate(request);
    if (!result.diagnostics.empty()) return result;
    if (!is_slice_covering_frame(request)) {
        result.diagnostics.push_back({audio_slicer_error_code_t::slice_shorter_than_one_frame,
                                      "slices must be at least one frame long"});
        return result;
    }
    const auto grid = grid_t::from(request);
    if (!grid) {
        result.diagnostics.push_back(
            {audio_slicer_error_code_t::invalid_offset, "offset is out of range"});
        return result;
    }
    const auto& offset = request.offset_seconds;
    u128_t positive_offset{};
    if (offset.numerator > 0) {
        positive_offset = *product({request.sample_rate, static_cast<std::uint64_t>(offset.numerator),
                                    request.beats_per_minute.numerator, request.division});
    }
    // The first index whose boundary is not before zero; earlier ones clamp to 0.
    std::uint64_t index = 0;
    if (grid->lead != u128_t{}) {
        const auto skipped = wide_math::divide(grid->lead, grid->step).quotient;
        if (!wide_math::can_fit_u64(skipped)) {
            result.diagnostics.push_back(
                {audio_slicer_error_code_t::invalid_offset, "offset is out of range"});
            return result;
        }
        index = skipped.low;
    }
    std::uint64_t previous = 0;
    if (positive_offset != u128_t{} && request.include_lead_in) {
        const auto first = boundary_frame(*grid, request, index, positive_offset);
        if (first > 0) {
            result.ranges.push_back({0, first});
            previous = first;
        }
    } else if (positive_offset != u128_t{}) {
        previous = boundary_frame(*grid, request, index, positive_offset);
    }
    while (previous < request.source_frames) {
        const auto next = boundary_frame(*grid, request, ++index, positive_offset);
        if (next <= previous) continue;
        if (result.ranges.size() >= request.maximum_slices) {
            result.ranges.clear();
            result.diagnostics.push_back({audio_slicer_error_code_t::capacity_exceeded,
                                          "the grid produces more slices than allowed"});
            return result;
        }
        result.ranges.push_back({previous, next});
        previous = next;
    }
    return result;
}

} // namespace wormedit

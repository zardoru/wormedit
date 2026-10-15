#pragma once

#include <wormedit/core/result.h>
#include <wormedit/core/timing.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wormedit {

enum class audio_slicer_error_code_t {
    invalid_tempo,
    invalid_division,
    invalid_offset,
    invalid_sample_rate,
    invalid_source_length,
    slice_shorter_than_one_frame,
    capacity_exceeded,
};

struct audio_slicer_diagnostic_t {
    audio_slicer_error_code_t code{audio_slicer_error_code_t::invalid_tempo};
    std::string message;

    [[nodiscard]] constexpr auto is_error() const noexcept -> bool { return true; }
    auto operator==(const audio_slicer_diagnostic_t&) const -> bool = default;
};

// Cuts a long DAW export on a beat grid (the Woslicer model): a boundary every
// 1/division of a beat, starting at offset_seconds.
struct audio_slicer_request_t {
    // Quarter-note tempo as an exact rational; numerator and denominator are
    // non-zero and at most 2^40 / 2^20.
    rational_t beats_per_minute{120, 1};
    // Each slice spans one beat divided by this value (1..1024). 4 gives 16ths.
    std::uint32_t division{1};
    // Time of the first boundary. May be negative; audio before the first
    // positive boundary then belongs to the first slice. Denominator <= 2^20.
    signed_rational_t offset_seconds{0, 1};
    std::uint32_t sample_rate{48'000};
    std::uint64_t source_frames{};
    // When the offset is positive, emit [0, offset) as its own first slice.
    bool include_lead_in{true};
    sample_rounding_t rounding{sample_rounding_t::nearest_ties_to_even};
    std::size_t maximum_slices{10'000};
};

struct audio_slice_range_t {
    std::uint64_t begin_frame{};
    std::uint64_t end_frame{};

    auto operator==(const audio_slice_range_t&) const -> bool = default;
};

// Returns contiguous, non-empty, non-overlapping ranges that cover
// [0, source_frames) exactly. Boundary N is computed from N in exact integer
// arithmetic, never accumulated, so long sources do not drift. The last slice
// may be shorter than the others. Any invalid input yields diagnostics and no
// ranges.
struct audio_slice_plan_t {
    [[nodiscard]] static auto from(const audio_slicer_request_t& request)
        -> diagnosed_t<audio_slice_plan_t, std::vector<audio_slicer_diagnostic_t>>;

    std::vector<audio_slice_range_t> ranges;
    auto operator==(const audio_slice_plan_t&) const -> bool = default;
};
using audio_slicer_outcome_t =
    diagnosed_t<audio_slice_plan_t, std::vector<audio_slicer_diagnostic_t>>;

} // namespace wormedit

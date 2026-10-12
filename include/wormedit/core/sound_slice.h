#pragma once

#include <wormedit/core/timing.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wormedit {

enum class sound_slice_error_code_t {
    invalid_document,
    invalid_sample_rate,
    invalid_source_range,
    timing_failure,
    arithmetic_overflow,
    capacity_exceeded,
};

using sound_slice_diagnostic_t =
    object_diagnostic_t<sound_slice_error_code_t, sound_slice_error_code_t::invalid_document>;

// One BMSON-derived slice. All channel events at the same musical row share
// this source range and therefore have slice polyphony one. restart records
// the normalized same-row continuation state (one c=false event wins).
struct derived_sound_slice_t {
    std::uint64_t absolute_row{};
    std::uint64_t source_begin_frame{};
    std::optional<std::uint64_t> source_end_frame;
    bool restart{};
    std::vector<object_id_t> events;
};

struct sound_slice_options_t {
    std::size_t maximum_events{200'000};
    sample_rounding_t rounding{sample_rounding_t::nearest_ties_to_even};
};

struct sound_slice_result_t {
    std::vector<derived_sound_slice_t> slices;
    std::vector<sound_slice_diagnostic_t> diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

// Derives exact source-frame boundaries from the channel's distinct musical
// event times. Each restart rewinds to source_clip.begin_frame and anchors
// subsequent continuation offsets at its own chart time. source_frame_count
// is the decoded asset length; when neither it
// nor source_clip.end_frame is known, the final slice remains open-ended.
[[nodiscard]] auto derive_sound_channel_slices(
    const chart_document_t& chart, const sound_channel_t& channel,
    std::uint32_t sample_rate, std::optional<std::uint64_t> source_frame_count = std::nullopt,
    sound_slice_options_t options = {}) -> sound_slice_result_t;

[[nodiscard]] auto find_sound_slice(const sound_slice_result_t& result, object_id_t event)
    -> const derived_sound_slice_t*;

} // namespace wormedit

#pragma once

#include "sys/audio/chart_preview_state.h"

#include <wormedit/audio/scrub_reader.h>
#include <wormedit/core/timing.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace wormedit::sys {

// The playback timeline frame of a (possibly fractional) absolute chart row, before export cuts.
[[nodiscard]] inline auto scrub_source_frame_at_row(const timing_index_t& timing, double row)
    -> std::optional<std::uint64_t> {
    if (!std::isfinite(row) || row < 0.0) return std::uint64_t{0};
    constexpr std::uint64_t scale = 1'000'000;
    auto whole = static_cast<std::uint64_t>(std::floor(row));
    auto fraction = static_cast<std::uint64_t>(std::llround((row - std::floor(row)) * scale));
    if (fraction == scale) {
        ++whole;
        fraction = 0;
    }
    const auto at = timing.query_seconds(exact_chart_row_t{whole, {fraction, scale}});
    if (!at) return std::nullopt;
    if (at->value.numerator < 0) return std::uint64_t{0};
    const auto frame = sample_frame_t::from(at->value, audio::output_sample_rate);
    if (!frame) return std::nullopt;
    return frame->value.frame;
}

// The song as stretches of the playback timeline (export cuts split it). Empty when the chart
// has no song audio to scrub.
[[nodiscard]] inline auto scrub_segments_of(const runtime::chart_preview_t::compiled_t& compiled)
    -> std::vector<audio::scrub_segment_t> {
    std::vector<audio::scrub_segment_t> segments;
    if (!compiled.schedule) return segments;
    const auto binding = std::ranges::find(compiled.bindings, audio::preview_music_object,
                                           &audio::preview_object_source_binding_t::object);
    if (binding == compiled.bindings.end() || binding->voices.empty()) return segments;
    for (const auto& event : compiled.schedule->music_events())
        segments.push_back({binding->voices.front().source, event.sample_frame,
                            event.source_start_frame, event.frame_count});
    return segments;
}

} // namespace wormedit::sys

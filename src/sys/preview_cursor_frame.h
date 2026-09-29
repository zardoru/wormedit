#pragma once

#include <wormedit/audio/preview_bridge.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/timing.h>

#include <cstdint>
#include <string>
#include <variant>

namespace wormedit::sys {

[[nodiscard]] inline auto preview_cursor_frame(const chart_document_t& chart,
                                               const timing_index_t& timing,
                                               local_position_t cursor)
    -> std::variant<std::uint64_t, std::string> {
    const auto built = build_measure_index(chart);
    const auto* measures = std::get_if<measure_index_t>(&built);
    if (!measures) return std::string{"Chart cursor has invalid measure geometry."};
    const auto at = timing_at(timing, *measures, cursor);
    if (!at) return std::string{"Chart cursor has no playable timing position."};
    if (at.seconds->numerator < 0) return std::uint64_t{0};
    const auto frame = sample_frame_at(*at.seconds, audio::output_sample_rate);
    if (!frame) return std::string{"Chart cursor time cannot be converted to audio frames."};
    return *frame.frame;
}

} // namespace wormedit::sys

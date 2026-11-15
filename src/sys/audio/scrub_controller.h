#pragma once

#include "sys/audio/sdl_preview_output.h"
#include "sys/audio/sdl_scrub_output.h"

#include <wormedit/audio/scrub_reader.h>
#include <wormedit/core/scrub_tracker.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace wormedit::sys {

struct scrub_frame_input_t {
    double now_seconds{};
    // "Scrub audio while scrolling" preference.
    bool enabled{true};
    // Continuous scroll input (touchpad, inertia) arrived this frame.
    bool scrolled{};
    // The chart preview was playing.
    bool playing{};
    // Where the scroll has dragged the view or playhead, as a playback timeline frame.
    std::optional<std::uint64_t> timeline_frame;
    // Fingers resting on the pad; empty when the platform does not report them.
    std::optional<int> fingers;
    // The song on the playback timeline.
    std::span<const audio::scrub_segment_t> segments;
};

// Drives the scrub output from the per-frame scroll position: the tracker turns that into needle
// speed, the output plays it, and while the chart preview is playing the song is muted until a
// short moment after the gesture ends.
class scrub_controller_t {
  public:
    scrub_controller_t(sdl_scrub_output_t& scrub, sdl_preview_output_t& preview)
        : scrub_(scrub), preview_(preview) {}

    void update(const scrub_frame_input_t& input);
    [[nodiscard]] auto active() const noexcept -> bool {
        return tracker_.phase() != scrub_phase_t::idle;
    }

  private:
    void cancel();

    sdl_scrub_output_t& scrub_;
    sdl_preview_output_t& preview_;
    scrub_tracker_t tracker_;
};

} // namespace wormedit::sys

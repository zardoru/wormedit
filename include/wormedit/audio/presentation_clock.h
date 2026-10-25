#pragma once

#include <wormedit/audio/playhead_snapshot.h>

namespace wormedit::audio {

// One frame-thread consumer of coherent output evidence. The output owner owns
// the clock; this projection never advances beyond already rendered PCM.
class presentation_clock_t {
  public:
    [[nodiscard]] auto frame(const playhead_state_t& report,
                             std::uint64_t presentation_ns,
                             std::uint32_t sample_rate) noexcept -> std::uint64_t;
    void reset() noexcept { *this = {}; }

  private:
    playhead_state_t previous_{};
    std::uint64_t previous_frame_{};
    std::uint64_t previous_time_ns_{};
};

} // namespace wormedit::audio

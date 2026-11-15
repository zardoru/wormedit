#pragma once

#include <cstdint>
#include <optional>

namespace wormedit {

// Turns the audio position that smooth scrolling drags across (in seconds on the playback
// timeline) into the needle motion of a turntable: a signed, smoothed speed, a "held" state
// while fingers rest on the touchpad, and the moment normal playback may resume.
struct scrub_settings_t {
    // Speed smoothing: a touchpad reports in bursts, the needle should not.
    double smoothing_seconds{0.04};
    // |speed| is clamped to this multiple of normal playback.
    double maximum_speed{4.0};
    // Below this |speed| the needle counts as stopped.
    double dead_zone{0.03};
    // Without scroll input for this long the gesture has stopped moving.
    double idle_seconds{0.1};
    // Playback that scrub interrupted resumes this long after the gesture ends.
    double resume_delay_seconds{0.15};
};

struct scrub_input_t {
    // Monotonic clock in seconds.
    double now_seconds{};
    // Where the scrolled-to point sits on the playback timeline, in seconds.
    std::optional<double> position_seconds;
    // A continuous scroll event (touchpad, momentum) arrived since the last update.
    bool scrolled{};
    // Fingers resting on the touchpad; empty when the platform cannot tell.
    std::optional<int> fingers;
    // Playback was running when the gesture began (read only on that update).
    bool was_playing{};
};

enum class scrub_phase_t : std::uint8_t {
    idle,
    // Needle follows the scroll.
    moving,
    // Fingers rest on the pad without moving it: the record is held.
    held,
};

struct scrub_output_t {
    scrub_phase_t phase{scrub_phase_t::idle};
    // Audio seconds per real second; negative reads backwards.
    double speed{};
    // The gesture started on this update; `anchor_seconds` is where the needle drops.
    bool began{};
    double anchor_seconds{};
    // The gesture finished on this update.
    bool ended{};
    // Playback that the gesture interrupted should restart now, at `resume_seconds`.
    bool resume_playback{};
    double resume_seconds{};

    [[nodiscard]] auto is_active() const noexcept -> bool { return phase != scrub_phase_t::idle; }
};

class scrub_tracker_t {
  public:
    explicit scrub_tracker_t(scrub_settings_t settings = {});

    [[nodiscard]] auto update(const scrub_input_t& input) -> scrub_output_t;
    void cancel() noexcept;
    [[nodiscard]] auto phase() const noexcept -> scrub_phase_t { return phase_; }
    [[nodiscard]] auto settings() const noexcept -> const scrub_settings_t& { return settings_; }

  private:
    [[nodiscard]] auto begin(const scrub_input_t& input) -> scrub_output_t;
    void follow(const scrub_input_t& input, double elapsed);
    [[nodiscard]] auto finish(const scrub_input_t& input) -> scrub_output_t;

    scrub_settings_t settings_;
    scrub_phase_t phase_{scrub_phase_t::idle};
    std::optional<double> last_time_;
    std::optional<double> last_position_;
    double last_scroll_time_{};
    double speed_{};
    bool resume_on_end_{};
    // The gesture ended and a playing session still has to restart.
    bool resume_pending_{};
    double resume_due_{};
    double resume_position_{};
};

} // namespace wormedit

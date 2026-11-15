#include <wormedit/core/scrub_tracker.h>

#include <algorithm>
#include <cmath>

namespace wormedit {

scrub_tracker_t::scrub_tracker_t(scrub_settings_t settings) : settings_(settings) {
    settings_.smoothing_seconds = std::max(settings_.smoothing_seconds, 1e-4);
    settings_.maximum_speed = std::max(settings_.maximum_speed, 0.1);
    settings_.dead_zone = std::clamp(settings_.dead_zone, 0.0, settings_.maximum_speed);
    settings_.idle_seconds = std::max(settings_.idle_seconds, 0.0);
    settings_.resume_delay_seconds = std::max(settings_.resume_delay_seconds, 0.0);
    settings_.touch_hold_fingers = std::max(settings_.touch_hold_fingers, 1);
    settings_.touch_hold_seconds = std::max(settings_.touch_hold_seconds, 0.0);
}

void scrub_tracker_t::cancel() noexcept {
    phase_ = scrub_phase_t::idle;
    last_time_.reset();
    last_position_.reset();
    speed_ = 0.0;
    resume_on_end_ = false;
    resume_pending_ = false;
    touch_since_.reset();
}

auto scrub_tracker_t::begin(const scrub_input_t& input, bool held) -> scrub_output_t {
    phase_ = held ? scrub_phase_t::held : scrub_phase_t::moving;
    touch_since_.reset();
    last_time_ = input.now_seconds;
    last_position_ = input.position_seconds;
    // A touch-held record starts out already quiet.
    last_scroll_time_ = held ? input.now_seconds - settings_.idle_seconds : input.now_seconds;
    speed_ = 0.0;
    // A gesture that restarts before playback resumed keeps owing that resume.
    resume_on_end_ = input.was_playing || resume_pending_;
    resume_pending_ = false;
    return {.phase = phase_, .began = true, .anchor_seconds = *input.position_seconds};
}

// Fingers laid on the pad while the chart plays hold the record even before any scroll.
auto scrub_tracker_t::begin_from_touch(const scrub_input_t& input) -> std::optional<scrub_output_t> {
    const auto touching = input.was_playing && input.position_seconds && input.fingers &&
                          *input.fingers >= settings_.touch_hold_fingers;
    if (!touching) {
        touch_since_.reset();
        return std::nullopt;
    }
    if (!touch_since_) touch_since_ = input.now_seconds;
    if (input.now_seconds - *touch_since_ < settings_.touch_hold_seconds) return std::nullopt;
    return begin(input, true);
}

void scrub_tracker_t::follow(const scrub_input_t& input, double elapsed) {
    if (input.scrolled) last_scroll_time_ = input.now_seconds;
    double raw = 0.0;
    if (elapsed > 0.0 && input.position_seconds && last_position_)
        raw = (*input.position_seconds - *last_position_) / elapsed;
    if (input.position_seconds) last_position_ = input.position_seconds;
    if (elapsed <= 0.0) return;
    const auto alpha = 1.0 - std::exp(-elapsed / settings_.smoothing_seconds);
    speed_ += (std::clamp(raw, -settings_.maximum_speed, settings_.maximum_speed) - speed_) * alpha;
}

auto scrub_tracker_t::finish(const scrub_input_t& input) -> scrub_output_t {
    scrub_output_t out{.ended = true};
    if (resume_on_end_) {
        resume_pending_ = true;
        resume_due_ = input.now_seconds + settings_.resume_delay_seconds;
        resume_position_ = last_position_.value_or(0.0);
    }
    phase_ = scrub_phase_t::idle;
    last_time_.reset();
    last_position_.reset();
    speed_ = 0.0;
    resume_on_end_ = false;
    return out;
}

auto scrub_tracker_t::update(const scrub_input_t& input) -> scrub_output_t {
    if (phase_ == scrub_phase_t::idle) {
        if (input.scrolled && input.position_seconds) return begin(input);
        if (const auto touched = begin_from_touch(input)) return *touched;
        scrub_output_t out;
        if (resume_pending_ && input.now_seconds >= resume_due_) {
            resume_pending_ = false;
            out.resume_playback = true;
            out.resume_seconds = resume_position_;
        }
        return out;
    }
    const auto elapsed = input.now_seconds - last_time_.value_or(input.now_seconds);
    last_time_ = input.now_seconds;
    follow(input, elapsed);
    const auto quiet = input.now_seconds - last_scroll_time_ >= settings_.idle_seconds;
    if (quiet) {
        const auto resting = input.fingers && *input.fingers > 0;
        if (!resting) return finish(input);
        phase_ = scrub_phase_t::held;
        speed_ = 0.0;
    } else {
        phase_ = scrub_phase_t::moving;
    }
    scrub_output_t out{.phase = phase_};
    out.speed = std::abs(speed_) < settings_.dead_zone ? 0.0 : speed_;
    return out;
}

} // namespace wormedit

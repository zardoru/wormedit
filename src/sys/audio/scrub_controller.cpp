#include "sys/audio/scrub_controller.h"

#include <wormedit/audio/decoder.h>

namespace wormedit::sys {

void scrub_controller_t::cancel() {
    if (tracker_.phase() != scrub_phase_t::idle || tracker_.is_resume_pending()) {
        tracker_.cancel();
        scrub_.end();
    }
    preview_.set_muted(false);
}

void scrub_controller_t::update(const scrub_frame_input_t& input) {
    if (!input.enabled || !scrub_.available() || input.segments.empty()) {
        cancel();
        scrub_.service();
        return;
    }
    scrub_.set_segments({input.segments.begin(), input.segments.end()});
    const auto sample_rate = static_cast<double>(audio::output_sample_rate);
    const auto out = tracker_.update({
        .now_seconds = input.now_seconds,
        .position_seconds = input.timeline_frame
            ? std::optional{static_cast<double>(*input.timeline_frame) / sample_rate} : std::nullopt,
        .scrolled = input.scrolled,
        .fingers = input.fingers,
        .was_playing = input.playing,
    });
    if (out.began && !scrub_.begin(*input.timeline_frame)) {
        tracker_.cancel();
        preview_.set_muted(false);
        return;
    }
    if (out.is_active()) scrub_.set_motion(out.speed, out.phase == scrub_phase_t::held);
    if (out.ended) scrub_.end();
    // The song stays silent from the first scrub until playback resumes; a stopped chart
    // preview has nothing to mute.
    const auto interrupted = tracker_.phase() != scrub_phase_t::idle || tracker_.is_resume_pending();
    preview_.set_muted(interrupted && input.playing);
    scrub_.service();
}

} // namespace wormedit::sys

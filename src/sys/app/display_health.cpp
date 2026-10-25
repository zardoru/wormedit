#include "sys/app/display_health.h"

namespace wormedit::sys {

auto display_health_t::observe(std::uint64_t now, std::uint64_t progress, frame_phase_t phase)
    -> std::optional<display_notice_t> {
    if (!progress || now < progress) { stall_progress_.reset(); return {}; }
    if (stall_progress_ && progress != *stall_progress_) {
        const auto duration = now - *stall_progress_;
        stall_progress_.reset();
        return display_notice_t{display_notice_kind_t::resumed, duration, phase};
    }
    if (!stall_progress_ && now - progress >= 2'000'000'000) {
        stall_progress_ = progress;
        return display_notice_t{display_notice_kind_t::stalled, now - progress, phase};
    }
    return {};
}

auto display_health_t::record_frame(std::uint64_t duration) -> std::optional<display_notice_t> {
    if (duration <= 250'000'000) {
        slow_frames_ = 0;
        slow_total_ns_ = 0;
        throttled_ = false;
        return {};
    }
    if (throttled_) return {};
    slow_total_ns_ += duration;
    if (++slow_frames_ < 10) return {};
    throttled_ = true;
    return display_notice_t{display_notice_kind_t::throttled, slow_total_ns_ / slow_frames_,
                            frame_phase_t::render_present};
}

} // namespace wormedit::sys

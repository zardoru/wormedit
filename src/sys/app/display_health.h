#pragma once

#include "sys/app/frame_timing.h"
#include <cstdint>
#include <optional>
#include <string_view>

namespace wormedit::sys {

enum class display_notice_kind_t { stalled, resumed, throttled };
struct display_notice_t {
    display_notice_kind_t kind{};
    std::uint64_t duration_ns{};
    frame_phase_t phase{frame_phase_t::count};
};

// Pure clock policy. The watchdog owns sampling/logging; the frame loop owns
// consecutive-frame evidence. Neither waits on the renderer or audio service.
class display_health_t {
  public:
    [[nodiscard]] auto observe(std::uint64_t now_ns, std::uint64_t progress_ns,
                               frame_phase_t phase) -> std::optional<display_notice_t>;
    [[nodiscard]] auto record_frame(std::uint64_t duration_ns) -> std::optional<display_notice_t>;
  private:
    std::optional<std::uint64_t> stall_progress_;
    std::uint64_t slow_total_ns_{};
    unsigned slow_frames_{};
    bool throttled_{};
};

[[nodiscard]] inline auto preferred_video_drivers(std::string_view wayland_display,
    std::string_view explicit_driver) -> std::optional<std::string_view> {
    if (!wayland_display.empty() && explicit_driver.empty()) return "wayland,x11";
    return {};
}

} // namespace wormedit::sys

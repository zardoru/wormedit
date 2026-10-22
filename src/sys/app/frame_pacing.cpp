#include "sys/app/frame_pacing.h"

namespace wormedit::sys {

auto frame_pacing_remainder_ns(std::uint64_t frame_start_ns, std::uint64_t now_ns,
                               bool inactive_window) noexcept -> std::uint64_t {
    if (now_ns < frame_start_ns) return 0;
    const auto period = inactive_window ? inactive_frame_period_ns : active_frame_period_ns;
    const auto elapsed = now_ns - frame_start_ns;
    return elapsed >= period ? 0 : period - elapsed;
}

} // namespace wormedit::sys

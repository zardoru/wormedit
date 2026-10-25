#include "sys/app/display_watchdog.h"
#include <SDL3/SDL_timer.h>
#include <array>
#include <cstdio>

namespace wormedit::sys {
display_watchdog_t::display_watchdog_t(frame_timing_t& timing, notification_sink_t notifications,
    std::string driver, std::filesystem::path crash_log)
    : timing_(timing), notifications_(notifications), driver_(std::move(driver)),
      crash_log_(std::move(crash_log)), presentation_progress_(SDL_GetTicksNS()),
      thread_([this](std::stop_token stop) { monitor(stop); }) {}

display_watchdog_t::~display_watchdog_t() {
    thread_.request_stop();
    wake_.notify_all();
    thread_.join();
}

void display_watchdog_t::publish_window(std::uint64_t flags, bool focused) noexcept {
    flags_.store(flags, std::memory_order_relaxed);
    focused_.store(focused, std::memory_order_relaxed);
}

void display_watchdog_t::monitor(std::stop_token stop) {
    display_health_t health;
    std::unique_lock lock{mutex_};
    while (!stop.stop_requested()) {
        wake_.wait_for(lock, stop, std::chrono::milliseconds{500}, [] { return false; });
        if (stop.stop_requested()) break;
        const auto progress = presentation_pending_.load(std::memory_order_acquire)
            ? frame_progress_t{presentation_progress_.load(std::memory_order_acquire), frame_phase_t::render_present}
            : timing_.progress();
        if (const auto notice = health.observe(SDL_GetTicksNS(), progress.timestamp_ns, progress.phase)) report(*notice);
    }
}

void display_watchdog_t::record_frame(std::uint64_t duration, bool pending) {
    if (!pending) presentation_progress_.store(SDL_GetTicksNS(), std::memory_order_release);
    presentation_pending_.store(pending, std::memory_order_release);
    if (const auto notice = frame_health_.record_frame(duration)) report(*notice);
}

void display_watchdog_t::report(const display_notice_t& notice) {
    const auto phase = static_cast<std::size_t>(notice.phase);
    const auto* name = phase < frame_phase_names.size() ? frame_phase_names[phase] : "between frames";
    const auto seconds = static_cast<double>(notice.duration_ns) / 1e9;
    std::array<char, 512> line{};
    std::snprintf(line.data(), line.size(), "wormedit: display %s: %.3f s; phase=%s driver=%s flags=0x%llx focused=%d\n",
        notice.kind == display_notice_kind_t::stalled ? "stalled" :
            notice.kind == display_notice_kind_t::resumed ? "resumed" : "throttled",
        seconds, name, driver_.c_str(), static_cast<unsigned long long>(flags_.load(std::memory_order_relaxed)),
        focused_.load(std::memory_order_relaxed));
    std::fputs(line.data(), stderr);
    if (!crash_log_.empty()) {
        if (auto* file = std::fopen(crash_log_.string().c_str(), "a")) {
            std::fputs(line.data(), file);
            std::fclose(file);
        }
    }
    if (notice.kind == display_notice_kind_t::stalled) return;
    std::array<char, 160> message{};
    if (notice.kind == display_notice_kind_t::resumed)
        std::snprintf(message.data(), message.size(), "Display presentation stalled for %.1f s", seconds);
    else std::snprintf(message.data(), message.size(), "The compositor is throttling this window (approximately %.1f fps)", 1.0 / seconds);
    notifications_.post(notification_severity_t::warning, notification_source_t::system, message.data());
}
} // namespace wormedit::sys

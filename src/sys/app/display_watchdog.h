#pragma once
#include "sys/app/display_health.h"
#include <wormedit/core/notifications.h>
#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace wormedit::sys {
class display_watchdog_t {
  public:
    display_watchdog_t(frame_timing_t& timing, notification_sink_t notifications,
                       std::string driver, std::filesystem::path crash_log);
    ~display_watchdog_t();
    void publish_window(std::uint64_t flags, bool focused) noexcept;
    void record_frame(std::uint64_t duration_ns, bool presentation_pending = false);
  private:
    void monitor(std::stop_token stop);
    void report(const display_notice_t& notice);
    frame_timing_t& timing_;
    notification_sink_t notifications_;
    std::string driver_;
    std::filesystem::path crash_log_;
    std::atomic<std::uint64_t> flags_{};
    std::atomic<bool> focused_{};
    std::atomic<bool> presentation_pending_{};
    std::atomic<std::uint64_t> presentation_progress_{};
    display_health_t frame_health_;
    std::mutex mutex_;
    std::condition_variable_any wake_;
    std::jthread thread_;
};
} // namespace wormedit::sys

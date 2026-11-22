#pragma once

#include "sys/updates/manifest_fetch.h"

#include <wormedit/core/notifications.h>
#include <wormedit/core/update_check.h>

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace wormedit::sys {

// Where the check stands, for the Help menu. Copied out under a lock.
struct update_status_t {
    enum class phase_t { idle, checking, up_to_date, available, failed } phase{phase_t::idle};
    update_manifest_entry_t latest; // Set when `available`.
    std::string error;              // Set when `failed`.
    auto operator==(const update_status_t&) const -> bool = default;
};

struct update_checker_options_t {
    std::string current_version;
    std::string manifest_url;
    std::filesystem::path state_path; // Throttle file; empty disables throttling.
    manifest_fetch_t fetch;
    notification_sink_t notifications{notification_sink_t::muted()};
};

// One background check at a time, on its own thread. Fails quietly: an automatic check that
// cannot reach the network posts nothing; a manual one reports the failure.
class update_checker_t {
  public:
    explicit update_checker_t(update_checker_options_t options);
    ~update_checker_t();
    update_checker_t(const update_checker_t&) = delete;
    auto operator=(const update_checker_t&) -> update_checker_t& = delete;

    // Automatic: skipped when the last check was under a day ago. Manual: always asks and
    // reports every outcome. Returns false when a check is already running or skipped.
    auto start(bool manual, std::int64_t now_unix_seconds) -> bool;
    [[nodiscard]] auto status() const -> update_status_t;
    // Waits for the running check; tests only.
    void join();

  private:
    void run(bool manual, std::int64_t now_unix_seconds, std::stop_token stop);
    void publish(update_status_t status);
    void report(const update_status_t& status, bool manual) const;

    update_checker_options_t options_;
    mutable std::mutex mutex_;
    update_status_t status_;
    std::jthread worker_;
};

} // namespace wormedit::sys

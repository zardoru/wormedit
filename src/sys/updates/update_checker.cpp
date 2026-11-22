#include "sys/updates/update_checker.h"

#include <wormedit/core/atomic_file_storage.h>

#include <fstream>
#include <sstream>

namespace wormedit::sys {
namespace {

auto read_last_check(const std::filesystem::path& path) -> std::int64_t {
    if (path.empty()) return 0;
    std::ifstream input(path);
    std::ostringstream text;
    text << input.rdbuf();
    return parse_update_check_state(text.str().substr(0, 64));
}

void write_last_check(const std::filesystem::path& path, std::int64_t now) {
    if (path.empty()) return;
    // Best effort: a failed write only means the next launch asks again.
    (void)create_file_atomic(path, serialize_update_check_state(now), 64);
}

} // namespace

update_checker_t::update_checker_t(update_checker_options_t options) : options_(std::move(options)) {}

update_checker_t::~update_checker_t() {
    worker_.request_stop();
    // The jthread joins here; the fetch observes the stop token through its progress callback.
}

auto update_checker_t::status() const -> update_status_t {
    std::lock_guard lock{mutex_};
    return status_;
}

void update_checker_t::join() {
    if (worker_.joinable()) worker_.join();
}

auto update_checker_t::start(bool manual, std::int64_t now_unix_seconds) -> bool {
    if (!options_.fetch) return false;
    {
        std::lock_guard lock{mutex_};
        if (status_.phase == update_status_t::phase_t::checking) return false;
        if (!manual && !is_update_check_due(read_last_check(options_.state_path), now_unix_seconds)) return false;
        status_.phase = update_status_t::phase_t::checking;
    }
    join(); // The previous worker has finished publishing; this only releases its thread.
    worker_ = std::jthread{[this, manual, now_unix_seconds](std::stop_token stop) {
        run(manual, now_unix_seconds, stop);
    }};
    return true;
}

void update_checker_t::run(bool manual, std::int64_t now_unix_seconds, std::stop_token stop) {
    update_status_t result;
    const auto body = options_.fetch(options_.manifest_url, stop);
    if (stop.stop_requested()) return;
    if (!body) {
        result.phase = update_status_t::phase_t::failed;
        result.error = body.error();
    } else if (const auto entry = parse_update_manifest(*body, update_channel_t::stable); !entry) {
        result.phase = update_status_t::phase_t::failed;
        result.error = entry.error();
    } else {
        write_last_check(options_.state_path, now_unix_seconds);
        const auto decision = evaluate_update(options_.current_version, *entry);
        result.phase = decision == update_decision_t::available ? update_status_t::phase_t::available
                                                                 : update_status_t::phase_t::up_to_date;
        if (decision == update_decision_t::available) result.latest = *entry;
        if (decision == update_decision_t::unsupported_build) result.error = "This build is not checked for updates.";
    }
    report(result, manual);
    publish(std::move(result));
}

void update_checker_t::publish(update_status_t status) {
    std::lock_guard lock{mutex_};
    status_ = std::move(status);
}

void update_checker_t::report(const update_status_t& status, bool manual) const {
    using phase_t = update_status_t::phase_t;
    if (status.phase == phase_t::available) {
        notification_post_t post{.severity = notification_severity_t::info,
                                 .source = notification_source_t::system,
                                 .message = "WormEdit " + status.latest.version + " is available."};
        post.detail = "Download: " + status.latest.url;
        if (!status.latest.notes_url.empty()) post.detail += "\nRelease notes: " + status.latest.notes_url;
        options_.notifications.post(post);
    } else if (manual && status.phase == phase_t::up_to_date) {
        options_.notifications.post(notification_severity_t::info, notification_source_t::system,
                                    status.error.empty() ? "WormEdit is up to date."
                                                         : "Updates are not checked for this build.");
    } else if (manual && status.phase == phase_t::failed) {
        options_.notifications.post(notification_severity_t::warning, notification_source_t::system,
                                    "Could not check for updates. " + status.error);
    }
}

} // namespace wormedit::sys

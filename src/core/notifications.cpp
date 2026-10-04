#include <wormedit/core/notifications.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wormedit {
namespace {

constexpr std::string_view ellipsis = "\xE2\x80\xA6"; // U+2026

auto is_utf8_continuation(char byte) noexcept -> bool {
    return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
}

auto steady_clock_now() -> notification_time_t {
    return std::chrono::duration_cast<notification_time_t>(
        std::chrono::steady_clock::now().time_since_epoch());
}

// hint and info share the cheapest class, so the oldest of either goes first.
auto eviction_class(notification_severity_t severity) noexcept -> int {
    switch (severity) {
    case notification_severity_t::hint:
    case notification_severity_t::info:
        return 0;
    case notification_severity_t::success:
        return 1;
    case notification_severity_t::warning:
        return 2;
    case notification_severity_t::error:
        return 3;
    }
    return 0;
}

auto same_message(const notification_entry_t& left, const notification_entry_t& right) -> bool {
    return left.severity == right.severity && left.source == right.source &&
           left.message == right.message;
}

auto saturating_count(std::uint32_t left, std::uint32_t right) noexcept -> std::uint32_t {
    const auto limit = std::numeric_limits<std::uint32_t>::max();
    return left > limit - right ? limit : left + right;
}

// Folds `incoming` into the newest entry when it repeats it within the window.
auto try_merge(std::vector<notification_entry_t>& entries, const notification_entry_t& incoming)
    -> bool {
    if (entries.empty())
        return false;
    auto& newest = entries.back();
    if (!same_message(newest, incoming) ||
        incoming.first_timestamp - newest.timestamp > notification_dedupe_window)
        return false;
    newest.count = saturating_count(newest.count, incoming.count);
    newest.timestamp = std::max(newest.timestamp, incoming.timestamp);
    newest.read = false;
    return true;
}

// Oldest entry of the cheapest class present.
auto eviction_victim(const std::vector<notification_entry_t>& entries) -> std::size_t {
    std::size_t victim = 0;
    for (std::size_t index = 1; index < entries.size(); ++index) {
        if (eviction_class(entries[index].severity) < eviction_class(entries[victim].severity))
            victim = index;
    }
    return victim;
}

// Appends with dedupe and the eviction policy. The incoming entry is itself a
// candidate: when everything stored outranks it, it is the one dropped.
void insert_bounded(std::vector<notification_entry_t>& entries, notification_entry_t&& incoming,
                    notification_statistics_t& statistics) {
    if (try_merge(entries, incoming)) {
        ++statistics.merged;
        return;
    }
    if (entries.size() >= notification_capacity) {
        ++statistics.evicted;
        const auto victim = eviction_victim(entries);
        if (eviction_class(entries[victim].severity) > eviction_class(incoming.severity))
            return;
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(victim));
    }
    entries.push_back(std::move(incoming));
}

auto find_entry(std::vector<notification_entry_t>& entries, notification_id_t id)
    -> std::vector<notification_entry_t>::iterator {
    return std::find_if(entries.begin(), entries.end(),
                        [id](const notification_entry_t& entry) { return entry.id == id; });
}

auto diagnostic_severity(diagnostic_severity_t severity) noexcept -> notification_severity_t {
    switch (severity) {
    case diagnostic_severity_t::info:
        return notification_severity_t::info;
    case diagnostic_severity_t::warning:
        return notification_severity_t::warning;
    case diagnostic_severity_t::error:
        return notification_severity_t::error;
    }
    return notification_severity_t::error;
}

void add_statistics(notification_statistics_t& total, const notification_statistics_t& part) {
    total.posted += part.posted;
    total.merged += part.merged;
    total.evicted += part.evicted;
    total.dismissed += part.dismissed;
}

} // namespace

void truncate_notification_text(std::string& text, std::size_t max_bytes) {
    if (text.size() <= max_bytes)
        return;
    if (max_bytes < ellipsis.size()) {
        text.clear();
        return;
    }
    auto cut = max_bytes - ellipsis.size();
    while (cut > 0 && is_utf8_continuation(text[cut]))
        --cut;
    text.resize(cut);
    text.append(ellipsis);
}

notification_center_t::notification_center_t(notification_clock_t clock)
    : clock_(clock ? std::move(clock) : notification_clock_t{steady_clock_now}) {
    inbox_.reserve(notification_capacity);
    draining_.reserve(notification_capacity);
    ring_.reserve(notification_capacity);
}

void notification_center_t::post(notification_post_t request) {
    truncate_notification_text(request.message, notification_message_max_bytes);
    truncate_notification_text(request.detail, notification_detail_max_bytes);
    notification_entry_t entry{.severity = request.severity,
                               .source = request.source,
                               .message = std::move(request.message),
                               .detail = std::move(request.detail),
                               .target = request.target};

    const std::scoped_lock lock{inbox_mutex_};
    // Taking the time under the lock keeps inbox order and timestamps aligned.
    last_post_time_ = std::max(last_post_time_, clock_());
    entry.id = next_id_++;
    entry.first_timestamp = last_post_time_;
    entry.timestamp = last_post_time_;
    ++inbox_statistics_.posted;
    insert_bounded(inbox_, std::move(entry), inbox_statistics_);
}

void notification_center_t::post(notification_severity_t severity, notification_source_t source,
                                 std::string message) {
    post(notification_post_t{.severity = severity,
                             .source = source,
                             .message = std::move(message),
                             .detail = {},
                             .target = {}});
}

void notification_center_t::post_diagnostics(notification_source_t source,
                                             const diagnostic_list_t& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        notification_post_t request{.severity = diagnostic_severity(diagnostic.severity),
                                    .source = source,
                                    .message = diagnostic.message,
                                    .detail = {},
                                    .target = {}};
        if (diagnostic.object)
            request.target = *diagnostic.object;
        post(std::move(request));
    }
}

void notification_sink_t::post(notification_severity_t severity, notification_source_t source,
                               std::string_view message) const noexcept {
    if (message.empty())
        return;
    try {
        center_->post(notification_post_t{.severity = severity,
                                          .source = source,
                                          .message = std::string{message},
                                          .detail = message.size() > notification_message_max_bytes
                                              ? std::string{message} : std::string{},
                                          .target = {}});
    } catch (...) {
        // Posting must not escape into worker/runtime code.
    }
}

void notification_sink_t::post(const notification_post_t& request) const noexcept {
    if (request.message.empty())
        return;
    try {
        center_->post(notification_post_t{.severity = request.severity,
                                          .source = request.source,
                                          .message = request.message,
                                          .detail = request.detail.empty() &&
                                                    request.message.size() > notification_message_max_bytes
                                              ? request.message : request.detail,
                                          .target = request.target});
    } catch (...) {
        // Copying and center posting can allocate or call an injected clock.
    }
}

void notification_sink_t::post_diagnostics(notification_source_t source,
                                           const diagnostic_list_t& diagnostics) const noexcept {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.message.empty())
            continue;
        try {
            notification_post_t request{.severity = diagnostic_severity(diagnostic.severity),
                                        .source = source,
                                        .message = diagnostic.message,
                                        .detail = diagnostic.message.size() > notification_message_max_bytes
                                            ? diagnostic.message : std::string{},
                                        .target = {}};
            if (diagnostic.object)
                request.target = *diagnostic.object;
            center_->post(std::move(request));
        } catch (...) {
            // Keep attempting later diagnostics if one entry cannot be posted.
        }
    }
}

auto notification_center_t::drain_inbox(notification_time_t now) -> std::size_t {
    {
        const std::scoped_lock lock{inbox_mutex_};
        inbox_.swap(draining_);
    }
    const auto drained = draining_.size();
    for (auto& entry : draining_)
        insert_bounded(ring_, std::move(entry), ring_statistics_);
    draining_.clear();
    frame_time_ = std::max(frame_time_, now);
    return drained;
}

auto notification_center_t::mark_read(notification_id_t id) -> bool {
    const auto found = find_entry(ring_, id);
    if (found == ring_.end())
        return false;
    found->read = true;
    return true;
}

void notification_center_t::mark_all_read() {
    for (auto& entry : ring_)
        entry.read = true;
}

auto notification_center_t::dismiss(notification_id_t id) -> bool {
    const auto found = find_entry(ring_, id);
    if (found == ring_.end())
        return false;
    ring_.erase(found);
    ++ring_statistics_.dismissed;
    return true;
}

void notification_center_t::dismiss_all() {
    ring_statistics_.dismissed += ring_.size();
    ring_.clear();
}

auto notification_center_t::snapshot(std::size_t max) const -> std::vector<notification_entry_t> {
    const auto count = std::min(max, ring_.size());
    std::vector<notification_entry_t> result;
    result.reserve(count);
    std::copy_n(ring_.rbegin(), count, std::back_inserter(result));
    return result;
}

auto notification_center_t::borrowed_snapshot(std::size_t max) const noexcept
    -> std::span<const notification_entry_t> {
    return std::span<const notification_entry_t>{ring_}.last(std::min(max, ring_.size()));
}

auto notification_center_t::current_line() const -> const notification_entry_t* {
    if (ring_.empty())
        return nullptr;
    const auto newest_non_hint =
        std::find_if(ring_.rbegin(), ring_.rend(), [](const notification_entry_t& entry) {
            return entry.severity != notification_severity_t::hint;
        });
    if (newest_non_hint != ring_.rend() &&
        frame_time_ - newest_non_hint->timestamp < notification_current_line_hold)
        return &*newest_non_hint;
    const auto newest_hint =
        std::find_if(ring_.rbegin(), ring_.rend(), [](const notification_entry_t& entry) {
            return entry.severity == notification_severity_t::hint;
        });
    return newest_hint == ring_.rend() ? nullptr : &*newest_hint;
}

auto notification_center_t::unread_counts() const -> notification_unread_counts_t {
    notification_unread_counts_t counts;
    for (const auto& entry : ring_) {
        if (entry.read)
            continue;
        if (entry.severity == notification_severity_t::warning)
            ++counts.warnings;
        else if (entry.severity == notification_severity_t::error)
            ++counts.errors;
    }
    return counts;
}

auto notification_center_t::size() const noexcept -> std::size_t {
    return ring_.size();
}

auto notification_center_t::statistics() const -> notification_statistics_t {
    auto total = ring_statistics_;
    const std::scoped_lock lock{inbox_mutex_};
    add_statistics(total, inbox_statistics_);
    return total;
}

} // namespace wormedit

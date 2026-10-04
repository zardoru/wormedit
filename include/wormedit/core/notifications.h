#pragma once

#include <wormedit/core/document.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace wormedit {

// Ordered by importance: eviction removes the least important class first and
// the status line prefers anything above `hint`.
enum class notification_severity_t : std::uint8_t {
    hint,
    info,
    success,
    warning,
    error,
};

enum class notification_source_t : std::uint8_t {
    document,
    edit,
    audio,
    export_,
    import_,
    recovery,
    format,
    input,
    system,
};

using notification_id_t = std::uint64_t;
using notification_time_t = std::chrono::nanoseconds;
// Must be monotonic and safe to call from any posting thread.
using notification_clock_t = std::function<notification_time_t()>;

inline constexpr std::size_t notification_capacity = 256;
inline constexpr std::size_t notification_message_max_bytes = 200;
inline constexpr std::size_t notification_detail_max_bytes = 4 * 1024;
inline constexpr notification_time_t notification_dedupe_window = std::chrono::seconds{2};
inline constexpr notification_time_t notification_current_line_hold = std::chrono::seconds{4};

struct notification_chart_position_t {
    chart_id_t chart;
    local_position_t position;

    auto operator==(const notification_chart_position_t&) const -> bool = default;
};

// "Go to" destination: nothing, an exact chart position, or a document object.
using notification_target_t =
    std::variant<std::monostate, notification_chart_position_t, object_id_t>;

struct notification_post_t {
    notification_severity_t severity{notification_severity_t::info};
    notification_source_t source{notification_source_t::system};
    std::string message;
    std::string detail;
    notification_target_t target;
};

struct notification_entry_t {
    notification_id_t id{};
    notification_severity_t severity{notification_severity_t::info};
    notification_source_t source{notification_source_t::system};
    std::string message;
    std::string detail;
    notification_target_t target;
    // First and most recent occurrence; repeats only move `timestamp`.
    notification_time_t first_timestamp{};
    notification_time_t timestamp{};
    std::uint32_t count{1};
    bool read{false};
};

struct notification_unread_counts_t {
    std::size_t warnings{};
    std::size_t errors{};
};

// Every accepted post ends up in exactly one bucket once the inbox is drained:
// posted == stored + merged + evicted + dismissed.
struct notification_statistics_t {
    std::uint64_t posted{};
    std::uint64_t merged{};
    std::uint64_t evicted{};
    std::uint64_t dismissed{};
};

// Truncates `text` in place to at most `max_bytes`, cutting at a UTF-8 code
// point boundary and ending with "…" when anything was removed.
void truncate_notification_text(std::string& text, std::size_t max_bytes);

// Bounded, deduplicating notification queue. `post` and `post_diagnostics` are
// thread-safe and only hold the inbox mutex briefly. Every other member belongs
// to the frame thread, which calls `drain_inbox` once per frame; posts become
// visible only after the next drain.
class notification_center_t {
  public:
    // A null clock means std::chrono::steady_clock.
    explicit notification_center_t(notification_clock_t clock = {});

    notification_center_t(const notification_center_t&) = delete;
    auto operator=(const notification_center_t&) -> notification_center_t& = delete;
    notification_center_t(notification_center_t&&) = delete;
    auto operator=(notification_center_t&&) -> notification_center_t& = delete;
    ~notification_center_t() = default;

    void post(notification_post_t request);
    void post(notification_severity_t severity, notification_source_t source,
              std::string message);
    // Maps diagnostic info/warning/error to the same severities; a diagnostic
    // object becomes the entry's target.
    void post_diagnostics(notification_source_t source, const diagnostic_list_t& diagnostics);

    // Moves pending posts into the ring and records `now` as the frame time
    // used by `current_line`. Returns the number of inbox entries taken.
    auto drain_inbox(notification_time_t now) -> std::size_t;

    auto mark_read(notification_id_t id) -> bool;
    void mark_all_read();
    auto dismiss(notification_id_t id) -> bool;
    void dismiss_all();

    // Newest first, at most `max` entries.
    [[nodiscard]] auto snapshot(std::size_t max) const -> std::vector<notification_entry_t>;
    // Allocation-free view of the newest `max` entries, oldest first.
    // Frame-thread only; do not retain across center mutations. Worker posts
    // alone do not invalidate the view, because they only modify the inbox.
    [[nodiscard]] auto borrowed_snapshot(std::size_t max) const noexcept
        -> std::span<const notification_entry_t>;
    // The newest non-hint entry while it is younger than the hold time at the
    // last drain, otherwise the newest hint, or nullptr if none exists. A hint
    // therefore never replaces a non-hint posted in the same frame.
    [[nodiscard]] auto current_line() const -> const notification_entry_t*;
    [[nodiscard]] auto unread_counts() const -> notification_unread_counts_t;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto statistics() const -> notification_statistics_t;

  private:
    notification_clock_t clock_;

    mutable std::mutex inbox_mutex_;
    std::vector<notification_entry_t> inbox_;
    notification_id_t next_id_{1};
    notification_time_t last_post_time_{};
    notification_statistics_t inbox_statistics_;

    std::vector<notification_entry_t> draining_;
    std::vector<notification_entry_t> ring_;
    notification_statistics_t ring_statistics_;
    notification_time_t frame_time_{};
};

// Non-owning, thread-safe posting handle. The owner must declare the center
// before every runtime/scheduler that receives a sink, then join all posting
// workers before destroying the center on normal and exceptional exits. The
// sink never accesses UI state.
// Messages longer than the 200-byte line bound also become detail when callers
// supply no explicit detail. The center's 4 KiB UTF-8 detail bound still applies.
class notification_sink_t {
  public:
    explicit notification_sink_t(notification_center_t& center) noexcept : center_(&center) {}

    void post(notification_severity_t severity, notification_source_t source,
              std::string_view message) const noexcept;
    void post(const notification_post_t& request) const noexcept;
    void post_diagnostics(notification_source_t source,
                          const diagnostic_list_t& diagnostics) const noexcept;

  private:
    notification_center_t* center_;
};

} // namespace wormedit

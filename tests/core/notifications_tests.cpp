#include <wormedit/core/notifications.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace {

using namespace wormedit;
using namespace std::chrono_literals;

// Manually advanced clock shared with the center; safe to read from any thread.
struct manual_clock_t {
    std::shared_ptr<std::atomic<std::int64_t>> nanoseconds =
        std::make_shared<std::atomic<std::int64_t>>(0);

    void advance(notification_time_t step) const {
        nanoseconds->fetch_add(step.count());
    }
    [[nodiscard]] auto now() const -> notification_time_t {
        return notification_time_t{nanoseconds->load()};
    }
    [[nodiscard]] auto function() const -> notification_clock_t {
        return [counter = nanoseconds] { return notification_time_t{counter->load()}; };
    }
};

const std::string ellipsis = "\xE2\x80\xA6";

// Every accepted post must be stored, merged, evicted, or dismissed.
auto accounted(const notification_center_t& center) -> bool {
    const auto stats = center.statistics();
    return center.size() + stats.merged + stats.evicted + stats.dismissed == stats.posted;
}

void post_underrun(notification_center_t& center,
                   notification_source_t source = notification_source_t::audio,
                   notification_severity_t severity = notification_severity_t::warning) {
    center.post(severity, source, "Underrun");
}

auto current_message(const notification_center_t& center) -> std::string {
    const auto* line = center.current_line();
    return line == nullptr ? std::string{"<none>"} : line->message;
}

void post_and_drain(notification_center_t& center, const manual_clock_t& clock,
                    notification_severity_t severity, const std::string& message) {
    center.post(severity, notification_source_t::input, message);
    center.drain_inbox(clock.now());
}

const std::vector<notification_severity_t> severity_cycle{
    notification_severity_t::hint, notification_severity_t::info,
    notification_severity_t::success, notification_severity_t::warning,
    notification_severity_t::error};

auto cycle_severity(int index) -> notification_severity_t {
    return severity_cycle[static_cast<std::size_t>(index) % severity_cycle.size()];
}

auto cycle_message(int index) -> std::string {
    return "message " + std::to_string(index);
}

// 300 posts over the five severities, drained every 7 posts so eviction
// happens in both the inbox and the ring.
void post_severity_cycle(notification_center_t& center, const manual_clock_t& clock) {
    for (int index = 0; index < 300; ++index) {
        center.post(cycle_severity(index), notification_source_t::system, cycle_message(index));
        if (index % 7 == 0)
            center.drain_inbox(clock.now());
    }
    center.drain_inbox(clock.now());
}

auto kept_messages(const notification_center_t& center) -> std::set<std::string> {
    std::set<std::string> kept;
    for (const auto& entry : center.snapshot(notification_capacity))
        kept.insert(entry.message);
    return kept;
}

auto count_at_or_below(const notification_center_t& center, notification_severity_t severity)
    -> std::size_t {
    std::size_t count = 0;
    for (const auto& entry : center.snapshot(notification_capacity))
        count += entry.severity <= severity ? 1U : 0U;
    return count;
}

auto every_important_post_kept(const std::set<std::string>& kept) -> bool {
    for (int index = 0; index < 300; ++index) {
        if (cycle_severity(index) > notification_severity_t::info &&
            !kept.contains(cycle_message(index)))
            return false;
    }
    return true;
}

constexpr int thread_count = 4;

// Each thread posts `posts` distinct messages; every 100th is an error.
void post_from_threads(notification_center_t& center, const manual_clock_t& clock, int posts,
                       std::atomic<int>& finished) {
    std::vector<std::thread> workers;
    for (int thread = 0; thread < thread_count; ++thread) {
        workers.emplace_back([&center, &clock, &finished, thread, posts] {
            for (int index = 0; index < posts; ++index) {
                const auto severity = index % 100 == 0 ? notification_severity_t::error
                                                       : notification_severity_t::info;
                clock.advance(1ns);
                center.post(severity, notification_source_t::system,
                            std::to_string(thread) + ":" + std::to_string(index));
            }
            finished.fetch_add(1);
        });
    }
    // The frame thread keeps draining while the workers post.
    while (finished.load() < thread_count)
        center.drain_inbox(clock.now());
    for (auto& worker : workers)
        worker.join();
    center.drain_inbox(clock.now());
}

struct ring_shape_t {
    std::size_t distinct_ids{};
    std::size_t errors{};
    bool newest_first{true};
};

auto ring_shape(const notification_center_t& center) -> ring_shape_t {
    ring_shape_t shape;
    std::set<notification_id_t> ids;
    auto previous = notification_time_t::max();
    for (const auto& entry : center.snapshot(notification_capacity)) {
        ids.insert(entry.id);
        shape.newest_first = shape.newest_first && entry.timestamp <= previous;
        previous = entry.timestamp;
        shape.errors += entry.severity == notification_severity_t::error ? 1U : 0U;
    }
    shape.distinct_ids = ids.size();
    return shape;
}

auto repeated(const std::string& unit, int times) -> std::string {
    std::string text;
    for (int index = 0; index < times; ++index)
        text += unit;
    return text;
}

void check_typed_sink_entry(const notification_entry_t& entry) {
    CHECK(entry.message == "typed message");
    CHECK(entry.detail == "typed detail");
    CHECK(entry.source == notification_source_t::format);
    CHECK(entry.target == notification_target_t{object_id_t{72}});
}

void check_basic_sink_entries(const notification_center_t& center) {
    const auto entries = center.snapshot(notification_capacity);
    REQUIRE(entries.size() == 2);
    CHECK(entries[1].message == "first exact bytes");
    CHECK(entries[1].severity == notification_severity_t::warning);
    CHECK(entries[1].source == notification_source_t::edit);
    check_typed_sink_entry(entries[0]);
}

auto sink_diagnostics_fixture() -> diagnostic_list_t {
    return {
        diagnostic_t{.severity = diagnostic_severity_t::error,
                     .code = diagnostic_code_t::unknown_lane,
                     .message = "dropped after clock failure",
                     .object = object_id_t{7}},
        diagnostic_t{.severity = diagnostic_severity_t::info,
                     .code = diagnostic_code_t::approximated_feature,
                     .message = "",
                     .object = std::nullopt},
        diagnostic_t{.severity = diagnostic_severity_t::warning,
                     .code = diagnostic_code_t::dropped_feature,
                     .message = "retained warning",
                     .object = object_id_t{42}},
        diagnostic_t{.severity = diagnostic_severity_t::info,
                     .code = diagnostic_code_t::approximated_feature,
                     .message = "retained info",
                     .object = std::nullopt},
    };
}

void check_diagnostic_info_entry(const notification_entry_t& entry) {
    CHECK(entry.message == "retained info");
    CHECK(entry.severity == notification_severity_t::info);
}

void check_diagnostic_sink_entries(const notification_center_t& center) {
    const auto entries = center.snapshot(10);
    REQUIRE(entries.size() == 2);
    CHECK(entries[1].message == "retained warning");
    CHECK(entries[1].severity == notification_severity_t::warning);
    CHECK(entries[1].source == notification_source_t::format);
    CHECK(entries[1].target == notification_target_t{object_id_t{42}});
    check_diagnostic_info_entry(entries[0]);
}

TEST_CASE("notification center merges a repeat of the newest entry within two seconds",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};

    post_underrun(center);
    clock.advance(1500ms);
    post_underrun(center);
    REQUIRE(center.drain_inbox(clock.now()) == 1);
    // Exactly at the window edge still merges, measured from the latest repeat.
    clock.advance(2s);
    post_underrun(center);
    center.drain_inbox(clock.now());

    const auto entries = center.snapshot(10);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].count == 3);
    CHECK(entries[0].first_timestamp == 0ns);
    CHECK(entries[0].timestamp == 3500ms);
    CHECK(accounted(center));
}

TEST_CASE("notification center adds rows outside the window or for other keys",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    post_underrun(center);
    clock.advance(2001ms);
    post_underrun(center);
    post_underrun(center, notification_source_t::export_);
    post_underrun(center, notification_source_t::export_, notification_severity_t::error);
    // Only the newest entry is a dedupe candidate.
    post_underrun(center);
    center.drain_inbox(clock.now());

    CHECK(center.size() == 5);
    CHECK(center.statistics().merged == 0);
    CHECK(center.snapshot(10).at(1).count == 1);
    CHECK(accounted(center));
}

TEST_CASE("notification dedupe bridges a premerged inbox batch across drains",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    post_underrun(center);
    center.drain_inbox(clock.now());
    clock.advance(1500ms);
    post_underrun(center);
    clock.advance(1500ms);
    post_underrun(center);
    center.drain_inbox(clock.now());
    const auto entries = center.snapshot(10);
    REQUIRE(entries.size() == 1);
    CHECK((entries[0].count == 3 && entries[0].first_timestamp == 0ns &&
           entries[0].timestamp == 3s));
    CHECK(accounted(center));
}

TEST_CASE("notification center marks a merged repeat unread again", "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    post_underrun(center, notification_source_t::recovery, notification_severity_t::error);
    center.drain_inbox(clock.now());
    const auto id = center.snapshot(1).at(0).id;
    REQUIRE(center.mark_read(id));
    CHECK(center.unread_counts().errors == 0);

    post_underrun(center, notification_source_t::recovery, notification_severity_t::error);
    center.drain_inbox(clock.now());
    const auto entry = center.snapshot(1).at(0);
    CHECK(entry.id == id);
    CHECK(entry.count == 2);
    CHECK_FALSE(entry.read);
    CHECK(center.unread_counts().errors == 1);
}

TEST_CASE("notification center evicts hint and info first and errors last under 300 posts",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    post_severity_cycle(center, clock);

    REQUIRE(center.size() == notification_capacity);
    CHECK(center.statistics().posted == 300);
    CHECK(center.statistics().evicted == 300 - notification_capacity);
    CHECK(accounted(center));
    // 44 evictions consume the oldest 44 of the 120 hint/info posts.
    CHECK(count_at_or_below(center, notification_severity_t::info) == 120 - 44);
    CHECK(every_important_post_kept(kept_messages(center)));
}

TEST_CASE("notification center evicts hint and info together, oldest first",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    post_severity_cycle(center, clock);

    // Hints are 0, 5, …; infos are 1, 6, …; the 44th of them is message 106.
    const auto kept = kept_messages(center);
    CHECK_FALSE(kept.contains("message 0"));
    CHECK_FALSE(kept.contains("message 1"));
    CHECK_FALSE(kept.contains("message 106"));
    CHECK(kept.contains("message 110"));
    CHECK(kept.contains("message 296"));
}

TEST_CASE("notification snapshot is newest first and bounded", "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    post_severity_cycle(center, clock);

    const auto newest = center.snapshot(3);
    REQUIRE(newest.size() == 3);
    CHECK(newest[0].message == "message 299");
    CHECK(newest[2].message == "message 297");
    CHECK(center.snapshot(1000).size() == notification_capacity);
}

TEST_CASE("notification center drops a low-severity post when only errors are stored",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    for (std::size_t index = 0; index < notification_capacity; ++index)
        center.post(notification_severity_t::error, notification_source_t::format,
                    "error " + std::to_string(index));
    center.drain_inbox(clock.now());
    post_and_drain(center, clock, notification_severity_t::info, "late info");
    CHECK(center.snapshot(1).at(0).message == "error 255");

    post_and_drain(center, clock, notification_severity_t::error, "newest error");
    const auto entries = center.snapshot(notification_capacity);
    CHECK(entries.front().message == "newest error");
    CHECK(entries.back().message == "error 1");
    CHECK(center.statistics().evicted == 2);
}

TEST_CASE("notification borrowed snapshot bounds the newest tail without copying entries",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    for (int index = 0; index < 4; ++index)
        post_and_drain(center, clock, notification_severity_t::info, cycle_message(index));
    const auto borrowed = center.borrowed_snapshot(2);
    REQUIRE(borrowed.size() == 2);
    CHECK((borrowed.front().message == "message 2" && borrowed.back().message == "message 3"));
    CHECK(&borrowed.back() == center.current_line());
    CHECK(center.borrowed_snapshot(0).empty());
    CHECK(center.borrowed_snapshot(1000).size() == 4);
}

TEST_CASE("notification center loses nothing that fits across 4 threads x 1000 posts",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    std::atomic<int> finished{0};
    post_from_threads(center, clock, 1000, finished);

    const auto stats = center.statistics();
    CHECK((stats.posted == thread_count * 1000 && stats.merged == 0));
    CHECK(center.size() == notification_capacity);
    CHECK(accounted(center));
    const auto shape = ring_shape(center);
    CHECK((shape.distinct_ids == notification_capacity && shape.newest_first));
    // All 40 errors fit, so none may be lost.
    CHECK(shape.errors == thread_count * 1000 / 100);
}

TEST_CASE("notification center keeps every post from four threads when all fit",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    std::atomic<int> finished{0};
    post_from_threads(center, clock, 60, finished);

    CHECK(center.size() == thread_count * 60);
    CHECK(center.statistics().evicted == 0);
    CHECK(ring_shape(center).distinct_ids == thread_count * 60);
    CHECK(ring_shape(center).errors == thread_count);
}

TEST_CASE("notification hint never replaces a same-frame error as the current line",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    CHECK(center.current_line() == nullptr);
    post_and_drain(center, clock, notification_severity_t::hint, "Hold Shift");
    CHECK(current_message(center) == "Hold Shift");

    clock.advance(16ms);
    center.post(notification_severity_t::error, notification_source_t::edit, "Save failed");
    center.post(notification_severity_t::hint, notification_source_t::input,
                "Compose navigation canceled.");
    center.drain_inbox(clock.now());
    CHECK(current_message(center) == "Save failed");
}

TEST_CASE("notification current line holds a non-hint for four seconds", "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    post_and_drain(center, clock, notification_severity_t::error, "Save failed");

    clock.advance(3s);
    post_and_drain(center, clock, notification_severity_t::hint, "Drag to select");
    CHECK(current_message(center) == "Save failed");

    post_and_drain(center, clock, notification_severity_t::success, "Layer added.");
    CHECK(current_message(center) == "Layer added.");

    clock.advance(4s);
    post_and_drain(center, clock, notification_severity_t::hint, "Drag to select");
    CHECK(current_message(center) == "Drag to select");
}

TEST_CASE("notification text truncates at a UTF-8 boundary with an ellipsis",
          "[notifications]") {
    // 196 ASCII bytes and two 3-byte code points: cutting at 197 would split
    // the first code point, so the cut backs off to 196.
    auto text = std::string(196, 'a') + repeated("\xE3\x81\x82", 2);
    truncate_notification_text(text, notification_message_max_bytes);
    CHECK(text == std::string(196, 'a') + ellipsis);

    auto fits = std::string(200, 'b');
    truncate_notification_text(fits, notification_message_max_bytes);
    CHECK(fits == std::string(200, 'b'));

    std::string tiny = "abcdef";
    truncate_notification_text(tiny, 2);
    CHECK(tiny.empty());
}

TEST_CASE("notification current line returns an older hint after non-hint expiry",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    post_and_drain(center, clock, notification_severity_t::hint, "Hold Shift");
    post_and_drain(center, clock, notification_severity_t::error, "Save failed");
    clock.advance(4s);
    center.drain_inbox(clock.now());
    CHECK(current_message(center) == "Hold Shift");
}

TEST_CASE("notification current line is empty after expiry when no hint exists",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    post_and_drain(center, clock, notification_severity_t::error, "Save failed");
    clock.advance(4s);
    center.drain_inbox(clock.now());
    CHECK(center.current_line() == nullptr);
    CHECK(center.size() == 1);
}

TEST_CASE("notification center truncates posted message and detail", "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    // 100 two-byte code points fill 200 bytes; one more byte forces a cut.
    center.post(notification_post_t{.severity = notification_severity_t::info,
                                    .source = notification_source_t::document,
                                    .message = repeated("\xC3\xA9", 100) + "x",
                                    .detail = std::string(5000, 'd'),
                                    .target = {}});
    center.drain_inbox(clock.now());

    const auto entry = center.snapshot(1).at(0);
    CHECK(entry.message == repeated("\xC3\xA9", 98) + ellipsis);
    CHECK(entry.detail.size() == notification_detail_max_bytes);
    CHECK(entry.detail.ends_with(ellipsis));
}

TEST_CASE("notification center keeps targets and counts unread warnings and errors",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    const notification_chart_position_t position{
        .chart = chart_id_t{3}, .position = local_position_t{.measure = measure_id_t{4}, .row = 96}};
    center.post(notification_post_t{.severity = notification_severity_t::warning,
                                    .source = notification_source_t::import_,
                                    .message = "Overlapping notes",
                                    .detail = {},
                                    .target = position});
    post_and_drain(center, clock, notification_severity_t::error, "Device lost");

    const auto entries = center.snapshot(10);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].id != entries[1].id);
    CHECK(std::get<notification_chart_position_t>(entries[1].target) == position);
    CHECK(center.unread_counts().warnings == 1);
    CHECK(center.unread_counts().errors == 1);
}

TEST_CASE("notification center marks read and dismisses by id", "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    center.post(notification_severity_t::warning, notification_source_t::import_, "Overlap");
    post_and_drain(center, clock, notification_severity_t::error, "Device lost");
    const auto entries = center.snapshot(10);

    CHECK(center.mark_read(entries[1].id));
    CHECK(center.unread_counts().warnings == 0);
    CHECK_FALSE(center.mark_read(9999));
    CHECK(center.dismiss(entries[0].id));
    CHECK_FALSE(center.dismiss(entries[0].id));
    center.mark_all_read();
    CHECK(center.unread_counts().errors == 0);
}

TEST_CASE("notification center dismiss_all clears entries and the current line",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    center.post(notification_severity_t::warning, notification_source_t::import_, "Overlap");
    post_and_drain(center, clock, notification_severity_t::error, "Device lost");

    center.dismiss_all();
    CHECK(center.size() == 0);
    CHECK(center.current_line() == nullptr);
    CHECK(center.statistics().dismissed == 2);
    CHECK(accounted(center));
}

TEST_CASE("notification center posts diagnostics with mapped severities", "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    const diagnostic_list_t diagnostics{
        diagnostic_t{.severity = diagnostic_severity_t::info,
                     .code = diagnostic_code_t::approximated_feature,
                     .message = "Approximated BPM",
                     .object = std::nullopt},
        diagnostic_t{.severity = diagnostic_severity_t::warning,
                     .code = diagnostic_code_t::dropped_feature,
                     .message = "Dropped mine",
                     .object = object_id_t{42}},
        diagnostic_t{.severity = diagnostic_severity_t::error,
                     .code = diagnostic_code_t::unknown_lane,
                     .message = "Unknown lane",
                     .object = std::nullopt},
    };
    center.post_diagnostics(notification_source_t::format, diagnostics);
    REQUIRE(center.drain_inbox(clock.now()) == 3);

    const auto entries = center.snapshot(10);
    REQUIRE(entries.size() == 3);
    CHECK(entries[2].severity == notification_severity_t::info);
    CHECK(entries[1].severity == notification_severity_t::warning);
    CHECK(entries[0].severity == notification_severity_t::error);
}

TEST_CASE("notification center posts diagnostic objects as targets", "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    center.post_diagnostics(notification_source_t::format,
                            diagnostic_list_t{
                                diagnostic_t{.severity = diagnostic_severity_t::warning,
                                             .code = diagnostic_code_t::dropped_feature,
                                             .message = "Dropped mine",
                                             .object = object_id_t{42}},
                                diagnostic_t{.severity = diagnostic_severity_t::warning,
                                             .code = diagnostic_code_t::collision,
                                             .message = "Collision",
                                             .object = std::nullopt},
                            });
    center.drain_inbox(clock.now());

    const auto entries = center.snapshot(10);
    REQUIRE(entries.size() == 2);
    CHECK(entries[1].source == notification_source_t::format);
    CHECK(std::get<object_id_t>(entries[1].target) == object_id_t{42});
    CHECK(std::holds_alternative<std::monostate>(entries[0].target));
}

TEST_CASE("notification sink preserves explicit posts and suppresses empty messages",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    const notification_sink_t sink{center};

    sink.post(notification_severity_t::warning, notification_source_t::edit, "first exact bytes");
    const notification_post_t typed{.severity = notification_severity_t::error,
                                    .source = notification_source_t::format,
                                    .message = "typed message",
                                    .detail = "typed detail",
                                    .target = object_id_t{72}};
    sink.post(typed);
    sink.post(notification_severity_t::info, notification_source_t::system, "");
    const notification_post_t empty_typed{.severity = notification_severity_t::error,
                                          .source = notification_source_t::format,
                                          .message = "",
                                          .detail = "ignored",
                                          .target = object_id_t{73}};
    sink.post(empty_typed);
    center.drain_inbox(clock.now());
    check_basic_sink_entries(center);
}

TEST_CASE("notification sink worker posts appear after a drain and support concurrent callers",
          "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    const notification_sink_t sink{center};
    constexpr int workers = 4;
    constexpr int posts_per_worker = 12;
    std::vector<std::thread> threads;
    for (int worker = 0; worker < workers; ++worker) {
        threads.emplace_back([sink, worker] {
            for (int index = 0; index < posts_per_worker; ++index)
                sink.post(notification_severity_t::info, notification_source_t::system,
                          std::to_string(worker) + ":" + std::to_string(index));
        });
    }
    for (auto& thread : threads)
        thread.join();

    CHECK(center.size() == 0);
    CHECK(center.drain_inbox(clock.now()) == workers * posts_per_worker);
    CHECK(center.size() == workers * posts_per_worker);
}

TEST_CASE("notification sink contains clock failures and remains usable", "[notifications]") {
    std::atomic<int> calls{0};
    notification_center_t center{[&calls] {
        if (calls.fetch_add(1) == 0)
            throw std::runtime_error{"clock failure"};
        return notification_time_t{0};
    }};
    const notification_sink_t sink{center};

    CHECK_NOTHROW(sink.post(notification_severity_t::error, notification_source_t::system,
                            "clock failure is contained"));
    sink.post(notification_severity_t::success, notification_source_t::edit, "still usable");
    center.drain_inbox(notification_time_t{0});

    const auto entries = center.snapshot(10);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].message == "still usable");
}

TEST_CASE("notification sink posts diagnostics independently with mapped severity and target",
          "[notifications]") {
    std::atomic<int> calls{0};
    notification_center_t center{[&calls] {
        if (calls.fetch_add(1) == 0)
            throw std::runtime_error{"first diagnostic fails"};
        return notification_time_t{0};
    }};
    const notification_sink_t sink{center};
    CHECK_NOTHROW(sink.post_diagnostics(notification_source_t::format,
                                        sink_diagnostics_fixture()));
    center.drain_inbox(notification_time_t{0});
    check_diagnostic_sink_entries(center);
}

TEST_CASE("notification hint does not replace success on the current line", "[notifications]") {
    const manual_clock_t clock;
    notification_center_t center{clock.function()};
    const notification_sink_t sink{center};
    sink.post(notification_severity_t::success, notification_source_t::edit, "Semantic layer added.");
    sink.post(notification_severity_t::hint, notification_source_t::input, "Navigation canceled");
    center.drain_inbox(clock.now());

    REQUIRE(center.current_line() != nullptr);
    CHECK(center.current_line()->message == "Semantic layer added.");
}

} // namespace

#include "sys/sound_drop_transaction.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

auto origin() -> wormedit::sys::sound_drop_origin_t {
    return {.project_id = 10,
            .chart_id = 20,
            .revision = 30,
            .tab = WORM_UI_WORKSPACE_TAB_COMPOSE,
            .window_id = 40,
            .slots_mode = true,
            .first_slot = 17};
}

auto drop_event(SDL_EventType type, std::uint32_t window_id, const char* path = nullptr) -> SDL_Event {
    SDL_Event event{};
    event.type = type;
    event.drop.type = type;
    event.drop.windowID = window_id;
    event.drop.data = const_cast<char*>(path);
    return event;
}

auto start_drop(wormedit::sys::sound_drop_transaction_t& transaction,
                const wormedit::sys::sound_drop_origin_t& current, bool over_list) -> bool {
    const auto begin = drop_event(SDL_EVENT_DROP_BEGIN, current.window_id);
    (void)transaction.handle(begin, current, over_list);
    const auto position = drop_event(SDL_EVENT_DROP_POSITION, current.window_id);
    (void)transaction.handle(position, current, over_list);
    return transaction.is_active();
}

auto send_file(wormedit::sys::sound_drop_transaction_t& transaction,
               const wormedit::sys::sound_drop_origin_t& current,
               const std::string& path, bool over_list = false) -> bool {
    const auto file = drop_event(SDL_EVENT_DROP_FILE, current.window_id, path.c_str());
    return transaction.handle(file, current, over_list).consumed;
}

auto finish_drop(wormedit::sys::sound_drop_transaction_t& transaction,
                 const wormedit::sys::sound_drop_origin_t& current,
                 std::uint32_t event_window = 0)
    -> wormedit::sys::sound_drop_event_result_t {
    const auto complete = drop_event(SDL_EVENT_DROP_COMPLETE,
                                     event_window == 0 ? current.window_id : event_window);
    return transaction.handle(complete, current, false);
}

auto batch_matches(const wormedit::sys::sound_drop_outcome_t& batch,
                   const std::vector<std::string>& paths) -> bool {
    return batch.has_value() && batch.value().paths == paths;
}

auto collect_two_file_drop_with_slot_change()
    -> std::optional<wormedit::sys::sound_drop_outcome_t> {
    using namespace wormedit::sys;
    sound_drop_transaction_t transaction;
    auto initial = origin();
    (void)start_drop(transaction, initial, true);
    (void)send_file(transaction, initial, "/tmp/kick.wav");
    const auto outside = drop_event(SDL_EVENT_DROP_POSITION, initial.window_id);
    (void)transaction.handle(outside, initial, false);
    (void)send_file(transaction, initial, "/tmp/snare.wav");
    initial.first_slot = 44;
    return finish_drop(transaction, initial).completed;
}

struct interruption_result_t {
    std::optional<wormedit::sys::sound_drop_outcome_t> batch;
    bool active{};
    bool claimed{};
};

auto interrupt_with_new_drop() -> interruption_result_t {
    using namespace wormedit::sys;
    sound_drop_transaction_t transaction;
    const auto current = origin();
    (void)start_drop(transaction, current, true);
    (void)send_file(transaction, current, "/tmp/abandoned.wav");
    auto next = current;
    next.first_slot = 18;
    const auto begin = drop_event(SDL_EVENT_DROP_BEGIN, next.window_id);
    auto result = transaction.handle(begin, next, true);
    return {.batch = std::move(result.completed),
            .active = transaction.is_active(), .claimed = transaction.is_claimed()};
}

auto rejects_changed_origin(wormedit::sys::sound_drop_origin_t changed) -> bool {
    using namespace wormedit::sys;
    sound_drop_transaction_t transaction;
    const auto current = origin();
    (void)start_drop(transaction, current, true);
    (void)send_file(transaction, current, "/tmp/origin.wav");
    const auto result = finish_drop(transaction, changed);
    return result.completed && !result.completed->has_value();
}

auto rejects_every_origin_change() -> bool {
    auto project = origin();
    ++project.project_id;
    auto chart = origin();
    ++chart.chart_id;
    auto revision = origin();
    ++revision.revision;
    auto tab = origin();
    tab.tab = WORM_UI_WORKSPACE_TAB_AUDIO_SLICES;
    auto mode = origin();
    mode.slots_mode = false;
    const wormedit::sys::sound_drop_origin_t changed[]{project, chart, revision, tab, mode};
    return std::all_of(std::begin(changed), std::end(changed), rejects_changed_origin);
}

auto unrelated_event_is_not_consumed() -> bool {
    using namespace wormedit::sys;
    sound_drop_transaction_t transaction;
    const auto current = origin();
    (void)start_drop(transaction, current, true);
    (void)send_file(transaction, current, "/tmp/input.wav");
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    return !transaction.handle(event, current, true).consumed && transaction.is_claimed();
}

} // namespace

TEST_CASE("sound drop retains files across polls and preserves its original slot",
          "[unit][sys][sound-drop]") {
    const auto result = collect_two_file_drop_with_slot_change();
    REQUIRE(result);
    REQUIRE(batch_matches(*result, {"/tmp/kick.wav", "/tmp/snare.wav"}));
    CHECK(result->value().origin.first_slot == 17);
}

TEST_CASE("sound drop claims only when its first file lands on the Sounds list",
          "[unit][sys][sound-drop]") {
    using namespace wormedit::sys;
    sound_drop_transaction_t transaction;
    const auto current = origin();
    REQUIRE(start_drop(transaction, current, false));
    const auto outside = drop_event(SDL_EVENT_DROP_POSITION, current.window_id);
    (void)transaction.handle(outside, current, false);
    std::string first{"/tmp/outside.wav"};
    const auto first_file = drop_event(SDL_EVENT_DROP_FILE, current.window_id, first.c_str());
    CHECK_FALSE(transaction.handle(first_file, current, true).consumed);
    const auto inside = drop_event(SDL_EVENT_DROP_POSITION, current.window_id);
    (void)transaction.handle(inside, current, true);
    std::string later{"/tmp/later.wav"};
    const auto later_file = drop_event(SDL_EVENT_DROP_FILE, current.window_id, later.c_str());
    CHECK_FALSE(transaction.handle(later_file, current, true).consumed);
    CHECK_FALSE(finish_drop(transaction, current).completed);
}

TEST_CASE("implicit sound drop begin retains files until complete",
          "[unit][sys][sound-drop]") {
    using namespace wormedit::sys;
    sound_drop_transaction_t transaction;
    const auto current = origin();
    std::string first{"/tmp/first.wav"};
    const auto first_file = drop_event(SDL_EVENT_DROP_FILE, current.window_id, first.c_str());
    const auto first_result = transaction.handle(first_file, current, true);
    REQUIRE(first_result.consumed);
    CHECK_FALSE(first_result.completed);
    CHECK(transaction.is_active());
    std::string second{"/tmp/second.wav"};
    CHECK(send_file(transaction, current, second));
    const auto completed = finish_drop(transaction, current);
    REQUIRE(completed.completed);
    CHECK(batch_matches(*completed.completed, {"/tmp/first.wav", "/tmp/second.wav"}));
}

TEST_CASE("sound drop batches are not clipped at prior fixed limits",
          "[unit][sys][sound-drop]") {
    using namespace wormedit::sys;
    constexpr std::size_t path_count = 1400;
    sound_drop_transaction_t transaction;
    const auto current = origin();
    REQUIRE(start_drop(transaction, current, true));
    std::vector<std::string> paths;
    paths.reserve(path_count);
    for (std::size_t index = 0; index < path_count; ++index)
        paths.push_back("/tmp/bulk-" + std::to_string(index) + ".wav");
    for (const auto& path : paths) (void)send_file(transaction, current, path);
    const auto completed = finish_drop(transaction, current);
    REQUIRE(completed.completed);
    REQUIRE(completed.completed->has_value());
    CHECK(completed.completed->value().paths.size() == path_count);
}

TEST_CASE("sound drop retains invalid paths and rejects the complete batch",
          "[unit][sys][sound-drop]") {
    using namespace wormedit::sys;
    sound_drop_transaction_t transaction;
    const auto current = origin();
    REQUIRE(start_drop(transaction, current, true));
    std::string invalid{"relative.wav"};
    REQUIRE(send_file(transaction, current, invalid));
    const auto completed = finish_drop(transaction, current);
    REQUIRE(completed.completed);
    REQUIRE_FALSE(completed.completed->has_value());
    CHECK(completed.completed->error().reason.find("absolute") != std::string::npos);
    CHECK(completed.completed->error().paths == std::vector<std::string>{"relative.wav"});
}

TEST_CASE("sound drop retains duplicate paths but rejects the whole batch",
          "[unit][sys][sound-drop]") {
    using namespace wormedit::sys;
    sound_drop_transaction_t transaction;
    const auto current = origin();
    REQUIRE(start_drop(transaction, current, true));
    REQUIRE(send_file(transaction, current, "/tmp/duplicate.wav"));
    REQUIRE(send_file(transaction, current, "/tmp/./duplicate.wav"));
    const auto completed = finish_drop(transaction, current);
    REQUIRE(completed.completed);
    REQUIRE_FALSE(completed.completed->has_value());
    CHECK(completed.completed->error().paths.size() == 2);
}

TEST_CASE("sound drop rejects a stale revision before completion",
          "[unit][sys][sound-drop]") {
    using namespace wormedit::sys;
    sound_drop_transaction_t stale_transaction;
    const auto current = origin();
    REQUIRE(start_drop(stale_transaction, current, true));
    std::string path{"/tmp/stale.wav"};
    REQUIRE(send_file(stale_transaction, current, path));
    auto changed = current;
    ++changed.revision;
    const auto stale = finish_drop(stale_transaction, changed);
    REQUIRE(stale.completed);
    REQUIRE_FALSE(stale.completed->has_value());
    CHECK(stale.completed->error().paths == std::vector<std::string>{path});
}

TEST_CASE("sound drop rejects project chart revision tab and mode changes",
          "[unit][sys][sound-drop]") {
    CHECK(rejects_every_origin_change());
}

TEST_CASE("sound drop cancellation returns the retained rejected batch",
          "[unit][sys][sound-drop]") {
    using namespace wormedit::sys;
    sound_drop_transaction_t transaction;
    const auto current = origin();
    REQUIRE(start_drop(transaction, current, true));
    std::string path{"/tmp/cancelled.wav"};
    REQUIRE(send_file(transaction, current, path));
    const auto cancelled = transaction.cancel();
    REQUIRE(cancelled);
    REQUIRE_FALSE(cancelled->has_value());
    CHECK(cancelled->error().reason.find("cancelled") != std::string::npos);
    CHECK(cancelled->error().paths == std::vector<std::string>{path});
}

TEST_CASE("sound drop completion rejects a different window",
          "[unit][sys][sound-drop]") {
    using namespace wormedit::sys;
    sound_drop_transaction_t transaction;
    const auto current = origin();
    REQUIRE(start_drop(transaction, current, true));
    REQUIRE(send_file(transaction, current, "/tmp/window.wav"));
    const auto result = finish_drop(transaction, current, current.window_id + 1);
    REQUIRE(result.completed);
    REQUIRE_FALSE(result.completed->has_value());
}

TEST_CASE("starting another drop returns cancellation for the previous batch",
          "[unit][sys][sound-drop]") {
    const auto result = interrupt_with_new_drop();
    REQUIRE(result.batch);
    REQUIRE_FALSE(result.batch->has_value());
    CHECK(result.batch->error().paths == std::vector<std::string>{"/tmp/abandoned.wav"});
    CHECK(result.active);
    CHECK_FALSE(result.claimed);
}

TEST_CASE("claimed sound drop does not consume unrelated window events",
          "[unit][sys][sound-drop]") {
    CHECK(unrelated_event_is_not_consumed());
}

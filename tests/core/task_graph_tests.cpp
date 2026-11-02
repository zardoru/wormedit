#include <wormedit/core/task_graph.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <array>
#include <latch>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

using namespace wormedit;

TEST_CASE("task graph waits for fan-in dependencies and preserves input order",
          "[unit][core][task-graph]") {
    task_scheduler_t scheduler(1, 8);
    task_graph_t graph(4);
    std::vector<int> execution_order;
    std::mutex order_mutex;

    const auto first = graph.create_task({}, [&](std::span<const task_outcome_t>, std::stop_token) {
        std::lock_guard lock(order_mutex);
        execution_order.push_back(1);
        return task_value_t::from(std::make_shared<const int>(17));
    });
    const auto second = graph.create_task({}, [&](std::span<const task_outcome_t>, std::stop_token) {
        std::lock_guard lock(order_mutex);
        execution_order.push_back(2);
        return task_value_t::from(std::make_shared<const int>(25));
    });
    REQUIRE(first);
    REQUIRE(second);
    const task_id_t dependencies[] = {*first, *second};
    const auto joined = graph.create_task(dependencies, [&](std::span<const task_outcome_t> inputs,
                                                         std::stop_token) -> task_outcome_t {
        if (inputs.size() != 2) {
            return std::unexpected(task_failure_t::from(failure_code_t::task_failed,
                                          "fan-in did not receive both inputs"));
        }
        const auto left = task_value_cast<int>(inputs[0]);
        const auto right = task_value_cast<int>(inputs[1]);
        if (!left || !right) {
            return std::unexpected(task_failure_t::from(failure_code_t::task_failed,
                                          "fan-in received an invalid payload"));
        }
        std::lock_guard lock(order_mutex);
        execution_order.push_back(3);
        return task_value_t::from(std::make_shared<const int>(*left + *right));
    });
    REQUIRE(joined);

    auto submitted = scheduler.begin(std::move(graph));
    REQUIRE(submitted);
    REQUIRE(submitted.value().wait());
    const auto result = submitted.value().outcome(*joined);
    REQUIRE(result.has_value());
    REQUIRE(result->has_value());
    CHECK(*task_value_cast<int>(*result) == 42);
    CHECK_FALSE(task_value_cast<std::string>(*result));
    CHECK(execution_order == std::vector<int>{1, 2, 3});
}

TEST_CASE("task errors and cancellation propagate through dependent continuations",
          "[unit][core][task-graph]") {
    task_scheduler_t scheduler(1, 8);
    task_graph_t failed_graph(3);
    std::atomic_int dependent_runs{};
    const auto root =
        failed_graph.create_task({}, [](std::span<const task_outcome_t>, std::stop_token) {
            return std::unexpected(task_failure_t::from(failure_code_t::task_failed, "decode failed"));
        });
    REQUIRE(root);
    const task_id_t root_dependency[] = {*root};
    const auto child = failed_graph.create_task(root_dependency,
                                             [&](std::span<const task_outcome_t>, std::stop_token) {
                                                 ++dependent_runs;
                                                 return task_outcome_t{};
                                             });
    REQUIRE(child);
    const task_id_t child_dependency[] = {*child};
    const auto grandchild = failed_graph.create_task(
        child_dependency, [&](std::span<const task_outcome_t>, std::stop_token) {
            ++dependent_runs;
            return task_outcome_t{};
        });
    REQUIRE(grandchild);

    auto failed = scheduler.begin(std::move(failed_graph));
    REQUIRE(failed);
    REQUIRE(failed.value().wait());
    CHECK(failed.value().outcome(*root)->error().cause.code == failure_code_t::task_failed);
    CHECK(failed.value().outcome(*child)->error().cause.code == failure_code_t::dependency_failed);
    CHECK(failed.value().outcome(*grandchild)->error().cause.code == failure_code_t::dependency_failed);
    CHECK(dependent_runs == 0);

    task_graph_t cancellable_graph(2);
    std::atomic_bool started{};
    const auto running =
        cancellable_graph.create_task({}, [&](std::span<const task_outcome_t>, std::stop_token stop) {
            started.store(true, std::memory_order_release);
            while (!stop.stop_requested()) {
                std::this_thread::yield();
            }
            return task_outcome_t{};
        });
    REQUIRE(running);
    const task_id_t running_dependency[] = {*running};
    const auto waiting = cancellable_graph.create_task(
        running_dependency,
        [](std::span<const task_outcome_t>, std::stop_token) { return task_outcome_t{}; });
    REQUIRE(waiting);
    auto cancelled = scheduler.begin(std::move(cancellable_graph));
    REQUIRE(cancelled);
    while (!started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    cancelled.value().cancel();
    REQUIRE(cancelled.value().wait());
    CHECK(cancelled.value().outcome(*running)->error().cause.code == failure_code_t::cancelled);
    CHECK(cancelled.value().outcome(*waiting)->error().cause.code == failure_code_t::cancelled);
}

TEST_CASE("task scheduler rejects graphs beyond its outstanding capacity",
          "[unit][core][task-graph]") {
    task_scheduler_t scheduler(2, 1);
    task_graph_t oversized(2);
    REQUIRE(oversized.create_task({}, [](std::span<const task_outcome_t>, std::stop_token) {
        return task_outcome_t{};
    }));
    REQUIRE(oversized.create_task({}, [](std::span<const task_outcome_t>, std::stop_token) {
        return task_outcome_t{};
    }));

    const auto rejected = scheduler.begin(std::move(oversized));
    CHECK_FALSE(rejected);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == failure_code_t::capacity_exceeded);
    CHECK(scheduler.outstanding_tasks() == 0);

    task_scheduler_t one_slot_scheduler(1, 1);
    task_graph_t active_graph(1);
    std::atomic_bool started{};
    const auto active_task =
        active_graph.create_task({}, [&](std::span<const task_outcome_t>, std::stop_token stop) {
            started.store(true, std::memory_order_release);
            while (!stop.stop_requested()) {
                std::this_thread::yield();
            }
            return task_outcome_t{};
        });
    REQUIRE(active_task);
    auto active = one_slot_scheduler.begin(std::move(active_graph));
    REQUIRE(active);
    while (!started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    task_graph_t second_graph(1);
    REQUIRE(second_graph.create_task({}, [](std::span<const task_outcome_t>, std::stop_token) {
        return task_outcome_t{};
    }));
    const auto second = one_slot_scheduler.begin(std::move(second_graph));
    CHECK_FALSE(second);
    REQUIRE_FALSE(second);
    CHECK(second.error().code == failure_code_t::capacity_exceeded);
    active.value().cancel();
    REQUIRE(active.value().wait());
}

TEST_CASE("task callback exceptions become typed task errors", "[unit][core][task-graph]") {
    task_scheduler_t scheduler(1, 2);
    task_graph_t graph(1);
    const auto task =
        graph.create_task({}, [](std::span<const task_outcome_t>, std::stop_token) -> task_outcome_t {
            throw std::runtime_error("unexpected decode error");
        });
    REQUIRE(task);
    const auto submitted = scheduler.begin(std::move(graph));
    REQUIRE(submitted);
    REQUIRE(submitted.value().wait());
    const auto result = submitted.value().outcome(*task);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->has_value());
    CHECK(result->error().cause.code == failure_code_t::task_threw);
    CHECK(result->error().cause.message == "unexpected decode error");
}

TEST_CASE("generation guard rejects stale async publication", "[unit][core][task-graph]") {
    generation_guard_t guard;
    auto published = 0;
    CHECK_FALSE(guard.is_current(0));
    CHECK_FALSE(guard.publish_if_current(0, [&] { published = -1; }));
    const auto old_generation = guard.begin_generation();
    const auto current_generation = guard.begin_generation();

    CHECK_FALSE(guard.is_current(old_generation));
    CHECK(guard.is_current(current_generation));
    CHECK_FALSE(guard.publish_if_current(old_generation, [&] { published = 1; }));
    CHECK(guard.publish_if_current(current_generation, [&] { published = 2; }));
    CHECK(published == 2);
}

TEST_CASE("scheduler shutdown requests cancellation and completes waiting graph handles",
          "[unit][core][task-graph]") {
    task_scheduler_t scheduler(1, 4);
    task_graph_t graph(2);
    std::atomic_bool started{};
    const auto active =
        graph.create_task({}, [&](std::span<const task_outcome_t>, std::stop_token stop) {
            started.store(true, std::memory_order_release);
            while (!stop.stop_requested()) {
                std::this_thread::yield();
            }
            return task_outcome_t{};
        });
    REQUIRE(active);
    const task_id_t active_dependency[] = {*active};
    const auto blocked =
        graph.create_task(active_dependency, [](std::span<const task_outcome_t>, std::stop_token) {
            return task_outcome_t{};
        });
    REQUIRE(blocked);
    auto submitted = scheduler.begin(std::move(graph));
    REQUIRE(submitted);
    while (!started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    scheduler.shutdown();
    REQUIRE(submitted.value().wait());
    CHECK(submitted.value().outcome(*active)->error().cause.code == failure_code_t::scheduler_stopped);
    CHECK(submitted.value().outcome(*blocked)->error().cause.code == failure_code_t::scheduler_stopped);
    CHECK(scheduler.outstanding_tasks() == 0);
}

TEST_CASE("task scheduler worker count is clamped to leave two hardware threads free",
          "[core][task-graph]") {
    CHECK(clamp_worker_count(8, 8) == 6);
    CHECK(clamp_worker_count(2, 8) == 2);
    CHECK(clamp_worker_count(4, 2) == 1);
    CHECK(clamp_worker_count(4, 1) == 1);
    CHECK(clamp_worker_count(0, 8) == 1);
    CHECK(clamp_worker_count(3, 0) == 3);
}

TEST_CASE("task scheduler runs the worker start hook once per worker and survives a throwing hook",
          "[core][task-graph]") {
    std::atomic<int> started{0};
    {
        task_scheduler_t scheduler(1, 2, {.on_worker_start = [&] { ++started; }});
        CHECK(scheduler.worker_count() == 1);
    }
    CHECK(started.load() == 1);

    task_scheduler_t throwing(1, 2,
                              {.on_worker_start = [] { throw std::runtime_error("denied"); }});
    task_graph_t graph(1);
    const auto added = graph.create_task({}, [](std::span<const task_outcome_t>, std::stop_token) {
        return task_value_t::from(std::make_shared<const int>(7));
    });
    REQUIRE(added);
    auto submitted = throwing.begin(std::move(graph));
    REQUIRE(submitted);
    REQUIRE(submitted.value().wait());
    CHECK(*task_value_cast<int>(*submitted.value().outcome(*added)) == 7);
}

TEST_CASE("task outcomes retain failure payloads through callbacks",
          "[unit][core][task-graph][result]") {
    const auto value = std::make_shared<const int>(91);
    task_graph_t graph{1};
    const auto task = graph.create_task({}, [value](std::span<const task_outcome_t>, std::stop_token) {
        return task_outcome_t{std::unexpect, task_failure_t::from(
            failure_code_t::task_failed, "failed with payload", task_value_t::from(value))};
    });
    REQUIRE(task.has_value());
    task_scheduler_t scheduler{1, 1};
    const auto submitted = scheduler.begin(std::move(graph));
    REQUIRE(submitted.has_value());
    REQUIRE(submitted->wait());
    const auto result = submitted->outcome(*task);
    REQUIRE(result.has_value());
    REQUIRE_FALSE(result->has_value());
    CHECK(result->error().payload.as<int>() == value);
}

TEST_CASE("task casts check payload types and reject failed outcomes",
          "[unit][core][task-graph][result]") {
    const auto value = std::make_shared<const int>(42);
    task_outcome_t result = task_value_t::from(value);
    CHECK(task_value_cast<int>(result) == value);
    CHECK_FALSE(task_value_cast<double>(result));
    const task_outcome_t failed = std::unexpected(task_failure_t::from(
        failure_code_t::task_failed, "failed with payload", task_value_t::from(value)));
    CHECK_FALSE(task_value_cast<int>(failed));
    CHECK(failed.error().payload.as<int>() == value);
}

namespace {
enum class retained_callback_mode_t { success, failure, exception };
struct retained_callback_t {
    std::latch entered{1}, release{1}, stopped{1};
    std::atomic<bool> released{}, observed_stop{};
    std::atomic<int> unclaimed_runs{};
    void unblock() { if (!released.exchange(true)) release.count_down(); }
    auto run(retained_callback_mode_t mode, std::stop_token stop) -> task_outcome_t {
        std::stop_callback notification{stop, [&] { stopped.count_down(); }};
        entered.count_down(); release.wait(); observed_stop.store(stop.stop_requested());
        if (mode == retained_callback_mode_t::exception) throw std::runtime_error{"after stop"};
        const auto value = task_value_t::from(std::make_shared<const int>(42));
        if (mode == retained_callback_mode_t::failure)
            return std::unexpected(task_failure_t::from(failure_code_t::io_failed, "precise worker failure", value));
        return value;
    }
};
struct retained_graph_t {
    retained_callback_t callback;
    task_scheduler_t scheduler{1,3};
    task_graph_handle_t handle;
    task_id_t running{}, dependent{}, queued{};
    retained_graph_t(task_cancel_result_policy_t policy, retained_callback_mode_t mode = retained_callback_mode_t::success) {
        task_graph_t graph{3};
        const auto first = graph.create_task({}, [&, mode](auto, std::stop_token stop) { return callback.run(mode, stop); }, policy);
        REQUIRE(first); running = *first;
        const std::array dependencies{running};
        const auto child = graph.create_task(dependencies, [&](auto, std::stop_token) {
            ++callback.unclaimed_runs; return task_outcome_t{};
        }, policy);
        REQUIRE(child); dependent = *child;
        const auto extra = graph.create_task({}, [&](auto, std::stop_token) {
            ++callback.unclaimed_runs; return task_outcome_t{};
        }, policy);
        REQUIRE(extra); queued = *extra;
        const auto submitted = scheduler.begin(std::move(graph)); REQUIRE(submitted); handle = *submitted;
        callback.entered.wait();
    }
    ~retained_graph_t() { callback.unblock(); scheduler.shutdown(); }
    void cancel_and_finish() { handle.cancel(); callback.unblock(); REQUIRE(handle.wait()); }
};
void check_cancelled_task(const task_graph_handle_t& handle, task_id_t task) {
    const auto outcome = handle.outcome(task);
    REQUIRE(outcome);
    REQUIRE_FALSE(*outcome);
    CHECK(outcome->error().cause.code == failure_code_t::cancelled);
}
}

TEST_CASE("default running callback success is still overridden by explicit cancellation", "[unit][core][task-graph][cancel-policy]") {
    retained_graph_t fixture{task_cancel_result_policy_t::discard_running_result}; fixture.cancel_and_finish();
    const auto outcome = fixture.handle.outcome(fixture.running); REQUIRE(outcome); REQUIRE_FALSE(*outcome);
    CHECK(outcome->error().cause.code == failure_code_t::cancelled);
    CHECK(fixture.callback.observed_stop.load());
}
TEST_CASE("opted running callback preserves success while observing the existing stop token", "[unit][core][task-graph][cancel-policy]") {
    retained_graph_t fixture{task_cancel_result_policy_t::retain_running_result}; fixture.cancel_and_finish();
    const auto outcome = fixture.handle.outcome(fixture.running); REQUIRE(outcome); REQUIRE(*outcome);
    const auto payload = task_value_cast<int>(*outcome); REQUIRE(payload); CHECK(*payload == 42);
    CHECK(fixture.callback.observed_stop.load());
}
TEST_CASE("opted running callback retains its exact failure and payload after cancellation", "[unit][core][task-graph][cancel-policy]") {
    retained_graph_t fixture{task_cancel_result_policy_t::retain_running_result, retained_callback_mode_t::failure};
    fixture.cancel_and_finish();
    const auto outcome = fixture.handle.outcome(fixture.running); REQUIRE(outcome); REQUIRE_FALSE(*outcome);
    CHECK(outcome->error().cause.code == failure_code_t::io_failed);
    CHECK(outcome->error().cause.message == "precise worker failure");
    const auto payload = outcome->error().payload.as<int>(); REQUIRE(payload); CHECK(*payload == 42);
}
TEST_CASE("opted running callback keeps the existing typed exception conversion", "[unit][core][task-graph][cancel-policy]") {
    retained_graph_t fixture{task_cancel_result_policy_t::retain_running_result, retained_callback_mode_t::exception};
    fixture.cancel_and_finish();
    const auto outcome = fixture.handle.outcome(fixture.running); REQUIRE(outcome); REQUIRE_FALSE(*outcome);
    CHECK(outcome->error().cause.code == failure_code_t::task_threw);
    CHECK(outcome->error().cause.message == "after stop");
}
TEST_CASE("retained running success cannot invoke queued or dependency-blocked work", "[unit][core][task-graph][cancel-policy]") {
    retained_graph_t fixture{task_cancel_result_policy_t::retain_running_result}; fixture.cancel_and_finish();
    check_cancelled_task(fixture.handle, fixture.queued);
    check_cancelled_task(fixture.handle, fixture.dependent);
    CHECK(fixture.callback.unclaimed_runs.load() == 0);
    CHECK(fixture.scheduler.outstanding_tasks() == 0);
}
TEST_CASE("retained cancellation releases full graph capacity for the next submission", "[unit][core][task-graph][cancel-policy]") {
    retained_graph_t fixture{task_cancel_result_policy_t::retain_running_result}; fixture.cancel_and_finish();
    task_graph_t next{3};
    for (int index = 0; index < 3; ++index)
        REQUIRE(next.create_task({}, [](auto, std::stop_token) { return task_outcome_t{}; }));
    const auto submitted = fixture.scheduler.begin(std::move(next)); REQUIRE(submitted); REQUIRE(submitted->wait());
    CHECK(fixture.scheduler.outstanding_tasks() == 0);
}
TEST_CASE("scheduler shutdown overrides even an opted running success", "[unit][core][task-graph][cancel-policy]") {
    retained_graph_t fixture{task_cancel_result_policy_t::retain_running_result};
    std::jthread shutdown{[&] { fixture.scheduler.shutdown(); }};
    fixture.callback.stopped.wait(); fixture.callback.unblock(); shutdown.join();
    REQUIRE(fixture.handle.wait());
    const auto outcome = fixture.handle.outcome(fixture.running); REQUIRE(outcome); REQUIRE_FALSE(*outcome);
    CHECK(outcome->error().cause.code == failure_code_t::scheduler_stopped);
    CHECK(fixture.callback.unclaimed_runs.load() == 0);
    CHECK(fixture.scheduler.outstanding_tasks() == 0);
}

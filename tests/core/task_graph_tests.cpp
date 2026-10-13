#include <wormedit/core/task_graph.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
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

    const auto first = graph.add_task({}, [&](std::span<const task_result_t>, std::stop_token) {
        std::lock_guard lock(order_mutex);
        execution_order.push_back(1);
        return task_result_t::success(task_value_t::from(std::make_shared<const int>(17)));
    });
    const auto second = graph.add_task({}, [&](std::span<const task_result_t>, std::stop_token) {
        std::lock_guard lock(order_mutex);
        execution_order.push_back(2);
        return task_result_t::success(task_value_t::from(std::make_shared<const int>(25)));
    });
    REQUIRE(first);
    REQUIRE(second);
    const task_id_t dependencies[] = {*first.id, *second.id};
    const auto joined = graph.add_task(dependencies, [&](std::span<const task_result_t> inputs,
                                                         std::stop_token) {
        if (inputs.size() != 2) {
            return task_result_t::failure(task_error_code_t::task_failed,
                                          "fan-in did not receive both inputs");
        }
        const auto left = task_value_cast<int>(inputs[0]);
        const auto right = task_value_cast<int>(inputs[1]);
        if (!left || !right) {
            return task_result_t::failure(task_error_code_t::task_failed,
                                          "fan-in received an invalid payload");
        }
        std::lock_guard lock(order_mutex);
        execution_order.push_back(3);
        return task_result_t::success(task_value_t::from(std::make_shared<const int>(*left + *right)));
    });
    REQUIRE(joined);

    auto submitted = scheduler.submit(std::move(graph));
    REQUIRE(submitted);
    REQUIRE(submitted.graph.wait());
    const auto result = submitted.graph.result(*joined.id);
    REQUIRE(result.has_value());
    REQUIRE(result->is_successful());
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
        failed_graph.add_task({}, [](std::span<const task_result_t>, std::stop_token) {
            return task_result_t::failure(task_error_code_t::task_failed, "decode failed");
        });
    REQUIRE(root);
    const task_id_t root_dependency[] = {*root.id};
    const auto child = failed_graph.add_task(root_dependency,
                                             [&](std::span<const task_result_t>, std::stop_token) {
                                                 ++dependent_runs;
                                                 return task_result_t::success();
                                             });
    REQUIRE(child);
    const task_id_t child_dependency[] = {*child.id};
    const auto grandchild = failed_graph.add_task(
        child_dependency, [&](std::span<const task_result_t>, std::stop_token) {
            ++dependent_runs;
            return task_result_t::success();
        });
    REQUIRE(grandchild);

    auto failed = scheduler.submit(std::move(failed_graph));
    REQUIRE(failed);
    REQUIRE(failed.graph.wait());
    CHECK(failed.graph.result(*root.id)->error->code == task_error_code_t::task_failed);
    CHECK(failed.graph.result(*child.id)->error->code == task_error_code_t::dependency_failed);
    CHECK(failed.graph.result(*grandchild.id)->error->code == task_error_code_t::dependency_failed);
    CHECK(dependent_runs == 0);

    task_graph_t cancellable_graph(2);
    std::atomic_bool started{};
    const auto running =
        cancellable_graph.add_task({}, [&](std::span<const task_result_t>, std::stop_token stop) {
            started.store(true, std::memory_order_release);
            while (!stop.stop_requested()) {
                std::this_thread::yield();
            }
            return task_result_t::success();
        });
    REQUIRE(running);
    const task_id_t running_dependency[] = {*running.id};
    const auto waiting = cancellable_graph.add_task(
        running_dependency,
        [](std::span<const task_result_t>, std::stop_token) { return task_result_t::success(); });
    REQUIRE(waiting);
    auto cancelled = scheduler.submit(std::move(cancellable_graph));
    REQUIRE(cancelled);
    while (!started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    cancelled.graph.cancel();
    REQUIRE(cancelled.graph.wait());
    CHECK(cancelled.graph.result(*running.id)->error->code == task_error_code_t::cancelled);
    CHECK(cancelled.graph.result(*waiting.id)->error->code == task_error_code_t::cancelled);
}

TEST_CASE("task scheduler rejects graphs beyond its outstanding capacity",
          "[unit][core][task-graph]") {
    task_scheduler_t scheduler(2, 1);
    task_graph_t oversized(2);
    REQUIRE(oversized.add_task({}, [](std::span<const task_result_t>, std::stop_token) {
        return task_result_t::success();
    }));
    REQUIRE(oversized.add_task({}, [](std::span<const task_result_t>, std::stop_token) {
        return task_result_t::success();
    }));

    const auto rejected = scheduler.submit(std::move(oversized));
    CHECK_FALSE(rejected);
    REQUIRE(rejected.error.has_value());
    CHECK(rejected.error->code == task_error_code_t::capacity_exceeded);
    CHECK(scheduler.outstanding_tasks() == 0);

    task_scheduler_t one_slot_scheduler(1, 1);
    task_graph_t active_graph(1);
    std::atomic_bool started{};
    const auto active_task =
        active_graph.add_task({}, [&](std::span<const task_result_t>, std::stop_token stop) {
            started.store(true, std::memory_order_release);
            while (!stop.stop_requested()) {
                std::this_thread::yield();
            }
            return task_result_t::success();
        });
    REQUIRE(active_task);
    auto active = one_slot_scheduler.submit(std::move(active_graph));
    REQUIRE(active);
    while (!started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    task_graph_t second_graph(1);
    REQUIRE(second_graph.add_task({}, [](std::span<const task_result_t>, std::stop_token) {
        return task_result_t::success();
    }));
    const auto second = one_slot_scheduler.submit(std::move(second_graph));
    CHECK_FALSE(second);
    REQUIRE(second.error.has_value());
    CHECK(second.error->code == task_error_code_t::capacity_exceeded);
    active.graph.cancel();
    REQUIRE(active.graph.wait());
}

TEST_CASE("task callback exceptions become typed task errors", "[unit][core][task-graph]") {
    task_scheduler_t scheduler(1, 2);
    task_graph_t graph(1);
    const auto task =
        graph.add_task({}, [](std::span<const task_result_t>, std::stop_token) -> task_result_t {
            throw std::runtime_error("unexpected decode error");
        });
    REQUIRE(task);
    const auto submitted = scheduler.submit(std::move(graph));
    REQUIRE(submitted);
    REQUIRE(submitted.graph.wait());
    const auto result = submitted.graph.result(*task.id);
    REQUIRE(result.has_value());
    REQUIRE(result->error.has_value());
    CHECK(result->error->code == task_error_code_t::task_threw);
    CHECK(result->error->message == "unexpected decode error");
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
        graph.add_task({}, [&](std::span<const task_result_t>, std::stop_token stop) {
            started.store(true, std::memory_order_release);
            while (!stop.stop_requested()) {
                std::this_thread::yield();
            }
            return task_result_t::success();
        });
    REQUIRE(active);
    const task_id_t active_dependency[] = {*active.id};
    const auto blocked =
        graph.add_task(active_dependency, [](std::span<const task_result_t>, std::stop_token) {
            return task_result_t::success();
        });
    REQUIRE(blocked);
    auto submitted = scheduler.submit(std::move(graph));
    REQUIRE(submitted);
    while (!started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    scheduler.shutdown();
    REQUIRE(submitted.graph.wait());
    CHECK(submitted.graph.result(*active.id)->error->code == task_error_code_t::scheduler_stopped);
    CHECK(submitted.graph.result(*blocked.id)->error->code == task_error_code_t::scheduler_stopped);
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
    const auto added = graph.add_task({}, [](std::span<const task_result_t>, std::stop_token) {
        return task_result_t::success(task_value_t::from(std::make_shared<const int>(7)));
    });
    REQUIRE(added);
    auto submitted = throwing.submit(std::move(graph));
    REQUIRE(submitted);
    REQUIRE(submitted.graph.wait());
    CHECK(*task_value_cast<int>(*submitted.graph.result(*added.id)) == 7);
}

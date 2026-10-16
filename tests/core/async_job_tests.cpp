#include <wormedit/core/async_job.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <latch>
#include <memory>
#include <semaphore>
#include <stdexcept>
#include <thread>

namespace {
void check_async(bool value) {
    CHECK(value);
}
void require_async(bool value) {
    REQUIRE(value);
}
struct request_t {
    int value{};
    std::latch* started{};
    std::latch* release{};
    bool* cancelled{};
    std::latch* finished{};
};
auto capture(request_t request) {
    return [request] { return request; };
}
using job_t = wormedit::async_job_t<request_t, int, int>;
auto work(const request_t& request, std::stop_token stop) -> job_t::work_outcome_t {
    if (request.started)
        request.started->count_down();
    if (request.release)
        request.release->wait();
    if (request.cancelled)
        *request.cancelled = stop.stop_requested();
    if (request.finished)
        request.finished->count_down();
    return request.value;
}
auto throwing_work(const request_t&, std::stop_token) -> job_t::work_outcome_t {
    throw std::runtime_error("async failure");
}
} // namespace

TEST_CASE("async job returns one completion with captured identity", "[unit][async-job]") {
    wormedit::task_scheduler_t scheduler{1, 2};
    job_t job{scheduler, 2, "invalid", "capacity", "missing"};
    check_async(!(job.poll()));
    check_async(!(job.wait()));
    const auto start = job.request(capture({.value = 42}), 17, work);
    require_async(static_cast<bool>(start));
    const auto result = job.wait();
    require_async(static_cast<bool>(result));
    require_async(static_cast<bool>(*result));
    check_async(result->value().generation == *start);
    check_async(result->value().identity == 17);
    check_async(*result->value().payload == 42);
    check_async(!(job.is_busy()));
    check_async(!(job.poll()));
}

TEST_CASE("async job supersedes only after successful submission", "[unit][async-job]") {
    wormedit::task_scheduler_t scheduler{1, 2};
    std::latch started{1}, release{1};
    bool cancelled = false;
    job_t job{scheduler, 2, "invalid", "capacity", "missing"};
    const auto first = job.request(
        capture({.value = 1, .started = &started, .release = &release, .cancelled = &cancelled}),
        11, work);
    require_async(static_cast<bool>(first));
    started.wait();
    const auto second = job.request(capture({.value = 2}), 22, work);
    const auto rejected = job.request(capture({.value = 3}), 33, work);
    release.count_down();
    const auto completion = job.wait();
    require_async(static_cast<bool>(second));
    require_async(!(rejected));
    check_async(rejected.error().code == wormedit::failure_code_t::capacity_exceeded);
    require_async(static_cast<bool>(completion));
    require_async(static_cast<bool>(*completion));
    check_async(completion->value().generation == *second);
    check_async(completion->value().identity == 22);
    check_async(*completion->value().payload == 2);
    check_async(cancelled);
    check_async(!(job.poll()));
}

TEST_CASE("async job scheduler rejection preserves current work", "[unit][async-job]") {
    wormedit::task_scheduler_t scheduler{1, 1};
    std::latch started{1}, release{1};
    bool cancelled = true;
    job_t job{scheduler, 2, "invalid", "capacity", "missing"};
    const auto first = job.request(
        capture({.value = 1, .started = &started, .release = &release, .cancelled = &cancelled}),
        11, work);
    require_async(static_cast<bool>(first));
    started.wait();
    const auto rejected = job.request(capture({.value = 2}), 22, work);
    release.count_down();
    const auto completion = job.wait();
    require_async(!(rejected));
    check_async(rejected.error().message == "scheduler outstanding-task capacity exceeded");
    require_async(static_cast<bool>(completion));
    require_async(static_cast<bool>(*completion));
    check_async(completion->value().generation == *first);
    check_async(!(cancelled));
}

TEST_CASE("async job cancel suppresses terminal publication", "[unit][async-job]") {
    wormedit::task_scheduler_t scheduler{1, 1};
    std::latch started{1}, release{1};
    bool cancelled = false;
    job_t job{scheduler, 1, "invalid", "capacity", "missing"};
    require_async(static_cast<bool>(job.request(
        capture({.value = 1, .started = &started, .release = &release, .cancelled = &cancelled}),
        11, work)));
    started.wait();
    job.cancel();
    check_async(!(job.is_busy()));
    check_async(!(job.wait()));
    const auto rejected = job.request(capture({.value = 2}), 22, work);
    release.count_down();
    scheduler.shutdown();
    check_async(!(rejected));
    check_async(cancelled);
    check_async(!(job.poll()));
}

TEST_CASE("async job exception is a typed completion failure", "[unit][async-job]") {
    wormedit::task_scheduler_t scheduler{1, 1};
    job_t job{scheduler, 1, "invalid", "capacity", "missing"};
    require_async(static_cast<bool>(job.request(capture({}), 19, throwing_work)));
    const auto result = job.wait();
    require_async(static_cast<bool>(result));
    require_async(!(*result));
    check_async(result->error().identity == 19);
    check_async(result->error().cause.code == wormedit::failure_code_t::task_threw);
    check_async(!(job.is_busy()));
}

TEST_CASE("async job zero bound disables submissions", "[unit][async-job]") {
    wormedit::task_scheduler_t scheduler{1, 1};
    job_t job{scheduler, 0, "invalid", "capacity", "missing"};
    int captures = 0;
    const auto rejected = job.request(
        [&captures] {
            ++captures;
            return request_t{};
        },
        1, work);
    check_async(captures == 0);
    require_async(!(rejected));
    check_async(rejected.error().code == wormedit::failure_code_t::invalid_request);
    check_async(!(job.is_busy()));
}

TEST_CASE("async startup retains the scheduler shutdown failure", "[unit][async-job]") {
    wormedit::task_scheduler_t scheduler{1, 1};
    scheduler.shutdown();
    job_t job{scheduler, 1, "invalid", "capacity", "missing"};
    const auto rejected = job.request(capture({.value = 1}), 17, work);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == wormedit::failure_code_t::scheduler_stopped);
    CHECK(rejected.error().message == "scheduler is shutting down");
    CHECK_FALSE(job.is_busy());
    CHECK_FALSE(job.poll());
}

TEST_CASE("async job can retain cancellation feedback for advisory owners", "[unit][async-job]") {
    using namespace wormedit;
    task_scheduler_t scheduler{1, 1};
    std::latch started{1}, release{1};
    bool cancelled = false;
    job_t job{scheduler, 1, "invalid", "capacity", "missing"};
    require_async(static_cast<bool>(job.request(
        capture({.value = 1, .started = &started, .release = &release, .cancelled = &cancelled}),
        11, work)));
    started.wait();
    job.cancel(async_job_cancel_policy_t::retain_completion);
    check_async(job.is_busy());
    release.count_down();
    const auto completion = job.wait();
    require_async(completion.has_value());
    check_async(cancelled);
    // The scheduler owns the final status if cancellation races the callback return.
    check_async(completion->has_value() ? completion->value().identity == 11
                                        : completion->error().identity == 11);
    check_async(!job.is_busy());
}

TEST_CASE("async job may release owned-input work without waiting or cancelling",
          "[unit][async-job]") {
    using namespace wormedit;
    task_scheduler_t scheduler{1, 1};
    std::latch started{1}, release{1}, finished{1};
    bool cancelled = true;
    auto job = std::make_unique<job_t>(scheduler, 1, "invalid", "capacity", "missing",
                                       async_job_destruction_policy_t::release);
    require_async(static_cast<bool>(job->request(capture({.value = 1,
                                                          .started = &started,
                                                          .release = &release,
                                                          .cancelled = &cancelled,
                                                          .finished = &finished}),
                                                 11, work)));
    started.wait();
    std::binary_semaphore destroyed{0};
    std::jthread destroyer([&job, &destroyed] {
        job.reset();
        destroyed.release();
    });
    const auto released_without_waiting = destroyed.try_acquire_for(std::chrono::seconds{1});
    release.count_down();
    destroyer.join();
    finished.wait();
    scheduler.shutdown();
    check_async(released_without_waiting);
    check_async(!cancelled);
}

TEST_CASE("async job moves current work and generations for single and multiple flight bounds",
          "[unit][async-job]") {
    for (const auto maximum : std::array<std::size_t, 2>{1, 2}) {
        wormedit::task_scheduler_t scheduler{1, 2};
        job_t source{scheduler, maximum, "invalid", "capacity", "missing"};
        const auto first = source.request(capture({.value = 42}), 17, work);
        require_async(static_cast<bool>(first));
        job_t moved{std::move(source)};
        check_async(!source.is_busy());
        check_async(!source.wait());
        const auto completed = moved.wait();
        require_async(completed.has_value());
        require_async(completed->has_value());
        check_async(completed->value().generation == *first);
        check_async(completed->value().identity == 17);
        check_async(*completed->value().payload == 42);
        const auto second = moved.request(capture({.value = 43}), 18, work);
        require_async(static_cast<bool>(second));
        check_async(*second == *first + 1);
        job_t assigned{scheduler, maximum, "invalid", "capacity", "missing"};
        assigned = std::move(moved);
        check_async(!moved.is_busy());
        const auto next = assigned.wait();
        require_async(next.has_value());
        require_async(next->has_value());
        check_async(next->value().generation == *second);
        check_async(next->value().identity == 18);
        check_async(*next->value().payload == 43);
        check_async(!assigned.poll());
    }
}

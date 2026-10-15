#include <wormedit/core/async_job.h>

#include <catch2/catch_test_macros.hpp>

#include <latch>
#include <stdexcept>

namespace {
void check_async(bool value) { CHECK(value); }
void require_async(bool value) { REQUIRE(value); }
struct request_t {
    int value{};
    std::latch* started{};
    std::latch* release{};
    bool* cancelled{};
};
using job_t = wormedit::async_job_t<request_t, int, int>;
auto work(const request_t& request, std::stop_token stop) -> job_t::work_outcome_t {
    if (request.started) request.started->count_down();
    if (request.release) request.release->wait();
    if (request.cancelled) *request.cancelled = stop.stop_requested();
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
    const auto start = job.request({42}, 17, work);
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
    job_t job{scheduler, 2, "invalid", "capacity", "missing"};
    std::latch started{1}, release{1};
    bool cancelled = false;
    const auto first = job.request({1, &started, &release, &cancelled}, 11, work);
    require_async(static_cast<bool>(first));
    started.wait();
    const auto second = job.request({2}, 22, work);
    const auto rejected = job.request({3}, 33, work);
    release.count_down();
    const auto completion = job.wait();
    require_async(static_cast<bool>(second));
    require_async(!(rejected));
    check_async(rejected.error().code == wormedit::async_job_start_status_t::capacity_exceeded);
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
    job_t job{scheduler, 2, "invalid", "capacity", "missing"};
    std::latch started{1}, release{1};
    bool cancelled = true;
    const auto first = job.request({1, &started, &release, &cancelled}, 11, work);
    require_async(static_cast<bool>(first));
    started.wait();
    const auto rejected = job.request({2}, 22, work);
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
    job_t job{scheduler, 1, "invalid", "capacity", "missing"};
    std::latch started{1}, release{1};
    bool cancelled = false;
    require_async(static_cast<bool>(job.request({1, &started, &release, &cancelled}, 11, work)));
    started.wait();
    job.cancel();
    check_async(!(job.is_busy()));
    check_async(!(job.wait()));
    const auto rejected = job.request({2}, 22, work);
    release.count_down();
    scheduler.shutdown();
    check_async(!(rejected));
    check_async(cancelled);
    check_async(!(job.poll()));
}

TEST_CASE("async job exception is a typed completion failure", "[unit][async-job]") {
    wormedit::task_scheduler_t scheduler{1, 1};
    job_t job{scheduler, 1, "invalid", "capacity", "missing"};
    require_async(static_cast<bool>(job.request({}, 19, throwing_work)));
    const auto result = job.wait();
    require_async(static_cast<bool>(result));
    require_async(!(*result));
    check_async(result->error().identity == 19);
    check_async(result->error().cause.code == wormedit::task_error_code_t::task_threw);
    check_async(!(job.is_busy()));
}

TEST_CASE("async job zero bound disables submissions", "[unit][async-job]") {
    wormedit::task_scheduler_t scheduler{1, 1};
    job_t job{scheduler, 0, "invalid", "capacity", "missing"};
    const auto rejected = job.request({}, 1, work);
    require_async(!(rejected));
    check_async(rejected.error().code == wormedit::async_job_start_status_t::invalid_request);
    check_async(!(job.is_busy()));
}

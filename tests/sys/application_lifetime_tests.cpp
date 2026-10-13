#include "sys/application_lifetime.h"

#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL_init.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <latch>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace {
void check_lifetime(bool value) { CHECK(value); }
void require_lifetime(bool value) { REQUIRE(value); }

struct shutdown_observer_t {
    std::latch started{7};
    std::mutex mutex;
    std::condition_variable_any cancelled;
    std::atomic<unsigned> observed_live_platform{};
};

auto is_work_released() -> bool { return false; }

auto observe_worker_shutdown(shutdown_observer_t& observer, std::stop_token stop)
    -> wormedit::task_result_t {
    observer.started.count_down();
    std::unique_lock lock{observer.mutex};
    observer.cancelled.wait(lock, stop, is_work_released);
    if (SDL_WasInit(SDL_INIT_EVENTS) != 0)
        observer.observed_live_platform.fetch_add(1, std::memory_order_relaxed);
    return wormedit::task_result_t::success();
}

void start_observed_worker(wormedit::task_scheduler_t& scheduler,
                           shutdown_observer_t& observer) {
    wormedit::task_graph_t graph{1};
    const auto added = graph.add_task({}, [&observer](auto, std::stop_token stop) {
        return observe_worker_shutdown(observer, stop);
    });
    require_lifetime(static_cast<bool>(added));
    const auto submitted = scheduler.submit(std::move(graph));
    require_lifetime(static_cast<bool>(submitted));
}

void unwind_initialized_platform(const std::filesystem::path& marker) {
    wormedit::sys::application_platform_t platform;
    require_lifetime(wormedit::sys::initialize_sdl_runtime(platform.runtime));
    require_lifetime(SDL_InitSubSystem(SDL_INIT_EVENTS));
    platform.crash_marker = marker;
    require_lifetime(wormedit::write_crash_marker(marker, wormedit::song_id_t{1}).empty());
    throw std::runtime_error{"frame failure"};
}
} // namespace

TEST_CASE("Application lifetime joins every worker before SDL teardown", "[sys][ownership][lifetime]") {
    wormedit::sys::application_platform_t platform;
    require_lifetime(wormedit::sys::initialize_sdl_runtime(platform.runtime));
    require_lifetime(SDL_InitSubSystem(SDL_INIT_EVENTS));
    shutdown_observer_t observer;
    std::array<std::unique_ptr<wormedit::task_scheduler_t>, 7> schedulers;
    std::array<wormedit::task_scheduler_t*, 7> scheduler_refs{};
    for (std::size_t index = 0; index < schedulers.size(); ++index) {
        schedulers[index] = std::make_unique<wormedit::task_scheduler_t>(1, 1);
        scheduler_refs[index] = schedulers[index].get();
        start_observed_worker(*schedulers[index], observer);
    }
    wormedit::sys::application_worker_set_t workers{platform, scheduler_refs};
    observer.started.wait();
    platform.shutdown();
    check_lifetime(observer.observed_live_platform.load() == schedulers.size());
    check_lifetime(!platform.runtime.sdl_initialized);
    check_lifetime(SDL_WasInit(SDL_INIT_EVENTS) == 0);
    platform.shutdown();
    workers.shutdown();
    check_lifetime(observer.observed_live_platform.load() == schedulers.size());
}

TEST_CASE("Application lifetime releases SDL and crash marker during exception unwinding",
          "[sys][ownership][lifetime]") {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto marker = std::filesystem::temp_directory_path() /
        ("wormedit-lifetime-" + std::to_string(nonce) + ".marker");
    CHECK_THROWS_AS(unwind_initialized_platform(marker), std::runtime_error);
    check_lifetime(SDL_WasInit(SDL_INIT_EVENTS) == 0);
    check_lifetime(!std::filesystem::exists(marker));
    std::error_code ignored;
    std::filesystem::remove(marker, ignored);
}

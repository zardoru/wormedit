#pragma once

#include "sys/application_startup.h"
#include <wormedit/core/task_graph.h>

#include <filesystem>
#include <span>

namespace wormedit::sys {
class application_worker_set_t;

// Declared before workers: platform teardown and marker removal happen last.
class application_platform_t {
public:
    application_platform_t() = default;
    ~application_platform_t();
    application_platform_t(const application_platform_t&) = delete;
    auto operator=(const application_platform_t&) -> application_platform_t& = delete;
    application_platform_t(application_platform_t&&) = delete;
    auto operator=(application_platform_t&&) -> application_platform_t& = delete;

    application_runtime_t runtime;
    std::filesystem::path crash_marker;
    void shutdown() noexcept;

private:
    friend class application_worker_set_t;
    application_worker_set_t* workers_{};
    bool shutdown_started_{};
};

// An inventory of application-owned schedulers, borrowed without allocation.
// Declare after the workers; destruction joins them before their dependencies.
class application_worker_set_t {
public:
    application_worker_set_t(application_platform_t& platform,
                             std::span<task_scheduler_t* const> schedulers) noexcept;
    ~application_worker_set_t();
    application_worker_set_t(const application_worker_set_t&) = delete;
    auto operator=(const application_worker_set_t&) -> application_worker_set_t& = delete;
    application_worker_set_t(application_worker_set_t&&) = delete;
    auto operator=(application_worker_set_t&&) -> application_worker_set_t& = delete;
    void shutdown() noexcept;

private:
    application_platform_t& platform_;
    std::span<task_scheduler_t* const> schedulers_;
    bool shutdown_started_{};
};
} // namespace wormedit::sys

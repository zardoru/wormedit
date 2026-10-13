#include "sys/application_lifetime.h"

#include <wormedit/formats/native_project_storage.h>

namespace wormedit::sys {
application_platform_t::~application_platform_t() { shutdown(); }

void application_platform_t::shutdown() noexcept {
    if (shutdown_started_) return;
    shutdown_started_ = true;
    if (workers_) workers_->shutdown();
    shutdown_application_runtime(runtime);
    if (!crash_marker.empty()) remove_crash_marker(crash_marker);
}

application_worker_set_t::application_worker_set_t(application_platform_t& platform,
    std::span<task_scheduler_t* const> schedulers) noexcept
    : platform_(platform), schedulers_(schedulers) {
    platform_.workers_ = this;
}

application_worker_set_t::~application_worker_set_t() {
    shutdown();
    platform_.workers_ = nullptr;
}

void application_worker_set_t::shutdown() noexcept {
    if (shutdown_started_) return;
    shutdown_started_ = true;
    for (auto* scheduler : schedulers_) scheduler->shutdown();
}
} // namespace wormedit::sys

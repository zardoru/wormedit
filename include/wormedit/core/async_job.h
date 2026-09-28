#pragma once

#include <cstdint>
#include <string>

namespace wormedit {

enum class async_job_start_status_t {
    started,
    invalid_request,
    capacity_exceeded,
    scheduler_rejected,
};

struct async_job_start_result_t {
    async_job_start_status_t status{async_job_start_status_t::invalid_request};
    std::uint64_t generation{};
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == async_job_start_status_t::started;
    }
};

} // namespace wormedit

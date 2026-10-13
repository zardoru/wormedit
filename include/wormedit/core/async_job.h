#pragma once

#include <wormedit/core/result.h>

#include <cstdint>
#include <string>
#include <utility>

namespace wormedit {

enum class async_job_start_status_t {
    started,
    invalid_request,
    capacity_exceeded,
    scheduler_rejected,
};

using async_job_start_outcome_t = result_t<std::uint64_t, error_t<async_job_start_status_t>>;

// Compatibility adapter for sys callers of the three request methods.
struct async_job_start_result_t {
    [[nodiscard]] static auto from(async_job_start_outcome_t&& outcome) -> async_job_start_result_t {
        if (!outcome) return {.status = outcome.error().code, .generation = 0,
                              .message = std::move(outcome.error().message)};
        return {.status = async_job_start_status_t::started, .generation = *outcome, .message = {}};
    }

    async_job_start_status_t status{async_job_start_status_t::invalid_request};
    std::uint64_t generation{};
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == async_job_start_status_t::started;
    }
};

} // namespace wormedit

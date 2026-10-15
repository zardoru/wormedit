#pragma once

#include <wormedit/core/result.h>
#include <wormedit/core/id.h>
#include <wormedit/core/task_graph.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace wormedit {

struct async_job_project_identity_t {
    song_id_t project;
    std::uint64_t document_revision{};
    auto operator==(const async_job_project_identity_t&) const -> bool = default;
};

template <class Identity> struct async_job_completion_error_t {
    std::uint64_t generation{};
    Identity identity;
    error_t<task_error_code_t> cause;
};

template <class Identity, class Payload> struct async_job_completion_value_t;
template <class Identity, class Payload>
using async_job_completion_outcome_t =
    result_t<async_job_completion_value_t<Identity, Payload>, async_job_completion_error_t<Identity>>;

template <class Identity, class Payload> struct async_job_completion_value_t {
    [[nodiscard]] static auto from(std::uint64_t generation, Identity identity,
                                   task_outcome_t&& task, std::string_view missing_payload_message)
        -> async_job_completion_outcome_t<Identity, Payload> {
        if (auto payload = task_value_cast<Payload>(task)) {
            return async_job_completion_value_t{.generation = generation,
                                                 .identity = std::move(identity),
                                                 .payload = std::move(payload)};
        }
        if (!task) {
            return std::unexpected(async_job_completion_error_t<Identity>{
                .generation = generation, .identity = std::move(identity),
                .cause = std::move(task.error().cause)});
        }
        return std::unexpected(async_job_completion_error_t<Identity>{
            .generation = generation, .identity = std::move(identity),
            .cause = {task_error_code_t::task_failed, std::string(missing_payload_message)}});
    }

    std::uint64_t generation{};
    Identity identity;
    std::shared_ptr<const Payload> payload;
};

enum class async_job_start_status_t {
    started,
    invalid_request,
    capacity_exceeded,
    scheduler_rejected,
};

using async_job_start_outcome_t = result_t<std::uint64_t, error_t<async_job_start_status_t>>;

} // namespace wormedit

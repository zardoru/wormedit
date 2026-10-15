#pragma once

#include <wormedit/core/result.h>
#include <wormedit/core/id.h>
#include <wormedit/core/task_graph.h>

#include <algorithm>
#include <optional>
#include <ranges>
#include <span>
#include <vector>
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

// UI-thread owner of bounded, latest-only work. Referenced resources and the
// scheduler must outlive the job. Work runs on the scheduler; publication stays
// with the caller. Request owns its worker inputs (or references stable owners).
template <class Request, class Payload, class Identity> class async_job_t {
  public:
    using work_outcome_t = result_t<Payload, error_t<task_error_code_t>>;
    using work_function_t = work_outcome_t (*)(const Request&, std::stop_token);
    using completion_t = async_job_completion_outcome_t<Identity, Payload>;

    async_job_t(task_scheduler_t& scheduler, std::size_t maximum_in_flight,
                std::string_view invalid_message, std::string_view capacity_message,
                std::string_view missing_payload_message)
        : scheduler_(scheduler), maximum_in_flight_(maximum_in_flight),
          invalid_message_(invalid_message), capacity_message_(capacity_message),
          missing_payload_message_(missing_payload_message) {
        workers_.reserve(maximum_in_flight_);
    }
    ~async_job_t() {
        cancel();
        for (const auto& worker : workers_) static_cast<void>(worker.graph.wait());
    }
    async_job_t(const async_job_t&) = delete;
    auto operator=(const async_job_t&) -> async_job_t& = delete;

    [[nodiscard]] auto request(Request request, Identity identity, work_function_t work)
        -> async_job_start_outcome_t {
        if (maximum_in_flight_ == 0)
            return std::unexpected(error_t<async_job_start_status_t>{
                async_job_start_status_t::invalid_request, std::string(invalid_message_)});
        std::erase_if(workers_, std::mem_fn(&worker_t::is_terminal));
        if (workers_.size() >= maximum_in_flight_)
            return std::unexpected(error_t<async_job_start_status_t>{
                async_job_start_status_t::capacity_exceeded, std::string(capacity_message_)});
        task_graph_t graph{1};
        const auto task = graph.create_task({},
            [request = std::move(request), work](std::span<const task_result_t>, std::stop_token stop) {
                auto result = work(request, stop);
                if (!result) return task_result_t::failure(result.error().code,
                                                          std::move(result.error().message));
                return task_result_t::success(task_value_t::from(
                    std::make_shared<const Payload>(std::move(*result))));
            });
        if (!task)
            return std::unexpected(error_t<async_job_start_status_t>{
                async_job_start_status_t::scheduler_rejected, task.error().message});
        auto submitted = scheduler_.begin(std::move(graph));
        if (!submitted)
            return std::unexpected(error_t<async_job_start_status_t>{
                submitted.error().code == task_error_code_t::capacity_exceeded
                    ? async_job_start_status_t::capacity_exceeded
                    : async_job_start_status_t::scheduler_rejected,
                submitted.error().message});
        // Failed submissions must leave the previous current request intact.
        cancel();
        if (++last_generation_ == 0) ++last_generation_;
        current_generation_ = last_generation_;
        workers_.push_back({last_generation_, std::move(identity), std::move(*submitted), *task});
        return last_generation_;
    }

    [[nodiscard]] auto poll() -> std::optional<completion_t> {
        std::optional<completion_t> completion;
        for (auto worker = workers_.begin(); worker != workers_.end();) {
            auto result = worker->graph.outcome(worker->task);
            if (!result) {
                ++worker;
                continue;
            }
            if (worker->generation == current_generation_) {
                completion = async_job_completion_value_t<Identity, Payload>::from(
                    worker->generation, std::move(worker->identity), std::move(*result),
                    missing_payload_message_);
                current_generation_ = 0;
            }
            worker = workers_.erase(worker);
        }
        return completion;
    }
    // For tests and orderly shutdown, never the UI frame loop.
    [[nodiscard]] auto wait() -> std::optional<completion_t> {
        const auto worker = std::ranges::find(workers_, current_generation_, &worker_t::generation);
        if (worker == workers_.end() || !worker->graph.wait()) return std::nullopt;
        return poll();
    }
    void cancel() noexcept {
        current_generation_ = 0;
        for (const auto& worker : workers_) worker.graph.cancel();
    }
    [[nodiscard]] auto is_busy() const noexcept -> bool { return current_generation_ != 0; }

  private:
    struct worker_t {
        std::uint64_t generation{};
        Identity identity;
        task_graph_handle_t graph;
        task_id_t task;
        [[nodiscard]] auto is_terminal() const -> bool { return graph.outcome(task).has_value(); }
    };
    task_scheduler_t& scheduler_;
    std::size_t maximum_in_flight_{};
    std::string_view invalid_message_;
    std::string_view capacity_message_;
    std::string_view missing_payload_message_;
    std::uint64_t last_generation_{};
    std::uint64_t current_generation_{};
    std::vector<worker_t> workers_;
};

} // namespace wormedit

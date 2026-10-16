#pragma once

#include <wormedit/core/result.h>
#include <wormedit/core/id.h>
#include <wormedit/core/task_graph.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wormedit {

struct async_job_project_identity_t {
    song_id_t project;
    std::uint64_t document_revision{};
    auto operator==(const async_job_project_identity_t&) const -> bool = default;
};

template <class Identity> struct async_job_completion_error_t {
    std::uint64_t generation{};
    Identity identity;
    error_t<failure_code_t> cause;
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
            .cause = {failure_code_t::task_failed, std::string(missing_payload_message)}});
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

// Owners whose workers own all inputs can retain their existing nonblocking
// handle-release teardown. Workers borrowing owner state use cancel_and_wait.
enum class async_job_destruction_policy_t { cancel_and_wait, release };
enum class async_job_cancel_policy_t { discard_completion, retain_completion };

// UI-thread owner of bounded, latest-only work. Referenced resources and the
// scheduler must outlive the job. Work runs on the scheduler; publication stays
// with the caller. Request owns its worker inputs (or references stable owners).
template <class Request, class Payload, class Identity> class async_job_t {
  public:
    using work_outcome_t = result_t<Payload, error_t<failure_code_t>>;
    using work_function_t = work_outcome_t (*)(const Request&, std::stop_token);
    using completion_t = async_job_completion_outcome_t<Identity, Payload>;

    // Diagnostic views must outlive the job; domain owners pass literals.
    async_job_t(task_scheduler_t& scheduler, std::size_t maximum_in_flight,
                std::string_view invalid_message, std::string_view capacity_message,
                std::string_view missing_payload_message,
                async_job_destruction_policy_t destruction =
                    async_job_destruction_policy_t::cancel_and_wait)
        : scheduler_(&scheduler), maximum_in_flight_(maximum_in_flight),
          invalid_message_(invalid_message), capacity_message_(capacity_message),
          missing_payload_message_(missing_payload_message), destruction_(destruction) {
        if (maximum_in_flight_ > 1)
            workers_.reserve(maximum_in_flight_);
    }
    ~async_job_t() {
        if (destruction_ == async_job_destruction_policy_t::cancel_and_wait)
            shutdown();
    }
    // Transfer the work without moving its captured inputs or reallocating storage.
    // Borrowed resources must still outlive the receiving job.
    async_job_t(async_job_t&& other) {
        swap(other);
    }
    auto operator=(async_job_t&& other) -> async_job_t& {
        if (this != &other) {
            async_job_t replacement{std::move(other)};
            swap(replacement);
        }
        return *this;
    }
    void shutdown() {
        cancel();
        for (const auto& worker : active_workers())
            static_cast<void>(worker.graph.wait());
        single_worker_.reset();
        workers_.clear();
    }
    async_job_t(const async_job_t&) = delete;
    auto operator=(const async_job_t&) -> async_job_t& = delete;

    // Capture runs once, only after admission; rejected image batches allocate no source copy.
    template <class Capture>
    [[nodiscard]] auto request(Capture&& capture, Identity identity, work_function_t work)
        -> async_job_start_outcome_t {
        if (maximum_in_flight_ == 0)
            return std::unexpected(error_t<async_job_start_status_t>{
                async_job_start_status_t::invalid_request, std::string(invalid_message_)});
        discard_terminal_workers();
        if (active_workers().size() >= maximum_in_flight_)
            return std::unexpected(error_t<async_job_start_status_t>{
                async_job_start_status_t::capacity_exceeded, std::string(capacity_message_)});
        task_graph_t graph{1};
        const auto task =
            graph.create_task({}, [request = std::invoke(std::forward<Capture>(capture)),
                                   work](std::span<const task_outcome_t>, std::stop_token stop)
                -> task_outcome_t {
                auto result = work(request, stop);
                if (!result)
                    return std::unexpected(task_failure_t::from(
                        result.error().code, std::move(result.error().message)));
                return task_value_t::from(std::make_shared<const Payload>(std::move(*result)));
            });
        if (!task)
            return std::unexpected(error_t<async_job_start_status_t>{
                async_job_start_status_t::scheduler_rejected, task.error().message});
        auto submitted = scheduler_->begin(std::move(graph));
        if (!submitted)
            return std::unexpected(error_t<async_job_start_status_t>{
                submitted.error().code == failure_code_t::capacity_exceeded
                    ? async_job_start_status_t::capacity_exceeded
                    : async_job_start_status_t::scheduler_rejected,
                submitted.error().message});
        // Failed submissions must leave the previous current request intact.
        cancel();
        if (++last_generation_ == 0)
            ++last_generation_;
        current_generation_ = last_generation_;
        append_worker({.generation = last_generation_,
                       .identity = std::move(identity),
                       .graph = std::move(*submitted),
                       .task = *task});
        return last_generation_;
    }

    [[nodiscard]] auto poll() -> std::optional<completion_t> {
        std::optional<completion_t> completion;
        auto workers = active_workers();
        for (std::size_t index = 0; index < workers.size();) {
            auto& worker = workers[index];
            auto result = worker.graph.outcome(worker.task);
            if (!result) {
                ++index;
                continue;
            }
            if (worker.generation == current_generation_) {
                completion = async_job_completion_value_t<Identity, Payload>::from(
                    worker.generation, std::move(worker.identity), std::move(*result),
                    missing_payload_message_);
                current_generation_ = 0;
            }
            erase_worker(index);
            workers = active_workers();
        }
        return completion;
    }
    // For tests and orderly shutdown, never the UI frame loop.
    [[nodiscard]] auto wait() -> std::optional<completion_t> {
        auto workers = active_workers();
        const auto worker = std::ranges::find(workers, current_generation_, &worker_t::generation);
        if (worker == workers.end() || !worker->graph.wait())
            return std::nullopt;
        return poll();
    }
    void cancel(
        async_job_cancel_policy_t policy = async_job_cancel_policy_t::discard_completion) noexcept {
        if (policy == async_job_cancel_policy_t::discard_completion)
            current_generation_ = 0;
        for (const auto& worker : active_workers())
            worker.graph.cancel();
    }
    [[nodiscard]] auto is_busy() const noexcept -> bool {
        return current_generation_ != 0;
    }
    [[nodiscard]] auto is_bound_to(const task_scheduler_t& scheduler) const noexcept -> bool {
        return scheduler_ == &scheduler;
    }

  private:
    struct worker_t {
        std::uint64_t generation{};
        Identity identity;
        task_graph_handle_t graph;
        task_id_t task;
        [[nodiscard]] auto is_terminal() const -> bool {
            return graph.outcome(task).has_value();
        }
    };
    // Single-flight UI owners need no heap allocation for job bookkeeping.
    [[nodiscard]] auto active_workers() noexcept -> std::span<worker_t> {
        if (maximum_in_flight_ == 1)
            return single_worker_ ? std::span{&*single_worker_, std::size_t{1}}
                                  : std::span<worker_t>{};
        return std::span{workers_};
    }
    void discard_terminal_workers() {
        if (maximum_in_flight_ == 1) {
            if (single_worker_ && single_worker_->is_terminal())
                single_worker_.reset();
            return;
        }
        std::erase_if(workers_, std::mem_fn(&worker_t::is_terminal));
    }
    void append_worker(worker_t&& worker) {
        if (maximum_in_flight_ == 1)
            single_worker_.emplace(std::move(worker));
        else
            workers_.push_back(std::move(worker));
    }
    void erase_worker(std::size_t index) {
        if (maximum_in_flight_ == 1)
            single_worker_.reset();
        else
            workers_.erase(workers_.begin() + static_cast<std::ptrdiff_t>(index));
    }
    void swap(async_job_t& other) {
        std::swap(scheduler_, other.scheduler_);
        std::swap(maximum_in_flight_, other.maximum_in_flight_);
        std::swap(invalid_message_, other.invalid_message_);
        std::swap(capacity_message_, other.capacity_message_);
        std::swap(missing_payload_message_, other.missing_payload_message_);
        std::swap(destruction_, other.destruction_);
        std::swap(last_generation_, other.last_generation_);
        std::swap(current_generation_, other.current_generation_);
        std::swap(single_worker_, other.single_worker_);
        std::swap(workers_, other.workers_);
    }
    task_scheduler_t* scheduler_{};
    std::size_t maximum_in_flight_{};
    std::string_view invalid_message_;
    std::string_view capacity_message_;
    std::string_view missing_payload_message_;
    async_job_destruction_policy_t destruction_{async_job_destruction_policy_t::cancel_and_wait};
    std::uint64_t last_generation_{};
    std::uint64_t current_generation_{};
    std::optional<worker_t> single_worker_;
    std::vector<worker_t> workers_;
};

} // namespace wormedit

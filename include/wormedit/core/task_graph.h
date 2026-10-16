#pragma once

#include <wormedit/core/result.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace wormedit {

struct task_id_t {
    std::uint32_t index{};
    auto operator==(const task_id_t&) const -> bool = default;
};

// Values are immutable, runtime-type-checked shared payloads so fan-in can pass
// large render/export buffers without copying. Keep the payload alive for as
// long as its graph handle.
struct task_value_t {
    template <typename value_t>
    [[nodiscard]] static auto from(std::shared_ptr<const value_t> value) -> task_value_t {
        return {
            .value = std::static_pointer_cast<const void>(std::move(value)),
            .type = &typeid(value_t),
        };
    }

    template <typename value_t> [[nodiscard]] auto as() const -> std::shared_ptr<const value_t> {
        if (type == nullptr || *type != typeid(value_t))
            return {};
        return std::static_pointer_cast<const value_t>(value);
    }

    std::shared_ptr<const void> value;
    const std::type_info* type{&typeid(void)};
    auto operator==(const task_value_t&) const -> bool = default;
};

struct task_failure_t {
    [[nodiscard]] static auto from(failure_code_t code, std::string message,
                                   task_value_t payload = {}) -> task_failure_t {
        return {.cause = {code, std::move(message)}, .payload = std::move(payload)};
    }

    // Failed tasks retain their payload independently of their cause.
    error_t<failure_code_t> cause{failure_code_t::task_failed, {}};
    task_value_t payload;
    auto operator==(const task_failure_t&) const -> bool = default;
};

using task_outcome_t = result_t<task_value_t, task_failure_t>;
using task_add_outcome_t = result_t<task_id_t, error_t<failure_code_t>>;

template <typename value_t>
[[nodiscard]] auto task_value_cast(const task_outcome_t& result) -> std::shared_ptr<const value_t> {
    if (!result)
        return {};
    return result->as<value_t>();
}

using task_function_t =
    std::function<task_outcome_t(std::span<const task_outcome_t>, std::stop_token)>;

class task_graph_handle_t;
class task_scheduler_t;

// A bounded, immutable-on-submit DAG. Dependencies must refer to tasks already
// added, which makes cycles impossible and keeps fan-in bookkeeping compact.
class task_graph_t {
  public:
    explicit task_graph_t(std::size_t maximum_tasks);
    task_graph_t(task_graph_t&&) noexcept;
    auto operator=(task_graph_t&&) noexcept -> task_graph_t&;
    ~task_graph_t();

    task_graph_t(const task_graph_t&) = delete;
    auto operator=(const task_graph_t&) -> task_graph_t& = delete;

    [[nodiscard]] auto create_task(std::span<const task_id_t> dependencies,
                                   task_function_t function) -> task_add_outcome_t;
    [[nodiscard]] auto size() const noexcept -> std::size_t;

  private:
    struct impl_t;
    std::unique_ptr<impl_t> impl_;
    friend class task_scheduler_t;
};

class task_graph_handle_t {
  public:
    task_graph_handle_t() = default;

    // Wait until every task has a terminal result. Returns false for an empty handle.
    [[nodiscard]] auto wait() const -> bool;
    void cancel() const;
    [[nodiscard]] auto outcome(task_id_t task) const -> std::optional<task_outcome_t>;
    [[nodiscard]] auto task_count() const noexcept -> std::size_t;
    [[nodiscard]] explicit operator bool() const noexcept;

  private:
    struct impl_t;
    explicit task_graph_handle_t(std::shared_ptr<impl_t> impl);
    std::shared_ptr<impl_t> impl_;
    friend class task_scheduler_t;
};

using task_submit_outcome_t = result_t<task_graph_handle_t, error_t<failure_code_t>>;

// Fixed jthread pool with a preallocated ring queue. The capacity covers queued,
// running, and dependency-blocked nodes across submitted graphs. It does not bound
// memory allocated by user callbacks or their result payloads. No std::async is used.
struct task_scheduler_options_t {
    // Runs once on each worker thread before it takes work (for example to lower its
    // priority). A throwing hook is ignored; the worker continues at default priority.
    std::function<void()> on_worker_start;
};

// Workers a scheduler may start: the request, capped at hardware_threads - 2 so the
// frame and audio threads keep cores, and never below one. Unknown hardware (0) does
// not cap the request.
[[nodiscard]] auto clamp_worker_count(std::size_t requested, std::size_t hardware_threads) noexcept
    -> std::size_t;

class task_scheduler_t {
  public:
    explicit task_scheduler_t(std::size_t worker_count, std::size_t maximum_outstanding_tasks,
                              task_scheduler_options_t options = {});
    task_scheduler_t(const task_scheduler_t&) = delete;
    auto operator=(const task_scheduler_t&) -> task_scheduler_t& = delete;
    ~task_scheduler_t();

    [[nodiscard]] auto begin(task_graph_t graph) -> task_submit_outcome_t;
    void shutdown();
    [[nodiscard]] auto outstanding_tasks() const noexcept -> std::size_t;
    [[nodiscard]] auto maximum_outstanding_tasks() const noexcept -> std::size_t;
    [[nodiscard]] auto worker_count() const noexcept -> std::size_t;

  private:
    struct impl_t;
    std::unique_ptr<impl_t> impl_;
};

// Use one guard per publication target. begin_generation() invalidates older work;
// publish_if_current() serializes a short publication (typically a pointer swap)
// against generation changes. Do not call begin_generation() from its callback.
class generation_guard_t {
  public:
    [[nodiscard]] auto begin_generation() -> std::uint64_t;
    [[nodiscard]] auto current_generation() const noexcept -> std::uint64_t;
    [[nodiscard]] auto is_current(std::uint64_t generation) const noexcept -> bool;

    template <typename callback_t>
    [[nodiscard]] auto publish_if_current(std::uint64_t generation, callback_t&& publish) -> bool {
        std::lock_guard lock(mutex_);
        if (generation == 0 || generation_.load(std::memory_order_relaxed) != generation) {
            return false;
        }
        std::invoke(std::forward<callback_t>(publish));
        return true;
    }

  private:
    mutable std::mutex mutex_;
    std::atomic<std::uint64_t> generation_{0};
};

} // namespace wormedit

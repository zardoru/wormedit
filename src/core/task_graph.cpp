#include <wormedit/core/task_graph.h>

#include <algorithm>
#include <condition_variable>
#include <exception>
#include <limits>
#include <mutex>
#include <ranges>
#include <thread>
#include <utility>

namespace wormedit {
namespace detail {

enum class node_status_t : std::uint8_t { blocked, queued, running, succeeded, failed, cancelled };

struct work_item_t {
    std::shared_ptr<struct graph_state_t> graph;
    std::uint32_t task{};
};

struct node_state_t {
    task_function_t function;
    std::vector<std::uint32_t> dependencies;
    std::vector<std::uint32_t> dependents;
    std::vector<task_result_t> inputs;
    std::size_t dependencies_left{};
    node_status_t status{node_status_t::blocked};
    task_result_t result;
};

struct graph_state_t {
    std::vector<node_state_t> nodes;
    std::vector<std::uint32_t> propagation;
    std::stop_source stop_source;
    std::size_t remaining{};
};

struct scheduler_state_t {
    explicit scheduler_state_t(std::size_t capacity)
        : queue(capacity), scratch(capacity), stop_scratch(capacity),
          maximum_outstanding(capacity) {
        graphs.reserve(capacity);
    }

    mutable std::mutex mutex;
    std::condition_variable_any changed;
    std::vector<work_item_t> queue;
    std::vector<work_item_t> scratch;
    std::vector<std::shared_ptr<graph_state_t>> stop_scratch;
    std::size_t queue_head{};
    std::size_t queue_tail{};
    std::size_t queue_size{};
    std::size_t maximum_outstanding{};
    std::size_t outstanding{};
    bool stopping{};
    std::vector<std::weak_ptr<graph_state_t>> graphs;
};

} // namespace detail

struct task_graph_t::impl_t {
    struct node_definition_t {
        task_function_t function;
        std::vector<std::uint32_t> dependencies;
    };

    explicit impl_t(std::size_t cap) : maximum_tasks(cap) {
        nodes.reserve(cap);
    }

    std::size_t maximum_tasks{};
    std::vector<node_definition_t> nodes;
};

struct task_graph_handle_t::impl_t {
    std::shared_ptr<detail::scheduler_state_t> scheduler;
    std::shared_ptr<detail::graph_state_t> graph;
};

namespace {

using detail::graph_state_t;
using detail::node_status_t;
using detail::scheduler_state_t;
using detail::work_item_t;

auto is_terminal(node_status_t status) noexcept -> bool {
    return status == node_status_t::succeeded || status == node_status_t::failed ||
           status == node_status_t::cancelled;
}

auto cancelled_result(task_error_code_t code, std::string message) -> task_result_t {
    return task_result_t::failure(code, std::move(message));
}

void enqueue_locked(scheduler_state_t& scheduler, const std::shared_ptr<graph_state_t>& graph,
                    std::uint32_t task) {
    if (scheduler.queue.empty() || scheduler.queue_size >= scheduler.queue.size()) {
        return;
    }
    scheduler.queue[scheduler.queue_tail] = {graph, task};
    scheduler.queue_tail = (scheduler.queue_tail + 1) % scheduler.queue.size();
    ++scheduler.queue_size;
    graph->nodes[task].status = node_status_t::queued;
}

void remove_graph_from_queue_locked(scheduler_state_t& scheduler,
                                    const std::shared_ptr<graph_state_t>& graph) {
    auto kept = std::size_t{};
    for (auto index = std::size_t{}; index < scheduler.queue_size; ++index) {
        const auto& item = scheduler.queue[(scheduler.queue_head + index) % scheduler.queue.size()];
        if (item.graph != graph) {
            scheduler.scratch[kept++] = item;
        }
    }
    scheduler.queue.swap(scheduler.scratch);
    for (auto index = kept; index < scheduler.queue.size(); ++index) {
        scheduler.queue[index] = {};
    }
    for (auto& item : scheduler.scratch) {
        item = {};
    }
    scheduler.queue_head = 0;
    scheduler.queue_size = kept;
    scheduler.queue_tail = kept % scheduler.queue.size();
}

void mark_terminal_locked(scheduler_state_t& scheduler, graph_state_t& graph, std::uint32_t task,
                          task_result_t result) {
    auto& node = graph.nodes[task];
    if (is_terminal(node.status)) {
        return;
    }
    node.result = std::move(result);
    node.status = node.result.is_successful()
                      ? node_status_t::succeeded
                      : (node.result.error->code == task_error_code_t::cancelled ||
                                 node.result.error->code == task_error_code_t::scheduler_stopped ||
                                 node.result.error->code == task_error_code_t::dependency_failed
                             ? node_status_t::cancelled
                             : node_status_t::failed);
    --graph.remaining;
    --scheduler.outstanding;
}

void prune_graphs_locked(scheduler_state_t& scheduler) {
    scheduler.graphs.erase(std::remove_if(scheduler.graphs.begin(), scheduler.graphs.end(),
                                          [](const auto& weak) {
                                              const auto graph = weak.lock();
                                              return !graph || graph->remaining == 0;
                                          }),
                           scheduler.graphs.end());
}

void cancel_graph_locked(scheduler_state_t& scheduler, const std::shared_ptr<graph_state_t>& graph,
                         task_error_code_t code, const char* reason) {
    remove_graph_from_queue_locked(scheduler, graph);
    for (std::uint32_t task = 0; task < graph->nodes.size(); ++task) {
        auto& node = graph->nodes[task];
        if (node.status == node_status_t::blocked || node.status == node_status_t::queued) {
            mark_terminal_locked(scheduler, *graph, task, cancelled_result(code, reason));
        }
    }
    prune_graphs_locked(scheduler);
}

void push_ready_dependents(graph_state_t& graph, std::uint32_t task) {
    for (const auto dependent : graph.nodes[task].dependents) {
        auto& child = graph.nodes[dependent];
        if (!is_terminal(child.status) && child.dependencies_left > 0 &&
            --child.dependencies_left == 0) {
            graph.propagation.push_back(dependent);
        }
    }
}

void complete_locked(scheduler_state_t& scheduler, const std::shared_ptr<graph_state_t>& graph,
                     std::uint32_t task, task_result_t result) {
    if (scheduler.stopping) {
        result =
            cancelled_result(task_error_code_t::scheduler_stopped, "scheduler is shutting down");
    } else if (graph->stop_source.stop_requested()) {
        result = cancelled_result(task_error_code_t::cancelled, "task graph was cancelled");
    }

    mark_terminal_locked(scheduler, *graph, task, std::move(result));
    graph->propagation.clear();
    push_ready_dependents(*graph, task);

    while (!graph->propagation.empty()) {
        const auto ready = graph->propagation.back();
        graph->propagation.pop_back();
        auto& node = graph->nodes[ready];
        if (is_terminal(node.status) || node.status == node_status_t::running) {
            continue;
        }

        if (graph->stop_source.stop_requested() || scheduler.stopping) {
            const auto code = scheduler.stopping ? task_error_code_t::scheduler_stopped
                                                 : task_error_code_t::cancelled;
            const auto* reason =
                scheduler.stopping ? "scheduler is shutting down" : "task graph was cancelled";
            mark_terminal_locked(scheduler, *graph, ready, cancelled_result(code, reason));
            push_ready_dependents(*graph, ready);
        } else if (const auto failed_dependency =
                       std::ranges::find_if(node.dependencies,
                                            [&graph](const auto dependency) {
                                                return !graph->nodes[dependency].result.succeeded();
                                            });
                   failed_dependency != node.dependencies.end()) {
            const auto& error = *graph->nodes[*failed_dependency].result.error;
            mark_terminal_locked(scheduler, *graph, ready,
                                 cancelled_result(task_error_code_t::dependency_failed,
                                                  "dependency failed: " + error.message));
            push_ready_dependents(*graph, ready);
        } else {
            enqueue_locked(scheduler, graph, ready);
        }
    }
    prune_graphs_locked(scheduler);
    scheduler.changed.notify_all();
}

void run_worker_start_hook(const std::function<void()>& on_start) noexcept {
    if (!on_start) return;
    try {
        on_start();
    } catch (...) {
        // Keep running at the default priority.
    }
}

void worker_loop(const std::shared_ptr<scheduler_state_t>& scheduler, std::stop_token worker_stop,
                 const std::function<void()>& on_start) {
    run_worker_start_hook(on_start);
    while (true) {
        work_item_t work;
        {
            std::unique_lock lock(scheduler->mutex);
            scheduler->changed.wait(lock, worker_stop, [&] {
                return scheduler->stopping || scheduler->queue_size != 0;
            });
            if ((scheduler->stopping || worker_stop.stop_requested()) &&
                scheduler->queue_size == 0) {
                return;
            }
            if (scheduler->queue_size == 0) {
                continue;
            }

            work = std::move(scheduler->queue[scheduler->queue_head]);
            scheduler->queue[scheduler->queue_head] = {};
            scheduler->queue_head = (scheduler->queue_head + 1) % scheduler->queue.size();
            --scheduler->queue_size;
            auto& node = work.graph->nodes[work.task];
            if (node.status != node_status_t::queued) {
                scheduler->changed.notify_all();
                continue;
            }
            if (work.graph->stop_source.stop_requested() || scheduler->stopping) {
                const auto code = scheduler->stopping ? task_error_code_t::scheduler_stopped
                                                      : task_error_code_t::cancelled;
                const auto* reason =
                    scheduler->stopping ? "scheduler is shutting down" : "task graph was cancelled";
                complete_locked(*scheduler, work.graph, work.task, cancelled_result(code, reason));
                continue;
            }
            node.status = node_status_t::running;
            node.inputs.clear();
            for (const auto dependency : node.dependencies) {
                node.inputs.push_back(work.graph->nodes[dependency].result);
            }
        }

        auto result =
            task_result_t::failure(task_error_code_t::task_failed, "task did not return a result");
        try {
            auto& node = work.graph->nodes[work.task];
            result = node.function(std::span<const task_result_t>(node.inputs),
                                   work.graph->stop_source.get_token());
        } catch (const std::exception& exception) {
            result = task_result_t::failure(task_error_code_t::task_threw, exception.what());
        } catch (...) {
            result = task_result_t::failure(task_error_code_t::task_threw,
                                            "task threw a non-standard exception");
        }

        work.graph->nodes[work.task].inputs.clear();
        {
            std::lock_guard lock(scheduler->mutex);
            complete_locked(*scheduler, work.graph, work.task, std::move(result));
        }
    }
}

} // namespace

auto task_result_t::success(task_value_t value) -> task_result_t {
    return {.payload = std::move(value)};
}

auto task_result_t::failure(task_error_code_t code, std::string message) -> task_result_t {
    return {.error = task_error_t{.code = code, .message = std::move(message)}};
}

task_graph_t::task_graph_t(std::size_t maximum_tasks)
    : impl_(std::make_unique<impl_t>(maximum_tasks)) {}

task_graph_t::task_graph_t(task_graph_t&&) noexcept = default;
auto task_graph_t::operator=(task_graph_t&&) noexcept -> task_graph_t& = default;
task_graph_t::~task_graph_t() = default;

auto task_graph_t::add_task(std::span<const task_id_t> dependencies, task_function_t function)
    -> task_add_result_t {
    if (!impl_ || !function) {
        return {.error = task_error_t{.code = task_error_code_t::invalid_task,
                                      .message = "task callback is empty"}};
    }
    if (impl_->nodes.size() >= impl_->maximum_tasks ||
        impl_->nodes.size() >= std::numeric_limits<std::uint32_t>::max()) {
        return {.error = task_error_t{.code = task_error_code_t::capacity_exceeded,
                                      .message = "task graph capacity exceeded"}};
    }
    const auto next = static_cast<std::uint32_t>(impl_->nodes.size());
    impl_t::node_definition_t node{.function = std::move(function)};
    node.dependencies.reserve(dependencies.size());
    for (const auto dependency : dependencies) {
        if (dependency.index >= next ||
            std::ranges::find(node.dependencies, dependency.index) != node.dependencies.end()) {
            return {.error =
                        task_error_t{.code = task_error_code_t::invalid_task,
                                     .message = "dependency must be unique and already added"}};
        }
        node.dependencies.push_back(dependency.index);
    }
    impl_->nodes.push_back(std::move(node));
    return {.id = task_id_t{next}};
}

auto task_graph_t::size() const noexcept -> std::size_t {
    return impl_ ? impl_->nodes.size() : 0;
}

task_graph_handle_t::task_graph_handle_t(std::shared_ptr<impl_t> impl) : impl_(std::move(impl)) {}

auto task_graph_handle_t::wait() const -> bool {
    if (!impl_) {
        return false;
    }
    auto& scheduler = *impl_->scheduler;
    std::unique_lock lock(scheduler.mutex);
    scheduler.changed.wait(lock, [&] { return impl_->graph->remaining == 0; });
    return true;
}

void task_graph_handle_t::cancel() const {
    if (!impl_) {
        return;
    }
    impl_->graph->stop_source.request_stop();
    auto& scheduler = *impl_->scheduler;
    std::lock_guard lock(scheduler.mutex);
    cancel_graph_locked(scheduler, impl_->graph, task_error_code_t::cancelled,
                        "task graph was cancelled");
    scheduler.changed.notify_all();
}

auto task_graph_handle_t::result(task_id_t task) const -> std::optional<task_result_t> {
    if (!impl_ || task.index >= impl_->graph->nodes.size()) {
        return std::nullopt;
    }
    std::lock_guard lock(impl_->scheduler->mutex);
    const auto& node = impl_->graph->nodes[task.index];
    if (!is_terminal(node.status)) {
        return std::nullopt;
    }
    return node.result;
}

auto task_graph_handle_t::task_count() const noexcept -> std::size_t {
    return impl_ ? impl_->graph->nodes.size() : 0;
}

task_graph_handle_t::operator bool() const noexcept {
    return static_cast<bool>(impl_);
}

struct task_scheduler_t::impl_t {
    explicit impl_t(std::size_t worker_count, std::size_t capacity,
                    task_scheduler_options_t options)
        : state(std::make_shared<scheduler_state_t>(std::max<std::size_t>(1, capacity))) {
        const auto count =
            clamp_worker_count(worker_count, std::thread::hardware_concurrency());
        workers.reserve(count);
        for (auto index = std::size_t{}; index < count; ++index) {
            workers.emplace_back([shared = state, hook = options.on_worker_start](
                                     std::stop_token stop) { worker_loop(shared, stop, hook); });
        }
    }

    std::shared_ptr<scheduler_state_t> state;
    std::vector<std::jthread> workers;
    bool joined{};
};

auto clamp_worker_count(std::size_t requested, std::size_t hardware_threads) noexcept
    -> std::size_t {
    requested = std::max<std::size_t>(1, requested);
    if (hardware_threads == 0) return requested;
    const auto limit = hardware_threads > 2 ? hardware_threads - 2 : std::size_t{1};
    return std::min(requested, limit);
}

task_scheduler_t::task_scheduler_t(std::size_t worker_count, std::size_t maximum_outstanding_tasks,
                                   task_scheduler_options_t options)
    : impl_(std::make_unique<impl_t>(worker_count, maximum_outstanding_tasks,
                                     std::move(options))) {}

auto task_scheduler_t::worker_count() const noexcept -> std::size_t {
    return impl_->workers.size();
}

task_scheduler_t::~task_scheduler_t() {
    shutdown();
}

auto task_scheduler_t::submit(task_graph_t graph) -> task_submit_result_t {
    if (!impl_ || !graph.impl_ || graph.impl_->nodes.empty()) {
        return {.error = task_error_t{.code = task_error_code_t::invalid_task,
                                      .message = "cannot submit an empty task graph"}};
    }
    auto state = impl_->state;
    auto runtime_graph = std::make_shared<graph_state_t>();
    runtime_graph->nodes.reserve(graph.impl_->nodes.size());
    runtime_graph->propagation.reserve(graph.impl_->nodes.size());
    for (auto& definition : graph.impl_->nodes) {
        detail::node_state_t node{.function = std::move(definition.function),
                                  .dependencies = std::move(definition.dependencies)};
        node.dependencies_left = node.dependencies.size();
        node.inputs.reserve(node.dependencies.size());
        runtime_graph->nodes.push_back(std::move(node));
    }
    for (std::uint32_t task = 0; task < runtime_graph->nodes.size(); ++task) {
        for (const auto dependency : runtime_graph->nodes[task].dependencies) {
            runtime_graph->nodes[dependency].dependents.push_back(task);
        }
    }
    runtime_graph->remaining = runtime_graph->nodes.size();

    std::lock_guard lock(state->mutex);
    if (state->stopping) {
        return {.error = task_error_t{.code = task_error_code_t::scheduler_stopped,
                                      .message = "scheduler is shutting down"}};
    }
    if (runtime_graph->nodes.size() > state->maximum_outstanding - state->outstanding) {
        return {.error = task_error_t{.code = task_error_code_t::capacity_exceeded,
                                      .message = "scheduler outstanding-task capacity exceeded"}};
    }
    state->outstanding += runtime_graph->nodes.size();
    prune_graphs_locked(*state);
    state->graphs.push_back(runtime_graph);
    for (std::uint32_t task = 0; task < runtime_graph->nodes.size(); ++task) {
        auto& node = runtime_graph->nodes[task];
        if (node.dependencies.empty()) {
            enqueue_locked(*state, runtime_graph, task);
        }
    }

    auto handle = std::make_shared<task_graph_handle_t::impl_t>();
    handle->scheduler = std::move(state);
    handle->graph = std::move(runtime_graph);
    handle->scheduler->changed.notify_all();
    return {.graph = task_graph_handle_t(std::move(handle))};
}

void task_scheduler_t::shutdown() {
    if (!impl_ || impl_->joined) {
        return;
    }
    auto state = impl_->state;
    auto stop_count = std::size_t{};
    {
        std::lock_guard lock(state->mutex);
        state->stopping = true;
        state->queue_head = 0;
        state->queue_tail = 0;
        state->queue_size = 0;
        for (auto& item : state->queue) {
            item = {};
        }
        for (auto& weak_graph : state->graphs) {
            if (auto graph = weak_graph.lock()) {
                state->stop_scratch[stop_count++] = graph;
                for (std::uint32_t task = 0; task < graph->nodes.size(); ++task) {
                    const auto status = graph->nodes[task].status;
                    if (status == node_status_t::blocked || status == node_status_t::queued) {
                        mark_terminal_locked(*state, *graph, task,
                                             cancelled_result(task_error_code_t::scheduler_stopped,
                                                              "scheduler is shutting down"));
                    }
                }
            }
        }
        state->changed.notify_all();
    }
    for (auto index = std::size_t{}; index < stop_count; ++index) {
        state->stop_scratch[index]->stop_source.request_stop();
        state->stop_scratch[index].reset();
    }
    for (auto& worker : impl_->workers) {
        worker.request_stop();
    }
    state->changed.notify_all();
    for (auto& worker : impl_->workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    impl_->joined = true;
}

auto task_scheduler_t::outstanding_tasks() const noexcept -> std::size_t {
    if (!impl_) {
        return 0;
    }
    std::lock_guard lock(impl_->state->mutex);
    return impl_->state->outstanding;
}

auto task_scheduler_t::maximum_outstanding_tasks() const noexcept -> std::size_t {
    return impl_ ? impl_->state->maximum_outstanding : 0;
}

auto generation_guard_t::begin_generation() -> std::uint64_t {
    std::lock_guard lock(mutex_);
    auto next = generation_.load(std::memory_order_relaxed) + 1;
    if (next == 0) {
        next = 1;
    }
    generation_.store(next, std::memory_order_release);
    return next;
}

auto generation_guard_t::current_generation() const noexcept -> std::uint64_t {
    return generation_.load(std::memory_order_acquire);
}

auto generation_guard_t::is_current(std::uint64_t generation) const noexcept -> bool {
    return generation != 0 && current_generation() == generation;
}

} // namespace wormedit

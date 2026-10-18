#pragma once

#include <wormedit/core/async_job.h>
#include <wormedit/core/command.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/workspace.h>

#include "sys/thread_priority.h"
#include "sys/output_group_export.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace wormedit::sys {

struct resource_copy_identity_t {
    wormedit::asset_id_t asset;
    wormedit::document_stamp_t document;
    std::filesystem::path source_path;
    std::filesystem::path output_path;
    bool export_copy{};
    auto operator==(const resource_copy_identity_t&) const -> bool = default;
};
struct resource_copy_request_t {
    resource_copy_identity_t target;
    std::filesystem::path project_directory;
    auto operator==(const resource_copy_request_t&) const -> bool = default;
};
using resource_copy_job_t =
    wormedit::async_job_t<resource_copy_request_t, wormedit::project_asset_copy_outcome_t,
                          resource_copy_identity_t>;

namespace runtime {

struct resource_copy_t {
    explicit resource_copy_t(
        wormedit::task_scheduler_t& scheduler,
        wormedit::notification_sink_t notifications = wormedit::notification_sink_t::muted());

    [[nodiscard]] auto is_busy() const noexcept -> bool {
        return job_.is_busy();
    }

    auto start(resource_copy_request_t request,
               resource_copy_job_t::work_function_t work = copy_file)
        -> wormedit::async_job_start_outcome_t;
    auto poll(wormedit::document_history_t& history,
              wormedit::workspace_controller_t& workspace) -> std::optional<std::string>;
    void cancel();

private:
  wormedit::notification_sink_t notifications_{wormedit::notification_sink_t::muted()};
  [[nodiscard]] static auto copy_file(const resource_copy_request_t& request, std::stop_token stop)
      -> resource_copy_job_t::work_outcome_t;
  resource_copy_job_t job_;
};

class output_group_export_t {
public:
    explicit output_group_export_t(
        wormedit::notification_sink_t notifications = wormedit::notification_sink_t::muted());
    ~output_group_export_t() noexcept;

    output_group_export_t(const output_group_export_t&) = delete;
    auto operator=(const output_group_export_t&) -> output_group_export_t& = delete;

    using work_function_t = output_group_export_task_result_t (*)(
        const output_group_export_request_t&, std::stop_token);
    using start_outcome_t = result_t<void, error_t<failure_code_t>>;

    [[nodiscard]] auto is_busy() const noexcept -> bool { return pending_.has_value(); }
    [[nodiscard]] auto scheduler() noexcept -> task_scheduler_t& { return scheduler_; }
    auto start(const song_document_t& document, output_group_export_plan_t plan,
               std::filesystem::path asset_base,
               work_function_t work = run_output_group_export) -> start_outcome_t;
    auto poll(const document_history_t& history) -> std::optional<std::string>;
    void cancel();
    void shutdown();

private:
    struct pending_t {
        task_graph_handle_t graph;
        task_id_t task;
        async_job_project_identity_t identity;
        // The scheduler replaces task results when cancelled, even after publication.
        // Read this domain receipt only after graph.outcome establishes completion.
        std::shared_ptr<std::optional<output_group_export_task_result_t>> receipt;
    };
    notification_sink_t notifications_;
    task_scheduler_t scheduler_{1, 1, background_scheduler_options()};
    std::optional<pending_t> pending_;
};

} // namespace runtime
} // namespace wormedit::sys

#pragma once

#include <wormedit/core/async_job.h>
#include <wormedit/core/command.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/workspace.h>

#include "sys/thread_priority.h"

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

struct output_group_export_task_result_t {
    bool published{};
    std::string message;
    wormedit::notification_severity_t severity{wormedit::notification_severity_t::error};
    std::string detail;
};

struct pending_output_group_export_t {
    wormedit::task_graph_handle_t graph;
    wormedit::task_id_t task;
    wormedit::song_id_t song;
    std::uint64_t revision{};
};

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

struct output_group_export_t {
    explicit output_group_export_t(
        wormedit::notification_sink_t notifications = wormedit::notification_sink_t::muted());
    ~output_group_export_t() noexcept;

    output_group_export_t(const output_group_export_t&) = delete;
    auto operator=(const output_group_export_t&) -> output_group_export_t& = delete;

private:
  wormedit::notification_sink_t notifications_{wormedit::notification_sink_t::muted()};

public:
    wormedit::task_scheduler_t scheduler{1, 1, wormedit::background_scheduler_options()};
    std::optional<pending_output_group_export_t> pending;

    [[nodiscard]] auto is_busy() const noexcept -> bool { return pending.has_value(); }

    auto start(pending_output_group_export_t&& pending) -> bool;
    auto poll(const wormedit::document_history_t& history) -> std::optional<std::string>;
    void cancel();
    void shutdown();
};

} // namespace runtime


} // namespace wormedit::sys

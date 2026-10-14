#pragma once

#include <wormedit/core/task_graph.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/workspace.h>

#include "sys/thread_priority.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace wormedit::sys {

struct pending_project_resource_copy_t {
    wormedit::task_graph_handle_t graph;
    wormedit::task_id_t task;
    wormedit::asset_id_t asset;
    wormedit::song_id_t song;
    std::uint64_t revision{};
    std::filesystem::path source_path;
    std::filesystem::path output_path;
    bool export_copy{};
};


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
        wormedit::notification_sink_t notifications = wormedit::notification_sink_t::muted());

    [[nodiscard]] auto is_busy() const noexcept -> bool { return pending_.has_value(); }

    auto start(pending_project_resource_copy_t&& pending) -> bool;
    auto poll(wormedit::document_history_t& history,
              wormedit::workspace_controller_t& workspace) -> std::optional<std::string>;
    void cancel();

private:
  wormedit::notification_sink_t notifications_{wormedit::notification_sink_t::muted()};
  std::optional<pending_project_resource_copy_t> pending_;
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

#pragma once

#include <wormedit/core/task_graph.h>
#include <wormedit/core/workspace.h>

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
};

struct pending_output_group_export_t {
    wormedit::task_graph_handle_t graph;
    wormedit::task_id_t task;
    wormedit::song_id_t song;
    std::uint64_t revision{};
};

namespace runtime {

struct resource_copy_t {
    [[nodiscard]] auto busy() const noexcept -> bool { return pending_.has_value(); }

    auto start(pending_project_resource_copy_t&& pending) -> bool;
    auto poll(wormedit::document_history_t& history,
              wormedit::workspace_controller_t& workspace) -> std::optional<std::string>;
    void cancel();

private:
    std::optional<pending_project_resource_copy_t> pending_;
};

struct output_group_export_t {
    wormedit::task_scheduler_t scheduler{1, 1};
    std::optional<pending_output_group_export_t> pending;

    [[nodiscard]] auto busy() const noexcept -> bool { return pending.has_value(); }
    auto start(pending_output_group_export_t&& pending) -> bool;
    auto poll(const wormedit::document_history_t& history) -> std::optional<std::string>;
    void cancel();
    void shutdown();
};

} // namespace runtime


} // namespace wormedit::sys

#pragma once

#include "ui/frontend.h"
#include <map>
#include <optional>
#include <wormedit/core/async_job.h>
#include <wormedit/core/document.h>
#include <wormedit/core/metadata_editor.h>
#include <wormedit/core/notifications.h>

#include <cstdint>
#include <string>
#include <vector>

namespace wormedit::sys {

inline constexpr std::size_t maximum_project_chart_rows = 256;

struct project_chart_row_view_t {
    chart_id_t chart;
    std::string name;
    std::string author;
    std::string rating;
    std::string mode;
    std::uint32_t key_count{};
    std::string output_groups;
    std::size_t output_group_count{};
    std::uint64_t note_count{};
    std::string length;
    std::string diagnostic;

    [[nodiscard]] static auto from(const song_document_t& document, const chart_document_t& chart)
        -> project_chart_row_view_t;
};

struct project_chart_table_cache_t;

struct project_chart_table_view_t {
    std::vector<project_chart_row_view_t> rows;
    std::size_t total_charts{};
    bool rows_truncated{};

    [[nodiscard]] static auto from(const song_document_t& document) -> project_chart_table_view_t;

    // Caller-owned session generation distinguishes reopened documents with identical IDs/revisions.
    [[nodiscard]] static auto from(project_chart_table_cache_t& cache,
                                   const song_document_t& document,
                                   std::uint64_t session_generation)
        -> const project_chart_table_view_t&;
};

struct project_chart_table_cache_t {
    project_chart_table_view_t table;
    song_id_t song;
    std::uint64_t session_generation{};
    std::uint64_t revision{};
    bool populated{};
};

enum class project_chart_command_kind_t { create, duplicate, erase, move_up, move_down, open, batch_metadata, reorder };
struct project_chart_command_t {
    project_chart_command_kind_t kind{project_chart_command_kind_t::open};
    std::uint64_t expected_revision{};
    chart_id_t chart; // create: active chart template; other actions: target row.
    batch_chart_metadata_action_t metadata;
    chart_id_t target_chart;
    bool insert_after{};
};
struct project_chart_command_result_t {
    document_command_result_t command;
    std::optional<chart_id_t> open_chart;
};
// Scalar UI actions are translated by the caller; all mutations remain revision checked.
[[nodiscard]] auto apply_project_chart_command(document_history_t& history,
    const project_chart_command_t& action) -> project_chart_command_result_t;

struct output_preview_request_t {
    song_document_t document;
    output_group_t group;
};
struct output_preview_identity_t {
    async_job_project_identity_t document;
    std::size_t index{};
    auto operator==(const output_preview_identity_t&) const -> bool = default;
};
using output_preview_job_t =
    async_job_t<output_preview_request_t, std::string, output_preview_identity_t>;

class project_chart_runtime_t {
  public:
    project_chart_runtime_t() = default;
    explicit project_chart_runtime_t(std::uint64_t generation) : session_generation(generation) {}
    std::uint64_t session_generation{1};
    project_chart_table_cache_t cache;
    std::vector<WormUiProjectChartRow> rows;
    std::map<std::uint64_t, std::string> row_errors;
    std::vector<WormUiProjectOutputGroupView> output_groups;
    std::vector<std::vector<uint64_t>> output_group_charts;
    std::vector<std::string> output_group_status;
    std::vector<std::string> output_group_paths;
    song_id_t output_group_song;
    uint64_t output_group_revision{};
    [[nodiscard]] auto preview_job() noexcept -> output_preview_job_t* {
        return preview_job_ ? &*preview_job_ : nullptr;
    }
    [[nodiscard]] auto preview_job(task_scheduler_t& scheduler) -> output_preview_job_t&;
    void cancel_preview();

  private:
    std::optional<output_preview_job_t> preview_job_;
};
struct project_chart_ui_result_t {
    std::string status;
    std::optional<chart_id_t> open_chart;
    bool applied{};
    notification_severity_t severity{notification_severity_t::warning};
};
void build_project_chart_view(project_chart_runtime_t& runtime, const song_document_t& document,
                              WormUiSetupView& view);
[[nodiscard]] auto apply_project_chart_ui_action(project_chart_runtime_t& runtime,
    document_history_t& history, const WormUiProjectChartAction& action,
    std::optional<chart_id_t> active_chart = std::nullopt) -> project_chart_ui_result_t;

[[nodiscard]] auto apply_project_output_group_ui_action(project_chart_runtime_t& runtime,
    document_history_t& history, const WormUiProjectOutputGroupAction& action,
    task_scheduler_t& scheduler) -> project_chart_ui_result_t;

} // namespace wormedit::sys

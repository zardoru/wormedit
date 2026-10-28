#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>
#include <wormedit/core/notifications.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/formats/conversion_publication.h>
#include <wormedit/formats/slice_spans.h>

namespace wormedit::sys {
struct output_group_export_target_t {
    output_group_t group;
    conversion_request_t request;
    existing_files_policy_t existing_files{existing_files_policy_t::ask};
};

struct output_group_export_plan_t {
    [[nodiscard]] static auto
    from(const song_document_t& document, output_group_t group,
         std::optional<existing_files_policy_t> one_run_policy = std::nullopt)
        -> output_group_export_plan_t;
    [[nodiscard]] static auto from_all(const song_document_t& document,
                                       std::span<const output_group_t> groups)
        -> std::optional<output_group_export_plan_t>;
    void replace_asked_files();
    output_group_t group;
    conversion_request_t request;
    existing_files_policy_t existing_files{existing_files_policy_t::ask};
    std::vector<output_group_export_target_t> additional_targets;
};

inline auto output_group_export_plan_t::from(const song_document_t&, output_group_t group,
                                             std::optional<existing_files_policy_t> one_run_policy)
    -> output_group_export_plan_t {
    const auto existing_files = one_run_policy.value_or(group.existing_files);
    conversion_request_t request;
    request.target = group.family;
    if (group.family != format_family_t::native_project)
        request.charts = group.charts;
    request.overwrite = existing_files == existing_files_policy_t::replace;
    request.loss_policy = loss_policy_t::warn;
    request.audio = group.audio;
    if (group.family != format_family_t::bms &&
        group.family != format_family_t::native_project && group.flatten_bms_branches)
        request.branches = branch_projection_t::active_projection;
    if (group.family == format_family_t::stepmania) {
        request.stepmania = group.stepmania;
    }
    return {
        .group = std::move(group), .request = std::move(request), .existing_files = existing_files};
}

struct output_group_export_request_t {
    song_document_t document;
    output_group_export_plan_t plan;
    std::filesystem::path asset_base;
    std::shared_ptr<job_progress_t> progress;
};

enum class output_group_export_file_action_t {
    new_file,
    replace,
    reuse,
    copy,
};

struct output_group_export_file_plan_t {
    std::filesystem::path path;
    output_group_export_file_action_t action{output_group_export_file_action_t::new_file};
    std::uintmax_t bytes{};
};

// Shared, read-only preparation for Preview and Export. Files are written only
// by run_output_group_export after this complete preflight succeeds.
struct output_group_export_preparation_t {
    [[nodiscard]] static auto from(const output_group_export_request_t& input,
                                   std::stop_token stop = {})
        -> result_t<output_group_export_preparation_t, std::string>;
    conversion_request_t request;
    conversion_result_t converted;
    std::vector<project_asset_copy_request_t> pending_assets;
    conversion_publication_plan_payload_t publication;
    std::vector<std::filesystem::path> conflicts;
    std::vector<output_group_export_file_plan_t> files;
    std::string blocking_error;
    notification_target_t blocking_target;
};

struct output_group_export_task_result_t {
    bool published{};
    bool confirmation_required{};
    std::string message;
    notification_severity_t severity{notification_severity_t::error};
    std::string detail;
    notification_target_t target;
};

[[nodiscard]] auto run_output_group_export(const output_group_export_request_t& request,
                                          std::stop_token stop)
    -> output_group_export_task_result_t;
} // namespace wormedit::sys

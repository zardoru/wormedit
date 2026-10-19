#pragma once

#include <wormedit/core/notifications.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/formats/conversion_publication.h>
#include <wormedit/formats/slice_spans.h>
#include <utility>
#include <memory>
#include <filesystem>
#include <stop_token>
#include <string>

namespace wormedit::sys {
struct output_group_export_plan_t {
    [[nodiscard]] static auto from(const song_document_t& document, output_group_t group,
                                   bool overwrite = false,
                                   stepmania_export_policy_t stepmania_policy = {})
        -> output_group_export_plan_t;
    output_group_t group;
    conversion_request_t request;
};

inline auto output_group_export_plan_t::from(const song_document_t& document, output_group_t group,
                                             bool overwrite, stepmania_export_policy_t stepmania_policy)
    -> output_group_export_plan_t {
    conversion_request_t request;
    request.target = group.family;
    if (group.family != format_family_t::native_project)
        request.charts = group.charts;
    request.overwrite = overwrite;
    request.loss_policy = loss_policy_t::warn;
    if (group.family == format_family_t::stepmania) {
        // The group owns its file family; target and timing syntax are session policy.
        stepmania_policy.file_profile = group.relative_output_path.extension() == ".sm" ||
                                                group.relative_output_path == "sm"
                                            ? stepmania_profile_t::sm
                                            : stepmania_profile_t::ssc;
        request.stepmania = stepmania_policy;
    } else if (group.family == format_family_t::bms) {
        if (!is_conversion_needs_slice_spans(document, request))
            request.bms_output_extension = group.relative_output_path.extension().string();
    }
    return {.group = std::move(group), .request = std::move(request)};
}

struct output_group_export_request_t {
    song_document_t document;
    output_group_export_plan_t plan;
    std::filesystem::path asset_base;
    std::shared_ptr<job_progress_t> progress;
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
};

struct output_group_export_task_result_t {
    bool published{};
    std::string message;
    notification_severity_t severity{notification_severity_t::error};
    std::string detail;
};

[[nodiscard]] auto run_output_group_export(const output_group_export_request_t& request,
                                          std::stop_token stop)
    -> output_group_export_task_result_t;
} // namespace wormedit::sys

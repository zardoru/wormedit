#pragma once

#include <wormedit/core/notifications.h>
#include <wormedit/formats/conversion.h>
#include <wormedit/formats/slice_spans.h>
#include <utility>
#include <filesystem>
#include <stop_token>
#include <string>

namespace wormedit::sys {
struct output_group_export_plan_t {
    [[nodiscard]] static auto from(const song_document_t& document, output_group_t group,
                                   bool overwrite = false) -> output_group_export_plan_t;
    output_group_t group;
    conversion_request_t request;
};

inline auto output_group_export_plan_t::from(const song_document_t& document, output_group_t group,
                                      bool overwrite) -> output_group_export_plan_t {
    conversion_request_t request;
    request.target = group.family;
    if (group.family != format_family_t::native_project)
        request.charts = group.charts;
    request.overwrite = overwrite;
    request.loss_policy = loss_policy_t::warn;
    if (group.family == format_family_t::stepmania) {
        request.stepmania = stepmania_export_policy_t{
            .file_profile = group.relative_output_path.extension() == ".sm" ||
                                    group.relative_output_path == "sm"
                                ? stepmania_profile_t::sm
                                : stepmania_profile_t::ssc};
    } else if (group.family == format_family_t::bms) {
        const auto extension = group.relative_output_path.extension().string();
        if ((extension == ".bme" || extension == ".bml" || extension == ".pms") &&
            !is_conversion_needs_slice_spans(document, request))
            request.target_profile = extension.substr(1);
    }
    return {.group = std::move(group), .request = std::move(request)};
}

struct output_group_export_request_t {
    song_document_t document;
    output_group_export_plan_t plan;
    std::filesystem::path asset_base;
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

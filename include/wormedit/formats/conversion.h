#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/job_progress.h>
#include <stop_token>
#include <wormedit/formats/slice_spans.h>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wormedit {

struct conversion_request_t {
    format_family_t target{format_family_t::native_project};
    std::string target_profile;
    std::optional<stepmania_export_policy_t> stepmania;
    std::optional<chart_id_t> chart;
    // Ordered subset for one output group. Mutually exclusive with chart.
    // Empty means every chart in the document.
    std::vector<chart_id_t> charts;
    branch_projection_t branches{branch_projection_t::preserve_tree};
    std::optional<text_encoding_t> encoding;
    std::optional<quantize_request_t> quantization;
    loss_policy_t loss_policy{loss_policy_t::reject};
    bool overwrite{false};
    struct staged_asset_t {
        asset_id_t asset;
        // Path relative to the output chart's directory, after a successful
        // create_export_asset_copy publication.
        std::filesystem::path relative_path;
        auto operator==(const staged_asset_t&) const -> bool = default;
    };
    std::vector<staged_asset_t> staged_assets;
    // Explicit decoder geometry: pure conversion never opens source files.
    std::vector<slice_span_source_t> slice_sources;
    slice_span_options_t slice_options;
    // BMS filename choice, independent of the lane profile.
    std::string bms_output_extension;
};

enum class conversion_stage_t {
    request,
    transform,
    serialize,
    reimport,
};

enum class conversion_diagnostic_code_t {
    invalid_request,
    unsupported_target,
    unknown_target_profile,
    chart_not_found,
    transform_failed,
    target_diagnostic,
    loss_rejected,
    output_limit,
    reimport_failed,
    cancelled,
};

struct conversion_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    conversion_diagnostic_code_t code{conversion_diagnostic_code_t::invalid_request};
    conversion_stage_t stage{conversion_stage_t::request};
    format_family_t target{format_family_t::native_project};
    std::optional<chart_id_t> chart;
    bool loss{};
    std::string message;
    std::string path;
    auto operator==(const conversion_diagnostic_t&) const -> bool = default;
};

struct conversion_output_t {
    format_family_t target{format_family_t::native_project};
    std::optional<chart_id_t> chart;
    std::string suggested_filename;
    std::string bytes;
    bool reimport_validated{};
    auto operator==(const conversion_output_t&) const -> bool = default;
};

// Advisory artifact counters; stop checks run between writers/reimports.
struct conversion_execution_t {
    std::stop_token stop;
    job_progress_t* progress{};
};

struct conversion_limits_t {
    std::size_t maximum_outputs{4096};
    std::size_t maximum_total_output_bytes{256U * 1024U * 1024U};
    std::size_t maximum_filename_bytes{240};
};

struct conversion_asset_copy_t {
    asset_id_t asset;
    std::filesystem::path relative_path;
    auto operator==(const conversion_asset_copy_t&) const -> bool = default;
};

struct conversion_payload_t {
    std::vector<conversion_output_t> outputs;
    // Render these detached plans only inside atomic publication staging.
    std::vector<slice_span_plan_outcome_t> slice_plans;
    std::vector<conversion_asset_copy_t> asset_copies;
    // Synthetic zero samples for soundless BMS gameplay, not source spans.
    std::vector<std::filesystem::path> silent_files;
};
using conversion_outcome_t = diagnosed_t<conversion_payload_t, std::vector<conversion_diagnostic_t>>;
struct conversion_result_t {
    [[nodiscard]] static auto from(const song_document_t& song, const conversion_request_t& request,
        conversion_limits_t limits = {}, conversion_execution_t execution = {}) -> conversion_outcome_t;
    [[nodiscard]] static auto from(conversion_outcome_t&& outcome) -> conversion_result_t;
    std::vector<conversion_output_t> outputs;
    // Render these detached plans only inside atomic publication staging.
    std::vector<slice_span_plan_outcome_t> slice_plans;
    std::vector<conversion_asset_copy_t> asset_copies;
    // Synthetic zero samples for soundless BMS gameplay, not source spans.
    std::vector<std::filesystem::path> silent_files;
    std::vector<conversion_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
    [[nodiscard]] explicit operator bool() const noexcept;
};

[[nodiscard]] auto is_conversion_needs_slice_spans(const song_document_t& song,
    const conversion_request_t& request) -> bool;

// Builds a detached, bounded output plan. It never mutates the source document
// or publishes files. Every serialized artifact is parsed again before the
// complete output set is returned; any error or rejected loss clears all
// outputs so preview, CLI, and later filesystem publication share one atomic
// preparation boundary.
[[nodiscard]] auto convert_document(const song_document_t& song,
                                    const conversion_request_t& request,
                                    conversion_limits_t limits = {}, conversion_execution_t execution = {})
    -> conversion_result_t;

} // namespace wormedit

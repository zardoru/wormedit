#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/osbx_lua_link.h>
#include <wormedit/core/export_cut_transform.h>
#include <wormedit/core/job_progress.h>
#include <stop_token>
#include <wormedit/formats/slice_spans.h>
#include <wormedit/formats/bms_export_profile.h>
#include <wormedit/core/osbx_osb_export.h>
#include <wormedit/core/resource_file_copy.h>

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
    std::vector<export_cut_source_t> export_cut_sources;
    // BMS filename choice, independent of the lane profile.
    std::string bms_output_extension;
    // Independent of target_profile's existing BMS lane-map selection.
    std::optional<bms_export_profile_t> bms_profile;
    export_audio_settings_t audio;
    // Validated media geometry and the actual song/output playback domain.
    // Pure conversion never opens image files.
    // Worker-prepared, strongly owned transient snapshots. Never persisted.
    std::vector<osbx_lua_prepared_scene_t> osbx_prepared;
    std::vector<osbx_osb_image_size_t> osbx_image_sizes;
    std::optional<osbx_osb_span_t> osbx_playback_span;
    // Used only by filesystem preflight/publication, never by pure conversion.
    std::vector<project_path_copy_request_t> osbx_image_sources;
    // Raw storyboard Sample sources: same publication receipts, no image probe.
    std::vector<project_path_copy_request_t> osbx_sample_sources;
    // Advisory native-storyboard preflight losses do not reject the chart.
    std::vector<osbx_osb_report_t> stepmania_storyboard_report;
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
    std::optional<object_id_t> object;
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
    std::optional<std::string> expected_hash; // Immutable original encoded-source bytes, when declared.
    [[nodiscard]] auto validate_staged_file(const std::filesystem::path& path, std::stop_token stop = {}) const
        -> result_t<void, error_t<failure_code_t>>;
    auto operator==(const conversion_asset_copy_t&) const -> bool = default;
};

// Owned, file-free package enumeration shared by pure conversion and source
// preparation. Frame indices refer to images, never to movable borrowed text.
struct storyboard_animation_assets_t {
    osbx_image_t image;
    std::vector<std::size_t> frames;
    auto operator==(const storyboard_animation_assets_t&) const -> bool = default;
};
struct storyboard_media_plan_t {
    std::vector<osbx_image_t> images;
    std::vector<std::filesystem::path> samples;
    std::vector<storyboard_animation_assets_t> animations;
    std::size_t logical_storage_bytes{}; // Shared logical plan/receipt workspace bound.
    [[nodiscard]] static auto from(const song_document_t&, osbx_validation_options_t = {}, std::stop_token = {})
        -> result_t<storyboard_media_plan_t, std::string>;
    auto operator==(const storyboard_media_plan_t&) const -> bool = default;
};

struct conversion_artifact_t {
    std::filesystem::path relative_path;
    std::string bytes;
    auto operator==(const conversion_artifact_t&) const -> bool = default;
};

struct conversion_payload_t {
    std::vector<conversion_output_t> outputs;
    std::vector<std::filesystem::path> storyboard_images;
    std::vector<std::filesystem::path> storyboard_samples;
    std::vector<conversion_artifact_t> storyboard_files;
    std::vector<osbx_osb_report_t> stepmania_storyboard_report;
    // Render these detached plans only inside atomic publication staging.
    std::vector<slice_span_plan_outcome_t> slice_plans;
    std::vector<export_cut_audio_plan_t> cut_audio_plans;
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
    std::vector<std::filesystem::path> storyboard_images;
    std::vector<std::filesystem::path> storyboard_samples;
    std::vector<conversion_artifact_t> storyboard_files;
    std::vector<osbx_osb_report_t> stepmania_storyboard_report;
    // Render these detached plans only inside atomic publication staging.
    std::vector<slice_span_plan_outcome_t> slice_plans;
    std::vector<export_cut_audio_plan_t> cut_audio_plans;
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
// Runs the same detached preparation and media-boundary checks without serializing files.
// A native-target query may select one difficulty; ordinary native conversion still preserves all.
[[nodiscard]] auto diagnose_conversion_preparation(const song_document_t& song,
    const conversion_request_t& request, conversion_execution_t execution = {})
    -> std::vector<conversion_diagnostic_t>;

[[nodiscard]] auto convert_document(const song_document_t& song,
                                    const conversion_request_t& request,
                                    conversion_limits_t limits = {}, conversion_execution_t execution = {})
    -> conversion_result_t;

} // namespace wormedit

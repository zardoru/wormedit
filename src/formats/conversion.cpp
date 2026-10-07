#include <wormedit/formats/conversion.h>
#include <wormedit/formats/slice_spans.h>

#include <wormedit/core/quantize.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/formats/stepmania.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit {
namespace {

auto byte_span(std::string_view bytes) -> std::span<const std::byte> {
    return {reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()};
}

void add_diagnostic(conversion_result_t& result, diagnostic_severity_t severity,
                    conversion_diagnostic_code_t code, conversion_stage_t stage,
                    format_family_t target, std::optional<chart_id_t> chart,
                    std::string message, std::string path = {}, bool loss = false) {
    result.diagnostics.push_back({severity, code, stage, target, chart, loss, std::move(message),
                                  std::move(path)});
}

auto result_has_errors(const conversion_result_t& result) -> bool {
    return std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto selected_chart_ids(const song_document_t& song, const conversion_request_t& request,
                        conversion_result_t& result) -> std::vector<chart_id_t> {
    if (request.chart && !request.charts.empty()) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::invalid_request, conversion_stage_t::request,
                       request.target, request.chart,
                       "Select either one chart or an ordered chart set");
        return {};
    }
    if (!request.charts.empty()) {
        std::unordered_set<std::uint64_t> seen;
        for (const auto id : request.charts) {
            const auto count = std::ranges::count(song.charts, id, &chart_document_t::id);
            if (!id || !seen.insert(id.value).second || count != 1) {
                add_diagnostic(result, diagnostic_severity_t::error,
                               conversion_diagnostic_code_t::invalid_request,
                               conversion_stage_t::request, request.target, id,
                               "Chart set contains a duplicate, missing, or ambiguous identity");
                return {};
            }
        }
        return request.charts;
    }
    if (request.chart) {
        const auto count = std::ranges::count(song.charts, *request.chart, &chart_document_t::id);
        if (count != 1) {
            add_diagnostic(result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::chart_not_found,
                           conversion_stage_t::request, request.target, request.chart,
                           count == 0 ? "Conversion chart was not found"
                                      : "Conversion chart identity is ambiguous");
            return {};
        }
        return {*request.chart};
    }
    std::vector<chart_id_t> ids;
    ids.reserve(song.charts.size());
    for (const auto& chart : song.charts)
        ids.push_back(chart.id);
    if (ids.empty())
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::chart_not_found, conversion_stage_t::request,
                       request.target, std::nullopt, "Conversion requires at least one chart");
    return ids;
}

auto slug(std::string_view source, std::string_view fallback, std::size_t capacity) -> std::string {
    std::string result;
    result.reserve(std::min(source.size(), capacity));
    bool separator{};
    for (const auto byte : source) {
        const auto value = static_cast<unsigned char>(byte);
        if (std::isalnum(value) != 0) {
            if (separator && !result.empty() && result.size() < capacity)
                result.push_back('-');
            separator = false;
            if (result.size() < capacity)
                result.push_back(static_cast<char>(std::tolower(value)));
        } else {
            separator = true;
        }
        if (result.size() >= capacity)
            break;
    }
    while (!result.empty() && result.back() == '-')
        result.pop_back();
    if (result.empty())
        result.assign(fallback.substr(0, capacity));
    return result;
}

auto filename_for(const song_document_t& song, const chart_document_t* chart,
                  std::string_view extension, const conversion_limits_t& limits)
    -> std::optional<std::string> {
    const auto identity = chart ? chart->id.value : song.id.value;
    const auto suffix = std::string{"-"} + std::to_string(identity) + std::string(extension);
    if (suffix.size() >= limits.maximum_filename_bytes)
        return std::nullopt;
    const auto& label = chart ? chart->metadata.name : song.metadata.title;
    const auto fallback = chart ? "chart" : "project";
    return slug(label, fallback, limits.maximum_filename_bytes - suffix.size()) + suffix;
}

auto bms_extension(std::string_view profile) -> std::string_view {
    if (profile.find("pms") != std::string_view::npos)
        return ".pms";
    if (profile.find("bme") != std::string_view::npos)
        return ".bme";
    if (profile.find("bml") != std::string_view::npos)
        return ".bml";
    return ".bms";
}

template <typename Diagnostic>
void append_target_diagnostics(conversion_result_t& result,
                               const std::vector<Diagnostic>& diagnostics,
                               format_family_t target, std::optional<chart_id_t> chart,
                               loss_policy_t loss_policy) {
    for (const auto& diagnostic : diagnostics) {
        const bool loss = diagnostic.severity == diagnostic_severity_t::warning;
        auto severity = diagnostic.severity;
        if (loss && loss_policy == loss_policy_t::allow)
            severity = diagnostic_severity_t::info;
        std::string path;
        if constexpr (requires { diagnostic.path; })
            path = diagnostic.path;
        else if constexpr (requires { diagnostic.field; })
            path = diagnostic.field;
        else if constexpr (requires { diagnostic.json_path; })
            path = diagnostic.json_path;
        add_diagnostic(result, severity, conversion_diagnostic_code_t::target_diagnostic,
                       conversion_stage_t::serialize, target, chart, diagnostic.message,
                       std::move(path), loss);
    }
}

auto selected_chart(const song_document_t& song, chart_id_t id) -> const chart_document_t* {
    const auto found = std::ranges::find(song.charts, id, &chart_document_t::id);
    return found == song.charts.end() ? nullptr : &*found;
}

auto valid_staged_relative_path(const std::filesystem::path& staged_path) -> bool {
    const auto path = staged_path.generic_string();
    return !staged_path.empty() && path.size() <= 4096 && !staged_path.filename().empty() &&
           !staged_path.is_absolute() && !staged_path.has_root_name() &&
           staged_path.lexically_normal() == staged_path &&
           std::ranges::none_of(staged_path,
                                [](const auto& part) { return part == "." || part == ".."; }) &&
           !std::ranges::any_of(path, [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

auto prepare_staged_assets(song_document_t& prepared, const conversion_request_t& request,
                           conversion_result_t& result) -> bool {
    std::unordered_map<std::uint64_t, std::size_t> known_assets;
    known_assets.reserve(prepared.audio_assets.size() + prepared.visual_assets.size());
    for (const auto& asset : prepared.audio_assets) {
        ++known_assets[asset.id.value];
    }
    for (const auto& asset : prepared.visual_assets) {
        ++known_assets[asset.id.value];
    }
    std::unordered_map<std::uint64_t, std::filesystem::path> paths_by_id;
    paths_by_id.reserve(request.staged_assets.size());
    std::unordered_set<std::string> paths;
    for (const auto& staged : request.staged_assets) {
        const auto path = staged.relative_path.generic_string();
        if (!staged.asset || !valid_staged_relative_path(staged.relative_path) ||
            known_assets[staged.asset.value] != 1 ||
            !paths_by_id.emplace(staged.asset.value, staged.relative_path).second ||
            !paths.insert(path).second) {
            add_diagnostic(result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::invalid_request,
                           conversion_stage_t::request, request.target, request.chart,
                           "Staged asset mapping has an invalid, duplicate, or unknown path");
            return false;
        }
    }
    for (auto& asset : prepared.audio_assets) {
        if (const auto found = paths_by_id.find(asset.id.value); found != paths_by_id.end()) {
            asset.source_path = found->second;
        }
    }
    for (auto& asset : prepared.visual_assets) {
        if (const auto found = paths_by_id.find(asset.id.value); found != paths_by_id.end()) {
            asset.source_path = found->second;
        }
    }
    for (auto& resource : prepared.resources) {
        if (const auto found = paths_by_id.find(resource.asset.value); found != paths_by_id.end()) {
            resource.name = found->second.generic_string();
        }
    }
    return true;
}

auto apply_quantization(song_document_t& prepared, const conversion_request_t& request,
                        const std::vector<chart_id_t>& charts,
                        conversion_result_t& result) -> bool {
    if (!request.quantization->objects.empty() && charts.size() != 1) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::invalid_request,
                       conversion_stage_t::transform, request.target, std::nullopt,
                       "Object-scoped quantization requires one selected chart");
        return false;
    }
    for (const auto chart_id : charts) {
        const auto found = std::ranges::find(prepared.charts, chart_id, &chart_document_t::id);
        if (found == prepared.charts.end()) {
            add_diagnostic(result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::chart_not_found,
                           conversion_stage_t::transform, request.target, chart_id,
                           "Quantization chart was not found in the detached document");
            return false;
        }
        auto quantized = quantize_chart_exact(*found, *request.quantization);
        for (const auto& diagnostic : quantized.diagnostics) {
            add_diagnostic(result, diagnostic.severity,
                           conversion_diagnostic_code_t::transform_failed,
                           conversion_stage_t::transform, request.target, chart_id,
                           diagnostic.message);
        }
        if (!quantized) {
            return false;
        }
        const auto moved = quantized.resolution_moves.size() + quantized.quantized_moves.size();
        *found = std::move(*quantized.chart);
        add_diagnostic(result, diagnostic_severity_t::info,
                       conversion_diagnostic_code_t::target_diagnostic,
                       conversion_stage_t::transform, request.target, chart_id,
                       "Quantization prepared " + std::to_string(moved) + " exact movement(s)");
    }
    return true;
}

auto prepare_document(const song_document_t& song, const conversion_request_t& request,
                      const std::vector<chart_id_t>& charts, conversion_result_t& result)
    -> std::optional<song_document_t> {
    auto prepared = song;
    if (!request.charts.empty() && request.target == format_family_t::stepmania) {
        std::vector<chart_document_t> selected;
        selected.reserve(charts.size());
        for (const auto id : charts) {
            const auto found = std::ranges::find(prepared.charts, id, &chart_document_t::id);
            selected.push_back(*found);
        }
        prepared.charts = std::move(selected);
    }
    if (!request.staged_assets.empty() && !prepare_staged_assets(prepared, request, result)) {
        return std::nullopt;
    }
    if (!request.quantization)
        return prepared;
    if (!apply_quantization(prepared, request, charts, result)) {
        return std::nullopt;
    }
    return prepared;
}

auto contains_bms_tree(const song_document_t& song, const std::vector<chart_id_t>& charts) -> bool {
    return std::ranges::any_of(charts, [&](const auto id) {
        const auto* chart = selected_chart(song, id);
        return chart && chart->bms_control_tree.has_value();
    });
}

auto validate_reimport(const conversion_output_t& output, const conversion_request_t& request,
                       const stepmania_export_policy_result_t* stepmania_options,
                       conversion_result_t& result) -> bool {
    switch (output.target) {
    case format_family_t::native_project: {
        const auto parsed = parse_native_project(output.bytes);
        if (parsed.song && !parsed.has_errors())
            return true;
        append_target_diagnostics(result, parsed.diagnostics, output.target, output.chart,
                                  loss_policy_t::warn);
        break;
    }
    case format_family_t::bms: {
        bms_parse_options_t options;
        options.file_extension = output.suggested_filename.substr(
            output.suggested_filename.find_last_of('.'));
        if (!request.target_profile.empty())
            options.lane_profile_id = request.target_profile;
        if (request.encoding && *request.encoding != text_encoding_t::ascii &&
            *request.encoding != text_encoding_t::utf8)
            options.encoding_override = request.encoding;
        const auto parsed = parse_bms_bytes(byte_span(output.bytes), std::move(options));
        if (parsed.song && !parsed.has_errors())
            return true;
        append_target_diagnostics(result, parsed.diagnostics, output.target, output.chart,
                                  loss_policy_t::warn);
        break;
    }
    case format_family_t::stepmania: {
        if (!stepmania_options || !stepmania_options->options)
            break;
        const auto& write = *stepmania_options->options;
        const auto parsed = parse_stepmania_bytes(
            byte_span(output.bytes),
            {.profile = write.profile,
             .source_timing_syntax = write.target_timing_syntax,
             .source_implementation_profile = write.target_implementation_profile});
        if (parsed.song && !parsed.has_errors())
            return true;
        append_target_diagnostics(result, parsed.diagnostics, output.target, output.chart,
                                  loss_policy_t::warn);
        break;
    }
    case format_family_t::osu_mania: {
        const auto parsed = parse_osu_mania_bytes(byte_span(output.bytes));
        if (parsed.song && !parsed.has_errors())
            return true;
        append_target_diagnostics(result, parsed.diagnostics, output.target, output.chart,
                                  loss_policy_t::warn);
        break;
    }
    case format_family_t::bmson: {
        const auto parsed = parse_bmson_bytes(byte_span(output.bytes));
        if (parsed.song && !parsed.has_errors())
            return true;
        append_target_diagnostics(result, parsed.diagnostics, output.target, output.chart,
                                  loss_policy_t::warn);
        break;
    }
    case format_family_t::midi:
    case format_family_t::quaver:
        break;
    }
    add_diagnostic(result, diagnostic_severity_t::error,
                   conversion_diagnostic_code_t::reimport_failed,
                   conversion_stage_t::reimport, output.target, output.chart,
                   "Serialized conversion output did not pass bounded reimport validation",
                   output.suggested_filename);
    return false;
}

auto validate_request_policies(const song_document_t& song,
                               const conversion_request_t& request,
                               conversion_limits_t limits, conversion_result_t& result) -> bool {
    const auto reject = [&](conversion_diagnostic_code_t code, std::string message) {
        add_diagnostic(result, diagnostic_severity_t::error, code, conversion_stage_t::request,
                       request.target, request.chart, std::move(message));
        return false;
    };
    if (limits.maximum_outputs == 0 || limits.maximum_total_output_bytes == 0 ||
        limits.maximum_filename_bytes < 16) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "Conversion limits must permit outputs, bytes, and bounded filenames");
    }
    if (request.target != format_family_t::stepmania && request.stepmania) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "StepMania export policy was supplied for a different target");
    }
    if (request.target != format_family_t::bms && request.encoding) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "Output text encoding is currently a BMS-family policy");
    }
    if (request.target == format_family_t::native_project &&
        (request.chart || !request.charts.empty())) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "Native project conversion preserves the whole project; chart selection "
                      "is not applicable");
    }
    if (!request.staged_assets.empty() && request.target != format_family_t::bms &&
        request.target != format_family_t::bmson && request.target != format_family_t::osu_mania &&
        request.target != format_family_t::stepmania) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "This target does not yet serialize staged resource paths");
    }
    if (request.staged_assets.size() > song.audio_assets.size() + song.visual_assets.size()) {
        return reject(conversion_diagnostic_code_t::invalid_request,
                      "Staged asset mapping exceeds the document asset count");
    }
    if (request.target == format_family_t::midi || request.target == format_family_t::quaver) {
        return reject(conversion_diagnostic_code_t::unsupported_target,
                      "The selected conversion target has no paired writer yet");
    }
    return true;
}

auto validate_selected_target(const song_document_t& song, const conversion_request_t& request,
                              const std::vector<chart_id_t>& charts,
                              conversion_limits_t limits, conversion_result_t& result) -> bool {
    if (charts.size() > limits.maximum_outputs && request.target != format_family_t::stepmania &&
        request.target != format_family_t::native_project) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::output_limit,
                       conversion_stage_t::request, request.target, request.chart,
                       "Conversion output count exceeds its configured limit");
        return false;
    }
    if (request.target == format_family_t::bms ||
        request.target == format_family_t::native_project || !contains_bms_tree(song, charts)) {
        return true;
    }
    if (request.branches != branch_projection_t::active_projection) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::invalid_request,
                       conversion_stage_t::request, request.target, request.chart,
                       "The target cannot preserve a BMS branch tree; select active projection "
                       "explicitly");
        return false;
    }
    const auto severity = request.loss_policy == loss_policy_t::allow
                              ? diagnostic_severity_t::info
                              : diagnostic_severity_t::warning;
    add_diagnostic(result, severity, conversion_diagnostic_code_t::target_diagnostic,
                   conversion_stage_t::transform, request.target, request.chart,
                   "BMS control flow will be flattened to its active materialized projection",
                   "branches", true);
    return true;
}

void append_conversion_output(conversion_result_t& result, const song_document_t& prepared,
                              const conversion_request_t& request, conversion_limits_t limits,
                              std::optional<chart_id_t> chart, std::string extension,
                              std::optional<std::string> bytes,
                              const chart_document_t* chart_document) {
    if (!bytes) {
        return;
    }
    const auto filename = filename_for(prepared, chart_document, extension, limits);
    if (!filename) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::output_limit,
                       conversion_stage_t::serialize, request.target, chart,
                       "Conversion filename cannot fit its configured byte limit");
        return;
    }
    result.outputs.push_back({request.target, chart, *filename, std::move(*bytes), false});
}

void write_native_output(const song_document_t& prepared, const conversion_request_t& request,
                         conversion_limits_t limits, conversion_result_t& result) {
    if (!request.target_profile.empty() && request.target_profile != "1") {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::unknown_target_profile,
                       conversion_stage_t::request, request.target, std::nullopt,
                       "Unknown native-project target profile: " + request.target_profile);
    }
    if (result_has_errors(result)) {
        return;
    }
    const auto written = write_native_project(prepared);
    append_target_diagnostics(result, written.diagnostics, request.target, std::nullopt,
                              request.loss_policy);
    append_conversion_output(result, prepared, request, limits, std::nullopt, ".wormedit",
                             written.bytes, nullptr);
}

void write_bms_outputs(const song_document_t& prepared, const conversion_request_t& request,
                       const std::vector<chart_id_t>& charts, conversion_limits_t limits,
                       conversion_result_t& result) {
    for (const auto chart_id : charts) {
        bms_write_options_t options;
        options.chart = chart_id;
        options.preserve_unchanged_source = request.staged_assets.empty();
        options.branch_policy = request.branches == branch_projection_t::preserve_tree
                                    ? bms_branch_policy_t::preserve_tree
                                    : bms_branch_policy_t::flatten_active;
        options.output_encoding = request.encoding;
        if (!request.target_profile.empty()) {
            options.lane_profile_id = request.target_profile;
        }
        const auto written = write_bms(prepared, std::move(options));
        append_target_diagnostics(result, written.diagnostics, request.target, chart_id,
                                  request.loss_policy);
        append_conversion_output(result, prepared, request, limits, chart_id,
                                 std::string(bms_extension(request.target_profile)), written.bytes,
                                 selected_chart(prepared, chart_id));
    }
}

auto resolve_stepmania_options(const conversion_request_t& request, conversion_result_t& result)
    -> std::optional<stepmania_export_policy_result_t> {
    if (!request.target_profile.empty()) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::unknown_target_profile,
                       conversion_stage_t::request, request.target, request.chart,
                       "StepMania target profiles use the typed export policy");
        return std::nullopt;
    }
    if (!request.stepmania) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::invalid_request,
                       conversion_stage_t::request, request.target, request.chart,
                       "StepMania conversion requires explicit file, syntax, and implementation "
                       "choices");
        return std::nullopt;
    }
    auto resolved = resolve_stepmania_export_policy(*request.stepmania);
    if (!resolved) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::unknown_target_profile,
                       conversion_stage_t::request, request.target, request.chart,
                       resolved.message);
        return std::nullopt;
    }
    return resolved;
}

auto write_stepmania_output(const song_document_t& prepared,
                            const conversion_request_t& request, conversion_limits_t limits,
                            conversion_result_t& result)
    -> std::optional<stepmania_export_policy_result_t> {
    auto resolved = resolve_stepmania_options(request, result);
    if (!resolved.has_value()) {
        return std::nullopt;
    }
    auto options = *resolved->options;
    options.chart = request.chart;
    const auto written = write_stepmania(prepared, std::move(options));
    append_target_diagnostics(result, written.diagnostics, request.target, request.chart,
                              request.loss_policy);
    const auto extension = request.stepmania->file_profile == stepmania_profile_t::sm ? ".sm"
                                                                                       : ".ssc";
    append_conversion_output(result, prepared, request, limits, request.chart, extension,
                             written.text,
                             request.chart ? selected_chart(prepared, *request.chart) : nullptr);
    return resolved;
}

auto osu_profile(const conversion_request_t& request, conversion_result_t& result)
    -> std::optional<osu_mania_file_profile_t> {
    if (request.target_profile.empty()) {
        return std::nullopt;
    }
    if (request.target_profile == "stable-v14") {
        return osu_mania_file_profile_t::stable_v14;
    }
    if (request.target_profile == "lazer-v128") {
        return osu_mania_file_profile_t::lazer_v128;
    }
    add_diagnostic(result, diagnostic_severity_t::error,
                   conversion_diagnostic_code_t::unknown_target_profile,
                   conversion_stage_t::request, request.target, request.chart,
                   "Unknown osu!mania target profile: " + request.target_profile);
    return std::nullopt;
}

void write_osu_outputs(const song_document_t& prepared, const conversion_request_t& request,
                       const std::vector<chart_id_t>& charts, conversion_limits_t limits,
                       conversion_result_t& result) {
    const auto profile = osu_profile(request, result);
    if (!request.target_profile.empty() && !profile.has_value()) {
        return;
    }
    for (const auto chart_id : charts) {
        const auto written = write_osu_mania(prepared, {.profile = profile, .chart = chart_id});
        append_target_diagnostics(result, written.diagnostics, request.target, chart_id,
                                  request.loss_policy);
        append_conversion_output(result, prepared, request, limits, chart_id, ".osu", written.text,
                                 selected_chart(prepared, chart_id));
    }
}

void write_bmson_outputs(const song_document_t& prepared, const conversion_request_t& request,
                         const std::vector<chart_id_t>& charts, conversion_limits_t limits,
                         conversion_result_t& result) {
    if (!request.target_profile.empty() && request.target_profile != "1.0") {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::unknown_target_profile,
                       conversion_stage_t::request, request.target, request.chart,
                       "Unknown BMSON target profile: " + request.target_profile);
        return;
    }
    for (const auto chart_id : charts) {
        const auto written = write_bmson(prepared, {.chart = chart_id});
        append_target_diagnostics(result, written.diagnostics, request.target, chart_id,
                                  request.loss_policy);
        append_conversion_output(result, prepared, request, limits, chart_id, ".bmson",
                                 written.json, selected_chart(prepared, chart_id));
    }
}

auto serialize_conversion(const song_document_t& prepared, const conversion_request_t& request,
                          const std::vector<chart_id_t>& charts, conversion_limits_t limits,
                          conversion_result_t& result)
    -> std::optional<stepmania_export_policy_result_t> {
    switch (request.target) {
    case format_family_t::native_project:
        write_native_output(prepared, request, limits, result);
        break;
    case format_family_t::bms:
        write_bms_outputs(prepared, request, charts, limits, result);
        break;
    case format_family_t::stepmania:
        return write_stepmania_output(prepared, request, limits, result);
    case format_family_t::osu_mania:
        write_osu_outputs(prepared, request, charts, limits, result);
        break;
    case format_family_t::bmson:
        write_bmson_outputs(prepared, request, charts, limits, result);
        break;
    case format_family_t::midi:
    case format_family_t::quaver:
        break;
    }
    return std::nullopt;
}

auto validate_conversion_outputs(conversion_result_t& result, const conversion_request_t& request,
                                 conversion_limits_t limits,
                                 const stepmania_export_policy_result_t* stepmania_options)
    -> bool {
    if (result_has_errors(result)) {
        result.outputs.clear();
        return false;
    }
    if (request.loss_policy == loss_policy_t::reject &&
        std::ranges::any_of(result.diagnostics, &conversion_diagnostic_t::loss)) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::loss_rejected,
                       conversion_stage_t::serialize, request.target, request.chart,
                       "Conversion produced a loss diagnostic under the reject policy");
        result.outputs.clear();
        return false;
    }
    if (result.outputs.empty()) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::target_diagnostic,
                       conversion_stage_t::serialize, request.target, request.chart,
                       "Target writer produced no conversion output");
        return false;
    }
    std::size_t total_bytes{};
    for (const auto& output : result.outputs) {
        if (output.bytes.size() > limits.maximum_total_output_bytes - total_bytes) {
            add_diagnostic(result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::output_limit,
                           conversion_stage_t::serialize, request.target, output.chart,
                           "Conversion output set exceeds its configured byte limit");
            result.outputs.clear();
            return false;
        }
        total_bytes += output.bytes.size();
    }
    for (auto& output : result.outputs) {
        if (!validate_reimport(output, request, stepmania_options, result)) {
            result.outputs.clear();
            return false;
        }
        output.reimport_validated = true;
    }
    return true;
}

} // namespace

auto conversion_result_t::has_errors() const -> bool {
    return result_has_errors(*this);
}

conversion_result_t::operator bool() const noexcept {
    return !outputs.empty() && !std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
               return diagnostic.severity == diagnostic_severity_t::error;
           });
}

auto plan_conversion_slice_spans(const song_document_t& song, chart_id_t chart,
    std::span<const slice_span_source_t> sources, const slice_span_options_t& options) -> slice_span_plan_t {
    return build_slice_span_plan(song, chart, sources, options);
}

auto convert_document(const song_document_t& song, const conversion_request_t& request,
                      conversion_limits_t limits) -> conversion_result_t {
    conversion_result_t result;
    if (!validate_request_policies(song, request, limits, result)) {
        return result;
    }
    const auto charts = selected_chart_ids(song, request, result);
    if (result_has_errors(result) || !validate_selected_target(song, request, charts, limits, result)) {
        return result;
    }
    auto prepared = prepare_document(song, request, charts, result);
    if (!prepared || result_has_errors(result)) {
        result.outputs.clear();
        return result;
    }
    auto stepmania_options =
        serialize_conversion(*prepared, request, charts, limits, result);
    if (!validate_conversion_outputs(result, request, limits,
                                     stepmania_options ? &*stepmania_options : nullptr)) {
        return result;
    }
    return result;
}

} // namespace wormedit

#include <wormedit/formats/conversion.h>

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
                       conversion_diagnostic_code_t::chart_not_found,
                       conversion_stage_t::request, request.target, std::nullopt,
                       "Conversion requires at least one chart");
    return ids;
}

auto slug(std::string_view source, std::string_view fallback, std::size_t capacity)
    -> std::string {
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

auto prepare_document(const song_document_t& song, const conversion_request_t& request,
                      const std::vector<chart_id_t>& charts, conversion_result_t& result)
    -> std::optional<song_document_t> {
    auto prepared = song;
    if (!request.quantization)
        return prepared;
    if (!request.quantization->objects.empty() && charts.size() != 1) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::invalid_request,
                       conversion_stage_t::transform, request.target, std::nullopt,
                       "Object-scoped quantization requires one selected chart");
        return std::nullopt;
    }
    for (const auto chart_id : charts) {
        const auto found = std::ranges::find(prepared.charts, chart_id, &chart_document_t::id);
        if (found == prepared.charts.end()) {
            add_diagnostic(result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::chart_not_found,
                           conversion_stage_t::transform, request.target, chart_id,
                           "Quantization chart was not found in the detached document");
            return std::nullopt;
        }
        auto quantized = quantize_chart_exact(*found, *request.quantization);
        for (const auto& diagnostic : quantized.diagnostics)
            add_diagnostic(result, diagnostic.severity,
                           conversion_diagnostic_code_t::transform_failed,
                           conversion_stage_t::transform, request.target, chart_id,
                           diagnostic.message);
        if (!quantized)
            return std::nullopt;
        const auto moved = quantized.resolution_moves.size() + quantized.quantized_moves.size();
        *found = std::move(*quantized.chart);
        add_diagnostic(result, diagnostic_severity_t::info,
                       conversion_diagnostic_code_t::target_diagnostic,
                       conversion_stage_t::transform, request.target, chart_id,
                       "Quantization prepared " + std::to_string(moved) + " exact movement(s)");
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

} // namespace

auto conversion_result_t::has_errors() const -> bool {
    return result_has_errors(*this);
}

conversion_result_t::operator bool() const noexcept {
    return !outputs.empty() && !std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
               return diagnostic.severity == diagnostic_severity_t::error;
           });
}

auto convert_document(const song_document_t& song, const conversion_request_t& request,
                      conversion_limits_t limits) -> conversion_result_t {
    conversion_result_t result;
    if (limits.maximum_outputs == 0 || limits.maximum_total_output_bytes == 0 ||
        limits.maximum_filename_bytes < 16) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::invalid_request,
                       conversion_stage_t::request, request.target, request.chart,
                       "Conversion limits must permit outputs, bytes, and bounded filenames");
        return result;
    }
    if (request.target != format_family_t::stepmania && request.stepmania) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::invalid_request,
                       conversion_stage_t::request, request.target, request.chart,
                       "StepMania export policy was supplied for a different target");
        return result;
    }
    if (request.target != format_family_t::bms && request.encoding) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::invalid_request,
                       conversion_stage_t::request, request.target, request.chart,
                       "Output text encoding is currently a BMS-family policy");
        return result;
    }
    if (request.target == format_family_t::native_project && request.chart) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::invalid_request,
                       conversion_stage_t::request, request.target, request.chart,
                       "Native project conversion preserves the whole project; chart selection "
                       "is not applicable");
        return result;
    }
    if (request.target == format_family_t::midi || request.target == format_family_t::quaver) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::unsupported_target,
                       conversion_stage_t::request, request.target, request.chart,
                       "The selected conversion target has no paired writer yet");
        return result;
    }

    const auto charts = selected_chart_ids(song, request, result);
    if (result_has_errors(result))
        return result;
    if (charts.size() > limits.maximum_outputs &&
        request.target != format_family_t::stepmania &&
        request.target != format_family_t::native_project) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::output_limit,
                       conversion_stage_t::request, request.target, request.chart,
                       "Conversion output count exceeds its configured limit");
        return result;
    }
    if (request.target != format_family_t::bms &&
        request.target != format_family_t::native_project && contains_bms_tree(song, charts)) {
        if (request.branches != branch_projection_t::active_projection) {
            add_diagnostic(result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::invalid_request,
                           conversion_stage_t::request, request.target, request.chart,
                           "The target cannot preserve a BMS branch tree; select active "
                           "projection explicitly");
            return result;
        }
        auto severity = request.loss_policy == loss_policy_t::allow
                            ? diagnostic_severity_t::info
                            : diagnostic_severity_t::warning;
        add_diagnostic(result, severity, conversion_diagnostic_code_t::target_diagnostic,
                       conversion_stage_t::transform, request.target, request.chart,
                       "BMS control flow will be flattened to its active materialized projection",
                       "branches", true);
    }

    auto prepared = prepare_document(song, request, charts, result);
    if (!prepared || result_has_errors(result)) {
        result.outputs.clear();
        return result;
    }

    std::optional<stepmania_export_policy_result_t> resolved_stepmania;
    const auto append_output = [&](std::optional<chart_id_t> chart, std::string extension,
                                   std::optional<std::string> bytes,
                                   const chart_document_t* chart_document) {
        if (!bytes)
            return;
        const auto filename = filename_for(*prepared, chart_document, extension, limits);
        if (!filename) {
            add_diagnostic(result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::output_limit,
                           conversion_stage_t::serialize, request.target, chart,
                           "Conversion filename cannot fit its configured byte limit");
            return;
        }
        result.outputs.push_back(
            {request.target, chart, *filename, std::move(*bytes), false});
    };

    switch (request.target) {
    case format_family_t::native_project: {
        if (!request.target_profile.empty() && request.target_profile != "1")
            add_diagnostic(result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::unknown_target_profile,
                           conversion_stage_t::request, request.target, std::nullopt,
                           "Unknown native-project target profile: " + request.target_profile);
        if (result_has_errors(result))
            break;
        const auto written = write_native_project(*prepared);
        append_target_diagnostics(result, written.diagnostics, request.target, std::nullopt,
                                  request.loss_policy);
        append_output(std::nullopt, ".wormedit", written.bytes, nullptr);
        break;
    }
    case format_family_t::bms: {
        for (const auto chart_id : charts) {
            bms_write_options_t options;
            options.chart = chart_id;
            options.branch_policy = request.branches == branch_projection_t::preserve_tree
                                        ? bms_branch_policy_t::preserve_tree
                                        : bms_branch_policy_t::flatten_active;
            options.output_encoding = request.encoding;
            if (!request.target_profile.empty())
                options.lane_profile_id = request.target_profile;
            const auto written = write_bms(*prepared, std::move(options));
            append_target_diagnostics(result, written.diagnostics, request.target, chart_id,
                                      request.loss_policy);
            append_output(chart_id, std::string(bms_extension(request.target_profile)),
                          written.bytes, selected_chart(*prepared, chart_id));
        }
        break;
    }
    case format_family_t::stepmania: {
        if (!request.target_profile.empty()) {
            add_diagnostic(result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::unknown_target_profile,
                           conversion_stage_t::request, request.target, request.chart,
                           "StepMania target profiles use the typed export policy");
            break;
        }
        if (!request.stepmania) {
            add_diagnostic(result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::invalid_request,
                           conversion_stage_t::request, request.target, request.chart,
                           "StepMania conversion requires explicit file, syntax, and "
                           "implementation choices");
            break;
        }
        resolved_stepmania = resolve_stepmania_export_policy(*request.stepmania);
        if (!*resolved_stepmania) {
            add_diagnostic(result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::unknown_target_profile,
                           conversion_stage_t::request, request.target, request.chart,
                           resolved_stepmania->message);
            break;
        }
        auto options = *resolved_stepmania->options;
        options.chart = request.chart;
        const auto written = write_stepmania(*prepared, std::move(options));
        append_target_diagnostics(result, written.diagnostics, request.target, request.chart,
                                  request.loss_policy);
        const auto extension = request.stepmania->file_profile == stepmania_profile_t::sm
                                   ? ".sm"
                                   : ".ssc";
        append_output(request.chart, extension, written.text,
                      request.chart ? selected_chart(*prepared, *request.chart) : nullptr);
        break;
    }
    case format_family_t::osu_mania: {
        std::optional<osu_mania_file_profile_t> profile;
        if (request.target_profile.empty()) {
            profile.reset();
        } else if (request.target_profile == "stable-v14") {
            profile = osu_mania_file_profile_t::stable_v14;
        } else if (request.target_profile == "lazer-v128") {
            profile = osu_mania_file_profile_t::lazer_v128;
        } else {
            add_diagnostic(result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::unknown_target_profile,
                           conversion_stage_t::request, request.target, request.chart,
                           "Unknown osu!mania target profile: " + request.target_profile);
            break;
        }
        for (const auto chart_id : charts) {
            const auto written = write_osu_mania(
                *prepared, {.profile = profile, .chart = chart_id});
            append_target_diagnostics(result, written.diagnostics, request.target, chart_id,
                                      request.loss_policy);
            append_output(chart_id, ".osu", written.text,
                          selected_chart(*prepared, chart_id));
        }
        break;
    }
    case format_family_t::bmson: {
        if (!request.target_profile.empty() && request.target_profile != "1.0") {
            add_diagnostic(result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::unknown_target_profile,
                           conversion_stage_t::request, request.target, request.chart,
                           "Unknown BMSON target profile: " + request.target_profile);
            break;
        }
        for (const auto chart_id : charts) {
            const auto written = write_bmson(*prepared, {.chart = chart_id});
            append_target_diagnostics(result, written.diagnostics, request.target, chart_id,
                                      request.loss_policy);
            append_output(chart_id, ".bmson", written.json,
                          selected_chart(*prepared, chart_id));
        }
        break;
    }
    case format_family_t::midi:
    case format_family_t::quaver:
        break;
    }

    if (result_has_errors(result)) {
        result.outputs.clear();
        return result;
    }
    if (request.loss_policy == loss_policy_t::reject &&
        std::ranges::any_of(result.diagnostics, &conversion_diagnostic_t::loss)) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::loss_rejected,
                       conversion_stage_t::serialize, request.target, request.chart,
                       "Conversion produced a loss diagnostic under the reject policy");
        result.outputs.clear();
        return result;
    }
    if (result.outputs.empty()) {
        add_diagnostic(result, diagnostic_severity_t::error,
                       conversion_diagnostic_code_t::target_diagnostic,
                       conversion_stage_t::serialize, request.target, request.chart,
                       "Target writer produced no conversion output");
        return result;
    }
    std::size_t total_bytes{};
    for (const auto& output : result.outputs) {
        if (output.bytes.size() > limits.maximum_total_output_bytes - total_bytes) {
            add_diagnostic(result, diagnostic_severity_t::error,
                           conversion_diagnostic_code_t::output_limit,
                           conversion_stage_t::serialize, request.target, output.chart,
                           "Conversion output set exceeds its configured byte limit");
            result.outputs.clear();
            return result;
        }
        total_bytes += output.bytes.size();
    }
    for (auto& output : result.outputs) {
        if (!validate_reimport(output, request,
                               resolved_stepmania ? &*resolved_stepmania : nullptr, result)) {
            result.outputs.clear();
            return result;
        }
        output.reimport_validated = true;
    }
    return result;
}

} // namespace wormedit

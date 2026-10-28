// Headless chart converter: reads a JSON request, runs the editor's conversion
// service, and prints a JSON result. Exit code 0 only when every artifact was
// written; invalid requests and rejected conversions write nothing.
#include "sys/project/project_open.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/formats/export_cut_audio.h>
#endif
#include <wormedit/formats/conversion.h>
#include <wormedit/formats/bms_writer.h>
#include <wormedit/formats/conversion_publication.h>

#if defined(WORMEDIT_HAS_LUA)
#include "scripting/lua_curve.h"
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using json = nlohmann::json;
using namespace wormedit;

constexpr std::size_t maximum_request_bytes = 1U * 1024U * 1024U;

struct request_error_t {
    std::string message;
};

struct cli_request_t {
    std::filesystem::path input;
    std::optional<std::string> input_bms_lane_profile;
    std::filesystem::path output_directory;
    conversion_request_t conversion;
    std::vector<std::size_t> chart_indices;
};

template <typename value_t>
auto lookup(const std::map<std::string, value_t, std::less<>>& table, std::string_view name,
            std::string_view field) -> value_t {
    const auto found = table.find(name);
    if (found == table.end()) {
        throw request_error_t{"unknown value '" + std::string(name) + "' for " +
                              std::string(field)};
    }
    return found->second;
}

auto string_field(const json& object, const char* key, bool required = false) -> std::string {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) {
        if (required) throw request_error_t{std::string("missing field '") + key + "'"};
        return {};
    }
    if (!found->is_string()) throw request_error_t{std::string("field '") + key + "' must be a string"};
    return found->get<std::string>();
}

auto is_field_true(const json& object, const char* key) -> bool {
    const auto found = object.find(key);
    if (found == object.end()) return false;
    if (!found->is_boolean()) throw request_error_t{std::string("field '") + key + "' must be a boolean"};
    return found->get<bool>();
}

auto target_family(std::string_view name) -> format_family_t {
    static const std::map<std::string, format_family_t, std::less<>> table{
        {"native_project", format_family_t::native_project},
        {"bms", format_family_t::bms},
        {"stepmania", format_family_t::stepmania},
        {"osu_mania", format_family_t::osu_mania},
        {"bmson", format_family_t::bmson},
    };
    return lookup(table, name, "target");
}

auto parse_encoding(const std::string& name) -> std::optional<text_encoding_t> {
    if (name.empty()) return std::nullopt;
    static const std::map<std::string, text_encoding_t, std::less<>> table{
        {"ascii", text_encoding_t::ascii},       {"utf8", text_encoding_t::utf8},
        {"cp932", text_encoding_t::cp932},       {"euc_kr", text_encoding_t::euc_kr},
        {"cp949", text_encoding_t::cp949},       {"windows_1252", text_encoding_t::windows_1252},
    };
    return lookup(table, name, "encoding");
}

auto parse_stepmania_policy(const json& object) -> stepmania_export_policy_t {
    static const std::map<std::string, stepmania_profile_t, std::less<>> files{
        {"sm", stepmania_profile_t::sm}, {"ssc", stepmania_profile_t::ssc}};
    static const std::map<std::string, stepmania_timing_syntax_t, std::less<>> syntaxes{
        {"unspecified", stepmania_timing_syntax_t::unspecified},
        {"native_warps", stepmania_timing_syntax_t::native_warps},
        {"legacy_negative_bpm", stepmania_timing_syntax_t::legacy_negative_bpm},
        {"legacy_negative_stop", stepmania_timing_syntax_t::legacy_negative_stop}};
    static const std::map<std::string, stepmania_implementation_profile_t, std::less<>> profiles{
        {"unspecified", stepmania_implementation_profile_t::unspecified},
        {"stepmania_5_1", stepmania_implementation_profile_t::stepmania_5_1},
        {"etterna_0_75_1", stepmania_implementation_profile_t::etterna_0_75_1}};
    if (!object.is_object()) throw request_error_t{"field 'stepmania' must be an object"};
    stepmania_export_policy_t policy;
    if (const auto name = string_field(object, "file_profile"); !name.empty())
        policy.file_profile = lookup(files, name, "stepmania.file_profile");
    if (const auto name = string_field(object, "timing_syntax"); !name.empty())
        policy.timing_syntax = lookup(syntaxes, name, "stepmania.timing_syntax");
    if (const auto name = string_field(object, "implementation_profile"); !name.empty())
        policy.implementation_profile = lookup(profiles, name, "stepmania.implementation_profile");
    return policy;
}

void parse_policies(const json& object, conversion_request_t& conversion) {
    static const std::map<std::string, loss_policy_t, std::less<>> losses{
        {"reject", loss_policy_t::reject}, {"warn", loss_policy_t::warn},
        {"allow", loss_policy_t::allow}};
    static const std::map<std::string, branch_projection_t, std::less<>> branches{
        {"preserve_tree", branch_projection_t::preserve_tree},
        {"active_projection", branch_projection_t::active_projection}};
    if (const auto name = string_field(object, "loss_policy"); !name.empty())
        conversion.loss_policy = lookup(losses, name, "loss_policy");
    if (const auto name = string_field(object, "branches"); !name.empty())
        conversion.branches = lookup(branches, name, "branches");
    conversion.encoding = parse_encoding(string_field(object, "encoding"));
    if (const auto name = string_field(object, "bms_profile"); !name.empty()) {
        const auto profile = bms_write_options_t::from_profile(name);
        if (!profile) throw request_error_t{"unknown BMS player profile: " + name};
        conversion.bms_profile = profile->profile;
    }
    conversion.overwrite = is_field_true(object, "overwrite");
    if (const auto found = object.find("stepmania"); found != object.end() && !found->is_null())
        conversion.stepmania = parse_stepmania_policy(*found);
}

auto parse_chart_indices(const json& object) -> std::vector<std::size_t> {
    std::vector<std::size_t> indices;
    const auto found = object.find("charts");
    if (found == object.end() || found->is_null()) return indices;
    if (!found->is_array()) throw request_error_t{"field 'charts' must be an array of indices"};
    for (const auto& item : *found) {
        if (!item.is_number_unsigned()) {
            throw request_error_t{"field 'charts' must hold zero-based chart indices"};
        }
        indices.push_back(item.get<std::size_t>());
    }
    return indices;
}

auto parse_request(const json& object) -> cli_request_t {
    if (!object.is_object()) throw request_error_t{"the request must be a JSON object"};
    cli_request_t request;
    request.input = string_field(object, "input", true);
    if (auto profile = string_field(object, "input_bms_lane_profile"); !profile.empty())
        request.input_bms_lane_profile = std::move(profile);
    request.output_directory = string_field(object, "output_directory", true);
    request.conversion.target = target_family(string_field(object, "target", true));
    request.conversion.target_profile = string_field(object, "target_profile");
    request.chart_indices = parse_chart_indices(object);
    parse_policies(object, request.conversion);
    if (const auto audio = object.find("audio"); audio != object.end()) {
        if (!audio->is_object()) throw request_error_t{"audio must be an object"};
        const auto codec = string_field(*audio, "codec");
        if (codec == "ogg") request.conversion.audio.codec = export_audio_codec_t::ogg;
        else if (!codec.empty() && codec != "mp3") throw request_error_t{"audio codec must be mp3 or ogg"};
        request.conversion.audio.mp3_bitrate_kbps = audio->value("mp3_bitrate_kbps", 0U);
        request.conversion.audio.ogg_quality = audio->value("ogg_quality", 0.4F);
        if (!request.conversion.audio.is_valid()) throw request_error_t{"invalid audio encoding settings"};
    }
    return request;
}

auto severity_name(diagnostic_severity_t severity) -> const char* {
    return severity == diagnostic_severity_t::error ? "error" : "warning";
}

auto diagnostic_json(const conversion_diagnostic_t& diagnostic) -> json {
    return {{"severity", severity_name(diagnostic.severity)},
            {"code", static_cast<int>(diagnostic.code)},
            {"stage", static_cast<int>(diagnostic.stage)},
            {"loss", diagnostic.loss},
            {"message", diagnostic.message},
            {"path", diagnostic.path}};
}

auto fail_json(const std::string& message) -> json {
    return {{"ok", false}, {"error", message}, {"artifacts", json::array()},
            {"diagnostics", json::array()}};
}

auto read_text(const std::filesystem::path& path, std::size_t limit) -> std::optional<std::string> {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    std::string text(limit + 1, '\0');
    stream.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(stream.gcount()));
    if (text.size() > limit) return std::nullopt;
    return text;
}

auto select_charts(const song_document_t& song, const std::vector<std::size_t>& indices,
                   conversion_request_t& conversion) -> std::optional<std::string> {
    const auto charts = song.read_charts();
    for (const auto index : indices) {
        if (index >= charts.size()) {
            return "chart index " + std::to_string(index) + " is out of range";
        }
        conversion.charts.push_back(charts[index]->id);
    }
    return std::nullopt;
}

auto destination_conflicts(const conversion_result_t& converted,
                           const std::filesystem::path& directory, bool overwrite)
    -> std::optional<std::string> {
    std::vector<std::string> names;
    for (const auto& output : converted.outputs) {
        const auto name = std::filesystem::path(output.suggested_filename).filename().string();
        if (name.empty() || name != output.suggested_filename) {
            return "conversion produced an unsafe output file name";
        }
        if (std::ranges::find(names, name) != names.end()) {
            return "two outputs share the file name '" + name + "'";
        }
        names.push_back(name);
        if (!overwrite && std::filesystem::exists(directory / name)) {
            return "'" + name + "' already exists and overwrite is false";
        }
    }
    return std::nullopt;
}

auto write_outputs(const conversion_result_t& converted, const std::filesystem::path& directory,
                   json& artifacts) -> std::optional<std::string> {
    for (const auto& output : converted.outputs) {
        const auto path = directory / output.suggested_filename;
        const auto temporary = path.string() + ".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            stream.write(output.bytes.data(), static_cast<std::streamsize>(output.bytes.size()));
            if (!stream) return "could not write '" + temporary + "'";
        }
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error) return "could not publish '" + path.string() + "': " + error.message();
        artifacts.push_back({{"path", path.string()},
                             {"filename", output.suggested_filename},
                             {"bytes", output.bytes.size()},
                             {"reimport_validated", output.reimport_validated}});
    }
    return std::nullopt;
}

auto prepare_cli_slices(const song_document_t& song, cli_request_t& request) -> std::optional<std::string> {
#ifdef WORMEDIT_HAS_AUDIO_DECODER
    if (!song.export_cuts.empty()) {
        auto sources = prepare_export_cut_sources(song, request.input.parent_path());
        if (!sources.ok()) return sources.diagnostics.front().message;
        request.conversion.export_cut_sources = std::move(sources.value);
    }
    if (!is_conversion_needs_slice_spans(song, request.conversion)) return std::nullopt;
    return prepare_conversion_slice_sources(song, request.conversion, request.input.parent_path());
#else
    // The pure planner accepts soundless BMSON; audible sources reject missing geometry.
    return std::nullopt;
#endif
}

auto publish_cli_slices(const song_document_t& song, const cli_request_t& request,
    const conversion_result_t& converted, json& artifacts) -> std::optional<std::string> {
#ifdef WORMEDIT_HAS_AUDIO_DECODER
    const auto directory = std::filesystem::absolute(request.output_directory).lexically_normal();
    output_group_t group;
    group.family = request.conversion.target;
    group.relative_output_path = directory.filename();
    group.destination_kind = output_destination_kind_t::directory;
    conversion_publication_options_t options;
    options.destination_is_directory = true;
    options.stage_generated_files = [&](const auto& staging) {
        return render_conversion_slice_plans(song, converted,
            {request.input.parent_path(), staging, {}});
    };
    const auto published = create_conversion_group(directory.parent_path(), group,
        request.conversion, converted, std::move(options));
    if (!published.value.published) return published.diagnostics.errors.empty() ? "Atomic publication failed" : published.diagnostics.errors.front();
    for (const auto& output : converted.outputs)
        artifacts.push_back({{"path", (directory / output.suggested_filename).string()},
            {"filename", output.suggested_filename}, {"bytes", output.bytes.size()},
            {"reimport_validated", output.reimport_validated}});
    return std::nullopt;
#else
    (void)song;
    if (!converted.slice_plans.empty() || !converted.cut_audio_plans.empty() || !converted.asset_copies.empty() || !converted.silent_files.empty())
        return "BMSON sliced-audio conversion requires the audio decoder build";
    return write_outputs(converted, request.output_directory, artifacts);
#endif
}

auto run_request(const json& body) -> std::pair<int, json> {
    auto request = parse_request(body);
    const auto opened = sys::open_project_file(request.input, {}, {}, nullptr, request.input_bms_lane_profile);
    if (!opened.is_success()) return {2, fail_json(opened.error.empty() ? "could not open the input" : opened.error)};
    if (const auto message = select_charts(*opened.document, request.chart_indices,
                                           request.conversion)) {
        return {2, fail_json(*message)};
    }
    std::error_code error;
    if (!std::filesystem::is_directory(request.output_directory, error)) {
        return {2, fail_json("output_directory must be an existing directory")};
    }
    if (auto failure = prepare_cli_slices(*opened.document, request)) return {1, fail_json(*failure)};
#if defined(WORMEDIT_HAS_LUA)
    scripting::lua_curve_backend_t scripts;
    sv_recipe_evaluation_scope_t script_scope{scripts};
#endif
    const auto converted = convert_document(*opened.document, request.conversion);
    json result{{"ok", false}, {"artifacts", json::array()}, {"diagnostics", json::array()},
                {"import_warnings", opened.warnings}};
    for (const auto& diagnostic : converted.diagnostics) {
        result["diagnostics"].push_back(diagnostic_json(diagnostic));
    }
    if (!converted) return {1, result};
    if (const auto conflict = destination_conflicts(converted, request.output_directory,
                                                    request.conversion.overwrite)) {
        result["error"] = *conflict;
        return {1, result};
    }
    const auto failure = (is_conversion_needs_slice_spans(*opened.document, request.conversion) ||
                          !converted.cut_audio_plans.empty() || !converted.asset_copies.empty())
        ? publish_cli_slices(*opened.document, request, converted, result["artifacts"])
        : write_outputs(converted, request.output_directory, result["artifacts"]);
    if (failure) {
        result["error"] = *failure;
        return {1, result};
    }
    result["ok"] = true;
    return {0, result};
}

auto load_body(int argc, char** argv) -> std::pair<int, json> {
    if (argc != 2) return {2, fail_json("usage: wormedit-convert <request.json | ->")};
    std::optional<std::string> text;
    if (std::string_view(argv[1]) == "-") {
        text = std::string(std::istreambuf_iterator<char>(std::cin), {});
        if (text->size() > maximum_request_bytes) text.reset();
    } else {
        text = read_text(argv[1], maximum_request_bytes);
    }
    if (!text) return {2, fail_json("the request could not be read or exceeds 1 MiB")};
    auto body = json::parse(*text, nullptr, false);
    if (body.is_discarded()) return {2, fail_json("the request is not valid JSON")};
    return {0, std::move(body)};
}

} // namespace

int main(int argc, char** argv) {
    try {
        auto [status, body] = load_body(argc, argv);
        if (status == 0) std::tie(status, body) = run_request(body);
        std::cout << body.dump(2) << '\n';
        return status;
    } catch (const request_error_t& error) {
        std::cout << fail_json(error.message).dump(2) << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cout << fail_json(std::string("internal error: ") + error.what()).dump(2) << '\n';
        return 3;
    }
}

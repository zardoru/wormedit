#include "sys/project_open.h"

#include <wormedit/formats/bms.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/formats/stepmania.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace wormedit::sys {
namespace {

constexpr std::size_t chart_source_limit = 16U * 1024U * 1024U;
constexpr std::size_t native_source_limit = 64U * 1024U * 1024U;

auto lowercase_extension(const std::filesystem::path& path) -> std::string {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension;
}

auto supported_source_limit(std::string_view extension) -> std::size_t {
    if (extension == ".wormedit") return native_source_limit;
    if (extension == ".bms" || extension == ".bme" || extension == ".bml" ||
        extension == ".pms" || extension == ".bmson" || extension == ".sm" ||
        extension == ".ssc" || extension == ".osu")
        return chart_source_limit;
    return 0;
}

auto read_bounded_file(const std::filesystem::path& path, std::size_t limit,
                       std::string& error_message) -> std::optional<std::string> {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        error_message = error ? "Could not inspect the selected file: " + error.message()
                           : "The selected path is not a regular file.";
        return std::nullopt;
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        error_message = "Could not measure the selected file: " + error.message();
        return std::nullopt;
    }
    if (size > limit) {
        error_message = "The selected file exceeds the supported source size limit.";
        return std::nullopt;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error_message = "Could not open the selected file for reading.";
        return std::nullopt;
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!bytes.empty() && !input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        error_message = "Could not read the complete selected file.";
        return std::nullopt;
    }
    if (input.peek() != std::char_traits<char>::eof()) {
        error_message = "The selected file changed while it was being read.";
        return std::nullopt;
    }
    if (input.bad()) {
        error_message = "Could not finish reading the selected file.";
        return std::nullopt;
    }
    return bytes;
}

template <typename ParseResult>
void take_parsed_document(ParseResult parsed, std::string_view format,
                          project_open_result_t& result) {
    if (parsed.song && !parsed.has_errors()) {
        result.document = std::move(parsed.song);
        return;
    }
    const auto error = std::find_if(parsed.diagnostics.begin(), parsed.diagnostics.end(),
                                    [](const auto& item) {
                                        return item.severity == diagnostic_severity_t::error;
                                    });
    const auto detail = error != parsed.diagnostics.end()
                            ? error->message
                            : parsed.diagnostics.empty() ? "No document was produced."
                                                         : parsed.diagnostics.front().message;
    result.error = std::string(format) + " could not be opened: " + detail;
}

void parse_selected_file(std::string_view extension, const std::string& source,
                         project_open_result_t& result) {
    if (extension == ".wormedit") {
        take_parsed_document(parse_native_project(source), "Native project", result);
        result.native_project = result.document.has_value();
        return;
    }
    const auto bytes = std::as_bytes(std::span{source.data(), source.size()});
    if (extension == ".bms" || extension == ".bme" || extension == ".bml" ||
        extension == ".pms") {
        auto options = bms_parse_options_t{};
        options.file_extension = std::string(extension);
        take_parsed_document(parse_bms_bytes(bytes, std::move(options)), "BMS chart", result);
    } else if (extension == ".bmson") {
        auto options = bmson_parse_options_t{};
        options.asset_base_directory = result.asset_base;
        take_parsed_document(parse_bmson_bytes(bytes, std::move(options)), "BMSON chart", result);
    } else if (extension == ".sm" || extension == ".ssc") {
        auto options = stepmania_parse_options_t{};
        options.profile = extension == ".ssc" ? stepmania_profile_t::ssc
                                                : stepmania_profile_t::sm;
        take_parsed_document(parse_stepmania_bytes(bytes, std::move(options)),
                             "StepMania chart", result);
    } else if (extension == ".osu") {
        auto options = osu_mania_parse_options_t{};
        options.asset_base_directory = result.asset_base;
        take_parsed_document(parse_osu_mania_bytes(bytes, std::move(options)), "osu!mania chart",
                             result);
    }
}

} // namespace

auto open_project_file(const std::filesystem::path& selected_path) -> project_open_result_t {
    project_open_result_t result;
    if (selected_path.empty()) {
        result.error = "Choose a project or chart file.";
        return result;
    }
    const auto extension = lowercase_extension(selected_path);
    const auto limit = supported_source_limit(extension);
    if (limit == 0) {
        result.error = "Unsupported project or chart file extension.";
        return result;
    }
    std::error_code error;
    result.source_path = std::filesystem::absolute(selected_path, error);
    if (error) {
        result.error = "Could not resolve the selected file path: " + error.message();
        return result;
    }
    result.asset_base = result.source_path.parent_path();
    auto source = read_bounded_file(result.source_path, limit, result.error);
    if (source) parse_selected_file(extension, *source, result);
    return result;
}

} // namespace wormedit::sys

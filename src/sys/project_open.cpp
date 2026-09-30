#include "sys/project_open.h"

#include <wormedit/formats/bms.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/formats/stepmania.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

auto utf8_path(const std::filesystem::path& path) -> std::string {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

auto ascii_lower(std::string value) -> std::string {
    std::transform(value.begin(), value.end(), value.begin(), [](char character) {
        return character >= 'A' && character <= 'Z'
                   ? static_cast<char>(character - 'A' + 'a')
                   : character;
    });
    return value;
}

struct bms_audio_directory_inventory_t {
    std::map<std::string, std::vector<std::filesystem::path>> directories;
    std::map<std::pair<std::string, std::string>, std::vector<std::filesystem::path>> files;
};

using bms_audio_directory_cache_t =
    std::map<std::filesystem::path, bms_audio_directory_inventory_t>;

auto directory_inventory(bms_audio_directory_cache_t& cache,
                         const std::filesystem::path& directory)
    -> const bms_audio_directory_inventory_t& {
    auto [position, inserted] = cache.try_emplace(directory);
    if (!inserted) return position->second;
    auto& inventory = position->second;
    std::error_code error;
    for (std::filesystem::directory_iterator entry(directory, error), end; !error && entry != end;
         entry.increment(error)) {
        std::error_code type_error;
        const auto path = entry->path();
        if (entry->is_directory(type_error) && !type_error) {
            inventory.directories[ascii_lower(utf8_path(path.filename()))].push_back(path);
        } else if (entry->is_regular_file(type_error) && !type_error) {
            inventory.files[{ascii_lower(utf8_path(path.stem())),
                             ascii_lower(utf8_path(path.extension()))}]
                .push_back(path);
        }
    }
    for (auto& [name, paths] : inventory.directories) std::sort(paths.begin(), paths.end());
    for (auto& [name, paths] : inventory.files) std::sort(paths.begin(), paths.end());
    return inventory;
}

auto matching_directories(bms_audio_directory_cache_t& cache,
                          const std::filesystem::path& directory, const std::string& name)
    -> const std::vector<std::filesystem::path>& {
    const auto& entries = directory_inventory(cache, directory).directories;
    const auto found = entries.find(name);
    static const std::vector<std::filesystem::path> empty;
    return found == entries.end() ? empty : found->second;
}

auto candidate_list(const std::vector<std::filesystem::path>& candidates) -> std::string {
    std::string list;
    for (const auto& candidate : candidates) {
        if (!list.empty()) list += ", ";
        list += utf8_path(candidate);
    }
    return list;
}

auto resolved_audio_directory(const std::filesystem::path& source,
                              const std::filesystem::path& asset_base,
                              bms_audio_directory_cache_t& cache,
                              std::string& warning) -> std::optional<std::filesystem::path> {
    if (source.empty() || source.is_absolute()) {
        warning = "Unsafe BMS audio path: " + utf8_path(source);
        return std::nullopt;
    }
    auto directory = asset_base;
    for (const auto& part : source.parent_path()) {
        if (part == ".") continue;
        if (part == "..") {
            warning = "Unsafe BMS audio path: " + utf8_path(source);
            return std::nullopt;
        }
        const auto folded_name = ascii_lower(utf8_path(part));
        const auto& matches = matching_directories(cache, directory, folded_name);
        const auto exact = std::find_if(matches.begin(), matches.end(), [&](const auto& match) {
            return match.filename() == part;
        });
        if (exact != matches.end()) {
            directory = *exact;
            continue;
        }
        if (matches.size() > 1) {
            warning = "Ambiguous BMS audio directory for " + utf8_path(source) + ": " +
                      candidate_list(matches);
            return std::nullopt;
        }
        if (matches.empty()) return std::nullopt;
        directory = matches.front();
    }
    return directory;
}

auto extension_matches(bms_audio_directory_cache_t& cache,
                       const std::filesystem::path& directory, const std::string& stem,
                       const std::string& extension)
    -> const std::vector<std::filesystem::path>& {
    const auto& entries = directory_inventory(cache, directory).files;
    const auto found = entries.find({stem, extension});
    static const std::vector<std::filesystem::path> empty;
    return found == entries.end() ? empty : found->second;
}

auto existing_bms_audio_path(const std::filesystem::path& source,
                             const std::filesystem::path& asset_base,
                             bms_audio_directory_cache_t& cache,
                             std::string& warning) -> std::optional<std::filesystem::path> {
    auto normalized_text = source.generic_u8string();
    std::replace(normalized_text.begin(), normalized_text.end(), u8'\\', u8'/');
    if (normalized_text.size() >= 2 && normalized_text[1] == u8':' &&
        ((normalized_text.front() >= u8'A' && normalized_text.front() <= u8'Z') ||
         (normalized_text.front() >= u8'a' && normalized_text.front() <= u8'z'))) {
        warning = "Unsafe BMS audio path: " + utf8_path(source);
        return std::nullopt;
    }
    const std::filesystem::path normalized{normalized_text};
    const auto directory = resolved_audio_directory(normalized, asset_base, cache, warning);
    if (!directory) return std::nullopt;
    const auto stem = ascii_lower(utf8_path(normalized.stem()));
    const auto authored_extension = ascii_lower(utf8_path(normalized.extension()));
    const auto& same_extension = extension_matches(cache, *directory, stem, authored_extension);
    const auto exact =
        std::find_if(same_extension.begin(), same_extension.end(), [&](const auto& match) {
            return match.filename() == normalized.filename();
        });
    if (exact != same_extension.end()) return exact->lexically_relative(asset_base);
    if (same_extension.size() == 1)
        return same_extension.front().lexically_relative(asset_base);
    if (same_extension.size() > 1) {
        warning = "Ambiguous BMS audio for " + utf8_path(source) + ": " +
                  candidate_list(same_extension);
        return std::nullopt;
    }

    constexpr std::array extensions{".wav", ".ogg", ".flac"};
    std::vector<std::filesystem::path> alternatives;
    for (const auto* extension : extensions) {
        if (authored_extension == extension) continue;
        const std::string alternate_extension{extension};
        const auto& matches = extension_matches(cache, *directory, stem, alternate_extension);
        alternatives.insert(alternatives.end(), matches.begin(), matches.end());
    }
    if (alternatives.size() == 1)
        return alternatives.front().lexically_relative(asset_base);
    if (alternatives.size() > 1)
        warning = "Ambiguous BMS audio for " + utf8_path(source) + ": " +
                  candidate_list(alternatives);
    return std::nullopt;
}

void resolve_bms_audio_assets(song_document_t& document,
                              const std::filesystem::path& asset_base,
                              std::vector<std::string>& warnings) {
    bms_audio_directory_cache_t cache;
    for (auto& asset : document.audio_assets) {
        std::string warning;
        const auto actual = existing_bms_audio_path(asset.source_path, asset_base, cache, warning);
        if (!warning.empty()) warnings.push_back(std::move(warning));
        if (!actual || *actual == asset.source_path) continue;
        asset.source_path = *actual;
    }
}

void resolve_stepmania_music(song_document_t& document,
                             const std::filesystem::path& asset_base,
                             std::vector<std::string>& warnings) {
    bms_audio_directory_cache_t cache;
    for (auto& resource : document.resources) {
        if (std::ranges::find(resource.roles, resource_role_t::music) == resource.roles.end())
            continue;
        const auto asset = std::ranges::find(document.audio_assets, resource.asset,
                                             &audio_asset_t::id);
        if (asset == document.audio_assets.end()) continue;
        std::string warning;
        const auto actual = existing_bms_audio_path(asset->source_path, asset_base, cache, warning);
        if (!warning.empty()) {
            auto label = warning.find("BMS");
            if (label != std::string::npos) warning.replace(label, 3, "StepMania");
            warnings.push_back(std::move(warning));
        }
        if (actual) {
            asset->source_path = *actual;
            resource.missing = false;
        }
    }
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
                         std::optional<text_encoding_t> encoding_override,
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
        options.encoding_override = encoding_override;
        auto parsed = parse_bms_bytes(bytes, std::move(options));
        result.needs_encoding_choice = !encoding_override && std::any_of(
            parsed.diagnostics.begin(), parsed.diagnostics.end(), [](const auto& diagnostic) {
                return diagnostic.code == bms_parse_diagnostic_code_t::invalid_encoding &&
                       diagnostic.message.find("without a UTF-8 BOM is ambiguous") !=
                           std::string::npos;
            });
        take_parsed_document(std::move(parsed), "BMS chart", result);
        if (result.document)
            resolve_bms_audio_assets(*result.document, result.asset_base, result.warnings);
    } else if (extension == ".bmson") {
        auto options = bmson_parse_options_t{};
        options.asset_base_directory = result.asset_base;
        take_parsed_document(parse_bmson_bytes(bytes, std::move(options)), "BMSON chart", result);
    } else if (extension == ".sm" || extension == ".ssc") {
        auto options = stepmania_parse_options_t{};
        options.profile = extension == ".ssc" ? stepmania_profile_t::ssc
                                                : stepmania_profile_t::sm;
        options.asset_base_directory = result.asset_base;
        take_parsed_document(parse_stepmania_bytes(bytes, std::move(options)),
                             "StepMania chart", result);
        if (result.document)
            resolve_stepmania_music(*result.document, result.asset_base, result.warnings);
    } else if (extension == ".osu") {
        auto options = osu_mania_parse_options_t{};
        options.asset_base_directory = result.asset_base;
        take_parsed_document(parse_osu_mania_bytes(bytes, std::move(options)), "osu!mania chart",
                             result);
    }
}

void normalize_empty_input_profiles(song_document_t& document) {
    for (auto& chart : document.charts) {
        if (!chart.input_profile.profile_id.empty() || !chart.input_profile.bindings.empty() ||
            chart.lane_layouts.empty()) continue;
        const auto input = default_input_profile_for(chart.lane_layouts.front().layout);
        if (input) chart.input_profile = *input;
    }
}

} // namespace

auto is_supported_project_file_extension(const std::filesystem::path& path) -> bool {
    return supported_source_limit(lowercase_extension(path)) != 0;
}

auto open_project_file(const std::filesystem::path& selected_path,
                       std::optional<text_encoding_t> encoding_override)
    -> project_open_result_t {
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
    if (source) parse_selected_file(extension, *source, encoding_override, result);
    if (result.document) normalize_empty_input_profiles(*result.document);
    return result;
}

} // namespace wormedit::sys

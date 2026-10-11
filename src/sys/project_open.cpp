#include "sys/project_open.h"

#include <wormedit/core/bmson_layer_defaults.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/formats/bms.h>
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
#include <type_traits>
#include <unordered_set>
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

struct bms_asset_directory_inventory_t {
    std::map<std::string, std::vector<std::filesystem::path>> directories;
    std::map<std::pair<std::string, std::string>, std::vector<std::filesystem::path>> files;
};

using bms_asset_directory_cache_t =
    std::map<std::filesystem::path, bms_asset_directory_inventory_t>;

auto directory_inventory(bms_asset_directory_cache_t& cache,
                         const std::filesystem::path& directory)
    -> const bms_asset_directory_inventory_t& {
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

auto matching_directories(bms_asset_directory_cache_t& cache,
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

auto resolved_asset_directory(const std::filesystem::path& source,
                              const std::filesystem::path& asset_base,
                              bms_asset_directory_cache_t& cache, std::string_view label,
                              std::string& warning) -> std::optional<std::filesystem::path> {
    if (source.empty() || source.is_absolute()) {
        warning = "Unsafe " + std::string(label) + " path: " + utf8_path(source);
        return std::nullopt;
    }
    auto directory = asset_base;
    for (const auto& part : source.parent_path()) {
        if (part == ".") continue;
        if (part == "..") {
            warning = "Unsafe " + std::string(label) + " path: " + utf8_path(source);
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
            warning = "Ambiguous " + std::string(label) + " directory for " + utf8_path(source) + ": " +
                      candidate_list(matches);
            return std::nullopt;
        }
        if (matches.empty()) return std::nullopt;
        directory = matches.front();
    }
    return directory;
}

auto extension_matches(bms_asset_directory_cache_t& cache,
                       const std::filesystem::path& directory, const std::string& stem,
                       const std::string& extension)
    -> const std::vector<std::filesystem::path>& {
    const auto& entries = directory_inventory(cache, directory).files;
    const auto found = entries.find({stem, extension});
    static const std::vector<std::filesystem::path> empty;
    return found == entries.end() ? empty : found->second;
}

auto existing_bms_asset_path(const std::filesystem::path& source,
                             const std::filesystem::path& asset_base,
                             bms_asset_directory_cache_t& cache,
                             std::span<const std::string_view> extensions, std::string_view format,
                             std::string_view kind,
                             std::string& warning) -> std::optional<std::filesystem::path> {
    auto normalized_text = source.generic_u8string();
    std::replace(normalized_text.begin(), normalized_text.end(), u8'\\', u8'/');
    if (normalized_text.size() >= 2 && normalized_text[1] == u8':' &&
        ((normalized_text.front() >= u8'A' && normalized_text.front() <= u8'Z') ||
         (normalized_text.front() >= u8'a' && normalized_text.front() <= u8'z'))) {
        warning = "Unsafe " + std::string(format) + " " + std::string(kind) +
                  " path: " + utf8_path(source);
        return std::nullopt;
    }
    const std::filesystem::path normalized{normalized_text};
    const auto label = std::string(format) + " " + std::string(kind);
    const auto directory = resolved_asset_directory(normalized, asset_base, cache, label, warning);
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
        warning = "Ambiguous " + std::string(format) + " " + std::string(kind) + " for " + utf8_path(source) + ": " +
                  candidate_list(same_extension);
        return std::nullopt;
    }
    std::vector<std::filesystem::path> alternatives;
    for (const auto extension : extensions) {
        if (authored_extension == extension) continue;
        const auto& matches = extension_matches(cache, *directory, stem, std::string(extension));
        alternatives.insert(alternatives.end(), matches.begin(), matches.end());
    }
    std::ranges::sort(alternatives);
    if (alternatives.size() == 1)
        return alternatives.front().lexically_relative(asset_base);
    if (alternatives.size() > 1)
        warning = "Ambiguous " + std::string(format) + " " + std::string(kind) + " for " + utf8_path(source) + ": " +
                  candidate_list(alternatives);
    return std::nullopt;
}

using audio_missing_t = std::map<asset_id_t, bool>;

void apply_audio_missing(song_document_t& document, const audio_missing_t& missing) {
    for (auto& resource : document.resources) {
        const auto found = missing.find(resource.asset);
        if (found != missing.end()) resource.missing = found->second;
    }
}

// Availability is file existence, not decoder support. One check per asset also
// keeps resource aliases consistent without editing document history.
void mark_audio_availability(song_document_t& document, const std::filesystem::path& asset_base) {
    audio_missing_t missing;
    for (const auto& asset : document.audio_assets) {
        std::error_code error;
        const auto path = asset.source_path.is_absolute() ? asset.source_path
                                                         : asset_base / asset.source_path;
        missing.emplace(asset.id, asset.source_path.empty() ||
                        (!asset.source_path.is_absolute() && asset_base.empty()) ||
                        !std::filesystem::is_regular_file(path, error));
    }
    apply_audio_missing(document, missing);
}

// Points an audio asset at the matching file beside the chart, preserving the authored path on
// failure. Audio assets keep their .wav/.ogg/.flac fallback behavior.
void resolve_audio_assets(song_document_t& document, const std::filesystem::path& asset_base,
                          std::vector<std::string>& warnings, std::string_view format = "BMS") {
    constexpr std::array<std::string_view, 3> extensions{".wav", ".ogg", ".flac"};
    bms_asset_directory_cache_t cache;
    audio_missing_t missing;
    for (auto& asset : document.audio_assets) {
        std::string warning;
        const auto actual = existing_bms_asset_path(asset.source_path, asset_base, cache,
                                                    extensions, format, "audio", warning);
        if (!warning.empty()) {
            warnings.push_back(std::move(warning));
        }
        missing.emplace(asset.id, !actual);
        if (actual) asset.source_path = *actual;
    }
    apply_audio_missing(document, missing);
}

void resolve_visual_assets(song_document_t& document, const std::filesystem::path& asset_base,
                           std::vector<std::string>& warnings, std::string_view format = "BMS") {
    constexpr std::array<std::string_view, 4> image_extensions{".bmp", ".png", ".jpg", ".jpeg"};
    constexpr std::array<std::string_view, 5> video_extensions{".mpg", ".mp4", ".avi", ".wmv",
                                                               ".webm"};
    bms_asset_directory_cache_t cache;
    for (auto& asset : document.visual_assets) {
        std::span<const std::string_view> extensions;
        std::string_view kind;
        const auto authored_extension = ascii_lower(utf8_path(asset.source_path.extension()));
        if (asset.kind == asset_kind_t::video ||
            std::ranges::find(video_extensions, authored_extension) != video_extensions.end()) {
            asset.kind = asset_kind_t::video;
            extensions = video_extensions;
            kind = "video";
        } else if (asset.kind == asset_kind_t::image) {
            extensions = image_extensions;
            kind = "image";
        } else {
            continue;
        }
        std::string warning;
        const auto actual = existing_bms_asset_path(asset.source_path, asset_base, cache,
                                                    extensions, format, kind, warning);
        if (!warning.empty())
            warnings.push_back(std::move(warning));
        for (auto& resource : document.resources)
            if (resource.asset == asset.id) resource.missing = !actual;
        if (actual) asset.source_path = *actual;
    }
}

void resolve_stepmania_music(song_document_t& document,
                             const std::filesystem::path& asset_base,
                             std::vector<std::string>& warnings) {
    bms_asset_directory_cache_t cache;
    audio_missing_t missing;
    for (const auto& resource : document.resources)
        if (std::ranges::find(resource.roles, resource_role_t::music) != resource.roles.end())
            missing.emplace(resource.asset, true);
    for (auto& asset : document.audio_assets) {
        const auto entry = missing.find(asset.id);
        if (entry == missing.end()) continue;
        std::string warning;
        constexpr std::array<std::string_view, 3> extensions{".wav", ".ogg", ".flac"};
        const auto actual = existing_bms_asset_path(asset.source_path, asset_base, cache,
                                                    extensions, "StepMania", "audio", warning);
        if (!warning.empty()) warnings.push_back(std::move(warning));
        entry->second = !actual;
        if (actual) asset.source_path = *actual;
    }
    apply_audio_missing(document, missing);
}

template <typename ParseResult>
void take_parsed_document(ParseResult parsed, std::string_view format,
                          project_open_result_t& result) {
    if (parsed.song && !parsed.has_errors()) {
        if constexpr (std::is_same_v<ParseResult, osu_mania_parse_result_t>) {
            for (const auto& diagnostic : parsed.diagnostics) {
                if (diagnostic.severity == diagnostic_severity_t::warning)
                    result.warnings.push_back(diagnostic.message);
            }
        }
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
        if (result.document) mark_audio_availability(*result.document, result.asset_base);
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
        if (result.document) {
            resolve_audio_assets(*result.document, result.asset_base, result.warnings);
            resolve_visual_assets(*result.document, result.asset_base, result.warnings);
        }
    } else if (extension == ".bmson") {
        auto options = bmson_parse_options_t{};
        options.asset_base_directory = result.asset_base;
        take_parsed_document(parse_bmson_bytes(bytes, std::move(options)), "BMSON chart", result);
        if (result.document) {
            const auto initialized = initialize_bmson_sound_channel_layers(
                result.document->charts.front());
            if (!initialized.error.empty())
                result.warnings.push_back("BMSON semantic layers were not initialized: " +
                                          initialized.error);
            resolve_audio_assets(*result.document, result.asset_base, result.warnings, "BMSON");
            resolve_visual_assets(*result.document, result.asset_base, result.warnings, "BMSON");
        }
    } else if (extension == ".sm" || extension == ".ssc") {
        auto options = stepmania_parse_options_t{};
        options.profile = extension == ".ssc" ? stepmania_profile_t::ssc
                                                : stepmania_profile_t::sm;
        options.asset_base_directory = result.asset_base;
        take_parsed_document(parse_stepmania_bytes(bytes, std::move(options)),
                             "StepMania chart", result);
        if (result.document) {
            mark_audio_availability(*result.document, result.asset_base);
            resolve_stepmania_music(*result.document, result.asset_base, result.warnings);
        }
    } else if (extension == ".osu") {
        auto options = osu_mania_parse_options_t{};
        options.asset_base_directory = result.asset_base;
        take_parsed_document(parse_osu_mania_bytes(bytes, std::move(options)), "osu!mania chart",
                             result);
        if (result.document) mark_audio_availability(*result.document, result.asset_base);
    }
}

void normalize_empty_input_profiles(song_document_t& document) {
    for (auto& chart : document.charts) {
        // The BMSON reader names its profile "bmson-unbound" and leaves the keys to us.
        const auto named = !chart.input_profile.profile_id.empty() &&
                           chart.input_profile.profile_id != "bmson-unbound";
        if (named || !chart.input_profile.bindings.empty() || chart.lane_layouts.empty()) continue;
        const auto input = default_input_profile_for(chart.lane_layouts.front().layout);
        if (input) chart.input_profile = *input;
    }
}

template <typename Object>
auto rehome_measure_objects(chart_document_t& chart,
                            std::vector<Object> measure_chunk_t::* member,
                            std::unordered_set<std::uint64_t>& repaired_ids) -> std::size_t {
    std::map<measure_id_t, std::size_t> destinations;
    for (std::size_t index = 0; index < chart.measures.size(); ++index)
        destinations.emplace(chart.measures[index].id, index);
    std::vector<std::pair<std::size_t, Object>> moves;
    for (auto& measure : chart.measures) {
        auto& objects = measure.*member;
        std::erase_if(objects, [&](Object& object) {
            const auto destination = destinations.find(object.position.measure);
            if (object.position.measure == measure.id || destination == destinations.end())
                return false;
            repaired_ids.insert(object.id.value);
            moves.emplace_back(destination->second, std::move(object));
            return true;
        });
    }
    for (auto& [destination, object] : moves)
        (chart.measures[destination].*member).push_back(std::move(object));
    return moves.size();
}

auto repair_measure_storage(song_document_t& document,
                            const diagnostic_list_t& diagnostics) -> std::size_t {
    auto proposed = document;
    std::unordered_set<std::uint64_t> repaired_ids;
    std::size_t repaired_count = 0;
    for (auto& chart : proposed.charts) {
        repaired_count += rehome_measure_objects(chart, &measure_chunk_t::notes, repaired_ids);
        repaired_count += rehome_measure_objects(chart, &measure_chunk_t::bgm_events, repaired_ids);
    }
    const auto repairable = [&](const diagnostic_t& diagnostic) {
        return diagnostic.code == diagnostic_code_t::invalid_measure && diagnostic.object &&
               repaired_ids.contains(diagnostic.object->value);
    };
    if (repaired_ids.empty() || !std::ranges::all_of(diagnostics, repairable) ||
        !validate_document(proposed).empty()) return 0;
    document = std::move(proposed);
    return repaired_count;
}

auto validation_error(const diagnostic_list_t& diagnostics) -> std::string {
    std::string result = "Document could not be opened: ";
    const auto count = std::min<std::size_t>(diagnostics.size(), 3);
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) result += "; ";
        result += diagnostics[index].message;
    }
    if (diagnostics.size() > count)
        result += "; " + std::to_string(diagnostics.size() - count) + " more diagnostics";
    return result;
}

} // namespace

void refresh_project_audio_availability(song_document_t& document,
                                        const std::filesystem::path& asset_base) {
    mark_audio_availability(document, asset_base);
}

void validate_opened_document(project_open_result_t& result) {
    if (!result.document) return;
    normalize_empty_input_profiles(*result.document);
    const auto diagnostics = validate_document(*result.document);
    if (diagnostics.empty()) return;
    const auto repaired = repair_measure_storage(*result.document, diagnostics);
    if (repaired != 0) {
        result.warnings.push_back("Repaired " + std::to_string(repaired) +
                                  " objects stored in the wrong measure");
        return;
    }
    result.error = validation_error(diagnostics);
    result.document.reset();
    result.native_project = false;
}

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
    validate_opened_document(result);
    return result;
}

} // namespace wormedit::sys

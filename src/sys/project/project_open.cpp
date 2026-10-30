#include <wormedit/core/bms_program_edit.h>
#include "core/text_util.h"
#include "sys/project/project_open.h"
#include "sys/project/project_view.h"

#include <wormedit/core/compiler.h>
#include <wormedit/core/bmson_layer_defaults.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/osu_mania.h>
#include <wormedit/formats/quaver.h>
#include <wormedit/formats/stepmania.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace wormedit::sys {
namespace {
using wormedit::text_util::ascii_lowercase;
using text_util::lowercase_extension;

auto read_bounded_file(const std::filesystem::path& path, std::size_t limit,
                       std::string& error_message, std::stop_token stop,
                       job_progress_t* progress) -> std::optional<std::string> {
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
    if (progress) {
        progress->phase.store(job_phase_t::reading);
        progress->total.store(bytes.size());
    }
    for (std::size_t offset = 0; offset < bytes.size();) {
        if (stop.stop_requested()) return std::nullopt;
        const auto count = std::min<std::size_t>(64 * 1024, bytes.size() - offset);
        if (!input.read(bytes.data() + offset, static_cast<std::streamsize>(count))) {
            error_message = "Could not read the complete selected file.";
            return std::nullopt;
        }
        offset += count;
        if (progress) progress->completed.store(offset);
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
            inventory.directories[ascii_lowercase(utf8_path(path.filename()))].push_back(path);
        } else if (entry->is_regular_file(type_error) && !type_error) {
            inventory.files[{ascii_lowercase(utf8_path(path.stem())),
                             ascii_lowercase(utf8_path(path.extension()))}]
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
        const auto lowercase_name = ascii_lowercase(utf8_path(part));
        const auto& matches = matching_directories(cache, directory, lowercase_name);
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

WORMEDIT_NO_DANGLING auto extension_matches(bms_asset_directory_cache_t& cache,
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
    const auto stem = ascii_lowercase(utf8_path(normalized.stem()));
    const auto authored_extension = ascii_lowercase(utf8_path(normalized.extension()));
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
        const auto authored_extension = ascii_lowercase(utf8_path(asset.source_path.extension()));
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

enum class import_warning_policy_t { discard, retain };

template <typename Diagnostics>
void take_parsed_document(std::optional<song_document_t>&& document,
                          const Diagnostics& diagnostics, std::string_view format,
                          project_open_result_t& result,
                          import_warning_policy_t warning_policy = import_warning_policy_t::discard) {
    if (document && !diagnostic_queries::has_errors(diagnostics)) {
        if (warning_policy == import_warning_policy_t::retain) {
            for (const auto& diagnostic : diagnostics) {
                if (diagnostic.severity == diagnostic_severity_t::warning)
                    result.warnings.push_back(diagnostic.message);
            }
        }
        result.document = std::move(document);
        return;
    }
    const auto error = std::find_if(diagnostics.begin(), diagnostics.end(),
                                    [](const auto& item) {
                                        return item.severity == diagnostic_severity_t::error;
                                    });
    const auto detail = error != diagnostics.end()
                            ? error->message
                            : diagnostics.empty() ? "No document was produced."
                                                  : diagnostics.front().message;
    result.error = std::string(format) + " could not be opened: " +
                   utf8_path(result.source_path) + ": " + detail;
}

void retain_bms_asset_resolutions(project_open_result_t& result) {
    for (auto& owner : result.document->prepared_bms_programs) {
        auto prepared = owner->prepared().with_resolved_catalog(*result.document);
        auto replacement = bms_program_document_t::from(owner->difficulty(), std::move(prepared), owner->evaluation());
        if (!replacement.value || !replacement.ok()) {
            result.error = "Resolved BMS assets cannot retain a valid scoped owner.";
            result.document.reset();
            return;
        }
        owner = std::move(*replacement.value);
    }
}

auto parse_native_or_quaver(std::string_view extension, const std::string& source,
                            project_open_result_t& result) -> bool {
    if (extension != ".wormedit" && extension != ".qua") return false;
    if (extension == ".wormedit") {
        auto parsed = parse_native_project(source);
        take_parsed_document(std::move(parsed.value), parsed.diagnostics, "Native project", result);
    } else {
        const auto bytes = std::as_bytes(std::span(source.data(), source.size()));
        auto parsed = parse_quaver_bytes(bytes, {.asset_base_directory = result.asset_base});
        take_parsed_document(std::move(parsed.value), parsed.diagnostics, "Experimental Quaver chart",
                             result, import_warning_policy_t::retain);
    }
    result.native_project = extension == ".wormedit" && result.document.has_value();
    if (result.document) mark_audio_availability(*result.document, result.asset_base);
    return true;
}

void parse_selected_file(std::string_view extension, const std::string& source,
                         std::optional<text_encoding_t> encoding_override,
                         project_open_result_t& result,
                         const std::optional<std::string>& lane_profile_id) {
    if (parse_native_or_quaver(extension, source, result)) return;
    const auto bytes = std::as_bytes(std::span{source.data(), source.size()});
    if (extension == ".bms" || extension == ".bme" || extension == ".bml" ||
        extension == ".pms") {
        auto options = bms_parse_options_t{};
        options.file_extension = std::string(extension);
        options.encoding_override = encoding_override;
        options.lane_profile_id = lane_profile_id;
        result.bms_encoding = encoding_override;
        auto parsed = parse_bms_bytes(bytes, std::move(options));
        result.needs_encoding_choice = !encoding_override && std::any_of(
            parsed.diagnostics.begin(), parsed.diagnostics.end(), [](const auto& diagnostic) {
                return diagnostic.code == bms_parse_diagnostic_code_t::invalid_encoding &&
                       diagnostic.message.find("without a UTF-8 BOM is ambiguous") !=
                           std::string::npos;
            });
        result.needs_profile_choice = std::ranges::any_of(parsed.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code == bms_parse_diagnostic_code_t::missing_profile;
        });
        take_parsed_document(std::move(parsed.value.song), parsed.diagnostics, "BMS chart", result);
        if (result.document) {
            resolve_audio_assets(*result.document, result.asset_base, result.warnings);
            resolve_visual_assets(*result.document, result.asset_base, result.warnings);
            retain_bms_asset_resolutions(result);
        }
    } else if (extension == ".bmson") {
        auto options = bmson_parse_options_t{};
        options.asset_base_directory = result.asset_base;
        auto parsed = parse_bmson_bytes(bytes, std::move(options));
        take_parsed_document(std::move(parsed.value), parsed.diagnostics, "BMSON chart", result);
        if (result.document) {
            const auto initialized = create_bmson_sound_channel_layers(
                result.document->charts.front());
            if (!initialized)
                result.warnings.push_back("BMSON semantic layers were not initialized: " +
                                          initialized.error());
            resolve_audio_assets(*result.document, result.asset_base, result.warnings, "BMSON");
            resolve_visual_assets(*result.document, result.asset_base, result.warnings, "BMSON");
        }
    } else if (extension == ".sm" || extension == ".ssc") {
        auto options = stepmania_parse_options_t{};
        options.profile = extension == ".ssc" ? stepmania_profile_t::ssc
                                                : stepmania_profile_t::sm;
        options.asset_base_directory = result.asset_base;
        auto parsed = parse_stepmania_bytes(bytes, std::move(options));
        take_parsed_document(std::move(parsed.value), parsed.diagnostics, "StepMania chart", result);
        if (result.document) {
            mark_audio_availability(*result.document, result.asset_base);
            resolve_stepmania_music(*result.document, result.asset_base, result.warnings);
        }
    } else if (extension == ".osu") {
        auto options = osu_mania_parse_options_t{};
        options.asset_base_directory = result.asset_base;
        auto parsed = parse_osu_mania_bytes(bytes, std::move(options));
        take_parsed_document(std::move(parsed.value), parsed.diagnostics, "osu!mania chart",
                             result, import_warning_policy_t::retain);
        if (result.document) mark_audio_availability(*result.document, result.asset_base);
    }
}

void normalize_empty_input_profiles(song_document_t& document) {
    for (auto& chart : document.charts) {
        // The BMSON reader names its profile "bmson-unbound" and leaves the keys to us.
        const auto named = !chart.input_profile.profile_id.empty() &&
                           chart.input_profile.profile_id != "bmson-unbound";
        if (named || !chart.input_profile.bindings.empty() || chart.lane_layouts.empty()) continue;
        const auto input = make_default_input_profile_for(chart.lane_layouts.front().layout);
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

void begin_project_session(project_identity_t& current, project_chart_runtime_t& charts,
                           project_identity_t replacement) {
    const auto generation = charts.session_generation + 1;
    charts = project_chart_runtime_t{generation};
    current.session_loaded = replacement.session_loaded;
    current.native_project_loaded = replacement.native_project_loaded;
    current.fresh_blank_project = replacement.fresh_blank_project;
    current.active_project_path = std::move(replacement.active_project_path);
    current.asset_base = std::move(replacement.asset_base);
    current.scanned_primary_version = replacement.scanned_primary_version;
    current.pending_import.audio_project_path.clear();
    current.pending_import.bms_encoding_path.clear();
    current.external_player_source_path.clear();
}

void refresh_project_audio_availability(song_document_t& document,
                                        const std::filesystem::path& asset_base) {
    mark_audio_availability(document, asset_base);
    (void)reconcile_bms_program_catalogs({}, document);
}

void validate_opened_document(project_open_result_t& result) {
    if (!result.document) return;
    normalize_empty_input_profiles(*result.document);
    auto diagnostics = reconcile_bms_program_catalogs({}, *result.document);
    if (!diagnostic_queries::has_errors(diagnostics)) diagnostics = validate_document(*result.document);
    if (!diagnostic_queries::has_errors(diagnostics)) {
        for (const auto& diagnostic : diagnostics)
            if (std::ranges::find(result.warnings, diagnostic.message) == result.warnings.end())
                result.warnings.push_back(diagnostic.message);
        return;
    }
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

auto open_project_file(const std::filesystem::path& selected_path,
                       std::optional<text_encoding_t> encoding_override,
                       std::stop_token stop, job_progress_t* progress,
                       std::optional<std::string> lane_profile_id)
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
    auto source = read_bounded_file(result.source_path, limit, result.error, stop, progress);
    if (stop.stop_requested()) return result;
    if (progress) progress->phase.store(job_phase_t::preparing);
    if (source) parse_selected_file(extension, *source, encoding_override, result, lane_profile_id);
    if (stop.stop_requested()) {
        result.document.reset();
        return result;
    }
    validate_opened_document(result);
    return result;
}

project_open_runtime_t::project_open_runtime_t(task_scheduler_t& scheduler)
    : job_(scheduler, 1, "Could not open file.", "Wait for the current file open.",
           "Opening returned no result.") {}

auto project_open_runtime_t::open(const project_open_request_t& request, std::stop_token stop)
    -> project_open_job_t::work_outcome_t {
    return std::make_unique<project_open_result_t>(
        open_project_file(request.path, request.encoding, stop, request.progress.get(), request.lane_profile_id));
}

auto project_open_runtime_t::start(std::filesystem::path path,
                                  std::optional<text_encoding_t> encoding,
                                  project_open_identity_t identity,
                                  project_open_job_t::work_function_t work,
                                  std::optional<std::string> lane_profile_id)
    -> async_job_start_outcome_t {
    if (is_busy())
        return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded,
                                                     "Wait for the current file open."});
    auto progress = std::make_shared<job_progress_t>();
    auto started = job_.request([&] {
        return project_open_request_t{.path = std::move(path), .encoding = encoding,
                                      .lane_profile_id = std::move(lane_profile_id),
                                      .progress = progress};
    }, identity, work);
    if (started) {
        progress_ = std::move(progress);
        cancelling_ = false;
        encoding_retry_ = encoding.has_value() || lane_profile_id.has_value();
    }
    return started;
}

auto project_open_runtime_t::collect(bool wait) -> std::optional<project_open_job_t::completion_t> {
    auto completion = wait ? job_.wait() : job_.poll();
    if (completion && *completion && cancelling_) {
        auto& value = **completion;
        return project_open_job_t::completion_t{std::unexpected(
            async_job_completion_error_t<project_open_identity_t>{
                .generation = value.generation, .identity = value.identity,
                .cause = {failure_code_t::cancelled, "Opening cancelled."}})};
    }
    return completion;
}
void project_open_runtime_t::cancel() {
    if (!is_busy()) return;
    cancelling_ = true;
    job_.cancel(async_job_cancel_policy_t::retain_completion);
}
auto project_open_runtime_t::view() const -> ui::file_job_view_t {
    return {.active = is_busy(), .cancelling = cancelling_,
            .completed = progress_ ? progress_->completed.load() : 0,
            .total = progress_ ? progress_->total.load() : 0,
            .label = progress_ && progress_->phase.load() == job_phase_t::reading
                         ? "Reading file" : "Parsing and validating"};
}

} // namespace wormedit::sys

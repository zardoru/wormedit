#include "sys/external_player.h"
#include "core/text_util.h"
#include <wormedit/core/catalogs.h>

#include <wormedit/formats/bms_writer.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_process.h>

#include <algorithm>
#include <cerrno>
#include <optional>
#include <sstream>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <sys/stat.h>
#endif
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <system_error>

namespace wormedit::sys {
namespace {
constexpr std::size_t maximum_players = 64;
constexpr std::size_t maximum_field_bytes = 4096;

auto substitute(std::string_view token, std::string_view app, std::string_view filename,
                std::uint64_t measure) -> std::string {
    // Scan the original token once, so inserted filenames containing placeholders remain literal.
    std::string result;
    while (!token.empty()) {
        if (token.starts_with("<apppath>")) {
            result += app;
            token.remove_prefix(9);
        } else if (token.starts_with("<filename>")) {
            result += filename;
            token.remove_prefix(10);
        } else if (token.starts_with("<measure>")) {
            result += std::to_string(measure);
            token.remove_prefix(9);
        } else {
            result += token.front();
            token.remove_prefix(1);
        }
    }
    return result;
}

auto valid_preferences(const external_player_preferences_t& preferences) -> bool {
    if (preferences.players.empty() || preferences.players.size() > maximum_players ||
        preferences.selected >= preferences.players.size()) return false;
    return std::ranges::all_of(preferences.players, [](const auto& player) {
        const auto valid = [](const std::string& field) {
            return field.size() <= maximum_field_bytes && field.find('\0') == std::string::npos;
        };
        return valid(player.name) && valid(player.path) && valid(player.start_arguments) &&
               valid(player.here_arguments) && valid(player.stop_arguments);
    });
}

auto argument_template_for(const external_player_definition_t& player,
                          external_player_action_t action) -> std::string_view {
    if (action == external_player_action_t::play_start) return player.start_arguments;
    if (action == external_player_action_t::play_here) return player.here_arguments;
    return player.stop_arguments;
}

struct file_identity_t {
    std::uint64_t volume{};
    std::uint64_t file{};
    auto operator==(const file_identity_t&) const -> bool = default;
};

auto file_identity(const std::filesystem::path& path) -> std::optional<file_identity_t> {
#ifdef _WIN32
    const auto handle = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE |
        FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
    BY_HANDLE_FILE_INFORMATION information{};
    const bool valid = GetFileInformationByHandle(handle, &information) != 0;
    CloseHandle(handle);
    if (!valid) return std::nullopt;
    return file_identity_t{information.dwVolumeSerialNumber,
        (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32) | information.nFileIndexLow};
#else
    struct stat information{};
    if (lstat(path.c_str(), &information) != 0 || !S_ISREG(information.st_mode))
        return std::nullopt;
    return file_identity_t{static_cast<std::uint64_t>(information.st_dev),
                          static_cast<std::uint64_t>(information.st_ino)};
#endif
}

auto preview_path(const std::filesystem::path& source) -> std::filesystem::path {
    const auto extension = text_util::lowercase_extension(source);
    if (!is_bms_extension(extension)) return {};
    return source.parent_path() / ("___wormedit_preview" + extension);
}

struct exclusive_write_error_t {
    std::string error;
    std::optional<file_identity_t> identity;
    bool created{};
    auto operator==(const exclusive_write_error_t&) const -> bool = default;
};
using exclusive_write_outcome_t = result_t<file_identity_t, exclusive_write_error_t>;

auto read_open_file_identity(std::FILE* file) -> std::optional<file_identity_t> {
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION information{};
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file)));
    if (!GetFileInformationByHandle(handle, &information)) return std::nullopt;
    return file_identity_t{information.dwVolumeSerialNumber,
        (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32) | information.nFileIndexLow};
#else
    struct stat information{};
    if (fstat(fileno(file), &information) != 0) return std::nullopt;
    return file_identity_t{static_cast<std::uint64_t>(information.st_dev),
                          static_cast<std::uint64_t>(information.st_ino)};
#endif
}

auto write_exclusive(const std::filesystem::path& path, std::string_view bytes)
    -> exclusive_write_outcome_t {
#ifdef _WIN32
    auto* file = _wfopen(path.c_str(), L"wbx");
#else
    auto* file = std::fopen(path.c_str(), "wbx");
#endif
    if (!file) return std::unexpected(exclusive_write_error_t{
        .error = "Cannot exclusively create file: " + path.string()});
    const auto identity = read_open_file_identity(file);
    const bool complete = identity && std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    const bool closed = std::fclose(file) == 0;
    if (complete && closed) return *identity;
    return std::unexpected(exclusive_write_error_t{
        .error = "Writing file failed.", .identity = identity, .created = true});
}

void remove_owned(const std::filesystem::path& path, const file_identity_t& owned) noexcept {
    const auto identity = file_identity(path);
    if (identity && *identity == owned) {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
}
} // namespace

auto expand_external_player_arguments(std::string_view text, std::string_view app,
    std::string_view filename, std::uint64_t measure) -> external_player_arguments_t {
    std::vector<std::string> arguments;
    std::string token;
    char quote{};
    bool active{};
    if (text.find('\0') != std::string_view::npos || app.find('\0') != std::string_view::npos ||
        filename.find('\0') != std::string_view::npos)
        return std::unexpected{"Player arguments contain a NUL character."};
    for (const char character : text) {
        if (quote != 0) {
            if (character == quote) quote = 0;
            else token += character;
        } else if (character == '"' || character == '\'') {
            quote = character;
            active = true;
        } else if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
            if (active) {
                arguments.push_back(substitute(token, app, filename, measure));
                token.clear();
                active = false;
            }
        } else {
            token += character;
            active = true;
        }
    }
    if (quote != 0) return std::unexpected{"Player argument template has an unmatched quote."};
    if (active) arguments.push_back(substitute(token, app, filename, measure));
    return arguments;
}


auto load_external_player_preferences(const std::filesystem::path& path,
    external_player_preferences_t& preferences) -> std::string {
    return wormedit::load_legacy_external_player_preferences(path, preferences);
}

auto save_external_player_preferences(const std::filesystem::path& path,
    const external_player_preferences_t& preferences) -> std::string {
    if (!valid_preferences(preferences)) return "External-player preferences exceed supported limits.";
    std::ostringstream output;
    output << "wormedit-external-players-v1 " << preferences.selected << ' '
           << preferences.players.size() << '\n';
    for (const auto& player : preferences.players) {
        output << std::quoted(player.name) << ' ' << std::quoted(player.path) << ' '
               << std::quoted(player.start_arguments) << ' ' << std::quoted(player.here_arguments)
               << ' ' << std::quoted(player.stop_arguments) << '\n';
    }
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    auto temporary = path;
    temporary += ".tmp-" + std::to_string(nonce);
    const auto written = write_exclusive(temporary, output.str());
    if (!written) {
        if (written.error().identity) remove_owned(temporary, *written.error().identity);
        return written.error().error;
    }
    std::error_code error;
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
#else
    std::filesystem::rename(temporary, path, error);
#endif
    if (error) {
        remove_owned(temporary, *written);
        return "Cannot replace external-player preferences: " + error.message();
    }
    return {};
}

external_player_t::~external_player_t() { cleanup(); }
void external_player_t::remove_temporary(temporary_file_t& temporary) noexcept {
    if (temporary.process) SDL_DestroyProcess(temporary.process);
    temporary.process = nullptr;
    remove_owned(temporary.path, {temporary.identity_volume, temporary.identity_file});
}

void external_player_t::cleanup() noexcept {
    for (auto& temporary : temporary_files_) remove_temporary(temporary);
    temporary_files_.clear();
}

auto external_player_t::prepare_preview(const song_document_t& song, chart_id_t chart,
    const std::filesystem::path& source) -> external_player_launch_result_t {
    external_player_launch_result_t result;
    std::error_code path_error;
    const auto absolute_source = std::filesystem::absolute(source, path_error);
    result.preview_path = path_error ? std::filesystem::path{} : preview_path(absolute_source);
    if (source.empty() || result.preview_path.empty() || absolute_source == result.preview_path)
        return {.error = "External preview requires a BMS, BME, BML, or PMS source chart."};
    for (auto iterator = temporary_files_.begin(); iterator != temporary_files_.end(); ++iterator) {
        if (iterator->path != result.preview_path) continue;
        if (iterator->process && !SDL_WaitProcess(iterator->process, false, nullptr))
            return {.error = "External preview is still in use by the previous player."};
        remove_temporary(*iterator);
        temporary_files_.erase(iterator);
        break;
    }
    bms_write_options_t options;
    options.chart = chart;
    options.preserve_unchanged_source = false;
    options.branch_policy = bms_branch_policy_t::flatten_active;
    const auto selected = song.find_chart(chart);
    if (selected != nullptr && selected->lane_layouts.empty() &&
        !selected->input_profile.profile_id.empty())
        options.lane_profile_id = selected->input_profile.profile_id;
    const auto written = write_bms(song, options);
    if (!written.bytes || written.has_errors()) {
        const auto error = std::ranges::find_if(written.diagnostics, [](const auto& diagnostic) {
            return diagnostic.severity == diagnostic_severity_t::error;
        });
        return {.error = error == written.diagnostics.end() ? "External preview export failed."
                                                           : error->message};
    }
    const auto created = write_exclusive(result.preview_path, *written.bytes);
    result.error = created ? std::string{} : created.error().error;
    if (!created && !created.error().created) return result;
    const auto identity = created ? std::optional{*created} : created.error().identity;
    if (!identity) return {.error = "External preview file ownership could not be established."};
    temporary_files_.push_back({result.preview_path, identity->volume, identity->file, nullptr});
    if (!result.error.empty()) {
        remove_temporary(temporary_files_.back());
        temporary_files_.pop_back();
        return result;
    }
    return result;
}

auto external_player_t::launch(const external_player_definition_t& player,
    external_player_action_t action, const song_document_t& song, chart_id_t chart,
    const std::filesystem::path& source, std::uint64_t measure,
    const std::filesystem::path& app) -> external_player_launch_result_t {
    if (player.path.empty()) return {.open_preferences = true, .error = "Choose an external player in Preferences."};
    if (player.path.find('\0') != std::string::npos)
        return {.error = "Player path contains a NUL character."};
    // Validate template syntax before creating any file.
    auto expanded = expand_external_player_arguments(argument_template_for(player, action),
                                                     app.string(), "", measure);
    if (!expanded) return {.error = expanded.error()};
    external_player_launch_result_t result;
    if (action != external_player_action_t::stop) {
        result = prepare_preview(song, chart, source);
        if (!result.error.empty()) return result;
    }
    expanded = expand_external_player_arguments(argument_template_for(player, action),
                                                app.string(), result.preview_path.string(), measure);
    if (!expanded) {
        if (!result.preview_path.empty()) {
            remove_temporary(temporary_files_.back());
            temporary_files_.pop_back();
        }
        return {.error = expanded.error()};
    }
    expanded->insert(expanded->begin(), player.path);
    std::vector<const char*> arguments;
    for (const auto& argument : *expanded) arguments.push_back(argument.c_str());
    arguments.push_back(nullptr);
    auto* process = SDL_CreateProcess(arguments.data(), false);
    if (!process) {
        result.error = "External player launch failed: " + std::string{SDL_GetError()};
        if (!result.preview_path.empty()) {
            remove_temporary(temporary_files_.back());
            temporary_files_.pop_back();
        }
        return result;
    }
    if (result.preview_path.empty()) SDL_DestroyProcess(process);
    else temporary_files_.back().process = process;
    result.launched = true;
    return result;
}
} // namespace wormedit::sys

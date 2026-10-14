#include <wormedit/core/atomic_file_storage.h>
#include <wormedit/core/shortcut_profile_storage.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace wormedit {
namespace {

// Conservative bound for the pretty-printed JSON envelope and each encoded
// binding/chord. Valid IDs use a restricted ASCII alphabet, so their encoded
// size is at most their source byte length.
constexpr std::size_t profile_json_fixed_overhead_bound = 256;
constexpr std::size_t binding_json_fixed_overhead_bound = 256;
constexpr std::size_t chord_json_overhead_bound = 96;
constexpr auto maximum_shortcut_profile_json_bound =
    profile_json_fixed_overhead_bound + maximum_command_id_bytes +
    maximum_shortcut_profile_bindings *
        (binding_json_fixed_overhead_bound + maximum_command_id_bytes +
         maximum_shortcuts_per_command * chord_json_overhead_bound);

auto shortcut_profile_json_size_bound(const shortcut_profile_t& profile)
    -> std::optional<std::size_t> {
    if (profile.id.size() > maximum_command_id_bytes ||
        profile.bindings.size() > maximum_shortcut_profile_bindings) {
        return std::nullopt;
    }
    auto bound = profile_json_fixed_overhead_bound + profile.id.size();
    for (const auto& binding : profile.bindings) {
        if (binding.command.value.size() > maximum_command_id_bytes ||
            binding.shortcuts.size() > maximum_shortcuts_per_command) {
            return std::nullopt;
        }
        bound += binding_json_fixed_overhead_bound + binding.command.value.size() +
                 binding.shortcuts.size() * chord_json_overhead_bound;
        if (bound > maximum_shortcut_profile_bytes) {
            return bound;
        }
    }
    return bound;
}

} // namespace

static_assert(maximum_shortcut_profile_json_bound < maximum_shortcut_profile_bytes,
              "shortcut profile schema bounds must keep serialization below its byte cap");

auto open_shortcut_profile(const std::filesystem::path& path) -> shortcut_profile_load_outcome_t {
    if (path.empty() || path.filename().empty()) {
        return std::unexpected(diagnosed_t<error_t<shortcut_profile_load_status_t>>{
            .value = {shortcut_profile_load_status_t::io_error,
                      "shortcut profile path must name a file"}});
    }

    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error == std::errc::no_such_file_or_directory ||
        status.type() == std::filesystem::file_type::not_found) {
        return std::unexpected(diagnosed_t<error_t<shortcut_profile_load_status_t>>{
            .value = {shortcut_profile_load_status_t::missing, {}}});
    }
    if (error) {
        return std::unexpected(diagnosed_t<error_t<shortcut_profile_load_status_t>>{
            .value = {shortcut_profile_load_status_t::io_error,
                      "could not inspect shortcut profile: " + error.message()}});
    }
    if (!std::filesystem::is_regular_file(status)) {
        return std::unexpected(diagnosed_t<error_t<shortcut_profile_load_status_t>>{
            .value = {shortcut_profile_load_status_t::io_error,
                      "shortcut profile is not a regular file"}});
    }

    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return std::unexpected(diagnosed_t<error_t<shortcut_profile_load_status_t>>{
            .value = {shortcut_profile_load_status_t::io_error,
                      "could not determine shortcut profile size: " + error.message()}});
    }
    if (size > maximum_shortcut_profile_bytes) {
        return std::unexpected(diagnosed_t<error_t<shortcut_profile_load_status_t>>{
            .value = {shortcut_profile_load_status_t::resource_limit,
                      "shortcut profile exceeds the 1 MiB limit"}});
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return std::unexpected(diagnosed_t<error_t<shortcut_profile_load_status_t>>{
            .value = {shortcut_profile_load_status_t::io_error,
                      "could not open shortcut profile"}});
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!bytes.empty()) {
        stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!stream || static_cast<std::size_t>(stream.gcount()) != bytes.size()) {
            return std::unexpected(diagnosed_t<error_t<shortcut_profile_load_status_t>>{
                .value = {shortcut_profile_load_status_t::io_error,
                          "could not read the complete shortcut profile"}});
        }
    }
    char extra{};
    if (stream.get(extra)) {
        return std::unexpected(diagnosed_t<error_t<shortcut_profile_load_status_t>>{
            .value = {shortcut_profile_load_status_t::io_error,
                      "shortcut profile changed while it was being read"}});
    }
    if (!stream.eof()) {
        return std::unexpected(diagnosed_t<error_t<shortcut_profile_load_status_t>>{
            .value = {shortcut_profile_load_status_t::io_error,
                      "could not finish reading shortcut profile"}});
    }

    auto parsed = parse_shortcut_profile(bytes);
    if (!parsed.profile) {
        return std::unexpected(diagnosed_t<error_t<shortcut_profile_load_status_t>>{
            .value = {shortcut_profile_load_status_t::invalid, {}},
            .diagnostics = std::move(parsed.diagnostics)});
    }
    // The legacy loader does not expose parser diagnostics on a successful load.
    return std::move(*parsed.profile);
}

auto create_shortcut_profile_file(const std::filesystem::path& path,
                                  const shortcut_profile_t& profile)
    -> shortcut_profile_save_outcome_t {
    if (path.empty() || path.filename().empty()) {
        return std::unexpected(error_t<shortcut_profile_save_status_t>{
            shortcut_profile_save_status_t::io_error, "shortcut profile path must name a file"});
    }

    const auto size_bound = shortcut_profile_json_size_bound(profile);
    if (!size_bound) {
        return std::unexpected(error_t<shortcut_profile_save_status_t>{
            shortcut_profile_save_status_t::invalid_profile,
            "shortcut profile exceeds a field or collection limit"});
    }
    if (*size_bound > maximum_shortcut_profile_bytes) {
        return std::unexpected(
            error_t<shortcut_profile_save_status_t>{shortcut_profile_save_status_t::invalid_profile,
                                                    "shortcut profile exceeds the 1 MiB limit"});
    }

    auto serialized = serialize_shortcut_profile(profile);
    if (serialized.empty()) {
        return std::unexpected(error_t<shortcut_profile_save_status_t>{
            shortcut_profile_save_status_t::invalid_profile,
            "shortcut profile is invalid or exceeds the 1 MiB limit"});
    }
    if (serialized.size() > maximum_shortcut_profile_bytes) {
        return std::unexpected(
            error_t<shortcut_profile_save_status_t>{shortcut_profile_save_status_t::invalid_profile,
                                                    "shortcut profile exceeds the 1 MiB limit"});
    }

    auto saved = create_file_atomic(path, serialized, maximum_shortcut_profile_bytes);
    if (!saved) {
        return std::unexpected(error_t<shortcut_profile_save_status_t>{
            shortcut_profile_save_status_t::io_error, std::move(saved.error())});
    }
    return {};
}

auto shortcut_profile_load_result_t::from(shortcut_profile_load_outcome_t&& outcome)
    -> shortcut_profile_load_result_t {
    if (!outcome) {
        return {.status = outcome.error().value.code,
                .profile = std::nullopt,
                .diagnostics = std::move(outcome.error().diagnostics),
                .error = std::move(outcome.error().value.message)};
    }
    return {.status = shortcut_profile_load_status_t::loaded,
            .profile = std::move(*outcome),
            .diagnostics = {},
            .error = {}};
}

auto shortcut_profile_save_result_t::from(shortcut_profile_save_outcome_t&& outcome)
    -> shortcut_profile_save_result_t {
    if (!outcome) {
        return {.status = outcome.error().code, .error = std::move(outcome.error().message)};
    }
    return {.status = shortcut_profile_save_status_t::published, .error = {}};
}

auto load_shortcut_profile(const std::filesystem::path& path) -> shortcut_profile_load_result_t {
    return shortcut_profile_load_result_t::from(open_shortcut_profile(path));
}

auto save_shortcut_profile_atomic(const std::filesystem::path& path,
                                  const shortcut_profile_t& profile)
    -> shortcut_profile_save_result_t {
    return shortcut_profile_save_result_t::from(create_shortcut_profile_file(path, profile));
}

} // namespace wormedit

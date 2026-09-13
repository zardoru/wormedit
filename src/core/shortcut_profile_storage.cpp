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

#ifdef _WIN32
using native_file_t = HANDLE;
constexpr auto invalid_file = INVALID_HANDLE_VALUE;
#else
using native_file_t = int;
constexpr auto invalid_file = -1;
#endif

struct temporary_file_t {
    std::filesystem::path path;
    native_file_t handle{invalid_file};
    bool owned{};

    ~temporary_file_t() {
        close_ignoring_errors();
        if (owned && !path.empty()) {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }

    auto close_ignoring_errors() noexcept -> void {
        if (handle == invalid_file) {
            return;
        }
#ifdef _WIN32
        (void)CloseHandle(handle);
#else
        (void)::close(handle);
#endif
        handle = invalid_file;
    }
};

auto process_id() -> std::uint64_t {
#ifdef _WIN32
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

auto native_error_category() -> const std::error_category& {
#ifdef _WIN32
    return std::system_category();
#else
    return std::generic_category();
#endif
}

auto make_temporary_path(const std::filesystem::path& destination, std::uint64_t sequence)
    -> std::filesystem::path {
    const auto parent =
        destination.has_parent_path() ? destination.parent_path() : std::filesystem::path{"."};
    auto name = destination.filename();
    name += ".wormedit-tmp-" + std::to_string(process_id()) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
            std::to_string(sequence);
    return parent / name;
}

auto create_exclusive_file(const std::filesystem::path& path, native_file_t& handle,
                           int& error_value) -> bool {
#ifdef _WIN32
    handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                         nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error_value = static_cast<int>(GetLastError());
        return false;
    }
#else
    handle = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (handle < 0) {
        error_value = errno;
        return false;
    }
#endif
    return true;
}

auto write_all(native_file_t handle, std::string_view bytes, int& error_value) -> bool {
    std::size_t offset{};
    while (offset < bytes.size()) {
#ifdef _WIN32
        const auto remaining = std::min<std::size_t>(bytes.size() - offset, 1U << 20U);
        DWORD written{};
        if (!WriteFile(handle, bytes.data() + offset, static_cast<DWORD>(remaining), &written,
                       nullptr) ||
            written == 0) {
            error_value = static_cast<int>(GetLastError());
            return false;
        }
        offset += written;
#else
        const auto written = ::write(handle, bytes.data() + offset, bytes.size() - offset);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            error_value = errno;
            return false;
        }
        offset += static_cast<std::size_t>(written);
#endif
    }
    return true;
}

auto flush_file(native_file_t handle, int& error_value) -> bool {
#ifdef _WIN32
    if (!FlushFileBuffers(handle)) {
        error_value = static_cast<int>(GetLastError());
        return false;
    }
#else
    while (::fsync(handle) != 0) {
        if (errno == EINTR) {
            continue;
        }
        error_value = errno;
        return false;
    }
#endif
    return true;
}

auto close_file(native_file_t& handle, int& error_value) -> bool {
#ifdef _WIN32
    const auto closed = CloseHandle(handle) != 0;
    if (!closed) {
        error_value = static_cast<int>(GetLastError());
    }
#else
    const auto closed = ::close(handle) == 0;
    if (!closed) {
        error_value = errno;
    }
#endif
    handle = invalid_file;
    return closed;
}

auto atomic_replace(const std::filesystem::path& temporary,
                    const std::filesystem::path& destination, std::string& error) -> bool {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "atomic profile replacement failed with Windows error " +
                std::to_string(GetLastError());
        return false;
    }
#else
    if (::rename(temporary.c_str(), destination.c_str()) != 0) {
        error = "atomic profile replacement failed: " +
                std::error_code(errno, std::generic_category()).message();
        return false;
    }
#endif
    return true;
}

auto io_error(std::string message) -> shortcut_profile_load_result_t {
    shortcut_profile_load_result_t result;
    result.status = shortcut_profile_load_status_t::io_error;
    result.error = std::move(message);
    return result;
}

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

auto load_shortcut_profile(const std::filesystem::path& path) -> shortcut_profile_load_result_t {
    if (path.empty() || path.filename().empty()) {
        return io_error("shortcut profile path must name a file");
    }

    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error == std::errc::no_such_file_or_directory ||
        status.type() == std::filesystem::file_type::not_found) {
        shortcut_profile_load_result_t result;
        result.status = shortcut_profile_load_status_t::missing;
        return result;
    }
    if (error) {
        return io_error("could not inspect shortcut profile: " + error.message());
    }
    if (!std::filesystem::is_regular_file(status)) {
        return io_error("shortcut profile is not a regular file");
    }

    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return io_error("could not determine shortcut profile size: " + error.message());
    }
    if (size > maximum_shortcut_profile_bytes) {
        shortcut_profile_load_result_t result;
        result.status = shortcut_profile_load_status_t::resource_limit;
        result.error = "shortcut profile exceeds the 1 MiB limit";
        return result;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return io_error("could not open shortcut profile");
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!bytes.empty()) {
        stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!stream || static_cast<std::size_t>(stream.gcount()) != bytes.size()) {
            return io_error("could not read the complete shortcut profile");
        }
    }
    char extra{};
    if (stream.get(extra)) {
        return io_error("shortcut profile changed while it was being read");
    }
    if (!stream.eof()) {
        return io_error("could not finish reading shortcut profile");
    }

    auto parsed = parse_shortcut_profile(bytes);
    shortcut_profile_load_result_t result;
    if (!parsed.profile) {
        result.status = shortcut_profile_load_status_t::invalid;
        result.diagnostics = std::move(parsed.diagnostics);
        return result;
    }
    result.status = shortcut_profile_load_status_t::loaded;
    result.profile = std::move(parsed.profile);
    return result;
}

auto save_shortcut_profile_atomic(const std::filesystem::path& path,
                                  const shortcut_profile_t& profile)
    -> shortcut_profile_save_result_t {
    shortcut_profile_save_result_t result;
    if (path.empty() || path.filename().empty()) {
        result.error = "shortcut profile path must name a file";
        return result;
    }

    const auto size_bound = shortcut_profile_json_size_bound(profile);
    if (!size_bound) {
        result.status = shortcut_profile_save_status_t::invalid_profile;
        result.error = "shortcut profile exceeds a field or collection limit";
        return result;
    }
    if (*size_bound > maximum_shortcut_profile_bytes) {
        result.status = shortcut_profile_save_status_t::invalid_profile;
        result.error = "shortcut profile exceeds the 1 MiB limit";
        return result;
    }

    auto serialized = serialize_shortcut_profile(profile);
    if (serialized.empty()) {
        result.status = shortcut_profile_save_status_t::invalid_profile;
        result.error = "shortcut profile is invalid or exceeds the 1 MiB limit";
        return result;
    }
    if (serialized.size() > maximum_shortcut_profile_bytes) {
        result.status = shortcut_profile_save_status_t::invalid_profile;
        result.error = "shortcut profile exceeds the 1 MiB limit";
        return result;
    }

    static std::atomic<std::uint64_t> temporary_sequence{1};
    temporary_file_t temporary;
    bool created{};
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto sequence = temporary_sequence.fetch_add(1, std::memory_order_relaxed);
        temporary.path = make_temporary_path(path, sequence);
        int error{};
        if (create_exclusive_file(temporary.path, temporary.handle, error)) {
            created = true;
            temporary.owned = true;
            break;
        }
#ifdef _WIN32
        const auto collision = error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS;
#else
        const auto collision = error == EEXIST;
#endif
        if (!collision) {
            result.error = "could not create same-directory temporary profile: " +
                           std::error_code(error, native_error_category()).message();
            return result;
        }
    }
    if (!created) {
        result.error = "could not allocate a unique temporary profile path";
        return result;
    }

    int error{};
    if (!write_all(temporary.handle, serialized, error)) {
        result.error = "could not write temporary shortcut profile: " +
                       std::error_code(error, native_error_category()).message();
        return result;
    }
    if (!flush_file(temporary.handle, error)) {
        result.error = "could not flush temporary shortcut profile: " +
                       std::error_code(error, native_error_category()).message();
        return result;
    }
    if (!close_file(temporary.handle, error)) {
        result.error = "could not close temporary shortcut profile: " +
                       std::error_code(error, native_error_category()).message();
        return result;
    }
    if (!atomic_replace(temporary.path, path, result.error)) {
        return result;
    }
    temporary.owned = false;
    result.status = shortcut_profile_save_status_t::published;
    return result;
}

} // namespace wormedit

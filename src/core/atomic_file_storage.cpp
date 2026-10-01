#include <wormedit/core/atomic_file_storage.h>

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
#ifdef _WIN32
using native_file_t = HANDLE;
const auto invalid_file = INVALID_HANDLE_VALUE;
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
        error = "atomic file replacement failed with Windows error " +
                std::to_string(GetLastError());
        return false;
    }
#else
    if (::rename(temporary.c_str(), destination.c_str()) != 0) {
        error = "atomic file replacement failed: " +
                std::error_code(errno, std::generic_category()).message();
        return false;
    }
#endif
    return true;
}

} // namespace

auto save_file_atomic(const std::filesystem::path& path, std::string_view bytes,
                       std::size_t maximum_bytes) -> atomic_file_save_result_t {
    atomic_file_save_result_t result;
    if (path.empty() || path.filename().empty()) { result.error = "destination must name a file"; return result; }
    if (bytes.size() > maximum_bytes) { result.error = "file exceeds its byte limit"; return result; }
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
            result.error = "could not create same-directory temporary file: " +
                           std::error_code(error, native_error_category()).message();
            return result;
        }
    }
    if (!created) {
        result.error = "could not allocate a unique temporary file path";
        return result;
    }

    int error{};
    if (!write_all(temporary.handle, bytes, error)) {
        result.error = "could not write temporary file: " +
                       std::error_code(error, native_error_category()).message();
        return result;
    }
    if (!flush_file(temporary.handle, error)) {
        result.error = "could not flush temporary file: " +
                       std::error_code(error, native_error_category()).message();
        return result;
    }
    if (!close_file(temporary.handle, error)) {
        result.error = "could not close temporary file: " +
                       std::error_code(error, native_error_category()).message();
        return result;
    }
    if (!atomic_replace(temporary.path, path, result.error)) {
        return result;
    }
    temporary.owned = false;
    result.published = true;
    return result;
}
} // namespace wormedit

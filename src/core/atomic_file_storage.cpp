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
                    const std::filesystem::path& destination, std::string& error,
                    bool replace_existing = true) -> bool {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                     (replace_existing ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH)) {
        error = "atomic file replacement failed with Windows error " +
                std::to_string(GetLastError());
        return false;
    }
#else
    if (!replace_existing) {
        if (::link(temporary.c_str(), destination.c_str()) == 0) return true;
        error = "atomic file publication failed: " + std::error_code(errno, std::generic_category()).message();
        return false;
    }
    if (::rename(temporary.c_str(), destination.c_str()) != 0) {
        error = "atomic file replacement failed: " +
                std::error_code(errno, std::generic_category()).message();
        return false;
    }
#endif
    return true;
}

} // namespace

struct prepared_atomic_file_t::storage_t {
    temporary_file_t temporary;
    std::filesystem::path destination;
};
prepared_atomic_file_t::prepared_atomic_file_t(std::unique_ptr<storage_t> storage) : storage_(std::move(storage)) {}
prepared_atomic_file_t::prepared_atomic_file_t(prepared_atomic_file_t&&) noexcept = default;
auto prepared_atomic_file_t::operator=(prepared_atomic_file_t&&) noexcept -> prepared_atomic_file_t& = default;
prepared_atomic_file_t::~prepared_atomic_file_t() = default;

auto prepared_atomic_file_t::from(const std::filesystem::path& path, std::string_view bytes,
    std::size_t maximum_bytes, validator_t validate, std::stop_token stop)
    -> result_t<prepared_atomic_file_t, std::string> {
    std::string message;
    if (path.empty() || path.filename().empty()) {
        message = "destination must name a file";
        return std::unexpected(std::move(message));
    }
    if (bytes.size() > maximum_bytes) {
        message = "file exceeds its byte limit";
        return std::unexpected(std::move(message));
    }
    static std::atomic<std::uint64_t> temporary_sequence{1};
    auto storage = std::make_unique<storage_t>();
    storage->destination = path;
    auto& temporary = storage->temporary;
    if (stop.stop_requested()) return std::unexpected("File preparation cancelled.");
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
            message = "could not create same-directory temporary file: " +
                      std::error_code(error, native_error_category()).message();
            return std::unexpected(std::move(message));
        }
    }
    if (!created) {
        message = "could not allocate a unique temporary file path";
        return std::unexpected(std::move(message));
    }

    int error{};
    if (!write_all(temporary.handle, bytes, error)) {
        message = "could not write temporary file: " +
                  std::error_code(error, native_error_category()).message();
        return std::unexpected(std::move(message));
    }
    if (!flush_file(temporary.handle, error)) {
        message = "could not flush temporary file: " +
                  std::error_code(error, native_error_category()).message();
        return std::unexpected(std::move(message));
    }
    if (!close_file(temporary.handle, error)) {
        message = "could not close temporary file: " +
                  std::error_code(error, native_error_category()).message();
        return std::unexpected(std::move(message));
    }
    if (stop.stop_requested()) return std::unexpected("File preparation cancelled.");
    if (validate) {
        if (auto verified = validate(temporary.path); !verified) return std::unexpected(std::move(verified.error()));
    }
    if (stop.stop_requested()) return std::unexpected("File preparation cancelled.");
    return prepared_atomic_file_t{std::move(storage)};
}

auto prepared_atomic_file_t::publish(bool replace_existing) -> atomic_file_save_outcome_t {
    if (!storage_) return std::unexpected("Prepared file is no longer available.");
    std::string message;
    if (!atomic_replace(storage_->temporary.path, storage_->destination, message, replace_existing)) {
        return std::unexpected(std::move(message));
    }
    storage_.reset();
    return {};
}

auto create_file_atomic(const std::filesystem::path& path, std::string_view bytes,
                        std::size_t maximum_bytes) -> atomic_file_save_outcome_t {
    auto prepared = prepared_atomic_file_t::from(path, bytes, maximum_bytes);
    if (!prepared) return std::unexpected(std::move(prepared.error()));
    return prepared->publish();
}
auto open_exclusive_file(const std::filesystem::path& path) -> unique_file_t {
#ifdef _WIN32
    return unique_file_t{_wfopen(path.c_str(), L"wbx")};
#else
    return unique_file_t{std::fopen(path.c_str(), "wbx")};
#endif
}
} // namespace wormedit

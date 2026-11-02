#include <wormedit/core/atomic_file_storage.h>

#include "checked_math.h"

#include <algorithm>
#include <limits>
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

auto write_all(native_file_t handle, std::string_view bytes, int& error_value, std::stop_token stop) -> bool {
    std::size_t offset{};
    while (offset < bytes.size()) {
        if (stop.stop_requested()) return false;
        const auto remaining = std::min<std::size_t>(bytes.size() - offset, 1U << 20U);
#ifdef _WIN32
        DWORD written{};
        if (!WriteFile(handle, bytes.data() + offset, static_cast<DWORD>(remaining), &written,
                       nullptr) ||
            written == 0) {
            error_value = static_cast<int>(GetLastError());
            return false;
        }
        offset += written;
#else
        const auto written = ::write(handle, bytes.data() + offset, remaining);
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

auto seek_file(native_file_t handle, std::int64_t position, int& error_value) -> bool {
#ifdef _WIN32
    LARGE_INTEGER offset;
    offset.QuadPart = position;
    if (SetFilePointerEx(handle, offset, nullptr, FILE_BEGIN)) return true;
    error_value = static_cast<int>(GetLastError());
#else
    if (::lseek(handle, static_cast<off_t>(position), SEEK_SET) >= 0) return true;
    error_value = errno;
#endif
    return false;
}

// The temporary owner remains responsible for exclusive creation and cleanup.
auto create_temporary(temporary_file_t& temporary, const std::filesystem::path& destination)
    -> atomic_file_save_outcome_t {
    static std::atomic<std::uint64_t> sequence{1};
    for (int attempt = 0; attempt < 32; ++attempt) {
        temporary.path = make_temporary_path(destination, sequence.fetch_add(1, std::memory_order_relaxed));
        int error{};
        if (create_exclusive_file(temporary.path, temporary.handle, error)) {
            temporary.owned = true;
            return {};
        }
#ifdef _WIN32
        const auto collision = error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS;
#else
        const auto collision = error == EEXIST;
#endif
        if (!collision) return std::unexpected("could not create same-directory temporary file: " +
            std::error_code(error, native_error_category()).message());
    }
    return std::unexpected("could not allocate a unique temporary file path");
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
    std::uint64_t maximum_bytes{}, position{}, extent{};
    std::stop_token stop;
    std::string failure;

    auto is_ready() -> bool {
        if (stop.stop_requested()) failure = "File preparation cancelled.";
        return failure.empty();
    }
    auto finish() -> atomic_file_save_outcome_t {
        if (!is_ready()) return std::unexpected(failure);
        int error{};
        if (!flush_file(temporary.handle, error))
            return std::unexpected("could not flush temporary file: " +
                std::error_code(error, native_error_category()).message());
        if (!close_file(temporary.handle, error))
            return std::unexpected("could not close temporary file: " +
                std::error_code(error, native_error_category()).message());
        return {};
    }
};
prepared_atomic_file_t::prepared_atomic_file_t(std::unique_ptr<storage_t> storage) : storage_(std::move(storage)) {}
prepared_atomic_file_t::prepared_atomic_file_t(prepared_atomic_file_t&&) noexcept = default;
auto prepared_atomic_file_t::operator=(prepared_atomic_file_t&&) noexcept -> prepared_atomic_file_t& = default;
prepared_atomic_file_t::~prepared_atomic_file_t() = default;

auto prepared_atomic_file_t::stream_t::write(std::span<const std::byte> bytes)
    -> atomic_file_save_outcome_t {
    auto& storage = storage_;
    if (!storage.is_ready()) return std::unexpected(storage.failure);
    if (bytes.size() > storage.maximum_bytes - storage.position) {
        storage.failure = "file exceeds its byte limit";
        return std::unexpected(storage.failure);
    }
    int error{};
    const std::string_view text{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    if (!write_all(storage.temporary.handle, text, error, storage.stop)) {
        storage.failure = storage.stop.stop_requested() ? "File preparation cancelled." :
            "could not write temporary file: " + std::error_code(error, native_error_category()).message();
        return std::unexpected(storage.failure);
    }
    storage.position += bytes.size();
    storage.extent = std::max(storage.extent, storage.position);
    return {};
}
auto prepared_atomic_file_t::stream_t::seek(std::int64_t offset, seek_origin_t origin)
    -> result_t<std::int64_t, std::string> {
    auto& storage = storage_;
    if (!storage.is_ready()) return std::unexpected(storage.failure);
    std::uint64_t base{};
    switch (origin) {
    case seek_origin_t::begin: break;
    case seek_origin_t::current: base = storage.position; break;
    case seek_origin_t::end: base = storage.extent; break;
    }
    const auto position = checked_math::checked_add(static_cast<std::int64_t>(base), offset);
    if (!position || *position < 0 || static_cast<std::uint64_t>(*position) > storage.maximum_bytes) {
        storage.failure = "file seek exceeds its byte limit";
        return std::unexpected(storage.failure);
    }
    int error{};
    if (!seek_file(storage.temporary.handle, *position, error)) {
        storage.failure = "could not seek temporary file: " + std::error_code(error, native_error_category()).message();
        return std::unexpected(storage.failure);
    }
    storage.position = static_cast<std::uint64_t>(*position);
    return *position;
}
auto prepared_atomic_file_t::stream_t::size() const noexcept -> std::uint64_t { return storage_.extent; }

auto prepared_atomic_file_t::from(const std::filesystem::path& path, std::string_view bytes,
    std::size_t maximum_bytes, validator_t validate, std::stop_token stop)
    -> result_t<prepared_atomic_file_t, std::string> {
    if (bytes.size() > maximum_bytes) return std::unexpected("file exceeds its byte limit");
    return from_stream(path, maximum_bytes, [bytes](stream_t& stream) {
        return stream.write(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    }, std::move(validate), stop);
}
auto prepared_atomic_file_t::from_stream(const std::filesystem::path& path,
    std::size_t maximum_bytes, producer_t produce, validator_t validate, std::stop_token stop)
    -> result_t<prepared_atomic_file_t, std::string> {
    if (path.empty() || path.filename().empty()) return std::unexpected("destination must name a file");
    if (!produce) return std::unexpected("File producer is required.");
    if (stop.stop_requested()) return std::unexpected("File preparation cancelled.");
    auto storage = std::make_unique<storage_t>();
    storage->destination = path;
    storage->maximum_bytes = std::min<std::uint64_t>(maximum_bytes, std::numeric_limits<std::int64_t>::max());
    storage->stop = stop;
    const auto created = create_temporary(storage->temporary, path);
    if (!created) return std::unexpected(created.error());
    stream_t stream{*storage};
    if (auto result = produce(stream); !result) return std::unexpected(std::move(result.error()));
    if (auto result = storage->finish(); !result) return std::unexpected(std::move(result.error()));
    if (stop.stop_requested()) return std::unexpected("File preparation cancelled.");
    if (validate) {
        if (auto result = validate(storage->temporary.path); !result) return std::unexpected(std::move(result.error()));
    }
    if (stop.stop_requested()) return std::unexpected("File preparation cancelled.");
    return prepared_atomic_file_t{std::move(storage)};
}

auto prepared_atomic_file_t::publish(bool replace_existing) -> atomic_file_save_outcome_t {
    if (!storage_) return std::unexpected("Prepared file is no longer available.");
    if (!storage_->is_ready()) return std::unexpected(storage_->failure);
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

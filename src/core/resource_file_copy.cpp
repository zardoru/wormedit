#include <wormedit/core/resource_file_copy.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace wormedit {
namespace {

auto is_valid_relative(const std::filesystem::path& path) -> bool {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    return std::ranges::none_of(path, [](const auto& part) { return part == ".."; });
}

auto file_bytes_equal(const std::filesystem::path& first, const std::filesystem::path& second,
                      std::stop_token stop) -> bool {
    std::error_code error;
    if (std::filesystem::file_size(first, error) != std::filesystem::file_size(second, error) ||
        error) {
        return false;
    }
    std::ifstream a(first, std::ios::binary);
    std::ifstream b(second, std::ios::binary);
    std::array<char, 64 * 1024> left{}, right{};
    if (!a || !b) {
        return false;
    }
    while (!stop.stop_requested()) {
        a.read(left.data(), left.size());
        b.read(right.data(), right.size());
        if (a.gcount() != b.gcount() ||
            !std::equal(left.begin(), left.begin() + a.gcount(), right.begin())) {
            return false;
        }
        if (a.eof() && b.eof()) {
            return true;
        }
        if (a.bad() || b.bad() || a.fail() || b.fail()) {
            return false;
        }
    }
    return false;
}

auto published_identity(const std::filesystem::path& path)
    -> std::optional<project_asset_copy_receipt_t::published_identity_t> {
    namespace fs = std::filesystem;
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error || fs::is_symlink(status) || !fs::is_regular_file(status))
        return std::nullopt;
    project_asset_copy_receipt_t::published_identity_t identity;
#if defined(_WIN32)
    const HANDLE handle = CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return std::nullopt;
    BY_HANDLE_FILE_INFORMATION info{};
    const bool valid = GetFileInformationByHandle(handle, &info) != 0 &&
                       (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
    CloseHandle(handle);
    if (!valid)
        return std::nullopt;
    identity.volume = info.dwVolumeSerialNumber;
    identity.file = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
#else
    struct stat info{};
    if (::lstat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
        return std::nullopt;
    identity.volume = static_cast<std::uint64_t>(info.st_dev);
    identity.file = static_cast<std::uint64_t>(info.st_ino);
#endif
    identity.size = fs::file_size(path, error);
    if (error)
        return std::nullopt;
    identity.modified = fs::last_write_time(path, error);
    return error ? std::nullopt : std::optional{identity};
}

auto published_identity_equal(const project_asset_copy_receipt_t::published_identity_t& first,
                              const project_asset_copy_receipt_t::published_identity_t& second)
    -> bool {
    return first == second;
}

auto is_existing_copy_directory_safe(const std::filesystem::path& project_directory,
                                     const std::filesystem::path& relative_directory) -> bool {
    namespace fs = std::filesystem;
    std::error_code error;
    const auto root = fs::symlink_status(project_directory, error);
    if (error || fs::is_symlink(root) || !fs::is_directory(root)) {
        return false;
    }
    auto directory = project_directory;
    for (const auto& component : relative_directory) {
        directory /= component;
        const auto status = fs::symlink_status(directory, error);
        if (error || fs::is_symlink(status) || !fs::is_directory(status)) {
            return false;
        }
    }
    return true;
}

auto discard_unlinked_copy(const project_asset_copy_request_t& request,
                           const std::filesystem::path& relative_directory,
                           const project_asset_copy_receipt_t& copy) -> bool {
    namespace fs = std::filesystem;
    if (copy.status != project_asset_copy_phase_t::copied || !copy.published_identity ||
        !request.project_directory.is_absolute() || !is_valid_relative(relative_directory) ||
        request.source_path.filename().empty())
        return false;
    const auto relative = relative_directory / request.destination_filename();
    if (copy.relative_path != relative ||
        copy.absolute_path != request.project_directory / relative)
        return false;
    std::error_code error;
    if (!is_existing_copy_directory_safe(request.project_directory, relative_directory))
        return false;
    const auto identity = published_identity(copy.absolute_path);
    if (!identity || !published_identity_equal(*identity, *copy.published_identity))
        return false;
    const auto source = request.source_path.is_absolute()
                            ? request.source_path
                            : request.project_directory / request.source_path;
    if (!file_bytes_equal(source, copy.absolute_path, {}))
        return false;
    const auto final_identity = published_identity(copy.absolute_path);
    if (!final_identity || !published_identity_equal(*final_identity, *copy.published_identity))
        return false;
    return fs::remove(copy.absolute_path, error) && !error;
}

#if defined(_WIN32)
using copy_output_handle_t = HANDLE;
#else
using copy_output_handle_t = int;
#endif

auto write_chunk(copy_output_handle_t output, const char* data, std::size_t size) -> bool {
    std::size_t position = 0;
    while (position < size) {
#if defined(_WIN32)
        DWORD n = 0;
        const auto ok =
            WriteFile(output, data + position, static_cast<DWORD>(size - position), &n, nullptr);
        if (!ok || n == 0) {
            return false;
        }
#else
        const auto n = ::write(output, data + position, size - position);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
#endif
        position += static_cast<std::size_t>(n);
    }
    return true;
}

auto write_flushed(const std::filesystem::path& path, const std::filesystem::path& source,
                   std::uintmax_t maximum_bytes, std::stop_token stop)
    -> std::optional<failure_code_t> {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return failure_code_t::unavailable;
    }
#if defined(_WIN32)
    HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        return failure_code_t::io_failed;
    }
#else
    const int output = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (output < 0) {
        return failure_code_t::io_failed;
    }
#endif
    std::optional<failure_code_t> status;
    std::uintmax_t written = 0;
    std::array<char, 64 * 1024> buffer{};
    while (!stop.stop_requested()) {
        input.read(buffer.data(), buffer.size());
        const auto count = input.gcount();
        if (count > 0) {
            if (written + static_cast<std::uintmax_t>(count) > maximum_bytes) {
                status = failure_code_t::invalid_request;
                break;
            }
            if (!write_chunk(output, buffer.data(), static_cast<std::size_t>(count))) {
                status = failure_code_t::io_failed;
                break;
            }
            written += static_cast<std::uintmax_t>(count);
        }
        if (input.eof()) {
            break;
        }
        if (input.bad() || input.fail()) {
            status = failure_code_t::io_failed;
            break;
        }
    }
    if (stop.stop_requested()) {
        status = failure_code_t::cancelled;
    }
    if (!status) {
#if defined(_WIN32)
        if (!FlushFileBuffers(output)) {
            status = failure_code_t::io_failed;
        }
#else
        if (::fsync(output) != 0) {
            status = failure_code_t::io_failed;
        }
#endif
    }
#if defined(_WIN32)
    if (!CloseHandle(output)) {
        status = failure_code_t::io_failed;
    }
#else
    if (::close(output) != 0) {
        status = failure_code_t::io_failed;
    }
#endif
    return status;
}

auto prepare_copy_directory(const std::filesystem::path& project_directory,
                            const std::filesystem::path& relative_directory, bool inspect_only,
                            std::filesystem::path& directory)
    -> result_t<void, error_t<failure_code_t>> {
    namespace fs = std::filesystem;
    std::error_code error;
    directory = project_directory;
    for (const auto& component : relative_directory) {
        directory /= component;
        error.clear();
        const auto status = fs::symlink_status(directory, error);
        if (error == std::errc::no_such_file_or_directory) {
            error.clear();
            if (inspect_only) {
                return std::unexpected(
                    error_t<failure_code_t>{failure_code_t::not_found,
                                                         "Asset copy destination is available."});
            }
            if (!fs::create_directory(directory, error) && error) {
                return std::unexpected(error_t<failure_code_t>{
                    failure_code_t::io_failed,
                    "Could not create the asset destination directory."});
            }
            error.clear();
            const auto created = fs::symlink_status(directory, error);
            if (error || fs::is_symlink(created) || !fs::is_directory(created)) {
                return std::unexpected(error_t<failure_code_t>{
                    failure_code_t::invalid_request,
                    "Asset destination directory changed during creation."});
            }
        } else if (error || fs::is_symlink(status) || !fs::is_directory(status)) {
            return std::unexpected(error_t<failure_code_t>{
                failure_code_t::invalid_request,
                "Asset destination contains a symlink or non-directory component."});
        }
    }
    return {};
}

struct copy_location_t {
    std::filesystem::path source;
    std::filesystem::path directory;
    std::filesystem::path relative;
    std::filesystem::path destination;
};

auto publish_copy(const project_asset_copy_request_t& request, const copy_location_t& location,
                  std::stop_token stop) -> project_asset_copy_outcome_t {
    namespace fs = std::filesystem;
    static std::atomic<std::uint64_t> sequence{0};
    const auto temporary =
        location.directory /
        (".wormedit-copy-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
         std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
    std::error_code error;
    const auto copied = write_flushed(temporary, location.source, request.maximum_bytes, stop);
    if (copied.has_value() || stop.stop_requested() ||
        !file_bytes_equal(location.source, temporary, stop)) {
        fs::remove(temporary, error);
        const auto status = stop.stop_requested() ? failure_code_t::cancelled
                            : copied.has_value()
                                ? *copied
                                : failure_code_t::unavailable;
        return std::unexpected(error_t<failure_code_t>{
            status, "Source changed, copy failed, or copy was cancelled."});
    }
    if (stop.stop_requested()) {
        fs::remove(temporary, error);
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::cancelled, "Copy cancelled."});
    }
    if (request.validate_temporary) {
        bool valid{};
        try {
            valid = request.validate_temporary(temporary);
        } catch (...) {
            valid = false;
        }
        if (!valid) {
            fs::remove(temporary, error);
            return std::unexpected(error_t<failure_code_t>{
                failure_code_t::invalid_data,
                "Copied asset failed validation before publication."});
        }
    }
    if (stop.stop_requested()) {
        fs::remove(temporary, error);
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::cancelled, "Copy cancelled."});
    }
#if defined(_WIN32)
    const auto installed =
        MoveFileExW(temporary.c_str(), location.destination.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
#else
    const auto installed = ::link(temporary.c_str(), location.destination.c_str()) == 0;
#endif
    fs::remove(temporary, error);
    if (!installed) {
        const auto destination_status = fs::symlink_status(location.destination, error);
        if (!error && !fs::is_symlink(destination_status) &&
            fs::is_regular_file(destination_status) &&
            file_bytes_equal(location.source, location.destination, stop)) {
            return project_asset_copy_receipt_t::from(location.relative, location.destination,
                                                      project_asset_copy_phase_t::reused);
        }
        return std::unexpected(error_t<failure_code_t>{
            fs::exists(location.destination, error)
                ? failure_code_t::conflict
                : failure_code_t::io_failed,
            "Could not publish the project asset without overwriting a file."});
    }
#if !defined(_WIN32)
    const auto fd = ::open(location.directory.c_str(), O_RDONLY);
    if (fd >= 0) {
        (void)::fsync(fd);
        (void)::close(fd);
    }
#endif
    return project_asset_copy_receipt_t::from(location.relative, location.destination,
                                              project_asset_copy_phase_t::copied);
}

} // namespace

auto project_asset_copy_receipt_t::from(const std::filesystem::path& relative_path,
                                        const std::filesystem::path& absolute_path,
                                        project_asset_copy_phase_t status)
    -> project_asset_copy_receipt_t {
    return {.status = status,
            .relative_path = relative_path,
            .absolute_path = absolute_path,
            .published_identity = status == project_asset_copy_phase_t::copied
                                      ? wormedit::published_identity(absolute_path)
                                      : std::nullopt};
}

static auto copy_resource_file_to_relative_directory(
    const project_asset_copy_request_t& request, const std::filesystem::path& relative_directory,
    std::stop_token stop, bool inspect_only = false) -> project_asset_copy_outcome_t {
    namespace fs = std::filesystem;
    if (!request.asset || !request.project_directory.is_absolute() || request.source_path.empty() ||
        request.maximum_bytes == 0 ||
        (!request.source_path.is_absolute() && !is_valid_relative(request.source_path)) ||
        !is_valid_relative(relative_directory)) {
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::invalid_request,
            "Asset, project directory, or source path is invalid."});
    }
    const auto filename = request.source_path.filename();
    if (filename.empty() || filename == "." || filename == "..") {
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::invalid_request, "Source has no filename."});
    }
    const auto source = request.source_path.is_absolute()
                            ? request.source_path
                            : request.project_directory / request.source_path;
    std::error_code error;
    const auto root_status = fs::symlink_status(request.project_directory, error);
    if (error || fs::is_symlink(root_status) || !fs::is_directory(root_status)) {
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::invalid_request,
            "Project directory is unavailable or is a symlink."});
    }
    if (!fs::is_regular_file(source, error) || error) {
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::unavailable, "Source file is unavailable."});
    }
    const auto size = fs::file_size(source, error);
    if (error || size > request.maximum_bytes) {
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::invalid_request,
            "Source file exceeds the copy limit or cannot be measured."});
    }
    std::filesystem::path directory;
    if (auto preparation = prepare_copy_directory(request.project_directory, relative_directory,
                                                  inspect_only, directory);
        !preparation) {
        return std::unexpected(std::move(preparation.error()));
    }
    const auto relative =
        relative_directory / request.destination_filename();
    const copy_location_t location{source, directory, relative,
                                   request.project_directory / relative};
    if (stop.stop_requested()) {
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::cancelled, "Copy cancelled."});
    }
    const auto destination_status = fs::symlink_status(location.destination, error);
    if (!error) {
        if (!fs::is_symlink(destination_status) && fs::is_regular_file(destination_status) &&
            file_bytes_equal(source, location.destination, stop)) {
            return project_asset_copy_receipt_t::from(location.relative, location.destination,
                                                      project_asset_copy_phase_t::reused);
        }
        return std::unexpected(error_t<failure_code_t>{
            stop.stop_requested() ? failure_code_t::cancelled
                                  : failure_code_t::conflict,
            "Asset destination filename already contains different data."});
    }
    if (error != std::errc::no_such_file_or_directory) {
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::io_failed, "Could not inspect the asset destination."});
    }
    error.clear();
    if (inspect_only)
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::not_found, "Asset copy destination is available."});
    return publish_copy(request, location, stop);
}

auto create_project_asset_copy(const project_asset_copy_request_t& request, std::stop_token stop)
    -> project_asset_copy_outcome_t {
    return copy_resource_file_to_relative_directory(request, "assets", stop);
}

auto inspect_project_asset_file(const project_asset_copy_request_t& request, std::stop_token stop)
    -> project_asset_copy_outcome_t {
    return copy_resource_file_to_relative_directory(request, "assets", stop, true);
}

auto export_asset_directory(const std::filesystem::path& relative_output_path)
    -> std::optional<std::filesystem::path> {
    if (!is_valid_relative(relative_output_path) || relative_output_path.filename().empty() ||
        relative_output_path.filename() == "." || relative_output_path.filename() == "..")
        return std::nullopt;
    return (relative_output_path.has_extension() ? relative_output_path.parent_path()
                                                 : relative_output_path) /
           "assets";
}

auto export_asset_reference_path(const std::filesystem::path& relative_output_path,
                                 const std::filesystem::path& copied_project_relative_path)
    -> std::optional<std::filesystem::path> {
    const auto directory = export_asset_directory(relative_output_path);
    if (!directory || !is_valid_relative(copied_project_relative_path) ||
        copied_project_relative_path.parent_path() != *directory ||
        copied_project_relative_path.filename().empty())
        return std::nullopt;
    return std::filesystem::path{"assets"} / copied_project_relative_path.filename();
}

auto create_export_asset_copy(const export_asset_copy_request_t& request, std::stop_token stop)
    -> project_asset_copy_outcome_t {
    const auto directory = export_asset_directory(request.relative_output_path);
    if (!directory) {
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::invalid_request,
            "Export output path must be a project-relative file or directory."});
    }
    const project_asset_copy_request_t copy{.project_directory = request.project_directory,
                                            .source_path = request.source_path,
                                            .asset = request.asset,
                                            .maximum_bytes = request.maximum_bytes,
                                            .validate_temporary = request.validate_temporary};
    return copy_resource_file_to_relative_directory(copy, *directory, stop);
}

auto inspect_export_asset_file(const export_asset_copy_request_t& request, std::stop_token stop)
    -> project_asset_copy_outcome_t {
    const auto directory = export_asset_directory(request.relative_output_path);
    if (!directory)
        return std::unexpected(error_t<failure_code_t>{
            failure_code_t::invalid_request,
            "Export output path must be a project-relative file or directory."});
    const project_asset_copy_request_t inspect{.project_directory = request.project_directory,
                                               .source_path = request.source_path,
                                               .asset = request.asset,
                                               .maximum_bytes = request.maximum_bytes};
    return copy_resource_file_to_relative_directory(inspect, *directory, stop, true);
}

auto discard_unlinked_project_asset_copy(const project_asset_copy_request_t& request,
                                         const project_asset_copy_receipt_t& copy) -> bool {
    return discard_unlinked_copy(request, "assets", copy);
}

auto discard_unlinked_export_asset_copy(const export_asset_copy_request_t& request,
                                        const project_asset_copy_receipt_t& copy) -> bool {
    const auto directory = export_asset_directory(request.relative_output_path);
    if (!directory)
        return false;
    return discard_unlinked_copy({.project_directory = request.project_directory,
                                  .source_path = request.source_path,
                                  .asset = request.asset,
                                  .maximum_bytes = request.maximum_bytes},
                                 *directory, copy);
}

} // namespace wormedit

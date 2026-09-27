#include <wormedit/core/resource_file_copy.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace wormedit {
namespace {

auto failure(project_asset_copy_status_t status, std::string message)
    -> project_asset_copy_result_t {
    return {
        .status = status, .relative_path = {}, .absolute_path = {}, .message = std::move(message)};
}

auto valid_relative(const std::filesystem::path& path) -> bool {
    if (path.empty() || path.is_absolute() || path.has_root_name()) {
        return false;
    }
    return std::ranges::none_of(path, [](const auto& part) { return part == ".."; });
}

auto same_bytes(const std::filesystem::path& first, const std::filesystem::path& second,
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

auto write_flushed(const std::filesystem::path& path, const std::filesystem::path& source,
                   std::uintmax_t maximum_bytes, std::stop_token stop)
    -> project_asset_copy_status_t {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return project_asset_copy_status_t::source_unavailable;
    }
#if defined(_WIN32)
    HANDLE output = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        return project_asset_copy_status_t::io_error;
    }
#else
    const int output = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (output < 0) {
        return project_asset_copy_status_t::io_error;
    }
#endif
    auto status = project_asset_copy_status_t::copied;
    std::uintmax_t written = 0;
    std::array<char, 64 * 1024> buffer{};
    while (!stop.stop_requested()) {
        input.read(buffer.data(), buffer.size());
        const auto count = input.gcount();
        if (count > 0) {
            if (written + static_cast<std::uintmax_t>(count) > maximum_bytes) {
                status = project_asset_copy_status_t::invalid_request;
                break;
            }
            std::size_t position = 0;
            while (position < static_cast<std::size_t>(count)) {
#if defined(_WIN32)
                DWORD n = 0;
                const auto ok = WriteFile(output, buffer.data() + position,
                                          static_cast<DWORD>(count - position), &n, nullptr);
                if (!ok || n == 0) {
                    status = project_asset_copy_status_t::io_error;
                    break;
                }
#else
                const auto n = ::write(output, buffer.data() + position,
                                       static_cast<std::size_t>(count) - position);
                if (n <= 0) {
                    if (n < 0 && errno == EINTR) {
                        continue;
                    }
                    status = project_asset_copy_status_t::io_error;
                    break;
                }
#endif
                position += static_cast<std::size_t>(n);
            }
            if (status != project_asset_copy_status_t::copied) {
                break;
            }
            written += static_cast<std::uintmax_t>(count);
        }
        if (input.eof()) {
            break;
        }
        if (input.bad() || input.fail()) {
            status = project_asset_copy_status_t::io_error;
            break;
        }
    }
    if (stop.stop_requested()) {
        status = project_asset_copy_status_t::cancelled;
    }
    if (status == project_asset_copy_status_t::copied) {
#if defined(_WIN32)
        if (!FlushFileBuffers(output)) {
            status = project_asset_copy_status_t::io_error;
        }
#else
        if (::fsync(output) != 0) {
            status = project_asset_copy_status_t::io_error;
        }
#endif
    }
#if defined(_WIN32)
    if (!CloseHandle(output)) {
        status = project_asset_copy_status_t::io_error;
    }
#else
    if (::close(output) != 0) {
        status = project_asset_copy_status_t::io_error;
    }
#endif
    return status;
}

} // namespace

static auto copy_resource_file_to_relative_directory(
    const project_asset_copy_request_t& request, const std::filesystem::path& relative_directory,
    std::stop_token stop, bool inspect_only = false) -> project_asset_copy_result_t {
    namespace fs = std::filesystem;
    if (!request.asset || !request.project_directory.is_absolute() || request.source_path.empty() ||
        request.maximum_bytes == 0 ||
        (!request.source_path.is_absolute() && !valid_relative(request.source_path)) ||
        !valid_relative(relative_directory)) {
        return failure(project_asset_copy_status_t::invalid_request,
                       "Asset, project directory, or source path is invalid.");
    }
    const auto filename = request.source_path.filename();
    if (filename.empty() || filename == "." || filename == "..") {
        return failure(project_asset_copy_status_t::invalid_request, "Source has no filename.");
    }
    const auto source = request.source_path.is_absolute()
                            ? request.source_path
                            : request.project_directory / request.source_path;
    std::error_code error;
    const auto root_status = fs::symlink_status(request.project_directory, error);
    if (error || fs::is_symlink(root_status) || !fs::is_directory(root_status)) {
        return failure(project_asset_copy_status_t::invalid_request,
                       "Project directory is unavailable or is a symlink.");
    }
    if (!fs::is_regular_file(source, error) || error) {
        return failure(project_asset_copy_status_t::source_unavailable,
                       "Source file is unavailable.");
    }
    const auto size = fs::file_size(source, error);
    if (error || size > request.maximum_bytes) {
        return failure(project_asset_copy_status_t::invalid_request,
                       "Source file exceeds the copy limit or cannot be measured.");
    }
    auto directory = request.project_directory;
    for (const auto& component : relative_directory) {
        directory /= component;
        error.clear();
        const auto status = fs::symlink_status(directory, error);
        if (error == std::errc::no_such_file_or_directory) {
            error.clear();
            if (inspect_only)
                return failure(project_asset_copy_status_t::not_staged,
                               "Asset copy destination is available.");
            if (!fs::create_directory(directory, error) && error) {
                return failure(project_asset_copy_status_t::io_error,
                               "Could not create the asset destination directory.");
            }
            error.clear();
            const auto created = fs::symlink_status(directory, error);
            if (error || fs::is_symlink(created) || !fs::is_directory(created)) {
                return failure(project_asset_copy_status_t::invalid_request,
                               "Asset destination directory changed during creation.");
            }
        } else if (error || fs::is_symlink(status) || !fs::is_directory(status)) {
            return failure(project_asset_copy_status_t::invalid_request,
                           "Asset destination contains a symlink or non-directory component.");
        }
    }
    const auto relative =
        relative_directory / (std::to_string(request.asset.value) + "-" + filename.string());
    const auto destination = request.project_directory / relative;
    auto success = [&](project_asset_copy_status_t status) {
        return project_asset_copy_result_t{.status = status,
                                           .relative_path = relative,
                                           .absolute_path = destination,
                                           .message = {}};
    };
    if (stop.stop_requested()) {
        return failure(project_asset_copy_status_t::cancelled, "Copy cancelled.");
    }
    const auto destination_status = fs::symlink_status(destination, error);
    if (!error) {
        if (!fs::is_symlink(destination_status) && fs::is_regular_file(destination_status) &&
            same_bytes(source, destination, stop)) {
            return success(project_asset_copy_status_t::reused);
        }
        return failure(stop.stop_requested() ? project_asset_copy_status_t::cancelled
                                             : project_asset_copy_status_t::destination_collision,
                       "Asset destination filename already contains different data.");
    }
    if (error != std::errc::no_such_file_or_directory) {
        return failure(project_asset_copy_status_t::io_error,
                       "Could not inspect the asset destination.");
    }
    error.clear();
    if (inspect_only)
        return failure(project_asset_copy_status_t::not_staged,
                       "Asset copy destination is available.");
    static std::atomic<std::uint64_t> sequence{0};
    const auto temporary =
        directory / (".wormedit-copy-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                     "-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
    const auto copied = write_flushed(temporary, source, request.maximum_bytes, stop);
    if (copied != project_asset_copy_status_t::copied || stop.stop_requested() ||
        !same_bytes(source, temporary, stop)) {
        fs::remove(temporary, error);
        const auto status = stop.stop_requested() ? project_asset_copy_status_t::cancelled
                            : copied != project_asset_copy_status_t::copied
                                ? copied
                                : project_asset_copy_status_t::source_unavailable;
        return failure(status, "Source changed, copy failed, or copy was cancelled.");
    }
    if (stop.stop_requested()) {
        fs::remove(temporary, error);
        return failure(project_asset_copy_status_t::cancelled, "Copy cancelled.");
    }
#if defined(_WIN32)
    const auto installed =
        MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
#else
    const auto installed = ::link(temporary.c_str(), destination.c_str()) == 0;
#endif
    fs::remove(temporary, error);
    if (!installed) {
        const auto destination_status = fs::symlink_status(destination, error);
        if (!error && !fs::is_symlink(destination_status) &&
            fs::is_regular_file(destination_status) && same_bytes(source, destination, stop)) {
            return success(project_asset_copy_status_t::reused);
        }
        return failure(fs::exists(destination, error)
                           ? project_asset_copy_status_t::destination_collision
                           : project_asset_copy_status_t::io_error,
                       "Could not publish the project asset without overwriting a file.");
    }
#if !defined(_WIN32)
    const auto fd = ::open(directory.c_str(), O_RDONLY);
    if (fd >= 0) {
        (void)::fsync(fd);
        (void)::close(fd);
    }
#endif
    return success(project_asset_copy_status_t::copied);
}

auto copy_resource_file_into_project(const project_asset_copy_request_t& request,
                                     std::stop_token stop) -> project_asset_copy_result_t {
    return copy_resource_file_to_relative_directory(request, "assets", stop);
}

auto inspect_project_asset_copy(const project_asset_copy_request_t& request, std::stop_token stop)
    -> project_asset_copy_result_t {
    return copy_resource_file_to_relative_directory(request, "assets", stop, true);
}

auto export_asset_directory(const std::filesystem::path& relative_output_path)
    -> std::optional<std::filesystem::path> {
    if (!valid_relative(relative_output_path) || relative_output_path.filename().empty() ||
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
    if (!directory || !valid_relative(copied_project_relative_path) ||
        copied_project_relative_path.parent_path() != *directory ||
        copied_project_relative_path.filename().empty())
        return std::nullopt;
    return std::filesystem::path{"assets"} / copied_project_relative_path.filename();
}

auto copy_resource_file_into_export(const export_asset_copy_request_t& request,
                                    std::stop_token stop) -> project_asset_copy_result_t {
    const auto directory = export_asset_directory(request.relative_output_path);
    if (!directory) {
        return failure(project_asset_copy_status_t::invalid_request,
                       "Export output path must be a project-relative file or directory.");
    }
    const project_asset_copy_request_t copy{.project_directory = request.project_directory,
                                            .source_path = request.source_path,
                                            .asset = request.asset,
                                            .maximum_bytes = request.maximum_bytes};
    return copy_resource_file_to_relative_directory(copy, *directory, stop);
}

auto inspect_export_asset_copy(const export_asset_copy_request_t& request, std::stop_token stop)
    -> project_asset_copy_result_t {
    const auto directory = export_asset_directory(request.relative_output_path);
    if (!directory)
        return failure(project_asset_copy_status_t::invalid_request,
                       "Export output path must be a project-relative file or directory.");
    const project_asset_copy_request_t inspect{.project_directory = request.project_directory,
                                               .source_path = request.source_path,
                                               .asset = request.asset,
                                               .maximum_bytes = request.maximum_bytes};
    return copy_resource_file_to_relative_directory(inspect, *directory, stop, true);
}

} // namespace wormedit

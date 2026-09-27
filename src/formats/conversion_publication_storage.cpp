#include <wormedit/formats/conversion_publication.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace wormedit {
namespace {
namespace fs = std::filesystem;

struct temporary_directory_t {
    fs::path path;
    ~temporary_directory_t() {
        if (!path.empty()) {
            std::error_code error;
            fs::remove_all(path, error);
        }
    }
};

auto allowed(const conversion_publication_options_t& options, conversion_publication_stage_t stage)
    -> bool {
    return !options.allow_stage || options.allow_stage(stage);
}

auto sync_file(const fs::path& path) -> bool {
#if defined(_WIN32)
    const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    const auto okay = FlushFileBuffers(handle) != 0;
    CloseHandle(handle);
    return okay;
#else
    const auto handle = ::open(path.c_str(), O_RDONLY);
    if (handle < 0)
        return false;
    const auto okay = ::fsync(handle) == 0;
    ::close(handle);
    return okay;
#endif
}

auto sync_directory(const fs::path& path) -> bool {
#if defined(_WIN32)
    (void)path;
    return true;
#else
    const auto handle = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (handle < 0)
        return false;
    const auto okay = ::fsync(handle) == 0;
    ::close(handle);
    return okay;
#endif
}

auto same_bytes(const fs::path& path, const std::string& expected) -> bool {
    std::error_code error;
    if (fs::file_size(path, error) != expected.size() || error)
        return false;
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return false;
    std::array<char, 64 * 1024> buffer{};
    std::size_t offset{};
    while (offset < expected.size()) {
        const auto count = std::min(buffer.size(), expected.size() - offset);
        stream.read(buffer.data(), static_cast<std::streamsize>(count));
        if (stream.gcount() != static_cast<std::streamsize>(count) ||
            !std::equal(buffer.data(), buffer.data() + count, expected.data() + offset))
            return false;
        offset += count;
    }
    return true;
}

auto same_files(const fs::path& left, const fs::path& right) -> bool {
    std::error_code error;
    if (fs::file_size(left, error) != fs::file_size(right, error) || error)
        return false;
    std::ifstream a(left, std::ios::binary);
    std::ifstream b(right, std::ios::binary);
    if (!a || !b)
        return false;
    std::array<char, 64 * 1024> first{}, second{};
    do {
        a.read(first.data(), first.size());
        b.read(second.data(), second.size());
        if (a.gcount() != b.gcount() ||
            !std::equal(first.data(), first.data() + a.gcount(), second.data()))
            return false;
    } while (a.gcount() > 0);
    return a.eof() && b.eof();
}

auto copy_existing(const fs::path& source, const fs::path& staging,
                   const conversion_publication_options_t& options, std::string& failure) -> bool {
    std::error_code error;
    std::uintmax_t bytes{};
    std::size_t entries{};
    for (fs::recursive_directory_iterator it(source, error), end; it != end && !error;
         it.increment(error)) {
        const auto status = it->symlink_status(error);
        if (error || ++entries > options.maximum_existing_entries || fs::is_symlink(status) ||
            (!fs::is_directory(status) && !fs::is_regular_file(status))) {
            failure = "Existing output group exceeds entry limits or contains an unsafe entry.";
            return false;
        }
        const auto target = staging / it->path().lexically_relative(source);
        if (fs::is_directory(status)) {
            if (!fs::create_directory(target, error) || error) {
                failure = "Could not stage an existing output directory.";
                return false;
            }
        } else {
            const auto size = fs::file_size(it->path(), error);
            if (error || size > options.maximum_existing_bytes - bytes) {
                failure = "Existing output group exceeds copy limits.";
                return false;
            }
            bytes += size;
            if (!fs::copy_file(it->path(), target, fs::copy_options::none, error) || error ||
                !sync_file(target) || !same_files(it->path(), target)) {
                failure = "Could not copy and flush an existing output file.";
                return false;
            }
        }
    }
    if (error) {
        failure = "Could not traverse the existing output group.";
        return false;
    }
    return true;
}

auto sync_tree_directories(const fs::path& root) -> bool {
    std::error_code error;
    for (fs::recursive_directory_iterator it(root, error), end; it != end && !error;
         it.increment(error)) {
        if (it->is_directory(error) && !sync_directory(it->path()))
            return false;
    }
    return !error && sync_directory(root);
}

auto exchange_directories(const fs::path& staging, const fs::path& destination) -> bool {
#if defined(__APPLE__)
#ifndef RENAME_SWAP
#define RENAME_SWAP 0x00000002
#endif
    return ::renameatx_np(AT_FDCWD, staging.c_str(), AT_FDCWD, destination.c_str(), RENAME_SWAP) ==
           0;
#elif defined(__linux__) && defined(SYS_renameat2)
#ifndef RENAME_EXCHANGE
#define RENAME_EXCHANGE (1U << 1U)
#endif
    return ::syscall(SYS_renameat2, AT_FDCWD, staging.c_str(), AT_FDCWD, destination.c_str(),
                     RENAME_EXCHANGE) == 0;
#else
    (void)staging;
    (void)destination;
    return false;
#endif
}

auto install_new_directory(const fs::path& staging, const fs::path& destination) -> bool {
#if defined(_WIN32)
    return MoveFileExW(staging.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
#elif defined(__APPLE__)
#ifndef RENAME_EXCL
#define RENAME_EXCL 0x00000004
#endif
    return ::renamex_np(staging.c_str(), destination.c_str(), RENAME_EXCL) == 0;
#elif defined(__linux__) && defined(SYS_renameat2)
#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1U << 0U)
#endif
    return ::syscall(SYS_renameat2, AT_FDCWD, staging.c_str(), AT_FDCWD, destination.c_str(),
                     RENAME_NOREPLACE) == 0;
#else
    (void)staging;
    (void)destination;
    return false;
#endif
}

} // namespace

auto publish_conversion_group_atomic(const fs::path& project_directory, const output_group_t& group,
                                     const conversion_request_t& request,
                                     const conversion_result_t& converted,
                                     conversion_publication_options_t options)
    -> conversion_publication_result_t {
    conversion_publication_result_t result;
    const auto plan = plan_conversion_publication(project_directory, group, request, converted);
    if (!plan) {
        result.errors = plan.errors;
        return result;
    }
    const auto relative_directory = group.relative_output_path.has_extension()
                                        ? group.relative_output_path.parent_path()
                                        : group.relative_output_path;
    if (relative_directory.empty() || options.maximum_existing_bytes == 0 ||
        options.maximum_existing_entries == 0) {
        result.errors.emplace_back(
            "Output group needs a non-root directory and valid copy limits.");
        return result;
    }
    const auto destination = project_directory / relative_directory;
    const auto parent = destination.parent_path();
    std::error_code error;
    const auto parent_status = fs::symlink_status(parent, error);
    if (error || fs::is_symlink(parent_status) || !fs::is_directory(parent_status)) {
        result.errors.emplace_back("Output group parent is unavailable or is a symlink.");
        return result;
    }
    const auto existing = fs::symlink_status(destination, error);
    const auto destination_exists = !error;
    if (error == std::errc::no_such_file_or_directory)
        error.clear();
    if (error ||
        (destination_exists && (fs::is_symlink(existing) || !fs::is_directory(existing)))) {
        result.errors.emplace_back("Output group directory is unsafe or unavailable.");
        return result;
    }
    static std::atomic<std::uint64_t> sequence{1};
    temporary_directory_t staging;
    for (int attempt = 0; attempt < 32; ++attempt) {
        staging.path =
            parent / (".wormedit-export-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                      "-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        if (fs::create_directory(staging.path, error))
            break;
        staging.path.clear();
        if (error != std::errc::file_exists) {
            result.errors.emplace_back("Could not create an export staging directory.");
            return result;
        }
        error.clear();
    }
    if (staging.path.empty()) {
        result.errors.emplace_back("Could not reserve an export staging directory.");
        return result;
    }
    std::string failure;
    if (destination_exists && (!allowed(options, conversion_publication_stage_t::copy_existing) ||
                               !copy_existing(destination, staging.path, options, failure))) {
        result.errors.push_back(failure.empty() ? "Existing-output copy was interrupted."
                                                : failure);
        return result;
    }
    for (const auto& file : plan.files) {
        if (!allowed(options, conversion_publication_stage_t::write_output)) {
            result.errors.emplace_back("Output writing was interrupted.");
            return result;
        }
        const auto relative = file.project_relative_path.lexically_relative(relative_directory);
        const auto path = staging.path / relative;
        fs::create_directories(path.parent_path(), error);
        if (error) {
            result.errors.emplace_back("Could not create a staged chart directory.");
            return result;
        }
        const auto& bytes = converted.outputs[file.output_index].bytes;
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            stream.flush();
            if (!stream) {
                result.errors.emplace_back("Could not write a staged chart file.");
                return result;
            }
        }
        if (!sync_file(path) || !same_bytes(path, bytes)) {
            result.errors.emplace_back("Staged chart bytes could not be flushed or verified.");
            return result;
        }
    }
    if (!allowed(options, conversion_publication_stage_t::validate_staging)) {
        result.errors.emplace_back("Staging validation was interrupted.");
        return result;
    }
    for (const auto& asset : plan.assets) {
        const auto relative = asset.project_relative_path.lexically_relative(relative_directory);
        if (!fs::is_regular_file(staging.path / relative, error) || error) {
            result.errors.emplace_back("A staged asset was absent from the complete output set.");
            return result;
        }
    }
    if (!sync_tree_directories(staging.path) ||
        !allowed(options, conversion_publication_stage_t::exchange_directory)) {
        result.errors.emplace_back(
            "Output directory could not be flushed or publication was interrupted.");
        return result;
    }
    if (destination_exists) {
        if (!exchange_directories(staging.path, destination)) {
            result.errors.emplace_back(
                "Atomic replacement of the output directory is unavailable.");
            return result;
        }
    } else {
        if (!install_new_directory(staging.path, destination)) {
            result.errors.emplace_back("Atomic creation of the output directory failed.");
            return result;
        }
        staging.path.clear();
    }
    result.published = true;
    if (destination_exists) {
        fs::remove_all(staging.path, error);
        if (error)
            result.warnings.emplace_back(
                "Output was published, but old files could not be removed.");
        else
            staging.path.clear();
    }
    if (!sync_directory(parent))
        result.warnings.emplace_back("Output was published, but parent directory sync failed.");
    return result;
}

} // namespace wormedit

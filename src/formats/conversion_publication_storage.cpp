#include <wormedit/formats/conversion_publication.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string_view>
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

constexpr std::string_view manifest_name = ".wormedit-output-set";
constexpr std::string_view manifest_header = "wormedit-output-set-v2\n";
constexpr std::uintmax_t maximum_manifest_bytes = 1024 * 1024;
constexpr std::size_t maximum_manifest_entries = 8192;

struct file_identity_t {
    std::uintmax_t bytes{};
    std::uint64_t hash{};
    auto operator==(const file_identity_t&) const -> bool = default;
};

using output_ownership_t = std::map<std::string, std::map<std::string, file_identity_t>>;

auto sync_file(const fs::path& path) -> bool;
auto file_bytes_equal(const fs::path& path, const std::string& expected) -> bool;

auto file_identity(const fs::path& path) -> std::optional<file_identity_t> {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return std::nullopt;
    std::array<char, 64 * 1024> buffer{};
    file_identity_t identity{.bytes = 0, .hash = 14695981039346656037ULL};
    while (true) {
        stream.read(buffer.data(), buffer.size());
        const auto count = stream.gcount();
        if (count < 0 || identity.bytes > std::numeric_limits<std::uintmax_t>::max() -
                                              static_cast<std::uintmax_t>(count))
            return std::nullopt;
        identity.bytes += static_cast<std::uintmax_t>(count);
        for (std::streamsize index = 0; index < count; ++index) {
            identity.hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
            identity.hash *= 1099511628211ULL;
        }
        if (stream.eof())
            return identity;
        if (stream.bad() || stream.fail())
            return std::nullopt;
    }
}

auto hex_hash(std::uint64_t value) -> std::string {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index) {
        result[static_cast<std::size_t>(index)] = digits[value & 15U];
        value >>= 4U;
    }
    return result;
}

auto is_safe_field(std::string_view value) -> bool {
    return !value.empty() && value.size() <= 4096 &&
           std::ranges::none_of(value, [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

auto is_safe_name(std::string_view value) -> bool {
    const fs::path path{value};
    return value.size() <= 240 && is_safe_field(value) && value != manifest_name &&
           !path.is_absolute() && !path.has_root_name() && !path.has_parent_path() &&
           path.filename() == path && path != "." && path != "..";
}

auto read_ownership(const fs::path& directory, output_ownership_t& owners, std::string& failure)
    -> bool {
    const auto path = directory / manifest_name;
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return true;
    if (error || !fs::is_regular_file(status)) {
        failure = "Output ownership manifest is unsafe or exceeds its size limit.";
        return false;
    }
    const auto measured_size = fs::file_size(path, error);
    if (error || measured_size > maximum_manifest_bytes) {
        failure = "Output ownership manifest is unsafe or exceeds its size limit.";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>{stream},
                            std::istreambuf_iterator<char>{}};
    if (stream.bad() || bytes.size() != measured_size || !bytes.starts_with(manifest_header)) {
        failure = "Output ownership manifest has an invalid header or could not be read.";
        return false;
    }
    std::set<std::string> globally_owned;
    std::size_t count{};
    std::size_t position = manifest_header.size();
    while (position < bytes.size()) {
        const auto end = bytes.find('\n', position);
        const auto first_tab = bytes.find('\t', position);
        const auto second_tab =
            first_tab == std::string::npos ? std::string::npos : bytes.find('\t', first_tab + 1);
        const auto third_tab =
            second_tab == std::string::npos ? std::string::npos : bytes.find('\t', second_tab + 1);
        if (end == std::string::npos || first_tab == std::string::npos ||
            second_tab == std::string::npos || third_tab == std::string::npos || third_tab >= end ||
            ++count > maximum_manifest_entries) {
            failure = "Output ownership manifest has an invalid or excessive record.";
            return false;
        }
        const auto group = bytes.substr(position, first_tab - position);
        const auto filename = bytes.substr(first_tab + 1, second_tab - first_tab - 1);
        const auto size_text =
            std::string_view{bytes}.substr(second_tab + 1, third_tab - second_tab - 1);
        const auto hash_text = std::string_view{bytes}.substr(third_tab + 1, end - third_tab - 1);
        file_identity_t identity;
        const auto size_parse =
            std::from_chars(size_text.data(), size_text.data() + size_text.size(), identity.bytes);
        const auto hash_parse = std::from_chars(
            hash_text.data(), hash_text.data() + hash_text.size(), identity.hash, 16);
        if (!is_safe_field(group) || !is_safe_name(filename) || size_parse.ec != std::errc{} ||
            size_parse.ptr != size_text.data() + size_text.size() || hash_text.size() != 16 ||
            hash_parse.ec != std::errc{} || hash_parse.ptr != hash_text.data() + hash_text.size() ||
            hex_hash(identity.hash) != hash_text ||
            !owners[group].emplace(filename, identity).second ||
            !globally_owned.insert(filename).second) {
            failure = "Output ownership manifest contains an unsafe or duplicate path.";
            return false;
        }
        position = end + 1;
    }
    return true;
}

auto write_ownership(const fs::path& directory, const output_ownership_t& owners,
                     std::string& failure) -> bool {
    std::string bytes{manifest_header};
    std::size_t count{};
    for (const auto& [group, filenames] : owners) {
        if (!is_safe_field(group)) {
            failure = "Output ownership group path is invalid.";
            return false;
        }
        for (const auto& [filename, identity] : filenames) {
            if (!is_safe_name(filename) || ++count > maximum_manifest_entries) {
                failure = "Output ownership record is invalid or exceeds its entry limit.";
                return false;
            }
            bytes += group + "\t" + filename + "\t" + std::to_string(identity.bytes) + "\t" +
                     hex_hash(identity.hash) + "\n";
            if (bytes.size() > maximum_manifest_bytes) {
                failure = "Output ownership manifest exceeds its size limit.";
                return false;
            }
        }
    }
    const auto path = directory / manifest_name;
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream) {
            failure = "Could not write the output ownership manifest.";
            return false;
        }
    }
    if (!sync_file(path) || !file_bytes_equal(path, bytes)) {
        failure = "Could not flush or verify the output ownership manifest.";
        return false;
    }
    return true;
}

struct temporary_directory_t {
    fs::path path;
    ~temporary_directory_t() {
        if (!path.empty()) {
            std::error_code error;
            fs::remove_all(path, error);
        }
    }
};

auto is_allowed(const conversion_publication_options_t& options, conversion_publication_stage_t stage)
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

auto file_bytes_equal(const fs::path& path, const std::string& expected) -> bool {
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

auto files_equal(const fs::path& left, const fs::path& right) -> bool {
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
                !sync_file(target) || !files_equal(it->path(), target)) {
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

#if defined(_WIN32)
// Windows has no directory exchange primitive. Park the old output beside the staging directory,
// move the new one in, then put the old files where the staging directory was; every step rolls
// back to the original layout, so a failure never leaves the destination missing.
auto move_directory(const fs::path& from, const fs::path& to) -> bool {
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
}

auto exchange_directories_windows(const fs::path& staging, const fs::path& destination) -> bool {
    auto parked = staging;
    parked += L".swap";
    if (!move_directory(destination, parked)) {
        return false;
    }
    if (!move_directory(staging, destination)) {
        move_directory(parked, destination);
        return false;
    }
    // The new output is live; if the old files cannot move back they stay parked beside it.
    move_directory(parked, staging);
    return true;
}
#endif

auto exchange_directories(const fs::path& staging, const fs::path& destination) -> bool {
#if defined(_WIN32)
    return exchange_directories_windows(staging, destination);
#elif defined(__APPLE__)
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

auto collect_owned_chart_names(const conversion_publication_plan_payload_t& plan,
                               const fs::path& relative_directory, std::string_view group_key,
                               const output_ownership_t& ownership,
                               std::set<std::string>& current_files) -> std::optional<std::string> {
    for (const auto& file : plan.files) {
        const auto relative = file.project_relative_path.lexically_relative(relative_directory);
        const auto filename = relative.generic_string();
        if (!is_safe_name(filename) || !current_files.insert(filename).second) {
            return "A generated chart filename cannot be tracked safely.";
        }
    }
    for (const auto& file : plan.generated_files) {
        const auto name = file.lexically_relative(relative_directory).generic_string();
        if (!is_safe_name(name) || !current_files.insert(name).second)
            return "A generated audio filename cannot be tracked safely.";
    }
    for (const auto& [other_group, filenames] : ownership) {
        if (other_group == group_key) {
            continue;
        }
        for (const auto& filename : current_files) {
            if (filenames.contains(filename)) {
                return "A chart filename is owned by another output group.";
            }
        }
    }
    return std::nullopt;
}

auto prune_stale_chart_files(const fs::path& staging, const output_ownership_t& ownership,
                             std::string_view group_key,
                             const std::set<std::string>& current_files,
                             std::size_t& pruned_files) -> std::optional<std::string> {
    const auto previous = ownership.find(std::string{group_key});
    if (previous == ownership.end()) {
        return std::nullopt;
    }
    std::error_code error;
    for (const auto& [filename, recorded] : previous->second) {
        const auto old_path = staging / filename;
        const auto old_status = fs::symlink_status(old_path, error);
        if (error == std::errc::no_such_file_or_directory) {
            error.clear();
            continue;
        }
        if (error || !fs::is_regular_file(old_status)) {
            return "A tracked generated chart is no longer a regular file.";
        }
        const auto actual = file_identity(old_path);
        if (!actual) {
            return "Could not verify an earlier generated chart.";
        }
        if (*actual != recorded) {
            if (current_files.contains(filename)) {
                return "A generated chart was edited since publication; move it before replacing.";
            }
            continue;
        }
        if (current_files.contains(filename)) {
            continue;
        }
        const auto removed = fs::remove(old_path, error);
        if (error) {
            return "Could not remove a stale generated chart from staging.";
        }
        pruned_files += removed ? 1 : 0;
    }
    return std::nullopt;
}

struct staged_chart_write_t {
    const conversion_publication_plan_payload_t& plan;
    const conversion_result_t& converted;
    const conversion_publication_options_t& options;
    const fs::path& relative_directory;
    const fs::path& staging;
    output_ownership_t& ownership;
    const std::string& group_key;
};

auto write_generated_charts(staged_chart_write_t context) -> std::optional<std::string> {
    std::error_code error;
    context.ownership[context.group_key].clear();
    for (const auto& file : context.plan.files) {
        if (!is_allowed(context.options, conversion_publication_stage_t::write_output)) {
            return "Output writing was interrupted.";
        }
        const auto relative =
            file.project_relative_path.lexically_relative(context.relative_directory);
        const auto path = context.staging / relative;
        fs::create_directories(path.parent_path(), error);
        if (error) {
            return "Could not create a staged chart directory.";
        }
        const auto& bytes = context.converted.outputs[file.output_index].bytes;
        {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            stream.flush();
            if (!stream) {
                return "Could not write a staged chart file.";
            }
        }
        if (!sync_file(path) || !file_bytes_equal(path, bytes)) {
            return "Staged chart bytes could not be flushed or verified.";
        }
        const auto identity = file_identity(path);
        if (!identity || identity->bytes != bytes.size()) {
            return "Could not identify a staged chart file.";
        }
        context.ownership[context.group_key].emplace(relative.generic_string(), *identity);
    }
    return std::nullopt;
}

auto write_generated_audio(staged_chart_write_t context) -> std::optional<std::string> {
    if (context.plan.generated_files.empty()) return std::nullopt;
    if (!context.options.stage_generated_files) return "Generated audio staging callback is required.";
    std::error_code error;
    for (const auto& file : context.plan.generated_files) {
        const auto relative = file.lexically_relative(context.relative_directory);
        fs::remove(context.staging / relative, error);
        if (error) return "Cannot prepare a generated audio staging destination.";
    }
    if (auto failure = context.options.stage_generated_files(context.staging)) return failure;
    for (const auto& file : context.plan.generated_files) {
        const auto relative = file.lexically_relative(context.relative_directory);
        const auto path = context.staging / relative;
        const auto status = fs::symlink_status(path, error);
        if (error || !fs::is_regular_file(status) || !sync_file(path))
            return "Generated audio was absent, unsafe, or could not be flushed.";
        const auto identity = file_identity(path);
        if (!identity) return "Cannot identify generated audio.";
        context.ownership[context.group_key].emplace(relative.generic_string(), *identity);
    }
    return std::nullopt;
}

auto reserve_publication_staging(const fs::path& parent, const fs::path& destination,
                                 temporary_directory_t& staging, bool& destination_exists)
    -> std::optional<std::string> {
    std::error_code error;
    const auto parent_status = fs::symlink_status(parent, error);
    if (error || fs::is_symlink(parent_status) || !fs::is_directory(parent_status)) {
        return "Output group parent is unavailable or is a symlink.";
    }
    const auto existing = fs::symlink_status(destination, error);
    destination_exists = !error;
    if (error == std::errc::no_such_file_or_directory) {
        error.clear();
    }
    if (error ||
        (destination_exists && (fs::is_symlink(existing) || !fs::is_directory(existing)))) {
        return "Output group directory is unsafe or unavailable.";
    }
    static std::atomic<std::uint64_t> sequence{1};
    for (int attempt = 0; attempt < 32; ++attempt) {
        staging.path = parent /
                       (".wormedit-export-" +
                        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                        "-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
        if (fs::create_directory(staging.path, error)) {
            return std::nullopt;
        }
        staging.path.clear();
        if (error != std::errc::file_exists) {
            return "Could not create an export staging directory.";
        }
        error.clear();
    }
    return "Could not reserve an export staging directory.";
}

struct publication_commit_t {
    const conversion_publication_plan_payload_t& plan;
    const conversion_publication_options_t& options;
    temporary_directory_t& staging;
    const fs::path& relative_directory;
    const fs::path& destination;
    const fs::path& parent;
    const output_ownership_t& ownership;
    bool destination_exists{};
    conversion_publication_outcome_t& result;
};

void install_staged_conversion(publication_commit_t context) {
    if (!is_allowed(context.options, conversion_publication_stage_t::validate_staging)) {
        context.result.diagnostics.errors.emplace_back("Staging validation was interrupted.");
        return;
    }
    std::error_code error;
    for (const auto& asset : context.plan.assets) {
        const auto relative =
            asset.project_relative_path.lexically_relative(context.relative_directory);
        if (!fs::is_regular_file(context.staging.path / relative, error) || error) {
            context.result.diagnostics.errors.emplace_back(
                "A staged asset was absent from the complete output set.");
            return;
        }
    }
    std::string failure;
    if (!is_allowed(context.options, conversion_publication_stage_t::update_manifest) ||
        !write_ownership(context.staging.path, context.ownership, failure)) {
        context.result.diagnostics.errors.push_back(failure.empty() ? "Output ownership update was interrupted."
                                                        : failure);
        return;
    }
    if (!sync_tree_directories(context.staging.path) ||
        !is_allowed(context.options, conversion_publication_stage_t::exchange_directory)) {
        context.result.diagnostics.errors.emplace_back(
            "Output directory could not be flushed or publication was interrupted.");
        return;
    }
    if (context.destination_exists) {
        if (!exchange_directories(context.staging.path, context.destination)) {
            context.result.diagnostics.errors.emplace_back(
                "Atomic replacement of the output directory is unavailable.");
            return;
        }
    } else {
        if (!install_new_directory(context.staging.path, context.destination)) {
            context.result.diagnostics.errors.emplace_back("Atomic creation of the output directory failed.");
            return;
        }
        context.staging.path.clear();
    }
    context.result.value.published = true;
    if (context.destination_exists) {
        fs::remove_all(context.staging.path, error);
        if (error) {
            context.result.diagnostics.warnings.emplace_back(
                "Output was published, but old files could not be removed.");
        } else {
            context.staging.path.clear();
        }
    }
    if (!sync_directory(context.parent)) {
        context.result.diagnostics.warnings.emplace_back(
            "Output was published, but parent directory sync failed.");
    }
}

} // namespace

auto create_conversion_group(const fs::path& project_directory, const output_group_t& group,
                                     const conversion_request_t& request,
                                     const conversion_result_t& converted,
                                     conversion_publication_options_t options)
    -> conversion_publication_outcome_t {
    conversion_publication_outcome_t result{};
    auto planned = conversion_publication_plan_payload_t::from(project_directory, group, request, converted, options.destination_is_directory);
    if (!planned) {
        result.diagnostics.errors.push_back(std::move(planned.error().message));
        return result;
    }
    const auto& plan = *planned;
    if (!plan.generated_files.empty() && !options.stage_generated_files) {
        result.diagnostics.errors.emplace_back("Generated audio staging callback is required.");
        return result;
    }
    const auto relative_directory = (!options.destination_is_directory && group.relative_output_path.has_extension())
                                        ? group.relative_output_path.parent_path()
                                        : group.relative_output_path;
    if (relative_directory.empty() || options.maximum_existing_bytes == 0 ||
        options.maximum_existing_entries == 0) {
        result.diagnostics.errors.emplace_back(
            "Output group needs a non-root directory and valid copy limits.");
        return result;
    }
    const auto destination = project_directory / relative_directory;
    const auto parent = destination.parent_path();
    temporary_directory_t staging;
    bool destination_exists{};
    if (auto staging_failure =
            reserve_publication_staging(parent, destination, staging, destination_exists)) {
        result.diagnostics.errors.push_back(std::move(*staging_failure));
        return result;
    }
    std::error_code error;
    std::string failure;
    if (destination_exists && (!is_allowed(options, conversion_publication_stage_t::copy_existing) ||
                               !copy_existing(destination, staging.path, options, failure))) {
        result.diagnostics.errors.push_back(failure.empty() ? "Existing-output copy was interrupted."
                                                : failure);
        return result;
    }
    output_ownership_t ownership;
    if (!read_ownership(staging.path, ownership, failure)) {
        result.diagnostics.errors.push_back(std::move(failure));
        return result;
    }
    const auto group_key = group.relative_output_path.generic_string();
    std::set<std::string> current_files;
    if (auto failure = collect_owned_chart_names(plan, relative_directory, group_key, ownership,
                                                 current_files)) {
        result.diagnostics.errors.push_back(std::move(*failure));
        return result;
    }
    std::size_t pruned_files{};
    if (auto failure = prune_stale_chart_files(staging.path, ownership, group_key, current_files,
                                              pruned_files)) {
        result.diagnostics.errors.push_back(std::move(*failure));
        return result;
    }
    if (auto write_failure = write_generated_charts(
            {plan, converted, options, relative_directory, staging.path, ownership, group_key})) {
        result.diagnostics.errors.push_back(std::move(*write_failure));
        return result;
    }
    if (auto failure = write_generated_audio(
            {plan, converted, options, relative_directory, staging.path, ownership, group_key})) {
        result.diagnostics.errors.push_back(std::move(*failure));
        return result;
    }
    install_staged_conversion({plan, options, staging, relative_directory, destination, parent,
                               ownership, destination_exists, result});
    if (result.value.published) {
        result.value.pruned_files = pruned_files;
    }
    return result;
}

auto conversion_publication_result_t::from(conversion_publication_outcome_t&& outcome) -> conversion_publication_result_t {
    return {.published = outcome.value.published, .pruned_files = outcome.value.pruned_files,
        .errors = std::move(outcome.diagnostics.errors), .warnings = std::move(outcome.diagnostics.warnings)};
}

auto publish_conversion_group_atomic(const fs::path& project_directory, const output_group_t& group,
    const conversion_request_t& request, const conversion_result_t& converted,
    conversion_publication_options_t options) -> conversion_publication_result_t {
    return conversion_publication_result_t::from(create_conversion_group(project_directory, group, request, converted, std::move(options)));
}

} // namespace wormedit

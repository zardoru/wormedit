#pragma once

#include <wormedit/formats/native_project.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit {

enum class native_project_storage_diagnostic_code_t {
    invalid_destination,
    serialization_failure,
    validation_failure,
    identity_mismatch,
    checksum_failure,
    checksum_mismatch,
    resource_limit,
    invalid_interval,
    temporary_file_failure,
    write_failure,
    flush_failure,
    read_failure,
    replacement_failure,
};

struct native_project_storage_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    native_project_storage_diagnostic_code_t code{
        native_project_storage_diagnostic_code_t::serialization_failure};
    std::string message;
    std::filesystem::path path;
};

enum class native_project_storage_stage_t {
    serialize,
    preflight_validate,
    checksum,
    create_temporary,
    write_temporary,
    flush_temporary,
    close_temporary,
    reopen_temporary,
    validate_reopened,
    replace_destination,
    sync_destination_directory,
};

// The callback is a narrow fault-injection seam. Returning false makes the
// current operation fail before it runs; production callers leave it empty.
struct native_project_storage_options_t {
    native_project_limits_t codec_limits{};
    std::function<bool(native_project_storage_stage_t, const std::filesystem::path&)> allow_stage;
};

struct native_project_storage_result_t {
    bool published{};
    std::vector<native_project_storage_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
};

struct native_project_document_version_t {
    song_id_t project_id;
    std::uint64_t revision{};

    auto operator==(const native_project_document_version_t&) const -> bool = default;
};

struct native_project_recovery_metadata_t {
    std::filesystem::path original_source_path;
    std::filesystem::path intended_project_path;
    std::optional<std::uint64_t> last_explicit_save_revision;
    std::chrono::system_clock::time_point utc_write_time{std::chrono::system_clock::now()};
    std::uint64_t session_sequence{};
};

struct native_project_recovery_snapshot_t {
    song_document_t song;
    native_project_recovery_metadata_t metadata;
};

struct native_project_recovery_parse_result_t {
    std::optional<native_project_recovery_snapshot_t> snapshot;
    std::vector<native_project_storage_diagnostic_t> diagnostics;

    [[nodiscard]] auto has_errors() const -> bool;
};

// Uses the documented adjacent name `<project-filename>.bak`.
[[nodiscard]] auto native_project_adjacent_backup_path(const std::filesystem::path& primary)
    -> std::filesystem::path;

// Atomically publish the native project JSON after a semantic re-open check.
// Serialization and preflight validation finish before a temporary is created.
// For both atomic publishers below, a directory-sync failure after replacement
// is reported as a warning with published=true; rolling back that replacement
// is not reliable.
[[nodiscard]] auto publish_native_project_atomic(const std::filesystem::path& destination,
                                                 const song_document_t& song,
                                                 native_project_storage_options_t options = {})
    -> native_project_storage_result_t;

// Atomically publish a checksummed recovery envelope containing a complete
// native-project payload. The destination (including an untitled-session
// recovery path) is always supplied by the caller.
[[nodiscard]] auto publish_native_project_recovery_atomic(
    const std::filesystem::path& destination, const song_document_t& song,
    native_project_recovery_metadata_t metadata, native_project_storage_options_t options = {})
    -> native_project_storage_result_t;

// Decode and validate an envelope independently of filesystem scanning. The
// payload checksum, native schema, project ID, and revision must all agree.
[[nodiscard]] auto parse_native_project_recovery_snapshot(std::string_view bytes,
                                                          native_project_limits_t limits = {})
    -> native_project_recovery_parse_result_t;

enum class native_project_autosave_status_t {
    not_due,
    published,
    failed,
};

struct native_project_autosave_result_t {
    native_project_autosave_status_t status{native_project_autosave_status_t::not_due};
    std::uint64_t revision{};
    std::uint64_t session_sequence{};
    std::vector<native_project_storage_diagnostic_t> diagnostics;
};

// A session-clocked, headless coordinator. It writes at most once for a given
// project revision and only when dirty and due. Recovery destinations and the
// original import path are caller-owned; no platform recovery directory is
// selected here.
class native_project_autosave_coordinator_t {
  public:
    using clock_t = std::chrono::steady_clock;

    explicit native_project_autosave_coordinator_t(
        clock_t::time_point session_started_at,
        std::optional<native_project_document_version_t> last_explicit_save = {},
        std::chrono::steady_clock::duration interval = std::chrono::minutes{5},
        std::uint64_t initial_session_sequence = 0);

    void mark_explicit_save(native_project_document_version_t saved_version) noexcept;

    [[nodiscard]] auto maybe_autosave(const song_document_t& song, clock_t::time_point now,
                                      const std::filesystem::path& caller_supplied_backup_path,
                                      std::filesystem::path original_source_path = {},
                                      std::filesystem::path intended_project_path = {},
                                      native_project_storage_options_t options = {})
        -> native_project_autosave_result_t;

    [[nodiscard]] auto last_autosaved_version() const noexcept
        -> std::optional<native_project_document_version_t>;
    [[nodiscard]] auto session_sequence() const noexcept -> std::uint64_t;

  private:
    clock_t::time_point last_successful_autosave_at_;
    std::chrono::steady_clock::duration interval_;
    std::optional<native_project_document_version_t> last_explicit_save_;
    std::optional<native_project_document_version_t> last_autosaved_;
    std::uint64_t session_sequence_{};
    bool interval_valid_{};
};

} // namespace wormedit

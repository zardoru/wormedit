#pragma once

#include "sys/project/project_file_types.h"

#include <wormedit/core/document.h>
#include <wormedit/core/command.h>
#include <wormedit/core/async_job.h>
#include <wormedit/core/job_progress.h>
#include "ui/frontend/file_jobs.h"
#include <wormedit/core/result.h>
#include <wormedit/formats/native_project_storage.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::sys {

class project_chart_runtime_t;

struct pending_import_t {
    std::string audio_project_path;
    std::string chart_project_path;
    std::string bms_encoding_path;
    bool bms_profile_required{};
    text_encoding_t bms_encoding{};
};

struct project_identity_t {
    bool session_loaded{};
    bool native_project_loaded{};
    bool fresh_blank_project{};
    pending_import_t pending_import;
    std::filesystem::path active_project_path;
    std::filesystem::path asset_base;
    std::optional<native_project_document_version_t> scanned_primary_version;
    std::filesystem::path external_player_source_path;

    [[nodiscard]] auto has_project_folder() const -> bool {
        return session_loaded && !asset_base.empty();
    }
};

// Generation changes before any project state, so captured work is stale first.
// The dropped-chart request retains its existing consume-on-dispatch lifetime.
void begin_project_session(project_identity_t& current, project_chart_runtime_t& charts,
                           project_identity_t replacement);

struct project_open_result_t {
    [[nodiscard]] auto is_success() const noexcept -> bool { return document.has_value(); }

    std::optional<song_document_t> document;
    std::filesystem::path source_path;
    std::filesystem::path asset_base;
    bool native_project{};
    // A BMS-family source contains non-ASCII bytes without a UTF-8 BOM and
    // needs the caller to choose an encoding before retrying this same path.
    bool needs_encoding_choice{};
    bool needs_profile_choice{};
    std::optional<text_encoding_t> bms_encoding;
    std::string error;
    // Nonfatal osu import quantization and asset-resolution diagnostics.
    std::vector<std::string> warnings;
};

// Refreshes exact audio file availability for documents loaded through startup,
// recovery or session replacement. Decoder support does not affect missing state;
// aliases share the per-asset result, without changing revision/history.
void refresh_project_audio_availability(song_document_t& document,
                                        const std::filesystem::path& asset_base);

// Applies load-time normalization and graph validation before a parsed document
// can replace a session. Repairs change storage only; rejection clears the
// proposed document and leaves its caller's active session untouched.
void validate_opened_document(project_open_result_t& result);

// Reads and parses a selected project or chart without changing the active
// workspace. Only an opened .wormedit is an existing saved native project;
// imported charts are new unsaved sessions rooted at their source directory.
[[nodiscard]] auto open_project_file(
    const std::filesystem::path& selected_path,
    std::optional<text_encoding_t> encoding_override = {},
    std::stop_token stop = {}, job_progress_t* progress = nullptr,
    std::optional<std::string> lane_profile_id = {})
    -> project_open_result_t;

struct project_open_request_t {
    std::filesystem::path path;
    std::optional<text_encoding_t> encoding;
    std::optional<std::string> lane_profile_id;
    std::shared_ptr<job_progress_t> progress;
};
struct project_open_identity_t {
    document_stamp_t document;
    std::uint64_t session_generation{};
    auto operator==(const project_open_identity_t&) const -> bool = default;
};
using project_open_job_t = async_job_t<project_open_request_t, std::unique_ptr<project_open_result_t>,
                                      project_open_identity_t>;
class project_open_runtime_t {
public:
    explicit project_open_runtime_t(task_scheduler_t& scheduler);
    auto start(std::filesystem::path path, std::optional<text_encoding_t> encoding,
               project_open_identity_t identity,
               project_open_job_t::work_function_t work = open,
               std::optional<std::string> lane_profile_id = {}) -> async_job_start_outcome_t;
    auto poll() -> std::optional<project_open_job_t::completion_t> { return collect(false); }
    void cancel();
    void shutdown() { job_.shutdown(); }
    auto wait() -> std::optional<project_open_job_t::completion_t> { return collect(true); }
    [[nodiscard]] auto is_busy() const -> bool { return job_.is_busy(); }
    [[nodiscard]] auto view() const -> ui::file_job_view_t;
    [[nodiscard]] auto is_encoding_retry() const -> bool { return encoding_retry_; }
    static auto open(const project_open_request_t& request, std::stop_token stop)
        -> project_open_job_t::work_outcome_t;
private:
    auto collect(bool wait) -> std::optional<project_open_job_t::completion_t>;
    project_open_job_t job_;
    std::shared_ptr<job_progress_t> progress_;
    bool cancelling_{};
    bool encoding_retry_{};
};

} // namespace wormedit::sys

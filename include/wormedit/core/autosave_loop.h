#pragma once

#include <wormedit/core/task_graph.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/command_journal.h>
#include <wormedit/formats/native_project_storage.h>

#include <chrono>
#include <filesystem>
#include <optional>

namespace wormedit {

struct document_history_t;

struct native_project_autosave_target_t {
    // Saved native projects keep the backup next to the primary. Sessions without
    // a native primary use their stable project ID inside the platform recovery
    // directory. An empty backup_path means no safe destination is available.
    [[nodiscard]] static auto from(song_id_t project_id, bool has_native_primary,
                                   const std::filesystem::path& native_primary_path,
                                   const std::filesystem::path& platform_recovery_directory,
                                   std::filesystem::path original_source_path = {})
        -> native_project_autosave_target_t;

    std::filesystem::path backup_path;
    std::filesystem::path original_source_path;
    std::filesystem::path intended_project_path;
    // Relative media assets are anchored to this live session directory.
    // Frozen generator recipes retain their project-relative reload origins.
    std::filesystem::path asset_base;
    void anchor_assets(song_document_t&) const;
};

[[nodiscard]] auto
native_project_recovery_session_sequence(const native_project_recovery_scan_outcome_t& scan,
                                         song_id_t project_id) noexcept -> std::uint64_t;

// Calls are cheap while the cadence is not due. When due, the document is
// copied on the caller thread to take a consistent snapshot and bounded storage
// work runs on a single scheduler worker. Failed writes are retried after
// retry_delay.
class native_project_autosave_loop_t {
  public:
    using clock_t = native_project_autosave_coordinator_t::clock_t;

    explicit native_project_autosave_loop_t(
        clock_t::time_point session_started_at, task_scheduler_t& scheduler,
        std::optional<native_project_document_version_t> last_explicit_save = {},
        std::uint64_t initial_session_sequence = 0,
        clock_t::duration interval = std::chrono::minutes{5},
        clock_t::duration retry_delay = std::chrono::seconds{30},
        notification_sink_t notifications = notification_sink_t::muted(),
        command_journal_limits_t journal_limits = {});
    ~native_project_autosave_loop_t();

    native_project_autosave_loop_t(const native_project_autosave_loop_t&) = delete;
    auto operator=(const native_project_autosave_loop_t&)
        -> native_project_autosave_loop_t& = delete;

    // Preserve the session and last successful publication; pending workers keep their snapshot.
    [[nodiscard]] auto set_interval(clock_t::duration interval) noexcept -> bool;
    void mark_explicit_save(native_project_document_version_t saved_version) noexcept;
    void mark_explicit_save(native_project_document_version_t saved_version, const song_document_t& current);
    void observe(document_history_t&, const native_project_autosave_target_t&);
    void flush_journal();
    [[nodiscard]] auto
    reset_session(clock_t::time_point session_started_at,
                  std::optional<native_project_document_version_t> last_explicit_save,
                  std::uint64_t initial_session_sequence) -> bool;

    // Returns a result only when a worker has completed. A dirty document with
    // no destination is left untouched and will be considered again on a later
    // tick if the caller obtains a platform recovery directory.
    [[nodiscard]] auto tick(const song_document_t& song, bool dirty, clock_t::time_point now,
                            const native_project_autosave_target_t& target,
                            native_project_storage_options_t options = {})
        -> std::optional<native_project_autosave_outcome_t>;

    [[nodiscard]] auto is_busy() const noexcept -> bool;
    [[nodiscard]] auto session_sequence() const noexcept -> std::uint64_t;

  private:
    void prepare_publication(const song_document_t&);
    struct completion_t {
        native_project_autosave_coordinator_t coordinator;
        native_project_autosave_outcome_t result;
        std::optional<native_project_journal_position_t> journal_position;
    };

    struct worker_t {
        task_graph_handle_t graph;
        task_id_t task;
    };

    void collect_completed(clock_t::time_point now,
                           std::optional<native_project_autosave_outcome_t>& result);

    task_scheduler_t& scheduler_;
    notification_sink_t notifications_{notification_sink_t::muted()};
    native_project_autosave_coordinator_t coordinator_;
    command_journal_runtime_t journal_;
    clock_t::duration interval_;
    clock_t::duration retry_delay_;
    std::optional<clock_t::time_point> retry_after_;
    std::optional<native_project_document_version_t> deferred_explicit_save_;
    std::optional<worker_t> worker_;
};

} // namespace wormedit

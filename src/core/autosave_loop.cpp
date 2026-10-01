#include <wormedit/core/autosave_loop.h>

#include <algorithm>
#include <string>
#include <utility>

namespace wormedit {

auto make_native_project_autosave_target(song_id_t project_id, bool has_native_primary,
                                         const std::filesystem::path& native_primary_path,
                                         const std::filesystem::path& platform_recovery_directory,
                                         std::filesystem::path original_source_path)
    -> native_project_autosave_target_t {
    native_project_autosave_target_t target{.original_source_path =
                                                std::move(original_source_path)};
    if (has_native_primary && !native_primary_path.empty()) {
        target.backup_path = native_project_adjacent_backup_path(native_primary_path);
        target.intended_project_path = native_primary_path;
    } else if (project_id && !platform_recovery_directory.empty()) {
        target.backup_path = platform_recovery_directory /
                             ("project-" + std::to_string(project_id.value) + ".wormedit.bak");
    }
    return target;
}

auto native_project_recovery_session_sequence(const native_project_recovery_scan_result_t& scan,
                                              song_id_t project_id) noexcept -> std::uint64_t {
    std::uint64_t sequence{};
    for (const auto& candidate : scan.candidates) {
        if (candidate.status != native_project_recovery_candidate_status_t::valid ||
            !candidate.version || candidate.version->project_id != project_id ||
            !candidate.recovery_metadata) {
            continue;
        }
        sequence = std::max(sequence, candidate.recovery_metadata->session_sequence);
    }
    return sequence;
}

native_project_autosave_loop_t::native_project_autosave_loop_t(
    clock_t::time_point session_started_at, task_scheduler_t& scheduler,
    std::optional<native_project_document_version_t> last_explicit_save,
    std::uint64_t initial_session_sequence, clock_t::duration interval,
    clock_t::duration retry_delay)
    : scheduler_(scheduler), coordinator_(session_started_at, std::move(last_explicit_save),
                                          interval, initial_session_sequence),
      interval_(interval),
      retry_delay_(retry_delay > clock_t::duration::zero() ? retry_delay
                                                           : clock_t::duration::zero()) {}

native_project_autosave_loop_t::~native_project_autosave_loop_t() = default;

auto native_project_autosave_loop_t::set_interval(clock_t::duration interval) noexcept -> bool {
    if (!coordinator_.set_interval(interval)) return false;
    interval_ = interval;
    return true;
}

void native_project_autosave_loop_t::mark_explicit_save(
    native_project_document_version_t saved_version) noexcept {
    if (busy()) {
        deferred_explicit_save_ = saved_version;
        return;
    }
    coordinator_.mark_explicit_save(saved_version);
}

auto native_project_autosave_loop_t::reset_session(
    clock_t::time_point session_started_at,
    std::optional<native_project_document_version_t> last_explicit_save,
    std::uint64_t initial_session_sequence) -> bool {
    if (worker_)
        return false;
    coordinator_ = native_project_autosave_coordinator_t(
        session_started_at, std::move(last_explicit_save), interval_, initial_session_sequence);
    retry_after_.reset();
    deferred_explicit_save_.reset();
    return true;
}

auto native_project_autosave_loop_t::tick(const song_document_t& song, bool dirty,
                                          clock_t::time_point now,
                                          const native_project_autosave_target_t& target,
                                          native_project_storage_options_t options)
    -> std::optional<native_project_autosave_result_t> {
    std::optional<native_project_autosave_result_t> completed;
    collect_completed(now, completed);
    if (busy() || !dirty || target.backup_path.empty() || (retry_after_ && now < *retry_after_) ||
        !coordinator_.is_due(song, now)) {
        return completed;
    }

    auto snapshot = song;
    auto coordinator = coordinator_;
    const auto backup_path = target.backup_path;
    auto original_source_path = target.original_source_path;
    auto intended_project_path = target.intended_project_path;
    task_graph_t graph(1);
    const auto added = graph.add_task(
        {},
        [coordinator = std::move(coordinator), snapshot = std::move(snapshot), now, backup_path,
         original_source_path = std::move(original_source_path),
         intended_project_path = std::move(intended_project_path), options = std::move(options)](
            std::span<const task_result_t>, std::stop_token) mutable -> task_result_t {
            auto result = coordinator.maybe_autosave(
                snapshot, now, backup_path, std::move(original_source_path),
                std::move(intended_project_path), std::move(options));
            auto completion = std::make_shared<const completion_t>(
                completion_t{.coordinator = std::move(coordinator), .result = std::move(result)});
            return task_result_t::success(make_task_value(std::move(completion)));
        });
    if (!added) {
        native_project_autosave_result_t failed{.status = native_project_autosave_status_t::failed,
                                                .revision = song.revision,
                                                .session_sequence =
                                                    coordinator_.session_sequence()};
        failed.diagnostics.push_back(
            {.code = native_project_storage_diagnostic_code_t::write_failure,
             .message = added.error ? added.error->message : "could not queue autosave task",
             .path = target.backup_path});
        retry_after_ = now + retry_delay_;
        return failed;
    }
    auto submitted = scheduler_.submit(std::move(graph));
    if (!submitted) {
        native_project_autosave_result_t failed{.status = native_project_autosave_status_t::failed,
                                                .revision = song.revision,
                                                .session_sequence =
                                                    coordinator_.session_sequence()};
        failed.diagnostics.push_back(
            {.code = native_project_storage_diagnostic_code_t::write_failure,
             .message =
                 submitted.error ? submitted.error->message : "could not queue autosave task",
             .path = target.backup_path});
        retry_after_ = now + retry_delay_;
        return failed;
    }
    worker_ = worker_t{.graph = std::move(submitted.graph), .task = *added.id};
    retry_after_.reset();
    return completed;
}

auto native_project_autosave_loop_t::busy() const noexcept -> bool {
    return worker_.has_value();
}

auto native_project_autosave_loop_t::session_sequence() const noexcept -> std::uint64_t {
    return coordinator_.session_sequence();
}

void native_project_autosave_loop_t::collect_completed(
    clock_t::time_point now, std::optional<native_project_autosave_result_t>& result) {
    if (!worker_)
        return;
    const auto task_result = worker_->graph.result(worker_->task);
    if (!task_result)
        return;
    const auto completion = task_value_cast<completion_t>(*task_result);
    worker_.reset();
    if (!completion) {
        native_project_autosave_result_t failed{.status = native_project_autosave_status_t::failed,
                                                .session_sequence =
                                                    coordinator_.session_sequence()};
        failed.diagnostics.push_back(
            {.code = native_project_storage_diagnostic_code_t::write_failure,
             .message = task_result->error ? task_result->error->message
                                           : "autosave worker returned no result"});
        if (deferred_explicit_save_) {
            coordinator_.mark_explicit_save(*deferred_explicit_save_);
            deferred_explicit_save_.reset();
        }
        retry_after_ = now + retry_delay_;
        result = std::move(failed);
        return;
    }
    coordinator_ = completion->coordinator;
    (void)coordinator_.set_interval(interval_);
    if (deferred_explicit_save_) {
        coordinator_.mark_explicit_save(*deferred_explicit_save_);
        deferred_explicit_save_.reset();
    }
    if (completion->result.status == native_project_autosave_status_t::failed)
        retry_after_ = now + retry_delay_;
    result = completion->result;
}

} // namespace wormedit

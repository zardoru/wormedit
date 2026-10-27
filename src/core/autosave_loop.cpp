#include <wormedit/core/autosave_loop.h>
#include <wormedit/core/command.h>

#include <algorithm>
#include <string>
#include <utility>

namespace wormedit {
namespace {

void anchor_recovery_asset_paths(song_document_t& snapshot, const std::filesystem::path& base) {
    if (base.empty()) return;
    std::error_code error;
    const auto absolute = std::filesystem::absolute(base, error);
    if (error) return;
    const auto anchor = [&](std::filesystem::path& path) {
        if (!path.empty() && path.is_relative()) path = (absolute / path).lexically_normal();
    };
    const auto assets = [&](auto& rows) {
        for (auto& row : rows) anchor(row.source_path);
    };
    for (auto& chart : snapshot.charts) chart.metadata.overrides.transform_audio_paths(anchor);
    assets(snapshot.audio_assets);
    assets(snapshot.visual_assets);
    assets(snapshot.midi_assets);
    // Generator sources are frozen in the recipe. Its project-relative path is
    // a reload origin, not a media asset; making it absolute invalidates the
    // same native payload used by both autosave and command journals.
    for (auto& asset : snapshot.audio_assets)
        if (asset.waveform_cache_path) anchor(*asset.waveform_cache_path);
}

void post_autosave_failure(const notification_sink_t& notifications,
                           const native_project_autosave_outcome_t& result) {
    if (notifications.is_muted() || result.value.status != native_project_autosave_status_t::failed)
        return;
    std::string message{"Autosave failed; the previous recovery backup was retained."};
    if (!result.diagnostics.empty()) {
        const auto& diagnostic = result.diagnostics.front();
        message = "Autosave failed: " + diagnostic.message;
        if (!diagnostic.path.empty())
            message += " (" + diagnostic.path.generic_string() + ")";
    }
    notifications.post(notification_severity_t::error, notification_source_t::recovery, message);
}

} // namespace

void native_project_autosave_target_t::anchor_assets(song_document_t& document) const {
    anchor_recovery_asset_paths(document, asset_base);
}

auto native_project_autosave_target_t::from(
    song_id_t project_id, bool has_native_primary, const std::filesystem::path& native_primary_path,
    const std::filesystem::path& platform_recovery_directory,
    std::filesystem::path original_source_path) -> native_project_autosave_target_t {
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

auto native_project_recovery_session_sequence(const native_project_recovery_scan_outcome_t& scan,
                                              song_id_t project_id) noexcept -> std::uint64_t {
    std::uint64_t sequence{};
    for (const auto& candidate : scan.value.candidates) {
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
    clock_t::duration retry_delay, notification_sink_t notifications, command_journal_limits_t journal_limits)
    : scheduler_(scheduler), notifications_(std::move(notifications)),
      coordinator_(session_started_at, last_explicit_save, interval,
                   initial_session_sequence),
      journal_(scheduler, notifications_, journal_limits),
      interval_(interval),
      retry_delay_(retry_delay > clock_t::duration::zero() ? retry_delay
                                                           : clock_t::duration::zero()) {
    journal_.reset(initial_session_sequence);
    if (last_explicit_save) journal_.mark_explicit_save(*last_explicit_save);
}

native_project_autosave_loop_t::~native_project_autosave_loop_t() { flush_journal(); }

void native_project_autosave_loop_t::observe(document_history_t& history,
    const native_project_autosave_target_t& target) {
    journal_.configure(history.document.id, target, coordinator_.session_sequence());
    history.publications.set([this](const song_document_t& document, std::uint64_t before, std::string_view command) {
        journal_.record(document, before, command);
    });
}
auto native_project_autosave_loop_t::flush_journal(const song_document_t& document) -> bool {
    flush_journal();
    if (!journal_.requires_checkpoint()) return journal_.is_durable();
    retry_after_.reset();
    coordinator_.request_checkpoint();
    (void)tick(document, true, clock_t::now(), journal_.target());
    flush_journal();
    return journal_.is_durable();
}
void native_project_autosave_loop_t::flush_journal() {
    if (worker_) {
        (void)worker_->graph.wait();
        std::optional<native_project_autosave_outcome_t> ignored;
        collect_completed(clock_t::now(), ignored);
    }
    journal_.drain();
}

auto native_project_autosave_loop_t::set_interval(clock_t::duration interval) noexcept -> bool {
    if (!coordinator_.set_interval(interval)) return false;
    interval_ = interval;
    return true;
}

void native_project_autosave_loop_t::mark_explicit_save(
    native_project_document_version_t saved_version) noexcept {
    journal_.mark_explicit_save(saved_version);
    if (is_busy()) {
        deferred_explicit_save_ = saved_version;
        return;
    }
    coordinator_.mark_explicit_save(saved_version);
}
void native_project_autosave_loop_t::mark_explicit_save(native_project_document_version_t saved,
    const song_document_t& current) {
    (void)flush_journal(current);
    mark_explicit_save(saved);
    journal_.record(current, current.revision, "Explicit save baseline");
}

auto native_project_autosave_loop_t::reset_session(
    clock_t::time_point session_started_at,
    std::optional<native_project_document_version_t> last_explicit_save,
    std::uint64_t initial_session_sequence) -> bool {
    if (is_busy())
        return false;
    journal_.reset(initial_session_sequence);
    if (last_explicit_save) journal_.mark_explicit_save(*last_explicit_save);
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
    -> std::optional<native_project_autosave_outcome_t> {
    std::optional<native_project_autosave_outcome_t> completed;
    collect_completed(now, completed);
    journal_.poll();
    if (journal_.requires_checkpoint()) coordinator_.request_checkpoint();
    if (is_busy()) return completed;
    if ((!dirty && !journal_.requires_checkpoint()) || target.backup_path.empty() || (retry_after_ && now < *retry_after_) ||
        !coordinator_.is_due(song, now)) {
        journal_.flush();
        return completed;
    }

    auto snapshot = song;
    auto coordinator = coordinator_;
    const auto backup_path = target.backup_path;
    const auto asset_base = target.asset_base;
    auto original_source_path = target.original_source_path;
    auto intended_project_path = target.intended_project_path;
    const auto journal_position = journal_.position();
    task_graph_t graph(1);
    const auto added = graph.create_task(
        {},
        [coordinator = std::move(coordinator), snapshot = std::move(snapshot), now, backup_path, asset_base,
         original_source_path = std::move(original_source_path),
         intended_project_path = std::move(intended_project_path), options = std::move(options), journal_position](
            std::span<const task_outcome_t>, std::stop_token) mutable -> task_outcome_t {
            anchor_recovery_asset_paths(snapshot, asset_base);
            auto result = coordinator.create_autosave(
                snapshot, now, backup_path, std::move(original_source_path),
                std::move(intended_project_path), std::move(options), journal_position);
            auto completion = std::make_shared<const completion_t>(
                completion_t{.coordinator = std::move(coordinator), .result = std::move(result), .journal_position = journal_position});
            return task_value_t::from(std::move(completion));
        });
    if (!added) {
        native_project_autosave_outcome_t failed{
            .value = {.status = native_project_autosave_status_t::failed,
                      .revision = song.revision,
                      .session_sequence = coordinator_.session_sequence()}};
        failed.diagnostics.push_back(
            {.code = native_project_storage_diagnostic_code_t::write_failure,
             .message = !added.has_value() ? added.error().message : "could not queue autosave task",
             .path = target.backup_path});
        retry_after_ = now + retry_delay_;
        post_autosave_failure(notifications_, failed);
        return failed;
    }
    auto submitted = scheduler_.begin(std::move(graph));
    if (!submitted) {
        native_project_autosave_outcome_t failed{
            .value = {.status = native_project_autosave_status_t::failed,
                      .revision = song.revision,
                      .session_sequence = coordinator_.session_sequence()}};
        failed.diagnostics.push_back(
            {.code = native_project_storage_diagnostic_code_t::write_failure,
             .message =
                 !submitted.has_value() ? submitted.error().message : "could not queue autosave task",
             .path = target.backup_path});
        retry_after_ = now + retry_delay_;
        post_autosave_failure(notifications_, failed);
        return failed;
    }
    worker_ = worker_t{.graph = std::move(submitted.value()), .task = *added};
    retry_after_.reset();
    return completed;
}

auto native_project_autosave_loop_t::is_busy() const noexcept -> bool {
    return worker_.has_value() || journal_.is_busy();
}

auto native_project_autosave_loop_t::session_sequence() const noexcept -> std::uint64_t {
    return coordinator_.session_sequence();
}

void native_project_autosave_loop_t::collect_completed(
    clock_t::time_point now, std::optional<native_project_autosave_outcome_t>& result) {
    if (!worker_)
        return;
    const auto task_result = worker_->graph.outcome(worker_->task);
    if (!task_result)
        return;
    const auto completion = task_value_cast<completion_t>(*task_result);
    worker_.reset();
    if (!completion) {
        native_project_autosave_outcome_t failed{
            .value = {.status = native_project_autosave_status_t::failed,
                      .session_sequence = coordinator_.session_sequence()}};
        failed.diagnostics.push_back(
            {.code = native_project_storage_diagnostic_code_t::write_failure,
             .message = !task_result->has_value() ? task_result->error().cause.message
                                           : "autosave worker returned no result"});
        if (deferred_explicit_save_) {
            coordinator_.mark_explicit_save(*deferred_explicit_save_);
            deferred_explicit_save_.reset();
        }
        retry_after_ = now + retry_delay_;
        post_autosave_failure(notifications_, failed);
        result = std::move(failed);
        return;
    }
    coordinator_ = completion->coordinator;
    const bool is_durable = completion->result.value.status == native_project_autosave_status_t::published &&
        std::ranges::none_of(completion->result.diagnostics, [](const native_project_storage_diagnostic_t& diagnostic) {
            return diagnostic.code == native_project_storage_diagnostic_code_t::write_failure;
        });
    if (is_durable && completion->journal_position)
        journal_.checkpoint(*completion->journal_position);
    if (completion->result.value.status == native_project_autosave_status_t::published && !is_durable)
        notifications_.post(notification_severity_t::warning, notification_source_t::recovery,
            "Autosave published with a directory-sync warning; the command journal was retained.");
    (void)coordinator_.set_interval(interval_);
    if (deferred_explicit_save_) {
        coordinator_.mark_explicit_save(*deferred_explicit_save_);
        deferred_explicit_save_.reset();
    }
    if (completion->result.value.status == native_project_autosave_status_t::failed)
        retry_after_ = now + retry_delay_;
    post_autosave_failure(notifications_, completion->result);
    result = completion->result;
}

} // namespace wormedit

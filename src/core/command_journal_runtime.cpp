#include <wormedit/core/command_journal.h>
#include <wormedit/core/async_job.h>
#include <wormedit/core/autosave_loop.h>
#include <deque>
#include <map>
#include <limits>

namespace wormedit {
namespace {
constexpr std::size_t maximum_pending_journal_snapshots = 4;
struct journal_work_t {
    native_project_journal_position_t position;
    native_project_autosave_target_t target;
    std::optional<command_journal_entry_t> entry;
    std::shared_ptr<command_journal_t> journal;
};
using journal_job_t = async_job_t<std::shared_ptr<const journal_work_t>, bool, async_job_project_identity_t>;
auto publish_journal_work(const std::shared_ptr<const journal_work_t>& request, std::stop_token stop)
    -> journal_job_t::work_outcome_t {
    if (stop.stop_requested()) return std::unexpected(error_t<failure_code_t>{failure_code_t::cancelled, "Journal write cancelled."});
    std::error_code error;
    const auto parent = request->position.path.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, error);
    if (error) return std::unexpected(error_t<failure_code_t>{failure_code_t::publication_failed, error.message()});
    if (!request->entry) {
        const auto compacted = request->journal->checkpoint(request->position.sequence);
        if (!compacted) return std::unexpected(error_t<failure_code_t>{failure_code_t::publication_failed, compacted.error()});
    } else {
        auto entry = *request->entry;
        request->target.anchor_assets(entry.snapshot.song);
        const auto appended = request->journal->append(entry);
        if (!appended) return std::unexpected(error_t<failure_code_t>{failure_code_t::publication_failed, appended.error()});
    }
    return true;
}
}

struct command_journal_runtime_t::storage_t {
    explicit storage_t(task_scheduler_t& scheduler, notification_sink_t sink, command_journal_limits_t requested_limits)
        : notifications(std::move(sink)), job(scheduler, 1, "Journal request is invalid.",
            "Journal worker is busy.", "Journal worker returned no result."), limits(requested_limits) {}
    notification_sink_t notifications;
    journal_job_t job;
    command_journal_limits_t limits;
    song_id_t project;
    native_project_autosave_target_t target;
    std::optional<native_project_journal_position_t> position;
    std::shared_ptr<command_journal_t> journal;
    std::optional<native_project_document_version_t> saved;
    std::deque<std::shared_ptr<const journal_work_t>> pending;
    std::map<std::filesystem::path, std::uint64_t> checkpoints;
    std::optional<std::uint64_t> failed_through;
    std::uint64_t sequence_floor{};

    void request_checkpoint(std::uint64_t sequence) {
        failed_through = std::max(failed_through.value_or(0), sequence);
    }
    void apply_checkpoints() {
        if (job.is_busy()) return;
        for (const auto& [path, sequence] : checkpoints) {
            std::erase_if(pending, [&](const auto& request) {
                return request->entry && request->position.path == path && request->position.sequence <= sequence;
            });
            pending.push_front(std::make_shared<const journal_work_t>(journal_work_t{
                .position = {.path = path, .sequence = sequence},
                .journal = position && position->path == path ? journal : std::make_shared<command_journal_t>(path, limits)}));
            if (position && position->path == path && failed_through && *failed_through <= sequence) failed_through.reset();
        }
        checkpoints.clear();
    }
    void complete(journal_job_t::completion_t completed) {
        if (pending.empty()) return;
        const auto request = pending.front();
        if (completed) pending.pop_front();
        else {
            notifications.post(notification_severity_t::warning, notification_source_t::recovery,
                "Command journal could not publish; a full recovery checkpoint was requested: " + completed.error().cause.message);
            if (position && position->path == request->position.path) request_checkpoint(position->sequence);
            else std::erase_if(pending, [&](const auto& old) { return old->position.path == request->position.path; });
            if (!request->entry && !pending.empty() && pending.front() == request) pending.pop_front();
        }
        apply_checkpoints();
    }
};

command_journal_runtime_t::command_journal_runtime_t(task_scheduler_t& scheduler, notification_sink_t sink,
    command_journal_limits_t limits)
    : storage_(std::make_unique<storage_t>(scheduler, std::move(sink), limits)) {}
command_journal_runtime_t::~command_journal_runtime_t() { drain(); }
void command_journal_runtime_t::configure(song_id_t project, const native_project_autosave_target_t& target,
    std::uint64_t sequence_floor) {
    auto& state = *storage_;
    if (state.project == project && state.target.backup_path == target.backup_path) {
        state.target = target;
        return;
    }
    drain();
    state.sequence_floor = std::max(state.sequence_floor, sequence_floor);
    if (state.position) state.sequence_floor = std::max(state.sequence_floor, state.position->sequence);
    state.project = project;
    state.target = target;
    state.position.reset();
    state.journal.reset();
    state.failed_through.reset();
    state.pending.clear();
    state.checkpoints.clear();
    if (state.saved && state.saved->project_id != project) state.saved.reset();
    if (project && !target.backup_path.empty()) {
        state.position = native_project_journal_position_t::from(target.backup_path, state.sequence_floor);
        state.journal = std::make_shared<command_journal_t>(state.position->path, state.limits);
    }
}
void command_journal_runtime_t::record(const song_document_t& document, std::uint64_t before, std::string_view command) {
    auto& state = *storage_;
    if (!state.position || document.id != state.project) return;
    if (state.position->sequence == std::numeric_limits<std::uint64_t>::max()) {
        state.request_checkpoint(state.position->sequence);
        return;
    }
    ++state.position->sequence;
    if (state.failed_through || state.pending.size() >= maximum_pending_journal_snapshots) {
        state.request_checkpoint(state.position->sequence);
        return;
    }
    native_project_recovery_metadata_t metadata{.original_source_path = state.target.original_source_path,
        .intended_project_path = state.target.intended_project_path,
        .last_explicit_save_revision = state.saved ? std::optional{state.saved->revision} : std::nullopt,
        .session_sequence = state.position->sequence, .journal_position = state.position};
    state.pending.push_back(std::make_shared<const journal_work_t>(journal_work_t{
        .position = *state.position, .target = state.target,
        .entry = command_journal_entry_t{.sequence = state.position->sequence, .before_revision = before,
            .command = std::string{command}, .snapshot = {.song = document, .metadata = std::move(metadata)}},
        .journal = state.journal}));
    flush();
}
void command_journal_runtime_t::mark_explicit_save(native_project_document_version_t saved) {
    storage_->saved = saved;
}
void command_journal_runtime_t::poll() {
    if (auto completed = storage_->job.poll()) storage_->complete(std::move(*completed));
    storage_->apply_checkpoints();
}
void command_journal_runtime_t::flush() {
    auto& state = *storage_;
    state.apply_checkpoints();
    if (state.job.is_busy() || state.pending.empty() || (state.failed_through && state.pending.front()->entry)) return;
    const auto request = state.pending.front();
    const auto project = request->entry ? request->entry->snapshot.song.id : state.project;
    const auto revision = request->entry ? request->entry->snapshot.song.revision : std::uint64_t{};
    const auto started = state.job.request([request] { return request; },
        async_job_project_identity_t{.project = project, .document_revision = revision}, publish_journal_work);
    if (!started && started.error().code != failure_code_t::capacity_exceeded && state.position)
        state.request_checkpoint(state.position->sequence);
}
void command_journal_runtime_t::drain() {
    auto& state = *storage_;
    poll();
    while (!state.pending.empty()) {
        flush();
        if (!state.job.is_busy()) break;
        auto completed = state.job.wait();
        if (!completed) break;
        state.complete(std::move(*completed));
        if (state.failed_through) break;
    }
}
void command_journal_runtime_t::reset(std::uint64_t initial_sequence) {
    drain();
    storage_->project = {};
    storage_->position.reset();
    storage_->journal.reset();
    storage_->pending.clear();
    storage_->checkpoints.clear();
    storage_->failed_through.reset();
    storage_->saved.reset();
    storage_->sequence_floor = initial_sequence;
}
void command_journal_runtime_t::checkpoint(native_project_journal_position_t position) {
    auto& checkpoints = storage_->checkpoints;
    if (checkpoints.size() >= maximum_pending_journal_snapshots && !checkpoints.contains(position.path)) return;
    auto& sequence = checkpoints[position.path];
    sequence = std::max(sequence, position.sequence);
    storage_->apply_checkpoints();
}
auto command_journal_runtime_t::position() const -> std::optional<native_project_journal_position_t> { return storage_->position; }
auto command_journal_runtime_t::requires_checkpoint() const -> bool { return storage_->failed_through.has_value(); }
auto command_journal_runtime_t::is_busy() const -> bool { return storage_->job.is_busy(); }
auto command_journal_runtime_t::target() const -> const native_project_autosave_target_t& { return storage_->target; }
} // namespace wormedit

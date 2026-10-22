#pragma once
#include "ui/frontend/audio_slicer.h"
#include "ui/frontend/file_jobs.h"
#include <wormedit/core/async_job.h>
#include <wormedit/core/audio_slicer.h>
#include <wormedit/core/job_progress.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/sound_import.h>
#include <wormedit/core/workspace.h>

namespace wormedit::sys {
[[nodiscard]] auto build_audio_slicer_plan(const ui::audio_slicer_settings_t&, std::uint32_t rate,
    std::uint64_t frames) -> result_t<audio_slice_plan_t, std::string>;
void refill_audio_slicer_view(ui::audio_slicer_view_t&, const ui::audio_slicer_settings_t&,
    std::uint32_t rate, std::uint64_t frames, bool can_slice);
struct audio_slicer_request_t {
    ui::audio_slicer_settings_t settings;
    std::filesystem::path source, project_directory, relative_directory;
    chart_id_t chart;
    bool allocate_bms_ids{};
    std::shared_ptr<job_progress_t> progress;
};
struct audio_slicer_files_t {
    std::filesystem::path directory;
    bulk_sound_import_action_t import;
    bool retained{};
    ~audio_slicer_files_t();
};
using audio_slicer_job_t = async_job_t<audio_slicer_request_t,
    std::shared_ptr<audio_slicer_files_t>, chart_stamp_t>;
[[nodiscard]] auto prepare_audio_slices(const audio_slicer_request_t&, std::stop_token)
    -> audio_slicer_job_t::work_outcome_t;
namespace runtime {
class audio_slicer_t {
public:
    explicit audio_slicer_t(task_scheduler_t&, notification_sink_t = notification_sink_t::muted());
    [[nodiscard]] auto is_busy() const -> bool { return job_.is_busy(); }
    [[nodiscard]] auto start(const song_document_t&, const ui::audio_slicer_action_t&,
        const std::filesystem::path&) -> async_job_start_outcome_t;
    void poll(document_history_t&, workspace_controller_t&);
    void cancel();
    void populate(ui::audio_slicer_view_t&, const ui::audio_slicer_settings_t&,
        std::uint32_t rate, std::uint64_t frames, bool available);
    [[nodiscard]] auto view() const -> ui::file_job_view_t;
private:
    notification_sink_t notifications_;
    audio_slicer_job_t job_;
    std::shared_ptr<job_progress_t> progress_;
    ui::audio_slicer_settings_t settings_;
    std::optional<std::pair<std::uint32_t, std::uint64_t>> geometry_;
    ui::audio_slicer_view_t cached_view_;
    bool available_{};
    bool cancelling_{};
};
}
}

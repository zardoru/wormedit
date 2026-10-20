#pragma once
#include "ui/frontend/preview_audio.h"
#include "ui/frontend/file_jobs.h"
#include <wormedit/core/atomic_file_storage.h>
#include <wormedit/core/async_job.h>
#include <wormedit/core/document.h>
#include <wormedit/core/job_progress.h>
#include <wormedit/core/proposal.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/workspace.h>

namespace wormedit::sys {
struct preview_audio_export_request_t {
    song_document_t document;
    ui::preview_audio_export_action_t settings;
    std::filesystem::path asset_base, skin_directory, bundle_directory;
    std::shared_ptr<job_progress_t> progress;
};
struct preview_audio_export_identity_t {
    document_stamp_t document;
    chart_id_t chart;
    auto operator==(const preview_audio_export_identity_t&) const -> bool = default;
};
struct preview_audio_prepared_t {
    std::shared_ptr<prepared_atomic_file_t> file;
    std::filesystem::path destination;
    bool replace_existing{};
};
using preview_audio_export_job_t = async_job_t<preview_audio_export_request_t,
    preview_audio_prepared_t, preview_audio_export_identity_t>;
[[nodiscard]] auto prepare_preview_audio_export(const preview_audio_export_request_t&, std::stop_token)
    -> preview_audio_export_job_t::work_outcome_t;

namespace runtime {
class preview_audio_export_t {
  public:
    explicit preview_audio_export_t(task_scheduler_t& scheduler,
        notification_sink_t notifications = notification_sink_t::muted());
    [[nodiscard]] auto is_busy() const -> bool { return preview_audio_job_.is_busy(); }
    [[nodiscard]] auto start(const song_document_t& document,
        const ui::preview_audio_export_action_t& action, const std::filesystem::path& asset_base,
        const std::filesystem::path& skin, const std::filesystem::path& bundle) -> async_job_start_outcome_t;
    [[nodiscard]] auto poll(const document_history_t&, const workspace_controller_t&) -> std::optional<std::string>;
    [[nodiscard]] auto view() const -> ui::file_job_view_t;
    void cancel();
  private:
    notification_sink_t notifications_;
    preview_audio_export_job_t preview_audio_job_;
    std::shared_ptr<job_progress_t> preview_audio_progress_;
    bool preview_audio_cancelling_{};
};
}
}

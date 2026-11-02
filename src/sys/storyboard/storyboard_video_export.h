#pragma once
#include "ui/frontend/storyboard_video_export.h"
#include "ui/frontend/file_jobs.h"
#include "sys/audio/offline_preview_mix.h"
#include <wormedit/core/async_job.h>
#include <wormedit/core/job_progress.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/osbx_lua_link.h>
#include <wormedit/media/video_encoder.h>
#include <chrono>
#include <deque>

namespace wormedit::sys {
struct storyboard_video_identity_t {
    document_stamp_t stamp;
    storyboard_id_t storyboard;
    std::uint64_t session{}, generation{};
    auto operator==(const storyboard_video_identity_t&) const -> bool = default;
};
struct storyboard_video_request_t {
    song_document_t document;
    chart_id_t chart;
    storyboard_video_identity_t identity;
    ui::storyboard_video_settings_t settings;
    std::optional<ui::storyboard_loop_region_t> selection;
    offline_preview_paths_t paths;
    std::shared_ptr<const std::vector<osbx_lua_prepared_scene_t>> prepared;
    std::uint64_t prepared_generation{};
    std::shared_ptr<job_progress_t> progress;
    bool probe{};
};
struct storyboard_video_result_t {
    std::filesystem::path destination;
    std::optional<media::video_encoder_report_t> encoded;
    std::vector<ui::storyboard_video_capability_t> capabilities;
    std::uint64_t total{}, estimated_bytes{}, free_bytes{};
    double estimated_seconds{};
};
using storyboard_video_job_t = async_job_t<storyboard_video_request_t, storyboard_video_result_t, storyboard_video_identity_t>;
[[nodiscard]] auto run_storyboard_video(const storyboard_video_request_t&, std::stop_token)
    -> storyboard_video_job_t::work_outcome_t;
// Pure capture/preflight queries; never decode, probe, rasterize or run Lua.
[[nodiscard]] auto storyboard_video_blockers(const storyboard_video_request_t&) -> std::vector<std::string>;

class storyboard_video_export_t {
  public:
    explicit storyboard_video_export_t(task_scheduler_t&, notification_sink_t = notification_sink_t::muted(),
        storyboard_video_job_t::work_function_t = run_storyboard_video);
    void open(storyboard_video_request_t);
    void configure(const ui::storyboard_video_settings_t&);
    void close();
    [[nodiscard]] auto start(storyboard_video_request_t) -> async_job_start_outcome_t;
    void cancel();
    void cancel_project(song_id_t);
    void poll();
    [[nodiscard]] auto is_busy() const -> bool { return export_.is_busy(); }
    [[nodiscard]] auto view() const -> ui::storyboard_video_export_view_t;
    void refill(ui::storyboard_video_export_view_t&) const;
    [[nodiscard]] auto file_view() const -> ui::file_job_view_t;
    [[nodiscard]] auto generation() const -> std::uint64_t { return generation_; }
  private:
    storyboard_video_job_t probe_, export_;
    notification_sink_t notifications_;
    storyboard_video_job_t::work_function_t work_;
    std::optional<storyboard_video_request_t> draft_;
    storyboard_video_result_t inspected_;
    std::shared_ptr<job_progress_t> progress_;
    std::chrono::steady_clock::time_point started_{};
    std::uint64_t generation_{};
    bool open_{}, cancelling_{}, needs_probe_{};
    std::string status_;
    struct remembered_t { song_id_t project; ui::storyboard_video_settings_t settings; };
    std::deque<remembered_t> remembered_;
    void request_probe();
    void remember();
};
}

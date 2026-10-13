#pragma once

#include <wormedit/core/async_job.h>
#include <wormedit/core/task_graph.h>
#include <wormedit/visual/video_decoder.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace wormedit::visual {

struct video_frame_cache_entry_t {
    song_id_t project;
    std::uint64_t document_revision{};
    resolved_visual_asset_t source;
    std::uint64_t media_time_numerator{};
    std::uint32_t media_time_denominator{1};
    std::shared_ptr<const rgba_video_frame_t> frame;
};

class video_frame_cache_t {
  public:
    explicit video_frame_cache_t(std::size_t maximum_rgba_bytes = 128U * 1024U * 1024U,
                                 std::size_t maximum_entries = 16);

    [[nodiscard]] auto find(song_id_t project, std::uint64_t document_revision,
                            const resolved_visual_asset_t& source,
                            const video_preview_clock_t& clock)
        -> std::shared_ptr<const rgba_video_frame_t>;
    [[nodiscard]] auto insert(song_id_t project, std::uint64_t document_revision,
                              const resolved_visual_asset_t& source,
                              const video_preview_clock_t& clock,
                              std::shared_ptr<const rgba_video_frame_t> frame)
        -> std::shared_ptr<const rgba_video_frame_t>;
    void clear();
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto rgba_bytes() const noexcept -> std::size_t;

  private:
    mutable std::mutex mutex_;
    std::size_t maximum_rgba_bytes_{};
    std::size_t maximum_entries_{};
    std::size_t rgba_bytes_{};
    std::vector<video_frame_cache_entry_t> entries_;
};

using video_job_start_status_t = ::wormedit::async_job_start_status_t;
using video_job_start_result_t = ::wormedit::async_job_start_result_t;

struct video_job_result_t {
    song_id_t project;
    std::uint64_t document_revision{};
    resolved_visual_asset_t source;
    video_preview_clock_t clock;
    video_frame_status_t status{video_frame_status_t::decode_failed};
    std::shared_ptr<const rgba_video_frame_t> frame;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == video_frame_status_t::ready && frame != nullptr;
    }
};

struct video_job_completion_t {
    std::uint64_t generation{};
    song_id_t project;
    std::uint64_t document_revision{};
    std::shared_ptr<const video_job_result_t> result;
    std::optional<task_error_t> task_error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return result != nullptr && !task_error.has_value();
    }
};

// Runs at most a bounded number of seek/decode requests off the UI thread.
// Starting a request cancels older generations; only the newest generation is
// observable, even if an interrupted FFmpeg call takes time to unwind.
class video_frame_job_t {
  public:
    video_frame_job_t(video_frame_cache_t& cache, task_scheduler_t& scheduler,
                      std::size_t maximum_in_flight_jobs = 2);
    ~video_frame_job_t();

    video_frame_job_t(const video_frame_job_t&) = delete;
    auto operator=(const video_frame_job_t&) -> video_frame_job_t& = delete;

    [[nodiscard]] auto begin(song_id_t project, std::uint64_t document_revision,
                               resolved_visual_asset_t source, video_preview_clock_t clock,
                               video_frame_decode_limits_t limits = {}) -> async_job_start_outcome_t;
    [[nodiscard]] auto request(song_id_t project, std::uint64_t document_revision,
                               resolved_visual_asset_t source, video_preview_clock_t clock,
                               video_frame_decode_limits_t limits = {}) -> video_job_start_result_t;
    [[nodiscard]] auto poll() -> std::optional<video_job_completion_t>;
    [[nodiscard]] auto wait() -> std::optional<video_job_completion_t>;
    void cancel() noexcept;
    [[nodiscard]] auto is_busy() const noexcept -> bool;

  private:
    struct decode_request_t {
        song_id_t project;
        std::uint64_t document_revision{};
        resolved_visual_asset_t source;
        video_preview_clock_t clock;
        video_frame_decode_limits_t limits;
    };

    struct worker_t {
        std::uint64_t generation{};
        song_id_t project;
        std::uint64_t document_revision{};
        task_graph_handle_t graph;
        task_id_t task;
    };

    void discard_terminal_jobs();
    [[nodiscard]] auto decode_uncached_frame(const decode_request_t& request,
                                             std::stop_token stop) -> video_frame_decode_result_t;
    [[nodiscard]] auto run_decode(const decode_request_t& request, std::stop_token stop)
        -> task_result_t;

    video_frame_cache_t& cache_;
    task_scheduler_t& scheduler_;
    std::size_t maximum_in_flight_jobs_{};
    std::uint64_t next_generation_{};
    std::uint64_t current_generation_{};
    std::vector<worker_t> workers_;
    std::mutex session_mutex_;
    std::unique_ptr<video_decoder_session_t> session_;
    song_id_t session_project_;
    std::uint64_t session_document_revision_{};
};

} // namespace wormedit::visual

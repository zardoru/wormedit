#pragma once

#include <wormedit/core/async_job.h>
#include <wormedit/core/task_graph.h>
#include <wormedit/visual/image_decoder.h>
#include <wormedit/visual/job_result.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit::visual {

struct image_cache_entry_t {
    song_id_t project;
    std::uint64_t document_revision{};
    resolved_visual_asset_t source;
    std::shared_ptr<const rgba_image_t> image;
};

// Thread-safe, byte-bounded least-recently-used cache. Renderer objects are
// deliberately excluded: SDL GPU and future frontends can own independent
// textures backed by the same immutable RGBA payload.
class image_cache_t {
  public:
    explicit image_cache_t(std::size_t maximum_rgba_bytes = 256U * 1024U * 1024U,
                           std::size_t maximum_entries = 64);

    [[nodiscard]] auto find(song_id_t project, std::uint64_t document_revision,
                            const resolved_visual_asset_t& source)
        -> std::shared_ptr<const rgba_image_t>;
    [[nodiscard]] auto insert(song_id_t project, std::uint64_t document_revision,
                              const resolved_visual_asset_t& source,
                              std::shared_ptr<const rgba_image_t> image)
        -> std::shared_ptr<const rgba_image_t>;
    void clear();
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto rgba_bytes() const noexcept -> std::size_t;

  private:
    mutable std::mutex mutex_;
    std::size_t maximum_rgba_bytes_{};
    std::size_t maximum_entries_{};
    std::size_t rgba_bytes_{};
    std::vector<image_cache_entry_t> entries_;
};

using image_job_start_status_t = ::wormedit::async_job_start_status_t;

struct image_job_options_t {
    image_decode_limits_t decode_limits;
    std::size_t maximum_result_rgba_bytes{256U * 1024U * 1024U};
};


using image_job_item_value_t = visual_job_value_t<resolved_visual_asset_t, rgba_image_t>;
using image_job_item_outcome_t = result_t<image_job_item_value_t,
    visual_job_error_t<resolved_visual_asset_t, rgba_image_t>>;

struct image_job_result_t {
    // A failed first match has no publishable image, even if a later item repeats its resource.
    [[nodiscard]] auto find(resource_id_t resource) const noexcept -> const image_job_item_value_t*;

    song_id_t project;
    std::uint64_t document_revision{};
    std::vector<image_job_item_outcome_t> items;
};

using image_job_completion_outcome_t =
    async_job_completion_outcome_t<async_job_project_identity_t, image_job_result_t>;

struct image_request_t {
    image_cache_t& cache;
    song_id_t project;
    std::uint64_t document_revision{};
    std::vector<resolved_visual_asset_t> sources;
    image_job_options_t options;
};

// Decodes a bounded batch off the UI thread. Only the newest request can be
// observed through poll()/wait(); superseded work is cancelled and discarded.
// The caller-provided project ID and document revision are part of every
// cache identity.
class image_job_t
    : public async_job_t<image_request_t, image_job_result_t, async_job_project_identity_t> {
  public:
    image_job_t(image_cache_t& cache, task_scheduler_t& scheduler,
                std::size_t maximum_sources_per_request = 5,
                std::size_t maximum_in_flight_jobs = 4);

    image_job_t(const image_job_t&) = delete;
    auto operator=(const image_job_t&) -> image_job_t& = delete;

    [[nodiscard]] auto request(song_id_t project, std::uint64_t document_revision,
                               std::span<const resolved_visual_asset_t> sources,
                               image_job_options_t options = {}) -> async_job_start_outcome_t;

  private:
    static constexpr const char* invalid_request_message_ =
        "image decode request is empty or exceeds its configured bounds";

    image_cache_t& cache_;
    std::size_t maximum_sources_per_request_{};
};

} // namespace wormedit::visual

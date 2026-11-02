#pragma once

#include <wormedit/core/storyboard_preview.h>
#include <wormedit/visual/image_job.h>

#include <memory>

namespace wormedit {

struct storyboard_raster_clock_t {
    std::uint32_t numerator{60};
    std::uint32_t denominator{1};
    // Exact frame_index * denominator * 1000 / numerator, independently of
    // previously requested frames. Rejects zero rates and signed-ms overflow.
    [[nodiscard]] auto time_at(std::uint64_t frame_index) const
        -> result_t<storyboard_time_t, failure_code_t>;
    auto operator==(const storyboard_raster_clock_t&) const -> bool = default;
};

struct storyboard_raster_options_t {
    static constexpr std::uint32_t maximum_width = 3840;
    static constexpr std::uint32_t maximum_height = 2160;
    static constexpr std::size_t maximum_threads = 64;
    static constexpr std::size_t maximum_frame_slots = 3;
    static constexpr std::size_t maximum_supported_instances = 100'000;
    std::uint32_t width{1920};
    std::uint32_t height{1080};
    std::size_t threads{1}; // 1..64, including the calling thread.
    std::size_t frame_slots{2}; // 1..3; encoder must release a slot before reuse.
    std::size_t maximum_instances{maximum_supported_instances};
};

struct storyboard_raster_image_t {
    std::string path;
    std::shared_ptr<const visual::rgba_image_t> image;
};

struct storyboard_raster_image_source_t {
    std::string path;
    visual::resolved_visual_asset_t source;
};

// Load once per immutable project/revision, using the preview's decoder/cache
// identity and budgets. Decode failures retain diagnostics and an empty binding;
// like the current preview, a missing image is skipped (no fabricated texture).
struct storyboard_raster_images_t {
    [[nodiscard]] static auto from(song_id_t project, std::uint64_t revision,
        std::span<const storyboard_raster_image_source_t> sources, visual::image_cache_t& cache,
        visual::image_job_options_t options = {})
        -> attempt_t<storyboard_raster_images_t>;
    std::vector<storyboard_raster_image_t> bindings;
};

struct storyboard_raster_frame_t {
    std::uint32_t width{};
    std::uint32_t height{};
    // Tightly packed, opaque, premultiplied RGBA8 on black. Valid until this ring slot is reused.
    std::span<const std::uint8_t> pixels;
};

// Contains and clips each logical canvas in opaque black letter/pillarboxes.
// Synchronous CPU rasterizer. One owner calls render at a time. Persistent
// workers own disjoint 32-row bands; every band traverses the same draw order.
// No per-frame allocation after warming the supplied arena. Images and draw
// views need only survive render(). Pixels have the ring lifetime above.
class storyboard_raster_t {
  public:
    [[nodiscard]] static auto from(storyboard_raster_options_t options = {})
        -> result_t<storyboard_raster_t, error_t<failure_code_t>>;
    ~storyboard_raster_t();
    storyboard_raster_t(storyboard_raster_t&&) noexcept;
    auto operator=(storyboard_raster_t&&) noexcept -> storyboard_raster_t&;

    [[nodiscard]] auto render(std::span<const storyboard_draw_instance_t> instances,
        std::span<const storyboard_raster_image_t> images, frame_arena_t& arena,
        storyboard_conditional_layer_t conditional_layer = storyboard_conditional_layer_t::pass)
        -> result_t<storyboard_raster_frame_t, error_t<failure_code_t>>;

  private:
    struct impl_t;
    explicit storyboard_raster_t(std::unique_ptr<impl_t> impl);
    std::unique_ptr<impl_t> impl_;
};

} // namespace wormedit

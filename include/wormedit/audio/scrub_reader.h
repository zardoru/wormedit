#pragma once

#include <wormedit/audio/block_cache.h>
#include <wormedit/audio/decoder.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace wormedit::audio {

// One stretch of a music source laid on the playback timeline (export cuts split the song
// into several).
struct scrub_segment_t {
    audio_source_identity_t source;
    std::uint64_t timeline_start_frame{};
    std::uint64_t source_start_frame{};
    std::uint64_t frame_count{};
    auto operator==(const scrub_segment_t&) const -> bool = default;
};

struct scrub_render_result_t {
    std::size_t frames{};
    // Source blocks the cache could not supply; those frames are silent.
    std::size_t missing_blocks{};
};

// Needle-on-record playback: reads the timeline at a fractional position that moves by a signed,
// slewing speed (1.0 is normal playback, negative is backwards, pitch follows speed). It reads
// only blocks that are already in the cache and never allocates after construction, so it can
// run in an audio callback. Not thread safe: the owner serializes it.
class scrub_reader_t {
  public:
    explicit scrub_reader_t(decoded_block_cache_t& cache) : cache_(cache) {}

    void set_segments(std::vector<scrub_segment_t> segments);
    [[nodiscard]] auto segments() const noexcept -> std::span<const scrub_segment_t> {
        return segments_;
    }
    // Drops the needle at a timeline frame, at rest and silent until the first render fades it in.
    void reset(double position_frames) noexcept;

    // Fills `interleaved_stereo` (whole stereo frames) while the speed slews toward
    // `target_speed`. A held record fades to silence: pass hold = true.
    [[nodiscard]] auto render(double target_speed, bool hold, std::span<float> interleaved_stereo)
        -> scrub_render_result_t;

    [[nodiscard]] auto position_frames() const noexcept -> double { return position_; }
    [[nodiscard]] auto speed() const noexcept -> double { return speed_; }
    // The needle stopped and the output faded out: nothing audible is left to render.
    [[nodiscard]] auto is_silent() const noexcept -> bool;

    // Blocks worth having cached around a needle at `position_frames`: `ahead_frames` of travel
    // along `direction` (its sign) and `behind_frames` the other way, nearest first.
    static void plan_blocks(std::span<const scrub_segment_t> segments, double position_frames,
                            std::uint64_t ahead_frames, std::uint64_t behind_frames,
                            double direction, std::vector<audio_block_key_t>& out);

    // Scrub never moves faster than this while rendering.
    static constexpr double maximum_speed = 8.0;
    static constexpr std::size_t chunk_frames = 512;

  private:
    [[nodiscard]] auto fill_window(std::int64_t first, std::size_t count) -> std::size_t;

    static constexpr std::size_t window_margin = 4;
    static constexpr std::size_t window_capacity =
        static_cast<std::size_t>(chunk_frames * maximum_speed) + 2 * window_margin + chunk_frames;

    decoded_block_cache_t& cache_;
    std::vector<scrub_segment_t> segments_;
    double position_{};
    double speed_{};
    double gain_{};
    std::array<float, window_capacity * output_channel_count> window_{};
};

} // namespace wormedit::audio

#pragma once

#include <wormedit/audio/block_cache.h>
#include <wormedit/core/result.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

namespace wormedit::audio {

struct waveform_key_t {
    audio_source_identity_t source;
    std::uint32_t frames_per_bin{};
    std::uint64_t begin_frame{};
    // Zero means the complete source; otherwise bins cover this exclusive end.
    std::uint64_t end_frame{};

    auto operator==(const waveform_key_t&) const -> bool = default;
};

struct waveform_bin_t {
    std::array<float, output_channel_count> minimum{};
    std::array<float, output_channel_count> maximum{};
    std::array<float, output_channel_count> absolute_peak{};
    std::array<float, output_channel_count> rms{};
    std::uint64_t frame_count{};

    auto operator==(const waveform_bin_t&) const -> bool = default;
};

struct waveform_level_t {
    std::uint64_t frames_per_bin{};
    std::vector<waveform_bin_t> bins;

    auto operator==(const waveform_level_t&) const -> bool = default;
};

struct waveform_t {
    waveform_key_t key;
    std::uint32_t sample_rate{output_sample_rate};
    std::uint64_t total_frames{};
    std::vector<waveform_bin_t> bins;
    std::vector<waveform_level_t> coarser_levels;

    auto operator==(const waveform_t&) const -> bool = default;
};

struct waveform_level_view_t {
    std::uint64_t frames_per_bin{};
    std::span<const waveform_bin_t> bins;
};

inline constexpr std::size_t maximum_waveform_projection_columns = 65'536;

struct waveform_column_t {
    std::uint64_t begin_frame{};
    std::uint64_t end_frame{};
    std::array<float, output_channel_count> minimum{};
    std::array<float, output_channel_count> maximum{};
    std::array<float, output_channel_count> absolute_peak{};
    std::array<float, output_channel_count> rms{};

    auto operator==(const waveform_column_t&) const -> bool = default;
};

struct waveform_projection_request_t {
    std::uint64_t begin_frame{};
    std::uint64_t end_frame{};
    std::size_t pixel_width{};
    std::size_t maximum_columns{4'096};
};

// Projects one visible source-frame interval into bounded implicit pixel
// columns. Each column retains its exact frame interval while peak/RMS values
// come from the closest cached pyramid level.
struct waveform_viewport_t;

struct waveform_view_t {
    [[nodiscard]] auto refill(const waveform_t& waveform, waveform_projection_request_t request)
        -> result_t<void, failure_code_t>;
    [[nodiscard]] auto refill(const waveform_t& waveform, const waveform_viewport_t& viewport)
        -> result_t<void, failure_code_t>;

    [[nodiscard]] static auto from(const waveform_t& waveform, waveform_projection_request_t request)
        -> result_t<waveform_view_t, failure_code_t>;

    std::uint64_t level_frames_per_bin{};
    std::vector<waveform_column_t> columns;
    auto operator==(const waveform_view_t&) const -> bool = default;

    // Projects the visible window through the closest pyramid level. An invalid
    // viewport or waveform yields a non-projected result.
    [[nodiscard]] static auto from(const waveform_t& waveform,
                                   const waveform_viewport_t& viewport)
        -> result_t<waveform_view_t, failure_code_t>;
};
using waveform_view_outcome_t = result_t<waveform_view_t, failure_code_t>;

struct waveform_cache_limits_t {
    std::size_t maximum_entries{32};
    std::size_t maximum_bytes{16U * 1024U * 1024U};
    std::size_t maximum_bins_per_entry{1'000'000};
};

struct waveform_cache_stats_t {
    std::size_t entries{};
    std::size_t cached_bytes{};
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t evictions{};
    std::uint64_t rejected_stores{};
};

// The analysis defaults keep short assets on the serial path and cap worker
// creation for large pyramids. Applications may tune the threshold and worker
// cap/alignment from platform benchmarks; zero workers disables parallel work.
// Alignment requests are capped internally at one page (4096 bytes).
struct waveform_analysis_options_t {
    std::size_t parallel_minimum_bins{16'384};
    std::size_t maximum_parallel_workers{4};
    std::size_t parallel_partition_alignment_bytes{64};
    std::uint64_t begin_frame{};
    std::uint64_t end_frame{};
    bool require_cached_pcm{};
};

// Waveform peaks are derived analysis and therefore have their own bounded LRU;
// they do not count against the decoded playback-block budget.
class waveform_cache_t {
  public:
    explicit waveform_cache_t(waveform_cache_limits_t limits = {});

    [[nodiscard]] auto find(const waveform_key_t& key) -> std::optional<waveform_t>;
    [[nodiscard]] auto store(waveform_t waveform) -> result_t<void, failure_code_t>;
    void clear();
    void clear_source(const audio_source_identity_t& source);

    [[nodiscard]] auto limits() const noexcept -> waveform_cache_limits_t;
    [[nodiscard]] auto stats() const -> waveform_cache_stats_t;

  private:
    using entry_list_t = std::list<waveform_t>;
    using entry_iterator_t = entry_list_t::iterator;

    [[nodiscard]] auto find_entry(const waveform_key_t& key) -> entry_iterator_t;
    void erase(entry_iterator_t entry, bool eviction);

    waveform_cache_limits_t limits_;
    mutable std::mutex mutex_;
    entry_list_t most_recent_first_;
    waveform_cache_stats_t stats_;
};

enum class waveform_analysis_phase_t { cache_hit, generated };

struct waveform_analysis_error_t {
    failure_code_t code{failure_code_t::unavailable};
    // Cache publication can fail after a complete usable waveform has been generated.
    std::optional<waveform_t> waveform;
    std::optional<std::uint64_t> failed_block_index;
    operation_state_t<block_decode_phase_t> block_status{wormedit::failure_code_t::unavailable};
    auto operator==(const waveform_analysis_error_t&) const -> bool = default;
};

// Generates one complete per-channel min/max/absolute-peak waveform. PCM is
// requested through the decoded-block cache and consumed one block at a time,
// so analysis of a long source remains bounded by both cache policies.
struct waveform_analysis_t {
    [[nodiscard]] static auto from(decoder_t& decoder, decoded_block_cache_t& block_cache,
                                  waveform_cache_t& waveform_cache, audio_source_identity_t source,
                                  std::uint32_t frames_per_bin, waveform_analysis_options_t options = {},
                                  std::stop_token stop = {})
        -> result_t<waveform_analysis_t, waveform_analysis_error_t>;

    // A complete usable waveform can survive a cache-publication failure.
    [[nodiscard]] static auto find_waveform(
        const result_t<waveform_analysis_t, waveform_analysis_error_t>& outcome) noexcept
        -> const waveform_t*;

    waveform_t waveform;
    waveform_analysis_phase_t status{waveform_analysis_phase_t::generated};
    auto operator==(const waveform_analysis_t&) const -> bool = default;
};
using waveform_analysis_outcome_t = result_t<waveform_analysis_t, waveform_analysis_error_t>;

// Retained for the beat-fit cache/gesture snapshot contract (T-630/T-632).
struct waveform_analysis_result_t {
    [[nodiscard]] static auto from(waveform_analysis_outcome_t&& outcome) -> waveform_analysis_result_t;
    operation_state_t<waveform_analysis_phase_t> status{wormedit::failure_code_t::unavailable};
    std::optional<waveform_t> waveform;
    std::optional<std::uint64_t> failed_block_index;
    operation_state_t<block_decode_phase_t> block_status{wormedit::failure_code_t::unavailable};
};

struct waveform_sample_t {
    std::uint64_t frame{};
    std::array<float, output_channel_count> samples{};
};

// Cache-only: missing PCM returns no partial polyline and never invokes a decoder.
[[nodiscard]] auto build_cached_waveform_samples_view(decoded_block_cache_t& cache,
    const audio_source_identity_t& source, std::uint64_t begin_frame, std::uint64_t end_frame,
    std::size_t maximum_samples = 4'096) -> std::optional<std::vector<waveform_sample_t>>;

// Chooses the cached level closest to one RMS/peak bucket per screen pixel.
// Values finer than the base level select the base; sample-level rendering can
// read the visible normalized PCM block directly from decoded_block_cache_t.
[[nodiscard]] auto waveform_level_for_zoom(const waveform_t& waveform,
                                           double source_frames_per_pixel)
    -> std::optional<waveform_level_view_t>;

} // namespace wormedit::audio

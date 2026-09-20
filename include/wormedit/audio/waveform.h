#pragma once

#include <wormedit/audio/block_cache.h>

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

enum class waveform_projection_status_t {
    projected,
    invalid_waveform,
    invalid_window,
    capacity_exceeded,
};

struct waveform_projection_result_t {
    waveform_projection_status_t status{waveform_projection_status_t::invalid_waveform};
    std::uint64_t level_frames_per_bin{};
    std::vector<waveform_column_t> columns;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == waveform_projection_status_t::projected;
    }
};

struct waveform_cache_limits_t {
    std::size_t maximum_entries{32};
    std::size_t maximum_bytes{16U * 1024U * 1024U};
    std::size_t maximum_bins_per_entry{1'000'000};
};

enum class waveform_store_status_t {
    stored,
    invalid_waveform,
    capacity_exceeded,
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
};

// Waveform peaks are derived analysis and therefore have their own bounded LRU;
// they do not count against the decoded playback-block budget.
class waveform_cache_t {
  public:
    explicit waveform_cache_t(waveform_cache_limits_t limits = {});

    [[nodiscard]] auto find(const waveform_key_t& key) -> std::optional<waveform_t>;
    [[nodiscard]] auto store(waveform_t waveform) -> waveform_store_status_t;
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

enum class waveform_analysis_status_t {
    cache_hit,
    generated,
    cancelled,
    invalid_request,
    decoder_unavailable,
    source_too_large,
    decode_failed,
    analysis_cache_capacity_exceeded,
};

struct waveform_analysis_result_t {
    waveform_analysis_status_t status{waveform_analysis_status_t::decoder_unavailable};
    std::optional<waveform_t> waveform;
    std::optional<std::uint64_t> failed_block_index;
    block_decode_status_t block_status{block_decode_status_t::decoder_unavailable};
};

// Generates one complete per-channel min/max/absolute-peak waveform. PCM is
// requested through the decoded-block cache and consumed one block at a time,
// so analysis of a long source remains bounded by both cache policies.
[[nodiscard]] auto analyze_waveform(decoder_t& decoder, decoded_block_cache_t& block_cache,
                                    waveform_cache_t& waveform_cache,
                                    audio_source_identity_t source, std::uint32_t frames_per_bin,
                                    waveform_analysis_options_t options = {},
                                    std::stop_token stop = {}) -> waveform_analysis_result_t;

// Chooses the cached level closest to one RMS/peak bucket per screen pixel.
// Values finer than the base level select the base; sample-level rendering can
// read the visible normalized PCM block directly from decoded_block_cache_t.
[[nodiscard]] auto waveform_level_for_zoom(const waveform_t& waveform,
                                           double source_frames_per_pixel)
    -> std::optional<waveform_level_view_t>;

// Projects one visible source-frame interval into bounded implicit pixel
// columns. Each column retains its exact frame interval while peak/RMS values
// come from the closest cached pyramid level.
[[nodiscard]] auto project_waveform(const waveform_t& waveform,
                                    waveform_projection_request_t request)
    -> waveform_projection_result_t;

} // namespace wormedit::audio

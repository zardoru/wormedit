#include <wormedit/audio/waveform.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace wormedit::audio {
namespace {

auto valid_source(const audio_source_identity_t& source) -> bool {
    return !source.asset_identity.empty() && !source.source_identity.empty();
}

auto expected_bin_count(std::uint64_t total_frames, std::uint32_t frames_per_bin)
    -> std::optional<std::uint64_t> {
    if (total_frames == 0 || frames_per_bin == 0) {
        return std::nullopt;
    }
    return 1U + (total_frames - 1U) / frames_per_bin;
}

auto bins_bytes(std::size_t bin_count) -> std::optional<std::size_t> {
    if (bin_count > std::numeric_limits<std::size_t>::max() / sizeof(waveform_bin_t)) {
        return std::nullopt;
    }
    return bin_count * sizeof(waveform_bin_t);
}

auto waveform_bytes(const waveform_t& waveform) -> std::optional<std::size_t> {
    auto result = bins_bytes(waveform.bins.size());
    if (!result.has_value()) {
        return std::nullopt;
    }
    for (const auto& level : waveform.coarser_levels) {
        const auto level_bytes = bins_bytes(level.bins.size());
        if (!level_bytes.has_value() ||
            *level_bytes > std::numeric_limits<std::size_t>::max() - *result) {
            return std::nullopt;
        }
        *result += *level_bytes;
    }
    return result;
}

auto valid_bins(std::span<const waveform_bin_t> bins, std::uint64_t total_frames,
                std::uint64_t frames_per_bin) -> bool {
    if (total_frames == 0 || frames_per_bin == 0 || bins.empty()) {
        return false;
    }
    const auto expected = 1U + (total_frames - 1U) / frames_per_bin;
    if (expected != bins.size()) {
        return false;
    }
    std::uint64_t accounted_frames{};
    for (const auto& bin : bins) {
        if (bin.frame_count == 0 || bin.frame_count > frames_per_bin ||
            bin.frame_count > std::numeric_limits<std::uint64_t>::max() -
                                  accounted_frames) {
            return false;
        }
        accounted_frames += bin.frame_count;
        for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
            if (!std::isfinite(bin.minimum[channel]) ||
                !std::isfinite(bin.maximum[channel]) ||
                !std::isfinite(bin.absolute_peak[channel]) ||
                !std::isfinite(bin.rms[channel]) ||
                bin.minimum[channel] > bin.maximum[channel] ||
                bin.absolute_peak[channel] < 0.0F || bin.rms[channel] < 0.0F) {
                return false;
            }
        }
    }
    return accounted_frames == total_frames;
}

auto valid_waveform(const waveform_t& waveform, std::size_t maximum_bins) -> bool {
    if (!valid_source(waveform.key.source) || waveform.key.frames_per_bin == 0 ||
        waveform.sample_rate != output_sample_rate || waveform.bins.empty() ||
        waveform.bins.size() > maximum_bins) {
        return false;
    }
    if (!valid_bins(waveform.bins, waveform.total_frames,
                    waveform.key.frames_per_bin)) {
        return false;
    }
    auto prior_frames_per_bin = static_cast<std::uint64_t>(waveform.key.frames_per_bin);
    auto prior_bin_count = waveform.bins.size();
    for (const auto& level : waveform.coarser_levels) {
        if (prior_frames_per_bin > std::numeric_limits<std::uint64_t>::max() / 2U ||
            level.frames_per_bin != prior_frames_per_bin * 2U ||
            level.bins.size() != (prior_bin_count + 1U) / 2U ||
            !valid_bins(level.bins, waveform.total_frames, level.frames_per_bin)) {
            return false;
        }
        prior_frames_per_bin = level.frames_per_bin;
        prior_bin_count = level.bins.size();
    }
    return waveform.coarser_levels.empty() || prior_bin_count == 1;
}

auto analysis_failure(std::uint64_t block_index, block_decode_status_t status)
    -> waveform_analysis_result_t {
    return {
        .status = waveform_analysis_status_t::decode_failed,
        .failed_block_index = block_index,
        .block_status = status,
    };
}

} // namespace

waveform_cache_t::waveform_cache_t(waveform_cache_limits_t limits)
    : limits_(limits) {}

auto waveform_cache_t::find_entry(const waveform_key_t& key) -> entry_iterator_t {
    return std::ranges::find(most_recent_first_, key, &waveform_t::key);
}

auto waveform_cache_t::find(const waveform_key_t& key) -> std::optional<waveform_t> {
    std::scoped_lock lock(mutex_);
    if (!valid_source(key.source) || key.frames_per_bin == 0) {
        ++stats_.misses;
        return std::nullopt;
    }
    const auto found = find_entry(key);
    if (found == most_recent_first_.end()) {
        ++stats_.misses;
        return std::nullopt;
    }
    ++stats_.hits;
    most_recent_first_.splice(most_recent_first_.begin(), most_recent_first_, found);
    return most_recent_first_.front();
}

auto waveform_cache_t::store(waveform_t waveform) -> waveform_store_status_t {
    if (!valid_waveform(waveform, limits_.maximum_bins_per_entry)) {
        std::scoped_lock lock(mutex_);
        ++stats_.rejected_stores;
        return waveform_store_status_t::invalid_waveform;
    }
    const auto bytes = waveform_bytes(waveform);
    if (!bytes.has_value() || limits_.maximum_entries == 0 ||
        *bytes > limits_.maximum_bytes) {
        std::scoped_lock lock(mutex_);
        ++stats_.rejected_stores;
        return waveform_store_status_t::capacity_exceeded;
    }

    std::scoped_lock lock(mutex_);
    if (const auto existing = find_entry(waveform.key);
        existing != most_recent_first_.end()) {
        erase(existing, false);
    }
    while (stats_.entries >= limits_.maximum_entries ||
           stats_.cached_bytes > limits_.maximum_bytes - *bytes) {
        if (most_recent_first_.empty()) {
            ++stats_.rejected_stores;
            return waveform_store_status_t::capacity_exceeded;
        }
        erase(std::prev(most_recent_first_.end()), true);
    }
    most_recent_first_.push_front(std::move(waveform));
    ++stats_.entries;
    stats_.cached_bytes += *bytes;
    return waveform_store_status_t::stored;
}

void waveform_cache_t::erase(entry_iterator_t entry, bool eviction) {
    stats_.cached_bytes -= *waveform_bytes(*entry);
    --stats_.entries;
    if (eviction) {
        ++stats_.evictions;
    }
    most_recent_first_.erase(entry);
}

void waveform_cache_t::clear() {
    std::scoped_lock lock(mutex_);
    most_recent_first_.clear();
    stats_.entries = 0;
    stats_.cached_bytes = 0;
}

void waveform_cache_t::clear_source(const audio_source_identity_t& source) {
    std::scoped_lock lock(mutex_);
    for (auto entry = most_recent_first_.begin(); entry != most_recent_first_.end();) {
        if (entry->key.source == source) {
            const auto removed = entry++;
            erase(removed, false);
        } else {
            ++entry;
        }
    }
}

auto waveform_cache_t::limits() const noexcept -> waveform_cache_limits_t {
    return limits_;
}

auto waveform_cache_t::stats() const -> waveform_cache_stats_t {
    std::scoped_lock lock(mutex_);
    return stats_;
}

auto analyze_waveform(decoder_t& decoder, decoded_block_cache_t& block_cache,
                      waveform_cache_t& waveform_cache,
                      audio_source_identity_t source,
                      std::uint32_t frames_per_bin) -> waveform_analysis_result_t {
    const waveform_key_t waveform_key{
        .source = std::move(source),
        .frames_per_bin = frames_per_bin,
    };
    if (!valid_source(waveform_key.source) || frames_per_bin == 0) {
        return {.status = waveform_analysis_status_t::invalid_request};
    }
    if (auto cached = waveform_cache.find(waveform_key); cached.has_value()) {
        return {
            .status = waveform_analysis_status_t::cache_hit,
            .waveform = std::move(cached),
        };
    }

    const auto stream_info = decoder.info();
    if (!stream_info.has_value()) {
        return {.status = waveform_analysis_status_t::decoder_unavailable};
    }
    const auto bin_count = expected_bin_count(stream_info->output_frame_count,
                                              frames_per_bin);
    const auto base_bytes = bin_count.has_value() &&
                                    *bin_count <= std::numeric_limits<std::size_t>::max()
                                ? bins_bytes(static_cast<std::size_t>(*bin_count))
                                : std::nullopt;
    // A pairwise pyramid adds fewer bins than the base level. Reserve a strict
    // 2x bound before allocating either the retained peaks or RMS accumulators.
    const auto pyramid_fits = base_bytes.has_value() &&
                              *base_bytes <= waveform_cache.limits().maximum_bytes / 2U;
    if (!bin_count.has_value() ||
        *bin_count > waveform_cache.limits().maximum_bins_per_entry ||
        *bin_count > std::numeric_limits<std::size_t>::max() ||
        !pyramid_fits) {
        return {.status = waveform_analysis_status_t::source_too_large};
    }

    waveform_t waveform{
        .key = waveform_key,
        .sample_rate = stream_info->output_sample_rate,
        .total_frames = stream_info->output_frame_count,
        .bins = std::vector<waveform_bin_t>(static_cast<std::size_t>(*bin_count)),
    };
    for (auto& bin : waveform.bins) {
        bin.minimum.fill(std::numeric_limits<float>::infinity());
        bin.maximum.fill(-std::numeric_limits<float>::infinity());
    }
    std::vector<std::array<double, output_channel_count>> square_sums(
        waveform.bins.size());

    const auto block_count =
        1U + (stream_info->output_frame_count - 1U) / maximum_read_frames;
    std::uint64_t visited_frames{};
    for (std::uint64_t block_index = 0; block_index < block_count; ++block_index) {
        const audio_block_key_t block_key{
            .source = waveform_key.source,
            .block_index = block_index,
        };
        auto block = block_cache.find(block_key);
        if (!block.has_value()) {
            const auto start_frame = block_index * maximum_read_frames;
            const auto requested_frames = static_cast<std::size_t>(
                std::min<std::uint64_t>(stream_info->output_frame_count - start_frame,
                                        maximum_read_frames));
            std::vector<float> samples(maximum_read_frames * output_channel_count);
            const auto read = decoder.read_at(start_frame, samples, requested_frames);
            if (read.status == read_status_t::seek_error) {
                return analysis_failure(block_index, block_decode_status_t::seek_failed);
            }
            if ((read.status != read_status_t::success &&
                 read.status != read_status_t::end_of_stream) ||
                read.frames_read == 0) {
                return analysis_failure(block_index, block_decode_status_t::decode_failed);
            }
            const auto sample_count = read.frames_read * output_channel_count;
            samples.resize(sample_count);
            block = decoded_audio_block_t{
                .key = block_key,
                .frame_count = read.frames_read,
                .interleaved_stereo = std::move(samples),
            };
            if (block_cache.store(block_key, block->frame_count,
                                  block->interleaved_stereo) !=
                block_store_status_t::stored) {
                return analysis_failure(block_index,
                                        block_decode_status_t::cache_capacity_exceeded);
            }
        }
        for (std::size_t frame = 0; frame < block->frame_count; ++frame) {
            const auto global_frame = visited_frames + frame;
            const auto bin_index = static_cast<std::size_t>(global_frame / frames_per_bin);
            auto& bin = waveform.bins[bin_index];
            ++bin.frame_count;
            for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
                const auto sample = block->interleaved_stereo[
                    frame * output_channel_count + channel];
                bin.minimum[channel] = std::min(bin.minimum[channel], sample);
                bin.maximum[channel] = std::max(bin.maximum[channel], sample);
                bin.absolute_peak[channel] =
                    std::max(bin.absolute_peak[channel], std::abs(sample));
                square_sums[bin_index][channel] +=
                    static_cast<double>(sample) * static_cast<double>(sample);
            }
        }
        visited_frames += block->frame_count;
    }
    if (visited_frames != stream_info->output_frame_count) {
        return {.status = waveform_analysis_status_t::decode_failed};
    }
    for (std::size_t bin_index = 0; bin_index < waveform.bins.size(); ++bin_index) {
        auto& bin = waveform.bins[bin_index];
        for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
            bin.rms[channel] = static_cast<float>(std::sqrt(
                square_sums[bin_index][channel] / static_cast<double>(bin.frame_count)));
        }
    }

    const auto* prior_bins = &waveform.bins;
    auto prior_frames_per_bin = static_cast<std::uint64_t>(frames_per_bin);
    while (prior_bins->size() > 1) {
        if (prior_frames_per_bin > std::numeric_limits<std::uint64_t>::max() / 2U) {
            return {.status = waveform_analysis_status_t::source_too_large};
        }
        waveform_level_t level{
            .frames_per_bin = prior_frames_per_bin * 2U,
            .bins = std::vector<waveform_bin_t>((prior_bins->size() + 1U) / 2U),
        };
        for (std::size_t output_index = 0; output_index < level.bins.size();
             ++output_index) {
            auto& output = level.bins[output_index];
            const auto first_index = output_index * 2U;
            const auto& first = (*prior_bins)[first_index];
            output = first;
            if (first_index + 1U == prior_bins->size()) {
                continue;
            }
            const auto& second = (*prior_bins)[first_index + 1U];
            output.frame_count += second.frame_count;
            for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
                output.minimum[channel] = std::min(first.minimum[channel],
                                                   second.minimum[channel]);
                output.maximum[channel] = std::max(first.maximum[channel],
                                                   second.maximum[channel]);
                output.absolute_peak[channel] = std::max(first.absolute_peak[channel],
                                                         second.absolute_peak[channel]);
                const auto weighted_squares =
                    static_cast<double>(first.rms[channel]) * first.rms[channel] *
                        static_cast<double>(first.frame_count) +
                    static_cast<double>(second.rms[channel]) * second.rms[channel] *
                        static_cast<double>(second.frame_count);
                output.rms[channel] = static_cast<float>(std::sqrt(
                    weighted_squares / static_cast<double>(output.frame_count)));
            }
        }
        waveform.coarser_levels.push_back(std::move(level));
        prior_bins = &waveform.coarser_levels.back().bins;
        prior_frames_per_bin = waveform.coarser_levels.back().frames_per_bin;
    }

    if (waveform_cache.store(waveform) != waveform_store_status_t::stored) {
        return {
            .status = waveform_analysis_status_t::analysis_cache_capacity_exceeded,
            .waveform = std::move(waveform),
        };
    }
    return {
        .status = waveform_analysis_status_t::generated,
        .waveform = std::move(waveform),
    };
}

auto waveform_level_for_zoom(const waveform_t& waveform,
                             double source_frames_per_pixel)
    -> std::optional<waveform_level_view_t> {
    if (!std::isfinite(source_frames_per_pixel) || source_frames_per_pixel <= 0.0 ||
        waveform.bins.empty() || waveform.key.frames_per_bin == 0) {
        return std::nullopt;
    }
    waveform_level_view_t best{
        .frames_per_bin = waveform.key.frames_per_bin,
        .bins = waveform.bins,
    };
    auto best_distance = std::abs(std::log(
        static_cast<double>(best.frames_per_bin) / source_frames_per_pixel));
    for (const auto& level : waveform.coarser_levels) {
        const auto distance = std::abs(std::log(
            static_cast<double>(level.frames_per_bin) / source_frames_per_pixel));
        if (distance < best_distance) {
            best = {.frames_per_bin = level.frames_per_bin, .bins = level.bins};
            best_distance = distance;
        }
    }
    return best;
}

} // namespace wormedit::audio

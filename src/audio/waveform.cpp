#include <wormedit/audio/waveform.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>
#include <utility>

namespace wormedit::audio {
namespace {

constexpr std::size_t maximum_waveform_workers = 8;
constexpr std::size_t maximum_partition_alignment_bytes = 4'096;

template <typename item_t, typename callable_t>
void for_each_partition(std::vector<item_t>& items, std::size_t worker_count,
                        std::size_t alignment_bytes, callable_t&& operation) {
    const auto item_count = items.size();
    alignment_bytes =
        std::clamp(alignment_bytes, std::size_t{1}, maximum_partition_alignment_bytes);
    if (worker_count < 2 || item_count < 2) {
        operation(0, item_count);
        return;
    }

    std::array<std::size_t, maximum_waveform_workers + 1U> boundaries{};
    boundaries[worker_count] = item_count;
    const auto base_address = reinterpret_cast<std::uintptr_t>(items.data());
    for (std::size_t worker_index = 1; worker_index < worker_count; ++worker_index) {
        const auto quotient = item_count / worker_count;
        const auto remainder = item_count % worker_count;
        const auto target = worker_index * quotient + std::min(worker_index, remainder);
        const auto maximum = item_count - (worker_count - worker_index);
        const auto minimum = boundaries[worker_index - 1U] + 1U;
        auto boundary = std::max(target, minimum);
        if (alignment_bytes > 1U) {
            while (boundary <= maximum &&
                   (base_address + boundary * sizeof(item_t)) % alignment_bytes != 0U) {
                ++boundary;
            }
        }
        if (boundary > maximum) {
            boundary = std::clamp(target, minimum, maximum);
        }
        boundaries[worker_index] = boundary;
    }
    const auto range_for = [&boundaries](std::size_t worker_index) {
        return std::pair{boundaries[worker_index], boundaries[worker_index + 1U]};
    };

    std::vector<std::thread> workers;
    try {
        workers.reserve(worker_count - 1U);
    } catch (...) {
        operation(0, item_count);
        return;
    }

    std::size_t launched_workers{};
    for (std::size_t worker_index = 1; worker_index < worker_count; ++worker_index) {
        const auto [begin, end] = range_for(worker_index);
        try {
            workers.emplace_back([&operation, begin, end] { operation(begin, end); });
            ++launched_workers;
        } catch (...) {
            break;
        }
    }

    const auto [first_begin, first_end] = range_for(0);
    operation(first_begin, first_end);
    for (std::size_t worker_index = launched_workers + 1U; worker_index < worker_count;
         ++worker_index) {
        const auto [begin, end] = range_for(worker_index);
        operation(begin, end);
    }
    for (auto& worker : workers) {
        worker.join();
    }
}

auto waveform_worker_count(std::size_t bin_count, const waveform_analysis_options_t& options)
    -> std::size_t {
    if (options.maximum_parallel_workers < 2 || bin_count < options.parallel_minimum_bins) {
        return 1;
    }
    return std::min({bin_count, options.maximum_parallel_workers, maximum_waveform_workers});
}

auto is_valid_source(const audio_source_identity_t& source) -> bool {
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

auto is_valid_bin(const waveform_bin_t& bin, std::uint64_t frames_per_bin) -> bool {
    if (bin.frame_count == 0 || bin.frame_count > frames_per_bin) {
        return false;
    }
    for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
        if (!std::isfinite(bin.minimum[channel]) || !std::isfinite(bin.maximum[channel]) ||
            !std::isfinite(bin.absolute_peak[channel]) || !std::isfinite(bin.rms[channel]) ||
            bin.minimum[channel] > bin.maximum[channel] || bin.absolute_peak[channel] < 0.0F ||
            bin.rms[channel] < 0.0F) {
            return false;
        }
    }
    return true;
}

auto is_valid_bins(std::span<const waveform_bin_t> bins, std::uint64_t total_frames,
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
        if (!is_valid_bin(bin, frames_per_bin) ||
            bin.frame_count > std::numeric_limits<std::uint64_t>::max() - accounted_frames) {
            return false;
        }
        accounted_frames += bin.frame_count;
    }
    return accounted_frames == total_frames;
}

auto waveform_end(const waveform_t& waveform) -> std::uint64_t {
    return waveform.key.end_frame == 0 ? waveform.total_frames : waveform.key.end_frame;
}

auto waveform_frames(const waveform_t& waveform) -> std::uint64_t {
    const auto end = waveform_end(waveform);
    return end <= waveform.total_frames && waveform.key.begin_frame < end
               ? end - waveform.key.begin_frame : 0;
}

auto is_valid_waveform_geometry(const waveform_t& waveform) -> bool {
    if (!is_valid_source(waveform.key.source) || waveform.key.frames_per_bin == 0 ||
        waveform.sample_rate != output_sample_rate || waveform_frames(waveform) == 0 ||
        waveform.bins.empty()) {
        return false;
    }
    auto frames_per_bin = static_cast<std::uint64_t>(waveform.key.frames_per_bin);
    auto expected_bins = 1U + (waveform_frames(waveform) - 1U) / frames_per_bin;
    if (expected_bins != waveform.bins.size()) {
        return false;
    }
    for (const auto& level : waveform.coarser_levels) {
        if (frames_per_bin > std::numeric_limits<std::uint64_t>::max() / 2U) {
            return false;
        }
        frames_per_bin *= 2U;
        expected_bins = 1U + (waveform_frames(waveform) - 1U) / frames_per_bin;
        if (level.frames_per_bin != frames_per_bin || level.bins.size() != expected_bins) {
            return false;
        }
    }
    return waveform.coarser_levels.empty() || expected_bins == 1;
}

auto is_valid_waveform(const waveform_t& waveform, std::size_t maximum_bins) -> bool {
    if (!is_valid_source(waveform.key.source) || waveform.key.frames_per_bin == 0 ||
        waveform.sample_rate != output_sample_rate || waveform.bins.empty() ||
        waveform.bins.size() > maximum_bins) {
        return false;
    }
    if (!is_valid_bins(waveform.bins, waveform_frames(waveform), waveform.key.frames_per_bin)) {
        return false;
    }
    auto prior_frames_per_bin = static_cast<std::uint64_t>(waveform.key.frames_per_bin);
    auto prior_bin_count = waveform.bins.size();
    for (const auto& level : waveform.coarser_levels) {
        if (prior_frames_per_bin > std::numeric_limits<std::uint64_t>::max() / 2U ||
            level.frames_per_bin != prior_frames_per_bin * 2U ||
            level.bins.size() != (prior_bin_count + 1U) / 2U ||
            !is_valid_bins(level.bins, waveform_frames(waveform), level.frames_per_bin)) {
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

struct waveform_analysis_work_t {
    decoder_t& decoder;
    decoded_block_cache_t& block_cache;
    const stream_info_t& stream_info;
    waveform_analysis_options_t options;
    std::stop_token stop;
    std::uint32_t frames_per_bin{};
    waveform_t waveform;
    std::vector<std::array<double, output_channel_count>> square_sums;
};

auto initialize_waveform_bins(waveform_t& waveform) -> void {
    for (auto& bin : waveform.bins) {
        bin.minimum.fill(std::numeric_limits<float>::infinity());
        bin.maximum.fill(-std::numeric_limits<float>::infinity());
    }
}

auto read_analysis_block(waveform_analysis_work_t& work, std::uint64_t block_index,
                         waveform_analysis_result_t& failure)
    -> std::optional<decoded_audio_block_t> {
    const audio_block_key_t key{.source = work.waveform.key.source, .block_index = block_index};
    if (auto cached = work.block_cache.find(key); cached.has_value()) {
        return cached;
    }
    const auto start_frame = block_index * maximum_read_frames;
    const auto requested_frames = static_cast<std::size_t>(std::min<std::uint64_t>(
        work.stream_info.output_frame_count - start_frame, maximum_read_frames));
    std::vector<float> samples(maximum_read_frames * output_channel_count);
    const auto read = work.decoder.read_at(start_frame, samples, requested_frames);
    if (read.status == read_status_t::seek_error) {
        failure = analysis_failure(block_index, block_decode_status_t::seek_failed);
        return std::nullopt;
    }
    if ((read.status != read_status_t::success && read.status != read_status_t::end_of_stream) ||
        read.frames_read == 0) {
        failure = analysis_failure(block_index, block_decode_status_t::decode_failed);
        return std::nullopt;
    }
    samples.resize(read.frames_read * output_channel_count);
    decoded_audio_block_t block{.key = key,
                                .frame_count = read.frames_read,
                                .interleaved_stereo = std::move(samples)};
    if (work.block_cache.store(key, block.frame_count, block.interleaved_stereo) !=
        block_store_status_t::stored) {
        failure = analysis_failure(block_index,
                                   block_decode_status_t::cache_capacity_exceeded);
        return std::nullopt;
    }
    return block;
}

auto accumulate_analysis_block(const decoded_audio_block_t& block, std::uint64_t& visited_frames,
                               waveform_analysis_work_t& work) -> void {
    const auto block_begin = block.key.block_index * maximum_read_frames;
    const auto first = std::max(block_begin, work.waveform.key.begin_frame);
    const auto end = std::min(block_begin + block.frame_count, waveform_end(work.waveform));
    for (auto global_frame = first; global_frame < end; ++global_frame) {
        const auto frame = static_cast<std::size_t>(global_frame - block_begin);
        const auto bin_index = static_cast<std::size_t>(
            (global_frame - work.waveform.key.begin_frame) / work.frames_per_bin);
        auto& bin = work.waveform.bins[bin_index];
        ++bin.frame_count;
        ++visited_frames;
        for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
            const auto sample = block.interleaved_stereo[frame * output_channel_count + channel];
            bin.minimum[channel] = std::min(bin.minimum[channel], sample);
            bin.maximum[channel] = std::max(bin.maximum[channel], sample);
            bin.absolute_peak[channel] = std::max(bin.absolute_peak[channel], std::abs(sample));
            work.square_sums[bin_index][channel] += static_cast<double>(sample) * sample;
        }
    }
}

auto decode_analysis_blocks(waveform_analysis_work_t& work) -> waveform_analysis_result_t {
    const auto first_block = work.waveform.key.begin_frame / maximum_read_frames;
    const auto last_block = (waveform_end(work.waveform) - 1U) / maximum_read_frames;
    std::uint64_t visited_frames{};
    for (auto index = first_block; index <= last_block; ++index) {
        if (work.stop.stop_requested()) return {.status = waveform_analysis_status_t::cancelled};
        waveform_analysis_result_t failure;
        auto block = read_analysis_block(work, index, failure);
        if (!block) return failure;
        accumulate_analysis_block(*block, visited_frames, work);
    }
    if (visited_frames != waveform_frames(work.waveform))
        return {.status = waveform_analysis_status_t::decode_failed};
    return {.status = waveform_analysis_status_t::generated};
}

auto calculate_base_rms(waveform_analysis_work_t& work) -> bool {
    const auto workers = waveform_worker_count(work.waveform.bins.size(), work.options);
    for_each_partition(work.waveform.bins, workers,
                       work.options.parallel_partition_alignment_bytes,
                       [&work](std::size_t begin, std::size_t end) {
                           for (auto index = begin; index < end; ++index) {
                               if (work.stop.stop_requested()) {
                                   return;
                               }
                               auto& bin = work.waveform.bins[index];
                               for (std::size_t channel = 0; channel < output_channel_count;
                                    ++channel) {
                                   bin.rms[channel] = static_cast<float>(std::sqrt(
                                       work.square_sums[index][channel] /
                                       static_cast<double>(bin.frame_count)));
                               }
                           }
                       });
    return !work.stop.stop_requested();
}

auto append_pyramid_level(waveform_analysis_work_t& work,
                          const std::vector<waveform_bin_t>& prior_bins,
                          std::uint64_t prior_frames_per_bin) -> bool {
    waveform_level_t level{
        .frames_per_bin = prior_frames_per_bin * 2U,
        .bins = std::vector<waveform_bin_t>((prior_bins.size() + 1U) / 2U),
    };
    const auto workers = waveform_worker_count(level.bins.size(), work.options);
    for_each_partition(level.bins, workers, work.options.parallel_partition_alignment_bytes,
                       [&level, &prior_bins, token = work.stop](std::size_t begin,
                                                                std::size_t end) {
                           for (auto out = begin; out < end; ++out) {
                               if (token.stop_requested()) {
                                   return;
                               }
                               auto& output = level.bins[out];
                               const auto first_index = out * 2U;
                               const auto& first = prior_bins[first_index];
                               output = first;
                               if (first_index + 1U == prior_bins.size()) {
                                   continue;
                               }
                               const auto& second = prior_bins[first_index + 1U];
                               output.frame_count += second.frame_count;
                               for (std::size_t channel = 0; channel < output_channel_count;
                                    ++channel) {
                                   output.minimum[channel] =
                                       std::min(first.minimum[channel], second.minimum[channel]);
                                   output.maximum[channel] =
                                       std::max(first.maximum[channel], second.maximum[channel]);
                                   output.absolute_peak[channel] = std::max(
                                       first.absolute_peak[channel], second.absolute_peak[channel]);
                                   const auto weighted_squares =
                                       static_cast<double>(first.rms[channel]) * first.rms[channel] *
                                           static_cast<double>(first.frame_count) +
                                       static_cast<double>(second.rms[channel]) * second.rms[channel] *
                                           static_cast<double>(second.frame_count);
                                   output.rms[channel] = static_cast<float>(std::sqrt(
                                       weighted_squares / static_cast<double>(output.frame_count)));
                               }
                           }
                       });
    if (work.stop.stop_requested()) {
        return false;
    }
    work.waveform.coarser_levels.push_back(std::move(level));
    return true;
}

auto build_waveform_pyramid(waveform_analysis_work_t& work)
    -> waveform_analysis_status_t {
    const std::vector<waveform_bin_t>* prior_bins = &work.waveform.bins;
    auto prior_frames_per_bin = static_cast<std::uint64_t>(work.frames_per_bin);
    while (prior_bins->size() > 1U) {
        if (work.stop.stop_requested()) {
            return waveform_analysis_status_t::cancelled;
        }
        if (prior_frames_per_bin > std::numeric_limits<std::uint64_t>::max() / 2U) {
            return waveform_analysis_status_t::source_too_large;
        }
        if (!append_pyramid_level(work, *prior_bins, prior_frames_per_bin)) {
            return waveform_analysis_status_t::cancelled;
        }
        const auto& level = work.waveform.coarser_levels.back();
        prior_bins = &level.bins;
        prior_frames_per_bin = level.frames_per_bin;
    }
    return waveform_analysis_status_t::generated;
}

} // namespace

waveform_cache_t::waveform_cache_t(waveform_cache_limits_t limits) : limits_(limits) {}

auto waveform_cache_t::find_entry(const waveform_key_t& key) -> entry_iterator_t {
    return std::ranges::find(most_recent_first_, key, &waveform_t::key);
}

auto waveform_cache_t::find(const waveform_key_t& key) -> std::optional<waveform_t> {
    std::scoped_lock lock(mutex_);
    if (!is_valid_source(key.source) || key.frames_per_bin == 0) {
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
    if (!is_valid_waveform(waveform, limits_.maximum_bins_per_entry)) {
        std::scoped_lock lock(mutex_);
        ++stats_.rejected_stores;
        return waveform_store_status_t::invalid_waveform;
    }
    const auto bytes = waveform_bytes(waveform);
    if (!bytes.has_value() || limits_.maximum_entries == 0 || *bytes > limits_.maximum_bytes) {
        std::scoped_lock lock(mutex_);
        ++stats_.rejected_stores;
        return waveform_store_status_t::capacity_exceeded;
    }

    std::scoped_lock lock(mutex_);
    if (const auto existing = find_entry(waveform.key); existing != most_recent_first_.end()) {
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
                      waveform_cache_t& waveform_cache, audio_source_identity_t source,
                      std::uint32_t frames_per_bin, waveform_analysis_options_t options,
                      std::stop_token stop) -> waveform_analysis_result_t {
    const waveform_key_t key{.source = std::move(source), .frames_per_bin = frames_per_bin,
                             .begin_frame = options.begin_frame, .end_frame = options.end_frame};
    if (!is_valid_source(key.source) || frames_per_bin == 0) {
        return {.status = waveform_analysis_status_t::invalid_request};
    }
    if (stop.stop_requested()) {
        return {.status = waveform_analysis_status_t::cancelled};
    }
    if (auto cached = waveform_cache.find(key); cached && !options.require_cached_pcm) {
        return {.status = waveform_analysis_status_t::cache_hit, .waveform = std::move(cached)};
    }

    const auto info = decoder.info();
    if (!info.has_value()) {
        return {.status = waveform_analysis_status_t::decoder_unavailable};
    }
    const auto end = key.end_frame == 0 ? info->output_frame_count : key.end_frame;
    if (key.begin_frame >= end || end > info->output_frame_count)
        return {.status = waveform_analysis_status_t::invalid_request};
    const auto count = expected_bin_count(end - key.begin_frame, frames_per_bin);
    const auto bytes = count.has_value() && *count <= std::numeric_limits<std::size_t>::max()
                           ? bins_bytes(static_cast<std::size_t>(*count))
                           : std::nullopt;
    const auto pyramid_fits =
        bytes.has_value() && *bytes <= waveform_cache.limits().maximum_bytes / 2U;
    if (!count.has_value() || *count > waveform_cache.limits().maximum_bins_per_entry ||
        *count > std::numeric_limits<std::size_t>::max() || !pyramid_fits) {
        return {.status = waveform_analysis_status_t::source_too_large};
    }

    waveform_analysis_work_t work{
        .decoder = decoder,
        .block_cache = block_cache,
        .stream_info = *info,
        .options = options,
        .stop = stop,
        .frames_per_bin = frames_per_bin,
        .waveform = {.key = key,
                     .sample_rate = info->output_sample_rate,
                     .total_frames = info->output_frame_count,
                     .bins = std::vector<waveform_bin_t>(static_cast<std::size_t>(*count))},
        .square_sums = std::vector<std::array<double, output_channel_count>>(
            static_cast<std::size_t>(*count)),
    };
    initialize_waveform_bins(work.waveform);
    const auto decoded = decode_analysis_blocks(work);
    if (decoded.status != waveform_analysis_status_t::generated) {
        return decoded;
    }
    // Decode and LRU access stay on this call's thread; workers touch only local arrays.
    if (!calculate_base_rms(work)) {
        return {.status = waveform_analysis_status_t::cancelled};
    }
    const auto pyramid_status = build_waveform_pyramid(work);
    if (pyramid_status != waveform_analysis_status_t::generated) {
        return {.status = pyramid_status};
    }
    if (stop.stop_requested()) {
        return {.status = waveform_analysis_status_t::cancelled};
    }
    if (waveform_cache.store(work.waveform) != waveform_store_status_t::stored) {
        return {.status = waveform_analysis_status_t::analysis_cache_capacity_exceeded,
                .waveform = std::move(work.waveform)};
    }
    return {.status = waveform_analysis_status_t::generated,
            .waveform = std::move(work.waveform)};
}

auto project_cached_waveform_samples(decoded_block_cache_t& cache,
    const audio_source_identity_t& source, std::uint64_t begin_frame, std::uint64_t end_frame,
    std::size_t maximum_samples) -> std::optional<std::vector<waveform_sample_t>> {
    if (begin_frame >= end_frame || end_frame - begin_frame > maximum_samples ||
        maximum_samples > maximum_waveform_projection_columns) return std::nullopt;
    std::vector<waveform_sample_t> points;
    points.reserve(static_cast<std::size_t>(end_frame - begin_frame));
    const auto first = begin_frame / maximum_read_frames;
    const auto last = (end_frame - 1U) / maximum_read_frames;
    for (auto index = first; index <= last; ++index) {
        const auto found = cache.with_block({source, index}, [&](const decoded_audio_block_t& block) {
            const auto block_begin = index * maximum_read_frames;
            const auto end = std::min(end_frame, block_begin + block.frame_count);
            for (auto frame = std::max(begin_frame, block_begin); frame < end; ++frame) {
                const auto offset = static_cast<std::size_t>(frame - block_begin) * output_channel_count;
                points.push_back({frame, {block.interleaved_stereo[offset],
                                         block.interleaved_stereo[offset + 1U]}});
            }
        });
        if (!found) return std::nullopt;
    }
    if (points.size() != end_frame - begin_frame) return std::nullopt;
    return points;
}

auto waveform_level_for_zoom(const waveform_t& waveform, double source_frames_per_pixel)
    -> std::optional<waveform_level_view_t> {
    if (!std::isfinite(source_frames_per_pixel) || source_frames_per_pixel <= 0.0 ||
        waveform.bins.empty() || waveform.key.frames_per_bin == 0) {
        return std::nullopt;
    }
    waveform_level_view_t best{
        .frames_per_bin = waveform.key.frames_per_bin,
        .bins = waveform.bins,
    };
    auto best_distance =
        std::abs(std::log(static_cast<double>(best.frames_per_bin) / source_frames_per_pixel));
    for (const auto& level : waveform.coarser_levels) {
        const auto distance =
            std::abs(std::log(static_cast<double>(level.frames_per_bin) / source_frames_per_pixel));
        if (distance < best_distance) {
            best = {.frames_per_bin = level.frames_per_bin, .bins = level.bins};
            best_distance = distance;
        }
    }
    return best;
}

auto project_waveform(const waveform_t& waveform, waveform_projection_request_t request)
    -> waveform_projection_result_t {
    if (!is_valid_waveform_geometry(waveform)) {
        return {.status = waveform_projection_status_t::invalid_waveform};
    }
    if (request.begin_frame >= request.end_frame ||
        request.begin_frame < waveform.key.begin_frame || request.end_frame > waveform_end(waveform) ||
        request.pixel_width == 0) {
        return {.status = waveform_projection_status_t::invalid_window};
    }
    if (request.maximum_columns == 0 ||
        request.maximum_columns > maximum_waveform_projection_columns ||
        request.pixel_width > request.maximum_columns) {
        return {.status = waveform_projection_status_t::capacity_exceeded};
    }

    const auto visible_frames = request.end_frame - request.begin_frame;
    const auto column_count =
        static_cast<std::size_t>(std::min<std::uint64_t>(visible_frames, request.pixel_width));
    const auto level = waveform_level_for_zoom(waveform, static_cast<double>(visible_frames) /
                                                             static_cast<double>(column_count));
    if (!level.has_value()) {
        return {.status = waveform_projection_status_t::invalid_waveform};
    }

    waveform_projection_result_t result{
        .status = waveform_projection_status_t::projected,
        .level_frames_per_bin = level->frames_per_bin,
    };
    result.columns.reserve(column_count);
    const auto frames_per_column = visible_frames / column_count;
    const auto remainder = visible_frames % column_count;
    auto begin_frame = request.begin_frame;
    for (std::size_t column_index = 0; column_index < column_count; ++column_index) {
        const auto frame_count =
            frames_per_column + (column_index < remainder ? std::uint64_t{1} : std::uint64_t{0});
        const auto end_frame = begin_frame + frame_count;
        waveform_column_t column{
            .begin_frame = begin_frame,
            .end_frame = end_frame,
        };
        column.minimum.fill(std::numeric_limits<float>::infinity());
        column.maximum.fill(-std::numeric_limits<float>::infinity());
        std::array<double, output_channel_count> weighted_squares{};
        double weighted_frames{};
        const auto first_bin = (begin_frame - waveform.key.begin_frame) / level->frames_per_bin;
        const auto last_bin = (end_frame - 1U - waveform.key.begin_frame) / level->frames_per_bin;
        for (auto bin_index = first_bin; bin_index <= last_bin; ++bin_index) {
            const auto& bin = level->bins[static_cast<std::size_t>(bin_index)];
            if (!is_valid_bin(bin, level->frames_per_bin)) {
                return {.status = waveform_projection_status_t::invalid_waveform};
            }
            weighted_frames += static_cast<double>(bin.frame_count);
            for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
                column.minimum[channel] = std::min(column.minimum[channel], bin.minimum[channel]);
                column.maximum[channel] = std::max(column.maximum[channel], bin.maximum[channel]);
                column.absolute_peak[channel] =
                    std::max(column.absolute_peak[channel], bin.absolute_peak[channel]);
                weighted_squares[channel] += static_cast<double>(bin.rms[channel]) *
                                             static_cast<double>(bin.rms[channel]) *
                                             static_cast<double>(bin.frame_count);
            }
        }
        for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
            column.rms[channel] =
                static_cast<float>(std::sqrt(weighted_squares[channel] / weighted_frames));
        }
        result.columns.push_back(column);
        begin_frame = end_frame;
    }
    return result;
}

} // namespace wormedit::audio

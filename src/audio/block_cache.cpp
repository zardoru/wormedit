#include <wormedit/audio/block_cache.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace wormedit::audio {
namespace {

auto is_valid_source(const audio_source_identity_t& source) -> bool {
    return !source.asset_identity.empty() && !source.source_identity.empty();
}

auto is_valid_key(const audio_block_key_t& key) -> bool {
    return is_valid_source(key.source);
}

auto block_bytes(std::size_t frames) -> std::optional<std::size_t> {
    constexpr auto samples_per_frame = static_cast<std::size_t>(output_channel_count);
    if (frames > std::numeric_limits<std::size_t>::max() / samples_per_frame ||
        frames * samples_per_frame >
            std::numeric_limits<std::size_t>::max() / sizeof(float)) {
        return std::nullopt;
    }
    return frames * samples_per_frame * sizeof(float);
}

auto publish_status(block_decode_phase_t success_status,
                    operation_state_t<session_publish_phase_t> publish_result)
    -> operation_state_t<block_decode_phase_t> {
    if (publish_result.has_failure())
        return std::get<failure_code_t>(publish_result.value());
    return success_status;
}

} // namespace

auto block_decode_result_t::from(operation_state_t<block_decode_phase_t> status, std::size_t frames,
                                 operation_state_t<read_phase_t> decoder_status) -> block_decode_result_t {
    return {.status = status, .frames = frames, .decoder_status = decoder_status};
}

decoded_block_cache_t::decoded_block_cache_t(block_cache_limits_t limits) : limits_(limits) {}

auto decoded_block_cache_t::find_entry(const audio_block_key_t& key) -> entry_iterator_t {
    return std::ranges::find(most_recent_first_, key, &decoded_audio_block_t::key);
}

auto decoded_block_cache_t::find(const audio_block_key_t& key)
    -> std::optional<decoded_audio_block_t> {
    std::scoped_lock lock(mutex_);
    if (!is_valid_key(key)) {
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

auto decoded_block_cache_t::cached_frame_count(const audio_block_key_t& key)
    -> std::optional<std::size_t> {
    std::scoped_lock lock(mutex_);
    if (!is_valid_key(key)) {
        ++stats_.misses;
        return std::nullopt;
    }
    const auto found = find_entry(key);
    if (found == most_recent_first_.end()) {
        ++stats_.misses;
        return std::nullopt;
    }
    ++stats_.hits;
    const auto frame_count = found->frame_count;
    most_recent_first_.splice(most_recent_first_.begin(), most_recent_first_, found);
    return frame_count;
}

auto decoded_block_cache_t::has_block(const audio_block_key_t& key) -> bool {
    return cached_frame_count(key).has_value();
}

auto decoded_block_cache_t::store(const audio_block_key_t& key, std::size_t frame_count,
                                  std::span<const float> interleaved_stereo)
    -> result_t<void, failure_code_t> {
    const auto required_bytes = block_bytes(frame_count);
    if (!is_valid_key(key)) {
        std::scoped_lock lock(mutex_);
        ++stats_.rejected_stores;
        return std::unexpected(failure_code_t::invalid_request);
    }
    if (!required_bytes.has_value() || frame_count == 0 ||
        frame_count > maximum_read_frames ||
        interleaved_stereo.size() != frame_count * output_channel_count) {
        std::scoped_lock lock(mutex_);
        ++stats_.rejected_stores;
        return std::unexpected(failure_code_t::invalid_data);
    }
    if (frame_count > limits_.maximum_frames || *required_bytes > limits_.maximum_bytes ||
        frame_count > limits_.maximum_frames_per_asset ||
        *required_bytes > limits_.maximum_bytes_per_asset) {
        std::scoped_lock lock(mutex_);
        ++stats_.rejected_stores;
        return std::unexpected(failure_code_t::capacity_exceeded);
    }

    std::scoped_lock lock(mutex_);
    if (const auto existing = find_entry(key); existing != most_recent_first_.end()) {
        erase(existing, false);
    }

    while (true) {
        const auto [asset_frames, asset_bytes] = asset_usage(key.source.asset_identity);
        const auto asset_over_capacity =
            asset_frames > limits_.maximum_frames_per_asset - frame_count ||
            asset_bytes > limits_.maximum_bytes_per_asset - *required_bytes;
        const auto global_over_capacity =
            stats_.cached_frames > limits_.maximum_frames - frame_count ||
            stats_.cached_bytes > limits_.maximum_bytes - *required_bytes;
        if (!asset_over_capacity && !global_over_capacity) {
            break;
        }

        auto victim = most_recent_first_.end();
        if (asset_over_capacity) {
            for (auto entry = most_recent_first_.rbegin(); entry != most_recent_first_.rend();
                 ++entry) {
                if (entry->key.source.asset_identity == key.source.asset_identity) {
                    victim = std::prev(entry.base());
                    break;
                }
            }
        } else if (!most_recent_first_.empty()) {
            victim = std::prev(most_recent_first_.end());
        }
        if (victim == most_recent_first_.end()) {
            ++stats_.rejected_stores;
            return std::unexpected(failure_code_t::capacity_exceeded);
        }
        erase(victim, true);
    }

    most_recent_first_.push_front({
        .key = key,
        .frame_count = frame_count,
        .interleaved_stereo = {interleaved_stereo.begin(), interleaved_stereo.end()},
    });
    ++stats_.entries;
    stats_.cached_frames += frame_count;
    stats_.cached_bytes += *required_bytes;
    return {};
}

auto decoded_block_cache_t::asset_usage(std::string_view asset_identity) const
    -> std::pair<std::size_t, std::size_t> {
    std::pair<std::size_t, std::size_t> usage{};
    for (const auto& entry : most_recent_first_) {
        if (entry.key.source.asset_identity == asset_identity) {
            usage.first += entry.frame_count;
            usage.second += entry.interleaved_stereo.size() * sizeof(float);
        }
    }
    return usage;
}

void decoded_block_cache_t::erase(entry_iterator_t entry, bool eviction) {
    stats_.cached_frames -= entry->frame_count;
    stats_.cached_bytes -= entry->interleaved_stereo.size() * sizeof(float);
    --stats_.entries;
    if (eviction) {
        ++stats_.evictions;
    }
    most_recent_first_.erase(entry);
}

void decoded_block_cache_t::clear() {
    std::scoped_lock lock(mutex_);
    most_recent_first_.clear();
    stats_.entries = 0;
    stats_.cached_frames = 0;
    stats_.cached_bytes = 0;
}

void decoded_block_cache_t::clear_source(const audio_source_identity_t& source) {
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

auto decoded_block_cache_t::limits() const noexcept -> block_cache_limits_t {
    return limits_;
}

auto decoded_block_cache_t::stats() const -> block_cache_stats_t {
    std::scoped_lock lock(mutex_);
    return stats_;
}

decode_session_t::decode_session_t(std::size_t maximum_pending_blocks)
    : maximum_pending_blocks_(maximum_pending_blocks) {}

auto decode_session_t::start() -> std::optional<session_token_t> {
    std::scoped_lock lock(mutex_);
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
        active_ = false;
        current_generation_.store(0, std::memory_order_release);
        ready_.clear();
        return std::nullopt;
    }
    ++generation_;
    active_ = true;
    ready_.clear();
    current_generation_.store(generation_, std::memory_order_release);
    return session_token_t{generation_};
}

void decode_session_t::stop() noexcept {
    std::scoped_lock lock(mutex_);
    if (!active_) {
        ready_.clear();
        return;
    }
    active_ = false;
    current_generation_.store(0, std::memory_order_release);
    ready_.clear();
    if (generation_ != std::numeric_limits<std::uint64_t>::max()) {
        ++generation_;
    }
}

auto decode_session_t::is_current(session_token_t token) const noexcept -> bool {
    return token.generation != 0
        && token.generation == current_generation_.load(std::memory_order_acquire);
}

auto decode_session_t::publish_if_current(session_token_t token,
                                          const audio_block_key_t& key)
    -> operation_state_t<session_publish_phase_t> {
    if (!is_valid_key(key)) {
        return wormedit::failure_code_t::invalid_request;
    }
    std::scoped_lock lock(mutex_);
    if (!active_ || token.generation != generation_) {
        return wormedit::failure_code_t::stale_generation;
    }
    if (std::ranges::find(ready_, key) != ready_.end()) {
        return session_publish_phase_t::already_pending;
    }
    if (ready_.size() >= maximum_pending_blocks_) {
        return wormedit::failure_code_t::capacity_exceeded;
    }
    ready_.push_back(key);
    return session_publish_phase_t::accepted;
}

auto decode_session_t::take_ready(session_token_t token) -> std::vector<audio_block_key_t> {
    std::scoped_lock lock(mutex_);
    if (!active_ || token.generation != generation_) {
        return {};
    }
    auto result = std::move(ready_);
    ready_.clear();
    return result;
}

auto decode_session_t::pending_count() const -> std::size_t {
    std::scoped_lock lock(mutex_);
    return ready_.size();
}

auto decode_cached_block(decoder_t& decoder, decoded_block_cache_t& cache,
                         decode_session_t& session, session_token_t token, audio_block_key_t key)
    -> block_decode_result_t {
    if (!session.is_current(token)) {
        return block_decode_result_t::from(wormedit::failure_code_t::stale_generation);
    }
    if (!is_valid_key(key)) {
        return block_decode_result_t::from(wormedit::failure_code_t::invalid_request);
    }

    if (const auto frame_count = cache.cached_frame_count(key); frame_count.has_value()) {
        return block_decode_result_t::from(publish_status(block_decode_phase_t::cache_hit,
                                                          session.publish_if_current(token, key)),
                                           *frame_count, read_phase_t::success);
    }

    const auto stream_info = decoder.info();
    if (!stream_info.has_value()) {
        return block_decode_result_t::from(wormedit::failure_code_t::unavailable);
    }
    if (key.block_index > std::numeric_limits<std::uint64_t>::max() / maximum_read_frames) {
        return block_decode_result_t::from(wormedit::failure_code_t::not_found);
    }
    const auto start_frame = key.block_index * maximum_read_frames;
    if (start_frame >= stream_info->output_frame_count) {
        return block_decode_result_t::from(wormedit::failure_code_t::not_found, 0,
                                           read_phase_t::end_of_stream);
    }
    const auto remaining_frames = stream_info->output_frame_count - start_frame;
    const auto requested_frames =
        static_cast<std::size_t>(std::min<std::uint64_t>(remaining_frames, maximum_read_frames));
    std::vector<float> block(maximum_read_frames * output_channel_count);
    const auto read = decoder.read_at(start_frame, block, requested_frames);
    if (read.status == wormedit::failure_code_t::seek_failed) {
        return block_decode_result_t::from(wormedit::failure_code_t::seek_failed, 0, read.status);
    }
    if (read.status == wormedit::failure_code_t::decode_failed || read.status == wormedit::failure_code_t::not_started ||
        read.status == wormedit::failure_code_t::capacity_exceeded ||
        read.status == wormedit::failure_code_t::invalid_request) {
        return block_decode_result_t::from(wormedit::failure_code_t::decode_failed, read.frames_read,
                                           read.status);
    }
    if (read.frames_read == 0) {
        return block_decode_result_t::from(wormedit::failure_code_t::not_found, 0, read.status);
    }
    if (!session.is_current(token)) {
        return block_decode_result_t::from(wormedit::failure_code_t::stale_generation,
                                           read.frames_read, read.status);
    }

    const auto sample_count = read.frames_read * output_channel_count;
    const auto stored =
        cache.store(key, read.frames_read, std::span<const float>(block.data(), sample_count));
    if (!stored) {
        return block_decode_result_t::from(wormedit::failure_code_t::allocation_failed,
                                           read.frames_read, read.status);
    }
    return block_decode_result_t::from(
        publish_status(block_decode_phase_t::decoded, session.publish_if_current(token, key)),
        read.frames_read, read.status);
}

auto decode_and_cache_block(decoder_t& decoder, decoded_block_cache_t& cache,
                            decode_session_t& session, session_token_t token, audio_block_key_t key)
    -> block_decode_result_t {
    return decode_cached_block(decoder, cache, session, token, std::move(key));
}

} // namespace wormedit::audio

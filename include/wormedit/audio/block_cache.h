#pragma once

#include <wormedit/audio/decoder.h>
#include <wormedit/core/result.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wormedit::audio {

// source_identity should include a content revision/hash when an asset can be
// replaced in place. Both identities participate in every cache key.
struct audio_source_identity_t {
    std::string asset_identity;
    std::string source_identity;

    auto operator==(const audio_source_identity_t&) const -> bool = default;
};

struct audio_block_key_t {
    audio_source_identity_t source;
    std::uint64_t block_index{};

    auto operator==(const audio_block_key_t&) const -> bool = default;
};

struct decoded_audio_block_t {
    audio_block_key_t key;
    std::size_t frame_count{};
    std::vector<float> interleaved_stereo;
};

// The default per-asset allowance is four maximum-size stereo float blocks
// (65,536 frames / 512 KiB). Global limits keep many distinct assets bounded too.
struct block_cache_limits_t {
    std::size_t maximum_bytes{2U * 1024U * 1024U};
    std::size_t maximum_frames{4U * 65'536U};
    std::size_t maximum_bytes_per_asset{512U * 1024U};
    std::size_t maximum_frames_per_asset{65'536U};
};

struct block_cache_stats_t {
    std::size_t entries{};
    std::size_t cached_frames{};
    std::size_t cached_bytes{};
    std::uint64_t hits{};
    std::uint64_t misses{};
    std::uint64_t evictions{};
    std::uint64_t rejected_stores{};
};

// A bounded, thread-safe LRU of normalized 44.1 kHz stereo float blocks.
// find() returns one value copy so callers never hold a pointer invalidated by
// a concurrent eviction. with_block() instead invokes a caller synchronously
// under the cache lock and does not copy PCM; the callback must not retain the
// reference or call back into this cache.
class decoded_block_cache_t {
  public:
    explicit decoded_block_cache_t(block_cache_limits_t limits = {});

    [[nodiscard]] auto find(const audio_block_key_t& key) -> std::optional<decoded_audio_block_t>;
    template <typename callable_t>
    [[nodiscard]] auto with_block(const audio_block_key_t& key, callable_t&& operation) -> bool {
        std::scoped_lock lock(mutex_);
        if (key.source.asset_identity.empty() || key.source.source_identity.empty()) {
            ++stats_.misses;
            return false;
        }
        const auto found = find_entry(key);
        if (found == most_recent_first_.end()) {
            ++stats_.misses;
            return false;
        }
        ++stats_.hits;
        most_recent_first_.splice(most_recent_first_.begin(), most_recent_first_, found);
        std::invoke(std::forward<callable_t>(operation),
                    static_cast<const decoded_audio_block_t&>(most_recent_first_.front()));
        return true;
    }
    [[nodiscard]] auto cached_frame_count(const audio_block_key_t& key)
        -> std::optional<std::size_t>;
    [[nodiscard]] auto has_block(const audio_block_key_t& key) -> bool;
    [[nodiscard]] auto store(const audio_block_key_t& key, std::size_t frame_count,
                             std::span<const float> interleaved_stereo) -> result_t<void, failure_code_t>;
    void clear();
    void clear_source(const audio_source_identity_t& source);

    [[nodiscard]] auto limits() const noexcept -> block_cache_limits_t;
    [[nodiscard]] auto stats() const -> block_cache_stats_t;

  private:
    using entry_list_t = std::list<decoded_audio_block_t>;
    using entry_iterator_t = entry_list_t::iterator;

    [[nodiscard]] auto find_entry(const audio_block_key_t& key) -> entry_iterator_t;
    [[nodiscard]] auto asset_usage(std::string_view asset_identity) const
        -> std::pair<std::size_t, std::size_t>;
    void erase(entry_iterator_t entry, bool eviction);

    block_cache_limits_t limits_;
    mutable std::mutex mutex_;
    entry_list_t most_recent_first_;
    block_cache_stats_t stats_;
};

struct session_token_t {
    std::uint64_t generation{};

    auto operator==(const session_token_t&) const -> bool = default;
};

enum class session_publish_phase_t { accepted, already_pending };

// A headless Stop/session boundary for decoded-block publications. Workers may
// publish only block keys, never PCM ownership into a live transport queue.
// Stop invalidates all prior tokens and clears pending keys. Consumers should
// use with_current() while enqueuing audio so Stop and handoff serialize on the
// same generation lock. A real transport must separately flush its scheduled
// events, voices, and decoder/ring buffers; this session object does not own
// those buffers or the persistent decoded-block cache.
class decode_session_t {
  public:
    explicit decode_session_t(std::size_t maximum_pending_blocks = 4);

    [[nodiscard]] auto start() -> std::optional<session_token_t>;
    void stop() noexcept;
    [[nodiscard]] auto is_current(session_token_t token) const noexcept -> bool;
    [[nodiscard]] auto publish_if_current(session_token_t token, const audio_block_key_t& key)
        -> operation_state_t<session_publish_phase_t>;
    [[nodiscard]] auto take_ready(session_token_t token) -> std::vector<audio_block_key_t>;
    [[nodiscard]] auto pending_count() const -> std::size_t;

    template <typename callable_t>
    [[nodiscard]] auto with_current(session_token_t token, callable_t&& operation) -> bool {
        const std::scoped_lock lock(mutex_);
        if (!active_ || token.generation != generation_) {
            return false;
        }
        std::invoke(std::forward<callable_t>(operation));
        return true;
    }

  private:
    mutable std::mutex mutex_;
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    std::atomic<std::uint64_t> current_generation_{};
    std::uint64_t generation_{};
    bool active_{};
    std::size_t maximum_pending_blocks_{};
    std::vector<audio_block_key_t> ready_;
};

enum class block_decode_phase_t { cache_hit, decoded };

struct block_decode_result_t {
    [[nodiscard]] static auto from(operation_state_t<block_decode_phase_t> status, std::size_t frames = 0,
                                   operation_state_t<read_phase_t> decoder_status = wormedit::failure_code_t::not_started)
        -> block_decode_result_t;
    [[nodiscard]] auto is_success() const -> bool {
        return !status.has_failure();
    }
    operation_state_t<block_decode_phase_t> status{wormedit::failure_code_t::unavailable};
    std::size_t frames{};
    operation_state_t<read_phase_t> decoder_status{wormedit::failure_code_t::not_started};
    auto operator==(const block_decode_result_t&) const -> bool = default;
};

[[nodiscard]] auto decode_cached_block(decoder_t& decoder, decoded_block_cache_t& cache,
                                     decode_session_t& session, session_token_t token,
                                     audio_block_key_t key) -> block_decode_result_t;

// Decodes at most one maximum_read_frames block. A completed block is retained
// in the cache and its key is published only if the supplied session generation
// remains live. Late completions can neither repopulate a stopped session's
// ready queue nor return a decoded payload to its caller.
[[nodiscard]] auto decode_and_cache_block(decoder_t& decoder, decoded_block_cache_t& cache,
                                          decode_session_t& session, session_token_t token,
                                          audio_block_key_t key) -> block_decode_result_t;

} // namespace wormedit::audio

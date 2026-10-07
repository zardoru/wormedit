#pragma once

#include <wormedit/audio/block_cache.h>
#include <wormedit/audio/decoder.h>
#include <wormedit/core/arena.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <vector>

namespace wormedit::audio {

// Short samples (keysounds) are decoded once into an append-only arena and
// stay resident while the project is open. Readers see an immutable snapshot;
// the realtime mixer never allocates, frees, or locks to read it.
struct resident_sample_options_t {
    // Sources longer than this keep streaming through the block cache.
    std::uint64_t maximum_frames_per_sample{30ULL * output_sample_rate};
    // Soft cap on arena payload; beyond it new samples stay on the streaming path.
    std::size_t budget_bytes{512U * 1024U * 1024U};
    // Compact once stale payload reaches this fraction of the appended payload.
    double stale_threshold{1.0 / 3.0};
    std::size_t chunk_bytes{64U * 1024U * 1024U};
};

// Immutable view of the resident samples, keyed by runtime source identity.
// Holding a snapshot keeps every referenced arena chunk alive.
class resident_snapshot_t {
  public:
    struct entry_t {
        std::string asset_identity;
        arena_chunk_handle_t chunk;
        const float* data{};
        std::size_t frame_count{};
    };

    [[nodiscard]] auto find(const audio_source_identity_t& source) const noexcept
        -> const entry_t*;
    // Interleaved stereo frames of one maximum_read_frames block; empty when the
    // source or block is not resident.
    [[nodiscard]] auto block(const audio_block_key_t& key) const noexcept
        -> std::span<const float>;
    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return entries_.size();
    }

  private:
    friend class resident_sample_store_t;
    std::unordered_map<std::string, entry_t> entries_;
};

using resident_snapshot_ptr_t = std::shared_ptr<const resident_snapshot_t>;

struct resident_request_t {
    audio_source_identity_t source;
    // Identifies the file content (path, size, modification time). Equal keys
    // share one decoded copy across source rebindings.
    std::string content_key;
    std::uint64_t frame_count{};
};

enum class resident_load_status_t {
    loaded,
    already_resident,
    too_long,
    over_budget,
    decode_failed,
    // The request is no longer wanted or the run was cancelled.
    stopped,
};

struct resident_sample_stats_t {
    std::size_t resident_samples{};
    std::size_t live_bytes{};
    std::size_t stale_bytes{};
    std::size_t committed_bytes{};
    std::uint64_t decode_count{};
    std::uint64_t compactions{};
    std::uint64_t over_budget_count{};
    std::uint64_t failed_count{};
};

class resident_sample_store_t {
  public:
    using listener_t = std::function<void(resident_snapshot_ptr_t)>;

    explicit resident_sample_store_t(resident_sample_options_t options = {});

    // Called after every publication, outside the store lock, on the thread that
    // changed the store. The listener hands the snapshot to its sessions.
    void set_listener(listener_t listener);

    // Declares the sources the open project uses. Content that is no longer
    // referenced is marked stale; content already resident is rebound to the new
    // source identities. Returns indices into `wanted` that still need load().
    [[nodiscard]] auto reconcile(std::span<const resident_request_t> wanted)
        -> std::vector<std::size_t>;
    // Decodes one wanted request straight into the arena and publishes it.
    [[nodiscard]] auto load(const resident_request_t& request, decoder_t& decoder,
                            std::stop_token stop = {}) -> resident_load_status_t;
    // Compacts when stale payload reaches the threshold; true when it did.
    auto compact_if_needed() -> bool;
    void clear();

    [[nodiscard]] auto snapshot() const -> resident_snapshot_ptr_t;
    [[nodiscard]] auto stats() const -> resident_sample_stats_t;
    // Changes the cap for future loads; existing snapshots and samples stay resident.
    // Returns whether the soft cap changed.
    auto set_budget_bytes(std::size_t budget_bytes) -> bool;
    [[nodiscard]] auto options() const -> resident_sample_options_t;

  private:
    struct content_t {
        append_arena_span_t span;
        std::size_t frame_count{};
        std::uint64_t order{};
    };
    struct binding_t {
        std::string asset_identity;
        std::string content_key;
    };

    [[nodiscard]] auto publish_locked() -> resident_snapshot_ptr_t;
    void notify(const resident_snapshot_ptr_t& snapshot) const;
    [[nodiscard]] auto reserve(const resident_request_t& request)
        -> std::pair<resident_load_status_t, append_arena_span_t>;
    [[nodiscard]] auto commit(const resident_request_t& request, append_arena_span_t span,
                              std::size_t frames) -> resident_load_status_t;
    void discard(const append_arena_span_t& span);
    auto compact_locked() -> bool;

    resident_sample_options_t options_;
    mutable std::mutex mutex_;
    append_arena_t arena_;
    std::unordered_map<std::string, content_t> contents_;
    std::unordered_map<std::string, binding_t> bindings_;
    resident_snapshot_ptr_t current_;
    listener_t listener_;
    std::uint64_t next_order_{};
    std::uint64_t decode_count_{};
    std::uint64_t compactions_{};
    std::uint64_t over_budget_count_{};
    std::uint64_t failed_count_{};
};

} // namespace wormedit::audio

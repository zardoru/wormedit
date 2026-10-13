#pragma once

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include <wormedit/audio/resident_samples.h>
#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/preview_bridge.h>
#include <wormedit/core/document.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/task_graph.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace wormedit::sys {

[[nodiscard]] auto register_audition_sources(
    const song_document_t& document, const std::filesystem::path& asset_base,
    std::uint64_t generation, audio::decoded_source_registry_t& sources,
    std::vector<audio::preview_metronome_source_binding_t>& metronome_bindings)
    -> std::size_t;

// Registers every audio asset without opening any file and builds the metronome
// clicks. The returned count only covers assets with no usable path; files that
// cannot be opened are reported later by decoded_source_registry_t::take_probe_failures().
[[nodiscard]] auto register_audition_sources_deferred(
    const song_document_t& document, const std::filesystem::path& asset_base,
    std::uint64_t generation, audio::decoded_source_registry_t& sources,
    std::vector<audio::preview_metronome_source_binding_t>& metronome_bindings)
    -> std::size_t;

// Submits at most `parallelism` background tasks that probe pending sources in
// registration order. Failure to submit is harmless: a decode or analysis request
// probes its own source on demand.
void start_audition_source_probes(audio::decoded_source_registry_t& sources,
                                  task_scheduler_t& scheduler, std::size_t parallelism = 2);

// Drains finished probe failures (each asset is reported once) into one status line;
// returns nullopt when there is nothing new to report.
[[nodiscard]] auto drain_audition_probe_failures(audio::decoded_source_registry_t& sources)
    -> std::optional<notification_post_t>;

// Assets in the order the chart first plays them (notes and BGM events, by measure
// then row), followed by assets the chart never references, in document order.
[[nodiscard]] auto first_use_asset_order(const song_document_t& document)
    -> std::vector<asset_id_t>;

// Decodes short keysounds into a resident_sample_store_t on a background
// scheduler. start() cancels the previous run (the newest run wins), probes each
// source, rebinds already-resident content, then loads what is missing in
// first-use order. The scheduler must be destroyed before the loader's store
// and registry.
class resident_sample_loader_t {
  public:
    resident_sample_loader_t(audio::resident_sample_store_t& store, task_scheduler_t& scheduler);
    ~resident_sample_loader_t();
    resident_sample_loader_t(const resident_sample_loader_t&) = delete;
    auto operator=(const resident_sample_loader_t&) -> resident_sample_loader_t& = delete;

    void start(const song_document_t& document, const std::filesystem::path& asset_base,
               std::uint64_t generation, audio::decoded_source_registry_t& sources);
    void cancel();
    // Applies a future-load cap and retries only this project's budget-skipped loads.
    void set_budget_bytes(std::size_t bytes);
    // Long sources (the song) cannot be resident; their first seconds are decoded
    // into this cache before the keysounds so playback can start at once.
    void set_head_cache(audio::decoded_block_cache_t* cache, std::uint64_t head_frames = 441'000) {
        head_cache_ = cache;
        head_frames_ = head_frames;
    }
    // Moves these assets to the front of the remaining load queue (for example the
    // sounds near the cursor after a far jump). Safe to call while a run is active;
    // a hint given before start() applies to that run.
    void prioritize(std::vector<asset_id_t> assets);

    struct progress_t {
        std::size_t total{};
        std::size_t done{};
        bool running{};
    };
    [[nodiscard]] auto progress() const -> progress_t;
    // One status line for samples that stayed on the streaming path (over budget or
    // failed to decode); nullopt when there is nothing new.
    [[nodiscard]] auto drain_report() -> std::optional<notification_post_t>;
    // "Loading keysounds 312/1,024" while a run is active and again when it ends;
    // nullopt unless the displayed count changed since the last call.
    [[nodiscard]] auto drain_progress() -> std::optional<notification_post_t>;

    struct run_state_t;

  private:
    audio::resident_sample_store_t& store_;
    task_scheduler_t& scheduler_;
    std::stop_source stop_;
    std::shared_ptr<run_state_t> state_;
    std::shared_ptr<std::mutex> pass_mutex_{std::make_shared<std::mutex>()};
    std::vector<asset_id_t> priority_hint_;
    audio::decoded_block_cache_t* head_cache_{};
    std::uint64_t head_frames_{};
    std::size_t last_progress_{static_cast<std::size_t>(-1)};
};

using audition_asset_entry_t = std::pair<asset_id_t, std::filesystem::path>;

} // namespace wormedit::sys

#endif

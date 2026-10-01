#pragma once

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/preview_bridge.h>
#include <wormedit/core/document.h>
#include <wormedit/core/task_graph.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
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
    -> std::optional<std::string>;

using audition_asset_entry_t = std::pair<asset_id_t, std::filesystem::path>;

struct audition_source_reconciliation_context_t {
    const song_document_t& document;
    const std::filesystem::path& asset_base;
    std::optional<song_id_t>& source_project;
    std::vector<audition_asset_entry_t>& asset_inventory;
    std::uint64_t& generation;
    audio::decoded_source_registry_t& sources;
    std::vector<audio::preview_metronome_source_binding_t>& metronome_bindings;
    std::function<void()> stop_playback;
    std::function<void()> reset_waveform;
    std::function<void()> invalidate_preview;
    // When set, sources are registered lazily and probed on this scheduler; otherwise
    // every source is opened synchronously during reconciliation.
    task_scheduler_t* probe_scheduler{};
};

[[nodiscard]] auto reconcile_audition_sources(
    audition_source_reconciliation_context_t context, bool stop_playback)
    -> std::optional<std::size_t>;

} // namespace wormedit::sys

#endif

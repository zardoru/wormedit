#pragma once

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/preview_bridge.h>
#include <wormedit/core/document.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace wormedit::sys {

[[nodiscard]] auto register_audition_sources(
    const song_document_t& document, const std::filesystem::path& asset_base,
    std::uint64_t generation, audio::decoded_source_registry_t& sources,
    std::vector<audio::preview_metronome_source_binding_t>& metronome_bindings)
    -> std::size_t;

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
};

[[nodiscard]] auto reconcile_audition_sources(
    audition_source_reconciliation_context_t context, bool stop_playback)
    -> std::optional<std::size_t>;

} // namespace wormedit::sys

#endif

#pragma once

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/preview_bridge.h>
#include <wormedit/core/document.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace wormedit::sys {

[[nodiscard]] auto register_audition_sources(
    const song_document_t& document, const std::filesystem::path& asset_base,
    std::uint64_t generation, audio::decoded_source_registry_t& sources,
    std::vector<audio::preview_metronome_source_binding_t>& metronome_bindings)
    -> std::size_t;

} // namespace wormedit::sys

#endif

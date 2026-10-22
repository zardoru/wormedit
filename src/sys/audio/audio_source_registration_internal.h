#pragma once
#include "sys/audio/audio_source_registration.h"
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
namespace wormedit::sys {
[[nodiscard]] auto resolve_asset_path(const audio_asset_t&, const std::filesystem::path&)
    -> std::optional<std::filesystem::path>;
[[nodiscard]] auto register_one_asset(const song_document_t&, const audio_asset_t&,
    std::filesystem::path, std::uint64_t, audio::decoded_source_registry_t&, bool) -> bool;
} // namespace wormedit::sys
#endif

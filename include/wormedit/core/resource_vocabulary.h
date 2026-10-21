#pragma once

#include <cstdint>

namespace wormedit {

enum class resource_use_filter_t : std::uint8_t { all, used, unused, missing };
enum class keysound_layer_filter_kind_t : std::uint8_t { any, no_layer, specific };

// How an authored audio path resolves on disk.
enum class audio_fallback_outcome_t {
    exact,      // the authored path exists as written
    fallback,   // one other file with the same stem and a .wav/.ogg/.flac extension exists
    ambiguous,  // several candidates (extensions or case variants); the user must choose
    missing,    // nothing usable, or the path is unsafe to search
};

} // namespace wormedit

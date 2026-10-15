#pragma once

#include <wormedit/audio/aligned_waveform.h>
#include <wormedit/core/sound_slice.h>

namespace wormedit::audio {

struct channel_waveform_source_geometry_t {
    std::uint32_t sample_rate{};
    std::optional<std::uint64_t> total_frames;
};

} // namespace wormedit::audio

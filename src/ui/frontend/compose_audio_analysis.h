#pragma once
#include <cstddef>

namespace wormedit::ui {
inline constexpr std::size_t compose_spectrum_band_count = 9;
enum class compose_audio_analysis_mode_t { waveform, spectrum, lows, mids, highs };
}

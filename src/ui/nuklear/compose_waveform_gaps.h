#pragma once

#include <algorithm>
#include <utility>
#include <vector>

// A Stop or Delay eats time without advancing rows, so the waveform is not drawn inside its
// band. These are the vertical strips of [top, bottom) left over once the bands are removed.
struct compose_waveform_strip_t {
    float top{};
    float bottom{};
};

inline auto compose_waveform_strips(float top, float bottom,
                                    std::vector<std::pair<float, float>> bands)
    -> std::vector<compose_waveform_strip_t> {
    std::ranges::sort(bands);
    std::vector<compose_waveform_strip_t> strips;
    auto cursor = top;
    for (const auto& [band_top, band_bottom] : bands) {
        if (band_bottom <= cursor || band_top >= bottom) continue;
        if (band_top > cursor) strips.push_back({cursor, band_top});
        cursor = std::max(cursor, band_bottom);
    }
    if (cursor < bottom) strips.push_back({cursor, bottom});
    return strips;
}

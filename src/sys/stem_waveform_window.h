#pragma once

#include <wormedit/core/document.h>

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace wormedit::sys {

// A BMSON chart can have dozens of stems, but only the ones playing near the cursor are worth
// decoding and drawing. The window is measured in chart measures around the cursor's measure.
inline constexpr std::size_t stem_window_measures_before = 8;
inline constexpr std::size_t stem_window_measures_after = 12;
inline constexpr std::size_t maximum_visible_stems = 12;

// The sound channels with an event inside the window, in chart order, at most
// `maximum_visible_stems`. A cursor outside the chart selects the first measures.
[[nodiscard]] inline auto visible_stem_channels(const chart_document_t& chart, measure_id_t cursor)
    -> std::vector<const sound_channel_t*> {
    std::unordered_map<std::uint64_t, std::size_t> index_of;
    index_of.reserve(chart.measures.size());
    for (std::size_t index = 0; index < chart.measures.size(); ++index)
        index_of.emplace(chart.measures[index].id.value, index);
    const auto found = index_of.find(cursor.value);
    const auto center = found == index_of.end() ? std::size_t{0} : found->second;
    const auto first = center > stem_window_measures_before ? center - stem_window_measures_before : 0;
    const auto last = center + stem_window_measures_after;
    std::vector<const sound_channel_t*> visible;
    for (const auto& channel : chart.sound_channels) {
        if (visible.size() >= maximum_visible_stems) break;
        const auto inside = std::ranges::any_of(channel.events, [&](const auto& event) {
            const auto at = index_of.find(event.position.measure.value);
            return at != index_of.end() && at->second >= first && at->second <= last;
        });
        if (inside) visible.push_back(&channel);
    }
    return visible;
}

} // namespace wormedit::sys

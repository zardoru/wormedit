#pragma once

#include <wormedit/core/document.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace wormedit::sys {

// A BMSON chart can have dozens of stems, but only the ones playing near the cursor are worth
// decoding and drawing. The window is measured in chart measures around the cursor's measure.
inline constexpr std::size_t stem_window_measures_before = 8;
inline constexpr std::size_t stem_window_measures_after = 12;
inline constexpr std::size_t maximum_visible_stems = 12;

// How many measures a stem's audio can reach past the event that starts it: its length at the
// chart's fastest tempo (a measure is four beats), rounded up, plus one. A stem whose audio length
// is unknown counts as thirty seconds.
[[nodiscard]] inline auto stem_reach_measures(const song_document_t& document, const chart_document_t& chart,
                                              const sound_channel_t& channel) -> std::size_t {
    double fastest = 0.0;
    for (const auto& bpm : chart.bpms)
        if (bpm.beats_per_minute.denominator != 0)
            fastest = std::max(fastest, static_cast<double>(bpm.beats_per_minute.numerator) /
                                            static_cast<double>(bpm.beats_per_minute.denominator));
    if (fastest <= 0.0) fastest = 240.0;
    double seconds = 30.0;
    const auto asset = std::ranges::find(document.audio_assets, channel.source_clip.source, &audio_asset_t::id);
    if (asset != document.audio_assets.end() && asset->stream.sample_rate != 0 && asset->stream.frame_count)
        seconds = static_cast<double>(*asset->stream.frame_count) / asset->stream.sample_rate;
    return static_cast<std::size_t>(std::ceil(seconds * fastest / 240.0)) + 1;
}

// The sound channels that can be heard in the window, in chart order, at most
// `maximum_visible_stems`: those with an event inside it, or an earlier one whose audio is long
// enough to reach it. A cursor outside the chart selects the first measures.
[[nodiscard]] inline auto visible_stem_channels(const song_document_t& document, const chart_document_t& chart,
                                                measure_id_t cursor) -> std::vector<const sound_channel_t*> {
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
        const auto reach = stem_reach_measures(document, chart, channel);
        const auto earliest = first > reach ? first - reach : 0;
        const auto inside = std::ranges::any_of(channel.events, [&](const auto& event) {
            const auto at = index_of.find(event.position.measure.value);
            return at != index_of.end() && at->second >= earliest && at->second <= last;
        });
        if (inside) visible.push_back(&channel);
    }
    return visible;
}

} // namespace wormedit::sys

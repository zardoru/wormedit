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
inline constexpr std::size_t maximum_visible_stems = 192;

// Measures to look at on each side of the cursor: what the viewport shows at the current zoom,
// plus a margin, so a sliced chart with a hundred tiny stems only decodes the ones in view.
[[nodiscard]] inline auto stem_window_measures(double viewport_height_pixels, double pixels_per_measure)
    -> std::size_t {
    if (!(pixels_per_measure > 0.0) || !(viewport_height_pixels > 0.0)) return 8;
    const auto shown = std::ceil(viewport_height_pixels / pixels_per_measure);
    return static_cast<std::size_t>(std::clamp(shown, 1.0, 64.0)) + 2;
}

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
                                                measure_id_t cursor, std::size_t window_measures)
    -> std::vector<const sound_channel_t*> {
    std::unordered_map<std::uint64_t, std::size_t> index_of;
    index_of.reserve(chart.measures.size());
    for (std::size_t index = 0; index < chart.measures.size(); ++index)
        index_of.emplace(chart.measures[index].id.value, index);
    const auto found = index_of.find(cursor.value);
    const auto center = found == index_of.end() ? std::size_t{0} : found->second;
    const auto first = center > window_measures ? center - window_measures : 0;
    const auto last = center + window_measures;
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

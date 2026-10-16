#include "core/text_util.h"
#include "sys/compose_area_actions.h"
#include "sys/compose_note_to_bgm.h"
#include "sys/compose_resource_presenter.h"
#include "sys/compose_sound_selection.h"
#include "sys/compose_view.h"
#include "sys/frame_timing.h"
#include "sys/frontend_geometry.h"
#include "sys/recipe_form.h"
#include "ui/nuklear/arrow_glyph.h"
#include <wormedit/core/dead_span_rules.h>
#include <wormedit/core/sv_recipe.h>

#include <wormedit/core/editor_scene.h>
#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/virtual_canvas.h>

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <functional>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace wormedit::sys {
void build_draw_stem_sources(const wormedit::song_document_t& document,
                             const wormedit::chart_document_t& chart,
                             compose_scene_cache_t::draw_stem_source_data_t& sources) {
    sources = {};
    for (std::size_t index = 0; index < document.resources.size(); ++index) {
        sources.resource_indices.emplace(document.resources[index].id.value, index);
        if (document.resources[index].sound_kind == wormedit::sound_kind_t::stem)
            sources.explicit_stem_resources.emplace(document.resources[index].id.value, index);
    }
    std::unordered_map<std::uint64_t, std::size_t> channel_lanes;
    for (std::size_t index = 0; index < chart.bgm_source_lanes.size(); ++index) {
        const auto& lane = chart.bgm_source_lanes[index];
        if (lane.stem_channel)
            channel_lanes.emplace(lane.stem_channel->value, index);
    }
    for (const auto& channel : chart.sound_channels)
        if (channel_lanes.contains(channel.id.value))
            sources.channel_continues.emplace(
                channel.id.value, std::ranges::any_of(channel.events, [](const auto& event) {
                    return event.continuation;
                }));
    for (const auto& binding : wormedit::bmson_channel_resource_bindings(document, chart.id)) {
        if (!binding.channel || binding.ambiguous)
            continue;
        const auto lane = channel_lanes.find(binding.channel->value);
        if (lane != channel_lanes.end())
            sources.resource_lanes.emplace(binding.resource.value, lane->second);
    }
}

void prepare_draw_stem_sources(compose_scene_cache_t& cache,
                               const wormedit::song_document_t& document,
                               const wormedit::chart_document_t& chart) {
    static_cast<void>(cache.stems.draw_stem_sources.get_or_build(
        std::tuple{&document, &chart, document.revision},
        std::bind_front(build_draw_stem_sources, std::cref(document), std::cref(chart))));
}

auto draw_stem_source_name(frontend_workspace_view_t& flattened, std::string_view name) -> const char* {
    const auto* owned = flattened.arena() ? arena_string(*flattened.arena(), name) : nullptr;
    if (!owned)
        flattened.view.compose.truncated = true;
    return owned ? owned : "";
}

auto bounded_draw_source_label(const wormedit::resource_entry_t& resource) -> std::string {
    auto identifier = std::to_string(resource.id.value);
    const auto identity =
        std::ranges::find(resource.target_identities, wormedit::format_family_t::bms,
                          &wormedit::target_resource_identity_t::family);
    if (identity != resource.target_identities.end() && !identity->identifier.empty())
        identifier = identity->identifier;
    auto text = identifier + " " + (resource.name.empty() ? "unnamed source" : resource.name);
    constexpr std::size_t limit = 512;
    if (text.size() <= limit)
        return text;
    text.resize(wormedit::text_util::truncate_utf8(text, limit - 3).size());
    text += "…";
    return text;
}

auto compose_scene_cache_t::draw_stem_source_data_t::label_t::from(
    const draw_stem_source_data_t& source, const wormedit::song_document_t& document,
    std::uint8_t kind, std::uint64_t id) -> label_t {
    const auto found = source.resource_indices.find(id);
    const bool missing = kind != 0 && found == source.resource_indices.end();
    return {.text = kind == 0 ? "silent note" : missing ? "missing source"
                   : bounded_draw_source_label(document.resources[found->second]),
            .is_missing = missing};
}

void populate_draw_source_label(frontend_workspace_view_t& flattened, compose_scene_cache_t& cache,
                                const wormedit::song_document_t& document) {
    auto& view = flattened.view.compose;
    const auto& source = cache.stems.draw_stem_sources.value();
    const auto& label = source.label.get_or_build(std::pair{view.draw_source_kind, view.draw_source_id},
        [&source, &document, &view] {
            return compose_scene_cache_t::draw_stem_source_data_t::label_t::from(
                source, document, view.draw_source_kind, view.draw_source_id);
        });
    view.draw_source_label = view.draw_source_kind == 0 ? "silent note"
                             : label.is_missing ? "missing source"
                             : draw_stem_source_name(flattened, label.text);
    view.draw_source_missing = label.is_missing;
}

// Draw on a BGM lane needs a sound; with a silent source the chip says so.
// BMSON lanes use stem and packed-column drawing, which have their own source
// tooltips.
void populate_bgm_draw_hint(WormUiComposeView& view, const wormedit::chart_document_t& chart) {
    if (view.draw_source_kind != 0 || chart.bgm_source_lanes.empty() ||
        chart.source_family == wormedit::format_family_t::bmson)
        return;
    view.draw_source_label = "silent note · Pick a sound to draw BGM";
}

auto compose_sound_is_stem(const compose_scene_cache_t& cache, std::uint64_t resource) -> bool {
    return cache.stems.draw_stem_sources.value().resource_lanes.contains(resource) ||
           cache.stems.draw_stem_sources.value().explicit_stem_resources.contains(resource);
}

auto compose_sound_missing(const compose_scene_cache_t& cache,
                           const wormedit::song_document_t& document, std::uint64_t resource)
    -> bool {
    const auto found = cache.stems.draw_stem_sources.value().resource_indices.find(resource);
    return found == cache.stems.draw_stem_sources.value().resource_indices.end() ||
           document.resources[found->second].missing;
}

void populate_note_stem_sources(frontend_workspace_view_t& flattened,
                                const compose_scene_cache_t& cache,
                                const wormedit::song_document_t& document) {
    for (auto& note : flattened.compose.note) {
        note.sound_is_stem = compose_sound_is_stem(cache, note.sound_resource_id);
        note.sound_missing = compose_sound_missing(cache, document, note.sound_resource_id);
    }
    for (auto& event : flattened.compose.bgm.event)
        event.armable_stem = event.has_sound && compose_sound_is_stem(cache, event.sound_id) &&
                             !compose_sound_missing(cache, document, event.sound_id);
}

void populate_draw_stem_sources(frontend_workspace_view_t& flattened, compose_scene_cache_t& cache,
                                const wormedit::song_document_t& document,
                                const wormedit::chart_document_t& chart) {
    prepare_draw_stem_sources(cache, document, chart);
    auto& view = flattened.view.compose;
    view.draw_source_is_stem = false;
    view.draw_source_stem_lane_id = 0;
    view.draw_source_stem_lane_name = "";
    for (auto& row : flattened.compose.keysound.row) {
        row.stem_source_lane_id = 0;
        row.stem_lane_name = "";
        const auto found = cache.stems.draw_stem_sources.value().resource_lanes.find(row.resource_id);
        if (found == cache.stems.draw_stem_sources.value().resource_lanes.end())
            continue;
        const auto& lane = chart.bgm_source_lanes[found->second];
        row.stem_source_lane_id = lane.id.value;
        row.stem_lane_name = draw_stem_source_name(flattened, lane.name);
        if (view.draw_source_kind == 1 && view.draw_source_id == row.resource_id) {
            view.draw_source_is_stem = true;
            view.draw_source_stem_lane_id = row.stem_source_lane_id;
            view.draw_source_stem_lane_name = row.stem_lane_name;
        }
    }
    if (view.draw_source_kind != 1 || view.draw_source_is_stem)
        return;
    const auto found = cache.stems.draw_stem_sources.value().resource_lanes.find(view.draw_source_id);
    if (found != cache.stems.draw_stem_sources.value().resource_lanes.end()) {
        const auto& lane = chart.bgm_source_lanes[found->second];
        view.draw_source_is_stem = true;
        view.draw_source_stem_lane_id = lane.id.value;
        view.draw_source_stem_lane_name = draw_stem_source_name(flattened, lane.name);
        return;
    }
    const auto explicit_source =
        cache.stems.draw_stem_sources.value().explicit_stem_resources.find(view.draw_source_id);
    if (explicit_source != cache.stems.draw_stem_sources.value().explicit_stem_resources.end()) {
        view.draw_source_is_stem = true;
        view.draw_source_stem_lane_name =
            draw_stem_source_name(flattened, document.resources[explicit_source->second].name);
    }
}

void populate_compose_keysound_fallbacks(frontend_workspace_view_t& flattened,
                                         const wormedit::document_history_t& history,
                                         const std::filesystem::path& asset_base) {
    auto& view = flattened.view.compose;
    view.keysound_fallbacks = nullptr;
    view.keysound_fallback_count = 0;
    auto* arena = flattened.arena();
    if (!arena || asset_base.empty())
        return;
    const auto found = build_keysound_fallbacks_view(history.document, asset_base,
                                                  WORM_UI_KEYSOUND_FALLBACK_CAPACITY);
    if (found.empty())
        return;
    auto rows = arena_array<WormUiKeysoundFallbackView>(*arena, found.size());
    for (const auto& entry : found) {
        WormUiKeysoundFallbackView row{};
        row.resource_id = entry.resource.value;
        row.outcome = entry.result.outcome == audio_fallback_outcome_t::fallback ? 1 : 2;
        for (const auto& candidate : entry.result.candidates) {
            if (row.candidate_count == WORM_UI_KEYSOUND_FALLBACK_CANDIDATES)
                break;
            const auto* text = arena_string(*arena, candidate.generic_string());
            if (!text)
                break;
            row.candidates[row.candidate_count++] = text;
        }
        if (row.candidate_count != 0)
            (void)rows.push_back(row);
    }
    view.keysound_fallbacks = rows.data();
    view.keysound_fallback_count = rows.size();
}

} // namespace wormedit::sys

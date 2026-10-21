#include "core/text_util.h"
#include "ui/compose_draw_source_labels.h"
#include "sys/compose_area_actions.h"
#include "sys/compose_note_to_bgm.h"
#include "sys/compose_resource_presenter.h"
#include "sys/compose_sound_selection.h"
#include "sys/compose_view.h"
#include "sys/frame_timing.h"
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
#include <limits>
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
    return {.text = kind == 0 ? ui::no_draw_sound_resource_label : missing ? "missing source"
                   : bounded_draw_source_label(document.resources[found->second]),
            .is_missing = missing};
}

void populate_draw_source_label(frontend_workspace_view_t& flattened, compose_scene_cache_t& cache,
                                const wormedit::song_document_t& document) {
    auto& view = flattened.view.compose;
    const auto& source = cache.stems.draw_stem_sources.value();
    const auto& label = source.label.get_or_build(std::pair{view.draw_source_kind, view.draw_source_id.value},
        [&source, &document, &view] {
            return compose_scene_cache_t::draw_stem_source_data_t::label_t::from(
                source, document, view.draw_source_kind, view.draw_source_id.value);
        });
    view.draw_source_label = view.draw_source_kind == 0 ? ui::no_draw_sound_resource_label
                             : label.is_missing ? "missing source"
                             : label.text;
    view.draw_source_missing = label.is_missing;
}

// Draw on a BGM lane needs a resource, independently of armed note hitsounds.
// BMSON lanes use stem and packed-column drawing, which have their own source
// tooltips.
void populate_bgm_draw_hint(wormedit::ui::compose_view_t& view, const wormedit::chart_document_t& chart) {
    if (view.draw_source_kind != 0 || chart.bgm_source_lanes.empty() ||
        chart.source_family == wormedit::format_family_t::bmson)
        return;
    view.draw_source_label = ui::no_draw_bgm_resource_hint;
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
    for (auto& note : flattened.view.compose.note_endpoints) {
        note.sound_is_stem = compose_sound_is_stem(cache, note.sound_resource_id.value);
        note.sound_missing = compose_sound_missing(cache, document, note.sound_resource_id.value);
    }
    for (auto& event : flattened.view.compose.bgm_events)
        event.armable_stem = event.has_sound && compose_sound_is_stem(cache, event.sound_id.value) &&
                             !compose_sound_missing(cache, document, event.sound_id.value);
}

void populate_draw_stem_sources(frontend_workspace_view_t& flattened, compose_scene_cache_t& cache,
                                const wormedit::song_document_t& document,
                                const wormedit::chart_document_t& chart) {
    prepare_draw_stem_sources(cache, document, chart);
    auto& view = flattened.view.compose;
    view.draw_source_is_stem = false;
    view.draw_source_stem_lane_id.value = 0;
    view.draw_source_stem_lane_name = "";
    for (auto& row : view.keysounds) {
        row.stem_source_lane_id = {};
        row.stem_lane_name.clear();
        const auto found = cache.stems.draw_stem_sources.value().resource_lanes.find(row.resource_id.value);
        if (found == cache.stems.draw_stem_sources.value().resource_lanes.end())
            continue;
        const auto& lane = chart.bgm_source_lanes[found->second];
        row.stem_source_lane_id = lane.id;
        row.stem_lane_name = lane.name;
        if (view.draw_source_kind == 1 && view.draw_source_id.value == row.resource_id.value) {
            view.draw_source_is_stem = true;
            view.draw_source_stem_lane_id.value = row.stem_source_lane_id.value;
            view.draw_source_stem_lane_name = lane.name;
        }
    }
    if (view.draw_source_kind != 1 || view.draw_source_is_stem)
        return;
    const auto found = cache.stems.draw_stem_sources.value().resource_lanes.find(view.draw_source_id.value);
    if (found != cache.stems.draw_stem_sources.value().resource_lanes.end()) {
        const auto& lane = chart.bgm_source_lanes[found->second];
        view.draw_source_is_stem = true;
        view.draw_source_stem_lane_id.value = lane.id.value;
        view.draw_source_stem_lane_name = lane.name;
        return;
    }
    const auto explicit_source =
        cache.stems.draw_stem_sources.value().explicit_stem_resources.find(view.draw_source_id.value);
    if (explicit_source != cache.stems.draw_stem_sources.value().explicit_stem_resources.end()) {
        view.draw_source_is_stem = true;
        view.draw_source_stem_lane_name =
            document.resources[explicit_source->second].name;
    }
}

void populate_compose_keysound_fallbacks(frontend_workspace_view_t& flattened,
                                         const wormedit::document_history_t& history,
                                         const std::filesystem::path& asset_base) {
    auto& view = flattened.view.compose;
    if (asset_base.empty()) { view.keysound_fallbacks.clear(); return; }
    const auto found = build_keysound_fallbacks_view(history.document, asset_base,
                                                  std::numeric_limits<std::size_t>::max(),
                                                  flattened.query_session_generation);
    view.keysound_fallbacks.resize(found.size());
    for (std::size_t index = 0; index < found.size(); ++index) {
        const auto& entry = found[index];
        auto& row = view.keysound_fallbacks[index];
        row.resource_id = entry.resource;
        row.outcome = entry.result.outcome;
        row.candidates.resize(entry.candidate_paths.size());
        for (std::size_t candidate = 0; candidate < entry.candidate_paths.size(); ++candidate)
            row.candidates[candidate] = entry.candidate_paths[candidate];
    }
}

} // namespace wormedit::sys

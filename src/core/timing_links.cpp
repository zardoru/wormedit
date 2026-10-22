#include <wormedit/core/timing_links.h>
#include "checked_math.h"
#include <algorithm>
#include <numeric>
#include <ranges>
#include <set>
#include <unordered_map>
#include <utility>

namespace wormedit {
auto timing_position_map_t::from(const chart_document_t& source_chart, const chart_document_t& target_chart)
    -> result_t<timing_position_map_t, diagnostic_list_t> {
    auto source_index = measure_index_t::from(source_chart);
    if (!source_index) return std::unexpected(std::move(source_index.error()));
    auto target_index = measure_index_t::from(target_chart);
    if (!target_index) return std::unexpected(std::move(target_index.error()));
    return timing_position_map_t{.source = std::move(*source_index), .target = std::move(*target_index),
        .source_resolution = source_chart.divisions_per_standard_measure,
        .target_resolution = target_chart.divisions_per_standard_measure};
}
auto timing_position_map_t::rows(std::uint64_t source_rows) const -> std::optional<std::uint64_t> {
    if (source_resolution == 0 || target_resolution == 0) return std::nullopt;
    const auto divisor = std::gcd(source_resolution, target_resolution);
    const auto denominator = source_resolution / divisor;
    if (source_rows % denominator != 0) return std::nullopt;
    return checked_math::checked_multiply(source_rows / denominator, target_resolution / divisor);
}
auto timing_position_map_t::position(local_position_t source_position, measure_chart_end_policy_t end) const
    -> std::optional<local_position_t> {
    const auto absolute = source.row_of(source_position, end);
    const auto mapped = absolute ? rows(*absolute) : std::nullopt;
    return mapped ? target.position_at(*mapped, end) : std::nullopt;
}
auto timing_position_map_t::span(position_span_t source_span) const -> std::optional<position_span_t> {
    const auto begin = position(source_span.begin);
    const auto end = position(source_span.end, measure_chart_end_policy_t::allow);
    if (!begin || !end) return std::nullopt;
    return position_span_t{.begin = *begin, .end = *end};
}
namespace {
template<class Id>
auto mapped_identity(const std::vector<timing_link_identity_t<Id>>& identities, Id source) -> std::optional<Id> {
    const auto found = std::ranges::find(identities, source, &timing_link_identity_t<Id>::source);
    return found != identities.end() ? std::optional{found->target} : std::nullopt;
}
template<class Id>
auto are_valid_identities(const std::vector<timing_link_identity_t<Id>>& identities) -> bool {
    std::set<Id> sources, targets;
    for (const auto& identity : identities)
        if (!identity.source || !identity.target || !sources.insert(identity.source).second ||
            !targets.insert(identity.target).second) return false;
    return true;
}
template<class Event>
auto map_event(Event& event, const timing_position_map_t& map) -> bool {
    if constexpr (requires { event.position; }) {
        const auto mapped = map.position(event.position);
        if (!mapped) return false;
        event.position = *mapped;
    } else {
        const auto mapped = map.span(event.span);
        if (!mapped) return false;
        event.span = *mapped;
    }
    return true;
}
auto map_event(sv_recipe_t& recipe, const timing_position_map_t& map) -> bool {
    const auto range = map.span(recipe.range);
    if (!range) return false;
    recipe.range = *range;
    if (recipe.sampling.fixed_step_rows) {
        const auto step = map.rows(*recipe.sampling.fixed_step_rows);
        if (!step || *step == 0) return false;
        recipe.sampling.fixed_step_rows = *step;
    }
    return true;
}
template<class Source, class Target, class Id>
auto mapped_events_match(Source&& source, Target&& target, const timing_position_map_t& map,
    const std::vector<timing_link_identity_t<Id>>& identities) -> bool {
    if (std::ranges::distance(source) != static_cast<std::ptrdiff_t>(identities.size())) return false;
    return std::ranges::equal(source, target, [&](const auto& original, const auto& actual) {
        auto expected = original;
        const auto id = mapped_identity(identities, original.id);
        if (!id || !map_event(expected, map)) return false;
        expected.id = *id;
        return expected == actual;
    });
}
auto recipes_match(const chart_document_t& source, const chart_document_t& target, timing_category_t category,
    const timing_position_map_t& map, const timing_category_link_t& link) -> bool {
    const auto selected = [category](const auto& recipe) { return timing_category_info_t::for_recipe(recipe.output) == category; };
    return mapped_events_match(source.sv_recipes | std::views::filter(selected),
        target.sv_recipes | std::views::filter(selected), map, link.recipes);
}
auto layouts_match(const chart_document_t& source, const chart_document_t& target,
    const timing_category_link_t& link) -> bool {
    if (!link.objects.empty() || source.measures.size() != link.measures.size()) return false;
    return std::ranges::equal(source.measures, target.measures, [&](const auto& original, const auto& actual) {
        return mapped_identity(link.measures, original.id) == actual.id && original.definition == actual.definition;
    });
}
auto points_match(const chart_document_t& source, const chart_document_t& target, timing_category_t category,
    const timing_position_map_t& map, const timing_category_link_t& link) -> bool {
    const auto events = [&]<class Original, class Actual>(Original&& originals, Actual&& actuals) {
        return mapped_events_match(std::forward<Original>(originals), std::forward<Actual>(actuals), map, link.objects);
    };
    const auto pauses = [&](pause_kind_t kind) {
        const auto selected = [kind](const auto& pause) { return pause.kind == kind; };
        return events(source.pauses | std::views::filter(selected), target.pauses | std::views::filter(selected));
    };
    switch (category) {
    case timing_category_t::bpm: return events(source.bpms, target.bpms);
    case timing_category_t::scroll: return events(source.scrolls, target.scrolls);
    case timing_category_t::speed: return events(source.speeds, target.speeds);
    case timing_category_t::kiai: return events(source.kiai, target.kiai);
    case timing_category_t::stop: return pauses(pause_kind_t::stop);
    case timing_category_t::delay: return pauses(pause_kind_t::delay);
    case timing_category_t::warp: return events(source.warps, target.warps);
    case timing_category_t::anchor: return events(source.timing_anchors, target.timing_anchors);
    case timing_category_t::offset:
        return link.objects.empty() && source.beat_zero_audio_offset_seconds == target.beat_zero_audio_offset_seconds;
    case timing_category_t::measures: return layouts_match(source, target, link);
    case timing_category_t::count: return false;
    }
    return false;
}
auto snapshot_matches(const chart_document_t& source, const chart_document_t& target,
    timing_category_t category, const timing_category_link_t& link) -> bool {
    const auto map = timing_position_map_t::from(source, target);
    if (!map || (category != timing_category_t::measures && !link.measures.empty())) return false;
    return points_match(source, target, category, *map, link) && recipes_match(source, target, category, *map, link);
}
auto has_cycle(const song_document_t& song, timing_category_t category) -> bool {
    std::unordered_map<std::uint64_t, std::size_t> indices;
    for (std::size_t index = 0; index < song.charts.size(); ++index) indices.emplace(song.charts[index].id.value, index);
    std::vector<unsigned char> states(song.charts.size());
    for (std::size_t root = 0; root < song.charts.size(); ++root) {
        if (states[root] != 0) continue;
        std::vector<std::size_t> path;
        std::optional<std::size_t> current = root;
        while (current && states[*current] == 0) {
            states[*current] = 1;
            path.push_back(*current);
            const auto& link = song.charts[*current].timing_links.value(category);
            const auto next = link && !link->is_local_override ? indices.find(link->source.value) : indices.end();
            current = next != indices.end() ? std::optional{next->second} : std::nullopt;
        }
        if (current && states[*current] == 1) return true;
        for (const auto index : path) states[index] = 2;
    }
    return false;
}
void validate_link(const song_document_t& song, const chart_document_t& target,
    const timing_category_info_t& category, const timing_category_link_t& link, diagnostic_list_t& diagnostics) {
    const auto report = [&](diagnostic_code_t code, std::string_view message) {
        diagnostics.push_back(error(code, "Chart " + std::to_string(target.id.value) + " " +
            std::string{category.label} + " link: " + std::string{message}));
    };
    const auto* source = song.find_chart(link.source);
    if (!source || source->id == target.id) { report(diagnostic_code_t::invalid_id, "source must be another chart in this project."); return; }
    if (!are_valid_identities(link.objects) || !are_valid_identities(link.recipes) || !are_valid_identities(link.measures)) {
        report(diagnostic_code_t::invalid_id, "identity mapping is invalid or ambiguous."); return;
    }
    if (!link.is_local_override && !snapshot_matches(*source, target, category.kind, link))
        report(diagnostic_code_t::unsupported_feature, "stored timing does not exactly match its source.");
}
}
auto validate_timing_links(const song_document_t& song) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    for (const auto& category : timing_categories)
        if (has_cycle(song, category.kind))
            diagnostics.push_back(error(diagnostic_code_t::invalid_id, std::string{category.label} + " timing links contain a source cycle."));
    for (const auto& chart : song.charts)
        for (const auto& category : timing_categories)
            if (const auto& link = chart.timing_links.value(category.kind))
                validate_link(song, chart, category, *link, diagnostics);
    return diagnostics;
}
} // namespace wormedit

#include <wormedit/core/timing_links.h>
#include "checked_math.h"
#include "id_ceiling.h"
#include "timing_links_internal.h"
#include <wormedit/core/command.h>
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
using timing_link_detail::visit_timing_events;
using timing_link_detail::timing_event_filter_t;
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
    if (category == timing_category_t::offset)
        return link.objects.empty() && source.beat_zero_audio_offset_seconds == target.beat_zero_audio_offset_seconds;
    if (category == timing_category_t::measures) return layouts_match(source, target, link);
    return visit_timing_events(source, target, category, [&](const auto& originals, const auto& actuals, timing_event_filter_t filter) {
        return mapped_events_match(originals | std::views::filter(filter), actuals | std::views::filter(filter), map, link.objects);
    });
}
auto snapshot_matches(const chart_document_t& source, const chart_document_t& target,
    timing_category_t category, const timing_category_link_t& link) -> bool {
    const auto map = timing_position_map_t::from(source, target);
    if (!map || (category != timing_category_t::measures && !link.measures.empty())) return false;
    return points_match(source, target, category, *map, link) && recipes_match(source, target, category, *map, link);
}
auto has_cycle(const song_document_t& song, timing_category_t category) -> bool {
    const auto active = std::ranges::any_of(song.charts, [category](const auto& chart) {
        const auto& link = chart.timing_links.value(category);
        return link && !link->is_local_override;
    });
    if (!active) return false;
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
    const timing_category_info_t& category, const timing_category_link_t& link, diagnostic_list_t& diagnostics, bool check_snapshots) {
    const auto report = [&](diagnostic_code_t code, std::string_view message) {
        diagnostics.push_back(error(code, "Chart " + std::to_string(target.id.value) + " " +
            std::string{category.label} + " link: " + std::string{message}));
    };
    const auto* source = song.find_chart(link.source);
    if (!source) { report(diagnostic_code_t::invalid_id, "source is missing; detach dependents before deleting a source."); return; }
    if (source->id == target.id) { report(diagnostic_code_t::invalid_id, "source must be another chart in this project."); return; }
    if (!are_valid_identities(link.objects) || !are_valid_identities(link.recipes) || !are_valid_identities(link.measures)) {
        report(diagnostic_code_t::invalid_id, "identity mapping is invalid or ambiguous."); return;
    }
    if (check_snapshots && !link.is_local_override && !snapshot_matches(*source, target, category.kind, link))
        report(diagnostic_code_t::unsupported_feature, "stored timing does not exactly match its source.");
}
auto has_links(const song_document_t& song) -> bool {
    return std::ranges::any_of(song.charts, [](const auto& chart) {
        return std::ranges::any_of(chart.timing_links.categories, [](const auto& link) { return link.has_value(); });
    });
}
auto local_identity_map(const chart_document_t& chart, timing_category_t category) -> timing_category_link_t {
    timing_category_link_t result;
    (void)visit_timing_events(chart, chart, category, [&](const auto& events, const auto&, timing_event_filter_t filter) {
        for (const auto& event : events | std::views::filter(filter)) result.objects.push_back({.source = event.id, .target = event.id});
        return true;
    });
    for (const auto& recipe : chart.sv_recipes)
        if (timing_category_info_t::for_recipe(recipe.output) == category)
            result.recipes.push_back({.source = recipe.id, .target = recipe.id});
    if (category == timing_category_t::measures)
        for (const auto& measure : chart.measures) result.measures.push_back({.source = measure.id, .target = measure.id});
    return result;
}
void detach_local_writes(const song_document_t& before, song_document_t& proposed) {
    for (auto& chart : proposed.charts) {
        const auto* previous = before.find_chart(chart.id);
        if (!previous) continue;
        for (const auto& category : timing_categories) {
            auto& link = chart.timing_links.value(category.kind);
            if (!link || link->is_local_override || link != previous->timing_links.value(category.kind)) continue;
            if (!snapshot_matches(*previous, chart, category.kind, local_identity_map(*previous, category.kind)))
                link->is_local_override = true;
        }
    }
}
template<class Id> class timing_identity_allocator_t {
  public:
    template<class Owner, class Events>
    static auto from(const Owner& owner, Events&& replaced, const std::vector<timing_link_identity_t<Id>>& identities)
        -> timing_identity_allocator_t {
        timing_identity_allocator_t result;
        result.ceiling_ = id_ceiling_t<Id>::from(owner);
        auto reserve = [&](Id id) { result.reserved_.insert(id); return true; };
        (void)id_declarations_t<Id>::visit(owner, reserve);
        for (const auto& event : replaced) { result.reserved_.erase(event.id); --result.ceiling_.count; }
        for (const auto& identity : identities) result.ceiling_.maximum = std::max(result.ceiling_.maximum, identity.target.value);
        result.ceiling_.limit = default_maximum_chart_objects;
        return result;
    }
    auto assign(Id source, const std::vector<timing_link_identity_t<Id>>& identities) -> std::optional<Id> {
        auto candidate = mapped_identity(identities, source);
        if (!candidate || reserved_.contains(*candidate)) candidate = ceiling_.next();
        if (!candidate || !ceiling_(*candidate)) return std::nullopt;
        reserved_.insert(*candidate);
        return candidate;
    }
  private:
    id_ceiling_t<Id> ceiling_;
    std::set<Id> reserved_;
};
template<class Source, class Id>
auto build_linked_events(Source&& source, const timing_position_map_t& map, timing_identity_allocator_t<Id>& allocator,
    const std::vector<timing_link_identity_t<Id>>& previous, std::vector<timing_link_identity_t<Id>>& identities)
    -> result_t<std::vector<std::ranges::range_value_t<Source>>, diagnostic_t> {
    std::vector<std::ranges::range_value_t<Source>> events;
    for (const auto& original : source) {
        const auto id = allocator.assign(original.id, previous);
        if (!id) return std::unexpected(error(diagnostic_code_t::invalid_id, "Linked timing identity capacity is exhausted."));
        auto event = original;
        if (!map_event(event, map)) return std::unexpected(error(diagnostic_code_t::row_out_of_range,
            "Linked timing cannot map exactly into the target grid or chart extent."));
        event.id = *id;
        events.push_back(std::move(event));
        identities.push_back({.source = original.id, .target = *id});
    }
    return events;
}
auto copy_points(const chart_document_t& source, chart_document_t& target, timing_category_t category,
    const timing_position_map_t& map, timing_category_link_t& link) -> std::optional<diagnostic_t> {
    return visit_timing_events(source, target, category,
        [&](const auto& originals, auto& actuals, timing_event_filter_t filter) -> std::optional<diagnostic_t> {
            auto allocator = timing_identity_allocator_t<object_id_t>::from(target, actuals | std::views::filter(filter), link.objects);
            std::vector<timing_link_identity_t<object_id_t>> identities;
            auto events = build_linked_events(originals | std::views::filter(filter), map, allocator, link.objects, identities);
            if (!events) return events.error();
            std::erase_if(actuals, filter);
            actuals.insert(actuals.end(), std::make_move_iterator(events->begin()), std::make_move_iterator(events->end()));
            link.objects = std::move(identities);
            return std::nullopt;
        });
}
auto copy_recipes(const song_document_t& song, const chart_document_t& source, chart_document_t& target,
    timing_category_t category, const timing_position_map_t& map, timing_category_link_t& link) -> std::optional<diagnostic_t> {
    const auto selected = [category](const auto& recipe) { return timing_category_info_t::for_recipe(recipe.output) == category; };
    auto originals = source.sv_recipes | std::views::filter(selected);
    auto actuals = target.sv_recipes | std::views::filter(selected);
    if (originals.empty() && actuals.empty()) { link.recipes.clear(); return std::nullopt; }
    auto allocator = timing_identity_allocator_t<recipe_id_t>::from(song, actuals, link.recipes);
    std::vector<timing_link_identity_t<recipe_id_t>> identities;
    auto events = build_linked_events(originals, map, allocator, link.recipes, identities);
    if (!events) return events.error();
    std::erase_if(target.sv_recipes, selected);
    target.sv_recipes.insert(target.sv_recipes.end(), std::make_move_iterator(events->begin()), std::make_move_iterator(events->end()));
    link.recipes = std::move(identities);
    return std::nullopt;
}
auto copy_layout(const chart_document_t& source, const chart_document_t& target, timing_category_link_t& link)
    -> std::optional<diagnostic_t> {
    if (!std::ranges::equal(source.measures, target.measures, [](const auto& left, const auto& right) { return left.definition == right.definition; }))
        return error(diagnostic_code_t::unsupported_feature, "The linked measure layout requires an exact local-content remap.");
    link.objects.clear(); link.recipes.clear(); link.measures.clear();
    for (std::size_t index = 0; index < source.measures.size(); ++index)
        link.measures.push_back({.source = source.measures[index].id, .target = target.measures[index].id});
    return std::nullopt;
}
auto materialized_record_count(const chart_document_t& source, timing_category_t category) -> std::size_t {
    auto count = visit_timing_events(source, source, category, [](const auto& events, const auto&, timing_event_filter_t filter) {
        return static_cast<std::size_t>(std::ranges::count_if(events, filter));
    });
    count += static_cast<std::size_t>(std::ranges::count_if(source.sv_recipes,
        [category](const auto& recipe) { return timing_category_info_t::for_recipe(recipe.output) == category; }));
    if (category == timing_category_t::measures) count += source.measures.size();
    return count;
}
auto materialize_category(song_document_t& song, const chart_document_t& source, chart_document_t& target,
    timing_category_t category, timing_category_link_t& link) -> std::optional<diagnostic_t> {
    if (category == timing_category_t::measures) return copy_layout(source, target, link);
    const auto map = timing_position_map_t::from(source, target);
    if (!map) return map.error().front();
    link.measures.clear();
    if (category == timing_category_t::offset) {
        link.objects.clear();
        target.beat_zero_audio_offset_seconds = source.beat_zero_audio_offset_seconds;
    } else if (auto failure = copy_points(source, target, category, *map, link)) return failure;
    return copy_recipes(song, source, target, category, *map, link);
}
class timing_resolution_t {
  public:
    timing_resolution_t(song_document_t& song, timing_link_limits_t limits) : song_(song), limits_(limits) {}
    auto resolve(timing_category_t category) -> std::optional<diagnostic_t> {
        std::set<chart_id_t> pending;
        for (const auto& chart : song_.charts) {
            const auto& link = chart.timing_links.value(category);
            if (link && !link->is_local_override) pending.insert(chart.id);
        }
        while (!pending.empty()) {
            auto ready = std::ranges::find_if(pending, [&](chart_id_t id) {
                return !pending.contains(song_.find_chart(id)->timing_links.value(category)->source);
            });
            if (ready == pending.end()) return error(diagnostic_code_t::invalid_id, "Timing links contain a source cycle.");
            if (auto failure = resolve_one(*ready, category)) return failure;
            pending.erase(ready);
        }
        return std::nullopt;
    }
  private:
    auto resolve_one(chart_id_t id, timing_category_t category) -> std::optional<diagnostic_t> {
        auto& target = *song_.find_chart(id);
        auto& link = *target.timing_links.value(category);
        const auto& source = *song_.find_chart(link.source);
        if (snapshot_matches(source, target, category, link)) return std::nullopt;
        if (!checked_math::account_size(copied_records_, materialized_record_count(source, category), limits_.maximum_materialized_records))
            return error(diagnostic_code_t::unsupported_feature, "Timing propagation exceeds its materialization budget.");
        return materialize_category(song_, source, target, category, link);
    }
    song_document_t& song_;
    timing_link_limits_t limits_;
    std::size_t copied_records_{};
};
auto has_category_content(const chart_document_t& chart, timing_category_t category) -> bool {
    if (category == timing_category_t::offset) return chart.beat_zero_audio_offset_seconds.numerator != 0;
    return materialized_record_count(chart, category) != 0;
}
auto update_link(chart_document_t& chart, const song_document_t& song, const timing_link_request_t& request)
    -> attempt_t<document_edit_effect_t> {
    if (static_cast<std::size_t>(request.category) >= timing_category_count ||
        static_cast<std::size_t>(request.operation) >= static_cast<std::size_t>(timing_link_operation_t::count))
        return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Unknown timing link category or operation.")}};
    auto& current = chart.timing_links.value(request.category);
    if (request.operation == timing_link_operation_t::detach) {
        const bool changed = current.has_value();
        current.reset();
        return {.value = document_edit_effect_t{.changed = changed, .command_label = "Detach timing",
            .options = {.preserve_retained_source_flags = true}}};
    }
    if (request.operation == timing_link_operation_t::relink && !current)
        return {.diagnostics = {error(diagnostic_code_t::invalid_id, "This timing category has no source to relink.")}};
    const auto source = request.operation == timing_link_operation_t::relink ? current->source : request.source;
    if (!source || source == chart.id || !song.find_chart(source))
        return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Timing source must be another chart in this project.")}};
    if (current && current->source == source && !current->is_local_override)
        return {.value = document_edit_effect_t{.command_label = "Link timing"}};
    if ((!current || current->is_local_override) && has_category_content(chart, request.category) && !request.discard_confirmed)
        return {.diagnostics = {error(diagnostic_code_t::unsupported_feature, "Confirm replacing this category's local timing before linking.")}};
    if (!current || current->source != source) current = timing_category_link_t{.source = source};
    current->is_local_override = false;
    return {.value = document_edit_effect_t{.changed = true, .command_label = "Link timing"}};
}
auto validate_links(const song_document_t& song, bool check_snapshots) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    if (!has_links(song)) return diagnostics;
    for (const auto& category : timing_categories)
        if (has_cycle(song, category.kind))
            diagnostics.push_back(error(diagnostic_code_t::invalid_id, std::string{category.label} + " timing links contain a source cycle."));
    for (const auto& chart : song.charts)
        for (const auto& category : timing_categories)
            if (const auto& link = chart.timing_links.value(category.kind))
                validate_link(song, chart, category, *link, diagnostics, check_snapshots);
    return diagnostics;
}
}
auto reconcile_timing_links(const song_document_t& before, song_document_t& proposed, timing_link_limits_t limits)
    -> diagnostic_list_t {
    if (!has_links(proposed)) return {};
    detach_local_writes(before, proposed);
    auto diagnostics = validate_links(proposed, false);
    if (diagnostic_queries::has_errors(diagnostics)) return diagnostics;
    timing_resolution_t resolution{proposed, limits};
    if (auto failure = resolution.resolve(timing_category_t::measures)) return {*failure};
    for (const auto& category : timing_categories)
        if (category.kind != timing_category_t::measures)
            if (auto failure = resolution.resolve(category.kind)) return {*failure};
    return diagnostics;
}
auto stage_timing_link_edit(document_command_transaction_t& transaction, chart_id_t chart,
    const timing_link_request_t& request) -> attempt_t<document_edit_effect_t> {
    return transaction.stage(chart, "Timing link refers to an unknown chart.",
        [&](chart_document_t& target, song_document_t& song) { return update_link(target, song, request); }, true);
}
auto apply_timing_link_edit(document_history_t& history, const chart_stamp_t& stamp,
    const timing_link_request_t& request) -> document_command_result_t {
    document_command_transaction_t transaction{history, stamp, "Timing link"};
    return transaction.finish_execution(stage_timing_link_edit(transaction, stamp.chart, request));
}
auto validate_timing_links(const song_document_t& song) -> diagnostic_list_t {
    return validate_links(song, true);
}
} // namespace wormedit

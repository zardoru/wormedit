#pragma once
#include <wormedit/core/document.h>
#include <wormedit/core/diagnostic.h>
#include <wormedit/core/timing_links.h>
#include "timing_links_internal.h"
#include <ranges>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>
namespace wormedit {
struct all_chart_timing_positions_t {
    auto operator()(timing_category_t) const -> bool { return true; }
};
template<class Chart, class Visit, class Include>
void for_each_timing_position(Chart& chart, Visit& visit, Include& include) {
    for (const auto& category : timing_categories) {
        if (!include(category.kind)) continue;
        (void)timing_link_detail::visit_timing_events(chart, chart, category.kind,
            [&](auto& events, auto&, timing_link_detail::timing_event_filter_t filter) {
                for (auto& event : events | std::views::filter(filter)) {
                    if constexpr (requires { event.position; }) visit(event.position, position_role_t::point);
                    else {
                        visit(event.span.begin, position_role_t::span_begin);
                        visit(event.span.end, position_role_t::span_end);
                    }
                }
                return true;
            });
    }
}
// Positions outside note/BGM head buckets, plus their hold ends. Existing measure
// operations use all timing; linked layout changes can exclude derived categories.
template<class Chart, class Visit, class Include = all_chart_timing_positions_t>
void for_each_free_position(Chart& chart, Visit&& visit, Include include = {}) {
    const auto ends = [&](auto& objects) {
        for (auto& object : objects)
            if (object.end) visit(*object.end, position_role_t::note_end);
    };
    for (auto& measure : chart.measures) { ends(measure.notes); ends(measure.bgm_events); }
    for_each_timing_position(chart, visit, include);
    // Sample defaults are chart-local; timing links do not own these lanes.
    for (auto& event : chart.volumes) visit(event.position, position_role_t::point);
    for (auto& event : chart.sample_sets) visit(event.position, position_role_t::point);
    for (auto& event : chart.bga_events) visit(event.position, position_role_t::point);
    for (auto& bookmark : chart.bookmarks)
        if (bookmark.position) visit(*bookmark.position, position_role_t::point);
    for (auto& section : chart.sections) {
        if (!section.beat_span) continue;
        visit(section.beat_span->begin, position_role_t::span_begin);
        visit(section.beat_span->end, position_role_t::span_end);
    }
    for (auto& channel : chart.sound_channels)
        for (auto& event : channel.events) {
            visit(event.position, position_role_t::point);
            if (event.end) visit(*event.end, position_role_t::span_end);
        }
}
// Only recipe range ends may address the exclusive chart boundary. Every other
// stored position, including hold/warp/section ends, requires an ordinary row.
template<class Chart, class Visit, class Include = all_chart_timing_positions_t>
void for_each_chart_position(Chart& chart, Visit&& visit, Include include = {}) {
    for_each_free_position(chart, [&](auto& position, position_role_t) {
        visit(position, measure_chart_end_policy_t::reject);
    }, include);
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) visit(note.position, measure_chart_end_policy_t::reject);
        for (auto& bgm : measure.bgm_events) visit(bgm.position, measure_chart_end_policy_t::reject);
    }
    for (auto& layout : chart.lane_layouts) visit(layout.position, measure_chart_end_policy_t::reject);
    for (auto& recipe : chart.sv_recipes) {
        if (!include(timing_category_info_t::for_recipe(recipe.output))) continue;
        visit(recipe.range.begin, measure_chart_end_policy_t::reject);
        visit(recipe.range.end, measure_chart_end_policy_t::allow);
    }
}
inline auto rehome_measure_objects(chart_document_t& chart, std::span<measure_chunk_t> sources,
    diagnostic_list_t& diagnostics) -> bool {
    std::unordered_map<std::uint64_t, std::size_t> destinations;
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        destinations.emplace(chart.measures[index].id.value, index);
    }

    std::vector<std::vector<note_t>> notes(chart.measures.size());
    std::vector<std::vector<bgm_event_t>> bgm_events(chart.measures.size());
    bool valid = true;
    for (auto& measure : sources) {
        for (auto& note : measure.notes) {
            const auto destination = destinations.find(note.position.measure.value);
            if (destination == destinations.end()) {
                diagnostics.push_back(error(diagnostic_code_t::unknown_measure,
                      "quantized note refers to an unknown measure", note.id));
                valid = false;
            } else {
                notes[destination->second].push_back(std::move(note));
            }
        }
        for (auto& bgm : measure.bgm_events) {
            const auto destination = destinations.find(bgm.position.measure.value);
            if (destination == destinations.end()) {
                diagnostics.push_back(error(diagnostic_code_t::unknown_measure,
                      "quantized BGM event refers to an unknown measure", bgm.id));
                valid = false;
            } else {
                bgm_events[destination->second].push_back(std::move(bgm));
            }
        }
    }
    if (!valid) {
        return false;
    }
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        chart.measures[index].notes = std::move(notes[index]);
        chart.measures[index].bgm_events = std::move(bgm_events[index]);
    }
    return true;
}

inline auto rehome_measure_objects(chart_document_t& chart, diagnostic_list_t& diagnostics) -> bool {
    return rehome_measure_objects(chart, chart.measures, diagnostics);
}

}

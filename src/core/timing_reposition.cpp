#include <wormedit/core/timing_reposition.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/quantize.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/snap.h>
#include "chart_position_storage.h"
#include <algorithm>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <unordered_map>
#include <variant>

namespace wormedit {
namespace {
using reposition_result_t = diagnosed_t<timing_reposition_report_t>;
using timing_link_detail::timing_event_filter_t;
using timing_link_detail::visit_timing_events;

auto is_local_category(const chart_document_t& chart, timing_category_t category) -> bool {
    const auto& link = chart.timing_links.value(category);
    return !link || link->is_local_override;
}

// One stored position, identified across the charts before and after the edit.
struct anchored_key_t {
    bool recipe{};
    std::uint64_t id{};
    bool end{};
    auto operator<=>(const anchored_key_t&) const = default;
    [[nodiscard]] auto object() const -> std::optional<object_id_t> {
        return recipe ? std::nullopt : std::optional{object_id_t{id}};
    }
};

template<class Chart, class Head>
auto visit_anchored_events(Chart& chart, const Head& head) -> bool {
    bool valid = true;
    for (const auto& category : timing_categories) {
        if (category.shapes_audio_time || !is_local_category(chart, category.kind)) continue;
        (void)visit_timing_events(chart, chart, category.kind, [&](auto& events, auto&, timing_event_filter_t filter) {
            for (auto& event : events | std::views::filter(filter))
                if constexpr (requires { event.position; }) valid = valid && head(event.position, event.id);
            return true;
        });
    }
    return valid && visit_chart_point_collections(chart, [&](auto& events, auto role) {
        if constexpr (role != chart_collection_position_t::timing_point)
            for (auto& event : events) valid = valid && head(event.position, event.id);
        return valid;
    });
}
// Everything placed in time except the events that shape the time map itself, lane layout
// segments, and beat-anchored markers, whose anchor already chose musical position.
template<class Chart, class Visit>
auto visit_anchored_positions(Chart& chart, const Visit& visit) -> bool {
    const auto head = [&](auto& position, auto id, bool recipe = false) {
        return visit(position, anchored_key_t{recipe, id.value, false}, measure_chart_end_policy_t::reject);
    };
    const auto tail = [&](auto& position, auto id, bool recipe = false) {
        return visit(position, anchored_key_t{recipe, id.value, true}, measure_chart_end_policy_t::allow);
    };
    const auto held = [&](auto& objects) {
        return std::ranges::all_of(objects, [&](auto& object) {
            return head(object.position, object.id) && (!object.end || tail(*object.end, object.id));
        });
    };
    for (auto& measure : chart.measures) if (!held(measure.notes) || !held(measure.bgm_events)) return false;
    for (auto& channel : chart.sound_channels) if (!held(channel.events)) return false;
    for (auto& recipe : chart.sv_recipes) {
        if (!is_local_category(chart, timing_category_info_t::for_recipe(recipe.output))) continue;
        if (!head(recipe.range.begin, recipe.id, true) || !tail(recipe.range.end, recipe.id, true)) return false;
    }
    return visit_anchored_events(chart, head);
}

struct audio_position_mapper_t {
    const chart_document_t& chart;
    const measure_index_t& measures;
    const timing_index_t& before;
    const timing_index_t& after;
    reposition_result_t& result;

    auto row_at(local_position_t position, std::optional<object_id_t> object,
        timing_boundary_side_t side = timing_boundary_side_t::event) const -> std::optional<std::uint64_t> {
        const auto old_row = measures.row_of(position, measure_chart_end_policy_t::allow);
        const auto old = old_row ? before.seconds_at(*old_row, side) : timing_query_result_t{};
        if (old.inside_warp) {
            result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
                "Objects inside a warp have no audio time to keep.", object));
            return {};
        }
        const auto inverse = old.seconds ? after.row_at(*old.seconds, chart_end_policy_t::extrapolate_tail,
            {.warp_side = timing_warp_inverse_side_t::exit})
                                         : timing_position_query_result_t{};
        if (!inverse.absolute_row || inverse.absolute_row->numerator < 0) {
            result.diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
                "Audio-time anchoring cannot map this position through the edited timing.", object));
            return {};
        }
        const auto exact = *inverse.absolute_row;
        const auto row = exact.numerator % exact.denominator == 0
            ? snap_canvas_row_or_exact(chart, measures, static_cast<std::uint64_t>(exact.numerator) / exact.denominator,
                {}, std::numeric_limits<std::uint64_t>::max())
            : snap_canvas_row(chart, measures, to_long_double(exact),
                {.denominator = chart.divisions_per_standard_measure}, std::numeric_limits<std::uint64_t>::max());
        // The exact inverse extrapolates beyond the stored tail. Interior rows additionally
        // need a forward check because stops/warps can make their audio time unrepresentable.
        const auto mapped = row && *row <= after.total_rows ? after.seconds_at(*row, side)
            : timing_query_result_t{.seconds = row ? old.seconds : std::nullopt};
        if (!mapped.seconds) {
            result.diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
                "Audio-time anchoring exceeds the supported chart extent.", object));
            return {};
        }
        if (exact.numerator % exact.denominator != 0 || *mapped.seconds != *old.seconds)
            ++result.value.rounded_positions;
        return row;
    }
};

// Unchanged BPM, pause and warp events keep their audio time too. Each event's time
// depends only on the events before it, so they are placed in old time order, each
// through the map built from the edited events and those already placed.
using timing_map_event_t = std::variant<bpm_event_t, pause_event_t, warp_event_t>;
struct held_timing_event_t {
    timing_map_event_t event;
    std::uint64_t old_row{};
    std::uint64_t span_rows{};
};
auto events_of(chart_document_t& chart, const bpm_event_t&) -> std::vector<bpm_event_t>& { return chart.bpms; }
auto events_of(chart_document_t& chart, const pause_event_t&) -> std::vector<pause_event_t>& { return chart.pauses; }
auto events_of(chart_document_t& chart, const warp_event_t&) -> std::vector<warp_event_t>& { return chart.warps; }
auto category_of(const bpm_event_t&) -> timing_category_t { return timing_category_t::bpm; }
auto category_of(const pause_event_t& event) -> timing_category_t {
    return event.kind == pause_kind_t::stop ? timing_category_t::stop : timing_category_t::delay;
}
auto category_of(const warp_event_t&) -> timing_category_t { return timing_category_t::warp; }
template<class Event> auto begin_of(const Event& event) -> local_position_t {
    if constexpr (requires { event.span; }) return event.span.begin;
    else return event.position;
}

struct timing_order_t {
    std::unordered_map<std::uint64_t, std::size_t> bpms, pauses, warps;
    template<class Event> static auto from(const std::vector<Event>& events) {
        std::unordered_map<std::uint64_t, std::size_t> order;
        for (std::size_t index = 0; index < events.size(); ++index) order.emplace(events[index].id.value, index);
        return order;
    }
    static auto from(const chart_document_t& chart) -> timing_order_t {
        return {from(chart.bpms), from(chart.pauses), from(chart.warps)};
    }
    // Placement appends; restoring vector order keeps source order and tie-breaks intact.
    void restore(chart_document_t& chart) const {
        const auto sort = [](auto& events, const auto& order) {
            std::ranges::stable_sort(events, {}, [&](const auto& event) { return order.at(event.id.value); });
        };
        sort(chart.bpms, bpms);
        sort(chart.pauses, pauses);
        sort(chart.warps, warps);
    }
};

template<class Event>
void take_unchanged(const std::vector<Event>& before, chart_document_t& after, const measure_index_t& measures,
    std::vector<held_timing_event_t>& held) {
    std::erase_if(events_of(after, Event{}), [&](const Event& event) {
        const auto old = std::ranges::find(before, event.id, &Event::id);
        if (old == before.end() || *old != event || !is_local_category(after, category_of(event))) return false;
        const auto row = measures.row_of(begin_of(event));
        // Row zero keeps its time under any tempo, and the map needs its initial BPM.
        if (!row || *row == 0) return false;
        std::uint64_t span_rows{};
        if constexpr (requires { event.span; }) {
            const auto end = measures.row_of(event.span.end);
            if (!end || *end < *row) return false;
            span_rows = *end - *row;
        }
        held.push_back({event, *row, span_rows});
        return true;
    });
}

auto is_timing_row_occupied(const chart_document_t& chart, const measure_index_t& measures,
    timing_category_t category, std::uint64_t row) -> bool {
    return visit_timing_events(chart, chart, category, [&](const auto& events, const auto&, timing_event_filter_t filter) {
        return std::ranges::any_of(events | std::views::filter(filter),
            [&](const auto& event) { return measures.row_of(begin_of(event)) == row; });
    });
}

auto install_timing_event(chart_document_t& chart, held_timing_event_t& held, std::uint64_t row,
    reposition_result_t& result) -> bool {
    if (!extend_chart_to_row(chart, row + held.span_rows)) return false;
    const auto measures = measure_index_t::from(chart);
    if (!measures) return false;
    return std::visit([&](auto& event) {
        if (is_timing_row_occupied(chart, *measures, category_of(event), row)) {
            result.diagnostics.push_back(error(diagnostic_code_t::collision,
                "Keeping audio time would put two timing events of one kind on the same row.", event.id));
            return false;
        }
        const auto begin = measures->position_at(row);
        if (!begin) return false;
        if constexpr (requires { event.span; }) {
            const auto end = measures->position_at(row + held.span_rows);
            if (!end) return false;
            event.span = {*begin, *end};
        } else event.position = *begin;
        if (row != held.old_row) ++result.value.moved_objects;
        events_of(chart, event).push_back(event);
        return true;
    }, held.event);
}

auto reanchor_timing_events(const chart_document_t& before, chart_document_t& after,
    const measure_index_t& measures, const timing_index_t& old_timing, reposition_result_t& result) -> bool {
    const auto order = timing_order_t::from(after);
    std::vector<held_timing_event_t> held;
    take_unchanged(before.bpms, after, measures, held);
    take_unchanged(before.pauses, after, measures, held);
    take_unchanged(before.warps, after, measures, held);
    std::ranges::stable_sort(held, {}, &held_timing_event_t::old_row);
    for (auto& event : held) {
        const auto timing = timing_index_t::from(after);
        if (!timing.value) return false;
        const audio_position_mapper_t mapper{after, measures, old_timing, *timing.value, result};
        const auto object = std::visit([](const auto& value) { return value.id; }, event.event);
        const auto begin = std::visit([](const auto& value) { return begin_of(value); }, event.event);
        const auto row = mapper.row_at(begin, object, timing_boundary_side_t::before_delays);
        if (!row || !install_timing_event(after, event, *row, result)) return false;
    }
    order.restore(after);
    return true;
}

using position_plan_t = std::map<anchored_key_t, std::uint64_t>;
auto plan_audio_positions(const chart_document_t& chart, const audio_position_mapper_t& mapper,
    position_plan_t& plan, std::uint64_t& extent) -> bool {
    std::set<std::pair<bool, std::uint64_t>> moved;
    const auto planned = visit_anchored_positions(chart,
        [&](const local_position_t& position, anchored_key_t key, measure_chart_end_policy_t policy) {
            const auto row = mapper.row_at(position, key.object());
            if (!row) return false;
            if (!plan.emplace(key, *row).second) {
                mapper.result.diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                    "Audio-time anchoring needs unique object identities.", key.object()));
                return false;
            }
            const bool exclusive_end = policy == measure_chart_end_policy_t::allow && *row != 0;
            extent = std::max(extent, exclusive_end ? *row - 1 : *row);
            if (mapper.measures.row_of(position, policy) != row) moved.emplace(key.recipe, key.id);
            return true;
        });
    mapper.result.value.moved_objects += moved.size();
    return planned;
}
auto install_audio_positions(chart_document_t& chart, const position_plan_t& plan) -> bool {
    const auto measures = measure_index_t::from(chart);
    if (!measures) return false;
    return visit_anchored_positions(chart,
        [&](local_position_t& position, anchored_key_t key, measure_chart_end_policy_t policy) {
            const auto found = plan.find(key);
            if (found == plan.end()) return true;
            const auto mapped = measures->position_at(found->second, policy);
            if (mapped) position = *mapped;
            return mapped.has_value();
        });
}
auto anchor_positions(const chart_document_t& before, chart_document_t& after, const measure_index_t& measures,
    const timing_index_t& old_timing, timing_anchoring_t options, reposition_result_t& result) -> bool {
    const auto new_timing = timing_index_t::from(after);
    if (!new_timing.value) return false;
    position_plan_t plan;
    std::uint64_t extent{};
    const audio_position_mapper_t mapper{after, measures, old_timing, *new_timing.value, result};
    if (!plan_audio_positions(before, mapper, plan, extent)) return false;
    if (result.value.rounded_positions && !options.allow_quantization) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
            std::to_string(result.value.rounded_positions) + " positions need rounding; enable Allow quantization to apply."));
        return false;
    }
    return plan.empty() || (extend_chart_to_row(after, extent) && install_audio_positions(after, plan) &&
        rehome_measure_objects(after, result.diagnostics));
}
}
auto preserve_audio_positions(const chart_document_t& before, chart_document_t& after,
    timing_anchoring_t options) -> diagnosed_t<timing_reposition_report_t> {
    reposition_result_t result;
    if (options.positions == timing_position_policy_t::musical) return result;
    if (before.bms_control_tree && before.bms_control_tree->has_control_flow()) {
        result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
            "Audio-time anchoring cannot rewrite retained BMS control flow."));
        return result;
    }
    const auto old_timing = timing_index_t::from(before);
    const auto measures = measure_index_t::from(before);
    if (!old_timing.value || !measures || !timing_index_t::from(after).value) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_rational,
            "Audio-time anchoring requires valid exact timing before and after the edit."));
        return result;
    }
    if (!reanchor_timing_events(before, after, *measures, *old_timing.value, result) ||
        !anchor_positions(before, after, *measures, *old_timing.value, options, result)) {
        if (result.diagnostics.empty()) result.diagnostics.push_back(error(diagnostic_code_t::row_out_of_range,
            "Audio-time anchoring exceeds the supported chart extent."));
        return result;
    }
    result.diagnostics = validate_repositioned_audio(before, after);
    return result;
}
auto stage_timing_reposition(document_command_transaction_t& transaction, chart_id_t chart,
    timing_anchoring_t options, attempt_t<document_edit_effect_t> edit, timing_reposition_report_t* report)
    -> attempt_t<document_edit_effect_t> {
    if (report) *report = {};
    if (!edit.value || !edit.value->changed || options.positions == timing_position_policy_t::musical) return edit;
    return transaction.stage(chart, "Timing edit refers to an unknown chart.",
        [&](chart_document_t& after, song_document_t&) -> attempt_t<document_edit_effect_t> {
            auto mapped = preserve_audio_positions(*transaction.source_chart(chart), after, options);
            if (report) *report = mapped.value;
            return {.value = std::move(edit.value), .diagnostics = std::move(mapped.diagnostics)};
        });
}

}

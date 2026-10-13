#include "../core/checked_math.h"
#include "interaction_geometry.h"
#include <wormedit/core/quantize.h>

#include "quantize_geometry.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <numeric>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit {
namespace {

constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();

auto error(diagnostic_list_t& diagnostics, diagnostic_code_t code, std::string message,
           std::optional<object_id_t> object = std::nullopt) -> void {
    diagnostics.push_back({
        .severity = diagnostic_severity_t::error,
        .code = code,
        .message = std::move(message),
        .object = object,
    });
}

using checked_math::checked_multiply;

using checked_math::checked_lcm;

auto required_authored_resolution(const chart_document_t& chart, std::uint64_t divisions)
    -> std::optional<std::uint64_t> {
    auto required = chart.divisions_per_standard_measure;
    for (const auto& measure : chart.measures) {
        const auto numerator = measure.definition.length.numerator;
        const auto denominator = measure.definition.length.denominator;
        if (numerator == 0 || denominator == 0) {
            return std::nullopt;
        }

        // Compute (denominator * divisions) / gcd(numerator,
        // denominator * divisions) without forming the potentially overflowing
        // product. After cancelling the denominator, only the divisions term
        // can share another factor with the reduced numerator.
        const auto denominator_common = std::gcd(numerator, denominator);
        const auto reduced_denominator = denominator / denominator_common;
        const auto reduced_numerator = numerator / denominator_common;
        const auto divisions_common = std::gcd(reduced_numerator, divisions);
        const auto reduced_divisions = divisions / divisions_common;
        std::uint64_t factor{};
        if (!checked_multiply(reduced_denominator, reduced_divisions, factor)) {
            return std::nullopt;
        }
        const auto combined = checked_lcm(required, factor);
        if (!combined.has_value()) {
            return std::nullopt;
        }
        required = *combined;
    }
    return required;
}

auto required_resolution(const chart_document_t& chart, const quantize_request_t& request)
    -> std::optional<std::uint64_t> {
    if (request.space == quantization_space_t::standard_measure) {
        return checked_lcm(chart.divisions_per_standard_measure, request.divisions);
    }
    return required_authored_resolution(chart, request.divisions);
}

auto nearest_multiple(std::uint64_t value, std::uint64_t step, quantization_tie_t tie,
                      diagnostic_list_t& diagnostics, object_id_t object)
    -> std::optional<std::uint64_t> {
    const auto remainder = value % step;
    if (remainder == 0) {
        return value;
    }
    const auto distance_to_upper = step - remainder;
    if (remainder < distance_to_upper) {
        return value - remainder;
    }
    if (remainder > distance_to_upper) {
        if (distance_to_upper > maximum - value) {
            error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "quantized row overflows the supported range", object);
            return std::nullopt;
        }
        return value + distance_to_upper;
    }
    if (tie == quantization_tie_t::reject) {
        error(diagnostics, diagnostic_code_t::collision,
              "object lies exactly halfway between quantization rows", object);
        return std::nullopt;
    }
    if (tie == quantization_tie_t::earlier) {
        return value - remainder;
    }
    if (distance_to_upper > maximum - value) {
        error(diagnostics, diagnostic_code_t::invalid_resolution,
              "quantized row overflows the supported range", object);
        return std::nullopt;
    }
    return value + distance_to_upper;
}

struct quantization_position_context_t {
    const detail::quantize_geometry_t& index;
    std::uint64_t resolution;
    const quantize_request_t& request;
    diagnostic_list_t& diagnostics;
};

auto quantized_position(quantization_position_context_t& context, local_position_t position,
                        object_id_t object)
    -> std::optional<local_position_t> {
    const auto& index = context.index;
    const auto& request = context.request;
    auto& diagnostics = context.diagnostics;
    const auto absolute = index.absolute(position);
    if (!absolute.has_value()) {
        error(diagnostics, diagnostic_code_t::row_out_of_range,
              "object position is outside the chart geometry", object);
        return std::nullopt;
    }

    std::uint64_t coordinate{};
    std::uint64_t step{};
    std::uint64_t base{};
    if (request.space == quantization_space_t::standard_measure) {
        coordinate = *absolute;
        step = context.resolution / request.divisions;
    } else {
        const auto* entry = index.entry(position.measure);
        if (entry == nullptr || entry->row_capacity % request.divisions != 0) {
            error(diagnostics, diagnostic_code_t::invalid_resolution,
                  "authored measure cannot be divided by the requested grid", object);
            return std::nullopt;
        }
        coordinate = position.row;
        step = entry->row_capacity / request.divisions;
        base = entry->absolute_begin_row;
    }
    if (step == 0) {
        error(diagnostics, diagnostic_code_t::invalid_resolution,
              "quantization grid is finer than the chart resolution", object);
        return std::nullopt;
    }
    const auto snapped = nearest_multiple(coordinate, step, request.tie, diagnostics, object);
    if (!snapped.has_value() || *snapped > maximum - base) {
        return std::nullopt;
    }
    const auto result = index.position(base + *snapped);
    if (!result.has_value()) {
        error(diagnostics, diagnostic_code_t::row_out_of_range,
              "quantization would move the object beyond the final measure", object);
    }
    return result;
}

auto note_collision_key(const detail::quantize_geometry_t& index, const note_t& note) -> std::string {
    const auto row = index.absolute(note.position).value_or(maximum);
    return std::to_string(row) + ":" + std::to_string(note.lane.value);
}

auto validate_note_intervals_and_collisions(const chart_document_t& before,
                                            const chart_document_t& after,
                                            const detail::quantize_geometry_t& index,
                                            quantization_collision_t collision_policy,
                                            diagnostic_list_t& diagnostics) -> void {
    std::unordered_map<std::uint64_t, std::string> original_keys;
    for (const auto& measure : before.measures) {
        for (const auto& note : measure.notes) {
            original_keys.emplace(note.id.value, note_collision_key(index, note));
        }
    }

    std::unordered_map<std::string, object_id_t> occupied;
    for (const auto& measure : after.measures) {
        for (const auto& note : measure.notes) {
            if (note.end.has_value()) {
                const auto begin = index.absolute(note.position);
                const auto end = index.absolute(*note.end);
                if (!begin.has_value() || !end.has_value() || *end <= *begin) {
                    error(diagnostics, diagnostic_code_t::invalid_hold,
                          "quantization would reverse or collapse a hold", note.id);
                }
            }
            if (collision_policy == quantization_collision_t::allow) {
                continue;
            }
            const auto key = note_collision_key(index, note);
            const auto [entry, inserted] = occupied.emplace(key, note.id);
            if (!inserted && entry->second != note.id &&
                original_keys[entry->second.value] != original_keys[note.id.value]) {
                error(diagnostics, diagnostic_code_t::collision,
                      "quantization would collide notes in one lane", note.id);
            }
        }
    }
}

auto validate_musical_spans(const chart_document_t& chart, const detail::quantize_geometry_t& index,
                            diagnostic_list_t& diagnostics) -> void {
    const auto validate_span = [&](local_position_t begin_position, local_position_t end_position,
                                   object_id_t object, std::string_view description) {
        const auto begin = index.absolute(begin_position);
        const auto end = index.absolute(end_position);
        if (!begin.has_value() || !end.has_value() || *end <= *begin) {
            error(diagnostics, diagnostic_code_t::invalid_hold,
                  std::string("quantization would reverse or collapse ") + std::string(description),
                  object);
        }
    };
    for (const auto& warp : chart.warps) {
        validate_span(warp.span.begin, warp.span.end, warp.id, "a warp");
    }
    for (const auto& section : chart.sections) {
        if (section.beat_span.has_value()) {
            validate_span(section.beat_span->begin, section.beat_span->end, section.id,
                          "a beat-anchored section");
        }
    }
    for (const auto& channel : chart.sound_channels) {
        for (const auto& event : channel.events) {
            if (event.end.has_value()) {
                validate_span(event.position, *event.end, event.id, "a sound-channel hold");
            }
        }
    }
}

struct quantize_row_resolver_t {
    const detail::quantize_geometry_t& index;
    auto operator()(local_position_t position) const -> std::optional<std::uint64_t> { return index.absolute(position); }
};
auto effective_quantize_layout(const chart_document_t& chart, const detail::quantize_geometry_t& index,
                               local_position_t position) -> const lane_layout_t* {
    return effective_layout_by_row(chart, position, quantize_row_resolver_t{index});
}

auto validate_layout_effects(const chart_document_t& chart, const detail::quantize_geometry_t& index,
                             diagnostic_list_t& diagnostics) -> void {
    std::unordered_set<std::uint64_t> layout_rows;
    for (const auto& segment : chart.lane_layouts) {
        const auto row = index.absolute(segment.position);
        if (row.has_value() && !layout_rows.insert(*row).second) {
            error(diagnostics, diagnostic_code_t::collision,
                  "quantization would collide lane-layout boundaries", segment.id);
        }
    }

    const auto is_lane_active = [&](local_position_t position, lane_id_t lane) {
        const auto* layout = effective_quantize_layout(chart, index, position);
        return layout != nullptr &&
               std::ranges::any_of(layout->lanes,
                                   [lane](const auto& candidate) { return candidate.id == lane; });
    };
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (!is_lane_active(note.position, note.lane) ||
                (note.end.has_value() && !is_lane_active(*note.end, note.lane))) {
                error(diagnostics, diagnostic_code_t::unknown_lane,
                      "quantization would move a note outside its active lane layout", note.id);
            }
        }
    }
    for (const auto& channel : chart.sound_channels) {
        for (const auto& event : channel.events) {
            if (event.playable &&
                (!is_lane_active(event.position, event.lane) ||
                 (event.end.has_value() && !is_lane_active(*event.end, event.lane)))) {
                error(diagnostics, diagnostic_code_t::unknown_lane,
                      "quantization would move a sound event outside its active lane layout",
                      event.id);
            }
        }
    }
}

auto rehome_measure_objects(chart_document_t& chart, diagnostic_list_t& diagnostics) -> bool {
    std::unordered_map<std::uint64_t, std::size_t> destinations;
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        destinations.emplace(chart.measures[index].id.value, index);
    }

    std::vector<std::vector<note_t>> notes(chart.measures.size());
    std::vector<std::vector<bgm_event_t>> bgm_events(chart.measures.size());
    bool valid = true;
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) {
            const auto destination = destinations.find(note.position.measure.value);
            if (destination == destinations.end()) {
                error(diagnostics, diagnostic_code_t::unknown_measure,
                      "quantized note refers to an unknown measure", note.id);
                valid = false;
            } else {
                notes[destination->second].push_back(std::move(note));
            }
        }
        for (auto& bgm : measure.bgm_events) {
            const auto destination = destinations.find(bgm.position.measure.value);
            if (destination == destinations.end()) {
                error(diagnostics, diagnostic_code_t::unknown_measure,
                      "quantized BGM event refers to an unknown measure", bgm.id);
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

auto transform_measure_positions(measure_chunk_t& measure, const quantize_request_t& request,
                                 const auto& transform) -> void {
    for (auto& note : measure.notes) {
        transform(note.position, note.id, position_role_t::point, quantize_object_family_t::note);
        if (note.end.has_value() && request.holds == quantization_hold_t::both_endpoints) {
            transform(*note.end, note.id, position_role_t::note_end, quantize_object_family_t::note);
        }
    }
    for (auto& bgm : measure.bgm_events) {
        transform(bgm.position, bgm.id, position_role_t::point, quantize_object_family_t::bgm_event);
        if (bgm.end && request.holds == quantization_hold_t::both_endpoints)
            transform(*bgm.end, bgm.id, position_role_t::note_end, quantize_object_family_t::bgm_event);
    }
}

auto transform_chart_positions(chart_document_t& chart, const quantize_request_t& request,
                               const auto& transform) -> void {
    const auto transform_positioned = [&](auto& values) {
        for (auto& value : values) {
            transform(value.position, value.id, position_role_t::point);
        }
    };
    for (auto& segment : chart.lane_layouts) {
        transform(segment.position, segment.id, position_role_t::point);
    }
    for (auto& measure : chart.measures) {
        transform_measure_positions(measure, request, transform);
    }
    transform_positioned(chart.bpms);
    transform_positioned(chart.pauses);
    transform_positioned(chart.scrolls);
    transform_positioned(chart.speeds);
    transform_positioned(chart.kiai);
    transform_positioned(chart.timing_anchors);
    transform_positioned(chart.bga_events);
    for (auto& warp : chart.warps) {
        transform(warp.span.begin, warp.id, position_role_t::span_begin);
        transform(warp.span.end, warp.id, position_role_t::span_end);
    }
    for (auto& bookmark : chart.bookmarks) {
        if (bookmark.position.has_value()) {
            transform(*bookmark.position, bookmark.id, position_role_t::point);
        }
    }
    for (auto& section : chart.sections) {
        if (section.beat_span.has_value()) {
            transform(section.beat_span->begin, section.id, position_role_t::span_begin);
            transform(section.beat_span->end, section.id, position_role_t::span_end);
        }
    }
    for (auto& channel : chart.sound_channels) {
        for (auto& event : channel.events) {
            transform(event.position, event.id, position_role_t::point);
            if (event.end.has_value() && request.holds == quantization_hold_t::both_endpoints) {
                transform(*event.end, event.id, position_role_t::note_end);
            }
        }
    }
}

auto prepare_quantization_chart(const chart_document_t& chart, const quantize_request_t& request,
                                std::uint64_t target_resolution, quantization_outcome_t& result)
    -> std::optional<chart_document_t> {
    if (target_resolution == chart.divisions_per_standard_measure) {
        return chart;
    }
    if (!request.allow_resolution_promotion) {
        error(result.diagnostics, diagnostic_code_t::invalid_resolution,
              "requested grid requires explicit chart-resolution promotion");
        return std::nullopt;
    }
    auto promotion = redivision_plan_t::from(chart, target_resolution);
    if (!promotion.value.chart) {
        result.diagnostics = std::move(promotion.diagnostics);
        return std::nullopt;
    }
    result.value.promoted_from = chart.divisions_per_standard_measure;
    result.value.promoted_to = target_resolution;
    result.value.resolution_moves = std::move(promotion.value.moves);
    return std::move(*promotion.value.chart);
}

struct quantization_selection_t {
    using key_t = std::pair<quantize_object_family_t, std::uint64_t>;
    std::set<key_t> selected;
    std::set<key_t> matched;
    bool typed{};

    auto has_object(object_id_t object, quantize_object_family_t family) -> bool {
        if (!typed && selected.empty()) return true;
        const key_t key{typed ? family : quantize_object_family_t::other, object.value};
        if (!selected.contains(key)) return false;
        matched.insert(key);
        return true;
    }
};

auto selected_quantization_objects(const quantize_request_t& request,
                                    diagnostic_list_t& diagnostics) -> quantization_selection_t {
    quantization_selection_t result{.typed = request.selection.has_value()};
    const auto append = [&](const auto& ids, quantize_object_family_t family) {
        for (const auto id : ids)
            if (!id || !result.selected.emplace(family, id.value).second)
                error(diagnostics, diagnostic_code_t::invalid_id,
                      "quantization identities must be nonzero and unique within their family", id);
    };
    if (request.selection) {
        append(request.selection->notes, quantize_object_family_t::note);
        append(request.selection->bgm_events, quantize_object_family_t::bgm_event);
        if (result.selected.empty())
            error(diagnostics, diagnostic_code_t::invalid_id, "Select notes or BGM before quantizing.");
    } else {
        append(request.objects, quantize_object_family_t::other);
    }
    return result;
}

auto quantization_branch_sources(const chart_document_t& chart) -> std::unordered_set<std::size_t> {
    std::unordered_set<std::size_t> orders;
    if (!chart.bms_control_tree) return orders;
    const auto visit = [&](const auto& self, const bms_control_node_t& node, bool branched) -> void {
        if (branched) orders.insert(node.source_order);
        const auto boundary = node.kind == bms_control_kind_t::random ||
            node.kind == bms_control_kind_t::switch_block || node.kind == bms_control_kind_t::if_group ||
            node.kind == bms_control_kind_t::if_branch || node.kind == bms_control_kind_t::else_if_branch ||
            node.kind == bms_control_kind_t::else_branch || node.kind == bms_control_kind_t::case_branch ||
            node.kind == bms_control_kind_t::default_branch;
        for (const auto& child : node.children) self(self, child, branched || boundary);
    };
    visit(visit, *chart.bms_control_tree, false);
    return orders;
}

// Keep retained BMSON slice events attached to their semantic note/BGM. Continue/restart,
// channel identity and playback properties are unchanged by quantizing a slice boundary.
auto sync_quantized_source(chart_document_t& chart, const auto& object,
                           const quantization_selection_t& selection,
                           const std::unordered_set<std::size_t>& branch_sources,
                           quantize_object_family_t family, diagnostic_list_t& diagnostics) -> void {
    if (!selection.selected.contains({family, object.id.value})) return;
    if (object.branch || (object.provenance.family == format_family_t::bms &&
        !object.provenance.source_spelling.empty() && branch_sources.contains(object.provenance.source_order)))
        error(diagnostics, diagnostic_code_t::unsupported_feature,
              "Quantizing branch-owned objects is unavailable.", object.id);
    if (!object.sound_channel_event) return;
    for (auto& channel : chart.sound_channels) {
        const auto event = std::ranges::find(channel.events, *object.sound_channel_event,
                                              &sound_channel_event_t::id);
        if (event == channel.events.end()) continue;
        event->position = object.position;
        event->end = object.end;
        return;
    }
    error(diagnostics, diagnostic_code_t::invalid_id, "The selected stem point is missing.", object.id);
}

auto validate_quantized_bgm(const chart_document_t& before, chart_document_t& after,
                            const detail::quantize_geometry_t& index,
                            quantization_collision_t policy, diagnostic_list_t& diagnostics) -> void {
    std::unordered_map<std::uint64_t, std::pair<object_id_t, local_position_t>> original;
    for (const auto& measure : before.measures)
        for (const auto& event : measure.bgm_events)
            original.emplace(event.id.value, std::pair{event.source_lane, event.position});
    std::map<std::pair<std::uint64_t, std::uint64_t>, object_id_t> occupied;
    for (const auto& measure : after.measures) {
        for (const auto& event : measure.bgm_events) {
            if (event.end && index.absolute(*event.end).value_or(0) <= index.absolute(event.position).value_or(maximum))
                error(diagnostics, diagnostic_code_t::invalid_hold,
                      "quantization would reverse or collapse a BGM hold", event.id);
            const auto key = std::pair{event.source_lane.value, index.absolute(event.position).value_or(maximum)};
            const auto [found, inserted] = occupied.emplace(key, event.id);
            if (!inserted && policy != quantization_collision_t::allow &&
                original[found->second.value] != original[event.id.value])
                error(diagnostics, diagnostic_code_t::collision,
                      "quantization would collide BGM events in one source lane", event.id);
        }
    }
}

auto validate_quantized_stems(const chart_document_t& before, const chart_document_t& after,
                               diagnostic_list_t& diagnostics) -> void {
    for (std::size_t i = 0; i < after.sound_channels.size(); ++i) {
        const auto& channel = after.sound_channels[i];
        const auto& previous = before.sound_channels[i];
        std::map<std::tuple<std::uint64_t, std::uint64_t, bool>, object_id_t> occupied;
        for (const auto& event : channel.events) {
            const auto [found, inserted] = occupied.emplace(std::tuple{event.position.measure.value, event.position.row, event.playable}, event.id);
            if (inserted) continue;
            const auto old = std::ranges::find(previous.events, event.id, &sound_channel_event_t::id);
            const auto peer = std::ranges::find(previous.events, found->second, &sound_channel_event_t::id);
            if (old != previous.events.end() && peer != previous.events.end() && old->position != peer->position)
                error(diagnostics, diagnostic_code_t::collision,
                      "another point of this stem is already on that row", event.id);
        }
    }
}

auto synchronize_quantized_selection(chart_document_t& chart,
                                     const quantization_selection_t& selection,
                                     diagnostic_list_t& diagnostics) -> void {
    if (!selection.typed) return;
    const auto branch_sources = quantization_branch_sources(chart);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes)
            sync_quantized_source(chart, note, selection, branch_sources, quantize_object_family_t::note, diagnostics);
        for (const auto& event : measure.bgm_events)
            sync_quantized_source(chart, event, selection, branch_sources, quantize_object_family_t::bgm_event, diagnostics);
    }
}

} // namespace

auto quantization_plan_t::from(const chart_document_t& chart, const quantize_request_t& request)
    -> quantization_outcome_t {
    quantization_outcome_t result;
    if (request.divisions == 0 || chart.divisions_per_standard_measure == 0) {
        error(result.diagnostics, diagnostic_code_t::invalid_resolution,
              "quantization divisions and chart resolution must be positive");
        return result;
    }

    auto selected = selected_quantization_objects(request, result.diagnostics);
    if (!result.diagnostics.empty()) {
        return result;
    }

    const auto target_resolution = required_resolution(chart, request);
    if (!target_resolution.has_value()) {
        error(result.diagnostics, diagnostic_code_t::invalid_resolution,
              "quantization resolution promotion overflows the supported range");
        return result;
    }

    auto prepared = prepare_quantization_chart(chart, request, *target_resolution, result);
    if (!prepared.has_value()) {
        return result;
    }
    auto working = std::move(*prepared);

    const auto built_index = build_measure_index(working);
    if (std::holds_alternative<diagnostic_list_t>(built_index)) {
        result.diagnostics = std::get<diagnostic_list_t>(built_index);
        return result;
    }
    const detail::quantize_geometry_t index{std::get<measure_index_t>(built_index)};
    const auto before_quantization = working;
    quantization_position_context_t position_context{
        index, working.divisions_per_standard_measure, request, result.diagnostics};
    bool failed = false;
    const auto transform = [&](local_position_t& position, object_id_t object,
                               position_role_t role, quantize_object_family_t family = quantize_object_family_t::other) {
        if (!selected.has_object(object, family)) {
            return;
        }
        const auto before = position;
        const auto after = quantized_position(position_context, position, object);
        if (!after.has_value()) {
            failed = true;
            return;
        }
        position = *after;
        if (position != before) {
            result.value.quantized_moves.push_back({
                .object = object,
                .role = role,
                .before = before,
                .after = position,
                .family = family,
            });
        }
    };
    transform_chart_positions(working, request, transform);
    if (!rehome_measure_objects(working, result.diagnostics)) {
        failed = true;
    }

    for (const auto& requested : selected.selected) {
        if (!selected.matched.contains(requested)) {
            error(result.diagnostics, diagnostic_code_t::invalid_id,
                  "quantization request refers to a missing or non-positional object",
                  object_id_t{requested.second});
            failed = true;
        }
    }
    validate_note_intervals_and_collisions(before_quantization, working, index, request.collisions,
                                           result.diagnostics);
    validate_quantized_bgm(before_quantization, working, index, request.collisions, result.diagnostics);
    synchronize_quantized_selection(working, selected, result.diagnostics);
    if (selected.typed) validate_quantized_stems(before_quantization, working, result.diagnostics);
    validate_musical_spans(working, index, result.diagnostics);
    validate_layout_effects(working, index, result.diagnostics);
    if (failed || !result.diagnostics.empty()) {
        return result;
    }
    result.value.chart = std::move(working);
    return result;
}

auto quantization_result_t::from(quantization_outcome_t&& outcome) -> quantization_result_t {
    return {.chart = std::move(outcome.value.chart),
            .resolution_moves = std::move(outcome.value.resolution_moves),
            .quantized_moves = std::move(outcome.value.quantized_moves),
            .diagnostics = std::move(outcome.diagnostics),
            .promoted_from = outcome.value.promoted_from,
            .promoted_to = outcome.value.promoted_to};
}

auto quantize_chart_exact(const chart_document_t& chart, const quantize_request_t& request)
    -> quantization_result_t {
    return quantization_result_t::from(quantization_plan_t::from(chart, request));
}

} // namespace wormedit

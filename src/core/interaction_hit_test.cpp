#include <wormedit/core/interaction.h>

#include <wormedit/core/rational.h>
#include <wormedit/core/virtual_canvas.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <unordered_map>
#include <utility>

namespace wormedit {
namespace {

auto error(diagnostic_code_t code, std::string message,
           std::optional<object_id_t> object = std::nullopt) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error,
            .code = code,
            .message = std::move(message),
            .object = object};
}

auto rational_label(signed_rational_t value) -> std::string {
    return std::to_string(value.numerator) +
           (value.denominator == 1 ? std::string{} : "/" + std::to_string(value.denominator));
}

auto position_label(local_position_t position) -> std::string {
    return std::to_string(position.measure.value) + ":" + std::to_string(position.row);
}

auto note_kind_label(note_kind_t kind) -> std::string_view {
    switch (kind) {
    case note_kind_t::tap: return "Tap";
    case note_kind_t::hold: return "Hold";
    case note_kind_t::roll: return "Roll";
    case note_kind_t::invisible: return "Invisible note";
    case note_kind_t::mine: return "Mine";
    case note_kind_t::lift: return "Lift";
    case note_kind_t::fake: return "Fake note";
    }
    return "Note";
}

auto position_at(const measure_index_t& index, std::uint64_t absolute)
    -> std::optional<local_position_t> {
    if (absolute >= index.total_rows)
        return std::nullopt;
    const auto after = std::ranges::upper_bound(index.entries, absolute, {},
                                                &measure_index_entry_t::absolute_begin_row);
    if (after == index.entries.begin())
        return std::nullopt;
    const auto entry = std::prev(after);
    return local_position_t{.measure = entry->id, .row = absolute - entry->absolute_begin_row};
}

auto effective_layout(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t position) -> const lane_layout_t* {
    const auto event_row = absolute_row(index, position);
    if (!event_row)
        return nullptr;
    const lane_layout_segment_t* selected{};
    std::uint64_t selected_row{};
    for (const auto& segment : chart.lane_layouts) {
        const auto segment_row = absolute_row(index, segment.position);
        if (segment_row && *segment_row <= *event_row &&
            (selected == nullptr || *segment_row >= selected_row)) {
            selected = &segment;
            selected_row = *segment_row;
        }
    }
    return selected == nullptr ? nullptr : &selected->layout;
}

auto editable_lane(const lane_definition_t& lane) -> bool {
    return lane.role != lane_role_t::autoplay && lane.role != lane_role_t::invisible &&
           lane.role != lane_role_t::mine;
}

auto snapped_position(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t target, std::uint64_t denominator)
    -> std::optional<local_position_t> {
    const auto absolute = absolute_row(index, target);
    if (!absolute || denominator == 0 || chart.divisions_per_standard_measure % denominator != 0)
        return std::nullopt;
    const auto step = chart.divisions_per_standard_measure / denominator;
    if (step == 0)
        return std::nullopt;
    const auto remainder = *absolute % step;
    auto snapped = *absolute - remainder;
    const auto distance_up = step - remainder;
    if (remainder > distance_up && distance_up <= std::numeric_limits<std::uint64_t>::max() - *absolute)
        snapped = *absolute + distance_up;
    return position_at(index, snapped);
}

auto absolute_row_at_y_pixels(const chart_document_t& chart, const measure_index_t& index,
                              const editor_interaction_view_model_t& view, double y_pixels,
                              bool include_virtual_bms_tail = false)
    -> std::optional<std::uint64_t> {
    auto total_rows = index.total_rows;
    if (include_virtual_bms_tail && chart.source_family == format_family_t::bms) {
        const auto built = build_virtual_canvas(chart);
        const auto* canvas = std::get_if<virtual_canvas_t>(&built);
        if (canvas == nullptr)
            return std::nullopt;
        total_rows = canvas->total_rows;
    }
    if (view.chart != chart.id || !std::isfinite(y_pixels) || y_pixels < 0.0 ||
        y_pixels >= view.viewport_height_pixels ||
        !std::isfinite(view.pixels_per_standard_measure) ||
        view.pixels_per_standard_measure < minimum_pixels_per_standard_measure ||
        view.pixels_per_standard_measure > maximum_pixels_per_standard_measure ||
        !std::isfinite(view.viewport_scroll_pixels) || chart.divisions_per_standard_measure == 0 ||
        total_rows == 0)
        return std::nullopt;
    const auto row = (static_cast<long double>(y_pixels) + view.viewport_scroll_pixels) *
                     static_cast<long double>(chart.divisions_per_standard_measure) /
                     static_cast<long double>(view.pixels_per_standard_measure);
    if (!std::isfinite(row) || row < 0.0L || row >= static_cast<long double>(total_rows))
        return std::nullopt;
    const auto nearest = std::floor(row + 0.5L);
    if (nearest >= static_cast<long double>(total_rows))
        return total_rows - 1;
    return static_cast<std::uint64_t>(nearest);
}

auto position_at_row_y_pixels(const chart_document_t& chart, const measure_index_t& index,
                              const editor_interaction_view_model_t& view, double y_pixels)
    -> std::optional<local_position_t> {
    const auto row = absolute_row_at_y_pixels(chart, index, view, y_pixels);
    return row ? position_at(index, *row) : std::nullopt;
}

struct hit_test_index_builder_t {
    const chart_document_t& chart;
    editor_hit_test_index_t index;
    std::unordered_map<std::uint64_t, std::size_t> bucket_by_measure;

    auto add_selection_span(editor_selection_object_kind_t kind, object_id_t id,
                            local_position_t begin, local_position_t end,
                            std::optional<lane_id_t> lane = std::nullopt)
        -> std::optional<diagnostic_t> {
        const auto begin_row = absolute_row(index.measures, begin);
        const auto end_row = absolute_row(index.measures, end);
        if (!begin_row || !end_row || *begin_row > *end_row)
            return error(diagnostic_code_t::row_out_of_range,
                         "selection object position lies outside chart geometry", id);
        const editor_selection_object_t object{kind, id};
        index.selection_spans.push_back({object, *begin_row, *end_row, lane});
        index.available_objects.push_back(object);
        return std::nullopt;
    }

    auto add_hover_object(editor_selection_object_kind_t kind, object_id_t id,
                          local_position_t position, editor_inline_editor_kind_t editor,
                          std::string tooltip,
                          std::optional<local_position_t> end_position = std::nullopt)
        -> std::optional<diagnostic_t> {
        const auto row = absolute_row(index.measures, position);
        const auto end_row = absolute_row(index.measures, end_position.value_or(position));
        if (!row || !end_row || *row > *end_row)
            return error(diagnostic_code_t::row_out_of_range,
                         "hover object position lies outside chart geometry", id);
        index.hover_objects_by_id.push_back({.object = {kind, id}, .editor = editor,
                                             .position = position, .absolute_row = *row,
                                             .end_absolute_row = *end_row,
                                             .tooltip = std::move(tooltip)});
        return std::nullopt;
    }

    auto add_note_entry(const note_t& note, local_position_t position, bool is_tail)
        -> std::optional<diagnostic_t> {
        const auto row = absolute_row(index.measures, position);
        const auto bucket = bucket_by_measure.find(position.measure.value);
        if (!row || bucket == bucket_by_measure.end())
            return error(diagnostic_code_t::row_out_of_range,
                         "note hit-test position lies outside chart geometry", note.id);
        index.measure_buckets[bucket->second].push_back({.note = note.id, .lane = note.lane,
            .kind = note.kind, .position = position, .absolute_row = *row, .is_tail = is_tail});
        return std::nullopt;
    }

    auto add_note(const note_t& note) -> std::optional<diagnostic_t> {
        if (auto invalid = add_note_entry(note, note.position, false)) return invalid;
        const auto end_position = note.end.value_or(note.position);
        if (note.end) {
            if (auto invalid = add_note_entry(note, *note.end, true)) return invalid;
        }
        const auto begin_row = absolute_row(index.measures, note.position);
        const auto end_row = absolute_row(index.measures, end_position);
        if (!begin_row || !end_row)
            return error(diagnostic_code_t::row_out_of_range,
                         "note span lies outside chart geometry", note.id);
        index.note_spans.push_back({.note = note.id, .lane = note.lane, .kind = note.kind,
                                    .begin_row = *begin_row, .end_row = *end_row});
        if (auto invalid = add_selection_span(editor_selection_object_kind_t::note, note.id,
                                              note.position, end_position, note.lane)) return invalid;
        auto tooltip = std::string{note_kind_label(note.kind)} + " · lane " +
                       std::to_string(note.lane.value) + " · row " + position_label(note.position);
        if (note.end) tooltip += "–" + position_label(*note.end);
        if (note.label && !note.label->empty()) tooltip += " · " + *note.label;
        if (note.sound) tooltip += " · sample " + std::to_string(note.sound->value);
        return add_hover_object(editor_selection_object_kind_t::note, note.id, note.position,
                                editor_inline_editor_kind_t::note_properties, std::move(tooltip),
                                end_position);
    }

    auto add_bgm_event(const bgm_event_t& event) -> std::optional<diagnostic_t> {
        if (auto invalid = add_selection_span(editor_selection_object_kind_t::bgm_event,
                                              event.id, event.position, event.position)) return invalid;
        auto tooltip = std::string{"BGM event · row "} + position_label(event.position);
        if (event.sound) tooltip += " · sample " + std::to_string(event.sound->value);
        return add_hover_object(editor_selection_object_kind_t::bgm_event, event.id,
                                event.position, editor_inline_editor_kind_t::bgm_event_properties,
                                std::move(tooltip));
    }

    template <typename Events, typename PositionOf, typename TooltipOf>
    auto add_points(const Events& events, editor_selection_object_kind_t kind,
                    editor_inline_editor_kind_t editor, PositionOf position_of,
                    TooltipOf tooltip_of) -> std::optional<diagnostic_t> {
        for (const auto& event : events) {
            const auto position = position_of(event);
            if (auto invalid = add_selection_span(kind, event.id, position, position)) return invalid;
            if (auto invalid = add_hover_object(kind, event.id, position, editor, tooltip_of(event)))
                return invalid;
        }
        return std::nullopt;
    }

    template <typename Events, typename TooltipOf>
    auto add_timing(const Events& events, editor_selection_object_kind_t kind,
                    editor_inline_editor_kind_t editor, TooltipOf tooltip_of)
        -> std::optional<diagnostic_t> {
        return add_points(events, kind, editor,
                          [](const auto& event) { return event.position; }, tooltip_of);
    }

    auto add_measure_objects() -> std::optional<diagnostic_t> {
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes)
                if (auto invalid = add_note(note)) return invalid;
            for (const auto& event : measure.bgm_events)
                if (auto invalid = add_bgm_event(event)) return invalid;
        }
        return std::nullopt;
    }

    auto add_pause(const pause_event_t& event) -> std::optional<diagnostic_t> {
        const auto kind = event.kind == pause_kind_t::stop
                              ? editor_selection_object_kind_t::stop_event
                              : editor_selection_object_kind_t::delay_event;
        const auto editor = event.kind == pause_kind_t::stop
                                ? editor_inline_editor_kind_t::stop_event
                                : editor_inline_editor_kind_t::delay_event;
        const auto name = event.kind == pause_kind_t::stop ? "Stop " : "Delay ";
        const auto unit = event.source_unit == pause_unit_t::seconds ? " seconds" :
                          event.source_unit == pause_unit_t::milliseconds ? " ms" :
                          event.source_unit == pause_unit_t::bms_stop_ticks ? " BMS ticks" :
                          event.source_unit == pause_unit_t::beats ? " beats" : "";
        if (auto invalid = add_selection_span(kind, event.id, event.position, event.position))
            return invalid;
        return add_hover_object(kind, event.id, event.position, editor,
                                std::string{name} + rational_label(event.duration) + unit +
                                    " · row " + position_label(event.position));
    }

    auto add_warp(const warp_event_t& event) -> std::optional<diagnostic_t> {
        if (auto invalid = add_selection_span(editor_selection_object_kind_t::warp_event,
                                              event.id, event.span.begin, event.span.end)) return invalid;
        return add_hover_object(editor_selection_object_kind_t::warp_event, event.id,
            event.span.begin, editor_inline_editor_kind_t::warp_event,
            std::string{"Warp · "} + position_label(event.span.begin) + "–" +
                position_label(event.span.end), event.span.end);
    }

    auto add_timing_objects() -> std::optional<diagnostic_t> {
        if (auto invalid = add_timing(chart.bpms, editor_selection_object_kind_t::bpm_event,
                editor_inline_editor_kind_t::bpm_event, [](const auto& event) {
                    return std::string{"BPM "} + rational_label(event.beats_per_minute) +
                           " · row " + position_label(event.position);
                })) return invalid;
        if (auto invalid = add_timing(chart.scrolls, editor_selection_object_kind_t::scroll_event,
                editor_inline_editor_kind_t::scroll_event, [](const auto& event) {
                    return std::string{"Scroll ×"} + rational_label(event.multiplier) +
                           " · row " + position_label(event.position);
                })) return invalid;
        if (auto invalid = add_timing(chart.speeds, editor_selection_object_kind_t::speed_event,
                editor_inline_editor_kind_t::speed_event, [](const auto& event) {
                    auto tooltip = std::string{"Speed ×"} + rational_label(event.multiplier);
                    if (event.transition_beats.numerator != 0)
                        tooltip += " · transition " + std::to_string(event.transition_beats.numerator) +
                                   "/" + std::to_string(event.transition_beats.denominator) +
                                   (event.transition_is_seconds ? " seconds" : " beats");
                    return tooltip + " · row " + position_label(event.position);
                })) return invalid;
        if (auto invalid = add_timing(chart.kiai, editor_selection_object_kind_t::kiai_event,
                editor_inline_editor_kind_t::kiai_event, [](const auto& event) {
                    return std::string{"Kiai "} + (event.enabled ? "on" : "off") +
                           " · row " + position_label(event.position);
                })) return invalid;
        if (auto invalid = add_points(chart.timing_anchors,
                editor_selection_object_kind_t::timing_anchor,
                editor_inline_editor_kind_t::timing_anchor,
                [](const auto& event) {
                    return local_position_t{event.position.measure, event.position.row};
                }, [](const auto& event) {
                    return std::string{"Timing anchor · frame "} +
                           std::to_string(event.source_sample_frame) +
                           (event.locked ? " · locked" : "");
                })) return invalid;
        return add_timing(chart.bga_events, editor_selection_object_kind_t::bga_event,
                editor_inline_editor_kind_t::bga_event, [](const auto& event) {
                    return std::string{"BGA resource "} + std::to_string(event.resource.value) +
                           " · row " + position_label(event.position);
                });
    }

    auto add_pauses_and_warps() -> std::optional<diagnostic_t> {
        for (const auto& event : chart.pauses)
            if (auto invalid = add_pause(event)) return invalid;
        for (const auto& event : chart.warps)
            if (auto invalid = add_warp(event)) return invalid;
        return std::nullopt;
    }

    auto add_markers() -> std::optional<diagnostic_t> {
        for (const auto& bookmark : chart.bookmarks) {
            if (bookmark.anchor != marker_anchor_t::beat || !bookmark.position) continue;
            if (auto invalid = add_selection_span(editor_selection_object_kind_t::bookmark,
                    bookmark.id, *bookmark.position, *bookmark.position)) return invalid;
            if (auto invalid = add_hover_object(editor_selection_object_kind_t::bookmark,
                    bookmark.id, *bookmark.position, editor_inline_editor_kind_t::bookmark,
                    std::string{"Bookmark"} +
                        (bookmark.label.empty() ? std::string{} : " · " + bookmark.label))) return invalid;
        }
        for (const auto& section : chart.sections) {
            if (section.anchor != marker_anchor_t::beat || !section.beat_span) continue;
            if (auto invalid = add_selection_span(editor_selection_object_kind_t::section,
                    section.id, section.beat_span->begin, section.beat_span->end)) return invalid;
            if (auto invalid = add_hover_object(editor_selection_object_kind_t::section,
                    section.id, section.beat_span->begin, editor_inline_editor_kind_t::section,
                    std::string{"Section"} +
                        (section.label.empty() ? std::string{} : " · " + section.label),
                    section.beat_span->end)) return invalid;
        }
        return std::nullopt;
    }

    auto finalize_note_index() -> std::optional<diagnostic_t> {
        for (auto& bucket : index.measure_buckets)
            std::ranges::sort(bucket, [](const auto& a, const auto& b) {
                if (a.absolute_row != b.absolute_row) return a.absolute_row < b.absolute_row;
                if (a.is_tail != b.is_tail) return a.is_tail < b.is_tail;
                return a.note.value < b.note.value;
            });
        std::ranges::sort(index.note_spans, [](const auto& a, const auto& b) {
            if (a.begin_row != b.begin_row) return a.begin_row < b.begin_row;
            if (a.end_row != b.end_row) return a.end_row < b.end_row;
            return a.note.value < b.note.value;
        });
        auto base = std::size_t{1};
        while (base < index.note_spans.size()) {
            if (base > std::numeric_limits<std::size_t>::max() / 2)
                return error(diagnostic_code_t::invalid_resolution,
                             "note span index exceeds addressable size");
            base *= 2;
        }
        if (base > std::numeric_limits<std::size_t>::max() / 2)
            return error(diagnostic_code_t::invalid_resolution,
                         "note span index exceeds addressable size");
        index.note_span_tree_leaf_base = base;
        index.note_span_tree_max_end.assign(base * 2, 0);
        for (std::size_t span = 0; span < index.note_spans.size(); ++span)
            index.note_span_tree_max_end[base + span] = index.note_spans[span].end_row;
        for (auto node = base; node-- > 1;)
            index.note_span_tree_max_end[node] = std::max(index.note_span_tree_max_end[node * 2],
                                                          index.note_span_tree_max_end[node * 2 + 1]);
        return std::nullopt;
    }

    auto finalize_selection_index() -> std::optional<diagnostic_t> {
        std::ranges::sort(index.selection_spans, [](const auto& a, const auto& b) {
            if (a.begin_row != b.begin_row) return a.begin_row < b.begin_row;
            if (a.end_row != b.end_row) return a.end_row < b.end_row;
            return a.object < b.object;
        });
        std::ranges::sort(index.available_objects);
        const auto duplicate = std::ranges::adjacent_find(index.available_objects);
        if (duplicate != index.available_objects.end())
            return error(diagnostic_code_t::duplicate_id,
                         "selection index contains a duplicate object ID", duplicate->id);
        index.available_objects.erase(std::unique(index.available_objects.begin(),
                                                   index.available_objects.end()),
                                      index.available_objects.end());
        std::ranges::sort(index.hover_objects_by_id, {}, &editor_hover_index_entry_t::object);
        auto base = std::size_t{1};
        while (base < index.selection_spans.size()) {
            if (base > std::numeric_limits<std::size_t>::max() / 2)
                return error(diagnostic_code_t::invalid_resolution,
                             "selection span index exceeds addressable size");
            base *= 2;
        }
        if (base > std::numeric_limits<std::size_t>::max() / 2)
            return error(diagnostic_code_t::invalid_resolution,
                         "selection span index exceeds addressable size");
        index.selection_span_tree_leaf_base = base;
        index.selection_span_tree_max_end.assign(base * 2, 0);
        for (std::size_t span = 0; span < index.selection_spans.size(); ++span)
            index.selection_span_tree_max_end[base + span] = index.selection_spans[span].end_row;
        for (auto node = base; node-- > 1;)
            index.selection_span_tree_max_end[node] =
                std::max(index.selection_span_tree_max_end[node * 2],
                         index.selection_span_tree_max_end[node * 2 + 1]);
        return std::nullopt;
    }
};

} // namespace

auto editor_position_y_pixels(const chart_document_t& chart,
                              const editor_interaction_view_model_t& view,
                              local_position_t position) -> std::optional<double> {
    if (view.chart != chart.id || !std::isfinite(view.pixels_per_standard_measure) ||
        view.pixels_per_standard_measure < minimum_pixels_per_standard_measure ||
        view.pixels_per_standard_measure > maximum_pixels_per_standard_measure ||
        !std::isfinite(view.viewport_scroll_pixels)) {
        return std::nullopt;
    }
    const auto built_index = build_measure_index(chart);
    if (std::holds_alternative<diagnostic_list_t>(built_index)) {
        return std::nullopt;
    }
    const auto& index = std::get<measure_index_t>(built_index);
    const auto absolute = absolute_row(index, position);
    if (!absolute.has_value()) {
        return std::nullopt;
    }
    const auto y = static_cast<double>(*absolute) * view.pixels_per_standard_measure /
                       static_cast<double>(chart.divisions_per_standard_measure) -
                   view.viewport_scroll_pixels;
    if (!std::isfinite(y)) {
        return std::nullopt;
    }
    return y;
}

auto editor_position_at_y_pixels(const chart_document_t& chart,
                                 const editor_interaction_view_model_t& view, double y_pixels)
    -> std::optional<local_position_t> {
    const auto built_index = build_measure_index(chart);
    if (std::holds_alternative<diagnostic_list_t>(built_index)) {
        return std::nullopt;
    }
    return position_at_row_y_pixels(chart, std::get<measure_index_t>(built_index), view, y_pixels);
}

auto build_editor_hit_test_index(const chart_document_t& chart, std::uint64_t document_revision)
    -> std::variant<editor_hit_test_index_t, diagnostic_list_t> {
    const auto built_index = build_measure_index(chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built_index))
        return *diagnostics;
    hit_test_index_builder_t builder{
        .chart = chart,
        .index = {.chart = chart.id, .document_revision = document_revision,
                  .measures = std::get<measure_index_t>(built_index)},
    };
    builder.index.measure_buckets.resize(builder.index.measures.entries.size());
    builder.bucket_by_measure.reserve(builder.index.measures.entries.size());
    for (std::size_t bucket = 0; bucket < builder.index.measures.entries.size(); ++bucket)
        builder.bucket_by_measure.emplace(builder.index.measures.entries[bucket].id.value, bucket);

    const auto fail_if = [](const auto& result) -> std::optional<diagnostic_list_t> {
        if (result) return diagnostic_list_t{*result};
        return std::nullopt;
    };
    if (auto failure = fail_if(builder.add_measure_objects())) return *failure;
    if (auto failure = fail_if(builder.add_timing_objects())) return *failure;
    if (auto failure = fail_if(builder.add_pauses_and_warps())) return *failure;
    if (auto failure = fail_if(builder.add_markers())) return *failure;
    if (auto failure = fail_if(builder.finalize_note_index())) return *failure;
    if (auto failure = fail_if(builder.finalize_selection_index())) return *failure;
    return std::move(builder.index);
}

namespace {

struct nearby_note_candidate_t {
    editor_hit_target_t target;
    int priority{};
    double distance{std::numeric_limits<double>::infinity()};
};

auto valid_hit_request(const chart_document_t& chart, const editor_hit_test_index_t& index,
                       const editor_interaction_view_model_t& view,
                       const editor_hit_test_request_t& request) -> bool {
    return index.chart == chart.id && view.chart == chart.id &&
           index.document_revision == view.document_revision &&
           index.measure_buckets.size() == index.measures.entries.size() &&
           std::isfinite(request.x_pixels) && std::isfinite(request.y_pixels) &&
           std::isfinite(request.field_left_pixels) && std::isfinite(request.field_width_pixels) &&
           request.field_width_pixels > 0.0 && std::isfinite(request.note_hit_radius_pixels) &&
           request.note_hit_radius_pixels >= 0.0 && std::isfinite(view.viewport_height_pixels) &&
           view.viewport_height_pixels > 0.0 && chart.divisions_per_standard_measure != 0;
}

auto hit_test_lanes(const lane_layout_t& layout) -> std::vector<const lane_definition_t*> {
    std::vector<const lane_definition_t*> lanes;
    lanes.reserve(layout.lanes.size());
    for (const auto& lane : layout.lanes)
        if (editable_lane(lane)) lanes.push_back(&lane);
    std::ranges::sort(lanes, [](const auto* left, const auto* right) {
        if (left->display_order != right->display_order)
            return left->display_order < right->display_order;
        return left->id.value < right->id.value;
    });
    return lanes;
}

auto lane_at_pointer(const chart_document_t& chart, const editor_hit_test_index_t& index,
                     const editor_interaction_view_model_t& view,
                     const editor_hit_test_request_t& request, local_position_t position)
    -> std::optional<lane_id_t> {
    const auto* layout = effective_layout(chart, index.measures, position);
    if (!layout) return std::nullopt;
    const auto lanes = hit_test_lanes(*layout);
    if (lanes.empty()) return std::nullopt;
    const auto lane_width = request.field_width_pixels / static_cast<double>(lanes.size());
    const auto offset = (request.x_pixels - request.field_left_pixels) / lane_width;
    if (!std::isfinite(offset) || offset < 0.0 || offset >= static_cast<double>(lanes.size()))
        return std::nullopt;
    (void)chart;
    (void)view;
    return lanes[static_cast<std::size_t>(std::floor(offset))]->id;
}

auto hit_priority(editor_hit_target_kind_t kind) -> int {
    return kind == editor_hit_target_kind_t::hold_head ? 3 :
           kind == editor_hit_target_kind_t::hold_tail ? 2 : 1;
}

auto note_target_kind(const editor_hit_test_entry_t& entry) -> editor_hit_target_kind_t {
    if (entry.is_tail) return editor_hit_target_kind_t::hold_tail;
    if (entry.kind == note_kind_t::hold || entry.kind == note_kind_t::roll)
        return editor_hit_target_kind_t::hold_head;
    return editor_hit_target_kind_t::note;
}

auto improves_candidate(const nearby_note_candidate_t& current, editor_hit_target_kind_t kind,
                        double distance, object_id_t note) -> bool {
    const auto priority = hit_priority(kind);
    if (priority != current.priority) return priority > current.priority;
    if (distance != current.distance) return distance < current.distance;
    return !current.target.note || note.value < current.target.note->value;
}

auto nearby_note_points(const chart_document_t& chart, const editor_hit_test_index_t& index,
                       const editor_interaction_view_model_t& view,
                       const editor_hit_test_request_t& request, std::uint64_t pointer_row,
                       lane_id_t lane, local_position_t lane_position)
    -> nearby_note_candidate_t {
    nearby_note_candidate_t best{.target = {.kind = editor_hit_target_kind_t::lane,
                                            .lane = lane, .position = lane_position}};
    const auto radius_rows = static_cast<long double>(request.note_hit_radius_pixels) *
        static_cast<long double>(chart.divisions_per_standard_measure) /
        static_cast<long double>(view.pixels_per_standard_measure);
    const auto row = static_cast<long double>(pointer_row);
    const auto minimum_row = row - radius_rows <= 0.0L ? std::uint64_t{0} :
        static_cast<std::uint64_t>(std::floor(row - radius_rows));
    const auto max_chart_row = index.measures.total_rows - 1;
    const auto maximum_row = row + radius_rows >= static_cast<long double>(max_chart_row)
        ? max_chart_row : static_cast<std::uint64_t>(std::ceil(row + radius_rows));
    const auto after_min = std::ranges::upper_bound(index.measures.entries, minimum_row, {},
        &measure_index_entry_t::absolute_begin_row);
    const auto first_bucket = after_min == index.measures.entries.begin()
        ? after_min : std::prev(after_min);
    for (auto measure = first_bucket; measure != index.measures.entries.end() &&
         measure->absolute_begin_row <= maximum_row; ++measure) {
        const auto number = static_cast<std::size_t>(
            std::distance(index.measures.entries.begin(), measure));
        const auto& bucket = index.measure_buckets[number];
        const auto first = std::ranges::lower_bound(bucket, minimum_row, {},
            &editor_hit_test_entry_t::absolute_row);
        for (auto entry = first; entry != bucket.end() && entry->absolute_row <= maximum_row; ++entry) {
            if (entry->lane != lane) continue;
            const auto y = static_cast<double>(static_cast<long double>(entry->absolute_row) *
                static_cast<long double>(view.pixels_per_standard_measure) /
                static_cast<long double>(chart.divisions_per_standard_measure) -
                static_cast<long double>(view.viewport_scroll_pixels));
            const auto distance = std::abs(y - request.y_pixels);
            if (distance > request.note_hit_radius_pixels) continue;
            const auto kind = note_target_kind(*entry);
            if (!improves_candidate(best, kind, distance, entry->note)) continue;
            best.target = {.kind = kind, .lane = lane, .note = entry->note,
                           .note_kind = entry->kind, .position = entry->position};
            best.priority = hit_priority(kind);
            best.distance = distance;
        }
    }
    return best;
}

auto first_body_note(const editor_hit_test_index_t& index, lane_id_t lane,
                     std::uint64_t pointer_row, std::size_t candidate_count,
                     std::size_t node, std::size_t first, std::size_t last)
    -> const editor_note_span_entry_t* {
    if (first >= candidate_count || index.note_span_tree_max_end[node] <= pointer_row)
        return nullptr;
    if (last - first == 1) {
        if (first >= index.note_spans.size()) return nullptr;
        const auto& span = index.note_spans[first];
        return span.lane == lane && span.begin_row < pointer_row && pointer_row < span.end_row
            ? &span : nullptr;
    }
    const auto middle = first + (last - first) / 2;
    const auto* left = first_body_note(index, lane, pointer_row, candidate_count,
                                       node * 2, first, middle);
    const auto* right = first_body_note(index, lane, pointer_row, candidate_count,
                                        node * 2 + 1, middle, last);
    if (!left) return right;
    if (!right) return left;
    return left->note.value <= right->note.value ? left : right;
}

auto body_note_at(const editor_hit_test_index_t& index, lane_id_t lane,
                  std::uint64_t pointer_row) -> const editor_note_span_entry_t* {
    if (index.note_span_tree_leaf_base == 0 ||
        index.note_span_tree_max_end.size() != index.note_span_tree_leaf_base * 2)
        return nullptr;
    const auto after = std::ranges::upper_bound(index.note_spans, pointer_row, {},
        &editor_note_span_entry_t::begin_row);
    const auto count = static_cast<std::size_t>(std::distance(index.note_spans.begin(), after));
    return count == 0 ? nullptr : first_body_note(index, lane, pointer_row, count, 1, 0,
                                                  index.note_span_tree_leaf_base);
}

} // namespace

auto hit_test_editor(const chart_document_t& chart, const editor_hit_test_index_t& index,
                     const editor_interaction_view_model_t& view,
                     const editor_hit_test_request_t& request)
    -> std::optional<editor_hit_target_t> {
    if (!valid_hit_request(chart, index, view, request)) return std::nullopt;
    const auto position = position_at_row_y_pixels(chart, index.measures, view, request.y_pixels);
    const auto row = absolute_row_at_y_pixels(chart, index.measures, view, request.y_pixels);
    if (!position || !row || request.x_pixels < request.field_left_pixels ||
        request.x_pixels >= request.field_left_pixels + request.field_width_pixels)
        return std::nullopt;
    const auto lane = lane_at_pointer(chart, index, view, request, *position);
    if (!lane) return std::nullopt;
    const auto snapped = snapped_position(chart, index.measures, *position,
                                          view.active_snap_denominator);
    auto best = nearby_note_points(chart, index, view, request, *row, *lane,
                                   snapped.value_or(*position));
    if (best.priority == 0) {
        if (const auto* body = body_note_at(index, *lane, *row))
            best.target = {.kind = editor_hit_target_kind_t::note, .lane = body->lane,
                           .note = body->note, .note_kind = body->kind, .position = *position};
    }
    return best.target;
}

namespace {

auto valid_hover_request(const chart_document_t& chart, const editor_hit_test_index_t& index,
                         const editor_interaction_view_model_t& view,
                         const editor_object_hover_request_t& request) -> bool {
    const auto right = request.field_left_pixels + request.field_width_pixels;
    return index.chart == chart.id && view.chart == chart.id &&
        index.document_revision == view.document_revision && std::isfinite(request.x_pixels) &&
        std::isfinite(request.y_pixels) && std::isfinite(request.field_left_pixels) &&
        std::isfinite(request.field_width_pixels) && request.field_width_pixels > 0.0 &&
        std::isfinite(request.hit_radius_pixels) && request.hit_radius_pixels >= 0.0 &&
        std::isfinite(right) && request.x_pixels >= request.field_left_pixels &&
        request.x_pixels < right &&
        index.hover_objects_by_id.size() == index.available_objects.size();
}

auto nearest_note_hover(const chart_document_t& chart, const editor_hit_test_index_t& index,
                        const editor_interaction_view_model_t& view,
                        const editor_object_hover_request_t& request)
    -> std::pair<const editor_hover_index_entry_t*, double> {
    const auto note_hit = hit_test_editor(chart, index, view,
        {.x_pixels = request.x_pixels, .y_pixels = request.y_pixels,
         .field_left_pixels = request.field_left_pixels,
         .field_width_pixels = request.field_width_pixels,
         .note_hit_radius_pixels = request.hit_radius_pixels});
    if (!note_hit || !note_hit->note) return {nullptr, std::numeric_limits<double>::infinity()};
    const auto object = editor_selection_object_t{editor_selection_object_kind_t::note,
                                                   *note_hit->note};
    const auto found = std::ranges::lower_bound(index.hover_objects_by_id, object, {},
                                                &editor_hover_index_entry_t::object);
    if (found == index.hover_objects_by_id.end() || found->object != object)
        return {nullptr, std::numeric_limits<double>::infinity()};
    const auto hit_row = absolute_row(index.measures, note_hit->position);
    const auto y = static_cast<double>(
        static_cast<long double>(hit_row.value_or(found->absolute_row)) *
            static_cast<long double>(view.pixels_per_standard_measure) /
            static_cast<long double>(chart.divisions_per_standard_measure) -
        static_cast<long double>(view.viewport_scroll_pixels));
    return {&*found, std::abs(y - request.y_pixels)};
}

} // namespace

auto hover_editor_object(const chart_document_t& chart, const editor_hit_test_index_t& index,
                         const editor_interaction_view_model_t& view,
                         const editor_object_hover_request_t& request)
    -> std::optional<editor_object_hover_state_t> {
    if (!valid_hover_request(chart, index, view, request)) return std::nullopt;
    const auto pointer_row =
        absolute_row_at_y_pixels(chart, index.measures, view, request.y_pixels);
    if (!pointer_row.has_value())
        return std::nullopt;

    const editor_hover_index_entry_t* best{};
    auto best_distance = std::numeric_limits<double>::infinity();
    if (request.scope.notes) {
        const auto [note, distance] = nearest_note_hover(chart, index, view, request);
        best = note;
        best_distance = distance;
    }

    const auto radius_rows = static_cast<long double>(request.hit_radius_pixels) *
                             static_cast<long double>(chart.divisions_per_standard_measure) /
                             static_cast<long double>(view.pixels_per_standard_measure);
    const auto min_row = radius_rows >= static_cast<long double>(*pointer_row)
                             ? std::uint64_t{0}
                             : static_cast<std::uint64_t>(std::floor(
                                   static_cast<long double>(*pointer_row) - radius_rows));
    const auto max_row =
        radius_rows >= static_cast<long double>(index.measures.total_rows - 1 - *pointer_row)
            ? index.measures.total_rows - 1
            : static_cast<std::uint64_t>(
                  std::ceil(static_cast<long double>(*pointer_row) + radius_rows));
    const auto row_objects =
        editor_objects_in_row_range(chart, index, min_row, max_row, request.scope);
    if (std::holds_alternative<diagnostic_list_t>(row_objects))
        return std::nullopt;
    const auto pointer_chart_row = (static_cast<long double>(request.y_pixels) +
                                    static_cast<long double>(view.viewport_scroll_pixels)) *
                                   static_cast<long double>(chart.divisions_per_standard_measure) /
                                   static_cast<long double>(view.pixels_per_standard_measure);
    const auto pixels_per_row = static_cast<long double>(view.pixels_per_standard_measure) /
                                static_cast<long double>(chart.divisions_per_standard_measure);
    for (const auto& object : std::get<std::vector<editor_selection_object_t>>(row_objects)) {
        // Notes have lane geometry; their candidate was resolved through the
        // ordinary lane-aware hit test above.
        if (object.kind == editor_selection_object_kind_t::note)
            continue;
        const auto candidate = std::ranges::lower_bound(index.hover_objects_by_id, object, {},
                                                        &editor_hover_index_entry_t::object);
        if (candidate == index.hover_objects_by_id.end() || candidate->object != object)
            continue;
        const auto nearest_row =
            std::clamp(pointer_chart_row, static_cast<long double>(candidate->absolute_row),
                       static_cast<long double>(candidate->end_absolute_row));
        const auto distance =
            static_cast<double>(std::abs(nearest_row - pointer_chart_row) * pixels_per_row);
        // `pointer_chart_row` is recovered from pixel coordinates while a
        // marker's row is an exact integer.  Permit only the accumulated
        // floating-point round-trip noise so a zero-radius hover still finds
        // the marker used to produce those pixels.
        constexpr auto pixel_round_trip_epsilon = 1.0e-9;
        if (distance > request.hit_radius_pixels + pixel_round_trip_epsilon ||
            (best != nullptr &&
             (distance > best_distance || (distance == best_distance && object >= best->object)))) {
            continue;
        }
        best = &*candidate;
        best_distance = distance;
    }
    if (best == nullptr)
        return std::nullopt;
    return editor_object_hover_state_t{
        .object = best->object,
        .editor = best->editor,
        .position = best->position,
        .document_revision = index.document_revision,
        .tooltip = best->tooltip,
    };
}

auto register_editor_object_click(editor_double_click_state_t& state,
                                  const editor_hit_test_index_t& index,
                                  const editor_interaction_view_model_t& view,
                                  const std::optional<editor_object_hover_state_t>& hover,
                                  double x_pixels, double y_pixels, std::uint64_t time_milliseconds,
                                  editor_double_click_settings_t settings, bool pointer_captured,
                                  bool focused) -> std::optional<editor_object_edit_request_t> {
    const auto clear = [&] { state.previous_click.reset(); };
    if (!focused || !pointer_captured || !hover.has_value() || view.chart != index.chart ||
        view.document_revision != index.document_revision ||
        hover->document_revision != index.document_revision || !std::isfinite(x_pixels) ||
        !std::isfinite(y_pixels) || !std::isfinite(settings.pointer_slop_pixels) ||
        settings.pointer_slop_pixels < 0.0 || settings.maximum_interval_milliseconds == 0) {
        clear();
        return std::nullopt;
    }
    const auto found = std::ranges::lower_bound(index.hover_objects_by_id, hover->object, {},
                                                &editor_hover_index_entry_t::object);
    if (found == index.hover_objects_by_id.end() || found->object != hover->object ||
        found->editor != hover->editor || found->position != hover->position) {
        clear();
        return std::nullopt;
    }
    if (state.previous_click.has_value()) {
        const auto& previous = *state.previous_click;
        const auto dx = x_pixels - previous.x_pixels;
        const auto dy = y_pixels - previous.y_pixels;
        const auto slop_squared = settings.pointer_slop_pixels * settings.pointer_slop_pixels;
        if (previous.object == hover->object &&
            previous.document_revision == index.document_revision &&
            time_milliseconds >= previous.time_milliseconds &&
            time_milliseconds - previous.time_milliseconds <=
                settings.maximum_interval_milliseconds &&
            dx * dx + dy * dy <= slop_squared) {
            const auto request = editor_object_edit_request_t{
                .chart = index.chart,
                .document_revision = index.document_revision,
                .object = found->object,
                .editor = found->editor,
            };
            clear();
            return request;
        }
    }
    state.previous_click = editor_click_candidate_t{
        .object = hover->object,
        .document_revision = index.document_revision,
        .time_milliseconds = time_milliseconds,
        .x_pixels = x_pixels,
        .y_pixels = y_pixels,
    };
    return std::nullopt;
}

auto clear_editor_double_click_state(editor_double_click_state_t& state) -> void {
    state.previous_click.reset();
}


} // namespace wormedit

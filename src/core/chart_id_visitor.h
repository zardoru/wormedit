#pragma once
#include <wormedit/core/document.h>

namespace wormedit::chart_id_detail {
template <class Position, class Visit> void visit_position(Position& position, Visit& visit) {
    visit(position.measure);
}

template <class Span, class Visit> void visit_span(Span& span, Visit& visit) {
    visit_position(span.begin, visit);
    visit_position(span.end, visit);
}

template <class Optional, class Visit> void visit_optional_id(Optional& id, Visit& visit) {
    if (id)
        visit(*id);
}

template <class Chart, class Visit> void visit_measures(Chart& chart, Visit& visit) {
    for (auto& measure : chart.measures) {
        visit(measure.id);
        for (auto& note : measure.notes) {
            visit(note.id);
            visit_position(note.position, visit);
            visit(note.lane);
            if (note.end)
                visit_position(*note.end, visit);
            visit_optional_id(note.sound_channel_event, visit);
            visit_optional_id(note.branch, visit);
            for (auto& layer : note.semantic_layers)
                visit(layer);
        }
        for (auto& event : measure.bgm_events) {
            visit(event.id);
            visit_position(event.position, visit);
            visit(event.source_lane);
            visit_optional_id(event.sound_channel_event, visit);
            visit_optional_id(event.branch, visit);
        }
    }
}

template <class Chart, class Visit> void visit_layouts(Chart& chart, Visit& visit) {
    for (auto& segment : chart.lane_layouts) {
        visit(segment.id);
        visit_position(segment.position, visit);
        for (auto& lane : segment.layout.lanes)
            visit(lane.id);
    }
    for (auto& binding : chart.input_profile.bindings)
        visit(binding.lane);
    for (auto& layer : chart.semantic_layers)
        visit(layer.id);
    for (auto& entry : chart.sound_layer_defaults)
        for (auto& layer : entry.layers)
            visit(layer);
    for (auto& lane : chart.bgm_source_lanes)
        visit(lane.id);
}

template <class Events, class Visit> void visit_point_events(Events& events, Visit& visit) {
    for (auto& event : events) {
        visit(event.id);
        visit_position(event.position, visit);
    }
}

template <class Chart, class Visit> void visit_timing(Chart& chart, Visit& visit) {
    visit_point_events(chart.bpms, visit);
    visit_point_events(chart.pauses, visit);
    visit_point_events(chart.scrolls, visit);
    visit_point_events(chart.speeds, visit);
    visit_point_events(chart.kiai, visit);
    visit_point_events(chart.timing_anchors, visit);
    visit_point_events(chart.bga_events, visit);
    for (auto& event : chart.warps) {
        visit(event.id);
        visit_span(event.span, visit);
    }
    for (auto& recipe : chart.sv_recipes) {
        visit(recipe.id);
        visit_span(recipe.range, visit);
    }
}

template <class Chart, class Visit> void visit_annotations(Chart& chart, Visit& visit) {
    for (auto& bookmark : chart.bookmarks) {
        visit(bookmark.id);
        if (bookmark.position)
            visit_position(*bookmark.position, visit);
    }
    for (auto& section : chart.sections) {
        visit(section.id);
        if (section.beat_span)
            visit_span(*section.beat_span, visit);
    }
    for (auto& channel : chart.sound_channels) {
        visit(channel.id);
        visit(channel.source_clip.id);
        for (auto& event : channel.events) {
            visit(event.id);
            visit_position(event.position, visit);
            visit(event.lane);
            if (event.end)
                visit_position(*event.end, visit);
        }
    }
}

template <class Node, class Visit> void visit_branch(Node& node, Visit& visit) {
    visit(node.id);
    for (auto& child : node.children)
        visit_branch(child, visit);
}

template <class Chart, class Visit> void visit_projections(Chart& chart, Visit& visit) {
    for (auto& projection : chart.bms_bpm_projections) {
        visit(projection.event);
        visit_position(projection.imported_position, visit);
    }
    for (auto& projection : chart.bms_pause_projections) {
        visit(projection.event);
        visit_position(projection.imported_position, visit);
    }
    if (chart.bms_control_tree)
        visit_branch(*chart.bms_control_tree, visit);
    for (auto& selection : chart.active_branch_selection)
        visit(selection.branch);
}

template <class Chart, class Visit> void visit_timing_link_ids(Chart& chart, Visit& visit) {
    for (auto& link : chart.timing_links.categories) {
        if (!link)
            continue;
        for (auto& identity : link->objects)
            visit(identity.target);
        for (auto& identity : link->recipes)
            visit(identity.target);
        for (auto& identity : link->measures)
            visit(identity.target);
    }
}

template <class Chart, class Visit> void visit_chart_ids(Chart& chart, Visit& visit) {
    visit(chart.id);
    visit_measures(chart, visit);
    visit_layouts(chart, visit);
    visit_timing(chart, visit);
    visit_annotations(chart, visit);
    visit_projections(chart, visit);
    visit_timing_link_ids(chart, visit);
}

} // namespace wormedit::chart_id_detail

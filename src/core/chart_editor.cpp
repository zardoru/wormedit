#include <wormedit/core/chart_editor.h>

#include <algorithm>
#include <limits>
#include <map>
#include <typeindex>

namespace wormedit {
namespace {

template<class Position, class Visit>
void visit_position(Position& position, Visit& visit) { visit(position.measure); }

template<class Span, class Visit>
void visit_span(Span& span, Visit& visit) {
    visit_position(span.begin, visit);
    visit_position(span.end, visit);
}

template<class Optional, class Visit>
void visit_optional_id(Optional& id, Visit& visit) { if (id) visit(*id); }

template<class Chart, class Visit>
void visit_measures(Chart& chart, Visit& visit) {
    for (auto& measure : chart.measures) {
        visit(measure.id);
        for (auto& note : measure.notes) {
            visit(note.id); visit_position(note.position, visit); visit(note.lane);
            if (note.end) visit_position(*note.end, visit);
            visit_optional_id(note.sound_channel_event, visit);
            visit_optional_id(note.branch, visit);
            for (auto& layer : note.semantic_layers) visit(layer);
        }
        for (auto& event : measure.bgm_events) {
            visit(event.id); visit_position(event.position, visit); visit(event.source_lane);
            visit_optional_id(event.sound_channel_event, visit);
            visit_optional_id(event.branch, visit);
        }
    }
}

template<class Chart, class Visit>
void visit_layouts(Chart& chart, Visit& visit) {
    for (auto& segment : chart.lane_layouts) {
        visit(segment.id); visit_position(segment.position, visit);
        for (auto& lane : segment.layout.lanes) visit(lane.id);
    }
    for (auto& binding : chart.input_profile.bindings) visit(binding.lane);
    for (auto& layer : chart.semantic_layers) visit(layer.id);
    for (auto& lane : chart.bgm_source_lanes) visit(lane.id);
}

template<class Events, class Visit>
void visit_point_events(Events& events, Visit& visit) {
    for (auto& event : events) { visit(event.id); visit_position(event.position, visit); }
}

template<class Chart, class Visit>
void visit_timing(Chart& chart, Visit& visit) {
    visit_point_events(chart.bpms, visit); visit_point_events(chart.pauses, visit);
    visit_point_events(chart.scrolls, visit); visit_point_events(chart.speeds, visit);
    visit_point_events(chart.kiai, visit); visit_point_events(chart.timing_anchors, visit);
    visit_point_events(chart.bga_events, visit);
    for (auto& event : chart.warps) { visit(event.id); visit_span(event.span, visit); }
    for (auto& recipe : chart.sv_recipes) { visit(recipe.id); visit_span(recipe.range, visit); }
}

template<class Chart, class Visit>
void visit_annotations(Chart& chart, Visit& visit) {
    for (auto& bookmark : chart.bookmarks) {
        visit(bookmark.id);
        if (bookmark.position) visit_position(*bookmark.position, visit);
    }
    for (auto& section : chart.sections) {
        visit(section.id);
        if (section.beat_span) visit_span(*section.beat_span, visit);
    }
    for (auto& channel : chart.sound_channels) {
        visit(channel.id); visit(channel.source_clip.id);
        for (auto& event : channel.events) {
            visit(event.id); visit_position(event.position, visit); visit(event.lane);
            if (event.end) visit_position(*event.end, visit);
        }
    }
}

template<class Node, class Visit>
void visit_branch(Node& node, Visit& visit) {
    visit(node.id);
    for (auto& child : node.children) visit_branch(child, visit);
}

template<class Chart, class Visit>
void visit_projections(Chart& chart, Visit& visit) {
    for (auto& projection : chart.bms_bpm_projections) {
        visit(projection.event); visit_position(projection.imported_position, visit);
    }
    for (auto& projection : chart.bms_pause_projections) {
        visit(projection.event); visit_position(projection.imported_position, visit);
    }
    if (chart.bms_control_tree) visit_branch(*chart.bms_control_tree, visit);
    for (auto& selection : chart.active_branch_selection) visit(selection.branch);
}

template<class Chart, class Visit>
void visit_chart_ids(Chart& chart, Visit& visit) {
    visit(chart.id);
    visit_measures(chart, visit); visit_layouts(chart, visit); visit_timing(chart, visit);
    visit_annotations(chart, visit); visit_projections(chart, visit);
}

auto maximum_chart_id(const song_document_t& document) -> std::uint64_t {
    std::uint64_t maximum{};
    auto visit = [&](const auto& id) { maximum = std::max(maximum, id.value); };
    for (const auto& chart : document.charts) visit_chart_ids(chart, visit);
    visit(document.id);
    for (const auto& asset : document.audio_assets) visit(asset.id);
    for (const auto& asset : document.visual_assets) visit(asset.id);
    for (const auto& asset : document.midi_assets) visit(asset.id);
    for (const auto& resource : document.resources) visit(resource.id);
    for (const auto& recipe : document.generator_recipes) visit(recipe.id);
    for (const auto& storyboard : document.storyboards) visit(storyboard.id);
    return maximum;
}

auto history_id_ceiling(const document_history_t& history) -> std::uint64_t {
    auto maximum = maximum_chart_id(history.document);
    const auto scan = [&](const auto& records) {
        for (const auto& record : records) {
            maximum = std::max(maximum, maximum_chart_id(record.before));
            maximum = std::max(maximum, maximum_chart_id(record.after));
        }
    };
    scan(history.undo); scan(history.redo);
    return maximum;
}

struct chart_id_remap_t {
    std::uint64_t next;
    bool exhausted{};
    std::map<std::pair<std::type_index, std::uint64_t>, std::uint64_t> ids;
    template<class Id> void operator()(Id& id) {
        if (!id) return;
        const auto key = std::pair{std::type_index(typeid(Id)), id.value};
        auto existing = ids.find(key);
        if (existing != ids.end()) { id.value = existing->second; return; }
        if (next == 0) { exhausted = true; return; }
        const auto fresh = next++;
        ids.emplace(key, fresh);
        id.value = fresh;
    }
};

auto reject(const document_history_t& history, std::string message) -> document_command_result_t {
    return {.revision = history.document.revision, .diagnostics = {{
        .severity = diagnostic_severity_t::error, .code = diagnostic_code_t::invalid_id,
        .message = std::move(message)}}};
}

auto find_chart(const song_document_t& document, chart_id_t id) {
    return std::ranges::find(document.charts, id, &chart_document_t::id);
}

auto check_chart(const document_history_t& history, std::uint64_t revision, chart_id_t id)
    -> std::optional<document_command_result_t> {
    if (history.document.revision != revision) return reject(history, "chart command revision is stale");
    if (find_chart(history.document, id) == history.document.charts.end())
        return reject(history, "chart command refers to an unknown chart");
    return std::nullopt;
}

void remove_memberships(song_document_t& document, chart_id_t id) {
    for (auto& group : document.output_groups) std::erase(group.charts, id);
}

auto commit_empty_chart(document_history_t& history, lane_input_profile_t profile,
    format_family_t source_family, std::uint64_t divisions, signed_rational_t offset)
    -> duplicate_chart_result_t {
    const auto ceiling = history_id_ceiling(history);
    if (ceiling == std::numeric_limits<std::uint64_t>::max())
        return {.command = reject(history, "chart ID sequence is exhausted")};
    chart_document_t chart{
        .id = chart_id_t{1}, .metadata = {.name = "New Difficulty"},
        .source_family = source_family, .divisions_per_standard_measure = divisions,
        .beat_zero_audio_offset_seconds = offset, .measures = {{.id = measure_id_t{1}}},
        .lane_layouts = {{.id = object_id_t{1}, .position = {measure_id_t{1}, 0},
                          .layout = std::move(profile.layout)}},
        .input_profile = std::move(profile.input),
        .bpms = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0},
                  .beats_per_minute = {120, 1}}}};
    chart_id_remap_t remap{.next = ceiling + 1};
    visit_chart_ids(chart, remap);
    if (remap.exhausted) return {.command = reject(history, "chart ID sequence is exhausted")};
    const auto id = chart.id;
    auto proposed = history.document;
    proposed.charts.push_back(std::move(chart));
    auto result = commit_document_command(history, std::move(proposed), "New chart");
    const auto applied = result.applied;
    return {.command = std::move(result), .chart = applied ? std::optional{id} : std::nullopt};
}

} // namespace

auto apply_new_chart_command(document_history_t& history, std::uint64_t revision,
    lane_input_profile_t profile, format_family_t source_family) -> duplicate_chart_result_t {
    if (history.document.revision != revision)
        return {.command = reject(history, "chart command revision is stale")};
    return commit_empty_chart(history, std::move(profile), source_family,
        default_divisions_per_standard_measure, {});
}

auto apply_add_chart_command(document_history_t& history, std::uint64_t revision,
    chart_id_t template_chart) -> duplicate_chart_result_t {
    if (auto invalid = check_chart(history, revision, template_chart))
        return {.command = std::move(*invalid)};
    const auto source = find_chart(history.document, template_chart);
    if (source->lane_layouts.empty())
        return {.command = reject(history, "new chart template has no initial lane layout")};
    lane_input_profile_t profile{.layout = source->lane_layouts.front().layout,
        .input = source->input_profile};
    std::erase_if(profile.input.bindings, [&](const auto& binding) {
        return std::ranges::none_of(profile.layout.lanes, [&](const auto& lane) { return lane.id == binding.lane; });
    });
    return commit_empty_chart(history, std::move(profile), source->source_family,
        source->divisions_per_standard_measure, source->beat_zero_audio_offset_seconds);
}

auto apply_duplicate_chart_command(document_history_t& history, std::uint64_t revision,
                                   chart_id_t id) -> duplicate_chart_result_t {
    if (auto invalid = check_chart(history, revision, id)) return {.command = std::move(*invalid)};
    const auto ceiling = history_id_ceiling(history);
    if (ceiling == std::numeric_limits<std::uint64_t>::max())
        return {.command = reject(history, "chart ID sequence is exhausted")};
    auto duplicate = *find_chart(history.document, id);
    chart_id_remap_t remap{.next = ceiling + 1};
    visit_chart_ids(duplicate, remap);
    if (remap.exhausted) return {.command = reject(history, "chart ID sequence is exhausted")};
    duplicate.metadata.name += " (copy)";
    const auto new_id = duplicate.id;
    auto proposed = history.document;
    proposed.charts.push_back(std::move(duplicate));
    auto result = commit_document_command(history, std::move(proposed), "Duplicate chart");
    const auto applied = result.applied;
    return {.command = std::move(result), .chart = applied ? std::optional{new_id} : std::nullopt};
}

auto apply_delete_chart_command(document_history_t& history, std::uint64_t revision,
                                chart_id_t id) -> document_command_result_t {
    if (auto invalid = check_chart(history, revision, id)) return std::move(*invalid);
    if (history.document.charts.size() == 1) return reject(history, "cannot delete the last chart");
    auto proposed = history.document;
    remove_memberships(proposed, id);
    std::erase_if(proposed.charts, [id](const auto& chart) { return chart.id == id; });
    return commit_document_command(history, std::move(proposed), "Delete chart");
}

auto apply_reorder_chart_command(document_history_t& history, std::uint64_t revision,
                                 chart_id_t id, std::size_t destination) -> document_command_result_t {
    if (auto invalid = check_chart(history, revision, id)) return std::move(*invalid);
    if (destination >= history.document.charts.size()) return reject(history, "chart destination is out of range");
    auto proposed = history.document;
    const auto source = std::ranges::find(proposed.charts, id, &chart_document_t::id);
    auto moved = std::move(*source);
    proposed.charts.erase(source);
    proposed.charts.insert(proposed.charts.begin() + static_cast<std::ptrdiff_t>(destination), std::move(moved));
    return commit_document_command(history, std::move(proposed), "Reorder charts");
}

auto apply_move_chart_group_command(document_history_t& history, std::uint64_t revision,
                                    chart_id_t id, std::optional<std::size_t> group) -> document_command_result_t {
    if (auto invalid = check_chart(history, revision, id)) return std::move(*invalid);
    if (group && *group >= history.document.output_groups.size()) return reject(history, "output group is out of range");
    auto proposed = history.document;
    remove_memberships(proposed, id);
    if (group) proposed.output_groups[*group].charts.push_back(id);
    return commit_document_command(history, std::move(proposed), "Move chart between output groups");
}

} // namespace wormedit

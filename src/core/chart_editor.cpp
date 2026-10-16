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
    for (auto& entry : chart.sound_layer_defaults)
        for (auto& layer : entry.layers) visit(layer);
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

auto check_chart(const document_history_t& history, chart_id_t id)
    -> std::optional<document_command_result_t> {
    if (history.document.find_chart(id) == nullptr)
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "chart command refers to an unknown chart");
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
        return {.command = document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "chart ID sequence is exhausted")};
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
    if (remap.exhausted) return {.command = document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "chart ID sequence is exhausted")};
    const auto id = chart.id;
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.document();
    proposed.charts.push_back(std::move(chart));
    auto result = transaction.commit("New chart");
    const auto applied = result.applied;
    return {.command = std::move(result), .payload = {.chart = applied ? std::optional{id} : std::nullopt}};
}

} // namespace

auto apply_new_chart_command(document_history_t& history,
    lane_input_profile_t profile, format_family_t source_family) -> duplicate_chart_result_t {
    return commit_empty_chart(history, std::move(profile), source_family,
        default_divisions_per_standard_measure, {});
}

auto apply_add_chart_command(document_history_t& history,
    chart_id_t template_chart) -> duplicate_chart_result_t {
    if (auto invalid = check_chart(history, template_chart))
        return {.command = std::move(*invalid)};
    const auto source = history.document.find_chart(template_chart);
    if (source->lane_layouts.empty())
        return {.command = document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "new chart template has no initial lane layout")};
    lane_input_profile_t profile{.layout = source->lane_layouts.front().layout,
        .input = source->input_profile};
    std::erase_if(profile.input.bindings, [&](const auto& binding) {
        return std::ranges::none_of(profile.layout.lanes, [&](const auto& lane) { return lane.id == binding.lane; });
    });
    return commit_empty_chart(history, std::move(profile), source->source_family,
        source->divisions_per_standard_measure, source->beat_zero_audio_offset_seconds);
}

auto apply_duplicate_chart_command(document_history_t& history,
                                   chart_id_t id) -> duplicate_chart_result_t {
    if (auto invalid = check_chart(history, id)) return {.command = std::move(*invalid)};
    const auto ceiling = history_id_ceiling(history);
    if (ceiling == std::numeric_limits<std::uint64_t>::max())
        return {.command = document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "chart ID sequence is exhausted")};
    auto duplicate = *history.document.find_chart(id);
    chart_id_remap_t remap{.next = ceiling + 1};
    visit_chart_ids(duplicate, remap);
    if (remap.exhausted) return {.command = document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "chart ID sequence is exhausted")};
    duplicate.metadata.name += " (copy)";
    const auto new_id = duplicate.id;
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.document();
    proposed.charts.push_back(std::move(duplicate));
    auto result = transaction.commit("Duplicate chart");
    const auto applied = result.applied;
    return {.command = std::move(result), .payload = {.chart = applied ? std::optional{new_id} : std::nullopt}};
}

auto apply_delete_chart_command(document_history_t& history,
                                chart_id_t id) -> document_command_result_t {
    if (auto invalid = check_chart(history, id)) return std::move(*invalid);
    if (history.document.charts.size() == 1) return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "cannot delete the last chart");
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.document();
    remove_memberships(proposed, id);
    std::erase_if(proposed.charts, [id](const auto& chart) { return chart.id == id; });
    return transaction.commit("Delete chart");
}

auto apply_reorder_chart_command(document_history_t& history,
                                 chart_id_t id, std::size_t destination) -> document_command_result_t {
    if (auto invalid = check_chart(history, id)) return std::move(*invalid);
    if (destination >= history.document.charts.size()) return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "chart destination is out of range");
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.document();
    const auto source = proposed.find_chart(id);
    auto moved = std::move(*source);
    proposed.charts.erase(proposed.charts.begin() + (source - proposed.charts.data()));
    proposed.charts.insert(proposed.charts.begin() + static_cast<std::ptrdiff_t>(destination), std::move(moved));
    return transaction.commit("Reorder charts");
}

auto apply_move_chart_group_command(document_history_t& history,
                                    chart_id_t id, std::optional<std::size_t> group) -> document_command_result_t {
    if (auto invalid = check_chart(history, id)) return std::move(*invalid);
    if (group && *group >= history.document.output_groups.size()) return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "output group is out of range");
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.document();
    remove_memberships(proposed, id);
    if (group) proposed.output_groups[*group].charts.push_back(id);
    return transaction.commit("Move chart between output groups");
}

namespace {
auto is_output_group_path_valid(const std::filesystem::path& path) -> bool {
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
        path != path.lexically_normal()) return false;
    for (const auto& part : path) {
        const auto text = part.generic_string();
        if (text.empty() || text == "." || text == ".." ||
            text.find_first_of("\\:") != std::string::npos) return false;
        if (std::ranges::any_of(text, [](unsigned char c) { return c < 32 || c == 127; })) return false;
    }
    return true;
}
auto output_group_error(const song_document_t& doc, std::optional<std::size_t> index,
    const output_group_t& group) -> std::string {
    if (index && *index >= doc.output_groups.size()) return "Output group is no longer available.";
    if (group.name.empty() || group.name.find_first_not_of(" \t\r\n") == std::string::npos)
        return "Output group needs a name.";
    for (std::size_t i = 0; i < doc.output_groups.size(); ++i)
        if (i != index && doc.output_groups[i].name == group.name) return "Output group name is already used.";
    if (group.family < format_family_t::native_project || group.family > format_family_t::bmson) return "Output format is not supported.";
    if (!is_output_group_path_valid(group.relative_output_path)) return "Output path must be a safe project-relative path.";
    if (group.relative_output_path.has_extension() && group.charts.size() > 1 &&
        group.family != format_family_t::stepmania && group.family != format_family_t::native_project)
        return "Use an output directory for multiple chart files.";
    std::vector<chart_id_t> seen;
    for (const auto chart : group.charts) {
        if (doc.find_chart(chart) == nullptr) return "Output group contains an unknown chart.";
        if (std::ranges::find(seen, chart) != seen.end()) return "Output group repeats a chart.";
        seen.push_back(chart);
    }
    return {};
}
} // namespace

auto apply_set_output_group_command(document_history_t& history,
    std::optional<std::size_t> index, output_group_t group) -> document_command_result_t {
    if (const auto error = output_group_error(history.document, index, group); !error.empty())
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, error);
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.document();
    if (index) proposed.output_groups[*index] = std::move(group);
    else proposed.output_groups.push_back(std::move(group));
    return transaction.commit(index ? "Edit output group" : "Create output group");
}
auto apply_delete_output_group_command(document_history_t& history,
    std::size_t index) -> document_command_result_t {
    if (index >= history.document.output_groups.size()) return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "Output group is no longer available.");
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.document();
    proposed.output_groups.erase(proposed.output_groups.begin() + static_cast<std::ptrdiff_t>(index));
    return transaction.commit("Delete output group");
}

} // namespace wormedit

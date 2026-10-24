#include <wormedit/core/chart_editor.h>
#include <wormedit/core/preferences.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/timing_links.h>
#include "id_ceiling.h"

#include <wormedit/formats/stepmania.h>

#include <algorithm>
#include <limits>
#include <map>
#include <set>
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
void visit_timing_link_ids(Chart& chart, Visit& visit) {
    for (auto& link : chart.timing_links.categories) {
        if (!link) continue;
        for (auto& identity : link->objects) visit(identity.target);
        for (auto& identity : link->recipes) visit(identity.target);
        for (auto& identity : link->measures) visit(identity.target);
    }
}

template<class Chart, class Visit>
void visit_chart_ids(Chart& chart, Visit& visit) {
    visit(chart.id);
    visit_measures(chart, visit); visit_layouts(chart, visit); visit_timing(chart, visit);
    visit_annotations(chart, visit); visit_projections(chart, visit);
    visit_timing_link_ids(chart, visit);
}

auto maximum_chart_id(const song_document_t& document) -> std::uint64_t {
    auto maximum = id_ceiling_t<object_id_t>::from(document).maximum;
    // Legacy chart duplication also remaps layout vocabulary IDs. Scoped lane
    // IDs stay in their profile domain; every scoped declaration is scanned above.
    auto visit = [&](const auto& id) { maximum = std::max(maximum, id.value); };
    for (const auto& chart : document.charts) visit_chart_ids(chart, visit);
    return maximum;
}

auto history_id_ceiling(const document_history_t& history) -> std::uint64_t {
    return maximum_history_identity(history, maximum_chart_id(history.document), maximum_chart_id);
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
    if (history.document.read_chart(id) == nullptr)
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "chart command refers to an unknown chart");
    return std::nullopt;
}

void remove_memberships(song_document_t& document, chart_id_t id) {
    for (auto& group : document.output_groups) std::erase(group.charts, id);
}

auto check_chart_selection(const document_history_t& history, std::span<const chart_id_t> charts)
    -> std::optional<document_command_result_t> {
    std::set<chart_id_t> seen;
    if (charts.empty())
        return document_command_transaction_t::rejection(history.document.revision,
            diagnostic_code_t::invalid_id, "chart selection is empty");
    for (const auto id : charts) {
        if (auto invalid = check_chart(history, id)) return invalid;
        if (!seen.insert(id).second)
            return document_command_transaction_t::rejection(history.document.revision,
                diagnostic_code_t::invalid_id, "chart selection contains duplicate IDs");
    }
    return std::nullopt;
}

auto duplicate_program(const bms_program_document_t& source, std::uint64_t& next)
    -> attempt_t<std::shared_ptr<const bms_program_document_t>, bms_program_projection_diagnostics_t> {
    if (!next || next == UINT64_MAX) return {};
    const auto difficulty = chart_id_t{next++};
    const auto& prepared = source.prepared();
    std::set<std::uint64_t> shared;
    for (const auto& resource : prepared.resources) shared.insert(resource.id.value);
    for (const auto& asset : prepared.audio_assets) shared.insert(asset.id.value);
    for (const auto& asset : prepared.visual_assets) shared.insert(asset.id.value);
    std::unordered_map<std::uint64_t, std::uint64_t> ids;
    auto reserve = [&](object_id_t id) {
        if (shared.contains(id.value)) return ids.emplace(id.value, id.value).second;
        if (!next || next == UINT64_MAX) return false;
        return ids.emplace(id.value, next++).second;
    };
    if (!id_declarations_t<object_id_t>::visit(prepared, reserve)) return {};
    auto copy = prepared.with_remapped_ids(ids);
    if (!copy) return {.diagnostics = {{.code = copy.error().code, .message = copy.error().message}}};
    const auto* view = source.read_chart();
    if (!view) return {};
    copy->settings.name = view->metadata.name + " (copy)";
    auto evaluation = source.evaluation();
    for (auto& choice : evaluation.selections) choice.branch = branch_id_t{ids.at(choice.branch.value)};
    return bms_program_document_t::from(difficulty, std::move(*copy), std::move(evaluation));
}

auto prepare_creation_timing_extent(const song_document_t& song, chart_document_t& chart,
    const timing_link_selection_t& timing) -> std::optional<diagnostic_t> {
    for (const auto& category : timing_categories) {
        const auto source_id = timing.source(category.kind);
        if (!source_id) continue;
        const auto* source = song.read_chart(source_id);
        if (!source) return error(diagnostic_code_t::invalid_id, "A selected timing source no longer exists.");
        if (category.kind == timing_category_t::offset) continue;
        const auto map = timing_position_map_t::from(*source, chart);
        if (!map) return map.error().front();
        const auto rows = map->rows(map->source.total_rows);
        if (!rows || (*rows > 0 && !extend_chart_to_row(chart, *rows - 1)))
            return error(diagnostic_code_t::invalid_resolution, "The new chart cannot cover the selected timing sources exactly on its grid.");
    }
    return std::nullopt;
}
auto stage_creation_timing(document_command_transaction_t& transaction, chart_id_t chart,
    const timing_link_selection_t& timing) -> diagnostic_list_t {
    for (const auto& category : timing_categories) {
        const auto source = timing.source(category.kind);
        if (!source) continue;
        auto staged = stage_timing_link_edit(transaction, chart,
            {.category = category.kind, .source = source, .discard_confirmed = true});
        if (!staged.value) return std::move(staged.diagnostics);
    }
    return {};
}

auto commit_empty_chart(document_history_t& history, lane_input_profile_t profile,
    format_family_t source_family, std::uint64_t divisions, signed_rational_t offset,
    const timing_link_selection_t& timing)
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
    if (const auto* stream = history.document.longest_music_or_bgm_stream();
        stream && !extend_chart_to_audio_end(chart, *stream))
        return {.command = document_command_transaction_t::rejection(history.document.revision,
            diagnostic_code_t::invalid_measure, "new chart cannot cover the linked music/BGM duration")};
    if (const auto issue = prepare_creation_timing_extent(history.document, chart, timing))
        return {.command = {.revision = history.document.revision, .diagnostics = {*issue}}};
    chart_id_remap_t remap{.next = ceiling + 1};
    visit_chart_ids(chart, remap);
    if (remap.exhausted) return {.command = document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "chart ID sequence is exhausted")};
    const auto id = chart.id;
    auto transaction = document_command_transaction_t(history);
    auto& proposed = transaction.document();
    if (source_family == format_family_t::bms || !proposed.prepared_bms_programs.empty()) proposed.ensure_chart_order();
    if (!proposed.chart_order.empty()) proposed.chart_order.push_back(id);
    proposed.charts.push_back(std::move(chart));
    const auto diagnostics = stage_creation_timing(transaction, id, timing);
    if (!diagnostics.empty()) return {.command = {.revision = history.document.revision, .diagnostics = diagnostics}};
    if (source_family == format_family_t::bms) {
        const auto first = transaction.next_bms_program_item_id();
        const auto owner = first ? bms_program_document_t::from_empty_chart(*proposed.find_chart(id), *first)
            : attempt_t<std::shared_ptr<const bms_program_document_t>, bms_program_projection_diagnostics_t>{};
        if (!owner.value || !owner.ok()) return {.command = transaction.reject(diagnostic_code_t::unsupported_feature,
            owner.diagnostics.empty() ? "Scoped creation identities are exhausted." : owner.diagnostics.front().message)};
        std::erase_if(proposed.charts, [id](const auto& value) { return value.id == id; });
        proposed.prepared_bms_programs.push_back(*owner.value);
    }
    auto result = transaction.commit("New chart");
    const auto applied = result.applied;
    return {.command = std::move(result), .payload = {.chart = applied ? std::optional{id} : std::nullopt}};
}

} // namespace

auto apply_new_chart_command(document_history_t& history,
    lane_input_profile_t profile, format_family_t source_family,
    const default_lane_keys_t* defaults, const timing_link_selection_t& timing) -> duplicate_chart_result_t {
    if (defaults) profile.input = defaults->profile_for(profile.layout, profile.input);
    return commit_empty_chart(history, std::move(profile), source_family,
        default_divisions_per_standard_measure, {}, timing);
}

auto apply_add_chart_command(document_history_t& history,
    chart_id_t template_chart, const default_lane_keys_t* defaults, const timing_link_selection_t& timing) -> duplicate_chart_result_t {
    if (auto invalid = check_chart(history, template_chart))
        return {.command = std::move(*invalid)};
    const auto source = history.document.read_chart(template_chart);
    if (source->lane_layouts.empty())
        return {.command = document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "new chart template has no initial lane layout")};
    lane_input_profile_t profile{.layout = source->lane_layouts.front().layout,
        .input = source->input_profile};
    std::erase_if(profile.input.bindings, [&](const auto& binding) {
        return std::ranges::none_of(profile.layout.lanes, [&](const auto& lane) { return lane.id == binding.lane; });
    });
    if (defaults) profile.input = defaults->profile_for(profile.layout, profile.input);
    return commit_empty_chart(history, std::move(profile), source->source_family,
        source->divisions_per_standard_measure, source->beat_zero_audio_offset_seconds, timing);
}

auto apply_duplicate_chart_command(document_history_t& history,
                                   chart_id_t id) -> duplicate_chart_result_t {
    return apply_duplicate_charts_command(history, std::span{&id, 1});
}

auto apply_duplicate_charts_command(document_history_t& history,
    std::span<const chart_id_t> charts) -> duplicate_chart_result_t {
    if (auto invalid = check_chart_selection(history, charts)) return {.command = std::move(*invalid)};
    const auto ceiling = history_id_ceiling(history);
    if (ceiling == std::numeric_limits<std::uint64_t>::max())
        return {.command = document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "chart ID sequence is exhausted")};
    auto transaction = document_command_transaction_t(history);
    auto& proposed = transaction.document();
    if (!proposed.prepared_bms_programs.empty()) proposed.ensure_chart_order();
    auto next_id = ceiling + 1;
    std::optional<chart_id_t> first;
    for (const auto id : charts) {
        if (const auto* source = history.document.find_bms_program(id)) {
            const auto duplicate = duplicate_program(*source, next_id);
            if (!duplicate.value || !duplicate.ok())
                return {.command = transaction.reject(diagnostic_code_t::invalid_id,
                    "Scoped difficulty cannot be duplicated with fresh identities.")};
            if (!first) first = (*duplicate.value)->difficulty();
            if (!proposed.chart_order.empty()) proposed.chart_order.push_back((*duplicate.value)->difficulty());
            proposed.prepared_bms_programs.push_back(*duplicate.value);
            continue;
        }
        auto duplicate = *history.read_chart(id);
        chart_id_remap_t remap{.next = next_id};
        visit_chart_ids(duplicate, remap);
        if (remap.exhausted) return {.command = document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "chart ID sequence is exhausted")};
        next_id = remap.next;
        if (!first) first = duplicate.id;
        duplicate.metadata.name += " (copy)";
        if (!proposed.chart_order.empty()) proposed.chart_order.push_back(duplicate.id);
        proposed.charts.push_back(std::move(duplicate));
    }
    auto result = transaction.commit(charts.size() == 1 ? "Duplicate chart" : "Duplicate charts");
    const auto applied = result.applied;
    return {.command = std::move(result), .payload = {.chart = applied ? first : std::nullopt}};
}

auto apply_delete_chart_command(document_history_t& history,
                                chart_id_t id) -> document_command_result_t {
    return apply_delete_charts_command(history, std::span{&id, 1});
}

auto apply_delete_charts_command(document_history_t& history,
    std::span<const chart_id_t> charts) -> document_command_result_t {
    if (auto invalid = check_chart_selection(history, charts)) return std::move(*invalid);
    if (history.document.read_charts().size() == charts.size()) return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "cannot delete the last chart");
    auto transaction = document_command_transaction_t(history);
    auto& proposed = transaction.document();
    for (const auto id : charts) remove_memberships(proposed, id);
    std::erase_if(proposed.chart_order, [charts](auto id) { return std::ranges::contains(charts, id); });
    std::erase_if(proposed.charts, [charts](const auto& chart) { return std::ranges::contains(charts, chart.id); });
    std::erase_if(proposed.prepared_bms_programs, [charts](const auto& owner) {
        return owner && std::ranges::contains(charts, owner->difficulty());
    });
    return transaction.commit(charts.size() == 1 ? "Delete chart" : "Delete charts");
}

auto apply_reorder_chart_command(document_history_t& history,
                                 chart_id_t id, std::size_t destination) -> document_command_result_t {
    if (auto invalid = check_chart(history, id)) return std::move(*invalid);
    const auto charts = history.document.read_charts();
    if (destination >= charts.size()) return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "chart destination is out of range");
    auto transaction = document_command_transaction_t(history);
    auto& proposed = transaction.document();
    proposed.chart_order.clear();
    for (const auto* chart : charts) if (chart->id != id) proposed.chart_order.push_back(chart->id);
    proposed.chart_order.insert(proposed.chart_order.begin() + static_cast<std::ptrdiff_t>(destination), id);
    if (proposed.prepared_bms_programs.empty()) {
        std::ranges::sort(proposed.charts, {}, [&](const auto& chart) {
            return std::ranges::find(proposed.chart_order, chart.id) - proposed.chart_order.begin();
        });
    }
    return transaction.commit("Reorder charts");
}

auto apply_move_chart_group_command(document_history_t& history,
                                    chart_id_t id, std::optional<std::size_t> group) -> document_command_result_t {
    if (auto invalid = check_chart(history, id)) return std::move(*invalid);
    if (group && *group >= history.document.output_groups.size()) return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "output group is out of range");
    auto transaction = document_command_transaction_t(history);
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
auto output_destination_error(const output_group_t& group) -> std::string {
    const auto expected = group.family == format_family_t::stepmania ||
                                  group.family == format_family_t::native_project
                              ? output_destination_kind_t::file
                              : output_destination_kind_t::directory;
    if (group.destination_kind != expected)
        return expected == output_destination_kind_t::file
                   ? "This output format requires a song file destination."
                   : "This output format requires a folder destination.";
    if (!is_output_group_path_valid(group.relative_output_path))
        return "Output path must be a safe project-relative path.";
    if (expected == output_destination_kind_t::file &&
        group.relative_output_path.filename().empty())
        return "Output file destination needs a filename.";
    return {};
}
auto output_group_error(const song_document_t& doc, std::optional<std::size_t> index,
    const output_group_t& group) -> std::string {
    if (index && *index >= doc.output_groups.size()) return "Output group is no longer available.";
    if (group.name.empty() || group.name.find_first_not_of(" \t\r\n") == std::string::npos)
        return "Output group needs a name.";
    for (std::size_t i = 0; i < doc.output_groups.size(); ++i)
        if (i != index && doc.output_groups[i].name == group.name) return "Output group name is already used.";
    if (group.family < format_family_t::native_project || group.family > format_family_t::bmson) return "Output format is not supported.";
    if (group.existing_files < existing_files_policy_t::ask ||
        group.existing_files > existing_files_policy_t::keep)
        return "Existing-files policy is not supported.";
    if (const auto error = output_destination_error(group); !error.empty()) return error;
    if (group.family == format_family_t::stepmania) {
        if (!group.stepmania)
            return "StepMania export policy is required.";
        if (const auto policy = stepmania_write_options_t::from(*group.stepmania); !policy)
            return policy.error().message;
    }
    std::vector<chart_id_t> seen;
    for (const auto chart : group.charts) {
        const auto* member = doc.read_chart(chart);
        if (member == nullptr) return "Output group contains an unknown chart.";
        if (std::ranges::find(seen, chart) != seen.end()) return "Output group repeats a chart.";
        const auto compatibility = output_family_compatibility(*member, group.family);
        if (!compatibility.compatible)
            return "Chart \"" + member->metadata.name + "\" is incompatible: " +
                   std::string{compatibility.reason};
        seen.push_back(chart);
    }
    return {};
}
} // namespace

auto apply_set_output_group_command(document_history_t& history,
    std::optional<std::size_t> index, output_group_t group) -> document_command_result_t {
    if (const auto error = output_group_error(history.document, index, group); !error.empty())
        return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, error);
    auto transaction = document_command_transaction_t(history);
    auto& proposed = transaction.document();
    if (index) proposed.output_groups[*index] = std::move(group);
    else proposed.output_groups.push_back(std::move(group));
    return transaction.commit(index ? "Edit output group" : "Create output group");
}
auto apply_delete_output_group_command(document_history_t& history,
    std::size_t index) -> document_command_result_t {
    if (index >= history.document.output_groups.size()) return document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id, "Output group is no longer available.");
    auto transaction = document_command_transaction_t(history);
    auto& proposed = transaction.document();
    proposed.output_groups.erase(proposed.output_groups.begin() + static_cast<std::ptrdiff_t>(index));
    return transaction.commit("Delete output group");
}

} // namespace wormedit

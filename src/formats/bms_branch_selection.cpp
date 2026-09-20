#include <wormedit/formats/bms_branch_selection.h>

#include <algorithm>
#include <ranges>
#include <span>
#include <unordered_map>
#include <utility>

namespace wormedit {
namespace {

auto has_error(const auto& diagnostics) -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto add_error(bms_branch_selection_result_t& result, bms_branch_selection_diagnostic_code_t code,
               std::string message, std::string path) -> void {
    result.diagnostics.push_back(
        {diagnostic_severity_t::error, code, std::move(message), std::move(path)});
}

auto is_choice_control(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::random || kind == bms_control_kind_t::switch_block;
}

auto begins_branch_scope(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::random || kind == bms_control_kind_t::if_group ||
           kind == bms_control_kind_t::if_branch || kind == bms_control_kind_t::else_if_branch ||
           kind == bms_control_kind_t::else_branch || kind == bms_control_kind_t::switch_block ||
           kind == bms_control_kind_t::case_branch || kind == bms_control_kind_t::default_branch;
}

struct control_signature_t {
    bms_control_kind_t kind{bms_control_kind_t::root};
    std::optional<std::int64_t> value;
    std::size_t depth{};
    std::string original_text;
    std::string command_token;
    std::string raw_value;
    std::optional<std::string> close_text;

    auto operator==(const control_signature_t&) const -> bool = default;
};

struct control_inventory_t {
    std::vector<control_signature_t> signatures;
    std::vector<const bms_control_node_t*> choices;
    std::unordered_map<std::uint64_t, std::size_t> choice_indices_by_id;
};

auto control_inventory(const bms_control_node_t& root) -> control_inventory_t {
    control_inventory_t result;
    const auto visit = [&](const auto& self, const bms_control_node_t& node, std::size_t depth,
                           bool inside_branch) -> void {
        if (node.kind != bms_control_kind_t::raw_statement || inside_branch) {
            result.signatures.push_back(
                node.kind == bms_control_kind_t::raw_statement
                    ? control_signature_t{.kind = node.kind,
                                          .value = std::nullopt,
                                          .depth = depth,
                                          .original_text = {},
                                          .command_token = {},
                                          .raw_value = {},
                                          .close_text = std::nullopt}
                    : control_signature_t{node.kind, node.value, depth, node.original_text,
                                          node.command_token, node.raw_value, node.close_text});
            if (is_choice_control(node.kind)) {
                result.choice_indices_by_id.emplace(node.id.value, result.choices.size());
                result.choices.push_back(&node);
            }
        }
        const auto child_inside_branch = inside_branch || begins_branch_scope(node.kind);
        for (const auto& child : node.children) {
            self(self, child, depth + 1, child_inside_branch);
        }
    };
    visit(visit, root, 0, false);
    return result;
}

auto selected_profile_id(const chart_document_t& chart) -> std::optional<std::string> {
    if (!chart.lane_layouts.empty() && !chart.lane_layouts.front().layout.profile_id.empty()) {
        return chart.lane_layouts.front().layout.profile_id;
    }
    return std::nullopt;
}

auto has_unroundtrippable_editor_state(const song_document_t& song, const chart_document_t& chart)
    -> bool {
    const auto has_non_bms_group = [](const auto& groups) {
        return std::ranges::any_of(
            groups, [](const auto& group) { return group.family != format_family_t::bms; });
    };
    const auto note_has_unmapped_state =
        std::ranges::any_of(chart.measures, [](const auto& measure) {
            return std::ranges::any_of(measure.notes, [](const auto& note) {
                return !note.semantic_layers.empty() || note.label.has_value();
            });
        });
    return !song.midi_assets.empty() || !song.generator_recipes.empty() ||
           !chart.semantic_layers.empty() || note_has_unmapped_state || !chart.bookmarks.empty() ||
           !chart.sv_recipes.empty() || has_non_bms_group(song.metadata.format_groups) ||
           has_non_bms_group(chart.metadata.format_groups);
}

auto bms_identity(const resource_entry_t& resource)
    -> std::optional<std::pair<std::string, std::string>> {
    const auto identity = std::ranges::find_if(resource.target_identities, [](const auto& item) {
        return item.family == format_family_t::bms;
    });
    if (identity == resource.target_identities.end()) {
        return std::nullopt;
    }
    return std::pair{identity->namespace_name, identity->identifier};
}

void retain_runtime_asset_metadata(const song_document_t& original,
                                   song_document_t& rematerialized) {
    for (auto& resource : rematerialized.resources) {
        const auto identity = bms_identity(resource);
        if (!identity.has_value()) {
            continue;
        }
        const auto old_resource = std::ranges::find_if(
            original.resources, [&](const auto& item) { return bms_identity(item) == identity; });
        if (old_resource == original.resources.end()) {
            continue;
        }
        const auto old_audio =
            std::ranges::find(original.audio_assets, old_resource->asset, &audio_asset_t::id);
        const auto new_audio =
            std::ranges::find(rematerialized.audio_assets, resource.asset, &audio_asset_t::id);
        if (old_audio != original.audio_assets.end() &&
            new_audio != rematerialized.audio_assets.end()) {
            new_audio->source_path = old_audio->source_path;
            new_audio->stream = old_audio->stream;
            new_audio->waveform_cache_path = old_audio->waveform_cache_path;
            resource.roles = old_resource->roles;
            continue;
        }
        const auto old_visual =
            std::ranges::find(original.visual_assets, old_resource->asset, &visual_asset_t::id);
        const auto new_visual =
            std::ranges::find(rematerialized.visual_assets, resource.asset, &visual_asset_t::id);
        if (old_visual != original.visual_assets.end() &&
            new_visual != rematerialized.visual_assets.end()) {
            new_visual->source_path = old_visual->source_path;
            new_visual->kind = old_visual->kind;
            new_visual->width = old_visual->width;
            new_visual->height = old_visual->height;
            resource.roles = old_resource->roles;
        }
    }
}

} // namespace

auto bms_branch_selection_result_t::has_errors() const -> bool {
    return has_error(diagnostics) || has_error(write_diagnostics) || has_error(parse_diagnostics);
}

auto rematerialize_bms_branch_selection(const song_document_t& song,
                                        const bms_branch_selection_request_t& request)
    -> bms_branch_selection_result_t {
    bms_branch_selection_result_t result;
    if (song.charts.size() != 1) {
        add_error(result, bms_branch_selection_diagnostic_code_t::unsupported_song_shape,
                  "BMS branch rematerialization currently requires a one-chart source document",
                  "$.charts");
        return result;
    }

    const auto chart = request.chart.has_value()
                           ? std::ranges::find(song.charts, *request.chart, &chart_document_t::id)
                           : song.charts.begin();
    if (chart == song.charts.end()) {
        add_error(result, bms_branch_selection_diagnostic_code_t::missing_chart,
                  "requested chart ID does not exist", "$.charts");
        return result;
    }
    if (!chart->bms_control_tree.has_value()) {
        add_error(result, bms_branch_selection_diagnostic_code_t::missing_control_tree,
                  "the selected chart has no retained BMS control tree",
                  "$.charts[0].bms_control_tree");
        return result;
    }
    if (chart->source_family != format_family_t::bms) {
        add_error(result, bms_branch_selection_diagnostic_code_t::unsupported_song_shape,
                  "branch rematerialization requires a BMS-family source chart",
                  "$.charts[0].source_family");
        return result;
    }

    result.preview =
        preview_bms_branch_projection(*chart->bms_control_tree, chart->active_branch_selection,
                                      request.proposed, request.projection_limits);
    if (!result.preview) {
        add_error(result, bms_branch_selection_diagnostic_code_t::invalid_selection,
                  "the current or proposed branch selection does not resolve exactly",
                  "$.charts[0].active_branch_selection");
        return result;
    }
    if (has_unroundtrippable_editor_state(song, *chart)) {
        add_error(result, bms_branch_selection_diagnostic_code_t::unsupported_editor_state,
                  "branch rematerialization cannot yet preserve MIDI assets, generators, "
                  "bookmarks, note labels/layers, SV recipes, or non-BMS metadata groups",
                  "$.charts[0]");
        return result;
    }

    const auto profile_id = selected_profile_id(*chart);
    auto written = write_bms(song, {
                                       .chart = chart->id,
                                       .lane_profile_id = profile_id,
                                       .preserve_unchanged_source = false,
                                       .branch_policy = bms_branch_policy_t::preserve_tree,
                                       .output_encoding = std::nullopt,
                                       .max_slots_per_line = request.max_slots_per_line,
                                   });
    result.write_diagnostics = std::move(written.diagnostics);
    if (!written.bytes.has_value() || has_error(result.write_diagnostics)) {
        add_error(result, bms_branch_selection_diagnostic_code_t::write_failed,
                  "the current BMS document could not be serialized without changing policy", "$");
        return result;
    }
    if (written.bytes->size() > request.max_source_bytes) {
        add_error(result, bms_branch_selection_diagnostic_code_t::resource_limit,
                  "serialized BMS source exceeds the configured rematerialization byte limit", "$");
        return result;
    }

    const auto parse_options = [&](std::vector<bms_parse_options_t::branch_choice_t> choices) {
        return bms_parse_options_t{
            .encoding_override = std::nullopt,
            .file_extension = {},
            .max_source_bytes = request.max_source_bytes,
            .max_line_bytes = request.max_line_bytes,
            .max_nesting_depth = request.max_nesting_depth,
            .max_lines = request.max_lines,
            .max_chart_resolution = request.max_chart_resolution,
            .lane_profile_id = profile_id,
            .branch_choices = std::move(choices),
        };
    };
    const auto bytes = std::as_bytes(std::span{written.bytes->data(), written.bytes->size()});
    auto baseline = parse_bms_bytes(bytes, parse_options({}));
    if (!baseline.song.has_value() || baseline.has_errors()) {
        result.parse_diagnostics = std::move(baseline.diagnostics);
        add_error(result, bms_branch_selection_diagnostic_code_t::parse_failed,
                  "serialized BMS source could not be reparsed for control-tree verification", "$");
        return result;
    }
    const auto& baseline_chart = baseline.song->charts.front();
    if (!baseline_chart.bms_control_tree.has_value()) {
        add_error(result, bms_branch_selection_diagnostic_code_t::control_tree_changed,
                  "serialized BMS source no longer contains its retained control tree",
                  "$.charts[0].bms_control_tree");
        return result;
    }

    const auto original_controls = control_inventory(*chart->bms_control_tree);
    const auto baseline_controls = control_inventory(*baseline_chart.bms_control_tree);
    if (original_controls.signatures != baseline_controls.signatures ||
        original_controls.choices.size() != baseline_controls.choices.size()) {
        add_error(result, bms_branch_selection_diagnostic_code_t::control_tree_changed,
                  "BMS serialization changed the branch-control structure",
                  "$.charts[0].bms_control_tree");
        return result;
    }

    std::vector<bms_parse_options_t::branch_choice_t> choices;
    choices.reserve(request.proposed.size());
    for (const auto& selection : request.proposed) {
        const auto index = original_controls.choice_indices_by_id.find(selection.branch.value);
        if (index == original_controls.choice_indices_by_id.end() ||
            index->second >= baseline_controls.choices.size()) {
            add_error(result, bms_branch_selection_diagnostic_code_t::selection_mismatch,
                      "proposed branch selection cannot be mapped to the serialized control tree",
                      "$.charts[0].active_branch_selection");
            return result;
        }
        choices.push_back(
            {baseline_controls.choices[index->second]->source_order, selection.selected_value});
    }

    auto reparsed = parse_bms_bytes(bytes, parse_options(std::move(choices)));
    result.parse_diagnostics = std::move(reparsed.diagnostics);
    if (!reparsed.song.has_value() || has_error(result.parse_diagnostics)) {
        add_error(result, bms_branch_selection_diagnostic_code_t::parse_failed,
                  "serialized BMS source could not materialize the proposed branch selection", "$");
        return result;
    }
    auto& reparsed_chart = reparsed.song->charts.front();
    if (!reparsed_chart.bms_control_tree.has_value()) {
        add_error(result, bms_branch_selection_diagnostic_code_t::control_tree_changed,
                  "rematerialized BMS source no longer contains its retained control tree",
                  "$.charts[0].bms_control_tree");
        return result;
    }
    const auto reparsed_controls = control_inventory(*reparsed_chart.bms_control_tree);
    if (reparsed_controls.signatures != original_controls.signatures ||
        reparsed_controls.choices.size() != original_controls.choices.size()) {
        add_error(result, bms_branch_selection_diagnostic_code_t::control_tree_changed,
                  "rematerialization changed the branch-control structure",
                  "$.charts[0].bms_control_tree");
        return result;
    }

    std::unordered_map<std::uint64_t, std::int64_t> actual_choices;
    for (const auto& selection : reparsed_chart.active_branch_selection) {
        actual_choices.emplace(selection.branch.value, selection.selected_value);
    }
    if (actual_choices.size() != request.proposed.size()) {
        add_error(result, bms_branch_selection_diagnostic_code_t::selection_mismatch,
                  "rematerialized BMS choices differ from the complete proposed selection",
                  "$.charts[0].active_branch_selection");
        return result;
    }
    for (const auto& selection : request.proposed) {
        const auto original_index =
            original_controls.choice_indices_by_id.find(selection.branch.value)->second;
        const auto* rematerialized_control = reparsed_controls.choices[original_index];
        const auto actual = actual_choices.find(rematerialized_control->id.value);
        if (actual == actual_choices.end() || actual->second != selection.selected_value) {
            add_error(result, bms_branch_selection_diagnostic_code_t::selection_mismatch,
                      "rematerialized BMS choices differ from the proposed selection",
                      "$.charts[0].active_branch_selection");
            return result;
        }
    }
    if (reparsed_chart.lane_layouts.size() != chart->lane_layouts.size()) {
        add_error(result, bms_branch_selection_diagnostic_code_t::selection_mismatch,
                  "rematerialized BMS lane-profile state differs from the source chart",
                  "$.charts[0].lane_layouts");
        return result;
    }

    auto rematerialized = std::move(*reparsed.song);
    retain_runtime_asset_metadata(song, rematerialized);
    rematerialized.schema_version = song.schema_version;
    rematerialized.id = song.id;
    rematerialized.revision = song.revision;
    rematerialized.metadata.ordered_search_tags = song.metadata.ordered_search_tags;
    rematerialized.midi_assets = song.midi_assets;
    rematerialized.storyboards = song.storyboards;
    rematerialized.output_groups = song.output_groups;
    rematerialized.charts.front().id = chart->id;
    rematerialized.charts.front().metadata.name = chart->metadata.name;
    rematerialized.charts.front().metadata.author = chart->metadata.author;
    rematerialized.charts.front().metadata.rating = chart->metadata.rating;
    rematerialized.charts.front().beat_zero_audio_offset_seconds =
        chart->beat_zero_audio_offset_seconds;
    rematerialized.charts.front().input_profile = chart->input_profile;
    for (std::size_t index = 0;
         index < chart->measures.size() && index < rematerialized.charts.front().measures.size();
         ++index) {
        rematerialized.charts.front().measures[index].definition.meter =
            chart->measures[index].definition.meter;
    }
    for (std::size_t index = 0; index < chart->lane_layouts.size(); ++index) {
        rematerialized.charts.front().lane_layouts[index].id = chart->lane_layouts[index].id;
        rematerialized.charts.front().lane_layouts[index].layout =
            chart->lane_layouts[index].layout;
    }
    result.song = std::move(rematerialized);
    return result;
}

} // namespace wormedit

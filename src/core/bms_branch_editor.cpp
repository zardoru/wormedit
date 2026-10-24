#include <wormedit/core/bms_branch_editor.h>
#include <wormedit/core/bms_program_edit.h>

#include <algorithm>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>

namespace wormedit {
namespace {


auto publish_branch_edit(document_history_t& history, bms_branch_statement_edit_result_t edit,
                         std::string label) -> bms_branch_statement_command_result_t {
    bms_branch_statement_command_result_t result;
    result.command.revision = history.document.revision;
    result.payload.format_diagnostics = std::move(edit.diagnostics);
    if (!edit.song || edit.has_errors()) {
        return result;
    }

    auto transaction = document_command_transaction_t(history);
    transaction.adopt_document(std::move(*edit.song));
    result.command = transaction.commit(std::move(label));
    return result;
}

void add_block_error(document_command_result_t& result, std::string message) {
    result.diagnostics.push_back({.severity = diagnostic_severity_t::error,
                                  .code = diagnostic_code_t::invalid_id,
                                  .message = std::move(message)});
}

auto retained_bms_bytes(const song_document_t& song, const chart_document_t& chart)
    -> std::optional<std::vector<std::byte>> {
    auto written = write_bms(song, {.chart = chart.id,
                                    .preserve_unchanged_source = true,
                                    .branch_policy = bms_branch_policy_t::preserve_tree});
    if (!written.value || !written.ok()) return std::nullopt;
    std::vector<std::byte> bytes;
    bytes.reserve(written.value->size());
    for (const auto character : *written.value) {
        bytes.push_back(static_cast<std::byte>(character));
    }
    return bytes;
}

auto selected_profile(const chart_document_t& chart) -> std::optional<std::string> {
    if (chart.lane_layouts.empty() || chart.lane_layouts.front().layout.profile_id.empty()) {
        return std::nullopt;
    }
    return chart.lane_layouts.front().layout.profile_id;
}

auto parse_options(const chart_document_t& chart, std::span<const std::byte> bytes)
    -> bms_parse_options_t {
    bms_parse_options_t options;
    options.lane_profile_id = selected_profile(chart);
    const auto has_utf8_bom = bytes.size() >= 3 && bytes[0] == std::byte{0xef} &&
                              bytes[1] == std::byte{0xbb} && bytes[2] == std::byte{0xbf};
    if (!has_utf8_bom) {
        const auto group = std::ranges::find_if(chart.metadata.format_groups, [](const auto& item) {
            return item.family == format_family_t::bms;
        });
        if (group != chart.metadata.format_groups.end()) {
            const auto encoding = group->encoding.user_override.value_or(group->encoding.detected);
            if (encoding != text_encoding_t::unknown) options.encoding_override = encoding;
        }
    }
    return options;
}

auto root_statement_range(const chart_document_t& chart, branch_id_t first, branch_id_t last,
                          const bms_branch_block_create_command_t& request)
    -> std::optional<std::pair<std::size_t, std::size_t>> {
    if (!chart.bms_control_tree || chart.bms_control_tree->kind != bms_control_kind_t::root) {
        return std::nullopt;
    }
    const auto& nodes = chart.bms_control_tree->children;
    const auto first_node = std::ranges::find(nodes, first, &bms_control_node_t::id);
    const auto last_node = std::ranges::find(nodes, last, &bms_control_node_t::id);
    if (first_node == nodes.end() || last_node == nodes.end() || first_node > last_node ||
        first_node->kind != bms_control_kind_t::raw_statement ||
        last_node->kind != bms_control_kind_t::raw_statement ||
        first_node->source_order != request.first_source_order ||
        last_node->source_order != request.last_source_order ||
        first_node->source_span != request.first_source_span ||
        last_node->source_span != request.last_source_span ||
        first_node->original_text != request.first_original_text ||
        last_node->original_text != request.last_original_text) {
        return std::nullopt;
    }
    if (!std::ranges::all_of(std::ranges::subrange(first_node, std::next(last_node)),
                             [](const auto& node) {
                                 return node.kind == bms_control_kind_t::raw_statement;
                             })) {
        return std::nullopt;
    }
    return std::pair{first_node->source_span.byte_begin, last_node->source_span.byte_end};
}

auto line_ending_text(bms_line_ending_t ending) -> std::string_view {
    switch (ending) {
    case bms_line_ending_t::crlf: return "\r\n";
    case bms_line_ending_t::cr: return "\r";
    case bms_line_ending_t::lf: return "\n";
    case bms_line_ending_t::none: return "\n";
    }
    return "\n";
}

auto block_source_size(std::size_t leaves, std::string_view ending, bool add_separator)
    -> std::optional<std::size_t> {
    constexpr std::size_t maximum_lines = 262'144;
    constexpr std::size_t maximum_bytes = 16U * 1024U * 1024U;
    if (leaves == 0 || leaves > maximum_lines - 3) return std::nullopt;
    std::size_t size = std::string_view{"#RANDOM "}.size() + std::to_string(leaves).size() +
                       ending.size() + std::string_view{"#IF 1"}.size() + ending.size() +
                       std::string_view{"#ENDIF"}.size() + ending.size() +
                       std::string_view{"#ENDRANDOM"}.size() + ending.size();
    for (std::size_t index = 2; index <= leaves; ++index) {
        size += std::string_view{"#ELSEIF "}.size() + std::to_string(index).size() + ending.size();
    }
    if (add_separator) size += ending.size();
    if (size > maximum_bytes) return std::nullopt;
    return size;
}

auto create_block_source(const std::vector<std::byte>& source, std::size_t begin,
                         std::size_t end, std::size_t leaves, std::string_view ending)
    -> std::optional<std::vector<std::byte>> {
    if (begin > end || end > source.size()) return std::nullopt;
    const auto separated = end > begin && source[end - 1] != std::byte{'\n'} &&
                           source[end - 1] != std::byte{'\r'};
    const auto added_size = block_source_size(leaves, ending, separated);
    if (!added_size || source.size() > 16U * 1024U * 1024U - *added_size) return std::nullopt;
    std::string prefix = "#RANDOM " + std::to_string(leaves) + std::string(ending) + "#IF 1" +
                         std::string(ending);
    std::string suffix;
    for (std::size_t index = 2; index <= leaves; ++index) {
        suffix += "#ELSEIF " + std::to_string(index) + std::string(ending);
    }
    suffix += "#ENDIF" + std::string(ending) + "#ENDRANDOM" + std::string(ending);
    std::vector<std::byte> result;
    result.reserve(source.size() + *added_size);
    const auto append_text = [&](std::string_view text) {
        for (const auto character : text) result.push_back(static_cast<std::byte>(character));
    };
    result.insert(result.end(), source.begin(), source.begin() + static_cast<std::ptrdiff_t>(begin));
    append_text(prefix);
    result.insert(result.end(), source.begin() + static_cast<std::ptrdiff_t>(begin),
                  source.begin() + static_cast<std::ptrdiff_t>(end));
    if (separated) {
        append_text(ending);
    }
    append_text(suffix);
    result.insert(result.end(), source.begin() + static_cast<std::ptrdiff_t>(end), source.end());
    return result;
}

auto tree_node(const bms_control_node_t& root, branch_id_t id) -> const bms_control_node_t* {
    if (root.id == id) return &root;
    for (const auto& child : root.children) {
        if (const auto* found = tree_node(child, id)) return found;
    }
    return nullptr;
}

auto parse_candidate(std::span<const std::byte> bytes, const chart_document_t& chart,
                     std::vector<bms_parse_options_t::branch_choice_t> choices)
    -> std::optional<song_document_t> {
    auto options = parse_options(chart, bytes);
    options.branch_choices = std::move(choices);
    auto parsed = parse_bms_bytes(bytes, std::move(options));
    if (!parsed.value.song || !parsed.ok()) return std::nullopt;
    return std::move(*parsed.value.song);
}

auto remap_node_ids(const bms_control_node_t& baseline, bms_control_node_t& updated,
                    std::span<const std::byte> bytes, std::size_t begin, std::size_t end,
                    std::size_t prefix_size, std::size_t suffix_size, bool inserted)
    -> std::unordered_map<std::uint64_t, std::uint64_t>;
void remap_active_branch_ids(
    song_document_t& song, const std::unordered_map<std::uint64_t, std::uint64_t>& mapping);

auto identities_equal(const std::vector<target_resource_identity_t>& left,
                     const std::vector<target_resource_identity_t>& right) -> bool {
    return left == right;
}



auto remap_lanes(const chart_document_t& source, chart_document_t& target) -> bool {
    if (source.lane_layouts.size() != target.lane_layouts.size() ||
        source.bgm_source_lanes.size() != target.bgm_source_lanes.size()) return false;
    std::unordered_map<std::uint64_t, lane_id_t> lane_ids;
    std::unordered_map<std::uint64_t, object_id_t> bgm_lane_ids;
    for (std::size_t segment = 0; segment < source.lane_layouts.size(); ++segment) {
        const auto& old_layout = source.lane_layouts[segment].layout;
        auto& new_layout = target.lane_layouts[segment].layout;
        if (!old_layout.has_same_shape(new_layout)) return false;
        target.lane_layouts[segment].id = source.lane_layouts[segment].id;
        for (std::size_t lane = 0; lane < old_layout.lanes.size(); ++lane) {
            lane_ids.emplace(new_layout.lanes[lane].id.value, old_layout.lanes[lane].id);
            new_layout.lanes[lane].id = old_layout.lanes[lane].id;
        }
    }
    for (std::size_t lane = 0; lane < source.bgm_source_lanes.size(); ++lane) {
        bgm_lane_ids.emplace(target.bgm_source_lanes[lane].id.value,
                             source.bgm_source_lanes[lane].id);
        target.bgm_source_lanes[lane].id = source.bgm_source_lanes[lane].id;
    }
    for (auto& measure : target.measures) {
        for (auto& note : measure.notes) {
            if (const auto found = lane_ids.find(note.lane.value); found != lane_ids.end()) {
                note.lane = found->second;
            }
        }
        for (auto& event : measure.bgm_events) {
            if (const auto found = bgm_lane_ids.find(event.source_lane.value);
                found != bgm_lane_ids.end()) {
                event.source_lane = found->second;
            }
        }
    }
    target.input_profile = source.input_profile;
    return true;
}

auto unique_resource_match(const song_document_t& source, const resource_entry_t& resource)
    -> const resource_entry_t* {
    const auto matches = std::ranges::count_if(source.resources, [&](const auto& item) {
        return identities_equal(item.target_identities, resource.target_identities);
    });
    if (matches != 1) return nullptr;
    const auto found = std::ranges::find_if(source.resources, [&](const auto& item) {
        return identities_equal(item.target_identities, resource.target_identities);
    });
    return found == source.resources.end() ? nullptr : &*found;
}

auto add_resource_mapping(const song_document_t& source, const resource_entry_t& parsed,
                          const resource_entry_t& old,
                          std::unordered_map<std::uint64_t, resource_id_t>& resources,
                          std::unordered_map<std::uint64_t, asset_id_t>& audio,
                          std::unordered_map<std::uint64_t, asset_id_t>& visual) -> bool {
    resources.emplace(parsed.id.value, old.id);
    const auto audio_asset = source.find_audio_asset(old.asset);
    if (audio_asset != nullptr) {
        const auto [mapped, inserted] = audio.emplace(parsed.asset.value, old.asset);
        return inserted || mapped->second == old.asset;
    }
    const auto visual_asset = std::ranges::find(source.visual_assets, old.asset, &visual_asset_t::id);
    if (visual_asset == source.visual_assets.end()) return false;
    const auto [mapped, inserted] = visual.emplace(parsed.asset.value, old.asset);
    return inserted || mapped->second == old.asset;
}

auto build_resource_mappings(
    const song_document_t& source, const song_document_t& target,
    std::unordered_map<std::uint64_t, resource_id_t>& resources,
    std::unordered_map<std::uint64_t, asset_id_t>& audio,
    std::unordered_map<std::uint64_t, asset_id_t>& visual) -> bool {
    for (const auto& parsed : target.resources) {
        const auto* old = unique_resource_match(source, parsed);
        if (old == nullptr || !add_resource_mapping(source, parsed, *old, resources, audio, visual)) {
            return false;
        }
    }
    return true;
}

void restore_resource_fields(const song_document_t& source, song_document_t& target) {
    for (auto& resource : target.resources) {
        const auto* old = unique_resource_match(source, resource);
        if (old == nullptr) continue;
        resource.id = old->id;
        resource.name = old->name;
        resource.asset = old->asset;
        resource.roles = old->roles;
        resource.missing = old->missing;
        resource.sound_kind = old->sound_kind;
        resource.tags = old->tags;
    }
}

auto restore_audio_assets(const song_document_t& source, song_document_t& target,
                          const std::unordered_map<std::uint64_t, asset_id_t>& mapping) -> bool {
    for (auto& asset : target.audio_assets) {
        const auto mapped = mapping.find(asset.id.value);
        if (mapped == mapping.end()) return false;
        const auto old = source.find_audio_asset(mapped->second);
        if (old == nullptr) return false;
        asset.id = old->id;
        asset.source_path = old->source_path;
        asset.stream = old->stream;
        asset.waveform_cache_path = old->waveform_cache_path;
    }
    return true;
}

auto restore_visual_assets(const song_document_t& source, song_document_t& target,
                           const std::unordered_map<std::uint64_t, asset_id_t>& mapping) -> bool {
    for (auto& asset : target.visual_assets) {
        const auto mapped = mapping.find(asset.id.value);
        if (mapped == mapping.end()) return false;
        const auto old = std::ranges::find(source.visual_assets, mapped->second, &visual_asset_t::id);
        if (old == source.visual_assets.end()) return false;
        asset.id = old->id;
        asset.source_path = old->source_path;
        asset.kind = old->kind;
        asset.width = old->width;
        asset.height = old->height;
    }
    return true;
}

auto remap_note_resources(measure_chunk_t& measure,
                          const std::unordered_map<std::uint64_t, resource_id_t>& mapping) -> bool {
    for (auto& note : measure.notes) {
        if (!note.sound) continue;
        const auto found = mapping.find(note.sound->value);
        if (found == mapping.end()) return false;
        note.sound = found->second;
    }
    return true;
}

auto remap_bgm_resources(measure_chunk_t& measure,
                         const std::unordered_map<std::uint64_t, resource_id_t>& mapping) -> bool {
    for (auto& event : measure.bgm_events) {
        if (!event.sound) continue;
        const auto found = mapping.find(event.sound->value);
        if (found == mapping.end()) return false;
        event.sound = found->second;
    }
    return true;
}

auto remap_chart_resources(chart_document_t& chart,
                           const std::unordered_map<std::uint64_t, resource_id_t>& mapping) -> bool {
    for (auto& measure : chart.measures) {
        if (!remap_note_resources(measure, mapping) || !remap_bgm_resources(measure, mapping)) {
            return false;
        }
    }
    for (auto& event : chart.bga_events) {
        const auto found = mapping.find(event.resource.value);
        if (found == mapping.end()) return false;
        event.resource = found->second;
    }
    return true;
}

auto remap_event_resources(song_document_t& target,
                           const std::unordered_map<std::uint64_t, resource_id_t>& mapping) -> bool {
    for (auto& chart : target.charts) {
        if (!remap_chart_resources(chart, mapping)) return false;
    }
    return true;
}

auto remap_resources(const song_document_t& source, song_document_t& target) -> bool {
    if (source.resources.size() != target.resources.size() ||
        source.audio_assets.size() != target.audio_assets.size() ||
        source.visual_assets.size() != target.visual_assets.size()) return false;
    std::unordered_map<std::uint64_t, resource_id_t> resource_ids;
    std::unordered_map<std::uint64_t, asset_id_t> audio_ids;
    std::unordered_map<std::uint64_t, asset_id_t> visual_ids;
    if (!build_resource_mappings(source, target, resource_ids, audio_ids, visual_ids)) return false;
    restore_resource_fields(source, target);
    if (!restore_audio_assets(source, target, audio_ids) ||
        !restore_visual_assets(source, target, visual_ids)) return false;
    if (!remap_event_resources(target, resource_ids)) return false;
    return true;
}

auto preserve_supported_editor_state(const song_document_t& source, const chart_document_t& chart,
                                     song_document_t& target) -> bool {
    if (target.charts.size() != 1 || target.charts.front().measures.size() != chart.measures.size() ||
        !remap_lanes(chart, target.charts.front()) || !remap_resources(source, target)) return false;
    target.id = source.id;
    target.revision = source.revision;
    target.schema_version = source.schema_version;
    target.metadata.ordered_search_tags = source.metadata.ordered_search_tags;
    target.output_groups = source.output_groups;
    target.midi_assets = source.midi_assets;
    target.storyboards = source.storyboards;
    auto& target_chart = target.charts.front();
    target_chart.id = chart.id;
    target_chart.metadata.name = chart.metadata.name;
    target_chart.metadata.author = chart.metadata.author;
    target_chart.metadata.rating = chart.metadata.rating;
    target_chart.beat_zero_audio_offset_seconds = chart.beat_zero_audio_offset_seconds;
    target_chart.input_profile = chart.input_profile;
    for (std::size_t index = 0; index < chart.measures.size() && index < target_chart.measures.size();
         ++index) {
        target_chart.measures[index].definition.meter = chart.measures[index].definition.meter;
    }
    for (std::size_t index = 0; index < chart.lane_layouts.size() &&
                                index < target_chart.lane_layouts.size(); ++index) {
        target_chart.lane_layouts[index].id = chart.lane_layouts[index].id;
        target_chart.lane_layouts[index].layout = chart.lane_layouts[index].layout;
    }
    return true;
}

auto has_unpreservable_state(const song_document_t& song, const chart_document_t& chart) -> bool {
    const auto other_format = [](const auto& groups) {
        return std::ranges::any_of(groups, [](const auto& item) {
            return item.family != format_family_t::bms;
        });
    };
    return !song.generator_recipes.empty() || !song.midi_assets.empty() ||
           !chart.sound_channels.empty() ||
           !chart.semantic_layers.empty() || !chart.bookmarks.empty() || !chart.sv_recipes.empty() ||
           other_format(song.metadata.format_groups) || other_format(chart.metadata.format_groups) ||
           std::ranges::any_of(chart.measures, [](const auto& measure) {
               return std::ranges::any_of(measure.notes, [](const auto& note) {
                   return !note.semantic_layers.empty() || note.label.has_value();
               });
           });
}

auto publish_reparsed(document_history_t& history, const chart_document_t& old_chart,
                      std::vector<std::byte> bytes,
                      std::vector<bms_parse_options_t::branch_choice_t> choices,
                      const bms_control_node_t& baseline_tree, std::size_t begin,
                      std::size_t end, std::size_t prefix_size, std::size_t suffix_size,
                      bool inserted, std::string label) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    auto parsed = parse_candidate(bytes, old_chart, std::move(choices));
    if (!parsed || parsed->charts.size() != 1) {
        add_block_error(result, "BMS branch block source could not be reparsed safely");
        return result;
    }
    if (!parsed->charts.front().bms_control_tree) {
        add_block_error(result, "reparsed BMS source lost its retained control tree");
        return result;
    }
    const auto mapping = remap_node_ids(baseline_tree, *parsed->charts.front().bms_control_tree,
                                        bytes, begin, end, prefix_size, suffix_size, inserted);
    remap_active_branch_ids(*parsed, mapping);
    if (!preserve_supported_editor_state(history.document, old_chart, *parsed)) {
        add_block_error(result,
                        "BMS branch edit could not preserve the current lane, resource, and asset graph");
        return result;
    }
    auto transaction = document_command_transaction_t(history);
    transaction.adopt_document(std::move(*parsed));
    result = transaction.commit(std::move(label));
    return result;
}

auto count_line_starts(std::span<const std::byte> bytes, std::size_t offset) -> std::size_t {
    offset = std::min(offset, bytes.size());
    std::size_t lines{};
    for (std::size_t index = 0; index < offset; ++index) {
        const auto value = std::to_integer<unsigned char>(bytes[index]);
        if (value == '\r') {
            ++lines;
            if (index + 1 < offset && bytes[index + 1] == std::byte{'\n'}) ++index;
        } else if (value == '\n') {
            ++lines;
        }
    }
    return lines;
}

auto mutable_node_at_identity(bms_control_node_t& root, std::size_t order,
                              bms_control_kind_t kind, std::string_view text)
    -> bms_control_node_t* {
    if (root.source_order == order && root.kind == kind && root.original_text == text) return &root;
    for (auto& child : root.children) {
        if (auto* found = mutable_node_at_identity(child, order, kind, text)) return found;
    }
    return nullptr;
}

void collect_nodes(const bms_control_node_t& root,
                   std::vector<const bms_control_node_t*>& nodes) {
    nodes.push_back(&root);
    for (const auto& child : root.children) collect_nodes(child, nodes);
}

auto node_key(bms_control_kind_t kind, std::string_view text) -> std::string {
    std::string key(1, static_cast<char>(kind));
    key.append(text);
    return key;
}

void assign_unmatched_baseline_ids(
    bms_control_node_t& node, std::uint64_t& maximum_id,
    const std::unordered_set<const bms_control_node_t*>& matched) {
    if (!matched.contains(&node)) node.id = branch_id_t{++maximum_id};
    for (auto& child : node.children) assign_unmatched_baseline_ids(child, maximum_id, matched);
}

auto restore_baseline_node_ids(const bms_control_node_t& original,
                               bms_control_node_t& baseline) -> void {
    std::vector<const bms_control_node_t*> old_nodes;
    std::vector<const bms_control_node_t*> new_const;
    collect_nodes(original, old_nodes);
    collect_nodes(baseline, new_const);
    auto maximum_id = std::uint64_t{};
    for (const auto* node : old_nodes) maximum_id = std::max(maximum_id, node->id.value);
    for (const auto* node : new_const) maximum_id = std::max(maximum_id, node->id.value);
    std::unordered_map<std::string, std::vector<std::size_t>> positions;
    for (std::size_t index = 0; index < new_const.size(); ++index) {
        positions[node_key(new_const[index]->kind, new_const[index]->original_text)].push_back(index);
    }
    std::unordered_set<const bms_control_node_t*> matched;
    if (original.kind == bms_control_kind_t::root && baseline.kind == bms_control_kind_t::root) {
        matched.insert(&baseline);
        baseline.id = original.id;
    }
    auto before = new_const.size();
    for (auto old = old_nodes.rbegin(); old != old_nodes.rend(); ++old) {
        if ((*old)->kind == bms_control_kind_t::root) continue;
        const auto found = positions.find(node_key((*old)->kind, (*old)->original_text));
        if (found == positions.end()) continue;
        const auto match = std::lower_bound(found->second.begin(), found->second.end(), before);
        if (match == found->second.begin()) continue;
        const auto index = *std::prev(match);
        auto* node = mutable_node_at_identity(baseline, new_const[index]->source_order,
                                              new_const[index]->kind,
                                              new_const[index]->original_text);
        if (node == nullptr) continue;
        matched.insert(node);
        node->id = (*old)->id;
        before = index;
    }
    assign_unmatched_baseline_ids(baseline, maximum_id, matched);
}

auto is_all_selected_branches_present(const chart_document_t& chart) -> bool {
    if (!chart.bms_control_tree) return false;
    return std::ranges::all_of(chart.active_branch_selection, [&](const auto& selection) {
        return tree_node(*chart.bms_control_tree, selection.branch) != nullptr;
    });
}

auto maximum_branch_id(const bms_control_node_t& root) -> std::uint64_t {
    auto maximum = root.id.value;
    for (const auto& child : root.children) maximum = std::max(maximum, maximum_branch_id(child));
    return maximum;
}

auto remap_existing_node(const bms_control_node_t& old, bms_control_node_t& updated,
                         std::span<const std::byte> bytes, std::size_t begin, std::size_t end,
                         std::size_t prefix_size, std::size_t suffix_size, bool inserted,
                         std::unordered_map<std::uint64_t, std::uint64_t>& mapping,
                         std::unordered_set<bms_control_node_t*>& matched) -> void {
    auto offset = old.source_span.byte_begin;
    if (inserted) {
        if (offset >= begin) offset += prefix_size;
        if (old.source_span.byte_begin >= end) offset += suffix_size;
    } else {
        if (offset >= begin && offset < end) return;
        if (offset >= end) offset -= end - begin;
    }
    const auto order = count_line_starts(bytes, offset);
    auto* target = mutable_node_at_identity(updated, order, old.kind, old.original_text);
    if (target == nullptr) return;
    mapping.emplace(target->id.value, old.id.value);
    matched.insert(target);
    target->id = old.id;
}

void assign_new_node_ids(bms_control_node_t& node, std::uint64_t& maximum,
                         std::unordered_map<std::uint64_t, std::uint64_t>& mapping,
                         const std::unordered_set<bms_control_node_t*>& matched) {
    if (!matched.contains(&node)) {
        const auto old_id = node.id.value;
        ++maximum;
        mapping.emplace(old_id, maximum);
        node.id = branch_id_t{maximum};
    }
    for (auto& child : node.children) assign_new_node_ids(child, maximum, mapping, matched);
}

auto remap_node_ids(const bms_control_node_t& baseline, bms_control_node_t& updated,
                    std::span<const std::byte> bytes, std::size_t begin, std::size_t end,
                    std::size_t prefix_size, std::size_t suffix_size, bool inserted)
    -> std::unordered_map<std::uint64_t, std::uint64_t> {
    std::vector<const bms_control_node_t*> old_nodes;
    collect_nodes(baseline, old_nodes);
    auto maximum = std::max(maximum_branch_id(baseline), maximum_branch_id(updated));
    std::unordered_map<std::uint64_t, std::uint64_t> mapping;
    std::unordered_set<bms_control_node_t*> matched;
    for (const auto* old : old_nodes) {
        remap_existing_node(*old, updated, bytes, begin, end, prefix_size, suffix_size, inserted,
                            mapping, matched);
    }
    assign_new_node_ids(updated, maximum, mapping, matched);
    return mapping;
}

void remap_active_branch_ids(song_document_t& song,
                             const std::unordered_map<std::uint64_t, std::uint64_t>& mapping) {
    if (song.charts.empty()) return;
    auto& chart = song.charts.front();
    for (auto& selection : chart.active_branch_selection) {
        if (const auto found = mapping.find(selection.branch.value); found != mapping.end()) {
            selection.branch = branch_id_t{found->second};
        }
    }
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) {
            if (!note.branch) continue;
            if (const auto found = mapping.find(note.branch->value); found != mapping.end()) {
                note.branch = branch_id_t{found->second};
            }
        }
        for (auto& event : measure.bgm_events) {
            if (!event.branch) continue;
            if (const auto found = mapping.find(event.branch->value); found != mapping.end()) {
                event.branch = branch_id_t{found->second};
            }
        }
    }
}

auto choice_nodes(const bms_control_node_t& root, std::vector<const bms_control_node_t*>& nodes)
    -> void {
    if (root.kind == bms_control_kind_t::random || root.kind == bms_control_kind_t::switch_block) {
        nodes.push_back(&root);
    }
    for (const auto& child : root.children) choice_nodes(child, nodes);
}

auto choices_for_insert(const chart_document_t& chart, const bms_control_node_t& candidate_tree,
                        std::span<const std::byte> source, std::size_t begin, std::size_t end,
                        std::size_t prefix_size, std::size_t suffix_size,
                        std::size_t new_random_order) -> std::vector<bms_parse_options_t::branch_choice_t> {
    std::vector<bms_parse_options_t::branch_choice_t> choices;
    std::vector<const bms_control_node_t*> old_nodes;
    std::vector<const bms_control_node_t*> new_nodes;
    if (chart.bms_control_tree) choice_nodes(*chart.bms_control_tree, old_nodes);
    choice_nodes(candidate_tree, new_nodes);
    for (const auto& selection : chart.active_branch_selection) {
        const auto old = std::ranges::find(old_nodes, selection.branch, [](const auto* node) {
            return node->id;
        });
        if (old == old_nodes.end()) continue;
        const auto* old_node = *old;
        auto offset = old_node->source_span.byte_begin;
        if (offset >= begin) offset += prefix_size;
        if (offset >= end + prefix_size) offset += suffix_size;
        const auto order = count_line_starts(source, offset);
        const auto found = std::ranges::find(new_nodes, order, [](const auto* node) {
            return node->source_order;
        });
        if (found != new_nodes.end() && (*found)->kind == old_node->kind &&
            (*found)->original_text == old_node->original_text) {
            choices.push_back({order, selection.selected_value});
        }
    }
    choices.push_back({new_random_order, 1});
    return choices;
}

auto choices_for_remove(const chart_document_t& chart, const bms_control_node_t& candidate_tree,
                        std::span<const std::byte> source, std::size_t begin,
                        std::size_t end) -> std::vector<bms_parse_options_t::branch_choice_t> {
    std::vector<bms_parse_options_t::branch_choice_t> choices;
    std::vector<const bms_control_node_t*> old_nodes;
    std::vector<const bms_control_node_t*> new_nodes;
    if (chart.bms_control_tree) choice_nodes(*chart.bms_control_tree, old_nodes);
    choice_nodes(candidate_tree, new_nodes);
    for (const auto& selection : chart.active_branch_selection) {
        const auto old = std::ranges::find(old_nodes, selection.branch, [](const auto* node) {
            return node->id;
        });
        if (old == old_nodes.end()) continue;
        const auto* old_node = *old;
        const auto offset = old_node->source_span.byte_begin;
        if (offset >= begin && offset < end) continue;
        const auto adjusted = offset >= end ? offset - (end - begin) : offset;
        const auto order = count_line_starts(source, adjusted);
        const auto found = std::ranges::find(new_nodes, order, [](const auto* node) {
            return node->source_order;
        });
        if (found != new_nodes.end() && (*found)->kind == old_node->kind &&
            (*found)->original_text == old_node->original_text) {
            choices.push_back({order, selection.selected_value});
        }
    }
    return choices;
}

auto preserve_guard(document_command_result_t& result, const song_document_t& song,
                    const chart_document_t& chart) -> bool {
    if (song.charts.size() != 1 || chart.source_family != format_family_t::bms ||
        !chart.bms_control_tree || has_unpreservable_state(song, chart) ||
        !song.output_groups.empty() || !song.storyboards.empty()) {
        add_block_error(result, "BMS block edits require one round-trippable BMS chart");
        return false;
    }
    return true;
}

auto make_branch_block_source(const chart_document_t& chart, std::vector<std::byte>& bytes,
                              std::size_t begin, std::size_t end, std::size_t leaves,
                              std::size_t& prefix_size, std::size_t& suffix_size)
    -> bool {
    const auto first_node = std::ranges::find_if(
        chart.bms_control_tree->children,
        [begin](const auto& node) { return node.source_span.byte_begin == begin; });
    if (first_node == chart.bms_control_tree->children.end()) return false;
    const auto ending = line_ending_text(first_node->line_ending);
    const auto prefix = "#RANDOM " + std::to_string(leaves) + std::string(ending) + "#IF 1" +
                        std::string(ending);
    prefix_size = prefix.size();
    suffix_size = 0;
    auto inserted = create_block_source(bytes, begin, end, leaves, ending);
    if (!inserted) return false;
    suffix_size = inserted->size() - bytes.size() - prefix_size;
    bytes = std::move(*inserted);
    return true;
}

auto root_range_by_ids(const chart_document_t& chart, branch_id_t first, branch_id_t last,
                       std::string_view first_text, std::string_view last_text)
    -> std::optional<std::pair<std::size_t, std::size_t>> {
    if (!chart.bms_control_tree) return std::nullopt;
    const auto& nodes = chart.bms_control_tree->children;
    const auto first_node = std::ranges::find(nodes, first, &bms_control_node_t::id);
    const auto last_node = std::ranges::find(nodes, last, &bms_control_node_t::id);
    if (first_node == nodes.end() || last_node == nodes.end() || first_node > last_node ||
        first_node->kind != bms_control_kind_t::raw_statement ||
        last_node->kind != bms_control_kind_t::raw_statement ||
        first_node->original_text != first_text || last_node->original_text != last_text ||
        !std::ranges::all_of(std::ranges::subrange(first_node, std::next(last_node)),
                             [](const auto& node) {
                                 return node.kind == bms_control_kind_t::raw_statement;
                             })) {
        return std::nullopt;
    }
    return std::pair{first_node->source_span.byte_begin, last_node->source_span.byte_end};
}

auto parse_unselected(std::span<const std::byte> bytes, const chart_document_t& chart)
    -> std::optional<song_document_t> {
    return parse_candidate(bytes, chart, {});
}

auto is_block_leaves_empty(const bms_control_node_t& random) -> bool {
    if (random.kind != bms_control_kind_t::random || random.children.size() != 1 ||
        !random.close_source_span || !random.close_text) return false;
    const auto& group = random.children.front();
    if (group.kind != bms_control_kind_t::if_group || group.children.empty() ||
        group.children.front().kind != bms_control_kind_t::if_branch) return false;
    return std::ranges::all_of(group.children, [](const auto& branch) {
        const auto leaf = branch.kind == bms_control_kind_t::if_branch ||
                          branch.kind == bms_control_kind_t::else_if_branch ||
                          branch.kind == bms_control_kind_t::else_branch;
        return leaf && branch.children.empty();
    });
}

auto create_block(document_history_t& history, const chart_document_t& chart,
                  const bms_branch_block_create_command_t& request)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    const auto old_range = root_statement_range(chart, request.first_statement,
                                                request.last_statement, request);
    if (!old_range) {
        add_block_error(result, "selected BMS statements are stale or overlap a branch block");
        return result;
    }
    auto serialized = retained_bms_bytes(history.document, chart);
    if (!serialized) {
        add_block_error(result, "current BMS document cannot be serialized as a retained branch tree");
        return result;
    }
    auto baseline = parse_unselected(*serialized, chart);
    if (!baseline || baseline->charts.size() != 1) {
        add_block_error(result, "current BMS source could not be reparsed before block creation");
        return result;
    }
    auto& baseline_chart = baseline->charts.front();
    if (!chart.bms_control_tree || !baseline_chart.bms_control_tree) {
        add_block_error(result, "serialized BMS source lost its retained control tree");
        return result;
    }
    (void)restore_baseline_node_ids(*chart.bms_control_tree, *baseline_chart.bms_control_tree);
    baseline_chart.active_branch_selection = chart.active_branch_selection;
    if (!is_all_selected_branches_present(baseline_chart)) {
        add_block_error(result, "serialized BMS source no longer contains an existing branch choice");
        return result;
    }
    const auto range = root_range_by_ids(baseline_chart, request.first_statement,
                                         request.last_statement, request.first_original_text,
                                         request.last_original_text);
    if (!range) {
        add_block_error(result, "serialized BMS source no longer contains the selected root statements");
        return result;
    }
    std::size_t prefix_size{};
    std::size_t suffix_size{};
    if (!make_branch_block_source(baseline_chart, *serialized, range->first, range->second,
                                  request.leaf_count, prefix_size, suffix_size)) {
        add_block_error(result, "selected BMS source range cannot be wrapped safely");
        return result;
    }
    auto unselected = parse_unselected(*serialized, baseline_chart);
    if (!unselected || unselected->charts.empty() ||
        !unselected->charts.front().bms_control_tree) {
        add_block_error(result, "created BMS branch block failed structural validation");
        return result;
    }
    const auto choices = choices_for_insert(
        baseline_chart, *unselected->charts.front().bms_control_tree, *serialized, range->first,
        range->second, prefix_size, suffix_size, count_line_starts(*serialized, range->first));
    return publish_reparsed(history, chart, std::move(*serialized), choices,
                             *baseline_chart.bms_control_tree, range->first, range->second,
                             prefix_size, suffix_size, true, "Create BMS branch block");
}

auto remove_block(document_history_t& history, const chart_document_t& chart,
                  const bms_branch_block_remove_command_t& request)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    const auto* old_node = chart.bms_control_tree ? tree_node(*chart.bms_control_tree,
                                                              request.random_block) : nullptr;
    if (old_node == nullptr || old_node->source_order != request.expected_source_order ||
        old_node->source_span != request.expected_source_span ||
        old_node->original_text != request.expected_original_text ||
        !is_block_leaves_empty(*old_node)) {
        add_block_error(result, "BMS branch block is stale, nonempty, or not removable");
        return result;
    }
    auto serialized = retained_bms_bytes(history.document, chart);
    if (!serialized) {
        add_block_error(result, "current BMS document cannot be serialized as a retained branch tree");
        return result;
    }
    auto baseline = parse_unselected(*serialized, chart);
    if (!baseline || baseline->charts.size() != 1 ||
        !baseline->charts.front().bms_control_tree) {
        add_block_error(result, "current BMS source could not be reparsed before block removal");
        return result;
    }
    auto& baseline_chart = baseline->charts.front();
    if (!chart.bms_control_tree) {
        add_block_error(result, "BMS source has no retained control tree");
        return result;
    }
    (void)restore_baseline_node_ids(*chart.bms_control_tree, *baseline_chart.bms_control_tree);
    baseline_chart.active_branch_selection = chart.active_branch_selection;
    if (!is_all_selected_branches_present(baseline_chart)) {
        add_block_error(result, "serialized BMS source no longer contains an existing branch choice");
        return result;
    }
    const auto* block = tree_node(*baseline_chart.bms_control_tree, request.random_block);
    if (block == nullptr || !is_block_leaves_empty(*block) || !block->close_source_span) {
        add_block_error(result, "serialized BMS branch block is no longer empty and removable");
        return result;
    }
    const auto begin = block->source_span.byte_begin;
    const auto end = block->close_source_span->byte_end;
    if (begin > end || end > serialized->size()) {
        add_block_error(result, "BMS branch block source span is invalid");
        return result;
    }
    serialized->erase(serialized->begin() + static_cast<std::ptrdiff_t>(begin),
                      serialized->begin() + static_cast<std::ptrdiff_t>(end));
    auto unselected = parse_unselected(*serialized, baseline_chart);
    if (!unselected || unselected->charts.empty() ||
        !unselected->charts.front().bms_control_tree) {
        add_block_error(result, "BMS source failed structural validation after block removal");
        return result;
    }
    const auto choices = choices_for_remove(
        baseline_chart, *unselected->charts.front().bms_control_tree, *serialized, begin, end);
    return publish_reparsed(history, chart, std::move(*serialized), choices,
                             *baseline_chart.bms_control_tree, begin, end, 0, 0, false,
                             "Remove empty BMS branch block");
}

struct leaf_swap_t {
    const bms_control_node_t* block{};
    const bms_control_node_t* upper{};
    const bms_control_node_t* lower{};
    std::size_t upper_body_begin{};
    std::size_t lower_body_begin{};
    std::size_t lower_body_end{};
};

auto is_movable_leaf(const bms_control_node_t& leaf) -> bool {
    return (leaf.kind == bms_control_kind_t::if_branch ||
            leaf.kind == bms_control_kind_t::else_if_branch) &&
           std::ranges::all_of(leaf.children, [](const auto& child) {
               return child.kind == bms_control_kind_t::raw_statement;
           });
}

// The one if-group of a canonical block, or null when the block has another shape.
auto leaf_group(const bms_control_node_t& block) -> const bms_control_node_t* {
    if (block.kind != bms_control_kind_t::random || block.children.size() != 1) return nullptr;
    const auto& group = block.children.front();
    if (group.kind != bms_control_kind_t::if_group || !group.close_source_span) return nullptr;
    return &group;
}

auto find_leaf_swap(const bms_control_node_t& block, branch_id_t leaf_id,
                    bms_branch_leaf_move_direction_t direction,
                    document_command_result_t& result) -> std::optional<leaf_swap_t> {
    const auto* group = leaf_group(block);
    if (group == nullptr) {
        add_block_error(result, "BMS branch leaf is stale or its block has an unsupported shape");
        return std::nullopt;
    }
    const auto leaf = std::ranges::find(group->children, leaf_id, &bms_control_node_t::id);
    if (leaf == group->children.end()) {
        add_block_error(result, "BMS branch leaf is stale or its block has an unsupported shape");
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(leaf - group->children.begin());
    const auto up = direction == bms_branch_leaf_move_direction_t::up;
    if (up ? index == 0 : index + 1 >= group->children.size()) {
        add_block_error(result, "BMS branch leaf is already at the edge of its block");
        return std::nullopt;
    }
    const auto& upper = group->children[up ? index - 1 : index];
    const auto& lower = group->children[up ? index : index + 1];
    if (!is_movable_leaf(upper) || !is_movable_leaf(lower)) {
        add_block_error(result, "only plain #IF and #ELSEIF leaves without nested blocks can be reordered");
        return std::nullopt;
    }
    const auto next = std::next(group->children.begin(), static_cast<std::ptrdiff_t>(up ? index + 1 : index + 2));
    const auto lower_end = next == group->children.end() ? group->close_source_span->byte_begin
                                                           : next->source_span.byte_begin;
    return leaf_swap_t{&block, &upper, &lower, upper.source_span.byte_end,
                       lower.source_span.byte_end, lower_end};
}

auto swapped_region(const std::vector<std::byte>& bytes, const leaf_swap_t& swap)
    -> std::vector<std::byte> {
    const auto at = [&](std::size_t offset) { return bytes.begin() + static_cast<std::ptrdiff_t>(offset); };
    const auto header_begin = swap.lower->source_span.byte_begin;
    std::vector<std::byte> region;
    region.insert(region.end(), at(swap.lower_body_begin), at(swap.lower_body_end));
    region.insert(region.end(), at(header_begin), at(swap.lower_body_begin));
    region.insert(region.end(), at(swap.upper_body_begin), at(header_begin));
    return region;
}

auto leaf_move_choices(const chart_document_t& baseline_chart, const bms_control_node_t& candidate_tree,
                       std::span<const std::byte> bytes, const leaf_swap_t& swap, std::size_t begin,
                       std::size_t end) -> std::vector<bms_parse_options_t::branch_choice_t> {
    auto choices = choices_for_insert(baseline_chart, candidate_tree, bytes, begin, end, 0, 0,
                                      count_line_starts(bytes, begin));
    choices.pop_back();
    const auto order = count_line_starts(bytes, swap.block->source_span.byte_begin);
    const auto upper = swap.upper->value.value_or(0);
    const auto lower = swap.lower->value.value_or(0);
    for (auto& choice : choices) {
        if (choice.control_source_order != order) continue;
        if (choice.selected_value == upper) choice.selected_value = lower;
        else if (choice.selected_value == lower) choice.selected_value = upper;
    }
    return choices;
}

auto leaf_move(document_history_t& history, const chart_document_t& chart,
               const bms_branch_leaf_move_command_t& request) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    const auto* old_block = chart.bms_control_tree ? tree_node(*chart.bms_control_tree,
                                                               request.random_block) : nullptr;
    const auto* old_leaf = chart.bms_control_tree ? tree_node(*chart.bms_control_tree, request.leaf)
                                                  : nullptr;
    if (old_block == nullptr || old_leaf == nullptr ||
        old_leaf->source_order != request.expected_leaf_source_order ||
        old_leaf->original_text != request.expected_leaf_original_text) {
        add_block_error(result, "BMS branch leaf is stale or not reorderable");
        return result;
    }
    auto serialized = retained_bms_bytes(history.document, chart);
    auto baseline = serialized ? parse_unselected(*serialized, chart) : std::nullopt;
    if (!baseline || baseline->charts.size() != 1 || !baseline->charts.front().bms_control_tree ||
        !chart.bms_control_tree) {
        add_block_error(result, "current BMS document cannot be reparsed before the leaf move");
        return result;
    }
    auto& baseline_chart = baseline->charts.front();
    (void)restore_baseline_node_ids(*chart.bms_control_tree, *baseline_chart.bms_control_tree);
    baseline_chart.active_branch_selection = chart.active_branch_selection;
    const auto* block = tree_node(*baseline_chart.bms_control_tree, request.random_block);
    if (!is_all_selected_branches_present(baseline_chart) || block == nullptr) {
        add_block_error(result, "serialized BMS source no longer contains the branch block");
        return result;
    }
    const auto swap = find_leaf_swap(*block, request.leaf, request.direction, result);
    if (!swap) return result;
    const auto region = swapped_region(*serialized, *swap);
    const auto begin = swap->upper_body_begin;
    const auto end = swap->lower_body_end;
    if (std::equal(region.begin(), region.end(), serialized->begin() + static_cast<std::ptrdiff_t>(begin))) {
        add_block_error(result, "the two BMS leaves have identical bodies; there is nothing to reorder");
        return result;
    }
    std::copy(region.begin(), region.end(), serialized->begin() + static_cast<std::ptrdiff_t>(begin));
    auto unselected = parse_unselected(*serialized, baseline_chart);
    if (!unselected || unselected->charts.empty() || !unselected->charts.front().bms_control_tree) {
        add_block_error(result, "reordered BMS branch block failed structural validation");
        return result;
    }
    const auto choices = leaf_move_choices(baseline_chart, *unselected->charts.front().bms_control_tree,
                                           *serialized, *swap, begin, end);
    return publish_reparsed(history, chart, std::move(*serialized), choices,
                            *baseline_chart.bms_control_tree, begin, end, 0, 0, true,
                            request.direction == bms_branch_leaf_move_direction_t::up
                                ? "Move BMS branch leaf up" : "Move BMS branch leaf down");
}

} // namespace

auto apply_bms_branch_statement_edit_command(document_history_t& history,
                                             bms_branch_statement_edit_t edit)
    -> bms_branch_statement_command_result_t {
    return publish_branch_edit(
        history, edit_bms_inactive_branch_statement(history.document, std::move(edit)),
        "Edit inactive BMS statement");
}

auto apply_bms_branch_statement_insert_command(document_history_t& history,
                                               bms_branch_statement_insert_t insertion)
    -> bms_branch_statement_command_result_t {
    return publish_branch_edit(
        history, insert_bms_inactive_branch_statement(history.document, std::move(insertion)),
        "Insert inactive BMS statement");
}

auto apply_bms_branch_statement_delete_command(document_history_t& history,
                                               bms_branch_statement_delete_t deletion)
    -> bms_branch_statement_command_result_t {
    return publish_branch_edit(
        history, delete_bms_inactive_branch_statement(history.document, std::move(deletion)),
        "Delete inactive BMS statement");
}

auto apply_bms_branch_selection_command(document_history_t& history,
                                        bms_branch_selection_request_t request)
    -> bms_branch_selection_command_result_t {
    bms_branch_selection_command_result_t result;
    result.command.revision = history.document.revision;

    auto rematerialized = rematerialize_bms_branch_selection(history.document, request);
    const auto can_publish = rematerialized.value.song.has_value() && rematerialized.ok();
    result.payload.preview = std::move(rematerialized.value.preview);
    result.payload.format_diagnostics = std::move(rematerialized.diagnostics.selection);
    result.payload.write_diagnostics = std::move(rematerialized.diagnostics.write);
    result.payload.parse_diagnostics = std::move(rematerialized.diagnostics.parse);
    if (!can_publish) {
        return result;
    }
    auto transaction = document_command_transaction_t(history);
    transaction.adopt_document(std::move(*rematerialized.value.song));
    result.command = transaction.commit("Change active BMS branch");
    return result;
}

auto apply_bms_branch_choice_command(document_history_t& history,
                                     bms_branch_choice_command_t choice)
    -> bms_branch_selection_command_result_t {
    bms_branch_selection_request_t request{.chart = choice.chart};
    const auto chart =
        history.document.find_chart(choice.chart);
    if (chart != nullptr) {
        request.proposed = chart->active_branch_selection;
        const auto selected =
            std::ranges::find(request.proposed, choice.branch, &branch_selection_t::branch);
        if (choice.selected_value.has_value()) {
            if (selected == request.proposed.end()) {
                request.proposed.push_back({choice.branch, *choice.selected_value});
            } else {
                selected->selected_value = *choice.selected_value;
            }
        } else if (selected != request.proposed.end()) {
            request.proposed.erase(selected);
        }
    }
    return apply_bms_branch_selection_command(history, std::move(request));
}

auto apply_bms_branch_block_create_command(document_history_t& history,
                                           bms_branch_block_create_command_t request)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    const auto* chart = history.document.find_chart(request.chart);
    if (chart == nullptr) {
        add_block_error(result, "selected BMS chart no longer exists");
        return result;
    }
    if (!preserve_guard(result, history.document, *chart)) return result;
    return create_block(history, *chart, request);
}

auto apply_bms_branch_block_remove_command(document_history_t& history,
                                           bms_branch_block_remove_command_t request)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    const auto* chart = history.document.find_chart(request.chart);
    if (chart == nullptr) {
        add_block_error(result, "selected BMS chart no longer exists");
        return result;
    }
    if (!preserve_guard(result, history.document, *chart)) return result;
    return remove_block(history, *chart, request);
}

auto apply_bms_branch_leaf_move_command(document_history_t& history,
                                        bms_branch_leaf_move_command_t request)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    const auto* chart = history.document.find_chart(request.chart);
    if (chart == nullptr) {
        add_block_error(result, "selected BMS chart no longer exists");
        return result;
    }
    if (!preserve_guard(result, history.document, *chart)) return result;
    return leaf_move(history, *chart, request);
}

} // namespace wormedit

namespace wormedit {
namespace {
using authored_program_t = result_t<bms_program_t, error_t<failure_code_t>>;
template<class Edit>
auto stage_program(document_command_transaction_t& transaction, chart_id_t difficulty, Edit edit,
    std::string label) -> attempt_t<document_edit_effect_t> {
    return transaction.stage([&](song_document_t& song) -> attempt_t<document_edit_effect_t> {
        const auto* owner = song.find_bms_program(difficulty);
        if (!owner) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "No scoped BMS program exists for this difficulty.")}};
        auto program = edit(owner->prepared().program);
        if (!program) return {.diagnostics = {error(diagnostic_code_t::invalid_id, program.error().message)}};
        if (owner->prepared().program == *program) return {.value = document_edit_effect_t{}};
        auto prepared = owner->prepared();
        prepared.program = std::move(*program);
        auto evaluation = owner->evaluation();
        std::erase_if(evaluation.selections, [&](const auto& selection) {
            return !prepared.program.find(selection.branch);
        });
        auto updated = bms_program_document_t::from(difficulty, std::move(prepared), std::move(evaluation));
        if (!updated.value || !updated.ok()) {
            const auto message = updated.diagnostics.empty() ? "The authored program cannot be projected." :
                updated.diagnostics.front().message;
            return {.diagnostics = {error(diagnostic_code_t::invalid_id, message)}};
        }
        const auto found = std::ranges::find_if(song.prepared_bms_programs,
            [&](const auto& item) { return item->difficulty() == difficulty; });
        *found = std::move(*updated.value);
        return {.value = document_edit_effect_t{.changed = true, .command_label = std::move(label)}};
    });
}
template<class Edit>
auto stage_program_proposal(proposal_edit_scope_t<chart_stamp_t>& scope, Edit edit, std::string label)
    -> attempt_t<std::string> {
    auto staged = stage_program(scope.transaction(), scope.captured_stamp().chart, std::move(edit), std::move(label));
    if (!staged.value || !staged.ok()) return {.diagnostics = std::move(staged.diagnostics)};
    return {.value = staged.value->changed ? std::optional{std::move(staged.value->command_label)} : std::nullopt,
        .diagnostics = std::move(staged.diagnostics)};
}
template<class Proposal, class Inspect, class... Arguments>
auto prepare_validated_program_proposal(const song_document_t& song, Proposal proposal,
    Inspect inspect, Arguments... arguments) -> attempt_t<Proposal> {
    const auto checked = document_command_transaction_t::preview(song, [&](document_history_t& preview) {
        auto result = apply_proposal(preview, proposal, arguments...);
        if (result.applied) inspect(proposal.payload, preview.document);
        return result;
    });
    if (!checked.applied) return {.diagnostics = checked.diagnostics};
    return {.value = std::move(proposal), .diagnostics = checked.diagnostics};
}
auto validate_insert_value(const bms_program_insert_value_t& value) -> std::optional<std::string> {
    if (const auto* chart = std::get_if<bms_chart_content_t>(&value); chart && !chart->items.empty())
        return "A new Chart must be empty; content uses the scoped content editor.";
    if (const auto* arm = std::get_if<bms_case_scope_t>(&value); arm && !arm->skips.empty())
        return "New cases start without SKIP; author behavior with a stable case anchor.";
    if (const auto* text = std::get_if<bms_text_t>(&value)) {
        const auto checked = validate_bms_program_text(*text);
        if (!checked) return checked.error().message;
    }
    return std::nullopt;
}
} // namespace

auto apply_bms_program_insert_command(document_history_t& history, bms_program_insert_command_t request)
    -> document_command_result_t {
    document_command_transaction_t transaction(history, request.stamp, "Add BMS node");
    return transaction.finish_execution(stage_program(transaction, request.stamp.chart,
        [&](const bms_program_t& program) -> authored_program_t {
            if (const auto message = validate_insert_value(request.value))
                return std::unexpected(error_t{failure_code_t::invalid_request, *message});
            bms_program_node_t node{.value = std::visit([](const auto& value) -> bms_program_node_value_t { return value; }, request.value)};
            const auto first = transaction.next_bms_program_item_id(node.is_scope() ? 2 : 1);
            if (!first) return std::unexpected(error_t{failure_code_t::capacity_exceeded, "No fresh BMS node identities are available."});
            node.id = branch_id_t{first->value};
            const auto chart = node.is_scope() ? std::optional{branch_id_t{first->value + 1}} : std::nullopt;
            return program.with_node(request.parent, request.after, std::move(node), chart, request.limits);
        }, "Add BMS node"));
}
auto apply_bms_program_text_command(document_history_t& history, bms_program_text_command_t request)
    -> document_command_result_t {
    document_command_transaction_t transaction(history, request.stamp, "Edit BMS Text");
    return transaction.finish_execution(stage_program(transaction, request.stamp.chart,
        [&](const bms_program_t& program) -> authored_program_t {
            const auto checked = validate_bms_program_text(request.text);
            if (!checked) return std::unexpected(checked.error());
            return program.with_text(request.node, std::move(request.text), request.limits);
        }, "Edit BMS Text"));
}
auto apply_bms_program_skip_command(document_history_t& history, bms_program_skip_command_t request)
    -> document_command_result_t {
    document_command_transaction_t transaction(history, request.stamp, "Add BMS SKIP");
    return transaction.finish_execution(stage_program(transaction, request.stamp.chart,
        [&](const bms_program_t& program) -> authored_program_t {
            const auto* node = program.find(request.case_node);
            if (!node || !std::holds_alternative<bms_case_scope_t>(node->value))
                return std::unexpected(error_t{failure_code_t::not_found, "SKIP requires an existing CASE/DEF owner."});
            auto nodes = std::vector<bms_program_node_t>(program.nodes().begin(), program.nodes().end());
            auto& arm = std::get<bms_case_scope_t>(std::ranges::find(nodes, request.case_node, &bms_program_node_t::id)->value);
            if (std::ranges::find(arm.skips, request.skip) != arm.skips.end()) return program;
            arm.skips.push_back(std::move(request.skip));
            return bms_program_t::from(std::move(nodes), request.limits);
        }, "Add BMS SKIP"));
}
} // namespace wormedit

namespace wormedit {
auto bms_program_move_report_t::from(const bms_program_document_t& owner, branch_id_t subtree)
    -> std::optional<bms_program_move_report_t> {
    const auto& program = owner.prepared().program;
    const auto parent = program.parent(subtree);
    if (!parent) return std::nullopt;
    bms_program_move_report_t report{.source_parent = *parent};
    auto contents = bms_program_subtree_t::from(program, subtree);
    if (!contents) return std::nullopt;
    report.subtree = std::move(contents->nodes);
    for (const auto& node : program.nodes()) {
        const auto* chart = std::get_if<bms_chart_content_t>(&node.value);
        if (!chart) continue;
        report.potentially_affected_charts.push_back(node.id);
        for (const auto& item : chart->items)
            if (std::holds_alternative<bms_resource_declaration_t>(item.value) ||
                std::holds_alternative<bms_timing_definition_t>(item.value) ||
                std::holds_alternative<bms_metadata_declaration_t>(item.value) ||
                std::holds_alternative<bms_measure_length_t>(item.value))
                report.potentially_affected_definitions.push_back(item.id);
    }
    const auto projection = owner.build_projection();
    if (!projection.value) return std::nullopt;
    const auto charts = projection.value->active_charts();
    report.before_active_charts.assign(charts.begin(), charts.end());
    return report;
}

auto bms_program_move_edit_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope) const -> attempt_t<std::string> {
    return stage_program_proposal(scope,
        [&](const bms_program_t& program) { return program.with_move(move, limits); }, "Move BMS subtree");
}
auto bms_program_move_edit_t::propose(const song_document_t& song, chart_id_t difficulty,
    bms_program_move_t move, bms_program_limits_t limits) -> attempt_t<bms_program_move_proposal_t> {
    auto proposal = bms_program_move_proposal_t::from(chart_stamp_t::from(song, difficulty),
        bms_program_move_edit_t{.move = move, .limits = limits});
    return prepare_validated_program_proposal(song, std::move(proposal),
        [&](bms_program_move_edit_t& payload, const song_document_t& after) {
            payload.report = *bms_program_move_report_t::from(*song.find_bms_program(difficulty), move.node);
            const auto projection = after.find_bms_program(difficulty)->build_projection();
            const auto charts = projection.value->active_charts();
            payload.report.after_active_charts.assign(charts.begin(), charts.end());
        });
}
} // namespace wormedit

namespace wormedit {
auto bms_program_delete_edit_t::validate_capture(const chart_stamp_t& stamp) const -> diagnostic_list_t {
    if (stamp == stamp_) return {};
    return {error(diagnostic_code_t::invalid_id, "Deletion confirmation does not match its prepared project, revision and difficulty.")};
}
auto bms_program_delete_edit_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope,
    bms_program_delete_decision_t decision) const -> attempt_t<std::string> {
    if (decision != bms_program_delete_decision_t::confirm) return {};
    return stage_program_proposal(scope,
        [&](const bms_program_t& program) { return program.without_subtree(summary_.root, limits_); }, "Delete BMS subtree");
}
auto bms_program_delete_edit_t::propose(const song_document_t& song, chart_id_t difficulty,
    branch_id_t node, bms_program_limits_t limits) -> attempt_t<bms_program_delete_proposal_t> {
    const auto* owner = song.find_bms_program(difficulty);
    if (!owner) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "No scoped BMS program exists for this difficulty.")}};
    auto summary = bms_program_subtree_t::from(owner->prepared().program, node);
    if (!summary) return {.diagnostics = {error(diagnostic_code_t::invalid_id, summary.error().message)}};
    const auto stamp = chart_stamp_t::from(song, difficulty);
    auto proposal = bms_program_delete_proposal_t::from(stamp,
        bms_program_delete_edit_t(stamp, std::move(*summary), limits));
    return prepare_validated_program_proposal(song, std::move(proposal),
        [](bms_program_delete_edit_t&, const song_document_t&) {}, bms_program_delete_decision_t::confirm);
}
} // namespace wormedit

namespace wormedit {
auto bms_program_unwrap_report_t::from(const bms_program_document_t& owner, branch_id_t node)
    -> std::optional<bms_program_unwrap_report_t> {
    const auto& program = owner.prepared().program;
    auto contents = bms_program_subtree_t::from(program, node);
    auto effects = bms_program_move_report_t::from(owner, node);
    if (!contents || !effects || !program.find(node)->is_scope()) return std::nullopt;
    bms_program_unwrap_report_t report{.contents = std::move(*contents), .effects = std::move(*effects),
        .unconditional_branches = program.unwrap_conversions(node)};
    auto changed = report.unconditional_branches;
    changed.push_back(node);
    for (const auto id : changed)
        if (const auto* arm = std::get_if<bms_case_scope_t>(&program.find(id)->value)) report.removed_skips += arm->skips.size();
    return report;
}
auto bms_program_unwrap_edit_t::validate_capture(const chart_stamp_t& stamp) const -> diagnostic_list_t {
    if (stamp == stamp_) return {};
    return {error(diagnostic_code_t::invalid_id, "Unwrap confirmation does not match its prepared project, revision and difficulty.")};
}
auto bms_program_unwrap_edit_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope,
    bms_program_unwrap_decision_t decision) const -> attempt_t<std::string> {
    if (decision != bms_program_unwrap_decision_t::confirm_promotion &&
        decision != bms_program_unwrap_decision_t::confirm_unconditional_conversion) return {};
    if ((!report_.unconditional_branches.empty() || report_.removed_skips) &&
        decision != bms_program_unwrap_decision_t::confirm_unconditional_conversion)
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
            "Unwrap requires explicit confirmation to make affected branches unconditional and remove their selector-specific SKIP behavior.")}};
    return stage_program_proposal(scope,
        [&](const bms_program_t& program) { return program.with_unwrap(report_.contents.root, limits_); }, "Unwrap BMS scope");
}
auto bms_program_unwrap_edit_t::propose(const song_document_t& song, chart_id_t difficulty,
    branch_id_t node, bms_program_limits_t limits) -> attempt_t<bms_program_unwrap_proposal_t> {
    const auto* owner = song.find_bms_program(difficulty);
    if (!owner) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "No scoped BMS program exists for this difficulty.")}};
    auto report = bms_program_unwrap_report_t::from(*owner, node);
    if (!report)
        return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Unwrap requires an existing non-Root body scope.")}};
    const auto stamp = chart_stamp_t::from(song, difficulty);
    auto proposal = bms_program_unwrap_proposal_t::from(stamp,
        bms_program_unwrap_edit_t(stamp, std::move(*report), limits));
    return prepare_validated_program_proposal(song, std::move(proposal),
        [&](bms_program_unwrap_edit_t& payload, const song_document_t& after) {
            const auto projection = after.find_bms_program(difficulty)->build_projection();
            const auto charts = projection.value->active_charts();
            payload.report_.effects.after_active_charts.assign(charts.begin(), charts.end());
        }, bms_program_unwrap_decision_t::confirm_unconditional_conversion);
}
} // namespace wormedit

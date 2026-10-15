#include <wormedit/core/bms36.h>
#include "chart_object_ids.h"
#include "text_util.h"
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/natural_order.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/virtual_canvas.h>

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace wormedit {
namespace {

auto has_role(const resource_entry_t& resource, resource_role_t role) -> bool;

auto resolve_bga_resource_kind(const song_document_t& document, resource_id_t id,
                               visual_event_kind_t requested, document_command_result_t& result)
    -> std::optional<visual_event_kind_t> {
    const auto lane = editor_bga_lane_index(requested);
    if (!lane) {
        reject(result, diagnostic_code_t::unsupported_feature, "Unsupported BGA lane kind");
        return std::nullopt;
    }
    const auto resource = std::ranges::find(document.resources, id, &resource_entry_t::id);
    if (resource == document.resources.end()) {
        reject(result, diagnostic_code_t::invalid_id, "BGA resource is missing");
        return std::nullopt;
    }
    const auto asset = std::ranges::find(document.visual_assets, resource->asset, &visual_asset_t::id);
    if (asset == document.visual_assets.end() ||
        (asset->kind != asset_kind_t::image && asset->kind != asset_kind_t::video)) {
        reject(result, diagnostic_code_t::unsupported_feature, "BGA requires an image or video resource");
        return std::nullopt;
    }
    if (asset->kind == asset_kind_t::video && *lane != 0) {
        reject(result, diagnostic_code_t::unsupported_feature, "Video can only be placed in the BGA base lane");
        return std::nullopt;
    }
    return *lane == 0 ? (asset->kind == asset_kind_t::video
        ? visual_event_kind_t::video : visual_event_kind_t::background) : requested;
}

struct resolved_bga_stroke_target_t {
    local_position_t position;
    std::uint64_t row{};
    std::size_t lane{};
    visual_event_kind_t kind{visual_event_kind_t::background};
};

auto resolve_bga_stroke_kind(asset_kind_t asset_kind, visual_event_kind_t requested,
                            document_command_result_t& result)
    -> std::optional<visual_event_kind_t> {
    const auto lane = editor_bga_lane_index(requested);
    if (!lane) {
        reject(result, diagnostic_code_t::unsupported_feature, "Unsupported BGA lane kind");
        return std::nullopt;
    }
    if (asset_kind != asset_kind_t::image && asset_kind != asset_kind_t::video) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "BGA requires an image or video resource");
        return std::nullopt;
    }
    if (asset_kind == asset_kind_t::video && *lane != 0) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "Video can only be placed in the BGA base lane");
        return std::nullopt;
    }
    return *lane == 0 ? (asset_kind == asset_kind_t::video
        ? visual_event_kind_t::video : visual_event_kind_t::background) : requested;
}

auto resolve_bga_stroke_targets(const measure_index_t& index,
                                std::span<const bga_stroke_target_t> targets,
                                std::optional<asset_kind_t> asset_kind, bool erase,
                                document_command_result_t& result)
    -> std::optional<std::vector<resolved_bga_stroke_target_t>> {
    std::vector<resolved_bga_stroke_target_t> resolved;
    resolved.reserve(targets.size());
    std::set<std::pair<std::uint64_t, std::size_t>> seen;
    for (const auto& target : targets) {
        const auto row = absolute_row(index, target.position);
        const auto kind = erase ? std::optional{target.kind}
                                : resolve_bga_stroke_kind(*asset_kind, target.kind, result);
        const auto lane = kind ? editor_bga_lane_index(*kind) : std::nullopt;
        if (!kind || !lane) {
            if (kind) reject(result, diagnostic_code_t::unsupported_feature,
                             "Unsupported BGA lane kind");
            return std::nullopt;
        }
        if (!row) {
            reject(result, diagnostic_code_t::row_out_of_range,
                   "BGA stroke row is outside the chart");
            return std::nullopt;
        }
        if (seen.emplace(*row, *lane).second)
            resolved.push_back({target.position, *row, *lane, *kind});
    }
    return resolved;
}

auto resolve_bga_stroke_resource(const song_document_t& document,
                                 std::optional<resource_id_t> resource,
                                 document_command_result_t& result)
    -> std::optional<asset_kind_t> {
    if (!resource) {
        reject(result, diagnostic_code_t::invalid_id, "BGA draw requires a resource");
        return std::nullopt;
    }
    const auto entry = std::ranges::find(document.resources, *resource, &resource_entry_t::id);
    if (entry == document.resources.end()) {
        reject(result, diagnostic_code_t::invalid_id, "resource placement refers to an unknown catalog resource");
        return std::nullopt;
    }
    if (entry->missing) {
        reject(result, diagnostic_code_t::dangling_resource,
               "cannot place a resource with a missing source");
        return std::nullopt;
    }
    if (!has_role(*entry, resource_role_t::bga)) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "resource is incompatible with the requested placement");
        return std::nullopt;
    }
    const auto asset = std::ranges::find(document.visual_assets, entry->asset, &visual_asset_t::id);
    if (asset == document.visual_assets.end() ||
        (asset->kind != asset_kind_t::image && asset->kind != asset_kind_t::video)) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "BGA requires an image or video resource");
        return std::nullopt;
    }
    return asset->kind;
}

auto commit_bga_stroke_targets(document_command_transaction_t& transaction, std::size_t chart_offset,
                               const measure_index_t& index,
                               std::span<const resolved_bga_stroke_target_t> targets,
                               std::optional<resource_id_t> resource, bool erase)
    -> document_command_result_t {
    auto& history = transaction.history;
    document_command_result_t unchanged{.revision = history.document.revision, .diagnostics = {}};
    auto& proposed = transaction.copy_document();
    auto& chart = proposed.charts[chart_offset];
    std::set<std::pair<std::uint64_t, std::size_t>> cells;
    for (const auto& target : targets) cells.emplace(target.row, target.lane);
    if (erase) {
        std::erase_if(chart.bga_events, [&](const auto& event) {
            const auto row = absolute_row(index, event.position);
            const auto lane = editor_bga_lane_index(event.kind);
            return row && lane && cells.contains({*row, *lane});
        });
        if (chart.bga_events.size() == history.document.charts[chart_offset].bga_events.size())
            return unchanged;
        return transaction.commit("Erase BGA events");
    }
    std::set<std::pair<std::uint64_t, std::size_t>> occupied;
    for (const auto& event : chart.bga_events) {
        const auto row = absolute_row(index, event.position);
        const auto lane = editor_bga_lane_index(event.kind);
        if (row && lane) occupied.emplace(*row, *lane);
    }
    std::vector<resolved_bga_stroke_target_t> additions;
    for (const auto& target : targets)
        if (occupied.emplace(target.row, target.lane).second) additions.push_back(target);
    if (additions.empty()) return unchanged;
    const auto first_id = next_chart_object_id(history.document.charts[chart_offset], default_maximum_chart_objects).id;
    if (!first_id || additions.size() - 1 >
            std::numeric_limits<std::uint64_t>::max() - first_id->value) {
        reject(unchanged, diagnostic_code_t::invalid_id,
               "BGA stroke chart object limit or ID space is exhausted");
        return unchanged;
    }
    auto next_id = first_id->value;
    for (const auto& target : additions)
        chart.bga_events.push_back({.id = object_id_t{next_id++}, .position = target.position,
                                    .resource = *resource, .kind = target.kind});
    return transaction.commit("Draw BGA events");
}

auto bms_occupied(const song_document_t& document, std::string_view name_space,
                  std::size_t base, std::span<const resource_id_t> ignored = {})
    -> std::vector<bool> {
    auto occupied = std::vector<bool>(base * base, false);
    for (const auto& resource : document.resources) {
        if (std::ranges::find(ignored, resource.id) != ignored.end()) continue;
        for (const auto& identity : resource.target_identities) {
            if (identity.family != format_family_t::bms || identity.namespace_name != name_space ||
                identity.identifier.size() != 2) continue;
            const auto high = bms36_value(static_cast<unsigned char>(identity.identifier[0]));
            const auto low = bms36_value(static_cast<unsigned char>(identity.identifier[1]));
            if (high >= 0 && low >= 0 && static_cast<std::size_t>(high) < base &&
                static_cast<std::size_t>(low) < base)
                occupied[static_cast<std::size_t>(high) * base + static_cast<std::size_t>(low)] = true;
        }
    }
    return occupied;
}

auto encode_bms_identifier(std::size_t value, std::size_t base) -> std::string {
    return {bms36_digits[value / base], bms36_digits[value % base]};
}

auto has_role(const resource_entry_t& resource, resource_role_t role) -> bool;

auto is_bms_resource_in_namespace(const resource_entry_t& resource, std::string_view name_space)
    -> bool {
    if (name_space == "BMP") return has_role(resource, resource_role_t::bga);
    return has_role(resource, resource_role_t::sample) || has_role(resource, resource_role_t::bgm) ||
           has_role(resource, resource_role_t::music) ||
           std::ranges::any_of(resource.target_identities, [name_space](const auto& identity) {
               return identity.family == format_family_t::bms &&
                      identity.namespace_name == name_space;
           });
}

auto renumber_resources(const song_document_t& document,
                        const renumber_bms_identifiers_action_t& action,
                        document_command_result_t& result)
    -> std::optional<std::vector<const resource_entry_t*>> {
    auto ids = action.resources;
    if (ids.empty())
        for (const auto& resource : document.resources)
            if (is_bms_resource_in_namespace(resource, action.name_space))
                ids.push_back(resource.id);
    std::ranges::sort(ids, {}, &resource_id_t::value);
    if (std::ranges::adjacent_find(ids) != ids.end()) {
        reject(result, diagnostic_code_t::invalid_id, "BMS identifier selection contains duplicates");
        return std::nullopt;
    }
    std::vector<const resource_entry_t*> resources;
    for (const auto id : ids) {
        const auto item = std::ranges::find(document.resources, id, &resource_entry_t::id);
        if (item == document.resources.end() || !is_bms_resource_in_namespace(*item, action.name_space)) {
            reject(result, diagnostic_code_t::invalid_id,
                   "BMS identifier selection has an unknown or incompatible resource");
            return std::nullopt;
        }
        resources.push_back(&*item);
    }
    std::ranges::sort(resources, [](const auto* left, const auto* right) {
        if (natural_name_less(left->name, right->name)) return true;
        if (natural_name_less(right->name, left->name)) return false;
        return left->id.value < right->id.value;
    });
    return resources;
}

auto first_free_bms_start(const std::vector<bool>& occupied, std::size_t count)
    -> std::optional<std::size_t> {
    if (count == 0) return std::nullopt;
    for (std::size_t candidate = 1; count <= occupied.size() - candidate; ++candidate) {
        auto free = true;
        for (std::size_t offset = 0; offset < count; ++offset)
            if (occupied[candidate + offset]) { free = false; break; }
        if (free) return candidate;
    }
    return std::nullopt;
}

auto has_role(const resource_entry_t& resource, resource_role_t role) -> bool {
    return resource.has_role(role);
}

void append_chart_reference_counts(const chart_document_t& chart,
                                   resource_reference_counts_t& counts,
                                   resource_reference_counts_t& retained) {
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes)
            if (note.sound) ++counts[note.sound->value];
        for (const auto& event : measure.bgm_events)
            if (event.sound) ++counts[event.sound->value];
    }
    for (const auto& event : chart.bga_events) ++counts[event.resource.value];
    for (const auto& channel : chart.sound_channels)
        retained[channel.source_clip.source.value] += channel.events.size();
}

auto snap_placement_position(const chart_document_t& chart, const measure_index_t& index,
                             local_position_t position, std::uint64_t denominator)
    -> std::optional<local_position_t> {
    if (!absolute_row(index, position)) {
        return std::nullopt;
    }
    if (denominator == 0) {
        return position;
    }
    return snap_position(chart, index, position, denominator);
}

auto branch_source_orders(const chart_document_t& chart)
    -> std::unordered_set<std::size_t> {
    std::unordered_set<std::size_t> orders;
    if (!chart.bms_control_tree)
        return orders;
    const auto visit = [&](const auto& self, const bms_control_node_t& node,
                           bool inside_branch) -> void {
        if (inside_branch)
            orders.insert(node.source_order);
        const auto boundary = node.kind == bms_control_kind_t::random ||
                              node.kind == bms_control_kind_t::switch_block ||
                              node.kind == bms_control_kind_t::if_group ||
                              node.kind == bms_control_kind_t::if_branch ||
                              node.kind == bms_control_kind_t::else_if_branch ||
                              node.kind == bms_control_kind_t::else_branch ||
                              node.kind == bms_control_kind_t::case_branch ||
                              node.kind == bms_control_kind_t::default_branch;
        for (const auto& child : node.children)
            self(self, child, inside_branch || boundary);
    };
    visit(visit, *chart.bms_control_tree, false);
    return orders;
}

} // namespace

auto validate_resource_role(const song_document_t& document, resource_id_t id,
    resource_role_t role, document_command_result_t& result) -> bool {
    const auto resource = std::ranges::find(document.resources, id, &resource_entry_t::id);
    if (!id || resource == document.resources.end()) {
        reject(result, diagnostic_code_t::invalid_id, "Resource is no longer in the catalog");
        return false;
    }
    if (resource->missing) {
        reject(result, diagnostic_code_t::dangling_resource, "Cannot use a resource with a missing source");
        return false;
    }
    const auto audio_role = role == resource_role_t::sample || role == resource_role_t::bgm;
    const auto asset_present = audio_role
        ? document.find_audio_asset(resource->asset) != nullptr
        : role == resource_role_t::bga &&
          std::ranges::find(document.visual_assets, resource->asset, &visual_asset_t::id) != document.visual_assets.end();
    // Background events may play ordinary keysounds as well as BGM-only sources.
    // This matches BGM strokes and keeps assignment/placement eligible in the same way.
    const auto role_present = has_role(*resource, role) ||
        (role == resource_role_t::bgm && has_role(*resource, resource_role_t::sample));
    if (!role_present || !asset_present) {
        reject(result, diagnostic_code_t::unsupported_feature, "Resource cannot be used in this catalog lane");
        return false;
    }
    return true;
}

auto find_bms_identifier_run(const song_document_t& document, std::string_view name_space,
                             std::size_t count) -> std::optional<std::vector<std::string>> {
    if ((name_space != "WAV" && name_space != "BMP") || count == 0) return std::nullopt;
    constexpr auto base = std::size_t{36};
    if (count > base * base - 1) return std::nullopt;
    const auto occupied = bms_occupied(document, name_space, base);
    for (std::size_t start = 1; count <= occupied.size() - start; ++start) {
        auto free = true;
        for (std::size_t offset = 0; offset < count; ++offset)
            if (occupied[start + offset]) { free = false; break; }
        if (free) {
            std::vector<std::string> result;
            result.reserve(count);
            for (std::size_t offset = 0; offset < count; ++offset)
                result.push_back(encode_bms_identifier(start + offset, base));
            return result;
        }
    }
    return std::nullopt;
}

auto bms_identifier_capacity(const song_document_t& document, std::string_view name_space)
    -> bms_identifier_capacity_t {
    if (name_space != "WAV" && name_space != "BMP") return {};
    constexpr auto base = std::size_t{36};
    const auto occupied = bms_occupied(document, name_space, base);
    bms_identifier_capacity_t result{.used = 0, .total = base * base - 1, .largest_free_run = 0};
    auto run = std::size_t{};
    for (std::size_t value = 1; value < occupied.size(); ++value) {
        if (occupied[value]) {
            ++result.used;
            run = 0;
        } else {
            result.largest_free_run = std::max(result.largest_free_run, ++run);
        }
    }
    return result;
}

auto apply_bms_identifier_renumber(document_history_t& history,
                                   const renumber_bms_identifiers_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    const auto& ns = action.name_space;
    if (ns != "WAV" && ns != "BMP") {
        reject(result, diagnostic_code_t::invalid_id, "BMS identifier namespace must be WAV or BMP");
        return result;
    }
    const auto resources = renumber_resources(history.document, action, result);
    if (!resources) return result;
    if (resources->empty()) return result;
    constexpr auto base = std::size_t{36};
    std::vector<resource_id_t> selected;
    selected.reserve(resources->size());
    for (const auto* resource : *resources) selected.push_back(resource->id);
    const auto occupied = bms_occupied(history.document, ns, base, selected);
    const auto start = first_free_bms_start(occupied, resources->size());
    if (!start) {
        reject(result, diagnostic_code_t::invalid_resolution, "BMS identifier namespace has no contiguous run for renumbering");
        return result;
    }
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    for (std::size_t index = 0; index < resources->size(); ++index) {
        auto item = std::ranges::find(proposed.resources, (*resources)[index]->id, &resource_entry_t::id);
        std::erase_if(item->target_identities, [&ns](const auto& identity) {
            return identity.family == format_family_t::bms && identity.namespace_name == ns;
        });
        item->target_identities.push_back({format_family_t::bms,
            encode_bms_identifier(*start + index, base), ns});
    }
    return transaction.commit("Renumber BMS identifiers");
}

auto resolve_resource_placement_position(const chart_document_t& chart,
                                         local_position_t position,
                                         std::uint64_t snap_denominator)
    -> std::optional<local_position_t> {
    const auto built = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built)) {
        return std::nullopt;
    }
    return snap_placement_position(chart, std::get<measure_index_t>(built), position,
                                   snap_denominator);
}

auto translate_bgm_end(const measure_index_t& index, local_position_t old_head,
                       local_position_t new_head, local_position_t old_end)
    -> std::optional<local_position_t> {
    const auto old_start = absolute_row(index, old_head);
    const auto new_start = absolute_row(index, new_head);
    const auto tail = absolute_row(index, old_end);
    if (!old_start || !new_start || !tail || *tail <= *old_start) return std::nullopt;
    std::uint64_t translated{};
    if (*new_start >= *old_start) {
        const auto delta = *new_start - *old_start;
        if (*tail > std::numeric_limits<std::uint64_t>::max() - delta) return std::nullopt;
        translated = *tail + delta;
    } else {
        const auto delta = *old_start - *new_start;
        if (*tail < delta) return std::nullopt;
        translated = *tail - delta;
    }
    for (const auto& entry : index.entries) {
        if (translated >= entry.absolute_begin_row &&
            translated - entry.absolute_begin_row < entry.row_capacity)
            return local_position_t{entry.id, translated - entry.absolute_begin_row};
    }
    return std::nullopt;
}

auto next_bms_resource_identity(const song_document_t& document, bool visual_role)
    -> std::optional<target_resource_identity_t> {
    const std::string_view name_space = visual_role ? "BMP" : "WAV";
    const auto run = find_bms_identifier_run(document, name_space, 1);
    if (!run) return std::nullopt;
    return target_resource_identity_t{format_family_t::bms, run->front(), std::string{name_space}};
}

auto validate_placement_resource(const song_document_t& document,
                                 const place_resource_action_t& action,
                                 document_command_result_t& result)
    -> std::optional<resource_role_t> {
    if (action.kind != resource_placement_kind_t::sample_note &&
        action.kind != resource_placement_kind_t::bgm_event &&
        action.kind != resource_placement_kind_t::bga_background) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "resource placement kind is unsupported");
        return std::nullopt;
    }
    const auto role = action.kind == resource_placement_kind_t::sample_note
        ? resource_role_t::sample : action.kind == resource_placement_kind_t::bgm_event
        ? resource_role_t::bgm : resource_role_t::bga;
    if (!validate_resource_role(document, action.resource, role, result)) return std::nullopt;
    return role;
}

struct resource_placement_target_t {
    std::size_t chart_offset{};
    measure_index_t index;
    local_position_t position;
    std::uint64_t row{};
    object_id_t first_id;
};

auto resolve_placement_target(const song_document_t& document,
                              const place_resource_action_t& action,
                              document_command_result_t& result)
    -> std::optional<resource_placement_target_t> {
    const auto chart = document.find_chart(action.chart);
    if (!action.chart || chart == nullptr) {
        reject(result, diagnostic_code_t::invalid_id, "resource placement refers to an unknown chart");
        return std::nullopt;
    }
    const auto built = build_measure_index(*chart);
    const auto* index = std::get_if<measure_index_t>(&built);
    if (!index) {
        reject(result, diagnostic_code_t::invalid_measure,
               "resource placement chart has no valid measure index");
        return std::nullopt;
    }
    const auto position = snap_placement_position(*chart, *index, action.position,
                                                  action.snap_denominator);
    const auto row = position ? absolute_row(*index, *position) : std::nullopt;
    if (!row) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "resource placement row is outside the chart");
        return std::nullopt;
    }
    const auto first_id = next_chart_object_id(*chart, default_maximum_chart_objects).id;
    if (!first_id) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource placement chart object limit or ID space is exhausted");
        return std::nullopt;
    }
    return resource_placement_target_t{
        static_cast<std::size_t>(chart - document.charts.data()), *index, *position, *row, *first_id};
}

struct resource_placement_context_t {
    document_command_result_t& result;
    chart_document_t& chart;
    measure_chunk_t& measure;
    const measure_index_t& index;
    const place_resource_action_t& action;
    local_position_t position;
    std::uint64_t row;
    object_id_t first_id;
};

auto place_sample_note(resource_placement_context_t& context) -> bool {
    const auto& action = context.action;
    if (!action.lane) {
        reject(context.result, diagnostic_code_t::unknown_lane,
               "Sample placement requires an active playable lane");
        return false;
    }
    const lane_layout_segment_t* active{};
    std::uint64_t active_row{};
    for (const auto& segment : context.chart.lane_layouts) {
        const auto begin = absolute_row(context.index, segment.position);
        if (begin && *begin <= context.row && (!active || *begin >= active_row)) {
            active = &segment;
            active_row = *begin;
        }
    }
    if (!active || active->layout.find(action.lane) == nullptr) {
        reject(context.result, diagnostic_code_t::unknown_lane,
               "Sample placement lane is absent at the cursor row");
        return false;
    }
    for (const auto& chunk : context.chart.measures)
        for (const auto& note : chunk.notes) {
            if (note.lane != action.lane) continue;
            const auto begin = absolute_row(context.index, note.position);
            const auto end = note.end ? absolute_row(context.index, *note.end) : begin;
            if (begin && end && *begin <= context.row && context.row <= *end) {
                reject(context.result, diagnostic_code_t::collision,
                       "Sample placement overlaps a playable note or hold");
                return false;
            }
        }
    context.measure.notes.push_back({.id = context.first_id, .position = context.position,
        .lane = action.lane, .kind = note_kind_t::tap, .end = std::nullopt,
        .sound = action.resource, .sound_channel_event = std::nullopt,
        .semantic_layers = resolve_placement_layers(context.chart, action.semantic_layers,
                                                    action.resource),
        .label = std::nullopt, .branch = std::nullopt, .provenance = {}});
    return true;
}

auto choose_bgm_placement_lane(resource_placement_context_t& context,
    const std::unordered_set<std::uint64_t>& occupied) -> object_id_t {
    const auto& chart = context.chart;
    if (context.action.source_lane) {
        const auto requested = std::ranges::find(chart.bgm_source_lanes,
            context.action.source_lane, &bgm_source_lane_t::id);
        if (requested == chart.bgm_source_lanes.end()) {
            reject(context.result, diagnostic_code_t::invalid_id,
                   "BGM placement refers to an unknown source lane");
            return {};
        }
        if (occupied.contains(context.action.source_lane.value)) {
            reject(context.result, diagnostic_code_t::collision,
                   "BGM source lane is occupied at the requested row");
            return {};
        }
        return context.action.source_lane;
    }
    const bgm_source_lane_t* available{};
    for (const auto& lane : chart.bgm_source_lanes)
        if (!occupied.contains(lane.id.value) &&
            (!available || std::tie(lane.source_order, lane.id.value) <
                           std::tie(available->source_order, available->id.value)))
            available = &lane;
    if (available) return available->id;
    if (chart.bgm_source_lanes.size() >= 1000000 ||
        context.first_id.value == std::numeric_limits<std::uint64_t>::max()) {
        reject(context.result, diagnostic_code_t::invalid_id,
               "BGM source lane or chart object ID space is exhausted");
        return {};
    }
    std::size_t next_order{};
    for (const auto& lane : chart.bgm_source_lanes) {
        if (lane.source_order == std::numeric_limits<std::size_t>::max()) {
            reject(context.result, diagnostic_code_t::invalid_id,
                   "BGM source-lane order space is exhausted");
            return {};
        }
        next_order = std::max(next_order, lane.source_order + 1);
    }
    const auto id = context.first_id;
    context.chart.bgm_source_lanes.push_back({.id = id,
        .name = "BGM " + std::to_string(context.chart.bgm_source_lanes.size() + 1),
        .source_order = next_order});
    return id;
}

auto place_bgm_event(resource_placement_context_t& context) -> bool {
    std::unordered_set<std::uint64_t> occupied;
    for (const auto& chunk : context.chart.measures)
        for (const auto& event : chunk.bgm_events)
            if (event.position == context.position) occupied.insert(event.source_lane.value);
    const auto lane = choose_bgm_placement_lane(context, occupied);
    if (!lane) return false;
    const auto event_id = lane == context.first_id
        ? object_id_t{context.first_id.value + 1} : context.first_id;
    context.measure.bgm_events.push_back({.id = event_id, .position = context.position,
        .source_lane = lane, .sound = context.action.resource,
        .sound_channel_event = std::nullopt, .structural_slot = 0,
        .branch = std::nullopt, .provenance = {},
        .semantic_layers = resolve_placement_layers(context.chart, context.action.semantic_layers,
                                                    context.action.resource)});
    return true;
}

auto place_bga_background(resource_placement_context_t& context) -> bool {
    const auto kind = context.action.bga_kind.value_or(visual_event_kind_t::background);
    if (std::ranges::any_of(context.chart.bga_events, [&](const auto& event) {
            return editor_bga_lane_index(event.kind) == editor_bga_lane_index(kind) &&
                   event.position == context.position;
        })) {
        reject(context.result, diagnostic_code_t::collision,
               "a BGA event already occupies this lane and row");
        return false;
    }
    context.chart.bga_events.push_back({.id = context.first_id, .position = context.position,
        .resource = context.action.resource, .kind = kind,
        .provenance = {}});
    return true;
}

auto apply_resource_placement(document_history_t& history,
                              const place_resource_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (!validate_placement_resource(history.document, action, result)) return result;
    auto resolved_action = action;
    if (action.kind == resource_placement_kind_t::bga_background) {
        resolved_action.bga_kind = resolve_bga_resource_kind(history.document, action.resource,
            action.bga_kind.value_or(visual_event_kind_t::background), result);
        if (!resolved_action.bga_kind) return result;
    }
    auto target = resolve_placement_target(history.document, action, result);
    if (!target) return result;
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto& chart = proposed.charts[target->chart_offset];
    auto measure = chart.find_measure(target->position.measure);
    if (measure == nullptr) {
        reject(result, diagnostic_code_t::invalid_measure, "resource placement measure is missing");
        return result;
    }
    resource_placement_context_t placement{result, chart, *measure, target->index, resolved_action,
                                            target->position, target->row, target->first_id};
    bool placed{};
    switch (action.kind) {
    case resource_placement_kind_t::sample_note: placed = place_sample_note(placement); break;
    case resource_placement_kind_t::bgm_event: placed = place_bgm_event(placement); break;
    case resource_placement_kind_t::bga_background: placed = place_bga_background(placement); break;
    }
    if (!placed) return result;
    return transaction.commit("Place resource");
}

namespace {
auto run_apply_bga_stroke(document_command_transaction_t& transaction,
                      chart_id_t chart_id, std::optional<resource_id_t> resource,
                      std::span<const bga_stroke_target_t> targets, bool erase)
    -> document_command_result_t {
    auto& history = transaction.history;
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (targets.size() > 50'000) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "BGA stroke exceeds the per-gesture target cap");
        return result;
    }
    const auto source_chart = history.document.find_chart(chart_id);
    if (!chart_id || source_chart == nullptr) {
        reject(result, diagnostic_code_t::invalid_id, "BGA stroke chart is missing");
        return result;
    }
    const auto asset_kind = erase ? std::optional<asset_kind_t>{}
                                  : resolve_bga_stroke_resource(history.document, resource, result);
    if (!erase && !asset_kind) return result;
    const auto built = build_measure_index(*source_chart);
    const auto* index = std::get_if<measure_index_t>(&built);
    if (!index) {
        reject(result, diagnostic_code_t::invalid_measure, "BGA stroke chart index is invalid");
        return result;
    }
    auto resolved = resolve_bga_stroke_targets(*index, targets, asset_kind, erase, result);
    if (!resolved) return result;
    if (resolved->empty()) return result;
    const auto chart_offset = static_cast<std::size_t>(source_chart - history.document.charts.data());
    return commit_bga_stroke_targets(transaction, chart_offset, *index, *resolved, resource, erase);
}

} // namespace

auto apply_bga_stroke(document_command_transaction_t& transaction,
                      chart_id_t chart_id, std::optional<resource_id_t> resource,
                      std::span<const bga_stroke_target_t> targets, bool erase)
    -> document_command_result_t {
    if (!transaction.is_ready()) return transaction.finish_without_commit();
    return transaction.finish_with(run_apply_bga_stroke(transaction, chart_id, resource, targets, erase));
}

auto apply_bga_stroke(document_history_t& history,
                      chart_id_t chart_id, std::optional<resource_id_t> resource,
                      std::span<const bga_stroke_target_t> targets, bool erase)
    -> document_command_result_t {
    auto transaction = document_command_transaction_t::from(history);
    return apply_bga_stroke(transaction, chart_id, resource, targets, erase);
}

namespace {

struct bgm_stroke_cell_t {
    local_position_t position;
    std::uint64_t row{};
};

auto validate_bgm_stroke_lane(const chart_document_t& chart, object_id_t lane,
                              document_command_result_t& result) -> bool {
    if (std::ranges::find(chart.bgm_source_lanes, lane, &bgm_source_lane_t::id) ==
        chart.bgm_source_lanes.end()) {
        reject(result, diagnostic_code_t::invalid_id, "BGM stroke refers to an unknown source lane");
        return false;
    }
    return true;
}

auto has_bms_wav_identity(const resource_entry_t& resource) -> bool {
    return std::ranges::any_of(resource.target_identities, [](const auto& identity) {
        return identity.family == format_family_t::bms && identity.namespace_name == "WAV";
    });
}

auto validate_bgm_stroke_resource(const song_document_t& document, const chart_document_t& chart,
                                  std::optional<resource_id_t> id,
                                  document_command_result_t& result) -> bool {
    const auto entry = id ? std::ranges::find(document.resources, *id, &resource_entry_t::id)
                          : document.resources.end();
    if (entry == document.resources.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "BGM draw requires a sound from the catalog");
        return false;
    }
    if (entry->missing) {
        reject(result, diagnostic_code_t::dangling_resource,
               "cannot place a resource with a missing source");
        return false;
    }
    const auto audio = document.find_audio_asset(entry->asset);
    if ((!has_role(*entry, resource_role_t::bgm) && !has_role(*entry, resource_role_t::sample)) ||
        audio == nullptr) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "resource is incompatible with the requested placement");
        return false;
    }
    if (chart.source_family == format_family_t::bms && !has_bms_wav_identity(*entry) &&
        !find_bms_identifier_run(document, "WAV", 1)) {
        reject(result, diagnostic_code_t::invalid_id,
               "BMS sound IDs are exhausted; this sound cannot be given an ID");
        return false;
    }
    return true;
}

auto resolve_bgm_stroke_cells(const measure_index_t& index,
                              std::span<const local_position_t> targets,
                              document_command_result_t& result)
    -> std::optional<std::vector<bgm_stroke_cell_t>> {
    std::vector<bgm_stroke_cell_t> cells;
    cells.reserve(targets.size());
    std::set<std::uint64_t> seen;
    for (const auto& position : targets) {
        const auto row = absolute_row(index, position);
        if (!row) {
            reject(result, diagnostic_code_t::row_out_of_range,
                   "BGM stroke row is outside the chart");
            return std::nullopt;
        }
        if (seen.insert(*row).second) cells.push_back({position, *row});
    }
    return cells;
}

auto bgm_stroke_lane_events(const chart_document_t& chart, const measure_index_t& index,
                            object_id_t lane) -> std::map<std::uint64_t, const bgm_event_t*> {
    std::map<std::uint64_t, const bgm_event_t*> occupied;
    for (const auto& measure : chart.measures)
        for (const auto& event : measure.bgm_events) {
            const auto row = event.source_lane == lane ? absolute_row(index, event.position)
                                                       : std::nullopt;
            if (row) occupied.emplace(*row, &event);
        }
    return occupied;
}

auto commit_bgm_stroke_erase(document_command_transaction_t& transaction, std::size_t chart_offset,
                             const measure_index_t& index, object_id_t lane,
                             std::span<const bgm_stroke_cell_t> cells)
    -> document_command_result_t {
    auto& history = transaction.history;
    document_command_result_t unchanged{.revision = history.document.revision, .diagnostics = {}};
    std::set<std::uint64_t> rows;
    for (const auto& cell : cells) rows.insert(cell.row);
    auto& proposed = transaction.copy_document();
    std::size_t removed{};
    for (auto& measure : proposed.charts[chart_offset].measures)
        removed += std::erase_if(measure.bgm_events, [&](const auto& event) {
            const auto row = event.source_lane == lane && !event.sound_channel_event
                                 ? absolute_row(index, event.position) : std::nullopt;
            return row && rows.contains(*row);
        });
    if (!removed) return unchanged;
    return transaction.commit("Erase BGM events");
}

auto commit_bgm_stroke_draw(document_command_transaction_t& transaction, std::size_t chart_offset,
                            const measure_index_t& index, object_id_t lane, resource_id_t sound,
                            std::span<const bgm_stroke_cell_t> cells)
    -> document_command_result_t {
    auto& history = transaction.history;
    document_command_result_t unchanged{.revision = history.document.revision, .diagnostics = {}};
    const auto& source = history.document.charts[chart_offset];
    const auto occupied = bgm_stroke_lane_events(source, index, lane);
    std::vector<bgm_stroke_cell_t> additions;
    for (const auto& cell : cells) {
        const auto found = occupied.find(cell.row);
        if (found == occupied.end()) additions.push_back(cell);
        else if (found->second->sound != std::optional{sound}) {
            reject(unchanged, diagnostic_code_t::collision,
                   "a different sound already occupies this BGM lane and row");
            return unchanged;
        }
    }
    if (additions.empty()) return unchanged;
    const auto first_id = next_chart_object_id(source, default_maximum_chart_objects).id;
    if (!first_id || additions.size() - 1 >
                        std::numeric_limits<std::uint64_t>::max() - first_id->value) {
        reject(unchanged, diagnostic_code_t::invalid_id,
               "BGM stroke chart object limit or ID space is exhausted");
        return unchanged;
    }
    auto& proposed = transaction.copy_document();
    auto& chart = proposed.charts[chart_offset];
    const auto layers = resolve_placement_layers(chart, std::nullopt, sound);
    auto next_id = first_id->value;
    for (const auto& cell : additions) {
        const auto measure = chart.find_measure(cell.position.measure);
        if (measure == nullptr) {
            reject(unchanged, diagnostic_code_t::invalid_measure,
                   "BGM stroke measure is missing");
            return unchanged;
        }
        measure->bgm_events.push_back({.id = object_id_t{next_id++}, .position = cell.position,
            .source_lane = lane, .sound = sound, .sound_channel_event = std::nullopt,
            .structural_slot = 0, .branch = std::nullopt, .provenance = {},
            .semantic_layers = layers});
    }
    return transaction.commit("Draw BGM events");
}

} // namespace

namespace {
auto run_apply_bgm_stroke(document_command_transaction_t& transaction,
                      chart_id_t chart_id, object_id_t source_lane,
                      std::optional<resource_id_t> resource,
                      std::span<const local_position_t> targets, bool erase)
    -> document_command_result_t {
    auto& history = transaction.history;
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (targets.size() > 50'000) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "BGM stroke exceeds the per-gesture target cap");
        return result;
    }
    const auto source_chart = history.document.find_chart(chart_id);
    if (!chart_id || source_chart == nullptr) {
        reject(result, diagnostic_code_t::invalid_id, "BGM stroke chart is missing");
        return result;
    }
    if (source_chart->source_family == format_family_t::bmson)
        return apply_bmson_packed_stroke(transaction, chart_id, source_lane,
                                         resource, targets, erase);
    if (!validate_bgm_stroke_lane(*source_chart, source_lane, result)) return result;
    if (!erase && !validate_bgm_stroke_resource(history.document, *source_chart, resource, result))
        return result;
    const auto built = build_measure_index(*source_chart);
    const auto* index = std::get_if<measure_index_t>(&built);
    if (!index) {
        reject(result, diagnostic_code_t::invalid_measure, "BGM stroke chart index is invalid");
        return result;
    }
    const auto cells = resolve_bgm_stroke_cells(*index, targets, result);
    if (!cells || cells->empty()) return result;
    const auto chart_offset = static_cast<std::size_t>(source_chart - history.document.charts.data());
    return erase ? commit_bgm_stroke_erase(transaction, chart_offset, *index, source_lane, *cells)
                 : commit_bgm_stroke_draw(transaction, chart_offset, *index, source_lane, *resource,
                                          *cells);
}



} // namespace

auto apply_bgm_stroke(document_command_transaction_t& transaction,
                      chart_id_t chart_id, object_id_t source_lane,
                      std::optional<resource_id_t> resource,
                      std::span<const local_position_t> targets, bool erase)
    -> document_command_result_t {
    if (!transaction.is_ready()) return transaction.finish_without_commit();
    return transaction.finish_with(run_apply_bgm_stroke(transaction, chart_id, source_lane, resource, targets, erase));
}

auto apply_bgm_stroke(document_history_t& history,
                      chart_id_t chart_id, object_id_t source_lane,
                      std::optional<resource_id_t> resource,
                      std::span<const local_position_t> targets, bool erase)
    -> document_command_result_t {
    auto transaction = document_command_transaction_t::from(history);
    return apply_bgm_stroke(transaction, chart_id, source_lane, resource, targets, erase);
}

auto is_branch_bgm_event(const chart_document_t& chart, const bgm_event_t& event) -> bool {
    return event.sound_channel_event || event.branch ||
        (event.provenance.family == format_family_t::bms &&
         !event.provenance.source_spelling.empty() &&
         branch_source_orders(chart).contains(event.provenance.source_order));
}

auto occupied_bgm_lanes_at(const chart_document_t& chart, local_position_t position,
                           object_id_t except) -> std::unordered_set<std::uint64_t> {
    std::unordered_set<std::uint64_t> occupied;
    for (const auto& measure : chart.measures)
        for (const auto& event : measure.bgm_events)
            if (event.id != except && event.position == position)
                occupied.insert(event.source_lane.value);
    return occupied;
}

auto create_bgm_source_lane(chart_document_t& chart, document_command_result_t& result)
    -> object_id_t {
    const auto new_id = next_chart_object_id(chart, default_maximum_chart_objects).id;
    if (!new_id || chart.bgm_source_lanes.size() >= default_maximum_chart_objects) {
        reject(result, diagnostic_code_t::invalid_id, "BGM source lane ID space is exhausted");
        return {};
    }
    std::size_t order{};
    for (const auto& lane : chart.bgm_source_lanes) {
        if (lane.source_order == std::numeric_limits<std::size_t>::max()) {
            reject(result, diagnostic_code_t::invalid_id,
                   "BGM source lane order space is exhausted");
            return {};
        }
        order = std::max(order, lane.source_order + 1);
    }
    const object_id_t id = *new_id;
    chart.bgm_source_lanes.push_back({.id = id,
        .name = "BGM " + std::to_string(chart.bgm_source_lanes.size() + 1),
        .source_order = order});
    return id;
}

auto apply_bgm_event_move(document_history_t& history,
                          const move_bgm_event_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    const auto source_chart = history.document.find_chart(action.chart);
    if (!action.chart || source_chart == nullptr || !action.event) {
        reject(result, diagnostic_code_t::invalid_id, "BGM move chart or event is missing");
        return result;
    }
    const auto built = build_measure_index(*source_chart);
    if (!std::holds_alternative<measure_index_t>(built)) {
        reject(result, diagnostic_code_t::invalid_measure, "BGM move chart index is invalid");
        return result;
    }
    const auto target = snap_placement_position(*source_chart,
                                                std::get<measure_index_t>(built),
                                                action.position, action.snap_denominator);
    if (!target) {
        reject(result, diagnostic_code_t::row_out_of_range, "BGM move row is outside the chart");
        return result;
    }
    const auto* original = (*source_chart).find_bgm_event(action.event);
    if (!original) {
        reject(result, diagnostic_code_t::invalid_id, "BGM move event no longer exists");
        return result;
    }
    std::optional<local_position_t> moved_end;
    if (original->end) {
        if (!(moved_end = translate_bgm_end(std::get<measure_index_t>(built),
                                            original->position, *target, *original->end))) {
            reject(result, diagnostic_code_t::row_out_of_range,
                   "BGM move would place its end outside the chart");
            return result;
        }
    }
    if (is_branch_bgm_event(*source_chart, *original)) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "BGM move requires an ordinary root event");
        return result;
    }
    const auto preferred = action.source_lane ? action.source_lane : original->source_lane;
    const auto preferred_lane = std::ranges::find(source_chart->bgm_source_lanes, preferred,
                                                 &bgm_source_lane_t::id);
    if (preferred_lane == source_chart->bgm_source_lanes.end()) {
        reject(result, diagnostic_code_t::invalid_id, "BGM move source lane is missing");
        return result;
    }
    const auto occupied = occupied_bgm_lanes_at(*source_chart, *target, action.event);
    std::vector<const bgm_source_lane_t*> lanes;
    lanes.reserve(source_chart->bgm_source_lanes.size());
    for (const auto& lane : source_chart->bgm_source_lanes)
        lanes.push_back(&lane);
    std::ranges::sort(lanes, [](const auto* left, const auto* right) {
        return std::tie(left->source_order, left->id.value) <
               std::tie(right->source_order, right->id.value);
    });
    const auto start = std::ranges::find(lanes, &*preferred_lane);
    object_id_t destination_lane{};
    for (auto it = start; it != lanes.end(); ++it) {
        if (!occupied.contains((*it)->id.value)) {
            destination_lane = (*it)->id;
            break;
        }
    }
    if (*target == original->position && destination_lane == original->source_lane)
        return result;
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto& chart = proposed.charts[static_cast<std::size_t>(source_chart - history.document.charts.data())];
    if (!destination_lane) {
        destination_lane = create_bgm_source_lane(chart, result);
        if (!destination_lane) return result;
    }
    bgm_event_t moved;
    for (auto& measure : chart.measures) {
        const auto found = std::ranges::find(measure.bgm_events, action.event, &bgm_event_t::id);
        if (found == measure.bgm_events.end())
            continue;
        moved = std::move(*found);
        measure.bgm_events.erase(found);
        break;
    }
    moved.position = *target;
    moved.end = moved_end;
    moved.source_lane = destination_lane;
    moved.structural_slot = 0;
    moved.provenance = {};
    const auto destination = chart.find_measure(target->measure);
    destination->bgm_events.push_back(std::move(moved));
    return transaction.commit("Move BGM event");
}

auto apply_bga_event_move(document_history_t& history,
                          const move_bga_event_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    const auto source_chart = history.document.find_chart(action.chart);
    if (!action.chart || source_chart == nullptr || !action.event) {
        reject(result, diagnostic_code_t::invalid_id, "BGA move chart or event is missing");
        return result;
    }
    const auto found = std::ranges::find(source_chart->bga_events, action.event, &bga_event_t::id);
    if (found == source_chart->bga_events.end()) {
        reject(result, diagnostic_code_t::invalid_id, "BGA move event no longer exists");
        return result;
    }
    if (!editor_bga_lane_index(found->kind)) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "This visual event has no editable BGA lane");
        return result;
    }
    const auto built = build_measure_index(*source_chart);
    if (!std::holds_alternative<measure_index_t>(built)) {
        reject(result, diagnostic_code_t::invalid_measure, "BGA move chart index is invalid");
        return result;
    }
    const auto target = snap_placement_position(*source_chart,
                                                std::get<measure_index_t>(built),
                                                action.position, action.snap_denominator);
    if (!target) {
        reject(result, diagnostic_code_t::row_out_of_range, "BGA move row is outside the chart");
        return result;
    }
    const auto kind = resolve_bga_resource_kind(history.document, found->resource,
        action.bga_kind.value_or(found->kind), result);
    if (!kind) return result;
    if (*target == found->position && *kind == found->kind) return result;
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    const auto chart_offset = static_cast<std::size_t>(source_chart - history.document.charts.data());
    const auto moved = std::ranges::find(proposed.charts[chart_offset].bga_events,
                                         action.event, &bga_event_t::id);
    if (moved == proposed.charts[chart_offset].bga_events.end()) {
        reject(result, diagnostic_code_t::invalid_id, "BGA move event no longer exists");
        return result;
    }
    moved->position = *target;
    moved->kind = *kind;
    return transaction.commit("Move BGA event");
}

auto apply_virtual_bms_resource_placement(
    document_history_t& history,
    const place_virtual_bms_resource_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    const auto source_chart = history.document.find_chart(action.chart);
    if (source_chart == nullptr ||
        source_chart->source_family != format_family_t::bms) {
        reject(result, diagnostic_code_t::invalid_id,
               "virtual resource placement requires a BMS chart");
        return result;
    }
    const auto built = build_virtual_canvas(*source_chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built)) {
        result.diagnostics = *diagnostics;
        return result;
    }
    const auto& canvas = std::get<virtual_canvas_t>(built);
    if (action.canvas_row >= canvas.total_rows ||
        (action.snap_denominator != 0 &&
         source_chart->divisions_per_standard_measure % action.snap_denominator != 0)) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "virtual resource placement row or snap is invalid");
        return result;
    }
    auto snapped_row = action.canvas_row;
    if (action.snap_denominator != 0) {
        const auto rounded =
            snap_canvas_row(*source_chart, canvas.authored_index, snapped_row,
                            action.snap_denominator, canvas.total_rows);
        if (!rounded) {
            reject(result, diagnostic_code_t::row_out_of_range,
                   "virtual snap row is invalid or overflows");
            return result;
        }
        snapped_row = *rounded;
    }
    const auto destination = canvas.measure_containing(snapped_row);
    if (!destination || destination->authored ||
        destination->number >= bms_addressable_measure_count) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "virtual resource placement requires an unmaterialized BMS measure");
        return result;
    }
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto& chart = proposed.charts[static_cast<std::size_t>(
        source_chart - history.document.charts.data())];
    const auto required = destination->number + 1 - chart.measures.size();
    std::uint64_t last_measure_id{};
    for (const auto& measure : chart.measures)
        last_measure_id = std::max(last_measure_id, measure.id.value);
    if (required > std::numeric_limits<std::uint64_t>::max() - last_measure_id) {
        reject(result, diagnostic_code_t::invalid_id,
               "virtual resource measure ID sequence is exhausted");
        return result;
    }
    chart.measures.reserve(destination->number + 1);
    for (std::size_t added = 0; added < required; ++added) {
        chart.measures.push_back({.id = measure_id_t{++last_measure_id},
                                  .definition = {{1, 1}, meter_t{4, 4}}});
    }
    const local_position_t position{chart.measures[destination->number].id,
                                    snapped_row - destination->begin_row};
    auto staged = document_history_t::from(std::move(proposed));
    const auto placed = apply_resource_placement(
        staged,
        {.chart = action.chart,
         .resource = action.resource,
         .position = position,
         .lane = action.lane,
         .source_lane = action.source_lane,
         .kind = action.kind,
         .semantic_layers = action.semantic_layers,
         .bga_kind = action.bga_kind});
    if (!placed) {
        result.diagnostics = placed.diagnostics;
        return result;
    }
    transaction.adopt_document(std::move(staged.document));
    return transaction.commit("Place resource in virtual BMS measure");
}

auto apply_virtual_bms_bgm_event_move(
    document_history_t& history,
    const move_virtual_bms_bgm_event_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    const auto source_chart = history.document.find_chart(action.chart);
    if (!action.chart || source_chart == nullptr ||
        source_chart->source_family != format_family_t::bms) {
        reject(result, diagnostic_code_t::invalid_id,
               "virtual BGM move requires a BMS chart");
        return result;
    }
    const auto built = build_virtual_canvas(*source_chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built)) {
        result.diagnostics = *diagnostics;
        return result;
    }
    const auto& canvas = std::get<virtual_canvas_t>(built);
    if (action.canvas_row >= canvas.total_rows ||
        (action.snap_denominator != 0 &&
         source_chart->divisions_per_standard_measure % action.snap_denominator != 0)) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "virtual BGM move row or snap is invalid");
        return result;
    }
    auto snapped_row = action.canvas_row;
    if (action.snap_denominator != 0) {
        const auto rounded =
            snap_canvas_row(*source_chart, canvas.authored_index, snapped_row,
                            action.snap_denominator, canvas.total_rows);
        if (!rounded) {
            reject(result, diagnostic_code_t::row_out_of_range,
                   "virtual snap row is invalid or overflows");
            return result;
        }
        snapped_row = *rounded;
    }
    const auto destination = canvas.measure_containing(snapped_row);
    if (!destination || destination->authored ||
        destination->number >= bms_addressable_measure_count) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "virtual BGM move requires an unmaterialized BMS measure");
        return result;
    }
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto& chart = proposed.charts[static_cast<std::size_t>(
        source_chart - history.document.charts.data())];
    const auto required = destination->number + 1 - chart.measures.size();
    std::uint64_t last_measure_id{};
    for (const auto& measure : chart.measures)
        last_measure_id = std::max(last_measure_id, measure.id.value);
    if (required > std::numeric_limits<std::uint64_t>::max() - last_measure_id) {
        reject(result, diagnostic_code_t::invalid_id,
               "virtual BGM move measure ID sequence is exhausted");
        return result;
    }
    chart.measures.reserve(destination->number + 1);
    for (std::size_t added = 0; added < required; ++added) {
        chart.measures.push_back({.id = measure_id_t{++last_measure_id},
                                  .definition = {{1, 1}, meter_t{4, 4}}});
    }
    const local_position_t position{chart.measures[destination->number].id,
                                    snapped_row - destination->begin_row};
    auto staged = document_history_t::from(std::move(proposed));
    const auto moved = apply_bgm_event_move(
        staged,
        {.chart = action.chart, .event = action.event, .position = position,
         .source_lane = action.source_lane});
    if (!moved) {
        result.diagnostics = moved.diagnostics;
        return result;
    }
    transaction.adopt_document(std::move(staged.document));
    return transaction.commit("Move BGM event into virtual BMS measure");
}

auto apply_virtual_bms_bga_event_move(
    document_history_t& history,
    const move_virtual_bms_bga_event_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    const auto source_chart = history.document.find_chart(action.chart);
    if (!action.chart || source_chart == nullptr ||
        source_chart->source_family != format_family_t::bms || !action.event) {
        reject(result, diagnostic_code_t::invalid_id, "virtual BGA move requires a BMS chart and event");
        return result;
    }
    const auto built = build_virtual_canvas(*source_chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built)) {
        result.diagnostics = *diagnostics;
        return result;
    }
    const auto& canvas = std::get<virtual_canvas_t>(built);
    if (action.canvas_row >= canvas.total_rows ||
        (action.snap_denominator != 0 &&
         source_chart->divisions_per_standard_measure % action.snap_denominator != 0)) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "virtual BGA move row or snap is invalid");
        return result;
    }
    auto snapped_row = action.canvas_row;
    if (action.snap_denominator != 0) {
        const auto rounded = snap_canvas_row(*source_chart, canvas.authored_index, snapped_row,
                            action.snap_denominator, canvas.total_rows);
        if (!rounded) {
            reject(result, diagnostic_code_t::row_out_of_range, "virtual BGA snap row is invalid");
            return result;
        }
        snapped_row = *rounded;
    }
    const auto destination = canvas.measure_containing(snapped_row);
    if (!destination || destination->authored ||
        destination->number >= bms_addressable_measure_count) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "virtual BGA move requires an unmaterialized BMS measure");
        return result;
    }
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto& chart = proposed.charts[static_cast<std::size_t>(
        source_chart - history.document.charts.data())];
    const auto required = destination->number + 1 - chart.measures.size();
    std::uint64_t last_measure_id{};
    for (const auto& measure : chart.measures)
        last_measure_id = std::max(last_measure_id, measure.id.value);
    if (required > std::numeric_limits<std::uint64_t>::max() - last_measure_id) {
        reject(result, diagnostic_code_t::invalid_id, "virtual BGA measure ID sequence is exhausted");
        return result;
    }
    chart.measures.reserve(destination->number + 1);
    for (std::size_t added = 0; added < required; ++added)
        chart.measures.push_back({.id = measure_id_t{++last_measure_id},
                                  .definition = {{1, 1}, meter_t{4, 4}}});
    const local_position_t position{chart.measures[destination->number].id,
                                    snapped_row - destination->begin_row};
    auto staged = document_history_t::from(std::move(proposed));
    const auto moved = apply_bga_event_move(staged,
        {.chart = action.chart, .event = action.event, .position = position, .bga_kind = action.bga_kind});
    if (!moved) {
        result.diagnostics = moved.diagnostics;
        return result;
    }
    transaction.adopt_document(std::move(staged.document));
    return transaction.commit("Move BGA event into virtual BMS measure");
}

auto next_eligible_placement_resource(const song_document_t& document,
                                      resource_id_t current, resource_role_t role)
    -> std::optional<resource_id_t> {
    if (!current || (role != resource_role_t::sample && role != resource_role_t::bgm &&
                     role != resource_role_t::bga)) {
        return std::nullopt;
    }
    const auto start = std::ranges::find(document.resources, current, &resource_entry_t::id);
    if (start == document.resources.end()) {
        return std::nullopt;
    }
    for (auto it = start + 1; it != document.resources.end(); ++it) {
        if (it->missing || !it->id || !it->asset ||
            std::ranges::find(it->roles, role) == it->roles.end()) {
            continue;
        }
        const auto valid_asset = role == resource_role_t::bga
            ? std::ranges::any_of(document.visual_assets, [&](const auto& asset) {
                  return asset.id == it->asset && asset.kind == asset_kind_t::image;
              })
            : std::ranges::any_of(document.audio_assets, [&](const auto& asset) {
                  return asset.id == it->asset;
              });
        if (valid_asset) {
            return it->id;
        }
    }
    return std::nullopt;
}

auto apply_resource_creation(document_history_t& history,
                             const create_resource_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (history.document.resources.size() >= 100'000 || !text_util::is_valid_bounded_text(action.name, 255)) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "resource catalog is full or the resource name is invalid");
        return result;
    }
    const auto audio_role = action.role == resource_role_t::sample ||
                            action.role == resource_role_t::bgm;
    const auto visual_role = action.role == resource_role_t::bga;
    if (!audio_role && !visual_role) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "resource creation requires a Sample, BGM, or BGA role");
        return result;
    }
    const auto& document = history.document;
    const auto audio = document.find_audio_asset(action.asset);
    const auto visual =
        std::ranges::find(document.visual_assets, action.asset, &visual_asset_t::id);
    if (!action.asset || (audio_role && audio == nullptr) ||
        (visual_role && visual == document.visual_assets.end())) {
        reject(result, diagnostic_code_t::dangling_asset,
               "resource creation requires a registered asset of the selected type");
        return result;
    }
    std::uint64_t largest_id{};
    for (const auto& resource : document.resources) {
        largest_id = std::max(largest_id, resource.id.value);
    }
    if (largest_id == std::numeric_limits<std::uint64_t>::max()) {
        reject(result, diagnostic_code_t::invalid_id, "resource ID space is exhausted");
        return result;
    }
    auto name = action.name;
    if (name.empty()) {
        const auto& source_path = audio_role ? audio->source_path : visual->source_path;
        name = source_path.filename().generic_string();
    }
    if (!text_util::is_valid_bounded_text(name, 255)) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "resource source filename exceeds the bounded name limit");
        return result;
    }
    resource_entry_t resource{.id = resource_id_t{largest_id + 1},
                              .name = std::move(name),
                              .asset = action.asset,
                              .roles = {action.role},
                              .target_identities = {},
                              .missing = false,
                              .provenance = {}};
    if (action.allocate_bms_identifier) {
        const auto identity = next_bms_resource_identity(document, visual_role);
        if (!identity) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "BMS resource namespace has no free nonzero identifier");
            return result;
        }
        resource.target_identities.push_back(*identity);
    }
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    proposed.resources.push_back(std::move(resource));
    return transaction.commit("Create resource");
}

auto next_resource_asset_id(const song_document_t& document) -> std::optional<asset_id_t> {
    const auto asset_count =
        document.audio_assets.size() + document.visual_assets.size() + document.midi_assets.size();
    if (asset_count >= 100'000)
        return std::nullopt;
    std::uint64_t largest{};
    for (const auto& item : document.audio_assets)
        largest = std::max(largest, item.id.value);
    for (const auto& item : document.visual_assets)
        largest = std::max(largest, item.id.value);
    for (const auto& item : document.midi_assets)
        largest = std::max(largest, item.id.value);
    if (largest == std::numeric_limits<std::uint64_t>::max())
        return std::nullopt;
    return asset_id_t{largest + 1};
}

auto validate_import_source_paths(const import_resource_asset_action_t& action,
                                  document_command_result_t& result)
    -> std::optional<std::pair<std::filesystem::path, std::optional<std::filesystem::path>>> {
    const auto path = action.source_path.lexically_normal();
    const auto text = path.generic_string();
    if (!path.is_absolute() || path.filename().empty() || !text_util::is_valid_bounded_text(text, 4096)) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource import requires a bounded absolute source file path");
        return std::nullopt;
    }
    if ((action.copied_project_relative_path || action.expected_new_asset_id) &&
        (!action.expected_new_asset_id || !*action.expected_new_asset_id)) {
        reject(result, diagnostic_code_t::invalid_id,
               "copied import requires a valid reserved asset ID");
        return std::nullopt;
    }
    std::optional<std::filesystem::path> copied;
    if (action.copied_project_relative_path) {
        const auto candidate = action.copied_project_relative_path->lexically_normal();
        const auto copied_text = candidate.generic_string();
        if (candidate.empty() || candidate.is_absolute() || candidate.has_root_name() ||
            candidate.has_root_directory() ||
            candidate.filename().empty() || !text_util::is_valid_bounded_text(copied_text, 4096) ||
            std::ranges::any_of(candidate, [](const auto& part) { return part == ".."; }) ||
            candidate.parent_path() != "assets" ||
            !candidate.filename().string().starts_with(
                std::to_string(action.expected_new_asset_id->value) + "-")) {
            reject(result, diagnostic_code_t::invalid_id,
                   "copied import path must be a safe ID-prefixed project asset path");
            return std::nullopt;
        }
        copied = candidate;
    }
    return std::pair{path, copied};
}

auto validate_import_source_geometry(const import_resource_asset_action_t& action,
                                     document_command_result_t& result) -> bool {
    const bool audio = action.role == resource_role_t::sample || action.role == resource_role_t::bgm;
    const bool visual = action.role == resource_role_t::bga;
    if ((!audio && !visual) || (audio && (!action.audio_stream || action.image_geometry)) ||
        (visual && (!action.image_geometry || action.audio_stream))) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "resource import role and decoded source type do not match");
        return false;
    }
    if ((audio && (action.audio_stream->sample_rate == 0 || action.audio_stream->channels == 0 ||
                   action.audio_stream->channels > 32)) ||
        (visual && (action.image_geometry->first == 0 || action.image_geometry->second == 0 ||
                    action.image_geometry->first > 16'384 || action.image_geometry->second > 16'384))) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "resource import decoded source geometry is invalid");
        return false;
    }
    return true;
}

auto find_registered_import_asset(const song_document_t& document,
                                 const import_resource_asset_action_t& action,
                                 const std::filesystem::path& path,
                                 bool audio_role, bool visual_role) -> asset_id_t {
    if (audio_role && !action.copied_project_relative_path) {
        const auto found = std::ranges::find_if(document.audio_assets, [&](const auto& item) {
            return item.source_path.lexically_normal() == path;
        });
        return found == document.audio_assets.end() ? asset_id_t{} : found->id;
    }
    if (visual_role && !action.copied_project_relative_path) {
        const auto found = std::ranges::find_if(document.visual_assets, [&](const auto& item) {
            return item.kind == asset_kind_t::image && item.source_path.lexically_normal() == path;
        });
        return found == document.visual_assets.end() ? asset_id_t{} : found->id;
    }
    return {};
}

auto append_imported_asset(song_document_t& document, const import_resource_asset_action_t& action,
                           const std::filesystem::path& source, asset_id_t& asset,
                           document_command_result_t& result) -> bool {
    const auto next_id = next_resource_asset_id(document);
    if (!next_id) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "resource import asset catalog is full or IDs are exhausted");
        return false;
    }
    asset = *next_id;
    if (action.expected_new_asset_id && asset != *action.expected_new_asset_id) {
        reject(result, diagnostic_code_t::invalid_id,
               "reserved asset ID changed before copied import could commit");
        return false;
    }
    if (action.copied_project_relative_path &&
        (std::ranges::any_of(document.audio_assets, [&](const auto& item) {
             return item.source_path.lexically_normal() == source;
         }) || std::ranges::any_of(document.visual_assets, [&](const auto& item) {
             return item.source_path.lexically_normal() == source;
         }))) {
        reject(result, diagnostic_code_t::invalid_id,
               "copied import path is already registered to another asset");
        return false;
    }
    if (action.role == resource_role_t::sample || action.role == resource_role_t::bgm)
        document.audio_assets.push_back({.id = asset, .source_path = source,
            .stream = *action.audio_stream, .waveform_cache_path = std::nullopt});
    else
        document.visual_assets.push_back({.id = asset, .kind = asset_kind_t::image,
            .source_path = source, .width = action.image_geometry->first,
            .height = action.image_geometry->second});
    return true;
}

auto extend_native_charts_to_audio(song_document_t& document, const audio_stream_info_t& stream,
                                   document_command_result_t& result) -> bool {
    for (auto& chart : document.charts) {
        if (chart.source_family == format_family_t::native_project &&
            !extend_chart_to_audio_end(chart, stream)) {
            reject(result, diagnostic_code_t::invalid_measure,
                   "imported audio exceeds the chart's timing or measure limits");
            return false;
        }
    }
    return true;
}

// The first catalog entry with the Music role whose asset is audio; exports and
// the transport treat it as the song's main audio.
auto find_primary_music_resource(const song_document_t& document)
    -> std::vector<resource_entry_t>::const_iterator {
    return std::ranges::find_if(document.resources, [&](const resource_entry_t& resource) {
        return std::ranges::find(resource.roles, resource_role_t::music) != resource.roles.end() &&
               document.find_audio_asset(resource.asset) !=
                   nullptr;
    });
}

auto import_primary_music(document_history_t& history,
                          const import_resource_asset_action_t& action)
    -> document_command_result_t {
    auto draft = document_history_t::from(history.document, 1, false);
    auto imported = apply_resource_asset_import(draft, action);
    if (!imported.applied) {
        imported.revision = history.document.revision;
        return imported;
    }
    auto& roles = draft.document.resources.back().roles;
    if (std::ranges::find(roles, resource_role_t::music) == roles.end())
        roles.insert(roles.begin(), resource_role_t::music);
    auto transaction = document_command_transaction_t::from(history);
    transaction.adopt_document(std::move(draft.document));
    return transaction.commit("Set song audio");
}

// Repoints the existing music asset so every resource and event that plays the
// song keeps its reference.
auto relink_primary_music(document_history_t& history, const resource_entry_t& music,
                          const std::filesystem::path& source,
                          const import_resource_asset_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    const auto asset = proposed.find_audio_asset(music.asset);
    if (asset->source_path.lexically_normal() == source) return result;
    asset->source_path = source;
    asset->stream = *action.audio_stream;
    asset->waveform_cache_path.reset();
    if (!action.name.empty()) {
        const auto resource = std::ranges::find(proposed.resources, music.id, &resource_entry_t::id);
        resource->name = action.name;
    }
    if (!extend_native_charts_to_audio(proposed, *action.audio_stream, result)) return result;
    return transaction.commit("Replace song audio");
}

auto apply_resource_asset_import(document_history_t& history,
                                 const import_resource_asset_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    const auto paths = validate_import_source_paths(action, result);
    if (!paths || !validate_import_source_geometry(action, result)) return result;
    const auto& path = paths->first;
    const auto& registered_copy = paths->second;
    const auto audio_role = action.role == resource_role_t::sample || action.role == resource_role_t::bgm;
    const auto visual_role = action.role == resource_role_t::bga;
    const auto& document = history.document;
    auto asset = find_registered_import_asset(document, action, path, audio_role, visual_role);
    if (asset && action.expected_new_asset_id) {
        reject(result, diagnostic_code_t::invalid_id,
               "reserved import expected a new asset but the source is already registered");
        return result;
    }
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    if (!asset) {
        if (!append_imported_asset(proposed, action, registered_copy.value_or(path), asset, result))
            return result;
    }
    auto draft = document_history_t::from(std::move(proposed), 1, false);
    const auto created =
        apply_resource_creation(draft,
                                {.asset = asset,
                                 .role = action.role,
                                 .name = action.name,
                                 .allocate_bms_identifier = action.allocate_bms_identifier});
    if (!created.applied) {
        result.diagnostics = created.diagnostics;
        return result;
    }
    if (action.role == resource_role_t::bgm &&
        !extend_native_charts_to_audio(draft.document, *action.audio_stream, result))
        return result;
    transaction.adopt_document(std::move(draft.document));
    return transaction.commit("Import resource asset");
}

namespace {

auto decode_supported_wav_identifier(std::string_view identifier) -> std::optional<std::size_t> {
    if (identifier.size() != 2) return std::nullopt;
    const auto high = bms36_value(static_cast<unsigned char>(identifier[0]));
    const auto low = bms36_value(static_cast<unsigned char>(identifier[1]));
    if (high < 0 || low < 0) return std::nullopt;
    const auto value = static_cast<std::size_t>(high) * 36 + static_cast<std::size_t>(low);
    return value == 0 ? std::nullopt : std::optional{value};
}

auto is_bms_wav_identity(const target_resource_identity_t& identity) -> bool {
    if (identity.family != format_family_t::bms || identity.namespace_name.size() != 3) return false;
    return text_util::ascii_iequal(identity.namespace_name, "WAV");
}

auto find_wav_slot_owner(const song_document_t& song, std::size_t value) -> bms_wav_slot_owner_t {
    bms_wav_slot_owner_t result;
    for (const auto& resource : song.resources) {
        const auto owns_slot = std::ranges::any_of(resource.target_identities, [value](const auto& id) {
            return bms_wav_slot_value(id) == value;
        });
        if (!owns_slot) continue;
        if (result.resource && result.resource != resource.id) result.ambiguous = true;
        else result.resource = resource.id;
    }
    return result;
}

auto replace_imported_resource_with_exact_slot(document_history_t& draft,
                                               std::string_view identifier,
                                               document_command_result_t& result) -> bool {
    if (draft.document.resources.empty()) {
        reject(result, diagnostic_code_t::invalid_id,
               "WAV slot import did not create a sound resource");
        return false;
    }
    auto& resource = draft.document.resources.back();
    std::erase_if(resource.target_identities, [](const auto& identity) {
        return is_bms_wav_identity(identity);
    });
    const auto value = decode_supported_wav_identifier(identifier);
    if (!value) return false;
    resource.target_identities.push_back(
        {format_family_t::bms, encode_bms_identifier(*value, 36), "WAV"});
    return true;
}

auto batch_slot_files(assign_bms_wav_slot_batch_action_t action,
                      std::size_t first_slot,
                      document_command_result_t& result)
    -> std::optional<std::vector<import_resource_asset_action_t>> {
    if (action.imported_files.empty() || action.imported_files.size() > 1295) {
        reject(result, diagnostic_code_t::invalid_id,
               "BMS WAV slot batch needs between 1 and 1295 files");
        return std::nullopt;
    }
    if (action.imported_files.size() - 1 > 1295 - first_slot) {
        reject(result, diagnostic_code_t::invalid_id,
               "BMS WAV slot batch would continue beyond ZZ");
        return std::nullopt;
    }
    auto paths = std::unordered_set<std::string>{};
    for (const auto& file : action.imported_files) {
        const auto path = file.source_path.lexically_normal();
        if (!paths.insert(path.generic_string()).second) {
            reject(result, diagnostic_code_t::invalid_id,
                   "BMS WAV slot batch contains a duplicate input path");
            return std::nullopt;
        }
        if (file.role != resource_role_t::sample) {
            reject(result, diagnostic_code_t::unsupported_feature,
                   "BMS WAV slot batch requires sample imports");
            return std::nullopt;
        }
    }
    std::ranges::sort(action.imported_files, [](const auto& left, const auto& right) {
        const auto left_name = left.source_path.filename().generic_string();
        const auto right_name = right.source_path.filename().generic_string();
        if (natural_name_less(left_name, right_name)) return true;
        if (natural_name_less(right_name, left_name)) return false;
        return left.source_path.generic_string() < right.source_path.generic_string();
    });
    return std::move(action.imported_files);
}

auto batch_slot_owners(const song_document_t& document, std::size_t first_slot,
                       std::size_t count, bool confirm_replacements,
                       document_command_result_t& result)
    -> std::optional<std::vector<resource_id_t>> {
    auto owners = std::vector<resource_id_t>{};
    auto replaced = std::unordered_set<std::uint64_t>{};
    owners.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto owner = find_wav_slot_owner(document, first_slot + index);
        if (owner.ambiguous) {
            reject(result, diagnostic_code_t::invalid_id,
                   "a destination WAV slot is assigned to more than one sound");
            return std::nullopt;
        }
        if (owner.resource && !confirm_replacements) {
            reject(result, diagnostic_code_t::invalid_id,
                   "replacing occupied WAV slots requires explicit confirmation");
            return std::nullopt;
        }
        if (owner.resource && !replaced.insert(owner.resource.value).second) {
            reject(result, diagnostic_code_t::invalid_id,
                   "multiple destination WAV slots refer to the same sound");
            return std::nullopt;
        }
        owners.push_back(owner.resource);
    }
    return owners;
}

auto import_slot_batch_entry(document_history_t& draft, import_resource_asset_action_t file,
                             std::size_t slot, resource_id_t owner,
                             document_command_result_t& result) -> bool {
    file.allocate_bms_identifier = false;
    const auto imported = apply_resource_asset_import(draft, file);
    if (!imported) {
        result.diagnostics.insert(result.diagnostics.end(), imported.diagnostics.begin(),
                                  imported.diagnostics.end());
        return false;
    }
    if (!owner) return replace_imported_resource_with_exact_slot(
        draft, encode_bms_identifier(slot, 36), result);
    const auto temporary = draft.document.resources.back();
    std::erase_if(draft.document.resources, [&temporary](const auto& resource) {
        return resource.id == temporary.id;
    });
    const auto target = std::ranges::find(draft.document.resources, owner,
                                          &resource_entry_t::id);
    if (target == draft.document.resources.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "an occupied WAV slot sound disappeared during batch import");
        return false;
    }
    target->asset = temporary.asset;
    target->missing = false;
    if (!file.name.empty()) target->name = file.name;
    return true;
}

} // namespace

auto bms_wav_slot_value(const target_resource_identity_t& identity) -> std::optional<std::size_t> {
    if (!is_bms_wav_identity(identity)) return std::nullopt;
    return decode_supported_wav_identifier(identity.identifier);
}

auto bms_wav_slot_owner(const song_document_t& document, std::size_t slot) -> bms_wav_slot_owner_t {
    return find_wav_slot_owner(document, slot);
}

auto apply_bms_wav_slot_assignment(document_history_t& history,
                                   const assign_bms_wav_slot_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    const auto value = decode_supported_wav_identifier(action.identifier);
    if (!value) {
        reject(result, diagnostic_code_t::invalid_id,
               "BMS WAV slot must be a supported nonzero base-36 identifier");
        return result;
    }
    if (action.imported_file.role != resource_role_t::sample) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "BMS WAV slot assignment requires a sample import");
        return result;
    }
    const auto owner = find_wav_slot_owner(history.document, *value);
    if (owner.ambiguous) {
        reject(result, diagnostic_code_t::invalid_id,
               "BMS WAV slot is assigned to more than one sound");
        return result;
    }
    if (owner.resource && !action.confirm_replacement) {
        reject(result, diagnostic_code_t::invalid_id,
               "replacing an occupied BMS WAV slot requires confirmation");
        return result;
    }

    auto draft = document_history_t::from(history.document, 1, false);
    if (!import_slot_batch_entry(draft, action.imported_file, *value, owner.resource, result)) return result;
    auto transaction = document_command_transaction_t::from(history);
    transaction.adopt_document(std::move(draft.document));
    return transaction.commit(owner.resource ? "Replace BMS WAV slot sound" : "Assign BMS WAV slot");
}

auto apply_bms_wav_slot_batch_assignment(document_history_t& history,
                                         const assign_bms_wav_slot_batch_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    const auto first_slot = decode_supported_wav_identifier(action.first_identifier);
    if (!first_slot) {
        reject(result, diagnostic_code_t::invalid_id,
               "BMS WAV slot batch must start at a supported nonzero identifier");
        return result;
    }
    auto files = batch_slot_files(action, *first_slot, result);
    if (!files) return result;
    const auto owners = batch_slot_owners(history.document, *first_slot, files->size(),
                                          action.confirm_replacements, result);
    if (!owners) return result;
    auto draft = document_history_t::from(history.document, 1, false);
    for (std::size_t index = 0; index < files->size(); ++index) {
        if (!import_slot_batch_entry(draft, std::move((*files)[index]), *first_slot + index,
                                     (*owners)[index], result)) return result;
    }
    auto transaction = document_command_transaction_t::from(history);
    transaction.adopt_document(std::move(draft.document));
    return transaction.commit("Assign BMS WAV slot batch");
}

auto apply_primary_music_set(document_history_t& history,
                             import_resource_asset_action_t action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (!action.audio_stream) {
        reject(result, diagnostic_code_t::invalid_id, "song audio requires probed stream geometry");
        return result;
    }
    action.role = resource_role_t::bgm;
    const auto music = find_primary_music_resource(history.document);
    if (music == history.document.resources.end()) return import_primary_music(history, action);
    const auto paths = validate_import_source_paths(action, result);
    if (!paths) return result;
    return relink_primary_music(history, *music, paths->second.value_or(paths->first), action);
}

auto apply_resource_asset_relink(document_history_t& history,
                                 const relink_resource_asset_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    const auto path = action.source_path.lexically_normal();
    const auto path_text = path.generic_string();
    if (path.empty() || path.filename().empty() || !text_util::is_valid_bounded_text(path_text, 4096) ||
        (path.has_root_name() && !path.is_absolute())) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource asset relink path is empty, malformed, or too long");
        return result;
    }
    if (!path.is_absolute() &&
        std::ranges::any_of(path, [](const auto& component) {
            return component == "..";
        })) {
        reject(result, diagnostic_code_t::invalid_id,
               "relative resource paths cannot escape the project directory");
        return result;
    }
    const auto audio = history.document.find_audio_asset(action.asset);
    const auto visual = std::ranges::find(history.document.visual_assets, action.asset,
                                           &visual_asset_t::id);
    if (!action.asset ||
        (audio == nullptr &&
         visual == history.document.visual_assets.end()) ||
        (audio != nullptr &&
         visual != history.document.visual_assets.end())) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource asset relink requires one known audio or image asset");
        return result;
    }
    const auto current = audio != nullptr
                             ? audio->source_path
                             : visual->source_path;
    if (current == path) {
        return result;
    }
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    if (audio != nullptr) {
        const auto index = static_cast<std::size_t>(audio -
                                                    history.document.audio_assets.data());
        proposed.audio_assets[index].source_path = path;
        // The user pointed the asset at a file they chose, so its sounds are no longer missing.
        for (auto& resource : proposed.resources)
            if (resource.asset == action.asset) resource.missing = false;
    } else {
        const auto index = static_cast<std::size_t>(visual -
                                                    history.document.visual_assets.begin());
        proposed.visual_assets[index].source_path = path;
    }
    return transaction.commit("Relink resource asset");
}

namespace {

using text_util::ascii_lowercase;

auto normalized_resource_name(std::string_view name) -> std::optional<std::string> {
    name = text_util::trim(name, " \t\r\n");
    if (name.empty()) return std::nullopt;
    if (!text_util::is_valid_bounded_text(name, 256) || std::ranges::any_of(name, [](unsigned char c) {
            return c < 32 || c == 127;
        })) return std::nullopt;
    return std::string{name};
}

auto normalized_sound_tag(std::string_view tag) -> std::optional<std::string> {
    tag = text_util::trim(tag, " \t");
    if (tag.empty() || !text_util::is_valid_bounded_text(tag, maximum_sound_tag_bytes) ||
        std::ranges::any_of(tag, [](char c) {
            return static_cast<unsigned char>(c) < 0x20 || c == 0x7f;
        })) {
        return std::nullopt;
    }
    return std::string{tag};
}

// Returns nullopt when any tag is invalid or the list is over the limit.
auto normalized_sound_tags(const std::vector<std::string>& tags)
    -> std::optional<std::vector<std::string>> {
    std::vector<std::string> result;
    std::vector<std::string> folded;
    for (const auto& tag : tags) {
        auto normalized = normalized_sound_tag(tag);
        if (!normalized) return std::nullopt;
        auto key = ascii_lowercase(*normalized);
        if (std::ranges::find(folded, key) != folded.end()) continue;
        folded.push_back(std::move(key));
        result.push_back(std::move(*normalized));
    }
    if (result.size() > maximum_sound_tags) return std::nullopt;
    return result;
}

auto remove_empty_sound_groups(song_document_t& document) -> void {
    std::erase_if(document.sound_groups, [&document](const auto& group) {
        return std::ranges::none_of(document.resources, [&group](const auto& resource) {
            return resource.sound_group == group.id;
        });
    });
}

} // namespace

auto count_resource_references(const song_document_t& document) -> resource_reference_counts_t {
    resource_reference_counts_t counts, retained;
    for (const auto& chart : document.charts) append_chart_reference_counts(chart, counts, retained);
    for (const auto& resource : document.resources) {
        const auto found = retained.find(resource.asset.value);
        if (found != retained.end()) counts[resource.id.value] += found->second;
    }
    return counts;
}

auto apply_resource_sound_metadata(document_history_t& history,
                                   const set_resource_sound_metadata_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    const auto resource = std::ranges::find(history.document.resources, action.resource,
                                            &resource_entry_t::id);
    if (!action.resource || resource == history.document.resources.end()) {
        reject(result, diagnostic_code_t::invalid_id, "resource sound metadata needs a known resource");
        return result;
    }
    if (static_cast<std::uint8_t>(action.kind) > static_cast<std::uint8_t>(sound_kind_t::sample)) {
        reject(result, diagnostic_code_t::invalid_id, "resource sound kind is not recognized");
        return result;
    }
    const auto name = action.name ? normalized_resource_name(*action.name)
                                  : std::optional<std::string>{resource->name};
    if (!name) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource name must be 1-256 bytes without control characters");
        return result;
    }
    auto tags = normalized_sound_tags(action.tags);
    if (!tags) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource sound tags must be 1-64 bytes without control characters, at most 32");
        return result;
    }
    if (resource->name == *name && resource->sound_kind == action.kind && resource->tags == *tags) {
        return result;
    }
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto& target = proposed.resources[static_cast<std::size_t>(
        resource - history.document.resources.begin())];
    target.name = *name;
    target.sound_kind = action.kind;
    target.tags = std::move(*tags);
    return transaction.commit("Edit sound metadata");
}

auto apply_resource_deletions(document_history_t& history,
                              std::span<const resource_id_t> resources)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (resources.empty() || resources.size() > maximum_resource_deletions) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "resource deletion needs a bounded nonempty selection");
        return result;
    }
    const auto reference_counts = count_resource_references(history.document);
    std::unordered_set<std::uint64_t> doomed;
    for (const auto resource_id : resources) {
        const auto resource = std::ranges::find(history.document.resources, resource_id,
                                                &resource_entry_t::id);
        if (!resource_id || resource == history.document.resources.end()) {
            reject(result, diagnostic_code_t::invalid_id,
                   "resource deletion refers to an unknown catalog resource");
            return result;
        }
        if (!doomed.insert(resource_id.value).second) {
            reject(result, diagnostic_code_t::invalid_id,
                   "resource deletion lists a catalog resource twice");
            return result;
        }
        const auto found_count = reference_counts.find(resource_id.value);
        const auto reference_count = found_count == reference_counts.end() ? 0 : found_count->second;
        if (reference_count != 0) {
            const auto subject = resources.size() == 1
                ? std::string{"resource"} : "resource '" + resource->name + "'";
            reject(result, diagnostic_code_t::unsupported_feature,
                   subject + " is still referenced by " + std::to_string(reference_count) +
                       " stored event(s)");
            return result;
        }
    }
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    std::erase_if(proposed.resources,
                  [&](const auto& entry) { return doomed.contains(entry.id.value); });
    remove_empty_sound_groups(proposed);
    for (auto& chart : proposed.charts) {
        std::erase_if(chart.sound_layer_defaults,
                      [&](const auto& entry) { return doomed.contains(entry.resource.value); });
    }
    return transaction.commit(resources.size() == 1 ? "Delete resource" : "Delete resources");
}

auto apply_resource_deletion(document_history_t& history,
                             resource_id_t resource_id) -> document_command_result_t {
    return apply_resource_deletions(history,
                                    std::span<const resource_id_t>{&resource_id, 1});
}

auto validate_resource_assignment_selection(const song_document_t& document,
    const assign_resource_action_t& action, resource_assignment_limits_t limits,
    document_command_result_t& result)
    -> std::optional<std::vector<editor_selection_object_t>> {
    if (limits.maximum_selected_objects == 0 || limits.maximum_chart_objects == 0 ||
        action.objects.empty() || action.objects.size() > limits.maximum_selected_objects) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "resource assignment requires a bounded nonempty selection");
        return std::nullopt;
    }
    auto selected = action.objects;
    std::ranges::sort(selected);
    if (std::ranges::adjacent_find(selected) != selected.end()) {
        reject(result, diagnostic_code_t::duplicate_id,
               "resource assignment contains a duplicate typed object");
        return std::nullopt;
    }
    std::set<resource_role_t> required_roles;
    for (const auto& object : selected) {
        if (!object.id) {
            reject(result, diagnostic_code_t::invalid_id,
                   "resource assignment uses the reserved zero object ID");
            return std::nullopt;
        }
        const auto role = object.kind == editor_selection_object_kind_t::note ? resource_role_t::sample
                        : object.kind == editor_selection_object_kind_t::bgm_event ? resource_role_t::bgm
                        : object.kind == editor_selection_object_kind_t::bga_event ? resource_role_t::bga
                                                                                : resource_role_t::music;
        required_roles.insert(role);
    }
    for (const auto role : required_roles)
        if (!validate_resource_role(document, action.resource, role, result)) return std::nullopt;
    return selected;
}

struct resource_assignment_scan_t {
    const std::vector<editor_selection_object_t>& selected;
    resource_id_t resource;
    std::size_t maximum_objects;
    std::vector<std::uint8_t>& occurrences;
    document_command_result_t& result;
    std::size_t visited{};
    bool changed{};
};

auto prepare_assigned_bga_kinds(const song_document_t& document, chart_document_t& chart,
                               resource_assignment_scan_t& scan) -> bool {
    for (auto& event : chart.bga_events) {
        if (!editor_bga_lane_index(event.kind)) continue;
        if (!std::ranges::binary_search(scan.selected,
            editor_selection_object_t{editor_selection_object_kind_t::bga_event, event.id})) continue;
        const auto kind = resolve_bga_resource_kind(document, scan.resource, event.kind, scan.result);
        if (!kind) return false;
        if (event.kind != *kind) {
            event.kind = *kind;
            scan.changed = true;
        }
    }
    return true;
}

template <typename Current>
auto assign_selected_resource(resource_assignment_scan_t& scan,
    editor_selection_object_t object, Current& current, bool retained_sound_link) -> bool {
    if (++scan.visited > scan.maximum_objects) {
        reject(scan.result, diagnostic_code_t::invalid_resolution,
               "resource assignment chart scan exceeds its configured cap");
        return false;
    }
    const auto found = std::ranges::lower_bound(scan.selected, object);
    if (found == scan.selected.end() || *found != object) return true;
    const auto index = static_cast<std::size_t>(found - scan.selected.begin());
    if (++scan.occurrences[index] != 1) {
        reject(scan.result, diagnostic_code_t::duplicate_id,
               "resource assignment target has an ambiguous typed identity");
        return false;
    }
    if (current == scan.resource) return true;
    if (retained_sound_link) {
        reject(scan.result, diagnostic_code_t::unsupported_feature,
               "edit the retained BMSON sound channel before changing its resource");
        return false;
    }
    current = scan.resource;
    scan.changed = true;
    return true;
}

auto scan_resource_assignment_targets(chart_document_t& chart, resource_assignment_scan_t& scan)
    -> bool {
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes)
            if (!assign_selected_resource(scan,
                    {editor_selection_object_kind_t::note, note.id}, note.sound,
                    note.sound_channel_event.has_value())) return false;
        for (auto& event : measure.bgm_events)
            if (!assign_selected_resource(scan,
                    {editor_selection_object_kind_t::bgm_event, event.id}, event.sound,
                    event.sound_channel_event.has_value())) return false;
    }
    for (auto& event : chart.bga_events)
        if (!assign_selected_resource(scan,
                {editor_selection_object_kind_t::bga_event, event.id}, event.resource, false))
            return false;
    if (std::ranges::find(scan.occurrences, std::uint8_t{0}) == scan.occurrences.end()) return true;
    reject(scan.result, diagnostic_code_t::invalid_id,
           "resource assignment refers to an unknown typed object");
    return false;
}

auto apply_resource_assignment(document_history_t& history,
                               const assign_resource_action_t& action,
                               resource_assignment_limits_t limits) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    auto selected = validate_resource_assignment_selection(history.document, action, limits, result);
    if (!selected) return result;
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    const auto chart = proposed.find_chart(action.chart);
    if (!action.chart || chart == nullptr) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource assignment refers to an unknown chart");
        return result;
    }
    std::vector<std::uint8_t> occurrences(selected->size());
    resource_assignment_scan_t scan{*selected, action.resource, limits.maximum_chart_objects,
                                    occurrences, result};
    if (!prepare_assigned_bga_kinds(proposed, *chart, scan)) return result;
    if (!scan_resource_assignment_targets(*chart, scan)) return result;
    if (!scan.changed) return result;
    return transaction.commit("Assign resource to selection");
}

} // namespace wormedit

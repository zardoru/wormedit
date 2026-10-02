#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/virtual_canvas.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace wormedit {
namespace {

void reject(document_command_result_t& result, diagnostic_code_t code, std::string message) {
    result.diagnostics.push_back(
        {.severity = diagnostic_severity_t::error,
         .code = code,
         .message = std::move(message),
         .object = std::nullopt});
}

auto has_role(const resource_entry_t& resource, resource_role_t role) -> bool {
    return std::ranges::find(resource.roles, role) != resource.roles.end();
}

auto next_chart_object_id(const chart_document_t& chart) -> std::optional<object_id_t> {
    std::uint64_t largest{};
    std::size_t count{};
    const auto scan = [&](const auto& values) {
        for (const auto& value : values) {
            largest = std::max(largest, value.id.value);
            ++count;
        }
    };
    scan(chart.bgm_source_lanes);
    scan(chart.lane_layouts);
    scan(chart.bpms);
    scan(chart.pauses);
    scan(chart.warps);
    scan(chart.scrolls);
    scan(chart.speeds);
    scan(chart.kiai);
    scan(chart.timing_anchors);
    scan(chart.bookmarks);
    scan(chart.sections);
    scan(chart.bga_events);
    scan(chart.sound_channels);
    for (const auto& channel : chart.sound_channels) {
        scan(channel.events);
    }
    for (const auto& measure : chart.measures) {
        scan(measure.notes);
        scan(measure.bgm_events);
    }
    if (count >= 1'000'000 || largest == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return object_id_t{largest + 1};
}

auto snap_placement_position(const chart_document_t& chart, const measure_index_t& index,
                             local_position_t position, std::uint64_t denominator)
    -> std::optional<local_position_t> {
    const auto absolute = absolute_row(index, position);
    if (!absolute) {
        return std::nullopt;
    }
    if (denominator == 0) {
        return position;
    }
    if (chart.divisions_per_standard_measure == 0 ||
        chart.divisions_per_standard_measure % denominator != 0) {
        return std::nullopt;
    }
    const auto step = chart.divisions_per_standard_measure / denominator;
    const auto remainder = *absolute % step;
    auto snapped = *absolute - remainder;
    const auto distance_up = step - remainder;
    if (remainder > distance_up &&
        distance_up <= std::numeric_limits<std::uint64_t>::max() - *absolute) {
        snapped = *absolute + distance_up;
    }
    if (snapped >= index.total_rows) {
        return std::nullopt;
    }
    const auto after = std::ranges::upper_bound(index.entries, snapped, {},
                                                &measure_index_entry_t::absolute_begin_row);
    if (after == index.entries.begin()) {
        return std::nullopt;
    }
    const auto entry = std::prev(after);
    return local_position_t{entry->id, snapped - entry->absolute_begin_row};
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

auto next_bms_resource_identity(const song_document_t& document, bool visual_role)
    -> std::optional<target_resource_identity_t> {
    const std::string_view name_space = visual_role ? "BMP" : "WAV";
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::array<bool, 36 * 36> occupied{};
    const auto digit_value = [](unsigned char character) -> int {
        const auto upper = std::toupper(character);
        if (upper >= '0' && upper <= '9') return upper - '0';
        if (upper >= 'A' && upper <= 'Z') return upper - 'A' + 10;
        return -1;
    };
    for (const auto& existing : document.resources) {
        for (const auto& identity : existing.target_identities) {
            if (identity.family != format_family_t::bms ||
                identity.namespace_name != name_space || identity.identifier.size() != 2) continue;
            const auto high = digit_value(static_cast<unsigned char>(identity.identifier[0]));
            const auto low = digit_value(static_cast<unsigned char>(identity.identifier[1]));
            if (high >= 0 && low >= 0)
                occupied[static_cast<std::size_t>(high * 36 + low)] = true;
        }
    }
    for (std::size_t value = 1; value < occupied.size(); ++value) {
        if (!occupied[value])
            return target_resource_identity_t{format_family_t::bms,
                std::string{digits[value / 36], digits[value % 36]}, std::string{name_space}};
    }
    return std::nullopt;
}

auto validate_placement_resource(const song_document_t& document,
                                 const place_resource_action_t& action,
                                 document_command_result_t& result)
    -> std::optional<resource_role_t> {
    const auto resource = std::ranges::find(document.resources, action.resource, &resource_entry_t::id);
    if (!action.resource || resource == document.resources.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource placement refers to an unknown catalog resource");
        return std::nullopt;
    }
    if (resource->missing) {
        reject(result, diagnostic_code_t::dangling_resource,
               "cannot place a resource with a missing source");
        return std::nullopt;
    }
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
    const auto audio = std::ranges::find(document.audio_assets, resource->asset, &audio_asset_t::id);
    const auto visual = std::ranges::find(document.visual_assets, resource->asset, &visual_asset_t::id);
    if (!has_role(*resource, role) ||
        (role == resource_role_t::bga ? visual == document.visual_assets.end()
                                      : audio == document.audio_assets.end())) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "resource is incompatible with the requested placement");
        return std::nullopt;
    }
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
    const auto chart = std::ranges::find(document.charts, action.chart, &chart_document_t::id);
    if (!action.chart || chart == document.charts.end()) {
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
    const auto first_id = next_chart_object_id(*chart);
    if (!first_id) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource placement chart object limit or ID space is exhausted");
        return std::nullopt;
    }
    return resource_placement_target_t{
        static_cast<std::size_t>(chart - document.charts.begin()), *index, *position, *row, *first_id};
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
    if (!active || std::ranges::find(active->layout.lanes, action.lane,
                                      &lane_definition_t::id) == active->layout.lanes.end()) {
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
        .semantic_layers = {}, .label = std::nullopt, .branch = std::nullopt, .provenance = {}});
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
        .branch = std::nullopt, .provenance = {}});
    return true;
}

auto place_bga_background(resource_placement_context_t& context) -> bool {
    if (std::ranges::any_of(context.chart.bga_events, [&](const auto& event) {
            return event.kind == visual_event_kind_t::background &&
                   event.position == context.position;
        })) {
        reject(context.result, diagnostic_code_t::collision,
               "a BGA background already occupies the cursor row");
        return false;
    }
    context.chart.bga_events.push_back({.id = context.first_id, .position = context.position,
        .resource = context.action.resource, .kind = visual_event_kind_t::background,
        .provenance = {}});
    return true;
}

auto apply_resource_placement(document_history_t& history, std::uint64_t expected_revision,
                              const place_resource_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource placement document revision is stale");
        return result;
    }
    if (!validate_placement_resource(history.document, action, result)) return result;
    auto target = resolve_placement_target(history.document, action, result);
    if (!target) return result;
    auto proposed = history.document;
    auto& chart = proposed.charts[target->chart_offset];
    auto measure = std::ranges::find(chart.measures, target->position.measure, &measure_chunk_t::id);
    if (measure == chart.measures.end()) {
        reject(result, diagnostic_code_t::invalid_measure, "resource placement measure is missing");
        return result;
    }
    resource_placement_context_t placement{result, chart, *measure, target->index, action,
                                            target->position, target->row, target->first_id};
    bool placed{};
    switch (action.kind) {
    case resource_placement_kind_t::sample_note: placed = place_sample_note(placement); break;
    case resource_placement_kind_t::bgm_event: placed = place_bgm_event(placement); break;
    case resource_placement_kind_t::bga_background: placed = place_bga_background(placement); break;
    }
    if (!placed) return result;
    return commit_document_command(history, std::move(proposed), "Place resource");
}

auto find_bgm_event(const chart_document_t& chart, object_id_t id) -> const bgm_event_t* {
    for (const auto& measure : chart.measures) {
        const auto found = std::ranges::find(measure.bgm_events, id, &bgm_event_t::id);
        if (found != measure.bgm_events.end()) return &*found;
    }
    return nullptr;
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
    const auto new_id = next_chart_object_id(chart);
    if (!new_id || chart.bgm_source_lanes.size() >= 1'000'000) {
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

auto apply_bgm_event_move(document_history_t& history, std::uint64_t expected_revision,
                          const move_bgm_event_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id, "BGM move document revision is stale");
        return result;
    }
    const auto source_chart = std::ranges::find(history.document.charts, action.chart,
                                                &chart_document_t::id);
    if (!action.chart || source_chart == history.document.charts.end() || !action.event) {
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
    const auto* original = find_bgm_event(*source_chart, action.event);
    if (!original) {
        reject(result, diagnostic_code_t::invalid_id, "BGM move event no longer exists");
        return result;
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
    auto proposed = history.document;
    auto& chart = proposed.charts[static_cast<std::size_t>(source_chart - history.document.charts.begin())];
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
    moved.source_lane = destination_lane;
    moved.structural_slot = 0;
    moved.provenance = {};
    const auto destination = std::ranges::find(chart.measures, target->measure,
                                               &measure_chunk_t::id);
    destination->bgm_events.push_back(std::move(moved));
    return commit_document_command(history, std::move(proposed), "Move BGM event");
}

auto apply_virtual_bms_resource_placement(
    document_history_t& history, std::uint64_t expected_revision,
    const place_virtual_bms_resource_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "virtual resource placement document revision is stale");
        return result;
    }
    const auto source_chart = std::ranges::find(history.document.charts, action.chart,
                                                &chart_document_t::id);
    if (source_chart == history.document.charts.end() ||
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
        const auto step = source_chart->divisions_per_standard_measure /
                          action.snap_denominator;
        const auto remainder = snapped_row % step;
        snapped_row -= remainder;
        if (remainder > step - remainder) {
            if (step - remainder >
                std::numeric_limits<std::uint64_t>::max() - snapped_row) {
                reject(result, diagnostic_code_t::row_out_of_range,
                       "virtual resource snap row overflows");
                return result;
            }
            snapped_row += step;
        }
    }
    const auto destination = canvas.measure_containing(snapped_row);
    if (!destination || destination->authored ||
        destination->number >= bms_addressable_measure_count) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "virtual resource placement requires an unmaterialized BMS measure");
        return result;
    }
    auto proposed = history.document;
    auto& chart = proposed.charts[static_cast<std::size_t>(
        source_chart - history.document.charts.begin())];
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
    auto staged = make_document_history(std::move(proposed));
    const auto placed = apply_resource_placement(
        staged, staged.document.revision,
        {.chart = action.chart,
         .resource = action.resource,
         .position = position,
         .lane = action.lane,
         .source_lane = action.source_lane,
         .kind = action.kind});
    if (!placed) {
        result.diagnostics = placed.diagnostics;
        return result;
    }
    return commit_document_command(history, std::move(staged.document),
                                   "Place resource in virtual BMS measure");
}

auto apply_virtual_bms_bgm_event_move(
    document_history_t& history, std::uint64_t expected_revision,
    const move_virtual_bms_bgm_event_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "virtual BGM move document revision is stale");
        return result;
    }
    const auto source_chart = std::ranges::find(history.document.charts, action.chart,
                                                &chart_document_t::id);
    if (!action.chart || source_chart == history.document.charts.end() ||
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
        const auto step = source_chart->divisions_per_standard_measure /
                          action.snap_denominator;
        const auto remainder = snapped_row % step;
        snapped_row -= remainder;
        if (remainder > step - remainder) {
            if (step - remainder >
                std::numeric_limits<std::uint64_t>::max() - snapped_row) {
                reject(result, diagnostic_code_t::row_out_of_range,
                       "virtual BGM move snap row overflows");
                return result;
            }
            snapped_row += step;
        }
    }
    const auto destination = canvas.measure_containing(snapped_row);
    if (!destination || destination->authored ||
        destination->number >= bms_addressable_measure_count) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "virtual BGM move requires an unmaterialized BMS measure");
        return result;
    }
    auto proposed = history.document;
    auto& chart = proposed.charts[static_cast<std::size_t>(
        source_chart - history.document.charts.begin())];
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
    auto staged = make_document_history(std::move(proposed));
    const auto moved = apply_bgm_event_move(
        staged, staged.document.revision,
        {.chart = action.chart, .event = action.event, .position = position,
         .source_lane = action.source_lane});
    if (!moved) {
        result.diagnostics = moved.diagnostics;
        return result;
    }
    return commit_document_command(history, std::move(staged.document),
                                   "Move BGM event into virtual BMS measure");
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

auto apply_resource_creation(document_history_t& history, std::uint64_t expected_revision,
                             const create_resource_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id, "resource creation document revision is stale");
        return result;
    }
    if (history.document.resources.size() >= 100'000 || action.name.size() > 255 ||
        action.name.find('\0') != std::string::npos) {
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
    const auto audio = std::ranges::find(document.audio_assets, action.asset, &audio_asset_t::id);
    const auto visual =
        std::ranges::find(document.visual_assets, action.asset, &visual_asset_t::id);
    if (!action.asset || (audio_role && audio == document.audio_assets.end()) ||
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
    if (name.size() > 255 || name.find('\0') != std::string::npos) {
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
    auto proposed = document;
    proposed.resources.push_back(std::move(resource));
    return commit_document_command(history, std::move(proposed), "Create resource");
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
    if (!path.is_absolute() || path.filename().empty() || text.size() > 4096 ||
        text.find('\0') != std::string::npos) {
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
            candidate.filename().empty() || copied_text.size() > 4096 ||
            copied_text.find('\0') != std::string::npos ||
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
               std::ranges::find(document.audio_assets, resource.asset, &audio_asset_t::id) !=
                   document.audio_assets.end();
    });
}

auto import_primary_music(document_history_t& history,
                          const import_resource_asset_action_t& action)
    -> document_command_result_t {
    auto draft = make_document_history(history.document, 1, false);
    auto imported = apply_resource_asset_import(draft, draft.document.revision, action);
    if (!imported.applied) {
        imported.revision = history.document.revision;
        return imported;
    }
    auto& roles = draft.document.resources.back().roles;
    if (std::ranges::find(roles, resource_role_t::music) == roles.end())
        roles.insert(roles.begin(), resource_role_t::music);
    return commit_document_command(history, std::move(draft.document), "Set song audio");
}

// Repoints the existing music asset so every resource and event that plays the
// song keeps its reference.
auto relink_primary_music(document_history_t& history, const resource_entry_t& music,
                          const std::filesystem::path& source,
                          const import_resource_asset_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    auto proposed = history.document;
    const auto asset = std::ranges::find(proposed.audio_assets, music.asset, &audio_asset_t::id);
    if (asset->source_path.lexically_normal() == source) return result;
    asset->source_path = source;
    asset->stream = *action.audio_stream;
    asset->waveform_cache_path.reset();
    if (!action.name.empty()) {
        const auto resource = std::ranges::find(proposed.resources, music.id, &resource_entry_t::id);
        resource->name = action.name;
    }
    if (!extend_native_charts_to_audio(proposed, *action.audio_stream, result)) return result;
    return commit_document_command(history, std::move(proposed), "Replace song audio");
}

auto apply_resource_asset_import(document_history_t& history, std::uint64_t expected_revision,
                                 const import_resource_asset_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id, "resource import document revision is stale");
        return result;
    }
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
    auto proposed = document;
    if (!asset) {
        if (!append_imported_asset(proposed, action, registered_copy.value_or(path), asset, result))
            return result;
    }
    auto draft = make_document_history(std::move(proposed), 1, false);
    const auto created =
        apply_resource_creation(draft, draft.document.revision,
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
    return commit_document_command(history, std::move(draft.document), "Import resource asset");
}

auto apply_primary_music_set(document_history_t& history, std::uint64_t expected_revision,
                             import_resource_asset_action_t action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id, "song audio document revision is stale");
        return result;
    }
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

auto apply_resource_asset_relink(document_history_t& history, std::uint64_t expected_revision,
                                 const relink_resource_asset_action_t& action)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource asset relink document revision is stale");
        return result;
    }
    const auto path = action.source_path.lexically_normal();
    const auto path_text = path.generic_string();
    if (path.empty() || path.filename().empty() || path_text.size() > 4096 ||
        path_text.find('\0') != std::string::npos ||
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
    const auto audio = std::ranges::find(history.document.audio_assets, action.asset,
                                          &audio_asset_t::id);
    const auto visual = std::ranges::find(history.document.visual_assets, action.asset,
                                           &visual_asset_t::id);
    if (!action.asset ||
        (audio == history.document.audio_assets.end() &&
         visual == history.document.visual_assets.end()) ||
        (audio != history.document.audio_assets.end() &&
         visual != history.document.visual_assets.end())) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource asset relink requires one known audio or image asset");
        return result;
    }
    const auto current = audio != history.document.audio_assets.end()
                             ? audio->source_path
                             : visual->source_path;
    if (current == path) {
        return result;
    }
    auto proposed = history.document;
    if (audio != history.document.audio_assets.end()) {
        const auto index = static_cast<std::size_t>(audio -
                                                    history.document.audio_assets.begin());
        proposed.audio_assets[index].source_path = path;
    } else {
        const auto index = static_cast<std::size_t>(visual -
                                                    history.document.visual_assets.begin());
        proposed.visual_assets[index].source_path = path;
    }
    return commit_document_command(history, std::move(proposed), "Relink resource asset");
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
    const auto resource = std::ranges::find(document.resources, action.resource, &resource_entry_t::id);
    if (!action.resource || resource == document.resources.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource assignment refers to an unknown catalog resource");
        return std::nullopt;
    }
    if (resource->missing) {
        reject(result, diagnostic_code_t::dangling_resource,
               "cannot assign a resource with a missing source");
        return std::nullopt;
    }
    const auto audio = std::ranges::find(document.audio_assets, resource->asset, &audio_asset_t::id);
    const auto visual = std::ranges::find(document.visual_assets, resource->asset, &visual_asset_t::id);
    const bool audio_eligible = audio != document.audio_assets.end() &&
        (has_role(*resource, resource_role_t::sample) || has_role(*resource, resource_role_t::bgm));
    const bool visual_eligible = visual != document.visual_assets.end() &&
                                 has_role(*resource, resource_role_t::bga);
    auto selected = action.objects;
    std::ranges::sort(selected);
    if (std::ranges::adjacent_find(selected) != selected.end()) {
        reject(result, diagnostic_code_t::duplicate_id,
               "resource assignment contains a duplicate typed object");
        return std::nullopt;
    }
    for (const auto& object : selected) {
        if (!object.id) {
            reject(result, diagnostic_code_t::invalid_id,
                   "resource assignment uses the reserved zero object ID");
            return std::nullopt;
        }
        const bool compatible = object.kind == editor_selection_object_kind_t::note ||
                object.kind == editor_selection_object_kind_t::bgm_event ? audio_eligible
            : object.kind == editor_selection_object_kind_t::bga_event ? visual_eligible : false;
        if (!compatible) {
            reject(result, diagnostic_code_t::unsupported_feature,
                   "selected object cannot use this resource type");
            return std::nullopt;
        }
    }
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

auto apply_resource_assignment(document_history_t& history, std::uint64_t expected_revision,
                               const assign_resource_action_t& action,
                               resource_assignment_limits_t limits) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource assignment document revision is stale");
        return result;
    }
    auto selected = validate_resource_assignment_selection(history.document, action, limits, result);
    if (!selected) return result;
    auto proposed = history.document;
    const auto chart = std::ranges::find(proposed.charts, action.chart, &chart_document_t::id);
    if (!action.chart || chart == proposed.charts.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource assignment refers to an unknown chart");
        return result;
    }
    std::vector<std::uint8_t> occurrences(selected->size());
    resource_assignment_scan_t scan{*selected, action.resource, limits.maximum_chart_objects,
                                    occurrences, result};
    if (!scan_resource_assignment_targets(*chart, scan)) return result;
    if (!scan.changed) return result;
    return commit_document_command(history, std::move(proposed), "Assign resource to selection");
}

} // namespace wormedit

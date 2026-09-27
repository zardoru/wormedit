#include <wormedit/core/resource_assignment.h>

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

} // namespace

auto apply_resource_placement(document_history_t& history, std::uint64_t expected_revision,
                              const place_resource_action_t& action) -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision, .diagnostics = {}};
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource placement document revision is stale");
        return result;
    }
    const auto& document = history.document;
    const auto resource =
        std::ranges::find(document.resources, action.resource, &resource_entry_t::id);
    if (!action.resource || resource == document.resources.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource placement refers to an unknown catalog resource");
        return result;
    }
    if (resource->missing) {
        reject(result, diagnostic_code_t::dangling_resource,
               "cannot place a resource with a missing source");
        return result;
    }
    if (action.kind != resource_placement_kind_t::sample_note &&
        action.kind != resource_placement_kind_t::bgm_event &&
        action.kind != resource_placement_kind_t::bga_background) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "resource placement kind is unsupported");
        return result;
    }
    const auto role = action.kind == resource_placement_kind_t::sample_note
                          ? resource_role_t::sample
                          : action.kind == resource_placement_kind_t::bgm_event
                                ? resource_role_t::bgm
                                : resource_role_t::bga;
    const auto audio =
        std::ranges::find(document.audio_assets, resource->asset, &audio_asset_t::id);
    const auto visual =
        std::ranges::find(document.visual_assets, resource->asset, &visual_asset_t::id);
    if (!has_role(*resource, role) ||
        (role == resource_role_t::bga ? visual == document.visual_assets.end()
                                     : audio == document.audio_assets.end())) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "resource is incompatible with the requested placement");
        return result;
    }
    const auto source_chart =
        std::ranges::find(document.charts, action.chart, &chart_document_t::id);
    if (!action.chart || source_chart == document.charts.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource placement refers to an unknown chart");
        return result;
    }
    const auto built = build_measure_index(*source_chart);
    if (!std::holds_alternative<measure_index_t>(built)) {
        reject(result, diagnostic_code_t::invalid_measure,
               "resource placement chart has no valid measure index");
        return result;
    }
    const auto& index = std::get<measure_index_t>(built);
    const auto row = absolute_row(index, action.position);
    if (!row) {
        reject(result, diagnostic_code_t::row_out_of_range,
               "resource placement row is outside the chart");
        return result;
    }
    const auto first_id = next_chart_object_id(*source_chart);
    if (!first_id) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource placement chart object limit or ID space is exhausted");
        return result;
    }
    auto proposed = document;
    auto& chart = proposed.charts[static_cast<std::size_t>(source_chart - document.charts.begin())];
    auto measure =
        std::ranges::find(chart.measures, action.position.measure, &measure_chunk_t::id);
    if (measure == chart.measures.end()) {
        reject(result, diagnostic_code_t::invalid_measure,
               "resource placement measure is missing");
        return result;
    }
    switch (action.kind) {
    case resource_placement_kind_t::sample_note: {
        if (!action.lane) {
            reject(result, diagnostic_code_t::unknown_lane,
                   "Sample placement requires an active playable lane");
            return result;
        }
        const lane_layout_segment_t* active_layout{};
        std::uint64_t active_layout_row{};
        for (const auto& segment : chart.lane_layouts) {
            const auto begin = absolute_row(index, segment.position);
            if (begin && *begin <= *row &&
                (!active_layout || *begin >= active_layout_row)) {
                active_layout = &segment;
                active_layout_row = *begin;
            }
        }
        if (!active_layout ||
            std::ranges::find(active_layout->layout.lanes, action.lane,
                              &lane_definition_t::id) == active_layout->layout.lanes.end()) {
            reject(result, diagnostic_code_t::unknown_lane,
                   "Sample placement lane is absent at the cursor row");
            return result;
        }
        for (const auto& chunk : chart.measures) {
            for (const auto& note : chunk.notes) {
                if (note.lane != action.lane) {
                    continue;
                }
                const auto begin = absolute_row(index, note.position);
                const auto end = note.end ? absolute_row(index, *note.end) : begin;
                if (begin && end && *begin <= *row && *row <= *end) {
                    reject(result, diagnostic_code_t::collision,
                           "Sample placement overlaps a playable note or hold");
                    return result;
                }
            }
        }
        measure->notes.push_back({.id = *first_id,
                                  .position = action.position,
                                  .lane = action.lane,
                                  .kind = note_kind_t::tap,
                                  .end = std::nullopt,
                                  .sound = action.resource,
                                  .sound_channel_event = std::nullopt,
                                  .semantic_layers = {},
                                  .label = std::nullopt,
                                  .branch = std::nullopt,
                                  .provenance = {}});
        break;
    }
    case resource_placement_kind_t::bgm_event: {
        object_id_t lane{};
        std::unordered_set<std::uint64_t> occupied_lanes;
        for (const auto& chunk : chart.measures) {
            for (const auto& event : chunk.bgm_events) {
                if (event.position == action.position) {
                    occupied_lanes.insert(event.source_lane.value);
                }
            }
        }
        const bgm_source_lane_t* first_available{};
        for (const auto& source_lane : chart.bgm_source_lanes) {
            if (!occupied_lanes.contains(source_lane.id.value) &&
                (!first_available ||
                 std::tie(source_lane.source_order, source_lane.id.value) <
                     std::tie(first_available->source_order, first_available->id.value))) {
                first_available = &source_lane;
            }
        }
        if (first_available) {
            lane = first_available->id;
        }
        auto event_id = *first_id;
        if (!lane) {
            if (chart.bgm_source_lanes.size() >= 1'000'000 ||
                event_id.value == std::numeric_limits<std::uint64_t>::max()) {
                reject(result, diagnostic_code_t::invalid_id,
                       "BGM source lane or chart object ID space is exhausted");
                return result;
            }
            std::size_t next_source_order{};
            for (const auto& source_lane : chart.bgm_source_lanes) {
                if (source_lane.source_order == std::numeric_limits<std::size_t>::max()) {
                    reject(result, diagnostic_code_t::invalid_id,
                           "BGM source-lane order space is exhausted");
                    return result;
                }
                next_source_order = std::max(next_source_order, source_lane.source_order + 1);
            }
            lane = event_id;
            event_id = object_id_t{event_id.value + 1};
            chart.bgm_source_lanes.push_back(
                {.id = lane,
                 .name = "BGM " + std::to_string(chart.bgm_source_lanes.size() + 1),
                 .source_order = next_source_order});
        }
        measure->bgm_events.push_back({.id = event_id,
                                       .position = action.position,
                                       .source_lane = lane,
                                       .sound = action.resource,
                                       .sound_channel_event = std::nullopt,
                                       .structural_slot = 0,
                                       .branch = std::nullopt,
                                       .provenance = {}});
        break;
    }
    case resource_placement_kind_t::bga_background: {
        if (std::ranges::any_of(chart.bga_events, [&](const auto& event) {
                return event.kind == visual_event_kind_t::background &&
                       event.position == action.position;
            })) {
            reject(result, diagnostic_code_t::collision,
                   "a BGA background already occupies the cursor row");
            return result;
        }
        chart.bga_events.push_back({.id = *first_id,
                                    .position = action.position,
                                    .resource = action.resource,
                                    .kind = visual_event_kind_t::background,
                                    .provenance = {}});
        break;
    }
    }
    return commit_document_command(history, std::move(proposed), "Place resource");
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
    resource_entry_t resource{.id = resource_id_t{largest_id + 1},
                              .name = action.name,
                              .asset = action.asset,
                              .roles = {action.role},
                              .target_identities = {},
                              .missing = false,
                              .provenance = {}};
    if (action.allocate_bms_identifier) {
        const std::string_view name_space = visual_role ? "BMP" : "WAV";
        constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        std::array<bool, 36 * 36> occupied{};
        const auto digit_value = [](unsigned char character) -> int {
            const auto upper = std::toupper(character);
            if (upper >= '0' && upper <= '9') {
                return upper - '0';
            }
            if (upper >= 'A' && upper <= 'Z') {
                return upper - 'A' + 10;
            }
            return -1;
        };
        for (const auto& existing : document.resources) {
            for (const auto& identity : existing.target_identities) {
                if (identity.family != format_family_t::bms ||
                    identity.namespace_name != name_space || identity.identifier.size() != 2) {
                    continue;
                }
                const auto high = digit_value(
                    static_cast<unsigned char>(identity.identifier[0]));
                const auto low = digit_value(
                    static_cast<unsigned char>(identity.identifier[1]));
                if (high >= 0 && low >= 0) {
                    occupied[static_cast<std::size_t>(high * 36 + low)] = true;
                }
            }
        }
        bool found{};
        for (std::size_t value = 1; value < 36 * 36; ++value) {
            if (!occupied[value]) {
                std::string candidate{digits[value / 36], digits[value % 36]};
                resource.target_identities.push_back(
                    {.family = format_family_t::bms,
                     .identifier = std::move(candidate),
                     .namespace_name = std::string{name_space}});
                found = true;
                break;
            }
        }
        if (!found) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "BMS resource namespace has no free nonzero identifier");
            return result;
        }
    }
    auto proposed = document;
    proposed.resources.push_back(std::move(resource));
    return commit_document_command(history, std::move(proposed), "Create resource");
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
    if (limits.maximum_selected_objects == 0 || limits.maximum_chart_objects == 0 ||
        action.objects.empty() || action.objects.size() > limits.maximum_selected_objects) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "resource assignment requires a bounded nonempty selection");
        return result;
    }
    const auto resource =
        std::ranges::find(history.document.resources, action.resource, &resource_entry_t::id);
    if (!action.resource || resource == history.document.resources.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource assignment refers to an unknown catalog resource");
        return result;
    }
    if (resource->missing) {
        reject(result, diagnostic_code_t::dangling_resource,
               "cannot assign a resource with a missing source");
        return result;
    }
    const auto audio_asset =
        std::ranges::find(history.document.audio_assets, resource->asset, &audio_asset_t::id);
    const auto visual_asset =
        std::ranges::find(history.document.visual_assets, resource->asset, &visual_asset_t::id);
    const auto audio_eligible =
        audio_asset != history.document.audio_assets.end() &&
        (has_role(*resource, resource_role_t::sample) || has_role(*resource, resource_role_t::bgm));
    const auto visual_eligible = visual_asset != history.document.visual_assets.end() &&
                                 has_role(*resource, resource_role_t::bga);

    auto selected = action.objects;
    std::ranges::sort(selected);
    if (std::ranges::adjacent_find(selected) != selected.end()) {
        reject(result, diagnostic_code_t::duplicate_id,
               "resource assignment contains a duplicate typed object");
        return result;
    }
    for (const auto& object : selected) {
        if (!object.id) {
            reject(result, diagnostic_code_t::invalid_id,
                   "resource assignment uses the reserved zero object ID");
            return result;
        }
        const auto compatible = [&] {
            switch (object.kind) {
            case editor_selection_object_kind_t::note:
            case editor_selection_object_kind_t::bgm_event:
                return audio_eligible;
            case editor_selection_object_kind_t::bga_event:
                return visual_eligible;
            default:
                return false;
            }
        }();
        if (!compatible) {
            reject(result, diagnostic_code_t::unsupported_feature,
                   "selected object cannot use this resource type");
            return result;
        }
    }

    auto proposed = history.document;
    const auto chart = std::ranges::find(proposed.charts, action.chart, &chart_document_t::id);
    if (!action.chart || chart == proposed.charts.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource assignment refers to an unknown chart");
        return result;
    }
    std::vector<std::uint8_t> occurrences(selected.size());
    std::size_t chart_objects{};
    bool changed{};
    const auto assign = [&](editor_selection_object_t object, auto& current,
                            bool retained_sound_link) -> bool {
        if (++chart_objects > limits.maximum_chart_objects) {
            reject(result, diagnostic_code_t::invalid_resolution,
                   "resource assignment chart scan exceeds its configured cap");
            return false;
        }
        const auto found = std::ranges::lower_bound(selected, object);
        if (found == selected.end() || *found != object) {
            return true;
        }
        const auto index = static_cast<std::size_t>(found - selected.begin());
        if (++occurrences[index] != 1) {
            reject(result, diagnostic_code_t::duplicate_id,
                   "resource assignment target has an ambiguous typed identity");
            return false;
        }
        if (current == action.resource) {
            return true;
        }
        if (retained_sound_link) {
            reject(result, diagnostic_code_t::unsupported_feature,
                   "edit the retained BMSON sound channel before changing its resource");
            return false;
        }
        current = action.resource;
        changed = true;
        return true;
    };
    for (auto& measure : chart->measures) {
        for (auto& note : measure.notes) {
            if (!assign({editor_selection_object_kind_t::note, note.id}, note.sound,
                        note.sound_channel_event.has_value())) {
                return result;
            }
        }
        for (auto& event : measure.bgm_events) {
            if (!assign({editor_selection_object_kind_t::bgm_event, event.id}, event.sound,
                        event.sound_channel_event.has_value())) {
                return result;
            }
        }
    }
    for (auto& event : chart->bga_events) {
        if (!assign({editor_selection_object_kind_t::bga_event, event.id}, event.resource, false)) {
            return result;
        }
    }
    if (std::ranges::find(occurrences, std::uint8_t{0}) != occurrences.end()) {
        reject(result, diagnostic_code_t::invalid_id,
               "resource assignment refers to an unknown typed object");
        return result;
    }
    if (!changed) {
        return result;
    }
    return commit_document_command(history, std::move(proposed), "Assign resource to selection");
}

} // namespace wormedit

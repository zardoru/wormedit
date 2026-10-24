#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/lane_migration.h>
#include <wormedit/core/document_index.h>
#include "lane_occupancy.h"
#include "id_ceiling.h"
#include "bms_native_content_internal.h"
#include "chart_position_storage.h"
#include <wormedit/core/virtual_canvas.h>

#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace wormedit {
namespace {

auto resolve_target(const song_document_t& document, chart_id_t difficulty,
    std::optional<branch_id_t> selected, bool authoring = false)
    -> result_t<bms_program_edit_target_t, error_t<failure_code_t>> {
    const auto* owner = document.find_bms_program(difficulty);
    if (!difficulty || !owner || document.find_chart(difficulty))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::not_found,
            "Scoped difficulty is absent or ambiguous."});
    const auto& program = owner->prepared().program;
    const auto node = selected.value_or(program.root());
    const auto chart = program.target_chart(node);
    if (!chart)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::not_found,
            "Selected scope or Chart is unavailable."});
    const auto scope = program.parent(*chart);
    if (!authoring) {
        const auto projection = owner->build_projection();
        if (!projection.value || !projection.ok())
            return std::unexpected(error_t<failure_code_t>{failure_code_t::materialize_failed,
                "Scoped difficulty cannot be evaluated."});
        if (std::ranges::find(projection.value->active_charts(), *chart) ==
            projection.value->active_charts().end())
            return std::unexpected(error_t<failure_code_t>{failure_code_t::unavailable,
                "Selected Chart is inactive in this evaluation."});
    }
    return bms_program_edit_target_t{.stamp = document_stamp_t::from(document),
        .difficulty = difficulty, .scope = *scope, .chart = *chart, .authoring = authoring};
}

auto resolve_native_target(const song_document_t& document, chart_id_t difficulty,
    std::optional<object_id_t> existing, const std::optional<bms_program_target_key_t>& selection)
    -> result_t<bms_program_edit_target_t, error_t<failure_code_t>> {
    if (selection && selection->difficulty != difficulty)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::conflict,
            "Selected semantic Chart belongs to another difficulty."});
    if (selection) return bms_program_edit_target_t::for_authoring(document, difficulty, selection->chart, existing);
    if (existing) return bms_program_edit_target_t::from(document, difficulty, *existing);
    return bms_program_edit_target_t::from(document, difficulty);
}

auto maximum_program_id(const song_document_t& document) -> std::uint64_t {
    return id_ceiling_t<object_id_t>::from(document).maximum;
}

// Declarations also contribute to a projected hold, but are independently
// editable. Only its authored endpoints determine whether a hold spans owners.
auto cross_owner_endpoints(const bms_program_document_t& owner, std::optional<branch_id_t> authoring_chart = {})
    -> std::set<object_id_t> {
    std::set<object_id_t> result;
    const auto projection = owner.build_projection(authoring_chart);
    if (!projection.value) return result;
    for (const auto& measure : projection.value->song().charts.front().measures) {
        for (const auto& note : measure.notes) {
            if (!note.end) continue;
            const auto endpoints = owner.prepared().program.hold_endpoints(projection.value->owners(note.id.value));
            if (endpoints.size() == 2 && endpoints.front().chart != endpoints.back().chart)
                for (const auto endpoint : endpoints) result.insert(endpoint.item);
        }
    }
    return result;
}

auto has_unchanged_item(const bms_program_document_t* before,
    const bms_program_document_t& proposed, branch_id_t chart, object_id_t item) -> bool {
    if (!before) return false;
    const auto* source = before->prepared().program.find(chart);
    const auto* destination = proposed.prepared().program.find(chart);
    if (!source || !destination) return false;
    const auto* old_content = std::get_if<bms_chart_content_t>(&source->value);
    const auto* new_content = std::get_if<bms_chart_content_t>(&destination->value);
    if (!old_content || !new_content) return false;
    const auto old_item = std::ranges::find(old_content->items, item, &bms_content_item_t::id);
    const auto new_item = std::ranges::find(new_content->items, item, &bms_content_item_t::id);
    return old_item != old_content->items.end() && new_item != new_content->items.end() && *old_item == *new_item;
}

auto has_existing_missing_reference(const bms_program_document_t* before,
    const bms_program_document_t& proposed, const bms_program_projection_diagnostic_t& diagnostic,
    std::span<const bms_program_projection_diagnostic_t> previous_diagnostics) -> bool {
    if (!has_unchanged_item(before, proposed, diagnostic.node, diagnostic.item)) return false;
    return std::ranges::find(previous_diagnostics, diagnostic) != previous_diagnostics.end();
}

auto has_unchanged_note_geometry(const note_t& note, const chart_document_t& chart,
    const note_t* old, const chart_document_t* previous) -> bool {
    if (!previous) return false;
    if (!old || old->lane != note.lane || old->position.measure != note.position.measure ||
        old->end.has_value() != note.end.has_value()) return false;
    if (rows_to_fraction(old->position.row, previous->divisions_per_standard_measure) !=
        rows_to_fraction(note.position.row, chart.divisions_per_standard_measure)) return false;
    return !note.end || (old->end->measure == note.end->measure &&
        rows_to_fraction(old->end->row, previous->divisions_per_standard_measure) ==
        rows_to_fraction(note.end->row, chart.divisions_per_standard_measure));
}

auto new_note_collision(const bms_program_projection_t& projection,
    const bms_program_projection_t* before) -> std::optional<object_id_t> {
    const auto& chart = projection.song().charts.front();
    const auto index = measure_index_t::from(chart);
    if (!index) return std::nullopt; // The shared document validator reports geometry failures.
    const auto* previous_chart = before ? &before->song().charts.front() : nullptr;
    document_id_index_t<note_t> old_notes;
    if (previous_chart)
        for (const auto& measure : previous_chart->measures)
            for (const auto& note : measure.notes) old_notes.add(note);
    std::vector<lane_occupant_t> intervals;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (auto interval = lane_occupant_t::from(note, *index)) {
                interval->proposed = !has_unchanged_note_geometry(note, chart, old_notes.find(note.id), previous_chart);
                intervals.push_back(*interval);
            }
        }
    }
    const auto occupancy = lane_occupancy_t::from(std::move(intervals));
    const auto* collision = occupancy.first_collision(lane_occupancy_pairs_t::proposed);
    return collision ? std::optional{collision->note} : std::nullopt;
}

template<class Value>
void find_native_collection(const chart_document_t& chart, object_id_t id,
    std::optional<bms_native_event_t>& result) {
    if constexpr (requires { bms_native_collection_t<Value>::member; }) {
        const auto& values = chart.*bms_native_collection_t<Value>::member;
        const auto found = std::ranges::find(values, id.value, [](const auto& value) { return value.id.value; });
        if (found != values.end()) result = *found;
    }
}

auto find_native_event(const chart_document_t& chart, object_id_t id) -> std::optional<bms_native_event_t> {
    if (const auto* note = chart.find_note(id)) return *note;
    if (const auto* bgm = chart.find_bgm_event(id)) return *bgm;
    std::optional<bms_native_event_t> result;
    [&]<std::size_t... Index>(std::index_sequence<Index...>) {
        (find_native_collection<std::variant_alternative_t<Index, bms_native_event_t>>(chart, id, result), ...);
    }(std::make_index_sequence<std::variant_size_v<bms_native_event_t>>{});
    return result;
}

struct existing_event_normalizer_t {
    const bms_program_document_t& program;
    const chart_document_t& original;
    const chart_document_t& geometry;

    auto identifier(resource_id_t resource, bms_resource_namespace_t name_space) const -> std::optional<std::string> {
        const auto projection = program.build_projection(original.bms_authoring_chart);
        if (!projection.value) return std::nullopt;
        const auto& resources = projection.value->song().resources;
        const auto entry = std::ranges::find(resources, resource, &resource_entry_t::id);
        if (entry == resources.end()) return std::nullopt;
        const auto name = name_space == bms_resource_namespace_t::wav ? "WAV" : "BMP";
        for (const auto& identity : entry->target_identities)
            if (identity.family == format_family_t::bms && identity.namespace_name == name) return identity.identifier;
        return std::nullopt;
    }
    auto native(const bms_native_event_t& event,
        std::optional<bms_native_resource_reference_t> reference = {}) const -> std::optional<bms_native_content_t> {
        if (!reference) {
            const auto resource = event_resource(event);
            const auto ns = std::holds_alternative<bga_event_t>(event)
                ? bms_resource_namespace_t::bmp : bms_resource_namespace_t::wav;
            const auto binding = resource ? identifier(*resource, ns) : std::nullopt;
            if (binding) reference = bms_native_resource_reference_t{.name_space = ns, .identifier = *binding};
        }
        return bms_native_content_t::from(event, geometry, std::move(reference));
    }
    auto fallback(const bms_content_value_t& before, const bms_native_event_t& after,
        const std::optional<bms_native_event_t>& previous) const -> std::optional<bms_content_value_t> {
        auto reference = bms_content_item_t{.value = before}.resource_identifier();
        if (!previous || event_resource(*previous) != event_resource(after)) reference.reset();
        const auto value = native(after, std::move(reference));
        if (!value) return std::nullopt;
        return *value;
    }
    auto note(const bms_note_content_t& before, const note_t& after,
        const bms_native_event_t& previous) const -> std::optional<bms_content_value_t> {
        if (after.end || after.sound_channel_event) return fallback(before, after, previous);
        const auto position = bms_program_position_t::from(after.position, geometry);
        if (!position) return std::nullopt;
        auto value = before;
        value.position = *position;
        value.lane = after.lane;
        value.kind = after.kind;
        value.attributes = bms_note_attributes_t::from(after);
        if (after.kind != note_kind_t::mine) value.mine_value.reset();
        if (event_resource(previous) != after.sound) {
            value.wav = after.sound ? identifier(*after.sound, bms_resource_namespace_t::wav) : std::nullopt;
            if (after.sound && !value.wav) return fallback(before, after, previous);
        }
        return value;
    }
    auto bgm(const bms_bgm_content_t& before, const bgm_event_t& after,
        const bms_native_event_t& previous) const -> std::optional<bms_content_value_t> {
        const auto* old = std::get_if<bgm_event_t>(&previous);
        if (!old || after.end || after.sound_channel_event || old->source_lane != after.source_lane)
            return fallback(before, after, previous);
        const auto position = bms_program_position_t::from(after.position, geometry);
        if (!position) return std::nullopt;
        if (*position != before.position) return fallback(before, after, previous);
        auto value = before;
        value.position = *position;
        value.semantic_layers = after.semantic_layers;
        if (old->sound != after.sound) {
            value.wav = after.sound ? identifier(*after.sound, bms_resource_namespace_t::wav) : std::nullopt;
            if (after.sound && !value.wav) return fallback(before, after, previous);
        }
        return value;
    }
    auto bga(const bms_bga_content_t& before, const bga_event_t& after,
        const bms_native_event_t& previous) const -> std::optional<bms_content_value_t> {
        const auto position = bms_program_position_t::from(after.position, geometry);
        if (!position) return std::nullopt;
        auto value = before;
        value.position = *position;
        value.kind = after.kind;
        if (event_resource(previous) != after.resource) {
            const auto reference = identifier(after.resource, bms_resource_namespace_t::bmp);
            if (!reference) return fallback(before, after, previous);
            value.bmp = *reference;
        }
        return value;
    }
    auto timing(const bms_timing_reference_t& before, const bms_native_event_t& after,
        const bms_native_event_t& previous) const -> std::optional<bms_content_value_t> {
        const auto old = bms_native_content_t::from(previous, original);
        const auto current = bms_native_content_t::from(after, geometry);
        if (!old || !current) return std::nullopt;
        if (!old->has_same_payload(*current)) return fallback(before, after, previous);
        auto value = before;
        value.position = current->positions().front();
        return value;
    }
    auto normalize(const bms_content_item_t& item, const bms_native_event_t& after) const
        -> std::optional<bms_content_value_t> {
        const auto previous = find_native_event(original, item.id);
        if (!previous) return std::nullopt;
        return std::visit([&](const auto& before) -> std::optional<bms_content_value_t> {
            using value_type = std::remove_cvref_t<decltype(before)>;
            if constexpr (std::same_as<value_type, bms_note_content_t>) {
                if (const auto* value = std::get_if<note_t>(&after)) return note(before, *value, *previous);
            } else if constexpr (std::same_as<value_type, bms_bgm_content_t>) {
                if (const auto* value = std::get_if<bgm_event_t>(&after)) return bgm(before, *value, *previous);
            } else if constexpr (std::same_as<value_type, bms_bga_content_t>) {
                if (const auto* value = std::get_if<bga_event_t>(&after)) return bga(before, *value, *previous);
            } else if constexpr (std::same_as<value_type, bms_timing_reference_t>) return timing(before, after, *previous);
            return fallback(item.value, after, previous);
        }, item.value);
    }
};

auto complete_endpoint_ids(const bms_program_document_t& program, std::span<const object_id_t> ids,
    std::optional<branch_id_t> authoring_chart = {})
    -> std::set<object_id_t> {
    std::set<object_id_t> result(ids.begin(), ids.end());
    const auto projection = program.build_projection(authoring_chart);
    if (!projection.value) return result;
    for (const auto id : ids) {
        const auto endpoints = program.prepared().program.hold_endpoints(projection.value->owners(id.value));
        for (const auto endpoint : endpoints) result.insert(endpoint.item);
    }
    return result;
}

void assign_new_layer_ids(chart_document_t& chart, const std::set<layer_id_t>& existing, object_id_t first) {
    std::map<layer_id_t, layer_id_t> remapped;
    for (auto& layer : chart.semantic_layers) {
        if (existing.contains(layer.id)) continue;
        const auto id = layer_id_t{first.value + remapped.size()};
        remapped.emplace(layer.id, id);
        layer.id = id;
    }
    for (auto& defaults : chart.sound_layer_defaults)
        for (auto& layer : defaults.layers)
            if (const auto found = remapped.find(layer); found != remapped.end()) layer = found->second;
}

auto resolve_object_target(const song_document_t& document, chart_id_t difficulty,
    std::span<const object_id_t> objects, const std::optional<bms_program_target_key_t>& selection)
    -> result_t<bms_program_edit_target_t, error_t<failure_code_t>> {
    const auto target = resolve_native_target(document, difficulty, objects.front(), selection);
    if (!target) return target;
    std::set<object_id_t> unique;
    for (const auto id : objects) {
        const auto owner = resolve_native_target(document, difficulty, id, selection);
        if (!unique.insert(id).second || !owner || owner->chart != target->chart)
            return std::unexpected(error_t<failure_code_t>{failure_code_t::conflict,
                "Scoped object edits require distinct objects with one complete semantic owner."});
    }
    return target;
}

auto has_same_native_positions(const chart_document_t& original, const chart_document_t& edited,
    const bms_native_event_t& event) -> bool {
    const auto before = bms_native_content_t::from(event, original);
    const auto after = bms_native_content_t::from(event, edited);
    return before && after && std::ranges::equal(before->positions(), after->positions());
}

struct native_batch_builder_t {
    const bms_program_document_t& program;
    const chart_document_t& original;
    const chart_document_t& edited;
    bms_program_content_edit_batch_t batch;
    diagnostic_list_t diagnostics;

    auto owner(object_id_t id) const -> bms_program_owner_t {
        return {.scope = batch.target.scope, .chart = batch.target.chart, .item = id};
    }
    void erase(object_id_t id) {
        batch.edits.push_back(bms_program_content_delete_t{.owner = owner(id)});
    }
    auto paired_tail(object_id_t id) const -> const bms_content_item_t* {
        const auto projection = program.build_projection(batch.target.authoring ? std::optional{batch.target.chart} : std::nullopt);
        if (!projection.value) return nullptr;
        const auto endpoints = program.prepared().program.hold_endpoints(projection.value->owners(id.value));
        if (endpoints.size() != 2) return nullptr;
        const auto tail = endpoints.front().item == id ? endpoints.back() : endpoints.front();
        if (tail.chart != batch.target.chart) return nullptr;
        const auto& items = std::get<bms_chart_content_t>(program.prepared().program.find(tail.chart)->value).items;
        const auto found = std::ranges::find(items, tail.item, &bms_content_item_t::id);
        return found == items.end() ? nullptr : &*found;
    }
    auto update_endpoint(const bms_content_item_t& item, const note_t& note) -> bool {
        const auto* endpoint = std::get_if<bms_hold_endpoint_t>(&item.value);
        if (!endpoint) return false;
        if (note.sound_channel_event) {
            diagnostics.push_back(error(diagnostic_code_t::unsupported_feature, "Scoped BMS hold edits cannot attach retained stem points."));
            return true;
        }
        const auto* previous = original.find_note(item.id);
        existing_event_normalizer_t normalizer{program, original, edited};
        if (!note.end) {
            const auto value = previous ? normalizer.note(bms_note_content_t{.position = endpoint->position,
                .lane = endpoint->lane, .wav = endpoint->wav, .attributes = endpoint->attributes}, note, *previous) : std::nullopt;
            if (!value) diagnostics.push_back(error(diagnostic_code_t::unsupported_feature, "Scoped hold conversion is unavailable."));
            else batch.edits.push_back(bms_program_content_update_t{.owner = owner(item.id), .value = *value});
            return true;
        }
        const auto* tail_item = paired_tail(item.id);
        const auto head_position = bms_program_position_t::from(note.position, edited);
        const auto tail_position = bms_program_position_t::from(*note.end, edited);
        if (!tail_item || !head_position || !tail_position || note.kind != note_kind_t::hold) {
            diagnostics.push_back(error(diagnostic_code_t::unsupported_feature, "Scoped hold edit requires its exact paired endpoints."));
            return true;
        }
        auto head = *endpoint;
        auto tail = std::get<bms_hold_endpoint_t>(tail_item->value);
        head.position = *head_position;
        head.lane = note.lane;
        head.attributes = bms_note_attributes_t::from(note);
        if (!previous || previous->sound != note.sound) {
            const auto reference = note.sound ? normalizer.identifier(*note.sound, bms_resource_namespace_t::wav) : std::nullopt;
            if (!reference) {
                diagnostics.push_back(error(diagnostic_code_t::unsupported_feature, "Scoped BMS hold assignment requires an active WAV slot."));
                return true;
            }
            head.wav = *reference;
        }
        tail.position = *tail_position;
        tail.lane = note.lane;
        batch.edits.push_back(bms_program_content_update_t{.owner = owner(item.id), .value = std::move(head)});
        batch.edits.push_back(bms_program_content_update_t{.owner = owner(tail_item->id), .value = std::move(tail)});
        return true;
    }
    void update(object_id_t id, const bms_native_event_t& event) {
        const auto previous = find_native_event(original, id);
        if (previous && *previous == event && has_same_native_positions(original, edited, event)) return;
        const auto& items = std::get<bms_chart_content_t>(program.prepared().program.find(batch.target.chart)->value).items;
        const auto found = std::ranges::find(items, id, &bms_content_item_t::id);
        if (found != items.end())
            if (const auto* note = std::get_if<note_t>(&event); note && update_endpoint(*found, *note)) return;
        const auto value = found == items.end() ? std::nullopt
            : existing_event_normalizer_t{program, original, edited}.normalize(*found, event);
        if (!value) diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
            "Scoped operation did not produce its named authored event.", id));
        else batch.edits.push_back(bms_program_content_update_t{.owner = owner(id), .value = *value});
    }
    void insert(object_id_t id, const bms_native_event_t& event) {
        const auto value = existing_event_normalizer_t{program, original, edited}.native(event);
        if (!value) diagnostics.push_back(error(diagnostic_code_t::invalid_measure,
            "Scoped insertion requires exact measure positions."));
        else batch.edits.push_back(bms_program_content_insert_t{.item = {.id = id, .value = *value}});
    }
    void edit_existing(std::span<const object_id_t> objects, bms_existing_edit_kind_t kind) {
        for (const auto id : objects) {
            if (kind == bms_existing_edit_kind_t::erase) { erase(id); continue; }
            const auto event = find_native_event(edited, id);
            if (!event) {
                diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
                    "Scoped operation did not produce its named event.", id));
                return;
            }
            update(id, *event);
        }
        complete_endpoints();
    }
    void edit_event(std::optional<object_id_t> existing, std::optional<object_id_t> inserted,
                    const std::optional<bms_native_event_t>& event) {
        if (existing) {
            if (event) update(*existing, *event);
            else erase(*existing);
            complete_endpoints();
            return;
        }
        if (!inserted || !event) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                "Scoped insertion has no event or available identity."));
            return;
        }
        insert(*inserted, *event);
    }
    void complete_endpoints() {
        std::vector<object_id_t> affected;
        for (const auto& operation : batch.edits)
            std::visit([&](const auto& edit) {
                if constexpr (requires { edit.owner; }) affected.push_back(edit.owner.item);
            }, operation);
        const std::set requested(affected.begin(), affected.end());
        for (const auto id : complete_endpoint_ids(program, affected, batch.target.authoring ? std::optional{batch.target.chart} : std::nullopt))
            if (!requested.contains(id)) erase(id);
    }
};

struct content_editor_t {
    const bms_program_edit_target_t& target;
    std::vector<bms_content_item_t>& items;
    const std::set<object_id_t>& cross_owner;
    std::uint64_t reserved_through;
    diagnostic_list_t diagnostics;
    bool changed{};

    auto find(bms_program_owner_t owner) -> bms_content_item_t* {
        if (owner.scope != target.scope || owner.chart != target.chart || cross_owner.contains(owner.item)) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                "Edit does not target the object's complete semantic owner.", owner.item));
            return nullptr;
        }
        const auto found = std::ranges::find(items, owner.item, &bms_content_item_t::id);
        if (found != items.end()) return &*found;
        diagnostics.push_back(error(diagnostic_code_t::invalid_id,
            "Scoped object no longer exists.", owner.item));
        return nullptr;
    }
    auto has_available_child_ids(const bms_content_value_t& value,
        const bms_content_value_t* previous = nullptr) -> bool {
        const auto* native = std::get_if<bms_native_content_t>(&value);
        if (!native) return true;
        std::vector<object_id_t> retained;
        if (previous)
            if (const auto* old = std::get_if<bms_native_content_t>(previous)) retained = old->declared_ids();
        for (const auto id : native->declared_ids()) {
            if (id.value > reserved_through || std::ranges::find(retained, id) != retained.end()) continue;
            diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                "New scoped child content requires a history-safe identity.", id));
            return false;
        }
        return true;
    }
    void operator()(const bms_program_content_insert_t& edit) {
        if (edit.item.id.value <= reserved_through ||
            std::ranges::find(items, edit.item.id, &bms_content_item_t::id) != items.end()) {
            diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                "New scoped content requires an unused history-safe identity.", edit.item.id));
            return;
        }
        if (!has_available_child_ids(edit.item.value)) return;
        items.push_back(edit.item);
        changed = true;
    }
    void operator()(const bms_program_content_update_t& edit) {
        auto* item = find(edit.owner);
        if (!item || item->value == edit.value) return;
        if (!has_available_child_ids(edit.value, &item->value)) return;
        item->value = edit.value;
        changed = true;
    }
    void operator()(const bms_program_content_delete_t& edit) {
        if (!find(edit.owner)) return;
        std::erase_if(items, [&](const auto& item) { return item.id == edit.owner.item; });
        changed = true;
    }
};

auto selector_values(const bms_program_t& program, const bms_program_node_t& node,
    std::size_t maximum) -> result_t<std::vector<std::int64_t>, error_t<failure_code_t>> {
    std::vector<std::int64_t> values;
    if (const auto* random = std::get_if<bms_random_scope_t>(&node.value)) {
        if (static_cast<std::uint64_t>(random->range) > maximum)
            return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded,
                "Scoped edit exceeds its bounded evaluation budget."});
        for (std::int64_t value = 1; value <= random->range; ++value) values.push_back(value);
    } else if (std::holds_alternative<bms_switch_scope_t>(node.value)) {
        bool has_default{};
        for (const auto child : node.children) {
            const auto* arm = std::get_if<bms_case_scope_t>(&program.find(child)->value);
            if (!arm) continue;
            if (arm->value) values.push_back(*arm->value);
            else has_default = true;
        }
        if (has_default) {
            const std::set occupied(values.begin(), values.end());
            std::int64_t fallback{};
            while (occupied.contains(fallback)) ++fallback;
            values.push_back(fallback);
        }
    }
    return values;
}

auto evaluation_candidates(const bms_program_document_t& owner, std::size_t maximum)
    -> result_t<std::vector<bms_program_evaluation_t>, error_t<failure_code_t>> {
    std::vector<bms_program_evaluation_t> candidates(1);
    if (!maximum) return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded,
        "Scoped publication needs an evaluation budget."});
    const auto nodes = owner.prepared().program.nodes();
    const auto has_fixed_random = std::ranges::any_of(nodes, [](const auto& node) {
        return std::holds_alternative<bms_set_random_t>(node.value);
    });
    const auto has_fixed_switch = std::ranges::any_of(nodes, [](const auto& node) {
        return std::holds_alternative<bms_set_switch_t>(node.value);
    });
    for (const auto& node : nodes) {
        auto values = selector_values(owner.prepared().program, node, maximum);
        if (!values) return std::unexpected(values.error());
        if (values->empty()) continue;
        const auto include_implicit = (has_fixed_random && std::holds_alternative<bms_random_scope_t>(node.value)) ||
            (has_fixed_switch && std::holds_alternative<bms_switch_scope_t>(node.value));
        if (values->size() + static_cast<std::size_t>(include_implicit) > maximum / candidates.size())
            return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded,
                "Scoped edit exceeds its bounded evaluation budget."});
        std::vector<bms_program_evaluation_t> expanded;
        for (const auto& candidate : candidates) {
            // Leaving this choice unset exercises authored SETRANDOM/SETSWITCH.
            // Explicit preview choices override settings and cannot prove them valid.
            if (include_implicit) expanded.push_back(candidate);
            for (const auto value : *values) {
                auto evaluation = candidate;
                evaluation.selections.push_back({node.id, value});
                expanded.push_back(std::move(evaluation));
            }
        }
        candidates = std::move(expanded);
    }
    return candidates;
}

// Reuse the evaluator's reachability checks instead of reimplementing IF/SWITCH
// interpretation for content and structural authoring edits.
auto reachable_evaluation(const bms_program_document_t& owner, bms_program_evaluation_t evaluation)
    -> bms_program_evaluation_t {
    const auto projection = bms_program_projection_t::from(owner.prepared(), evaluation);
    for (const auto& diagnostic : projection.diagnostics) {
        if (diagnostic.code != failure_code_t::unavailable) continue;
        std::erase_if(evaluation.selections,
            [&](const auto& selection) { return selection.branch == diagnostic.node; });
    }
    return evaluation;
}

auto has_changed_cross_owner_hold(const bms_program_document_t& before,
    const bms_program_document_t& proposed, const bms_program_projection_t& projection,
    const bms_program_document_t& observed) -> bool {
    for (const auto& measure : projection.song().charts.front().measures) {
        for (const auto& note : measure.notes) {
            if (!note.end) continue;
            const auto endpoints = observed.prepared().program.hold_endpoints(projection.owners(note.id.value));
            if (endpoints.size() != 2 || endpoints.front().chart == endpoints.back().chart) continue;
            for (const auto endpoint : endpoints)
                if (!has_unchanged_item(&before, proposed, endpoint.chart, endpoint.item)) return true;
        }
    }
    return false;
}

auto validate_scoped_projection(song_document_t song) -> diagnostic_list_t {
    // Cross-difficulty associations are validated against the complete project.
    for (auto& chart : song.charts) chart.timing_links = {};
    for (auto& resource : song.resources) resource.sound_group.reset();
    return validate_document(song);
}

auto validate_evaluation(const bms_program_document_t* previous,
    const bms_program_document_t& owner, bms_program_evaluation_t evaluation) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    const auto projection = bms_program_projection_t::from(owner.prepared(), evaluation);
    if (!projection.value) {
        for (const auto& diagnostic : projection.diagnostics)
            diagnostics.push_back(error(diagnostic_code_t::invalid_binding, diagnostic.message, diagnostic.item));
        return diagnostics;
    }
    const auto old = previous ? bms_program_projection_t::from(previous->prepared(), evaluation) :
        attempt_t<bms_program_projection_t, bms_program_projection_diagnostics_t>{};
    const auto* before = old.value ? &*old.value : nullptr;
    if (previous && before && (has_changed_cross_owner_hold(*previous, owner, *before, *previous) ||
        has_changed_cross_owner_hold(*previous, owner, *projection.value, owner)))
        diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
            "Scoped edit would partially change a cross-owner hold in an affected evaluation."));
    if (const auto collision = new_note_collision(*projection.value, before))
        diagnostics.push_back(error(diagnostic_code_t::collision,
            "Scoped edit would overlap playable notes in an affected evaluation.", *collision));
    for (const auto& diagnostic : projection.diagnostics) {
        if (diagnostic.code != failure_code_t::not_found || diagnostic.is_error()) continue;
        if (has_existing_missing_reference(previous, owner, diagnostic, old.diagnostics)) continue;
        diagnostics.push_back(error(diagnostic_code_t::dangling_resource,
            "Scoped edit would introduce an unresolved resource or timing reference.", diagnostic.item));
    }
    auto validation = validate_scoped_projection(projection.value->song());
    diagnostics.insert(diagnostics.end(), validation.begin(), validation.end());
    return diagnostics;
}
} // namespace

auto bms_program_document_t::read_chart() const -> const chart_document_t* {
    std::call_once(playback_once_, [this] {
        const auto projection = build_projection();
        if (!projection.value || !projection.ok()) return;
        view_ = projection.value->song();
        view_->charts.front().id = difficulty_;
        playback_resources_ = document_id_index_t<resource_entry_t>::from(view_->resources);
    });
    return view_ ? &view_->charts.front() : nullptr;
}

auto bms_program_document_t::read_authoring_chart(branch_id_t chart) const -> const chart_document_t* {
    if (!authoring_view_ || authoring_view_->first != chart) {
        auto projection = build_projection(chart);
        if (!projection.value || !projection.ok()) return nullptr;
        auto song = projection.value->song();
        song.charts.front().id = difficulty_;
        authoring_view_ = std::pair{chart, std::move(song)};
        authoring_resources_ = document_id_index_t<resource_entry_t>::from(authoring_view_->second.resources);
    }
    return &authoring_view_->second.charts.front();
}

auto bms_program_document_t::read_catalog(std::optional<branch_id_t> authoring_chart) const
    -> const song_document_t* {
    if (authoring_chart)
        return read_authoring_chart(*authoring_chart) ? &authoring_view_->second : nullptr;
    return read_chart() ? &*view_ : nullptr;
}

auto bms_program_document_t::read_bound_resource(resource_id_t id,
    std::optional<branch_id_t> authoring_chart) const -> const resource_entry_t* {
    if (!read_catalog(authoring_chart)) return nullptr;
    return authoring_chart ? authoring_resources_.find(id) : playback_resources_.find(id);
}

auto bms_program_document_t::read_resource(resource_id_t id) const -> const resource_entry_t* {
    const auto canonical = std::ranges::find(prepared_.resources, id, &resource_entry_t::id);
    if (canonical != prepared_.resources.end()) return &*canonical;
    if (!read_chart()) return nullptr;
    const auto found = std::ranges::find(view_->resources, id, &resource_entry_t::id);
    return found == view_->resources.end() ? nullptr : &*found;
}

auto bms_program_document_t::build_projection(std::optional<branch_id_t> authoring_chart) const
    -> attempt_t<bms_program_projection_t, bms_program_projection_diagnostics_t> {
    return authoring_chart ? bms_program_projection_t::for_authoring(prepared_, *authoring_chart, evaluation_)
        : bms_program_projection_t::from(prepared_, evaluation_);
}

auto bms_program_document_t::from(chart_id_t difficulty, bms_prepared_program_t prepared,
    bms_program_evaluation_t evaluation)
    -> attempt_t<std::shared_ptr<const bms_program_document_t>, bms_program_projection_diagnostics_t> {
    if (!difficulty) return {.diagnostics = {{.code = failure_code_t::invalid_request,
        .message = "Prepared difficulty requires a stable identity."}}};
    auto candidate = std::shared_ptr<const bms_program_document_t>(
        new bms_program_document_t(difficulty, std::move(prepared), std::move(evaluation)));
    auto projection = candidate->build_projection();
    if (!projection.value || !projection.ok()) return {.diagnostics = std::move(projection.diagnostics)};
    return {.value = std::move(candidate), .diagnostics = std::move(projection.diagnostics)};
}

namespace {
struct fresh_program_builder_t {
    const chart_document_t& chart;
    std::uint64_t next;
    bool valid{true}, initial_bpm{};
    bms_chart_content_t body;
    std::vector<measure_id_t> measures;
    std::map<std::uint64_t, std::uint64_t> measure_ids, object_ids, recipe_ids, column_ids, layer_ids, child_ids;
    std::vector<bgm_source_lane_t> columns;
    std::vector<semantic_layer_t> layers;
    std::vector<sound_layer_default_t> defaults;

    auto allocate() -> object_id_t {
        if (!next || next == UINT64_MAX) { valid = false; return {}; }
        return object_id_t{next++};
    }
    template<class Id> void mapped(Id& id, const std::map<std::uint64_t, std::uint64_t>& ids) {
        const auto found = ids.find(id.value);
        if (found == ids.end()) valid = false;
        else id.value = found->second;
    }
    void geometry() {
        for (const auto& measure : chart.measures) {
            const auto id = measure_id_t{allocate().value};
            if (!measure_ids.emplace(measure.id.value, id.value).second) valid = false;
            measures.push_back(id);
            body.items.push_back({.id = allocate(), .value = bms_measure_length_t{
                .measure = id, .length = measure.definition.length, .meter = measure.definition.meter}});
        }
    }
    void pools() {
        columns = chart.bgm_source_lanes;
        for (auto& column : columns) {
            const auto old = column.id;
            column.id = allocate();
            if (!column_ids.emplace(old.value, column.id.value).second) valid = false;
        }
        layers = chart.semantic_layers;
        for (auto& layer : layers) {
            const auto old = layer.id;
            layer.id = layer_id_t{allocate().value};
            if (!layer_ids.emplace(old.value, layer.id.value).second) valid = false;
        }
        defaults = chart.sound_layer_defaults;
        for (auto& entry : defaults)
            for (auto& id : entry.layers) mapped(id, layer_ids);
        for (const auto& channel : chart.sound_channels) {
            child_ids.emplace(channel.source_clip.id.value, allocate().value);
            for (const auto& event : channel.events) child_ids.emplace(event.id.value, allocate().value);
        }
    }
    template<class Event> void references(Event& event) {
        if constexpr (requires { event.semantic_layers; })
            for (auto& id : event.semantic_layers) mapped(id, layer_ids);
        if constexpr (requires { event.source_lane; }) mapped(event.source_lane, column_ids);
        if constexpr (requires { event.sound_channel_event; })
            if (event.sound_channel_event) mapped(*event.sound_channel_event, child_ids);
        if constexpr (std::same_as<Event, sound_channel_t>) {
            mapped(event.source_clip.id, child_ids);
            for (auto& child : event.events) mapped(child.id, child_ids);
        }
    }
    template<class Event> void event(const Event& event) {
        const auto id = allocate();
        auto& identities = std::same_as<Event, sv_recipe_t> ? recipe_ids : object_ids;
        if (!identities.emplace(event.id.value, id.value).second) valid = false;
        if constexpr (std::same_as<Event, bpm_event_t>) {
            if (!initial_bpm && event.position == local_position_t{chart.measures.front().id, 0}) {
                initial_bpm = true;
                body.items.push_back({.id = id, .value = bms_timing_definition_t{.value = event.beats_per_minute}});
                return;
            }
        }
        auto detached = event;
        references(detached);
        auto native = bms_native_content_t::from(std::move(detached), chart);
        if (!native) { valid = false; return; }
        auto positions = std::vector<bms_program_position_t>(native->positions().begin(), native->positions().end());
        for (auto& position : positions) mapped(position.measure, measure_ids);
        auto value = bms_native_content_t::from(native->payload(), std::move(positions), native->resource_reference());
        if (!value) valid = false;
        else body.items.push_back({.id = id, .value = std::move(*value)});
    }
    template<class Events> void events(const Events& values) { for (const auto& value : values) event(value); }
    auto links() -> timing_links_t {
        auto result = chart.timing_links;
        for (auto& link : result.categories) {
            if (!link) continue;
            for (auto& identity : link->objects) mapped(identity.target, object_ids);
            for (auto& identity : link->recipes) mapped(identity.target, recipe_ids);
            for (auto& identity : link->measures) mapped(identity.target, measure_ids);
        }
        return result;
    }
};
auto is_empty_creation_chart(const chart_document_t& chart) -> bool {
    return !chart.measures.empty() && chart.lane_layouts.size() == 1 && !chart.bms_control_tree &&
        chart.bga_events.empty() && chart.sound_channels.empty() && chart.semantic_layers.empty() &&
        chart.sound_layer_defaults.empty() && chart.bookmarks.empty() && chart.sections.empty() &&
        std::ranges::none_of(chart.measures, [](const auto& measure) {
            return !measure.notes.empty() || !measure.bgm_events.empty();
        });
}
}
auto bms_program_document_t::from_empty_chart(const chart_document_t& chart, object_id_t first_id)
    -> attempt_t<std::shared_ptr<const bms_program_document_t>, bms_program_projection_diagnostics_t> {
    if (!first_id || !is_empty_creation_chart(chart)) return {.diagnostics = {{.code = failure_code_t::invalid_request,
        .message = "Fresh scoped creation requires an empty chart with one initial layout."}}};
    return from_native_chart(chart, {}, first_id);
}
auto bms_program_document_t::from_native_chart(const chart_document_t& chart,
    const song_document_t& catalog, object_id_t first_id)
    -> attempt_t<std::shared_ptr<const bms_program_document_t>, bms_program_projection_diagnostics_t> {
    if (!first_id || chart.measures.empty() || chart.lane_layouts.size() > 1 || chart.bms_control_tree)
        return {.diagnostics = {{.code = failure_code_t::invalid_request,
            .message = "Native migration requires a single static layout and no retained control graph."}}};
    fresh_program_builder_t build{.chart = chart, .next = first_id.value};
    const auto root = branch_id_t{build.allocate().value};
    const auto content = branch_id_t{build.allocate().value};
    build.geometry();
    build.pools();
    for (const auto& measure : chart.measures) {
        build.events(measure.notes);
        build.events(measure.bgm_events);
    }
    build.events(chart.bga_events); build.events(chart.bookmarks); build.events(chart.sections);
    build.events(chart.sound_channels);
    build.events(chart.bpms); build.events(chart.pauses); build.events(chart.warps);
    build.events(chart.scrolls); build.events(chart.speeds); build.events(chart.kiai);
    build.events(chart.timing_anchors); build.events(chart.sv_recipes);
    if (chart.beat_zero_audio_offset_seconds.numerator)
        build.body.items.push_back({.id = build.allocate(), .value = bms_metadata_declaration_t{
            .field = bms_metadata_field_t::offset, .value = chart.beat_zero_audio_offset_seconds}});
    auto links = build.links();
    const auto layout = build.allocate();
    if (!build.valid) return {.diagnostics = {{.code = failure_code_t::capacity_exceeded,
        .message = "Fresh scoped identities or exact timing mappings are unavailable."}}};
    auto program = bms_program_t::from({{.id = root, .value = bms_root_scope_t{}, .children = {content}},
        {.id = content, .value = std::move(build.body)}});
    if (!program) return {.diagnostics = {{.code = program.error().code, .message = program.error().message}}};
    std::optional<lane_input_profile_t> profile;
    if (!chart.lane_layouts.empty()) profile = lane_input_profile_t{
        .layout = chart.lane_layouts.front().layout, .input = chart.input_profile};
    auto prepared = bms_prepared_program_t{.program = std::move(*program), .measures = std::move(build.measures),
        .lane_profile = std::move(profile),
        .bgm_columns = std::move(build.columns), .semantic_layers = std::move(build.layers),
        .sound_layer_defaults = std::move(build.defaults),
        .settings = {.name = chart.metadata.name, .author = chart.metadata.author, .rating = chart.metadata.rating,
            .overrides = chart.metadata.overrides, .divisions_per_standard_measure = chart.divisions_per_standard_measure,
            .layout_segment = layout, .timing_links = std::move(links), .show_empty_bgm_columns = true}};
    return from(chart.id, prepared.with_catalog(catalog));
}

auto bms_program_edit_target_t::from(const song_document_t& document,
    chart_id_t difficulty, std::optional<branch_id_t> selected)
    -> result_t<bms_program_edit_target_t, error_t<failure_code_t>> {
    return resolve_target(document, difficulty, selected);
}

auto bms_program_edit_target_t::from(const song_document_t& document,
    chart_id_t difficulty, object_id_t existing_item)
    -> result_t<bms_program_edit_target_t, error_t<failure_code_t>> {
    const auto* owner = document.find_bms_program(difficulty);
    if (!existing_item || !owner)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::not_found,
            "Existing scoped item or difficulty is unavailable."});
    if (cross_owner_endpoints(*owner).contains(existing_item))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::conflict,
            "Existing hold spans multiple semantic owners."});
    for (const auto& node : owner->prepared().program.nodes()) {
        const auto* content = std::get_if<bms_chart_content_t>(&node.value);
        if (!content) continue;
        if (std::ranges::find(content->items, existing_item, &bms_content_item_t::id) != content->items.end())
            return resolve_target(document, difficulty, node.id);
    }
    return std::unexpected(error_t<failure_code_t>{failure_code_t::not_found,
        "Existing scoped item no longer exists."});
}

auto bms_program_edit_target_t::for_authoring(const song_document_t& document,
    chart_id_t difficulty, branch_id_t selected, std::optional<object_id_t> existing)
    -> result_t<bms_program_edit_target_t, error_t<failure_code_t>> {
    auto target = resolve_target(document, difficulty, selected, true);
    if (!target || !existing) return target;
    const auto* owner = document.find_bms_program(difficulty);
    const auto& items = std::get<bms_chart_content_t>(owner->prepared().program.find(target->chart)->value).items;
    if (std::ranges::find(items, *existing, &bms_content_item_t::id) == items.end())
        return std::unexpected(error_t<failure_code_t>{failure_code_t::unavailable,
            "Inherited content is read-only; select its owning node to edit it."});
    const auto* view = owner->read_authoring_chart(target->chart);
    if (!view || view->is_bms_authoring_read_only(*existing))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::unavailable,
            "This object is read-only context in the authoring view."});
    return target;
}

auto bms_program_edit_target_t::validate(const song_document_t& document) const
    -> result_t<const bms_program_document_t*, error_t<failure_code_t>> {
    if (!stamp.is_current(document))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::stale_generation,
            stale_action_message("Scoped edit")});
    const auto current = resolve_target(document, difficulty, chart, authoring);
    if (!current) return std::unexpected(current.error());
    if (current->scope != scope)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::stale_generation,
            "Chart no longer belongs to the captured scope."});
    return document.find_bms_program(difficulty);
}

auto document_command_transaction_t::next_bms_program_item_id(std::size_t count) const
    -> std::optional<object_id_t> {
    auto maximum = maximum_program_id(current_document());
    for (const auto& record : history_.undo) {
        maximum = std::max({maximum, maximum_program_id(record.before), maximum_program_id(record.after)});
    }
    for (const auto& record : history_.redo) {
        maximum = std::max({maximum, maximum_program_id(record.before), maximum_program_id(record.after)});
    }
    if (!count || count > std::numeric_limits<std::uint64_t>::max() - maximum) return std::nullopt;
    return object_id_t{maximum + 1};
}

auto document_command_transaction_t::stage(const bms_program_content_edit_batch_t& edit)
    -> attempt_t<document_edit_effect_t> {
    const auto key = bms_program_target_key_t{.difficulty = edit.target.difficulty, .chart = edit.target.chart};
    if (edit.target.authoring && bms_target_ && *bms_target_ != key) {
        (void)reject(diagnostic_code_t::invalid_id, "Authoring target changed; select the owning node again.");
        return {.diagnostics = outcome().diagnostics};
    }
    if (bms_write_owner_ && *bms_write_owner_ != key) {
        (void)reject(diagnostic_code_t::invalid_id, "Scoped transaction cannot cross semantic owners.");
        return {.diagnostics = outcome().diagnostics};
    }
    bms_write_owner_ = key;
    return stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        const auto owner = edit.target.validate(document);
        if (!owner) return {.diagnostics = {error(diagnostic_code_t::invalid_id, owner.error().message)}};
        const auto& source = (*owner)->prepared();
        auto nodes = std::vector<bms_program_node_t>(source.program.nodes().begin(), source.program.nodes().end());
        auto node = std::ranges::find(nodes, edit.target.chart, &bms_program_node_t::id);
        auto& items = std::get<bms_chart_content_t>(node->value).items;
        auto cross_owner = cross_owner_endpoints(**owner, edit.target.authoring ? std::optional{edit.target.chart} : std::nullopt);
        if (edit.target.authoring)
            if (const auto* view = (*owner)->read_authoring_chart(edit.target.chart))
                for (const auto id : view->bms_authoring_read_only) cross_owner.insert(object_id_t{id});
        const auto next_id = next_bms_program_item_id();
        const auto maximum = next_id ? next_id->value - 1 : std::numeric_limits<std::uint64_t>::max();
        content_editor_t editor{.target = edit.target, .items = items,
            .cross_owner = cross_owner, .reserved_through = maximum};
        for (const auto& operation : edit.edits) std::visit(editor, operation);
        if (!editor.diagnostics.empty()) return {.diagnostics = std::move(editor.diagnostics)};
        if (!editor.changed) return {.value = document_edit_effect_t{}};
        auto program = bms_program_t::from(std::move(nodes));
        if (!program) return {.diagnostics = {error(diagnostic_code_t::invalid_id, program.error().message)}};
        if (*program == (*owner)->prepared().program) return {.value = document_edit_effect_t{}};
        auto prepared = source;
        prepared.program = std::move(*program);
        prepared = prepared.with_catalog(document);
        auto proposed = std::shared_ptr<const bms_program_document_t>(new bms_program_document_t(
            (*owner)->difficulty(), std::move(prepared), (*owner)->evaluation()));
        const auto destination = std::ranges::find_if(document.prepared_bms_programs,
            [&](const auto& value) { return value.get() == *owner; });
        *destination = std::move(proposed);
        return {.value = document_edit_effect_t{.changed = true, .command_label = edit.label}};
    });
}

auto validate_prepared_bms_programs(const song_document_t& document) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    std::set<chart_id_t> difficulties;
    for (const auto& owner : document.prepared_bms_programs) {
        if (!owner || !owner->difficulty() || !difficulties.insert(owner->difficulty()).second ||
            document.find_chart(owner->difficulty())) {
            diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                "Prepared difficulty must have one semantic owner and no writable chart mirror."));
            continue;
        }
        const auto projection = owner->build_projection();
        for (const auto& diagnostic : projection.diagnostics)
            diagnostics.push_back({.severity = diagnostic.severity,
                .code = diagnostic_code_t::invalid_binding, .message = diagnostic.message,
                .object = diagnostic.item ? std::optional{diagnostic.item} : std::nullopt});
        if (!projection.value) {
            if (projection.diagnostics.empty()) diagnostics.push_back(error(
                diagnostic_code_t::invalid_binding, "Prepared scoped projection failed."));
            continue;
        }
        auto validation = validate_scoped_projection(projection.value->song());
        diagnostics.insert(diagnostics.end(), validation.begin(), validation.end());
    }
    return diagnostics;
}

auto validate_bms_program_publication(const song_document_t& before,
    const song_document_t& proposed, std::size_t maximum_evaluations) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    for (const auto& owner : proposed.prepared_bms_programs) {
        if (!owner) continue; // The shared document validator rejects malformed ownership.
        const auto* previous = before.find_bms_program(owner->difficulty());
        if (previous == owner.get()) continue;
        const auto evaluations = evaluation_candidates(*owner, maximum_evaluations);
        if (!evaluations) {
            diagnostics.push_back(error(diagnostic_code_t::unsupported_feature, evaluations.error().message));
            continue;
        }
        for (const auto& evaluation : *evaluations) {
            auto validation = validate_evaluation(previous, *owner, reachable_evaluation(*owner, evaluation));
            diagnostics.insert(diagnostics.end(), validation.begin(), validation.end());
            if (diagnostic_queries::has_errors(diagnostics)) return diagnostics;
        }
    }
    return diagnostics;
}

namespace {
auto has_unchanged_measure_prefix(const chart_document_t& before, const chart_document_t& after) -> bool {
    if (after.measures.size() < before.measures.size() || after.divisions_per_standard_measure != before.divisions_per_standard_measure) return false;
    for (std::size_t index = 0; index < before.measures.size(); ++index)
        if (before.measures[index].id != after.measures[index].id || before.measures[index].definition != after.measures[index].definition) return false;
    return true;
}
auto remap_extent_event(const bms_native_event_t& event, const chart_document_t& before,
    const chart_document_t& after, const std::map<measure_id_t, measure_id_t>& ids) -> std::optional<bms_native_event_t> {
    const auto native = bms_native_content_t::from(event, before);
    if (!native) return {};
    auto positions = std::vector<bms_program_position_t>(native->positions().begin(), native->positions().end());
    for (auto& position : positions)
        if (const auto found = ids.find(position.measure); found != ids.end()) position.measure = found->second;
    const auto remapped = bms_native_content_t::from(native->payload(), std::move(positions), native->resource_reference());
    const auto id = std::visit([](const auto& value) { return object_id_t{value.id.value}; }, event);
    return remapped ? remapped->build_event(id, after) : std::nullopt;
}
auto stage_scoped_extent(document_command_transaction_t& transaction, chart_id_t difficulty,
    const chart_document_t& original, chart_document_t& edited, std::optional<bms_native_event_t>* event = nullptr)
    -> attempt_t<document_edit_effect_t> {
    if (!has_unchanged_measure_prefix(original, edited)) return {.diagnostics = {error(diagnostic_code_t::unsupported_feature,
        "This scoped content command cannot replace shared measure geometry.")}};
    if (edited.measures.size() == original.measures.size()) return {.value = document_edit_effect_t{}};
    const auto count = edited.measures.size() - original.measures.size();
    const auto first = transaction.next_bms_program_item_id(count);
    if (!first) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped measure identities are exhausted.")}};
    std::map<measure_id_t, measure_id_t> ids;
    for (std::size_t index = original.measures.size(); index < edited.measures.size(); ++index) {
        const auto& measure = edited.measures[index];
        if (measure.definition.length != rational_t{1, 1}) return {.diagnostics = {error(diagnostic_code_t::unsupported_feature,
            "Virtual scoped measures must use the shared default length.")}};
        if (!ids.emplace(measure.id, measure_id_t{first->value + index - original.measures.size()}).second)
            return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped measure identities are ambiguous.")}};
    }
    const auto previous = edited;
    for_each_chart_position(edited, [&](local_position_t& position, measure_chart_end_policy_t) {
        if (const auto found = ids.find(position.measure); found != ids.end()) position.measure = found->second;
    });
    for (auto& measure : edited.measures)
        if (const auto found = ids.find(measure.id); found != ids.end()) measure.id = found->second;
    if (event && *event) {
        *event = remap_extent_event(**event, previous, edited, ids);
        if (!*event) return {.diagnostics = {error(diagnostic_code_t::invalid_measure, "Scoped event cannot use the extended geometry exactly.")}};
    }
    return transaction.stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        const auto* source = document.find_bms_program(difficulty);
        auto prepared = source->prepared();
        for (std::size_t index = original.measures.size(); index < edited.measures.size(); ++index)
            prepared.measures.push_back(edited.measures[index].id);
        auto replacement = bms_program_document_t::from(difficulty, std::move(prepared), source->evaluation());
        if (!replacement.value || !replacement.ok()) return {.diagnostics = {error(diagnostic_code_t::invalid_measure,
            "The shared scoped extent cannot be extended without changing an existing outcome.")}};
        const auto destination = std::ranges::find_if(document.prepared_bms_programs,
            [&](const auto& owner) { return owner.get() == source; });
        *destination = std::move(*replacement.value);
        return {.value = document_edit_effect_t{.changed = true, .command_label = "Extend scoped measures"}};
    });
}
}

auto document_command_transaction_t::stage_bms_measure_extent(chart_id_t difficulty, std::size_t count)
    -> attempt_t<document_edit_effect_t> {
    const auto* view = read_chart(difficulty);
    if (!view || !has_bms_program(difficulty) || count > bms_addressable_measure_count)
        return {.diagnostics = {error(diagnostic_code_t::invalid_measure, "Scoped extent exceeds the BMS measure range.")}};
    if (count <= view->measures.size()) return {.value = document_edit_effect_t{}};
    const auto original = *view;
    auto edited = original;
    const auto first = next_bms_program_item_id(count - edited.measures.size());
    if (!first) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped measure identities are exhausted.")}};
    while (edited.measures.size() < count) edited.measures.push_back({.id = measure_id_t{
        first->value + edited.measures.size() - original.measures.size()}});
    return stage_scoped_extent(*this, difficulty, original, edited);
}

namespace {
auto native_event_map(const chart_document_t& chart) -> std::map<object_id_t, bms_native_event_t> {
    std::map<object_id_t, bms_native_event_t> result;
    auto include = [&](const auto& values) { for (const auto& value : values) result.emplace(object_id_t{value.id.value}, value); };
    for (const auto& measure : chart.measures) { include(measure.notes); include(measure.bgm_events); }
    [&]<std::size_t... Index>(std::index_sequence<Index...>) {
        ([&] {
            using value_t = std::variant_alternative_t<Index, bms_native_event_t>;
            if constexpr (!std::same_as<value_t, note_t> && !std::same_as<value_t, bgm_event_t>)
                include(chart.*bms_native_collection_t<value_t>::member);
        }(), ...);
    }(std::make_index_sequence<std::variant_size_v<bms_native_event_t>>{});
    return result;
}

void remap_new_geometry_measures(const chart_document_t& before, chart_document_t& after, std::uint64_t& next) {
    std::map<measure_id_t, measure_id_t> replacements;
    for (auto& measure : after.measures)
        if (!before.find_measure(measure.id)) replacements.emplace(measure.id, measure_id_t{next++});
    for_each_chart_position(after, [&](local_position_t& position, measure_chart_end_policy_t) {
        if (const auto found = replacements.find(position.measure); found != replacements.end()) position.measure = found->second;
    });
    for (auto& measure : after.measures)
        if (const auto found = replacements.find(measure.id); found != replacements.end()) measure.id = found->second;
}

struct structure_program_builder_t {
    const bms_program_document_t& owner;
    const chart_document_t& before;
    const chart_document_t& after;
    std::uint64_t next;
    std::vector<bms_program_node_t> nodes;
    diagnostic_list_t diagnostics;

    void update_chart(bms_program_node_t& node, bms_chart_content_t& chart) {
        native_batch_builder_t builder{.program = owner, .original = before, .edited = after,
            .batch = {.target = {.difficulty = owner.difficulty(), .scope = *owner.prepared().program.parent(node.id),
                .chart = node.id}}};
        for (const auto& item : chart.items) {
            if (!find_native_event(before, item.id)) continue;
            const auto event = find_native_event(after, item.id);
            if (event) builder.update(item.id, *event);
            else builder.erase(item.id);
        }
        builder.complete_endpoints();
        if (!builder.diagnostics.empty()) { diagnostics = std::move(builder.diagnostics); return; }
        const std::set<object_id_t> cross_owner;
        content_editor_t editor{.target = builder.batch.target, .items = chart.items, .cross_owner = cross_owner, .reserved_through = next - 1};
        for (const auto& edit : builder.batch.edits) std::visit(editor, edit);
        diagnostics.insert(diagnostics.end(), editor.diagnostics.begin(), editor.diagnostics.end());
        std::erase_if(chart.items, [](const auto& item) { return std::holds_alternative<bms_measure_length_t>(item.value); });
    }
    void append_geometry(bms_chart_content_t& chart) {
        for (const auto& measure : after.measures)
            chart.items.push_back({.id = object_id_t{next++}, .value = bms_measure_length_t{
                .measure = measure.id, .length = measure.definition.length, .meter = measure.definition.meter}});
    }
    void append_new_events(bms_chart_content_t& chart) {
        existing_event_normalizer_t normalizer{owner, before, after};
        for (const auto& [id, event] : native_event_map(after)) {
            if (find_native_event(before, id)) continue;
            const auto content = normalizer.native(event);
            if (!content) { diagnostics.push_back(error(diagnostic_code_t::invalid_measure,
                "New structural content requires exact positions.")); return; }
            chart.items.push_back({.id = object_id_t{next++}, .value = *content});
        }
    }
    auto build() -> result_t<bms_prepared_program_t, diagnostic_list_t> {
        for (auto& node : nodes)
            if (auto* chart = std::get_if<bms_chart_content_t>(&node.value)) update_chart(node, *chart);
        if (!diagnostics.empty()) return std::unexpected(std::move(diagnostics));
        const auto root_chart = *owner.prepared().program.target_chart(owner.prepared().program.root());
        auto& chart = std::get<bms_chart_content_t>(std::ranges::find(nodes, root_chart, &bms_program_node_t::id)->value);
        append_geometry(chart);
        append_new_events(chart);
        if (!diagnostics.empty()) return std::unexpected(std::move(diagnostics));
        auto program = bms_program_t::from(std::move(nodes));
        if (!program) return std::unexpected(diagnostic_list_t{error(diagnostic_code_t::invalid_id, program.error().message)});
        auto prepared = owner.prepared();
        prepared.program = std::move(*program);
        prepared.measures.clear();
        for (const auto& measure : after.measures) prepared.measures.push_back(measure.id);
        prepared.settings.divisions_per_standard_measure = after.divisions_per_standard_measure;
        if (!after.lane_layouts.empty()) {
            prepared.lane_profile = lane_input_profile_t{after.lane_layouts.front().layout, after.input_profile};
            if (!prepared.settings.layout_segment) prepared.settings.layout_segment = object_id_t{next++};
        }
        return prepared;
    }
};
}

namespace {
struct lane_program_mapper_t {
    std::unordered_map<std::uint64_t, lane_id_t> mapping;
    bool valid{true};
    void lane(lane_id_t& id) {
        const auto found = mapping.find(id.value);
        if (found == mapping.end() || !found->second) { valid = false; return; }
        id = found->second;
    }
    auto content(bms_content_value_t value) -> bms_content_value_t {
        return std::visit([&](auto item) -> bms_content_value_t {
            using value_t = std::remove_cvref_t<decltype(item)>;
            if constexpr (std::same_as<value_t, bms_native_content_t>) {
                auto payload = item.payload();
                std::visit([&](auto& event) {
                    using event_t = std::remove_cvref_t<decltype(event)>;
                    if constexpr (std::same_as<event_t, note_t>) lane(event.lane);
                    else if constexpr (std::same_as<event_t, sound_channel_t>)
                        for (auto& point : event.events) lane(point.lane);
                }, payload);
                return *bms_native_content_t::from(std::move(payload),
                    {item.positions().begin(), item.positions().end()}, item.resource_reference());
            } else if constexpr (requires { item.lane; }) lane(item.lane);
            return item;
        }, std::move(value));
    }
};
}

auto document_command_transaction_t::stage_bms_lane_profile(chart_id_t difficulty, lane_input_profile_t profile,
    std::span<const lane_migration_mapping_t> mapping) -> attempt_t<document_edit_effect_t> {
    return stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        const auto* owner = document.find_bms_program(difficulty);
        if (!owner) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped lane profile has no owner.")}};
        auto prepared = owner->prepared();
        lane_program_mapper_t mapper;
        for (const auto& entry : mapping) mapper.mapping.emplace(entry.source_lane.value, entry.target_lane.value_or(lane_id_t{}));
        auto nodes = std::vector<bms_program_node_t>(prepared.program.nodes().begin(), prepared.program.nodes().end());
        for (auto& node : nodes)
            if (auto* chart = std::get_if<bms_chart_content_t>(&node.value))
                for (auto& item : chart->items) item.value = mapper.content(std::move(item.value));
        if (!mapper.valid) return {.diagnostics = {error(diagnostic_code_t::unsupported_feature,
            "Scoped lane migration requires a complete mapping for every active and inactive authored lane; dropping inactive content is unavailable.")}};
        auto program = bms_program_t::from(std::move(nodes));
        if (!program) return {.diagnostics = {error(diagnostic_code_t::invalid_id, program.error().message)}};
        prepared.program = std::move(*program);
        prepared.lane_profile = std::move(profile);
        const auto destination = std::ranges::find_if(document.prepared_bms_programs,
            [&](const auto& entry) { return entry.get() == owner; });
        *destination = std::shared_ptr<const bms_program_document_t>(new bms_program_document_t(
            difficulty, std::move(prepared), owner->evaluation()));
        return {.value = document_edit_effect_t{.changed = true, .command_label = "Edit scoped lane profile"}};
    });
}

auto document_command_transaction_t::stage_bms_structure_edit(chart_id_t difficulty,
    const std::function<attempt_t<document_edit_effect_t>(chart_document_t&)>& edit)
    -> attempt_t<document_edit_effect_t> {
    return stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        const auto* owner = document.find_bms_program(difficulty);
        if (!owner || owner->prepared().program.has_control_flow()) return {.diagnostics = {error(
            diagnostic_code_t::unsupported_feature, "Shared structure edits require an unconditional scoped difficulty.")}};
        const auto* before = owner->read_chart();
        if (!before) return {.diagnostics = {error(diagnostic_code_t::invalid_measure, "Scoped structure has no complete playback view.")}};
        auto after = *before;
        const auto result = edit(after);
        if (!result.value || !result.ok() || !result.value->changed) return result;
        if (after.measures.empty() || after.measures.size() > bms_addressable_measure_count || after.lane_layouts.size() > 1)
            return {.diagnostics = {error(diagnostic_code_t::unsupported_feature, "Scoped structure requires one static layout and a bounded measure extent.")}};
        const auto first = next_bms_program_item_id(after.measures.size() * 2 + native_event_map(after).size() + 1);
        if (!first) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped structural identities are exhausted.")}};
        auto next = first->value;
        remap_new_geometry_measures(*before, after, next);
        auto prepared = structure_program_builder_t{.owner = *owner, .before = *before, .after = after,
            .next = next, .nodes = {owner->prepared().program.nodes().begin(), owner->prepared().program.nodes().end()}}.build();
        if (!prepared) return {.diagnostics = std::move(prepared.error())};
        const auto destination = std::ranges::find_if(document.prepared_bms_programs,
            [&](const auto& entry) { return entry.get() == owner; });
        *destination = std::shared_ptr<const bms_program_document_t>(new bms_program_document_t(
            difficulty, std::move(*prepared), owner->evaluation()));
        return result;
    });
}

auto document_command_transaction_t::stage_bms_native_objects(chart_id_t difficulty,
    std::span<const object_id_t> objects, bms_existing_edit_kind_t kind,
    const std::function<attempt_t<document_edit_effect_t>(chart_document_t&)>& edit)
    -> attempt_t<document_edit_effect_t> {
    return stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        if (objects.empty()) return {.value = document_edit_effect_t{}};
        const auto target = resolve_object_target(document, difficulty, objects, bms_target_);
        if (!target) return {.diagnostics = {error(diagnostic_code_t::invalid_id, target.error().message)}};
        const auto source_owner = *std::ranges::find_if(document.prepared_bms_programs,
            [&](const auto& owner) { return owner->difficulty() == difficulty; });
        const auto* program = source_owner.get();
        const auto* original = target->authoring ? program->read_authoring_chart(target->chart) : program->read_chart();
        if (!original) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped edit has no playable source.")}};
        auto chart = *original;
        const auto staged = edit(chart);
        if (!staged.value || !staged.ok() || !staged.value->changed) return staged;
        const auto extent = stage_scoped_extent(*this, difficulty, *original, chart);
        if (!extent.value || !extent.ok()) return extent;
        native_batch_builder_t builder{.program = *program, .original = *original, .edited = chart,
            .batch = {.target = *target, .label = staged.value->command_label}};
        builder.edit_existing(objects, kind);
        if (!builder.diagnostics.empty()) return {.diagnostics = std::move(builder.diagnostics)};
        return stage(builder.batch);
    });
}

auto document_command_transaction_t::stage_bms_layer_settings(chart_id_t difficulty,
    const std::function<attempt_t<document_edit_effect_t>(chart_document_t&)>& edit)
    -> attempt_t<document_edit_effect_t> {
    return stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        const auto* source = document.find_bms_program(difficulty);
        const auto* view = source ? read_chart(difficulty) : nullptr;
        if (!view) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped layer settings have no source.")}};
        auto chart = *view;
        chart.sound_layer_defaults = source->prepared().sound_layer_defaults;
        const auto staged = edit(chart);
        if (!staged.value || !staged.ok() || !staged.value->changed) return staged;
        std::set<layer_id_t> existing;
        for (const auto& layer : source->prepared().semantic_layers) existing.insert(layer.id);
        const auto count = std::ranges::count_if(chart.semantic_layers,
            [&](const auto& layer) { return !existing.contains(layer.id); });
        const auto first = count ? next_bms_program_item_id(static_cast<std::size_t>(count)) : std::nullopt;
        if (count && !first) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped layer identities are exhausted.")}};
        assign_new_layer_ids(chart, existing, first.value_or(object_id_t{}));
        if (chart.semantic_layers == source->prepared().semantic_layers &&
            chart.sound_layer_defaults == source->prepared().sound_layer_defaults) return {.value = document_edit_effect_t{}};
        auto prepared = source->prepared();
        prepared.semantic_layers = std::move(chart.semantic_layers);
        prepared.sound_layer_defaults = std::move(chart.sound_layer_defaults);
        const auto destination = std::ranges::find_if(document.prepared_bms_programs,
            [&](const auto& owner) { return owner.get() == source; });
        *destination = std::shared_ptr<const bms_program_document_t>(new bms_program_document_t(
            difficulty, std::move(prepared), source->evaluation()));
        return staged;
    });
}

auto document_command_transaction_t::stage_bms_bgm_column(chart_id_t difficulty) -> attempt_t<object_id_t> {
    const auto id = next_bms_program_item_id();
    if (!id) {
        (void)reject(diagnostic_code_t::invalid_id, "Scoped BGM column identities are exhausted.");
        return {.diagnostics = outcome().diagnostics};
    }
    const auto staged = stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        const auto* source = document.find_bms_program(difficulty);
        if (!source || source->prepared().bgm_columns.size() >= 1'000'000)
            return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped BGM columns are unavailable or full.")}};
        auto prepared = source->prepared();
        std::size_t order{};
        for (const auto& column : prepared.bgm_columns) {
            if (column.source_order == std::numeric_limits<std::size_t>::max())
                return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped BGM column order is exhausted.")}};
            order = std::max(order, column.source_order + 1);
        }
        prepared.bgm_columns.push_back({.id = *id,
            .name = "BGM " + std::to_string(prepared.bgm_columns.size() + 1), .source_order = order});
        prepared.settings.show_empty_bgm_columns = true;
        const auto destination = std::ranges::find_if(document.prepared_bms_programs,
            [&](const auto& owner) { return owner.get() == source; });
        *destination = std::shared_ptr<const bms_program_document_t>(new bms_program_document_t(
            difficulty, std::move(prepared), source->evaluation()));
        return {.value = document_edit_effect_t{.changed = true, .command_label = "Add BGM column"}};
    });
    if (!staged.value || !staged.ok()) return {.diagnostics = staged.diagnostics};
    return {.value = *id};
}

auto document_command_transaction_t::stage_bms_native_events(chart_id_t difficulty,
    std::span<const object_id_t> removals, std::span<const bms_native_event_t> additions,
    std::string label) -> attempt_t<std::vector<object_id_t>> {
    if (!is_ready()) return {.diagnostics = outcome().diagnostics};
    const auto& current = current_document();
    const auto existing = additions.empty() && !removals.empty() ? std::optional{removals.front()} : std::nullopt;
    const auto target = bms_target(difficulty, existing);
    if (!target) {
        (void)reject(diagnostic_code_t::invalid_id, target.error().message);
        return {.diagnostics = outcome().diagnostics};
    }
    bms_program_content_edit_batch_t batch{.target = *target, .label = std::move(label)};
    const auto complete_removals = complete_endpoint_ids(*current.find_bms_program(difficulty), removals, target->authoring ? std::optional{target->chart} : std::nullopt);
    for (const auto id : complete_removals) {
        const auto owner = resolve_native_target(current, difficulty, id, bms_target_);
        if (!owner || owner->chart != target->chart) {
            (void)reject(diagnostic_code_t::invalid_id, "Scoped replacement cannot cross semantic owners.");
            return {.diagnostics = outcome().diagnostics};
        }
        batch.edits.push_back(bms_program_content_delete_t{.owner = {
            .scope = owner->scope, .chart = owner->chart, .item = id}});
    }
    const auto* geometry = read_chart(difficulty);
    const auto first = additions.empty() ? std::optional<object_id_t>{}
        : next_bms_program_item_id(additions.size());
    if (!geometry || (!additions.empty() && !first)) {
        (void)reject(diagnostic_code_t::invalid_id, "Scoped insertion geometry or identity is unavailable.");
        return {.diagnostics = outcome().diagnostics};
    }
    const auto* program = current.find_bms_program(difficulty);
    existing_event_normalizer_t normalizer{*program, *geometry, *geometry};
    std::vector<object_id_t> created;
    for (const auto& event : additions) {
        const auto native = normalizer.native(event);
        if (!native) {
            (void)reject(diagnostic_code_t::invalid_measure, "Scoped insertion needs an exact measure position.");
            return {.diagnostics = outcome().diagnostics};
        }
        const auto id = object_id_t{first->value + created.size()};
        created.push_back(id);
        batch.edits.push_back(bms_program_content_insert_t{.item = {.id = id, .value = *native}});
    }
    const auto staged = stage(batch);
    if (!staged.value || !staged.ok()) return {.diagnostics = staged.diagnostics};
    return {.value = std::move(created)};
}

auto document_command_transaction_t::bms_target(chart_id_t difficulty,
    std::optional<object_id_t> existing) const -> result_t<bms_program_edit_target_t, error_t<failure_code_t>> {
    return resolve_native_target(current_document(), difficulty, existing, bms_target_);
}

auto document_command_transaction_t::has_bms_program(chart_id_t difficulty) const -> bool {
    return current_document().find_bms_program(difficulty) != nullptr;
}

auto document_command_transaction_t::stage_bms_native_edit(chart_id_t difficulty,
    std::optional<object_id_t> existing,
    const std::function<attempt_t<bms_native_edit_effect_t>(chart_document_t&)>& edit)
    -> attempt_t<document_edit_effect_t> {
    return stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        const auto target = bms_target(difficulty, existing);
        if (!target) return {.diagnostics = {error(diagnostic_code_t::invalid_id, target.error().message)}};
        const auto source_owner = *std::ranges::find_if(document.prepared_bms_programs,
            [&](const auto& owner) { return owner->difficulty() == difficulty; });
        const auto* program = source_owner.get();
        const auto* original = target->authoring ? program->read_authoring_chart(target->chart) : program->read_chart();
        if (!original) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped edit has no playable source.")}};
        auto chart = *original;
        auto staged = edit(chart);
        if (!staged.value || !staged.ok()) return {.diagnostics = staged.diagnostics};
        if (!staged.value->effect.changed) return {.value = document_edit_effect_t{}};
        const auto extent = stage_scoped_extent(*this, difficulty, *original, chart, &staged.value->event);
        if (!extent.value || !extent.ok()) return extent;
        native_batch_builder_t builder{.program = *program, .original = *original, .edited = chart,
            .batch = {.target = *target, .label = staged.value->effect.command_label}};
        builder.edit_event(existing, existing ? std::nullopt : next_bms_program_item_id(), staged.value->event);
        if (!builder.diagnostics.empty()) return {.diagnostics = std::move(builder.diagnostics)};
        return stage(builder.batch);
    });
}

} // namespace wormedit

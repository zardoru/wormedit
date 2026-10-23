#include <wormedit/core/bms_program_edit.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/document_index.h>
#include "lane_occupancy.h"
#include "id_ceiling.h"
#include "bms_native_content_internal.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace wormedit {
namespace {

auto resolve_target(const song_document_t& document, chart_id_t difficulty,
    std::optional<branch_id_t> selected)
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
    const auto projection = owner->build_projection();
    if (!projection.value || !projection.ok())
        return std::unexpected(error_t<failure_code_t>{failure_code_t::materialize_failed,
            "Scoped difficulty cannot be evaluated."});
    if (std::ranges::find(projection.value->active_charts(), *chart) ==
        projection.value->active_charts().end())
        return std::unexpected(error_t<failure_code_t>{failure_code_t::unavailable,
            "Selected Chart is inactive in this evaluation."});
    return bms_program_edit_target_t{.stamp = document_stamp_t::from(document),
        .difficulty = difficulty, .scope = *scope, .chart = *chart};
}

auto resolve_native_target(const song_document_t& document, chart_id_t difficulty,
    std::optional<object_id_t> existing, const std::optional<bms_program_target_key_t>& selection)
    -> result_t<bms_program_edit_target_t, error_t<failure_code_t>> {
    if (existing) return bms_program_edit_target_t::from(document, difficulty, *existing);
    if (selection && selection->difficulty != difficulty)
        return std::unexpected(error_t<failure_code_t>{failure_code_t::conflict,
            "Selected semantic Chart belongs to another difficulty."});
    return bms_program_edit_target_t::from(document, difficulty,
        selection ? std::optional{selection->chart} : std::nullopt);
}

auto maximum_program_id(const song_document_t& document) -> std::uint64_t {
    id_ceiling_t<object_id_t> ceiling;
    for (const auto& owner : document.prepared_bms_programs) {
        if (!owner) continue;
        (void)id_declarations_t<object_id_t>::visit(owner->prepared(), ceiling);
    }
    return ceiling.maximum;
}

auto endpoint_owners(const bms_program_document_t& owner,
    std::span<const bms_program_owner_t> contributors) -> std::vector<bms_program_owner_t> {
    std::vector<bms_program_owner_t> endpoints;
    for (const auto contributor : contributors) {
        const auto* node = owner.prepared().program.find(contributor.chart);
        const auto& items = std::get<bms_chart_content_t>(node->value).items;
        const auto item = std::ranges::find(items, contributor.item, &bms_content_item_t::id);
        if (item != items.end() && std::holds_alternative<bms_hold_endpoint_t>(item->value))
            endpoints.push_back(contributor);
    }
    return endpoints;
}

// Declarations also contribute to a projected hold, but are independently
// editable. Only its authored endpoints determine whether a hold spans owners.
auto cross_owner_endpoints(const bms_program_document_t& owner)
    -> std::set<object_id_t> {
    std::set<object_id_t> result;
    const auto projection = owner.build_projection();
    if (!projection.value) return result;
    for (const auto& measure : projection.value->song().charts.front().measures) {
        for (const auto& note : measure.notes) {
            if (!note.end) continue;
            const auto endpoints = endpoint_owners(owner, projection.value->owners(note.id.value));
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

auto event_resource(const bms_native_event_t& event) -> std::optional<resource_id_t> {
    return std::visit([](const auto& value) -> std::optional<resource_id_t> {
        if constexpr (requires { value.sound; }) return value.sound;
        else if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, bga_event_t>) return value.resource;
        else return std::nullopt;
    }, event);
}

auto content_reference(const bms_content_value_t& content) -> std::optional<bms_native_resource_reference_t> {
    return std::visit([](const auto& value) -> std::optional<bms_native_resource_reference_t> {
        using value_type = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<value_type, bms_native_content_t>) return value.resource_reference();
        else if constexpr (std::same_as<value_type, bms_bga_content_t>)
            return bms_native_resource_reference_t{.name_space = bms_resource_namespace_t::bmp, .identifier = value.bmp};
        else if constexpr (std::same_as<value_type, bms_hold_endpoint_t>)
            return bms_native_resource_reference_t{.identifier = value.wav};
        else if constexpr (requires { value.wav; }) {
            if (value.wav) return bms_native_resource_reference_t{.identifier = *value.wav};
            return std::nullopt;
        } else return std::nullopt;
    }, content);
}

struct existing_event_normalizer_t {
    const bms_program_document_t& program;
    const chart_document_t& original;
    const chart_document_t& geometry;

    auto identifier(resource_id_t resource, bms_resource_namespace_t name_space) const -> std::optional<std::string> {
        const auto* entry = program.read_resource(resource);
        if (!entry) return std::nullopt;
        const auto name = name_space == bms_resource_namespace_t::wav ? "WAV" : "BMP";
        for (const auto& identity : entry->target_identities)
            if (identity.family == format_family_t::bms && identity.namespace_name == name) return identity.identifier;
        return std::nullopt;
    }
    auto fallback(const bms_content_value_t& before, const bms_native_event_t& after,
        const std::optional<bms_native_event_t>& previous) const -> std::optional<bms_content_value_t> {
        auto reference = content_reference(before);
        if (!previous || event_resource(*previous) != event_resource(after)) reference.reset();
        const auto value = bms_native_content_t::from(after, geometry, std::move(reference));
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
            if (after.sound && !value.wav) return std::nullopt;
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
        auto value = before;
        value.position = *position;
        value.semantic_layers = after.semantic_layers;
        if (old->sound != after.sound) {
            value.wav = after.sound ? identifier(*after.sound, bms_resource_namespace_t::wav) : std::nullopt;
            if (after.sound && !value.wav) return std::nullopt;
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
            if (!reference) return std::nullopt;
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

auto complete_endpoint_ids(const bms_program_document_t& program, std::span<const object_id_t> ids)
    -> std::set<object_id_t> {
    std::set<object_id_t> result(ids.begin(), ids.end());
    const auto projection = program.build_projection();
    if (!projection.value) return result;
    for (const auto id : ids) {
        const auto endpoints = endpoint_owners(program, projection.value->owners(id.value));
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
    std::span<const object_id_t> objects) -> result_t<bms_program_edit_target_t, error_t<failure_code_t>> {
    const auto target = bms_program_edit_target_t::from(document, difficulty, objects.front());
    if (!target) return target;
    std::set<object_id_t> unique;
    for (const auto id : objects) {
        const auto owner = bms_program_edit_target_t::from(document, difficulty, id);
        if (!unique.insert(id).second || !owner || owner->chart != target->chart)
            return std::unexpected(error_t<failure_code_t>{failure_code_t::conflict,
                "Scoped object edits require distinct objects with one complete semantic owner."});
    }
    return target;
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
        const auto projection = program.build_projection();
        if (!projection.value) return nullptr;
        const auto endpoints = endpoint_owners(program, projection.value->owners(id.value));
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
        if (previous && *previous == event) return;
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
        const auto value = bms_native_content_t::from(event, edited);
        if (!value) diagnostics.push_back(error(diagnostic_code_t::invalid_measure,
            "Scoped insertion requires exact measure positions."));
        else batch.edits.push_back(bms_program_content_insert_t{.item = {.id = id, .value = *value}});
    }
    void complete_endpoints() {
        std::vector<object_id_t> affected;
        for (const auto& operation : batch.edits)
            std::visit([&](const auto& edit) {
                if constexpr (requires { edit.owner; }) affected.push_back(edit.owner.item);
            }, operation);
        const std::set requested(affected.begin(), affected.end());
        for (const auto id : complete_endpoint_ids(program, affected))
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
    for (const auto& node : owner.prepared().program.nodes()) {
        auto values = selector_values(owner.prepared().program, node, maximum);
        if (!values) return std::unexpected(values.error());
        if (values->empty()) continue;
        if (values->size() > maximum / candidates.size())
            return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded,
                "Scoped edit exceeds its bounded evaluation budget."});
        std::vector<bms_program_evaluation_t> expanded;
        for (const auto& candidate : candidates) {
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
// interpretation. Content edits cannot change control flow or selector settings.
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
            const auto endpoints = endpoint_owners(observed, projection.owners(note.id.value));
            if (endpoints.size() != 2 || endpoints.front().chart == endpoints.back().chart) continue;
            for (const auto endpoint : endpoints)
                if (!has_unchanged_item(&before, proposed, endpoint.chart, endpoint.item)) return true;
        }
    }
    return false;
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
    auto validation = validate_document(projection.value->song());
    diagnostics.insert(diagnostics.end(), validation.begin(), validation.end());
    return diagnostics;
}
} // namespace

auto bms_program_document_t::read_chart() const -> const chart_document_t* {
    if (!view_) {
        const auto projection = build_projection();
        if (!projection.value || !projection.ok()) return nullptr;
        view_ = projection.value->song();
        view_->charts.front().id = difficulty_;
    }
    return &view_->charts.front();
}

auto bms_program_document_t::read_resource(resource_id_t id) const -> const resource_entry_t* {
    if (!read_chart()) return nullptr;
    const auto found = std::ranges::find(view_->resources, id, &resource_entry_t::id);
    return found == view_->resources.end() ? nullptr : &*found;
}

auto bms_program_document_t::build_projection() const
    -> attempt_t<bms_program_projection_t, bms_program_projection_diagnostics_t> {
    return bms_program_projection_t::from(prepared_, evaluation_);
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

auto bms_program_edit_target_t::validate(const song_document_t& document) const
    -> result_t<const bms_program_document_t*, error_t<failure_code_t>> {
    if (!stamp.is_current(document))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::stale_generation,
            stale_action_message("Scoped edit")});
    const auto current = resolve_target(document, difficulty, chart);
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
        const auto cross_owner = cross_owner_endpoints(**owner);
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
        auto prepared = bms_prepared_program_t{.program = std::move(*program),
            .measures = source.measures, .lane_profile = source.lane_profile,
            .bgm_columns = source.bgm_columns, .resources = source.resources,
            .audio_assets = source.audio_assets, .visual_assets = source.visual_assets,
            .semantic_layers = source.semantic_layers, .sound_layer_defaults = source.sound_layer_defaults};
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
        auto validation = validate_document(projection.value->song());
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

auto document_command_transaction_t::stage_bms_native_objects(chart_id_t difficulty,
    std::span<const object_id_t> objects, bms_existing_edit_kind_t kind,
    const std::function<attempt_t<document_edit_effect_t>(chart_document_t&)>& edit)
    -> attempt_t<document_edit_effect_t> {
    return stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        if (objects.empty()) return {.value = document_edit_effect_t{}};
        const auto target = resolve_object_target(document, difficulty, objects);
        if (!target) return {.diagnostics = {error(diagnostic_code_t::invalid_id, target.error().message)}};
        const auto* program = document.find_bms_program(difficulty);
        const auto* original = program->read_chart();
        if (!original) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped edit has no playable source.")}};
        auto chart = *original;
        const auto staged = edit(chart);
        if (!staged.value || !staged.ok() || !staged.value->changed) return staged;
        native_batch_builder_t builder{.program = *program, .original = *original, .edited = chart,
            .batch = {.target = *target, .label = staged.value->command_label}};
        for (const auto id : objects) {
            if (kind == bms_existing_edit_kind_t::erase) builder.erase(id);
            else {
                const auto event = find_native_event(chart, id);
                if (!event) return {.diagnostics = {error(diagnostic_code_t::unsupported_feature,
                    "Scoped operation did not produce its named event.", id)}};
                builder.update(id, *event);
            }
        }
        builder.complete_endpoints();
        if (!builder.diagnostics.empty()) return {.diagnostics = std::move(builder.diagnostics)};
        return stage(builder.batch);
    });
}

auto document_command_transaction_t::stage_bms_layer_settings(chart_id_t difficulty,
    const std::function<attempt_t<document_edit_effect_t>(chart_document_t&)>& edit)
    -> attempt_t<document_edit_effect_t> {
    return stage([&](song_document_t& document) -> attempt_t<document_edit_effect_t> {
        const auto* source = document.find_bms_program(difficulty);
        const auto* view = source ? source->read_chart() : nullptr;
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
    const auto complete_removals = complete_endpoint_ids(*current.find_bms_program(difficulty), removals);
    for (const auto id : complete_removals) {
        const auto owner = bms_program_edit_target_t::from(current, difficulty, id);
        if (!owner || owner->chart != target->chart) {
            (void)reject(diagnostic_code_t::invalid_id, "Scoped replacement cannot cross semantic owners.");
            return {.diagnostics = outcome().diagnostics};
        }
        batch.edits.push_back(bms_program_content_delete_t{.owner = {
            .scope = owner->scope, .chart = owner->chart, .item = id}});
    }
    const auto* geometry = current.read_chart(difficulty);
    const auto first = additions.empty() ? std::optional<object_id_t>{}
        : next_bms_program_item_id(additions.size());
    if (!geometry || (!additions.empty() && !first)) {
        (void)reject(diagnostic_code_t::invalid_id, "Scoped insertion geometry or identity is unavailable.");
        return {.diagnostics = outcome().diagnostics};
    }
    std::vector<object_id_t> created;
    for (const auto& event : additions) {
        const auto native = bms_native_content_t::from(event, *geometry);
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
        const auto* program = document.find_bms_program(difficulty);
        const auto* original = program->read_chart();
        if (!original) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped edit has no playable source.")}};
        auto chart = *original;
        const auto staged = edit(chart);
        if (!staged.value || !staged.ok()) return {.diagnostics = staged.diagnostics};
        if (!staged.value->effect.changed) return {.value = document_edit_effect_t{}};
        native_batch_builder_t builder{.program = *program, .original = *original, .edited = chart,
            .batch = {.target = *target, .label = staged.value->effect.command_label}};
        if (existing) {
            if (staged.value->event) builder.update(*existing, *staged.value->event);
            else builder.erase(*existing);
            builder.complete_endpoints();
        } else {
            const auto id = next_bms_program_item_id();
            if (!id || !staged.value->event) return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                "Scoped insertion has no event or available identity.")}};
            builder.insert(*id, *staged.value->event);
        }
        if (!builder.diagnostics.empty()) return {.diagnostics = std::move(builder.diagnostics)};
        return stage(builder.batch);
    });
}

} // namespace wormedit

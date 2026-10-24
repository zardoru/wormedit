#include <wormedit/core/bms_program.h>
#include "checked_math.h"
#include "bms_native_content_internal.h"
#include <wormedit/core/rational.h>

#include <map>
#include <set>
#include <limits>

namespace wormedit {
namespace {
struct contribution_t {
    const bms_content_item_t* item;
    bms_program_owner_t owner;
    std::size_t order{};
    [[nodiscard]] auto provenance() const -> source_provenance_t {
        auto value = item->provenance;
        value.source_order = order + 1;
        return value;
    }
};
using owner_map_t = std::unordered_map<std::uint64_t, std::vector<bms_program_owner_t>>;
struct projection_builder_t {
    const bms_prepared_program_t& prepared;
    bms_program_projection_limits_t limits;
    bms_program_projection_diagnostics_t diagnostics;
    bool failed{};
    std::vector<contribution_t> active;
    std::vector<branch_id_t> charts;
    std::vector<branch_selection_t> selections;
    std::map<std::uint64_t, std::int64_t> choices;
    std::set<std::uint64_t> visited;
    std::optional<std::int64_t> pending_random, pending_switch;
    song_document_t song;
    owner_map_t owners;
    std::map<std::uint64_t, std::size_t> measures;
    std::map<std::pair<bms_resource_namespace_t, std::string>, contribution_t> resources;
    std::map<std::pair<bms_timing_definition_kind_t, std::string>, contribution_t> definitions;
    std::map<std::string, std::pair<asset_id_t, resource_id_t>> missing_bmp_ids;
    std::map<std::string, resource_id_t> missing_bmps;
    std::map<std::uint64_t, const resource_entry_t*> catalog;
    std::map<std::uint64_t, std::vector<contribution_t>> endpoints;
    std::uint64_t next_id{};
    std::set<std::uint64_t> identities;
    std::map<std::uint64_t, bms_resource_namespace_t> assets;
    std::set<lane_id_t> lanes;
    std::map<object_id_t, object_id_t> authoring_pairs;
    bool warned_authoring_endpoint{};
    std::set<std::pair<branch_id_t, std::optional<branch_id_t>>> skip_points;

    auto chart() -> chart_document_t& { return song.charts.front(); }
    void diagnose(failure_code_t code, std::string message, branch_id_t node = {},
        object_id_t item = {}, diagnostic_severity_t severity = diagnostic_severity_t::error);
    auto prepare() -> bool;
    auto index_inputs() -> bool;
    auto index_program(std::size_t& count) -> bool;
    auto index_pools(std::size_t& count) -> bool;
    auto index_layer_defaults(std::size_t& count) -> bool;
    auto index_native(const bms_content_item_t& item, std::size_t& count) -> bool;
    auto observe_id(std::uint64_t id) -> bool;
    void evaluate(const bms_program_node_t& node, std::optional<std::int64_t> random,
        bool* stopped, const bms_case_scope_t* owning_case, std::size_t depth);
    void evaluate_children(const bms_program_node_t& node, std::optional<std::int64_t> random,
        bool* stopped, const bms_case_scope_t* owning_case, std::size_t depth);
    void evaluate_switch(const bms_program_node_t& node, std::optional<std::int64_t> random,
        std::size_t depth);
    auto choice(const bms_program_node_t& node, std::optional<std::int64_t>& pending)
        -> std::optional<std::int64_t>;
    void skip_at(branch_id_t scope, std::optional<branch_id_t> after, bool* stopped,
        const bms_case_scope_t* owning_case);
    void evaluate_playback(const bms_program_evaluation_t& evaluation);
    void evaluate_authoring(branch_id_t chart);
    void collect_declarations();
    void declaration(contribution_t entry, const bms_resource_declaration_t& value);
    void declaration(contribution_t entry, const bms_timing_definition_t& value);
    void declaration(contribution_t entry, const bms_measure_length_t& value);
    void declaration(contribution_t entry, const bms_metadata_declaration_t& value);
    template<class T> void declaration(contribution_t, const T&) {}
    auto build_geometry() -> bool;
    auto promote(rational_t length, rational_t fraction) -> bool;
    auto position(bms_program_position_t value) -> std::optional<local_position_t>;
    auto is_lane(lane_id_t lane, contribution_t entry) -> bool;
    auto sound(const std::string& identifier, contribution_t entry) -> std::optional<resource_id_t>;
    auto visual(const std::string& identifier, contribution_t entry) -> resource_id_t;
    void add_owners(std::uint64_t id, contribution_t entry);
    void add_resource_owner(std::uint64_t id, bms_resource_namespace_t ns, const std::string& identifier);
    void emit(contribution_t entry, const bms_native_content_t& value);
    void emit(contribution_t entry, const bms_note_content_t& value);
    void emit(contribution_t entry, const bms_hold_endpoint_t& value);
    void emit(contribution_t entry, const bms_bga_content_t& value);
    void emit(contribution_t entry, const bms_direct_bpm_t& value);
    void emit(contribution_t entry, const bms_timing_definition_t& value);
    void emit(contribution_t entry, const bms_timing_reference_t& value);
    template<class T> void emit(contribution_t, const T&) {}
    void emit_bgm();
    void pair_holds();
    void pair_authoring_holds(const std::vector<contribution_t>& entries);
    void emit_authoring_endpoint(contribution_t entry);
    void mark_authoring_context(branch_id_t target);
    void emit_hold(contribution_t head, contribution_t tail);
};

void projection_builder_t::diagnose(failure_code_t code, std::string message,
    branch_id_t node, object_id_t item, diagnostic_severity_t severity) {
    failed = failed || severity == diagnostic_severity_t::error;
    if (diagnostics.size() < limits.maximum_diagnostics)
        diagnostics.push_back({severity, code, std::move(message), node, item});
    else {
        failed = true;
        if (!diagnostics.empty()) diagnostics.back() = {diagnostic_severity_t::error,
            failure_code_t::capacity_exceeded, "Projection diagnostic limit exceeded.", node, item};
    }
}

auto projection_builder_t::observe_id(std::uint64_t id) -> bool {
    next_id = std::max(next_id, id);
    if (id && identities.insert(id).second) return true;
    diagnose(failure_code_t::invalid_data, "Prepared identities must be nonzero and unambiguous.");
    return false;
}

auto projection_builder_t::index_native(const bms_content_item_t& item, std::size_t& count) -> bool {
    const auto* native = std::get_if<bms_native_content_t>(&item.value);
    if (!native) return true;
    const auto ids = native->declared_ids();
    if (!checked_math::account_size(count, ids.size(), limits.maximum_input_items) ||
        !checked_math::account_size(count, native->positions().size(), limits.maximum_input_items)) return false;
    for (const auto id : ids) if (!observe_id(id.value)) return false;
    return true;
}

auto projection_builder_t::index_program(std::size_t& count) -> bool {
    for (const auto& node : prepared.program.nodes()) {
        if (!observe_id(node.id.value)) return false;
        if (const auto* arm = std::get_if<bms_case_scope_t>(&node.value)) {
            if (!checked_math::account_size(count, arm->skips.size(), limits.maximum_input_items)) return false;
            for (const auto& skip : arm->skips) skip_points.emplace(skip.scope, skip.after_child);
        }
        const auto* content = std::get_if<bms_chart_content_t>(&node.value);
        if (!content) continue;
        if (!checked_math::account_size(count, content->items.size(), limits.maximum_input_items)) return false;
        for (const auto& item : content->items) {
            if (!observe_id(item.id.value) || !index_native(item, count)) return false;
            if (const auto* bga = std::get_if<bms_bga_content_t>(&item.value)) missing_bmp_ids.try_emplace(bga->bmp);
        }
    }
    return true;
}

auto projection_builder_t::index_layer_defaults(std::size_t& count) -> bool {
    std::set<layer_id_t> layers;
    for (const auto& layer : prepared.semantic_layers) layers.insert(layer.id);
    std::set<resource_id_t> resources_with_defaults;
    for (const auto& defaults : prepared.sound_layer_defaults) {
        const auto resource = catalog.find(defaults.resource.value);
        if (resource == catalog.end() || defaults.layers.empty() || !resources_with_defaults.insert(defaults.resource).second) {
            diagnose(failure_code_t::invalid_data, "Layer defaults require distinct known sounds and nonempty memberships.");
            return false;
        }
        if (!std::ranges::any_of(resource->second->roles, [](resource_role_t role) {
            return role == resource_role_t::sample || role == resource_role_t::bgm;
        })) {
            diagnose(failure_code_t::invalid_data, "Layer defaults require an audio sample resource.");
            return false;
        }
        if (!checked_math::account_size(count, defaults.layers.size(), limits.maximum_input_items)) return false;
        std::set<layer_id_t> memberships;
        for (const auto id : defaults.layers) {
            if (!layers.contains(id) || !memberships.insert(id).second) {
                diagnose(failure_code_t::invalid_data, "Layer defaults require distinct known layer identities.");
                return false;
            }
        }
    }
    return true;
}

auto projection_builder_t::index_pools(std::size_t& count) -> bool {
    const std::array sizes{prepared.measures.size(), prepared.resources.size(), prepared.audio_assets.size(),
        prepared.visual_assets.size(), prepared.bgm_columns.size(), prepared.semantic_layers.size(), prepared.sound_layer_defaults.size()};
    for (const auto size : sizes) if (!checked_math::account_size(count, size, limits.maximum_input_items)) return false;
    for (const auto id : prepared.measures) if (!observe_id(id.value)) return false;
    for (const auto& lane : prepared.bgm_columns) if (!observe_id(lane.id.value)) return false;
    for (const auto& layer : prepared.semantic_layers) if (!observe_id(layer.id.value)) return false;
    for (const auto& resource : prepared.resources) {
        if (!observe_id(resource.id.value)) return false;
        catalog.emplace(resource.id.value, &resource);
    }
    for (const auto& asset : prepared.audio_assets) {
        if (!observe_id(asset.id.value)) return false;
        assets.emplace(asset.id.value, bms_resource_namespace_t::wav);
    }
    for (const auto& asset : prepared.visual_assets) {
        if (!observe_id(asset.id.value)) return false;
        assets.emplace(asset.id.value, bms_resource_namespace_t::bmp);
    }
    return index_layer_defaults(count);
}

auto projection_builder_t::index_inputs() -> bool {
    std::size_t count{};
    if (!index_program(count) || !index_pools(count)) return false;
    if (prepared.lane_profile) {
        const auto& profile = *prepared.lane_profile;
        if (!checked_math::account_size(count, profile.layout.lanes.size(), limits.maximum_input_items) ||
            !checked_math::account_size(count, profile.input.bindings.size(), limits.maximum_input_items)) return false;
        for (const auto& lane : profile.layout.lanes) lanes.insert(lane.id);
    }
    // Reserve derived identities from the complete input, never from an outcome's
    // active subset. Missing BMP slots remain stable as choices change.
    const auto reserve = checked_math::checked_multiply(missing_bmp_ids.size(), 2);
    return reserve && *reserve <= UINT64_MAX - 3 && next_id <= UINT64_MAX - 3 - *reserve;
}

auto projection_builder_t::prepare() -> bool {
    if (prepared.program.nodes().size() > limits.maximum_nodes || !index_inputs()) {
        if (!failed) diagnose(failure_code_t::capacity_exceeded, "Prepared program exceeds projection input or identity limits.");
        return false;
    }
    if (prepared.measures.empty()) {
        diagnose(failure_code_t::invalid_geometry, "Projection requires shared measures.");
        return false;
    }
    song.id = song_id_t{++next_id};
    song.charts.push_back({.id = chart_id_t{++next_id}, .source_family = format_family_t::bms});
    for (const auto id : prepared.measures) {
        if (!id.value || !measures.emplace(id.value, chart().measures.size()).second) {
            diagnose(failure_code_t::invalid_geometry, "Shared measure identities must be nonzero and unique.");
            return false;
        }
        chart().measures.push_back({.id = id});
    }
    const auto layout_id = object_id_t{++next_id};
    if (prepared.lane_profile) {
        chart().lane_layouts.push_back({layout_id, {prepared.measures.front(), 0}, prepared.lane_profile->layout});
        chart().input_profile = prepared.lane_profile->input;
    }
    for (auto& [identifier, ids] : missing_bmp_ids) {
        (void)identifier;
        ids = {asset_id_t{++next_id}, resource_id_t{++next_id}};
    }
    song.audio_assets = prepared.audio_assets;
    song.visual_assets = prepared.visual_assets;
    chart().semantic_layers = prepared.semantic_layers;
    return true;
}

auto projection_builder_t::choice(const bms_program_node_t& node,
    std::optional<std::int64_t>& pending) -> std::optional<std::int64_t> {
    auto selected = pending;
    pending.reset();
    if (const auto found = choices.find(node.id.value); found != choices.end()) {
        selected = found->second;
        visited.insert(node.id.value);
    }
    if (selected) selections.push_back({node.id, *selected});
    return selected;
}

void projection_builder_t::skip_at(branch_id_t scope, std::optional<branch_id_t> after,
    bool* stopped, const bms_case_scope_t* owning_case) {
    if (!stopped || !owning_case) return;
    if (skip_points.contains({scope, after})) *stopped = true;
}

void projection_builder_t::evaluate_children(const bms_program_node_t& node,
    std::optional<std::int64_t> random, bool* stopped, const bms_case_scope_t* owning_case,
    std::size_t depth) {
    bool matched = false;
    skip_at(node.id, std::nullopt, stopped, owning_case);
    for (const auto child : node.children) {
        const auto& child_node = *prepared.program.find(child);
        bool active_arm = true;
        if (const auto* arm = std::get_if<bms_if_scope_t>(&child_node.value)) {
            matched = random && *random == arm->value;
            active_arm = matched;
        } else if (const auto* arm = std::get_if<bms_else_if_scope_t>(&child_node.value)) {
            active_arm = !matched && random && *random == arm->value;
            matched = matched || active_arm;
        } else if (std::holds_alternative<bms_else_scope_t>(child_node.value)) {
            active_arm = !matched && random.has_value();
            matched = true;
        }
        if (active_arm) evaluate(child_node, random, stopped, owning_case, depth + 1);
        skip_at(node.id, child, stopped, owning_case);
        if (failed) return;
    }
}

void projection_builder_t::evaluate_switch(const bms_program_node_t& node,
    std::optional<std::int64_t> random, std::size_t depth) {
    const auto selected = choice(node, pending_switch);
    if (!selected) return;
    std::optional<branch_id_t> first;
    std::optional<branch_id_t> fallback;
    for (auto child : node.children) {
        const auto* arm = std::get_if<bms_case_scope_t>(&prepared.program.find(child)->value);
        if (!arm) continue;
        if (!arm->value) fallback = child;
        else if (!first && arm->value == selected) first = child;
    }
    if (!first) first = fallback;
    if (!first) {
        diagnose(failure_code_t::invalid_request, "SWITCH choice matches no CASE or DEF.", node.id);
        return;
    }
    bool entered = false, stopped = false;
    for (auto child : node.children) {
        entered = entered || child == first;
        if (entered && !stopped) evaluate(*prepared.program.find(child), random, &stopped, nullptr, depth + 1);
        if (failed) return;
    }
}

void projection_builder_t::evaluate(const bms_program_node_t& node,
    std::optional<std::int64_t> random, bool* stopped, const bms_case_scope_t* owning_case,
    std::size_t depth) {
    if (depth > limits.maximum_depth) {
        diagnose(failure_code_t::capacity_exceeded, "Projection nesting limit exceeded.", node.id);
        return;
    }
    if (const auto* setting = std::get_if<bms_set_random_t>(&node.value)) { pending_random = setting->value; return; }
    if (const auto* setting = std::get_if<bms_set_switch_t>(&node.value)) { pending_switch = setting->value; return; }
    if (std::holds_alternative<bms_switch_scope_t>(node.value)) { evaluate_switch(node, random, depth); return; }
    if (const auto* selector = std::get_if<bms_random_scope_t>(&node.value)) {
        random = choice(node, pending_random);
        if (!random) return;
        if (*random < 1 || *random > selector->range) {
            diagnose(failure_code_t::invalid_request, "RANDOM choice is outside the declared range.", node.id);
            return;
        }
    }
    if (const auto* content = std::get_if<bms_chart_content_t>(&node.value)) {
        if (content->items.size() > limits.maximum_active_items - active.size()) {
            diagnose(failure_code_t::capacity_exceeded, "Projection active content limit exceeded.", node.id);
            return;
        }
        charts.push_back(node.id);
        for (const auto& item : content->items)
            active.push_back({.item = &item,
                .owner = {.scope = *prepared.program.parent(node.id), .chart = node.id, .item = item.id},
                .order = active.size()});
        return;
    }
    if (const auto* arm = std::get_if<bms_case_scope_t>(&node.value)) owning_case = arm;
    evaluate_children(node, random, stopped, owning_case, depth);
}

void projection_builder_t::add_owners(std::uint64_t id, contribution_t entry) {
    // Each contribution is emitted once per target; graph/content IDs are unique.
    owners[id].push_back(entry.owner);
}

void projection_builder_t::declaration(contribution_t entry, const bms_resource_declaration_t& value) {
    const auto found = catalog.find(value.resource.value);
    if (found == catalog.end()) {
        diagnose(failure_code_t::not_found, "Resource declaration references a missing catalog entry.", entry.owner.chart, entry.item->id);
        return;
    }
    if (!resources.emplace(std::pair{value.name_space, value.identifier}, entry).second) {
        diagnose(failure_code_t::conflict, "Duplicate resource declaration; the first active value wins.",
            entry.owner.chart, entry.item->id, diagnostic_severity_t::warning);
        return;
    }
    const auto asset = assets.find(found->second->asset.value);
    if (asset == assets.end() || asset->second != value.name_space || owners.contains(value.resource.value)) {
        diagnose(failure_code_t::invalid_data, "Resource declaration has a missing/wrong asset or ambiguous catalog owner.", entry.owner.chart, entry.item->id);
        return;
    }
    auto resource = *found->second;
    resource.target_identities = {{format_family_t::bms, value.identifier,
        value.name_space == bms_resource_namespace_t::wav ? "WAV" : "BMP"}};
    song.resources.push_back(std::move(resource));
    add_owners(value.resource.value, entry);
}

void projection_builder_t::declaration(contribution_t entry, const bms_timing_definition_t& value) {
    if (!value.value.denominator || (value.kind != bms_timing_definition_kind_t::scroll && value.value.numerator <= 0)) {
        diagnose(failure_code_t::invalid_data, "Invalid timing definition.", entry.owner.chart, entry.item->id);
        return;
    }
    definitions.insert_or_assign({value.kind, value.identifier.value_or("")}, entry);
}

void projection_builder_t::declaration(contribution_t entry, const bms_measure_length_t& value) {
    const auto found = measures.find(value.measure.value);
    if (found == measures.end() || !value.length.numerator || !value.length.denominator) {
        diagnose(failure_code_t::invalid_geometry, "Invalid measure-length declaration.", entry.owner.chart, entry.item->id);
        return;
    }
    auto& measure = chart().measures[found->second];
    measure.definition = {reduce_fraction(value.length), std::nullopt};
    owners.erase(value.measure.value);
    add_owners(value.measure.value, entry);
}

void projection_builder_t::declaration(contribution_t entry, const bms_metadata_declaration_t& value) {
    add_owners(entry.item->id.value, entry);
    if (value.field == bms_metadata_field_t::offset) {
        const auto* offset = std::get_if<signed_rational_t>(&value.value);
        if (!offset || !offset->denominator) diagnose(failure_code_t::invalid_data, "Invalid OFFSET declaration.", entry.owner.chart, entry.item->id);
        else chart().beat_zero_audio_offset_seconds = *offset;
        return;
    }
    if (value.field == bms_metadata_field_t::player) {
        const auto* player = std::get_if<signed_rational_t>(&value.value);
        if (!player || player->denominator != 1 || player->numerator < 0 ||
            static_cast<std::uint64_t>(player->numerator) > std::numeric_limits<unsigned int>::max())
            diagnose(failure_code_t::invalid_data, "Invalid PLAYER declaration.", entry.owner.chart, entry.item->id);
        return;
    }
    const auto* text = std::get_if<std::string>(&value.value);
    if (!text) {
        diagnose(failure_code_t::invalid_data, "Metadata text requires a string value.", entry.owner.chart, entry.item->id);
        return;
    }
    if (value.field == bms_metadata_field_t::title) song.metadata.title = *text;
    if (value.field == bms_metadata_field_t::artist) song.metadata.artist = *text;
    const auto definition = std::ranges::find(bms_metadata_definitions, value.field, &bms_metadata_definition_t::field);
    if (definition != bms_metadata_definitions.end() && value.field != bms_metadata_field_t::octave_fp) {
        auto& group = song.metadata.format_group_for(format_family_t::bms);
        group.ordered_fields.emplace_back(definition->command, *text);
        group.ordered_field_source_orders.push_back(entry.order + 1);
    }

}

void projection_builder_t::collect_declarations() {
    // Graph order is the program order, including explicitly reordered/new Charts.
    // Source offsets are provenance only and never decide semantic precedence.
    for (const auto entry : active) std::visit([&](const auto& value) { declaration(entry, value); }, entry.item->value);
    for (const auto& defaults : prepared.sound_layer_defaults)
        if (std::ranges::find(song.resources, defaults.resource, &resource_entry_t::id) != song.resources.end())
            chart().sound_layer_defaults.push_back(defaults);
    chart().metadata.name = song.metadata.title;
    chart().metadata.author = song.metadata.artist;

}

// R * length * fraction must be integral. Cancel across both numerators
// before multiplying denominators; use the existing checked arithmetic owner.
auto projection_builder_t::promote(rational_t length, rational_t fraction) -> bool {
    if (!length.denominator || !fraction.denominator) return false;
    auto a = reduce_fraction(length), b = reduce_fraction(fraction);
    const auto cross_a = std::gcd(a.numerator, b.denominator);
    a.numerator /= cross_a; b.denominator /= cross_a;
    const auto cross_b = std::gcd(b.numerator, a.denominator);
    b.numerator /= cross_b; a.denominator /= cross_b;
    const auto denominator = checked_math::checked_multiply(a.denominator, b.denominator);
    const auto resolution = denominator ? checked_math::checked_lcm(chart().divisions_per_standard_measure, *denominator) : std::nullopt;
    if (!resolution || *resolution > limits.maximum_resolution) return false;
    chart().divisions_per_standard_measure = *resolution;
    return true;
}

struct content_position_t {
    bms_program_position_t position;
    bool allows_chart_end{};
};
auto content_positions(const bms_content_item_t& item) -> std::vector<content_position_t> {
    return std::visit([](const auto& value) -> std::vector<content_position_t> {
        if constexpr (requires { value.position; }) return {{.position = value.position}};
        else if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, bms_native_content_t>) {
            std::vector<content_position_t> result;
            for (std::size_t i = 0; i < value.positions().size(); ++i)
                result.push_back({.position = value.positions()[i], .allows_chart_end = value.allows_chart_end(i)});
            return result;
        } else return {};
    }, item.value);
}

auto projection_builder_t::build_geometry() -> bool {
    for (const auto& measure : chart().measures) {
        if (!promote(measure.definition.length, {1, 1})) {
            diagnose(failure_code_t::capacity_exceeded, "Measure grid exceeds the exact resolution limit.");
            return false;
        }
    }
    for (const auto entry : active) {
        for (const auto source : content_positions(*entry.item)) {
        const auto at = source.position;
        const auto found = measures.find(at.measure.value);
        const bool is_end = source.allows_chart_end && at.measure == prepared.measures.back();
        if (found == measures.end() || !at.fraction.denominator ||
            at.fraction.numerator > at.fraction.denominator ||
            (at.fraction.numerator == at.fraction.denominator && !is_end)) {
            diagnose(failure_code_t::invalid_geometry, "Content position is outside a shared measure.", entry.owner.chart, entry.item->id);
            return false;
        }
        if (!promote(chart().measures[found->second].definition.length, at.fraction)) {
            diagnose(failure_code_t::capacity_exceeded, "Content grid exceeds the exact resolution limit.", entry.owner.chart, entry.item->id);
            return false;
        }
    }
        }
    for (const auto& measure : chart().measures) {
        if (!measure_capacity_t::from(chart().divisions_per_standard_measure, measure.definition)) {
            diagnose(failure_code_t::invalid_geometry, "Measure capacity cannot be represented exactly.");
            return false;
        }
    }
    return true;
}

auto projection_builder_t::position(bms_program_position_t value) -> std::optional<local_position_t> {
    const auto found = measures.find(value.measure.value);
    if (found == measures.end()) return std::nullopt;
    const auto capacity = measure_capacity_t::from(chart().divisions_per_standard_measure, chart().measures[found->second].definition);
    if (!capacity) return std::nullopt;
    // Geometry already proves exact representability; divide before multiplying.
    const auto fraction = reduce_fraction(value.fraction);
    if (capacity->row_capacity % fraction.denominator != 0) return std::nullopt;
    return local_position_t{value.measure, capacity->row_capacity / fraction.denominator * fraction.numerator};
}

auto projection_builder_t::is_lane(lane_id_t lane, contribution_t entry) -> bool {
    if (lanes.contains(lane)) return true;
    diagnose(failure_code_t::not_found, "Playable content references an unknown lane.", entry.owner.chart, entry.item->id);
    return false;
}

void projection_builder_t::add_resource_owner(std::uint64_t id, bms_resource_namespace_t ns, const std::string& identifier) {
    if (const auto found = resources.find({ns, identifier}); found != resources.end()) add_owners(id, found->second);
}

auto projection_builder_t::sound(const std::string& identifier, contribution_t entry) -> std::optional<resource_id_t> {
    if (const auto found = resources.find({bms_resource_namespace_t::wav, identifier}); found != resources.end())
        return std::get<bms_resource_declaration_t>(found->second.item->value).resource;
    diagnose(failure_code_t::not_found, "Undeclared WAV reference remains unbound.", entry.owner.chart, entry.item->id, diagnostic_severity_t::warning);
    return std::nullopt;
}

auto projection_builder_t::visual(const std::string& identifier, contribution_t entry) -> resource_id_t {
    if (const auto found = resources.find({bms_resource_namespace_t::bmp, identifier}); found != resources.end())
        return std::get<bms_resource_declaration_t>(found->second.item->value).resource;
    if (const auto found = missing_bmps.find(identifier); found != missing_bmps.end()) {
        add_owners(found->second.value, entry);
        return found->second;
    }
    const auto [asset, resource] = missing_bmp_ids.at(identifier);
    song.visual_assets.push_back({.id = asset, .source_path = {}});
    song.resources.push_back({.id = resource, .name = "Missing BMS BMP " + identifier, .asset = asset,
        .roles = {resource_role_t::bga}, .target_identities = {{format_family_t::bms, identifier, "BMP"}},
        .missing = true, .provenance = entry.provenance()});
    missing_bmps.emplace(identifier, resource);
    add_owners(resource.value, entry);
    diagnose(failure_code_t::not_found, "Undeclared BMP reference uses a missing visual placeholder.",
        entry.owner.chart, entry.item->id, diagnostic_severity_t::warning);
    return resource;
}

void projection_builder_t::emit(contribution_t entry, const bms_native_content_t& value) {
    const auto event = value.build_event(entry.item->id, chart());
    if (!event) {
        diagnose(failure_code_t::invalid_geometry, "Native content cannot use the exact projected grid.", entry.owner.chart, entry.item->id);
        return;
    }
    std::visit([&](auto native) {
        using value_type = std::remove_cvref_t<decltype(native)>;
        if constexpr (requires { native.provenance; }) native.provenance = entry.provenance();
        if (const auto& reference = value.resource_reference()) {
            if constexpr (requires { native.sound; }) native.sound = sound(reference->identifier, entry);
            else if constexpr (std::same_as<value_type, bga_event_t>) native.resource = visual(reference->identifier, entry);
            add_resource_owner(entry.item->id.value, reference->name_space, reference->identifier);
        }
        if constexpr (std::same_as<value_type, note_t>) {
            if (!is_lane(native.lane, entry)) return;
            chart().measures[measures.at(native.position.measure.value)].notes.push_back(std::move(native));
        } else if constexpr (std::same_as<value_type, bgm_event_t>) {
            const auto column = std::ranges::find(prepared.bgm_columns, native.source_lane, &bgm_source_lane_t::id);
            if (column != prepared.bgm_columns.end() &&
                std::ranges::find(chart().bgm_source_lanes, column->id, &bgm_source_lane_t::id) == chart().bgm_source_lanes.end())
                chart().bgm_source_lanes.push_back(*column);
            chart().measures[measures.at(native.position.measure.value)].bgm_events.push_back(std::move(native));
        } else native_events<value_type>(chart()).push_back(std::move(native));
    }, *event);
    add_owners(entry.item->id.value, entry);
    for (const auto id : value.declared_ids()) add_owners(id.value, entry);
}

void projection_builder_t::emit(contribution_t entry, const bms_note_content_t& value) {
    if (!is_lane(value.lane, entry)) return;
    const auto at = position(value.position);
    if (!at) return;
    if (value.kind == note_kind_t::hold) {
        diagnose(failure_code_t::invalid_data, "Holds require paired authored endpoints.", entry.owner.chart, entry.item->id);
        return;
    }
    const auto binding = value.wav ? sound(*value.wav, entry) : std::nullopt;
    chart().measures[measures.at(at->measure.value)].notes.push_back({.id = entry.item->id,
        .position = *at, .lane = value.lane, .kind = value.kind, .sound = binding,
        .semantic_layers = value.attributes.semantic_layers, .label = value.attributes.label,
        .provenance = entry.provenance(), .osu_hitsample = value.attributes.osu_hitsample});
    add_owners(entry.item->id.value, entry);
    if (value.wav) add_resource_owner(entry.item->id.value, bms_resource_namespace_t::wav, *value.wav);
}
void projection_builder_t::emit(contribution_t entry, const bms_hold_endpoint_t& value) {
    if (is_lane(value.lane, entry)) endpoints[value.lane.value].push_back(entry);
}
void projection_builder_t::emit(contribution_t entry, const bms_bga_content_t& value) {
    const auto at = position(value.position);
    if (!at) return;
    const auto resource = visual(value.bmp, entry);
    chart().bga_events.push_back({entry.item->id, *at, resource, value.kind, entry.provenance()});
    add_owners(entry.item->id.value, entry);
    add_resource_owner(entry.item->id.value, bms_resource_namespace_t::bmp, value.bmp);
}
void projection_builder_t::emit(contribution_t entry, const bms_timing_definition_t& value) {
    if (value.identifier || value.kind != bms_timing_definition_kind_t::bpm) return;
    const auto winner = definitions.find({bms_timing_definition_kind_t::bpm, ""});
    if (winner == definitions.end() || winner->second.item != entry.item) return;
    chart().bpms.push_back({entry.item->id, {prepared.measures.front(), 0}, value.value, entry.provenance()});
    add_owners(entry.item->id.value, entry);
}
void projection_builder_t::emit(contribution_t entry, const bms_direct_bpm_t& value) {
    if (value.value.numerator <= 0 || !value.value.denominator) {
        diagnose(failure_code_t::invalid_data, "Direct BPM must be positive.", entry.owner.chart, entry.item->id);
        return;
    }
    const auto at = position(value.position);
    if (!at) return;
    chart().bpms.push_back({entry.item->id, *at, value.value, entry.provenance()});
    add_owners(entry.item->id.value, entry);
}
void projection_builder_t::emit(contribution_t entry, const bms_timing_reference_t& value) {
    const auto found = definitions.find({value.kind, value.identifier});
    if (found == definitions.end()) {
        diagnose(failure_code_t::not_found, "Undeclared timing reference remains unprojected.",
            entry.owner.chart, entry.item->id, diagnostic_severity_t::warning);
        return;
    }
    const auto at = position(value.position);
    if (!at) return;
    const auto number = std::get<bms_timing_definition_t>(found->second.item->value).value;
    switch (value.kind) {
    case bms_timing_definition_kind_t::bpm:
        chart().bpms.push_back({entry.item->id, *at, number, entry.provenance()}); break;
    case bms_timing_definition_kind_t::stop:
        chart().pauses.push_back({entry.item->id, *at, pause_kind_t::stop, number, pause_unit_t::bms_stop_ticks, entry.provenance()}); break;
    case bms_timing_definition_kind_t::scroll:
        chart().scrolls.push_back({entry.item->id, *at, number, entry.provenance()}); break;
    }
    add_owners(entry.item->id.value, entry);
    add_owners(entry.item->id.value, found->second);
}

void projection_builder_t::emit_bgm() {
    std::map<std::uint64_t, std::map<object_id_t, std::size_t>> lines;
    for (const auto entry : active) {
        const auto* value = std::get_if<bms_bgm_content_t>(&entry.item->value);
        if (!value) continue;
        auto& measure_lines = lines[value->position.measure.value];
        // Authored column identity groups a line independently of source offsets.
        // Active ranks follow program order rather than all-scope column order.
        const auto key = value->column;
        const auto [line, inserted] = measure_lines.try_emplace(key, measure_lines.size());
        (void)inserted;
        const auto rank = line->second;
        if (rank >= prepared.bgm_columns.size()) {
            diagnose(failure_code_t::not_found, "BGM content has no shared column identity.", entry.owner.chart, entry.item->id);
            return;
        }
        if (std::ranges::find(chart().bgm_source_lanes, prepared.bgm_columns[rank].id,
            &bgm_source_lane_t::id) == chart().bgm_source_lanes.end())
            chart().bgm_source_lanes.push_back(prepared.bgm_columns[rank]);
        if (!value->wav) continue;
        const auto at = position(value->position);
        if (!at) continue;
        chart().measures[measures.at(at->measure.value)].bgm_events.push_back({.id = entry.item->id,
            .position = *at, .source_lane = prepared.bgm_columns[rank].id, .sound = sound(*value->wav, entry),
            .structural_slot = value->structural_slot, .provenance = entry.provenance(),
            .semantic_layers = value->semantic_layers});
        add_owners(entry.item->id.value, entry);
        add_resource_owner(entry.item->id.value, bms_resource_namespace_t::wav, *value->wav);
        add_owners(prepared.bgm_columns[rank].id.value, entry);
    }
    std::ranges::sort(chart().bgm_source_lanes, [](const auto& left, const auto& right) {
        return std::tie(left.source_order, left.id) < std::tie(right.source_order, right.id);
    });
}

void projection_builder_t::emit_hold(contribution_t head, contribution_t tail) {
    const auto& start = std::get<bms_hold_endpoint_t>(head.item->value);
    const auto& end = std::get<bms_hold_endpoint_t>(tail.item->value);
    const auto begin = *position(start.position), finish = *position(end.position);
    if (std::pair{measures.at(begin.measure.value), begin.row} >= std::pair{measures.at(finish.measure.value), finish.row}) {
        diagnose(failure_code_t::invalid_geometry, "Hold endpoints must span positive time.", tail.owner.chart, tail.item->id);
        return;
    }
    chart().measures[measures.at(begin.measure.value)].notes.push_back({.id = head.item->id,
        .position = begin, .lane = start.lane, .kind = note_kind_t::hold, .end = finish,
        .sound = sound(start.wav, head), .semantic_layers = start.attributes.semantic_layers,
        .label = start.attributes.label, .provenance = head.provenance(), .osu_hitsample = start.attributes.osu_hitsample});
    add_owners(head.item->id.value, head);
    add_owners(head.item->id.value, tail);
    add_resource_owner(head.item->id.value, bms_resource_namespace_t::wav, start.wav);
}
void projection_builder_t::emit_authoring_endpoint(contribution_t entry) {
    const auto& endpoint = std::get<bms_hold_endpoint_t>(entry.item->value);
    const auto at = position(endpoint.position);
    if (!at) return;
    chart().measures[measures.at(at->measure.value)].notes.push_back({.id = entry.item->id,
        .position = *at, .lane = endpoint.lane, .kind = note_kind_t::tap,
        .sound = sound(endpoint.wav, entry), .semantic_layers = endpoint.attributes.semantic_layers,
        .label = endpoint.attributes.label.value_or("Hold endpoint (read-only scope context)"),
        .provenance = entry.provenance(), .osu_hitsample = endpoint.attributes.osu_hitsample});
    add_owners(entry.item->id.value, entry);
    add_resource_owner(entry.item->id.value, bms_resource_namespace_t::wav, endpoint.wav);
    chart().bms_authoring_read_only.insert(entry.item->id.value);
    if (!warned_authoring_endpoint) {
        warned_authoring_endpoint = true;
        diagnose(failure_code_t::unavailable,
            "Some hold endpoints cannot be paired in this scope view; they are shown as read-only markers.",
            entry.owner.chart, entry.item->id, diagnostic_severity_t::warning);
    }
}
void projection_builder_t::pair_authoring_holds(const std::vector<contribution_t>& entries) {
    std::map<object_id_t, contribution_t> present;
    for (const auto entry : entries) present.emplace(entry.item->id, entry);
    std::set<object_id_t> paired;
    for (const auto entry : entries) {
        const auto known = authoring_pairs.find(entry.item->id);
        if (known == authoring_pairs.end()) continue;
        const auto tail = present.find(known->second);
        if (tail == present.end()) continue;
        emit_hold(entry, tail->second);
        paired.insert(entry.item->id);
        paired.insert(tail->first);
        if (entry.owner.chart != tail->second.owner.chart) chart().bms_authoring_read_only.insert(entry.item->id.value);
    }
    for (const auto entry : entries) if (!paired.contains(entry.item->id)) emit_authoring_endpoint(entry);
}
void projection_builder_t::mark_authoring_context(branch_id_t target) {
    std::set<std::uint64_t> editable;
    for (const auto entry : active) {
        if (entry.owner.chart != target) continue;
        editable.insert(entry.item->id.value);
        if (const auto* native = std::get_if<bms_native_content_t>(&entry.item->value))
            for (const auto id : native->declared_ids()) editable.insert(id.value);
    }
    for (const auto& [id, contributors] : owners) {
        (void)contributors;
        if (!editable.contains(id)) chart().bms_authoring_read_only.insert(id);
    }
}
void projection_builder_t::pair_holds() {
    // Stable sort preserves authored order for colliding endpoints. Exact rows
    // carry musical order; source offsets never override reordered Chart content.
    for (auto& [lane, entries] : endpoints) {
        (void)lane;
        std::ranges::stable_sort(entries, [&](auto left, auto right) {
            const auto a = *position(std::get<bms_hold_endpoint_t>(left.item->value).position);
            const auto b = *position(std::get<bms_hold_endpoint_t>(right.item->value).position);
            return std::pair{measures.at(a.measure.value), a.row} < std::pair{measures.at(b.measure.value), b.row};
        });
        if (chart().bms_authoring_chart) { pair_authoring_holds(entries); continue; }
        for (std::size_t index = 0; index + 1 < entries.size(); index += 2) emit_hold(entries[index], entries[index + 1]);
        if (entries.size() % 2) diagnose(failure_code_t::invalid_geometry, "Unpaired active hold endpoint.", entries.back().owner.chart, entries.back().item->id);
    }
}
void projection_builder_t::evaluate_playback(const bms_program_evaluation_t& evaluation) {
    if (evaluation.selections.size() > limits.maximum_nodes)
        diagnose(failure_code_t::capacity_exceeded, "Evaluation choice count exceeds the node limit.");
    else for (const auto& selection : evaluation.selections) {
        const auto* node = prepared.program.find(selection.branch);
        if (!node || (!std::holds_alternative<bms_random_scope_t>(node->value) && !std::holds_alternative<bms_switch_scope_t>(node->value)))
            diagnose(failure_code_t::invalid_request, "Evaluation choice does not identify a selector.", selection.branch);
        else if (!choices.emplace(selection.branch.value, selection.selected_value).second)
            diagnose(failure_code_t::conflict, "Duplicate evaluation choice.", selection.branch);
    }
    if (!failed) evaluate(*prepared.program.find(prepared.program.root()), std::nullopt, nullptr, nullptr, 0);
    for (const auto& [id, value] : choices) {
        (void)value;
        if (!visited.contains(id)) diagnose(failure_code_t::unavailable, "Evaluation choice targets an inactive selector.", branch_id_t{id});
    }
}
void projection_builder_t::evaluate_authoring(branch_id_t target) {
    const auto* selected = prepared.program.find(target);
    if (!selected || !std::holds_alternative<bms_chart_content_t>(selected->value)) {
        diagnose(failure_code_t::not_found, "Authoring requires an existing Chart node.", target);
        return;
    }
    std::set<branch_id_t> path;
    for (auto ancestor = prepared.program.parent(target); ancestor; ancestor = prepared.program.parent(*ancestor))
        path.insert(*ancestor);
    struct pending_t { branch_id_t node; std::size_t depth; };
    std::vector<pending_t> pending{{.node = prepared.program.root(), .depth = 0}};
    while (!pending.empty() && !failed) {
        const auto current = pending.back();
        pending.pop_back();
        if (current.depth > limits.maximum_depth) {
            diagnose(failure_code_t::capacity_exceeded, "Authoring nesting limit exceeded.", current.node);
            return;
        }
        const auto& node = *prepared.program.find(current.node);
        if (std::holds_alternative<bms_chart_content_t>(node.value))
            evaluate(node, std::nullopt, nullptr, nullptr, current.depth);
        else if (path.contains(node.id))
            for (auto child = node.children.rbegin(); child != node.children.rend(); ++child)
                pending.push_back({.node = *child, .depth = current.depth + 1});
    }
    chart().bms_authoring_chart = target;
}
} // namespace

auto bms_program_projection_t::from(const bms_prepared_program_t& prepared,
    const bms_program_evaluation_t& evaluation, bms_program_projection_limits_t limits)
    -> attempt_t<bms_program_projection_t, bms_program_projection_diagnostics_t> {
    return build(prepared, evaluation, std::nullopt, limits);
}
auto bms_program_projection_t::for_authoring(const bms_prepared_program_t& prepared,
    branch_id_t chart, const bms_program_evaluation_t& witness, bms_program_projection_limits_t limits)
    -> attempt_t<bms_program_projection_t, bms_program_projection_diagnostics_t> {
    return build(prepared, witness, chart, limits);
}
auto bms_program_projection_t::build(const bms_prepared_program_t& prepared,
    const bms_program_evaluation_t& evaluation, std::optional<branch_id_t> authoring_chart,
    bms_program_projection_limits_t limits)
    -> attempt_t<bms_program_projection_t, bms_program_projection_diagnostics_t> {
    projection_builder_t builder{.prepared = prepared, .limits = limits};
    if (!builder.prepare()) return {std::nullopt, std::move(builder.diagnostics)};
    if (authoring_chart) {
        const auto witness = from(prepared, evaluation, limits);
        if (witness.value)
            for (const auto& measure : witness.value->song().charts.front().measures)
                for (const auto& note : measure.notes) {
                    if (!note.end) continue;
                    const auto endpoints = prepared.program.hold_endpoints(witness.value->owners(note.id.value));
                    if (endpoints.size() == 2) builder.authoring_pairs.emplace(endpoints.front().item, endpoints.back().item);
                }
        builder.evaluate_authoring(*authoring_chart);
    } else builder.evaluate_playback(evaluation);
    if (!builder.failed) builder.collect_declarations();
    if (!builder.failed && builder.build_geometry()) {
        for (const auto entry : builder.active) std::visit([&](const auto& value) { builder.emit(entry, value); }, entry.item->value);
        builder.emit_bgm();
        builder.pair_holds();
        if (authoring_chart) builder.mark_authoring_context(*authoring_chart);
    }
    if (builder.failed) return {std::nullopt, std::move(builder.diagnostics)};
    bms_program_projection_t projection;
    projection.song_ = std::move(builder.song);
    projection.active_charts_ = std::move(builder.charts);
    projection.selections_ = std::move(builder.selections);
    projection.owners_ = std::move(builder.owners);
    return {std::move(projection), std::move(builder.diagnostics)};
}

auto bms_program_projection_t::owners(std::uint64_t id) const -> std::span<const bms_program_owner_t> {
    const auto found = owners_.find(id);
    return found == owners_.end() ? std::span<const bms_program_owner_t>{} : found->second;
}
auto bms_program_projection_t::resolve_owner(std::uint64_t id, bms_program_owner_t expected) const
    -> result_t<bms_program_owner_t, error_t<failure_code_t>> {
    const auto contributors = owners(id);
    if (std::ranges::find(contributors, expected) == contributors.end())
        return std::unexpected(error_t<failure_code_t>{failure_code_t::stale_generation,
            "Projected object or its scoped owner is no longer visible."});
    return expected;
}
} // namespace wormedit

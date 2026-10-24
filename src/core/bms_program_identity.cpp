#include <wormedit/core/bms_program.h>
#include "id_ceiling.h"
#include <unordered_set>

namespace wormedit {
namespace {
struct program_identity_map_t {
    const std::unordered_map<std::uint64_t, std::uint64_t>& ids;
    bool valid{true};

    template<class Id> void remap(Id& id) {
        if (!id) return;
        const auto found = ids.find(id.value);
        if (found == ids.end() || !found->second) valid = false;
        else id.value = found->second;
    }
    template<class Id> void remap(std::optional<Id>& id) { if (id) remap(*id); }
    template<class Id> void remap(std::vector<Id>& values) {
        for (auto& value : values) remap(value);
    }
    template<class Values> void declarations(Values& values) {
        for (auto& value : values) remap(value.id);
    }

    template<class Value> void native_payload(Value& value) {
        // The top-level native ID and positions are canonical zeroes. The item
        // owns that identity, and exact positions are remapped separately.
        if constexpr (requires { value.sound; }) remap(value.sound);
        if constexpr (requires { value.sound_channel_event; }) remap(value.sound_channel_event);
        if constexpr (requires { value.resource; }) remap(value.resource);
        if constexpr (requires { value.semantic_layers; }) remap(value.semantic_layers);
        if constexpr (requires { value.source_lane; }) remap(value.source_lane);
        if constexpr (std::same_as<Value, sound_channel_t>) {
            remap(value.source_clip.id);
            remap(value.source_clip.source);
            declarations(value.events);
        }
    }
    void native_content(bms_native_content_t& value) {
        auto payload = value.payload();
        std::visit([&](auto& event) { native_payload(event); }, payload);
        auto positions = std::vector<bms_program_position_t>(value.positions().begin(), value.positions().end());
        for (auto& position : positions) remap(position.measure);
        auto result = bms_native_content_t::from(std::move(payload), std::move(positions), value.resource_reference());
        if (result) value = std::move(*result);
        else valid = false;
    }
    template<class Value> void content(Value& value) {
        if constexpr (requires { value.position; }) remap(value.position.measure);
        if constexpr (requires { value.measure; }) remap(value.measure);
        if constexpr (requires { value.column; }) remap(value.column);
        if constexpr (requires { value.resource; }) remap(value.resource);
        if constexpr (requires { value.attributes; }) remap(value.attributes.semantic_layers);
        if constexpr (requires { value.semantic_layers; }) remap(value.semantic_layers);
        if constexpr (std::same_as<Value, bms_native_content_t>) native_content(value);
    }
    template<class Value> void node_value(Value& value) {
        if constexpr (requires { value.previous_arm; }) remap(value.previous_arm);
        if constexpr (std::same_as<Value, bms_case_scope_t>) {
            for (auto& skip : value.skips) {
                remap(skip.scope);
                remap(skip.after_child);
            }
        }
        if constexpr (std::same_as<Value, bms_chart_content_t>) {
            for (auto& item : value.items) {
                remap(item.id);
                std::visit([&](auto& payload) { content(payload); }, item.value);
            }
        }
    }
    void pools(bms_prepared_program_t& prepared) {
        remap(prepared.measures);
        declarations(prepared.bgm_columns);
        declarations(prepared.audio_assets);
        declarations(prepared.visual_assets);
        declarations(prepared.semantic_layers);
        for (auto& resource : prepared.resources) {
            remap(resource.id);
            remap(resource.asset);
            // Sound groups belong to the project, outside the scoped identity
            // domain. Their numeric values may coincide with local node IDs.
        }
        for (auto& defaults : prepared.sound_layer_defaults) {
            remap(defaults.resource);
            remap(defaults.layers);
        }
    }
};
}

auto bms_prepared_program_t::with_remapped_ids(
    const std::unordered_map<std::uint64_t, std::uint64_t>& ids) const
    -> result_t<bms_prepared_program_t, error_t<failure_code_t>> {
    auto result = *this;
    program_identity_map_t mapping{.ids = ids};
    mapping.pools(result);
    auto nodes = std::vector<bms_program_node_t>(program.nodes().begin(), program.nodes().end());
    for (auto& node : nodes) {
        mapping.remap(node.id);
        mapping.remap(node.children);
        std::visit([&](auto& value) { mapping.node_value(value); }, node.value);
    }
    if (!mapping.valid)
        return std::unexpected(error_t{failure_code_t::invalid_data,
            "Scoped identity mapping is incomplete or contains a zero identity."});
    auto updated = bms_program_t::from(std::move(nodes));
    if (!updated) return std::unexpected(updated.error());
    result.program = std::move(*updated);
    std::unordered_set<std::uint64_t> declarations;
    auto reserve = [&](object_id_t id) { return id && declarations.insert(id.value).second; };
    if (!id_declarations_t<object_id_t>::visit(result, reserve))
        return std::unexpected(error_t{failure_code_t::invalid_data,
            "Scoped identity mapping merges distinct declarations."});
    return result;
}
} // namespace wormedit

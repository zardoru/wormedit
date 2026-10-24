#include "lua_generator.h"
#include "lua_document_api.h"
#include "../core/text_util.h"
#include <wormedit/core/numeric_text.h>
#include <wormedit/formats/native_project_storage.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <set>

namespace wormedit::scripting {
namespace {
using table_t = lua_sandbox_context_t;
using node_t = lua_sandbox_node_t;
[[noreturn]] void malformed(std::string message) { throw std::runtime_error{std::move(message)}; }
auto children(const node_t& node) -> const table_t& {
    if (const auto* value = std::get_if<table_t>(&node.value)) return *value;
    malformed("Generator output needs table values.");
}
auto field(const table_t& table, std::string_view name) -> const node_t* {
    for (const auto& entry : table)
        if (const auto* key = std::get_if<std::string>(&entry.key); key && *key == name) return &entry;
    return nullptr;
}
auto required(const table_t& table, std::string_view name) -> const node_t& {
    if (const auto* value = field(table, name)) return *value;
    malformed("Missing generator field: " + std::string{name});
}
auto scalar(const node_t& node) -> const lua_sandbox_value_t& {
    if (const auto* value = std::get_if<lua_sandbox_value_t>(&node.value)) return *value;
    malformed("Generator field needs a scalar value.");
}
auto text(const node_t& node) -> std::string {
    if (const auto* value = std::get_if<std::string>(&scalar(node))) return *value;
    malformed("Generator key/name needs text.");
}
auto integer(const node_t& node) -> std::uint64_t {
    const auto& value = scalar(node);
    if (const auto* number = std::get_if<std::int64_t>(&value); number && *number >= 0)
        return static_cast<std::uint64_t>(*number);
    if (const auto* string = std::get_if<std::string>(&value)) {
        std::uint64_t number{};
        if (parse_integer(std::string_view{*string},number)) return number;
    }
    malformed("Generator positions and identities need exact unsigned integers or decimal strings.");
}
void dense(const table_t& table) {
    for (std::size_t i = 0; i < table.size(); ++i) {
        const auto* key = std::get_if<std::int64_t>(&table[i].key);
        if (!key || *key != static_cast<std::int64_t>(i + 1)) malformed("Generator lists must be dense arrays starting at 1.");
    }
}
auto position(const table_t& table, std::string_view measure, std::string_view row) -> local_position_t {
    return {.measure=measure_id_t{integer(required(table,measure))},.row=integer(required(table,row))};
}
void only_fields(const table_t& table, std::initializer_list<std::string_view> allowed) {
    for (const auto& entry : table) {
        const auto* key = std::get_if<std::string>(&entry.key);
        if (!key || std::ranges::find(allowed, *key) == allowed.end()) malformed("Unknown generator proposal field.");
    }
}
auto decode_note(const node_t& node) -> generator_note_t {
    const auto& table = children(node);
    only_fields(table, {"key", "measure", "row", "lane", "kind", "end_measure", "end_row", "sound", "layers"});
    generator_note_t note;
    note.key = text(required(table, "key"));
    note.value.position = position(table, "measure", "row");
    note.value.lane = lane_id_t{integer(required(table, "lane"))};
    const auto* kind = field(table, "kind");
    const auto kind_name = kind ? text(*kind) : "tap";
    if (kind_name != "tap" && kind_name != "hold") malformed("Generator note kind must be tap or hold.");
    note.value.kind = kind_name == "hold" ? note_kind_t::hold : note_kind_t::tap;
    if (kind_name == "hold") note.value.end = position(table, "end_measure", "end_row");
    else if (field(table,"end_measure") || field(table,"end_row")) malformed("Tap notes cannot provide hold endpoint fields.");
    if (const auto* sound = field(table, "sound")) note.value.sound = resource_id_t{integer(*sound)};
    if (const auto* layers = field(table, "layers")) {
        dense(children(*layers));
        for (const auto& layer : children(*layers)) note.layers.push_back(text(layer));
    }
    return note;
}
auto color_channel(const table_t& table, std::string_view name) -> std::uint8_t {
    const auto value = integer(required(table, name));
    if (value > 255) malformed("Generator layer color channels must be integers from 0 to 255.");
    return static_cast<std::uint8_t>(value);
}
auto decode_layer(const node_t& node) -> generator_layer_t {
    const auto& table = children(node);
    only_fields(table, {"key", "name", "color"});
    generator_layer_t layer;
    layer.key = text(required(table, "key"));
    layer.value.name = text(required(table, "name"));
    layer.value.color = {128, 160, 255, 255};
    if (const auto* color = field(table, "color")) {
        const auto& channels = children(*color);
        only_fields(channels, {"r", "g", "b", "a"});
        layer.value.color = {color_channel(channels, "r"), color_channel(channels, "g"),
            color_channel(channels, "b"), color_channel(channels, "a")};
    }
    return layer;
}
auto decode_output(const table_t& table) -> generator_output_t {
    only_fields(table, {"notes", "layers"});
    if (!field(table, "notes") && !field(table, "layers")) malformed("Generator must return notes or layers arrays explicitly.");
    generator_output_t output;
    if (const auto* layers = field(table, "layers")) {
        const auto& values = children(*layers); dense(values);
        if (values.size() > maximum_generator_layers) malformed("Generator layer limit exceeded.");
        for (const auto& layer : values) output.layers.push_back(decode_layer(layer));
    }
    if (const auto* notes = field(table, "notes")) {
        const auto& values = children(*notes); dense(values);
        if (values.size() > maximum_generator_notes) malformed("Generator note limit exceeded.");
        for (const auto& note : values) output.notes.push_back(decode_note(note));
    }
    return output;
}
class generator_context_builder_t {
  public:
    explicit generator_context_builder_t(lua_sandbox_limits_t& limits):limits_(limits) {}
    void charge(std::size_t units) {
        if (limits_.cancellation.stop_requested()) malformed("Generator preparation cancelled.");
        if (units>limits_.maximum_instructions) malformed("Generator preparation exhausted the instruction budget.");
        limits_.maximum_instructions-=units;
    }
    void storage(std::size_t bytes) {
        if (bytes>limits_.maximum_memory_bytes-storage_) malformed("Generator context exceeds its construction memory limit.");
        storage_+=bytes;
    }
    auto json_node(std::string key,const nlohmann::json& value,unsigned depth=0) -> node_t {
        charge(1); storage(sizeof(node_t)+key.size());
        if (depth>16) malformed("Generator parameters exceed the nesting limit.");
        if (value.is_null()) return node_t::from(std::move(key),lua_sandbox_value_t{std::monostate{}});
        if (value.is_boolean()) return node_t::from(std::move(key),lua_sandbox_value_t{value.get<bool>()});
        if (value.is_number_unsigned()) return node_t::from_unsigned(std::move(key),value.get<std::uint64_t>());
        if (value.is_number_integer()) return node_t::from(std::move(key),lua_sandbox_value_t{value.get<std::int64_t>()});
        if (value.is_number_float()) return node_t::from(std::move(key),lua_sandbox_value_t{value.get<double>()});
        if (value.is_string()) {
            storage(value.get_ref<const std::string&>().size());
            return node_t::from(std::move(key),lua_sandbox_value_t{value.get<std::string>()});
        }
        table_t nested;
        for (auto item=value.begin();item!=value.end();++item) {
            auto node=json_node(value.is_array() ? "" : item.key(),*item,depth+1);
            if (value.is_array()) node.key=static_cast<std::int64_t>(nested.size()+1);
            nested.push_back(std::move(node));
        }
        return node_t::from(std::move(key),std::move(nested));
    }
    auto context(const generator_recipe_t& recipe) -> table_t {
        table_t parameters,inputs;
        std::set<std::string> names;
        for (const auto& parameter:recipe.parameters) {
            charge(parameter.serialized_value.size()+parameter.name.size());
            if (parameter.name.empty() || !text_util::is_valid_bounded_text(parameter.name,128) ||
                !names.insert(parameter.name).second || !text_util::is_valid_bounded_text(parameter.serialized_value,16384))
                malformed("Generator parameters need unique bounded UTF8 names and JSON values.");
            if (parameter.serialized_value.size()>limits_.maximum_memory_bytes/32)
                malformed("Generator JSON parameter exceeds its parsing memory limit.");
            const auto value=nlohmann::json::parse(parameter.serialized_value);
            parameters.push_back(json_node(parameter.name,value));
        }
        for (const auto& input:recipe.input_assets) {
            charge(3+input.content_hash.size()); storage(3*sizeof(node_t)+input.content_hash.size());
            auto node=node_t::from("",table_t{node_t::from("asset",lua_sandbox_value_t{std::to_string(input.asset.value)}),
                node_t::from("content_hash",lua_sandbox_value_t{input.content_hash})});
            node.key=static_cast<std::int64_t>(inputs.size()+1);
            inputs.push_back(std::move(node));
        }
        return {node_t::from("parameters",std::move(parameters)),node_t::from("inputs",std::move(inputs)),
            node_t::from("seed",lua_sandbox_value_t{std::to_string(recipe.seed)}),
            node_t::from("api_version",lua_sandbox_value_t{std::string{"1"}})};
    }
    void account(const table_t& table) {
        for (const auto& node:table) {
            charge(2);
            if (const auto* key=std::get_if<std::string>(&node.key)) charge(key->size());
            if (const auto* children=std::get_if<table_t>(&node.value)) account(*children);
            else if (const auto* text=std::get_if<std::string>(&std::get<lua_sandbox_value_t>(node.value))) charge(text->size());
        }
    }
    auto declared_inputs(const song_document_t& song,const generator_recipe_t& recipe) -> std::vector<asset_id_t> {
        std::set<asset_id_t> missing;
        std::vector<asset_id_t> inputs;
        for (const auto& input:recipe.input_assets) {
            charge(1);
            if (!input.asset || !missing.insert(input.asset).second) malformed("Generator input IDs must be nonzero and unique.");
            inputs.push_back(input.asset);
        }
        for (const auto& asset:song.midi_assets) {
            if (missing.empty()) break;
            charge(1); missing.erase(asset.id);
        }
        if (!missing.empty()) malformed("Generator declared an unavailable MIDI asset.");
        return inputs;
    }
  private:
    lua_sandbox_limits_t& limits_;
    std::size_t storage_{};
};
auto execute_once(const lua_document_snapshot_t& snapshot,const table_t& context,
    const generator_recipe_t& recipe,lua_sandbox_limits_t& limits,std::optional<lua_sandbox_t>* retain=nullptr)
    -> result_t<table_t,error_t<failure_code_t>> {
    if (!limits.maximum_instructions) return std::unexpected(error_t{
        failure_code_t::capacity_exceeded,"Generator exhausted the complete build instruction budget."});
    auto sandbox=lua_sandbox_t::from(limits);
    if (!sandbox) return std::unexpected(sandbox.error());
    if (auto installed=snapshot.install(*sandbox); !installed) return std::unexpected(installed.error());
    if (auto installed=sandbox->install_snapshot("generator",context); !installed) return std::unexpected(installed.error());
    const auto name=recipe.source_path.empty() ? "generator" : "@"+text_util::path_utf8(recipe.source_path);
    auto result=sandbox->evaluate_context(recipe.source,name);
    limits.maximum_instructions=sandbox->usage().remaining_instructions;
    if (result && retain) *retain=std::move(*sandbox);
    return result;
}
auto rejected(std::string message) -> diagnosed_t<generator_output_t> {
    return {.diagnostics = {error(diagnostic_code_t::invalid_binding, std::move(message))}};
}
} // namespace

auto run_lua_generator(const song_document_t& song, const chart_document_t& chart,
    const generator_recipe_t& recipe, std::span<const editor_selection_object_t> selection,
    lua_sandbox_limits_t limits) -> diagnosed_t<generator_output_t> {
    try {
        if (recipe.api_version!="1" || recipe.source.empty() ||
            !text_util::is_valid_bounded_text(recipe.source,maximum_generator_source_bytes))
            return rejected("Generator requires API1 and bounded nonempty UTF8 source.");
        if (recipe.parameters.size()>maximum_generator_parameters || recipe.input_assets.size()>maximum_generator_parameters)
            return rejected("Generator parameter or input count exceeds its limit.");
        if (!recipe.source_hash.empty() && !text_util::ascii_iequal(recipe.source_hash,native_project_content_checksum(recipe.source)))
            return rejected("Generator source does not match its content hash.");
        generator_context_builder_t preparation(limits);
        const auto declared=preparation.declared_inputs(song,recipe);
        const auto context=preparation.context(recipe);
        const auto snapshot=lua_document_snapshot_t::from(song,chart,selection,
            {.maximum_nodes=static_cast<std::size_t>(std::min<std::uint64_t>(65536,limits.maximum_instructions)),
             .maximum_bytes=std::min<std::size_t>(8U*1024U*1024U,limits.maximum_memory_bytes),.cancellation=limits.cancellation},
            std::span<const asset_id_t>{declared});
        if (!snapshot) return rejected(snapshot.error().message);
        preparation.account(snapshot->context());
        limits.seed=recipe.seed;
        std::optional<lua_sandbox_t> initial;
        const auto first=execute_once(*snapshot,context,recipe,limits,&initial);
        if (!first) return rejected(first.error().message);
        auto output=decode_output(*first);
        limits.maximum_memory_bytes-=initial->usage().allocated_bytes;
        const auto replay=execute_once(*snapshot,context,recipe,limits);
        if (!replay) return rejected("Generator replay failed: "+replay.error().message);
        if (output!=decode_output(*replay)) return rejected("Generator output is not deterministic for the frozen inputs and seed.");
        return {.value=std::move(output)};
    } catch (const std::exception& exception) {
        return rejected(exception.what());
    }
}
} // namespace wormedit::scripting

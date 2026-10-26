#include "lua_generator.h"
#include "lua_project.h"
#include "lua_value_reader.h"
#include "chart_lua.h"
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
namespace values = lua_value_detail;
auto position(const values::table_t& table, std::string_view measure, std::string_view row) -> local_position_t {
    return {.measure=measure_id_t{values::integer(values::required(table,measure))},.row=values::integer(values::required(table,row))};
}
auto decode_note(const values::node_t& node) -> generator_note_t {
    const auto& table = values::children(node);
    values::only_fields(table, {"key", "measure", "row", "lane", "kind", "end_measure", "end_row", "sound", "layers"});
    generator_note_t note;
    note.key = values::text(values::required(table, "key"));
    note.value.position = position(table, "measure", "row");
    note.value.lane = lane_id_t{values::integer(values::required(table, "lane"))};
    const auto* kind = values::field(table, "kind");
    const auto kind_name = kind ? values::text(*kind) : "tap";
    if (kind_name != "tap" && kind_name != "hold") values::malformed("Generator note kind must be tap or hold.");
    note.value.kind = kind_name == "hold" ? note_kind_t::hold : note_kind_t::tap;
    if (kind_name == "hold") note.value.end = position(table, "end_measure", "end_row");
    else if (values::field(table,"end_measure") || values::field(table,"end_row")) values::malformed("Tap notes cannot provide hold endpoint fields.");
    if (const auto* sound = values::field(table, "sound")) note.value.sound = resource_id_t{values::integer(*sound)};
    if (const auto* layers = values::field(table, "layers")) {
        values::dense(values::children(*layers));
        for (const auto& layer : values::children(*layers)) note.layers.push_back(values::text(layer));
    }
    return note;
}
auto color_channel(const values::table_t& table, std::string_view name) -> std::uint8_t {
    const auto value = values::integer(values::required(table, name));
    if (value > 255) values::malformed("Generator layer color channels must be integers from 0 to 255.");
    return static_cast<std::uint8_t>(value);
}
auto decode_layer(const values::node_t& node) -> generator_layer_t {
    const auto& table = values::children(node);
    values::only_fields(table, {"key", "name", "color"});
    generator_layer_t layer;
    layer.key = values::text(values::required(table, "key"));
    layer.value.name = values::text(values::required(table, "name"));
    layer.value.color = {128, 160, 255, 255};
    if (const auto* color = values::field(table, "color")) {
        const auto& channels = values::children(*color);
        values::only_fields(channels, {"r", "g", "b", "a"});
        layer.value.color = {color_channel(channels, "r"), color_channel(channels, "g"),
            color_channel(channels, "b"), color_channel(channels, "a")};
    }
    return layer;
}
auto decode_output(const values::table_t& table) -> generator_output_t {
    values::only_fields(table, {"notes", "layers", "charts", "sv_points", "sv_spans"});
    if (const auto* charts = values::field(table, "charts")) {
        if (values::field(table, "notes") || values::field(table, "layers") || values::field(table, "sv_points") || values::field(table, "sv_spans")) values::malformed("Chart batches cannot mix loose generator output.");
        return decode_lua_charts(values::children(*charts));
    }
    if (!values::field(table, "notes") && !values::field(table, "layers") && !values::field(table, "sv_points") && !values::field(table, "sv_spans")) values::malformed("Generator must return an explicit notes, layers, sv_points or sv_spans array.");
    generator_output_t output;
    decode_lua_sv(table, output);
    if (const auto* layers = values::field(table, "layers")) {
        const auto& values = values::children(*layers); values::dense(values);
        if (values.size() > maximum_generator_layers) values::malformed("Generator layer limit exceeded.");
        for (const auto& layer : values) output.layers.push_back(decode_layer(layer));
    }
    if (const auto* notes = values::field(table, "notes")) {
        const auto& values = values::children(*notes); values::dense(values);
        if (values.size() > maximum_generator_notes) values::malformed("Generator note limit exceeded.");
        for (const auto& note : values) output.notes.push_back(decode_note(note));
    }
    return output;
}
class generator_context_builder_t {
  public:
    explicit generator_context_builder_t(lua_sandbox_limits_t& limits):limits_(limits) {}
    void charge(std::size_t units) {
        if (limits_.cancellation.stop_requested()) values::malformed("Generator preparation cancelled.");
        if (units>limits_.maximum_instructions) values::malformed("Generator preparation exhausted the instruction budget.");
        limits_.maximum_instructions-=units;
    }
    void storage(std::size_t bytes) {
        if (bytes>limits_.maximum_memory_bytes-storage_) values::malformed("Generator context exceeds its construction memory limit.");
        storage_+=bytes;
    }
    auto json_node(std::string key,const nlohmann::json& value,unsigned depth=0) -> values::node_t {
        charge(1); storage(sizeof(values::node_t)+key.size());
        if (depth>16) values::malformed("Generator parameters exceed the nesting limit.");
        if (value.is_null()) return values::node_t::from(std::move(key),lua_sandbox_value_t{std::monostate{}});
        if (value.is_boolean()) return values::node_t::from(std::move(key),lua_sandbox_value_t{value.get<bool>()});
        if (value.is_number_unsigned()) return values::node_t::from_unsigned(std::move(key),value.get<std::uint64_t>());
        if (value.is_number_integer()) return values::node_t::from(std::move(key),lua_sandbox_value_t{value.get<std::int64_t>()});
        if (value.is_number_float()) return values::node_t::from(std::move(key),lua_sandbox_value_t{value.get<double>()});
        if (value.is_string()) {
            storage(value.get_ref<const std::string&>().size());
            return values::node_t::from(std::move(key),lua_sandbox_value_t{value.get<std::string>()});
        }
        values::table_t nested;
        for (auto item=value.begin();item!=value.end();++item) {
            auto node=json_node(value.is_array() ? "" : item.key(),*item,depth+1);
            if (value.is_array()) node.key=static_cast<std::int64_t>(nested.size()+1);
            nested.push_back(std::move(node));
        }
        return values::node_t::from(std::move(key),std::move(nested));
    }
    auto context(const generator_recipe_t& recipe) -> values::table_t {
        values::table_t parameters,inputs;
        std::set<std::string> names;
        for (const auto& parameter:recipe.parameters) {
            charge(parameter.serialized_value.size()+parameter.name.size());
            if (parameter.name.empty() || !text_util::is_valid_bounded_text(parameter.name,128) ||
                !names.insert(parameter.name).second || !text_util::is_valid_bounded_text(parameter.serialized_value,16384))
                values::malformed("Generator parameters need unique bounded UTF8 names and JSON values.");
            if (parameter.serialized_value.size()>limits_.maximum_memory_bytes/32)
                values::malformed("Generator JSON parameter exceeds its parsing memory limit.");
            const auto value=nlohmann::json::parse(parameter.serialized_value);
            parameters.push_back(json_node(parameter.name,value));
        }
        for (const auto& input:recipe.input_assets) {
            charge(3+input.content_hash.size()); storage(3*sizeof(values::node_t)+input.content_hash.size());
            auto node=values::node_t::from("",values::table_t{values::node_t::from("asset",lua_sandbox_value_t{std::to_string(input.asset.value)}),
                values::node_t::from("content_hash",lua_sandbox_value_t{input.content_hash})});
            node.key=static_cast<std::int64_t>(inputs.size()+1);
            inputs.push_back(std::move(node));
        }
        return {values::node_t::from("parameters",std::move(parameters)),values::node_t::from("inputs",std::move(inputs)),
            values::node_t::from("seed",lua_sandbox_value_t{std::to_string(recipe.seed)}),
            values::node_t::from("api_version",lua_sandbox_value_t{std::string{"1"}})};
    }
    void account(const values::table_t& table) {
        for (const auto& node:table) {
            charge(2);
            if (const auto* key=std::get_if<std::string>(&node.key)) charge(key->size());
            if (const auto* children=std::get_if<values::table_t>(&node.value)) account(*children);
            else if (const auto* text=std::get_if<std::string>(&std::get<lua_sandbox_value_t>(node.value))) charge(text->size());
        }
    }
    auto declared_inputs(const song_document_t& song,const generator_recipe_t& recipe) -> std::vector<asset_id_t> {
        std::set<asset_id_t> missing;
        std::vector<asset_id_t> inputs;
        for (const auto& input:recipe.input_assets) {
            charge(1);
            if (!input.asset || !missing.insert(input.asset).second) values::malformed("Generator input IDs must be nonzero and unique.");
            inputs.push_back(input.asset);
        }
        for (const auto& asset:song.midi_assets) {
            if (missing.empty()) break;
            charge(1); missing.erase(asset.id);
        }
        if (!missing.empty()) values::malformed("Generator declared an unavailable MIDI asset.");
        return inputs;
    }
  private:
    lua_sandbox_limits_t& limits_;
    std::size_t storage_{};
};
auto execute_once(const lua_document_snapshot_t& snapshot,const values::table_t& context,
    const generator_recipe_t& recipe,lua_sandbox_limits_t& limits, const lua_project_reader_t& reader,
    std::optional<lua_sandbox_t>* retain=nullptr)
    -> result_t<values::table_t,error_t<failure_code_t>> {
    if (!limits.maximum_instructions) return std::unexpected(error_t{
        failure_code_t::capacity_exceeded,"Generator exhausted the complete build instruction budget."});
    auto sandbox=lua_sandbox_t::from(limits);
    if (!sandbox) return std::unexpected(sandbox.error());
    if (reader || lua_project_source_t::is_frozen(recipe.source)) {
        if (auto installed=sandbox->install_project_runtime(reader); !installed) return std::unexpected(installed.error());
    }
    if (auto installed=snapshot.install(*sandbox); !installed) return std::unexpected(installed.error());
    if (auto installed=sandbox->install_snapshot("generator",context); !installed) return std::unexpected(installed.error());
    const auto name=recipe.source_path.empty() ? "generator" : "@"+text_util::path_utf8(recipe.source_path);
    const auto adapter=std::string(chart_lua_prelude())+"\nreturn finish_chart_declarations";
    auto result=sandbox->evaluate_context(recipe.source,name,adapter);
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
    lua_sandbox_limits_t limits, lua_project_reader_t reader) -> diagnosed_t<generator_output_t> {
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
        const auto first=execute_once(*snapshot,context,recipe,limits,reader,&initial);
        if (!first) return rejected(first.error().message);
        auto output=decode_output(*first);
        limits.maximum_memory_bytes-=initial->usage().allocated_bytes;
        const auto replay=execute_once(*snapshot,context,recipe,limits,reader);
        if (!replay) return rejected("Generator replay failed: "+replay.error().message);
        if (output!=decode_output(*replay)) return rejected("Generator output is not deterministic for the frozen inputs and seed.");
        return {.value=std::move(output)};
    } catch (const std::exception& exception) {
        return rejected(exception.what());
    }
}
} // namespace wormedit::scripting

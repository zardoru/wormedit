#include "lua_document_api.h"
#include <wormedit/core/catalogs.h>
#include <wormedit/core/rational.h>
#include <algorithm>
#include <optional>
#include <set>
#include <utility>
#include <type_traits>

namespace wormedit::scripting {
namespace {
class document_snapshot_builder_t {
  public:
    explicit document_snapshot_builder_t(lua_document_limits_t limits) : limits_(std::move(limits)) {}
    auto check(std::size_t bytes) -> bool {
        if (failure_) return false;
        if (limits_.cancellation.stop_requested()) failure_=error_t{failure_code_t::cancelled,"Lua document snapshot cancelled."};
        else if (nodes_>=limits_.maximum_nodes || bytes>limits_.maximum_bytes-bytes_)
            failure_=error_t{failure_code_t::capacity_exceeded,"Lua document snapshot exceeds its construction limits."};
        if (failure_) return false;
        ++nodes_; bytes_+=bytes;
        return true;
    }
    auto text(std::string key,std::string_view value) -> lua_sandbox_node_t {
        if (!check(sizeof(lua_sandbox_node_t)+key.size()+value.size())) return {};
        return lua_sandbox_node_t::from(std::move(key),std::string(value));
    }
    auto scalar(std::string key,lua_sandbox_value_t value) -> lua_sandbox_node_t {
        if (!check(sizeof(lua_sandbox_node_t)+key.size())) return {};
        return lua_sandbox_node_t::from(std::move(key),std::move(value));
    }
    auto integer(std::string key,std::uint64_t value) -> lua_sandbox_node_t {
        if (!check(sizeof(lua_sandbox_node_t)+key.size()+20)) return {};
        return lua_sandbox_node_t::from_unsigned(std::move(key),value);
    }
    auto object(std::string key,lua_sandbox_context_t children) -> lua_sandbox_node_t {
        if (!check(sizeof(lua_sandbox_node_t)+key.size())) return {};
        return lua_sandbox_node_t::from(std::move(key),std::move(children));
    }
    template<class Id> auto id(std::string key,Id value) -> lua_sandbox_node_t {
        return text(std::move(key),std::to_string(value.value));
    }
    template<class Rational> auto rational(std::string key,Rational value) -> lua_sandbox_node_t {
        lua_sandbox_context_t fields;
        if constexpr (std::is_signed_v<decltype(value.numerator)>)
            fields.push_back(scalar("num",static_cast<std::int64_t>(value.numerator)));
        else fields.push_back(integer("num",value.numerator));
        fields.push_back(integer("den",value.denominator));
        return object(std::move(key),std::move(fields));
    }
    auto position(std::string key,local_position_t value) -> lua_sandbox_node_t {
        return object(std::move(key),{id("measure",value.measure),integer("row",value.row)});
    }
    template<class Range,class Operation> auto array(const Range& values,Operation operation) -> lua_sandbox_context_t {
        lua_sandbox_context_t result;
        for (const auto& value:values) {
            if (!check(sizeof(lua_sandbox_node_t)+32)) break;
            auto fields=operation(value);
            if (failure_) break;
            result.push_back({.key=static_cast<std::int64_t>(result.size()+1),.value=std::move(fields)});
        }
        return result;
    }
    template<class Range,class Fields> auto events(const Range& values,Fields fields) -> lua_sandbox_context_t {
        return array(values,[&](const auto& value) {
            auto result=fields(value);
            result.push_back(id("id",value.id));
            result.push_back(position("position",value.position));
            return result;
        });
    }
    auto timing(const chart_document_t& chart) -> lua_sandbox_context_t {
        return {
            object("bpms",events(chart.bpms,[&](const auto& event) { return lua_sandbox_context_t{rational("value",event.beats_per_minute)}; })),
            object("scrolls",events(chart.scrolls,[&](const auto& event) { return lua_sandbox_context_t{rational("value",event.multiplier)}; })),
            object("speeds",events(chart.speeds,[&](const auto& event) { return lua_sandbox_context_t{
                rational("value",event.multiplier),rational("transition",event.transition_beats),scalar("transition_is_seconds",event.transition_is_seconds)}; })),
            object("pauses",events(chart.pauses,[&](const auto& event) { return lua_sandbox_context_t{
                rational("duration",event.duration),text("kind",event.kind==pause_kind_t::stop ? "stop" : "delay"),
                integer("unit",static_cast<std::uint64_t>(event.source_unit))}; })),
            object("warps",array(chart.warps,[&](const auto& event) { return lua_sandbox_context_t{
                id("id",event.id),position("begin",event.span.begin),position("end",event.span.end)}; })),
            object("kiai",events(chart.kiai,[&](const auto& event) { return lua_sandbox_context_t{scalar("enabled",event.enabled),integer("effect_bits",event.retained_effect_bits)}; })),
            object("anchors",events(chart.timing_anchors,[&](const auto& event) { return lua_sandbox_context_t{
                integer("source_sample_frame",event.source_sample_frame),scalar("locked",event.locked)}; })),
            rational("beat_zero_audio_offset_seconds",chart.beat_zero_audio_offset_seconds)};
    }
    auto measure(const measure_chunk_t& value,const measure_index_entry_t& entry) -> lua_sandbox_context_t {
        lua_sandbox_context_t fields{id("id",value.id),rational("length",value.definition.length),
            integer("start_row",entry.absolute_begin_row),integer("row_count",entry.row_capacity)};
        if (value.definition.meter) fields.push_back(object("meter",{
            integer("num",value.definition.meter->numerator),integer("den",value.definition.meter->denominator)}));
        return fields;
    }
    auto lane(const lane_definition_t& value) -> lua_sandbox_context_t {
        lua_sandbox_context_t fields{id("id",value.id),text("name",value.name),integer("role",static_cast<std::uint64_t>(value.role)),
            integer("stage",static_cast<std::uint64_t>(value.stage)),integer("display_order",value.display_order),
            integer("glyph",static_cast<std::uint64_t>(value.glyph)),integer("key_color",static_cast<std::uint64_t>(value.key_color))};
        if (value.midi_destination) fields.push_back(object("midi_destination",{
            integer("channel",value.midi_destination->channel),integer("note",value.midi_destination->note)}));
        return fields;
    }
    auto layouts(const chart_document_t& chart) -> lua_sandbox_context_t {
        return array(chart.lane_layouts,[&](const auto& segment) {
            auto lanes=array(segment.layout.lanes,[&](const auto& value) { return lane(value); });
            return lua_sandbox_context_t{id("id",segment.id),position("position",segment.position),
                text("profile_id",segment.layout.profile_id),text("display_name",segment.layout.display_name),
                scalar("split_stage",segment.layout.split_stage),object("lanes",std::move(lanes))};
        });
    }
    auto note(const note_t& value) -> lua_sandbox_context_t {
        auto layers=array(value.semantic_layers,[&](const auto& layer) { return lua_sandbox_value_t{std::to_string(layer.value)}; });
        lua_sandbox_context_t fields{id("id",value.id),position("position",value.position),id("lane",value.lane),
            text("kind",note_kind_label(value.kind)),object("layers",std::move(layers))};
        if (value.end) fields.push_back(position("end",*value.end));
        if (value.sound) fields.push_back(id("sound",*value.sound));
        if (value.label) fields.push_back(text("label",*value.label));
        return fields;
    }
    auto notes(const chart_document_t& chart) -> lua_sandbox_context_t {
        lua_sandbox_context_t result;
        for (const auto& measure:chart.measures) {
            auto entries=array(measure.notes,[&](const auto& value) { return note(value); });
            for (auto& entry:entries) {
                entry.key=static_cast<std::int64_t>(result.size()+1);
                result.push_back(std::move(entry));
            }
            if (failure_) break;
        }
        return result;
    }
    static auto midi_kind(const midi_event_t& event) -> std::string_view {
        if (event.data.empty() || event.channel>15) return {};
        const auto status=std::to_integer<unsigned>(event.data[0]);
        if (event.data.size()==3 && std::to_integer<unsigned>(event.data[1])<128 && std::to_integer<unsigned>(event.data[2])<128) {
            switch (status&0xf0U) {
            case 0x80: return "note_off";
            case 0x90: return "note_on";
            case 0xb0: return "controller";
            default: break;
            }
        }
        if (status!=0xff || event.data.size()<2) return {};
        switch (std::to_integer<unsigned>(event.data[1])) {
        case 0x03: return "track_name";
        case 0x06: return "marker";
        case 0x51: return "tempo";
        case 0x58: return "time_signature";
        default: return {};
        }
    }
    auto midi_event(const midi_event_t& event,std::string_view kind) -> lua_sandbox_context_t {
        auto data=array(event.data,[](std::byte value) { return lua_sandbox_value_t{static_cast<std::int64_t>(std::to_integer<unsigned>(value))}; });
        lua_sandbox_context_t fields{integer("tick",event.tick),integer("channel",event.channel),text("kind",kind),object("data",std::move(data))};
        if (kind=="note_on" || kind=="note_off") {
            fields.push_back(integer("pitch",std::to_integer<unsigned>(event.data[1])));
            fields.push_back(integer("velocity",std::to_integer<unsigned>(event.data[2])));
        } else if (kind=="controller") {
            fields.push_back(integer("controller",std::to_integer<unsigned>(event.data[1])));
            fields.push_back(integer("value",std::to_integer<unsigned>(event.data[2])));
        }
        return fields;
    }
    auto midi_asset(const midi_asset_t& asset) -> lua_sandbox_context_t {
        lua_sandbox_context_t entries;
        std::uint64_t discarded{};
        for (const auto& event:asset.events) {
            if (!check(sizeof(lua_sandbox_node_t))) break;
            const auto kind=midi_kind(event);
            if (kind.empty()) { ++discarded; continue; }
            auto fields=midi_event(event,kind);
            if (failure_) break;
            entries.push_back({.key=static_cast<std::int64_t>(entries.size()+1),.value=std::move(fields)});
        }
        return {id("id",asset.id),integer("ticks_per_quarter",asset.ticks_per_quarter),
            object("events",std::move(entries)),integer("discarded_event_count",discarded)};
    }
    auto midi(const song_document_t& song,std::optional<std::span<const asset_id_t>> inputs) -> lua_sandbox_context_t {
        std::set<asset_id_t> declared;
        if (inputs) for (const auto id:*inputs) {
            if (!check(sizeof(asset_id_t))) return {};
            declared.insert(id);
        }
        lua_sandbox_context_t result;
        for (const auto& asset:song.midi_assets) {
            if (!check(sizeof(lua_sandbox_node_t))) break;
            if (inputs && !declared.contains(asset.id)) continue;
            auto fields=midi_asset(asset);
            if (failure_) break;
            result.push_back({.key=static_cast<std::int64_t>(result.size()+1),.value=std::move(fields)});
        }
        return result;
    }
    auto build(const song_document_t& song,const chart_document_t& chart,
        std::span<const editor_selection_object_t> selection,const measure_index_t& index,
        std::optional<std::span<const asset_id_t>> midi_inputs) -> lua_sandbox_context_t {
        auto selected=array(selection,[&](const auto& value) { return lua_sandbox_context_t{
            id("id",value.id),integer("kind",static_cast<std::uint64_t>(value.kind))}; });
        std::size_t measure_position{};
        auto measure_data=array(chart.measures,[&](const auto& value) { return measure(value,index.entries[measure_position++]); });
        auto layers=array(chart.semantic_layers,[&](const auto& value) { return lua_sandbox_context_t{
            id("id",value.id),text("name",value.name),scalar("visible",value.visible),scalar("locked",value.locked)}; });
        return {integer("api_version",1),id("project_id",song.id),id("chart_id",chart.id),integer("revision",song.revision),
            integer("divisions_per_standard_measure",chart.divisions_per_standard_measure),
            object("selection",std::move(selected)),object("measures",std::move(measure_data)),
            object("timing",timing(chart)),object("lane_profile",layouts(chart)),object("notes",notes(chart)),
            object("layers",std::move(layers)),object("midi_assets",midi(song,midi_inputs))};
    }
    auto failure() const -> const std::optional<error_t<failure_code_t>>& { return failure_; }
  private:
    lua_document_limits_t limits_;
    std::size_t nodes_{},bytes_{};
    std::optional<error_t<failure_code_t>> failure_;
};
} // namespace

auto lua_document_snapshot_t::from(const song_document_t& song,const chart_document_t& chart,
    std::span<const editor_selection_object_t> selection,lua_document_limits_t limits,
    std::optional<std::span<const asset_id_t>> midi_inputs)
    -> result_t<lua_document_snapshot_t,error_t<failure_code_t>> {
    if (!limits.maximum_nodes || !limits.maximum_bytes || chart.measures.size()>limits.maximum_nodes)
        return std::unexpected(error_t{failure_code_t::capacity_exceeded,"Invalid or exceeded Lua snapshot construction limits."});
    if (limits.cancellation.stop_requested()) return std::unexpected(error_t{failure_code_t::cancelled,"Lua document snapshot cancelled."});
    const auto index=measure_index_t::from(chart);
    if (!index) return std::unexpected(error_t{failure_code_t::invalid_data,"Lua document snapshot requires a valid measure grid."});
    document_snapshot_builder_t builder(std::move(limits));
    auto context=builder.build(song,chart,selection,*index,midi_inputs);
    if (builder.failure()) return std::unexpected(*builder.failure());
    return lua_document_snapshot_t(std::move(context));
}
auto lua_document_snapshot_t::install(lua_sandbox_t& sandbox) const -> result_t<void,error_t<failure_code_t>> {
    return sandbox.install_snapshot("wormedit",context_);
}
} // namespace wormedit::scripting

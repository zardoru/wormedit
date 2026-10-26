#include "../../src/scripting/lua_generator.h"
#include "../support/lua_documentation.h"
#include <wormedit/core/lane_profiles.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
using namespace wormedit;
using namespace wormedit::scripting;
namespace {
auto generator_song() -> song_document_t {
    chart_document_t chart{.id = chart_id_t{2}, .measures = {{.id = measure_id_t{3}}}};
    chart.lane_layouts.push_back({.id = object_id_t{4}, .position = {measure_id_t{3}, 0}, .layout = make_bms_7k_profile().layout});
    return {.id = song_id_t{1}, .charts = {std::move(chart)}};
}
auto evaluate_generator(std::string source, lua_sandbox_limits_t limits = {}) -> diagnosed_t<generator_output_t> {
    auto song = generator_song();
    generator_recipe_t recipe{.api_version = "1", .parameters = {{"gain", "{\"num\":3,\"den\":2}"}},
        .seed = UINT64_MAX, .source = std::move(source), .chart = chart_id_t{2}};
    return run_lua_generator(song, song.charts.front(), recipe, {}, limits);
}
}
TEST_CASE("Lua generator returns deterministic keyed note and layer proposals from immutable context", "[lua-generator]") {
    const std::string source = R"(
        assert(generator.seed == '18446744073709551615')
        assert(generator.parameters.gain.num==3 and generator.parameters.gain.den==2)
        return {layers={{key='melody',name='Melody',color={r=10,g=20,b=30,a=255}}},
          notes={{key='a',measure=wormedit.measures[1].id,row=math.random(120,240),lane='1',layers={'melody'}}}}
    )";
    const auto first = evaluate_generator(source);
    const auto second = evaluate_generator(source);
    REQUIRE(first.diagnostics.empty()); REQUIRE(second.diagnostics.empty());
    REQUIRE(first.value.notes.size() == 1); REQUIRE(first.value.layers.size() == 1);
    CHECK(first.value.notes.front().value == second.value.notes.front().value);
    CHECK(first.value.notes.front().layers == std::vector<std::string>{"melody"});
}
TEST_CASE("Lua generator rejects malformed output atomically", "[lua-generator]") {
    const auto source = GENERATE("return {unknown={}}", "return {notes={[2]={key='x'}}}",
        "return {notes={{key='x',measure='3',row=1.5,lane='1'}}}",
        "return {notes={{key='x',measure='3',row=1,lane='1',kind='unsupported'}}}",
        "return {notes={{key='x',measure='3',row=1,lane='1',unexpected=true}}}",
        "return {notes={function()end}}", "return {notes={},extra=function()end}");
    const auto result = evaluate_generator(source);
    CHECK_FALSE(result.diagnostics.empty());
    CHECK(result.value.notes.empty());
}
TEST_CASE("Lua generator snapshots cannot mutate documents and execution observes cancellation", "[lua-generator]") {
    const auto result = evaluate_generator("wormedit.measures[1].id='changed'; return {notes={}}");
    CHECK_FALSE(result.diagnostics.empty());
    std::stop_source cancellation; cancellation.request_stop();
    lua_sandbox_limits_t limits; limits.cancellation = cancellation.get_token();
    CHECK_FALSE(evaluate_generator("while true do end", limits).diagnostics.empty());
}

TEST_CASE("Lua generator public execution exposes only declared MIDI assets", "[lua-generator]") {
    auto song=generator_song();
    song.midi_assets={{.id=asset_id_t{10},.ticks_per_quarter=480},{.id=asset_id_t{20},.ticks_per_quarter=480}};
    generator_recipe_t recipe{.api_version="1",.input_assets={{asset_id_t{10},std::string(64,'0')}},
        .source="assert(#wormedit.midi_assets==1 and wormedit.midi_assets[1].id=='10'); return {notes={}}",.chart=chart_id_t{2}};
    const auto result=run_lua_generator(song,song.charts.front(),recipe);
    CHECK(result.ok());
    recipe.input_assets.clear();
    recipe.source="assert(#wormedit.midi_assets==0); return {notes={}}";
    CHECK(run_lua_generator(song,song.charts.front(),recipe).ok());
}

TEST_CASE("Lua generator public execution rejects undeclared unavailable or duplicate inputs", "[lua-generator]") {
    auto song=generator_song();
    const auto duplicate=GENERATE(false,true);
    song.midi_assets={{.id=asset_id_t{10},.ticks_per_quarter=480}};
    generator_recipe_t recipe{.api_version="1",.input_assets={{asset_id_t{duplicate ? 10U : 20U},std::string(64,'0')}},
        .source="return {notes={}}",.chart=chart_id_t{2}};
    if (duplicate) recipe.input_assets.push_back(recipe.input_assets.front());
    CHECK_FALSE(run_lua_generator(song,song.charts.front(),recipe).ok());
}

TEST_CASE("Lua generator rejects address-dependent stable output keys", "[lua-generator]") {
    const auto result=evaluate_generator("local t={}; keep=t; return {layers={{key=tostring(t),name='x'}}}");
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().message.find("deterministic")!=std::string::npos);
}

TEST_CASE("Lua generator replay shares the complete build instruction budget", "[lua-generator]") {
    const auto result=evaluate_generator("local n=0; for i=1,3000 do n=n+i end; return {notes={}}",{.maximum_instructions=12000});
    REQUIRE_FALSE(result.ok()); REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().message.find("replay")!=std::string::npos);
}

TEST_CASE("Lua generator rejects parameter construction before overrunning a small budget", "[lua-generator]") {
    auto song=generator_song();
    generator_recipe_t recipe{.api_version="1",.parameters={{"many","[0,0,0,0,0,0,0,0]"}},
        .source="return {notes={}}",.chart=chart_id_t{2}};
    const auto result=run_lua_generator(song,song.charts.front(),recipe,{}, {.maximum_instructions=8});
    REQUIRE_FALSE(result.ok()); REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().message.find("preparation")!=std::string::npos);
}

TEST_CASE("Lua generator rejects hold endpoints supplied on tap proposals", "[lua-generator]") {
    const auto source=GENERATE(
        "return {notes={{key='x',measure='3',row=0,lane='1',end_measure='3',end_row=240}}}",
        "return {notes={{key='x',measure='3',row=0,lane='1',end_row=240}}}");
    CHECK_FALSE(evaluate_generator(source).ok());
}

TEST_CASE("Lua generator decoder retains full unsigned row positions", "[lua-generator]") {
    const auto result=evaluate_generator("return {notes={{key='x',measure='3',row='4294967296',lane='1'}}}");
    REQUIRE(result.ok()); REQUIRE(result.value.notes.size()==1);
    CHECK(result.value.notes.front().value.position.row==UINT64_C(4294967296));
}

TEST_CASE("Lua generator decoder rejects identities with embedded NUL suffixes", "[lua-generator]") {
    const auto result=evaluate_generator(R"(return {notes={{key='x',measure='3\0ignored',row=0,lane='1'}}})");
    CHECK_FALSE(result.ok());
}

TEST_CASE("Documented generator example produces an owned note and layer", "[lua-generator][documentation]") {
    const auto result = evaluate_generator(lua_documentation_example("generator.lua"));
    REQUIRE(result.ok());
    REQUIRE(result.value.notes.size() == 1);
    REQUIRE(result.value.layers.size() == 1);
    CHECK(result.value.notes.front().layers == std::vector<std::string>{"generated"});
    CHECK(result.value.layers.front().key == "generated");
}

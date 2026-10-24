#include "../../src/scripting/lua_curve.h"
#include <wormedit/core/rational.h>
#include <wormedit/formats/native_project_storage.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <array>

using namespace wormedit;
using namespace wormedit::scripting;
namespace {
auto chart() -> chart_document_t {
    return {.id=chart_id_t{10}, .measures={{.id=measure_id_t{11}}, {.id=measure_id_t{12}}}};
}
auto recipe(std::string source) -> sv_recipe_t {
    sv_recipe_t result{.id=recipe_id_t{20}, .name="Lua", .range={{measure_id_t{11},0},{measure_id_t{12},0}},
        .curve=sv_recipe_curve_t::lua_expression, .expression_source=std::move(source)};
    result.sampling.fixed_point_count=3;
    return result;
}
auto start(const lua_curve_backend_t& backend, const sv_recipe_t& value)
    -> std::unique_ptr<sv_recipe_script_session_t> {
    auto started=backend.start(chart(),value);
    INFO((started.diagnostics.empty() ? "success" : started.diagnostics.front().message));
    REQUIRE(started.ok()); REQUIRE(started.value);
    return std::move(started.value);
}
auto ordered_document() -> chart_document_t {
    auto document=chart();
    auto value=recipe("local n=0; return function(p,ctx) n=n+1; return n end");
    value.lua=sv_recipe_lua_t{.mode=sv_recipe_lua_mode_t::ordered,.cycles={{2},{1}}};
    document.sv_recipes.push_back(std::move(value));
    return document;
}
auto prepare(const chart_document_t& document, const lua_curve_backend_t& backend) -> sv_recipe_prepared_t {
    sv_recipe_evaluation_scope_t scope(backend);
    auto result=sv_recipe_prepared_t::from(document,document.sv_recipes.front());
    INFO((result.diagnostics.empty() ? "success" : result.diagnostics.front().message));
    REQUIRE(result.ok());
    return std::move(result.value);
}
auto compose(const chart_document_t& document, const sv_recipe_prepared_t& prepared) -> sv_recipe_compiled_output_t {
    auto result=sv_recipe_compiled_output_t::from(document,sv_recipe_output_t::sv,std::span{&prepared,1});
    REQUIRE(result.ok());
    return std::move(result.value);
}
void check_baked(const chart_document_t& document) {
    const auto baked=bake_sv_recipes_for_export(document);
    REQUIRE(baked.ok()); REQUIRE(baked.value);
    REQUIRE(baked.value->scrolls.size()>=3);
    std::vector<double> values;
    for (std::size_t i=0;i<3;++i) values.push_back(to_double(baked.value->scrolls[i].multiplier));
    CHECK(values==std::vector<double>{1,2,1});
    CHECK(baked.value->sv_recipes.empty());
}
auto seeded_run(const lua_curve_backend_t& backend, const sv_recipe_t& value) -> std::array<long double,3> {
    auto session=start(backend,value);
    std::array<long double,3> result{};
    for (std::uint32_t i=0;i<3;++i) {
        const auto evaluated=session->evaluate({.p=i*0.5L,.cycle_index=1,.emission_index=i+1});
        REQUIRE(evaluated.ok());
        result[i]=evaluated.value;
    }
    CHECK(session->finish().empty());
    return result;
}

}

TEST_CASE("Lua curve backend accepts function values wrappers and returning chunks", "[lua-curve]") {
    const auto source=GENERATE("function(p,ctx) return p+ctx.baseline end",
        "values(function(p,ctx) return p+ctx.baseline end)",
        "-- returning chunk\nreturn function(p,ctx) return p+ctx.baseline end");
    lua_curve_backend_t backend;
    auto session=start(backend,recipe(source));
    const auto value=session->evaluate({.p=0.5L,.baseline=2});
    REQUIRE(value.ok()); CHECK(value.value==2.5L);
    CHECK(session->finish().empty());
}

TEST_CASE("Lua curve module factories receive frozen parameters and snapshots", "[lua-curve]") {
    auto value=recipe(R"(return {curve=function(ctx)
        local offset=ctx.parameters.offset
        assert(ctx.interval.start_row==0 and ctx.interval.end_row==960)
        assert(#ctx.measures==2 and ctx.measures[1].row_count==960)
        assert(ctx.seed=='18446744073709551615' and ctx.seed_integer==nil)
        return function(p,ctx) return offset+p+ctx.baseline end
    end})");
    value.lua=sv_recipe_lua_t{.entry_point="curve", .parameters={{"offset",{3,2}}}, .seed=UINT64_MAX};
    lua_curve_backend_t backend;
    auto session=start(backend,value);
    const auto evaluated=session->evaluate({.p=0.5L,.baseline=2});
    REQUIRE(evaluated.ok()); CHECK(evaluated.value==4);
    CHECK(session->finish().empty());
}

TEST_CASE("Lua curve pure mode rejects retained state and ordered mode retains state across cycles", "[lua-curve]") {
    lua_curve_backend_t backend;
    auto value=recipe("local n=0; return function(p,ctx) n=n+1; return n+ctx.cycle_index*10 end");
    auto pure=start(backend,value);
    CHECK_FALSE(pure->evaluate({}).ok());
    CHECK_FALSE(pure->finish().empty());
    value.lua=sv_recipe_lua_t{.mode=sv_recipe_lua_mode_t::ordered,.cycles={{2},{1}}};
    auto ordered=start(backend,value);
    CHECK(ordered->evaluate({.cycle_index=1,.emission_index=1,.cycle_emission_index=1}).value==11);
    CHECK(ordered->evaluate({.cycle_index=1,.emission_index=2,.cycle_emission_index=2}).value==12);
    CHECK(ordered->evaluate({.cycle_index=2,.emission_index=3,.cycle_emission_index=1}).value==23);
    CHECK(ordered->finish().empty());
}

TEST_CASE("Lua curve preparation captures ordered counter emissions", "[lua-curve]") {
    const auto document=ordered_document();
    lua_curve_backend_t backend;
    const auto prepared=prepare(document,backend);
    const auto& points=prepared.samples().points;
    REQUIRE(points.size()==3);
    CHECK((std::array{points[0].value,points[1].value,points[2].value}==std::array{1.0,2.0,3.0}));
}

TEST_CASE("Lua curve emission transforms and composition do not need a runtime", "[lua-curve]") {
    const auto document=ordered_document();
    lua_curve_backend_t backend;
    const auto prepared=prepare(document,backend);
    auto scaled=document.sv_recipes.front();
    scaled.postprocess.push_back({.kind=sv_recipe_postprocess_kind_t::multiply,.value={2,1}});
    const std::array emissions{sv_recipe_emission_t{0,1},sv_recipe_emission_t{480,2},sv_recipe_emission_t{960,3}};
    const auto transformed=sv_recipe_prepared_t::from_emissions(document,scaled,emissions);
    REQUIRE(transformed.ok()); CHECK(transformed.value.value_at(0)==2);
    CHECK_FALSE(compose(document,prepared).points.empty());
}

TEST_CASE("Lua curve compile and export builds reset ordered sessions", "[lua-curve]") {
    const auto document=ordered_document();
    lua_curve_backend_t backend;
    const auto prepared=prepare(document,backend);
    const auto composed=compose(document,prepared);
    for (int build=0;build<2;++build) {
        sv_recipe_evaluation_scope_t scope(backend);
        const auto compiled=sv_recipe_compiled_output_t::from(document,sv_recipe_output_t::sv);
        REQUIRE(compiled.ok()); CHECK(compiled.value==composed);
        check_baked(document);
    }
}

TEST_CASE("Lua curve seeded ordered replay is deterministic and fresh builds reset it", "[lua-curve]") {
    auto value=recipe("function(p,ctx) return math.random(1,1000000) end");
    value.lua=sv_recipe_lua_t{.seed=42,.mode=sv_recipe_lua_mode_t::ordered,.cycles={{3}}};
    lua_curve_backend_t backend;
    CHECK(seeded_run(backend,value)==seeded_run(backend,value));
}

TEST_CASE("Lua curve diagnostics preserve source locations and reject invalid results", "[lua-curve]") {
    lua_curve_backend_t backend;
    auto value=recipe("function()\nerror('broken')\nend");
    value.lua=sv_recipe_lua_t{.source_reference="scripts/curve.lua"};
    auto session=start(backend,value);
    const auto failed=session->evaluate({});
    REQUIRE_FALSE(failed.ok()); REQUIRE_FALSE(failed.diagnostics.empty());
    CHECK(failed.diagnostics.front().message.find("scripts/curve.lua:2")!=std::string::npos);
    const auto bad=GENERATE("function() return {} end", "function() return 0/0 end", "function() return '1' end");
    auto invalid=start(backend,recipe(bad));
    CHECK_FALSE(invalid->evaluate({}).ok());
}

TEST_CASE("Lua curve replay shares the complete build instruction budget", "[lua-curve]") {
    lua_curve_backend_t backend({.maximum_instructions=4000});
    auto value=recipe("function(p) local n=0; for i=1,600 do n=n+i end; return p+n end");
    auto session=start(backend,value);
    REQUIRE(session->evaluate({.p=0.5L}).ok());
    CHECK_FALSE(session->finish().empty());
}

TEST_CASE("Lua curve backend checks frozen source hashes without reading references", "[lua-curve]") {
    lua_curve_backend_t backend;
    auto value=recipe("function() return 1 end");
    value.lua=sv_recipe_lua_t{.source_reference="missing/source.lua",
        .source_hash=native_project_content_checksum(value.expression_source)};
    auto good=start(backend,value);
    CHECK(good->evaluate({}).ok());
    value.expression_source="function() return 2 end";
    const auto bad=backend.start(chart(),value);
    CHECK_FALSE(bad.ok()); CHECK_FALSE(bad.value);
}

TEST_CASE("pure Lua one plus p matches built-in linear recipe inspection", "[lua-curve]") {
    const auto document = chart();
    const auto scripted = recipe("function(p) return 1+p end");
    auto linear = scripted;
    linear.curve = sv_recipe_curve_t::linear;
    linear.expression_source.clear();
    linear.lua.reset();
    linear.parameters.value_from = {1, 1};
    linear.parameters.value_to = {2, 1};
    lua_curve_backend_t backend;
    const sv_recipe_evaluation_scope_t scope{backend};
    const auto actual = sample_sv_recipe(document, scripted);
    const auto expected = sample_sv_recipe(document, linear);
    REQUIRE(actual);
    REQUIRE(expected);
    CHECK(actual.points == expected.points);
}

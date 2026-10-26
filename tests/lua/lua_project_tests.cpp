#include "../../src/scripting/lua_project.h"
#include "../support/temporary_directory.h"
#include <wormedit/core/lane_profiles.h>
#include <wormedit/formats/native_project_storage.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
using namespace wormedit;
using namespace wormedit::scripting;
namespace {
auto project_song() -> song_document_t {
    chart_document_t chart{.id = chart_id_t{2}, .measures = {{.id = measure_id_t{3}}}};
    chart.lane_layouts.push_back({.id = object_id_t{4}, .position = {measure_id_t{3}, 0},
        .layout = make_bms_7k_profile().layout});
    return {.id = song_id_t{1}, .charts = {std::move(chart)}};
}
void source_file(const std::filesystem::path& root, const char* path, std::string_view source) {
    const auto file = root / path;
    std::filesystem::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary);
    output << source;
    REQUIRE(output.good());
}
auto project_recipe() -> generator_recipe_t {
    return {.api_version = "1", .source_path = "project.lua", .seed = 42, .chart = chart_id_t{2}};
}
auto freeze(const song_document_t& song, const std::filesystem::path& root,
    lua_sandbox_limits_t limits = {}) -> diagnosed_t<lua_project_source_t> {
    return lua_project_source_t::from(song, song.charts.front(), project_recipe(), {}, root, limits);
}
auto execute(const song_document_t& song, const lua_project_source_t& frozen) -> diagnosed_t<generator_output_t> {
    auto recipe = project_recipe();
    recipe.source = frozen.source;
    recipe.source_hash = native_project_content_checksum(recipe.source);
    return run_lua_generator(song, song.charts.front(), recipe);
}
void check_frozen_assets(const song_document_t& song, const lua_project_source_t& frozen) {
    CHECK(frozen.source.find(native_project_content_checksum("audio bytes")) != std::string::npos);
    CHECK(execute(song, frozen).ok());
}
}
TEST_CASE("project Lua freezes ordered includes relative helpers and shared module results", "[lua-project]") {
    auto root = test::temporary_directory_t::from(); REQUIRE(root);
    source_file(root->path(), "project.lua", "worm_project 'Song'; include 'parts/first.lua'; include 'parts/second.lua'; include 'parts/first.lua'");
    source_file(root->path(), "helpers.lua", "return function() return 120 end");
    source_file(root->path(), "parts/first.lua", "local row=include '../helpers.lua'; order=1; return {notes={{key='a',measure='3',row=row(),lane='101'}}}");
    source_file(root->path(), "parts/second.lua", "assert(order==1 and require==nil and _wormedit_read_project_file==nil); return {layers={{key='b',name='B'}}}");
    const auto song = project_song();
    const auto frozen = freeze(song, root->path());
    INFO((frozen.diagnostics.empty() ? "success" : frozen.diagnostics.front().message));
    REQUIRE(frozen.ok());
    std::filesystem::remove_all(root->path());
    const auto output = execute(song, frozen.value);
    REQUIRE(output.ok()); REQUIRE(output.value.notes.size() == 1); REQUIRE(output.value.layers.size() == 1);
    CHECK(output.value.notes.front().value.position.row == 120);
}
TEST_CASE("project Lua declares and hashes assets without importing or changing documents", "[lua-project]") {
    auto root = test::temporary_directory_t::from(); REQUIRE(root);
    source_file(root->path(), "project.lua", R"(worm_project 'Song'
        audio {id='music',file='song.ogg'}
        image {id='cover',file='cover.png'}
        midi {id='score',file='score.mid'}
        assert(project.assets.music.asset_id=='20' and project.assets.cover.asset_id==nil)
        assert(project.assets.score.asset_id=='21' and #project.assets.music.content_hash==64)
        return {notes={}})");
    source_file(root->path(), "song.ogg", "audio bytes");
    source_file(root->path(), "cover.png", "image bytes");
    source_file(root->path(), "score.mid", "MIDI bytes");
    auto song = project_song();
    song.audio_assets.push_back({.id = asset_id_t{20}, .source_path = "song.ogg"});
    song.midi_assets.push_back({.id = asset_id_t{21}, .source_path = "score.mid"});
    const auto frozen = freeze(song, root->path());
    REQUIRE(frozen.ok());
    check_frozen_assets(song, frozen.value);
    source_file(root->path(), "song.ogg", "changed audio bytes");
    const auto changed = freeze(song, root->path()); REQUIRE(changed.ok());
    CHECK(frozen.value.source != changed.value.source);
    CHECK(song.visual_assets.empty());
}
TEST_CASE("project Lua rejects include cycles missing paths escapes and malformed declarations", "[lua-project]") {
    const auto source = GENERATE(
        "include 'project.lua'", "include 'absent.lua'", "include '../outside.lua'",
        "include '/etc/passwd'", "audio {id='a',file='absent.ogg'}",
        "worm_project 'twice'", "return {notes={[2]={}}}", "return {notes={},unexpected=true}",
        "pcall(function() include 'absent.lua' end)",
        "error=function()end; return {notes={[2]={}}}",
        "error=function()end; include 'project.lua'");
    auto root = test::temporary_directory_t::from(); REQUIRE(root);
    source_file(root->path(), "project.lua", std::string{"worm_project 'Song'; "} + source);
    const auto frozen = freeze(project_song(), root->path());
    CHECK_FALSE(frozen.ok());
}
TEST_CASE("project Lua rejects symlink escapes and preserves internal aliases in frozen execution", "[lua-project]") {
    auto root = test::temporary_directory_t::from(); REQUIRE(root);
    auto outside = test::temporary_directory_t::from(); REQUIRE(outside);
    source_file(outside->path(), "outside.lua", "return {notes={}}");
    source_file(root->path(), "module.lua", "return {notes={}}");
    std::error_code error;
    std::filesystem::create_symlink(root->path() / "module.lua", root->path() / "alias.lua", error);
    if (error) { SKIP("Symlink creation is unavailable on this host"); }
    source_file(root->path(), "project.lua", "worm_project 'Song'; include 'alias.lua'");
    const auto song = project_song();
    const auto frozen = freeze(song, root->path()); REQUIRE(frozen.ok());
    CHECK(execute(song, frozen.value).ok());
    std::filesystem::create_symlink(outside->path() / "outside.lua", root->path() / "escape.lua");
    source_file(root->path(), "project.lua", "worm_project 'Song'; include 'escape.lua'");
    CHECK_FALSE(freeze(song, root->path()).ok());
}
TEST_CASE("project Lua bounds source IO VM execution and cancellation", "[lua-project]") {
    auto root = test::temporary_directory_t::from(); REQUIRE(root);
    const auto song = project_song();
    source_file(root->path(), "project.lua", "worm_project 'Song'; while true do end");
    CHECK_FALSE(freeze(song, root->path(), {.maximum_instructions = 2000}).ok());
    source_file(root->path(), "project.lua", std::string(maximum_generator_source_bytes + 1, ' '));
    CHECK_FALSE(freeze(song, root->path()).ok());
    std::stop_source stop; stop.request_stop();
    CHECK_FALSE(freeze(song, root->path(), {.cancellation = stop.get_token()}).ok());
}

TEST_CASE("Documented project entry point freezes and executes its generator module", "[lua-project][documentation]") {
    const auto song = project_song();
    const auto frozen = freeze(song, std::filesystem::path(WORMEDIT_LUA_EXAMPLE_DIR));
    INFO((frozen.diagnostics.empty() ? "success" : frozen.diagnostics.front().message));
    REQUIRE(frozen.ok());
    const auto output = execute(song, frozen.value);
    REQUIRE(output.ok());
    REQUIRE(output.value.notes.size() == 1);
    CHECK(output.value.notes.front().layers == std::vector<std::string>{"generated"});
}

TEST_CASE("project Lua helper closures retain their defining file for later includes", "[lua-project]") {
    auto root = test::temporary_directory_t::from(); REQUIRE(root);
    source_file(root->path(), "project.lua", "worm_project 'Song'; local helper=include 'lib/helper.lua'; return helper()");
    source_file(root->path(), "lib/helper.lua", "return function() return include 'notes.lua' end");
    source_file(root->path(), "lib/notes.lua", "return {notes={{key='a',measure='3',row=120,lane='101'}}}");
    const auto song = project_song();
    const auto frozen = freeze(song, root->path());
    INFO((frozen.diagnostics.empty() ? "success" : frozen.diagnostics.front().message));
    REQUIRE(frozen.ok());
    const auto output = execute(song, frozen.value);
    REQUIRE(output.ok()); REQUIRE(output.value.notes.size() == 1);
    CHECK(output.value.notes.front().value.position.row == 120);
}

TEST_CASE("project Lua requires its declaration even if a module writes project metadata", "[lua-project]") {
    auto root = test::temporary_directory_t::from(); REQUIRE(root);
    source_file(root->path(), "project.lua", "project.name='bypass'; return {notes={}}");
    CHECK_FALSE(freeze(project_song(), root->path()).ok());
}

TEST_CASE("project Lua collects SV proposals from included modules under frozen replay", "[lua-project][sv]") {
    auto root = test::temporary_directory_t::from(); REQUIRE(root);
    source_file(root->path(), "project.lua", "worm_project 'SV'; include 'sv.lua'; include 'sv.lua'");
    source_file(root->path(), "sv.lua", R"(
local m=wormedit.measures[1].id
return {sv_points={sv_point{key='base',at=at_measure(m,0),value=2}},
        sv_spans={sv_span{key='rise',from=at_measure(m,0),to=at_measure(m,480),value_from=1,value_to=2}}}
)");
    const auto song = project_song();
    const auto frozen = freeze(song, root->path());
    INFO((frozen.diagnostics.empty() ? "success" : frozen.diagnostics.front().message));
    REQUIRE(frozen.ok());
    std::filesystem::remove_all(root->path());
    const auto output = execute(song, frozen.value);
    INFO((output.diagnostics.empty() ? "success" : output.diagnostics.front().message));
    REQUIRE(output.ok());
    CHECK(output.value.scrolls.size() == 1);
    CHECK(output.value.sv_recipes.size() == 1);
}

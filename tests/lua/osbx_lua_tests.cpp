#include "../../src/scripting/lua_generator.h"
#include "../../src/scripting/lua_project.h"
#include "../../src/scripting/osbx_lua.h"
#include "../support/temporary_directory.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <fstream>
#include <iterator>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/formats/native_project_storage.h>
using namespace wormedit;
using namespace wormedit::scripting;

namespace {
void check_lua_clip_bindings(const osbx_document_t& scene) {
    REQUIRE(scene.clips.size() == 1);
    CHECK((scene.clips[0].parameters[0].default_value == std::vector<double>{4,5}));
    REQUIRE(scene.nodes.size() == 2);
    CHECK((scene.nodes[1].uses[0].parameters[0].value == std::vector<double>{8,9}));
}
}
TEST_CASE("OSBX Lua clips retain typed definitions and independent instance clocks", "[lua][osbx][clips]") {
    const auto result = run_osbx_lua(R"lua(storyboard('scene')
clip {id='motion',parameters={{name='offset',default={4,5}}},
  ops={{id='motion-op',kind='translate',track={parameter='offset'}}}}
image {id='image',file='image.png'}
sprite('a'); image('image'); use_clip {id='use-a',clip='motion',start=10,time_scale=0.5}
sprite('b'); image('image'); use_clip {id='use-b',clip='motion',start=20,parameters={{name='offset',value={8,9}}}}
)lua", "@clips.osbx.lua");
    REQUIRE(result.ok());
    check_lua_clip_bindings(result.value);
    CHECK(result.value.nodes[0].uses[0].start_ms == 10);
    CHECK(result.value.nodes[0].uses[0].time_scale == 0.5);
}
namespace {
auto example() -> std::string {
    std::ifstream input(std::filesystem::path(WORMEDIT_LUA_EXAMPLE_DIR) / "main.osbx.lua");
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>{input}, {}};
}
auto golden() -> osbx_document_t {
    osbx_node_t group{.id = "title_group",
                      .kind = osbx_node_kind_t::group,
                      .layer = osbx_layer_t::foreground,
                      .provenance = {.file = "main.osbx.lua", .line = 7}};
    group.ops = {{.id = "title_group/op/1",
                  .kind = osbx_transform_kind_t::translate,
                  .track = {.default_value = {320, 240}}},
                 {.id = "title_group/op/2",
                  .kind = osbx_transform_kind_t::rotate,
                  .track = {.default_value = {0},
                            .keys = {{.id = "title_group/op/2/key/1", .time_ms = 0, .value = {0}},
                                     {.id = "title_group/op/2/key/2",
                                      .time_ms = 2000,
                                      .value = {360},
                                      .easing = osbx_osb_easing_t{17}}}}}};
    osbx_node_t sprite{.id = "title",
                       .kind = osbx_node_kind_t::sprite,
                       .parent = "title_group",
                       .layer = osbx_layer_t::foreground,
                       .image = "logo",
                       .provenance = {.file = "main.osbx.lua", .line = 15}};
    sprite.ops = {
        {.id = "title/op/1",
         .kind = osbx_transform_kind_t::matrix,
         .track = {
             .default_value = {1, 0, 0, 1, 0, 0},
             .keys = {
                 {.id = "title/op/1/key/1", .time_ms = 0, .value = {1, 0, 0, 1, 0, 0}},
                 {.id = "title/op/1/key/2", .time_ms = 2000, .value = {1, 0.2, 0, 1, 0, 0}}}}}};
    sprite.tracks.opacity.keys = {{.id = "title/opacity/key/1", .time_ms = 0, .value = {0}},
                                  {.id = "title/opacity/key/2", .time_ms = 250, .value = {1}}};
    return {.images = {{"logo", "sb/logo.png"}}, .nodes = {std::move(group), std::move(sprite)}};
}
auto project_song() -> song_document_t {
    chart_document_t chart{.id = chart_id_t{2}, .measures = {{.id = measure_id_t{3}}}};
    chart.lane_layouts.push_back({.id = object_id_t{4},
                                  .position = {measure_id_t{3}, 0},
                                  .layout = make_bms_7k_profile().layout});
    return {.id = song_id_t{1}, .charts = {std::move(chart)}};
}
void source_file(const std::filesystem::path& root, std::string_view name,
                 std::string_view source) {
    const auto path = root / name;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path);
    output << source;
    REQUIRE(output.good());
}
auto freeze(const song_document_t& song, const std::filesystem::path& root)
    -> diagnosed_t<lua_project_source_t> {
    generator_recipe_t recipe{
        .api_version = "1", .source_path = "project.lua", .chart = chart_id_t{2}};
    return lua_project_source_t::from(song, song.charts.front(), recipe, {}, root);
}
void check_failure(std::string_view source, std::string_view message) {
    const auto result = run_osbx_lua(source, "@bad.osbx.lua");
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.value.nodes.empty());
    INFO(result.diagnostics.front().message);
    CHECK(result.diagnostics.front().message.find(message) != std::string::npos);
}
auto checked_scene(const diagnosed_t<osbx_document_t>& result) -> const osbx_document_t& {
    INFO((result.diagnostics.empty() ? "success" : result.diagnostics.front().message));
    REQUIRE(result.ok());
    return result.value;
}
void check_stack(const osbx_node_t& node) {
    CHECK(node.order == -3);
    REQUIRE(node.ops.size() == 3);
    CHECK(node.ops[0].kind == osbx_transform_kind_t::translate);
    CHECK(node.ops[1].kind == osbx_transform_kind_t::pivot);
    CHECK(node.ops[2].kind == osbx_transform_kind_t::skew);
}
void check_pivot(const osbx_transform_op_t& op) {
    REQUIRE(op.children.size() == 2);
    CHECK(op.children[0].kind == osbx_transform_kind_t::rotate);
    CHECK(op.children[1].track.default_value == std::vector<double>{2, 3});
}
void check_properties(const osbx_node_t& node) {
    REQUIRE(node.tracks.opacity.keys.size() == 2);
    CHECK(node.tracks.opacity.keys[0].id == "fade");
    CHECK(std::holds_alternative<osbx_step_easing_t>(node.tracks.opacity.keys[0].easing));
    CHECK(node.tracks.color.default_value == std::vector<double>{0.1, 0.2, 0.3, 0.4});
    CHECK(node.tracks.blend.default_value == osbx_blend_t::additive);
}
void check_frozen_scene(const osbx_document_t& scene) {
    REQUIRE(scene.nodes.size() == 2);
    REQUIRE(scene.images.size() == 1);
    CHECK(scene.images[0].file == "parts/sb/logo.png");
    CHECK(scene.nodes[0].provenance == osbx_provenance_t{.file = "parts/main.osbx.lua", .line = 7});
    CHECK(scene.nodes[1].provenance ==
          osbx_provenance_t{.file = "parts/main.osbx.lua", .line = 15});
}
} // namespace
TEST_CASE("OSBX spec declaration example decodes every scene field", "[osbx-lua]") {
    const auto result = run_osbx_lua(example(), "@main.osbx.lua");
    const auto& actual = checked_scene(result);
    const auto expected = golden();
    CHECK(actual.api_version == expected.api_version);
    CHECK(actual.canvas == expected.canvas);
    CHECK(actual.images == expected.images);
    REQUIRE(actual.nodes.size() == 2);
    CHECK(actual.nodes[0] == expected.nodes[0]);
    CHECK(actual.nodes[1] == expected.nodes[1]);
}
TEST_CASE("OSBX declaration calls reject malformed arguments immediately", "[osbx-lua]") {
    const auto call = GENERATE(
        "storyboard {}", "api_version {}", "canvas {width=1}", "canvas {width=1,height=1,extra=2}",
        "image {}", "image {id='i',file=1}", "group {}", "sprite {}", "layer {}", "order(0.5)",
        "parent {}", "image(false)", "origin {}", "transform.translate {1}",
        "transform.rotate {0,1}", "transform.scale(true)", "transform.skew 'x'",
        "transform.matrix {1,0}", "transform.pivot {1}", "transform.pivot {1,2,ops={false}}",
        "opacity(true)", "color {1}", "blend {false}", "key {}", "key {0.5,value=1}",
        "key {0,value={false}}", "key {0,value=1,other=2}", "key {0,value=1,easing=false}",
        "key {0,value=1,id={}}", "transform.translate {[2]=1}", "opacity {key{0,value={1,2}}}");
    CAPTURE(call);
    // Boolean descriptors and empty discrete tracks are now valid; malformed
    // components still cannot be hidden with pcall to publish a partial scene.
    const auto source =
        std::string{"storyboard 'Main'; group 'g'; pcall(function() "} + call + " end)";
    check_failure(source, "bad.osbx.lua:1:");
}
TEST_CASE("OSBX object settings require a selected node", "[osbx-lua]") {
    const auto call =
        GENERATE("layer 'foreground'", "order(1)", "parent 'g'", "image 'logo'", "origin 'centre'",
                 "transform.translate {1,2}", "transform.rotate {0}", "transform.scale {1,1}",
                 "transform.skew {0,0}", "transform.matrix {1,0,0,1,0,0}", "transform.pivot {1,2}",
                 "opacity(1)", "color {1,1,1,1}", "blend 'normal'");
    check_failure(std::string{"storyboard 'Main'; "} + call, "Declare group or sprite");
}
TEST_CASE("OSBX declarations reject invalid graphs and tracks with source diagnostics",
          "[osbx-lua]") {
    const auto source = GENERATE(
        "storyboard 'Main'; api_version '9'", "storyboard 'Main'; canvas {width=0,height=10}",
        "storyboard 'Main'; image {id='i',file='../escape.png'}", "storyboard 'Main'; sprite 's'",
        "storyboard 'Main'; group 'g'; parent 'absent'", "storyboard 'Main'; group 'g'; parent 'g'",
        "storyboard 'Main'; group 'g'; layer 'unknown'",
        "storyboard 'Main'; group 'g'; origin 'unknown'",
        "storyboard 'Main'; group 'g'; blend 'unknown'",
        "storyboard 'Main'; group 'g'; opacity(-1)", "storyboard 'Main'; group 'g'; opacity (0/0)",
        "storyboard 'Main'; group 'g'; color {1,1,1,0/0}",
        "storyboard 'Main'; group 'g'; opacity {key{2,value=1},key{1,value=0}}",
        "storyboard 'Main'; group 'g'; opacity {key{0,value=1,easing='unknown'}}",
        "storyboard 'Main'; group 'g'; opacity {key{0,value=1,easing={0,1}}}",
        "storyboard 'Main'; group 'g'; opacity {key{0,value=1,easing=35}}",
        "storyboard 'Main'; group 'g'; opacity {key{0,value=1,easing={-1,0,1,1}}}");
    check_failure(source, "bad.osbx.lua:1:");
}
TEST_CASE("OSBX duplicate IDs identify both original declaration locations", "[osbx-lua]") {
    const auto result =
        run_osbx_lua("storyboard 'Main'\ngroup 'same'\nsprite 'same'", "@duplicate.osbx.lua");
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().message.find("duplicate.osbx.lua:2") != std::string::npos);
    CHECK(result.diagnostics.front().message.find("duplicate.osbx.lua:3") != std::string::npos);
    check_failure(
        "storyboard 'Main'; group 'g'; opacity {key{0,id='same',value=0},key{1,id='same',value=1}}",
        "Duplicate OSBX ID");
}
TEST_CASE("OSBX transforms preserve stack order pivot children defaults and authored keys",
          "[osbx-lua]") {
    const auto result = run_osbx_lua(R"(storyboard 'Main'
      group 'first'
      order(-3)
      transform.translate {1,2}
      transform.pivot {3,4,ops={transform.rotate {45},transform.scale {2,3}}}
      transform.skew {5,6}
      opacity {key{-1,id='fade',value=0,easing='step'},key{1,value=1,easing={0,0,1,1}}}
      color {0.1,0.2,0.3,0.4}
      blend 'additive'
      group 'second'
      transform.scale {1,1}
    )");
    const auto& scene = checked_scene(result);
    REQUIRE(scene.nodes.size() == 2);
    check_stack(scene.nodes[0]);
    check_pivot(scene.nodes[0].ops[1]);
    check_properties(scene.nodes[0]);
    CHECK(scene.nodes[1].ops.size() == 1);
    CHECK(scene.nodes[1].tracks.opacity.keys.empty());
}
TEST_CASE("OSBX seed context replay and budgets reject partial output", "[osbx-lua]") {
    const std::string source =
        "storyboard 'Main'; group 'g'; transform.translate {math.random(),generator.x}";
    const lua_sandbox_context_t context{lua_sandbox_node_t::from("x", std::int64_t{12})};
    const auto first = run_osbx_lua(source, "@seed.lua", {.seed = 42}, context);
    const auto second = run_osbx_lua(source, "@seed.lua", {.seed = 42}, context);
    const auto other = run_osbx_lua(source, "@seed.lua", {.seed = 43}, context);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(other.ok());
    CHECK(first.value == second.value);
    CHECK(first.value != other.value);
    check_failure("local t={}; keep=t; storyboard 'Main'; group(tostring(t))", "deterministic");
}
TEST_CASE("OSBX execution limits reject partial output", "[osbx-lua]") {
    const auto budget = run_osbx_lua("storyboard 'Main'; group 'g'; while true do end",
                                     "@budget.lua", {.maximum_instructions = 20000});
    CHECK_FALSE(budget.ok());
    CHECK(budget.value.nodes.empty());
    const auto small_result = run_osbx_lua(example(), "@result.lua", {.maximum_result_bytes = 20});
    CHECK_FALSE(small_result.ok());
    CHECK(small_result.value.nodes.empty());
    std::stop_source stop;
    stop.request_stop();
    CHECK_FALSE(
        run_osbx_lua("storyboard 'Main'", "@cancel.lua", {.cancellation = stop.get_token()}).ok());
}
TEST_CASE("OSBX caller provenance capability stays private in every sandbox consumer",
          "[osbx-lua]") {
    constexpr auto probe = "assert(debug==nil and caller_location==nil and "
                           "declaration_location==nil and _wormedit_caller_location==nil)";
    auto ordinary = lua_sandbox_t::from();
    REQUIRE(ordinary);
    CHECK(ordinary->evaluate(probe));
    CHECK(ordinary->evaluate_context(std::string{probe} + "; return {}"));
    const auto output =
        run_osbx_lua(std::string{probe} + "; storyboard 'Main'\ngroup 'g'", "@private.osbx.lua");
    REQUIRE(output.ok());
    REQUIRE(output.value.nodes.size() == 1);
    CHECK(output.value.nodes[0].provenance ==
          osbx_provenance_t{.file = "private.osbx.lua", .line = 2});
}
TEST_CASE(
    "OSBX project bundles preserve original modules canonical images and hashes without live files",
    "[osbx-lua]") {
    auto root = test::temporary_directory_t::from();
    REQUIRE(root);
    source_file(root->path(), "project.lua", "worm_project 'Song'; include 'parts/main.osbx.lua'");
    source_file(root->path(), "parts/main.osbx.lua", example());
    source_file(root->path(), "parts/sb/logo.png", "image bytes");
    const auto song = project_song();
    const auto frozen = freeze(song, root->path());
    INFO((frozen.diagnostics.empty() ? "success" : frozen.diagnostics.front().message));
    REQUIRE(frozen.ok());
    CHECK(frozen.value.source.find(native_project_content_checksum("image bytes")) !=
          std::string::npos);
    std::filesystem::remove_all(root->path());
    const auto scene = run_osbx_lua(frozen.value.source, "@project.lua");
    check_frozen_scene(checked_scene(scene));
}
TEST_CASE("OSBX discovery replay compares the full typed scene before freezing", "[osbx-lua]") {
    auto root = test::temporary_directory_t::from();
    REQUIRE(root);
    source_file(root->path(), "project.lua", "worm_project 'Song'; include 'main.osbx.lua'");
    source_file(root->path(), "main.osbx.lua",
                "local t={}; keep=t; storyboard 'Main'; group(tostring(t))");
    const auto frozen = freeze(project_song(), root->path());
    REQUIRE_FALSE(frozen.ok());
    REQUIRE_FALSE(frozen.diagnostics.empty());
    CHECK(frozen.diagnostics.front().message.find("deterministic") != std::string::npos);
}
TEST_CASE("OSBX ordinary generator carries standalone and combined scenes",
          "[osbx-lua]") {
    const auto tail = GENERATE("", "; return {}", "; return {notes={}}", "; difficulty 'Easy'; measure{}",
                               "; return {layers={{key='x',name='x'}}}");
    const auto song = project_song();
    generator_recipe_t recipe{.api_version = "1",
                              .source = std::string{"storyboard 'Main'; group 'g'"} + tail,
                              .chart = chart_id_t{2}};
    const auto output = run_lua_generator(song, song.charts.front(), recipe);
    INFO((output.diagnostics.empty() ? "success" : output.diagnostics.front().message));
    REQUIRE(output.ok());
    REQUIRE(output.value.storyboards.size() == 1);
    CHECK(output.value.storyboards.front().key == "Main");
    CHECK(output.value.storyboards.front().value.nodes.front().id == "g");
}
TEST_CASE("OSBX ordinary generator carries frozen scenes", "[osbx-lua]") {
    const auto song = project_song();
    generator_recipe_t recipe{.api_version = "1", .chart = chart_id_t{2}};
    auto root = test::temporary_directory_t::from();
    REQUIRE(root);
    source_file(root->path(), "project.lua", "worm_project 'Song'; storyboard 'Main'; group 'g'");
    const auto frozen = freeze(song, root->path());
    REQUIRE(frozen.ok());
    recipe.source = frozen.value.source;
    const auto replay = run_lua_generator(song, song.charts.front(), recipe);
    REQUIRE(replay.ok());
    REQUIRE(replay.value.storyboards.size() == 1);
    CHECK(replay.value.storyboards.front().key == "Main");
}
TEST_CASE("OSBX frozen include collisions retain image and node source locations", "[osbx-lua]") {
    auto root = test::temporary_directory_t::from();
    REQUIRE(root);
    source_file(root->path(), "project.lua",
                "worm_project 'Song'; include 'a.osbx.lua'; include 'b.osbx.lua'");
    source_file(root->path(), "a.osbx.lua", "storyboard 'Main'\nimage{id='same',file='logo.png'}");
    source_file(root->path(), "b.osbx.lua", "-- node declaration\ngroup 'same'");
    source_file(root->path(), "logo.png", "image bytes");
    const auto result = freeze(project_song(), root->path());
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().message.find("a.osbx.lua:2") != std::string::npos);
    CHECK(result.diagnostics.front().message.find("b.osbx.lua:2") != std::string::npos);
}
TEST_CASE("OSBX key validation diagnostics retain the authored key line", "[osbx-lua]") {
    check_failure("storyboard 'Main'\ngroup 'g'\nopacity {key{2,value=1},\nkey{1,value=0}}",
                  "bad.osbx.lua:4:");
}
TEST_CASE("OSBX included scenes reuse shared project images declared before the storyboard",
          "[osbx-lua]") {
    auto root = test::temporary_directory_t::from();
    REQUIRE(root);
    source_file(root->path(), "project.lua",
                "worm_project 'Song'\nimage{id='shared',file='assets/logo.png'}\ninclude "
                "'parts/main.osbx.lua'");
    source_file(root->path(), "parts/main.osbx.lua",
                "storyboard 'Main'\nsprite 's'\nimage 'shared'");
    source_file(root->path(), "assets/logo.png", "image bytes");
    const auto frozen = freeze(project_song(), root->path());
    REQUIRE(frozen.ok());
    const auto result = run_osbx_lua(frozen.value.source, "@project.lua");
    const auto& scene = checked_scene(result);
    CHECK(scene.images == std::vector<osbx_image_t>{{"shared", "assets/logo.png"}});
    REQUIRE(scene.nodes.size() == 1);
    CHECK(scene.nodes[0].image == "shared");
    CHECK(scene.nodes[0].provenance == osbx_provenance_t{.file = "parts/main.osbx.lua", .line = 2});
}

TEST_CASE("OSBX generator rejects duplicate keys across included modules", "[osbx-lua][osbx-generator]") {
    auto root = test::temporary_directory_t::from();
    REQUIRE(root);
    source_file(root->path(), "project.lua", "worm_project 'Song'; include 'a.osbx.lua'; include 'b.osbx.lua'");
    source_file(root->path(), "a.osbx.lua", "storyboard 'same'; group 'one'");
    source_file(root->path(), "b.osbx.lua", "storyboard 'same'; group 'two'");
    const auto frozen = freeze(project_song(), root->path());
    REQUIRE_FALSE(frozen.ok());
    REQUIRE_FALSE(frozen.diagnostics.empty());
    CHECK(frozen.diagnostics.front().code == diagnostic_code_t::invalid_binding);
    CHECK(frozen.diagnostics.front().message.find("Duplicate storyboard key: same") != std::string::npos);
}

TEST_CASE("OSBX standalone host rejects multiple scenes while generator retains their keys", "[osbx-lua][osbx-generator]") {
    const std::string source = "storyboard 'alpha'; group 'root'; storyboard 'beta'; group 'root'";
    const auto song = project_song();
    const generator_recipe_t recipe{.api_version = "1", .source = source, .chart = chart_id_t{2}};
    const auto output = run_lua_generator(song, song.charts.front(), recipe);
    REQUIRE(output.ok());
    REQUIRE(output.value.storyboards.size() == 2);
    CHECK(output.value.storyboards[0].key == "alpha");
    CHECK(output.value.storyboards[1].key == "beta");
    CHECK(output.value.storyboards[0].value == output.value.storyboards[1].value);
    check_failure(source, "Declare exactly one storyboard");
}

TEST_CASE("OSBX generator replay compares storyboard declaration keys even when scenes match", "[osbx-lua][osbx-generator]") {
    const auto song = project_song();
    const generator_recipe_t recipe{.api_version = "1",
        .source = "local t={}; keep=t; storyboard(tostring(t)); group 'root'", .chart = chart_id_t{2}};
    const auto output = run_lua_generator(song, song.charts.front(), recipe);
    REQUIRE_FALSE(output.ok());
    REQUIRE_FALSE(output.diagnostics.empty());
    CHECK(output.diagnostics.front().message.find("deterministic") != std::string::npos);
}

namespace {
void check_simplified_generator(const std::string& source) {
    const auto song = project_song();
    const generator_recipe_t recipe{.api_version = "1", .source = source, .chart = chart_id_t{2}};
    const auto result = run_lua_generator(song, song.charts.front(), recipe);
    INFO((result.diagnostics.empty() ? "success" : result.diagnostics.front().message));
    REQUIRE(result.ok());
    REQUIRE(result.value.storyboards.size() == 1);
    CHECK(result.value.storyboards[0].value.nodes[0].ops[0].track.keys.size() == 2);
}
} // namespace
TEST_CASE("OSBX key simplification shares native behavior in both hosts", "[osbx-lua][simplify]") {
    const auto combined = GENERATE(false, true);
    const std::string source = R"lua(
storyboard 'Main'; group 'g'
local input={key{0,value={0,0}},key{100,value={1,2}},key{200,value={2,4}}}
local output=keys.simplify{keys=input,tolerance=0.001,dims=2}
assert(#output==2 and output[1]==input[1] and output[2]==input[3])
assert(_G.native_simplify==nil)
transform.translate(output)
)lua";
    if (combined)
        check_simplified_generator(source);
    else {
        const auto result = run_osbx_lua(source);
        CHECK(checked_scene(result).nodes[0].ops[0].track.keys.size() == 2);
    }
}
TEST_CASE("OSBX key simplification rejects nonlinear invalid and over-budget input",
          "[osbx-lua][simplify]") {
    check_failure("storyboard 'Main'; group 'g'; "
                  "keys.simplify{keys={key{0,value=0,easing='step'}},tolerance=1}",
                  "linear");
    check_failure("storyboard 'Main'; group 'g'; "
                  "keys.simplify{keys={key{1,value=0},key{0,value=0}},tolerance=1}",
                  "Invalid keys");
    check_failure("storyboard 'Main'; group 'g'; keys.simplify{keys={},tolerance=-1}",
                  "nonnegative");
    check_failure(
        "storyboard 'Main'; group 'g'; keys.simplify{keys={key{0,value=0}},tolerance=1,dims=2}",
        "dimensions");
    auto sandbox = lua_sandbox_t::from({.maximum_instructions = 5000});
    REQUIRE(sandbox);
    // Build input in Lua under the shared budget, then exhaust it in native work.
    const auto adapter = std::string(osbx_lua_prelude()) + "\nreturn function() return {} end";
    auto result = sandbox->evaluate_context(R"lua(
local input={}
for i=1,50 do input[i]=key{i,value=i%2} end
while true do keys.simplify{keys=input,tolerance=0.01} end
)lua",
                                            "budget", adapter);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == failure_code_t::capacity_exceeded);
    CHECK_FALSE(sandbox->is_open());
}

namespace {
// A trusted test adapter isolates native work from descriptor construction.
constexpr std::string_view native_simplify_adapter=R"lua(
local _,simplify=...
native_test=simplify
return function() return {} end
)lua";
auto native_simplify_source(bool jagged) -> std::string {
    std::string source="native_test({";
    for (int i=0;i<128;++i)
        source+=std::to_string(i)+","+std::to_string(jagged ? i%2 : i)+",";
    return source+"},1,0.1)";
}
} // namespace
TEST_CASE("OSBX native simplifier charges RDP work to sandbox", "[osbx-lua][simplify]") {
    auto linear=lua_sandbox_t::from({.maximum_instructions=4000});
    auto jagged=lua_sandbox_t::from({.maximum_instructions=4000});
    REQUIRE(linear);
    REQUIRE(jagged);
    CHECK(linear->evaluate_context(native_simplify_source(false),"native",native_simplify_adapter));
    const auto failed=jagged->evaluate_context(native_simplify_source(true),"native",native_simplify_adapter);
    REQUIRE_FALSE(failed);
    CHECK(failed.error().code==failure_code_t::capacity_exceeded);
}
TEST_CASE("OSBX native simplifier charges scratch memory to sandbox", "[osbx-lua][simplify]") {
    auto small=lua_sandbox_t::from({.maximum_memory_bytes=20'000});
    REQUIRE(small);
    const auto memory=small->evaluate_context(native_simplify_source(false),"native",native_simplify_adapter);
    REQUIRE_FALSE(memory);
    CHECK(memory.error().code==failure_code_t::out_of_memory);
}

TEST_CASE("OSBX Lua declares bounded loop and trigger key programs", "[lua][osbx][program]") {
    const auto result = run_osbx_lua(R"lua(storyboard('scene')
image {id='image',file='image.png'}
sprite('sprite'); image('image')
opacity {loop {start=100,count=3,keys={key {0,value=0},key {10,value=1}}},
 trigger {name='Passing',from=0,to=200,keys={key {0,value=0},key {10,value=1}}}}
)lua");
    REQUIRE(result.ok());
    REQUIRE(result.value.nodes[0].tracks.opacity.program.size() == 2);
    CHECK(std::get<osbx_loop_t<>>(result.value.nodes[0].tracks.opacity.program[0].data).count == 3);
    CHECK(std::get<osbx_trigger_t<>>(result.value.nodes[0].tracks.opacity.program[1].data).trigger.name.value == "Passing");
}

TEST_CASE("OSBX Lua refuses malformed temporal declarations through the shared model", "[lua][osbx][program]") {
    const auto declaration = GENERATE(
        "loop {count=0,keys={key {0,value=1}}}",
        "loop {count=1,keys={}}",
        "loop {count=1,keys={key {-1,value=1}}}",
        "loop {count=1,keys={loop {count=1,keys={key {0,value=1}}}}}",
        "trigger {name='passing',keys={key {0,value=1}}}",
        "trigger {name='Passing',from=10,to=0,keys={key {0,value=1}}}",
        "trigger {name='Passing',group=1,keys={key {0,value=1}}}");
    const auto source = std::string{"storyboard('scene'); image{id='i',file='i.png'}; sprite('s'); image('i'); opacity {"} + declaration + "}";
    const auto result = run_osbx_lua(source, "@temporal-invalid.osbx.lua");
    CHECK_FALSE(result.ok());
    CHECK_FALSE(result.diagnostics.empty());
}
TEST_CASE("OSBX Lua clip and discrete tracks reuse typed temporal programs", "[lua][osbx][program]") {
    const auto result = run_osbx_lua(R"lua(storyboard('scene')
clip {id='clip',ops={{id='move',kind='translate',track={loop {count=2,keys={key {0,value={0,0}},key {3,value={6,0}}}}}}}}
image {id='image',file='image.png'}; sprite('sprite'); image('image')
use_clip {clip='clip',start=10,time_scale=0.5}
visibility {loop {count=2,keys={key {0,value=false},key {2,value=true}}}}
blend {trigger {name='Failing',keys={key {0,value='additive'}}}}
)lua", "@temporal-clip.osbx.lua");
    REQUIRE(result.ok());
    REQUIRE(result.value.clips[0].ops[0].track.program.size() == 1);
    CHECK(result.value.nodes[0].tracks.visibility.program.size() == 1);
    CHECK(result.value.nodes[0].tracks.blend.program.size() == 1);
}
TEST_CASE("OSBX Lua discrete root keys and programs share the ordered descriptor form", "[lua][osbx][program]") {
    const auto result = run_osbx_lua(R"lua(storyboard('scene')
image {id='image',file='image.png'}; sprite('sprite'); image('image')
visibility {key {0,value=true,easing='step'},loop {start=10,count=2,keys={key {0,value=false}}}}
blend {key {0,value='normal'},trigger {name='Passing',keys={key {0,value='additive'}}}}
)lua", "@discrete-program.osbx.lua");
    REQUIRE(result.ok());
    CHECK(result.value.nodes[0].tracks.visibility.keys.size() == 1);
    CHECK(result.value.nodes[0].tracks.visibility.program.size() == 1);
    CHECK(result.value.nodes[0].tracks.blend.keys.size() == 1);
    CHECK(result.value.nodes[0].tracks.blend.program.size() == 1);
}

TEST_CASE("OSBX Lua media declarations copy canonical metadata and freeze Animation templates", "[lua][osbx][media]") {
    const auto result = run_osbx_lua(R"lua(storyboard('scene')
local settings={file='hero.png',frame_count=3,frame_delay_ms=12.5,loop_type='LoopOnce'}
local handle=animation('hero',settings); settings.frame_count=99
assert(handle.scope=='scene' and handle.animation==nil)
sample('cue',{path='tone.wav',time_ms=-10,volume=70})
)lua");
    REQUIRE(result.ok());
    CHECK(result.value.nodes[0].animation->frame_count == 3);
    CHECK(result.value.nodes[0].animation->frame_delay_ms == 12.5);
    CHECK(result.value.nodes[1].sample->effective_volume() == 70);
    const auto changed = run_osbx_lua("storyboard('s'); animation('a',{file='a.png',frame_count=2,frame_delay_ms=10}); image('other')");
    CHECK_FALSE(changed.ok());
}
TEST_CASE("OSBX Lua rejects malformed media metadata without activating placeholders", "[lua][osbx][media]") {
    for (const auto* declaration : {"animation('a',{file='a.png',frame_count=0,frame_delay_ms=1})",
        "animation('a',{file='a.png',frame_count=1,frame_delay_ms=1,loop_type='once'})",
        "sample('s',{path='tone.wav',time_ms=1.5})", "sample('s',{path='tone.wav',time_ms=0,volume=0})"}) {
        CHECK_FALSE(run_osbx_lua(std::string{"storyboard('scene'); "} + declaration).ok());
    }
}

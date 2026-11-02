#include "scripting/osbx_lua_link.h"
#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/osbx_osb_export.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_storage.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <nlohmann/json.hpp>
#include <source_location>

using namespace wormedit;
using namespace wormedit::scripting;
namespace {
void check_link(bool value, std::source_location at = std::source_location::current()) { INFO(at.line()); CHECK(value); }
void require_link(bool value, std::source_location at = std::source_location::current()) { INFO(at.line()); REQUIRE(value); }
auto link(std::string source, std::string id = "wobble-link") -> osbx_lua_link_t {
    const auto hash = native_project_content_checksum(source);
    return {.id = std::move(id), .source = {.frozen_source = std::move(source),
        .source_reference = "scripts/wobble.lua", .source_hash = hash, .seed = 42}};
}
auto scene(std::string source = "return function(t,ctx) return {t/10, 0} end") -> osbx_document_t {
    auto binding = link(std::move(source));
    return {.images = {{.id = "image", .file = "image.png"}},
        .nodes = {{.id = "sprite", .kind = osbx_node_kind_t::sprite, .image = "image",
            .ops = {{.id = "move", .track = {.default_value = {7, 9},
                .lua = osbx_lua_track_link_t{.link = std::move(binding), .begin_ms = 0, .end_ms = 100}}}}}}};
}
auto prepare(const osbx_document_t& source, const osbx_lua_prepared_scene_t* old = nullptr,
    lua_sandbox_limits_t limits = {}, bool require_success = true) -> osbx_lua_prepared_scene_t {
    const osbx_lua_link_backend_t backend{limits};
    auto result = osbx_lua_prepared_scene_t::from(source, {.project = song_id_t{1}, .revision = 1}, backend, old);
    std::string message;
    for (const auto& diagnostic : result.diagnostics) message += diagnostic.message + "\n";
    INFO(message);
    if (require_success) require_link(result.complete);
    return result;
}
auto frame(const osbx_evaluation_index_t& index, std::int64_t time) -> osbx_draw_instance_t {
    frame_arena_t arena;
    const auto evaluated = evaluate_osbx_frame(index, time, arena);
    require_link(static_cast<bool>(evaluated.is_success())); require_link(static_cast<bool>(evaluated.instances.size() == 1));
    return evaluated.instances.front();
}
auto subtree() -> osbx_document_t {
    return {.nodes = {{.id = "hand-group", .kind = osbx_node_kind_t::group,
        .layer = osbx_layer_t::foreground,
        .ops = {{.id = "hand-position", .track = {.default_value = {100, 200}}}},
        .children_link = link(R"lua(return function(ctx)
  image {id='generated-image',file='generated.png'}
  sprite('child')
    image('generated-image')
    transform.translate {5,7}
end)lua", "children-link")}}};
}
} // namespace

TEST_CASE("OSBX Lua links reproduce linear keys and retain hand data outside their range", "[osbx-link]") {
    auto source = scene();
    auto& authored = source.nodes.front().ops.front().track;
    authored.keys = {{.id = "a", .time_ms = -100, .value = {20,30}, .easing = osbx_osb_easing_t{17}},
        {.id = "b", .time_ms = 200, .value = {50,60}}};
    const auto prepared = prepare(source);
    require_link(static_cast<bool>(prepared.complete)); require_link(static_cast<bool>(prepared.scene));
    require_link(static_cast<bool>(prepared.links.size() == 1));
    check_link(static_cast<bool>(prepared.links.front().samples.size() == 2));
    check_link(!static_cast<bool>(osbx_evaluation_index_t::from(source).value));
    const osbx_lua_prepared_scope_t scope{std::span{&prepared,1}};
    auto index = osbx_evaluation_index_t::from(source);
    require_link(static_cast<bool>(index.value));
    check_link(static_cast<bool>(frame(*index.value, 50).world.values[4] == Catch::Approx(5)));
    for (const auto t : {-101, -99, -1, 101, 150, 201}) {
        const auto expected = sample_osbx_numeric_track(authored, t);
        check_link(static_cast<bool>(frame(*index.value, t).world.values[4] == Catch::Approx(expected[0])));
        check_link(static_cast<bool>(frame(*index.value, t).world.values[5] == Catch::Approx(expected[1])));
    }
}

TEST_CASE("OSBX named Lua entry points receive frozen parameters and adaptively sample a wobble", "[osbx-link]") {
    auto source = scene("return {wobble=function(t,ctx) return ctx.parameters.amplitude * math.sin(2*math.pi*t/ctx.parameters.period) end}");
    auto& op = source.nodes.front().ops.front();
    op.kind = osbx_transform_kind_t::rotate; op.track.default_value = {0};
    auto& binding = *op.track.lua;
    binding.end_ms = 1000; binding.link.source.entry_point = "wobble";
    binding.link.source.parameters = {{.name = "amplitude", .value = {4,1}}, {.name = "period", .value = {1200,1}}};
    const auto prepared = prepare(source);
    require_link(prepared.complete); require_link(static_cast<bool>(prepared.scene));
    check_link(prepared.links.front().samples.size() > 2);
    const auto value = sample_osbx_numeric_track(prepared.scene->nodes.front().ops.front().track,500);
    check_link(value.front() == Catch::Approx(2));
}

TEST_CASE("OSBX Lua links replay seeds and stop loops without discarding the last good result", "[osbx-link]") {
    const auto source = scene("local x=math.random(); return function(t,ctx) return {x,t} end");
    const auto first = prepare(source), second = prepare(source);
    require_link(static_cast<bool>(first.complete)); require_link(static_cast<bool>(second.complete));
    check_link(static_cast<bool>(first.links == second.links));
    auto broken = source;
    broken.nodes.front().ops.front().track.lua->link = link("return function(t,ctx)\n error('broken')\nend");
    const auto failed = prepare(broken, &first);
    require_link(static_cast<bool>(failed.complete)); require_link(static_cast<bool>(failed.scene)); require_link(!static_cast<bool>(failed.diagnostics.empty()));
    check_link(static_cast<bool>(failed.links.front().is_last_good));
    check_link(static_cast<bool>(failed.diagnostics.front().location.id == "sprite"));
    check_link(static_cast<bool>(failed.diagnostics.front().message.find("wobble.lua:2:") != std::string::npos));
    check_link(static_cast<bool>(failed.scene->nodes.front().ops.front().track == first.scene->nodes.front().ops.front().track));
    const auto repeated_failure = prepare(broken, &failed);
    require_link(!static_cast<bool>(repeated_failure.diagnostics.empty()));
    auto loop = scene("return function() while true do end end");
    const auto stopped = prepare(loop, nullptr, {.maximum_instructions = 10'000},false);
    check_link(!static_cast<bool>(stopped.complete)); check_link(!static_cast<bool>(stopped.diagnostics.empty()));
    check_link(!static_cast<bool>(convert_osbx_to_osb(loop,{}).value.model));
}

TEST_CASE("OSBX guard-budget failure retains the last good track range and future fallback", "[osbx-link]") {
    auto source = scene();
    auto& track = source.nodes.front().ops.front().track;
    track.keys = {{.id = "a", .time_ms = 0, .value = {20,30}, .easing = osbx_osb_easing_t{17}},
        {.id = "b", .time_ms = 200000, .value = {50,60}}};
    track.lua->end_ms = 200000;
    const auto good = prepare(source);
    track.lua->begin_ms = 100000; track.lua->end_ms = 110000;
    const auto limited = prepare(source,&good);
    check_link(limited.complete && !limited.diagnostics.empty());
    check_link(limited.links.front().is_last_good);
    check_link(*limited.scene == *good.scene);
    track.lua->link = link("return function() error('still broken') end");
    const auto failed = prepare(source,&limited);
    check_link(failed.complete && !failed.diagnostics.empty());
    check_link(*failed.scene == *good.scene);
}

TEST_CASE("OSBX Lua generated children have stable scoped identities and retain prepared storage", "[osbx-link]") {
    const auto source = subtree();
    std::optional<osbx_evaluation_index_t> retained;
    {
        const auto a = prepare(source), b = prepare(source);
        require_link(static_cast<bool>(a.complete)); require_link(static_cast<bool>(b.complete)); require_link(static_cast<bool>(a.scene));
        check_link(static_cast<bool>(*a.scene == *b.scene));
        require_link(static_cast<bool>(a.scene->nodes.size() == 2));
        check_link(static_cast<bool>(a.scene->nodes[1].parent == "hand-group"));
        check_link(static_cast<bool>(a.scene->nodes[1].layer == osbx_layer_t::foreground));
        const osbx_lua_prepared_scope_t scope{std::span{&a,1}};
        auto index = osbx_evaluation_index_t::from(source);
        require_link(static_cast<bool>(index.value)); retained = std::move(index.value);
    }
    require_link(static_cast<bool>(retained->prepared_owner));
    check_link(static_cast<bool>(frame(*retained,0).world.values[4] == Catch::Approx(105)));
    check_link(static_cast<bool>(frame(*retained,0).world.values[5] == Catch::Approx(207)));
}

TEST_CASE("OSBX Lua link persistence contains descriptors and bake or detach is one Undo", "[osbx-link]") {
    const auto children = GENERATE(false,true);
    const auto source = children ? subtree() : scene();
    song_document_t song{.id = song_id_t{1}, .revision = 1,
        .storyboards = {{.id = storyboard_id_t{2}, .name = "hand-authored", .osbx = source}}};
    const auto written = write_native_project(song);
    require_link(static_cast<bool>(written.value));
    const auto encoded = nlohmann::json::parse(*written.value);
    const auto& stored = encoded["document"]["storyboards"][0]["osbx"];
    check_link(static_cast<bool>(stored["nodes"].size() == 1));
    check_link(static_cast<bool>(written.value->find("sample/") == std::string::npos));
    if (children) check_link(static_cast<bool>(written.value->find("generated.png") != std::string::npos)); // Only inside frozen Lua text.
    const auto loaded = parse_native_project(*written.value);
    require_link(static_cast<bool>(loaded.value));
    check_link(static_cast<bool>(loaded.value->storyboards.front().osbx == source));
    auto history = document_history_t::from(song);
    auto prepared = prepare(source);
    require_link(static_cast<bool>(prepared.complete));
    prepared.stamp = document_stamp_t::from(history);
    const osbx_materialize_link_t edit{.storyboard = storyboard_id_t{2}, .node_id = children ? "hand-group" : "sprite",
        .target = children ? "children" : "op/move", .prepared = prepared};
    const auto applied = apply_osbx_materialize_link(history, edit);
    require_link(static_cast<bool>(applied.applied));
    check_link(!static_cast<bool>(has_osbx_lua_links(*history.document.storyboards.front().osbx)));
    check_link(static_cast<bool>(*history.document.storyboards.front().osbx == *prepared.scene));
    require_link(static_cast<bool>(history.undo.size() == 1));
    require_link(static_cast<bool>(undo_document_command(history)));
    check_link(static_cast<bool>(history.document.storyboards.front().osbx == source));
}

TEST_CASE("OSBX prepared metadata lookup requires board stamp source and captured generation", "[osbx-link][metadata]") {
    const auto source = scene();
    auto value = prepare(source); value.storyboard = storyboard_id_t{2};
    const osbx_lua_prepared_scope_t scope{std::span{&value, 1}, 7};
    CHECK(osbx_lua_prepared_scope_t::prepared(source, value.stamp, storyboard_id_t{2}, 7) == &value);
    CHECK_FALSE(osbx_lua_prepared_scope_t::prepared(source, value.stamp, storyboard_id_t{3}, 7));
    CHECK_FALSE(osbx_lua_prepared_scope_t::prepared(source, {.project = song_id_t{1}, .revision = 99}, storyboard_id_t{2}, 7));
    CHECK_FALSE(osbx_lua_prepared_scope_t::prepared(source, value.stamp, storyboard_id_t{2}, 8));
    auto changed = source; changed.nodes[0].order = 2;
    CHECK_FALSE(osbx_lua_prepared_scope_t::prepared(changed, value.stamp, storyboard_id_t{2}, 7));
}
TEST_CASE("OSBX failed link diagnostics identify the actual link while retaining its last-good samples", "[osbx-link][metadata]") {
    auto source = scene();
    source.nodes[0].tracks.opacity.lua = {.link = link("return function() return 0.5 end", "opacity-link"), .begin_ms = 0, .end_ms = 100};
    const auto good = prepare(source);
    source.nodes[0].tracks.opacity.lua->link = link("return function()\n error('bad opacity')\nend", "opacity-link");
    const auto failed = prepare(source, &good);
    REQUIRE(failed.complete);
    REQUIRE_FALSE(failed.diagnostics.empty());
    CHECK(failed.diagnostics[0].location.link_id == "opacity-link");
    CHECK(failed.links[1].is_last_good);
    CHECK(failed.links[1].samples == good.links[1].samples);
    CHECK(failed.scene->nodes[0].tracks.opacity == good.scene->nodes[0].tracks.opacity);
}

TEST_CASE("OSBX outside-range hand-key gestures rebase existing Lua samples and publish one Undo", "[osbx-link][gesture]") {
    auto source = scene();
    source.nodes[0].ops[0].track.keys = {{.id = "hand", .time_ms = -100, .value = {20, 30}}};
    auto prepared = prepare(source); prepared.storyboard = storyboard_id_t{2};
    const osbx_lua_prepared_scope_t scope{std::span{&prepared, 1}, 7};
    auto history = document_history_t::from(song_document_t{.id = song_id_t{1}, .revision = 1,
        .storyboards = {{.id = storyboard_id_t{2}, .osbx = source}}});
    editor_interaction_state_t state;
    const osbx_edit_t edit{.storyboard = storyboard_id_t{2}, .change = osbx_move_key_t{
        .track = {.node = "sprite", .kind = osbx_track_kind_t::transform, .op = "move"}, .key = "hand", .time_ms = -90}};
    REQUIRE(dispatch_editor_action(history, state, begin_osbx_edit_gesture_action_t{.edit = edit}));
    CHECK(history.document.storyboards[0].osbx == source);
    REQUIRE(dispatch_editor_action(history, state,
        update_osbx_edit_gesture_action_t{.change = edit.change, .operation = proposal_operation_t::apply}));
    CHECK(history.undo.size() == 1);
    CHECK(history.document.storyboards[0].osbx->nodes[0].ops[0].track.keys[0].time_ms == -90);
    CHECK(history.document.storyboards[0].osbx->nodes[0].ops[0].track.lua == source.nodes[0].ops[0].track.lua);
}
TEST_CASE("OSBX exact Lua metadata remains available under a same-generation preview scope", "[osbx-link][metadata]") {
    const auto source = scene();
    auto prepared = prepare(source); prepared.storyboard = storyboard_id_t{2};
    const osbx_lua_prepared_scope_t outer{std::span{&prepared, 1}, 7};
    auto preview = prepared; preview.source.nodes[0].order = 1;
    const osbx_lua_prepared_scope_t inner{std::span{&preview, 1}, 7};
    CHECK(osbx_lua_prepared_scope_t::prepared(source, prepared.stamp, storyboard_id_t{2}, 7) == &prepared);
    CHECK(osbx_lua_prepared_scope_t::prepared(preview.source, preview.stamp, storyboard_id_t{2}, 7) == &preview);
    CHECK_FALSE(osbx_lua_prepared_scope_t::prepared(source, prepared.stamp, storyboard_id_t{2}, 8));
}

TEST_CASE("OSBX retained Lua rebase refuses changed clip definitions and use parameters", "[osbx-link][clips]") {
    auto source = scene();
    source.clips = {{.id = "motion", .ops = {{.id = "clip-op"}}}};
    source.nodes[0].uses = {{.id = "use", .clip = "motion"}};
    auto prepared = prepare(source); prepared.storyboard = storyboard_id_t{2};
    const osbx_lua_prepared_scope_t scope{std::span{&prepared, 1}};
    auto changed = source;
    SECTION("definition") { changed.clips[0].ops[0].track.default_value = {1,2}; }
    SECTION("use") { changed.nodes[0].uses[0].start_ms = 1; }
    const auto rebased = osbx_lua_prepared_scope_t::rebase(source, changed, prepared.stamp, prepared.storyboard);
    CHECK_FALSE(rebased);
    CHECK(rebased.error() == failure_code_t::stale_generation);
}

namespace {
void check_generated_clip_bindings(const osbx_lua_prepared_scene_t& prepared) {
    REQUIRE(prepared.scene);
    REQUIRE(prepared.scene->clips.size() == 1);
    CHECK(prepared.scene->clips[0].parameters[0].name == "offset");
    CHECK(prepared.scene->nodes[1].uses[0].clip == prepared.scene->clips[0].id);
}
}
TEST_CASE("OSBX prepared generated children scope clip owners and retain parameter names", "[osbx-link][clips]") {
    auto source = subtree();
    source.nodes[0].children_link = link(R"lua(return function(ctx)
clip {id='motion', parameters={{name='offset',default={5,7}}},
  ops={{id='shift',kind='translate',track={parameter='offset'}}}}
image {id='generated-image',file='generated.png'}
sprite('child'); image('generated-image'); use_clip {id='use',clip='motion'}
end)lua", "clip-children");
    std::optional<osbx_evaluation_index_t> retained;
    {
        const auto prepared = prepare(source);
        check_generated_clip_bindings(prepared);
        const osbx_lua_prepared_scope_t scope{std::span{&prepared,1}};
        auto index = osbx_evaluation_index_t::from(source); REQUIRE(index.value);
        retained = std::move(index.value);
    }
    REQUIRE(retained->prepared_owner); REQUIRE(retained->compiled_owner);
    CHECK(frame(*retained,0).world[4] == Catch::Approx(105));
}

TEST_CASE("Prepared Lua child programs retain scoped identities and immutable ownership", "[osbx-link][program]") {
    auto source = subtree();
    source.nodes[0].children_link = link(R"lua(return function(ctx)
 image {id='image',file='image.png'}; sprite('child'); image('image')
 opacity {loop {start=0,count=2,keys={key {0,value=0},key {10,value=1}}}}
end)lua", "program-link");
    const auto prepared = prepare(source);
    require_link(prepared.complete);
    const osbx_lua_prepared_scope_t scope{std::span{&prepared, 1}};
    const auto index = osbx_evaluation_index_t::from(source);
    require_link(index.value.has_value());
    const auto value = frame(*index.value, 5);
    check_link(value.opacity == Catch::Approx(0.5));
}

TEST_CASE("OSBX retained Lua rebasing refuses changed media metadata and lifetime", "[osbx-link][media]") {
    auto source = scene(); source.nodes[0].kind = osbx_node_kind_t::animation;
    source.nodes[0].animation = storyboard_animation_t{.frame_count = 2, .frame_delay_ms = 10};
    auto prepared = prepare(source); prepared.storyboard = storyboard_id_t{2};
    const osbx_lua_prepared_scope_t scope{std::span{&prepared, 1}};
    auto changed = source;
    SECTION("frame metadata") { changed.nodes[0].animation->frame_delay_ms = 20; }
    SECTION("sample metadata") { changed.nodes.push_back({.id = "cue", .kind = osbx_node_kind_t::sample,
        .sample = osbx_sample_t{.path = "tone.wav"}}); }
    SECTION("lifetime") { changed.nodes[0].lifetime = osbx_lifetime_t{0,100}; }
    const auto rebased = osbx_lua_prepared_scope_t::rebase(source, changed, prepared.stamp, prepared.storyboard);
    check_link(!rebased); if (!rebased) check_link(rebased.error() == failure_code_t::stale_generation);
}

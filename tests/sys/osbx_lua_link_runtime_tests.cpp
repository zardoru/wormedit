#include "sys/storyboard/osbx_lua_link_runtime.h"
#include "../support/temporary_directory.h"
#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/formats/native_project_storage.h>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <latch>

using namespace wormedit;
using namespace wormedit::sys;
namespace {
void check_link_runtime(bool value) { CHECK(value); }
void require_link_runtime(bool value) { REQUIRE(value); }
auto linked_song(std::string source = "return function(t,ctx) return t/100 end") -> song_document_t {
    osbx_lua_link_t link{.id = "opacity-link", .source = {.frozen_source = source,
        .source_reference = "curve.lua", .source_hash = native_project_content_checksum(source)}};
    osbx_node_t node{.id = "sprite", .kind = osbx_node_kind_t::sprite, .image = "image"};
    node.tracks.opacity.lua = {.link = std::move(link), .begin_ms = 0, .end_ms = 100};
    return {.id = song_id_t{1}, .revision = 1,
        .storyboards = {{.id = storyboard_id_t{2}, .name = "scene",
            .osbx = osbx_document_t{.images = {{.id = "image", .file = "image.png"}}, .nodes = {node}}}}};
}
void set_source(song_document_t& song, std::string source) {
    auto& descriptor = song.storyboards.front().osbx->nodes.front().tracks.opacity.lua->link.source;
    descriptor.frozen_source = std::move(source);
    descriptor.source_hash = native_project_content_checksum(descriptor.frozen_source);
    ++song.revision;
}
} // namespace

#if defined(WORMEDIT_HAS_LUA)
TEST_CASE("storyboard link worker publishes ready results and preserves last-good payloads", "[osbx-link][runtime]") {
    task_scheduler_t scheduler{1,2};
    osbx_lua_link_runtime_t runtime{scheduler};
    auto song = linked_song();
    runtime.update(song,1);
    runtime.wait(song,1);
    require_link_runtime(static_cast<bool>(runtime.scenes(song).size() == 1)); require_link_runtime(static_cast<bool>(runtime.scenes(song).front().complete));
    const auto first = runtime.snapshot();
    const auto generation = runtime.generation();
    set_source(song,"return function() error('bad update') end");
    runtime.update(song,1); runtime.wait(song,1);
    require_link_runtime(static_cast<bool>(runtime.scenes(song).front().complete));
    require_link_runtime(!static_cast<bool>(runtime.scenes(song).front().diagnostics.empty()));
    check_link_runtime(static_cast<bool>(runtime.generation() > generation));
    check_link_runtime(static_cast<bool>(runtime.scenes(song).front().scene->nodes.front().tracks.opacity == first->front().scene->nodes.front().tracks.opacity));
    const osbx_lua_prepared_scope_t scope{runtime.scenes(song),runtime.generation()};
    auto index = osbx_evaluation_index_t::from(*song.storyboards.front().osbx);
    require_link_runtime(static_cast<bool>(index.value));
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*index.value,50,arena);
    require_link_runtime(static_cast<bool>(frame.instances.size() == 1)); check_link_runtime(static_cast<bool>(frame.instances.front().opacity == 0.5));
}

TEST_CASE("storyboard link worker rejects stale stamps and replacement sessions", "[osbx-link][runtime]") {
    task_scheduler_t scheduler{1,2};
    std::latch entered{1}, release{1};
    task_graph_t blocker{1};
    require_link_runtime(static_cast<bool>(blocker.create_task({},[&](auto, std::stop_token) -> task_outcome_t {
        entered.count_down(); release.wait(); return task_value_t{};
    })));
    auto blocking = scheduler.begin(std::move(blocker));
    require_link_runtime(static_cast<bool>(blocking)); entered.wait();
    osbx_lua_link_runtime_t runtime{scheduler};
    auto song = linked_song();
    runtime.update(song,1);
    ++song.revision;
    release.count_down();
    runtime.wait(song,1);
    check_link_runtime(static_cast<bool>(runtime.scenes(song).empty()));
    runtime.update(song,1); runtime.wait(song,1);
    require_link_runtime(static_cast<bool>(runtime.scenes(song).size() == 1));
    const auto old = runtime.snapshot();
    runtime.update(song,2); // Same ID/revision, newly opened project session.
    check_link_runtime(static_cast<bool>(runtime.scenes(song).empty()));
    runtime.wait(song,2);
    require_link_runtime(static_cast<bool>(runtime.scenes(song).size() == 1));
    check_link_runtime(static_cast<bool>(runtime.snapshot() != old));
    check_link_runtime(static_cast<bool>(old->front().scene)); // Prior borrowed storage remains alive independently.
}
#endif

TEST_CASE("storyboard Lua Reload alone reads the module and freezes its identity", "[osbx-link][runtime]") {
    const auto directory = test::temporary_directory_t::from(); require_link_runtime(static_cast<bool>(directory));
    const auto song = linked_song();
    const auto original = song.storyboards.front().osbx->nodes.front().tracks.opacity.lua->link.source;
    { std::ofstream file{directory->path()/"curve.lua"}; file << "return function() return 0.25 end"; }
    auto reloaded = reload_osbx_lua_source(original,directory->path()); require_link_runtime(static_cast<bool>(reloaded));
    check_link_runtime(static_cast<bool>(reloaded->frozen_source != original.frozen_source));
    check_link_runtime(static_cast<bool>(reloaded->source_hash == native_project_content_checksum(reloaded->frozen_source)));
    check_link_runtime(static_cast<bool>(song.storyboards.front().osbx->nodes.front().tracks.opacity.lua->link.source == original));
    std::stop_source cancellation; cancellation.request_stop();
    check_link_runtime(!static_cast<bool>(reload_osbx_lua_source(original,directory->path(),cancellation.get_token())));
}

TEST_CASE("Storyboard source worker freezes module bytes without evaluating or publishing the document",
          "[osbx-link][runtime][source-worker]") {
    const auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    { std::ofstream file{directory->path() / "curve.lua"}; file << "return function() return 0.25 end"; }
    task_scheduler_t scheduler{1, 2};
    osbx_lua_link_runtime_t runtime{scheduler};
    const auto song = linked_song();
    const auto before = song.storyboards;
    const ui::storyboard_action_t action{.kind = ui::storyboard_action_kind_t::lua_link,
        .project = song.id, .revision = song.revision, .session_generation = 7, .storyboard = storyboard_id_t{2}};
    const auto source = song.storyboards[0].osbx->nodes[0].tracks.opacity.lua->link.source;
    REQUIRE(runtime.request_source(action, source, directory->path()));
    runtime.wait(song, 7);
    const auto result = runtime.take_source();
    REQUIRE(result);
    REQUIRE(result->source);
    CHECK(result->source->source_hash == native_project_content_checksum(result->source->frozen_source));
    CHECK(song.storyboards == before);
}
TEST_CASE("Storyboard source worker missing files preserve prepared snapshots and reject stale revision publication",
          "[osbx-link][runtime][source-worker]") {
    task_scheduler_t scheduler{1, 2};
    osbx_lua_link_runtime_t runtime{scheduler};
    auto song = linked_song();
#if defined(WORMEDIT_HAS_LUA)
    runtime.update(song, 7); runtime.wait(song, 7);
#endif
    const auto before = runtime.snapshot();
    const ui::storyboard_action_t action{.kind = ui::storyboard_action_kind_t::lua_link,
        .project = song.id, .revision = song.revision, .session_generation = 7, .storyboard = storyboard_id_t{2}};
    const auto source = song.storyboards[0].osbx->nodes[0].tracks.opacity.lua->link.source;
    REQUIRE(runtime.request_source(action, source, "/missing/sb14"));
    runtime.wait(song, 7);
    const auto failure = runtime.take_source();
    REQUIRE(failure);
    CHECK_FALSE(failure->source);
    CHECK(runtime.snapshot() == before);
    REQUIRE(runtime.request_source(action, source, "/missing/sb14"));
    ++song.revision; runtime.wait(song, 7);
    CHECK_FALSE(runtime.take_source());
}

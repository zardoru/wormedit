#include "sys/storyboard/storyboard_inspector_view.h"
#include "sys/storyboard/storyboard_timeline_view.h"
#include "sys/storyboard/storyboard_hierarchy_view.h"
#include <wormedit/core/osbx_lua_link.h>
#include <wormedit/formats/native_project_storage.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace {
using namespace wormedit;
auto linked_song() -> song_document_t {
    const std::string source = "return function(t,ctx) return t/100 end";
    const osbx_lua_track_link_t link{
        .link = {.id = "opacity-link", .source = {.frozen_source = source,
            .source_reference = "scripts/opacity.lua", .source_hash = native_project_content_checksum(source)}},
        .begin_ms = 0, .end_ms = 100};
    return {.id = song_id_t{1}, .revision = 3,
        .storyboards = {{.id = storyboard_id_t{11}, .osbx = osbx_document_t{
            .images = {{.id = "image", .file = "sprite.png"}},
            .nodes = {{.id = "sprite", .kind = osbx_node_kind_t::sprite, .image = "image",
                .tracks = {.opacity = {.default_value = {1}, .keys = {
                    {.id = "hand-before", .time_ms = -20, .value = {0.2}},
                    {.id = "hand-after", .time_ms = 120, .value = {0.8}}}, .lua = link}}}}}}}};
}
auto linked_root() -> ui::storyboard_view_t {
    ui::storyboard_view_t root{.visible = true, .project = song_id_t{1}, .revision = 3,
        .session_generation = 7};
    root.refill_state({.selection = {.storyboard = storyboard_id_t{11}, .nodes = {"sprite"}}});
    return root;
}
auto prepared_scene(const song_document_t& song) -> osbx_document_t {
    // A supplied worker-result fixture, not a Lua invocation or a second sampler.
    auto scene = *song.storyboards[0].osbx;
    auto& track = scene.nodes[0].tracks.opacity;
    track.lua.reset();
    track.keys.insert(track.keys.begin() + 1, {
        {.id = "prepared/sample/0", .time_ms = 0, .value = {0.25}},
        {.id = "prepared/sample/100", .time_ms = 100, .value = {0.75}}});
    return scene;
}
} // namespace

TEST_CASE("Lua-linked storyboard hand-key insertion outside the range remains one undoable edit",
          "[sys][storyboard-lua-links][range]") {
    const auto song = linked_song();
    auto history = document_history_t::from(song);
    const auto applied = apply_osbx_edit(history, {.storyboard = storyboard_id_t{11},
        .change = osbx_add_key_t{.track = {.node = "sprite", .kind = osbx_track_kind_t::opacity},
            .time_ms = -10, .value = std::vector<double>{0.4}}});
    REQUIRE(applied.applied);
    CHECK(history.undo.size() == 1);
    CHECK(history.document.storyboards[0].osbx->nodes[0].tracks.opacity.keys.size() == 3);
    CHECK(history.document.storyboards[0].osbx->nodes[0].tracks.opacity.lua ==
        song.storyboards[0].osbx->nodes[0].tracks.opacity.lua);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.storyboards == song.storyboards);
}

TEST_CASE("Lua-linked storyboard inclusive endpoints and interior reject direct key insertion atomically",
          "[sys][storyboard-lua-links][range]") {
    const auto time = GENERATE(0, 50, 100);
    const auto song = linked_song();
    auto history = document_history_t::from(song);
    const auto refused = apply_osbx_edit(history, {.storyboard = storyboard_id_t{11},
        .change = osbx_add_key_t{.track = {.node = "sprite", .kind = osbx_track_kind_t::opacity},
            .time_ms = time, .value = std::vector<double>{0.4}}});
    CHECK_FALSE(refused.applied);
    CHECK_FALSE(refused.diagnostics.empty());
    CHECK(history.undo.empty());
    CHECK(history.document.storyboards == song.storyboards);
}

TEST_CASE("Lua-linked storyboard authored hand-key moves stay editable outside the scripted range",
          "[sys][storyboard-lua-links][range]") {
    const auto song = linked_song();
    auto history = document_history_t::from(song);
    const auto applied = apply_osbx_edit(history, {.storyboard = storyboard_id_t{11},
        .change = osbx_move_key_t{.track = {.node = "sprite", .kind = osbx_track_kind_t::opacity},
            .key = "hand-before", .time_ms = -10}});
    REQUIRE(applied.applied);
    CHECK(history.document.storyboards[0].osbx->nodes[0].tracks.opacity.keys[0].time_ms == -10);
    CHECK(history.document.storyboards[0].osbx->nodes[0].tracks.opacity.lua ==
        song.storyboards[0].osbx->nodes[0].tracks.opacity.lua);
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Lua-linked storyboard hand-key moves into inclusive scripted times reject without publication",
          "[sys][storyboard-lua-links][range]") {
    const auto time = GENERATE(0, 50, 100);
    const auto song = linked_song();
    auto history = document_history_t::from(song);
    const auto refused = apply_osbx_edit(history, {.storyboard = storyboard_id_t{11},
        .change = osbx_move_key_t{.track = {.node = "sprite", .kind = osbx_track_kind_t::opacity},
            .key = "hand-before", .time_ms = time}});
    CHECK_FALSE(refused.applied);
    CHECK_FALSE(refused.diagnostics.empty());
    CHECK(history.undo.empty());
    CHECK(history.document.storyboards == song.storyboards);
}

TEST_CASE("Storyboard Lua inspector projects supplied prepared values without unlocking scripted samples",
          "[sys][storyboard-lua-links][projection]") {
    const auto song = linked_song();
    const auto prepared = prepared_scene(song);
    auto root = linked_root();
    sys::build_storyboard_inspector_view(root, song, {.milliseconds = 50}, nullptr, &prepared);
    REQUIRE(root.inspector.opacity.value.size() == 1);
    CHECK(root.inspector.opacity.value[0] == Catch::Approx(0.5));
    CHECK_FALSE(root.inspector.opacity.editable);
    CHECK_FALSE(root.inspector.opacity.reason.empty());
    CHECK(root.inspector.editable);
}

TEST_CASE("Storyboard Lua inspector plans a hand-key edit outside the scripted range through the common policy",
          "[sys][storyboard-lua-links][range]") {
    const auto song = linked_song();
    const auto plan = sys::plan_storyboard_inspector_edit(song, storyboard_id_t{11},
        {.field = ui::storyboard_inspector_field_t::opacity, .node = "sprite", .value = {0.4},
            .key_time_ms = -10});
    REQUIRE(plan);
    const auto& edit = std::get<osbx_add_key_t>(*plan);
    CHECK(edit.track == osbx_track_target_t{.node = "sprite", .kind = osbx_track_kind_t::opacity});
    CHECK(edit.time_ms == -10);
    CHECK(std::get<std::vector<double>>(edit.value) == std::vector<double>{0.4});
}

TEST_CASE("Storyboard Lua timeline keeps prepared sample IDs out of authored key markers",
          "[sys][storyboard-lua-links][projection]") {
    const auto song = linked_song();
    const auto prepared = prepared_scene(song);
    const auto root = linked_root();
    ui::storyboard_timeline_view_t view;
    sys::storyboard_timeline_projection_t projection;
    projection.refill(view, song, root, {}, &prepared);
    REQUIRE(view.rows.size() == 4);
    REQUIRE(view.rows[0].keys.size() == 2);
    CHECK(view.rows[0].keys[0].id == "hand-before");
    CHECK(view.rows[0].keys[1].id == "hand-after");
    CHECK(view.rows[0].track == osbx_track_target_t{.node = "sprite", .kind = osbx_track_kind_t::opacity});
}

TEST_CASE("Storyboard Lua timeline plans outside-range insertion without removing the authored link",
          "[sys][storyboard-lua-links][range]") {
    const auto song = linked_song();
    const auto plan = sys::plan_storyboard_timeline_edit(song, storyboard_id_t{11},
        {.kind = ui::storyboard_timeline_action_kind_t::add_key,
            .track = {.node = "sprite", .kind = osbx_track_kind_t::opacity}}, {.milliseconds = -10});
    REQUIRE(plan);
    CHECK(std::get<osbx_add_key_t>(*plan).time_ms == -10);
    CHECK(song.storyboards[0].osbx->nodes[0].tracks.opacity.lua.has_value());
}

TEST_CASE("Storyboard Lua-owned hierarchy descendants expose the shared read-only reason without changing IDs",
          "[sys][storyboard-lua-links][projection]") {
    auto song = linked_song();
    auto& scene = *song.storyboards[0].osbx;
    auto link = scene.nodes[0].tracks.opacity.lua->link;
    link.id = "children-link";
    scene.nodes[0].parent = "group";
    scene.nodes.push_back({.id = "group", .children_link = std::move(link)});
    auto root = linked_root();
    sys::build_storyboard_hierarchy_view(root, song);
    const auto& rows = root.hierarchy.layers[0].rows;
    REQUIRE(rows.size() == 2);
    CHECK(rows[1].id == "sprite");
    CHECK_FALSE(rows[1].editable);
    CHECK(rows[1].warning.find("Lua-linked children") != std::string::npos);
}

TEST_CASE("Storyboard Lua keyless edits outside the interval create keys instead of changing defaults",
          "[sys][storyboard-lua-links][range]") {
    auto song = linked_song();
    auto& track = song.storyboards[0].osbx->nodes[0].tracks.opacity;
    track.keys.clear();
    const auto plan = sys::plan_storyboard_inspector_edit(song, storyboard_id_t{11},
        {.field = ui::storyboard_inspector_field_t::opacity, .node = "sprite", .value = {0.4}, .key_time_ms = -10});
    REQUIRE(plan);
    REQUIRE(std::holds_alternative<osbx_add_key_t>(*plan));
    auto history = document_history_t::from(song);
    REQUIRE(apply_osbx_edit(history, {.storyboard = storyboard_id_t{11}, .change = *plan}).applied);
    CHECK(history.document.storyboards[0].osbx->nodes[0].tracks.opacity.default_value == track.default_value);
    CHECK(history.document.storyboards[0].osbx->nodes[0].tracks.opacity.lua == track.lua);
}

TEST_CASE("Storyboard Lua projection separates scripted bands from editable hand markers and exposes worker identity",
          "[sys][storyboard-lua-links][projection]") {
    const auto song = linked_song();
    const auto& source = *song.storyboards[0].osbx;
    const auto& link = *source.nodes[0].tracks.opacity.lua;
    osbx_lua_prepared_scene_t prepared{.stamp = document_stamp_t::from(song), .storyboard = storyboard_id_t{11},
        .source = source, .scene = std::make_shared<const osbx_document_t>(prepared_scene(song)),
        .links = {{.node_id = "sprite", .target = "opacity", .link = link.link, .track_link = link,
            .samples = {{.id = "sample/0", .time_ms = 0, .value = {0.25}},
                        {.id = "sample/100", .time_ms = 100, .value = {0.75}}}}}, .complete = true};
    const osbx_lua_prepared_scope_t scope{std::span{&prepared, 1}, 7};
    auto root = linked_root(); root.canvas.prepared_generation = 7;
    root.state.viewport.timeline_begin_ms = -20; root.state.viewport.timeline_pixels_per_ms = 0.5;
    sys::build_storyboard_inspector_view(root, song, {.milliseconds = 50}, nullptr, prepared.scene.get());
    const auto* row = root.inspector.lua_link_for({.track = {.node = "sprite"}});
    REQUIRE(row);
    CHECK(row->link_id == "opacity-link");
    CHECK(row->source_hash == link.link.source.source_hash);
    ui::storyboard_timeline_view_t view;
    sys::storyboard_timeline_projection_t projection;
    projection.refill(view, song, root, {}, prepared.scene.get());
    REQUIRE(view.rows[0].lua);
    CHECK(view.rows[0].lua->samples == std::vector<double>{10, 60});
    CHECK(view.rows[0].keys[0].editable);
}

namespace {
auto outside_hand_edit(int kind) -> osbx_edit_change_t {
    const osbx_track_target_t target{.node = "sprite"};
    switch (kind) {
    case 0: return osbx_set_key_value_t{.track = target, .key = "hand-before", .value = std::vector<double>{0.4}};
    case 1: return osbx_set_easing_t{.track = target, .key = "hand-before", .easing = osbx_step_easing_t{}};
    case 2: return osbx_delete_key_t{.track = target, .key = "hand-before"};
    default: return osbx_set_default_t{.track = target, .value = std::vector<double>{0.4}};
    }
}

}
namespace {
void check_outside_hand_publication(const document_history_t& history, const document_command_result_t& result, int kind) {
    CHECK(result.applied == (kind != 3));
    CHECK(history.undo.size() == (kind == 3 ? 0 : 1));
}
}
TEST_CASE("Storyboard Lua outside hand values easing and deletion remain editable while linked defaults refuse",
          "[sys][storyboard-lua-links][range]") {
    const auto kind = GENERATE(0, 1, 2, 3);
    const auto song = linked_song();
    auto history = document_history_t::from(song);
    const auto result = apply_osbx_edit(history, {.storyboard = storyboard_id_t{11}, .change = outside_hand_edit(kind)});
    check_outside_hand_publication(history, result, kind);
    const auto& track = history.document.storyboards[0].osbx->nodes[0].tracks.opacity;
    CHECK(track.lua == song.storyboards[0].osbx->nodes[0].tracks.opacity.lua);
    CHECK(track.keys.size() == (kind == 2 ? 1 : 2));
}
TEST_CASE("Storyboard Lua mixed moves refuse an originally scripted authored key even when its destination is outside",
          "[sys][storyboard-lua-links][range]") {
    auto song = linked_song();
    auto& track = song.storyboards[0].osbx->nodes[0].tracks.opacity;
    track.keys.insert(track.keys.begin() + 1, {.id = "inside", .time_ms = 50, .value = {0.5}});
    auto history = document_history_t::from(song);
    const auto result = apply_osbx_edit(history, {.storyboard = storyboard_id_t{11}, .change = osbx_move_keys_t{
        .keys = {{.track = {.node = "sprite"}, .key = "hand-before"},
            {.track = {.node = "sprite"}, .key = "inside"}}, .delta_ms = -100}});
    CHECK_FALSE(result.applied);
    CHECK_FALSE(result.diagnostics.empty());
    CHECK(history.document.storyboards == song.storyboards);
    CHECK(history.undo.empty());
}

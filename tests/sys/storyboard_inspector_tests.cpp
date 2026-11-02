#include "sys/storyboard/storyboard_inspector_view.h"
#include "../support/document_assertions.h"
#include "ui/frontend/storyboard.h"
#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/interaction_dispatch.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <source_location>

namespace {
using namespace wormedit;
void check_inspector(bool value, std::source_location where = std::source_location::current()) {
    INFO(where.file_name() << ':' << where.line());
    CHECK(value);
}
auto inspector_song() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 1, .storyboards = {
        {.id = storyboard_id_t{3}, .name = "Scene", .osbx = osbx_document_t{
            .images = {{.id = "image", .file = "image.png"}}, .nodes = {
                {.id = "sprite", .kind = osbx_node_kind_t::sprite, .image = "image",
                 .ops = {{.id = "translate", .track = {.default_value = {10, 0}}},
                     {.id = "rotate", .kind = osbx_transform_kind_t::rotate,
                      .track = {.default_value = {90}}}},
                 .lifetime = osbx_lifetime_t{.begin_ms = -10, .end_ms = 100}}}}}}};
}
auto inspector_node(const document_history_t& history) -> const osbx_node_t& {
    return history.document.storyboards[0].osbx->nodes[0];
}
auto inspector_world(const document_history_t& history) -> visual_affine_t {
    const auto index = osbx_evaluation_index_t::from(*history.document.storyboards[0].osbx);
    REQUIRE(index.value);
    frame_arena_t arena;
    const auto frame = evaluate_osbx_frame(*index.value, 0, arena);
    REQUIRE(frame.is_success());
    REQUIRE(frame.instances.size() == 1);
    return frame.instances[0].world;
}
auto inspector_frontend() -> ui::storyboard_view_t {
    ui::storyboard_view_t view{.visible = true, .project = song_id_t{1}, .revision = 1,
        .storyboards = {{.id = storyboard_id_t{3}, .name = "Scene", .kind = ui::storyboard_kind_t::osbx}}};
    view.refill_state({.selection = {.storyboard = storyboard_id_t{3}, .nodes = {"sprite"}}});
    return view;
}

void check_auto_key_commit_and_undo(document_history_t& history, editor_interaction_state_t& state,
    const osbx_edit_change_t& revised, const song_document_t& before) {
    REQUIRE(dispatch_editor_action(history, state, update_osbx_edit_gesture_action_t{
        .change = revised, .operation = proposal_operation_t::apply}));
    check_inspector(!state.is_pending() && history.undo.size() == 1);
    const auto& track = inspector_node(history).tracks.opacity;
    REQUIRE(track.keys.size() == 2);
    check_inspector(track.keys[1].time_ms == 50 && track.keys[1].value == std::vector<double>{0.75});
    check_inspector(inspector_node(history).lifetime == before.storyboards[0].osbx->nodes[0].lifetime);
    REQUIRE(undo_document_command(history));
    check_inspector(history.document.storyboards == before.storyboards);
}
} // namespace

TEST_CASE("Storyboard inspector projection samples the fractional playhead separately from key snapping",
          "[unit][sys][storyboard-inspector]") {
    auto song = inspector_song();
    auto& node = song.storyboards[0].osbx->nodes[0];
    node.tracks.opacity.keys = {{.id = "left", .time_ms = -2, .value = {0}},
                                {.id = "right", .time_ms = -1, .value = {1}}};
    auto view = inspector_frontend();
    const storyboard_time_t time{.milliseconds = -2, .remainder = 1, .denominator = 2};
    const osbx_node_frame_t evaluated{.matrix = {1, 0, 0, 1, 10, 20}};
    sys::build_storyboard_inspector_view(view, song, time, &evaluated);
    check_inspector(view.inspector.time == time && view.inspector.key_time_ms == -2);
    REQUIRE(view.inspector.opacity.value.size() == 1);
    check_inspector(view.inspector.opacity.value[0] == Catch::Approx(0.5));
    check_inspector(view.inspector.opacity.keyed && !view.inspector.opacity.at_key);
    check_inspector(view.inspector.opacity.key == "left");
    check_inspector(view.inspector.origins.size() == 9 && view.inspector.images[0].id == "image");
    check_inspector(view.inspector.world == evaluated.matrix);
    check_inspector(view.inspector.lifetime == node.lifetime);
    REQUIRE(view.inspector.ops.size() == 2);
    check_inspector(view.inspector.ops[0].id == "translate" && view.inspector.ops[1].id == "rotate");
}

TEST_CASE("Storyboard inspector planner resolves captured node op and time into the common policy",
          "[unit][sys][storyboard-inspector]") {
    auto song = inspector_song();
    song.storyboards[0].osbx->nodes[0].ops[0].track.keys = {{.id = "position", .time_ms = 10, .value = {10, 0}}};
    const ui::storyboard_inspector_action_t action{.field = ui::storyboard_inspector_field_t::transform,
        .node = "sprite", .op = "translate", .value = {20, 30}, .key_time_ms = 10};
    const auto plan = sys::plan_storyboard_inspector_edit(song, storyboard_id_t{3}, action);
    REQUIRE(plan);
    const auto& edit = std::get<osbx_set_key_value_t>(*plan);
    check_inspector(edit.track.op == "translate" && edit.key == "position");
    auto history = document_history_t::from(song);
    REQUIRE(apply_osbx_edit(history, {.storyboard = storyboard_id_t{3}, .change = *plan}).applied);
    check_inspector(history.undo.size() == 1 && inspector_node(history).lifetime == song.storyboards[0].osbx->nodes[0].lifetime);
}

TEST_CASE("Storyboard inspector inherited Lua children ownership is readonly in projection and planning",
          "[unit][sys][storyboard-inspector]") {
    auto song = inspector_song();
    auto& scene = *song.storyboards[0].osbx;
    scene.nodes[0].parent = "linked-parent";
    scene.nodes.push_back({.id = "linked-parent", .children_link = osbx_lua_link_t{.id = "link"}});
    auto view = inspector_frontend();
    sys::build_storyboard_inspector_view(view, song, {});
    check_inspector(!view.inspector.editable && !view.inspector.reason.empty());
    const auto plan = sys::plan_storyboard_inspector_edit(song, storyboard_id_t{3},
        {.field = ui::storyboard_inspector_field_t::lifetime, .node = "sprite"});
    REQUIRE_FALSE(plan);
    check_inspector(plan.error() == failure_code_t::unavailable);
}

TEST_CASE("Storyboard inspector rejects malformed field text before producing an edit",
          "[unit][sys][storyboard-inspector]") {
    const auto song = inspector_song();
    const auto plan = sys::plan_storyboard_inspector_edit(song, storyboard_id_t{3},
        {.field = ui::storyboard_inspector_field_t::order, .node = "sprite", .has_valid_value = false});
    REQUIRE_FALSE(plan);
    check_inspector(plan.error() == failure_code_t::invalid_data);
}

TEST_CASE("Storyboard inspector samples prepared Lua values while retaining authored readonly guards",
          "[unit][sys][storyboard-inspector]") {
    auto song = inspector_song();
    auto& node = song.storyboards[0].osbx->nodes[0];
    node.tracks.opacity.lua = osbx_lua_track_link_t{};
    auto prepared = *song.storyboards[0].osbx;
    prepared.nodes[0].tracks.opacity.lua.reset();
    prepared.nodes[0].tracks.opacity.default_value = {0.125};
    auto view = inspector_frontend();
    sys::build_storyboard_inspector_view(view, song, {}, nullptr, &prepared);
    check_inspector(view.inspector.opacity.value == std::vector<double>{0.125});
    check_inspector(!view.inspector.opacity.editable && !view.inspector.opacity.reason.empty());
}

TEST_CASE("Storyboard inspector exposes authored image choices with no selected node",
          "[unit][sys][storyboard-inspector]") {
    auto song = inspector_song(); song.storyboards[0].osbx->nodes.clear();
    auto prepared = *song.storyboards[0].osbx;
    prepared.images.push_back({.id = "materialized-image", .file = "generated.png"});
    auto view = inspector_frontend(); view.state.selection.nodes.clear();
    sys::build_storyboard_inspector_view(view, song, {}, nullptr, &prepared);
    check_inspector(view.inspector.node.empty() && !view.inspector.editable);
    REQUIRE(view.inspector.images.size() == 1);
    check_inspector(view.inspector.images[0].id == "image");
}

TEST_CASE("Storyboard shared inspector guard and transform labels retain one authored policy",
          "[unit][sys][storyboard-inspector]") {
    auto song = inspector_song(); auto& storyboard = song.storyboards[0];
    check_inspector(sys::storyboard_node_edit_reason(storyboard, "sprite").empty());
    check_inspector(!sys::storyboard_node_edit_reason(storyboard, "materialized-only").empty());
    storyboard.owning_generator_recipe = recipe_id_t{40};
    check_inspector(!sys::storyboard_node_edit_reason(storyboard, "sprite").empty());
    storyboard.owning_generator_recipe.reset(); storyboard.osbx.reset(); storyboard.model.emplace();
    check_inspector(!sys::storyboard_node_edit_reason(storyboard, "sprite").empty());
    check_inspector(!sys::storyboard_transform_label(osbx_transform_kind_t::rotate).empty());
    check_inspector(sys::storyboard_transform_label(static_cast<osbx_transform_kind_t>(999)).empty());
}

TEST_CASE("Storyboard inspector plans defaults and occupied or new auto keys with one policy",
          "[unit][sys][storyboard-inspector]") {
    const osbx_track_target_t target{.node = "sprite", .kind = osbx_track_kind_t::opacity};
    osbx_track_t<> track{.default_value = {1}};
    const auto plain = sys::plan_storyboard_track_edit(track, target, std::vector<double>{0.5}, 50);
    REQUIRE(plain);
    check_inspector(std::holds_alternative<osbx_set_default_t>(*plain));
    track.keys = {{.id = "existing", .time_ms = 50, .value = {1}, .easing = osbx_step_easing_t{}}};
    const auto occupied = sys::plan_storyboard_track_edit(track, target, std::vector<double>{0.5}, 50);
    REQUIRE(occupied);
    check_inspector(std::get<osbx_set_key_value_t>(*occupied).key == "existing");
    const auto between = sys::plan_storyboard_track_edit(track, target, std::vector<double>{0.25}, 60);
    REQUIRE(between);
    check_inspector(std::get<osbx_add_key_t>(*between).time_ms == 60);
    const auto stale = sys::plan_storyboard_track_edit(track, target, std::vector<double>{0.5}, 50, "different-key");
    REQUIRE_FALSE(stale);
    check_inspector(stale.error() == failure_code_t::stale_generation);
}

TEST_CASE("Storyboard inspector track plan uses the shared transaction and preserves lifetime",
          "[unit][sys][storyboard-inspector]") {
    auto history = document_history_t::from(inspector_song());
    const auto before = history.document;
    const auto& op = inspector_node(history).ops[0];
    const auto plan = sys::plan_storyboard_track_edit(op.track,
        {.node = "sprite", .kind = osbx_track_kind_t::transform, .op = op.id},
        std::vector<double>{20, 30}, 50);
    REQUIRE(plan);
    const auto applied = apply_osbx_edit(history, {.storyboard = storyboard_id_t{3}, .change = *plan});
    REQUIRE(applied.applied);
    check_inspector(history.undo.size() == 1);
    check_inspector(inspector_node(history).ops[0].track.keys.empty());
    check_inspector(inspector_node(history).ops[0].track.default_value == std::vector<double>{20, 30});
    check_inspector(inspector_node(history).lifetime == before.storyboards[0].osbx->nodes[0].lifetime);
    REQUIRE(undo_document_command(history));
    check_inspector(history.document.storyboards == before.storyboards);
}

TEST_CASE("Storyboard inspector auto-key updates preserve key identity easing and defaults",
          "[unit][sys][storyboard-inspector]") {
    auto song = inspector_song();
    song.storyboards[0].osbx->nodes[0].tracks.color.keys = {
        {.id = "tint", .time_ms = -2, .value = {1, 1, 1, 1}, .easing = osbx_step_easing_t{}}};
    auto history = document_history_t::from(std::move(song));
    const osbx_track_target_t target{.node = "sprite", .kind = osbx_track_kind_t::color};
    const auto plan = sys::plan_storyboard_track_edit(inspector_node(history).tracks.color, target,
        std::vector<double>{0.5, 0.5, 0.5, 1}, -2);
    REQUIRE(plan);
    REQUIRE(apply_osbx_edit(history, {.storyboard = storyboard_id_t{3}, .change = *plan}).applied);
    const auto& track = inspector_node(history).tracks.color;
    REQUIRE(track.keys.size() == 1);
    check_inspector(track.keys[0].id == "tint");
    check_inspector(std::holds_alternative<osbx_step_easing_t>(track.keys[0].easing));
    check_inspector(track.default_value == std::vector<double>{1, 1, 1, 1});
    check_inspector(history.undo.size() == 1);
}

TEST_CASE("Storyboard inspector Lua-linked values refuse direct planning",
          "[unit][sys][storyboard-inspector]") {
    osbx_track_t<> track{.default_value = {1}, .lua = osbx_lua_track_link_t{}};
    const auto plan = sys::plan_storyboard_track_edit(track,
        {.node = "sprite", .kind = osbx_track_kind_t::opacity}, std::vector<double>{0}, 0);
    REQUIRE_FALSE(plan);
    check_inspector(plan.error() == failure_code_t::unavailable);
}

TEST_CASE("Storyboard inspector reorders noncommuting transforms by stable op ID",
          "[unit][sys][storyboard-inspector]") {
    auto history = document_history_t::from(inspector_song());
    const auto before = inspector_world(history);
    REQUIRE(apply_osbx_edit(history, {.storyboard = storyboard_id_t{3},
        .change = osbx_reorder_op_t{.node = "sprite", .op = "rotate", .index = 0}}).applied);
    check_inspector(inspector_node(history).ops[0].id == "rotate");
    const auto after = inspector_world(history);
    check_inspector(before != after);
    check_inspector(before[4] == Catch::Approx(0).margin(1e-9));
    check_inspector(before[5] == Catch::Approx(10));
    check_inspector(after[4] == Catch::Approx(10));
    check_inspector(after[5] == Catch::Approx(0).margin(1e-9));
    check_inspector(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    check_inspector(inspector_world(history) == before);
}

TEST_CASE("Storyboard inspector lifetime set clear and invalid range use typed edits",
          "[unit][sys][storyboard-inspector]") {
    auto history = document_history_t::from(inspector_song());
    const auto ops = inspector_node(history).ops;
    REQUIRE(apply_osbx_edit(history, {.storyboard = storyboard_id_t{3},
        .change = osbx_set_lifetime_t{.node = "sprite",
            .lifetime = osbx_lifetime_t{.begin_ms = -50, .end_ms = -10}}}).applied);
    check_inspector(inspector_node(history).lifetime->begin_ms == -50);
    check_inspector(inspector_node(history).lifetime->end_ms == -10);
    const auto before_refusal = history.document;
    const auto refused = apply_osbx_edit(history, {.storyboard = storyboard_id_t{3},
        .change = osbx_set_lifetime_t{.node = "sprite",
            .lifetime = osbx_lifetime_t{.begin_ms = 10, .end_ms = -10}}});
    check_inspector(!refused.applied && !refused.diagnostics.empty());
    test::check_same_document(history.document, before_refusal);
    check_inspector(history.undo.size() == 1);
    REQUIRE(apply_osbx_edit(history, {.storyboard = storyboard_id_t{3},
        .change = osbx_set_lifetime_t{.node = "sprite", .lifetime = std::nullopt}}).applied);
    check_inspector(!inspector_node(history).lifetime);
    check_inspector(inspector_node(history).ops == ops);
    check_inspector(history.undo.size() == 2);
}

TEST_CASE("Storyboard inspector auto-key scrub stages insertion through the shared gesture runner",
          "[unit][sys][storyboard-inspector]") {
    auto song = inspector_song();
    song.storyboards[0].osbx->nodes[0].tracks.opacity.keys = {
        {.id = "fade", .time_ms = 0, .value = {0}}};
    auto history = document_history_t::from(std::move(song));
    editor_interaction_state_t state;
    const auto before = history.document;
    const osbx_track_target_t target{.node = "sprite", .kind = osbx_track_kind_t::opacity};
    const auto first = sys::plan_storyboard_track_edit(inspector_node(history).tracks.opacity,
        target, std::vector<double>{0.25}, 50);
    REQUIRE(first);
    REQUIRE(dispatch_editor_action(history, state, begin_osbx_edit_gesture_action_t{
        .edit = {.storyboard = storyboard_id_t{3}, .change = *first}}));
    const auto revised = sys::plan_storyboard_track_edit(inspector_node(history).tracks.opacity,
        target, std::vector<double>{0.75}, 50);
    REQUIRE(revised);
    REQUIRE(dispatch_editor_action(history, state, update_osbx_edit_gesture_action_t{.change = *revised}));
    test::check_same_document(history.document, before);
    check_inspector(history.undo.empty());
    check_auto_key_commit_and_undo(history, state, *revised, before);
}

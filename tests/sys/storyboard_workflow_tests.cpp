#include "sys/app/application_command_state.h"
#include "../support/document_assertions.h"
#include <wormedit/core/osbx_edit.h>
#include <wormedit/core/osbx_osb_export.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/formats/native_project_storage.h>
#include <wormedit/formats/osu_mania.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <array>

namespace {
using namespace wormedit;
using namespace wormedit::ui;
auto workflow_song() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 1,
        .visual_assets = {{.id = asset_id_t{7}, .kind = asset_kind_t::image, .source_path = "sprite.png"}},
        .resources = {{.id = resource_id_t{8}, .name = "Sprite", .asset = asset_id_t{7}, .roles = {resource_role_t::bga}}}};
}
struct authoring_workflow_t {
    document_history_t history{document_history_t::from(workflow_song())};
    workspace_controller_t workspace;
    sys::application_command_state_t application;
    editor_interaction_state_t editor;
    notification_center_t notifications;
    notification_sink_t sink{notifications};
    storyboard_view_t view;
    storyboard_id_t board;
    std::string group, sprite, image, rotation;
    void project() {
        application.project.storyboard.refill(view, history.document, 7);
        view.canvas.prepared_generation = osbx_lua_prepared_scope_t::generation();
    }
    void initialize() {
        const auto bound = workspace_controller_t::from(history); REQUIRE(bound.value);
        workspace = *bound.value; workspace.active_tab = workspace_tab_t::storyboard;
        project();
    }
    auto action(storyboard_action_kind_t kind) const -> storyboard_action_t {
        return storyboard_action_t::from(view, kind, board);
    }
    void dispatch(const storyboard_action_t& request) {
        REQUIRE(application.project.storyboard.dispatch(request,
            {.history = history, .workspace = workspace, .notifications = sink}, application, &editor, false));
        project();
    }
    auto scene() const -> const osbx_document_t& {
        const auto found = std::ranges::find(history.document.storyboards, board, &storyboard_document_t::id);
        REQUIRE(found != history.document.storyboards.end()); REQUIRE(found->osbx);
        return *found->osbx;
    }
    void select_node(const std::string& node) {
        auto request = action(storyboard_action_kind_t::select_nodes); request.nodes = {node}; dispatch(request);
    }
    void create_and_add_image() {
        dispatch(action(storyboard_action_kind_t::create));
        board = view.state.selection.storyboard; REQUIRE(board);
        auto capture = action(storyboard_action_kind_t::assets);
        capture.assets = {.kind = storyboard_assets_action_kind_t::add_image, .resource = resource_id_t{8}};
        REQUIRE(application.project.storyboard.complete_assets_import(capture,
            {.source = osbx_resource_image_t{.resource = resource_id_t{8}, .asset = asset_id_t{7},
                .source_path = "sprite.png", .file = "sprite.png"}},
            {.history = history, .workspace = workspace, .notifications = sink}, application, &editor, false));
        project(); REQUIRE(scene().images.size() == 1); image = scene().images.front().id;
    }
    void add_nodes_and_reparent() {
        auto request = action(storyboard_action_kind_t::hierarchy);
        request.hierarchy.kind = storyboard_hierarchy_mutation_kind_t::add_group; dispatch(request);
        group = view.state.selection.nodes.front();
        request = action(storyboard_action_kind_t::assets);
        request.assets = {.kind = storyboard_assets_action_kind_t::add_sprite, .image = image, .position = {100, 0}};
        dispatch(request); sprite = view.state.selection.nodes.front();
        request = action(storyboard_action_kind_t::hierarchy);
        request.hierarchy = {.kind = storyboard_hierarchy_mutation_kind_t::drop, .node = sprite, .parent = group};
        dispatch(request);
    }
    void set_rotation_key(std::int64_t time, double value) {
        auto request = action(storyboard_action_kind_t::inspector);
        request.inspector = {.field = storyboard_inspector_field_t::transform, .node = group, .op = rotation,
            .value = {value}, .key_time_ms = time};
        dispatch(request);
    }
    void add_rotation_keys() {
        select_node(group);
        auto request = action(storyboard_action_kind_t::inspector);
        request.inspector = {.field = storyboard_inspector_field_t::add_op, .node = group,
            .op_kind = osbx_transform_kind_t::rotate}; dispatch(request);
        rotation = std::ranges::find(scene().nodes, group, &osbx_node_t::id)->ops.back().id;
        // A first key must be added explicitly; inspector edits then update its value.
        for (const auto time : {0, 2000}) {
            request = action(storyboard_action_kind_t::timeline);
            request.timeline = {.kind = storyboard_timeline_action_kind_t::add_key,
                .track = {.node = group, .kind = osbx_track_kind_t::transform, .op = rotation},
                .time = {.milliseconds = time}}; dispatch(request);
            set_rotation_key(time, time == 0 ? 0 : 90);
        }
    }
    void ease_rotation() {
        const auto& keys = std::ranges::find(scene().nodes, group, &osbx_node_t::id)->ops.back().track.keys;
        auto request = action(storyboard_action_kind_t::timeline);
        request.timeline = {.kind = storyboard_timeline_action_kind_t::select_keys,
            .track = {.node = group, .kind = osbx_track_kind_t::transform, .op = rotation}};
        request.timeline.keys = {{.track = request.timeline.track, .key = keys.front().id}}; dispatch(request);
        request = action(storyboard_action_kind_t::timeline);
        request.timeline = {.kind = storyboard_timeline_action_kind_t::easing,
            .track = {.node = group, .kind = osbx_track_kind_t::transform, .op = rotation},
            .keys = {{.track = {.node = group, .kind = osbx_track_kind_t::transform, .op = rotation},
                      .key = view.state.selection.keys.front()}}, .easing = osbx_osb_easing_t{.value = 1}};
        dispatch(request);
    }
};
void check_authored_scene(const authoring_workflow_t& flow) {
    const auto& scene = flow.scene(); REQUIRE(scene.nodes.size() == 2);
    const auto node = std::ranges::find(scene.nodes, flow.sprite, &osbx_node_t::id);
    REQUIRE(node != scene.nodes.end());
    CHECK(node->parent == flow.group); CHECK(node->image == flow.image);
    CHECK(flow.workspace.document_revision == flow.history.document.revision);
}
void check_rotation(const authoring_workflow_t& flow) {
    const auto& track = std::ranges::find(flow.scene().nodes, flow.group, &osbx_node_t::id)->ops.back().track;
    REQUIRE(track.keys.size() == 2);
    CHECK(track.keys[0].time_ms == 0); CHECK(track.keys[1].time_ms == 2000);
    CHECK(track.keys[1].value == std::vector<double>{90});
    CHECK(track.keys[0].easing == osbx_easing_t{osbx_osb_easing_t{.value = 1}});
}
auto roundtrip(const song_document_t& song) -> song_document_t {
    const auto written = write_native_project(song); REQUIRE(written.value);
    const auto loaded = parse_native_project(*written.value); REQUIRE(loaded.value);
    test::check_same_document(*loaded.value, song);
    return *loaded.value;
}
void check_baked_classification(const osbx_document_t& scene, const osbx_osb_options_t& options) {
    const auto report = convert_osbx_to_osb(scene, options);
    REQUIRE(report.value.model);
    REQUIRE(report.value.report.size() == 1);
    CHECK(report.value.report[0].classification == osbx_osb_class_t::baked);
}
void check_baked_export(const authoring_workflow_t& flow) {
    const auto song = roundtrip(flow.history.document);
    const std::array sizes{osbx_osb_image_size_t{.image_id = flow.image, .width = 64, .height = 64}};
    osu_mania_write_options_t options;
    options.osbx.image_sizes = sizes; options.osbx.playback_span = osbx_osb_span_t{0, 2000};
    const auto exported = write_osu_storyboard(song, options); REQUIRE(exported.value);
    CHECK(std::ranges::any_of(exported.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == osu_mania_write_diagnostic_code_t::storyboard_bake;
    }));
    const auto parsed = parse_osb(*exported.value); REQUIRE(parsed.value);
    CHECK(parsed.value->sprites.size() == 1);
    check_baked_classification(flow.scene(), options.osbx);
}
void check_imported_readonly(authoring_workflow_t& flow) {
    const auto before = flow.history.document;
    auto request = flow.action(storyboard_action_kind_t::hierarchy);
    request.hierarchy.kind = storyboard_hierarchy_mutation_kind_t::add_group;
    flow.dispatch(request);
    test::check_same_document(flow.history.document, before);
    CHECK(flow.history.undo.empty());
}
} // namespace

TEST_CASE("Storyboard acceptance workflow retains hierarchy easing native bytes and an actual baked osu report",
          "[sys][storyboard-workflow]") {
    authoring_workflow_t flow; flow.initialize(); flow.create_and_add_image();
    flow.add_nodes_and_reparent(); flow.add_rotation_keys(); flow.ease_rotation();
    check_authored_scene(flow); check_rotation(flow); check_baked_export(flow);
}

TEST_CASE("Storyboard acceptance converts imported OSB into a distinct editable persistent copy",
          "[sys][storyboard-workflow][convert]") {
    authoring_workflow_t flow;
    const auto imported = parse_osb("Sprite,Background,Centre,sprite.png,100,0\n F,0,0,2000,1,1\n");
    REQUIRE(imported.value);
    flow.history.document.storyboards = {{.id = storyboard_id_t{11}, .name = "Imported", .model = *imported.value}};
    flow.initialize(); flow.board = storyboard_id_t{11}; flow.dispatch(flow.action(storyboard_action_kind_t::select));
    check_imported_readonly(flow);
    const auto original = flow.history.document.storyboards.front();
    auto request = flow.action(storyboard_action_kind_t::inspector);
    request.inspector.field = storyboard_inspector_field_t::convert; flow.dispatch(request);
    CHECK(flow.view.state.selection.storyboard != original.id);
    CHECK(flow.history.document.storyboards.front() == original);
    CHECK(flow.history.undo.size() == 1);
    const auto loaded = roundtrip(flow.history.document);
    CHECK(loaded.storyboards.back().osbx.has_value());
}

namespace {
auto generated_workflow_song() -> song_document_t {
    auto song = workflow_song();
    song.charts = {{.id = chart_id_t{100}, .measures = {{.id = measure_id_t{101}}}}};
    song.storyboards = {{.id = storyboard_id_t{11}, .name = "Generated", .osbx = osbx_document_t{},
        .owning_generator_recipe = recipe_id_t{950}}};
    const std::string source = "return {}";
    song.generator_recipes = {{.id = recipe_id_t{950}, .api_version = "1", .source_path = "scripts/generated.lua",
        .source_hash = native_project_content_checksum(source), .source = source, .chart = chart_id_t{100},
        .generated_storyboards = {{.key = "scene", .storyboard = storyboard_id_t{11}}}}};
    return song;
}
void check_detach_persistence(const authoring_workflow_t& flow) {
    const auto loaded = roundtrip(flow.history.document);
    CHECK_FALSE(loaded.storyboards.front().owning_generator_recipe);
    CHECK(loaded.generator_recipes.front().generated_storyboards.empty());
    CHECK(flow.history.undo.size() == 1);
}
} // namespace

TEST_CASE("Storyboard acceptance Detach persists the same scene and Undo restores both ownership records",
          "[sys][storyboard-workflow][detach]") {
    authoring_workflow_t flow; flow.history = document_history_t::from(generated_workflow_song());
    flow.initialize(); flow.board = storyboard_id_t{11}; flow.dispatch(flow.action(storyboard_action_kind_t::select));
    const auto before = flow.history.document;
    auto request = flow.action(storyboard_action_kind_t::detach_generator); request.expected_owner = recipe_id_t{950};
    flow.dispatch(request); check_detach_persistence(flow);
    CHECK(flow.history.document.storyboards.front().osbx == before.storyboards.front().osbx);
    REQUIRE(undo_document_command(flow.history));
    CHECK(flow.history.document.storyboards == before.storyboards);
    CHECK(flow.history.document.generator_recipes == before.generator_recipes);
    REQUIRE(redo_document_command(flow.history)); check_detach_persistence(flow);
}

TEST_CASE("Storyboard acceptance stale asset completion preserves authored graph selection and Undo",
          "[sys][storyboard-workflow][stale]") {
    authoring_workflow_t flow; flow.initialize(); flow.create_and_add_image(); flow.add_nodes_and_reparent();
    const auto before = flow.history.document; const auto selection = flow.view.state.selection;
    const auto undo = flow.history.undo.size();
    auto capture = flow.action(storyboard_action_kind_t::assets); --capture.revision;
    CHECK_FALSE(flow.application.project.storyboard.complete_assets_import(capture,
        {.source = osbx_existing_image_t{flow.image}, .sprite = osbx_sprite_placement_t{}},
        {.history = flow.history, .workspace = flow.workspace, .notifications = flow.sink}, flow.application, &flow.editor, false));
    test::check_same_document(flow.history.document, before);
    CHECK(flow.history.undo.size() == undo); CHECK(flow.application.project.storyboard.state().selection == selection);
}

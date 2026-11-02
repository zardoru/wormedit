#include "sys/storyboard/storyboard_hierarchy_view.h"
#include "../support/document_assertions.h"
#include <catch2/catch_test_macros.hpp>
#include <wormedit/core/storyboard_canvas.h>
#include "sys/storyboard/storyboard_scene_presenter.h"
#include <wormedit/core/osbx_lua_link.h>
#include <wormedit/formats/native_project_storage.h>
#include <algorithm>
#include <source_location>

namespace {
using namespace wormedit;
void check_hierarchy(bool value, std::source_location where = std::source_location::current()) {
    INFO(where.file_name() << ':' << where.line());
    CHECK(value);
}
auto hierarchy_song() -> song_document_t {
    osbx_document_t scene{.images = {{.id = "image", .file = "image.png"}}, .nodes = {
        {.id = "tail", .order = 20},
        {.id = "child-b", .kind = osbx_node_kind_t::sprite, .parent = "root", .order = 4, .image = "image"},
        {.id = "root", .order = 0},
        {.id = "child-a", .parent = "root", .order = 4},
        {.id = "foreground", .layer = osbx_layer_t::foreground}}};
    return {.id = song_id_t{1}, .revision = 3, .storyboards = {
        {.id = storyboard_id_t{11}, .name = "Scene", .osbx = std::move(scene)}}};
}
auto hierarchy_frontend() -> ui::storyboard_view_t {
    ui::storyboard_view_t view{.visible = true, .project = song_id_t{1}, .revision = 3,
        .storyboards = {{.id = storyboard_id_t{11}, .name = "Scene", .kind = ui::storyboard_kind_t::osbx}}};
    view.refill_state({.selection = {.storyboard = storyboard_id_t{11}, .nodes = {"child-b"}}});
    return view;
}
} // namespace

// The row contract is shared with the canvas through ui/frontend/storyboard.h;
// only the hierarchy projection is under test, never an alternate selection owner.
TEST_CASE("Storyboard hierarchy preserves sibling draw order and stable node selection",
          "[unit][sys][storyboard-hierarchy]") {
    const auto song = hierarchy_song();
    auto view = hierarchy_frontend();
    sys::build_storyboard_hierarchy_view(view, song);
    const auto& rows = view.hierarchy.layers[0].rows;
    REQUIRE(rows.size() == 4);
    check_hierarchy(rows[0].id == "root" && rows[0].depth == 0);
    check_hierarchy(rows[1].id == "child-b" && rows[1].depth == 1);
    check_hierarchy(rows[2].id == "child-a" && rows[2].depth == 1);
    check_hierarchy(rows[3].id == "tail" && rows[3].depth == 0);
    check_hierarchy(rows[1].selected && !rows[2].selected);
    check_hierarchy(view.state.selection.nodes == std::vector<std::string>{"child-b"});
}
TEST_CASE("Storyboard hierarchy uses the canonical layer catalog and retains empty conditional layers",
          "[unit][sys][storyboard-hierarchy]") {
    const auto song = hierarchy_song();
    auto view = hierarchy_frontend();
    sys::build_storyboard_hierarchy_view(view, song);
    for (std::size_t i = 0; i < osbx_layer_names.size(); ++i)
        check_hierarchy(view.hierarchy.layers[i].title == osbx_layer_names[i]);
    check_hierarchy(view.hierarchy.layers[1].rows.empty());
    check_hierarchy(view.hierarchy.layers[2].rows.empty());
    check_hierarchy(view.hierarchy.layers[3].rows.size() == 1);
}
TEST_CASE("Storyboard hierarchy projects shared unsupported export reasons without changing the song",
          "[unit][sys][storyboard-hierarchy]") {
    const auto song = hierarchy_song();
    const auto before = song;
    auto view = hierarchy_frontend();
    const std::array reports{osbx_osb_report_t{.node_id = "child-b",
        .classification = osbx_osb_class_t::unsupported, .reason = "Shared classifier reason"}};
    sys::build_storyboard_hierarchy_view(view, song, reports);
    check_hierarchy(view.hierarchy.layers[0].rows[1].warning == "Shared classifier reason");
    test::check_same_document(song, before);
}
TEST_CASE("Storyboard hierarchy consumes the OSBX export owner's real unsupported report",
          "[unit][sys][storyboard-hierarchy]") {
    auto song = hierarchy_song();
    auto& scene = *song.storyboards[0].osbx;
    scene.nodes[1].ops.push_back({.id = "shear", .kind = osbx_transform_kind_t::skew,
        .track = {.default_value = {0.1, 0}}});
    const std::array sizes{osbx_osb_image_size_t{.image_id = "image", .width = 32, .height = 32}};
    const auto classified = convert_osbx_to_osb(scene, {.image_sizes = sizes});
    REQUIRE(!classified.value.report.empty());
    auto view = hierarchy_frontend();
    sys::build_storyboard_hierarchy_view(view, song, classified.value.report);
    check_hierarchy(!view.hierarchy.layers[0].rows[1].warning.empty());
    check_hierarchy(view.hierarchy.layers[0].rows[1].warning == classified.value.report[0].reason);
}
TEST_CASE("Storyboard hierarchy retains valid rows beside a missing image reference",
          "[unit][sys][storyboard-hierarchy]") {
    auto song = hierarchy_song();
    song.storyboards[0].osbx->nodes[1].image = "missing";
    auto view = hierarchy_frontend();
    sys::build_storyboard_hierarchy_view(view, song);
    const auto& rows = view.hierarchy.layers[0].rows;
    REQUIRE(rows.size() == 4);
    check_hierarchy(!rows[1].warning.empty());
    check_hierarchy(rows[0].warning.empty() && rows[2].warning.empty() && rows[3].warning.empty());
    check_hierarchy(rows[1].id == "child-b");
}
TEST_CASE("Storyboard hierarchy remains selectable and read-only for generated content",
          "[unit][sys][storyboard-hierarchy]") {
    auto song = hierarchy_song();
    song.storyboards[0].owning_generator_recipe = recipe_id_t{40};
    auto view = hierarchy_frontend();
    sys::build_storyboard_hierarchy_view(view, song);
    const auto& rows = view.hierarchy.layers[0].rows;
    REQUIRE(rows.size() == 4);
    check_hierarchy(rows[1].selected);
    check_hierarchy(std::ranges::none_of(rows, &ui::storyboard_hierarchy_row_t::editable));
}
TEST_CASE("Storyboard hierarchy clears old rows when its selected storyboard is missing",
          "[unit][sys][storyboard-hierarchy]") {
    const auto song = hierarchy_song();
    auto view = hierarchy_frontend();
    sys::build_storyboard_hierarchy_view(view, song);
    view.state.selection.storyboard = storyboard_id_t{999};
    sys::build_storyboard_hierarchy_view(view, song);
    for (const auto& layer : view.hierarchy.layers) check_hierarchy(layer.rows.empty());
}
TEST_CASE("Storyboard hierarchy caches topology while shared node selection changes",
          "[unit][sys][storyboard-hierarchy]") {
    const auto song = hierarchy_song();
    auto view = hierarchy_frontend();
    sys::storyboard_hierarchy_projection_t projection;
    projection.refill(view, song);
    const auto builds = projection.build_count();
    const auto* rows = view.hierarchy.layers[0].rows.data();
    view.state.selection.nodes = {"tail"};
    projection.refill(view, song);
    check_hierarchy(projection.build_count() == builds);
    check_hierarchy(view.hierarchy.layers[0].rows.data() == rows);
    check_hierarchy(!view.hierarchy.layers[0].rows[1].selected);
    check_hierarchy(view.hierarchy.layers[0].rows[3].selected);
}

TEST_CASE("Storyboard hierarchy shares imported OSB node IDs with the canvas",
          "[unit][sys][storyboard-hierarchy]") {
    auto song = hierarchy_song();
    song.storyboards[0].osbx.reset();
    song.storyboards[0].model = storyboard_document_model_t{.sprites = {
        {.stable_id = 42, .source_order = 2, .layer = "Background", .path = "second.png"},
        {.stable_id = 9, .source_order = 1, .layer = "Background", .path = "first.png"}}};
    auto view = hierarchy_frontend();
    const auto id = storyboard_canvas_target_t::legacy_node_id(9);
    view.state.selection.nodes = {id};
    sys::build_storyboard_hierarchy_view(view, song);
    const auto& rows = view.hierarchy.layers[0].rows;
    REQUIRE(rows.size() == 2);
    check_hierarchy(rows[0].id == id && rows[0].selected);
    check_hierarchy(!rows[0].editable && !rows[1].editable);
    check_hierarchy(rows[1].id == storyboard_canvas_target_t::legacy_node_id(42));
}
TEST_CASE("Storyboard hierarchy projects editor-hidden IDs without changing authored visibility",
          "[unit][sys][storyboard-hierarchy]") {
    const auto song = hierarchy_song();
    auto view = hierarchy_frontend();
    sys::storyboard_hierarchy_projection_t projection;
    projection.refill(view, song);
    const auto builds = projection.build_count();
    view.state.hidden_nodes = {"child-b"};
    projection.refill(view, song);
    check_hierarchy(view.hierarchy.layers[0].rows[1].editor_hidden);
    check_hierarchy(!view.hierarchy.layers[0].rows[0].editor_hidden);
    check_hierarchy(projection.build_count() == builds);
    check_hierarchy(song.storyboards[0].osbx->nodes[1].tracks.visibility.default_value);
}

TEST_CASE("Storyboard hierarchy overlays shared scene image diagnostics without rebuilding topology",
          "[unit][sys][storyboard-hierarchy]") {
    const auto song = hierarchy_song();
    auto view = hierarchy_frontend();
    sys::storyboard_scene_presenter_t scenes;
    scenes.begin(song, 7, {});
    std::vector<ui::player_preview_storyboard_sprite_view_t> sprites;
    (void)scenes.refill(sprites, storyboard_id_t{11}, {}, 1024);
    const auto reason = scenes.node_diagnostic(storyboard_id_t{11}, "child-b");
    REQUIRE(!reason.empty());
    sys::storyboard_hierarchy_projection_t projection;
    projection.refill(view, song, &scenes);
    const auto builds = projection.build_count();
    check_hierarchy(view.hierarchy.layers[0].rows[1].warning == reason);
    projection.refill(view, song, &scenes);
    check_hierarchy(projection.build_count() == builds);
    check_hierarchy(view.hierarchy.layers[0].rows[1].warning == reason);
    check_hierarchy(view.hierarchy.layers[0].rows[0].warning.empty());
}
TEST_CASE("Storyboard hierarchy rebuilds on prepared generation and marks generated-only IDs read-only",
          "[unit][sys][storyboard-hierarchy]") {
    auto song = hierarchy_song();
    auto& source = *song.storyboards[0].osbx;
    const std::string code = "error('projection must not run Lua')";
    source.nodes[0].children_link = osbx_lua_link_t{.id = "children", .source = {
        .frozen_source = code, .source_reference = "scripts/source.lua",
        .source_hash = native_project_content_checksum(code)}};
    auto prepared = source;
    prepared.nodes[0].children_link.reset();
    prepared.nodes.push_back({.id = "generated-child", .parent = "tail", .order = 21});
    const std::array results{osbx_lua_prepared_scene_t{.stamp = document_stamp_t::from(song),
        .storyboard = storyboard_id_t{11}, .source = source,
        .scene = std::make_shared<const osbx_document_t>(prepared), .complete = true}};
    const osbx_lua_prepared_scope_t scope{results, 9};
    sys::storyboard_scene_presenter_t scenes;
    scenes.begin(song, 7, {});
    std::vector<ui::player_preview_storyboard_sprite_view_t> sprites;
    (void)scenes.refill(sprites, storyboard_id_t{11}, {}, 1024);
    auto view = hierarchy_frontend();
    view.canvas.prepared_generation = 9;
    sys::storyboard_hierarchy_projection_t projection;
    projection.refill(view, song, &scenes);
    const auto& rows = view.hierarchy.layers[0].rows;
    const auto generated = std::ranges::find(rows, "generated-child", &ui::storyboard_hierarchy_row_t::id);
    REQUIRE(generated != rows.end());
    check_hierarchy(!generated->editable && generated->parent == "tail");
    const auto builds = projection.build_count();
    ++view.canvas.prepared_generation;
    projection.refill(view, song, &scenes);
    check_hierarchy(projection.build_count() == builds + 1);
    check_hierarchy(song.revision == 3);
}

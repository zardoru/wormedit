#include "sys/storyboard/storyboard_canvas_view.h"
#include "sys/storyboard/storyboard_scene_presenter.h"
#include <wormedit/core/osbx_document.h>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
using namespace wormedit;
namespace {
void check_selected_sprite_and_group_outlines(const ui::storyboard_view_t& view) {
    REQUIRE(view.canvas.outlines.size() == 2);
    CHECK(view.canvas.outlines[0].origin == visual_point_t{320, 240});
    CHECK(view.canvas.outlines[0].points[0] == visual_point_t{285, 185});
    CHECK(view.canvas.outlines[1].group);
    CHECK(view.canvas.outlines[1].points[0] == visual_point_t{255, 185});
    CHECK(view.canvas.outlines[1].points[2] == visual_point_t{385, 295});
}

void check_fractional_lifetime_canvas(const ui::storyboard_canvas_view_t& canvas) {
    CHECK(canvas.available);
    CHECK(canvas.sprites.empty());
    CHECK(canvas.time_ms == Catch::Approx(1.0 / 48));
}

void check_atomic_canvas_refusal(const ui::storyboard_canvas_view_t& canvas) {
    CHECK_FALSE(canvas.available);
    CHECK_FALSE(canvas.status.empty());
    CHECK(canvas.sprites.empty());
}
} // namespace

TEST_CASE("Storyboard canvas outlines transformed sprites and selected group child bounds", "[storyboard-canvas][sys]") {
    ui::storyboard_view_t view;
    view.state.selection.nodes = {"sprite", "group"};
    view.canvas.sprites = {{.node_id = "sprite", .transform = {1, 0.5, -0.5, 1, 320, 240},
        .origin_x = 0.5, .origin_y = 0.5, .opacity = 1, .image_ready = true, .image_width = 100, .image_height = 60}};
    osbx_document_t scene{.nodes = {{.id = "group"}, {.id = "sprite", .kind = osbx_node_kind_t::sprite, .parent = "group"}}};
    sys::refill_storyboard_canvas_outlines(view, &scene);
    check_selected_sprite_and_group_outlines(view);
    view.canvas.sprites.clear();
    sys::refill_storyboard_canvas_outlines(view, &scene);
    CHECK(view.canvas.outlines.empty());
}
TEST_CASE("Storyboard shared scene binds exact fractional time session and stable node IDs without a chart", "[storyboard-canvas][sys]") {
    song_document_t song{.id = song_id_t{1}, .revision = 3,
        .storyboards = {{.id = storyboard_id_t{11}, .osbx = osbx_document_t{
            .images = {{.id = "image", .file = "sprite.png"}},
            .nodes = {{.id = "actual-ID", .kind = osbx_node_kind_t::sprite, .image = "image",
                .lifetime = osbx_lifetime_t{0, 0}}}}}}};
    sys::storyboard_scene_presenter_t scenes;
    scenes.begin(song, 9, {});
    ui::storyboard_canvas_view_t canvas;
    sys::populate_storyboard_canvas(canvas, scenes, storyboard_id_t{11}, storyboard_time_t::from_frame(0, 48000));
    REQUIRE(canvas.available);
    REQUIRE(canvas.sprites.size() == 1);
    CHECK(canvas.sprites[0].storyboard == storyboard_id_t{11});
    CHECK(canvas.sprites[0].node_id == "actual-ID");
    sys::populate_storyboard_canvas(canvas, scenes, storyboard_id_t{11}, storyboard_time_t::from_frame(1, 48000));
    check_fractional_lifetime_canvas(canvas);
    canvas.reset_frame();
    song.storyboards.clear();
    scenes.begin(song, 10, {});
    CHECK(scenes.scene(storyboard_id_t{11}) == nullptr);
}

TEST_CASE("Shared storyboard scene refuses overflowed OSB values atomically before publishing UI sprites",
          "[storyboard-canvas][sys][negative]") {
    storyboard_sprite_t good{.stable_id = 1, .layer = "Background", .origin = "Centre", .path = "sprite.png",
        .nodes = {{.command = {.opcode = storyboard_opcode_t::fade, .end_time_ms = 1000,
            .from = {1}, .to = {1}, .value_count = 1}}}};
    auto overflow = good;
    overflow.stable_id = 2;
    overflow.nodes[0].command.from[0] = -1e308;
    overflow.nodes[0].command.to[0] = 1e308;
    song_document_t song{.id = song_id_t{1}, .revision = 3,
        .storyboards = {{.id = storyboard_id_t{11}, .model = storyboard_document_model_t{.sprites = {good}}},
                       {.id = storyboard_id_t{12}, .model = storyboard_document_model_t{.sprites = {overflow}}}}};
    const auto before = song.storyboards;
    sys::storyboard_scene_presenter_t scenes;
    scenes.begin(song, 9, {});
    std::vector<ui::player_preview_storyboard_sprite_view_t> sprites;
    const auto player = scenes.refill(sprites, std::nullopt, {.milliseconds = 500}, 1024);
    CHECK_FALSE(player.value);
    CHECK_FALSE(player.diagnostics.empty());
    CHECK(sprites.empty());
    ui::storyboard_canvas_view_t canvas;
    sys::populate_storyboard_canvas(canvas, scenes, storyboard_id_t{12}, storyboard_time_t::from_frame(24000, 48000));
    check_atomic_canvas_refusal(canvas);
    CHECK(song.storyboards == before);
}

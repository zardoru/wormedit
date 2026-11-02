#include "sys/storyboard/storyboard_assets_view.h"
#include "sys/storyboard/storyboard_inspector_view.h"
#include "ui/frontend/storyboard.h"
#include "../support/temporary_directory.h"
#include <catch2/catch_test_macros.hpp>
#include <fstream>

namespace {
using namespace wormedit;
auto assets_song() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 3,
        .visual_assets = {{.id = asset_id_t{1}, .kind = asset_kind_t::image, .source_path = "a.bmp"},
                         {.id = asset_id_t{2}, .kind = asset_kind_t::video, .source_path = "video.mkv"}},
        .resources = {{.id = resource_id_t{10}, .name = "Song image", .asset = asset_id_t{1}, .roles = {resource_role_t::bga}},
                      {.id = resource_id_t{11}, .name = "Song video", .asset = asset_id_t{2}, .roles = {resource_role_t::bga}}},
        .storyboards = {{.id = storyboard_id_t{11}, .osbx = osbx_document_t{
            .images = {{.id = "used", .file = "a.bmp"}, {.id = "unused-alias", .file = "./a.bmp"},
                       {.id = "missing", .file = "missing.bmp"}},
            .nodes = {{.id = "sprite", .kind = osbx_node_kind_t::sprite, .image = "used"}}}}}};
}
auto assets_root(const song_document_t& song) -> ui::storyboard_view_t {
    ui::storyboard_view_t root{.visible = true, .project = song.id, .revision = song.revision,
        .session_generation = 7, .storyboards = {{.id = storyboard_id_t{11}, .kind = ui::storyboard_kind_t::osbx}}};
    root.refill_state({.selection = {.storyboard = storyboard_id_t{11}}});
    sys::build_storyboard_inspector_view(root, song, {});
    return root;
}
void check_authored_asset_rows(const ui::storyboard_assets_view_t& view) {
    REQUIRE(view.images.size() == 3);
    CHECK(view.images[0].id == "used");
    CHECK(view.images[0].users == std::vector<std::string>{"sprite"});
    CHECK(view.images[1].is_unused());
    CHECK_FALSE(view.images[1].missing);
    CHECK(view.images[2].missing);
}
void check_importable_image_rows(const ui::storyboard_assets_view_t& view) {
    REQUIRE(view.resources.size() == 1);
    CHECK(view.resources[0].resource == resource_id_t{10});
    CHECK(view.resources[0].image == "used");
    CHECK(view.resources[0].available);
    CHECK(view.editable);
    CHECK(view.import_available);
}
}
using namespace wormedit;
TEST_CASE("Storyboard assets count stable authored and generated users without alias double counting",
          "[sys][storyboard-assets]") {
    const auto song = assets_song();
    const auto& authored = *song.storyboards[0].osbx;
    auto prepared = authored;
    prepared.nodes.push_back({.id = "generated-child", .kind = osbx_node_kind_t::sprite, .image = "used"});
    std::vector<std::string> users;
    sys::build_storyboard_image_users(users, "used", authored, &prepared);
    CHECK(users == std::vector<std::string>{"sprite", "generated-child"});
    sys::build_storyboard_image_users(users, "unused-alias", authored, &prepared);
    CHECK(users.empty());
    CHECK(song.storyboards[0].osbx->nodes.size() == 1);
}
TEST_CASE("Storyboard assets refuse used removal with every user and allow only unused authored images",
          "[sys][storyboard-assets][negative]") {
    auto song = assets_song(); auto& board = song.storyboards[0];
    const auto used = sys::validate_storyboard_image_removal(board, "used");
    REQUIRE_FALSE(used);
    CHECK(used.error().code == failure_code_t::conflict);
    CHECK(used.error().message.find("sprite") != std::string::npos);
    CHECK(sys::validate_storyboard_image_removal(board, "unused-alias").has_value());
    CHECK(sys::validate_storyboard_image_removal(board, "absent").error().code == failure_code_t::not_found);
}
TEST_CASE("Storyboard assets projection uses canonical image choices with no selected node and marks missing files",
          "[sys][storyboard-assets]") {
    const auto directory = test::temporary_directory_t::from(); REQUIRE(directory);
    // Path validation uses a real regular file; image decoding belongs to the shared presenter tests.
    std::ofstream(directory->path() / "a.bmp").put('x');
    const auto song = assets_song(); auto root = assets_root(song);
    sys::storyboard_assets_projection_t projection;
    projection.refill(root, song, directory->path());
    check_authored_asset_rows(root.assets);
    check_importable_image_rows(root.assets);
    CHECK(root.state.selection.nodes.empty());
}
TEST_CASE("Storyboard assets removal keeps generator owned and generated users readonly",
          "[sys][storyboard-assets][negative]") {
    auto song = assets_song(); auto& board = song.storyboards[0];
    auto prepared = *board.osbx;
    prepared.nodes.push_back({.id = "linked-user", .kind = osbx_node_kind_t::sprite, .image = "unused-alias"});
    const auto generated_use = sys::validate_storyboard_image_removal(board, "unused-alias", &prepared);
    REQUIRE_FALSE(generated_use);
    CHECK(generated_use.error().code == failure_code_t::conflict);
    CHECK(generated_use.error().message.find("linked-user") != std::string::npos);
    board.owning_generator_recipe = recipe_id_t{1};
    CHECK(sys::validate_storyboard_image_removal(board, "unused-alias").error().code == failure_code_t::unavailable);
}

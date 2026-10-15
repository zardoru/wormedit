#include "sys/bga_list_view.h"
#include <catch2/catch_test_macros.hpp>
#include <source_location>
#include <array>
#include <algorithm>
using namespace wormedit;
using namespace wormedit::sys;
namespace {
void check_bga(bool value, std::source_location where = std::source_location::current()) {
    INFO("BGA check at line " << where.line()); CHECK(value);
}
void require_bga(bool value, std::source_location where = std::source_location::current()) {
    INFO("BGA check at line " << where.line()); REQUIRE(value);
}

auto images() -> song_document_t {
    song_document_t document;
    for (unsigned index = 1; index <= 1200; ++index) {
        document.visual_assets.push_back({.id = asset_id_t{index}, .source_path = "image.png"});
        document.resources.push_back({.id = resource_id_t{index}, .name = "Image" + std::to_string(index),
            .asset = asset_id_t{index}, .roles = {resource_role_t::bga},
            .target_identities = {{format_family_t::bms, std::to_string(index), "BMP"}}});
    }
    document.resources[100].missing = true;
    document.visual_assets[101].kind = asset_kind_t::video;
    document.charts.push_back({.bga_events = {{.resource = resource_id_t{102}}, {.resource = resource_id_t{102}}}});
    return document;
}
}
TEST_CASE("BGA projection searches and builds only the requested window with ID use state", "[sys][bga]") {
    const auto result = bga_list_view_t::from(images(), {.search = "Image101", .row_limit = 3});
    require_bga(static_cast<bool>(result.rows.size() == 3));
    check_bga(result.total_matching == 11);
    check_bga(result.matching_ids.size() == 11);
    check_bga(result.rows.front().resource == resource_id_t{101});
    check_bga(result.rows[1].resource == resource_id_t{1010});
    check_bga(result.rows[2].resource == resource_id_t{1011});
    check_bga(result.matching_ids[0] == resource_id_t{101});
    check_bga(result.matching_ids[1] == resource_id_t{1010});
    check_bga(result.matching_ids[2] == resource_id_t{1011});
    check_bga(result.advance_ids.size() == 10);
    check_bga(std::ranges::find(result.advance_ids, resource_id_t{101}) == result.advance_ids.end());
    check_bga(result.rows.front().missing);
    check_bga(result.rows.front().slot == "101");
    const auto used = bga_list_view_t::from(images(), {.search = "Image102"});
    check_bga(used.rows.front().video);
    check_bga(used.rows.front().use_count == 2);
    check_bga(used.advance_ids.size() == 11);
    check_bga(used.advance_ids == used.matching_ids);
}

TEST_CASE("BGA dock sorts matching rows naturally before the requested window", "[sys][bga][dock]") {
    auto document = images();
    document.resources.resize(3);
    document.visual_assets.resize(3);
    document.charts.clear();
    const auto names = std::array{"snd10.bmp", "snd2.bmp", "snd1.bmp"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        auto& resource = document.resources[index];
        resource.name = names[index];
        resource.target_identities = {{format_family_t::bms,
            index == 0 ? "0Z" : index == 1 ? "0a" : "10", "BMP"}};
    }
    const auto result = bga_list_view_t::from(document, {.search = "snd", .row_limit = 2});
    require_bga(result.rows.size() == 2);
    check_bga(result.rows[0].name == "snd1.bmp");
    check_bga(result.rows[1].name == "snd2.bmp");
    check_bga(result.matching_ids[0] == resource_id_t{3});
    check_bga(result.advance_ids == result.matching_ids);
    const auto by_id = bga_list_view_t::from(document, {.search = "snd", .row_limit = 2,
                                                   .sort = dock_sort_t::bms_id});
    require_bga(by_id.rows.size() == 2);
    check_bga(by_id.rows[0].name == "snd2.bmp");
    check_bga(by_id.rows[1].name == "snd10.bmp");
    check_bga(result.bms_capacity.used == 3);
    check_bga(result.bms_capacity.total == 1295);
    const auto without_ids = bga_list_view_t::from(document, {.search = "snd", .row_limit = 2,
        .sort = dock_sort_t::bms_id, .ids_available = false});
    require_bga(without_ids.rows.size() == 2);
    check_bga(without_ids.rows[0].name == "snd1.bmp");
    check_bga(without_ids.rows[1].name == "snd2.bmp");
    check_bga(without_ids.advance_ids == without_ids.matching_ids);
}

TEST_CASE("BGA advance order spans off-screen matches and skips unavailable or nonvisual assets",
          "[sys][bga][advance]") {
    auto document = images();
    document.resources[3].missing = true;
    document.visual_assets[4].kind = asset_kind_t::storyboard;
    document.resources[5].asset = asset_id_t{9999};
    const auto result = bga_list_view_t::from(document, {.search = "Image", .row_limit = 2});

    require_bga(result.rows.size() == 2);
    check_bga(result.matching_ids.size() == 1200);
    check_bga(result.total_matching == 1200);
    check_bga(result.advance_ids.size() == 1196);
    check_bga(result.advance_ids.front() == resource_id_t{1});
    check_bga(result.advance_ids[1] == resource_id_t{2});
    check_bga(std::ranges::find(result.advance_ids, resource_id_t{4}) == result.advance_ids.end());
    check_bga(std::ranges::find(result.advance_ids, resource_id_t{5}) == result.advance_ids.end());
    check_bga(std::ranges::find(result.advance_ids, resource_id_t{6}) == result.advance_ids.end());
}
TEST_CASE("BGA search matches names and BMP IDs and clamps its window", "[sys][bga]") {
    const auto document = images();
    const auto result = bga_list_view_t::from(document, {.search = "iMaGe1200"});
    require_bga(static_cast<bool>(result.rows.size() == 1));
    check_bga(result.rows.front().resource == resource_id_t{1200});
    check_bga(bga_list_view_t::from(document, {.search = "1200"}).total_matching == 1);
    const auto outside = bga_list_view_t::from(document, {.first_row = 2000, .row_limit = 3});
    check_bga(outside.first_row == 1200);
    check_bga(outside.rows.empty());
}

TEST_CASE("Images use and full-path filters precede windows while membership remains complete",
          "[sys][bga][sound-workflow]") {
    auto document = images();
    document.visual_assets[101].source_path = "nested/Visual/scene.png";
    document.charts.push_back({.bga_events = {{.resource = resource_id_t{102}}}});
    const auto used = bga_list_view_t::from(document, {.search = "NESTED/VISUAL", .row_limit = 1,
        .use_filter = resource_use_filter_t::used});
    require_bga(used.rows.size() == 1);
    check_bga(used.rows[0].resource == resource_id_t{102});
    check_bga(used.rows[0].use_count == 3);
    check_bga(used.rows[0].source_path == "nested/Visual/scene.png");
    check_bga(used.all_resource_ids.size() == document.resources.size());
    check_bga(used.matching_ids.size() == 1);
    const auto missing = bga_list_view_t::from(document, {.row_limit = 1,
        .use_filter = resource_use_filter_t::missing});
    require_bga(missing.rows.size() == 1);
    check_bga(missing.rows[0].resource == resource_id_t{101});
    check_bga(missing.all_resource_ids.size() == document.resources.size());
    const auto unused = bga_list_view_t::from(document, {.first_row = 100, .row_limit = 2,
        .use_filter = resource_use_filter_t::unused});
    check_bga(unused.total_matching == document.resources.size() - 1);
    check_bga(unused.rows.size() == 2);
    check_bga(unused.bms_capacity.used == bms_identifier_capacity(document, "BMP").used);
}

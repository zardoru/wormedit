#include "sys/bga_list_projection.h"
#include <catch2/catch_test_macros.hpp>
#include <source_location>
#include <array>
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
    const auto result = project_bga_list(images(), {.search = "Image101", .row_limit = 3});
    require_bga(static_cast<bool>(result.rows.size() == 3));
    check_bga(result.total_matching == 11);
    check_bga(result.matching_ids.size() == 11);
    check_bga(result.rows.front().resource == resource_id_t{101});
    check_bga(result.rows[1].resource == resource_id_t{1010});
    check_bga(result.rows[2].resource == resource_id_t{1011});
    check_bga(result.matching_ids[0] == resource_id_t{101});
    check_bga(result.matching_ids[1] == resource_id_t{1010});
    check_bga(result.matching_ids[2] == resource_id_t{1011});
    check_bga(result.rows.front().missing);
    check_bga(result.rows.front().slot == "101");
    const auto used = project_bga_list(images(), {.search = "Image102"});
    check_bga(used.rows.front().video);
    check_bga(used.rows.front().use_count == 2);
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
    const auto result = project_bga_list(document, {.search = "snd", .row_limit = 2});
    require_bga(result.rows.size() == 2);
    check_bga(result.rows[0].name == "snd1.bmp");
    check_bga(result.rows[1].name == "snd2.bmp");
    check_bga(result.matching_ids[0] == resource_id_t{3});
    const auto by_id = project_bga_list(document, {.search = "snd", .row_limit = 2,
                                                   .sort = dock_sort_t::bms_id});
    require_bga(by_id.rows.size() == 2);
    check_bga(by_id.rows[0].name == "snd2.bmp");
    check_bga(by_id.rows[1].name == "snd10.bmp");
    check_bga(result.bms_capacity.used == 3);
    check_bga(result.bms_capacity.total == 1295);
    const auto without_ids = project_bga_list(document, {.search = "snd", .row_limit = 2,
        .sort = dock_sort_t::bms_id, .ids_available = false});
    require_bga(without_ids.rows.size() == 2);
    check_bga(without_ids.rows[0].name == "snd1.bmp");
    check_bga(without_ids.rows[1].name == "snd2.bmp");
}
TEST_CASE("BGA search matches names and BMP IDs and clamps its window", "[sys][bga]") {
    const auto document = images();
    const auto result = project_bga_list(document, {.search = "iMaGe1200"});
    require_bga(static_cast<bool>(result.rows.size() == 1));
    check_bga(result.rows.front().resource == resource_id_t{1200});
    check_bga(project_bga_list(document, {.search = "1200"}).total_matching == 1);
    const auto outside = project_bga_list(document, {.first_row = 2000, .row_limit = 3});
    check_bga(outside.first_row == 1200);
    check_bga(outside.rows.empty());
}

#include "sys/bga_list_projection.h"
#include <catch2/catch_test_macros.hpp>
#include <source_location>
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
TEST_CASE("BGA projection builds only the requested window with IDs uses and missing state", "[sys][bga]") {
    const auto result = project_bga_list(images(), {.first_row = 100, .row_limit = 3});
    require_bga(static_cast<bool>(result.rows.size() == 3));
    check_bga(result.total_matching == 1200);
    check_bga(result.matching_ids.size() == 1200);
    check_bga(result.rows.front().resource == resource_id_t{101});
    check_bga(result.rows.front().missing);
    check_bga(result.rows[1].video);
    check_bga(result.rows[1].use_count == 2);
    check_bga(result.rows[1].slot == "102");
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

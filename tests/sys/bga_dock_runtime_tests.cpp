#include "sys/resources/bga_dock_runtime.h"
#include "sys/frame_timing.h"
#include "../support/temporary_directory.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <source_location>
#include <thread>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
void check_bga_dock(bool condition, std::source_location source = std::source_location::current()) {
    INFO("Assertion at line " << source.line());
    CHECK(condition);
}

auto wait_for_thumbnail(bga_dock_runtime_t& runtime, const song_document_t& document,
                         const std::filesystem::path& base, const ui::bga_dock_request_t& request,
                         wormedit::ui::compose_view_t& view) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (std::chrono::steady_clock::now() < deadline) {
        runtime.project(document, base, request, view);
        if (!view.bga_rows.empty() && !view.bga_rows.front().rgba_pixels.empty()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
}
}

TEST_CASE("Decoded BGA thumbnails retain warmed source validation and refresh changed paths",
          "[sys][compose-bga][allocation][image]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto first = std::string(80, 'a') + ".ppm";
    const auto second = std::string(80, 'b') + ".ppm";
    // Two real PPM pixels exercise the ordinary decoder without another copied BMP fixture.
    std::ofstream(directory->path() / first, std::ios::binary).write("P6\n2 1\n255\n\xff\0\0\0\xff\0", 17);
    std::ofstream(directory->path() / second, std::ios::binary).write("P6\n2 1\n255\n\0\xff\0\0\0\xff", 17);
    song_document_t document{.id = song_id_t{7}, .revision = 1,
        .visual_assets = {{.id = asset_id_t{1}, .kind = asset_kind_t::image,
                          .source_path = first, .width = 2, .height = 1}},
        .resources = {{.id = resource_id_t{2}, .name = std::string(80, 'i'),
                       .asset = asset_id_t{1}, .roles = {resource_role_t::bga}}}};
    bga_dock_runtime_t runtime;
    wormedit::ui::compose_view_t view{};
    const ui::bga_dock_request_t request{.visible = true, .row_limit = 64};
    REQUIRE(wait_for_thumbnail(runtime, document, directory->path(), request, view));
    runtime.project(document, directory->path(), request, view);
    const auto before = frame_heap_allocation_snapshot();
    runtime.project(document, directory->path(), request, view);
    const auto after = frame_heap_allocation_snapshot();
    check_bga_dock(after.count == before.count);
    check_bga_dock(after.bytes == before.bytes);
    check_bga_dock(view.bga_rows.front().rgba_pixels.size() == 32 * 16 * 4);
    check_bga_dock(view.bga_rows.front().rgba_pixels[0] == 255);
    document.visual_assets.front().source_path = second;
    ++document.revision;
    REQUIRE(wait_for_thumbnail(runtime, document, directory->path(), request, view));
    check_bga_dock(view.bga_rows.front().rgba_pixels[0] == 0);
    check_bga_dock(view.bga_rows.front().rgba_pixels[1] == 255);
}

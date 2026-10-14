#include <wormedit/visual/image_decoder.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>

namespace {

using namespace wormedit;
using namespace wormedit::visual;

constexpr std::array<std::uint8_t, 62> two_pixel_bmp{
    0x42, 0x4d, 0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00,
};

auto encoded_bmp() -> std::span<const std::byte> {
    return std::as_bytes(std::span{two_pixel_bmp});
}

auto image_source(asset_kind_t kind = asset_kind_t::image) -> resolved_visual_asset_t {
    return {.resource = resource_id_t{10},
            .asset = asset_id_t{20},
            .kind = kind,
            .path = "visual.bmp",
            .declared_width = 2,
            .declared_height = 1};
}

struct temporary_file_t {
    std::filesystem::path path;

    ~temporary_file_t() {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

} // namespace

TEST_CASE("visual resources resolve through explicit project-relative asset paths",
          "[unit][visual][resolver]") {
    song_document_t document{
        .id = song_id_t{1},
        .visual_assets = {{.id = asset_id_t{20},
                           .kind = asset_kind_t::image,
                           .source_path = "images/background.png",
                           .width = 640,
                           .height = 480}},
        .resources = {{.id = resource_id_t{10},
                       .name = "background",
                       .asset = asset_id_t{20},
                       .roles = {resource_role_t::bga}}},
    };
    const auto resolved = resolved_visual_asset_t::from(document, resource_id_t{10}, "/project/song");
    REQUIRE(resolved);
    CHECK(resolved->resource == resource_id_t{10});
    CHECK(resolved->asset == asset_id_t{20});
    CHECK(resolved->path == std::filesystem::path{"/project/song/images/background.png"});
    CHECK(resolved->declared_width == 640);
    CHECK(resolved->declared_height == 480);

    CHECK(resolve_visual_asset(document, resource_id_t{99}, "/project/song").status ==
          visual_asset_status_t::unknown_resource);
    CHECK(resolve_visual_asset(document, resource_id_t{10}, {}).status ==
          visual_asset_status_t::relative_path_without_base);
    document.resources.front().missing = true;
    CHECK(resolve_visual_asset(document, resource_id_t{10}, "/project/song").status ==
          visual_asset_status_t::missing_resource);
}

TEST_CASE("bounded static image decoding publishes exact RGBA8 pixels", "[unit][visual][decode]") {
    const auto decoded = rgba_image_t::from(image_source(), encoded_bmp());
    REQUIRE(decoded);
    CHECK(decoded->width == 2);
    CHECK(decoded->height == 1);
    REQUIRE(decoded->pixels.size() == 8);
    CHECK(decoded->pixels[0] == 255);
    CHECK(decoded->pixels[1] == 0);
    CHECK(decoded->pixels[2] == 0);
    CHECK(decoded->pixels[3] == 255);
    CHECK(decoded->pixels[4] == 0);
    CHECK(decoded->pixels[5] == 255);
    CHECK(decoded->pixels[6] == 0);
    CHECK(decoded->pixels[7] == 255);
}

TEST_CASE("image decoding rejects unsupported kinds malformed data and every configured cap",
          "[unit][visual][decode][negative]") {
    CHECK(decode_image_rgba(image_source(asset_kind_t::video), encoded_bmp()).status ==
          image_status_t::unsupported_asset_kind);

    const std::array<std::byte, 8> malformed{};
    CHECK(decode_image_rgba(image_source(), malformed).status == image_status_t::decode_failed);

    auto input_limited = image_decode_limits_t{};
    input_limited.maximum_input_bytes = 10;
    CHECK(decode_image_rgba(image_source(), encoded_bmp(), input_limited).status ==
          image_status_t::input_too_large);

    auto dimension_limited = image_decode_limits_t{};
    dimension_limited.maximum_width = 1;
    CHECK(decode_image_rgba(image_source(), encoded_bmp(), dimension_limited).status ==
          image_status_t::dimension_limit_exceeded);

    auto pixel_limited = image_decode_limits_t{};
    pixel_limited.maximum_pixels = 1;
    CHECK(decode_image_rgba(image_source(), encoded_bmp(), pixel_limited).status ==
          image_status_t::pixel_limit_exceeded);

    auto byte_limited = image_decode_limits_t{};
    byte_limited.maximum_rgba_bytes = 7;
    CHECK(decode_image_rgba(image_source(), encoded_bmp(), byte_limited).status ==
          image_status_t::byte_limit_exceeded);

    auto mismatched = image_source();
    mismatched.declared_width = 3;
    CHECK(decode_image_rgba(mismatched, encoded_bmp()).status ==
          image_status_t::declared_geometry_mismatch);
}

TEST_CASE("visual image file loading is bounded before decode", "[unit][visual][file]") {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    temporary_file_t temporary{std::filesystem::temp_directory_path() /
                               ("wormedit-visual-image-" + std::to_string(nonce) + ".bmp")};
    {
        std::ofstream output(temporary.path, std::ios::binary | std::ios::trunc);
        REQUIRE(output);
        output.write(reinterpret_cast<const char*>(two_pixel_bmp.data()),
                     static_cast<std::streamsize>(two_pixel_bmp.size()));
        REQUIRE(output);
    }
    auto source = image_source();
    source.path = temporary.path;
    REQUIRE(open_image_rgba(source));

    auto bounded = image_decode_limits_t{};
    bounded.maximum_input_bytes = two_pixel_bmp.size() - 1;
    CHECK(load_image_rgba(source, bounded).status == image_status_t::input_too_large);

    source.path += ".missing";
    CHECK(load_image_rgba(source).status == image_status_t::read_failed);
}

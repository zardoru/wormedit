#include <wormedit/visual/image_decoder.h>

#define STBI_FAILURE_USERMSG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace wormedit::visual {
namespace {

auto checked_geometry(int width, int height, image_decode_limits_t limits, std::size_t& rgba_bytes)
    -> image_status_t {
    if (width <= 0 || height <= 0 || static_cast<std::uint64_t>(width) > limits.maximum_width ||
        static_cast<std::uint64_t>(height) > limits.maximum_height) {
        return image_status_t::dimension_limit_exceeded;
    }
    const auto pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixels > limits.maximum_pixels) {
        return image_status_t::pixel_limit_exceeded;
    }
    if (pixels > std::numeric_limits<std::size_t>::max() / 4U) {
        return image_status_t::byte_limit_exceeded;
    }
    rgba_bytes = static_cast<std::size_t>(pixels) * 4U;
    return rgba_bytes <= limits.maximum_rgba_bytes ? image_status_t::ready
                                                   : image_status_t::byte_limit_exceeded;
}

auto geometry_message(image_status_t status) -> std::string {
    switch (status) {
    case image_status_t::dimension_limit_exceeded:
        return "decoded image dimensions exceed the configured limit";
    case image_status_t::pixel_limit_exceeded:
        return "decoded image pixel count exceeds the configured limit";
    case image_status_t::byte_limit_exceeded:
        return "decoded RGBA storage exceeds the configured byte limit";
    default:
        return "decoded image geometry is invalid";
    }
}

} // namespace

auto rgba_image_t::from(const resolved_visual_asset_t& source, std::span<const std::byte> encoded,
                        image_decode_limits_t limits) -> image_decode_outcome_t {
    if (source.kind != asset_kind_t::image) {
        return std::unexpected(
            error_t<image_status_t>{image_status_t::unsupported_asset_kind,
                                    "only static image assets can be decoded as RGBA frames"});
    }
    if (limits.maximum_input_bytes == 0 || limits.maximum_width == 0 ||
        limits.maximum_height == 0 || limits.maximum_pixels == 0 ||
        limits.maximum_rgba_bytes == 0 || encoded.empty() ||
        encoded.size() > limits.maximum_input_bytes ||
        encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return std::unexpected(error_t<image_status_t>{
            image_status_t::input_too_large,
            "encoded image is empty or exceeds the configured input limit"});
    }
    const auto* bytes = reinterpret_cast<const stbi_uc*>(encoded.data());
    const auto byte_count = static_cast<int>(encoded.size());
    int width{};
    int height{};
    int source_channels{};
    if (stbi_info_from_memory(bytes, byte_count, &width, &height, &source_channels) == 0) {
        const auto* reason = stbi_failure_reason();
        return std::unexpected(
            error_t<image_status_t>{image_status_t::decode_failed,
                                    reason ? std::string{"image header decode failed: "} + reason
                                           : "image header decode failed"});
    }
    std::size_t rgba_bytes{};
    const auto geometry = checked_geometry(width, height, limits, rgba_bytes);
    if (geometry != image_status_t::ready) {
        return std::unexpected(error_t<image_status_t>{geometry, geometry_message(geometry)});
    }
    if ((source.declared_width && *source.declared_width != static_cast<std::uint32_t>(width)) ||
        (source.declared_height && *source.declared_height != static_cast<std::uint32_t>(height))) {
        return std::unexpected(error_t<image_status_t>{
            image_status_t::declared_geometry_mismatch,
            "decoded image geometry differs from the document declaration"});
    }
    int decoded_width{};
    int decoded_height{};
    int decoded_channels{};
    auto* decoded = stbi_load_from_memory(bytes, byte_count, &decoded_width, &decoded_height,
                                          &decoded_channels, STBI_rgb_alpha);
    if (decoded == nullptr || decoded_width != width || decoded_height != height) {
        const auto* reason = stbi_failure_reason();
        if (decoded != nullptr) {
            stbi_image_free(decoded);
        }
        return std::unexpected(
            error_t<image_status_t>{image_status_t::decode_failed,
                                    reason ? std::string{"image pixel decode failed: "} + reason
                                           : "image pixel decode failed"});
    }
    rgba_image_t image{.resource = source.resource,
                       .asset = source.asset,
                       .path = source.path,
                       .width = static_cast<std::uint32_t>(width),
                       .height = static_cast<std::uint32_t>(height),
                       .pixels = {}};
    image.pixels.assign(decoded, decoded + rgba_bytes);
    stbi_image_free(decoded);
    return image;
}

auto open_image_rgba(const resolved_visual_asset_t& source, image_decode_limits_t limits)
    -> image_decode_outcome_t {
    if (source.kind != asset_kind_t::image) {
        return std::unexpected(
            error_t<image_status_t>{image_status_t::unsupported_asset_kind,
                                    "only static image assets can be loaded as RGBA frames"});
    }
    std::error_code error;
    const auto file_size = std::filesystem::file_size(source.path, error);
    if (error) {
        return std::unexpected(
            error_t<image_status_t>{image_status_t::read_failed,
                                    "visual asset size could not be read: " + error.message()});
    }
    if (file_size == 0 || file_size > limits.maximum_input_bytes ||
        file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        return std::unexpected(error_t<image_status_t>{
            image_status_t::input_too_large,
            "visual asset file is empty or exceeds the configured input limit"});
    }
    std::ifstream input(source.path, std::ios::binary);
    if (!input) {
        return std::unexpected(error_t<image_status_t>{image_status_t::read_failed,
                                                       "visual asset file could not be opened"});
    }
    std::vector<std::byte> encoded(static_cast<std::size_t>(file_size));
    input.read(reinterpret_cast<char*>(encoded.data()),
               static_cast<std::streamsize>(encoded.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(encoded.size())) {
        return std::unexpected(error_t<image_status_t>{
            image_status_t::read_failed, "visual asset file could not be read fully"});
    }
    return rgba_image_t::from(source, encoded, limits);
}

auto image_decode_result_t::from(image_decode_outcome_t&& outcome) -> image_decode_result_t {
    if (!outcome)
        return {.status = outcome.error().code,
                .image = std::nullopt,
                .message = std::move(outcome.error().message)};
    return {.status = image_status_t::ready, .image = std::move(*outcome), .message = {}};
}


auto load_image_rgba(const resolved_visual_asset_t& source, image_decode_limits_t limits)
    -> image_decode_result_t {
    return image_decode_result_t::from(open_image_rgba(source, limits));
}

} // namespace wormedit::visual

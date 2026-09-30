#include "ui/field/field_glyphs.h"
#include <algorithm>

namespace wormedit::ui::field {
namespace {
auto continuation(unsigned char byte) -> bool {
    return (byte & 0xc0U) == 0x80U;
}
auto sequence_length(unsigned char byte) -> unsigned {
    if (byte < 0x80U)
        return 1;
    if (byte >= 0xc2U && byte <= 0xdfU)
        return 2;
    if (byte >= 0xe0U && byte <= 0xefU)
        return 3;
    if (byte >= 0xf0U && byte <= 0xf4U)
        return 4;
    return 0;
}
auto valid_scalar(std::uint32_t value, unsigned bytes) -> bool {
    if (value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU))
        return false;
    if (bytes == 2)
        return value >= 0x80U;
    if (bytes == 3)
        return value >= 0x800U;
    if (bytes == 4)
        return value >= 0x10000U;
    return true;
}
} // namespace

auto decode_utf8(std::string_view text) noexcept -> decoded_codepoint_t {
    if (text.empty())
        return {};
    const auto first = static_cast<unsigned char>(text.front());
    const auto length = sequence_length(first);
    if (length == 0 || text.size() < length)
        return {0xfffdU, 1};
    if (length == 1)
        return {first, 1};
    auto value = static_cast<std::uint32_t>(first & (0x7fU >> length));
    for (unsigned index = 1; index < length; ++index) {
        const auto byte = static_cast<unsigned char>(text[index]);
        if (!continuation(byte))
            return {0xfffdU, 1};
        value = (value << 6U) | (byte & 0x3fU);
    }
    return valid_scalar(value, length) ? decoded_codepoint_t{value, length}
                                       : decoded_codepoint_t{0xfffdU, 1};
}
auto rounded_rectangle_points(SDL_FRect bounds, float radius) -> std::array<SDL_FPoint, 16> {
    radius = std::clamp(radius, 0.0f, std::max(0.0f, std::min(bounds.w, bounds.h) * .5f));
    const std::array<SDL_FPoint, 4> centers{{{bounds.x + radius, bounds.y + radius},
                                              {bounds.x + bounds.w - radius, bounds.y + radius},
                                              {bounds.x + bounds.w - radius, bounds.y + bounds.h - radius},
                                              {bounds.x + radius, bounds.y + bounds.h - radius}}};
    constexpr std::array<SDL_FPoint, 4> arc{{{-1, 0}, {-.8660254f, -.5f},
                                              {-.5f, -.8660254f}, {0, -1}}};
    std::array<SDL_FPoint, 16> points{};
    for (std::size_t corner = 0; corner < centers.size(); ++corner) {
        for (std::size_t step = 0; step < arc.size(); ++step) {
            auto direction = arc[step];
            for (std::size_t rotation = 0; rotation < corner; ++rotation)
                direction = {-direction.y, direction.x};
            points[corner * arc.size() + step] =
                {centers[corner].x + radius * direction.x, centers[corner].y + radius * direction.y};
        }
    }
    return points;
}
auto glyph_bounds(glyph_t glyph, SDL_FPoint pen) noexcept -> SDL_FRect {
    return {pen.x + glyph.offset.x, pen.y + glyph.offset.y, glyph.size.x, glyph.size.y};
}
} // namespace wormedit::ui::field

#include "ui/nuklear/image_quad.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

std::array<WormUiNuklearImageQuad, wormedit::ui::maximum_preview_storyboard_instances> image_quads{};
std::size_t image_quad_count{};

} // namespace

void worm_ui_nuklear_begin_image_quad_frame(void) {
    image_quad_count = 0;
}

bool worm_ui_nuklear_push_image_quad(struct nk_command_buffer* canvas, const struct nk_image* image,
                                     const struct nk_vec2* points, struct nk_color color) {
    if (!canvas || !image || !points || image_quad_count == image_quads.size()) {
        return false;
    }
    auto& quad = image_quads[image_quad_count++];
    quad.image = *image;
    std::copy_n(points, 4, quad.points);
    if (!std::all_of(std::begin(quad.points), std::end(quad.points), [](const auto& point) {
            return std::isfinite(point.x) && std::isfinite(point.y);
        })) {
        --image_quad_count;
        return false;
    }
    if (nk_image_is_subimage(image)) {
        const auto left = static_cast<float>(image->region[0]) / image->w;
        const auto top = static_cast<float>(image->region[1]) / image->h;
        const auto right = static_cast<float>(image->region[0] + image->region[2]) / image->w;
        const auto bottom = static_cast<float>(image->region[1] + image->region[3]) / image->h;
        const std::array uvs{nk_vec2(left, top), nk_vec2(right, top), nk_vec2(right, bottom),
                             nk_vec2(left, bottom)};
        std::copy(uvs.begin(), uvs.end(), quad.uvs);
    } else {
        const std::array uvs{nk_vec2(0.0F, 0.0F), nk_vec2(1.0F, 0.0F), nk_vec2(1.0F, 1.0F),
                             nk_vec2(0.0F, 1.0F)};
        std::copy(uvs.begin(), uvs.end(), quad.uvs);
    }
    quad.color = color;
    const auto [minimum_x, maximum_x] =
        std::minmax_element(std::begin(quad.points), std::end(quad.points),
                            [](const auto& left, const auto& right) { return left.x < right.x; });
    const auto [minimum_y, maximum_y] =
        std::minmax_element(std::begin(quad.points), std::end(quad.points),
                            [](const auto& left, const auto& right) { return left.y < right.y; });
    nk_push_custom(canvas,
                   nk_rect(minimum_x->x, minimum_y->y, maximum_x->x - minimum_x->x,
                           maximum_y->y - minimum_y->y),
                   worm_ui_nuklear_append_image_quad, nk_handle_ptr(&quad));
    return true;
}

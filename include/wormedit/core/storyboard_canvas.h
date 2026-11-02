#pragma once

#include <wormedit/core/geometry.h>
#include <wormedit/core/visual_layout.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit {

struct storyboard_canvas_geometry_t {
    static constexpr double minimum_zoom = 0.05;
    static constexpr double maximum_zoom = 32;
    rect_t viewport;
    visual_destination_rect_t canvas;
    double scale{1};
    [[nodiscard]] static auto from(rect_t viewport, double logical_width, double logical_height,
        double zoom, visual_point_t pan) -> std::optional<storyboard_canvas_geometry_t>;
    [[nodiscard]] auto to_canvas(visual_point_t screen) const -> visual_point_t;
    [[nodiscard]] auto to_screen(visual_point_t logical) const -> visual_point_t;
    [[nodiscard]] auto contains(visual_point_t screen) const -> bool;
    auto operator==(const storyboard_canvas_geometry_t&) const -> bool = default;
};

// Resolved image dimensions are supplied by the existing image metadata/cache
// owner. No decoder, filesystem access or pixel-alpha test belongs here.
struct storyboard_canvas_target_t {
    std::string_view node;
    visual_affine_t world;
    std::uint32_t width{}, height{};
    double origin_x{0.5}, origin_y{0.5};
    [[nodiscard]] static auto legacy_node_id(std::uint64_t stable_id) -> std::string;
    [[nodiscard]] auto contains(visual_point_t logical) const -> bool;
    [[nodiscard]] auto quad(const storyboard_canvas_geometry_t&, double logical_width,
        double logical_height) const -> std::optional<visual_image_quad_t>;
    auto operator==(const storyboard_canvas_target_t&) const -> bool = default;
};

// Input order is render order; output is top-to-bottom. Refill retained storage.
void build_storyboard_canvas_hits(std::span<const storyboard_canvas_target_t>, visual_point_t logical,
    std::vector<std::string_view>& hits);

} // namespace wormedit

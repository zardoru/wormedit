#include <wormedit/core/storyboard_canvas.h>

#include <algorithm>
#include <cmath>

namespace wormedit {
auto storyboard_canvas_geometry_t::from(rect_t viewport, double width, double height,
    double zoom, visual_point_t pan) -> std::optional<storyboard_canvas_geometry_t> {
    const auto viewport_width = viewport.right - viewport.left;
    const auto viewport_height = viewport.bottom - viewport.top;
    if (!std::isfinite(viewport.left) || !std::isfinite(viewport.top) ||
        !std::isfinite(viewport_width) || !std::isfinite(viewport_height) ||
        viewport_width <= 0 || viewport_height <= 0 || !std::isfinite(width) ||
        !std::isfinite(height) || width <= 0 || height <= 0 || !std::isfinite(zoom) ||
        zoom < minimum_zoom || zoom > maximum_zoom || !std::isfinite(pan.x) || !std::isfinite(pan.y)) return {};
    const auto scale = std::min(viewport_width / width, viewport_height / height) * zoom;
    const auto left = viewport.left + (viewport_width - width * scale) / 2 + pan.x * scale;
    const auto top = viewport.top + (viewport_height - height * scale) / 2 + pan.y * scale;
    if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(left) || !std::isfinite(top) ||
        !std::isfinite(width * scale) || !std::isfinite(height * scale) ||
        !std::isfinite(left + width * scale) || !std::isfinite(top + height * scale)) return {};
    return storyboard_canvas_geometry_t{.viewport = viewport,
        .canvas = {.left = left, .top = top, .width = width * scale, .height = height * scale}, .scale = scale};
}
auto storyboard_canvas_geometry_t::to_canvas(visual_point_t screen) const -> visual_point_t {
    return {.x = (screen.x - canvas.left) / scale, .y = (screen.y - canvas.top) / scale};
}
auto storyboard_canvas_geometry_t::to_screen(visual_point_t logical) const -> visual_point_t {
    return {.x = canvas.left + logical.x * scale, .y = canvas.top + logical.y * scale};
}
auto storyboard_canvas_geometry_t::contains(visual_point_t screen) const -> bool {
    return screen.x >= viewport.left && screen.x <= viewport.right &&
        screen.y >= viewport.top && screen.y <= viewport.bottom;
}
auto storyboard_canvas_target_t::legacy_node_id(std::uint64_t stable_id) -> std::string {
    return "osb/sprite/" + std::to_string(stable_id);
}
auto storyboard_canvas_target_t::contains(visual_point_t logical) const -> bool {
    if (!width || !height || !std::isfinite(origin_x) || !std::isfinite(origin_y) ||
        origin_x < 0 || origin_x > 1 || origin_y < 0 || origin_y > 1) return false;
    const auto inverse = world.inverse();
    if (!inverse) return false;
    const auto local = inverse->apply(logical);
    return std::isfinite(local.x) && std::isfinite(local.y) && local.x >= -origin_x * width &&
        local.x <= (1 - origin_x) * width && local.y >= -origin_y * height && local.y <= (1 - origin_y) * height;
}
auto storyboard_canvas_target_t::quad(const storyboard_canvas_geometry_t& geometry,
    double logical_width, double logical_height) const -> std::optional<visual_image_quad_t> {
    return visual_image_quad_t::from({.source_width = width, .source_height = height,
        .destination = geometry.canvas, .logical_width = logical_width, .logical_height = logical_height,
        .origin_x = origin_x, .origin_y = origin_y, .transform = world});
}
void build_storyboard_canvas_hits(std::span<const storyboard_canvas_target_t> targets,
    visual_point_t logical, std::vector<std::string_view>& hits) {
    hits.clear();
    for (auto target = targets.rbegin(); target != targets.rend(); ++target)
        if (!target->node.empty() && target->contains(logical)) hits.push_back(target->node);
}
} // namespace wormedit

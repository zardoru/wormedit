#include "sys/storyboard/storyboard_canvas_view.h"
#include "sys/storyboard/storyboard_scene_presenter.h"
#include <wormedit/core/storyboard_canvas.h>
#include <wormedit/core/osbx_document.h>
#include <wormedit/core/osbx_lua_link.h>
#include <algorithm>
#include <limits>

namespace wormedit::sys {
namespace {
auto target(const ui::player_preview_storyboard_sprite_view_t& sprite) -> storyboard_canvas_target_t {
    return {.node = sprite.node_id, .world = {.values = sprite.transform},
        .width = sprite.image_width, .height = sprite.image_height,
        .origin_x = sprite.origin_x, .origin_y = sprite.origin_y};
}
auto is_descendant(const osbx_document_t& scene, std::string_view child, std::string_view group) -> bool {
    // A validated graph is acyclic, but keep this query bounded for stale inputs.
    for (std::size_t i = 0; i < scene.nodes.size(); ++i) {
        const auto node = std::ranges::find(scene.nodes, child, &osbx_node_t::id);
        if (node == scene.nodes.end() || !node->parent) return false;
        if (*node->parent == group) return true;
        child = *node->parent;
    }
    return false;
}
auto sprite_outline(const ui::player_preview_storyboard_sprite_view_t& sprite,
    const storyboard_canvas_geometry_t& geometry, double width, double height)
    -> std::optional<ui::storyboard_canvas_outline_t> {
    if (!sprite.image_ready || sprite.opacity <= 0) return std::nullopt;
    const auto hit = target(sprite);
    const auto quad = hit.quad(geometry, width, height);
    if (!quad) return std::nullopt;
    return ui::storyboard_canvas_outline_t{.node = std::string{sprite.node_id},
        .points = quad->points, .origin = hit.world.apply({})};
}
auto group_outline(const ui::storyboard_canvas_view_t& view, const osbx_document_t& scene,
    std::string_view group, const storyboard_canvas_geometry_t& geometry)
    -> std::optional<ui::storyboard_canvas_outline_t> {
    double left = std::numeric_limits<double>::infinity(), top = left;
    double right = -left, bottom = -left;
    for (const auto& sprite : view.sprites) {
        if (!is_descendant(scene, sprite.node_id, group)) continue;
        const auto outline = sprite_outline(sprite, geometry, view.width, view.height);
        if (!outline) continue;
        for (const auto point : outline->points) {
            left = std::min(left, point.x); top = std::min(top, point.y);
            right = std::max(right, point.x); bottom = std::max(bottom, point.y);
        }
    }
    if (left > right) return std::nullopt;
    // Group bounds have no invented authored origin; the frontend omits its marker.
    return ui::storyboard_canvas_outline_t{.node = std::string{group},
        .points = {{{left, top}, {right, top}, {right, bottom}, {left, bottom}}}, .group = true};
}
} // namespace

void populate_storyboard_canvas(ui::storyboard_canvas_view_t& view, storyboard_scene_presenter_t& scenes,
    storyboard_id_t selected, const result_t<storyboard_time_t, failure_code_t>& time,
    storyboard_conditional_layer_t conditional_layer) {
    view.prepared_generation = osbx_lua_prepared_scope_t::generation();
    if (!time) { view.status = "The storyboard clock is invalid or exceeds its time range."; return; }
    const auto frame = scenes.refill(view.sprites, selected, *time, ui::maximum_preview_storyboard_instances,
        conditional_layer);
    view.available = frame.value;
    view.time_ms = static_cast<double>(time->milliseconds) + time->fraction();
    if (const auto* scene = scenes.scene(selected)) {
        view.width = scene->canvas.width;
        view.height = scene->canvas.height;
    }
    if (!frame.diagnostics.empty()) view.status = frame.diagnostics.front().message;
    else view.status = osbx_lua_prepared_scope_t::diagnostic();
}
void filter_storyboard_canvas_hidden(ui::storyboard_view_t& view, const osbx_document_t* scene) {
    std::erase_if(view.canvas.sprites, [&](const auto& sprite) {
        return std::ranges::any_of(view.state.hidden_nodes, [&](const auto& hidden) {
            return sprite.node_id == hidden || (scene && is_descendant(*scene, sprite.node_id, hidden));
        });
    });
}
void refill_storyboard_canvas_outlines(ui::storyboard_view_t& view, const osbx_document_t* scene) {
    auto& canvas = view.canvas;
    canvas.outlines.clear();
    const auto geometry = storyboard_canvas_geometry_t::from({.right = canvas.width, .bottom = canvas.height},
        canvas.width, canvas.height, 1, {});
    if (!geometry) return;
    for (const auto& id : view.state.selection.nodes) {
        const auto sprite = std::ranges::find(canvas.sprites, id, &ui::player_preview_storyboard_sprite_view_t::node_id);
        if (sprite != canvas.sprites.end()) {
            if (auto outline = sprite_outline(*sprite, *geometry, canvas.width, canvas.height)) canvas.outlines.push_back(std::move(*outline));
        } else if (scene) {
            if (auto outline = group_outline(canvas, *scene, id, *geometry)) canvas.outlines.push_back(std::move(*outline));
        }
    }
}
} // namespace wormedit::sys

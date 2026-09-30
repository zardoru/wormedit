#include "ui/field/field_renderer.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace wormedit::ui::field {
namespace {
auto rgba(color_t color) -> SDL_FColor {
    constexpr float unit = 1.0f / 255.0f;
    return {color.r * unit, color.g * unit, color.b * unit, color.a * unit};
}
auto intersection(SDL_FRect a, SDL_FRect b) -> SDL_FRect {
    const auto left = std::max(a.x, b.x), top = std::max(a.y, b.y);
    const auto right = std::min(a.x + a.w, b.x + b.w);
    const auto bottom = std::min(a.y + a.h, b.y + b.h);
    return {left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
}
auto bounds_of(std::span<const SDL_FPoint> points) -> SDL_FRect {
    if (points.empty())
        return {};
    float left = points.front().x, right = left, top = points.front().y, bottom = top;
    for (const auto point : points) {
        left = std::min(left, point.x);
        right = std::max(right, point.x);
        top = std::min(top, point.y);
        bottom = std::max(bottom, point.y);
    }
    return {left, top, right - left, bottom - top};
}
auto normal(SDL_FPoint a, SDL_FPoint b) -> SDL_FPoint {
    const auto x = b.x - a.x, y = b.y - a.y;
    const auto length = std::hypot(x, y);
    return length == 0 ? SDL_FPoint{} : SDL_FPoint{y / length, -x / length};
}
auto outward_corner(std::span<const SDL_FPoint> points, std::size_t index, float width)
    -> SDL_FPoint {
    const auto point = points[index];
    const auto before = normal(points[(index + points.size() - 1) % points.size()], point);
    const auto after = normal(point, points[(index + 1) % points.size()]);
    const auto x = (before.x + after.x) * .5f, y = (before.y + after.y) * .5f;
    const auto squared = x * x + y * y;
    const auto factor = squared > 0 ? std::min(100.0f, 1.0f / squared) * width : 0;
    return {point.x + x * factor, point.y + y * factor};
}
auto clipped_uv(float start, float end, float offset, float size) -> float {
    return start + (end - start) * offset / size;
}
} // namespace

void renderer_t::begin(SDL_FRect viewport, SDL_FPoint scale, solid_texture_t solid) {
    solid_ = solid;
    viewport_ = viewport;
    scale_ = {std::max(.001f, scale.x), std::max(.001f, scale.y)};
    vertices_.clear();
    indices_.clear();
    batches_.clear();
}
void renderer_t::append_batch(layer_t layer, SDL_Texture* texture, std::size_t count) {
    const auto offset = indices_.size() - count;
    const auto maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (!batches_.empty() && batches_.back().layer == layer && batches_.back().texture == texture &&
        batches_.back().index_count <= maximum - count) {
        batches_.back().index_count += count;
    } else {
        batches_.push_back({layer, texture, offset, count});
    }
}
void renderer_t::quad(layer_t layer, std::span<const SDL_FPoint, 4> points, color_t color,
                      SDL_Texture* texture, SDL_FPoint uv_min, SDL_FPoint uv_max) {
    if (!texture) {
        texture = solid_.texture;
        uv_min = solid_.white_uv;
        uv_max = solid_.white_uv;
    }
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    const std::array<SDL_FPoint, 4> uv{
        {uv_min, {uv_max.x, uv_min.y}, uv_max, {uv_min.x, uv_max.y}}};
    for (std::size_t index = 0; index < 4; ++index)
        vertices_.push_back({points[index], rgba(color), uv[index]});
    for (const auto index : {0U, 1U, 2U, 0U, 2U, 3U})
        indices_.push_back(base + index);
    append_batch(layer, texture, 6);
}
void renderer_t::rect(layer_t layer, SDL_FRect bounds, color_t color) {
    if (color.a == 0 || bounds.w <= 0 || bounds.h <= 0)
        return;
    const auto left = std::round(bounds.x * scale_.x) / scale_.x;
    const auto top = std::round(bounds.y * scale_.y) / scale_.y;
    const auto right =
        std::max(left + 1.0f / scale_.x, std::round((bounds.x + bounds.w) * scale_.x) / scale_.x);
    const auto bottom =
        std::max(top + 1.0f / scale_.y, std::round((bounds.y + bounds.h) * scale_.y) / scale_.y);
    bounds = intersection({left, top, right - left, bottom - top}, viewport_);
    if (bounds.w <= 0 || bounds.h <= 0)
        return;
    const std::array<SDL_FPoint, 4> points{{{bounds.x, bounds.y},
                                            {bounds.x + bounds.w, bounds.y},
                                            {bounds.x + bounds.w, bounds.y + bounds.h},
                                            {bounds.x, bounds.y + bounds.h}}};
    quad(layer, points, color);
}
void renderer_t::hline(layer_t layer, float x0, float x1, float y, float width, color_t color) {
    rect(layer, {std::min(x0, x1), y - width * .5f, std::abs(x1 - x0), width}, color);
}
void renderer_t::vline(layer_t layer, float x, float y0, float y1, float width, color_t color) {
    rect(layer, {x - width * .5f, std::min(y0, y1), width, std::abs(y1 - y0)}, color);
}
void renderer_t::line(layer_t layer, SDL_FPoint a, SDL_FPoint b, float width, color_t color) {
    if (width <= 0 || color.a == 0)
        return;
    if (a.y == b.y) {
        hline(layer, a.x, b.x, a.y, width, color);
        return;
    }
    if (a.x == b.x) {
        vline(layer, a.x, a.y, b.y, width, color);
        return;
    }
    const auto offset = normal(a, b);
    const auto half = width * .5f;
    const std::array<SDL_FPoint, 4> points{{{a.x + offset.x * half, a.y + offset.y * half},
                                            {b.x + offset.x * half, b.y + offset.y * half},
                                            {b.x - offset.x * half, b.y - offset.y * half},
                                            {a.x - offset.x * half, a.y - offset.y * half}}};
    const auto clipped = intersection(bounds_of(points), viewport_);
    if (clipped.w > 0 && clipped.h > 0)
        quad(layer, points, color);
}
void renderer_t::polygon_fringe(layer_t layer, std::span<const SDL_FPoint> points, color_t color,
                                float width, std::uint32_t base) {
    color.a = 0;
    for (std::size_t index = 0; index < points.size(); ++index)
        vertices_.push_back({outward_corner(points, index, width * .5f), rgba(color), solid_.white_uv});
    const auto outer = base + static_cast<std::uint32_t>(points.size());
    for (std::uint32_t index = 0; index < points.size(); ++index) {
        const auto next = (index + 1U) % static_cast<std::uint32_t>(points.size());
        for (const auto vertex :
             {base + index, base + next, outer + next, base + index, outer + next, outer + index})
            indices_.push_back(vertex);
    }
    append_batch(layer, solid_.texture, points.size() * 6);
}
void renderer_t::polygon(layer_t layer, std::span<const SDL_FPoint> points, color_t color,
                         float aa_fringe) {
    if (points.size() < 3 || color.a == 0)
        return;
    const auto clipped = intersection(bounds_of(points), viewport_);
    if (clipped.w <= 0 || clipped.h <= 0)
        return;
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto point = aa_fringe > 0 ? outward_corner(points, index, -aa_fringe * .5f)
                                         : points[index];
        vertices_.push_back({point, rgba(color), solid_.white_uv});
    }
    // Compose arrows are star-shaped about their first (tip) vertex, matching
    // the pinned adapter's fan; callers retain their original point order.
    for (std::uint32_t index = 1; index + 1 < points.size(); ++index)
        for (const auto vertex : {base, base + index, base + index + 1})
            indices_.push_back(vertex);
    append_batch(layer, solid_.texture, (points.size() - 2) * 3);
    if (aa_fringe > 0)
        polygon_fringe(layer, points, color, aa_fringe, base);
}
void renderer_t::stroke_joined(layer_t layer, std::span<const SDL_FPoint> points, float width,
                               color_t color) {
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    for (std::size_t index = 0; index < points.size(); ++index) {
        vertices_.push_back({outward_corner(points, index, width * .5f), rgba(color), solid_.white_uv});
        vertices_.push_back({outward_corner(points, index, -width * .5f), rgba(color), solid_.white_uv});
    }
    for (std::uint32_t index = 0; index < points.size(); ++index) {
        const auto current = base + index * 2;
        const auto next = base + ((index + 1) % static_cast<std::uint32_t>(points.size())) * 2;
        for (const auto vertex : {current, next, next + 1, current, next + 1, current + 1})
            indices_.push_back(vertex);
    }
    append_batch(layer, solid_.texture, points.size() * 6);
}
void renderer_t::stroke_fringe(layer_t layer, std::span<const SDL_FPoint> points, float width,
                                color_t color, float aa_fringe) {
    const auto base = static_cast<std::uint32_t>(vertices_.size());
    const bool thin = width <= aa_fringe;
    const std::uint32_t rings = thin ? 3 : 4;
    const auto inner = std::max(0.0f, (width - aa_fringe) * .5f);
    const std::array<float, 4> offsets{inner + aa_fringe, inner, -inner, -inner - aa_fringe};
    auto transparent = color;
    transparent.a = 0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        for (std::uint32_t ring = 0; ring < rings; ++ring) {
            const auto offset = thin ? aa_fringe * (1.0f - static_cast<float>(ring)) : offsets[ring];
            const auto shade = ring == 0 || ring + 1 == rings ? transparent : color;
            vertices_.push_back({outward_corner(points, index, offset), rgba(shade), solid_.white_uv});
        }
    }
    for (std::uint32_t index = 0; index < points.size(); ++index) {
        const auto current = base + index * rings;
        const auto next = base + ((index + 1) % static_cast<std::uint32_t>(points.size())) * rings;
        for (std::uint32_t ring = 0; ring + 1 < rings; ++ring)
            for (const auto vertex : {current + ring, next + ring, next + ring + 1,
                                      current + ring, next + ring + 1, current + ring + 1})
                indices_.push_back(vertex);
    }
    append_batch(layer, solid_.texture, points.size() * (rings - 1) * 6);
}
void renderer_t::stroke_polygon(layer_t layer, std::span<const SDL_FPoint> points, float width,
                                color_t color, float aa_fringe) {
    if (points.size() < 2 || width <= 0 || color.a == 0)
        return;
    const auto bounds = bounds_of(points);
    const auto margin = width * .5f + aa_fringe;
    const auto visible = intersection({bounds.x - margin, bounds.y - margin,
                                       bounds.w + margin * 2, bounds.h + margin * 2}, viewport_);
    if (visible.w <= 0 || visible.h <= 0)
        return;
    if (points.size() == 2) {
        line(layer, points.front(), points.back(), width, color);
        return;
    }
    if (aa_fringe > 0) {
        stroke_fringe(layer, points, width, color, aa_fringe);
        return;
    }
    stroke_joined(layer, points, width, color);
}
void renderer_t::glyph(layer_t layer, SDL_FRect bounds, color_t color, SDL_Texture* texture,
                       SDL_FPoint uv_min, SDL_FPoint uv_max) {
    if (bounds.w <= 0 || bounds.h <= 0)
        return;
    const auto clipped = intersection(bounds, viewport_);
    if (clipped.w <= 0 || clipped.h <= 0)
        return;
    const SDL_FPoint first{clipped_uv(uv_min.x, uv_max.x, clipped.x - bounds.x, bounds.w),
                           clipped_uv(uv_min.y, uv_max.y, clipped.y - bounds.y, bounds.h)};
    const SDL_FPoint last{
        clipped_uv(uv_min.x, uv_max.x, clipped.x + clipped.w - bounds.x, bounds.w),
        clipped_uv(uv_min.y, uv_max.y, clipped.y + clipped.h - bounds.y, bounds.h)};
    const std::array<SDL_FPoint, 4> points{{{clipped.x, clipped.y},
                                            {clipped.x + clipped.w, clipped.y},
                                            {clipped.x + clipped.w, clipped.y + clipped.h},
                                            {clipped.x, clipped.y + clipped.h}}};
    quad(layer, points, color, texture, first, last);
}
void renderer_t::text(layer_t layer, SDL_FPoint pen, std::string_view utf8, color_t color,
                      const font_t& font) {
    if (!font.query || !font.texture || color.a == 0)
        return;
    while (!utf8.empty()) {
        const auto current = decode_utf8(utf8);
        utf8.remove_prefix(current.bytes);
        const auto next = decode_utf8(utf8);
        const auto shape = font.query(font.userdata, font.height, current.value, next.value);
        glyph(layer, glyph_bounds(shape, pen), color, font.texture, shape.uv_min, shape.uv_max);
        pen.x += shape.advance;
    }
}
auto renderer_t::workload() const noexcept -> workload_t {
    return {vertices_.size(), indices_.size(), batches_.size()};
}
auto renderer_t::submit(SDL_Renderer* renderer, submission_t hooks) const -> bool {
    return submit_range(renderer, 0, indices_.size(), hooks);
}
auto renderer_t::submit_range(SDL_Renderer* renderer, std::size_t first_index,
                              std::size_t index_count, submission_t hooks) const -> bool {
    if (!renderer || vertices_.empty() || index_count == 0)
        return renderer != nullptr;
    const auto draw = hooks.geometry ? hooks.geometry : SDL_RenderGeometryRaw;
    const auto clip = hooks.clip ? hooks.clip : SDL_SetRenderClipRect;
    const bool was_clipped = SDL_RenderClipEnabled(renderer);
    SDL_BlendMode previous_blend{};
    if (!SDL_GetRenderDrawBlendMode(renderer, &previous_blend))
        return false;
    SDL_Rect before{};
    if (!SDL_GetRenderClipRect(renderer, &before))
        return false;
    const SDL_Rect viewport{
        static_cast<int>(std::floor(viewport_.x)), static_cast<int>(std::floor(viewport_.y)),
        static_cast<int>(std::ceil(viewport_.x + viewport_.w) - std::floor(viewport_.x)),
        static_cast<int>(std::ceil(viewport_.y + viewport_.h) - std::floor(viewport_.y))};
    bool success = SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    success = clip(renderer, &viewport) && success;
    const auto& first = vertices_.front();
    const auto end = std::min(indices_.size(), first_index + index_count);
    for (const auto& batch : batches_) {
        const auto begin = std::max(first_index, batch.index_offset);
        const auto batch_end = std::min(end, batch.index_offset + batch.index_count);
        if (begin >= batch_end)
            continue;
        success = draw(renderer, batch.texture, &first.position.x, sizeof(SDL_Vertex), &first.color,
                       sizeof(SDL_Vertex), &first.tex_coord.x, sizeof(SDL_Vertex),
                       static_cast<int>(vertices_.size()), indices_.data() + begin,
                       static_cast<int>(batch_end - begin), sizeof(std::uint32_t)) &&
                  success;
    }
    const bool restored_clip = clip(renderer, was_clipped ? &before : nullptr);
    const bool restored_blend = SDL_SetRenderDrawBlendMode(renderer, previous_blend);
    return restored_clip && restored_blend && success;
}
} // namespace wormedit::ui::field

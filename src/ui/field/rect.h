#pragma once
#include <SDL3/SDL_rect.h>
#include <algorithm>

namespace wormedit::ui {
enum class rect_edges_t { inclusive, half_open };
inline auto is_point_in_rect(SDL_FRect rect, float x, float y,
                          rect_edges_t edges = rect_edges_t::half_open) -> bool {
    const SDL_FPoint point{x, y};
    return SDL_PointInRectFloat(&point, &rect) &&
        (edges == rect_edges_t::inclusive || (x < rect.x + rect.w && y < rect.y + rect.h));
}

inline auto rect_intersection(SDL_FRect a, SDL_FRect b) -> SDL_FRect {
    SDL_FRect result{};
    if (SDL_GetRectIntersectionFloat(&a, &b, &result)) return result;
    // Preserve the old clipped origin and each independent empty dimension.
    // SDL can leave a negative extent or clear the result for an invalid input.
    result.x = std::max(a.x, b.x);
    result.y = std::max(a.y, b.y);
    result.w = std::max(0.0f, std::min(a.x + a.w, b.x + b.w) - result.x);
    result.h = std::max(0.0f, std::min(a.y + a.h, b.y + b.h) - result.y);
    return result;
}
inline auto has_rect_overlap(SDL_FRect a, SDL_FRect b) -> bool {
    const auto overlap = rect_intersection(a, b);
    return overlap.w > 0 && overlap.h > 0;
}
} // namespace wormedit::ui

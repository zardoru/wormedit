#pragma once

#include "wormedit/nuklear_config.h"

#include <algorithm>

// Lucide shapes drawn with Nuklear primitives, for icons that are not in the
// rasterized pack in lucide_icon_pixels.h. Coordinates follow Lucide's 24x24 grid.
namespace wormedit::ui::vector_icons {

struct Frame {
    float x;
    float y;
    float scale;

    static auto fit(struct nk_rect bounds) -> Frame {
        const auto side = std::min(bounds.w, bounds.h) * 0.62f;
        return {bounds.x + (bounds.w - side) * 0.5f, bounds.y + (bounds.h - side) * 0.5f,
                side / 24.0f};
    }
    [[nodiscard]] auto px(float grid_x) const -> float { return x + grid_x * scale; }
    [[nodiscard]] auto py(float grid_y) const -> float { return y + grid_y * scale; }
    [[nodiscard]] auto stroke() const -> float { return std::max(1.0f, 2.0f * scale); }
};

// Lucide "mouse-pointer-2": the select tool.
inline void select_cursor(struct nk_command_buffer* canvas, struct nk_rect bounds,
                          struct nk_color tint) {
    const auto frame = Frame::fit(bounds);
    const float points[] = {
        frame.px(4.7f),  frame.py(4.7f),  frame.px(20.4f), frame.py(11.0f),
        frame.px(13.9f), frame.py(12.8f), frame.px(11.9f), frame.py(19.4f),
    };
    nk_stroke_polygon(canvas, const_cast<float*>(points), 4, frame.stroke(), tint);
}

// Lucide "timer": the Timing focus mode.
inline void timer(struct nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
    const auto frame = Frame::fit(bounds);
    const auto thickness = frame.stroke();
    nk_stroke_circle(canvas, nk_rect(frame.px(4.0f), frame.py(6.0f), 16.0f * frame.scale,
                                     16.0f * frame.scale), thickness, tint);
    nk_stroke_line(canvas, frame.px(10.0f), frame.py(2.0f), frame.px(14.0f), frame.py(2.0f),
                   thickness, tint);
    nk_stroke_line(canvas, frame.px(12.0f), frame.py(14.0f), frame.px(15.0f), frame.py(11.0f),
                   thickness, tint);
}

// Lucide "grid-3x3": snapping to the edit grid.
inline void grid(struct nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
    const auto frame = Frame::fit(bounds);
    const auto thickness = frame.stroke();
    nk_stroke_rect(canvas, nk_rect(frame.px(3.0f), frame.py(3.0f), 18.0f * frame.scale,
                                   18.0f * frame.scale), 2.0f * frame.scale, thickness, tint);
    nk_stroke_line(canvas, frame.px(3.0f), frame.py(9.0f), frame.px(21.0f), frame.py(9.0f),
                   thickness, tint);
    nk_stroke_line(canvas, frame.px(3.0f), frame.py(15.0f), frame.px(21.0f), frame.py(15.0f),
                   thickness, tint);
    nk_stroke_line(canvas, frame.px(9.0f), frame.py(3.0f), frame.px(9.0f), frame.py(21.0f),
                   thickness, tint);
    nk_stroke_line(canvas, frame.px(15.0f), frame.py(3.0f), frame.px(15.0f), frame.py(21.0f),
                   thickness, tint);
}

// Lucide "plus": add sounds.
inline void plus(struct nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
    const auto frame = Frame::fit(bounds);
    nk_stroke_line(canvas, frame.px(5.0f), frame.py(12.0f), frame.px(19.0f), frame.py(12.0f),
                   frame.stroke(), tint);
    nk_stroke_line(canvas, frame.px(12.0f), frame.py(5.0f), frame.px(12.0f), frame.py(19.0f),
                   frame.stroke(), tint);
}

// Lucide "trash-2": remove sounds.
inline void trash(struct nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
    const auto frame = Frame::fit(bounds);
    const auto thickness = frame.stroke();
    nk_stroke_line(canvas, frame.px(3.0f), frame.py(6.0f), frame.px(21.0f), frame.py(6.0f),
                   thickness, tint);
    const float can[] = {
        frame.px(19.0f), frame.py(6.0f),  frame.px(19.0f), frame.py(20.0f),
        frame.px(17.0f), frame.py(22.0f), frame.px(7.0f),  frame.py(22.0f),
        frame.px(5.0f),  frame.py(20.0f), frame.px(5.0f),  frame.py(6.0f),
    };
    nk_stroke_polyline(canvas, const_cast<float*>(can), 6, thickness, tint);
    const float lid[] = {
        frame.px(8.0f),  frame.py(6.0f), frame.px(8.0f),  frame.py(4.0f),
        frame.px(10.0f), frame.py(2.0f), frame.px(14.0f), frame.py(2.0f),
        frame.px(16.0f), frame.py(4.0f), frame.px(16.0f), frame.py(6.0f),
    };
    nk_stroke_polyline(canvas, const_cast<float*>(lid), 6, thickness, tint);
    nk_stroke_line(canvas, frame.px(10.0f), frame.py(11.0f), frame.px(10.0f), frame.py(17.0f),
                   thickness, tint);
    nk_stroke_line(canvas, frame.px(14.0f), frame.py(11.0f), frame.px(14.0f), frame.py(17.0f),
                   thickness, tint);
}

// Lucide "square-check": select every listed sound.
inline void select_all(struct nk_command_buffer* canvas, struct nk_rect bounds,
                       struct nk_color tint) {
    const auto frame = Frame::fit(bounds);
    const auto thickness = frame.stroke();
    nk_stroke_rect(canvas, nk_rect(frame.px(3.0f), frame.py(3.0f), 18.0f * frame.scale,
                                   18.0f * frame.scale), 2.0f * frame.scale, thickness, tint);
    const float tick[] = {
        frame.px(8.0f), frame.py(12.0f), frame.px(11.0f), frame.py(15.0f),
        frame.px(16.0f), frame.py(9.0f),
    };
    nk_stroke_polyline(canvas, const_cast<float*>(tick), 3, thickness, tint);
}

// Lucide "list-music": the Compose sounds list.
inline void sound_list(struct nk_command_buffer* canvas, struct nk_rect bounds,
                       struct nk_color tint) {
    const auto frame = Frame::fit(bounds);
    const auto thickness = frame.stroke();
    nk_stroke_line(canvas, frame.px(3.0f), frame.py(6.0f), frame.px(14.0f), frame.py(6.0f),
                   thickness, tint);
    nk_stroke_line(canvas, frame.px(3.0f), frame.py(12.0f), frame.px(14.0f), frame.py(12.0f),
                   thickness, tint);
    nk_stroke_line(canvas, frame.px(3.0f), frame.py(18.0f), frame.px(9.0f), frame.py(18.0f),
                   thickness, tint);
    nk_stroke_circle(canvas, nk_rect(frame.px(14.0f), frame.py(15.0f), 6.0f * frame.scale,
                                     6.0f * frame.scale), thickness, tint);
    nk_stroke_line(canvas, frame.px(20.0f), frame.py(18.0f), frame.px(20.0f), frame.py(7.0f),
                   thickness, tint);
    nk_stroke_line(canvas, frame.px(20.0f), frame.py(7.0f), frame.px(22.0f), frame.py(9.0f),
                   thickness, tint);
}

} // namespace wormedit::ui::vector_icons

#pragma once

#include "wormedit/nuklear_config.h"
#include "ui/hitsound_icons.h"

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

inline void image(struct nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
    const auto f = Frame::fit(bounds);
    nk_stroke_rect(canvas, nk_rect(f.px(3), f.py(3), 18 * f.scale, 18 * f.scale),
                   2 * f.scale, f.stroke(), tint);
    nk_stroke_circle(canvas, nk_rect(f.px(7), f.py(7), 3 * f.scale, 3 * f.scale), f.stroke(), tint);
    const float mountain[]{f.px(4), f.py(18), f.px(10), f.py(12), f.px(14), f.py(16),
                           f.px(17), f.py(13), f.px(21), f.py(17)};
    nk_stroke_polyline(canvas, const_cast<float*>(mountain), 5, f.stroke(), tint);
}

// The view segment uses keys, a fraction and a layer stack as distinct marks.
inline void view_keys(struct nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
    const auto f = Frame::fit(bounds);
    nk_stroke_rect(canvas, nk_rect(f.px(2), f.py(4), 20*f.scale, 16*f.scale), 0, f.stroke(), tint);
    nk_stroke_line(canvas, f.px(9), f.py(4), f.px(9), f.py(20), f.stroke(), tint);
    nk_stroke_line(canvas, f.px(15), f.py(4), f.px(15), f.py(20), f.stroke(), tint);
    nk_fill_rect(canvas, nk_rect(f.px(7), f.py(4), 4*f.scale, 9*f.scale), 0, tint);
    nk_fill_rect(canvas, nk_rect(f.px(13), f.py(4), 4*f.scale, 9*f.scale), 0, tint);
}

inline void view_snap(struct nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
    const auto f = Frame::fit(bounds);
    nk_stroke_line(canvas, f.px(8), f.py(5), f.px(10), f.py(3), f.stroke(), tint);
    nk_stroke_line(canvas, f.px(10), f.py(3), f.px(10), f.py(10), f.stroke(), tint);
    nk_stroke_line(canvas, f.px(4), f.py(14), f.px(20), f.py(10), f.stroke(), tint);
    nk_stroke_line(canvas, f.px(12), f.py(15), f.px(10), f.py(20), f.stroke(), tint);
    nk_stroke_line(canvas, f.px(10), f.py(20), f.px(17), f.py(20), f.stroke(), tint);
    nk_stroke_line(canvas, f.px(15), f.py(16), f.px(15), f.py(23), f.stroke(), tint);
}

inline void view_layers(struct nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
    const auto f = Frame::fit(bounds);
    const float top[]{f.px(12),f.py(3),f.px(22),f.py(8),f.px(12),f.py(13),f.px(2),f.py(8)};
    nk_stroke_polygon(canvas, const_cast<float*>(top), 4, f.stroke(), tint);
    const float middle[]{f.px(2),f.py(13),f.px(12),f.py(18),f.px(22),f.py(13)};
    nk_stroke_polyline(canvas, const_cast<float*>(middle), 3, f.stroke(), tint);
    const float bottom[]{f.px(2),f.py(18),f.px(12),f.py(23),f.px(22),f.py(18)};
    nk_stroke_polyline(canvas, const_cast<float*>(bottom), 3, f.stroke(), tint);
}

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

// Y-lock: a vertical double arrow crossed by a bar, rows stay put while notes slide sideways.
inline void y_lock(struct nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
    const auto frame = Frame::fit(bounds);
    const auto thickness = frame.stroke();
    nk_stroke_line(canvas, frame.px(12.0f), frame.py(3.0f), frame.px(12.0f), frame.py(21.0f), thickness, tint);
    const float top[] = {frame.px(8.0f), frame.py(7.0f), frame.px(12.0f), frame.py(3.0f), frame.px(16.0f), frame.py(7.0f)};
    const float bottom[] = {frame.px(8.0f), frame.py(17.0f), frame.px(12.0f), frame.py(21.0f), frame.px(16.0f), frame.py(17.0f)};
    nk_stroke_polyline(canvas, const_cast<float*>(top), 3, thickness, tint);
    nk_stroke_polyline(canvas, const_cast<float*>(bottom), 3, thickness, tint);
    nk_stroke_line(canvas, frame.px(4.0f), frame.py(12.0f), frame.px(20.0f), frame.py(12.0f), thickness, tint);
}

// Measure lane: a rail with bar-line ticks, like a ruler.
inline void measure_lane(struct nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
    const auto frame = Frame::fit(bounds);
    const auto thickness = frame.stroke();
    nk_stroke_line(canvas, frame.px(6.0f), frame.py(3.0f), frame.px(6.0f), frame.py(21.0f), thickness, tint);
    for (const auto y : {5.0f, 12.0f, 19.0f})
        nk_stroke_line(canvas, frame.px(6.0f), frame.py(y), frame.px(20.0f), frame.py(y), thickness, tint);
}

// Bar-line time propagation: a bar line pushing the content below it along.
inline void bar_line_time(struct nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
    const auto frame = Frame::fit(bounds);
    const auto thickness = frame.stroke();
    nk_stroke_line(canvas, frame.px(3.0f), frame.py(5.0f), frame.px(21.0f), frame.py(5.0f), thickness, tint);
    nk_stroke_line(canvas, frame.px(12.0f), frame.py(9.0f), frame.px(12.0f), frame.py(21.0f), thickness, tint);
    const float head[] = {frame.px(7.0f), frame.py(16.0f), frame.px(12.0f), frame.py(21.0f), frame.px(17.0f), frame.py(16.0f)};
    nk_stroke_polyline(canvas, const_cast<float*>(head), 3, thickness, tint);
}

// Lucide "sliders-vertical": three faders, the Compose mixer.
inline void sliders_vertical(struct nk_command_buffer* canvas, struct nk_rect bounds,
                             struct nk_color tint) {
    const auto frame = Frame::fit(bounds);
    const auto thickness = frame.stroke();
    struct fader_t { float x, gap_top, gap_bottom, knob; };
    for (const auto fader : {fader_t{4.0f, 10.0f, 14.0f, 14.0f}, fader_t{12.0f, 8.0f, 12.0f, 8.0f},
                             fader_t{20.0f, 12.0f, 16.0f, 16.0f}}) {
        nk_stroke_line(canvas, frame.px(fader.x), frame.py(3.0f), frame.px(fader.x),
                       frame.py(fader.gap_top), thickness, tint);
        nk_stroke_line(canvas, frame.px(fader.x), frame.py(fader.gap_bottom), frame.px(fader.x),
                       frame.py(21.0f), thickness, tint);
        nk_stroke_line(canvas, frame.px(fader.x - 2.0f), frame.py(fader.knob), frame.px(fader.x + 2.0f),
                       frame.py(fader.knob), thickness, tint);
    }
}

// Lucide "list-ordered" in outline: the Compose measures list.
inline void measure_list(struct nk_command_buffer* canvas, struct nk_rect bounds,
                         struct nk_color tint) {
    const auto frame = Frame::fit(bounds);
    const auto thickness = frame.stroke();
    for (const auto y : {6.0f, 12.0f, 18.0f}) {
        nk_stroke_line(canvas, frame.px(10.0f), frame.py(y), frame.px(21.0f), frame.py(y),
                       thickness, tint);
        nk_stroke_line(canvas, frame.px(3.0f), frame.py(y), frame.px(6.0f), frame.py(y),
                       thickness, tint);
    }
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

struct HitsoundStroke {
    nk_command_buffer* canvas;
    Frame frame;
    nk_color tint;
    void line(float x0, float y0, float x1, float y1) {
        nk_stroke_line(canvas, frame.px(x0), frame.py(y0), frame.px(x1), frame.py(y1), frame.stroke(), tint);
    }
    void path(std::span<const hitsound_icons::point_t> points) {
        for (std::size_t index = 1; index < points.size(); ++index)
            line(points[index-1][0], points[index-1][1], points[index][0], points[index][1]);
    }
    void circle(float x, float y, float radius) {
        nk_stroke_circle(canvas, nk_rect(frame.px(x-radius), frame.py(y-radius),
            2*radius*frame.scale, 2*radius*frame.scale), frame.stroke(), tint);
    }
};
inline void whistle(struct nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
    HitsoundStroke stroke{canvas, Frame::fit(bounds), tint};
    hitsound_icons::draw(stroke, hitsound_icons::shape_t::whistle);
}
inline void cymbal(struct nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
    HitsoundStroke stroke{canvas, Frame::fit(bounds), tint};
    hitsound_icons::draw(stroke, hitsound_icons::shape_t::cymbal);
}
inline void clap(struct nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
    HitsoundStroke stroke{canvas, Frame::fit(bounds), tint};
    hitsound_icons::draw(stroke, hitsound_icons::shape_t::clap);
}

} // namespace wormedit::ui::vector_icons

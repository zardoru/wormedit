#pragma once

#include <array>
#include <span>

namespace wormedit::ui::hitsound_icons {
using point_t = std::array<float, 2>;
enum class shape_t { normal, soft, drum, whistle, cymbal, clap };

// Shared 24-unit geometry. Backends supply line/path/circle strokes; neither
// canvas rendering nor toolbar controls own a second definition of a shape.
template <class Stroke>
void draw(Stroke& stroke, shape_t shape) {
    switch (shape) {
    case shape_t::normal:
        stroke.circle(7, 18, 4);
        stroke.line(11, 18, 11, 3);
        stroke.line(11, 3, 21, 6);
        stroke.line(21, 6, 21, 11);
        break;
    case shape_t::soft:
        stroke.path(std::array{point_t{2, 12}, point_t{5, 7}, point_t{8, 7}, point_t{11, 12},
            point_t{14, 17}, point_t{17, 17}, point_t{20, 12}, point_t{23, 7}});
        stroke.line(4, 21, 20, 21);
        break;
    case shape_t::drum:
        stroke.path(std::array{point_t{3, 8}, point_t{12, 5}, point_t{21, 8}, point_t{12, 11}, point_t{3, 8},
            point_t{3, 18}, point_t{12, 21}, point_t{21, 18}, point_t{21, 8}});
        stroke.line(8, 11, 8, 19);
        stroke.line(16, 11, 16, 19);
        break;
    case shape_t::whistle:
        stroke.circle(9, 16, 6);
        stroke.path(std::array{point_t{13, 11}, point_t{21, 7}, point_t{23, 11}, point_t{15, 17}});
        stroke.circle(9, 16, 2);
        stroke.line(15, 2, 16, 5);
        stroke.line(20, 1, 19, 4);
        break;
    case shape_t::cymbal:
        stroke.path(std::array{point_t{2, 13}, point_t{8, 11}, point_t{10, 8}, point_t{14, 8},
            point_t{16, 11}, point_t{22, 13}});
        stroke.line(2, 13, 22, 13);
        stroke.line(12, 14, 12, 22);
        stroke.line(8, 22, 16, 22);
        stroke.line(17, 3, 14, 6);
        stroke.line(21, 6, 18, 8);
        break;
    case shape_t::clap:
        stroke.path(std::array{point_t{7, 23}, point_t{2, 17}, point_t{2, 11}, point_t{5, 11},
            point_t{5, 15}, point_t{8, 17}, point_t{5, 7}, point_t{8, 6}, point_t{13, 18}, point_t{12, 22}});
        stroke.path(std::array{point_t{15, 22}, point_t{20, 17}, point_t{22, 11}, point_t{19, 10},
            point_t{17, 15}, point_t{14, 17}, point_t{17, 7}, point_t{14, 6}, point_t{12, 11}});
        stroke.line(11, 1, 11, 4);
        stroke.line(4, 2, 6, 4);
        stroke.line(18, 2, 16, 4);
        break;
    }
}
} // namespace wormedit::ui::hitsound_icons

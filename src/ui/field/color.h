#pragma once
#include <cstdint>

namespace wormedit::ui {
// Byte RGBA shared by renderer and glyph palettes. Palette structs contain
// several colors; Nuklear colors are adapted at the library API boundary.
struct color_t {
    std::uint8_t r{}, g{}, b{}, a{255};
};
} // namespace wormedit::ui

#pragma once

#include "wormedit/nuklear_config.h"

#include <cstdint>
#include <filesystem>

namespace wormedit::ui::theme {

enum class UiColor {
    background, surface, surface_hover, surface_active, border, border_hover,
    text, text_bright, text_muted, selected, selected_text, accent, status,
    conflict, count
};

enum class ComposeColor { viewport, rail, grid, label, overview, semantic_layer_none, count };

// Call once before creating the first frame. WORMEDIT_THEME_DIR overrides the bundled files.
void load(const std::filesystem::path& bundled_directory);
auto ui(UiColor color) -> nk_color;
auto compose(ComposeColor color) -> nk_color;
auto compose_lane(size_t lane, bool alternate) -> nk_color;
auto compose_note_accent(size_t index) -> nk_color;
// Instrument colors for wormedit::lane_key_color_t values (0 = generic: has_color is false).
struct key_color_t {
    bool has_color{};
    nk_color lane;
    nk_color note;
};
auto compose_key_color(std::uint8_t key_color, bool alternate) -> key_color_t;

} // namespace wormedit::ui::theme

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

enum class ComposeColor {
    viewport, rail, grid, label, overview, semantic_layer_none, selection_outline, count
};

// Call once before creating the first frame. WORMEDIT_THEME_DIR overrides the bundled files.
void load(const std::filesystem::path& bundled_directory);
auto ui(UiColor color) -> nk_color;
auto compose(ComposeColor color) -> nk_color;
auto compose_note_accent(size_t index) -> nk_color;
// Colors for one wormedit::lane_key_color_t value. Values outside the table use generic.
struct key_color_t {
    bool has_color{};
    nk_color lane;
    nk_color lane_alt;
    nk_color note;
    nk_color note_border;
    nk_color hold_body;
};
auto compose_key_color(std::uint8_t key_color) -> key_color_t;

} // namespace wormedit::ui::theme

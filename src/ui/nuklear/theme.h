#pragma once

#include "wormedit/nuklear_config.h"

#include <filesystem>

namespace wormedit::ui::theme {

enum class UiColor {
    background, surface, surface_hover, surface_active, border, border_hover,
    text, text_bright, text_muted, selected, selected_text, accent, status,
    count
};

enum class ComposeColor { viewport, rail, grid, label, overview, count };

// Call once before creating the first frame. WORMEDIT_THEME_DIR overrides the bundled files.
void load(const std::filesystem::path& bundled_directory);
auto ui(UiColor color) -> nk_color;
auto compose(ComposeColor color) -> nk_color;
auto compose_lane(size_t lane, bool alternate) -> nk_color;
auto compose_note_accent(size_t index) -> nk_color;

} // namespace wormedit::ui::theme

#pragma once

#include "wormedit/nuklear_config.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::ui::theme {

enum class UiColor {
    background,
    surface,
    surface_hover,
    surface_active,
    border,
    border_hover,
    text,
    text_bright,
    text_muted,
    selected,
    selected_text,
    accent,
    status,
    conflict,
    separator,
    popup,
    count
};

enum class ComposeColor {
    viewport,
    rail,
    grid,
    label,
    overview,
    semantic_layer_none,
    selection_outline,
    measure_line,
    beat_line,
    subgrid_line,
    subgrid_emphasis,
    lane_line,
    lane_label,
    conflict,
    hover_fill,
    hover_border,
    hold_hover,
    erase_fill,
    erase_border,
    note_border,
    selection_box,
    selection_box_border,
    selection_add,
    selection_add_border,
    selection_toggle,
    selection_toggle_border,
    bgm_selected,
    bgm_border,
    bgm_label,
    band,
    band_split,
    band_top,
    band_bottom,
    bgm_line,
    timing_note,
    timing_border,
    timing_selected,
    timing_hover,
    timing_title_bg,
    tail,
    arrow_selection,
    count
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
    nk_color note;
    nk_color note_border;
    nk_color hold_body;
    nk_color lane_alternate;
    nk_color lane_label;
    std::optional<nk_color> label{}, long_label{}, long_note{};
};
auto compose_key_color(std::uint8_t key_color, std::uint64_t lane_id = 0)
    -> key_color_t;
struct field_lane_t {
    std::uint32_t display_order{};
    std::uint8_t stage{}, key_color{}, role{};
    std::uint64_t lane_id{};
    auto operator==(const field_lane_t&) const -> bool = default;
};
// Semantic colors use each object's class. Only compatible numbered exceptions bind by authored ID.
void bind_field_layout(std::span<const field_lane_t> lanes);
auto compose_bgm_color() -> key_color_t;
auto compose_timing_color(std::size_t kind) -> key_color_t;
struct field_scheme_t {
    std::string id, name;
};
auto field_schemes() -> const std::vector<field_scheme_t>&;
// Empty selects Wormedit; unknown IDs fall back with one diagnostic per ID.
auto select_field_scheme(std::string_view id) -> bool;
auto field_scheme_id() -> std::string_view;
// Core stage values: none/left use 1P, right uses 2P; absent means chart layout.
// false = visually left, true = visually right; authored lane identity never changes.
auto field_scratch_side(std::uint8_t stage) -> std::optional<bool>;
auto field_scratch_side(std::uint8_t stage, std::span<const field_lane_t> lanes) -> std::optional<bool>;
// Semantic palettes support every layout; numbered palettes require matching stages/classes.
auto field_scheme_supports_layout(std::string_view id, std::span<const field_lane_t> lanes) -> bool;
// Empty request uses fallback; incompatible requests never rewrite persisted preferences.
auto select_field_scheme_for_layout(std::string_view requested, std::string_view fallback,
    std::span<const field_lane_t> lanes) -> bool;
auto field_generation() -> std::uint64_t;
void set_snap_colored_grid(bool enabled);
void set_snap_grid_opacity(std::uint32_t percent);
void set_field_waveform_opacity(std::uint32_t percent);
auto field_waveform_color(nk_color color) -> nk_color;
auto snap_colored_grid() -> bool;
struct grid_line_style_t { nk_color color; float thickness; };
auto compose_grid_line_style(std::uint8_t kind, std::uint8_t emphasis, std::uint8_t snap_class)
    -> grid_line_style_t;
auto field_uses_positions() -> bool;

} // namespace wormedit::ui::theme

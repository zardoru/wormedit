#pragma once
#include <wormedit/core/result.h>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace wormedit {
inline constexpr float dock_minimum_width = 240.0f;
inline constexpr float dock_maximum_width = 520.0f;
inline constexpr double dock_minimum_split = 0.2;
inline constexpr double dock_maximum_split = 0.8;
enum class dock_panel_t { inspector, sounds, images, measures, layers, markers };
struct dock_panel_definition_t {
    dock_panel_t id;
    std::string_view key;
    std::string_view title;
};
inline constexpr std::array dock_panel_catalog{
    dock_panel_definition_t{dock_panel_t::inspector, "inspector", "Inspector"},
    dock_panel_definition_t{dock_panel_t::sounds, "sounds", "Sounds"},
    dock_panel_definition_t{dock_panel_t::images, "images", "Images"},
    dock_panel_definition_t{dock_panel_t::measures, "measures", "Measures"},
    dock_panel_definition_t{dock_panel_t::layers, "layers", "Layers"},
    dock_panel_definition_t{dock_panel_t::markers, "markers", "Markers"}};

// Application preferences; the order also determines the order of the two visible panes.
class ui_layout_preferences_t {
  public:
    ui_layout_preferences_t() = default;
    [[nodiscard]] static auto from(std::string_view text)
        -> result_t<ui_layout_preferences_t, error_t<failure_code_t>>;
    [[nodiscard]] auto serialized() const -> std::string;
    [[nodiscard]] auto is_visible(dock_panel_t panel) const -> bool;
    [[nodiscard]] auto visible_panels() const -> std::array<std::optional<dock_panel_t>, 2>;
    [[nodiscard]] auto order() const -> const std::array<dock_panel_t, 6>& { return order_; }
    [[nodiscard]] auto width() const -> float { return width_; }
    [[nodiscard]] auto split() const -> float { return split_; }
    [[nodiscard]] auto is_side_by_side() const -> bool { return side_by_side_; }
    void set_side_by_side(bool enabled) { side_by_side_ = enabled; }
    void show(dock_panel_t panel, bool visible = true);
    void activate(dock_panel_t panel, bool stacked);
    void reorder(dock_panel_t panel, std::size_t destination);
    void set_width(float width);
    void set_split(float split);
    auto operator==(const ui_layout_preferences_t&) const -> bool = default;
  private:
    std::array<dock_panel_t, 6> order_{dock_panel_t::inspector, dock_panel_t::sounds,
        dock_panel_t::images, dock_panel_t::measures, dock_panel_t::layers, dock_panel_t::markers};
    std::array<bool, 6> visible_{true, true, false, false, false, false};
    float width_{320.0f};
    float split_{0.5f};
    bool side_by_side_{};
};
} // namespace wormedit

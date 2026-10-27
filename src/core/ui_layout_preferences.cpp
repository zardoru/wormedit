#include <wormedit/core/ui_layout_preferences.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace wormedit {
namespace {
using json_t = nlohmann::json;
auto panel_from(const json_t& value) -> std::optional<dock_panel_t> {
    if (!value.is_string()) return std::nullopt;
    for (const auto& entry : dock_panel_catalog)
        if (entry.key == value.get_ref<const std::string&>()) return entry.id;
    return std::nullopt;
}
// Records saved before a panel existed omit it; missing panels follow in catalog order.
auto ordered_panels(const json_t& value) -> std::optional<std::array<dock_panel_t, dock_panel_count>> {
    if (!value.is_array() || value.empty() || value.size() > dock_panel_count) return std::nullopt;
    std::array<dock_panel_t, dock_panel_count> result{};
    std::array<bool, dock_panel_count> seen{};
    std::size_t count{};
    for (const auto& item : value) {
        const auto panel = panel_from(item);
        if (!panel || seen[static_cast<std::size_t>(*panel)]) return std::nullopt;
        seen[static_cast<std::size_t>(*panel)] = true;
        result[count++] = *panel;
    }
    for (const auto& entry : dock_panel_catalog)
        if (!seen[static_cast<std::size_t>(entry.id)]) result[count++] = entry.id;
    return result;
}
auto read_visible(const json_t& value, ui_layout_preferences_t& layout) -> bool {
    if (!value.is_array() || value.size() > 2) return false;
    for (const auto& entry : dock_panel_catalog) layout.show(entry.id, false);
    for (const auto& item : value) {
        const auto panel = panel_from(item);
        if (!panel || layout.is_visible(*panel)) return false;
        layout.show(*panel);
    }
    return true;
}
auto has_layout_shape(const json_t& value) -> bool {
    if (!value.is_object()) return false;
    const auto version = value.value("version", json_t{});
    if (version == 1) return value.size() == 5;
    return version == 2 && value.size() == 6 && value.contains("side_by_side") &&
        value.at("side_by_side").is_boolean();
}
auto read_dimensions(const json_t& value, ui_layout_preferences_t& layout) -> bool {
    const auto width = value.find("width");
    const auto split = value.find("split");
    if (width == value.end() || split == value.end() || !width->is_number() || !split->is_number()) return false;
    const auto w = width->get<double>();
    const auto s = split->get<double>();
    if (!std::isfinite(w) || !std::isfinite(s) || w < dock_minimum_width || w > dock_maximum_width || s < dock_minimum_split || s > dock_maximum_split) return false;
    layout.set_width(static_cast<float>(w));
    layout.set_split(static_cast<float>(s));
    return true;
}
}
auto ui_layout_preferences_t::from(std::string_view text)
    -> result_t<ui_layout_preferences_t, error_t<failure_code_t>> {
    const auto failure = error_t<failure_code_t>{failure_code_t::invalid_request,
        "Dock layout is invalid or unsupported; defaults retained."};
    if (text.size() > 4096) return std::unexpected(failure);
    const auto value = json_t::parse(text, nullptr, false);
    if (!has_layout_shape(value))
        return std::unexpected(failure);
    const auto order = value.find("order");
    const auto visible = value.find("visible");
    if (order == value.end() || visible == value.end()) return std::unexpected(failure);
    const auto ordered = ordered_panels(*order);
    ui_layout_preferences_t layout;
    if (!ordered || !read_visible(*visible, layout) || !read_dimensions(value, layout))
        return std::unexpected(failure);
    layout.order_ = *ordered;
    layout.side_by_side_ = value.value("side_by_side", false);
    return layout;
}
auto ui_layout_preferences_t::serialized() const -> std::string {
    auto order = json_t::array();
    auto visible = json_t::array();
    for (const auto panel : order_) {
        const auto key = dock_panel_catalog[static_cast<std::size_t>(panel)].key;
        order.push_back(key);
        if (is_visible(panel)) visible.push_back(key);
    }
    return json_t{{"version", 2}, {"order", order}, {"visible", visible},
                  {"width", width_}, {"split", split_}, {"side_by_side", side_by_side_}}.dump();
}
auto ui_layout_preferences_t::is_visible(dock_panel_t panel) const -> bool {
    const auto index = static_cast<std::size_t>(panel);
    return index < visible_.size() && visible_[index];
}
auto ui_layout_preferences_t::visible_panels() const -> std::array<std::optional<dock_panel_t>, 2> {
    std::array<std::optional<dock_panel_t>, 2> result;
    std::size_t index{};
    for (const auto panel : order_)
        if (is_visible(panel)) result[index++] = panel;
    return result;
}
void ui_layout_preferences_t::show(dock_panel_t panel, bool visible) {
    const auto index = static_cast<std::size_t>(panel);
    if (index >= visible_.size() || visible_[index] == visible) return;
    if (visible) {
        const auto panes = visible_panels();
        if (panes.back()) visible_[static_cast<std::size_t>(*panes.back())] = false;
    }
    visible_[index] = visible;
}
void ui_layout_preferences_t::activate(dock_panel_t panel, bool stacked) {
    if (static_cast<std::size_t>(panel) >= visible_.size()) return;
    if (stacked) { show(panel, !is_visible(panel)); return; }
    visible_.fill(false);
    show(panel);
}
void ui_layout_preferences_t::reorder(dock_panel_t panel, std::size_t destination) {
    const auto found = std::ranges::find(order_, panel);
    if (found == order_.end() || destination >= order_.size()) return;
    const auto source = static_cast<std::size_t>(found - order_.begin());
    if (source < destination) std::rotate(found, found + 1, order_.begin() + destination + 1);
    else std::rotate(order_.begin() + destination, found, found + 1);
}
void ui_layout_preferences_t::set_width(float width) {
    if (std::isfinite(width)) width_ = std::clamp(width, dock_minimum_width, dock_maximum_width);
}
void ui_layout_preferences_t::set_split(float split) {
    if (std::isfinite(split)) split_ = std::clamp(split, static_cast<float>(dock_minimum_split), static_cast<float>(dock_maximum_split));
}
} // namespace wormedit

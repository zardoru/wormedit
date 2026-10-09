#include "ui/nuklear/theme.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace wormedit::ui::theme {
namespace {

constexpr std::array<std::string_view, static_cast<size_t>(UiColor::count)> ui_names{
    "background", "surface", "surface_hover", "surface_active", "border",
    "border_hover", "text", "text_bright", "text_muted", "selected",
    "selected_text", "accent", "status", "conflict"};
constexpr std::array<std::string_view, static_cast<size_t>(ComposeColor::count)> compose_names{
    "viewport", "rail", "grid", "label", "overview", "semantic_layer_none", "selection_outline"};
// Indexed by wormedit::lane_key_color_t; the names match its enumerators.
constexpr std::array<std::string_view, 11> key_names{
    "generic", "white", "black", "scratch", "foot_pedal", "free_zone",
    "pms_white", "pms_yellow", "pms_green", "pms_blue", "pms_red"};

std::array<nk_color, ui_names.size()> ui_colors{
    nk_rgb(18, 19, 26), nk_rgb(30, 31, 40), nk_rgb(44, 46, 60),
    nk_rgb(58, 60, 75), nk_rgb(58, 60, 75), nk_rgb(134, 180, 255),
    nk_rgb(206, 210, 223), nk_rgb(245, 246, 250), nk_rgb(173, 179, 197),
    nk_rgb(20, 92, 227), nk_rgb(255, 255, 255), nk_rgb(201, 211, 94),
    nk_rgb(30, 31, 40), nk_rgb(248, 116, 89)};
std::array<nk_color, compose_names.size()> compose_colors{
    nk_rgb(22, 25, 33), nk_rgb(18, 21, 29), nk_rgb(58, 67, 83),
    nk_rgb(190, 196, 207), nk_rgb(22, 31, 42), nk_rgb(148, 153, 163),
    nk_rgb(74, 222, 128)};
// Built-in colors: every key starts as the generic entry until the theme file overrides it.
auto builtin_key_colors() -> std::array<key_color_t, key_names.size()> {
    std::array<key_color_t, key_names.size()> colors{};
    for (size_t index = 0; index < colors.size(); ++index) {
        colors[index] = {index != 0, nk_rgb(33, 40, 53), nk_rgb(133, 182, 222),
                         nk_rgb(58, 90, 120), nk_rgba(133, 182, 222, 153)};
    }
    return colors;
}
std::array<key_color_t, key_names.size()> key_colors = builtin_key_colors();
std::array<nk_color, 8> note_accents{
    nk_rgb(242, 190, 102), nk_rgb(177, 206, 129), nk_rgb(113, 184, 218),
    nk_rgb(224, 132, 119), nk_rgb(207, 153, 104), nk_rgb(176, 142, 218),
    nk_rgb(244, 116, 174), nk_rgb(130, 219, 206)};

auto hex_digit(char ch) -> int {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

// Accepts #rrggbb and #rrggbbaa.
auto hex_color(std::string_view value, nk_color& color) -> bool {
    if ((value.size() != 7 && value.size() != 9) || value.front() != '#') return false;
    const auto channel_count = (value.size() - 1) / 2;
    std::array<int, 4> channels{255, 255, 255, 255};
    for (size_t index = 0; index < channel_count; ++index) {
        const int high = hex_digit(value[1 + index * 2]);
        const int low = hex_digit(value[2 + index * 2]);
        if (high < 0 || low < 0) return false;
        channels[index] = high * 16 + low;
    }
    color = nk_rgba(channels[0], channels[1], channels[2], channels[3]);
    return true;
}

auto resolve(const nlohmann::json& document, const nlohmann::json& value,
             nk_color& color) -> bool {
    if (!value.is_string()) return false;
    const auto token = value.get<std::string>();
    if (!token.empty() && token.front() == '$') {
        const auto dot = token.find('.');
        if (dot == std::string::npos) return false;
        const auto scale = token.substr(1, dot - 1);
        const auto shade = token.substr(dot + 1);
        if (!document.contains("scales") || !document["scales"].contains(scale) ||
            !document["scales"][scale].contains(shade) ||
            !document["scales"][scale][shade].is_string()) return false;
        return hex_color(document["scales"][scale][shade].get<std::string>(), color);
    }
    return hex_color(token, color);
}

template <size_t N>
void read_roles(const nlohmann::json& document, const std::array<std::string_view, N>& names,
                std::array<nk_color, N>& colors) {
    if (!document.contains("roles") || !document["roles"].is_object()) return;
    const auto& roles = document["roles"];
    for (size_t index = 0; index < N; ++index) {
        const auto name = std::string(names[index]);
        if (roles.contains(name)) resolve(document, roles[name], colors[index]);
    }
}

// A missing file is silent (built-in colors apply); a malformed one is reported once.
auto read_document(const std::filesystem::path& path) -> nlohmann::json {
    std::ifstream input(path);
    if (!input) return {};
    try {
        return nlohmann::json::parse(input);
    } catch (const nlohmann::json::exception& error) {
        std::cerr << "wormedit: theme file " << path.string() << " is malformed ("
                  << error.what() << "); using built-in colors for it\n";
        return {};
    }
}

void read_key_entry(const nlohmann::json& document, const nlohmann::json& entry,
                    key_color_t& color) {
    if (!entry.is_object()) return;
    resolve(document, entry.value("note", nlohmann::json{}), color.note);
    resolve(document, entry.value("note_border", nlohmann::json{}), color.note_border);
    resolve(document, entry.value("lane", nlohmann::json{}), color.lane);
    resolve(document, entry.value("hold_body", nlohmann::json{}), color.hold_body);
}

// Each key color starts from the generic entry, so a missing or malformed entry falls back to it.
void read_key_colors(const nlohmann::json& document) {
    if (!document.contains("key_colors") || !document["key_colors"].is_object()) return;
    const auto& entries = document["key_colors"];
    read_key_entry(document, entries.value(std::string(key_names[0]), nlohmann::json{}),
                   key_colors[0]);
    for (size_t index = 1; index < key_names.size(); ++index) {
        auto color = key_colors[0];
        color.has_color = true;
        read_key_entry(document, entries.value(std::string(key_names[index]), nlohmann::json{}),
                       color);
        key_colors[index] = color;
    }
}

void read_note_accents(const nlohmann::json& document) {
    if (document.contains("note_accents") && document["note_accents"].is_array() &&
        document["note_accents"].size() == note_accents.size()) {
        for (size_t index = 0; index < note_accents.size(); ++index)
            resolve(document, document["note_accents"][index], note_accents[index]);
    }
}

} // namespace

void load(const std::filesystem::path& bundled_directory) {
    const char* override_directory = std::getenv("WORMEDIT_THEME_DIR");
    const auto directory = override_directory && *override_directory
                               ? std::filesystem::path(override_directory)
                               : bundled_directory;
    read_roles(read_document(directory / "ui-dark.json"), ui_names, ui_colors);
    const auto compose_document = read_document(directory / "compose-field.json");
    read_roles(compose_document, compose_names, compose_colors);
    read_key_colors(compose_document);
    read_note_accents(compose_document);
}

auto ui(UiColor color) -> nk_color { return ui_colors[static_cast<size_t>(color)]; }
auto compose(ComposeColor color) -> nk_color {
    return compose_colors[static_cast<size_t>(color)];
}
auto compose_key_color(std::uint8_t key_color) -> key_color_t {
    if (key_color == 0 || key_color >= key_colors.size()) return key_colors[0];
    return key_colors[key_color];
}
auto compose_note_accent(size_t index) -> nk_color {
    return note_accents[index % note_accents.size()];
}

} // namespace wormedit::ui::theme

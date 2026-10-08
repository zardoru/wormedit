#include "ui/nuklear/theme.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

namespace wormedit::ui::theme {
namespace {

constexpr std::array<std::string_view, static_cast<size_t>(UiColor::count)> ui_names{
    "background", "surface", "surface_hover", "surface_active", "border",
    "border_hover", "text", "text_bright", "text_muted", "selected",
    "selected_text", "accent", "status", "conflict"};
constexpr std::array<std::string_view, static_cast<size_t>(ComposeColor::count)> compose_names{
    "viewport", "rail", "grid", "label", "overview", "semantic_layer_none"};

std::array<nk_color, ui_names.size()> ui_colors{
    nk_rgb(18, 19, 26), nk_rgb(30, 31, 40), nk_rgb(44, 46, 60),
    nk_rgb(58, 60, 75), nk_rgb(58, 60, 75), nk_rgb(134, 180, 255),
    nk_rgb(206, 210, 223), nk_rgb(245, 246, 250), nk_rgb(173, 179, 197),
    nk_rgb(20, 92, 227), nk_rgb(255, 255, 255), nk_rgb(201, 211, 94),
    nk_rgb(30, 31, 40), nk_rgb(248, 116, 89)};
std::array<nk_color, compose_names.size()> compose_colors{
    nk_rgb(22, 25, 33), nk_rgb(18, 21, 29), nk_rgb(58, 67, 83),
    nk_rgb(190, 196, 207), nk_rgb(22, 31, 42), nk_rgb(148, 153, 163)};
std::array<std::array<nk_color, 2>, 4> lane_colors{{
    {nk_rgb(42, 39, 61), nk_rgb(48, 44, 68)},
    {nk_rgb(42, 49, 58), nk_rgb(47, 54, 63)},
    {nk_rgb(36, 52, 54), nk_rgb(40, 58, 59)},
    {nk_rgb(33, 40, 53), nk_rgb(37, 45, 59)}}};
std::array<nk_color, 8> note_accents{
    nk_rgb(242, 190, 102), nk_rgb(177, 206, 129), nk_rgb(113, 184, 218),
    nk_rgb(224, 132, 119), nk_rgb(207, 153, 104), nk_rgb(176, 142, 218),
    nk_rgb(244, 116, 174), nk_rgb(130, 219, 206)};

auto hex_color(std::string_view value, nk_color& color) -> bool {
    if (value.size() != 7 || value.front() != '#') return false;
    auto digit = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    std::array<int, 3> channels{};
    for (size_t index = 0; index < channels.size(); ++index) {
        const int high = digit(value[1 + index * 2]);
        const int low = digit(value[2 + index * 2]);
        if (high < 0 || low < 0) return false;
        channels[index] = high * 16 + low;
    }
    color = nk_rgb(channels[0], channels[1], channels[2]);
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

auto read_document(const std::filesystem::path& path) -> nlohmann::json {
    std::ifstream input(path);
    if (!input) return {};
    try {
        return nlohmann::json::parse(input);
    } catch (const nlohmann::json::exception&) {
        return {};
    }
}

void read_compose_arrays(const nlohmann::json& document) {
    constexpr std::array<std::string_view, 4> lane_names{"scratch", "free_zone",
                                                           "foot_pedal", "default"};
    if (document.contains("lane_backgrounds") && document["lane_backgrounds"].is_object()) {
        const auto& lanes = document["lane_backgrounds"];
        for (size_t lane = 0; lane < lane_names.size(); ++lane) {
            const auto name = std::string(lane_names[lane]);
            if (!lanes.contains(name) || !lanes[name].is_array() || lanes[name].size() != 2) continue;
            for (size_t shade = 0; shade < 2; ++shade)
                resolve(document, lanes[name][shade], lane_colors[lane][shade]);
        }
    }
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
    read_compose_arrays(compose_document);
}

auto ui(UiColor color) -> nk_color { return ui_colors[static_cast<size_t>(color)]; }
auto compose(ComposeColor color) -> nk_color {
    return compose_colors[static_cast<size_t>(color)];
}
auto compose_lane(size_t lane, bool alternate) -> nk_color {
    return lane_colors[lane < lane_colors.size() ? lane : lane_colors.size() - 1][alternate];
}
auto compose_key_color(std::uint8_t key_color, bool alternate) -> key_color_t {
    const auto lane = [alternate](int r, int g, int b) {
        const auto shift = alternate ? 5 : 0;
        return nk_rgb(r + shift, g + shift, b + shift);
    };
    switch (key_color) {
    case 1: return {true, lane(58, 62, 74), nk_rgb(226, 232, 240)};   // white
    case 2: return {true, lane(24, 27, 36), nk_rgb(96, 128, 190)};    // black
    case 3: return {true, lane(62, 32, 40), nk_rgb(232, 84, 96)};     // scratch
    case 4: return {true, lane(36, 52, 54), nk_rgb(113, 201, 216)};   // foot pedal
    case 5: return {true, lane(42, 49, 58), nk_rgb(236, 194, 112)};   // free zone
    case 6: return {true, lane(58, 62, 74), nk_rgb(238, 240, 246)};   // pms white
    case 7: return {true, lane(60, 56, 30), nk_rgb(244, 214, 82)};    // pms yellow
    case 8: return {true, lane(30, 58, 38), nk_rgb(96, 206, 118)};    // pms green
    case 9: return {true, lane(30, 42, 66), nk_rgb(92, 150, 240)};    // pms blue
    case 10: return {true, lane(66, 32, 36), nk_rgb(240, 90, 92)};    // pms red
    default: return {};
    }
}
auto compose_note_accent(size_t index) -> nk_color {
    return note_accents[index % note_accents.size()];
}

} // namespace wormedit::ui::theme

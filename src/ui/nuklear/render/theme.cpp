#include "ui/nuklear/chrome/color.h"
#include "ui/nuklear/render/theme.h"
#include "ui/nuklear/render/arrow_glyph.h"
#include <wormedit/core/lane_profiles.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace wormedit::ui::theme {
namespace {

constexpr std::array<std::string_view, static_cast<size_t>(UiColor::count)> ui_names{
    "background",    "surface", "surface_hover", "surface_active", "border",
    "border_hover",  "text",    "text_bright",   "text_muted",     "selected",
    "selected_text", "accent",  "status",        "conflict",      "separator",     "popup",
    "waveform_background", "waveform_guide", "waveform_rms", "waveform_peak", "waveform_playhead"};
constexpr std::array<std::string_view, static_cast<size_t>(ComposeColor::count)> compose_names{
    "viewport",
    "rail",
    "grid",
    "label",
    "overview",
    "semantic_layer_none",
    "selection_outline",
    "measure_line",
    "beat_line",
    "subgrid_line",
    "subgrid_emphasis",
    "lane_line",
    "lane_label",
    "conflict",
    "hover_fill",
    "hover_border",
    "hold_hover",
    "erase_fill",
    "erase_border",
    "note_border",
    "selection_box",
    "selection_box_border",
    "selection_add",
    "selection_add_border",
    "selection_toggle",
    "selection_toggle_border",
    "bgm_selected",
    "bgm_border",
    "bgm_label",
    "band",
    "band_split",
    "band_top",
    "band_bottom",
    "bgm_line",
    "timing_note",
    "timing_border",
    "timing_selected",
    "timing_hover",
    "timing_title_bg",
    "tail",
    "arrow_selection", "export_cut"};
// Indexed by wormedit::lane_key_color_t; the names match its enumerators.
constexpr std::array<std::string_view, 11> key_names{
    "generic",   "white",      "black",     "scratch",  "foot_pedal", "free_zone",
    "pms_white", "pms_yellow", "pms_green", "pms_blue", "pms_red"};

std::array<nk_color, ui_names.size()> ui_colors{
    nk_rgb(18, 19, 26),    nk_rgb(30, 31, 40),    nk_rgb(44, 46, 60),    nk_rgb(58, 60, 75),
    nk_rgb(58, 60, 75),    nk_rgb(134, 180, 255), nk_rgb(206, 210, 223), nk_rgb(245, 246, 250),
    nk_rgb(173, 179, 197), nk_rgb(20, 92, 227),   nk_rgb(255, 255, 255), nk_rgb(201, 211, 94),
    nk_rgb(139, 146, 169), nk_rgb(248, 116, 89), nk_rgb(44, 46, 60), nk_rgb(30, 31, 40),
    nk_rgb(22, 25, 33), nk_rgb(57, 63, 77), nk_rgb(59, 122, 147), nk_rgb(107, 214, 235), nk_rgb(255, 213, 99)};
const auto builtin_ui_colors = ui_colors;
std::filesystem::path interface_directory;
std::vector<interface_theme_t> interface_catalog;
std::array<nk_color, compose_names.size()> compose_colors{
    nk_rgb(22, 25, 33),          nk_rgb(18, 21, 29),          nk_rgb(58, 67, 83),
    nk_rgb(190, 196, 207),       nk_rgb(22, 31, 42),          nk_rgb(148, 153, 163),
    nk_rgb(74, 222, 128),        nk_rgba(95, 107, 128, 255),  nk_rgba(60, 70, 88, 255),
    nk_rgba(42, 49, 63, 255),    nk_rgba(52, 61, 78, 255),    nk_rgba(62, 70, 86, 255),
    nk_rgba(240, 244, 250, 255), nk_rgba(248, 116, 89, 255),  nk_rgba(255, 226, 127, 255),
    nk_rgba(255, 247, 190, 255), nk_rgba(255, 235, 148, 255), nk_rgba(244, 94, 111, 210),
    nk_rgba(255, 184, 190, 255), nk_rgba(17, 24, 34, 255),    nk_rgba(101, 183, 242, 34),
    nk_rgba(101, 183, 242, 255), nk_rgba(113, 214, 164, 34),  nk_rgba(113, 214, 164, 255),
    nk_rgba(197, 151, 245, 34),  nk_rgba(197, 151, 245, 255), nk_rgba(248, 224, 255, 255),
    nk_rgba(50, 35, 58, 255),    nk_rgba(255, 245, 255, 255), nk_rgba(25, 29, 39, 255),
    nk_rgba(27, 31, 43, 255),    nk_rgba(80, 91, 112, 255),   nk_rgba(61, 70, 87, 255),
    nk_rgba(79, 67, 91, 255),    nk_rgba(18, 22, 31, 255),    nk_rgba(55, 64, 79, 255),
    nk_rgba(116, 92, 49, 255),   nk_rgba(255, 220, 112, 255), nk_rgba(18, 21, 29, 215),
    nk_rgba(113, 201, 216, 255), nk_rgb(220, 247, 255), nk_rgb(236, 104, 174)};
// Built-in Wormedit fallback remains complete if the default asset is missing.
auto builtin_key_colors() -> std::array<key_color_t, key_names.size()> {
    std::array<key_color_t, key_names.size()> colors{};
    colors[0] = {false,
                 nk_rgba(33, 40, 53, 160),
                 nk_rgba(133, 182, 222, 255),
                 nk_rgba(58, 90, 120, 255),
                 nk_rgba(133, 182, 222, 153),
                 nk_rgba(37, 44, 57, 160),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[1] = {true,
                 nk_rgba(44, 48, 58, 160),
                 nk_rgba(226, 232, 240, 255),
                 nk_rgba(51, 65, 85, 255),
                 nk_rgba(226, 232, 240, 153),
                 nk_rgba(48, 52, 62, 160),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[2] = {true,
                 nk_rgba(30, 37, 50, 160),
                 nk_rgba(59, 130, 246, 255),
                 nk_rgba(30, 64, 175, 255),
                 nk_rgba(59, 130, 246, 153),
                 nk_rgba(34, 41, 54, 160),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[3] = {true,
                 nk_rgba(48, 34, 40, 160),
                 nk_rgba(248, 113, 113, 255),
                 nk_rgba(185, 28, 28, 255),
                 nk_rgba(248, 113, 113, 153),
                 nk_rgba(52, 38, 44, 160),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[4] = {true,
                 nk_rgba(30, 43, 47, 160),
                 nk_rgba(34, 211, 238, 255),
                 nk_rgba(14, 116, 144, 255),
                 nk_rgba(34, 211, 238, 153),
                 nk_rgba(34, 47, 51, 160),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[5] = {true,
                 nk_rgba(47, 41, 32, 160),
                 nk_rgba(251, 191, 36, 255),
                 nk_rgba(180, 83, 9, 255),
                 nk_rgba(251, 191, 36, 153),
                 nk_rgba(51, 45, 36, 160),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[6] = {true,
                 nk_rgba(44, 48, 58, 160),
                 nk_rgba(233, 235, 242, 255),
                 nk_rgba(51, 65, 85, 255),
                 nk_rgba(233, 235, 242, 153),
                 nk_rgba(48, 52, 62, 160),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[7] = {true,
                 nk_rgba(47, 44, 31, 160),
                 nk_rgba(250, 204, 21, 255),
                 nk_rgba(161, 98, 7, 255),
                 nk_rgba(250, 204, 21, 153),
                 nk_rgba(51, 48, 35, 160),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[8] = {true,
                 nk_rgba(29, 45, 36, 160),
                 nk_rgba(74, 222, 128, 255),
                 nk_rgba(21, 128, 61, 255),
                 nk_rgba(74, 222, 128, 153),
                 nk_rgba(33, 49, 40, 160),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[9] = {true,
                 nk_rgba(30, 39, 55, 160),
                 nk_rgba(96, 165, 250, 255),
                 nk_rgba(29, 78, 216, 255),
                 nk_rgba(96, 165, 250, 153),
                 nk_rgba(34, 43, 59, 160),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[10] = {true,
                  nk_rgba(48, 34, 40, 160),
                  nk_rgba(248, 113, 113, 255),
                  nk_rgba(185, 28, 28, 255),
                  nk_rgba(248, 113, 113, 153),
                  nk_rgba(52, 38, 44, 160),
                  nk_rgba(240, 244, 250, 255),
                  {},
                  {},
                  {}};
    colors[6] = colors[1];
    colors[9] = colors[2];
    colors[10] = colors[3];
    return colors;
}
std::array<key_color_t, key_names.size()> key_colors = builtin_key_colors();
std::array<nk_color, 8> note_accents{
    nk_rgb(242, 190, 102), nk_rgb(177, 206, 129), nk_rgb(113, 184, 218), nk_rgb(224, 132, 119),
    nk_rgb(207, 153, 104), nk_rgb(176, 142, 218), nk_rgb(244, 116, 174), nk_rgb(130, 219, 206)};

struct field_palette_t {
    decltype(compose_colors) roles = compose_colors;
    decltype(key_colors) keys = key_colors;
    decltype(note_accents) accents = note_accents;
    nlohmann::json numbered_layout;
    bool rotate_dp5{};
    std::array<std::optional<bool>, 2> scratch_sides{};
    nlohmann::json document;
    key_color_t bgm{true,
                    nk_rgb(38, 34, 47),
                    nk_rgb(177, 116, 207),
                    nk_rgb(50, 35, 58),
                    nk_rgb(102, 88, 113),
                    nk_rgb(43, 38, 52),
                    nk_rgb(255, 245, 255),
                    {},
                    {}};
    std::array<key_color_t, 8> timing{};
};
const auto builtin_roles = compose_colors;
const auto builtin_keys = key_colors;
const auto builtin_accents = note_accents;
std::map<std::string, field_palette_t> palettes;
std::vector<field_scheme_t> schemes;
std::unordered_map<std::uint64_t, std::pair<std::uint8_t, key_color_t>> layout_colors;
std::vector<field_lane_t> bound_layout;
auto create_default_palette() -> field_palette_t;
field_palette_t active = create_default_palette();
std::string active_id = "wormedit";
std::set<std::string> warned_ids;
std::uint64_t generation{};
bool colored_grid{true};
std::uint32_t grid_opacity_percent{35};
std::uint32_t waveform_opacity_percent{75};
constexpr std::array<std::string_view, 8> timing_names{"bpm",   "scroll", "speed", "stop",
                                                       "delay", "warp",   "kiai",  "anchor"};

auto hex_digit(char ch) -> int {
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    return -1;
}

// Accepts #rrggbb and #rrggbbaa.
auto hex_color(std::string_view value, nk_color& color) -> bool {
    if ((value.size() != 7 && value.size() != 9) || value.front() != '#')
        return false;
    const auto channel_count = (value.size() - 1) / 2;
    std::array<int, 4> channels{255, 255, 255, 255};
    for (size_t index = 0; index < channel_count; ++index) {
        const int high = hex_digit(value[1 + index * 2]);
        const int low = hex_digit(value[2 + index * 2]);
        if (high < 0 || low < 0)
            return false;
        channels[index] = high * 16 + low;
    }
    color = nk_rgba(channels[0], channels[1], channels[2], channels[3]);
    return true;
}

auto resolve(const nlohmann::json& document, const nlohmann::json& value, nk_color& color) -> bool {
    if (!value.is_string())
        return false;
    const auto token = value.get<std::string>();
    if (!token.empty() && token.front() == '$') {
        const auto dot = token.find('.');
        if (dot == std::string::npos)
            return false;
        const auto scale = token.substr(1, dot - 1);
        const auto shade = token.substr(dot + 1);
        if (!document.contains("scales") || !document["scales"].contains(scale) ||
            !document["scales"][scale].contains(shade) ||
            !document["scales"][scale][shade].is_string())
            return false;
        return hex_color(document["scales"][scale][shade].get<std::string>(), color);
    }
    return hex_color(token, color);
}

template <size_t N>
void read_roles(const nlohmann::json& document, const std::array<std::string_view, N>& names,
                std::array<nk_color, N>& colors) {
    if (!document.contains("roles") || !document["roles"].is_object())
        return;
    const auto& roles = document["roles"];
    for (size_t index = 0; index < N; ++index) {
        const auto name = std::string(names[index]);
        if (roles.contains(name))
            resolve(document, roles[name], colors[index]);
    }
}

// A missing file is silent (built-in colors apply); a malformed one is reported once.
auto read_document(const std::filesystem::path& path) -> nlohmann::json {
    std::ifstream input(path);
    if (!input)
        return {};
    try {
        return nlohmann::json::parse(input);
    } catch (const nlohmann::json::exception& error) {
        std::cerr << "wormedit: theme file " << path.string() << " is malformed (" << error.what()
                  << "); using built-in colors for it\n";
        return {};
    }
}

void read_key_entry(const nlohmann::json& document, const nlohmann::json& entry,
                    key_color_t& color) {
    if (!entry.is_object())
        return;
    resolve(document, entry.value("note", nlohmann::json{}), color.note);
    resolve(document, entry.value("note_border", nlohmann::json{}), color.note_border);
    resolve(document, entry.value("lane", nlohmann::json{}), color.lane);
    if (entry.contains("lane"))
        color.lane_alternate = color.lane;
    resolve(document, entry.value("lane_alternate", nlohmann::json{}), color.lane_alternate);
    resolve(document, entry.value("lane_label", nlohmann::json{}), color.lane_label);
    nk_color text{};
    if (resolve(document, entry.value("label", nlohmann::json{}), text))
        color.label = text;
    if (resolve(document, entry.value("long_label", nlohmann::json{}), text))
        color.long_label = text;
    if (resolve(document, entry.value("long_note", nlohmann::json{}), text))
        color.long_note = text;
    resolve(document, entry.value("hold_body", nlohmann::json{}), color.hold_body);
}

// Each key color starts from the generic entry, so a missing or malformed entry falls back to it.
void read_key_colors(const nlohmann::json& document) {
    if (!document.contains("key_colors") || !document["key_colors"].is_object())
        return;
    const auto& entries = document["key_colors"];
    read_key_entry(document, entries.value(std::string(key_names[0]), nlohmann::json{}),
                   key_colors[0]);
    for (size_t index = 1; index < key_names.size(); ++index) {
        auto color = key_colors[index];
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

void validate_colors(const nlohmann::json& document, const nlohmann::json& value) {
    if (value.is_object() || value.is_array()) {
        for (const auto& child : value)
            validate_colors(document, child);
        return;
    }
    nk_color color{};
    if (!resolve(document, value, color))
        throw std::runtime_error("invalid color or role token");
}
void validate_entries(const nlohmann::json& entries) {
    for (const auto& entry : entries)
        if (!entry.is_object())
            throw std::runtime_error("expected lane-role object");
}
constexpr std::array<std::string_view, 5> class_names{"white", "blue", "green", "yellow", "red"};
constexpr std::array<std::uint8_t, 5> class_keys{1, 2, 8, 7, 3};

auto semantic_class(const field_lane_t& lane) -> std::string_view {
    if (lane.role == 1) return "red";
    return wormedit::field_skin_key_class(static_cast<wormedit::lane_key_color_t>(lane.key_color));
}

void read_lane_classes(const nlohmann::json& document) {
    const auto entries = document.value("lane_classes", nlohmann::json::object());
    for (std::size_t i = 0; i < class_names.size(); ++i)
        read_key_entry(document, entries.value(std::string(class_names[i]), nlohmann::json{}),
                       key_colors[class_keys[i]]);
    key_colors[6] = key_colors[1];
    key_colors[9] = key_colors[2];
    key_colors[10] = key_colors[3];
}

void validate_lane_classes(const nlohmann::json& entries) {
    if (!entries.is_object()) throw std::runtime_error("expected lane_classes object");
    for (const auto& [name, entry] : entries.items()) {
        if (std::ranges::find(class_names, name) == class_names.end() || !entry.is_object())
            throw std::runtime_error("expected white/blue/green/yellow/red lane class objects");
    }
}

void validate_numbered_layout(const nlohmann::json& numbered) {
    if (!numbered.is_object() || !numbered.contains("stages") || !numbered.contains("colors"))
        throw std::runtime_error("numbered_layout requires stages and colors");
    const auto& stages = numbered["stages"];
    const auto& colors = numbered["colors"];
    if (!stages.is_array() || stages.empty() || stages.size() > 2 ||
        !colors.is_array() || colors.size() != stages.size())
        throw std::runtime_error("numbered_layout expects one or two matching stages");
    for (std::size_t stage = 0; stage < stages.size(); ++stage) {
        if (!stages[stage].is_array() || stages[stage].empty() ||
            !colors[stage].is_array() || colors[stage].size() != stages[stage].size())
            throw std::runtime_error("numbered_layout stage colors must match its classes");
        for (const auto& name : stages[stage])
            if (!name.is_string() || std::ranges::find(class_names, name.get<std::string>()) == class_names.end())
                throw std::runtime_error("numbered_layout expects semantic lane classes");
        validate_entries(colors[stage]);
    }
}

using numbered_stages_t = std::vector<std::vector<const field_lane_t*>>;
auto numbered_stages(std::span<const field_lane_t> lanes) -> numbered_stages_t {
    std::map<std::uint8_t, std::vector<const field_lane_t*>> groups;
    for (const auto& lane : lanes) {
        if (lane.role != 0 && lane.role != 1) continue;
        if (lane.stage > 2) return {};
        groups[lane.stage == 2 ? 1 : 0].push_back(&lane);
    }
    numbered_stages_t result;
    for (auto& [stage, group] : groups) {
        std::ranges::stable_sort(group, {}, [](const auto* lane) { return lane->display_order; });
        result.push_back(std::move(group));
    }
    return result;
}

auto numbered_matches(const nlohmann::json& numbered, const numbered_stages_t& groups) -> bool {
    if (numbered.is_null()) return true;
    const auto& stages = numbered["stages"];
    if (stages.size() != groups.size()) return false;
    for (std::size_t stage = 0; stage < groups.size(); ++stage) {
        if (stages[stage].size() != groups[stage].size()) return false;
        for (std::size_t key = 0; key < groups[stage].size(); ++key)
            if (stages[stage][key] != semantic_class(*groups[stage][key])) return false;
    }
    return true;
}

auto parse_scratch_sides(const nlohmann::json& document)
    -> std::array<std::optional<bool>, 2> {
    std::array<std::optional<bool>, 2> sides{};
    if (!document.contains("scratch_placement")) return sides;
    const auto& placements = document["scratch_placement"];
    if (!placements.is_object())
        throw std::runtime_error("expected scratch_placement object");
    for (const auto& [player, side] : placements.items()) {
        if (player != "1p" && player != "2p")
            throw std::runtime_error("scratch placement expects 1p or 2p");
        if (side != "left" && side != "right")
            throw std::runtime_error("scratch placement expects left or right");
        sides[player == "2p" ? 1 : 0] = side == "right";
    }
    return sides;
}
void validate_scheme(const nlohmann::json& document) {
    if (!document.is_object() || document.value("version", 0) != 2 || !document.contains("name") ||
        !document["name"].is_string() || document["name"].get<std::string>().empty())
        throw std::runtime_error("expected schema version 2 and a name");
    (void)parse_scratch_sides(document);
    if (document.contains("keys") || document.contains("key_count"))
        throw std::runtime_error("ungated positional keys are unsupported; use lane_classes or numbered_layout");
    if (document.contains("rotate_dp5") && !document["rotate_dp5"].is_boolean())
        throw std::runtime_error("rotate_dp5 must be boolean");
    for (const auto* section : {"roles", "key_colors", "lane_classes", "bgm", "timing"}) {
        if (!document.contains(section)) continue;
        if (!document[section].is_object()) throw std::runtime_error("expected color-role object");
        validate_colors(document, document[section]);
    }
    for (const auto* section : {"key_colors", "timing"})
        if (document.contains(section)) validate_entries(document[section]);
    if (document.contains("lane_classes")) validate_lane_classes(document["lane_classes"]);
    if (document.contains("numbered_layout")) {
        validate_numbered_layout(document["numbered_layout"]);
        validate_colors(document, document["numbered_layout"]["colors"]);
    }
    if (document.contains("note_accents")) {
        if (!document["note_accents"].is_array() || document["note_accents"].size() != 8)
            throw std::runtime_error("expected eight note accents");
        validate_colors(document, document["note_accents"]);
    }
}
auto parse_palette(const nlohmann::json& document, field_palette_t base) -> field_palette_t {
    compose_colors = base.roles;
    key_colors = base.keys;
    note_accents = base.accents;
    read_roles(document, compose_names, compose_colors);
    read_key_colors(document);
    read_lane_classes(document);
    read_note_accents(document);
    base.roles = compose_colors;
    base.keys = key_colors;
    base.accents = note_accents;
    base.document = document;
    base.scratch_sides = parse_scratch_sides(document);
    base.rotate_dp5 = document.value("rotate_dp5", false);
    base.numbered_layout = document.value("numbered_layout", nlohmann::json{});
    read_key_entry(document, document.value("bgm", nlohmann::json{}), base.bgm);
    const auto timing = document.value("timing", nlohmann::json::object());
    for (std::size_t i = 0; i < timing_names.size(); ++i)
        read_key_entry(document, timing.value(std::string(timing_names[i]), nlohmann::json{}),
                       base.timing[i]);
    return base;
}
auto create_default_palette() -> field_palette_t {
    compose_colors = builtin_roles;
    key_colors = builtin_keys;
    note_accents = builtin_accents;
    field_palette_t result;
    for (std::size_t i = 0; i < result.timing.size(); ++i)
        result.timing[i] = {true,
                            i % 2 ? nk_rgb(25, 29, 38) : nk_rgb(28, 32, 42),
                            builtin_accents[i],
                            nk_rgb(55, 64, 79),
                            nk_rgb(18, 22, 31),
                            i % 2 ? nk_rgb(25, 29, 38) : nk_rgb(28, 32, 42),
                            builtin_accents[i],
                            {},
                            {}};
    return result;
}
void load_field_schemes(const std::filesystem::path& directory) {
    palettes.clear();
    schemes.clear();
    warned_ids.clear();
    auto fallback = create_default_palette();
    std::vector<std::filesystem::path> paths;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
         it.increment(error))
        if (it->path().extension() == ".json")
            paths.push_back(it->path());
    std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
        if ((a.stem() == "wormedit") != (b.stem() == "wormedit"))
            return a.stem() == "wormedit";
        return a < b;
    });
    if (!std::filesystem::exists(directory / "wormedit.json", error))
        std::cerr << "wormedit: missing field scheme " << (directory / "wormedit.json").string()
                  << "; using built-in Wormedit\n";
    error.clear();
    palettes.emplace("wormedit", fallback);
    schemes.push_back({"wormedit", "Wormedit"});
    for (const auto& path : paths) {
        try {
            std::ifstream input(path);
            const auto document = nlohmann::json::parse(input);
            validate_scheme(document);
            const auto id = path.stem().string();
            auto palette = parse_palette(document, fallback);
            palettes.insert_or_assign(id, std::move(palette));
            if (id == "wormedit")
                fallback = palettes.at(id);
            else
                schemes.push_back({id, document["name"].get<std::string>()});
        } catch (const std::exception& error) {
            std::cerr << "wormedit: invalid field scheme " << path.string() << " (" << error.what()
                      << "); using Wormedit for it\n";
            warned_ids.insert(path.stem().string());
        }
    }
    if (error)
        std::cerr << "wormedit: cannot read field schemes " << directory.string()
                  << "; using Wormedit\n";
}

} // namespace

void load(const std::filesystem::path& bundled_directory) {
    const char* override_directory = std::getenv("WORMEDIT_THEME_DIR");
    const auto directory = override_directory && *override_directory
                               ? std::filesystem::path(override_directory)
                               : bundled_directory;
    interface_directory = directory;
    interface_catalog.clear();
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        const auto id = entry.path().stem().string();
        if (!id.starts_with("ui-") || entry.path().extension() != ".json") continue;
        const auto document = read_document(entry.path());
        const auto name = document.contains("name") && document["name"].is_string()
            ? document["name"].get<std::string>() : id;
        interface_catalog.push_back({id, name});
    }
    std::ranges::sort(interface_catalog, {}, &interface_theme_t::id);
    static_cast<void>(reload_interface_theme(""));
    load_field_schemes(directory / "field");
    active_id.clear();
    select_field_scheme("wormedit");
}

auto interface_themes() -> std::span<const interface_theme_t> { return interface_catalog; }

auto reload_interface_theme(std::string_view id) -> std::string {
    if (id.empty()) id = "ui-dark";
    if (std::ranges::find(interface_catalog, id, &interface_theme_t::id) == interface_catalog.end())
        return "Interface theme is unavailable; the previous theme is retained.";
    const auto path = interface_directory / (std::string{id} + ".json");
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > 1024 * 1024) return "Interface theme could not be read; the previous theme is retained.";
    const auto document = read_document(path);
    if (!document.contains("roles") || !document["roles"].is_object())
        return "Invalid interface theme roles; the previous theme is retained.";
    auto colors = builtin_ui_colors;
    const auto& roles = document["roles"];
    for (std::size_t index = 0; index < ui_names.size(); ++index) {
        const auto name = std::string{ui_names[index]};
        if (roles.contains(name) && !resolve(document, roles[name], colors[index]))
            return "Invalid interface theme color; the previous theme is retained.";
    }
    ui_colors = colors;
    return {};
}

auto ui(UiColor color) -> nk_color {
    return ui_colors[static_cast<size_t>(color)];
}
auto compose(ComposeColor color) -> nk_color {
    return compose_colors[static_cast<size_t>(color)];
}
auto compose_key_color(std::uint8_t key_color, std::uint64_t lane_id) -> key_color_t {
    const auto found = layout_colors.find(lane_id);
    if (lane_id && found != layout_colors.end() &&
        wormedit::field_skin_key_class(static_cast<wormedit::lane_key_color_t>(key_color)) ==
            wormedit::field_skin_key_class(static_cast<wormedit::lane_key_color_t>(found->second.first)))
        return found->second.second;
    if (key_color >= key_colors.size())
        return key_colors[0];
    return key_colors[key_color];
}
void bind_field_layout(std::span<const field_lane_t> lanes) {
    if (std::ranges::equal(lanes, bound_layout))
        return;
    bound_layout.assign(lanes.begin(), lanes.end());
    layout_colors.clear();
    if (active.numbered_layout.is_null()) return;
    const auto groups = numbered_stages(lanes);
    if (!numbered_matches(active.numbered_layout, groups)) return;
    for (std::size_t stage = 0; stage < groups.size(); ++stage) {
        for (std::size_t key = 0; key < groups[stage].size(); ++key) {
            const auto& lane = *groups[stage][key];
            auto color = key_colors[lane.key_color < key_colors.size() ? lane.key_color : 0];
            read_key_entry(active.document, active.numbered_layout["colors"][stage][key], color);
            layout_colors.insert_or_assign(lane.lane_id, std::pair{lane.key_color, color});
        }
    }
}

auto compose_bgm_color() -> key_color_t {
    return active.bgm;
}
auto compose_timing_color(std::size_t kind) -> key_color_t {
    return active.timing[kind % active.timing.size()];
}
auto field_schemes() -> const std::vector<field_scheme_t>& {
    return schemes;
}
auto field_scheme_id() -> std::string_view {
    return active_id;
}
auto field_scheme_supports_layout(std::string_view id, std::span<const field_lane_t> lanes) -> bool {
    const auto found = palettes.find(id.empty() ? std::string("wormedit") : std::string(id));
    return found != palettes.end() && numbered_matches(found->second.numbered_layout, numbered_stages(lanes));
}

auto select_field_scheme_for_layout(std::string_view requested, std::string_view fallback,
                                    std::span<const field_lane_t> lanes) -> bool {
    const bool compatible = !requested.empty() && field_scheme_supports_layout(requested, lanes);
    const auto selected = compatible ? requested : field_scheme_supports_layout(fallback, lanes)
        ? fallback : std::string_view("wormedit");
    select_field_scheme(selected);
    bind_field_layout(lanes);
    return compatible;
}

auto is_dp5(std::span<const field_lane_t> lanes) -> bool {
    const auto groups = numbered_stages(lanes);
    if (groups.size() != 2) return false;
    for (const auto& group : groups) {
        if (group.size() != 6 || std::ranges::count_if(group, [](const auto* lane) {
                return lane->role == 1 || (lane->role == 0 && lane->key_color == 3);
            }) != 1) return false;
    }
    return true;
}

auto field_scratch_side(std::uint8_t stage, std::span<const field_lane_t> lanes) -> std::optional<bool> {
    if (stage > 2) return std::nullopt;
    if (active.rotate_dp5 && is_dp5(lanes)) return false;
    return active.scratch_sides[stage == 2 ? 1 : 0];
}
auto field_scratch_side(std::uint8_t stage) -> std::optional<bool> {
    return field_scratch_side(stage, {});
}
auto field_generation() -> std::uint64_t {
    return generation;
}
void set_snap_colored_grid(bool enabled) {
    if (colored_grid == enabled) return;
    colored_grid = enabled;
    ++generation;
}
auto snap_colored_grid() -> bool { return colored_grid; }
void set_snap_grid_opacity(std::uint32_t percent) {
    percent = std::min(percent, std::uint32_t{100});
    if (grid_opacity_percent == percent) return;
    grid_opacity_percent = percent;
    ++generation;
}
void set_field_waveform_opacity(std::uint32_t percent) {
    percent = std::min(percent, std::uint32_t{100});
    if (waveform_opacity_percent == percent) return;
    waveform_opacity_percent = percent;
    ++generation;
}
auto field_waveform_color(nk_color color) -> nk_color {
    return chrome::with_alpha(color, chrome::percent_to_alpha(waveform_opacity_percent, color.a));
}
auto compose_grid_line_style(std::uint8_t kind, std::uint8_t emphasis, std::uint8_t snap_class)
    -> grid_line_style_t {
    if (colored_grid) {
        const auto rgb = wormedit::ui::arrow_snap_color(snap_class);
        const auto thickness = kind == 3 ? 2.0f : snap_class == 4 ? 1.6f : 1.0f;
        return {nk_rgba(rgb.r, rgb.g, rgb.b,
            chrome::percent_to_alpha(grid_opacity_percent)), thickness};
    }
    if (kind == 3) return {compose(ComposeColor::measure_line), 1.2f};
    if (kind == 1 || kind == 2)
        return {compose(ComposeColor::beat_line), emphasis >= 2 ? 1.1f : 0.9f};
    return {compose(emphasis >= 2 ? ComposeColor::subgrid_emphasis : ComposeColor::subgrid_line), 0.7f};
}

auto field_uses_positions() -> bool {
    return !active.numbered_layout.is_null();
}
auto select_field_scheme(std::string_view id) -> bool {
    const auto requested = id.empty() ? std::string("wormedit") : std::string(id);
    const auto found = palettes.find(requested);
    const bool valid = found != palettes.end();
    if (!valid && warned_ids.insert(requested).second)
        std::cerr << "wormedit: field scheme " << requested << " unavailable; using Wormedit\n";
    const auto selected = valid ? requested : std::string("wormedit");
    if (active_id == selected) return valid;
    active = palettes.contains(selected) ? palettes.at(selected) : create_default_palette();
    active_id = selected;
    compose_colors = active.roles;
    key_colors = active.keys;
    note_accents = active.accents;
    layout_colors.clear();
    bound_layout.clear();
    ++generation;
    return valid;
}
auto compose_note_accent(size_t index) -> nk_color {
    return note_accents[index % note_accents.size()];
}

} // namespace wormedit::ui::theme

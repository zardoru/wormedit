#include "ui/nuklear/theme.h"

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
    "selected_text", "accent",  "status",        "conflict"};
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
    "arrow_selection"};
// Indexed by wormedit::lane_key_color_t; the names match its enumerators.
constexpr std::array<std::string_view, 11> key_names{
    "generic",   "white",      "black",     "scratch",  "foot_pedal", "free_zone",
    "pms_white", "pms_yellow", "pms_green", "pms_blue", "pms_red"};

std::array<nk_color, ui_names.size()> ui_colors{
    nk_rgb(18, 19, 26),    nk_rgb(30, 31, 40),    nk_rgb(44, 46, 60),    nk_rgb(58, 60, 75),
    nk_rgb(58, 60, 75),    nk_rgb(134, 180, 255), nk_rgb(206, 210, 223), nk_rgb(245, 246, 250),
    nk_rgb(173, 179, 197), nk_rgb(20, 92, 227),   nk_rgb(255, 255, 255), nk_rgb(201, 211, 94),
    nk_rgb(30, 31, 40),    nk_rgb(248, 116, 89)};
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
    nk_rgba(113, 201, 216, 255), nk_rgb(220, 247, 255)};
// Built-in Wormedit fallback remains complete if the default asset is missing.
auto builtin_key_colors() -> std::array<key_color_t, key_names.size()> {
    std::array<key_color_t, key_names.size()> colors{};
    colors[0] = {false,
                 nk_rgba(33, 40, 53, 255),
                 nk_rgba(133, 182, 222, 255),
                 nk_rgba(58, 90, 120, 255),
                 nk_rgba(133, 182, 222, 153),
                 nk_rgba(33, 40, 53, 255),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[1] = {true,
                 nk_rgba(18, 19, 26, 255),
                 nk_rgba(226, 232, 240, 255),
                 nk_rgba(51, 65, 85, 255),
                 nk_rgba(226, 232, 240, 153),
                 nk_rgba(18, 19, 26, 255),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[2] = {true,
                 nk_rgba(23, 37, 84, 255),
                 nk_rgba(59, 130, 246, 255),
                 nk_rgba(30, 64, 175, 255),
                 nk_rgba(59, 130, 246, 153),
                 nk_rgba(23, 37, 84, 255),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[3] = {true,
                 nk_rgba(69, 10, 10, 255),
                 nk_rgba(248, 113, 113, 255),
                 nk_rgba(185, 28, 28, 255),
                 nk_rgba(248, 113, 113, 153),
                 nk_rgba(69, 10, 10, 255),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[4] = {true,
                 nk_rgba(8, 51, 68, 255),
                 nk_rgba(34, 211, 238, 255),
                 nk_rgba(14, 116, 144, 255),
                 nk_rgba(34, 211, 238, 153),
                 nk_rgba(8, 51, 68, 255),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[5] = {true,
                 nk_rgba(69, 26, 3, 255),
                 nk_rgba(251, 191, 36, 255),
                 nk_rgba(180, 83, 9, 255),
                 nk_rgba(251, 191, 36, 153),
                 nk_rgba(69, 26, 3, 255),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[6] = {true,
                 nk_rgba(18, 19, 26, 255),
                 nk_rgba(233, 235, 242, 255),
                 nk_rgba(51, 65, 85, 255),
                 nk_rgba(233, 235, 242, 153),
                 nk_rgba(18, 19, 26, 255),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[7] = {true,
                 nk_rgba(66, 32, 6, 255),
                 nk_rgba(250, 204, 21, 255),
                 nk_rgba(161, 98, 7, 255),
                 nk_rgba(250, 204, 21, 153),
                 nk_rgba(66, 32, 6, 255),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[8] = {true,
                 nk_rgba(5, 46, 22, 255),
                 nk_rgba(74, 222, 128, 255),
                 nk_rgba(21, 128, 61, 255),
                 nk_rgba(74, 222, 128, 153),
                 nk_rgba(5, 46, 22, 255),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[9] = {true,
                 nk_rgba(23, 37, 84, 255),
                 nk_rgba(96, 165, 250, 255),
                 nk_rgba(29, 78, 216, 255),
                 nk_rgba(96, 165, 250, 153),
                 nk_rgba(23, 37, 84, 255),
                 nk_rgba(240, 244, 250, 255),
                 {},
                 {},
                 {}};
    colors[10] = {true,
                  nk_rgba(69, 10, 10, 255),
                  nk_rgba(248, 113, 113, 255),
                  nk_rgba(185, 28, 28, 255),
                  nk_rgba(248, 113, 113, 153),
                  nk_rgba(69, 10, 10, 255),
                  nk_rgba(240, 244, 250, 255),
                  {},
                  {},
                  {}};
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
    std::map<std::string, nlohmann::json> positions;
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
std::unordered_map<std::uint32_t, key_color_t> layout_colors;
std::vector<field_lane_t> bound_layout;
auto default_palette() -> field_palette_t;
field_palette_t active = default_palette();
std::string active_id = "wormedit";
std::set<std::string> warned_ids;
std::uint64_t generation{};
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
void validate_positions(const nlohmann::json& players) {
    for (const auto& [player, positions] : players.items()) {
        if (player.size() < 2 || player.back() != 'p' ||
            !std::all_of(player.begin(), player.end() - 1,
                         [](char c) { return c >= '0' && c <= '9'; }))
            throw std::runtime_error("invalid player/stage key");
        if (!positions.is_object())
            throw std::runtime_error("expected position-role object");
        for (const auto& [position, entry] : positions.items()) {
            if (position != "scratch" &&
                (position.empty() || position.front() == '0' ||
                 !std::all_of(position.begin(), position.end(),
                              [](char c) { return c >= '0' && c <= '9'; })))
                throw std::runtime_error("invalid key position");
            if (!entry.is_object())
                throw std::runtime_error("expected lane-role object");
        }
    }
}
void validate_scheme(const nlohmann::json& document) {
    if (!document.is_object() || document.value("version", 0) != 2 || !document.contains("name") ||
        !document["name"].is_string() || document["name"].get<std::string>().empty())
        throw std::runtime_error("expected schema version 2 and a name");
    for (const auto* section : {"roles", "key_colors", "keys", "bgm", "timing"}) {
        if (!document.contains(section))
            continue;
        if (!document[section].is_object())
            throw std::runtime_error("expected color-role object");
        validate_colors(document, document[section]);
    }
    for (const auto* section : {"key_colors", "timing"})
        if (document.contains(section))
            validate_entries(document[section]);
    if (document.contains("keys"))
        validate_positions(document["keys"]);
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
    read_note_accents(document);
    base.roles = compose_colors;
    base.keys = key_colors;
    base.accents = note_accents;
    base.document = document;
    read_key_entry(document, document.value("bgm", nlohmann::json{}), base.bgm);
    const auto timing = document.value("timing", nlohmann::json::object());
    for (std::size_t i = 0; i < timing_names.size(); ++i)
        read_key_entry(document, timing.value(std::string(timing_names[i]), nlohmann::json{}),
                       base.timing[i]);
    const auto keys = document.value("keys", nlohmann::json::object());
    for (const auto& [player, positions] : keys.items()) {
        if (!positions.is_object())
            throw std::runtime_error("expected position-role objects");
        for (const auto& [position, entry] : positions.items())
            base.positions[player + "/" + position] = entry;
    }
    return base;
}
auto default_palette() -> field_palette_t {
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
    auto fallback = default_palette();
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
    read_roles(read_document(directory / "ui-dark.json"), ui_names, ui_colors);
    load_field_schemes(directory / "field");
    select_field_scheme("wormedit");
}

auto ui(UiColor color) -> nk_color {
    return ui_colors[static_cast<size_t>(color)];
}
auto compose(ComposeColor color) -> nk_color {
    return compose_colors[static_cast<size_t>(color)];
}
auto compose_key_color(std::uint8_t key_color, std::uint32_t display_lane_number) -> key_color_t {
    const auto found = layout_colors.find(display_lane_number);
    if (display_lane_number && found != layout_colors.end())
        return found->second;
    if (key_color >= key_colors.size())
        return key_colors[0];
    return key_colors[key_color];
}
void bind_field_layout(std::span<const field_lane_t> lanes) {
    if (std::ranges::equal(lanes, bound_layout))
        return;
    bound_layout.assign(lanes.begin(), lanes.end());
    layout_colors.clear();
    if (active.positions.empty())
        return;
    std::map<std::uint32_t, field_lane_t> ordered;
    for (const auto& lane : lanes)
        ordered.emplace(lane.display_order, lane);
    std::map<std::uint8_t, std::size_t> positions;
    for (const auto& [order, lane] : ordered) {
        const bool scratch = lane.role == 1 || lane.key_color == 3;
        const auto position = scratch ? "scratch" : std::to_string(++positions[lane.stage]);
        const auto key = std::to_string(lane.stage + 1) + "p/" + position;
        const auto found = active.positions.find(key);
        if (found == active.positions.end())
            continue;
        auto color = key_colors[lane.key_color < key_colors.size() ? lane.key_color : 0];
        read_key_entry(active.document, found->second, color);
        layout_colors.emplace(order + 1, color);
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
auto field_generation() -> std::uint64_t {
    return generation;
}
auto field_uses_positions() -> bool {
    return !active.positions.empty();
}
auto select_field_scheme(std::string_view id) -> bool {
    const auto requested = id.empty() ? std::string("wormedit") : std::string(id);
    const auto found = palettes.find(requested);
    const bool valid = found != palettes.end();
    if (!valid && warned_ids.insert(requested).second)
        std::cerr << "wormedit: field scheme " << requested << " unavailable; using Wormedit\n";
    const auto selected = valid ? requested : std::string("wormedit");
    active = palettes.contains(selected) ? palettes.at(selected) : default_palette();
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

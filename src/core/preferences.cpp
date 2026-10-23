#include "text_util.h"
#include <wormedit/core/preferences.h>
#include <wormedit/audio/playback_rate.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <limits>
#include <stdexcept>

namespace wormedit {
namespace {
using json_t = nlohmann::json;

auto is_valid_string(std::string_view value) -> bool {
    return text_util::is_valid_bounded_text(value, maximum_preference_string_bytes);
}
auto is_supported_rate(rational_t rate) -> bool {
    return std::ranges::find(audio::playback_rates, rate) != audio::playback_rates.end();
}

struct preference_reader_t {
    preferences_parse_effect_t& result;
    bool unknown{};
    void warn(std::string message) {
        if (result.warnings.size() < 128) result.warnings.push_back(std::move(message));
    }
    void check_keys(const json_t& object, std::initializer_list<std::string_view> keys) {
        for (const auto& item : object.items())
            if (std::ranges::find(keys, item.key()) == keys.end()) unknown = true;
    }
    auto section(const json_t& root, std::string_view key) -> const json_t& {
        static const json_t empty = json_t::object();
        const auto found = root.find(std::string{key});
        if (found == root.end()) return empty;
        if (!found->is_object()) { warn(std::string{key} + " must be an object; defaults retained."); return empty; }
        return *found;
    }
    void boolean(const json_t& object, const char* key, bool& value) {
        const auto found = object.find(key);
        if (found == object.end()) return;
        if (!found->is_boolean()) { warn(std::string{key} + " must be boolean; default retained."); return; }
        value = found->get<bool>();
    }
    template<class Integer>
    void integer(const json_t& object, const char* key, Integer& value,
                 std::uint64_t minimum, std::uint64_t maximum) {
        const auto found = object.find(key);
        if (found == object.end()) return;
        if (!found->is_number_integer() ||
            (!found->is_number_unsigned() && found->get<std::int64_t>() < 0)) {
            warn(std::string{key} + " must be a bounded integer; default retained."); return;
        }
        const auto candidate = found->get<std::uint64_t>();
        if (candidate < minimum || candidate > maximum) {
            warn(std::string{key} + " is out of range; default retained."); return;
        }
        value = static_cast<Integer>(candidate);
    }
    void string(const json_t& object, const char* key, std::string& value) {
        const auto found = object.find(key);
        if (found == object.end()) return;
        if (!found->is_string() || !is_valid_string(found->get_ref<const std::string&>())) {
            warn(std::string{key} + " must be a bounded string without NUL; default retained."); return;
        }
        value = found->get<std::string>();
    }
    void signed_integer(const json_t& object, const char* key, std::int32_t& value,
                        std::int32_t minimum, std::int32_t maximum) {
        const auto found = object.find(key);
        if (found == object.end()) return;
        if (!found->is_number_integer() ||
            (found->is_number_unsigned() && found->get<std::uint64_t>() > static_cast<std::uint64_t>(maximum))) {
            warn(std::string{key} + " must be a bounded integer; default retained."); return;
        }
        const auto candidate = found->get<std::int64_t>();
        if (candidate < minimum || candidate > maximum) {
            warn(std::string{key} + " is out of range; default retained."); return;
        }
        value = static_cast<std::int32_t>(candidate);
    }
};

void read_general(preference_reader_t& reader, const json_t& root) {
    const auto& section = reader.section(root, "general");
    auto& value = reader.result.preferences.general;
    reader.check_keys(section, {"autosave_minutes", "recent_files_count", "confirm_unsaved_close", "shortcut_profile_path"});
    reader.integer(section, "autosave_minutes", value.autosave_minutes, 1, 30);
    reader.integer(section, "recent_files_count", value.recent_files_count, 1, 100);
    reader.boolean(section, "confirm_unsaved_close", value.confirm_unsaved_close);
    reader.string(section, "shortcut_profile_path", value.shortcut_profile_path);
    if (value.shortcut_profile_path.empty()) {
        value.shortcut_profile_path = "shortcuts.json";
        reader.warn("shortcut_profile_path cannot be empty; default retained.");
    }
}

constexpr std::array<const char*, editor_mode_family_count> scroll_keys{
    "beatmania", "popn", "dance", "pump", "keyboard", "other"};
void read_editor(preference_reader_t& reader, const json_t& root) {
    const auto& section = reader.section(root, "editor");
    auto& value = reader.result.preferences.editor;
    reader.check_keys(section, {"default_snap", "scroll_directions", "playhead_anchor_percent",
        "note_kind_palette_visible", "lane_key_legend_visible", "area_band_alpha", "audition_on_click", "audition_on_playback", "move_collision_policy", "invert_scroll_wheel"});
    std::string policy{"reject"};
    reader.string(section, "move_collision_policy", policy);
    if (policy == "displace_to_bgm") value.move_collision_policy = move_collision_policy_t::displace_to_bgm;
    else if (policy != "reject") reader.warn("Unknown move_collision_policy; Reject retained.");
    reader.integer(section, "default_snap", value.default_snap, 1, 1920);
    reader.integer(section, "playhead_anchor_percent", value.playhead_anchor_percent, 0, 100);
    reader.integer(section, "area_band_alpha", value.area_band_alpha, 0, 255);
    reader.boolean(section, "note_kind_palette_visible", value.note_kind_palette_visible);
    reader.boolean(section, "lane_key_legend_visible", value.lane_key_legend_visible);
    reader.boolean(section, "audition_on_click", value.audition_on_click);
    reader.boolean(section, "audition_on_playback", value.audition_on_playback);
    reader.boolean(section, "invert_scroll_wheel", value.invert_scroll_wheel);
    const auto& scroll = reader.section(section, "scroll_directions");
    reader.check_keys(scroll, {"beatmania", "popn", "dance", "pump", "keyboard", "other"});
    for (std::size_t index = 0; index < scroll_keys.size(); ++index)
        reader.integer(scroll, scroll_keys[index], value.scroll_directions[index], 0, 2);
}
auto is_valid_editor(const editor_preferences_t& value) -> bool {
    return static_cast<std::uint32_t>(value.move_collision_policy) <= 1 && value.default_snap >= 1 && value.default_snap <= 1920 &&
        value.playhead_anchor_percent <= 100 && value.area_band_alpha <= 255 &&
        std::ranges::all_of(value.scroll_directions, [](auto direction) { return direction <= 2; });
}
auto editor_json(const editor_preferences_t& value) -> json_t {
    auto scroll = json_t::object();
    for (std::size_t index = 0; index < scroll_keys.size(); ++index)
        scroll[scroll_keys[index]] = value.scroll_directions[index];
    return {{"move_collision_policy", value.move_collision_policy == move_collision_policy_t::reject ? "reject" : "displace_to_bgm"},
        {"default_snap", value.default_snap}, {"scroll_directions", std::move(scroll)},
        {"playhead_anchor_percent", value.playhead_anchor_percent},
        {"note_kind_palette_visible", value.note_kind_palette_visible},
        {"lane_key_legend_visible", value.lane_key_legend_visible}, {"area_band_alpha", value.area_band_alpha},
        {"audition_on_click", value.audition_on_click}, {"audition_on_playback", value.audition_on_playback},
        {"invert_scroll_wheel", value.invert_scroll_wheel}};
}
auto read_lane_bindings(const json_t& value) -> std::optional<lane_key_bindings_t> {
    if (!value.is_array() || value.size() > maximum_lane_key_bindings) return {};
    lane_key_bindings_t bindings;
    for (const auto& key : value) {
        if (!key.is_number_unsigned() || key.get<std::uint64_t>() > 0xffff) return {};
        bindings.keys.push_back(key.get<std::uint16_t>());
    }
    return bindings.is_valid() ? std::optional{std::move(bindings)} : std::nullopt;
}
void read_lane_keys(preference_reader_t& reader, const json_t& root) {
    const auto& section = reader.section(root, "lane_keys");
    if (section.size() > maximum_lane_key_layouts) { reader.warn("Lane keys exceed the 128-layout limit; ignored."); return; }
    for (const auto& [id, value] : section.items()) {
        if (id.empty() || !is_valid_string(id) || !value.is_array() || value.size() > maximum_lane_key_bindings) {
            reader.warn("Invalid lane-key layout ignored."); continue;
        }
        auto bindings = read_lane_bindings(value);
        if (bindings) reader.result.preferences.lane_keys.layouts.emplace(id, std::move(*bindings));
        else reader.warn("Invalid physical lane keys ignored.");
    }
}
void read_default_lane_keys(preference_reader_t& reader, const json_t& root) {
    const auto found = root.find("default_lane_keys");
    if (found == root.end()) return;
    if (!found->is_array() || found->size() > maximum_default_lane_count) {
        reader.warn("Invalid default lane-key preferences ignored."); return;
    }
    default_lane_keys_t candidate;
    for (const auto& item : *found) {
        if (!item.is_object() || !item.contains("count") || !item["count"].is_number_unsigned() ||
            item["count"].get<std::uint64_t>() > maximum_default_lane_count || !item.contains("keys")) {
            reader.warn("Invalid default lane-key preferences ignored."); return;
        }
        auto bindings = read_lane_bindings(item["keys"]);
        if (!bindings || !candidate.bindings.emplace(item["count"].get<std::uint32_t>(), std::move(*bindings)).second) {
            reader.warn("Invalid default lane-key preferences ignored."); return;
        }
    }
    if (candidate.is_valid()) reader.result.preferences.default_lane_keys = std::move(candidate);
    else reader.warn("Conflicting or invalid default lane keys ignored.");
}
auto default_lane_keys_json(const default_lane_keys_t& value) -> json_t {
    auto result = json_t::array();
    for (const auto& [count, keys] : value.bindings)
        result.push_back({{"count", count}, {"keys", keys.keys}});
    return result;
}
auto lane_keys_json(const lane_key_preferences_t& value) -> json_t {
    auto result = json_t::object();
    for (const auto& [id, bindings] : value.layouts) result[id] = bindings.keys;
    return result;
}
void read_keysound_budget(preference_reader_t& reader, const json_t& object) {
    const auto found = object.find("keysound_budget_mib");
    if (found == object.end()) return;
    if (!found->is_number_integer()) {
        reader.warn("keysound_budget_mib must be an integer; default retained.");
        return;
    }
    const auto candidate = found->is_number_unsigned()
        ? found->get<std::uint64_t>()
        : static_cast<std::uint64_t>(std::max<std::int64_t>(found->get<std::int64_t>(), 0));
    reader.result.preferences.audio.keysound_budget_mib =
        static_cast<std::uint32_t>(std::clamp<std::uint64_t>(candidate, 256, 8192));
}

void read_editor_audio(preference_reader_t& reader, const json_t& root) {
    read_editor(reader, root);
    auto& values = reader.result.preferences;
    const auto& audio = reader.section(root, "audio");
    reader.check_keys(audio, {"playback_rate", "keep_pitch", "keysound_budget_mib", "osu_skin_directory",
        "output_device", "buffer_frames", "metronome_volume", "accent_volume", "count_in_measures",
        "audition_volume", "preview_latency_ms"});
    reader.string(audio, "output_device", values.audio.output_device);
    reader.integer(audio, "buffer_frames", values.audio.buffer_frames, 0, 8192);
    reader.integer(audio, "metronome_volume", values.audio.metronome_volume, 0, 100);
    reader.integer(audio, "accent_volume", values.audio.accent_volume, 0, 100);
    reader.integer(audio, "count_in_measures", values.audio.count_in_measures, 0, 4);
    reader.integer(audio, "audition_volume", values.audio.audition_volume, 0, 100);
    reader.signed_integer(audio, "preview_latency_ms", values.audio.preview_latency_ms, -1000, 1000);
    reader.string(audio, "osu_skin_directory", values.audio.osu_skin_directory);
    read_keysound_budget(reader, audio);
    reader.boolean(audio, "keep_pitch", values.audio.keep_pitch);
    const auto& rate = reader.section(audio, "playback_rate");
    reader.check_keys(rate, {"numerator", "denominator"});
    reader.integer(rate, "numerator", values.audio.playback_rate.numerator, 1, std::numeric_limits<std::uint64_t>::max());
    reader.integer(rate, "denominator", values.audio.playback_rate.denominator, 1, std::numeric_limits<std::uint64_t>::max());
    if (!is_supported_rate(values.audio.playback_rate)) {
        values.audio.playback_rate = {1, 1};
        reader.warn("playback_rate is unsupported; default retained.");
    }
}

void read_scratch_positions(preference_reader_t& reader, const json_t& display) {
    const auto found = display.find("scratch_positions");
    if (found == display.end()) return;
    if (!found->is_array() || found->size() != 2) {
        reader.warn("scratch_positions must contain Single/P1 and P2 settings; defaults retained.");
        return;
    }
    for (std::size_t index = 0; index < 2; ++index) {
        const auto& side = (*found)[index];
        if (!side.is_number_unsigned() || side.get<std::uint64_t>() > 3) {
            reader.warn("scratch position must be Skin default, Chart layout, Left, or Right; default retained.");
            continue;
        }
        reader.result.preferences.display.scratch_positions[index] = side.get<std::uint32_t>();
    }
}
void read_field_scheme_overrides(preference_reader_t& reader, const json_t& display) {
    const auto found = display.find("field_scheme_overrides");
    if (found == display.end()) return;
    if (!found->is_object() || found->size() > maximum_field_scheme_overrides) {
        reader.warn("field_scheme_overrides must be an object with at most 128 layouts; ignored.");
        return;
    }
    auto& overrides = reader.result.preferences.display.field_scheme_overrides;
    for (const auto& [key, value] : found->items()) {
        if (key.empty() || !is_valid_string(key) || !value.is_string()) {
            reader.warn("Invalid layout skin override ignored.");
            continue;
        }
        const auto& scheme = value.get_ref<const std::string&>();
        if (scheme.empty() || !is_valid_string(scheme)) {
            reader.warn("Invalid or inherited layout skin override ignored.");
            continue;
        }
        overrides.emplace(key, scheme);
    }
}
void read_display_minimap_layout(preference_reader_t& reader, const json_t& root) {
    auto& values = reader.result.preferences;
    const auto& display = reader.section(root, "display");
    reader.check_keys(display, {"scratch_positions", "field_scheme", "field_scheme_overrides", "theme", "ui_scale_percent", "frame_timing_overlay", "reduced_motion", "snap_colored_grid", "snap_grid_opacity_percent", "field_waveform_opacity_percent"});
    reader.string(display, "field_scheme", values.display.field_scheme);
    read_field_scheme_overrides(reader, display);
    read_scratch_positions(reader, display);
    reader.string(display, "theme", values.display.theme);
    reader.integer(display, "ui_scale_percent", values.display.ui_scale_percent, 0, 200);
    reader.boolean(display, "frame_timing_overlay", values.display.frame_timing_overlay);
    reader.boolean(display, "reduced_motion", values.display.reduced_motion);
    reader.boolean(display, "snap_colored_grid", values.display.snap_colored_grid);
    reader.integer(display, "snap_grid_opacity_percent", values.display.snap_grid_opacity_percent, 0, 100);
    reader.integer(display, "field_waveform_opacity_percent", values.display.field_waveform_opacity_percent, 0, 100);
    if (values.display.ui_scale_percent != 0 && values.display.ui_scale_percent < 100) {
        values.display.ui_scale_percent = 0;
        reader.warn("ui_scale_percent must be automatic or 100..200; default retained.");
    }
    const auto& minimap = reader.section(root, "minimap");
    reader.check_keys(minimap, {"click_moves_paused_playhead"});
    reader.boolean(minimap, "click_moves_paused_playhead", values.minimap.click_moves_paused_playhead);
    const auto& layout = reader.section(root, "layout");
    reader.check_keys(layout, {"dock_layout", "compose_dock"});
    const auto dock = layout.find("compose_dock");
    if (dock != layout.end()) {
        const auto parsed = ui_layout_preferences_t::from(dock->dump());
        if (parsed) values.layout.dock = *parsed;
        else reader.warn(parsed.error().message);
    } else if (const auto legacy = layout.find("dock_layout"); legacy != layout.end() && *legacy != "") {
        reader.warn("Legacy dock layout placeholder ignored; defaults retained.");
    }
}

auto read_player(preference_reader_t& reader, const json_t& item, std::size_t index)
    -> external_player_definition_t {
    const auto defaults = external_player_preferences_t::with_defaults();
    auto value = index < defaults.players.size() ? defaults.players[index]
        : external_player_definition_t{"New player", "", "\"<filename>\"", "-s <measure> \"<filename>\"", "-t"};
    if (!item.is_object()) { reader.warn("Invalid player entry; defaults retained."); return value; }
    reader.check_keys(item, {"name", "path", "start_arguments", "here_arguments", "stop_arguments"});
    reader.string(item, "name", value.name); reader.string(item, "path", value.path);
    reader.string(item, "start_arguments", value.start_arguments);
    reader.string(item, "here_arguments", value.here_arguments);
    reader.string(item, "stop_arguments", value.stop_arguments);
    return value;
}

void read_players(preference_reader_t& reader, const json_t& root) {
    const auto& section = reader.section(root, "players");
    reader.check_keys(section, {"selected", "definitions"});
    auto& value = reader.result.preferences.players;
    const auto definitions = section.find("definitions");
    if (definitions != section.end()) {
        if (!definitions->is_array() || definitions->empty() || definitions->size() > maximum_external_players)
            reader.warn("Player collection must contain 1..64 entries; defaults retained.");
        else {
            value.players.clear();
            for (const auto& item : *definitions) value.players.push_back(read_player(reader, item, value.players.size()));
        }
    }
    reader.integer(section, "selected", value.selected, 0, value.players.size() - 1);
}

auto is_valid_preferences(const program_preferences_t& value) -> bool {
    return value.general.autosave_minutes >= 1 && value.general.autosave_minutes <= 30 &&
        value.general.recent_files_count >= 1 && value.general.recent_files_count <= 100 &&
        is_valid_string(value.general.shortcut_profile_path) && !value.general.shortcut_profile_path.empty() &&
        is_valid_editor(value.editor) &&
        value.audio.is_valid() &&
        is_valid_string(value.display.theme) && is_valid_string(value.display.field_scheme) && is_valid_field_scheme_overrides(value.display) &&
        std::ranges::all_of(value.display.scratch_positions, [](auto side) { return side <= 3; }) &&
        value.display.snap_grid_opacity_percent <= 100 && value.display.field_waveform_opacity_percent <= 100 &&
        (value.display.ui_scale_percent == 0 ||
            (value.display.ui_scale_percent >= 100 && value.display.ui_scale_percent <= 200)) &&
        is_valid_external_player_preferences(value.players);
}

auto preferences_json(const program_preferences_t& value) -> json_t {
    auto players = json_t::array();
    for (const auto& player : value.players.players) players.push_back({
        {"name", player.name}, {"path", player.path}, {"start_arguments", player.start_arguments},
        {"here_arguments", player.here_arguments}, {"stop_arguments", player.stop_arguments}});
    return {{"version", 1},
        {"general", {{"autosave_minutes", value.general.autosave_minutes},
            {"recent_files_count", value.general.recent_files_count},
            {"confirm_unsaved_close", value.general.confirm_unsaved_close},
            {"shortcut_profile_path", value.general.shortcut_profile_path}}},
        {"editor", editor_json(value.editor)},
        {"lane_keys", lane_keys_json(value.lane_keys)},
        {"default_lane_keys", default_lane_keys_json(value.default_lane_keys)},
        {"audio", {{"playback_rate", {{"numerator", value.audio.playback_rate.numerator},
            {"denominator", value.audio.playback_rate.denominator}}}, {"keep_pitch", value.audio.keep_pitch},
            {"keysound_budget_mib", value.audio.keysound_budget_mib},
            {"output_device", value.audio.output_device}, {"buffer_frames", value.audio.buffer_frames},
            {"metronome_volume", value.audio.metronome_volume}, {"accent_volume", value.audio.accent_volume},
            {"count_in_measures", value.audio.count_in_measures}, {"audition_volume", value.audio.audition_volume},
            {"preview_latency_ms", value.audio.preview_latency_ms},
            {"osu_skin_directory", value.audio.osu_skin_directory}}},
        {"display", {{"scratch_positions", value.display.scratch_positions}, {"field_scheme", value.display.field_scheme}, {"field_scheme_overrides", value.display.field_scheme_overrides}, {"theme", value.display.theme}, {"ui_scale_percent", value.display.ui_scale_percent},
            {"frame_timing_overlay", value.display.frame_timing_overlay}, {"reduced_motion", value.display.reduced_motion},
            {"snap_colored_grid", value.display.snap_colored_grid},
            {"snap_grid_opacity_percent", value.display.snap_grid_opacity_percent},
            {"field_waveform_opacity_percent", value.display.field_waveform_opacity_percent}}},
        {"players", {{"selected", value.players.selected}, {"definitions", std::move(players)}}},
        {"minimap", {{"click_moves_paused_playhead", value.minimap.click_moves_paused_playhead}}},
        {"layout", {{"compose_dock", json_t::parse(value.layout.dock.serialized())}}}};
}
} // namespace

auto external_player_preferences_t::with_defaults() -> external_player_preferences_t {
    return {.players = {
        {"mBMplay", "", "\"<filename>\"", "-s <measure> \"<filename>\"", "-t"},
        {"uBMplay", "", "-P -N0 \"<filename>\"", "-P -N<measure> \"<filename>\"", "-S"},
        {"o2play", "", "-P -N0 \"<filename>\"", "-P -N<measure> \"<filename>\"", "-S"}}};
}
auto resolve_field_scheme(const display_preferences_t& display, std::string_view layout_key)
    -> std::string_view {
    const auto found = display.field_scheme_overrides.find(layout_key);
    if (!layout_key.empty() && found != display.field_scheme_overrides.end() && !found->second.empty())
        return found->second;
    return display.field_scheme.empty() ? std::string_view{"wormedit"} : std::string_view{display.field_scheme};
}

auto is_valid_field_scheme_overrides(const display_preferences_t& display) -> bool {
    if (display.field_scheme_overrides.size() > maximum_field_scheme_overrides) return false;
    return std::ranges::all_of(display.field_scheme_overrides, [](const auto& entry) {
        return !entry.first.empty() && is_valid_string(entry.first) &&
               !entry.second.empty() && is_valid_string(entry.second);
    });
}

auto audio_preferences_t::is_valid() const -> bool {
    return is_supported_rate(playback_rate) && keysound_budget_mib >= 256 && keysound_budget_mib <= 8192 &&
        is_valid_string(osu_skin_directory) && is_valid_string(output_device) && buffer_frames <= 8192 &&
        metronome_volume <= 100 && accent_volume <= 100 && count_in_measures <= 4 && audition_volume <= 100 &&
        preview_latency_ms >= -1000 && preview_latency_ms <= 1000;
}
auto program_preferences_t::with_defaults() -> program_preferences_t { return {}; }
auto is_valid_external_player_preferences(const external_player_preferences_t& value) -> bool {
    if (value.players.empty() || value.players.size() > maximum_external_players || value.selected >= value.players.size())
        return false;
    return std::ranges::all_of(value.players, [](const auto& player) {
        return is_valid_string(player.name) && is_valid_string(player.path) && is_valid_string(player.start_arguments) &&
            is_valid_string(player.here_arguments) && is_valid_string(player.stop_arguments);
    });
}

namespace {
struct preference_json_budget_t {
    std::size_t nodes{};
    auto operator()(int depth, json_t::parse_event_t, json_t&) -> bool {
        if (depth > static_cast<int>(maximum_preferences_depth) || ++nodes > 65536)
            throw std::runtime_error("Preferences exceed nesting or node limits.");
        return true;
    }
};
} // namespace

auto preferences_parse_effect_t::from(std::string_view bytes) -> preferences_parse_effect_t {
    preferences_parse_effect_t result{.succeeded = false, .preferences = program_preferences_t::with_defaults()};
    if (bytes.size() > maximum_preferences_bytes) {
        result.warnings.push_back("Preferences exceed the 1 MiB limit; defaults retained."); return result;
    }
    try {
        preference_json_budget_t budget;
        const auto root = json_t::parse(bytes.begin(), bytes.end(), std::ref(budget));
        if (!root.is_object()) throw std::runtime_error("Preferences root must be an object.");
        const auto version = root.find("version");
        if (version != root.end() && (!version->is_number_integer() || *version != 1))
            throw std::runtime_error("Unsupported preferences version.");
        preference_reader_t reader{result};
        reader.check_keys(root, {"version", "general", "editor", "audio", "display", "players", "minimap", "layout", "lane_keys", "default_lane_keys"});
        read_lane_keys(reader, root);
        read_default_lane_keys(reader, root);
        read_general(reader, root); read_editor_audio(reader, root);
        read_display_minimap_layout(reader, root); read_players(reader, root);
        if (reader.unknown) reader.warn("Unknown preference keys were ignored.");
        result.succeeded = true;
        return result;
    } catch (const std::exception&) {
        result.preferences = program_preferences_t::with_defaults();
        result.warnings = {"Preferences are corrupt, unsupported, or exceed limits; defaults retained."};
    }
    return result;
}

auto serialize_program_preferences(const program_preferences_t& value) -> std::optional<std::string> {
    if (!value.lane_keys.is_valid() || !value.default_lane_keys.is_valid()) return std::nullopt;
    if (!is_valid_preferences(value)) return std::nullopt;
    try {
        auto bytes = preferences_json(value).dump(2);
        if (bytes.size() > maximum_preferences_bytes) return std::nullopt;
        return bytes;
    } catch (const std::exception&) { return std::nullopt; }
}

auto load_program_preferences(const std::filesystem::path& path) -> preferences_load_result_t {
    preferences_load_result_t result;
    auto& contents = result.contents;
    std::error_code error;
    const auto status = std::filesystem::status(path, error);
    if (error == std::errc::no_such_file_or_directory || status.type() == std::filesystem::file_type::not_found) {
        result.missing = true;
        return result;
    }
    if (error || !std::filesystem::is_regular_file(status)) {
        contents.warnings = {"Preferences are not a readable regular file; defaults retained."};
        return result;
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum_preferences_bytes) {
        contents.warnings = {"Preferences are unreadable or exceed the 1 MiB limit; defaults retained."};
        return result;
    }
    std::ifstream stream(path, std::ios::binary);
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!stream || (!bytes.empty() && !stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))) {
        contents.warnings = {"Preferences could not be read completely; defaults retained."};
        return result;
    }
    char extra{};
    if (stream.get(extra) || !stream.eof()) {
        contents.warnings = {"Preferences changed while being read; defaults retained."};
        return result;
    }
    result.contents = preferences_parse_effect_t::from(bytes);
    return result;
}

auto save_program_preferences_atomic(const std::filesystem::path& path,
    const program_preferences_t& value) -> atomic_file_save_outcome_t {
    const auto bytes = serialize_program_preferences(value);
    if (!bytes) return std::unexpected{"Preferences contain invalid or oversized values."};
    return create_file_atomic(path, *bytes, maximum_preferences_bytes);
}
auto load_legacy_external_player_preferences(const std::filesystem::path& path,
    external_player_preferences_t& preferences) -> std::string {
    std::error_code error;
    if (!std::filesystem::exists(path, error))
        return error ? "Cannot inspect external-player preferences." : std::string{};
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum_external_players * maximum_preference_string_bytes * 6)
        return "External-player preferences are unreadable or too large.";
    std::ifstream input(path);
    std::string magic;
    std::size_t count{};
    external_player_preferences_t candidate;
    if (!(input >> magic >> candidate.selected >> count) || magic != "wormedit-external-players-v1" ||
        count == 0 || count > maximum_external_players) return "Invalid external-player preferences header.";
    candidate.players.resize(count);
    for (auto& player : candidate.players) {
        if (!(input >> std::quoted(player.name) >> std::quoted(player.path) >>
              std::quoted(player.start_arguments) >> std::quoted(player.here_arguments) >>
              std::quoted(player.stop_arguments))) return "Invalid external-player preferences entry.";
    }
    input >> std::ws;
    if (!input.eof() || !is_valid_external_player_preferences(candidate)) return "Invalid external-player preferences.";
    preferences = std::move(candidate);
    return {};
}

auto load_program_preferences_for_startup(const std::filesystem::path& path,
    const std::filesystem::path& legacy_player_path) -> preferences_load_result_t {
    auto result = load_program_preferences(path);
    if (!result.missing) return result;
    auto& contents = result.contents;
    std::error_code error;
    const bool legacy_exists = std::filesystem::exists(legacy_player_path, error);
    if (error) contents.warnings.push_back("Cannot inspect legacy external-player preferences.");
    if (legacy_exists && !error) {
        const auto failure = load_legacy_external_player_preferences(legacy_player_path, contents.preferences.players);
        if (!failure.empty()) contents.warnings.push_back(failure);
        else result.migrated = true;
    }
    const auto saved = save_program_preferences_atomic(path, contents.preferences);
    if (!saved) contents.warnings.push_back("Preferences could not be saved: " + saved.error());
    return result;
}

} // namespace wormedit

#include "sys/shortcut_rows.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string_view>
#include <utility>

namespace wormedit::sys {
namespace {

constexpr std::uint16_t first_letter_usage = 0x04;
constexpr std::uint16_t last_letter_usage = 0x1d;
constexpr std::uint16_t first_digit_usage = 0x1e;
constexpr std::uint16_t last_digit_usage = 0x26;
constexpr std::uint16_t digit_zero_usage = 0x27;
constexpr std::uint16_t first_function_usage = 0x3a;
constexpr std::uint16_t last_function_usage = 0x45;
constexpr std::size_t max_chords_shown = 8;

auto key_name(std::uint16_t usage) -> std::string {
    if (usage >= first_letter_usage && usage <= last_letter_usage)
        return std::string(1, static_cast<char>('A' + (usage - first_letter_usage)));
    if (usage >= first_digit_usage && usage <= last_digit_usage)
        return std::string(1, static_cast<char>('1' + (usage - first_digit_usage)));
    if (usage == digit_zero_usage) return "0";
    if (usage >= first_function_usage && usage <= last_function_usage)
        return "F" + std::to_string(usage - first_function_usage + 1);
    switch (static_cast<shortcut_key_t>(usage)) {
    case shortcut_key_t::enter: return "Enter";
    case shortcut_key_t::escape: return "Escape";
    case shortcut_key_t::backspace: return "Backspace";
    case shortcut_key_t::space: return "Space";
    case shortcut_key_t::delete_key: return "Delete";
    case shortcut_key_t::right: return "Right";
    case shortcut_key_t::left: return "Left";
    case shortcut_key_t::down: return "Down";
    case shortcut_key_t::up: return "Up";
    default: return {};
    }
}

auto chord_list_text(const std::vector<shortcut_chord_t>& chords) -> std::string {
    if (chords.empty()) return "Unbound";
    std::string text;
    for (std::size_t index = 0; index < chords.size() && index < max_chords_shown; ++index) {
        if (index != 0) text += ", ";
        text += shortcut_chord_text(chords[index]);
    }
    return text;
}

auto conflicting_ids(const command_registry_t& registry, shortcut_primary_modifier_t primary)
    -> std::set<std::string> {
    std::set<std::string> ids;
    for (const auto& conflict : find_shortcut_conflicts(registry, primary)) {
        ids.insert(conflict.first.value);
        ids.insert(conflict.second.value);
    }
    return ids;
}

} // namespace

void project_shortcut_rows(const command_registry_t& registry,
                           shortcut_primary_modifier_t primary, shortcut_rows_projection_t& out) {
    const auto conflicts = conflicting_ids(registry, primary);
    const auto count = std::min(registry.commands.size(),
                                static_cast<std::size_t>(WORM_UI_SHORTCUT_ROW_CAPACITY));
    out.commands.clear();
    out.labels.clear();
    out.chords.clear();
    out.rows.clear();
    for (std::size_t index = 0; index < count; ++index) {
        const auto& command = registry.commands[index];
        out.commands.push_back(command.id);
        out.labels.push_back(command.display_name.empty() ? command.id.value : command.display_name);
        out.chords.push_back(chord_list_text(command.current_shortcuts));
    }
    for (std::size_t index = 0; index < count; ++index) {
        out.rows.push_back({.label = out.labels[index].c_str(), .chord = out.chords[index].c_str(),
                            .conflict = conflicts.contains(out.commands[index].value)});
    }
}

auto shortcut_chord_text(const shortcut_chord_t& chord) -> std::string {
    const auto key = key_name(static_cast<std::uint16_t>(chord.key));
    if (key.empty()) return {};
    constexpr std::array<std::pair<shortcut_modifier_t, std::string_view>, 5> names{{
        {shortcut_modifier_t::control, "Ctrl"}, {shortcut_modifier_t::shift, "Shift"},
        {shortcut_modifier_t::alt, "Alt"}, {shortcut_modifier_t::meta, "Meta"},
        {shortcut_modifier_t::primary, "Primary"}}};
    std::string text;
    for (const auto& [modifier, name] : names) {
        if ((chord.modifiers & static_cast<shortcut_modifiers_t>(modifier)) == 0) continue;
        text += std::string(name) + "+";
    }
    return text + key;
}

auto capture_shortcut_batch(std::span<const WormUiShortcutEvent> events) -> shortcut_capture_t {
    for (const auto& event : events) {
        if (event.repeat || event.tap_timing_space || key_name(event.hid_usage).empty()) continue;
        if (event.hid_usage == static_cast<std::uint16_t>(shortcut_key_t::escape) && event.modifiers == 0)
            return {.cancel = true, .chord = std::nullopt};
        return {.cancel = false,
                .chord = shortcut_chord_t{.key = static_cast<shortcut_key_t>(event.hid_usage),
                                          .modifiers = event.modifiers}};
    }
    return {};
}

} // namespace wormedit::sys

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
constexpr std::uint16_t first_keypad_digit_usage = 0x59;
constexpr std::uint16_t last_keypad_digit_usage = 0x61;
constexpr std::size_t max_chords_shown = 8;

// Names follow the usual English keyboard legends. Usages come from the USB HID Keyboard
// page, so punctuation and keypad keys are named here even though shortcut_key_t has no
// enumerator for them.
auto key_name(std::uint16_t usage) -> std::string {
    if (usage >= first_letter_usage && usage <= last_letter_usage)
        return std::string(1, static_cast<char>('A' + (usage - first_letter_usage)));
    if (usage >= first_digit_usage && usage <= last_digit_usage)
        return std::string(1, static_cast<char>('1' + (usage - first_digit_usage)));
    if (usage == digit_zero_usage) return "0";
    if (usage >= first_function_usage && usage <= last_function_usage)
        return "F" + std::to_string(usage - first_function_usage + 1);
    if (usage >= first_keypad_digit_usage && usage <= last_keypad_digit_usage)
        return "Keypad " + std::to_string(usage - first_keypad_digit_usage + 1);
    switch (usage) {
    case 0x28: return "Enter";
    case 0x29: return "Escape";
    case 0x2a: return "Backspace";
    case 0x2b: return "Tab";
    case 0x2c: return "Space";
    case 0x2d: return "-";
    case 0x2e: return "=";
    case 0x2f: return "[";
    case 0x30: return "]";
    case 0x31: return "\\";
    case 0x33: return ";";
    case 0x34: return "'";
    case 0x35: return "`";
    case 0x36: return ",";
    case 0x37: return ".";
    case 0x38: return "/";
    case 0x49: return "Insert";
    case 0x4a: return "Home";
    case 0x4b: return "PageUp";
    case 0x4c: return "Delete";
    case 0x4d: return "End";
    case 0x4e: return "PageDown";
    case 0x4f: return "Right";
    case 0x50: return "Left";
    case 0x51: return "Down";
    case 0x52: return "Up";
    case 0x54: return "Keypad /";
    case 0x55: return "Keypad *";
    case 0x56: return "Keypad -";
    case 0x57: return "Keypad +";
    case 0x58: return "Keypad Enter";
    case 0x62: return "Keypad 0";
    case 0x63: return "Keypad .";
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

#include <wormedit/core/command_registry.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <ranges>
#include <set>
#include <string>
#include <utility>

namespace wormedit {
namespace {

using json = nlohmann::json;

constexpr shortcut_modifiers_t known_modifiers =
    static_cast<shortcut_modifiers_t>(shortcut_modifier_t::shift) |
    static_cast<shortcut_modifiers_t>(shortcut_modifier_t::control) |
    static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt) |
    static_cast<shortcut_modifiers_t>(shortcut_modifier_t::meta) |
    static_cast<shortcut_modifiers_t>(shortcut_modifier_t::primary);
constexpr shortcut_modifiers_t actual_modifiers =
    static_cast<shortcut_modifiers_t>(shortcut_modifier_t::shift) |
    static_cast<shortcut_modifiers_t>(shortcut_modifier_t::control) |
    static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt) |
    static_cast<shortcut_modifiers_t>(shortcut_modifier_t::meta);

auto error(diagnostic_code_t code, std::string message) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error, .code = code, .message = std::move(message)};
}

auto warning(diagnostic_code_t code, std::string message) -> diagnostic_t {
    return {
        .severity = diagnostic_severity_t::warning, .code = code, .message = std::move(message)};
}

[[nodiscard]] auto has_errors(const diagnostic_list_t& diagnostics) -> bool {
    return std::ranges::any_of(diagnostics, [](const diagnostic_t& item) {
        return item.severity == diagnostic_severity_t::error;
    });
}

[[nodiscard]] auto valid_stable_text(std::string_view value, std::size_t maximum,
                                     bool allow_empty = false) -> bool {
    if (value.size() > maximum || (!allow_empty && value.empty())) {
        return false;
    }
    return std::ranges::all_of(value, [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
               ch == '_' || ch == '-' || ch == '.';
    });
}

[[nodiscard]] auto valid_chord(const shortcut_chord_t& chord, bool event = false) -> bool {
    const auto key = static_cast<std::uint16_t>(chord.key);
    const auto modifier_keys_begin = std::uint16_t{0xe0};
    const auto modifier_keys_end = std::uint16_t{0xe7};
    const auto valid_key =
        key >= 0x04 && key <= 0xe7 && !(key >= modifier_keys_begin && key <= modifier_keys_end);
    const auto valid_modifier_bits =
        (chord.modifiers & static_cast<shortcut_modifiers_t>(~known_modifiers)) == 0;
    const auto event_has_only_real_modifiers =
        !event || (chord.modifiers & static_cast<shortcut_modifiers_t>(~actual_modifiers)) == 0;
    return valid_key && valid_modifier_bits && event_has_only_real_modifiers;
}

[[nodiscard]] auto valid_chord_list(const std::vector<shortcut_chord_t>& chords) -> bool {
    if (chords.size() > maximum_shortcuts_per_command) {
        return false;
    }
    for (std::size_t i = 0; i < chords.size(); ++i) {
        if (!valid_chord(chords[i])) {
            return false;
        }
        if (std::ranges::find(chords.begin(), chords.begin() + static_cast<std::ptrdiff_t>(i),
                              chords[i]) != chords.begin() + static_cast<std::ptrdiff_t>(i)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto valid_registration(const command_registration_t& registration)
    -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    if (!valid_stable_text(registration.id.value, maximum_command_id_bytes)) {
        diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "command ID must be bounded stable ASCII text"));
    }
    if (registration.display_name.empty() ||
        registration.display_name.size() > maximum_command_display_name_bytes) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                    "command display name is empty or exceeds its byte limit"));
    }
    if (!valid_stable_text(registration.localization_key, maximum_localization_key_bytes)) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                    "command localization key must be bounded stable ASCII text"));
    }
    if (registration.focus_context.size() > maximum_focus_context_bytes) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                    "command focus context exceeds its byte limit"));
    }
    if (!valid_chord_list(registration.default_shortcuts) ||
        (registration.current_shortcuts && !valid_chord_list(*registration.current_shortcuts))) {
        diagnostics.push_back(
            error(diagnostic_code_t::invalid_binding,
                  "command shortcuts are invalid, duplicated, or exceed the cap"));
    }
    if (static_cast<unsigned int>(registration.category) >
            static_cast<unsigned int>(command_category_t::tools) ||
        static_cast<unsigned int>(registration.focus_scope) >
            static_cast<unsigned int>(command_focus_scope_t::ime) ||
        static_cast<unsigned int>(registration.repeat_policy) >
            static_cast<unsigned int>(command_repeat_policy_t::allow)) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                    "command metadata contains an unknown enum value"));
        return diagnostics;
    }

    const auto scope = registration.focus_scope;
    if ((scope == command_focus_scope_t::global || scope == command_focus_scope_t::modal ||
         scope == command_focus_scope_t::text_entry || scope == command_focus_scope_t::ime) &&
        (registration.tab || registration.lane)) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                    "global and focus-only commands cannot target a tab or lane"));
    }
    if ((scope == command_focus_scope_t::tab || scope == command_focus_scope_t::workspace) &&
        registration.lane) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                    "tab and workspace commands cannot target a lane"));
    }
    if ((scope == command_focus_scope_t::modal || scope == command_focus_scope_t::text_entry ||
         scope == command_focus_scope_t::ime) &&
        (registration.lane || registration.tab)) {
        diagnostics.push_back(
            error(diagnostic_code_t::invalid_binding,
                  "modal, text, and IME commands cannot target a chart tab or lane"));
    }
    return diagnostics;
}

[[nodiscard]] auto normalized_modifiers(shortcut_modifiers_t modifiers,
                                        shortcut_primary_modifier_t primary_modifier)
    -> shortcut_modifiers_t {
    if ((modifiers & static_cast<shortcut_modifiers_t>(shortcut_modifier_t::primary)) != 0) {
        modifiers &= static_cast<shortcut_modifiers_t>(
            ~static_cast<shortcut_modifiers_t>(shortcut_modifier_t::primary));
        modifiers |= static_cast<shortcut_modifiers_t>(
            primary_modifier == shortcut_primary_modifier_t::control ? shortcut_modifier_t::control
                                                                     : shortcut_modifier_t::meta);
    }
    return modifiers;
}

[[nodiscard]] auto chords_match(const shortcut_chord_t& binding, const shortcut_chord_t& event,
                                shortcut_primary_modifier_t primary_modifier) -> bool {
    return binding.key == event.key &&
           normalized_modifiers(binding.modifiers, primary_modifier) == event.modifiers;
}

[[nodiscard]] auto scope_rank(command_focus_scope_t scope) -> int {
    switch (scope) {
    case command_focus_scope_t::global:
        return 0;
    case command_focus_scope_t::workspace:
        return 1;
    case command_focus_scope_t::tab:
        return 2;
    case command_focus_scope_t::lane:
        return 3;
    case command_focus_scope_t::modal:
    case command_focus_scope_t::text_entry:
    case command_focus_scope_t::ime:
        return 0;
    }
    return 4;
}

[[nodiscard]] auto is_visible(const command_focus_t& focus, workspace_tab_t tab) -> bool {
    return std::ranges::find(focus.visible_tabs, tab) != focus.visible_tabs.end();
}

[[nodiscard]] auto eligible(const command_t& command, const command_focus_t& focus) -> bool {
    if (focus.ime_composition_active) {
        return command.focus_scope == command_focus_scope_t::ime &&
               (command.focus_context.empty() || command.focus_context == focus.ime_context);
    }
    if (focus.modal_active) {
        return command.focus_scope == command_focus_scope_t::modal &&
               (command.focus_context.empty() || command.focus_context == focus.modal_context);
    }
    if (focus.text_entry_active) {
        return command.focus_scope == command_focus_scope_t::text_entry &&
               (command.focus_context.empty() || command.focus_context == focus.text_entry_context);
    }

    switch (command.focus_scope) {
    case command_focus_scope_t::global:
        return true;
    case command_focus_scope_t::workspace:
        if (!focus.workspace_active) {
            return false;
        }
        // Tab commands on the workspace bar target a destination tab. They run
        // only when that destination is actually visible.
        return !command.tab || is_visible(focus, *command.tab);
    case command_focus_scope_t::tab:
        return focus.workspace_active && focus.active_tab && is_visible(focus, *focus.active_tab) &&
               (!command.tab || *command.tab == *focus.active_tab);
    case command_focus_scope_t::lane:
        return focus.workspace_active && focus.active_tab && is_visible(focus, *focus.active_tab) &&
               focus.focused_lane && (!command.tab || *command.tab == *focus.active_tab) &&
               (!command.lane || *command.lane == *focus.focused_lane);
    case command_focus_scope_t::modal:
    case command_focus_scope_t::text_entry:
    case command_focus_scope_t::ime:
        return false;
    }
    return false;
}

[[nodiscard]] auto has_binding(const command_t& command, const shortcut_chord_t& chord,
                               shortcut_primary_modifier_t primary_modifier) -> bool {
    return std::ranges::any_of(command.current_shortcuts, [&](const shortcut_chord_t& binding) {
        return chords_match(binding, chord, primary_modifier);
    });
}

[[nodiscard]] auto restricted_focus_scope(const command_focus_t& focus)
    -> std::optional<command_focus_scope_t> {
    if (focus.ime_composition_active) {
        return command_focus_scope_t::ime;
    }
    if (focus.modal_active) {
        return command_focus_scope_t::modal;
    }
    if (focus.text_entry_active) {
        return command_focus_scope_t::text_entry;
    }
    return std::nullopt;
}

[[nodiscard]] auto scopes_overlap(const command_t& left, const command_t& right) -> bool {
    const auto restricted = [](command_focus_scope_t scope) {
        return scope == command_focus_scope_t::modal ||
               scope == command_focus_scope_t::text_entry || scope == command_focus_scope_t::ime;
    };
    if (restricted(left.focus_scope) || restricted(right.focus_scope)) {
        return left.focus_scope == right.focus_scope &&
               (left.focus_context.empty() || right.focus_context.empty() ||
                left.focus_context == right.focus_context);
    }

    if (left.focus_scope == command_focus_scope_t::global ||
        right.focus_scope == command_focus_scope_t::global) {
        return true;
    }
    if (left.focus_scope == command_focus_scope_t::workspace ||
        right.focus_scope == command_focus_scope_t::workspace) {
        return true;
    }
    if (left.tab && right.tab && left.tab != right.tab) {
        return false;
    }
    if (left.focus_scope == command_focus_scope_t::lane &&
        right.focus_scope == command_focus_scope_t::lane && left.lane && right.lane &&
        left.lane != right.lane) {
        return false;
    }
    return true;
}

[[nodiscard]] auto profiles_overlap(const command_registry_t& registry,
                                    shortcut_primary_modifier_t primary_modifier)
    -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    for (std::size_t left_index = 0; left_index < registry.commands.size(); ++left_index) {
        const auto& left = registry.commands[left_index];
        for (std::size_t right_index = left_index + 1; right_index < registry.commands.size();
             ++right_index) {
            const auto& right = registry.commands[right_index];
            if (!scopes_overlap(left, right)) {
                continue;
            }
            const auto collides = std::ranges::any_of(
                left.current_shortcuts, [&](const shortcut_chord_t& left_chord) {
                    return std::ranges::any_of(
                        right.current_shortcuts, [&](const shortcut_chord_t& right_chord) {
                            return left_chord.key == right_chord.key &&
                                   normalized_modifiers(left_chord.modifiers, primary_modifier) ==
                                       normalized_modifiers(right_chord.modifiers,
                                                            primary_modifier);
                        });
                });
            if (collides) {
                diagnostics.push_back(warning(diagnostic_code_t::duplicate_binding,
                                              "shortcut conflict between '" + left.id.value +
                                                  "' and '" + right.id.value + "'"));
            }
        }
    }
    return diagnostics;
}

[[nodiscard]] auto valid_profile(const shortcut_profile_t& profile) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    if (!valid_stable_text(profile.id, maximum_command_id_bytes)) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                    "shortcut profile ID must be bounded stable ASCII text"));
    }
    if (profile.bindings.size() > maximum_shortcut_profile_bindings) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                    "shortcut profile exceeds its binding cap"));
        return diagnostics;
    }
    std::set<command_id_t> ids;
    for (const auto& binding : profile.bindings) {
        if (!valid_stable_text(binding.command.value, maximum_command_id_bytes)) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                        "shortcut profile contains an invalid command ID"));
        }
        if (!ids.insert(binding.command).second) {
            diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                                        "shortcut profile contains duplicate command bindings"));
        }
        if (!valid_chord_list(binding.shortcuts)) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                        "shortcut profile contains invalid or excessive chords"));
        }
    }
    return diagnostics;
}

auto add_builtin(command_registry_t& registry, command_invoker_t invoker, std::string id,
                 std::string display, command_category_t category, command_focus_scope_t scope,
                 std::vector<shortcut_chord_t> shortcuts = {}, bool mutation = false,
                 std::optional<workspace_tab_t> tab = std::nullopt,
                 std::optional<bool> checked = std::nullopt) -> void {
    command_registration_t registration{
        .id = {std::move(id)},
        .display_name = std::move(display),
        .localization_key = "command." + id,
        .category = category,
        .focus_scope = scope,
        .tab = tab,
        .default_shortcuts = std::move(shortcuts),
        .enabled = true,
        .checked = checked,
        .mutation = mutation,
    };
    // The ID was moved into the registration. Rebuild the key from its stable
    // value before binding the generic application/controller invoker.
    registration.localization_key = "command." + registration.id.value;
    if (invoker) {
        registration.invoke = [invoker](const command_invocation_context_t& context) {
            return invoker(context);
        };
    }
    (void)register_command(registry, std::move(registration));
}

[[nodiscard]] constexpr auto primary() -> shortcut_modifiers_t {
    return static_cast<shortcut_modifiers_t>(shortcut_modifier_t::primary);
}

[[nodiscard]] constexpr auto modified(shortcut_modifier_t modifier) -> shortcut_modifiers_t {
    return primary() | modifier;
}

[[nodiscard]] constexpr auto chord(shortcut_key_t key, shortcut_modifiers_t modifiers = 0)
    -> shortcut_chord_t {
    return {.key = key, .modifiers = modifiers};
}

} // namespace

auto register_command(command_registry_t& registry, command_registration_t registration)
    -> command_registry_result_t {
    command_registry_result_t result;
    result.diagnostics = valid_registration(registration);
    if (has_errors(result.diagnostics)) {
        return result;
    }
    if (registry.commands.size() >= maximum_registered_commands) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution, "command registry is at its entry cap"));
        return result;
    }
    if (find_command(registry, registration.id)) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::duplicate_id, "command ID is already registered"));
        return result;
    }

    auto current = registration.current_shortcuts.value_or(registration.default_shortcuts);
    registry.commands.push_back({
        .id = std::move(registration.id),
        .display_name = std::move(registration.display_name),
        .localization_key = std::move(registration.localization_key),
        .category = registration.category,
        .focus_scope = registration.focus_scope,
        .tab = registration.tab,
        .lane = registration.lane,
        .focus_context = std::move(registration.focus_context),
        .default_shortcuts = std::move(registration.default_shortcuts),
        .current_shortcuts = std::move(current),
        .enabled = registration.enabled,
        .checked = registration.checked,
        .mutation = registration.mutation,
        .repeat_policy = registration.repeat_policy,
        .invoke = std::move(registration.invoke),
    });
    result.applied = true;
    return result;
}

auto find_command(const command_registry_t& registry, const command_id_t& id) noexcept
    -> const command_t* {
    const auto found = std::ranges::find(registry.commands, id, &command_t::id);
    return found == registry.commands.end() ? nullptr : &*found;
}

auto update_command_state(command_registry_t& registry, const command_id_t& id, bool enabled,
                          std::optional<bool> checked) -> command_registry_result_t {
    command_registry_result_t result;
    const auto found = std::ranges::find(registry.commands, id, &command_t::id);
    if (found == registry.commands.end()) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "cannot update state for an unknown command"));
        return result;
    }
    found->enabled = enabled;
    found->checked = checked;
    result.applied = true;
    return result;
}

auto diagnose_shortcut_conflicts(const command_registry_t& registry,
                                 shortcut_primary_modifier_t primary_modifier)
    -> diagnostic_list_t {
    return profiles_overlap(registry, primary_modifier);
}

auto remap_command(command_registry_t& registry, const command_id_t& id,
                   std::vector<shortcut_chord_t> shortcuts) -> command_registry_result_t {
    command_registry_result_t result;
    if (!valid_chord_list(shortcuts)) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                           "replacement shortcuts are invalid or exceed the cap"));
        return result;
    }
    const auto found = std::ranges::find(registry.commands, id, &command_t::id);
    if (found == registry.commands.end()) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "cannot remap an unknown command"));
        return result;
    }
    found->current_shortcuts = std::move(shortcuts);
    result.applied = true;
    result.diagnostics = diagnose_shortcut_conflicts(registry);
    return result;
}

auto make_shortcut_profile(const command_registry_t& registry, std::string profile_id)
    -> shortcut_profile_t {
    shortcut_profile_t profile{.id = std::move(profile_id)};
    for (const auto& command : registry.commands) {
        if (command.current_shortcuts != command.default_shortcuts) {
            profile.bindings.push_back(
                {.command = command.id, .shortcuts = command.current_shortcuts});
        }
    }
    return profile;
}

auto apply_shortcut_profile(command_registry_t& registry, const shortcut_profile_t& profile)
    -> shortcut_profile_result_t {
    shortcut_profile_result_t result;
    result.diagnostics = valid_profile(profile);
    if (has_errors(result.diagnostics)) {
        return result;
    }
    if (registry.commands.size() > maximum_registered_commands) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                           "command registry exceeds its profile application cap"));
        return result;
    }
    for (const auto& binding : profile.bindings) {
        if (!find_command(registry, binding.command)) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_id, "shortcut profile names an unknown command"));
        }
    }
    if (has_errors(result.diagnostics)) {
        return result;
    }

    auto proposed = registry.commands;
    for (auto& command : proposed) {
        command.current_shortcuts = command.default_shortcuts;
    }
    for (const auto& binding : profile.bindings) {
        const auto command = std::ranges::find(proposed, binding.command, &command_t::id);
        command->current_shortcuts = binding.shortcuts;
    }
    registry.commands = std::move(proposed);
    result.applied = true;
    result.diagnostics = diagnose_shortcut_conflicts(registry);
    return result;
}

auto serialize_shortcut_profile(const shortcut_profile_t& profile) -> std::string {
    if (has_errors(valid_profile(profile))) {
        return {};
    }
    json root = {
        {"format", "wormedit-shortcuts"},
        {"version", 1},
        {"id", profile.id},
        {"bindings", json::array()},
    };
    for (const auto& binding : profile.bindings) {
        json encoded = {{"command", binding.command.value}, {"shortcuts", json::array()}};
        for (const auto& shortcut : binding.shortcuts) {
            encoded["shortcuts"].push_back({{"key", static_cast<std::uint16_t>(shortcut.key)},
                                            {"modifiers", shortcut.modifiers}});
        }
        root["bindings"].push_back(std::move(encoded));
    }
    auto output = root.dump(2);
    if (output.size() > maximum_shortcut_profile_bytes) {
        return {};
    }
    return output;
}

auto parse_shortcut_profile(std::string_view serialized) -> shortcut_profile_parse_result_t {
    shortcut_profile_parse_result_t result;
    if (serialized.empty() || serialized.size() > maximum_shortcut_profile_bytes) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                           "shortcut profile bytes are empty or exceed the cap"));
        return result;
    }
    try {
        const auto root = json::parse(serialized);
        if (!root.is_object() || root.value("format", std::string{}) != "wormedit-shortcuts" ||
            root.value("version", 0) != 1 || !root.contains("id") || !root.at("id").is_string() ||
            !root.contains("bindings") || !root.at("bindings").is_array()) {
            result.diagnostics.push_back(
                error(diagnostic_code_t::invalid_binding,
                      "shortcut profile has an unsupported shape or version"));
            return result;
        }
        const auto& encoded_bindings = root.at("bindings");
        if (encoded_bindings.size() > maximum_shortcut_profile_bindings) {
            result.diagnostics.push_back(error(diagnostic_code_t::invalid_resolution,
                                               "shortcut profile exceeds its binding cap"));
            return result;
        }
        shortcut_profile_t profile{.id = root.at("id").get<std::string>()};
        profile.bindings.reserve(encoded_bindings.size());
        for (const auto& encoded_binding : encoded_bindings) {
            if (!encoded_binding.is_object() || !encoded_binding.contains("command") ||
                !encoded_binding.at("command").is_string() ||
                !encoded_binding.contains("shortcuts") ||
                !encoded_binding.at("shortcuts").is_array() ||
                encoded_binding.at("shortcuts").size() > maximum_shortcuts_per_command) {
                result.diagnostics.push_back(
                    error(diagnostic_code_t::invalid_binding,
                          "shortcut profile binding has an invalid shape"));
                return result;
            }
            shortcut_profile_binding_t binding{
                .command = {encoded_binding.at("command").get<std::string>()}};
            for (const auto& encoded_chord : encoded_binding.at("shortcuts")) {
                if (!encoded_chord.is_object() || !encoded_chord.contains("key") ||
                    !encoded_chord.at("key").is_number_unsigned() ||
                    !encoded_chord.contains("modifiers") ||
                    !encoded_chord.at("modifiers").is_number_unsigned()) {
                    result.diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                                       "shortcut chord has an invalid shape"));
                    return result;
                }
                const auto key = encoded_chord.at("key").get<std::uint32_t>();
                const auto modifiers = encoded_chord.at("modifiers").get<std::uint32_t>();
                if (key > std::numeric_limits<std::uint16_t>::max() ||
                    modifiers > std::numeric_limits<shortcut_modifiers_t>::max()) {
                    result.diagnostics.push_back(
                        error(diagnostic_code_t::invalid_binding,
                              "shortcut chord numeric value is out of range"));
                    return result;
                }
                binding.shortcuts.push_back(
                    {.key = static_cast<shortcut_key_t>(key),
                     .modifiers = static_cast<shortcut_modifiers_t>(modifiers)});
            }
            profile.bindings.push_back(std::move(binding));
        }
        result.diagnostics = valid_profile(profile);
        if (!has_errors(result.diagnostics)) {
            result.profile = std::move(profile);
        }
    } catch (const json::exception&) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                           "shortcut profile is not valid bounded JSON"));
    }
    return result;
}

namespace {

auto shortcut_candidates(const command_registry_t& registry, const shortcut_event_t& event,
                         const command_focus_t& focus, shortcut_primary_modifier_t primary_modifier,
                         std::optional<command_focus_scope_t> restricted)
    -> std::vector<const command_t*> {
    std::vector<const command_t*> candidates;
    candidates.reserve(registry.commands.size());
    for (const auto& command : registry.commands) {
        if (restricted && command.focus_scope != *restricted) {
            continue;
        }
        if (eligible(command, focus) && has_binding(command, event.chord, primary_modifier)) {
            candidates.push_back(&command);
        }
    }
    return candidates;
}

} // namespace

auto dispatch_shortcut(const command_registry_t& registry, const shortcut_event_t& event,
                       const command_focus_t& focus, shortcut_primary_modifier_t primary_modifier)
    -> command_dispatch_result_t {
    command_dispatch_result_t result;
    if (!valid_chord(event.chord, true) ||
        focus.modal_context.size() > maximum_focus_context_bytes ||
        focus.text_entry_context.size() > maximum_focus_context_bytes ||
        focus.ime_context.size() > maximum_focus_context_bytes ||
        registry.commands.size() > maximum_registered_commands) {
        result.status = command_dispatch_status_t::invalid_event;
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_binding,
                  "shortcut event, focus, or registry exceeds its bounds"));
        return result;
    }

    const auto restricted = restricted_focus_scope(focus);
    auto candidates = shortcut_candidates(registry, event, focus, primary_modifier, restricted);
    if (candidates.empty()) {
        result.status = restricted ? command_dispatch_status_t::focus_consumed
                                   : command_dispatch_status_t::no_match;
        return result;
    }

    std::ranges::sort(candidates, [](const command_t* left, const command_t* right) {
        if (scope_rank(left->focus_scope) != scope_rank(right->focus_scope)) {
            return scope_rank(left->focus_scope) < scope_rank(right->focus_scope);
        }
        return left->id < right->id;
    });
    const auto best_rank = scope_rank(candidates.front()->focus_scope);
    const auto peers = static_cast<std::size_t>(
        std::ranges::count_if(candidates, [best_rank](const command_t* command) {
            return scope_rank(command->focus_scope) == best_rank;
        }));
    if (peers > 1) {
        result.status = command_dispatch_status_t::conflict;
        for (const auto* candidate : candidates) {
            if (scope_rank(candidate->focus_scope) == best_rank) {
                result.diagnostics.push_back(
                    warning(diagnostic_code_t::duplicate_binding,
                            "shortcut dispatch is ambiguous at this focus scope: '" +
                                candidate->id.value + "'"));
            }
        }
        return result;
    }

    const auto& command = *candidates.front();
    result.command = command.id;
    if (!command.enabled) {
        result.status = command_dispatch_status_t::disabled;
        return result;
    }
    if (event.auto_repeat && command.mutation &&
        command.repeat_policy == command_repeat_policy_t::suppress_mutations) {
        result.status = command_dispatch_status_t::repeat_suppressed;
        return result;
    }
    if (!command.invoke) {
        result.status = command_dispatch_status_t::rejected;
        result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
                                           "command has no controller invocation handler"));
        return result;
    }

    const command_invocation_context_t context{
        .command = command.id,
        .focus = focus,
        .shortcut = event.chord,
        .auto_repeat = event.auto_repeat,
    };
    const auto invocation = command.invoke(context);
    result.diagnostics = invocation.diagnostics;
    result.status = invocation.accepted && !has_errors(invocation.diagnostics)
                        ? command_dispatch_status_t::invoked
                        : command_dispatch_status_t::rejected;
    return result;
}

auto invoke_command(const command_registry_t& registry, const command_id_t& id,
                    const command_focus_t& focus) -> command_dispatch_result_t {
    command_dispatch_result_t result;
    if (focus.modal_context.size() > maximum_focus_context_bytes ||
        focus.text_entry_context.size() > maximum_focus_context_bytes ||
        focus.ime_context.size() > maximum_focus_context_bytes ||
        registry.commands.size() > maximum_registered_commands) {
        result.status = command_dispatch_status_t::invalid_event;
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                           "command invocation context exceeds its bounds"));
        return result;
    }
    const auto command = find_command(registry, id);
    if (!command) {
        result.status = command_dispatch_status_t::no_match;
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "cannot invoke an unknown command"));
        return result;
    }
    result.command = command->id;
    const auto restricted = restricted_focus_scope(focus);
    if (restricted && command->focus_scope != *restricted) {
        result.status = command_dispatch_status_t::focus_consumed;
        return result;
    }
    if (!eligible(*command, focus)) {
        result.status = command_dispatch_status_t::focus_consumed;
        return result;
    }
    if (!command->enabled) {
        result.status = command_dispatch_status_t::disabled;
        return result;
    }
    if (!command->invoke) {
        result.status = command_dispatch_status_t::rejected;
        result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
                                           "command has no controller invocation handler"));
        return result;
    }
    const command_invocation_context_t context{.command = command->id, .focus = focus};
    const auto invocation = command->invoke(context);
    result.diagnostics = invocation.diagnostics;
    result.status = invocation.accepted && !has_errors(invocation.diagnostics)
                        ? command_dispatch_status_t::invoked
                        : command_dispatch_status_t::rejected;
    return result;
}

auto make_default_editor_command_registry(command_invoker_t invoker) -> command_registry_t {
    command_registry_t registry;
    const auto global = command_focus_scope_t::global;
    const auto workspace = command_focus_scope_t::workspace;
    const auto compose = std::optional{workspace_tab_t::compose};

    add_builtin(registry, invoker, "view.notifications", "Notifications",
                command_category_t::workspace, global,
                {chord(shortcut_key_t::m, modified(shortcut_modifier_t::shift))});
    add_builtin(registry, invoker, "workspace.options", "Options", command_category_t::workspace,
                global, {chord(static_cast<shortcut_key_t>(0x36), primary())});
    add_builtin(registry, invoker, "document.new_project", "New Project",
                command_category_t::document, global, {chord(shortcut_key_t::n, primary())}, true);
    add_builtin(registry, invoker, "document.new_chart", "New Chart", command_category_t::document,
                global, {chord(shortcut_key_t::n, modified(shortcut_modifier_t::shift))}, true);
    add_builtin(registry, invoker, "history.undo", "Undo", command_category_t::edit, global,
                {chord(shortcut_key_t::z, primary())}, true);
    add_builtin(registry, invoker, "history.redo", "Redo", command_category_t::edit, global,
                {chord(shortcut_key_t::z, modified(shortcut_modifier_t::shift))}, true);
    add_builtin(registry, invoker, "edit.copy", "Copy", command_category_t::edit,
                command_focus_scope_t::tab, {chord(shortcut_key_t::c, primary())}, false, compose);
    add_builtin(registry, invoker, "edit.cut", "Cut", command_category_t::edit,
                command_focus_scope_t::tab, {chord(shortcut_key_t::x, primary())}, true, compose);
    add_builtin(registry, invoker, "edit.paste", "Paste", command_category_t::edit,
                command_focus_scope_t::tab, {chord(shortcut_key_t::v, primary())}, false, compose);
    add_builtin(registry, invoker, "edit.delete", "Delete", command_category_t::edit,
                command_focus_scope_t::tab,
                {chord(shortcut_key_t::delete_key), chord(shortcut_key_t::backspace)}, true,
                compose);
    add_builtin(registry, invoker, "selection.clear", "Clear Selection",
                command_category_t::selection, command_focus_scope_t::tab,
                {chord(shortcut_key_t::escape)}, false, compose);
    add_builtin(registry, invoker, "selection.select_all", "Select All",
                command_category_t::selection, command_focus_scope_t::tab,
                {chord(shortcut_key_t::a, primary())}, false, compose);
    add_builtin(registry, invoker, "selection.invert", "Invert Selection",
                command_category_t::selection, command_focus_scope_t::tab,
                {chord(shortcut_key_t::i, modified(shortcut_modifier_t::shift))}, false, compose);
    add_builtin(
        registry, invoker, "navigation.previous_marker", "Previous Bookmark or Section",
        command_category_t::navigation, command_focus_scope_t::tab,
        {chord(shortcut_key_t::left, static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt))},
        false, compose);
    add_builtin(
        registry, invoker, "navigation.next_marker", "Next Bookmark or Section",
        command_category_t::navigation, command_focus_scope_t::tab,
        {chord(shortcut_key_t::right, static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt))},
        false, compose);
    add_builtin(registry, invoker, "navigation.previous_bookmark", "Previous Bookmark",
                command_category_t::navigation, command_focus_scope_t::tab,
                {chord(shortcut_key_t::up, primary())}, false, compose);
    add_builtin(registry, invoker, "navigation.next_bookmark", "Next Bookmark",
                command_category_t::navigation, command_focus_scope_t::tab,
                {chord(shortcut_key_t::down, primary())}, false, compose);
    add_builtin(registry, invoker, "markers.add_bookmark", "Add Bookmark",
                command_category_t::edit, command_focus_scope_t::tab,
                {chord(shortcut_key_t::b, primary())}, true, compose);
    add_builtin(registry, invoker, "navigation.previous_snap", "Previous Snap",
                command_category_t::navigation, command_focus_scope_t::tab,
                {chord(shortcut_key_t::left)}, false, compose);
    add_builtin(registry, invoker, "navigation.next_snap", "Next Snap",
                command_category_t::navigation, command_focus_scope_t::tab,
                {chord(shortcut_key_t::right)}, false, compose);
    add_builtin(registry, invoker, "transport.play_stop", "Play / Stop",
                command_category_t::transport, workspace, {chord(shortcut_key_t::space)}, true);
    add_builtin(registry, invoker, "transport.play_pause", "Play from Cursor / Pause",
                command_category_t::transport, workspace,
                {chord(shortcut_key_t::space, primary())});
    add_builtin(registry, invoker, "transport.play_from_start", "Play from Start",
                command_category_t::transport, workspace,
                {chord(shortcut_key_t::space, modified(shortcut_modifier_t::shift))});
    add_builtin(registry, invoker, "transport.stop", "Stop", command_category_t::transport,
                workspace, {});

    add_builtin(registry, invoker, "transport.toggle_metronome", "Toggle Metronome",
                command_category_t::transport, workspace, {});

    add_builtin(registry, invoker, "transport.external_play_start", "External Play from Start",
                command_category_t::transport, workspace, {});
    add_builtin(registry, invoker, "transport.external_play_here", "External Play from Here",
                command_category_t::transport, workspace, {});
    add_builtin(registry, invoker, "transport.external_stop", "External Stop",
                command_category_t::transport, workspace, {});

    add_builtin(registry, invoker, "transport.rate_slower", "Slower Playback",
                command_category_t::transport, workspace,
                {chord(shortcut_key_t::f11, primary())});
    add_builtin(registry, invoker, "transport.rate_faster", "Faster Playback",
                command_category_t::transport, workspace,
                {chord(shortcut_key_t::f12, primary())});

    add_builtin(registry, invoker, "timing.tap_assistant", "Tap Timing",
                command_category_t::timing, workspace, {}, false, std::nullopt, false);
    add_builtin(registry, invoker, "timing.tap", "Tap Beat",
                command_category_t::timing, workspace, {chord(shortcut_key_t::t)});
    (void)update_command_state(registry, {"timing.tap"}, false, std::nullopt);

    constexpr std::array workspace_tabs{
        workspace_tab_t::setup,
        workspace_tab_t::metadata,
        workspace_tab_t::timing,
        workspace_tab_t::compose,
        workspace_tab_t::storyboard,
        workspace_tab_t::audio_slices,
        workspace_tab_t::compare_validate,
    };
    constexpr std::array tab_names{
        "Setup",        "Metadata",          "Timing", "Compose", "Storyboard",
        "Audio Slices", "Compare / Validate"};
    constexpr std::array tab_ids{"setup",      "metadata",     "timing",          "compose",
                                 "storyboard", "audio_slices", "compare_validate"};
    constexpr std::array function_keys{
        shortcut_key_t::f1, shortcut_key_t::f2, shortcut_key_t::f3, shortcut_key_t::f4,
        shortcut_key_t::f5, shortcut_key_t::f6, shortcut_key_t::f7,
    };
    for (std::size_t i = 0; i < workspace_tabs.size(); ++i) {
        add_builtin(registry, invoker, "workspace.open_" + std::string(tab_ids[i]),
                    "Open " + std::string(tab_names[i]), command_category_t::workspace, workspace,
                    {chord(function_keys[i])}, false, workspace_tabs[i]);
    }

    add_builtin(registry, invoker, "timing.toggle_lock", "Toggle Timing Lock",
                command_category_t::timing, command_focus_scope_t::tab,
                {chord(shortcut_key_t::l, modified(shortcut_modifier_t::shift))}, true,
                workspace_tab_t::timing, false);
    add_builtin(registry, invoker, "timing.insert_bpm", "Insert BPM Change",
                command_category_t::timing, command_focus_scope_t::tab, {}, true,
                workspace_tab_t::compose);
    add_builtin(registry, invoker, "timing.insert_sv", "Insert Scroll Velocity",
                command_category_t::timing, command_focus_scope_t::tab, {}, true,
                workspace_tab_t::compose);
    add_builtin(registry, invoker, "timing.open_settings", "Timing Settings",
                command_category_t::timing, command_focus_scope_t::tab, {}, false,
                workspace_tab_t::timing);

    constexpr std::array note_kind_names{"tap", "hold", "mine", "invisible", "lift", "fake", "roll"};
    constexpr std::array note_kind_keys{shortcut_key_t::digit_1, shortcut_key_t::digit_2,
        shortcut_key_t::digit_3, shortcut_key_t::digit_4, shortcut_key_t::digit_5,
        shortcut_key_t::digit_6, shortcut_key_t::digit_7};
    for (std::size_t i = 0; i < note_kind_names.size(); ++i)
        add_builtin(registry, invoker, "edit.note_kind_" + std::string(note_kind_names[i]),
            "Arm note kind: " + std::string(note_kind_names[i]), command_category_t::edit,
            command_focus_scope_t::tab,
            {chord(note_kind_keys[i], static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt))},
            false, compose);

    constexpr std::array layer_keys{
        shortcut_key_t::digit_1, shortcut_key_t::digit_2, shortcut_key_t::digit_3,
        shortcut_key_t::digit_4, shortcut_key_t::digit_5, shortcut_key_t::digit_6,
        shortcut_key_t::digit_7, shortcut_key_t::digit_8, shortcut_key_t::digit_9,
    };
    for (std::size_t i = 0; i < layer_keys.size(); ++i) {
        const auto slot = std::to_string(i + 1);
        add_builtin(registry, invoker, "semantic_layer.select_slot_" + slot,
                    "Select Semantic Layer Slot " + slot, command_category_t::semantic_layer,
                    command_focus_scope_t::tab, {chord(layer_keys[i], primary())}, false, compose);
    }
    add_builtin(registry, invoker, "semantic_layer.clear_target", "Clear Layer Target",
                command_category_t::semantic_layer, command_focus_scope_t::tab,
                {chord(shortcut_key_t::digit_0, primary())}, false, compose);
    add_builtin(registry, invoker, "semantic_layer.apply", "Apply Layer to Selection",
                command_category_t::semantic_layer, command_focus_scope_t::tab, {}, true, compose);
    add_builtin(registry, invoker, "semantic_layer.remove", "Remove Layer from Selection",
                command_category_t::semantic_layer, command_focus_scope_t::tab, {}, true, compose);
    add_builtin(registry, invoker, "semantic_layer.toggle", "Toggle Layer on Selection",
                command_category_t::semantic_layer, command_focus_scope_t::tab, {}, true, compose);
    add_builtin(registry, invoker, "audio.paint_hitsound", "Paint Hitsound / Keysound",
                command_category_t::audio, command_focus_scope_t::tab, {}, false, compose);

    return registry;
}

} // namespace wormedit

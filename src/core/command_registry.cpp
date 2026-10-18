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

using diagnostic_queries::has_errors;
auto command_id_of(const command_t& command) -> const command_id_t& {
    return command.definition.id;
}

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

auto warning(diagnostic_code_t code, std::string message) -> diagnostic_t {
    return {
        .severity = diagnostic_severity_t::warning, .code = code, .message = std::move(message)};
}

[[nodiscard]] auto is_valid_stable_text(std::string_view value, std::size_t maximum,
                                     bool allow_empty = false) -> bool {
    if (value.size() > maximum || (!allow_empty && value.empty())) {
        return false;
    }
    return std::ranges::all_of(value, [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
               ch == '_' || ch == '-' || ch == '.';
    });
}

[[nodiscard]] auto is_valid_chord(const shortcut_chord_t& chord, bool event = false) -> bool {
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

[[nodiscard]] auto is_valid_chord_list(const std::vector<shortcut_chord_t>& chords) -> bool {
    if (chords.size() > maximum_shortcuts_per_command) {
        return false;
    }
    for (std::size_t i = 0; i < chords.size(); ++i) {
        if (!is_valid_chord(chords[i])) {
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
    if (!is_valid_stable_text(registration.definition.id.value, maximum_command_id_bytes)) {
        diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "command ID must be bounded stable ASCII text"));
    }
    if (registration.definition.display_name.empty() ||
        registration.definition.display_name.size() > maximum_command_display_name_bytes) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                    "command display name is empty or exceeds its byte limit"));
    }
    if (!is_valid_stable_text(registration.definition.localization_key,
                           maximum_localization_key_bytes)) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                    "command localization key must be bounded stable ASCII text"));
    }
    if (registration.definition.focus_context.size() > maximum_focus_context_bytes) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                    "command focus context exceeds its byte limit"));
    }
    if (!is_valid_chord_list(registration.definition.default_shortcuts) ||
        (registration.current_shortcuts && !is_valid_chord_list(*registration.current_shortcuts))) {
        diagnostics.push_back(
            error(diagnostic_code_t::invalid_binding,
                  "command shortcuts are invalid, duplicated, or exceed the cap"));
    }
    if (static_cast<unsigned int>(registration.definition.category) >
            static_cast<unsigned int>(command_category_t::tools) ||
        static_cast<unsigned int>(registration.definition.focus_scope) >
            static_cast<unsigned int>(command_focus_scope_t::ime) ||
        static_cast<unsigned int>(registration.definition.repeat_policy) >
            static_cast<unsigned int>(command_repeat_policy_t::allow)) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                    "command metadata contains an unknown enum value"));
        return diagnostics;
    }

    const auto scope = registration.definition.focus_scope;
    if ((scope == command_focus_scope_t::global || scope == command_focus_scope_t::modal ||
         scope == command_focus_scope_t::text_entry || scope == command_focus_scope_t::ime) &&
        (registration.definition.tab || registration.definition.lane)) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                    "global and focus-only commands cannot target a tab or lane"));
    }
    if ((scope == command_focus_scope_t::tab || scope == command_focus_scope_t::workspace) &&
        registration.definition.lane) {
        diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                    "tab and workspace commands cannot target a lane"));
    }
    if ((scope == command_focus_scope_t::modal || scope == command_focus_scope_t::text_entry ||
         scope == command_focus_scope_t::ime) &&
        (registration.definition.lane || registration.definition.tab)) {
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

[[nodiscard]] auto is_chord_match(const shortcut_chord_t& binding, const shortcut_chord_t& event,
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

[[nodiscard]] auto is_eligible(const command_t& command, const command_focus_t& focus) -> bool {
    if (focus.ime_composition_active) {
        return command.definition.focus_scope == command_focus_scope_t::ime &&
               (command.definition.focus_context.empty() ||
                command.definition.focus_context == focus.ime_context);
    }
    if (focus.modal_active) {
        return command.definition.focus_scope == command_focus_scope_t::modal &&
               (command.definition.focus_context.empty() ||
                command.definition.focus_context == focus.modal_context);
    }
    if (focus.text_entry_active) {
        return command.definition.focus_scope == command_focus_scope_t::text_entry &&
               (command.definition.focus_context.empty() ||
                command.definition.focus_context == focus.text_entry_context);
    }

    switch (command.definition.focus_scope) {
    case command_focus_scope_t::global:
        return true;
    case command_focus_scope_t::workspace:
        if (!focus.workspace_active) {
            return false;
        }
        // Tab commands on the workspace bar target a destination tab. They run
        // only when that destination is actually visible.
        return !command.definition.tab || is_visible(focus, *command.definition.tab);
    case command_focus_scope_t::tab:
        return focus.workspace_active && focus.active_tab && is_visible(focus, *focus.active_tab) &&
               (!command.definition.tab || *command.definition.tab == *focus.active_tab);
    case command_focus_scope_t::lane:
        return focus.workspace_active && focus.active_tab && is_visible(focus, *focus.active_tab) &&
               focus.focused_lane &&
               (!command.definition.tab || *command.definition.tab == *focus.active_tab) &&
               (!command.definition.lane || *command.definition.lane == *focus.focused_lane);
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
        return is_chord_match(binding, chord, primary_modifier);
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

[[nodiscard]] auto is_scope_overlap(const command_t& left, const command_t& right) -> bool {
    const auto restricted = [](command_focus_scope_t scope) {
        return scope == command_focus_scope_t::modal ||
               scope == command_focus_scope_t::text_entry || scope == command_focus_scope_t::ime;
    };
    if (restricted(left.definition.focus_scope) || restricted(right.definition.focus_scope)) {
        return left.definition.focus_scope == right.definition.focus_scope &&
               (left.definition.focus_context.empty() || right.definition.focus_context.empty() ||
                left.definition.focus_context == right.definition.focus_context);
    }

    if (left.definition.focus_scope == command_focus_scope_t::global ||
        right.definition.focus_scope == command_focus_scope_t::global) {
        return true;
    }
    if (left.definition.focus_scope == command_focus_scope_t::workspace ||
        right.definition.focus_scope == command_focus_scope_t::workspace) {
        return true;
    }
    if (left.definition.tab && right.definition.tab &&
        left.definition.tab != right.definition.tab) {
        return false;
    }
    if (left.definition.focus_scope == command_focus_scope_t::lane &&
        right.definition.focus_scope == command_focus_scope_t::lane && left.definition.lane &&
        right.definition.lane && left.definition.lane != right.definition.lane) {
        return false;
    }
    return true;
}

[[nodiscard]] auto overlapping_chord_pairs(const command_registry_t& registry,
                                           shortcut_primary_modifier_t primary_modifier)
    -> std::vector<shortcut_conflict_t> {
    std::vector<shortcut_conflict_t> pairs;
    for (std::size_t left_index = 0; left_index < registry.commands.size(); ++left_index) {
        const auto& left = registry.commands[left_index];
        for (std::size_t right_index = left_index + 1; right_index < registry.commands.size();
             ++right_index) {
            const auto& right = registry.commands[right_index];
            if (!is_scope_overlap(left, right)) {
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
                pairs.push_back({.first = left.definition.id, .second = right.definition.id});
            }
        }
    }
    return pairs;
}

[[nodiscard]] auto profiles_overlap(const command_registry_t& registry,
                                    shortcut_primary_modifier_t primary_modifier)
    -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    for (const auto& pair : overlapping_chord_pairs(registry, primary_modifier)) {
        diagnostics.push_back(warning(diagnostic_code_t::duplicate_binding,
                                      "shortcut conflict between '" + pair.first.value +
                                          "' and '" + pair.second.value + "'"));
    }
    return diagnostics;
}

[[nodiscard]] auto valid_profile(const shortcut_profile_t& profile) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    if (!is_valid_stable_text(profile.id, maximum_command_id_bytes)) {
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
        if (!is_valid_stable_text(binding.command.value, maximum_command_id_bytes)) {
            diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                        "shortcut profile contains an invalid command ID"));
        }
        if (!ids.insert(binding.command).second) {
            diagnostics.push_back(error(diagnostic_code_t::duplicate_id,
                                        "shortcut profile contains duplicate command bindings"));
        }
        if (!is_valid_chord_list(binding.shortcuts)) {
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
    command_registration_t registration{.definition = {.id = {std::move(id)},
                                                       .display_name = std::move(display),
                                                       .localization_key = "command." + id,
                                                       .category = category,
                                                       .focus_scope = scope,
                                                       .tab = tab,
                                                       .default_shortcuts = std::move(shortcuts),
                                                       .enabled = true,
                                                       .checked = checked,
                                                       .mutation = mutation}};
    // The ID was moved into the registration. Rebuild the key from its stable
    // value before binding the generic application/controller invoker.
    registration.definition.localization_key = "command." + registration.definition.id.value;
    if (invoker) {
        registration.definition.invoke = [invoker](const command_invocation_context_t& context) {
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
    -> dispatch_outcome_t {
    dispatch_outcome_t result{};
    result.diagnostics = valid_registration(registration);
    if (has_errors(result.diagnostics)) {
        return result;
    }
    if (registry.commands.size() >= maximum_registered_commands) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution, "command registry is at its entry cap"));
        return result;
    }
    if (find_command(registry, registration.definition.id)) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::duplicate_id, "command ID is already registered"));
        return result;
    }

    auto current =
        registration.current_shortcuts.value_or(registration.definition.default_shortcuts);
    registry.commands.push_back({.definition = std::move(registration.definition),
                                 .current_shortcuts = std::move(current)});
    result.value = true;
    return result;
}

auto find_command(const command_registry_t& registry, const command_id_t& id) noexcept
    -> const command_t* {
    const auto found = std::ranges::find(registry.commands, id, command_id_of);
    return found == registry.commands.end() ? nullptr : &*found;
}

auto update_command_state(command_registry_t& registry, const command_id_t& id, bool enabled,
                          std::optional<bool> checked) -> dispatch_outcome_t {
    dispatch_outcome_t result{};
    const auto found = std::ranges::find(registry.commands, id, command_id_of);
    if (found == registry.commands.end()) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "cannot update state for an unknown command"));
        return result;
    }
    found->definition.enabled = enabled;
    found->definition.checked = checked;
    result.value = true;
    return result;
}

auto diagnose_shortcut_conflicts(const command_registry_t& registry,
                                 shortcut_primary_modifier_t primary_modifier)
    -> diagnostic_list_t {
    return profiles_overlap(registry, primary_modifier);
}

auto find_shortcut_conflicts(const command_registry_t& registry,
                             shortcut_primary_modifier_t primary_modifier)
    -> std::vector<shortcut_conflict_t> {
    return overlapping_chord_pairs(registry, primary_modifier);
}

auto remap_command(command_registry_t& registry, const command_id_t& id,
                   std::vector<shortcut_chord_t> shortcuts) -> dispatch_outcome_t {
    dispatch_outcome_t result{};
    if (!is_valid_chord_list(shortcuts)) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                           "replacement shortcuts are invalid or exceed the cap"));
        return result;
    }
    const auto found = std::ranges::find(registry.commands, id, command_id_of);
    if (found == registry.commands.end()) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "cannot remap an unknown command"));
        return result;
    }
    found->current_shortcuts = std::move(shortcuts);
    result.value = true;
    result.diagnostics = diagnose_shortcut_conflicts(registry);
    return result;
}

auto shortcut_profile_t::from(const command_registry_t& registry, std::string profile_id)
    -> shortcut_profile_t {
    shortcut_profile_t profile{.id = std::move(profile_id)};
    for (const auto& command : registry.commands) {
        if (command.current_shortcuts != command.definition.default_shortcuts) {
            profile.bindings.push_back(
                {.command = command.definition.id, .shortcuts = command.current_shortcuts});
        }
    }
    return profile;
}

auto apply_shortcut_profile(command_registry_t& registry, const shortcut_profile_t& profile)
    -> dispatch_outcome_t {
    dispatch_outcome_t result{};
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
        command.current_shortcuts = command.definition.default_shortcuts;
    }
    for (const auto& binding : profile.bindings) {
        const auto command = std::ranges::find(proposed, binding.command, command_id_of);
        command->current_shortcuts = binding.shortcuts;
    }
    registry.commands = std::move(proposed);
    result.value = true;
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

auto shortcut_profile_t::from(std::string_view serialized) -> shortcut_profile_parse_outcome_t {
    shortcut_profile_parse_outcome_t result{};
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
            result.value = std::move(profile);
        }
    } catch (const json::exception&) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                           "shortcut profile is not valid bounded JSON"));
    }
    return result;
}

namespace {

auto candidate_scope_less(const command_t* left, const command_t* right) -> bool {
    const auto left_rank = scope_rank(left->definition.focus_scope);
    const auto right_rank = scope_rank(right->definition.focus_scope);
    return left_rank == right_rank ? left->definition.id < right->definition.id : left_rank < right_rank;
}

auto is_scope_rank(const command_t* command, int rank) -> bool {
    return scope_rank(command->definition.focus_scope) == rank;
}

auto shortcut_candidates(const command_registry_t& registry, const shortcut_event_t& event,
                         const command_focus_t& focus, shortcut_primary_modifier_t primary_modifier,
                         std::optional<command_focus_scope_t> restricted)
    -> std::vector<const command_t*> {
    std::vector<const command_t*> candidates;
    candidates.reserve(registry.commands.size());
    for (const auto& command : registry.commands) {
        if (restricted && command.definition.focus_scope != *restricted) {
            continue;
        }
        if (is_eligible(command, focus) && has_binding(command, event.chord, primary_modifier)) {
            candidates.push_back(&command);
        }
    }
    return candidates;
}

} // namespace

auto dispatch_shortcut(const command_registry_t& registry, const shortcut_event_t& event,
                       const command_focus_t& focus, shortcut_primary_modifier_t primary_modifier)
    -> command_dispatch_outcome_t {
    command_dispatch_outcome_t result{};
    if (!is_valid_chord(event.chord, true) ||
        focus.modal_context.size() > maximum_focus_context_bytes ||
        focus.text_entry_context.size() > maximum_focus_context_bytes ||
        focus.ime_context.size() > maximum_focus_context_bytes ||
        registry.commands.size() > maximum_registered_commands) {
        result.value.status = command_dispatch_status_t::invalid_event;
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_binding,
                  "shortcut event, focus, or registry exceeds its bounds"));
        return result;
    }

    const auto restricted = restricted_focus_scope(focus);
    auto candidates = shortcut_candidates(registry, event, focus, primary_modifier, restricted);
    if (candidates.empty()) {
        result.value.status = restricted ? command_dispatch_status_t::focus_consumed
                                   : command_dispatch_status_t::no_match;
        return result;
    }

    std::ranges::sort(candidates, candidate_scope_less);
    const auto best_rank = scope_rank(candidates.front()->definition.focus_scope);
    const auto peers = static_cast<std::size_t>(
        std::ranges::count_if(candidates, std::bind_back(is_scope_rank, best_rank)));
    if (peers > 1) {
        result.value.status = command_dispatch_status_t::conflict;
        for (const auto* candidate : candidates) {
            if (scope_rank(candidate->definition.focus_scope) == best_rank) {
                result.diagnostics.push_back(
                    warning(diagnostic_code_t::duplicate_binding,
                            "shortcut dispatch is ambiguous at this focus scope: '" +
                                candidate->definition.id.value + "'"));
            }
        }
        return result;
    }

    const auto& command = *candidates.front();
    result.value.command = command.definition.id;
    if (!command.definition.enabled) {
        result.value.status = command_dispatch_status_t::disabled;
        return result;
    }
    if (event.auto_repeat && command.definition.mutation &&
        command.definition.repeat_policy == command_repeat_policy_t::suppress_mutations) {
        result.value.status = command_dispatch_status_t::repeat_suppressed;
        return result;
    }
    if (!command.definition.invoke) {
        result.value.status = command_dispatch_status_t::rejected;
        result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
                                           "command has no controller invocation handler"));
        return result;
    }

    const command_invocation_context_t context{
        .command = command.definition.id,
        .focus = focus,
        .shortcut = event.chord,
        .auto_repeat = event.auto_repeat,
    };
    auto invocation = command.definition.invoke(context);
    const auto accepted = invocation.value && invocation.ok();
    result.diagnostics = std::move(invocation.diagnostics);
    result.value.status = accepted
                        ? command_dispatch_status_t::invoked
                        : command_dispatch_status_t::rejected;
    return result;
}

auto invoke_command(const command_registry_t& registry, const command_id_t& id,
                    const command_focus_t& focus) -> command_dispatch_outcome_t {
    command_dispatch_outcome_t result{};
    if (focus.modal_context.size() > maximum_focus_context_bytes ||
        focus.text_entry_context.size() > maximum_focus_context_bytes ||
        focus.ime_context.size() > maximum_focus_context_bytes ||
        registry.commands.size() > maximum_registered_commands) {
        result.value.status = command_dispatch_status_t::invalid_event;
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                           "command invocation context exceeds its bounds"));
        return result;
    }
    const auto command = find_command(registry, id);
    if (!command) {
        result.value.status = command_dispatch_status_t::no_match;
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "cannot invoke an unknown command"));
        return result;
    }
    result.value.command = command->definition.id;
    const auto restricted = restricted_focus_scope(focus);
    if (restricted && command->definition.focus_scope != *restricted) {
        result.value.status = command_dispatch_status_t::focus_consumed;
        return result;
    }
    if (!is_eligible(*command, focus)) {
        result.value.status = command_dispatch_status_t::focus_consumed;
        return result;
    }
    if (!command->definition.enabled) {
        result.value.status = command_dispatch_status_t::disabled;
        return result;
    }
    if (!command->definition.invoke) {
        result.value.status = command_dispatch_status_t::rejected;
        result.diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
                                           "command has no controller invocation handler"));
        return result;
    }
    const command_invocation_context_t context{.command = command->definition.id, .focus = focus};
    auto invocation = command->definition.invoke(context);
    const auto accepted = invocation.value && invocation.ok();
    result.diagnostics = std::move(invocation.diagnostics);
    result.value.status = accepted
                        ? command_dispatch_status_t::invoked
                        : command_dispatch_status_t::rejected;
    return result;
}

auto command_registry_t::from(command_invoker_t invoker) -> command_registry_t {
    command_registry_t registry;
    const auto global = command_focus_scope_t::global;
    const auto workspace = command_focus_scope_t::workspace;
    const auto compose = std::optional{workspace_tab_t::compose};

    add_builtin(registry, invoker, "view.notifications", "Notifications",
                command_category_t::workspace, global,
                {chord(shortcut_key_t::m, modified(shortcut_modifier_t::shift))});
    add_builtin(registry, invoker, "workspace.command_menu", "Command menu", command_category_t::workspace,
                global, {chord(shortcut_key_t::p, modified(shortcut_modifier_t::shift))});
    add_builtin(registry, invoker, "workspace.options", "Options", command_category_t::workspace,
                global, {chord(static_cast<shortcut_key_t>(0x36), primary())});
    add_builtin(registry, invoker, "document.new_project", "New Project",
                command_category_t::document, global, {chord(shortcut_key_t::n, primary())}, true);
    add_builtin(registry, invoker, "document.save_project", "Save Project",
                command_category_t::document, global, {chord(shortcut_key_t::s, primary())}, true);
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
    add_builtin(registry, invoker, "edit.move_to_bgm", "Move Selection to BGM",
                command_category_t::edit, command_focus_scope_t::tab,
                {chord(shortcut_key_t::backquote)}, true, compose);
    add_builtin(registry, invoker, "edit.move_next_to_bgm", "Move Next Selected Note to BGM",
                command_category_t::edit, command_focus_scope_t::tab,
                {chord(shortcut_key_t::backquote, static_cast<shortcut_modifiers_t>(shortcut_modifier_t::shift))},
                true, compose);
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
    add_builtin(registry, invoker, "selection.select_all_objects", "Select All Objects",
                command_category_t::selection, command_focus_scope_t::tab,
                {chord(shortcut_key_t::a, modified(shortcut_modifier_t::alt))}, false, compose);
    add_builtin(registry, invoker, "selection.select_same_sound_chart", "Select Same Sound (Whole Chart)",
                command_category_t::selection, command_focus_scope_t::tab,
                {chord(shortcut_key_t::a, modified(shortcut_modifier_t::shift))}, false, compose);
    add_builtin(registry, invoker, "selection.select_all_after_cursor", "Select All After Cursor",
                command_category_t::selection, command_focus_scope_t::tab,
                {chord(shortcut_key_t::e, modified(shortcut_modifier_t::shift))}, false, compose);
    add_builtin(registry, invoker, "selection.select_all_objects_after_cursor",
                "Select All Objects After Cursor", command_category_t::selection,
                command_focus_scope_t::tab,
                {chord(shortcut_key_t::e, modified(shortcut_modifier_t::alt))}, false, compose);
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
    add_builtin(registry, invoker, "navigation.go_to_bookmark", "Go To Bookmark",
                command_category_t::navigation, command_focus_scope_t::tab,
                {chord(shortcut_key_t::g, modified(shortcut_modifier_t::shift))},
                false, compose);
    add_builtin(registry, invoker, "navigation.go_to_sound", "Go to sound",
                command_category_t::navigation, command_focus_scope_t::tab,
                {chord(shortcut_key_t::k, primary())}, false, compose);
    add_builtin(registry, invoker, "edit.toggle_quantize_preview", "Toggle Quantize Preview",
                command_category_t::edit, command_focus_scope_t::tab,
                {chord(shortcut_key_t::q, modified(shortcut_modifier_t::alt))}, true, compose);
    add_builtin(registry, invoker, "edit.apply_quantize", "Apply Quantization",
                command_category_t::edit, command_focus_scope_t::tab, {}, true, compose);
    // Dock visibility mutates session UI state; suppress held-key repeats.
    add_builtin(registry, invoker, "view.open_layers", "Open Layers",
                command_category_t::workspace, command_focus_scope_t::tab,
                {chord(shortcut_key_t::l, primary())}, true, compose);
    add_builtin(registry, invoker, "document.export_output_group", "Export Output Group…",
                command_category_t::document, command_focus_scope_t::workspace, {}, true);
    add_builtin(registry, invoker, "semantic_layer.create", "Create Layer",
                command_category_t::semantic_layer, command_focus_scope_t::tab,
                {chord(shortcut_key_t::l, modified(shortcut_modifier_t::shift))}, true, compose);
    add_builtin(registry, invoker, "view.toggle_sounds", "Toggle Sounds Dock",
                command_category_t::workspace, command_focus_scope_t::tab,
                {chord(shortcut_key_t::k, modified(shortcut_modifier_t::shift))}, true, compose);
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
                command_category_t::transport, workspace, {});
    add_builtin(registry, invoker, "transport.play_from_start", "Play from Start",
                command_category_t::transport, workspace,
                {chord(shortcut_key_t::space, modified(shortcut_modifier_t::shift))});
    add_builtin(registry, invoker, "measures.split_at_cursor", "Toggle Measure Line at Cursor",
                command_category_t::edit, command_focus_scope_t::tab,
                {chord(shortcut_key_t::m, primary())}, true, compose);
    add_builtin(registry, invoker, "transport.loop_selection", "Play Selection Looped",
                command_category_t::transport, command_focus_scope_t::tab,
                {chord(shortcut_key_t::space,
                       static_cast<shortcut_modifiers_t>(shortcut_modifier_t::shift))}, true, compose);
    add_builtin(registry, invoker, "transport.toggle_loop_selection", "Toggle Selection Loop",
                command_category_t::transport, command_focus_scope_t::tab, {}, true, compose);
    add_builtin(registry, invoker, "measures.insert_before", "Insert Measure Before",
                command_category_t::edit, command_focus_scope_t::tab, {}, true, compose);
    add_builtin(registry, invoker, "measures.insert_after", "Insert Measure After",
                command_category_t::edit, command_focus_scope_t::tab, {}, true, compose);
    add_builtin(registry, invoker, "measures.remove_empty", "Remove Empty Measure",
                command_category_t::edit, command_focus_scope_t::tab, {}, true, compose);
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

    constexpr std::array function_keys{
        shortcut_key_t::f1, shortcut_key_t::f2, shortcut_key_t::f3, shortcut_key_t::f4,
        shortcut_key_t::f5, shortcut_key_t::f6, shortcut_key_t::f7,
    };
    for (std::size_t i = 0; i < workspace_tabs.size(); ++i) {
        add_builtin(registry, invoker, "workspace.open_" + std::string(workspace_tabs[i].id),
                    "Open " + std::string(workspace_tabs[i].label), command_category_t::workspace, workspace,
                    {chord(function_keys[i])}, false, workspace_tabs[i].kind);
    }

    add_builtin(registry, invoker, "timing.toggle_lock", "Toggle Timing Lock",
                command_category_t::timing, command_focus_scope_t::tab,
                {chord(shortcut_key_t::t, modified(shortcut_modifier_t::shift))}, true,
                workspace_tab_t::compose, false);
    add_builtin(registry, invoker, "timing.insert_bpm", "Insert BPM Change",
                command_category_t::timing, command_focus_scope_t::tab, {}, true,
                workspace_tab_t::compose);
    add_builtin(registry, invoker, "timing.insert_sv", "Insert Scroll Velocity",
                command_category_t::timing, command_focus_scope_t::tab, {}, true,
                workspace_tab_t::compose);
    add_builtin(registry, invoker, "timing.open_settings", "Timing Settings",
                command_category_t::timing, command_focus_scope_t::tab, {}, false,
                workspace_tab_t::timing);

    add_builtin(registry, invoker, "edit.tool_select", "Select tool", command_category_t::edit,
        command_focus_scope_t::tab,
        {chord(shortcut_key_t::digit_1, static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt))},
        false, compose);
    add_builtin(registry, invoker, "edit.tool_draw", "Draw tool", command_category_t::edit,
        command_focus_scope_t::tab,
        {chord(shortcut_key_t::digit_2, static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt))},
        false, compose);
    add_builtin(registry, invoker, "edit.tool_layer", "Layer tool", command_category_t::edit,
        command_focus_scope_t::tab,
        {chord(shortcut_key_t::digit_3, static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt))},
        false, compose);
    for (const auto kind : note_palette_kinds) {
        const auto& entry = note_kinds[static_cast<std::size_t>(kind)];
        std::vector<shortcut_chord_t> shortcuts;
        if (entry.shortcut_digit != 0)
            shortcuts.push_back(chord(static_cast<shortcut_key_t>(
                static_cast<unsigned>(shortcut_key_t::digit_1) + entry.shortcut_digit - 1),
                static_cast<shortcut_modifiers_t>(shortcut_modifier_t::alt)));
        add_builtin(registry, invoker, "edit.note_kind_" + std::string(entry.command_id_suffix),
            "Arm note kind: " + std::string(entry.command_id_suffix), command_category_t::edit,
            command_focus_scope_t::tab, std::move(shortcuts), false, compose);
    }

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
    add_builtin(registry, invoker, "audio.toggle_click_audition", "Play Sound on Click",
                command_category_t::audio, global, {}, false);

    return registry;
}

} // namespace wormedit

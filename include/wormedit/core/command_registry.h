#pragma once

#include <wormedit/core/workspace.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit {

// Chords use USB HID Keyboard/Keypad page usages, so a frontend can translate
// SDL, native, or other backend events without making the registry backend
// dependent. Modifier keys are represented separately from the key usage.
enum class shortcut_key_t : std::uint16_t {
    a = 0x04,
    b = 0x05,
    c = 0x06,
    d = 0x07,
    e = 0x08,
    f = 0x09,
    g = 0x0a,
    h = 0x0b,
    i = 0x0c,
    j = 0x0d,
    k = 0x0e,
    l = 0x0f,
    m = 0x10,
    n = 0x11,
    o = 0x12,
    p = 0x13,
    q = 0x14,
    r = 0x15,
    s = 0x16,
    t = 0x17,
    u = 0x18,
    v = 0x19,
    w = 0x1a,
    x = 0x1b,
    y = 0x1c,
    z = 0x1d,
    digit_1 = 0x1e,
    digit_2 = 0x1f,
    digit_3 = 0x20,
    digit_4 = 0x21,
    digit_5 = 0x22,
    digit_6 = 0x23,
    digit_7 = 0x24,
    digit_8 = 0x25,
    digit_9 = 0x26,
    digit_0 = 0x27,
    enter = 0x28,
    escape = 0x29,
    backspace = 0x2a,
    space = 0x2c,
    backquote = 0x35,
    f1 = 0x3a,
    f2 = 0x3b,
    f3 = 0x3c,
    f4 = 0x3d,
    f5 = 0x3e,
    f6 = 0x3f,
    f7 = 0x40,
    f8 = 0x41,
    f9 = 0x42,
    f10 = 0x43,
    f11 = 0x44,
    f12 = 0x45,
    delete_key = 0x4c,
    right = 0x4f,
    left = 0x50,
    down = 0x51,
    up = 0x52,
};

enum class shortcut_modifier_t : std::uint8_t {
    none = 0,
    shift = 1U << 0U,
    control = 1U << 1U,
    alt = 1U << 2U,
    meta = 1U << 3U,
    primary = 1U << 4U,
};

using shortcut_modifiers_t = std::uint8_t;

[[nodiscard]] constexpr auto operator|(shortcut_modifier_t left, shortcut_modifier_t right) noexcept
    -> shortcut_modifiers_t {
    return static_cast<shortcut_modifiers_t>(left) | static_cast<shortcut_modifiers_t>(right);
}

[[nodiscard]] constexpr auto operator|(shortcut_modifiers_t left,
                                       shortcut_modifier_t right) noexcept -> shortcut_modifiers_t {
    return left | static_cast<shortcut_modifiers_t>(right);
}

struct shortcut_chord_t {
    shortcut_key_t key{shortcut_key_t::a};
    shortcut_modifiers_t modifiers{};

    auto operator==(const shortcut_chord_t&) const -> bool = default;
    auto operator<=>(const shortcut_chord_t&) const = default;
};

struct command_id_t {
    std::string value;

    auto operator==(const command_id_t&) const -> bool = default;
    auto operator<=>(const command_id_t&) const = default;
};

enum class command_category_t : std::uint8_t {
    document,
    edit,
    selection,
    navigation,
    workspace,
    transport,
    timing,
    semantic_layer,
    audio,
    tools,
};

enum class command_focus_scope_t : std::uint8_t {
    global,
    workspace,
    tab,
    lane,
    modal,
    text_entry,
    ime,
};

enum class command_repeat_policy_t : std::uint8_t {
    suppress_mutations,
    allow,
};

enum class shortcut_primary_modifier_t : std::uint8_t {
    control,
    meta,
};

struct command_focus_t {
    bool modal_active{};
    bool text_entry_active{};
    bool ime_composition_active{};
    bool workspace_active{};
    std::optional<workspace_tab_t> active_tab;
    std::vector<workspace_tab_t> visible_tabs;
    std::optional<lane_id_t> focused_lane;
    std::string modal_context;
    std::string text_entry_context;
    std::string ime_context;
};

struct command_invocation_context_t {
    command_id_t command;
    command_focus_t focus;
    std::optional<shortcut_chord_t> shortcut;
    bool auto_repeat{};
};

using command_invocation_outcome_t = diagnosed_t<bool>;

struct command_invocation_result_t {
    [[nodiscard]] static auto from(command_invocation_outcome_t&& outcome) -> command_invocation_result_t;
    [[nodiscard]] auto into_outcome() && -> command_invocation_outcome_t;
    bool accepted{};
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return accepted && diagnostics.empty();
    }
};

using command_invoker_t =
    std::function<command_invocation_result_t(const command_invocation_context_t&)>;

struct command_definition_t {
    command_id_t id;
    std::string display_name;
    std::string localization_key;
    command_category_t category{command_category_t::edit};
    command_focus_scope_t focus_scope{command_focus_scope_t::global};
    std::optional<workspace_tab_t> tab;
    std::optional<lane_id_t> lane;
    std::string focus_context;
    std::vector<shortcut_chord_t> default_shortcuts;
    bool enabled{true};
    std::optional<bool> checked;
    bool mutation{};
    command_repeat_policy_t repeat_policy{command_repeat_policy_t::suppress_mutations};
    command_invoker_t invoke;
};

struct command_registration_t {
    command_definition_t definition;
    // Omitted means use defaults; an explicit empty vector means unbound.
    std::optional<std::vector<shortcut_chord_t>> current_shortcuts;
};
struct command_t {
    command_definition_t definition;
    std::vector<shortcut_chord_t> current_shortcuts;
};

struct command_registry_t {
    [[nodiscard]] static auto from(command_invoker_t invoker) -> command_registry_t;

    std::vector<command_t> commands;
};

inline constexpr std::size_t maximum_registered_commands = 512;
inline constexpr std::size_t maximum_command_id_bytes = 128;
inline constexpr std::size_t maximum_command_display_name_bytes = 512;
inline constexpr std::size_t maximum_localization_key_bytes = 256;
inline constexpr std::size_t maximum_focus_context_bytes = 128;
inline constexpr std::size_t maximum_shortcuts_per_command = 8;
inline constexpr std::size_t maximum_shortcut_profile_bindings = maximum_registered_commands;
inline constexpr std::size_t maximum_shortcut_profile_bytes = 1024U * 1024U;

using command_registry_outcome_t = diagnosed_t<bool>;

struct command_registry_result_t {
    [[nodiscard]] static auto from(command_registry_t& registry, command_registration_t registration)
        -> command_registry_outcome_t;
    [[nodiscard]] static auto from(command_registry_outcome_t&& outcome) -> command_registry_result_t;
    [[nodiscard]] static auto update_state(command_registry_t& registry, const command_id_t& id,
        bool enabled, std::optional<bool> checked) -> command_registry_outcome_t;
    [[nodiscard]] static auto remap(command_registry_t& registry, const command_id_t& id,
        std::vector<shortcut_chord_t> shortcuts) -> command_registry_outcome_t;
    bool applied{};
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return applied && diagnostics.empty();
    }
};

[[nodiscard]] auto register_command(command_registry_t& registry,
                                    command_registration_t registration)
    -> command_registry_result_t;
[[nodiscard]] auto find_command(const command_registry_t& registry, const command_id_t& id) noexcept
    -> const command_t*;
[[nodiscard]] auto update_command_state(command_registry_t& registry, const command_id_t& id,
                                        bool enabled, std::optional<bool> checked)
    -> command_registry_result_t;
[[nodiscard]] auto remap_command(command_registry_t& registry, const command_id_t& id,
                                 std::vector<shortcut_chord_t> shortcuts)
    -> command_registry_result_t;

struct shortcut_profile_binding_t {
    command_id_t command;
    std::vector<shortcut_chord_t> shortcuts;

    auto operator==(const shortcut_profile_binding_t&) const -> bool = default;
};

// This profile is intentionally independent of song_document_t and native
// chart serialization. Persist it as a user preference alongside app settings.
struct shortcut_profile_t {
    [[nodiscard]] static auto from(const command_registry_t& registry, std::string profile_id)
        -> shortcut_profile_t;

    std::string id;
    std::vector<shortcut_profile_binding_t> bindings;

    auto operator==(const shortcut_profile_t&) const -> bool = default;
};

struct shortcut_profile_result_t {
    bool applied{};
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return applied && diagnostics.empty();
    }
};

[[nodiscard]] auto apply_shortcut_profile(command_registry_t& registry,
                                          const shortcut_profile_t& profile)
    -> shortcut_profile_result_t;
[[nodiscard]] auto diagnose_shortcut_conflicts(
    const command_registry_t& registry,
    shortcut_primary_modifier_t primary_modifier = shortcut_primary_modifier_t::control)
    -> diagnostic_list_t;
// The command pairs whose enabled-scope and chord collide, in registry order. These are the
// same pairs diagnose_shortcut_conflicts reports, as data for callers that must not parse text.
struct shortcut_conflict_t {
    command_id_t first;
    command_id_t second;

    auto operator==(const shortcut_conflict_t&) const -> bool = default;
};
[[nodiscard]] auto find_shortcut_conflicts(
    const command_registry_t& registry,
    shortcut_primary_modifier_t primary_modifier = shortcut_primary_modifier_t::control)
    -> std::vector<shortcut_conflict_t>;
[[nodiscard]] auto serialize_shortcut_profile(const shortcut_profile_t& profile) -> std::string;

struct shortcut_profile_parse_result_t {
    std::optional<shortcut_profile_t> profile;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return profile.has_value() && diagnostics.empty();
    }
};

[[nodiscard]] auto parse_shortcut_profile(std::string_view serialized)
    -> shortcut_profile_parse_result_t;

enum class command_dispatch_status_t : std::uint8_t {
    invoked,
    rejected,
    no_match,
    focus_consumed,
    disabled,
    repeat_suppressed,
    conflict,
    invalid_event,
};

struct command_dispatch_result_t {
    command_dispatch_status_t status{command_dispatch_status_t::no_match};
    std::optional<command_id_t> command;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == command_dispatch_status_t::invoked;
    }
};

struct shortcut_event_t {
    shortcut_chord_t chord;
    bool auto_repeat{};
};

[[nodiscard]] auto dispatch_shortcut(const command_registry_t& registry,
                                     const shortcut_event_t& event, const command_focus_t& focus,
                                     shortcut_primary_modifier_t primary_modifier)
    -> command_dispatch_result_t;
[[nodiscard]] auto invoke_command(const command_registry_t& registry, const command_id_t& id,
                                  const command_focus_t& focus = {}) -> command_dispatch_result_t;

// Creates discoverable built-ins from the editor plan. The supplied invoker
// is called by keyboard, menu, and toolbar paths through the same signature.
[[nodiscard]] auto make_default_editor_command_registry(command_invoker_t invoker)
    -> command_registry_t;

} // namespace wormedit

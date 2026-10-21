#pragma once

#include <wormedit/core/result.h>
#include <wormedit/core/shortcut_vocabulary.h>
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

using command_invoker_t =
    std::function<dispatch_outcome_t(const command_invocation_context_t&)>;

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

[[nodiscard]] auto register_command(command_registry_t& registry,
                                    command_registration_t registration)
    -> dispatch_outcome_t;
[[nodiscard]] auto find_command(const command_registry_t& registry, const command_id_t& id) noexcept
    -> const command_t*;
[[nodiscard]] auto update_command_state(command_registry_t& registry, const command_id_t& id,
                                        bool enabled, std::optional<bool> checked)
    -> dispatch_outcome_t;
[[nodiscard]] auto remap_command(command_registry_t& registry, const command_id_t& id,
                                 std::vector<shortcut_chord_t> shortcuts)
    -> dispatch_outcome_t;

struct shortcut_profile_binding_t {
    command_id_t command;
    std::vector<shortcut_chord_t> shortcuts;

    auto operator==(const shortcut_profile_binding_t&) const -> bool = default;
};

// This profile is intentionally independent of song_document_t and native
// chart serialization. Persist it as a user preference alongside app settings.
struct shortcut_profile_t {
    [[nodiscard]] static auto from(std::string_view serialized)
        -> diagnosed_t<std::optional<shortcut_profile_t>>;
    [[nodiscard]] static auto from(const command_registry_t& registry, std::string profile_id)
        -> shortcut_profile_t;

    std::string id;
    std::vector<shortcut_profile_binding_t> bindings;

    auto operator==(const shortcut_profile_t&) const -> bool = default;
};

[[nodiscard]] auto apply_shortcut_profile(command_registry_t& registry,
                                          const shortcut_profile_t& profile)
    -> dispatch_outcome_t;
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

using shortcut_profile_parse_outcome_t = diagnosed_t<std::optional<shortcut_profile_t>>;

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

struct shortcut_event_t;
struct command_dispatch_effect_t {
    [[nodiscard]] auto is_invoked() const noexcept -> bool {
        return status == command_dispatch_status_t::invoked;
    }
    command_dispatch_status_t status{command_dispatch_status_t::no_match};
    std::optional<command_id_t> command;
    auto operator==(const command_dispatch_effect_t&) const -> bool = default;
};
using command_dispatch_outcome_t = diagnosed_t<command_dispatch_effect_t>;

struct shortcut_event_t {
    shortcut_chord_t chord;
    bool auto_repeat{};
};

[[nodiscard]] auto dispatch_shortcut(const command_registry_t& registry,
                                     const shortcut_event_t& event, const command_focus_t& focus,
                                     shortcut_primary_modifier_t primary_modifier)
    -> command_dispatch_outcome_t;
[[nodiscard]] auto invoke_command(const command_registry_t& registry, const command_id_t& id,
                                  const command_focus_t& focus = {}) -> command_dispatch_outcome_t;


} // namespace wormedit

#pragma once

#include <wormedit/core/measure.h>
#include <wormedit/core/atomic_file_storage.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit {

inline constexpr std::size_t maximum_preferences_bytes = 1024 * 1024;
inline constexpr std::size_t maximum_preferences_depth = 16;
inline constexpr std::size_t maximum_external_players = 64;
inline constexpr std::size_t maximum_preference_string_bytes = 4096;

struct general_preferences_t {
    std::uint32_t autosave_minutes{5};
    std::uint32_t recent_files_count{10};
    bool confirm_unsaved_close{true};
    std::string shortcut_profile_path{"shortcuts.json"};
    auto operator==(const general_preferences_t&) const -> bool = default;
};
enum class editor_mode_family_t : std::uint32_t { beatmania, popn, dance, pump, keyboard, other };
inline constexpr std::size_t editor_mode_family_count = 6;
struct editor_preferences_t {
    std::uint32_t default_snap{4};
    // Indexed by editor_mode_family_t; 0 preserves the profile, 1 up, 2 down.
    std::array<std::uint32_t, editor_mode_family_count> scroll_directions{};
    std::uint32_t playhead_anchor_percent{20};
    bool note_kind_palette_visible{true};
    bool lane_key_legend_visible{true};
    bool audition_on_click{true};
    std::uint32_t area_band_alpha{48};
    auto operator==(const editor_preferences_t&) const -> bool = default;
};
struct audio_preferences_t {
    rational_t playback_rate{1, 1};
    bool keep_pitch{};
    std::uint32_t keysound_budget_mib{512};
    auto operator==(const audio_preferences_t&) const -> bool = default;
};
struct display_preferences_t {
    std::string theme; // Empty selects the bundled default; environment override remains independent.
    std::uint32_t ui_scale_percent{}; // 0 = automatic; explicit values are 100..200.
    bool frame_timing_overlay{};
    auto operator==(const display_preferences_t&) const -> bool = default;
};
struct external_player_definition_t {
    std::string name;
    std::string path;
    std::string start_arguments;
    std::string here_arguments;
    std::string stop_arguments;
    auto operator==(const external_player_definition_t&) const -> bool = default;
};
struct external_player_preferences_t {
    std::vector<external_player_definition_t> players;
    std::size_t selected{};
    auto operator==(const external_player_preferences_t&) const -> bool = default;
};
[[nodiscard]] auto default_external_player_preferences() -> external_player_preferences_t;
struct minimap_preferences_t {
    bool click_moves_paused_playhead{};
    auto operator==(const minimap_preferences_t&) const -> bool = default;
};
struct layout_preferences_t {
    std::string dock_layout; // Empty preserves the built-in layout until the docking card applies it.
    auto operator==(const layout_preferences_t&) const -> bool = default;
};
struct program_preferences_t {
    general_preferences_t general;
    editor_preferences_t editor;
    audio_preferences_t audio;
    display_preferences_t display;
    external_player_preferences_t players{default_external_player_preferences()};
    minimap_preferences_t minimap;
    layout_preferences_t layout;
    auto operator==(const program_preferences_t&) const -> bool = default;
};
struct preferences_parse_result_t {
    program_preferences_t preferences;
    std::vector<std::string> warnings;
    bool parsed{};
};
struct preferences_load_result_t {
    preferences_parse_result_t contents;
    bool missing{};
    bool migrated{};
};

[[nodiscard]] auto default_program_preferences() -> program_preferences_t;
[[nodiscard]] auto valid_external_player_preferences(const external_player_preferences_t&) -> bool;
[[nodiscard]] auto parse_program_preferences(std::string_view bytes) -> preferences_parse_result_t;
// Invalid typed values reject serialization; no project data or shortcut bindings are serialized.
[[nodiscard]] auto serialize_program_preferences(const program_preferences_t& preferences)
    -> std::optional<std::string>;
[[nodiscard]] auto load_program_preferences(const std::filesystem::path& path)
    -> preferences_load_result_t;
[[nodiscard]] auto save_program_preferences_atomic(const std::filesystem::path& path,
    const program_preferences_t& preferences) -> atomic_file_save_result_t;

// Legacy quoted-text file is read only during first-run migration; failures retain values.
[[nodiscard]] auto load_legacy_external_player_preferences(const std::filesystem::path& path,
    external_player_preferences_t& preferences) -> std::string;
[[nodiscard]] auto load_program_preferences_for_startup(const std::filesystem::path& path,
    const std::filesystem::path& legacy_player_path) -> preferences_load_result_t;

} // namespace wormedit

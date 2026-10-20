#pragma once

#include <wormedit/core/measure.h>
#include <wormedit/core/atomic_file_storage.h>

#include <array>
#include <cstdint>
#include <optional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit {

struct lane_layout_t;
struct input_profile_t;
inline constexpr std::size_t maximum_lane_key_layouts = 128;
inline constexpr std::size_t maximum_lane_key_bindings = 128;

// USB HID usages in lane display order; zero leaves a lane unbound.
struct lane_key_bindings_t {
    std::vector<std::uint16_t> keys;
    [[nodiscard]] auto is_valid() const -> bool;
    [[nodiscard]] static auto from(const lane_layout_t&, const input_profile_t&) -> lane_key_bindings_t;
    [[nodiscard]] static auto preset(const lane_layout_t&, const input_profile_t&, bool interleaved)
        -> lane_key_bindings_t;
    [[nodiscard]] static auto has_interleaved_preset(const lane_layout_t&) -> bool;
    [[nodiscard]] auto profile_for(const lane_layout_t&, const input_profile_t& fallback) const
        -> result_t<input_profile_t, std::string>;
    auto operator==(const lane_key_bindings_t&) const -> bool = default;
};
struct lane_key_preferences_t {
    std::map<std::string, lane_key_bindings_t, std::less<>> layouts;
    [[nodiscard]] auto is_valid() const -> bool;
    auto operator==(const lane_key_preferences_t&) const -> bool = default;
};

inline constexpr std::size_t maximum_preferences_bytes = 1024 * 1024;
inline constexpr std::size_t maximum_preferences_depth = 16;
inline constexpr std::size_t maximum_external_players = 64;
inline constexpr std::size_t maximum_field_scheme_overrides = 128;
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
enum class move_collision_policy_t : std::uint32_t { reject, displace_to_bgm };
struct editor_preferences_t {
    move_collision_policy_t move_collision_policy{move_collision_policy_t::reject};
    std::uint32_t default_snap{4};
    // Indexed by editor_mode_family_t; 0 preserves the profile, 1 up, 2 down.
    std::array<std::uint32_t, editor_mode_family_count> scroll_directions{};
    std::uint32_t playhead_anchor_percent{20};
    bool note_kind_palette_visible{true};
    bool lane_key_legend_visible{true};
    // Editor placement audition is distinct from Sounds-panel preview-on-click.
    bool audition_on_click{true};
    bool invert_scroll_wheel{};
    std::uint32_t area_band_alpha{48};
    auto operator==(const editor_preferences_t&) const -> bool = default;
};
struct audio_preferences_t {
    rational_t playback_rate{1, 1};
    bool keep_pitch{};
    std::uint32_t keysound_budget_mib{512};
    std::string osu_skin_directory;
    auto operator==(const audio_preferences_t&) const -> bool = default;
};
struct display_preferences_t {
    std::string theme; // Empty selects the bundled default; environment override remains independent.
    std::uint32_t ui_scale_percent{}; // 0 = automatic; explicit values are 100..200.
    bool frame_timing_overlay{};
    std::string field_scheme{"wormedit"};
    std::map<std::string, std::string, std::less<>> field_scheme_overrides;
    // Single/P1 then P2: 0 skin default, 1 chart layout, 2 left, 3 right.
    std::array<std::uint32_t, 2> scratch_positions{};
    bool snap_colored_grid{true};
    std::uint32_t snap_grid_opacity_percent{35};
    std::uint32_t field_waveform_opacity_percent{75};
    auto operator==(const display_preferences_t&) const -> bool = default;
};
[[nodiscard]] auto resolve_field_scheme(const display_preferences_t&, std::string_view layout_key)
    -> std::string_view;
[[nodiscard]] auto is_valid_field_scheme_overrides(const display_preferences_t&) -> bool;

struct external_player_definition_t {
    std::string name;
    std::string path;
    std::string start_arguments;
    std::string here_arguments;
    std::string stop_arguments;
    auto operator==(const external_player_definition_t&) const -> bool = default;
};
struct external_player_preferences_t {
    [[nodiscard]] static auto with_defaults() -> external_player_preferences_t;

    std::vector<external_player_definition_t> players;
    std::size_t selected{};
    auto operator==(const external_player_preferences_t&) const -> bool = default;
};
struct minimap_preferences_t {
    bool click_moves_paused_playhead{};
    auto operator==(const minimap_preferences_t&) const -> bool = default;
};
struct layout_preferences_t {
    std::string dock_layout; // Empty preserves the built-in layout until the docking card applies it.
    auto operator==(const layout_preferences_t&) const -> bool = default;
};
struct program_preferences_t {
    [[nodiscard]] static auto with_defaults() -> program_preferences_t;

    general_preferences_t general;
    editor_preferences_t editor;
    audio_preferences_t audio;
    display_preferences_t display;
    external_player_preferences_t players{external_player_preferences_t::with_defaults()};
    minimap_preferences_t minimap;
    layout_preferences_t layout;
    lane_key_preferences_t lane_keys;
    auto operator==(const program_preferences_t&) const -> bool = default;
};
struct preferences_parse_effect_t {
    [[nodiscard]] static auto from(std::string_view bytes)
        -> preferences_parse_effect_t;

    bool succeeded{true};
    [[nodiscard]] auto is_success() const -> bool { return succeeded; }
    program_preferences_t preferences;
    std::vector<std::string> warnings;
    auto operator==(const preferences_parse_effect_t&) const -> bool = default;
};
struct preferences_load_result_t {
    preferences_parse_effect_t contents{.succeeded = false, .preferences = program_preferences_t::with_defaults()};
    bool missing{};
    bool migrated{};
};

[[nodiscard]] auto is_valid_external_player_preferences(const external_player_preferences_t&) -> bool;
// Invalid typed values reject serialization; no project data or shortcut bindings are serialized.
[[nodiscard]] auto serialize_program_preferences(const program_preferences_t& preferences)
    -> std::optional<std::string>;
[[nodiscard]] auto load_program_preferences(const std::filesystem::path& path)
    -> preferences_load_result_t;
[[nodiscard]] auto save_program_preferences_atomic(const std::filesystem::path& path,
    const program_preferences_t& preferences) -> atomic_file_save_outcome_t;

// Legacy quoted-text file is read only during first-run migration; failures retain values.
[[nodiscard]] auto load_legacy_external_player_preferences(const std::filesystem::path& path,
    external_player_preferences_t& preferences) -> std::string;
[[nodiscard]] auto load_program_preferences_for_startup(const std::filesystem::path& path,
    const std::filesystem::path& legacy_player_path) -> preferences_load_result_t;

} // namespace wormedit

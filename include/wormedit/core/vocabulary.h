#pragma once

// Core vocabulary both sides of the frontend boundary may use (code-style.md, "Frontend
// boundary"): enums, strong IDs, rationals and small pure queries over them. Documents, history,
// indices and editor state stay out, so the frontend and C++ view types can include this alone.

#include <wormedit/core/id.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace wormedit {
enum class bookmark_sort_t { custom, position, name };
struct bookmark_reorder_t {
    object_id_t bookmark;
    object_id_t target;
    bool place_after{};
    bookmark_sort_t sort{bookmark_sort_t::custom};
    bool descending{};
    auto operator==(const bookmark_reorder_t&) const -> bool = default;
};

enum class project_setting_field_t { default_snap, audio_directory, visual_directory };
inline constexpr std::uint32_t maximum_default_lane_count = 10;

inline constexpr std::size_t maximum_batch_chart_metadata_charts = 256;
inline constexpr std::size_t maximum_quantization_presets = 256;
inline constexpr std::size_t maximum_external_players = 64;
inline constexpr std::size_t maximum_preference_string_bytes = 4096;
inline constexpr std::size_t maximum_project_path_bytes = 4096;
inline constexpr std::size_t maximum_recovery_destination_bytes = 511;

// Instrument color class; append only because native projects store the integer value.
enum class lane_key_color_t {
    generic, white, black, scratch, foot_pedal, free_zone,
    pms_white, pms_yellow, pms_green, pms_blue, pms_red,
};

struct signed_rational_t {
    std::int64_t numerator{};
    std::uint64_t denominator{1};

    auto operator==(const signed_rational_t&) const -> bool = default;
};

enum class note_kind_t {
    tap,
    hold,
    roll,
    invisible,
    mine,
    lift,
    fake,
};

enum class visual_event_kind_t {
    background,
    poor,
    layer,
    video,
    storyboard,
    layer2,
};

enum class resource_role_t {
    sample,
    bgm,
    bga,
    music,
    preview,
};

// How the user classifies a sound in the Sounds tab. Unspecified keeps the
// kind implied by the resource roles; it never affects exported files.
enum class sound_kind_t : std::uint8_t {
    unspecified,
    stem,
    keysound,
    sample,
};

// What moving a bar line does to the content after it. Bar-line edits are presentational by
// default, so everything keeps its song time; propagation is an explicit choice (open question 19).
enum class bar_line_time_t { keep_song_time, propagate };

enum class editor_selection_object_kind_t {
    note,
    bgm_event,
    bpm_event,
    stop_event,
    delay_event,
    warp_event,
    scroll_event,
    speed_event,
    kiai_event,
    timing_anchor,
    bga_event,
    bookmark,
    section,
    // The bar line that starts a measure, keyed by that measure's ID. The first measure has none.
    bar_line,
};

// Four editor lanes: background (including video), layer, layer 2, poor.
inline constexpr std::uint8_t editor_bga_lane_mask_all = 15;
[[nodiscard]] auto editor_bga_lane_index(visual_event_kind_t kind)
    -> std::optional<std::size_t>;

enum class format_family_t {
    native_project,
    bms,
    stepmania,
    osu_mania,
    bmson,
    midi,
    quaver,
};

enum class quantization_space_t {
    standard_measure,
    authored_measure,
};

enum class quantization_tie_t {
    earlier,
    later,
    reject,
};

enum class quantization_hold_t {
    start_only,
    both_endpoints,
};

enum class text_encoding_t {
    unknown,
    ascii,
    utf8,
    cp932,
    euc_kr,
    cp949,
    windows_1252,
};

enum class lane_role_t {
    playable,
    scratch,
    autoplay,
    invisible,
    mine,
    free_zone,
    foot_pedal,
};

enum class lane_glyph_t {
    bar,
    square,
    arrow_left,
    arrow_down,
    arrow_up,
    arrow_right,
    // Appended after the cardinal arrows so stored values keep their meaning.
    arrow_up_left,
    arrow_up_right,
    arrow_down_left,
    arrow_down_right,
    arrow_center,
};

enum class pause_unit_t {
    seconds,
    milliseconds,
    bms_stop_ticks,
    beats,
};

enum class existing_files_policy_t {
    ask,
    replace,
    keep,
};

enum class output_destination_kind_t {
    directory,
    file,
};

enum class stepmania_profile_t {
    sm,
    ssc,
};

enum class stepmania_timing_syntax_t {
    unspecified,
    native_warps,
    legacy_negative_bpm,
    legacy_negative_stop,
};

enum class stepmania_implementation_profile_t {
    unspecified,
    stepmania_5_1,
    etterna_0_75_1,
};

struct stepmania_export_policy_t {
    stepmania_profile_t file_profile{stepmania_profile_t::ssc};
    stepmania_timing_syntax_t timing_syntax{stepmania_timing_syntax_t::native_warps};
    stepmania_implementation_profile_t implementation_profile{
        stepmania_implementation_profile_t::unspecified};
    auto operator==(const stepmania_export_policy_t&) const -> bool = default;
};

enum class workspace_tab_t : std::uint8_t {
    setup,
    metadata,
    timing,
    compose,
    storyboard,
    audio_slices,
    compare_validate,
};

enum class notification_severity_t : std::uint8_t {
    hint,
    info,
    success,
    warning,
    error,
};

enum class notification_source_t : std::uint8_t {
    document,
    edit,
    audio,
    export_,
    import_,
    recovery,
    format,
    input,
    system,
};

enum class timing_lane_kind_t {
    bpm,
    scroll,
    speed,
    stop,
    delay,
    warp,
    kiai,
    anchor,
};

enum class editor_grid_line_kind_t {
    active_snap,
    beat,
    group,
    measure,
};

enum class editor_grid_emphasis_t {
    subtle,
    normal,
    strong,
    strongest,
};

enum class player_preview_skin_t : std::uint8_t {
    neutral,
    mania,
    arrows,
};

enum class native_project_recovery_action_t {
    inspect_read_only,
    recover_into_session,
    save_as_new_project,
    discard,
    quarantine,
};

inline constexpr std::size_t timing_lane_kind_count = static_cast<std::size_t>(timing_lane_kind_t::anchor) + 1;

enum class sound_usage_kind_t : std::uint8_t {
    note,
    bgm,
    sound_channel_event,
    sound_channel_source,
};

enum class sound_usage_scope_t : std::uint8_t { resource_reference, shared_asset_reference };

enum class stage_group_t {
    none,
    left,
    right,
};

// SV and scrolls are one family: both multiply the chart's scroll events. Stored value 1 was a
// separate `scrolls` output until schema 21 folded it into `sv`; the gap keeps `speeds` stable.
enum class sv_recipe_output_t {
    sv = 0,
    speeds = 2,
};

[[nodiscard]] constexpr auto is_known_sv_recipe_output(sv_recipe_output_t output) noexcept -> bool {
    return output == sv_recipe_output_t::sv || output == sv_recipe_output_t::speeds;
}

inline constexpr std::size_t maximum_sv_recipe_postprocess_stages = 32;
inline constexpr std::size_t maximum_sv_recipe_impulses = 256;
enum class sv_recipe_postprocess_kind_t { multiply, normalize, balance_bump };
enum class sv_recipe_normalization_t { preserve_displacement, preserve_distance, preserve_average };
enum class sv_recipe_bump_solve_t { y, x };

enum class sv_recipe_lua_mode_t { pure, ordered };

enum class sv_recipe_sampling_mode_t {
    fixed,
    adaptive,
};

enum class sv_recipe_curve_t {
    constant,
    linear,
    exponential,
    bump,
    // Reserved for Lua expression curves (see docs/osbx-draft-spec.md): the recipe's relative value
    // is a Lua function of the normalized position t in [0, 1], kept in expression_source. The
    // source is stored and round-tripped, but nothing evaluates it yet: sampling and export report
    // an unsupported-feature diagnostic instead of guessing.
    lua_expression,
};

enum class note_move_conflict_t { none, occupied, out_of_lane, out_of_chart, other };

} // namespace wormedit

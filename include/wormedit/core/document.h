#pragma once

#include <wormedit/core/diagnostic.h>
#include <wormedit/core/result.h>
#include <wormedit/core/measure.h>
#include <wormedit/core/storyboard.h>
#include <wormedit/core/vocabulary.h>

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace wormedit {

struct chart_document_t;

inline constexpr std::size_t default_maximum_chart_objects = 1'000'000;

struct color_rgba_t {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255};

    auto operator==(const color_rgba_t&) const -> bool = default;
};

enum class format_family_t {
    native_project,
    bms,
    stepmania,
    osu_mania,
    bmson,
    midi,
    quaver,
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

struct source_provenance_t {
    format_family_t family{format_family_t::native_project};
    std::string profile;
    std::string source_field;
    std::string source_spelling;
    std::size_t source_order{};
};

struct encoding_provenance_t {
    text_encoding_t detected{text_encoding_t::unknown};
    std::optional<text_encoding_t> user_override;
    // Imported source can be shared by parse results and song/chart provenance without
    // multiplying the retained file-sized allocation. A null pointer means no source bytes.
    std::shared_ptr<const std::vector<std::byte>> original_bytes;
    bool unchanged_since_import{false};
};

struct local_position_t {
    measure_id_t measure;
    std::uint64_t row{};

    auto operator==(const local_position_t&) const -> bool = default;
};

struct position_span_t {
    local_position_t begin;
    local_position_t end;

    auto operator==(const position_span_t&) const -> bool = default;
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

enum class stage_group_t {
    none,
    left,
    right,
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

struct midi_destination_t {
    std::uint8_t channel{};
    std::uint8_t note{};
    auto operator==(const midi_destination_t&) const -> bool = default;
};

struct bms_channel_map_t {
    [[nodiscard]] static auto from(std::string_view visible) -> bms_channel_map_t;

    std::optional<std::string> visible;
    std::optional<std::string> invisible;
    std::optional<std::string> long_note;
    std::optional<std::string> mine;
    auto operator==(const bms_channel_map_t&) const -> bool = default;
};

// Instrument color class of a lane. Appended-only: the integer value is stored
// in native projects.
enum class lane_key_color_t {
    generic,
    white,
    black,
    scratch,
    foot_pedal,
    free_zone,
    pms_white,
    pms_yellow,
    pms_green,
    pms_blue,
    pms_red,
};

struct lane_definition_t {
    [[nodiscard]] static auto from(std::uint64_t id, std::uint32_t column, stage_group_t stage)
        -> lane_definition_t;

    lane_id_t id;
    std::string name;
    lane_role_t role{lane_role_t::playable};
    stage_group_t stage{stage_group_t::none};
    std::uint32_t display_order{};
    lane_glyph_t glyph{lane_glyph_t::bar};
    bms_channel_map_t bms_channels;
    std::optional<midi_destination_t> midi_destination;
    lane_key_color_t key_color{lane_key_color_t::generic};
    auto operator==(const lane_definition_t&) const -> bool = default;
    [[nodiscard]] auto is_editable() const noexcept -> bool;
};

struct lane_layout_t {
    std::string profile_id;
    std::string display_name;
    std::vector<lane_definition_t> lanes;
    bool split_stage{false};
    std::optional<std::uint32_t> split_after_column;
    bool bmson_generic_keys_only{false};
    auto operator==(const lane_layout_t&) const -> bool = default;
    [[nodiscard]] auto find(lane_id_t id) const -> const lane_definition_t*;
    [[nodiscard]] auto find(lane_id_t id) -> lane_definition_t*;
    [[nodiscard]] auto has_lane(lane_id_t id) const -> bool;
    // Branch reprojection compares source lane shape, ignoring generated IDs/presentation.
    [[nodiscard]] auto has_same_shape(const lane_layout_t& other) const -> bool;
};

struct lane_layout_segment_t {
    object_id_t id;
    local_position_t position;
    lane_layout_t layout;
};

enum class physical_key_t {
    unknown,
    digit_1,
    digit_2,
    digit_3,
    digit_4,
    digit_5,
    digit_6,
    digit_7,
    digit_8,
    digit_9,
    digit_0,
    key_q,
    key_w,
    key_e,
    key_r,
    key_t,
    key_y,
    key_u,
    key_i,
    key_o,
    key_p,
    key_a,
    key_s,
    key_d,
    key_f,
    key_g,
    key_h,
    key_j,
    key_k,
    key_l,
    key_z,
    key_x,
    key_c,
    key_v,
    key_b,
    key_n,
    key_m,
};

// USB HID keyboard usage of each physical key (SDL scancodes use the same numbers), indexed by
// physical_key_t; unknown is 0.
inline constexpr std::array<std::uint16_t, 37> physical_key_hid_usages{
    0x00, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,      // digits 1-9, 0
    0x14, 0x1a, 0x08, 0x15, 0x17, 0x1c, 0x18, 0x0c, 0x12, 0x13,            // Q W E R T Y U I O P
    0x04, 0x16, 0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f,                  // A S D F G H J K L
    0x1d, 0x1b, 0x06, 0x19, 0x05, 0x11, 0x10,                              // Z X C V B N M
};
static_assert(physical_key_hid_usages.size() == static_cast<std::size_t>(physical_key_t::key_m) + 1);

[[nodiscard]] constexpr auto physical_key_hid_usage(physical_key_t key) noexcept -> std::uint16_t {
    const auto index = static_cast<std::size_t>(key);
    return index < physical_key_hid_usages.size() ? physical_key_hid_usages[index] : 0;
}

struct lane_binding_t {
    physical_key_t key{physical_key_t::unknown};
    lane_id_t lane;
    std::optional<std::uint32_t> focused_stage;
    auto operator==(const lane_binding_t&) const -> bool = default;
};

struct input_profile_t {
    [[nodiscard]] static auto with_defaults(const chart_document_t& chart,
                                            const lane_layout_t& layout)
        -> std::optional<input_profile_t>;

    std::string profile_id;
    std::vector<lane_binding_t> bindings;
    std::uint32_t stage_count{1};
    bool requires_focused_stage{false};
    auto operator==(const input_profile_t&) const -> bool = default;
};

struct semantic_layer_t {
    layer_id_t id;
    std::string name;
    color_rgba_t color;
    bool visible{true};
    bool locked{false};
};

struct note_t {
    object_id_t id;
    local_position_t position;
    lane_id_t lane;
    note_kind_t kind{note_kind_t::tap};
    std::optional<local_position_t> end;
    std::optional<resource_id_t> sound;
    // Stable link to the retained BMSON sound-channel event that materialized
    // this semantic note. Ordinary BMS/native notes may leave it empty.
    std::optional<object_id_t> sound_channel_event;
    std::vector<layer_id_t> semantic_layers;
    std::optional<std::string> label;
    std::optional<branch_id_t> branch;
    source_provenance_t provenance;
};

struct bgm_event_t {
    object_id_t id;
    local_position_t position;
    object_id_t source_lane;
    // Empty BMS channel objects still occupy a source slot, so imported events
    // may have no sound binding while retaining their structural position.
    std::optional<resource_id_t> sound;
    // Stable link to the retained BMSON sound-channel event that materialized
    // this semantic autoplay event. Ordinary BMS events may leave it empty.
    std::optional<object_id_t> sound_channel_event;
    std::size_t structural_slot{};
    std::optional<branch_id_t> branch;
    source_provenance_t provenance;
    std::vector<layer_id_t> semantic_layers;
    std::optional<local_position_t> end{};
};

struct measure_chunk_t {
    measure_id_t id;
    measure_definition_t definition{{1, 1}, meter_t{4, 4}};
    std::vector<note_t> notes;
    std::vector<bgm_event_t> bgm_events;
};

// BMS measure lengths are control-flow-sensitive. This records which source
// declaration supplied the currently materialized measure length so an exporter
// can preserve the full source tree without mistaking a branch projection for a
// root-level edit.
struct bms_measure_length_projection_t {
    std::size_t measure_index{};
    std::size_t source_order{};
    rational_t imported_length{};
};

struct bgm_source_lane_t {
    object_id_t id;
    std::string name;
    std::size_t source_order{};
    std::optional<object_id_t> stem_channel{};
};

struct bpm_event_t {
    object_id_t id;
    local_position_t position;
    signed_rational_t beats_per_minute;
    source_provenance_t provenance;
};

enum class pause_kind_t {
    stop,
    delay,
};

enum class pause_unit_t {
    seconds,
    milliseconds,
    bms_stop_ticks,
    beats,
};

struct pause_event_t {
    object_id_t id;
    local_position_t position;
    pause_kind_t kind{pause_kind_t::stop};
    signed_rational_t duration;
    pause_unit_t source_unit{pause_unit_t::seconds};
    source_provenance_t provenance;
};

// BMS timing declarations and channel rows may be distributed across root and
// conditional scopes. Imported events keep an explicit semantic baseline so a
// full-tree writer can retain those source statements without silently ignoring
// a later edit to the materialized projection.
struct bms_bpm_projection_t {
    object_id_t event;
    std::size_t source_order{};
    local_position_t imported_position;
    signed_rational_t imported_beats_per_minute;
};

struct bms_pause_projection_t {
    object_id_t event;
    std::size_t source_order{};
    local_position_t imported_position;
    pause_kind_t imported_kind{pause_kind_t::stop};
    signed_rational_t imported_duration;
    pause_unit_t imported_source_unit{pause_unit_t::seconds};
};

enum class warp_provenance_t {
    native,
    negative_bpm,
    negative_stop,
};

struct warp_event_t {
    object_id_t id;
    position_span_t span;
    warp_provenance_t source_encoding{warp_provenance_t::native};
    source_provenance_t provenance;
};

struct scroll_event_t {
    object_id_t id;
    local_position_t position;
    signed_rational_t multiplier{1, 1};
    source_provenance_t provenance;
};

struct speed_event_t {
    object_id_t id;
    local_position_t position;
    signed_rational_t multiplier{1, 1};
    rational_t transition_beats{};
    bool transition_is_seconds{false};
    source_provenance_t provenance;
};

// Parametric SV recipes are authored chart data. Sampled control points are
// deliberately not part of the document model: they are derived preview/export
// data and must never be persisted as ordinary scroll/speed events.
enum class sv_recipe_output_t {
    sv,
    scrolls,
    speeds,
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

enum class sv_recipe_sampling_mode_t {
    fixed,
    adaptive,
};

struct sv_recipe_parameters_t {
    // constant uses value_from; linear/exponential use value_from/value_to;
    // exponential also uses shape. Bump uses bump_start/bump_length and
    // bump_x/bump_y, all in normalized [0, 1] interval coordinates as relevant.
    signed_rational_t value_from{1, 1};
    signed_rational_t value_to{1, 1};
    signed_rational_t shape{};
    signed_rational_t bump_start{};
    signed_rational_t bump_length{1, 2};
    signed_rational_t bump_x{2, 1};
    signed_rational_t bump_y{1, 1};

    auto operator==(const sv_recipe_parameters_t&) const -> bool = default;
};

struct sv_recipe_sampling_t {
    sv_recipe_sampling_mode_t mode{sv_recipe_sampling_mode_t::fixed};
    // Exactly one of fixed_point_count and fixed_step_rows is set in fixed
    // mode. Adaptive mode uses tolerance and max_points.
    std::optional<std::uint32_t> fixed_point_count{2};
    std::optional<std::uint64_t> fixed_step_rows;
    signed_rational_t tolerance{1, 1000};
    std::uint32_t max_points{256};

    auto operator==(const sv_recipe_sampling_t&) const -> bool = default;
};

struct sv_recipe_t {
    recipe_id_t id;
    std::string name;
    // Half-open interval. The end may be the one-past row of the final
    // measure, representing the exact exclusive chart boundary; no point
    // event may otherwise use that sentinel position.
    position_span_t range;
    sv_recipe_output_t output{sv_recipe_output_t::sv};
    sv_recipe_curve_t curve{sv_recipe_curve_t::linear};
    sv_recipe_parameters_t parameters;
    sv_recipe_sampling_t sampling;
    // Lua source for the lua_expression curve; empty for every other curve.
    std::string expression_source;
    // StepMania SPEEDS transition behavior. It is ignored for SV/SCROLLS.
    std::optional<rational_t> transition_duration;
    std::optional<bool> transition_is_seconds;
    bool enabled{true};

    auto operator==(const sv_recipe_t&) const -> bool = default;
};

struct kiai_event_t {
    object_id_t id;
    local_position_t position;
    bool enabled{false};
    std::uint32_t retained_effect_bits{};
    source_provenance_t provenance;
};

struct timing_anchor_t {
    object_id_t id;
    local_position_t position;
    std::uint64_t source_sample_frame{};
    bool locked{false};
};

enum class marker_anchor_t {
    beat,
    audio_time,
};

struct bookmark_t {
    object_id_t id;
    std::string label;
    marker_anchor_t anchor{marker_anchor_t::beat};
    std::optional<local_position_t> position;
    std::optional<std::int64_t> audio_milliseconds;
};

struct section_t {
    object_id_t id;
    std::string label;
    marker_anchor_t anchor{marker_anchor_t::beat};
    std::optional<position_span_t> beat_span;
    std::optional<std::pair<std::int64_t, std::int64_t>> audio_millisecond_span;
};

struct bga_event_t {
    object_id_t id;
    local_position_t position;
    resource_id_t resource;
    visual_event_kind_t kind{visual_event_kind_t::background};
    source_provenance_t provenance;
};

enum class asset_kind_t {
    audio,
    image,
    video,
    storyboard,
    midi,
};

struct audio_stream_info_t {
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
    std::optional<std::uint64_t> frame_count;
    bool seekable{false};
};

struct audio_asset_t {
    asset_id_t id;
    std::filesystem::path source_path;
    audio_stream_info_t stream;
    std::optional<std::filesystem::path> waveform_cache_path;
};

struct visual_asset_t {
    asset_id_t id;
    asset_kind_t kind{asset_kind_t::image};
    std::filesystem::path source_path;
    std::optional<std::uint32_t> width;
    std::optional<std::uint32_t> height;
};

enum class resource_role_t {
    sample,
    bgm,
    bga,
    music,
    preview,
};

struct target_resource_identity_t {
    format_family_t family{format_family_t::native_project};
    std::string identifier;
    // Format-specific namespace, required for BMS WAV versus BMP IDs.
    std::string namespace_name;
    auto operator==(const target_resource_identity_t&) const -> bool = default;
};

inline constexpr std::size_t maximum_sound_tags = 32;
inline constexpr std::size_t maximum_sound_tag_bytes = 64;

struct sound_group_t {
    sound_group_id_t id;
    std::string name;
    auto operator==(const sound_group_t&) const -> bool = default;
};

struct resource_entry_t {
    [[nodiscard]] auto has_role(resource_role_t role) const -> bool;
    resource_id_t id;
    std::string name;
    asset_id_t asset;
    std::vector<resource_role_t> roles;
    std::vector<target_resource_identity_t> target_identities;
    bool missing{false};
    source_provenance_t provenance;
    sound_kind_t sound_kind{sound_kind_t::unspecified};
    // Free-text user tags: trimmed, non-empty, unique ignoring ASCII case.
    std::vector<std::string> tags;
    std::optional<sound_group_id_t> sound_group;
};

struct audio_clip_t {
    clip_id_t id;
    asset_id_t source;
    std::uint64_t begin_frame{};
    std::optional<std::uint64_t> end_frame;
};

struct sound_channel_event_t {
    object_id_t id;
    local_position_t position;
    std::optional<local_position_t> end;
    lane_id_t lane;
    bool continuation{false};
    bool playable{true};
    source_provenance_t provenance;
};

struct sound_channel_t {
    object_id_t id;
    std::string name;
    audio_clip_t source_clip;
    std::vector<sound_channel_event_t> events;
};

enum class bms_control_kind_t {
    root,
    raw_statement,
    random,
    set_random,
    if_group,
    if_branch,
    else_if_branch,
    else_branch,
    switch_block,
    set_switch,
    case_branch,
    default_branch,
    skip,
};

inline auto has_control_value(bms_control_kind_t kind) -> bool {
    switch (kind) {
    case bms_control_kind_t::random:
    case bms_control_kind_t::set_random:
    case bms_control_kind_t::if_branch:
    case bms_control_kind_t::else_if_branch:
    case bms_control_kind_t::switch_block:
    case bms_control_kind_t::set_switch:
    case bms_control_kind_t::case_branch:
        return true;
    case bms_control_kind_t::root:
    case bms_control_kind_t::raw_statement:
    case bms_control_kind_t::if_group:
    case bms_control_kind_t::else_branch:
    case bms_control_kind_t::default_branch:
    case bms_control_kind_t::skip:
        return false;
    }
    return false;
}


inline auto is_valid_control_kind(bms_control_kind_t kind) -> bool {
    switch (kind) {
    case bms_control_kind_t::root:
    case bms_control_kind_t::raw_statement:
    case bms_control_kind_t::random:
    case bms_control_kind_t::set_random:
    case bms_control_kind_t::if_group:
    case bms_control_kind_t::if_branch:
    case bms_control_kind_t::else_if_branch:
    case bms_control_kind_t::else_branch:
    case bms_control_kind_t::switch_block:
    case bms_control_kind_t::set_switch:
    case bms_control_kind_t::case_branch:
    case bms_control_kind_t::default_branch:
    case bms_control_kind_t::skip:
        return true;
    }
    return false;
}

inline auto is_conditional_branch(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::if_branch || kind == bms_control_kind_t::else_if_branch ||
           kind == bms_control_kind_t::else_branch;
}

inline auto can_have_control_close(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::random || kind == bms_control_kind_t::if_group ||
           kind == bms_control_kind_t::switch_block;
}

inline auto is_switch_branch(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::case_branch || kind == bms_control_kind_t::default_branch;
}

inline auto is_choice_control(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::random || kind == bms_control_kind_t::switch_block;
}

inline auto is_branch_boundary(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::random || kind == bms_control_kind_t::if_group ||
           kind == bms_control_kind_t::if_branch || kind == bms_control_kind_t::else_if_branch ||
           kind == bms_control_kind_t::else_branch || kind == bms_control_kind_t::switch_block ||
           kind == bms_control_kind_t::case_branch || kind == bms_control_kind_t::default_branch;
}

inline auto is_branch_body(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::if_branch || kind == bms_control_kind_t::else_if_branch ||
           kind == bms_control_kind_t::else_branch || kind == bms_control_kind_t::case_branch ||
           kind == bms_control_kind_t::default_branch;
}

inline auto is_branch_scope(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::if_group || kind == bms_control_kind_t::if_branch ||
           kind == bms_control_kind_t::else_if_branch || kind == bms_control_kind_t::else_branch ||
           kind == bms_control_kind_t::switch_block || kind == bms_control_kind_t::case_branch ||
           kind == bms_control_kind_t::default_branch;
}

enum class bms_line_ending_t {
    none,
    lf,
    crlf,
    cr,
};

struct bms_source_span_t {
    // Half-open offsets into the original encoded byte stream, including the line ending.
    std::size_t byte_begin{};
    std::size_t byte_end{};

    auto operator==(const bms_source_span_t&) const -> bool = default;
};

struct bms_control_node_t {
    branch_id_t id;
    bms_control_kind_t kind{bms_control_kind_t::raw_statement};
    std::optional<std::int64_t> value;
    std::string original_text;
    std::string command_token;
    std::string raw_value;
    bms_source_span_t source_span;
    std::size_t source_order{};
    bms_line_ending_t line_ending{bms_line_ending_t::none};
    std::optional<bms_source_span_t> close_source_span;
    std::optional<std::string> close_text;
    std::optional<std::size_t> close_source_order;
    std::optional<bms_line_ending_t> close_line_ending;
    std::vector<bms_control_node_t> children;
};

struct branch_selection_t {
    branch_id_t branch;
    std::int64_t selected_value{};
    auto operator==(const branch_selection_t&) const -> bool = default;
};

struct format_metadata_t {
    format_family_t family{format_family_t::native_project};
    std::vector<std::pair<std::string, std::string>> ordered_fields;
    // Optional parallel source-line identities for imported fields. Entries that
    // have no corresponding source order were authored after import.
    std::vector<std::size_t> ordered_field_source_orders;
    encoding_provenance_t encoding;
};

struct chart_metadata_t {
    std::string name;
    std::string author;
    std::optional<signed_rational_t> rating;
    std::vector<format_metadata_t> format_groups;

    [[nodiscard]] auto format_group_for(format_family_t family) -> format_metadata_t&;
};

// Layers a sound receives when it is painted onto this chart. Layer IDs are
// chart-local, so the mapping lives on the chart rather than on the shared
// catalog entry. Entries are unique per resource and never empty.
struct sound_layer_default_t {
    resource_id_t resource;
    std::vector<layer_id_t> layers;
    auto operator==(const sound_layer_default_t&) const -> bool = default;
};

struct chart_indices_t;
struct document_history_t;

class chart_indices_slot_t {
    friend struct document_history_t;
    // Only the main-loop history fills this slot. Workers copy snapshots and
    // share already-built immutable values; they never fill their copied slots.
    mutable std::shared_ptr<const chart_indices_t> value_;
};

// Identity domains stay typed; only the uniqueness verdict is shared. Counts may come
// from a collection, the chart traversal, or an already built index.
struct target_query_messages_t {
    std::string_view missing;
    std::string_view ambiguous;
    // Empty means zero is an ordinary ID in this domain.
    std::string_view reserved_zero{};
    std::string_view domain{};
    // Preserve the two existing diagnostic grammars, including their zero policy.
    [[nodiscard]] static constexpr auto from_event(std::string_view domain)
        -> target_query_messages_t {
        return {.missing = " command refers to an unknown event",
                .ambiguous = " event ID is ambiguous because it appears more than once",
                .domain = domain};
    }
    [[nodiscard]] static constexpr auto from_object(std::string_view domain)
        -> target_query_messages_t {
        return {.missing = " command refers to an unknown object",
                .ambiguous = " ID is ambiguous because it appears more than once",
                .reserved_zero = " command uses the reserved zero object ID",
                .domain = domain};
    }
    auto operator==(const target_query_messages_t&) const -> bool = default;
};

struct target_identity_query_t {
    std::size_t count{};
    bool is_zero{};
    auto operator==(const target_identity_query_t&) const -> bool = default;

    template <class Id>
    [[nodiscard]] static auto from(Id id, std::size_t count) -> target_identity_query_t {
        return {.count = count, .is_zero = !id};
    }
    template <class Value, class Id>
    [[nodiscard]] static auto from(const std::vector<Value>& values, Id id)
        -> target_identity_query_t {
        return from(id, std::ranges::count(values, id, &Value::id));
    }
    [[nodiscard]] auto validate(diagnostic_list_t& diagnostics,
                                 const target_query_messages_t& messages) const -> bool;
};

enum class target_uniqueness_scope_t { collection, chart_objects };
enum class target_query_order_t { kind_first, identity_first };

struct target_query_policy_t {
    target_query_messages_t messages;
    target_uniqueness_scope_t scope{target_uniqueness_scope_t::collection};
    target_query_order_t order{target_query_order_t::kind_first};
    auto operator==(const target_query_policy_t&) const -> bool = default;
};

struct chart_document_t {
    chart_id_t id;
    chart_metadata_t metadata;
    format_family_t source_family{format_family_t::native_project};
    std::uint64_t divisions_per_standard_measure{default_divisions_per_standard_measure};
    signed_rational_t beat_zero_audio_offset_seconds{};
    std::vector<measure_chunk_t> measures;
    std::vector<lane_layout_segment_t> lane_layouts;
    input_profile_t input_profile;
    std::vector<semantic_layer_t> semantic_layers;
    std::vector<sound_layer_default_t> sound_layer_defaults;
    std::vector<bgm_source_lane_t> bgm_source_lanes;
    std::vector<bpm_event_t> bpms;
    std::vector<pause_event_t> pauses;
    std::vector<warp_event_t> warps;
    std::vector<scroll_event_t> scrolls;
    std::vector<speed_event_t> speeds;
    // Vector order is the stable composition order for enabled recipes.
    std::vector<sv_recipe_t> sv_recipes;
    std::vector<kiai_event_t> kiai;
    std::vector<timing_anchor_t> timing_anchors;
    std::vector<bookmark_t> bookmarks;
    std::vector<section_t> sections;
    std::vector<bga_event_t> bga_events;
    std::vector<sound_channel_t> sound_channels;
    std::vector<bms_measure_length_projection_t> bms_measure_length_projections;
    std::vector<bms_bpm_projection_t> bms_bpm_projections;
    std::vector<bms_pause_projection_t> bms_pause_projections;
    std::optional<bms_control_node_t> bms_control_tree;
    std::vector<branch_selection_t> active_branch_selection;
    // Derived, never serialized: number of addressable measures the virtual canvas
    // exposes for non-BMS charts. Kept in sync by sync_virtual_tails().
    std::uint64_t virtual_tail_measures{};
    // Derived, never serialized. Unchanged snapshot copies share the payload.
    chart_indices_slot_t indices_slot{};
    [[nodiscard]] auto query_object_id(object_id_t id) const -> target_identity_query_t;
    template <class Value>
    [[nodiscard]] auto unique_target(std::vector<Value> chart_document_t::*collection, decltype(Value::id) id,
        diagnostic_list_t& diagnostics, const target_query_policy_t& policy,
        std::string_view wrong_kind = {}) -> Value* {
        auto& values = this->*collection;
        auto found = values.end();
        auto identity = target_identity_query_t::from(id, 0);
        if constexpr (std::is_same_v<decltype(Value::id), object_id_t>) {
            if (policy.scope == target_uniqueness_scope_t::chart_objects) {
                if (policy.order == target_query_order_t::kind_first) {
                    found = std::ranges::find(values, id, &Value::id);
                    if (found == values.end()) {
                        (void)identity.validate(diagnostics, policy.messages);
                        return nullptr;
                    }
                }
                identity = query_object_id(id);
            } else {
                identity = target_identity_query_t::from(values, id);
            }
        } else {
            // Measures and layers retain their independent typed ID domains.
            identity = target_identity_query_t::from(values, id);
        }
        if (!identity.validate(diagnostics, policy.messages)) return nullptr;
        if (found == values.end()) found = std::ranges::find(values, id, &Value::id);
        if (found == values.end()) {
            const bool has_custom_message = !wrong_kind.empty();
            const target_query_messages_t missing_kind{
                .missing = has_custom_message ? wrong_kind : policy.messages.missing,
                .ambiguous = {},
                .domain = has_custom_message ? std::string_view{} : policy.messages.domain};
            (void)target_identity_query_t::from(id, 0).validate(diagnostics, missing_kind);
            return nullptr;
        }
        return &*found;
    }
    [[nodiscard]] auto find_note(object_id_t id) const -> const note_t*;
    [[nodiscard]] auto find_note(object_id_t id) -> note_t*;
    [[nodiscard]] auto find_bgm_event(object_id_t id) const -> const bgm_event_t*;
    [[nodiscard]] auto find_bgm_event(object_id_t id) -> bgm_event_t*;
    [[nodiscard]] auto find_measure(measure_id_t id) const -> const measure_chunk_t*;
    [[nodiscard]] auto find_measure(measure_id_t id) -> measure_chunk_t*;
};

struct midi_event_t {
    std::uint64_t tick{};
    std::uint8_t channel{};
    std::vector<std::byte> data;
};

struct midi_asset_t {
    asset_id_t id;
    std::filesystem::path source_path;
    std::uint16_t ticks_per_quarter{};
    std::vector<midi_event_t> events;
};

struct generator_parameter_t {
    std::string name;
    std::string serialized_value;
};

struct generator_recipe_t {
    recipe_id_t id;
    std::string api_version;
    std::filesystem::path source_path;
    std::vector<asset_id_t> input_assets;
    std::vector<generator_parameter_t> parameters;
    std::uint64_t seed{};
    std::string source_hash;
};

struct storyboard_document_t {
    storyboard_id_t id;
    std::string name;
    std::vector<std::string> retained_source_lines;
    std::optional<storyboard_document_model_t> model;
};

enum class existing_files_policy_t {
    ask,
    replace,
    keep,
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

struct output_group_t {
    std::string name;
    format_family_t family{format_family_t::native_project};
    std::vector<chart_id_t> charts;
    std::filesystem::path relative_output_path;
    existing_files_policy_t existing_files{existing_files_policy_t::ask};
    std::optional<stepmania_export_policy_t> stepmania;
};

struct song_metadata_t {
    std::string title;
    std::string artist;
    std::vector<std::string> ordered_search_tags;
    std::vector<format_metadata_t> format_groups;

    [[nodiscard]] auto format_group_for(format_family_t family) -> format_metadata_t&;
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

enum class quantization_collision_t {
    reject,
    allow,
};

enum class quantization_hold_t {
    start_only,
    both_endpoints,
};

struct quantization_preset_t {
    static constexpr std::size_t maximum_name_bytes = 64;
    static constexpr std::size_t maximum_count = 256;

    std::string name;
    std::uint64_t divisions{};
    quantization_space_t space{quantization_space_t::standard_measure};
    quantization_tie_t tie{quantization_tie_t::earlier};
    quantization_hold_t holds{quantization_hold_t::both_endpoints};
    auto operator==(const quantization_preset_t&) const -> bool = default;
};

struct song_document_t {
    std::uint32_t schema_version{8};
    song_id_t id;
    std::uint64_t revision{};
    song_metadata_t metadata;
    std::vector<audio_asset_t> audio_assets;
    std::vector<visual_asset_t> visual_assets;
    std::vector<resource_entry_t> resources;
    std::vector<sound_group_t> sound_groups;
    std::vector<midi_asset_t> midi_assets;
    std::vector<generator_recipe_t> generator_recipes;
    std::vector<storyboard_document_t> storyboards;
    std::vector<chart_document_t> charts;
    std::vector<output_group_t> output_groups;
    std::vector<quantization_preset_t> quantization_presets;
    [[nodiscard]] auto find_chart(chart_id_t id) const -> const chart_document_t*;
    [[nodiscard]] auto find_chart(chart_id_t id) -> chart_document_t*;
    [[nodiscard]] auto find_audio_asset(asset_id_t id) const -> const audio_asset_t*;
    [[nodiscard]] auto find_audio_asset(asset_id_t id) -> audio_asset_t*;
    // Longest known positive duration among linked Music/BGM resources.
    [[nodiscard]] auto longest_music_or_bgm_stream() const -> const audio_stream_info_t*;
    void reset_chart_indices() noexcept;
};

enum class warp_encoding_t {
    native,
    negative_bpm,
    negative_stop,
    reject,
};

enum class loss_policy_t {
    reject,
    warn,
    allow,
};

enum class branch_projection_t {
    preserve_tree,
    active_projection,
};

// Editor targets explicitly name their family as well as ID. Presence, even when empty,
// means selected objects only; the legacy objects list retains its chart-wide semantics.
struct quantize_selection_t {
    std::vector<object_id_t> notes;
    std::vector<object_id_t> bgm_events;
    auto operator==(const quantize_selection_t&) const -> bool = default;
};

enum class quantize_object_family_t { other, note, bgm_event };

struct quantize_request_t {
    std::uint64_t divisions{};
    quantization_space_t space{quantization_space_t::standard_measure};
    quantization_tie_t tie{quantization_tie_t::earlier};
    quantization_collision_t collisions{quantization_collision_t::reject};
    quantization_hold_t holds{quantization_hold_t::both_endpoints};
    bool allow_resolution_promotion{false};
    std::vector<object_id_t> objects;
    std::optional<quantize_selection_t> selection;
};

[[nodiscard]] auto error(diagnostic_code_t code, std::string message,
                         std::optional<object_id_t> object = std::nullopt) -> diagnostic_t;

struct conversion_statistics_t {
    std::size_t preserved_fields{};
    std::size_t approximated_fields{};
    std::size_t dropped_fields{};
    auto operator==(const conversion_statistics_t&) const -> bool = default;
};
using conversion_report_t = diagnosed_t<conversion_statistics_t>;

struct measure_index_entry_t {
    measure_id_t id;
    std::uint64_t absolute_begin_row{};
    std::uint64_t row_capacity{};

    [[nodiscard]] auto row_of(std::uint64_t local_row) const
        -> std::optional<std::uint64_t>;
};

enum class measure_chart_end_policy_t { reject, allow };

struct measure_index_t {
    [[nodiscard]] static auto from(const chart_document_t& chart)
        -> result_t<measure_index_t, diagnostic_list_t>;
    std::vector<measure_index_entry_t> entries;
    std::uint64_t total_rows{};

    [[nodiscard]] auto entry_for(measure_id_t id) const -> const measure_index_entry_t*;
    [[nodiscard]] auto row_of(local_position_t position,
        measure_chart_end_policy_t chart_end = measure_chart_end_policy_t::reject) const
        -> std::optional<std::uint64_t>;
    [[nodiscard]] auto position_at(std::uint64_t row,
        measure_chart_end_policy_t chart_end = measure_chart_end_policy_t::reject) const
        -> std::optional<local_position_t>;
};

// Call-local acceleration only at sites that already build a map. Borrows an
// index whose entries must remain alive and unchanged for the lookup's lifetime.
// Duplicate IDs retain their first entry, as the existing private maps do.
struct measure_lookup_t {
    const measure_index_t* index{};
    std::unordered_map<std::uint64_t, std::size_t> entry_indices;

    [[nodiscard]] static auto from(const measure_index_t& index) -> measure_lookup_t;
    [[nodiscard]] auto bucket_for(measure_id_t id) const -> std::optional<std::size_t>;
    [[nodiscard]] auto entry_for(measure_id_t id) const -> const measure_index_entry_t*;
    [[nodiscard]] auto row_of(local_position_t position,
        measure_chart_end_policy_t chart_end = measure_chart_end_policy_t::reject) const
        -> std::optional<std::uint64_t>;
};

enum class position_role_t {
    point,
    span_begin,
    span_end,
    note_end,
};

struct redivision_move_t {
    object_id_t object;
    position_role_t role{position_role_t::point};
    local_position_t before;
    local_position_t after;
    quantize_object_family_t family{quantize_object_family_t::other};
};

struct redivision_plan_t {
    [[nodiscard]] static auto from(const chart_document_t& chart,
                                  std::uint64_t target_divisions_per_standard_measure)
        -> diagnosed_t<redivision_plan_t>;

    std::optional<chart_document_t> chart;
    std::vector<redivision_move_t> moves;
};
[[nodiscard]] auto layout_at(const chart_document_t& chart, const measure_index_t& index,
    local_position_t position) -> const lane_layout_t*;
[[nodiscard]] auto layout_at(const chart_document_t& chart, const measure_index_t& index,
    std::uint64_t row) -> const lane_layout_t*;

[[nodiscard]] auto validate_input_profile(const lane_layout_t& layout,
                                          const input_profile_t& profile) -> diagnostic_list_t;
[[nodiscard]] auto validate_document(const song_document_t& song) -> diagnostic_list_t;

} // namespace wormedit

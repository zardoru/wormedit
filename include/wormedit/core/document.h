#pragma once

#include <wormedit/core/measure.h>
#include <wormedit/core/storyboard.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace wormedit {

template <typename tag_t> struct stable_id_t {
    std::uint64_t value{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return value != 0;
    }
    auto operator<=>(const stable_id_t&) const = default;
};

struct song_id_tag_t;
struct chart_id_tag_t;
struct measure_id_tag_t;
struct object_id_tag_t;
struct lane_id_tag_t;
struct layer_id_tag_t;
struct sound_group_id_tag_t;
struct resource_id_tag_t;
struct asset_id_tag_t;
struct clip_id_tag_t;
struct branch_id_tag_t;
struct recipe_id_tag_t;
struct storyboard_id_tag_t;

using song_id_t = stable_id_t<song_id_tag_t>;
using chart_id_t = stable_id_t<chart_id_tag_t>;
using measure_id_t = stable_id_t<measure_id_tag_t>;
using object_id_t = stable_id_t<object_id_tag_t>;
using lane_id_t = stable_id_t<lane_id_tag_t>;
using layer_id_t = stable_id_t<layer_id_tag_t>;
using sound_group_id_t = stable_id_t<sound_group_id_tag_t>;
using resource_id_t = stable_id_t<resource_id_tag_t>;
using asset_id_t = stable_id_t<asset_id_tag_t>;
using clip_id_t = stable_id_t<clip_id_tag_t>;
using branch_id_t = stable_id_t<branch_id_tag_t>;
using recipe_id_t = stable_id_t<recipe_id_tag_t>;
using storyboard_id_t = stable_id_t<storyboard_id_tag_t>;

struct id_allocator_t {
    std::uint64_t next_value{1};
};

template <typename id_t>
[[nodiscard]] auto allocate_id(id_allocator_t& allocator) -> std::optional<id_t> {
    if (allocator.next_value == 0) {
        return std::nullopt;
    }
    const auto value = allocator.next_value++;
    return id_t{value};
}

struct signed_rational_t {
    std::int64_t numerator{};
    std::uint64_t denominator{1};

    auto operator==(const signed_rational_t&) const -> bool = default;
};

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
};

struct bms_channel_map_t {
    std::optional<std::string> visible;
    std::optional<std::string> invisible;
    std::optional<std::string> long_note;
    std::optional<std::string> mine;
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
    lane_id_t id;
    std::string name;
    lane_role_t role{lane_role_t::playable};
    stage_group_t stage{stage_group_t::none};
    std::uint32_t display_order{};
    lane_glyph_t glyph{lane_glyph_t::bar};
    bms_channel_map_t bms_channels;
    std::optional<midi_destination_t> midi_destination;
    lane_key_color_t key_color{lane_key_color_t::generic};
};

struct lane_layout_t {
    std::string profile_id;
    std::string display_name;
    std::vector<lane_definition_t> lanes;
    bool split_stage{false};
    std::optional<std::uint32_t> split_after_column;
    bool bmson_generic_keys_only{false};
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

struct lane_binding_t {
    physical_key_t key{physical_key_t::unknown};
    lane_id_t lane;
    std::optional<std::uint32_t> focused_stage;
};

struct input_profile_t {
    std::string profile_id;
    std::vector<lane_binding_t> bindings;
    std::uint32_t stage_count{1};
    bool requires_focused_stage{false};
};

struct semantic_layer_t {
    layer_id_t id;
    std::string name;
    color_rgba_t color;
    bool visible{true};
    bool locked{false};
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

enum class visual_event_kind_t {
    background,
    poor,
    layer,
    video,
    storyboard,
    layer2,
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
};

// How the user classifies a sound in the Sounds tab. Unspecified keeps the
// kind implied by the resource roles; it never affects exported files.
enum class sound_kind_t : std::uint8_t {
    unspecified,
    stem,
    keysound,
    sample,
};

inline constexpr std::size_t maximum_sound_tags = 32;
inline constexpr std::size_t maximum_sound_tag_bytes = 64;

struct sound_group_t {
    sound_group_id_t id;
    std::string name;
    auto operator==(const sound_group_t&) const -> bool = default;
};

struct resource_entry_t {
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
};

// Layers a sound receives when it is painted onto this chart. Layer IDs are
// chart-local, so the mapping lives on the chart rather than on the shared
// catalog entry. Entries are unique per resource and never empty.
struct sound_layer_default_t {
    resource_id_t resource;
    std::vector<layer_id_t> layers;
    auto operator==(const sound_layer_default_t&) const -> bool = default;
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

struct output_group_t {
    std::string name;
    format_family_t family{format_family_t::native_project};
    std::vector<chart_id_t> charts;
    std::filesystem::path relative_output_path;
};

struct song_metadata_t {
    std::string title;
    std::string artist;
    std::vector<std::string> ordered_search_tags;
    std::vector<format_metadata_t> format_groups;
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
    std::uint32_t schema_version{5};
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

struct quantize_request_t {
    std::uint64_t divisions{};
    quantization_space_t space{quantization_space_t::standard_measure};
    quantization_tie_t tie{quantization_tie_t::earlier};
    quantization_collision_t collisions{quantization_collision_t::reject};
    quantization_hold_t holds{quantization_hold_t::both_endpoints};
    bool allow_resolution_promotion{false};
    std::vector<object_id_t> objects;
};

enum class diagnostic_severity_t {
    info,
    warning,
    error,
};

enum class diagnostic_code_t {
    duplicate_id,
    invalid_id,
    invalid_resolution,
    invalid_measure,
    unknown_measure,
    row_out_of_range,
    unknown_lane,
    invalid_hold,
    dangling_resource,
    dangling_asset,
    duplicate_binding,
    invalid_binding,
    invalid_stage,
    invalid_rational,
    collision,
    unsupported_feature,
    approximated_feature,
    dropped_feature,
};

struct diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    diagnostic_code_t code{diagnostic_code_t::invalid_id};
    std::string message;
    std::optional<object_id_t> object;
};

using diagnostic_list_t = std::vector<diagnostic_t>;

struct conversion_report_t {
    diagnostic_list_t diagnostics;
    std::size_t preserved_fields{};
    std::size_t approximated_fields{};
    std::size_t dropped_fields{};

    [[nodiscard]] auto has_errors() const -> bool;
};

struct measure_index_entry_t {
    measure_id_t id;
    std::uint64_t absolute_begin_row{};
    std::uint64_t row_capacity{};
};

struct measure_index_t {
    std::vector<measure_index_entry_t> entries;
    std::uint64_t total_rows{};
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
};

struct redivision_result_t {
    std::optional<chart_document_t> chart;
    std::vector<redivision_move_t> moves;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return chart.has_value();
    }
};

[[nodiscard]] auto build_measure_index(const chart_document_t& chart)
    -> std::variant<measure_index_t, diagnostic_list_t>;
[[nodiscard]] auto absolute_row(const measure_index_t& index, local_position_t position)
    -> std::optional<std::uint64_t>;
[[nodiscard]] auto validate_input_profile(const lane_layout_t& layout,
                                          const input_profile_t& profile) -> diagnostic_list_t;
[[nodiscard]] auto validate_document(const song_document_t& song) -> diagnostic_list_t;
[[nodiscard]] auto redivide_chart_exact(const chart_document_t& chart,
                                        std::uint64_t target_divisions_per_standard_measure)
    -> redivision_result_t;

} // namespace wormedit

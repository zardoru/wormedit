#pragma once

#include <wormedit/core/measure.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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
    std::vector<std::byte> original_bytes;
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

struct lane_definition_t {
    lane_id_t id;
    std::string name;
    lane_role_t role{lane_role_t::playable};
    stage_group_t stage{stage_group_t::none};
    std::uint32_t display_order{};
    lane_glyph_t glyph{lane_glyph_t::bar};
    bms_channel_map_t bms_channels;
    std::optional<midi_destination_t> midi_destination;
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
    std::vector<layer_id_t> semantic_layers;
    std::optional<std::string> label;
    std::optional<branch_id_t> branch;
    source_provenance_t provenance;
};

struct bgm_event_t {
    object_id_t id;
    local_position_t position;
    object_id_t source_lane;
    resource_id_t sound;
    std::size_t structural_slot{};
    std::optional<branch_id_t> branch;
    source_provenance_t provenance;
};

struct measure_chunk_t {
    measure_id_t id;
    measure_definition_t definition{{1, 1}, meter_t{4, 4}};
    std::vector<note_t> notes;
    std::vector<bgm_event_t> bgm_events;
};

struct bgm_source_lane_t {
    object_id_t id;
    std::string name;
    std::size_t source_order{};
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
};

struct resource_entry_t {
    resource_id_t id;
    std::string name;
    asset_id_t asset;
    std::vector<resource_role_t> roles;
    std::vector<target_resource_identity_t> target_identities;
    bool missing{false};
    source_provenance_t provenance;
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
    if_branch,
    else_if_branch,
    else_branch,
    switch_block,
    case_branch,
    default_branch,
    skip,
};

struct bms_control_node_t {
    branch_id_t id;
    bms_control_kind_t kind{bms_control_kind_t::raw_statement};
    std::optional<std::int64_t> value;
    std::string original_text;
    std::vector<bms_control_node_t> children;
};

struct branch_selection_t {
    branch_id_t branch;
    std::int64_t selected_value{};
};

struct format_metadata_t {
    format_family_t family{format_family_t::native_project};
    std::vector<std::pair<std::string, std::string>> ordered_fields;
    encoding_provenance_t encoding;
};

struct chart_metadata_t {
    std::string name;
    std::string author;
    std::optional<signed_rational_t> rating;
    std::vector<format_metadata_t> format_groups;
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
    std::vector<bgm_source_lane_t> bgm_source_lanes;
    std::vector<bpm_event_t> bpms;
    std::vector<pause_event_t> pauses;
    std::vector<warp_event_t> warps;
    std::vector<scroll_event_t> scrolls;
    std::vector<speed_event_t> speeds;
    std::vector<kiai_event_t> kiai;
    std::vector<timing_anchor_t> timing_anchors;
    std::vector<bookmark_t> bookmarks;
    std::vector<section_t> sections;
    std::vector<bga_event_t> bga_events;
    std::vector<sound_channel_t> sound_channels;
    std::optional<bms_control_node_t> bms_control_tree;
    std::vector<branch_selection_t> active_branch_selection;
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

struct song_document_t {
    std::uint32_t schema_version{1};
    song_id_t id;
    std::uint64_t revision{};
    song_metadata_t metadata;
    std::vector<audio_asset_t> audio_assets;
    std::vector<visual_asset_t> visual_assets;
    std::vector<resource_entry_t> resources;
    std::vector<midi_asset_t> midi_assets;
    std::vector<generator_recipe_t> generator_recipes;
    std::vector<storyboard_document_t> storyboards;
    std::vector<chart_document_t> charts;
    std::vector<output_group_t> output_groups;
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

struct quantize_request_t {
    std::uint64_t divisions{};
    quantization_space_t space{quantization_space_t::standard_measure};
    quantization_tie_t tie{quantization_tie_t::earlier};
    quantization_collision_t collisions{quantization_collision_t::reject};
    quantization_hold_t holds{quantization_hold_t::both_endpoints};
    bool allow_resolution_promotion{false};
    std::vector<object_id_t> objects;
};

struct conversion_request_t {
    format_family_t target{format_family_t::native_project};
    std::string target_profile;
    std::optional<chart_id_t> chart;
    warp_encoding_t warp_encoding{warp_encoding_t::native};
    branch_projection_t branches{branch_projection_t::preserve_tree};
    std::optional<text_encoding_t> encoding;
    std::optional<quantize_request_t> quantization;
    loss_policy_t loss_policy{loss_policy_t::reject};
    bool overwrite{false};
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

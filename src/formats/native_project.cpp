#include "../core/text_util.h"
#include <wormedit/formats/native_project.h>

#include <wormedit/core/lane_profiles.h>

#include "native_project_migrations.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace nlohmann {

template <typename value_t> struct adl_serializer<std::optional<value_t>> {
    static void to_json(json& output, const std::optional<value_t>& value) {
        output = value.has_value() ? json(*value) : json(nullptr);
    }

    static void from_json(const json& input, std::optional<value_t>& value) {
        if (input.is_null()) {
            value.reset();
        } else {
            value = input.get<value_t>();
        }
    }
};

template <typename enum_t> struct adl_serializer<enum_t, std::enable_if_t<std::is_enum_v<enum_t>>> {
    static void to_json(json& output, enum_t value) {
        using underlying_t = std::underlying_type_t<enum_t>;
        output = static_cast<underlying_t>(value);
    }

    static void from_json(const json& input, enum_t& value) {
        using underlying_t = std::underlying_type_t<enum_t>;
        if constexpr (std::is_signed_v<underlying_t>) {
            if (input.is_number_unsigned()) {
                const auto number = input.get<json::number_unsigned_t>();
                if (number > static_cast<json::number_unsigned_t>(
                                 std::numeric_limits<underlying_t>::max())) {
                    throw std::out_of_range("enum value exceeds its underlying integer range");
                }
                value = static_cast<enum_t>(static_cast<underlying_t>(number));
            } else if (input.is_number_integer()) {
                const auto number = input.get<json::number_integer_t>();
                if (number < static_cast<json::number_integer_t>(
                                 std::numeric_limits<underlying_t>::min()) ||
                    number > static_cast<json::number_integer_t>(
                                 std::numeric_limits<underlying_t>::max())) {
                    throw std::out_of_range("enum value exceeds its underlying integer range");
                }
                value = static_cast<enum_t>(static_cast<underlying_t>(number));
            } else {
                throw std::invalid_argument("enum value must be an integer");
            }
        } else {
            if (!input.is_number_unsigned()) {
                throw std::invalid_argument("enum value must be an unsigned integer");
            }
            const auto number = input.get<json::number_unsigned_t>();
            if (number >
                static_cast<json::number_unsigned_t>(std::numeric_limits<underlying_t>::max())) {
                throw std::out_of_range("enum value exceeds its underlying integer range");
            }
            value = static_cast<enum_t>(static_cast<underlying_t>(number));
        }
    }
};

} // namespace nlohmann

namespace wormedit {

using json = nlohmann::json;

void to_json(json& value, const format_metadata_t& metadata);
void from_json(const json& value, format_metadata_t& metadata);

template <typename tag_t> void to_json(json& value, const stable_id_t<tag_t>& id) {
    value = id.value;
}

template <typename unsigned_t> auto strict_unsigned(const json& value) -> unsigned_t {
    static_assert(std::is_unsigned_v<unsigned_t>);
    if (!value.is_number_unsigned()) {
        throw std::invalid_argument("value must be an unsigned integer");
    }
    const auto number = value.get<json::number_unsigned_t>();
    if (number > static_cast<json::number_unsigned_t>(std::numeric_limits<unsigned_t>::max())) {
        throw std::out_of_range("unsigned integer is outside its destination range");
    }
    return static_cast<unsigned_t>(number);
}

auto strict_signed_64(const json& value) -> std::int64_t {
    if (value.is_number_integer() && !value.is_number_unsigned()) {
        return value.get<json::number_integer_t>();
    }
    if (value.is_number_unsigned()) {
        const auto number = value.get<json::number_unsigned_t>();
        if (number <=
            static_cast<json::number_unsigned_t>(std::numeric_limits<std::int64_t>::max())) {
            return static_cast<std::int64_t>(number);
        }
    }
    throw std::out_of_range("value must fit a signed 64-bit integer");
}

template <typename tag_t> void from_json(const json& value, stable_id_t<tag_t>& id) {
    id.value = strict_unsigned<std::uint64_t>(value);
}

void to_json(json& value, const rational_t& rational) {
    value = json{{"numerator", rational.numerator}, {"denominator", rational.denominator}};
}

void from_json(const json& value, rational_t& rational) {
    rational.numerator = strict_unsigned<std::uint64_t>(value.at("numerator"));
    rational.denominator = strict_unsigned<std::uint64_t>(value.at("denominator"));
}

void to_json(json& value, const meter_t& meter) {
    value = json{{"numerator", meter.numerator}, {"denominator", meter.denominator}};
}

void from_json(const json& value, meter_t& meter) {
    meter.numerator = strict_unsigned<std::uint32_t>(value.at("numerator"));
    meter.denominator = strict_unsigned<std::uint32_t>(value.at("denominator"));
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(measure_definition_t, length, meter)

void to_json(json& value, const signed_rational_t& rational) {
    value = json{{"numerator", rational.numerator}, {"denominator", rational.denominator}};
}

void from_json(const json& value, signed_rational_t& rational) {
    rational.numerator = strict_signed_64(value.at("numerator"));
    rational.denominator = strict_unsigned<std::uint64_t>(value.at("denominator"));
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(color_rgba_t, red, green, blue, alpha)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(source_provenance_t, family, profile, source_field,
                                   source_spelling, source_order)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(local_position_t, measure, row)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(position_span_t, begin, end)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(midi_destination_t, channel, note)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(bms_channel_map_t, visible, invisible, long_note, mine)
void to_json(json& value, const lane_definition_t& lane) {
    value = json{{"id", lane.id},
                 {"name", lane.name},
                 {"role", lane.role},
                 {"stage", lane.stage},
                 {"display_order", lane.display_order},
                 {"glyph", lane.glyph},
                 {"bms_channels", lane.bms_channels},
                 {"midi_destination", lane.midi_destination},
                 {"key_color", lane.key_color}};
}

// `key_color` is optional so projects saved before it existed still load.
void from_json(const json& value, lane_definition_t& lane) {
    value.at("id").get_to(lane.id);
    value.at("name").get_to(lane.name);
    value.at("role").get_to(lane.role);
    value.at("stage").get_to(lane.stage);
    value.at("display_order").get_to(lane.display_order);
    value.at("glyph").get_to(lane.glyph);
    value.at("bms_channels").get_to(lane.bms_channels);
    value.at("midi_destination").get_to(lane.midi_destination);
    if (value.contains("key_color")) {
        value.at("key_color").get_to(lane.key_color);
    }
}

void to_json(json& value, const lane_layout_t& layout) {
    value = json{{"profile_id", layout.profile_id},
                 {"display_name", layout.display_name},
                 {"lanes", layout.lanes},
                 {"split_stage", layout.split_stage},
                 {"split_after_column", layout.split_after_column},
                 {"bmson_generic_keys_only", layout.bmson_generic_keys_only}};
}

auto bmson_beat_key_count(std::string_view profile_id) -> std::optional<std::uint32_t> {
    if (profile_id == "bmson-beat-5k" || profile_id == "bmson-beat-10k") return 5;
    if (profile_id == "bmson-beat-7k" || profile_id == "bmson-beat-14k") return 7;
    return std::nullopt;
}

auto is_bmson_default_color_profile(std::string_view profile_id) -> bool {
    return profile_id.starts_with("bmson-beat-") || profile_id.starts_with("bmson-popn-");
}

auto is_legacy_bmson_beat_lane(const lane_definition_t& lane, std::uint32_t key_count,
                               bool double_play, std::array<bool, 17>& seen) -> bool {
    if (lane.id.value < 100001 || lane.id.value > 100016) return false;
    const auto x = static_cast<std::uint32_t>(lane.id.value - 100000);
    const auto right = double_play && x >= 9;
    const auto expected_role = x == 8 || (double_play && x == 16)
                                   ? lane_role_t::scratch
                                   : lane_role_t::playable;
    const auto valid_x = x == 8 || (double_play && x == 16) || (x >= 1 && x <= key_count) ||
                         (double_play && x >= 9 && x <= 8 + key_count);
    if (!valid_x || seen[x] || lane.role != expected_role ||
        lane.stage != (right ? stage_group_t::right : stage_group_t::left) ||
        lane.display_order != x - 1)
        return false;
    seen[x] = true;
    return true;
}

auto has_legacy_bmson_beat_order(const lane_layout_t& layout, std::uint32_t key_count,
                                 bool double_play) -> bool {
    const auto expected_count = static_cast<std::size_t>(key_count) * (double_play ? 2U : 1U) +
                                (double_play ? 2U : 1U);
    if (layout.lanes.size() != expected_count) return false;
    std::array<bool, 17> seen{};
    for (const auto& lane : layout.lanes)
        if (!is_legacy_bmson_beat_lane(lane, key_count, double_play, seen)) return false;
    return true;
}

// Older imports used x-1 as the display order. Repair only the exact generated
// lane identity/role/stage pattern, so a custom lane order remains authored.
void repair_legacy_bmson_beat_order(lane_layout_t& layout) {
    const auto key_count = bmson_beat_key_count(layout.profile_id);
    if (!key_count) return;
    const auto double_play = layout.profile_id == "bmson-beat-10k" ||
                             layout.profile_id == "bmson-beat-14k";
    if (!has_legacy_bmson_beat_order(layout, *key_count, double_play)) return;
    for (auto& lane : layout.lanes) {
        const auto x = static_cast<std::uint32_t>(lane.id.value - 100000);
        if (lane.stage == stage_group_t::right) {
            lane.display_order = x == 16 ? 2 * *key_count + 1 : *key_count + x - 8;
        } else {
            lane.display_order = x == 8 ? 0 : x;
        }
    }
}

void from_json(const json& value, lane_layout_t& layout) {
    value.at("profile_id").get_to(layout.profile_id);
    value.at("display_name").get_to(layout.display_name);
    value.at("lanes").get_to(layout.lanes);
    value.at("split_stage").get_to(layout.split_stage);
    value.at("split_after_column").get_to(layout.split_after_column);
    value.at("bmson_generic_keys_only").get_to(layout.bmson_generic_keys_only);
    const auto& lanes = value.at("lanes");
    const auto missing_color = std::ranges::any_of(
        lanes, [](const json& lane) { return !lane.contains("key_color"); });
    // osu!mania layouts gained semantic key classes after projects were already saved
    // with every lane generic. Derive those defaults again; retain explicit classes.
    const auto needs_osu_colors =
        (layout.profile_id.starts_with("osu-mania-") || layout.profile_id == "bms-10k-scratchless") &&
        std::ranges::all_of(layout.lanes, [](const lane_definition_t& lane) {
            return lane.key_color == lane_key_color_t::generic;
        });
    const auto needs_bmson_colors =
        is_bmson_default_color_profile(layout.profile_id) &&
        std::ranges::all_of(layout.lanes, [](const lane_definition_t& lane) {
            return lane.key_color == lane_key_color_t::generic;
        });
    if (missing_color || needs_bmson_colors) repair_legacy_bmson_beat_order(layout);
    if (missing_color || needs_osu_colors || needs_bmson_colors) {
        assign_lane_key_colors(layout);
    }
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(lane_layout_segment_t, id, position, layout)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(lane_binding_t, key, lane, focused_stage)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(input_profile_t, profile_id, bindings, stage_count,
                                   requires_focused_stage)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(semantic_layer_t, id, name, color, visible, locked)
void to_json(json& value, const note_t& note) {
    value = json{{"id", note.id},
                 {"position", note.position},
                 {"lane", note.lane},
                 {"kind", note.kind},
                 {"end", note.end},
                 {"sound", note.sound},
                 {"semantic_layers", note.semantic_layers},
                 {"label", note.label},
                 {"branch", note.branch},
                 {"provenance", note.provenance}};
    if (note.sound_channel_event.has_value()) {
        value["sound_channel_event"] = *note.sound_channel_event;
    }
}

void from_json(const json& value, note_t& note) {
    value.at("id").get_to(note.id);
    value.at("position").get_to(note.position);
    value.at("lane").get_to(note.lane);
    value.at("kind").get_to(note.kind);
    value.at("end").get_to(note.end);
    value.at("sound").get_to(note.sound);
    value.at("semantic_layers").get_to(note.semantic_layers);
    value.at("label").get_to(note.label);
    value.at("branch").get_to(note.branch);
    value.at("provenance").get_to(note.provenance);
    note.sound_channel_event.reset();
    if (const auto linked = value.find("sound_channel_event"); linked != value.end()) {
        linked->get_to(note.sound_channel_event);
    }
}

void to_json(json& value, const bgm_event_t& event) {
    value = json{{"id", event.id},
                 {"position", event.position},
                 {"source_lane", event.source_lane},
                 {"sound", event.sound},
                 {"structural_slot", event.structural_slot},
                 {"branch", event.branch},
                 {"provenance", event.provenance}};
    value["end"] = event.end;
    if (event.sound_channel_event.has_value()) {
        value["sound_channel_event"] = *event.sound_channel_event;
    }
    if (!event.semantic_layers.empty()) value["semantic_layers"] = event.semantic_layers;
}

void from_json(const json& value, bgm_event_t& event) {
    value.at("id").get_to(event.id);
    value.at("position").get_to(event.position);
    value.at("source_lane").get_to(event.source_lane);
    value.at("sound").get_to(event.sound);
    event.semantic_layers = value.value("semantic_layers", std::vector<layer_id_t>{});
    value.at("structural_slot").get_to(event.structural_slot);
    value.at("branch").get_to(event.branch);
    value.at("provenance").get_to(event.provenance);
    event.end.reset();
    if (const auto end = value.find("end"); end != value.end()) end->get_to(event.end);
    event.sound_channel_event.reset();
    if (const auto linked = value.find("sound_channel_event"); linked != value.end()) {
        linked->get_to(event.sound_channel_event);
    }
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(measure_chunk_t, id, definition, notes, bgm_events)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(bms_measure_length_projection_t, measure_index, source_order,
                                   imported_length)
void to_json(json& value, const bgm_source_lane_t& lane) {
    value = json{{"id", lane.id}, {"name", lane.name}, {"source_order", lane.source_order}};
    if (lane.stem_channel) value["stem_channel"] = *lane.stem_channel;
}
void from_json(const json& value, bgm_source_lane_t& lane) {
    value.at("id").get_to(lane.id);
    value.at("name").get_to(lane.name);
    value.at("source_order").get_to(lane.source_order);
    lane.stem_channel.reset();
    if (const auto linked = value.find("stem_channel"); linked != value.end())
        linked->get_to(lane.stem_channel);
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(bpm_event_t, id, position, beats_per_minute, provenance)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(pause_event_t, id, position, kind, duration, source_unit,
                                   provenance)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(bms_bpm_projection_t, event, source_order, imported_position,
                                   imported_beats_per_minute)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(bms_pause_projection_t, event, source_order, imported_position,
                                   imported_kind, imported_duration, imported_source_unit)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(warp_event_t, id, span, source_encoding, provenance)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(scroll_event_t, id, position, multiplier, provenance)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(speed_event_t, id, position, multiplier, transition_beats,
                                   transition_is_seconds, provenance)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(sv_recipe_parameters_t, value_from, value_to, shape, bump_start,
                                   bump_length, bump_x, bump_y)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(sv_recipe_sampling_t, mode, fixed_point_count, fixed_step_rows,
                                   tolerance, max_points)
// expression_source is schema 2; schema 1 documents gain an empty one in migrate_v1_to_v2.
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(sv_recipe_t, id, name, range, output, curve, parameters,
                                   sampling, expression_source, transition_duration,
                                   transition_is_seconds, enabled)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(kiai_event_t, id, position, enabled, retained_effect_bits,
                                   provenance)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(timing_anchor_t, id, position, source_sample_frame, locked)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(bookmark_t, id, label, anchor, position, audio_milliseconds)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(section_t, id, label, anchor, beat_span, audio_millisecond_span)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(bga_event_t, id, position, resource, kind, provenance)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(audio_stream_info_t, sample_rate, channels, frame_count,
                                   seekable)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(target_resource_identity_t, family, identifier, namespace_name)
} // namespace wormedit

namespace wormedit {
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(sound_group_t, id, name)
// sound_kind and tags are written only when set, so projects that never use
// them keep their previous bytes and older files load with the defaults.
void to_json(nlohmann::json& value, const resource_entry_t& resource) {
    value = nlohmann::json{{"id", resource.id},
                           {"name", resource.name},
                           {"asset", resource.asset},
                           {"roles", resource.roles},
                           {"target_identities", resource.target_identities},
                           {"missing", resource.missing},
                           {"provenance", resource.provenance}};
    if (resource.sound_kind != sound_kind_t::unspecified) {
        value["sound_kind"] = static_cast<std::uint8_t>(resource.sound_kind);
    }
    if (!resource.tags.empty()) value["tags"] = resource.tags;
    if (resource.sound_group) value["sound_group"] = *resource.sound_group;
}

void from_json(const nlohmann::json& value, resource_entry_t& resource) {
    value.at("id").get_to(resource.id);
    value.at("name").get_to(resource.name);
    value.at("asset").get_to(resource.asset);
    value.at("roles").get_to(resource.roles);
    value.at("target_identities").get_to(resource.target_identities);
    value.at("missing").get_to(resource.missing);
    value.at("provenance").get_to(resource.provenance);
    resource.sound_kind = static_cast<sound_kind_t>(value.value("sound_kind", std::uint8_t{0}));
    resource.tags = value.value("tags", std::vector<std::string>{});
    if (value.contains("sound_group")) {
        resource.sound_group = value.at("sound_group").get<sound_group_id_t>();
    } else {
        resource.sound_group.reset();
    }
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(sound_layer_default_t, resource, layers)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(audio_clip_t, id, source, begin_frame, end_frame)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(sound_channel_event_t, id, position, end, lane, continuation,
                                   playable, provenance)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(sound_channel_t, id, name, source_clip, events)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(bms_source_span_t, byte_begin, byte_end)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(bms_control_node_t, id, kind, value, original_text,
                                   command_token, raw_value, source_span, source_order, line_ending,
                                   close_source_span, close_text, close_source_order,
                                   close_line_ending, children)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(branch_selection_t, branch, selected_value)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(chart_metadata_t, name, author, rating, format_groups)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(storyboard_command_t, opcode, easing, start_time_ms, end_time_ms,
                                   from, to, value_count, source_order)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(storyboard_node_t, kind, command, loop_start_time_ms,
                                   loop_iteration_count, source_order, retained_line, children)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(storyboard_sprite_t, stable_id, source_order, layer, origin,
                                   path, x, y, nodes)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(storyboard_top_level_line_t, source_order, text)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(storyboard_document_model_t, sprites, retained_top_level_lines)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(song_metadata_t, title, artist, ordered_search_tags,
                                   format_groups)

void to_json(json& value, const storyboard_document_t& storyboard) {
    value = json{{"id", storyboard.id},
                 {"name", storyboard.name},
                 {"retained_source_lines", storyboard.retained_source_lines},
                 {"model", storyboard.model}};
}

void from_json(const json& value, storyboard_document_t& storyboard) {
    value.at("id").get_to(storyboard.id);
    value.at("name").get_to(storyboard.name);
    value.at("retained_source_lines").get_to(storyboard.retained_source_lines);
    if (value.contains("model")) {
        value.at("model").get_to(storyboard.model);
    } else {
        // Version-1 projects written before structured storyboard support contain
        // only the retained source-line bag.
        storyboard.model.reset();
    }
}

namespace {

constexpr std::string_view native_project_format = "wormedit-project";
// The envelope (`format`, `format_version`, `document`) and the document schema evolve separately:
// the envelope is still version 1, while the document is at schema 6 (older documents migrate on
// load; see native_project_migrations.cpp).
constexpr std::uint32_t native_project_format_version = 1;
constexpr std::uint32_t native_project_schema_version = 6;

auto path_to_utf8(const std::filesystem::path& path) -> std::string {
    return text_util::path_utf8(path);
}

auto path_from_utf8(const std::string& path) -> std::filesystem::path {
    return text_util::path_from_utf8(path);
}

constexpr std::string_view hex_digits = "0123456789abcdef";

auto bytes_to_hex(const std::vector<std::byte>& bytes) -> std::string {
    if (bytes.size() > std::numeric_limits<std::size_t>::max() / 2) {
        throw std::length_error("retained byte stream is too large to encode");
    }
    std::string encoded;
    encoded.reserve(bytes.size() * 2);
    for (const auto item : bytes) {
        const auto value = std::to_integer<std::uint8_t>(item);
        encoded.push_back(hex_digits[value >> 4U]);
        encoded.push_back(hex_digits[value & 0x0fU]);
    }
    return encoded;
}

auto hex_nibble(char value) -> std::optional<std::uint8_t> {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
    }
    if (value >= 'A' && value <= 'F') {
        return static_cast<std::uint8_t>(value - 'A' + 10);
    }
    return std::nullopt;
}

auto hex_to_bytes(std::string_view encoded) -> std::vector<std::byte> {
    if ((encoded.size() % 2) != 0) {
        throw std::invalid_argument("hex byte stream has odd length");
    }
    std::vector<std::byte> result;
    result.reserve(encoded.size() / 2);
    for (std::size_t index = 0; index < encoded.size(); index += 2) {
        const auto high = hex_nibble(encoded[index]);
        const auto low = hex_nibble(encoded[index + 1]);
        if (!high.has_value() || !low.has_value()) {
            throw std::invalid_argument("hex byte stream contains a non-hexadecimal digit");
        }
        result.push_back(static_cast<std::byte>((*high << 4U) | *low));
    }
    return result;
}

} // namespace

void to_json(json& value, const encoding_provenance_t& provenance) {
    value = json{{"detected", provenance.detected},
                 {"user_override", provenance.user_override},
                 {"unchanged_since_import", provenance.unchanged_since_import}};
    value["original_bytes_hex"] =
        provenance.original_bytes ? json(bytes_to_hex(*provenance.original_bytes)) : json(nullptr);
}

void from_json(const json& value, encoding_provenance_t& provenance) {
    value.at("detected").get_to(provenance.detected);
    value.at("user_override").get_to(provenance.user_override);
    value.at("unchanged_since_import").get_to(provenance.unchanged_since_import);
    const auto& encoded = value.at("original_bytes_hex");
    if (encoded.is_null()) {
        provenance.original_bytes.reset();
    } else {
        provenance.original_bytes = std::make_shared<const std::vector<std::byte>>(
            hex_to_bytes(encoded.get<std::string>()));
    }
}

void to_json(json& value, const format_metadata_t& metadata) {
    value = json{{"family", metadata.family},
                 {"ordered_fields", metadata.ordered_fields},
                 {"ordered_field_source_orders", metadata.ordered_field_source_orders},
                 {"encoding", metadata.encoding}};
}

void from_json(const json& value, format_metadata_t& metadata) {
    value.at("family").get_to(metadata.family);
    value.at("ordered_fields").get_to(metadata.ordered_fields);
    value.at("ordered_field_source_orders").get_to(metadata.ordered_field_source_orders);
    value.at("encoding").get_to(metadata.encoding);
}

void to_json(json& value, const quantization_preset_t& preset) {
    value = json{{"name", preset.name},
                 {"divisions", preset.divisions},
                 {"space", preset.space},
                 {"tie", preset.tie},
                 {"holds", preset.holds}};
}

void from_json(const json& value, quantization_preset_t& preset) {
    value.at("name").get_to(preset.name);
    preset.divisions = strict_unsigned<std::uint64_t>(value.at("divisions"));
    value.at("space").get_to(preset.space);
    value.at("tie").get_to(preset.tie);
    value.at("holds").get_to(preset.holds);
}

void to_json(json& value, const audio_asset_t& asset) {
    value = json{{"id", asset.id},
                 {"source_path", path_to_utf8(asset.source_path)},
                 {"stream", asset.stream},
                 {"waveform_cache_path", asset.waveform_cache_path
                                             ? json(path_to_utf8(*asset.waveform_cache_path))
                                             : json(nullptr)}};
}

void from_json(const json& value, audio_asset_t& asset) {
    value.at("id").get_to(asset.id);
    asset.source_path = path_from_utf8(value.at("source_path").get<std::string>());
    value.at("stream").get_to(asset.stream);
    const auto& cache = value.at("waveform_cache_path");
    asset.waveform_cache_path = cache.is_null() ? std::optional<std::filesystem::path>{}
                                                : path_from_utf8(cache.get<std::string>());
}

void to_json(json& value, const visual_asset_t& asset) {
    value = json{{"id", asset.id},
                 {"kind", asset.kind},
                 {"source_path", path_to_utf8(asset.source_path)},
                 {"width", asset.width},
                 {"height", asset.height}};
}

void from_json(const json& value, visual_asset_t& asset) {
    value.at("id").get_to(asset.id);
    value.at("kind").get_to(asset.kind);
    asset.source_path = path_from_utf8(value.at("source_path").get<std::string>());
    value.at("width").get_to(asset.width);
    value.at("height").get_to(asset.height);
}

void to_json(json& value, const midi_event_t& event) {
    value = json{
        {"tick", event.tick}, {"channel", event.channel}, {"data_hex", bytes_to_hex(event.data)}};
}

void from_json(const json& value, midi_event_t& event) {
    value.at("tick").get_to(event.tick);
    value.at("channel").get_to(event.channel);
    event.data = hex_to_bytes(value.at("data_hex").get<std::string>());
}

void to_json(json& value, const midi_asset_t& asset) {
    value = json{{"id", asset.id},
                 {"source_path", path_to_utf8(asset.source_path)},
                 {"ticks_per_quarter", asset.ticks_per_quarter},
                 {"events", asset.events}};
}

void from_json(const json& value, midi_asset_t& asset) {
    value.at("id").get_to(asset.id);
    asset.source_path = path_from_utf8(value.at("source_path").get<std::string>());
    value.at("ticks_per_quarter").get_to(asset.ticks_per_quarter);
    value.at("events").get_to(asset.events);
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(generator_parameter_t, name, serialized_value)

void to_json(json& value, const generator_recipe_t& recipe) {
    value = json{{"id", recipe.id},
                 {"api_version", recipe.api_version},
                 {"source_path", path_to_utf8(recipe.source_path)},
                 {"input_assets", recipe.input_assets},
                 {"parameters", recipe.parameters},
                 {"seed", recipe.seed},
                 {"source_hash", recipe.source_hash}};
}

void from_json(const json& value, generator_recipe_t& recipe) {
    value.at("id").get_to(recipe.id);
    value.at("api_version").get_to(recipe.api_version);
    recipe.source_path = path_from_utf8(value.at("source_path").get<std::string>());
    value.at("input_assets").get_to(recipe.input_assets);
    value.at("parameters").get_to(recipe.parameters);
    value.at("seed").get_to(recipe.seed);
    value.at("source_hash").get_to(recipe.source_hash);
}

void to_json(json& value, const output_group_t& group) {
    value = json{{"name", group.name},
                 {"family", group.family},
                 {"charts", group.charts},
                 {"relative_output_path", path_to_utf8(group.relative_output_path)}};
}

void from_json(const json& value, output_group_t& group) {
    value.at("name").get_to(group.name);
    value.at("family").get_to(group.family);
    value.at("charts").get_to(group.charts);
    group.relative_output_path =
        path_from_utf8(value.at("relative_output_path").get<std::string>());
}

void to_json(json& value, const chart_document_t& chart) {
    value = json{{"id", chart.id},
                 {"metadata", chart.metadata},
                 {"source_family", chart.source_family},
                 {"divisions_per_standard_measure", chart.divisions_per_standard_measure},
                 {"beat_zero_audio_offset_seconds", chart.beat_zero_audio_offset_seconds},
                 {"measures", chart.measures},
                 {"lane_layouts", chart.lane_layouts},
                 {"input_profile", chart.input_profile},
                 {"semantic_layers", chart.semantic_layers},
                 {"bgm_source_lanes", chart.bgm_source_lanes},
                 {"bpms", chart.bpms},
                 {"pauses", chart.pauses},
                 {"warps", chart.warps},
                 {"scrolls", chart.scrolls},
                 {"speeds", chart.speeds},
                 {"sv_recipes", chart.sv_recipes},
                 {"kiai", chart.kiai},
                 {"timing_anchors", chart.timing_anchors},
                 {"bookmarks", chart.bookmarks},
                 {"sections", chart.sections},
                 {"bga_events", chart.bga_events},
                 {"sound_channels", chart.sound_channels},
                 {"bms_measure_length_projections", chart.bms_measure_length_projections},
                 {"bms_bpm_projections", chart.bms_bpm_projections},
                 {"bms_pause_projections", chart.bms_pause_projections},
                 {"bms_control_tree", chart.bms_control_tree},
                 {"active_branch_selection", chart.active_branch_selection}};
    // Written only when used, so projects without sound layers keep their bytes.
    if (!chart.sound_layer_defaults.empty()) {
        value["sound_layer_defaults"] = chart.sound_layer_defaults;
    }
}

void from_json(const json& value, chart_document_t& chart) {
    value.at("id").get_to(chart.id);
    value.at("metadata").get_to(chart.metadata);
    value.at("source_family").get_to(chart.source_family);
    value.at("divisions_per_standard_measure").get_to(chart.divisions_per_standard_measure);
    value.at("beat_zero_audio_offset_seconds").get_to(chart.beat_zero_audio_offset_seconds);
    value.at("measures").get_to(chart.measures);
    value.at("lane_layouts").get_to(chart.lane_layouts);
    value.at("input_profile").get_to(chart.input_profile);
    value.at("semantic_layers").get_to(chart.semantic_layers);
    chart.sound_layer_defaults =
        value.value("sound_layer_defaults", std::vector<sound_layer_default_t>{});
    value.at("bgm_source_lanes").get_to(chart.bgm_source_lanes);
    value.at("bpms").get_to(chart.bpms);
    value.at("pauses").get_to(chart.pauses);
    value.at("warps").get_to(chart.warps);
    value.at("scrolls").get_to(chart.scrolls);
    value.at("speeds").get_to(chart.speeds);
    if (value.contains("sv_recipes")) {
        value.at("sv_recipes").get_to(chart.sv_recipes);
    } else {
        // Earlier version-1 projects predate durable parametric SV recipes.
        chart.sv_recipes.clear();
    }
    value.at("kiai").get_to(chart.kiai);
    value.at("timing_anchors").get_to(chart.timing_anchors);
    value.at("bookmarks").get_to(chart.bookmarks);
    value.at("sections").get_to(chart.sections);
    value.at("bga_events").get_to(chart.bga_events);
    value.at("sound_channels").get_to(chart.sound_channels);
    value.at("bms_measure_length_projections").get_to(chart.bms_measure_length_projections);
    value.at("bms_bpm_projections").get_to(chart.bms_bpm_projections);
    value.at("bms_pause_projections").get_to(chart.bms_pause_projections);
    value.at("bms_control_tree").get_to(chart.bms_control_tree);
    value.at("active_branch_selection").get_to(chart.active_branch_selection);
}

void to_json(json& value, const song_document_t& song) {
    value = json{{"schema_version", song.schema_version},
                 {"id", song.id},
                 {"revision", song.revision},
                 {"metadata", song.metadata},
                 {"audio_assets", song.audio_assets},
                 {"visual_assets", song.visual_assets},
                 {"resources", song.resources},
                 {"midi_assets", song.midi_assets},
                 {"generator_recipes", song.generator_recipes},
                 {"storyboards", song.storyboards},
                 {"charts", song.charts},
                 {"output_groups", song.output_groups},
                 {"quantization_presets", song.quantization_presets}};
    if (!song.sound_groups.empty()) value["sound_groups"] = song.sound_groups;
}

void from_json(const json& value, song_document_t& song) {
    value.at("schema_version").get_to(song.schema_version);
    value.at("id").get_to(song.id);
    value.at("revision").get_to(song.revision);
    value.at("metadata").get_to(song.metadata);
    value.at("audio_assets").get_to(song.audio_assets);
    value.at("visual_assets").get_to(song.visual_assets);
    value.at("resources").get_to(song.resources);
    value.at("midi_assets").get_to(song.midi_assets);
    value.at("generator_recipes").get_to(song.generator_recipes);
    value.at("storyboards").get_to(song.storyboards);
    value.at("charts").get_to(song.charts);
    value.at("output_groups").get_to(song.output_groups);
    value.at("quantization_presets").get_to(song.quantization_presets);
    song.sound_groups = value.value("sound_groups", std::vector<sound_group_t>{});
}

namespace {

template <typename enum_t> auto enum_at_most(enum_t value, enum_t maximum) -> bool {
    using underlying_t = std::underlying_type_t<enum_t>;
    const auto numeric = static_cast<underlying_t>(value);
    return numeric >= 0 && numeric <= static_cast<underlying_t>(maximum);
}

auto valid_provenance(const source_provenance_t& provenance) -> bool {
    return enum_at_most(provenance.family, format_family_t::quaver);
}

auto valid_encoding(const encoding_provenance_t& encoding) -> bool {
    return enum_at_most(encoding.detected, text_encoding_t::windows_1252) &&
           (!encoding.user_override.has_value() ||
            enum_at_most(*encoding.user_override, text_encoding_t::windows_1252));
}

auto valid_metadata(const format_metadata_t& metadata) -> bool {
    return enum_at_most(metadata.family, format_family_t::quaver) &&
           valid_encoding(metadata.encoding);
}

auto valid_quantization_preset(const quantization_preset_t& preset) -> bool {
    return !preset.name.empty() &&
           preset.name.size() <= quantization_preset_t::maximum_name_bytes &&
           preset.name.find('\0') == std::string::npos &&
           preset.divisions > 0 &&
           enum_at_most(preset.space, quantization_space_t::authored_measure) &&
           enum_at_most(preset.tie, quantization_tie_t::reject) &&
           enum_at_most(preset.holds, quantization_hold_t::both_endpoints);
}

auto valid_quantization_presets(const std::vector<quantization_preset_t>& presets) -> bool {
    if (presets.size() > quantization_preset_t::maximum_count) return false;
    std::set<std::string_view> names;
    for (const auto& preset : presets)
        if (!valid_quantization_preset(preset) || !names.insert(preset.name).second) return false;
    return true;
}

auto valid_control_node(const bms_control_node_t& root, std::size_t maximum_depth) -> bool {
    std::vector<std::pair<const bms_control_node_t*, std::size_t>> pending{{&root, 1}};
    while (!pending.empty()) {
        const auto [node, depth] = pending.back();
        pending.pop_back();
        if (depth > maximum_depth || !enum_at_most(node->kind, bms_control_kind_t::skip) ||
            !enum_at_most(node->line_ending, bms_line_ending_t::cr) ||
            (node->close_line_ending.has_value() &&
             !enum_at_most(*node->close_line_ending, bms_line_ending_t::cr))) {
            return false;
        }
        for (const auto& child : node->children) {
            pending.emplace_back(&child, depth + 1);
        }
    }
    return true;
}

auto valid_resource_enum_domains(const song_document_t& song) -> bool {
    for (const auto& resource : song.resources) {
        if (!valid_provenance(resource.provenance) ||
            !std::ranges::all_of(resource.roles,
                                 [](auto role) { return enum_at_most(role, resource_role_t::preview); }) ||
            !std::ranges::all_of(resource.target_identities, [](const auto& identity) {
                return enum_at_most(identity.family, format_family_t::quaver);
            })) {
            return false;
        }
    }
    return true;
}

auto valid_chart_layout_domains(const chart_document_t& chart) -> bool {
    if (!enum_at_most(chart.source_family, format_family_t::quaver) ||
        !std::ranges::all_of(chart.metadata.format_groups, valid_metadata)) {
        return false;
    }
    for (const auto& segment : chart.lane_layouts) {
        for (const auto& lane : segment.layout.lanes) {
            if (!enum_at_most(lane.role, lane_role_t::foot_pedal) ||
                !enum_at_most(lane.stage, stage_group_t::right) ||
                !enum_at_most(lane.glyph, lane_glyph_t::arrow_center) ||
                !enum_at_most(lane.key_color, lane_key_color_t::pms_red)) {
                return false;
            }
        }
    }
    return std::ranges::all_of(chart.input_profile.bindings, [](const auto& binding) {
        return enum_at_most(binding.key, physical_key_t::key_m);
    });
}

auto valid_chart_measure_domains(const chart_document_t& chart) -> bool {
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (!enum_at_most(note.kind, note_kind_t::fake) ||
                !valid_provenance(note.provenance)) {
                return false;
            }
        }
        if (!std::ranges::all_of(measure.bgm_events, [](const auto& bgm) {
                return valid_provenance(bgm.provenance);
            })) {
            return false;
        }
    }
    return true;
}

auto valid_chart_timing_domains(const chart_document_t& chart) -> bool {
    if (!std::ranges::all_of(chart.bpms, [](const auto& event) {
            return valid_provenance(event.provenance);
        })) {
        return false;
    }
    if (!std::ranges::all_of(chart.pauses, [](const auto& pause) {
            return enum_at_most(pause.kind, pause_kind_t::delay) &&
                   enum_at_most(pause.source_unit, pause_unit_t::beats) &&
                   valid_provenance(pause.provenance);
        })) {
        return false;
    }
    if (!std::ranges::all_of(chart.bms_pause_projections, [](const auto& projection) {
            return enum_at_most(projection.imported_kind, pause_kind_t::delay) &&
                   enum_at_most(projection.imported_source_unit, pause_unit_t::beats);
        })) {
        return false;
    }
    return std::ranges::all_of(chart.warps, [](const auto& warp) {
        return enum_at_most(warp.source_encoding, warp_provenance_t::negative_stop) &&
               valid_provenance(warp.provenance);
    });
}

auto valid_chart_effect_domains(const chart_document_t& chart) -> bool {
    if (!std::ranges::all_of(chart.scrolls, [](const auto& item) {
            return valid_provenance(item.provenance);
        }) ||
        !std::ranges::all_of(chart.speeds, [](const auto& item) {
            return valid_provenance(item.provenance);
        })) {
        return false;
    }
    if (!std::ranges::all_of(chart.sv_recipes, [](const auto& recipe) {
            return enum_at_most(recipe.output, sv_recipe_output_t::speeds) &&
                   enum_at_most(recipe.curve, sv_recipe_curve_t::lua_expression) &&
                   enum_at_most(recipe.sampling.mode, sv_recipe_sampling_mode_t::adaptive);
        })) {
        return false;
    }
    return std::ranges::all_of(chart.kiai, [](const auto& item) {
        return valid_provenance(item.provenance);
    });
}

auto valid_chart_visual_domains(const chart_document_t& chart) -> bool {
    const auto valid_anchor = [](const auto& item) {
        return enum_at_most(item.anchor, marker_anchor_t::audio_time);
    };
    if (!std::ranges::all_of(chart.bookmarks, valid_anchor) ||
        !std::ranges::all_of(chart.sections, valid_anchor)) {
        return false;
    }
    if (!std::ranges::all_of(chart.bga_events, [](const auto& event) {
            return enum_at_most(event.kind, visual_event_kind_t::layer2) &&
                   valid_provenance(event.provenance);
        })) {
        return false;
    }
    return std::ranges::all_of(chart.sound_channels, [](const auto& channel) {
        return std::ranges::all_of(channel.events, [](const auto& event) {
            return valid_provenance(event.provenance);
        });
    });
}

auto valid_chart_enum_domains(const chart_document_t& chart, std::size_t maximum_depth) -> bool {
    return valid_chart_layout_domains(chart) && valid_chart_measure_domains(chart) &&
           valid_chart_timing_domains(chart) && valid_chart_effect_domains(chart) &&
           valid_chart_visual_domains(chart) &&
           (!chart.bms_control_tree.has_value() ||
            valid_control_node(*chart.bms_control_tree, maximum_depth));
}

auto valid_enum_domains(const song_document_t& song, std::size_t maximum_depth) -> bool {
    if (!std::ranges::all_of(song.metadata.format_groups, valid_metadata) ||
        !valid_resource_enum_domains(song) ||
        !valid_quantization_presets(song.quantization_presets)) {
        return false;
    }
    if (!std::ranges::all_of(song.visual_assets, [](const auto& asset) {
            return enum_at_most(asset.kind, asset_kind_t::midi);
        }) ||
        !std::ranges::all_of(song.output_groups, [](const auto& group) {
            return enum_at_most(group.family, format_family_t::quaver);
        })) {
        return false;
    }
    return std::ranges::all_of(song.charts, [&](const auto& chart) {
        return valid_chart_enum_domains(chart, maximum_depth);
    });
}

auto lexical_nesting_within(std::string_view source, std::size_t maximum_depth) -> bool {
    std::size_t depth{};
    bool in_string{};
    bool escaped{};
    for (const auto character : source) {
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                in_string = false;
            }
            continue;
        }
        if (character == '"') {
            in_string = true;
        } else if (character == '{' || character == '[') {
            if (depth == maximum_depth) {
                return false;
            }
            ++depth;
        } else if ((character == '}' || character == ']') && depth > 0) {
            --depth;
        }
    }
    return true;
}

class json_limit_sax_t final : public nlohmann::json_sax<json> {
  public:
    json_limit_sax_t(std::size_t maximum_nodes, std::size_t maximum_depth)
        : maximum_nodes_(maximum_nodes), maximum_depth_(maximum_depth) {}

    auto null() -> bool override {
        return scalar();
    }
    auto boolean(bool) -> bool override {
        return scalar();
    }
    auto number_integer(number_integer_t) -> bool override {
        return scalar();
    }
    auto number_unsigned(number_unsigned_t) -> bool override {
        return scalar();
    }
    auto number_float(number_float_t, const string_t&) -> bool override {
        return scalar();
    }
    auto string(string_t&) -> bool override {
        return scalar();
    }
    auto binary(binary_t&) -> bool override {
        return scalar();
    }
    auto start_object(std::size_t) -> bool override {
        return start_container();
    }
    auto key(string_t&) -> bool override {
        return true;
    }
    auto end_object() -> bool override {
        if (depth_ > 0) {
            --depth_;
        }
        return true;
    }
    auto start_array(std::size_t) -> bool override {
        return start_container();
    }
    auto end_array() -> bool override {
        if (depth_ > 0) {
            --depth_;
        }
        return true;
    }
    auto parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&)
        -> bool override {
        malformed_ = !limit_exceeded_;
        return false;
    }

    [[nodiscard]] auto limit_exceeded() const noexcept -> bool {
        return limit_exceeded_;
    }
    [[nodiscard]] auto malformed() const noexcept -> bool {
        return malformed_;
    }

  private:
    auto scalar() -> bool {
        return count_node();
    }
    auto start_container() -> bool {
        if (!count_node() || depth_ == maximum_depth_) {
            limit_exceeded_ = true;
            return false;
        }
        ++depth_;
        return true;
    }
    auto count_node() -> bool {
        if (nodes_ == maximum_nodes_) {
            limit_exceeded_ = true;
            return false;
        }
        ++nodes_;
        return true;
    }

    std::size_t maximum_nodes_{};
    std::size_t maximum_depth_{};
    std::size_t nodes_{};
    std::size_t depth_{};
    bool limit_exceeded_{};
    bool malformed_{};
};

auto is_signed_numeric_field(std::string_view key, bool signed_array) -> bool {
    return key == "numerator" || key == "selected_value" || key == "value" ||
           key == "audio_milliseconds" || key == "start_time_ms" || key == "end_time_ms" ||
           key == "loop_start_time_ms" || signed_array;
}

auto unsigned_fits_schema_field(std::string_view key, json::number_unsigned_t number) -> bool {
    const auto at_most = [number](std::uint64_t maximum) { return number <= maximum; };
    if ((key == "red" || key == "green" || key == "blue" || key == "alpha" ||
         key == "value_count" || key == "channel" || key == "note") &&
        !at_most(std::numeric_limits<std::uint8_t>::max())) {
        return false;
    }
    if (key == "ticks_per_quarter" && !at_most(std::numeric_limits<std::uint16_t>::max())) {
        return false;
    }
    return !((key == "sample_rate" || key == "channels" || key == "width" ||
              key == "height" || key == "display_order" || key == "split_after_column" ||
              key == "focused_stage" || key == "stage_count" ||
              key == "retained_effect_bits" || key == "schema_version" ||
              key == "fixed_point_count" || key == "max_points") &&
             !at_most(std::numeric_limits<std::uint32_t>::max()));
}

auto strict_numeric_shape(const json& value, std::string_view key = {}, bool signed_array = false)
    -> bool {
    if (key == "x" || key == "y") {
        return value.is_number_float() && std::isfinite(value.get<double>());
    }
    if (key == "from" || key == "to") {
        return value.is_array() && std::ranges::all_of(value, [](const auto& component) {
                   return component.is_number_float() &&
                          std::isfinite(component.template get<double>());
               });
    }
    if (value.is_number_float()) {
        return false;
    }
    if (value.is_number_integer() && !value.is_number_unsigned()) {
        return is_signed_numeric_field(key, signed_array);
    }
    if (value.is_number_unsigned()) {
        const auto number = value.get<json::number_unsigned_t>();
        const auto signed_field = is_signed_numeric_field(key, signed_array) && key != "numerator";
        if (signed_field && number > static_cast<json::number_unsigned_t>(
                                         std::numeric_limits<std::int64_t>::max())) {
            return false;
        }
        return unsigned_fits_schema_field(key, number);
    }
    if (value.is_array()) {
        const auto elements_are_signed = key == "audio_millisecond_span";
        return std::ranges::all_of(value, [&](const auto& element) {
            return strict_numeric_shape(element, {}, elements_are_signed);
        });
    }
    if (value.is_object()) {
        return std::ranges::all_of(value.items(), [&](const auto& item) {
            return strict_numeric_shape(item.value(), item.key(), false);
        });
    }
    return true;
}

enum class schema_shape_context_t {
    ordinary,
    storyboards,
    storyboard_entry,
    charts,
    chart_entry,
    bgm_events,
    bgm_event_entry,
};

auto same_schema_shape(const json& input, const json& canonical,
                       schema_shape_context_t context) -> bool;

auto allows_legacy_object_shape(const json& input, const json& canonical,
                                schema_shape_context_t context) -> bool {
    if (input.size() + 1 != canonical.size()) {
        return false;
    }
    if (context == schema_shape_context_t::storyboard_entry) {
        return !input.contains("model") && canonical.contains("model") &&
               canonical.at("model").is_null();
    }
    if (context == schema_shape_context_t::chart_entry) {
        return !input.contains("sv_recipes") && canonical.contains("sv_recipes") &&
               canonical.at("sv_recipes").empty();
    }
    if (context == schema_shape_context_t::bgm_event_entry) {
        return !input.contains("end") && canonical.contains("end");
    }
    // Lane definitions saved before key colors existed; derived on load.
    return !input.contains("key_color") && canonical.contains("key_color");
}

auto object_child_context(std::string_view key) -> schema_shape_context_t {
    if (key == "storyboards") {
        return schema_shape_context_t::storyboards;
    }
    if (key == "charts") {
        return schema_shape_context_t::charts;
    }
    if (key == "bgm_events") {
        return schema_shape_context_t::bgm_events;
    }
    return schema_shape_context_t::ordinary;
}

auto array_child_context(schema_shape_context_t context) -> schema_shape_context_t {
    if (context == schema_shape_context_t::storyboards) {
        return schema_shape_context_t::storyboard_entry;
    }
    if (context == schema_shape_context_t::charts) {
        return schema_shape_context_t::chart_entry;
    }
    if (context == schema_shape_context_t::bgm_events) {
        return schema_shape_context_t::bgm_event_entry;
    }
    return schema_shape_context_t::ordinary;
}

auto same_object_schema_shape(const json& input, const json& canonical,
                              schema_shape_context_t context) -> bool {
    if (!input.is_object() || !canonical.is_object()) {
        return false;
    }
    const auto legacy_shape = allows_legacy_object_shape(input, canonical, context);
    if (input.size() != canonical.size() && !legacy_shape) {
        return false;
    }
    for (const auto& [key, value] : input.items()) {
        const auto expected = canonical.find(key);
        if (expected == canonical.end() ||
            !same_schema_shape(value, *expected, object_child_context(key))) {
            return false;
        }
    }
    return true;
}

auto same_array_schema_shape(const json& input, const json& canonical,
                             schema_shape_context_t context) -> bool {
    if (!input.is_array() || !canonical.is_array() || input.size() != canonical.size()) {
        return false;
    }
    const auto child_context = array_child_context(context);
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (!same_schema_shape(input[index], canonical[index], child_context)) {
            return false;
        }
    }
    return true;
}

auto same_schema_shape(const json& input, const json& canonical,
                       schema_shape_context_t context = schema_shape_context_t::ordinary) -> bool {
    if (input.is_object() || canonical.is_object()) {
        return same_object_schema_shape(input, canonical, context);
    }
    if (input.is_array() || canonical.is_array()) {
        return same_array_schema_shape(input, canonical, context);
    }
    return true;
}

auto json_nodes_within(const json& root, std::size_t maximum_nodes, std::size_t maximum_depth)
    -> bool {
    std::vector<std::pair<const json*, std::size_t>> pending{{&root, 1}};
    std::size_t nodes{};
    while (!pending.empty()) {
        const auto [value, depth] = pending.back();
        pending.pop_back();
        if (nodes == maximum_nodes ||
            ((value->is_array() || value->is_object()) && depth > maximum_depth)) {
            return false;
        }
        ++nodes;
        if (value->is_array()) {
            for (const auto& item : *value) {
                pending.emplace_back(&item, depth + 1);
            }
        } else if (value->is_object()) {
            for (const auto& [key, item] : value->items()) {
                (void)key;
                pending.emplace_back(&item, depth + 1);
            }
        }
    }
    return true;
}

auto has_errors(const std::vector<native_project_diagnostic_t>& diagnostics) -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto graph_diagnostic_text(const diagnostic_t& diagnostic) -> std::string {
    auto message = diagnostic.message;
    if (diagnostic.object.has_value()) {
        message += " (object " + std::to_string(diagnostic.object->value) + ")";
    }
    return message;
}

auto migration_diagnostic_code(native_project_internal::migration_status_t status)
    -> native_project_diagnostic_code_t {
    switch (status) {
    case native_project_internal::migration_status_t::malformed_version:
        return native_project_diagnostic_code_t::malformed_json;
    case native_project_internal::migration_status_t::failed:
        return native_project_diagnostic_code_t::invalid_document;
    default:
        return native_project_diagnostic_code_t::unsupported_version;
    }
}

} // namespace

auto native_project_write_result_t::has_errors() const -> bool {
    return wormedit::has_errors(diagnostics);
}

auto native_project_parse_result_t::has_errors() const -> bool {
    return wormedit::has_errors(diagnostics);
}

auto write_native_project(const song_document_t& song, native_project_limits_t limits)
    -> native_project_write_result_t {
    native_project_write_result_t result;
    if (song.schema_version < 2 || song.schema_version > native_project_schema_version) {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, native_project_diagnostic_code_t::unsupported_version,
             "document schema version is unsupported", "$.document.schema_version"});
        return result;
    }
    if (!valid_enum_domains(song, limits.maximum_nesting_depth)) {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, native_project_diagnostic_code_t::invalid_document,
             "document contains an enum value outside the native-project schema", "$.document"});
    }
    if (result.has_errors()) {
        return result;
    }
    const auto validation = validate_document(song);
    for (const auto& diagnostic : validation) {
        if (diagnostic.severity == diagnostic_severity_t::error) {
            result.diagnostics.push_back({diagnostic.severity,
                                          native_project_diagnostic_code_t::invalid_document,
                                          graph_diagnostic_text(diagnostic), "$.document"});
        }
    }
    if (result.has_errors()) {
        return result;
    }
    try {
        json serialized_document = song;
        if (song.schema_version < native_project_schema_version) {
            const auto migrated = native_project_internal::migrate_document(
                serialized_document, native_project_schema_version,
                native_project_internal::default_migrations());
            if (migrated.status != native_project_internal::migration_status_t::migrated &&
                migrated.status != native_project_internal::migration_status_t::unchanged) {
                result.diagnostics.push_back(
                    {diagnostic_severity_t::error,
                     native_project_diagnostic_code_t::invalid_document,
                     "native project schema migration failed: " + migrated.message,
                     "$.document.schema_version"});
                return result;
            }
        }
        const json root{{"format", native_project_format},
                        {"format_version", native_project_format_version},
                        {"document", std::move(serialized_document)}};
        if (!json_nodes_within(root, limits.maximum_json_nodes, limits.maximum_nesting_depth)) {
            result.diagnostics.push_back(
                {diagnostic_severity_t::error, native_project_diagnostic_code_t::resource_limit,
                 "native project exceeds the configured JSON node or nesting-depth limit", "$"});
            return result;
        }
        auto bytes = root.dump(2);
        bytes.push_back('\n');
        if (bytes.size() > limits.maximum_source_bytes) {
            result.diagnostics.push_back(
                {diagnostic_severity_t::error, native_project_diagnostic_code_t::resource_limit,
                 "serialized native project exceeds the configured byte limit", "$"});
            return result;
        }
        result.bytes = std::move(bytes);
    } catch (const std::exception& exception) {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, native_project_diagnostic_code_t::serialization_failure,
             std::string("native project serialization failed: ") + exception.what(), "$"});
    }
    return result;
}

auto parse_native_project(std::string_view bytes, native_project_limits_t limits)
    -> native_project_parse_result_t {
    return native_project_internal::parse_native_project_with_migrations(
        bytes, limits, native_project_schema_version, native_project_internal::default_migrations());
}

namespace native_project_internal {

auto parse_native_project_with_migrations(std::string_view bytes, native_project_limits_t limits,
                                          std::uint32_t target_version,
                                          std::span<const migration_t> table)
    -> native_project_parse_result_t {
    native_project_parse_result_t result;
    if (bytes.size() > limits.maximum_source_bytes) {
        result.diagnostics.push_back({diagnostic_severity_t::error,
                                      native_project_diagnostic_code_t::resource_limit,
                                      "native project exceeds the configured byte limit", "$"});
        return result;
    }
    if (!lexical_nesting_within(bytes, limits.maximum_nesting_depth)) {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, native_project_diagnostic_code_t::resource_limit,
             "native project exceeds the configured nesting-depth limit", "$"});
        return result;
    }
    json_limit_sax_t sax{limits.maximum_json_nodes, limits.maximum_nesting_depth};
    if (!json::sax_parse(bytes.begin(), bytes.end(), &sax)) {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error,
             sax.limit_exceeded() ? native_project_diagnostic_code_t::resource_limit
                                  : native_project_diagnostic_code_t::malformed_json,
             sax.limit_exceeded()
                 ? "native project exceeds the configured JSON node or nesting-depth limit"
                 : "native project JSON is malformed",
             "$"});
        return result;
    }
    try {
        const auto root = json::parse(bytes);
        if (!root.is_object() || root.value("format", std::string{}) != native_project_format) {
            result.diagnostics.push_back({diagnostic_severity_t::error,
                                          native_project_diagnostic_code_t::unsupported_version,
                                          "input is not a wormedit native project", "$.format"});
            return result;
        }
        if (root.size() != 3 || !root.contains("format_version") || !root.contains("document")) {
            result.diagnostics.push_back(
                {diagnostic_severity_t::error, native_project_diagnostic_code_t::malformed_json,
                 "native project envelope fields do not match the selected schema version", "$"});
            return result;
        }
        if (!root.at("format_version").is_number_unsigned() ||
            root.at("format_version").get<std::uint64_t>() != native_project_format_version) {
            result.diagnostics.push_back({diagnostic_severity_t::error,
                                          native_project_diagnostic_code_t::unsupported_version,
                                          "native project format version is unsupported",
                                          "$.format_version"});
            return result;
        }
        auto document = root.at("document");
        if (const auto outcome = migrate_document(document, target_version, table);
            outcome.status != migration_status_t::unchanged &&
            outcome.status != migration_status_t::migrated) {
            result.diagnostics.push_back(
                {diagnostic_severity_t::error, migration_diagnostic_code(outcome.status),
                 outcome.message, "$.document.schema_version"});
            return result;
        }
        if (!strict_numeric_shape(document)) {
            result.diagnostics.push_back(
                {diagnostic_severity_t::error, native_project_diagnostic_code_t::malformed_json,
                 "native project numeric fields must use exact integer types and ranges",
                 "$.document"});
            return result;
        }
        auto song = document.get<song_document_t>();
        const json canonical_document = song;
        if (!same_schema_shape(document, canonical_document)) {
            result.diagnostics.push_back(
                {diagnostic_severity_t::error, native_project_diagnostic_code_t::malformed_json,
                 "native project fields do not match the selected schema version", "$.document"});
            return result;
        }
        if (song.schema_version != target_version) {
            result.diagnostics.push_back({diagnostic_severity_t::error,
                                          native_project_diagnostic_code_t::unsupported_version,
                                          "document schema version is unsupported",
                                          "$.document.schema_version"});
            return result;
        }
        if (!valid_enum_domains(song, limits.maximum_nesting_depth)) {
            result.diagnostics.push_back(
                {diagnostic_severity_t::error, native_project_diagnostic_code_t::invalid_document,
                 "native project contains an enum value outside the schema", "$.document"});
            return result;
        }
        (void)normalize_legacy_bms_bgm(song);
        const auto validation = validate_document(song);
        for (const auto& diagnostic : validation) {
            if (diagnostic.severity == diagnostic_severity_t::error) {
                result.diagnostics.push_back({diagnostic.severity,
                                              native_project_diagnostic_code_t::invalid_document,
                                              graph_diagnostic_text(diagnostic), "$.document"});
            }
        }
        if (!result.has_errors()) {
            result.song = std::move(song);
        }
    } catch (const json::exception& exception) {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, native_project_diagnostic_code_t::malformed_json,
             std::string("native project JSON is malformed: ") + exception.what(), "$"});
    } catch (const std::exception& exception) {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, native_project_diagnostic_code_t::malformed_json,
             std::string("native project payload is malformed: ") + exception.what(), "$"});
    }
    return result;
}

} // namespace native_project_internal

} // namespace wormedit

#include "../core/text_util.h"
#include <wormedit/formats/osu_mania.h>

#include <wormedit/core/rational.h>
#include <wormedit/core/numeric_text.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/snap.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <set>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit {
namespace {

struct source_line_t {
    std::string_view text;
    std::string_view raw_text;
    std::string section;
    std::size_t order{};
    std::size_t byte_offset{};
};

struct field_t {
    std::string section;
    std::string key;
    std::string value;
    std::string raw_line;
    std::size_t order{};
};

struct timing_point_t {
    long double time{};
    long double beat_length{};
    std::int64_t meter{4};
    std::int64_t sample_set{};
    std::int64_t sample_index{};
    std::int64_t volume{};
    bool uninherited{};
    std::uint32_t effects{};
    std::size_t order{};
    std::string raw_line;
};

struct hit_object_t {
    std::int64_t x{};
    long double time{};
    std::string time_spelling;
    std::uint64_t type{};
    std::int64_t hit_sound{};
    std::optional<long double> end_time;
    std::optional<std::string> end_time_spelling;
    std::string hit_sample;
    std::string raw_line;
    std::size_t order{};
};

struct pending_visual_t {
    long double time{};
    std::string path;
    asset_kind_t kind{asset_kind_t::image};
    visual_event_kind_t event_kind{visual_event_kind_t::background};
    std::size_t order{};
    // "xOffset,yOffset" as spelled in the source, empty when absent.
    std::string offsets;
};

struct pending_resource_t {
    std::string path;
    asset_kind_t kind{asset_kind_t::audio};
    resource_role_t role{resource_role_t::sample};
    std::size_t order{};
    std::optional<long double> time;
    bool sample_event{};
    // "layer,volume" as spelled in the source, empty when absent.
    std::string sample_fields;
};

struct parsed_event_lines_t {
    std::vector<pending_visual_t> visuals;
    std::vector<pending_resource_t> resources;
    std::vector<source_line_t> storyboard_lines;
};

struct timeline_line_t {
    long double time{};
    long double beat_length{};
    std::int64_t meter_numerator{4};
    std::int64_t meter_denominator{4};
    bool begins_at_timing_point{};
};

struct measure_geometry_t {
    long double begin{};
    long double end{};
    std::uint64_t capacity{};
    std::vector<std::pair<long double, std::uint64_t>> row_boundaries;
};

struct timing_geometry_plan_t {
    std::vector<timing_point_t> anchors;
    std::vector<timeline_line_t> timeline;
    std::vector<std::pair<std::size_t, long double>> suppressed_lines;
};

struct editable_measure_plan_t {
    std::vector<measure_chunk_t> measures;
    std::vector<measure_geometry_t> geometries;
};

struct parse_abort_t {};

struct source_scan_result_t {
    std::vector<source_line_t> lines;
    std::vector<field_t> fields;
    std::optional<std::uint32_t> format_version;
};

struct osu_section_lines_t {
    std::vector<field_t> fields;
    std::vector<source_line_t> events;
    std::vector<source_line_t> timing;
    std::vector<source_line_t> hit_objects;
};

struct metadata_budget_t {
    std::size_t count{};
    std::size_t bytes{};
};

struct retained_source_entry_t {
    std::size_t order{};
    std::string key;
    std::string value;
};

auto trim(std::string_view text) -> std::string_view;
auto valid_utf8(std::string_view source) -> std::optional<std::size_t>;
template <typename value_t> auto parse_integer(std::string_view text, value_t& value) -> bool;
auto parse_number(std::string_view text, long double& value) -> bool;
auto split(std::string_view text, char delimiter) -> std::vector<std::string_view>;
auto split_csv(std::string_view line) -> std::optional<std::vector<std::string>>;

auto find_osu_value(const std::vector<field_t>& fields, std::string_view section,
                    std::string_view key) -> std::optional<std::string_view> {
    for (auto found = fields.rbegin(); found != fields.rend(); ++found)
        if (found->section == section && found->key == key) return found->value;
    return std::nullopt;
}

template <typename fail_t>
auto validate_osu_mania_keys(const std::vector<field_t>& fields, fail_t&& fail) -> std::int64_t {
    const auto mode_text = find_osu_value(fields, "General", "Mode");
    std::int64_t mode{};
    if (!mode_text || !parse_integer(*mode_text, mode) || mode != 3)
        fail(osu_mania_parse_diagnostic_code_t::unsupported_mode,
             "only osu! mode 3 (mania) is supported");
    const auto keys_text = find_osu_value(fields, "Difficulty", "CircleSize");
    std::int64_t keys{};
    if (!keys_text || !parse_integer(*keys_text, keys) || keys < minimum_osu_mania_keys || keys > maximum_osu_mania_keys)
        fail(osu_mania_parse_diagnostic_code_t::invalid_difficulty,
             "CircleSize must be an integer key count from 1 through 18");
    return keys;
}

auto make_source_group(const std::shared_ptr<const std::vector<std::byte>>& bytes,
                       const std::vector<field_t>& fields,
                       const std::vector<source_line_t>& events,
                       const std::vector<source_line_t>& timing,
                       const std::vector<source_line_t>& hits) -> format_metadata_t {
    format_metadata_t group;
    group.family = format_family_t::osu_mania;
    group.encoding.detected = text_encoding_t::utf8;
    group.encoding.original_bytes = bytes;
    group.encoding.unchanged_since_import = true;
    std::vector<retained_source_entry_t> retained;
    retained.reserve(fields.size() + events.size() + timing.size() + hits.size());
    for (const auto& field : fields)
        retained.push_back({field.order, field.section + "." + field.key, field.value});
    const auto append_lines = [&](const std::vector<source_line_t>& lines, std::string_view key) {
        for (const auto& line : lines)
            retained.push_back({line.order, std::string(key), std::string(line.text)});
    };
    append_lines(events, "Events[]");
    append_lines(timing, "TimingPoints[]");
    append_lines(hits, "HitObjects[]");
    std::ranges::stable_sort(retained, {}, &retained_source_entry_t::order);
    for (auto& entry : retained) {
        group.ordered_fields.emplace_back(std::move(entry.key), std::move(entry.value));
        group.ordered_field_source_orders.push_back(entry.order);
    }
    return group;
}

void retain_osu_source_value(format_metadata_t& group, std::string key, std::string value,
                             std::size_t order, bool has_identity = true) {
    group.ordered_fields.emplace_back(std::move(key), std::move(value));
    if (has_identity) group.ordered_field_source_orders.push_back(order);
}

template <typename object_id_t>
void append_osu_layout(chart_document_t& chart, std::int64_t keys, bool special_style,
                       object_id_t&& take_object_id) {
    lane_layout_t layout;
    layout.profile_id = "osu-mania-" + std::to_string(keys) + "k";
    layout.display_name = std::to_string(keys) + "K";
    layout.lanes.reserve(static_cast<std::size_t>(keys));
    for (std::int64_t column = 0; column < keys; ++column)
        layout.lanes.push_back({.id = lane_id_t{static_cast<std::uint64_t>(column + 1)},
                                .name = "Key " + std::to_string(column + 1),
                                .role = lane_role_t::playable,
                                .display_order = static_cast<std::uint32_t>(column)});
    assign_lane_key_colors(layout);
    if (special_style && (keys == 12 || keys == 16)) {
        auto profile = make_osu_mania_profile(static_cast<std::uint32_t>(keys), true);
        layout = std::move(profile->layout);
        chart.input_profile = std::move(profile->input);
    } else if (const auto input = make_default_input_profile_for(layout)) {
        chart.input_profile = *input;
    }
    chart.lane_layouts.push_back({.id = take_object_id(0),
                                  .position = {chart.measures.front().id, 0},
                                  .layout = std::move(layout)});
}

void populate_osu_metadata(song_document_t& song, chart_document_t& chart,
                           const std::vector<field_t>& fields,
                           std::optional<storyboard_document_model_t> storyboard,
                           const format_metadata_t& source_group) {
    const auto value = [&](std::string_view section, std::string_view key) {
        const auto found = find_osu_value(fields, section, key);
        return found ? std::string(*found) : std::string{};
    };
    song.metadata.title = value("Metadata", "Title");
    if (song.metadata.title.empty()) song.metadata.title = value("Metadata", "TitleUnicode");
    song.metadata.artist = value("Metadata", "Artist");
    if (song.metadata.artist.empty()) song.metadata.artist = value("Metadata", "ArtistUnicode");
    if (const auto tags = find_osu_value(fields, "Metadata", "Tags")) {
        std::string_view remaining = *tags;
        while (!remaining.empty()) {
            remaining = trim(remaining);
            if (remaining.empty()) break;
            const auto end = remaining.find_first_of(" \t");
            song.metadata.ordered_search_tags.emplace_back(remaining.substr(0, end));
            if (end == std::string_view::npos) break;
            remaining.remove_prefix(end);
        }
    }
    chart.metadata.name = value("Metadata", "Version");
    chart.metadata.author = value("Metadata", "Creator");
    if (storyboard) {
        auto name = chart.metadata.name.empty() ? "osu!mania storyboard" : chart.metadata.name;
        song.storyboards.push_back({.id = storyboard_id_t{1}, .name = std::move(name),
                                    .model = std::move(storyboard)});
    }
    chart.metadata.format_groups.push_back(source_group);
    song.metadata.format_groups.push_back(source_group);
}

template <typename resource_catalog_t>
auto resolve_hit_samples(const std::vector<hit_object_t>& objects,
                         resource_catalog_t& resources) -> std::vector<std::optional<resource_id_t>> {
    std::vector<std::optional<resource_id_t>> result;
    result.reserve(objects.size());
    for (const auto& object : objects) {
        std::optional<resource_id_t> sound;
        if (!object.hit_sample.empty()) {
            const auto parts = split(object.hit_sample, ':');
            const auto index = object.end_time ? std::size_t{5} : std::size_t{4};
            if (parts.size() > index && !parts[index].empty())
                sound = resources.add_audio(parts[index], resource_role_t::sample,
                                            "HitObjects.hitSample", object.order);
        }
        result.push_back(sound);
    }
    return result;
}

template <typename resource_catalog_t>
auto resolve_visual_resources(const std::vector<pending_visual_t>& visuals,
                              resource_catalog_t& resources)
    -> std::vector<std::optional<resource_id_t>> {
    std::vector<std::optional<resource_id_t>> result;
    result.reserve(visuals.size());
    for (const auto& visual : visuals)
        result.push_back(resources.add_visual(visual.path, visual.kind, resource_role_t::bga,
                                              "Events", visual.order));
    return result;
}

template <typename object_id_t, typename fail_t>
void append_osu_bookmarks(chart_document_t& chart,
                          std::optional<std::string_view> bookmark_text,
                          object_id_t&& take_object_id, fail_t&& fail) {
    if (!bookmark_text) return;
    for (const auto value : split(*bookmark_text, ',')) {
        std::int64_t milliseconds{};
        if (!parse_integer(value, milliseconds))
            fail(osu_mania_parse_diagnostic_code_t::malformed_field,
                 "Editor Bookmarks must be a comma-separated list of integer milliseconds");
        chart.bookmarks.push_back({.id = take_object_id(0), .label = {},
                                   .anchor = marker_anchor_t::audio_time,
                                   .audio_milliseconds = milliseconds});
    }
}

template <typename fail_t>
auto decode_osu_source(std::span<const std::byte> bytes,
                       const osu_mania_parse_options_t& options, std::string& source,
                       fail_t&& fail) -> std::size_t {
    if (bytes.size() > options.max_source_bytes)
        fail(osu_mania_parse_diagnostic_code_t::source_limit,
             "osu! source exceeds the configured byte limit");
    source.reserve(bytes.size());
    for (const auto byte : bytes)
        source.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    std::string_view text(source);
    std::size_t bom_size{};
    if (text.starts_with("\xef\xbb\xbf")) {
        text.remove_prefix(3);
        bom_size = 3;
    }
    if (const auto bad = valid_utf8(text))
        fail(osu_mania_parse_diagnostic_code_t::invalid_encoding,
             "osu! source must be valid UTF-8", 0, *bad + bom_size);
    return bom_size;
}

template <typename fail_t>
void account_metadata(metadata_budget_t& budget, std::size_t bytes, std::size_t order,
                      std::size_t offset, const osu_mania_parse_options_t& options,
                      fail_t&& fail) {
    if (budget.count >= options.max_metadata_fields || bytes > options.max_metadata_bytes ||
        budget.bytes > options.max_metadata_bytes - bytes)
        fail(osu_mania_parse_diagnostic_code_t::metadata_limit,
             "osu! metadata exceeds the configured field-count or byte limit", order, offset);
    ++budget.count;
    budget.bytes += bytes;
}

template <typename fail_t>
void partition_osu_line(const source_line_t& line, osu_section_lines_t& result,
                        metadata_budget_t& budget, const osu_mania_parse_options_t& options,
                        fail_t&& fail) {
    if (line.section == "Events") { result.events.push_back(line); return; }
    if (line.section == "TimingPoints") { result.timing.push_back(line); return; }
    if (line.section == "HitObjects") { result.hit_objects.push_back(line); return; }
    const auto colon = line.text.find(':');
    if (colon == std::string_view::npos) {
        if (line.section == "General" || line.section == "Editor" ||
            line.section == "Metadata" || line.section == "Difficulty" ||
            line.section == "Colours")
            fail(osu_mania_parse_diagnostic_code_t::malformed_field,
                 "known osu! key/value sections require a colon-delimited field", line.order,
                 line.byte_offset);
        account_metadata(budget, line.text.size(), line.order, line.byte_offset, options, fail);
        result.fields.push_back({line.section, "[]", std::string(line.text),
                                 std::string(line.text), line.order});
        return;
    }
    account_metadata(budget, line.text.size(), line.order, line.byte_offset, options, fail);
    result.fields.push_back({line.section, std::string(trim(line.text.substr(0, colon))),
                             std::string(trim(line.text.substr(colon + 1))),
                             std::string(line.text), line.order});
}

template <typename fail_t>
auto partition_osu_sections(const std::vector<source_line_t>& lines,
                            const osu_mania_parse_options_t& options, fail_t&& fail)
    -> osu_section_lines_t {
    osu_section_lines_t result;
    metadata_budget_t budget;
    for (const auto& line : lines) partition_osu_line(line, result, budget, options, fail);
    return result;
}

template <typename fail_t>
void consume_source_line(std::string_view trimmed, std::string_view raw_line,
                         std::string& current_section, std::size_t order,
                         std::size_t byte_offset, const osu_mania_parse_options_t& options,
                         source_scan_result_t& result, fail_t&& fail) {
    if (!result.format_version) {
        constexpr std::string_view prefix = "osu file format v";
        if (!trimmed.starts_with(prefix))
            fail(osu_mania_parse_diagnostic_code_t::malformed_field,
                 "the first osu! content line must declare its file format version", order,
                 byte_offset);
        const auto version_text = trim(trimmed.substr(prefix.size()));
        std::uint32_t version{};
        if (!parse_integer(version_text, version))
            fail(osu_mania_parse_diagnostic_code_t::malformed_field,
                 "osu! file format version must be an unsigned integer", order,
                 byte_offset + prefix.size());
        if (version != 14 && version != 128)
            fail(osu_mania_parse_diagnostic_code_t::unsupported_version,
                 "only stable v14 and lazer v128 osu! file formats are supported", order,
                 byte_offset + prefix.size());
        result.format_version = version;
        result.fields.push_back({"osu", "file_format", std::string(version_text),
                                 std::string(trimmed), order});
    } else if (trimmed.front() == '[' && trimmed.back() == ']') {
        if (trimmed.size() <= 2)
            fail(osu_mania_parse_diagnostic_code_t::invalid_section,
                 "section name cannot be empty", order, byte_offset);
        current_section = std::string(trim(trimmed.substr(1, trimmed.size() - 2)));
    } else if (current_section.empty() && trimmed.starts_with("osu file format v")) {
        fail(osu_mania_parse_diagnostic_code_t::malformed_field,
             "osu! source declares its file format version more than once", order, byte_offset);
    } else {
        result.lines.push_back({trimmed, raw_line, current_section, order, byte_offset});
        if (result.lines.size() > options.max_lines)
            fail(osu_mania_parse_diagnostic_code_t::source_limit,
                 "osu! source exceeds the configured line limit", order, byte_offset);
    }
}

template <typename fail_t>
auto scan_source_lines(std::string_view text, std::size_t bom_size,
                       const osu_mania_parse_options_t& options, fail_t&& fail)
    -> source_scan_result_t {
    source_scan_result_t result;
    std::string current_section;
    std::size_t offset{};
    std::size_t order{};
    while (offset < text.size()) {
        const auto newline = text.find('\n', offset);
        auto line = text.substr(offset, newline == std::string_view::npos
                                           ? text.size() - offset : newline - offset);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto trimmed = trim(line);
        if (!trimmed.empty() && !trimmed.starts_with("//")) {
            consume_source_line(trimmed, line, current_section, order, offset + bom_size,
                                options, result, fail);
            ++order;
        }
        if (newline == std::string_view::npos) break;
        offset = newline + 1;
    }
    if (!result.format_version)
        fail(osu_mania_parse_diagnostic_code_t::malformed_field,
             "osu! source is missing its file format version header");
    return result;
}

template <typename fail_t>
auto parse_timing_points(const std::vector<source_line_t>& lines, fail_t&& fail)
    -> std::vector<timing_point_t> {
    std::vector<timing_point_t> result;
    result.reserve(lines.size());
    for (const auto& line : lines) {
        const auto parts = split(line.text, ',');
        timing_point_t point;
        std::int64_t uninherited{};
        std::uint64_t effects{};
        if (parts.size() < 8 || !parse_number(parts[0], point.time) ||
            !parse_number(parts[1], point.beat_length) || !parse_integer(parts[2], point.meter) ||
            !parse_integer(parts[3], point.sample_set) ||
            !parse_integer(parts[4], point.sample_index) || !parse_integer(parts[5], point.volume) ||
            !parse_integer(parts[6], uninherited) || !parse_integer(parts[7], effects) ||
            (uninherited != 0 && uninherited != 1) || point.meter < 1 || point.meter > 32 ||
            point.beat_length == 0.0L || (uninherited == 1 && point.beat_length < 0.0L) ||
            (uninherited == 0 && point.beat_length > 0.0L) || effects > UINT32_MAX) {
            fail(osu_mania_parse_diagnostic_code_t::invalid_timing_point,
                 "invalid osu! timing point; expected finite time, nonzero beat length, meter, "
                 "and flags",
                 line.order, line.byte_offset);
        }
        point.uninherited = uninherited == 1;
        point.effects = static_cast<std::uint32_t>(effects);
        point.order = line.order;
        point.raw_line = std::string(line.text);
        result.push_back(std::move(point));
    }
    return result;
}

auto parse_osu_object_time(std::string_view text, std::uint32_t version, long double& value)
    -> bool {
    if (version != 14) return parse_number(text, value);
    std::int64_t integer{};
    if (!parse_integer(text, integer)) return false;
    value = static_cast<long double>(integer);
    return true;
}

template <typename fail_t>
void parse_hold_fields(const std::vector<std::string>& parts, hit_object_t& object,
                       std::uint32_t version, const source_line_t& line, fail_t&& fail) {
    if (parts.size() < 6)
        fail(osu_mania_parse_diagnostic_code_t::invalid_hit_object,
             "hold object is missing its end-time/sample field", line.order, line.byte_offset);
    const auto hold_parts = split(parts[5], ':');
    long double end_time{};
    if (hold_parts.empty() || !parse_osu_object_time(hold_parts.front(), version, end_time) ||
        end_time < object.time || end_time > 604'800'000.0L)
        fail(osu_mania_parse_diagnostic_code_t::invalid_hit_object,
             version == 14 ? "hold end time must be an integer at or after its head"
                           : "hold end time must be a finite decimal at or after its head",
             line.order, line.byte_offset);
    object.end_time = end_time;
    object.end_time_spelling = std::string(hold_parts.front());
    object.hit_sample = parts[5];
}

template <typename fail_t>
auto parse_hit_object_line(const source_line_t& line, std::uint32_t version, fail_t&& fail)
    -> hit_object_t {
    const auto parts = split_csv(line.text);
    hit_object_t object;
    if (!parts || parts->size() < 5 || !parse_integer((*parts)[0], object.x) ||
        !parse_osu_object_time((*parts)[2], version, object.time) ||
        !parse_integer((*parts)[3], object.type) || !parse_integer((*parts)[4], object.hit_sound) ||
        object.x < 0 || object.x > 512 || object.time < -86'400'000.0L ||
        object.time > 604'800'000.0L || object.type == 0)
        fail(osu_mania_parse_diagnostic_code_t::invalid_hit_object,
             "malformed osu! hit object or out-of-range lane/time", line.order, line.byte_offset);
    object.time_spelling = (*parts)[2];
    const bool hold = (object.type & 128U) != 0;
    const bool circle = (object.type & 1U) != 0;
    if ((!hold && !circle) || (object.type & (2U | 8U)) != 0)
        fail(osu_mania_parse_diagnostic_code_t::invalid_hit_object,
             "osu!mania supports tap and hold objects; sliders and spinners are not supported",
             line.order, line.byte_offset);
    if (hold) parse_hold_fields(*parts, object, version, line, fail);
    else if (parts->size() >= 6) object.hit_sample = (*parts)[5];
    object.order = line.order;
    object.raw_line = std::string(line.text);
    return object;
}

auto event_offsets(const std::vector<std::string>& parts) -> std::string {
    return parts.size() >= 5 ? parts[3] + "," + parts[4] : std::string{};
}

auto sample_event_fields(const std::vector<std::string>& parts) -> std::string {
    return parts[2] + "," + (parts.size() >= 5 ? parts[4] : std::string{"100"});
}

template <typename fail_t>
auto parse_event_line(const source_line_t& line, fail_t&& fail)
    -> parsed_event_lines_t {
    parsed_event_lines_t result;
    const auto parts = split_csv(line.text);
    if (!parts || parts->empty())
        fail(osu_mania_parse_diagnostic_code_t::malformed_field,
             "malformed comma-separated osu! event", line.order, line.byte_offset);
    const auto& kind = parts->front();
    const auto indentation = line.raw_text.find_first_not_of(" \t");
    const bool indented = indentation != std::string_view::npos && indentation != 0;
    const bool background = kind == "0" || kind == "Background";
    const bool video = kind == "Video" || kind == "1";
    if (!indented && (background || video) && parts->size() >= 3) {
        long double event_time{};
        if (!parse_number((*parts)[1], event_time))
            fail(osu_mania_parse_diagnostic_code_t::malformed_field,
                 "background/video event time must be finite", line.order, line.byte_offset);
        result.visuals.push_back(
            {event_time, (*parts)[2], video ? asset_kind_t::video : asset_kind_t::image,
             video ? visual_event_kind_t::video : visual_event_kind_t::background, line.order,
             event_offsets(*parts)});
    } else if (!indented && (kind == "Sprite" || kind == "Animation") && parts->size() >= 4) {
        result.resources.push_back({(*parts)[3], asset_kind_t::image, resource_role_t::bga,
                                    line.order, std::nullopt, false});
        result.storyboard_lines.push_back(line);
    } else if (!indented && kind == "Sample" && parts->size() >= 4) {
        long double sample_time{};
        if (!parse_number((*parts)[1], sample_time))
            fail(osu_mania_parse_diagnostic_code_t::malformed_field,
                 "sample event time must be finite", line.order, line.byte_offset);
        result.resources.push_back(
            {(*parts)[3], asset_kind_t::audio, resource_role_t::sample, line.order, sample_time,
             true, sample_event_fields(*parts)});
    } else {
        result.storyboard_lines.push_back(line);
    }
    return result;
}

template <typename fail_t>
auto parse_event_lines(const std::vector<source_line_t>& lines, fail_t&& fail)
    -> parsed_event_lines_t {
    parsed_event_lines_t result;
    for (const auto& line : lines) {
        auto parsed = parse_event_line(line, fail);
        result.visuals.insert(result.visuals.end(), parsed.visuals.begin(), parsed.visuals.end());
        result.resources.insert(result.resources.end(), parsed.resources.begin(), parsed.resources.end());
        result.storyboard_lines.insert(result.storyboard_lines.end(), parsed.storyboard_lines.begin(),
                                       parsed.storyboard_lines.end());
    }
    return result;
}

template <typename fail_t>
void validate_storyboard_indentation(const std::vector<storyboard_node_t>& nodes,
                                     const std::vector<source_line_t>& lines,
                                     std::size_t expected_indent, fail_t&& fail) {
    for (const auto& node : nodes) {
        const auto& source_line = lines[static_cast<std::size_t>(node.source_order - 1)];
        const auto first_content = source_line.raw_text.find_first_not_of(" \t");
        const auto indentation = first_content == std::string_view::npos
                                     ? source_line.raw_text.size() : first_content;
        if (indentation != expected_indent)
            fail(osu_mania_parse_diagnostic_code_t::unsupported_storyboard,
                 "embedded storyboard uses a nested directive block that is not represented by "
                 "Sprite/loop nodes",
                 source_line.order, source_line.byte_offset);
        if (node.kind == storyboard_node_kind_t::loop)
            validate_storyboard_indentation(node.children, lines, expected_indent + 1, fail);
    }
}

template <typename order_map_t>
void remap_storyboard_nodes(std::vector<storyboard_node_t>& nodes, const order_map_t& source_order_for) {
    for (auto& node : nodes) {
        const auto order = source_order_for(node.source_order);
        node.source_order = order;
        node.command.source_order = order;
        remap_storyboard_nodes(node.children, source_order_for);
    }
}

auto count_storyboard_nodes(const std::vector<storyboard_node_t>& nodes) -> std::size_t {
    std::size_t count{};
    for (const auto& node : nodes) count += 1 + count_storyboard_nodes(node.children);
    return count;
}

template <typename fail_t, typename account_events_t>
auto parse_embedded_storyboard(const std::vector<source_line_t>& lines,
                               const osu_mania_parse_options_t& options,
                               fail_t&& fail, account_events_t&& account_events)
    -> std::optional<storyboard_document_model_t> {
    if (lines.empty()) return std::nullopt;
    std::string source;
    std::vector<std::size_t> orders;
    orders.reserve(lines.size());
    for (const auto& line : lines) {
        source.append(line.raw_text);
        source.push_back('\n');
        orders.push_back(line.order);
    }
    const auto node_limit = std::min<std::size_t>(options.max_events, 250'000);
    const auto line_limit = std::min<std::size_t>(options.max_lines, 250'000);
    auto parsed = parse_osb(source, {.maximum_input_bytes = options.max_source_bytes,
                                     .maximum_lines = line_limit,
                                     .maximum_nodes = node_limit});
    if (!parsed.document) {
        const auto index = parsed.diagnostics.empty() || parsed.diagnostics.front().line == 0
                               ? std::size_t{0}
                               : std::min(parsed.diagnostics.front().line - 1, orders.size() - 1);
        const auto bounded = !parsed.diagnostics.empty() &&
            (parsed.diagnostics.front().code == storyboard_diagnostic_code_t::input_too_large ||
             parsed.diagnostics.front().code == storyboard_diagnostic_code_t::line_limit ||
             parsed.diagnostics.front().code == storyboard_diagnostic_code_t::node_limit ||
             parsed.diagnostics.front().code == storyboard_diagnostic_code_t::nesting_limit ||
             parsed.diagnostics.front().code == storyboard_diagnostic_code_t::capacity_exceeded);
        const auto message = parsed.diagnostics.empty() ? "OSB parser failed without a diagnostic"
                                                        : parsed.diagnostics.front().message;
        fail(bounded ? osu_mania_parse_diagnostic_code_t::storyboard_limit
                     : osu_mania_parse_diagnostic_code_t::invalid_storyboard,
             "invalid embedded osu! storyboard: " + message, orders[index], lines[index].byte_offset);
    }
    const auto source_order_for = [&](std::uint64_t order) {
        return order == 0 || order > orders.size() ? std::size_t{}
                                                   : orders[static_cast<std::size_t>(order - 1)];
    };
    auto& model = *parsed.document;
    for (const auto& sprite : model.sprites)
        validate_storyboard_indentation(sprite.nodes, lines, 1, fail);
    for (auto& sprite : model.sprites) {
        const auto order = source_order_for(sprite.source_order);
        sprite.source_order = order;
        sprite.stable_id = static_cast<std::uint64_t>(order) + 1;
        remap_storyboard_nodes(sprite.nodes, source_order_for);
    }
    for (auto& line : model.retained_top_level_lines) line.source_order = source_order_for(line.source_order);
    std::size_t node_count{};
    for (const auto& sprite : model.sprites) node_count += count_storyboard_nodes(sprite.nodes);
    auto output_lines = model.sprites.size() + model.retained_top_level_lines.size();
    if (node_count > std::numeric_limits<std::size_t>::max() - output_lines)
        fail(osu_mania_parse_diagnostic_code_t::storyboard_limit,
             "embedded storyboard output line count overflowed");
    output_lines += node_count;
    if (output_lines > lines.size()) account_events(output_lines - lines.size());
    return std::move(model);
}

inline constexpr long double barline_alignment_tolerance_ms = 1.0L;

auto trim(std::string_view text) -> std::string_view {
    return text_util::trim(text, " \t\r");
}

auto valid_utf8(std::string_view text) -> std::optional<std::size_t> {
    return text_util::invalid_utf8_offset(text);
}

template <class Integer> auto parse_integer(std::string_view text, Integer& value) -> bool {
    return wormedit::parse_integer(trim(text), value);
}

auto parse_number(std::string_view text, long double& value) -> bool {
    text = trim(text);
    if (text.empty())
        return false;
    double parsed_value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), parsed_value,
                                        std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(parsed_value))
        return false;
    value = static_cast<long double>(parsed_value);
    return true;
}

auto split(std::string_view text, char delimiter) -> std::vector<std::string_view> {
    std::vector<std::string_view> fields;
    while (true) {
        const auto position = text.find(delimiter);
        if (position == std::string_view::npos) {
            fields.push_back(text);
            return fields;
        }
        fields.push_back(text.substr(0, position));
        text.remove_prefix(position + 1);
    }
}

auto split_csv(std::string_view line) -> std::optional<std::vector<std::string>> {
    std::vector<std::string> fields;
    std::string current;
    bool quoted{};
    for (std::size_t index = 0; index < line.size(); ++index) {
        const auto character = line[index];
        if (character == '"') {
            if (quoted && index + 1 < line.size() && line[index + 1] == '"') {
                current.push_back('"');
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (character == ',' && !quoted) {
            fields.push_back(std::string(trim(current)));
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    if (quoted)
        return std::nullopt;
    fields.push_back(std::string(trim(current)));
    return fields;
}

using text_util::path_from_utf8;

auto safe_relative_path(std::string_view value) -> std::optional<std::filesystem::path> {
    if (value.empty() || value.find('\0') != std::string_view::npos)
        return std::nullopt;
    std::string normalized(value);
    std::ranges::replace(normalized, '\\', '/');
    if (normalized.front() == '/' || (normalized.size() >= 2 &&
                                      ((normalized[0] >= 'A' && normalized[0] <= 'Z') ||
                                       (normalized[0] >= 'a' && normalized[0] <= 'z')) &&
                                      normalized[1] == ':'))
        return std::nullopt;
    auto path = path_from_utf8(normalized);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
        return std::nullopt;
    for (const auto& component : path)
        if (component == "..")
            return std::nullopt;
    return path.lexically_normal();
}

auto rationalize(long double value, std::uint64_t maximum_denominator = 1'000'000'000ULL)
    -> std::optional<rational_t> {
    if (!std::isfinite(value) || value <= 0.0L ||
        value > static_cast<long double>(std::numeric_limits<double>::max()))
        return std::nullopt;
    const auto result = rationalize_positive_double(static_cast<double>(value),
                                                    {.maximum_denominator = maximum_denominator,
                                                     .iteration_limit = 96,
                                                     .maximum_absolute_error = 1.0e-10L});
    return result ? std::optional{result->value} : std::nullopt;
}

auto format_double(long double value) -> std::string {
    char buffer[96];
    const auto converted =
        std::to_chars(std::begin(buffer), std::end(buffer), static_cast<double>(value),
                      std::chars_format::general, std::numeric_limits<double>::max_digits10);
    if (converted.ec != std::errc{})
        return {};
    return std::string(buffer, converted.ptr);
}

auto timing_period(const timing_point_t& point) -> long double {
    return point.beat_length * static_cast<long double>(point.meter);
}

void add_timeline_line(std::vector<timeline_line_t>& timeline, long double time,
                       const timing_point_t& point, bool anchor) {
    if (!timeline.empty() && std::fabs(timeline.back().time - time) < 1.0e-12L) {
        if (anchor) {
            timeline.back().beat_length = point.beat_length;
            timeline.back().meter_numerator = point.meter;
            timeline.back().meter_denominator = 4;
            timeline.back().begins_at_timing_point = true;
        }
        return;
    }
    timeline.push_back({time, point.beat_length, point.meter, 4, anchor});
}

template <typename fail_t>
auto effective_timing_anchors(const std::vector<timing_point_t>& points, fail_t&& fail)
    -> std::vector<timing_point_t> {
    std::vector<timing_point_t> anchors;
    for (const auto& point : points)
        if (point.uninherited) anchors.push_back(point);
    std::ranges::stable_sort(anchors, [](const auto& left, const auto& right) {
        if (left.time != right.time) return left.time < right.time;
        return left.order < right.order;
    });
    if (anchors.empty())
        fail(osu_mania_parse_diagnostic_code_t::missing_timing_anchor,
             "chart has no uninherited timing point from which to derive bar phase");
    std::vector<timing_point_t> unique;
    for (const auto& point : anchors) {
        if (!unique.empty() && unique.back().time == point.time) unique.back() = point;
        else unique.push_back(point);
    }
    return unique;
}

auto event_time_bounds(const std::vector<timing_point_t>& points,
                       const std::vector<hit_object_t>& objects,
                       const std::vector<pending_visual_t>& visuals,
                       const timing_point_t& first) -> std::pair<long double, long double> {
    std::vector<long double> times;
    times.reserve(objects.size() * 2 + visuals.size() + points.size());
    for (const auto& object : objects) {
        times.push_back(object.time);
        if (object.end_time) times.push_back(*object.end_time);
    }
    for (const auto& visual : visuals) times.push_back(visual.time);
    for (const auto& point : points) times.push_back(point.time);
    if (times.empty()) times.push_back(first.time);
    const auto [minimum, maximum] = std::minmax_element(times.begin(), times.end());
    return {*minimum, *maximum};
}

template <typename fail_t>
void append_prefix_lines(const std::vector<timing_point_t>& anchors, long double minimum_event,
                         std::size_t max_measures, std::vector<timeline_line_t>& timeline,
                         fail_t&& fail) {
    const auto& first = anchors.front();
    const auto period = timing_period(first);
    if (!std::isfinite(period) || period <= 0.0L)
        fail(osu_mania_parse_diagnostic_code_t::invalid_timing_point,
             "first effective timing point has an invalid bar duration", first.order);
    std::vector<long double> prefix;
    auto cursor = first.time;
    std::size_t generated{};
    while (cursor > minimum_event) {
        cursor -= period;
        prefix.push_back(cursor);
        if (++generated > max_measures)
            fail(osu_mania_parse_diagnostic_code_t::measure_limit,
                 "negative-time objects require more measures than the configured limit");
    }
    if (prefix.empty() && (first.effects & 8U) != 0) {
        cursor -= period;
        prefix.push_back(cursor);
    }
    for (auto line = prefix.rbegin(); line != prefix.rend(); ++line)
        add_timeline_line(timeline, *line, first, false);
    if ((first.effects & 8U) == 0) add_timeline_line(timeline, first.time, first, true);
}

template <typename fail_t>
void append_anchor_span(const std::vector<timing_point_t>& anchors, std::size_t index,
                        long double maximum_event, std::size_t max_measures,
                        std::vector<timeline_line_t>& timeline,
                        std::vector<std::pair<std::size_t, long double>>& suppressed,
                        fail_t&& fail) {
    const auto& anchor = anchors[index];
    const auto next = index + 1 < anchors.size()
                          ? std::optional<long double>(anchors[index + 1].time)
                          : std::nullopt;
    const auto period = timing_period(anchor);
    auto candidate = anchor.time + period;
    if (!std::isfinite(candidate) || candidate <= anchor.time)
        fail(osu_mania_parse_diagnostic_code_t::invalid_timing_point,
             "timing point produces a non-positive or overflowing measure", anchor.order);
    std::size_t generated{};
    if (next) {
        const bool suppressed_barline = candidate >= *next - 1.0e-12L &&
                                         candidate - *next <= barline_alignment_tolerance_ms;
        if (suppressed_barline) {
            suppressed.emplace_back(anchor.order, candidate - *next);
        } else {
            while (candidate < *next - 1.0e-12L) {
                add_timeline_line(timeline, candidate, anchor, false);
                candidate += period;
                if (!std::isfinite(candidate) || ++generated > max_measures)
                    fail(osu_mania_parse_diagnostic_code_t::measure_limit,
                         "timing-point span exceeds the configured measure limit", anchor.order);
            }
        }
        if ((anchors[index + 1].effects & 8U) == 0)
            add_timeline_line(timeline, *next, anchors[index + 1], true);
        return;
    }
    while (candidate <= maximum_event + 1.0e-12L) {
        add_timeline_line(timeline, candidate, anchor, false);
        candidate += period;
        if (!std::isfinite(candidate) || ++generated > max_measures)
            fail(osu_mania_parse_diagnostic_code_t::measure_limit,
                 "chart tail exceeds the configured measure limit", anchor.order);
    }
    add_timeline_line(timeline, candidate, anchor, false);
}

template <typename fail_t>
auto derive_timing_geometry(const std::vector<timing_point_t>& points,
                            const std::vector<hit_object_t>& objects,
                            const std::vector<pending_visual_t>& visuals,
                            const osu_mania_parse_options_t& options, fail_t&& fail)
    -> timing_geometry_plan_t {
    timing_geometry_plan_t result;
    result.anchors = effective_timing_anchors(points, fail);
    const auto [minimum, maximum] = event_time_bounds(points, objects, visuals,
                                                       result.anchors.front());
    append_prefix_lines(result.anchors, minimum, options.max_measures, result.timeline, fail);
    for (std::size_t index = 0; index < result.anchors.size(); ++index) {
        append_anchor_span(result.anchors, index, maximum, options.max_measures,
                           result.timeline, result.suppressed_lines, fail);
        if (result.timeline.size() > options.max_measures + 1)
            fail(osu_mania_parse_diagnostic_code_t::measure_limit,
                 "chart exceeds the configured measure limit", result.anchors[index].order);
    }
    if (result.timeline.size() < 2) {
        const auto& last = result.anchors.back();
        add_timeline_line(result.timeline, last.time + timing_period(last), last, false);
    }
    return result;
}

auto quarter_beats_between(const std::vector<timing_point_t>& anchors, long double begin,
                           long double end) -> long double {
    std::size_t active{};
    std::size_t next{};
    for (std::size_t index = 0; index < anchors.size(); ++index) {
        if (anchors[index].time <= begin) {
            active = index;
            next = index + 1;
        } else {
            next = index;
            break;
        }
    }
    auto cursor = begin;
    long double beats{};
    for (std::size_t index = next; index < anchors.size(); ++index) {
        const auto& anchor = anchors[index];
        if (anchor.time >= end) break;
        if (anchor.time > cursor) {
            beats += (anchor.time - cursor) / anchors[active].beat_length;
            cursor = anchor.time;
        }
        active = index;
    }
    if (end > cursor) beats += (end - cursor) / anchors[active].beat_length;
    return beats;
}

template <typename fail_t>
auto quantized_measure_length(long double beats, bool& quantized, fail_t&& fail) -> rational_t {
    constexpr auto divisions = default_divisions_per_standard_measure;
    const auto raw_rows = beats * static_cast<long double>(divisions) / 4.0L;
    if (!std::isfinite(raw_rows) || raw_rows > static_cast<long double>(divisions) * 1'000'000.0L)
        fail(osu_mania_parse_diagnostic_code_t::measure_limit,
             "osu! timing span exceeds the bounded editor measure limit");
    const auto rows = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::llround(raw_rows)));
    quantized = quantized || std::fabs(raw_rows - static_cast<long double>(rows)) > 1.0e-9L;
    const auto divisor = std::gcd(rows, divisions);
    return {rows / divisor, divisions / divisor};
}

template <typename fail_t>
auto derive_measure_lengths(const std::vector<timeline_line_t>& timeline,
                           const std::vector<timing_point_t>& anchors, bool& quantized,
                           fail_t&& fail) -> std::pair<std::vector<rational_t>, std::uint64_t> {
    std::vector<rational_t> lengths;
    lengths.reserve(timeline.size() - 1);
    constexpr std::uint64_t divisions = default_divisions_per_standard_measure;
    for (std::size_t index = 0; index + 1 < timeline.size(); ++index) {
        const auto& begin = timeline[index];
        const auto end = timeline[index + 1].time;
        const auto duration = end - begin.time;
        const auto beats = quarter_beats_between(anchors, begin.time, end);
        if (!std::isfinite(duration) || duration <= 0.0L || !std::isfinite(beats) || beats <= 0.0L)
            fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                 "measure timeline contains a non-positive interval");
        lengths.push_back(quantized_measure_length(beats, quantized, fail));
    }
    return {std::move(lengths), divisions};
}

auto measure_row_boundaries(long double begin, long double end,
                            const std::vector<timing_point_t>& anchors,
                            std::uint64_t& capacity, bool& quantized)
    -> std::vector<std::pair<long double, std::uint64_t>> {
    std::vector<std::pair<long double, std::uint64_t>> result{{begin, 0}};
    for (const auto& anchor : anchors)
        if (anchor.time > begin && anchor.time < end) result.emplace_back(anchor.time, 0);
    result.emplace_back(end, 0);
    if (capacity < result.size() - 1) {
        capacity = result.size() - 1;
        quantized = true;
    }
    const auto beats = quarter_beats_between(anchors, begin, end);
    for (std::size_t index = 1; index + 1 < result.size(); ++index) {
        const auto fraction = quarter_beats_between(anchors, begin, result[index].first) / beats;
        const auto nearest = static_cast<std::uint64_t>(std::llround(fraction * capacity));
        const auto maximum = capacity - (result.size() - index - 1);
        const auto aligned = std::clamp(nearest, result[index - 1].second + 1, maximum);
        quantized = quantized || aligned != nearest;
        result[index].second = aligned;
    }
    result.back().second = capacity;
    return result;
}

template <typename fail_t>
auto make_editable_measures(const std::vector<rational_t>& lengths,
                            const std::vector<timeline_line_t>& timeline,
                            const std::vector<timing_point_t>& anchors, std::uint64_t divisions,
                            bool& quantized, fail_t&& fail) -> editable_measure_plan_t {
    editable_measure_plan_t result;
    result.measures.reserve(lengths.size());
    result.geometries.reserve(lengths.size());
    for (std::size_t index = 0; index < lengths.size(); ++index) {
        const auto capacity = validate_measure(divisions, {.length = lengths[index]});
        if (!capacity)
            fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                 "derived osu! measure exceeds the editor's exact row capacity");
        measure_chunk_t measure;
        measure.id = measure_id_t{static_cast<std::uint64_t>(index + 1)};
        measure.definition = {.length = lengths[index], .meter = std::nullopt};
        const auto& line = timeline[index];
        auto rows = *capacity.row_capacity;
        auto boundaries = measure_row_boundaries(line.time, timeline[index + 1].time,
                                                 anchors, rows, quantized);
        const auto common = std::gcd(rows, divisions);
        measure.definition.length = {rows / common, divisions / common};
        const auto expected = static_cast<long double>(line.meter_numerator) /
                              static_cast<long double>(line.meter_denominator);
        const auto actual = static_cast<long double>(rows) / divisions;
        const bool crosses_timing_point = std::ranges::any_of(anchors, [&](const auto& anchor) {
            return anchor.time > line.time && anchor.time < timeline[index + 1].time;
        });
        if (!crosses_timing_point && std::fabs(actual - expected) < 1.0e-9L &&
            line.meter_numerator > 0 && line.meter_denominator > 0)
            measure.definition.meter = meter_t{static_cast<std::uint32_t>(line.meter_numerator),
                                               static_cast<std::uint32_t>(line.meter_denominator)};
        result.measures.push_back(std::move(measure));
        result.geometries.push_back(
            {line.time, timeline[index + 1].time, rows, std::move(boundaries)});
    }
    if (result.measures.empty())
        fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
             "osu! timing did not produce an editable measure");
    return result;
}

auto position_at_time(long double time, const std::vector<measure_geometry_t>& geometries,
                      const std::vector<measure_chunk_t>& measures,
                      const std::vector<timing_point_t>& anchors)
    -> std::optional<local_position_t> {
    const auto found = std::upper_bound(geometries.begin(), geometries.end(), time,
                                        [](long double value, const auto& measure) {
                                            return value < measure.begin;
                                        });
    if (found == geometries.begin()) return std::nullopt;
    const auto index = static_cast<std::size_t>(std::distance(geometries.begin(), found) - 1);
    if (index >= geometries.size() || time < geometries[index].begin - 1.0e-12L ||
        time >= geometries[index].end + 1.0e-12L)
        return std::nullopt;
    const auto& boundaries = geometries[index].row_boundaries;
    const auto after = std::upper_bound(boundaries.begin(), boundaries.end(), time,
        [](long double value, const auto& boundary) { return value < boundary.first; });
    const auto right = after == boundaries.end() ? std::prev(after) : after;
    const auto left = std::prev(right);
    const auto total_beats = quarter_beats_between(anchors, left->first, right->first);
    auto fraction = quarter_beats_between(anchors, left->first, time) / total_beats;
    if (fraction < 0.0L && fraction > -1.0e-12L) fraction = 0.0L;
    auto row = static_cast<std::uint64_t>(
        std::llround(fraction * static_cast<long double>(right->second - left->second))) +
        left->second;
    if (row >= geometries[index].capacity) row = geometries[index].capacity - 1;
    return local_position_t{measures[index].id, row};
}

template <typename fail_t>
auto source_microseconds(long double milliseconds, fail_t&& fail, std::size_t order = 0)
    -> std::int64_t {
    // Use the same binary64 fractional rounding on every platform: extended
    // precision can place decimal half ties just below their microsecond boundary.
    // Larger timestamps need checked integer scaling to avoid rounded overflow.
    if (std::isfinite(milliseconds) && std::fabs(milliseconds) < 0x1p43L)
        return static_cast<std::int64_t>(std::round(static_cast<double>(milliseconds) * 1000.0));

    const auto reject = [&] {
        fail(osu_mania_parse_diagnostic_code_t::invalid_timing_point,
             "timing point exceeds the supported microsecond source grid", order);
    };
    constexpr auto exclusive_limit = 0x1p63L;
    if (!std::isfinite(milliseconds) || milliseconds < -exclusive_limit ||
        milliseconds >= exclusive_limit)
        reject();
    constexpr auto scale = std::int64_t{1000};
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto whole_ms = static_cast<std::int64_t>(milliseconds);
    if (whole_ms < minimum / scale || whole_ms > maximum / scale) reject();
    const auto whole_us = whole_ms * scale;
    const auto fractional_us = static_cast<std::int64_t>(
        std::round((milliseconds - static_cast<long double>(whole_ms)) * scale));
    if ((fractional_us < 0 && whole_us < minimum - fractional_us) ||
        (fractional_us > 0 && whole_us > maximum - fractional_us))
        reject();
    return whole_us + fractional_us;
}

template <typename fail_t>
auto source_origin_seconds(long double milliseconds, fail_t&& fail) -> signed_rational_t {
    const auto microseconds = source_microseconds(milliseconds, fail);
    const auto magnitude = microseconds < 0
                               ? static_cast<std::uint64_t>(-(microseconds + 1)) + 1
                               : static_cast<std::uint64_t>(microseconds);
    const auto common = std::gcd(magnitude, std::uint64_t{1'000'000});
    return {microseconds / static_cast<std::int64_t>(common), 1'000'000 / common};
}

template <typename fail_t>
auto osu_measure_index(const chart_document_t& chart, fail_t&& fail) -> measure_index_t {
    auto built = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built))
        fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
             "derived osu! measure geometry is invalid");
    return std::get<measure_index_t>(std::move(built));
}

template <typename fail_t>
auto span_bpm(std::uint64_t rows, std::uint64_t divisions, std::uint64_t microseconds,
              fail_t&& fail, std::size_t order) -> signed_rational_t {
    // Cross-reduce before multiplication; timing over the complete span then
    // cancels the row count and retains a common microsecond denominator.
    auto factor = std::uint64_t{240'000'000};
    auto common = std::gcd(rows, divisions);
    rows /= common;
    divisions /= common;
    common = std::gcd(factor, microseconds);
    factor /= common;
    microseconds /= common;
    common = std::gcd(rows, microseconds);
    rows /= common;
    microseconds /= common;
    common = std::gcd(factor, divisions);
    factor /= common;
    divisions /= common;
    if (microseconds == 0 || divisions > UINT64_MAX / microseconds || rows > UINT64_MAX / factor)
        fail(osu_mania_parse_diagnostic_code_t::invalid_timing_point,
             "effective editor BPM exceeds the bounded timing representation", order);
    const auto numerator = rows * factor;
    const auto denominator = divisions * microseconds;
    if (numerator > static_cast<std::uint64_t>(INT64_MAX) || denominator > INT64_MAX)
        fail(osu_mania_parse_diagnostic_code_t::invalid_timing_point,
             "effective editor BPM exceeds the bounded timing representation", order);
    return {static_cast<std::int64_t>(numerator), denominator};
}

auto simple_exact_tail_bpm(const timing_point_t& point, std::uint64_t rows,
                           std::uint64_t divisions, long double duration_ms)
    -> std::optional<signed_rational_t> {
    const auto original = rationalize(60'000.0L / point.beat_length);
    if (!original || original->numerator > 1'000'000 || original->denominator > 1'000'000)
        return std::nullopt;
    const auto exact_ms = static_cast<long double>(rows) * 240'000.0L / divisions *
                          original->denominator / original->numerator;
    if (std::fabs(exact_ms - duration_ms) > 1.0e-8L) return std::nullopt;
    // Only the final span may retain a non-microsecond period: it cannot cause
    // an LCM cascade. The small coefficients also bound subsequent queries.
    return signed_rational_t{static_cast<std::int64_t>(original->numerator), original->denominator};
}

template <typename position_t, typename fail_t>
auto adjusted_osu_bpms(const chart_document_t& chart,
                       const std::vector<timing_point_t>& anchors, long double chart_end,
                       position_t&& position_at,
                       fail_t&& fail) -> std::unordered_map<std::size_t, signed_rational_t> {
    const auto measures = osu_measure_index(chart, fail);
    std::unordered_map<std::size_t, signed_rational_t> result;
    for (std::size_t index = 0; index < anchors.size(); ++index) {
        const auto& begin = anchors[index];
        const auto end_time = index + 1 < anchors.size() ? anchors[index + 1].time : chart_end;
        const auto begin_position = position_at(begin.time);
        const auto end_position = index + 1 < anchors.size() ? position_at(end_time) : std::nullopt;
        if (!begin_position || (index + 1 < anchors.size() && !end_position))
            fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                 "timing point falls outside the derived measure timeline", begin.order);
        const auto first = absolute_row(measures, *begin_position);
        const auto last = end_position ? absolute_row(measures, *end_position)
                                       : std::optional<std::uint64_t>{measures.total_rows};
        if (!first || !last || *last <= *first) continue;
        const auto first_us = source_microseconds(begin.time, fail, begin.order);
        const auto last_us = source_microseconds(end_time, fail, begin.order);
        // Positive source spans below one microsecond remain editable. Their
        // bounded minimum duration contributes at most one microsecond each.
        const auto elapsed = static_cast<std::uint64_t>(last_us) -
                             static_cast<std::uint64_t>(first_us);
        const auto exact_tail = index + 1 == anchors.size()
            ? simple_exact_tail_bpm(begin, *last - *first,
                chart.divisions_per_standard_measure, end_time - begin.time)
            : std::nullopt;
        result.emplace(begin.order, exact_tail ? *exact_tail : span_bpm(*last - *first,
            chart.divisions_per_standard_measure, std::max<std::uint64_t>(1, elapsed),
            fail, begin.order));
    }
    return result;
}

template <typename position_t, typename fail_t>
void ensure_hold_tail_room(chart_document_t& chart, std::vector<measure_geometry_t>& geometries,
                           const std::vector<hit_object_t>& objects, long double beat_length,
                           std::size_t maximum_measures, position_t&& position_at, fail_t&& fail) {
    const auto index = osu_measure_index(chart, fail);
    const bool needs_tail = std::ranges::any_of(objects, [&](const auto& object) {
        if (!object.end_time || *object.end_time <= object.time) return false;
        const auto head = position_at(object.time);
        const auto tail = position_at(*object.end_time);
        if (!head || !tail) return false;
        const auto first = absolute_row(index, *head);
        const auto last = absolute_row(index, *tail);
        return first && last && *first == *last && *first == index.total_rows - 1;
    });
    if (!needs_tail) return;
    if (chart.measures.size() >= maximum_measures)
        fail(osu_mania_parse_diagnostic_code_t::measure_limit,
             "preserving a positive hold requires one additional tail measure");
    const auto begin = geometries.back().end;
    const auto end = begin + beat_length * 4.0L;
    if (!std::isfinite(end) || end <= begin)
        fail(osu_mania_parse_diagnostic_code_t::invalid_timing_point,
             "positive hold tail produces an invalid measure");
    chart.measures.push_back({.id = measure_id_t{chart.measures.back().id.value + 1},
                              .definition = {{1, 1}, meter_t{4, 4}}});
    geometries.push_back({begin, end, chart.divisions_per_standard_measure,
                         {{begin, 0}, {end, chart.divisions_per_standard_measure}}});
}

template <typename fail_t>
auto positive_hold_end(const measure_index_t& measures, local_position_t head,
                       local_position_t tail, bool& quantized, fail_t&& fail, std::size_t order)
    -> local_position_t {
    const auto begin = absolute_row(measures, head);
    const auto end = absolute_row(measures, tail);
    if (begin && end && *end > *begin) return tail;
    const auto next = begin ? position_at_absolute_row(measures, *begin + 1) : std::nullopt;
    if (!next)
        fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
             "positive hold has no bounded editable endpoint", order);
    quantized = true;
    return *next;
}

template <typename position_t, typename object_id_t, typename fail_t>
void append_initial_osu_bpm(chart_document_t& chart, const std::vector<timing_point_t>& anchors,
                            long double origin, position_t&& position_at,
                            object_id_t&& take_object_id, fail_t&& fail) {
    const auto measures = osu_measure_index(chart, fail);
    const auto position = position_at(anchors.front().time);
    const auto row = position ? absolute_row(measures, *position) : std::nullopt;
    if (!row || *row == 0) return;
    const auto begin_us = source_microseconds(origin, fail);
    const auto end_us = source_microseconds(anchors.front().time, fail, anchors.front().order);
    const auto elapsed = static_cast<std::uint64_t>(end_us) - static_cast<std::uint64_t>(begin_us);
    chart.bpms.push_back({.id = take_object_id(anchors.front().order),
                          .position = {chart.measures.front().id, 0},
                          .beats_per_minute = span_bpm(*row, chart.divisions_per_standard_measure,
                              std::max<std::uint64_t>(1, elapsed), fail, anchors.front().order)});
}

template <typename fail_t>
class osu_resource_catalog_t {
  public:
    osu_resource_catalog_t(song_document_t& source_song,
                           const osu_mania_parse_options_t& parse_options,
                           fail_t failure)
        : song(source_song), options(parse_options), fail(std::move(failure)) {}

    auto add_audio(std::string_view name, resource_role_t role, std::string source_field,
                   std::size_t source_order) -> std::optional<resource_id_t> {
        const auto path = safe_relative_path(name);
        if (!path)
            fail(osu_mania_parse_diagnostic_code_t::invalid_resource_path,
                 "osu! resource path must be a safe relative path", source_order);
        const auto key = path->generic_string();
        if (const auto found = audio_resources.find(key); found != audio_resources.end()) {
            add_audio_role(found->second, role);
            return found->second;
        }
        const auto asset = allocate_id<asset_id_t>(asset_ids);
        const auto resource = allocate_id<resource_id_t>(resource_ids);
        if (!asset || !resource)
            fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                 "resource ID space is exhausted", source_order);
        song.audio_assets.push_back({.id = *asset, .source_path = *path});
        song.resources.push_back({.id = *resource,
                                  .name = std::string(name),
                                  .asset = *asset,
                                  .roles = {role},
                                  .target_identities = {{format_family_t::osu_mania,
                                                         std::string(name), "audio-path"}},
                                  .missing = is_resource_missing(*path),
                                  .provenance = {format_family_t::osu_mania, "osu-mania",
                                                 std::move(source_field), std::string(name),
                                                 source_order}});
        audio_resources.emplace(key, *resource);
        return resource;
    }

    auto add_visual(std::string_view name, asset_kind_t kind, resource_role_t role,
                    std::string source_field, std::size_t source_order)
        -> std::optional<resource_id_t> {
        const auto path = safe_relative_path(name);
        if (!path)
            fail(osu_mania_parse_diagnostic_code_t::invalid_resource_path,
                 "osu! visual resource path must be a safe relative path", source_order);
        const auto key = path->generic_string();
        if (const auto found = visual_resources.find(key); found != visual_resources.end())
            return found->second;
        const auto asset = allocate_id<asset_id_t>(asset_ids);
        const auto resource = allocate_id<resource_id_t>(resource_ids);
        if (!asset || !resource)
            fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                 "resource ID space is exhausted", source_order);
        song.visual_assets.push_back({.id = *asset, .kind = kind, .source_path = *path});
        song.resources.push_back({.id = *resource,
                                  .name = std::string(name),
                                  .asset = *asset,
                                  .roles = {role},
                                  .target_identities = {{format_family_t::osu_mania,
                                                         std::string(name), "visual-path"}},
                                  .missing = is_resource_missing(*path),
                                  .provenance = {format_family_t::osu_mania, "osu-mania",
                                                 std::move(source_field), std::string(name),
                                                 source_order}});
        visual_resources.emplace(key, *resource);
        return resource;
    }

  private:
    void add_audio_role(resource_id_t resource, resource_role_t role) {
        const auto existing = std::ranges::find(song.resources, resource, &resource_entry_t::id);
        if (existing != song.resources.end() &&
            std::ranges::find(existing->roles, role) == existing->roles.end())
            existing->roles.push_back(role);
    }

    auto is_resource_missing(const std::filesystem::path& path) const -> bool {
        if (!options.asset_base_directory) return true;
        std::error_code error;
        return !std::filesystem::exists(*options.asset_base_directory / path, error) || error;
    }

    song_document_t& song;
    const osu_mania_parse_options_t& options;
    fail_t fail;
    std::unordered_map<std::string, resource_id_t> audio_resources;
    std::unordered_map<std::string, resource_id_t> visual_resources;
    id_allocator_t asset_ids;
    id_allocator_t resource_ids;
};

template <typename position_t, typename object_id_t, typename fail_t>
void append_osu_hit_notes(chart_document_t& chart, const std::vector<hit_object_t>& objects,
                          const std::vector<std::optional<resource_id_t>>& samples,
                          std::int64_t keys, position_t&& position_at,
                          object_id_t&& take_object_id, format_metadata_t& source_group,
                          bool& quantized,
                          fail_t&& fail) {
    const auto measures = osu_measure_index(chart, fail);
    for (std::size_t index = 0; index < objects.size(); ++index) {
        const auto& object = objects[index];
        const auto position = position_at(object.time);
        auto end = object.end_time ? position_at(*object.end_time) : std::nullopt;
        if (!position || (object.end_time && !end))
            fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                 "hit object falls outside the derived measure timeline", object.order);
        if (object.end_time && *object.end_time > object.time)
            end = positive_hold_end(measures, *position, *end, quantized, fail, object.order);
        const auto id = take_object_id(object.order);
        const auto column = std::min(static_cast<std::uint64_t>(object.x * keys / 512),
                                     static_cast<std::uint64_t>(keys - 1));
        note_t note;
        note.id = id;
        note.position = *position;
        note.lane = lane_id_t{column + 1};
        note.kind = object.end_time ? note_kind_t::hold : note_kind_t::tap;
        note.end = end;
        note.sound = samples[index];
        note.provenance = {format_family_t::osu_mania, "osu-mania", "HitObjects",
                           object.raw_line, object.order};
        chart.measures[static_cast<std::size_t>(position->measure.value - 1)].notes.push_back(note);
        retain_osu_source_value(source_group, "osu.hitobject." + std::to_string(id.value) + ".time",
                                object.time_spelling, object.order, false);
        if (object.end_time)
            retain_osu_source_value(source_group,
                                    "osu.hitobject." + std::to_string(id.value) + ".end_time",
                                    *object.end_time_spelling, object.order, false);
    }
}

template <typename position_t, typename object_id_t, typename fail_t>
void append_osu_timing_points(chart_document_t& chart,
                              const std::vector<timing_point_t>& points,
                              const std::unordered_map<std::size_t, signed_rational_t>& adjusted_bpms,
                              position_t&& position_at, object_id_t&& take_object_id,
                              fail_t&& fail) {
    for (const auto& point : points) {
        const auto position = position_at(point.time);
        if (!position)
            fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                 "timing point falls outside the derived measure timeline", point.order);
        const source_provenance_t provenance{format_family_t::osu_mania, "osu-mania",
                                             "TimingPoints", point.raw_line, point.order};
        if (point.uninherited) {
            const auto adjusted = adjusted_bpms.find(point.order);
            const auto bpm = adjusted == adjusted_bpms.end()
                                 ? rationalize(60'000.0L / point.beat_length)
                                 : std::optional<rational_t>{};
            if (adjusted != adjusted_bpms.end()) {
                chart.bpms.push_back({.id = take_object_id(point.order), .position = *position,
                                      .beats_per_minute = adjusted->second,
                                      .provenance = provenance});
                continue;
            }
            if (!bpm || bpm->numerator > static_cast<std::uint64_t>(INT64_MAX))
                fail(osu_mania_parse_diagnostic_code_t::invalid_timing_point,
                     "BPM cannot be represented by the exact editor timing model", point.order);
            chart.bpms.push_back({.id = take_object_id(point.order),
                                  .position = *position,
                                  .beats_per_minute = {static_cast<std::int64_t>(bpm->numerator),
                                                       bpm->denominator},
                                  .provenance = provenance});
            continue;
        }
        const auto multiplier = rationalize(100.0L / -point.beat_length);
        if (!multiplier || multiplier->numerator > static_cast<std::uint64_t>(INT64_MAX))
            fail(osu_mania_parse_diagnostic_code_t::invalid_timing_point,
                 "inherited scroll multiplier cannot be represented exactly", point.order);
        chart.scrolls.push_back({.id = take_object_id(point.order),
                                 .position = *position,
                                 .multiplier = {static_cast<std::int64_t>(multiplier->numerator),
                                                multiplier->denominator},
                                 .provenance = provenance});
    }
}

template <typename position_t, typename object_id_t>
void append_osu_kiai(chart_document_t& chart, const std::vector<timing_point_t>& points,
                     position_t&& position_at, object_id_t&& take_object_id) {
    auto ordered = points;
    std::ranges::stable_sort(ordered, [](const auto& left, const auto& right) {
        if (left.time != right.time) return left.time < right.time;
        return left.order < right.order;
    });
    std::optional<std::uint32_t> prior_effects;
    for (const auto& point : ordered) {
        if (prior_effects && *prior_effects == point.effects) continue;
        const auto position = position_at(point.time);
        if (!position) continue;
        chart.kiai.push_back({.id = take_object_id(point.order),
                              .position = *position,
                              .enabled = (point.effects & 1U) != 0,
                              .retained_effect_bits = point.effects,
                              .provenance = {format_family_t::osu_mania, "osu-mania",
                                             "TimingPoints.effects", point.raw_line, point.order}});
        prior_effects = point.effects;
    }
}

template <typename position_t, typename object_id_t>
void append_osu_bga_events(chart_document_t& chart,
                           const std::vector<pending_visual_t>& visuals,
                           const std::vector<std::optional<resource_id_t>>& resources,
                           position_t&& position_at, object_id_t&& take_object_id,
                           format_metadata_t& source_group) {
    for (std::size_t index = 0; index < visuals.size(); ++index) {
        const auto position = position_at(visuals[index].time);
        if (!position || !resources[index]) continue;
        const auto event_id = take_object_id(visuals[index].order);
        if (!visuals[index].offsets.empty())
            retain_osu_source_value(source_group,
                                    "osu.event." + std::to_string(event_id.value) + ".offset",
                                    visuals[index].offsets, visuals[index].order, false);
        chart.bga_events.push_back({.id = event_id,
                                    .position = *position,
                                    .resource = *resources[index],
                                    .kind = visuals[index].event_kind,
                                    .provenance = {format_family_t::osu_mania, "osu-mania", "Events",
                                                   visuals[index].path, visuals[index].order}});
    }
}

template <typename resource_catalog_t, typename position_t, typename object_id_t, typename fail_t>
auto append_osu_event_resources(chart_document_t& chart,
                                const std::vector<pending_resource_t>& events,
                                resource_catalog_t& resources, position_t&& position_at,
                                object_id_t&& take_object_id, format_metadata_t& source_group,
                                fail_t&& fail)
    -> std::optional<decltype(take_object_id(0))> {
    if (!std::ranges::any_of(events, [](const auto& event) { return event.sample_event; }))
        return std::nullopt;
    const auto source_lane = take_object_id(0);
    chart.bgm_source_lanes.push_back(
        {.id = source_lane, .name = "osu! samples", .source_order = 0});
    for (const auto& event : events) {
        if (event.kind != asset_kind_t::audio) {
            resources.add_visual(event.path, event.kind, event.role, "Events.resource", event.order);
            continue;
        }
        const auto sound = resources.add_audio(event.path, event.role, "Events.resource", event.order);
        if (!event.sample_event || !event.time || !sound) continue;
        const auto position = position_at(*event.time);
        if (!position)
            fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                 "sample event falls outside the derived measure timeline", event.order);
        const auto event_id = take_object_id(event.order);
        retain_osu_source_value(source_group,
                                "osu.event." + std::to_string(event_id.value) + ".sample",
                                event.sample_fields, event.order, false);
        chart.measures[static_cast<std::size_t>(position->measure.value - 1)].bgm_events.push_back(
            {.id = event_id,
             .position = *position,
             .source_lane = source_lane,
             .sound = *sound,
             .provenance = {format_family_t::osu_mania, "osu-mania", "Events", event.path,
                            event.order}});
    }
    return source_lane;
}

} // namespace

auto osu_mania_parse_result_t::has_errors() const -> bool {
    return diagnostic_queries::has_errors(diagnostics);
}

auto osu_mania_parse_result_t::from(osu_mania_parse_outcome_t&& outcome) -> osu_mania_parse_result_t {
    return {.song = std::move(outcome.value), .diagnostics = std::move(outcome.diagnostics)};
}

auto osu_mania_parse_result_t::from(std::span<const std::byte> bytes, osu_mania_parse_options_t options)
    -> osu_mania_parse_outcome_t {
    osu_mania_parse_outcome_t result{};
    const auto fail = [&](osu_mania_parse_diagnostic_code_t code, std::string message,
                          std::size_t order = 0,
                          std::optional<std::size_t> offset = std::nullopt) -> void {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, code, std::move(message), order, offset});
        throw parse_abort_t{};
    };
    try {
        std::string source;
        const auto bom_size = decode_osu_source(bytes, options, source, fail);
        std::string_view text(source);

        auto scanned = scan_source_lines(text, bom_size, options, fail);
        auto fields = std::move(scanned.fields);
        const auto format_version = scanned.format_version;
        auto partitioned = partition_osu_sections(scanned.lines, options, fail);
        fields.insert(fields.end(), std::make_move_iterator(partitioned.fields.begin()),
                      std::make_move_iterator(partitioned.fields.end()));
        auto event_lines = std::move(partitioned.events);
        auto timing_lines = std::move(partitioned.timing);
        auto hit_lines = std::move(partitioned.hit_objects);
        const auto keys = validate_osu_mania_keys(fields, fail);

        std::size_t expanded_event_count{};
        const auto account_events = [&](std::size_t count) {
            if (count > options.max_events - expanded_event_count) {
                fail(osu_mania_parse_diagnostic_code_t::event_limit,
                     "osu! source exceeds the configured event limit");
            }
            expanded_event_count += count;
        };
        account_events(timing_lines.size());
        account_events(hit_lines.size());
        account_events(event_lines.size());
        if (const auto bookmarks = find_osu_value(fields, "Editor", "Bookmarks");
            bookmarks && !trim(*bookmarks).empty()) {
            std::size_t bookmark_count{1};
            for (const auto character : *bookmarks) {
                if (character == ',') {
                    if (bookmark_count == std::numeric_limits<std::size_t>::max()) {
                        fail(osu_mania_parse_diagnostic_code_t::event_limit,
                             "osu! bookmark count overflows the supported range");
                    }
                    ++bookmark_count;
                }
            }
            account_events(bookmark_count);
        }
        auto timing_points = parse_timing_points(timing_lines, fail);
        std::vector<hit_object_t> hit_objects;
        hit_objects.reserve(hit_lines.size());
        for (const auto& line : hit_lines)
            hit_objects.push_back(parse_hit_object_line(line, *format_version, fail));

        auto parsed_events = parse_event_lines(event_lines, fail);
        auto visuals = std::move(parsed_events.visuals);
        auto event_resources = std::move(parsed_events.resources);
        auto storyboard_lines = std::move(parsed_events.storyboard_lines);

        auto embedded_storyboard = parse_embedded_storyboard(
            storyboard_lines, options, fail, account_events);

        auto geometry = derive_timing_geometry(timing_points, hit_objects, visuals, options, fail);
        const auto& anchors = geometry.anchors;
        const auto& timeline = geometry.timeline;
        const auto& suppressed_lines = geometry.suppressed_lines;
        bool timing_quantized{};
        auto [measure_lengths, divisions] = derive_measure_lengths(timeline, anchors, timing_quantized, fail);

        song_document_t song;
        song.id = song_id_t{1};
        chart_document_t chart;
        chart.id = chart_id_t{1};
        chart.source_family = format_family_t::osu_mania;
        chart.divisions_per_standard_measure = divisions;
        chart.beat_zero_audio_offset_seconds = source_origin_seconds(timeline.front().time, fail);
        const auto source_bytes =
            std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
        auto source_group = make_source_group(source_bytes, fields, event_lines, timing_lines,
                                              hit_lines);
        retain_osu_source_value(source_group, "osu.import.explicit_source_origin", "1", 0, false);

        populate_osu_metadata(song, chart, fields, std::move(embedded_storyboard), source_group);
        id_allocator_t object_ids;
        const auto take_object_id = [&](std::size_t source_order) -> object_id_t {
            const auto id = allocate_id<object_id_t>(object_ids);
            if (!id)
                fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                     "chart object ID space is exhausted", source_order);
            return *id;
        };
        auto measure_plan = make_editable_measures(measure_lengths, timeline, anchors, divisions,
                                                   timing_quantized, fail);
        chart.measures = std::move(measure_plan.measures);
        auto geometries = std::move(measure_plan.geometries);
        const auto position_at = [&](long double time) {
            return position_at_time(time, geometries, chart.measures, anchors);
        };
        ensure_hold_tail_room(chart, geometries, hit_objects, anchors.back().beat_length,
                               options.max_measures, position_at, fail);

        append_osu_layout(chart, keys, find_osu_value(fields, "General", "SpecialStyle") == "1",
                          take_object_id);

        osu_resource_catalog_t resource_catalog{song, options, fail};
        if (const auto audio = find_osu_value(fields, "General", "AudioFilename"); audio && !audio->empty()) {
            resource_catalog.add_audio(*audio, resource_role_t::music, "General.AudioFilename", 0);
        }
        auto hit_samples = resolve_hit_samples(hit_objects, resource_catalog);
        auto visual_event_resources = resolve_visual_resources(visuals, resource_catalog);
        const auto sample_source_lane = append_osu_event_resources(
            chart, event_resources, resource_catalog, position_at, take_object_id, source_group,
            fail);
        (void)sample_source_lane;

        append_osu_hit_notes(chart, hit_objects, hit_samples, keys, position_at,
                             take_object_id, source_group, timing_quantized, fail);

        const auto adjusted_bpms = adjusted_osu_bpms(chart, anchors, geometries.back().end,
                                                    position_at, fail);
        append_osu_timing_points(chart, timing_points, adjusted_bpms, position_at,
                                 take_object_id, fail);
        append_initial_osu_bpm(chart, anchors, timeline.front().time, position_at,
                               take_object_id, fail);
        append_osu_kiai(chart, timing_points, position_at, take_object_id);
        for (const auto& [source_order, displacement] : suppressed_lines) {
            retain_osu_source_value(source_group, "osu.suppressed_bar_line_displacement_ms", format_double(displacement),
                         source_order, false);
        }
        append_osu_bga_events(chart, visuals, visual_event_resources, position_at, take_object_id,
                              source_group);
        append_osu_bookmarks(chart, find_osu_value(fields, "Editor", "Bookmarks"),
                             take_object_id, fail);

        // Resource paths, storyboard/event rows and every unnormalized metadata
        // field remain addressable in the ordered source group above.
        chart.metadata.format_groups.front().ordered_fields = source_group.ordered_fields;
        chart.metadata.format_groups.front().ordered_field_source_orders =
            source_group.ordered_field_source_orders;
        song.metadata.format_groups.front().ordered_fields = source_group.ordered_fields;
        song.metadata.format_groups.front().ordered_field_source_orders =
            source_group.ordered_field_source_orders;

        song.charts.push_back(std::move(chart));
        result.value = std::move(song);
        if (timing_quantized)
            result.diagnostics.push_back({diagnostic_severity_t::warning,
                osu_mania_parse_diagnostic_code_t::timing_quantization,
                "osu! timing was quantized to the 960-division editor grid; editable BPMs "
                "retain bounded source-time alignment and original timestamps are preserved",
                0, std::nullopt});
    } catch (const parse_abort_t&) {
        result.value.reset();
    } catch (const std::exception& error) {
        result.value.reset();
        result.diagnostics.push_back({diagnostic_severity_t::error,
                                      osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                                      std::string("osu! import failed atomically: ") + error.what(),
                                      0, std::nullopt});
    } catch (...) {
        result.value.reset();
        result.diagnostics.push_back({diagnostic_severity_t::error,
                                      osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                                      "osu! import failed atomically", 0, std::nullopt});
    }
    return result;
}

namespace {

struct osu_write_abort_t {};

struct positioned_bpm_write_t {
    std::uint64_t row{};
    const bpm_event_t* event{};
    std::size_t insertion_order{};
};

struct positioned_scroll_write_t {
    std::uint64_t row{};
    const scroll_event_t* event{};
    std::size_t insertion_order{};
};

struct positioned_kiai_write_t {
    std::uint64_t row{};
    const kiai_event_t* event{};
    std::size_t insertion_order{};
};

struct osu_output_event_t {
    long double milliseconds{};
    std::uint64_t row{};
    std::size_t order{};
    std::size_t serial{};
    std::string line;
};

template <typename fail_t, typename account_t, typename timestamp_t, typename effects_t>
void append_osu_timing_line(std::vector<osu_output_event_t>& output,
                            std::set<std::uint64_t>& red_rows,
                            std::set<std::uint64_t>& timing_rows, std::size_t& serial,
                            std::uint64_t row, long double beat_length, std::uint32_t meter,
                            bool uninherited, std::size_t order,
                            std::array<std::int64_t, 3> sample_fields,
                            const measure_index_t& measures, timestamp_t&& timestamp_at_row,
                            effects_t&& effects_at, account_t&& account_event, fail_t&& fail) {
    const auto milliseconds = timestamp_at_row(row);
    if (!std::isfinite(milliseconds))
        fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
             "timing event time exceeds finite osu! numeric range", "timing");
    auto retained_effects = effects_at(row);
    if (uninherited) {
        constexpr std::uint32_t omit_first_barline_bit = 1U << 3U;
        const auto entry = std::ranges::lower_bound(
            measures.entries, row, {}, &measure_index_entry_t::absolute_begin_row);
        const bool boundary = entry != measures.entries.end() && entry->absolute_begin_row == row;
        if (boundary)
            retained_effects &= ~omit_first_barline_bit;
        else
            retained_effects |= omit_first_barline_bit;
    }
    const auto beat_text = format_double(beat_length);
    const auto time_text = format_double(milliseconds);
    if (beat_text.empty() || time_text.empty())
        fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
             "timing event cannot be serialized as a finite decimal", "timing");
    const auto line = time_text + "," + beat_text + "," + std::to_string(meter) + "," +
                      std::to_string(sample_fields[0]) + "," +
                      std::to_string(sample_fields[1]) + "," +
                      std::to_string(sample_fields[2]) + "," + (uninherited ? "1," : "0,") +
                      std::to_string(retained_effects);
    output.push_back({milliseconds, row, order, serial++, line});
    account_event();
    timing_rows.insert(row);
    if (uninherited) red_rows.insert(row);
}

struct passthrough_field_t {
    std::string section;
    std::string line;
};

auto rational_value(signed_rational_t value) -> std::optional<long double> {
    if (value.denominator == 0)
        return std::nullopt;
    const auto result =
        to_long_double(value);
    if (!std::isfinite(result))
        return std::nullopt;
    return result;
}

auto path_utf8(const std::filesystem::path& path) -> std::string {
    return text_util::path_utf8(path);
}

auto is_writer_safe_text(std::string_view value) -> bool {
    return value.find('\0') == std::string_view::npos &&
           value.find('\n') == std::string_view::npos &&
           value.find('\r') == std::string_view::npos && !valid_utf8(value).has_value();
}

auto is_writer_safe_key(std::string_view value) -> bool {
    if (value.empty())
        return false;
    return std::ranges::all_of(value, [](unsigned char character) {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_' || character == '-';
    });
}

auto quote_csv(std::string_view value) -> std::string {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('"');
    for (const auto character : value) {
        if (character == '"')
            result.push_back('"');
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

auto writer_round_ms(long double value) -> std::optional<std::int64_t> {
    if (!std::isfinite(value) ||
        value < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) + 1.0L ||
        value > static_cast<long double>(std::numeric_limits<std::int64_t>::max()) - 1.0L)
        return std::nullopt;
    // Values are assembled from rational chart rows but represented here for
    // osu!'s decimal writer.  A mathematical half can land one long-double
    // ULP toward zero (for example 4 / 4000 * 60000 / 120).  Move one ULP
    // outward before rounding so its documented away-from-zero tie rule stays
    // stable across host libm implementations.
    const auto outward =
        std::nextafter(value, value < 0.0L ? -std::numeric_limits<long double>::infinity()
                                           : std::numeric_limits<long double>::infinity());
    const auto rounded = std::round(outward); // half ties move away from zero
    if (rounded < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
        rounded > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    return static_cast<std::int64_t>(rounded);
}

auto osu_resource_path(const song_document_t& song, resource_id_t id, bool expect_audio)
    -> std::optional<std::string> {
    const auto resource = std::ranges::find(song.resources, id, &resource_entry_t::id);
    if (resource == song.resources.end())
        return std::nullopt;
    std::filesystem::path asset_path;
    if (expect_audio) {
        const auto asset =
            song.find_audio_asset(resource->asset);
        if (asset == nullptr)
            return std::nullopt;
        asset_path = asset->source_path;
    } else {
        const auto asset =
            std::ranges::find(song.visual_assets, resource->asset, &visual_asset_t::id);
        if (asset == song.visual_assets.end())
            return std::nullopt;
        asset_path = asset->source_path;
    }
    const auto name = path_utf8(asset_path);
    const auto safe = safe_relative_path(name);
    if (!safe || !is_writer_safe_text(name))
        return std::nullopt;
    return safe->generic_string();
}

struct retained_osu_time_t {
    long double value{};
    std::string spelling;
};

struct retained_osu_note_times_t {
    std::optional<retained_osu_time_t> head;
    std::optional<retained_osu_time_t> tail;
};

// Source-spelled event fields that the chart model has no slot for: background
// and video offsets, and the layer and volume of Sample events.
struct retained_osu_event_fields_t {
    std::unordered_map<std::uint64_t, std::string> offsets;
    std::unordered_map<std::uint64_t, std::string> samples;
};

auto retained_osu_event_fields(const chart_document_t& chart) -> retained_osu_event_fields_t {
    retained_osu_event_fields_t result;
    constexpr std::string_view prefix{"osu.event."};
    for (const auto& group : chart.metadata.format_groups) {
        if (group.family != format_family_t::osu_mania)
            continue;
        for (const auto& [key, value] : group.ordered_fields) {
            const auto key_view = std::string_view{key};
            if (!key_view.starts_with(prefix))
                continue;
            const auto dot = key_view.find('.', prefix.size());
            std::uint64_t id{};
            if (dot == std::string_view::npos ||
                !parse_integer(key_view.substr(prefix.size(), dot - prefix.size()), id))
                continue;
            const auto field = key_view.substr(dot + 1);
            if (field == "offset")
                result.offsets[id] = value;
            else if (field == "sample")
                result.samples[id] = value;
        }
    }
    return result;
}

// A retained "x,y" offset pair is reused only when both values are plain numbers.
auto retained_event_offsets(const retained_osu_event_fields_t& retained, std::uint64_t id)
    -> std::string {
    const auto found = retained.offsets.find(id);
    if (found == retained.offsets.end())
        return "0,0";
    const auto parts = split(found->second, ',');
    long double ignored{};
    if (parts.size() != 2 || !parse_number(parts[0], ignored) || !parse_number(parts[1], ignored))
        return "0,0";
    return found->second;
}

// Layer and volume of a Sample event; new events get layer 1 at full volume.
auto retained_sample_event_fields(const retained_osu_event_fields_t& retained, std::uint64_t id)
    -> std::pair<std::uint32_t, std::int64_t> {
    const auto found = retained.samples.find(id);
    if (found == retained.samples.end())
        return {1, 100};
    const auto parts = split(found->second, ',');
    std::uint32_t layer{};
    std::int64_t volume{};
    if (parts.size() != 2 || !parse_integer(parts[0], layer) || !parse_integer(parts[1], volume))
        return {1, 100};
    return {layer, volume};
}

auto embedded_storyboard_source_orders(const song_document_t& song)
    -> std::unordered_set<std::uint64_t> {
    std::unordered_set<std::uint64_t> orders;
    const auto collect_nodes = [&](auto&& self,
                                   const std::vector<storyboard_node_t>& nodes) -> void {
        for (const auto& node : nodes) {
            orders.insert(node.source_order);
            self(self, node.children);
        }
    };
    for (const auto& storyboard : song.storyboards) {
        if (!storyboard.model)
            continue;
        for (const auto& sprite : storyboard.model->sprites) {
            orders.insert(sprite.source_order);
            collect_nodes(collect_nodes, sprite.nodes);
        }
        for (const auto& line : storyboard.model->retained_top_level_lines)
            orders.insert(line.source_order);
    }
    return orders;
}

auto retained_osu_note_times(const chart_document_t& chart)
    -> std::unordered_map<std::uint64_t, retained_osu_note_times_t> {
    std::unordered_map<std::uint64_t, retained_osu_note_times_t> values;
    for (const auto& group : chart.metadata.format_groups) {
        if (group.family != format_family_t::osu_mania)
            continue;
        for (const auto& [key, value] : group.ordered_fields) {
            constexpr std::string_view prefix{"osu.hitobject."};
            const auto key_view = std::string_view{key};
            const bool is_head = key_view.ends_with(".time");
            const bool is_tail = key_view.ends_with(".end_time");
            if (!key_view.starts_with(prefix) || (!is_head && !is_tail))
                continue;
            const auto suffix_size =
                is_tail ? std::string_view{".end_time"}.size() : std::string_view{".time"}.size();
            const auto id_text =
                key_view.substr(prefix.size(), key_view.size() - prefix.size() - suffix_size);
            std::uint64_t id{};
            long double time{};
            if (parse_integer(id_text, id) && parse_number(value, time)) {
                auto& retained = values[id];
                if (is_tail)
                    retained.tail = retained_osu_time_t{time, value};
                else
                    retained.head = retained_osu_time_t{time, value};
            }
        }
    }
    return values;
}

void warn_omitted_passthrough(std::vector<osu_mania_write_diagnostic_t>& diagnostics,
                             std::string_view identity) {
    diagnostics.push_back({.severity = diagnostic_severity_t::warning,
                           .code = osu_mania_write_diagnostic_code_t::invalid_metadata,
                           .message = "unsafe retained osu! metadata was omitted",
                           .field = std::string(identity)});
}

void append_passthrough_event(std::vector<passthrough_field_t>& output,
                              std::vector<osu_mania_write_diagnostic_t>& diagnostics,
                              std::string_view identity, std::string_view key,
                              std::string_view value, bool is_storyboard_line) {
    if (is_storyboard_line) return;
    if (key != "[]" || !is_writer_safe_text(value) || value.empty() || value.front() == '[') {
        if (key == "[]") warn_omitted_passthrough(diagnostics, identity);
        return;
    }
    const auto comma = value.find(',');
    const auto kind = value.substr(0, comma);
    if (kind == "0" || kind == "Background" || kind == "1" || kind == "Video" ||
        kind == "Sample")
        return;
    output.push_back({"Events", std::string(value)});
}

auto is_writer_managed_field(std::string_view section, std::string_view key) -> bool {
    return (section == "General" && (key == "Mode" || key == "AudioFilename")) ||
           (section == "Editor" && key == "Bookmarks") ||
           (section == "Difficulty" && key == "CircleSize") ||
           (section == "Metadata" && (key == "Title" || key == "Artist" || key == "Creator" ||
                                      key == "Version" || key == "Tags"));
}

void append_passthrough_field(std::vector<passthrough_field_t>& output,
                              std::vector<osu_mania_write_diagnostic_t>& diagnostics,
                              std::string_view identity, std::string_view section,
                              std::string_view key, std::string_view value) {
    if (section == "HitObjects" || section == "TimingPoints") return;
    if (section == "Events") {
        append_passthrough_event(output, diagnostics, identity, key, value, false);
        return;
    }
    if (key == "[]") {
        if (section.empty() || !is_writer_safe_key(section) || !is_writer_safe_text(value) ||
            value.empty() || value.front() == '[') {
            warn_omitted_passthrough(diagnostics, identity);
            return;
        }
        output.push_back({std::string(section), std::string(value)});
        return;
    }
    if (!is_writer_safe_key(section) || !is_writer_safe_key(key) || !is_writer_safe_text(value)) {
        warn_omitted_passthrough(diagnostics, identity);
        return;
    }
    if (!is_writer_managed_field(section, key))
        output.push_back({std::string(section), std::string(key) + ":" + std::string(value)});
}

struct retained_field_identity_t {
    const std::vector<std::byte>* source{};
    std::size_t order{};
    std::pair<std::string, std::string> field;
};

auto record_retained_field(const format_metadata_t& group, std::size_t index,
                           std::vector<retained_field_identity_t>& retained,
                           std::string_view identity,
                           std::vector<osu_mania_write_diagnostic_t>& diagnostics) -> bool {
    if (index >= group.ordered_field_source_orders.size() || !group.encoding.original_bytes)
        return false;
    const auto order = group.ordered_field_source_orders[index];
    const auto existing = std::ranges::find_if(retained, [&](const auto& item) {
        return item.source == group.encoding.original_bytes.get() && item.order == order;
    });
    if (existing != retained.end()) {
        if (existing->field != group.ordered_fields[index])
            diagnostics.push_back({.severity = diagnostic_severity_t::error,
                                   .code = osu_mania_write_diagnostic_code_t::invalid_metadata,
                                   .message = "conflicting scoped copies of one retained osu! field",
                                   .field = std::string(identity)});
        return true;
    }
    retained.push_back({group.encoding.original_bytes.get(), order,
                        group.ordered_fields[index]});
    return false;
}

auto passthrough_fields(const chart_document_t& chart, const song_document_t& song,
                        std::vector<osu_mania_write_diagnostic_t>& diagnostics)
    -> std::vector<passthrough_field_t> {
    std::vector<passthrough_field_t> result;
    const auto storyboard_source_orders = embedded_storyboard_source_orders(song);
    std::vector<const format_metadata_t*> groups;
    for (const auto& group : song.metadata.format_groups)
        if (group.family == format_family_t::osu_mania)
            groups.push_back(&group);
    for (const auto& group : chart.metadata.format_groups)
        if (group.family == format_family_t::osu_mania)
            groups.push_back(&group);
    std::vector<retained_field_identity_t> retained_fields;
    for (const auto* group : groups)
        for (std::size_t index = 0; index < group->ordered_fields.size(); ++index) {
            const auto& [identity, value] = group->ordered_fields[index];
            if (record_retained_field(*group, index, retained_fields, identity, diagnostics))
                continue;
            const auto dot = identity.find('.');
            if (dot == std::string::npos || identity.starts_with("osu."))
                continue;
            const auto section = std::string_view{identity}.substr(0, dot);
            const auto key = std::string_view{identity}.substr(dot + 1);
            if (section == "Events") {
                const bool storyboard_line =
                    key == "[]" && index < group->ordered_field_source_orders.size() &&
                    storyboard_source_orders.contains(group->ordered_field_source_orders[index]);
                append_passthrough_event(result, diagnostics, identity, key, value,
                                         storyboard_line);
            } else {
                append_passthrough_field(result, diagnostics, identity, section, key, value);
            }
        }
    return result;
}

template <typename fail_t>
auto select_osu_chart(const song_document_t& song, const osu_mania_write_options_t& options,
                      fail_t&& fail) -> const chart_document_t* {
    if (options.chart) {
        const auto found = song.find_chart(*options.chart);
        if (found == nullptr)
            fail(osu_mania_write_diagnostic_code_t::missing_chart,
                 "requested chart ID does not exist in this song", "charts");
        return &*found;
    }
    if (song.charts.size() == 1) return &song.charts.front();
    fail(osu_mania_write_diagnostic_code_t::missing_chart,
         "select one chart because osu! stores one chart per .osu file", "charts");
    return nullptr;
}

template <typename fail_t>
auto validate_and_bake_osu_chart(const song_document_t& song, const chart_document_t& chart,
                                 fail_t&& fail) -> sv_recipe_export_bake_result_t {
    const auto validation = validate_document(song);
    const auto invalid = std::ranges::find_if(validation, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
    if (invalid != validation.end())
        fail(osu_mania_write_diagnostic_code_t::invalid_document,
             "document failed core validation: " + invalid->message, "chart");
    auto baked = bake_sv_recipes_for_export(chart);
    if (!baked)
        fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
             "SV recipe export bake failed: " + baked.diagnostics.front().message, "scrolls");
    return baked;
}

template <typename fail_t>
void inspect_osu_profile_groups(const std::vector<format_metadata_t>& groups,
                                std::optional<osu_mania_file_profile_t>& retained_profile,
                                fail_t&& fail) {
    for (const auto& group : groups) {
        if (group.family != format_family_t::osu_mania) continue;
        for (const auto& [key, value] : group.ordered_fields) {
            if (key != "osu.file_format") continue;
            std::uint32_t version{};
            if (!parse_integer(value, version) || (version != 14 && version != 128))
                fail(osu_mania_write_diagnostic_code_t::invalid_metadata,
                     "retained osu! file-format profile is malformed or unsupported",
                     "osu.file_format");
            const auto candidate = version == 128 ? osu_mania_file_profile_t::lazer_v128
                                                   : osu_mania_file_profile_t::stable_v14;
            if (retained_profile && *retained_profile != candidate)
                fail(osu_mania_write_diagnostic_code_t::invalid_metadata,
                     "retained osu! file-format profiles conflict", "osu.file_format");
            retained_profile = candidate;
        }
    }
}

template <typename fail_t>
auto select_osu_profile(const song_document_t& song, const chart_document_t& chart,
                        const osu_mania_write_options_t& options, fail_t&& fail)
    -> osu_mania_file_profile_t {
    std::optional<osu_mania_file_profile_t> retained_profile;
    inspect_osu_profile_groups(song.metadata.format_groups, retained_profile, fail);
    inspect_osu_profile_groups(chart.metadata.format_groups, retained_profile, fail);
    return options.profile.value_or(
        retained_profile.value_or(osu_mania_file_profile_t::stable_v14));
}

template <typename fail_t>
void validate_storyboard_retained_line(std::string_view text, fail_t&& fail) {
    const auto trimmed = trim(text);
    if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']')
        fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
             "storyboard retained lines cannot inject osu! section headers", "storyboards");
    const auto fields = split_csv(text);
    if (fields && fields->size() >= 4 && fields->front() == "Animation" &&
        !safe_relative_path((*fields)[3]))
        fail(osu_mania_write_diagnostic_code_t::invalid_resource,
             "retained Animation event has an unsafe storyboard asset path", "storyboards");
}

template <typename fail_t>
void validate_storyboard_nodes(const storyboard_document_model_t& model, fail_t&& fail) {
    const auto validate_nodes = [&](auto&& self, const std::vector<storyboard_node_t>& nodes) -> void {
        for (const auto& node : nodes) {
            const auto trimmed = trim(node.retained_line);
            if (node.kind == storyboard_node_kind_t::retained_line && trimmed.size() >= 2 &&
                trimmed.front() == '[' && trimmed.back() == ']')
                fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                     "storyboard retained lines cannot inject osu! section headers", "storyboards");
            self(self, node.children);
        }
    };
    for (const auto& sprite : model.sprites) validate_nodes(validate_nodes, sprite.nodes);
}

template <typename fail_t>
auto emit_and_validate_osu_storyboard(const storyboard_document_model_t& model,
                                      const osu_mania_write_options_t& options, fail_t&& fail)
    -> std::optional<std::string> {
    const auto emitted = emit_osb(model, options.max_output_bytes);
    if (!emitted) {
        const bool bounded = std::ranges::any_of(emitted.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code == storyboard_diagnostic_code_t::capacity_exceeded ||
                   diagnostic.code == storyboard_diagnostic_code_t::node_limit ||
                   diagnostic.code == storyboard_diagnostic_code_t::nesting_limit;
        });
        fail(bounded ? osu_mania_write_diagnostic_code_t::output_limit
                     : osu_mania_write_diagnostic_code_t::unsupported_feature,
             "structured storyboard cannot be emitted within the OSB limits", "storyboards");
    }
    const auto reparsed = parse_osb(*emitted.text,
                                    {.maximum_input_bytes = options.max_output_bytes,
                                     .maximum_lines = options.max_lines,
                                     .maximum_nodes =
                                         std::min<std::size_t>(options.max_events, 250'000)});
    if (!reparsed.document) {
        const bool bounded = std::ranges::any_of(reparsed.diagnostics, [](const auto& diagnostic) {
            return diagnostic.code == storyboard_diagnostic_code_t::input_too_large ||
                   diagnostic.code == storyboard_diagnostic_code_t::line_limit ||
                   diagnostic.code == storyboard_diagnostic_code_t::node_limit ||
                   diagnostic.code == storyboard_diagnostic_code_t::nesting_limit ||
                   diagnostic.code == storyboard_diagnostic_code_t::capacity_exceeded;
        });
        fail(bounded ? osu_mania_write_diagnostic_code_t::output_limit
                     : osu_mania_write_diagnostic_code_t::unsupported_feature,
             "emitted storyboard exceeds the configured re-import bounds", "storyboards");
    }
    if (!storyboard_semantically_equal(model, *reparsed.document))
        fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
             "structured storyboard does not semantically re-import from emitted OSB",
             "storyboards");
    return *emitted.text;
}

template <typename fail_t>
auto prepare_osu_storyboard(const song_document_t& song,
                            const osu_mania_write_options_t& options, fail_t&& fail)
    -> std::optional<std::string> {
    const storyboard_document_model_t* model{};
    for (const auto& storyboard : song.storyboards) {
        if (!storyboard.retained_source_lines.empty())
            fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                 "osu!mania cannot export storyboard source lines without an unambiguous "
                 "structured model",
                 "storyboards");
        if (!storyboard.model || (storyboard.model->sprites.empty() &&
                                  storyboard.model->retained_top_level_lines.empty()))
            continue;
        if (model)
            fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                 "osu!mania output cannot combine multiple difficulty storyboard documents",
                 "storyboards");
        if (song.charts.size() != 1)
            fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                 "song-level storyboard content has no chart ownership for multi-chart osu! "
                 "export",
                 "storyboards");
        for (const auto& sprite : storyboard.model->sprites)
            if (!safe_relative_path(sprite.path))
                fail(osu_mania_write_diagnostic_code_t::invalid_resource,
                     "storyboard sprite path must be a safe relative asset path", "storyboards");
        for (const auto& line : storyboard.model->retained_top_level_lines)
            validate_storyboard_retained_line(line.text, fail);
        validate_storyboard_nodes(*storyboard.model, fail);
        model = &*storyboard.model;
    }
    if (!model) return std::nullopt;
    return emit_and_validate_osu_storyboard(*model, options, fail);
}

struct osu_export_geometry_t {
    measure_index_t measures;
    std::vector<lane_id_t> ordered_lane_ids;
};

struct osu_bpm_timing_t {
    std::vector<positioned_bpm_write_t> events;
    std::vector<std::pair<std::uint64_t, signed_rational_t>> groups;
    std::vector<long double> prefix_ms;
};

auto osu_bpm_group_before_or_at(const osu_bpm_timing_t& timing, std::uint64_t row)
    -> std::size_t;

template <typename fail_t>
auto positive_bpm_value(signed_rational_t value, fail_t&& fail) -> long double {
    const auto rational = rational_value(value);
    if (!rational || *rational <= 0.0L)
        fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
             "osu! timing requires finite positive BPM values", "bpms");
    return *rational;
}

template <typename fail_t>
auto build_osu_bpm_timing(const chart_document_t& chart, const measure_index_t& measures,
                          fail_t&& fail) -> osu_bpm_timing_t {
    osu_bpm_timing_t timing;
    timing.events.reserve(chart.bpms.size());
    for (std::size_t index = 0; index < chart.bpms.size(); ++index) {
        const auto& event = chart.bpms[index];
        const auto row = absolute_row(measures, event.position);
        if (!row)
            fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                 "BPM event lies outside chart geometry", "bpms");
        if (event.beats_per_minute.numerator <= 0 || event.beats_per_minute.denominator == 0)
            fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                 "osu! timing requires positive BPM values", "bpms");
        timing.events.push_back({*row, &event, index});
    }
    std::ranges::stable_sort(timing.events, [](const auto& left, const auto& right) {
        return std::tuple{left.row, left.event->provenance.source_order, left.insertion_order} <
               std::tuple{right.row, right.event->provenance.source_order,
                          right.insertion_order};
    });
    if (timing.events.empty())
        fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
             "osu!mania requires at least one BPM event", "bpms");
    for (const auto& event : timing.events) {
        if (!timing.groups.empty() && timing.groups.back().first == event.row)
            timing.groups.back().second = event.event->beats_per_minute;
        else
            timing.groups.emplace_back(event.row, event.event->beats_per_minute);
    }
    timing.prefix_ms.resize(timing.groups.size());
    std::uint64_t prefix_row{};
    long double elapsed{};
    std::size_t active{};
    for (std::size_t index = 0; index < timing.groups.size(); ++index) {
        const auto boundary = timing.groups[index].first;
        if (boundary < prefix_row)
            fail(osu_mania_write_diagnostic_code_t::invalid_document,
                 "BPM events are not ordered by chart position", "bpms");
        const auto beats = static_cast<long double>(boundary - prefix_row) * 4.0L /
                           static_cast<long double>(chart.divisions_per_standard_measure);
        elapsed += beats * 60'000.0L /
                   positive_bpm_value(timing.groups[active].second, fail);
        timing.prefix_ms[index] = elapsed;
        prefix_row = boundary;
        active = index;
    }
    return timing;
}

template <typename relative_time_t>
auto infer_osu_offset_ms(const chart_document_t& chart, const measure_index_t& measures,
                         const std::vector<positioned_bpm_write_t>& bpms,
                         const std::unordered_map<std::uint64_t, retained_osu_note_times_t>& retained,
                         relative_time_t&& relative_milliseconds_at) -> std::optional<long double> {
    const bool explicit_origin = std::ranges::any_of(chart.metadata.format_groups, [](const auto& group) {
        return group.family == format_family_t::osu_mania &&
               std::ranges::any_of(group.ordered_fields, [](const auto& field) {
                   return field.first == "osu.import.explicit_source_origin" && field.second == "1";
               });
    });
    if (explicit_origin) return std::nullopt;
    if (chart.source_family != format_family_t::osu_mania ||
        chart.beat_zero_audio_offset_seconds.numerator != 0)
        return std::nullopt;
    std::vector<long double> baselines;
    for (const auto& bpm : bpms) {
        if (bpm.event->provenance.source_field != "TimingPoints") continue;
        const auto parts = split(bpm.event->provenance.source_spelling, ',');
        long double source_ms{};
        if (!parts.empty() && parse_number(parts.front(), source_ms))
            baselines.push_back(source_ms - relative_milliseconds_at(bpm.row));
    }
    if (!baselines.empty()) {
        std::ranges::sort(baselines);
        return baselines[baselines.size() / 2];
    }
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto source = retained.find(note.id.value);
            const auto row = absolute_row(measures, note.position);
            if (source != retained.end() && source->second.head && row)
                baselines.push_back(source->second.head->value - relative_milliseconds_at(*row));
        }
    }
    if (baselines.empty()) return std::nullopt;
    std::ranges::sort(baselines);
    return baselines[baselines.size() / 2];
}

template <typename fail_t>
auto collect_osu_scroll_events(const chart_document_t& chart, const measure_index_t& measures,
                               fail_t&& fail) -> std::vector<positioned_scroll_write_t> {
    std::vector<positioned_scroll_write_t> events;
    events.reserve(chart.scrolls.size());
    for (std::size_t index = 0; index < chart.scrolls.size(); ++index) {
        const auto& event = chart.scrolls[index];
        const auto row = absolute_row(measures, event.position);
        if (!row)
            fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                 "scroll event lies outside chart geometry", "scrolls");
        if (event.multiplier.numerator <= 0 || event.multiplier.denominator == 0)
            fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                 "osu! inherited timing requires positive scroll multipliers", "scrolls");
        events.push_back({*row, &event, index});
    }
    std::ranges::stable_sort(events, [](const auto& left, const auto& right) {
        return std::tuple{left.row, left.event->provenance.source_order, left.insertion_order} <
               std::tuple{right.row, right.event->provenance.source_order,
                          right.insertion_order};
    });
    return events;
}

template <typename fail_t>
auto collect_osu_kiai_events(const chart_document_t& chart, const measure_index_t& measures,
                             fail_t&& fail) -> std::vector<positioned_kiai_write_t> {
    std::vector<positioned_kiai_write_t> events;
    events.reserve(chart.kiai.size());
    for (std::size_t index = 0; index < chart.kiai.size(); ++index) {
        const auto row = absolute_row(measures, chart.kiai[index].position);
        if (!row)
            fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                 "kiai event lies outside chart geometry", "kiai");
        events.push_back({*row, &chart.kiai[index], index});
    }
    std::ranges::stable_sort(events, [](const auto& left, const auto& right) {
        return std::tuple{left.row, left.event->provenance.source_order, left.insertion_order} <
               std::tuple{right.row, right.event->provenance.source_order,
                          right.insertion_order};
    });
    return events;
}

template <typename fail_t>
auto osu_meter_at(const chart_document_t& chart, const measure_index_t& measures,
                  std::uint64_t row, fail_t&& fail) -> std::uint32_t {
    const auto after = std::ranges::upper_bound(measures.entries, row, {},
                                                &measure_index_entry_t::absolute_begin_row);
    const auto index = after == measures.entries.begin()
                           ? std::size_t{0}
                           : static_cast<std::size_t>(std::distance(measures.entries.begin(), after) - 1);
    const auto& definition = chart.measures[index].definition;
    if (definition.meter) {
        if (definition.meter->denominator != 4 || definition.meter->numerator < 1 ||
            definition.meter->numerator > 32)
            fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                 "osu! timing points support time signatures with a 4 denominator and numerators "
                 "from 1 through 32",
                 "measures.meter");
        return definition.meter->numerator;
    }
    if (definition.length.denominator != 0 && definition.length.numerator <= UINT64_MAX / 4) {
        const auto quarter_beats = definition.length.numerator * 4;
        if (quarter_beats % definition.length.denominator == 0) {
            const auto inferred = quarter_beats / definition.length.denominator;
            if (inferred >= 1 && inferred <= 32) return static_cast<std::uint32_t>(inferred);
        }
    }
    return 4;
}

auto osu_timing_sample_fields(const source_provenance_t& provenance)
    -> std::array<std::int64_t, 3> {
    std::array<std::int64_t, 3> fields{0, 0, 100};
    const auto source = split(provenance.source_spelling, ',');
    if (source.size() < 6) return fields;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        std::int64_t parsed{};
        if (parse_integer(source[index + 3], parsed)) fields[index] = parsed;
    }
    return fields;
}

template <typename timestamp_t, typename effects_t, typename account_t, typename fail_t>
void append_scroll_and_kiai_timing_lines(
    std::vector<osu_output_event_t>& output, std::set<std::uint64_t>& timing_rows,
    std::size_t& serial, const measure_index_t& measures,
    const std::vector<positioned_scroll_write_t>& scrolls,
    const std::vector<positioned_kiai_write_t>& kiais, timestamp_t&& timestamp_at_row,
    effects_t&& effects_at, account_t&& account_event, fail_t&& fail) {
    std::set<std::uint64_t> unused_red_rows;
    const auto scroll_at = [&](std::uint64_t row) {
        const auto after = std::ranges::upper_bound(scrolls, row, {},
                                                    &positioned_scroll_write_t::row);
        return after == scrolls.begin() ? signed_rational_t{1, 1}
                                        : std::prev(after)->event->multiplier;
    };
    for (const auto& scroll : scrolls) {
        const auto value = rational_value(
            {scroll.event->multiplier.numerator, scroll.event->multiplier.denominator});
        if (!value || *value <= 0.0L)
            fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                 "osu! inherited timing requires positive finite scroll multipliers", "scrolls");
        append_osu_timing_line(output, unused_red_rows, timing_rows, serial, scroll.row,
                               -100.0L / *value, 4,
                               false,
                               scroll.event->provenance.source_order
                                   ? scroll.event->provenance.source_order
                                   : scroll.insertion_order,
                               osu_timing_sample_fields(scroll.event->provenance), measures,
                               timestamp_at_row, effects_at, account_event, fail);
    }
    for (const auto& kiai : kiais) {
        if (timing_rows.contains(kiai.row)) continue;
        const auto multiplier = rational_value(scroll_at(kiai.row));
        if (!multiplier || *multiplier <= 0.0L)
            fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                 "osu! inherited timing requires positive finite scroll multipliers", "scrolls");
        append_osu_timing_line(output, unused_red_rows, timing_rows, serial, kiai.row,
                               -100.0L / *multiplier, 4, false,
                               kiai.event->provenance.source_order
                                   ? kiai.event->provenance.source_order
                                   : kiai.insertion_order,
                               osu_timing_sample_fields(kiai.event->provenance), measures,
                               timestamp_at_row, effects_at, account_event, fail);
    }
}

// A timing line synthesized at a measure start carries the sample set, index and
// volume of the source point that is active there, so the sample mix does not
// change where the file gains a new line.
template <typename bpm_events_t, typename scrolls_t, typename kiais_t>
auto active_osu_sample_fields(std::uint64_t row, const bpm_events_t& bpms, const scrolls_t& scrolls,
                              const kiais_t& kiais) -> std::array<std::int64_t, 3> {
    std::array<std::int64_t, 3> fields{0, 0, 100};
    std::optional<std::uint64_t> best_row;
    const auto consider = [&](const auto& points) {
        for (const auto& point : points) {
            if (point.row > row || point.event->provenance.source_spelling.empty() ||
                (best_row && point.row < *best_row))
                continue;
            best_row = point.row;
            fields = osu_timing_sample_fields(point.event->provenance);
        }
    };
    consider(bpms);
    consider(scrolls);
    consider(kiais);
    return fields;
}

template <typename timestamp_t, typename account_t, typename fail_t>
auto build_osu_timing_output(
    const chart_document_t& chart, const measure_index_t& measures,
    const osu_bpm_timing_t& bpm_timing, const std::vector<positioned_scroll_write_t>& scrolls,
    const std::vector<positioned_kiai_write_t>& kiais, timestamp_t&& timestamp_at_row,
    account_t&& account_event, std::size_t& serial, fail_t&& fail)
    -> std::vector<osu_output_event_t> {
    std::vector<osu_output_event_t> output;
    std::set<std::uint64_t> red_rows;
    std::set<std::uint64_t> timing_rows;
    const auto effects_at = [&](std::uint64_t row) {
        const auto after = std::ranges::upper_bound(kiais, row, {}, &positioned_kiai_write_t::row);
        if (after == kiais.begin()) return std::uint32_t{};
        const auto& event = *std::prev(after)->event;
        auto effects = event.retained_effect_bits;
        if (event.enabled)
            effects |= 1U;
        else
            effects &= ~1U;
        return effects;
    };
    for (const auto& bpm : bpm_timing.events)
        append_osu_timing_line(output, red_rows, timing_rows, serial, bpm.row,
                               60'000.0L / positive_bpm_value(bpm.event->beats_per_minute, fail),
                               osu_meter_at(chart, measures, bpm.row, fail), true,
                               bpm.event->provenance.source_order
                                   ? bpm.event->provenance.source_order
                                   : bpm.insertion_order,
                               osu_timing_sample_fields(bpm.event->provenance), measures,
                               timestamp_at_row, effects_at, account_event, fail);
    for (const auto& entry : measures.entries) {
        const auto row = entry.absolute_begin_row;
        if (red_rows.contains(row)) continue;
        const auto active = osu_bpm_group_before_or_at(bpm_timing, row);
        append_osu_timing_line(
            output, red_rows, timing_rows, serial, row,
            60'000.0L / positive_bpm_value(bpm_timing.groups[active].second, fail),
            osu_meter_at(chart, measures, row, fail), true,
            entry.id.value ? static_cast<std::size_t>(entry.id.value) : serial,
            active_osu_sample_fields(row, bpm_timing.events, scrolls, kiais), measures, timestamp_at_row, effects_at, account_event, fail);
    }
    append_scroll_and_kiai_timing_lines(output, timing_rows, serial, measures, scrolls, kiais,
                                        timestamp_at_row, effects_at, account_event, fail);
    std::ranges::stable_sort(output, [](const auto& left, const auto& right) {
        return std::tuple{left.milliseconds, left.row, left.order, left.serial} <
               std::tuple{right.milliseconds, right.row, right.order, right.serial};
    });
    return output;
}

auto osu_bpm_group_before_or_at(const osu_bpm_timing_t& timing, std::uint64_t row)
    -> std::size_t {
    const auto after = std::ranges::upper_bound(
        timing.groups, row, {}, &std::pair<std::uint64_t, signed_rational_t>::first);
    return after == timing.groups.begin()
               ? std::size_t{0}
               : static_cast<std::size_t>(std::distance(timing.groups.begin(), after) - 1);
}

template <typename fail_t>
auto osu_relative_milliseconds(const osu_bpm_timing_t& timing, std::uint64_t row,
                               std::uint64_t divisions, fail_t&& fail) -> long double {
    if (row < timing.groups.front().first) {
        const auto beats = static_cast<long double>(row) * 4.0L /
                           static_cast<long double>(divisions);
        return beats * 60'000.0L / positive_bpm_value(timing.groups.front().second, fail);
    }
    const auto active = osu_bpm_group_before_or_at(timing, row);
    const auto beats = static_cast<long double>(row - timing.groups[active].first) * 4.0L /
                       static_cast<long double>(divisions);
    return timing.prefix_ms[active] +
           beats * 60'000.0L / positive_bpm_value(timing.groups[active].second, fail);
}

template <typename fail_t, typename warn_t>
void validate_osu_chart_features(const chart_document_t& chart,
                                 const osu_mania_write_options_t& options, fail_t&& fail,
                                 warn_t&& warn) {
    if (chart.measures.empty())
        fail(osu_mania_write_diagnostic_code_t::invalid_document,
             "osu!mania requires at least one chart measure", "measures");
    if (chart.measures.size() > options.max_measures)
        fail(osu_mania_write_diagnostic_code_t::output_limit,
             "chart measure count exceeds the configured export bound", "measures");
    const bool unsupported = !chart.pauses.empty() || !chart.warps.empty() ||
                             !chart.speeds.empty() || !chart.sections.empty() ||
                             !chart.timing_anchors.empty() || chart.bms_control_tree ||
                             !chart.active_branch_selection.empty();
    if (unsupported)
        fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
             "osu!mania cannot represent pauses, warps, speed curves, sections, timing anchors, "
             "or conditional branches",
             "chart");
    if (chart.beat_zero_audio_offset_seconds.denominator == 0)
        fail(osu_mania_write_diagnostic_code_t::invalid_document,
             "beat-zero audio offset has a zero denominator", "beat_zero_audio_offset_seconds");
    const bool semantic_layers = chart.semantic_layers.size() != 0 ||
                                 std::ranges::any_of(chart.measures, [](const auto& measure) {
                                     return std::ranges::any_of(measure.notes, [](const auto& note) {
                                         return !note.semantic_layers.empty();
                                     });
                                 });
    if (semantic_layers)
        warn(osu_mania_write_diagnostic_code_t::lossy_transform,
             "semantic note layers were flattened while preserving each note's gameplay kind",
             "semantic_layers");
}

template <typename fail_t>
auto build_osu_measure_index(const chart_document_t& chart,
                             const osu_mania_write_options_t& options, fail_t&& fail)
    -> measure_index_t {
    const auto built = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built))
        fail(osu_mania_write_diagnostic_code_t::invalid_document,
             "chart measure geometry is invalid", "measures");
    const auto& measures = std::get<measure_index_t>(built);
    if (chart.divisions_per_standard_measure == 0 || measures.total_rows == 0)
        fail(osu_mania_write_diagnostic_code_t::invalid_document,
             "chart must have a positive row resolution", "divisions_per_standard_measure");
    for (const auto& entry : measures.entries)
        if (entry.row_capacity > options.max_rows_per_measure)
            fail(osu_mania_write_diagnostic_code_t::output_limit,
                 "measure row capacity exceeds the configured export bound", "measures");
    return measures;
}

template <typename fail_t>
auto normalize_osu_layout(const lane_layout_t& layout, fail_t&& fail) -> std::vector<lane_id_t> {
    if (layout.lanes.empty() || layout.lanes.size() > maximum_osu_mania_keys)
        fail(osu_mania_write_diagnostic_code_t::invalid_layout,
             "osu!mania supports integer lane counts from 1 through 18", "lane_layouts");
    std::vector<const lane_definition_t*> ordered(layout.lanes.size());
    for (const auto& lane : layout.lanes) {
        if (lane.display_order >= ordered.size() || ordered[lane.display_order])
            fail(osu_mania_write_diagnostic_code_t::invalid_layout,
                 "lane display order must be unique and contiguous", "lane_layouts");
        if (lane.role != lane_role_t::playable)
            fail(osu_mania_write_diagnostic_code_t::invalid_layout,
                 "osu!mania lane layouts can contain playable lanes only", "lane_layouts");
        ordered[lane.display_order] = &lane;
    }
    std::vector<lane_id_t> ids;
    ids.reserve(ordered.size());
    for (const auto* lane : ordered) {
        if (!lane)
            fail(osu_mania_write_diagnostic_code_t::invalid_layout,
                 "lane display order has a gap", "lane_layouts");
        ids.push_back(lane->id);
    }
    return ids;
}

template <typename fail_t>
auto build_osu_export_geometry(const chart_document_t& chart,
                               const osu_mania_write_options_t& options, fail_t&& fail)
    -> osu_export_geometry_t {
    if (chart.lane_layouts.empty())
        fail(osu_mania_write_diagnostic_code_t::invalid_layout,
             "osu!mania export requires an explicit lane layout", "lane_layouts");
    auto ordered_lane_ids = normalize_osu_layout(chart.lane_layouts.front().layout, fail);
    for (const auto& segment : chart.lane_layouts)
        if (normalize_osu_layout(segment.layout, fail) != ordered_lane_ids)
            fail(osu_mania_write_diagnostic_code_t::invalid_layout,
                 "osu!mania requires one stable lane order for the entire chart", "lane_layouts");
    return {.measures = build_osu_measure_index(chart, options, fail),
            .ordered_lane_ids = std::move(ordered_lane_ids)};
}

template <typename fail_t>
void append_osu_output_line(std::string& output, std::size_t& output_lines,
                            const osu_mania_write_options_t& options, std::string_view line,
                            fail_t&& fail) {
    if (output_lines >= options.max_lines || line.size() >= options.max_output_bytes ||
        output.size() > options.max_output_bytes - line.size() - 1)
        fail(osu_mania_write_diagnostic_code_t::output_limit,
             "osu! output exceeds the configured byte or line limit", "output");
    output.append(line);
    output.push_back('\n');
    ++output_lines;
}

template <typename append_line_t>
void append_osu_section(const std::vector<passthrough_field_t>& fields,
                        std::string_view section, append_line_t&& append_line) {
    append_line("[" + std::string(section) + "]");
    for (const auto& field : fields)
        if (field.section == section) append_line(field.line);
}

auto osu_bookmarks_text(const std::vector<std::int64_t>& bookmarks) -> std::string {
    std::string line{"Bookmarks: "};
    for (std::size_t index = 0; index < bookmarks.size(); ++index) {
        if (index != 0) line.push_back(',');
        line += std::to_string(bookmarks[index]);
    }
    return line;
}

template <typename fail_t>
auto collect_osu_music_files(const song_document_t& song, fail_t&& fail)
    -> std::vector<std::string> {
    std::vector<std::string> files;
    for (const auto& resource : song.resources) {
        if (std::ranges::find(resource.roles, resource_role_t::music) == resource.roles.end())
            continue;
        const auto name = osu_resource_path(song, resource.id, true);
        if (!name)
            fail(osu_mania_write_diagnostic_code_t::invalid_resource,
                 "music resource refers to an unsafe or missing audio asset",
                 "General.AudioFilename");
        if (std::ranges::find(files, *name) == files.end()) files.push_back(*name);
    }
    if (files.size() > 1)
        fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
             "osu! General.AudioFilename can represent only one music resource",
             "General.AudioFilename");
    return files;
}

template <typename fail_t>
auto collect_osu_tags(const song_document_t& song, const chart_document_t& chart,
                      fail_t&& fail) -> std::string {
    const auto check = [&](std::string_view value, std::string_view field) {
        if (!is_writer_safe_text(value))
            fail(osu_mania_write_diagnostic_code_t::invalid_metadata,
                 "metadata cannot contain newlines or NUL bytes", std::string(field));
    };
    check(song.metadata.title, "Metadata.Title");
    check(song.metadata.artist, "Metadata.Artist");
    check(chart.metadata.name, "Metadata.Version");
    check(chart.metadata.author, "Metadata.Creator");
    std::string tags;
    for (const auto& tag : song.metadata.ordered_search_tags) {
        check(tag, "Metadata.Tags");
        const bool invalid = tag.empty() || std::ranges::any_of(tag, [](unsigned char character) {
            return character == ' ' || character == '\t';
        });
        if (invalid)
            fail(osu_mania_write_diagnostic_code_t::invalid_metadata,
                 "osu! tags must be nonempty whitespace-free tokens", "Metadata.Tags");
        if (!tags.empty()) tags.push_back(' ');
        tags += tag;
    }
    return tags;
}

template <typename account_t>
void account_osu_metadata(const std::vector<std::string>& music_files,
                          const std::vector<std::int64_t>& bookmarks, const song_document_t& song,
                          const chart_document_t& chart, std::string_view tags, std::size_t keys,
                          const std::vector<passthrough_field_t>& fields, account_t&& account) {
    if (!music_files.empty()) account("AudioFilename: " + music_files.front());
    account("Mode: 3");
    if (!bookmarks.empty()) account(osu_bookmarks_text(bookmarks));
    account("Title:" + song.metadata.title);
    account("Artist:" + song.metadata.artist);
    account("Creator:" + chart.metadata.author);
    account("Version:" + chart.metadata.name);
    if (!tags.empty()) account("Tags:" + std::string(tags));
    account("CircleSize:" + std::to_string(keys));
    for (const auto& field : fields)
        if (field.section != "Events" && field.section != "TimingPoints" &&
            field.section != "HitObjects")
            account(field.line);
}

// Layout selection owns SpecialStyle for the two supported split-key modes.
// Other counts retain their original metadata, including unknown style values.
void project_osu_specialstyle_field(const chart_document_t& chart,
                                    std::vector<passthrough_field_t>& fields) {
    if (chart.lane_layouts.empty()) return;
    const auto& layout = chart.lane_layouts.front().layout;
    const auto keys = layout.lanes.size();
    if (keys != 12 && keys != 16) return;
    const auto base = "osu-mania-" + std::to_string(keys) + "k";
    if (layout.profile_id != base && layout.profile_id != base + "-specialstyle") return;
    std::erase_if(fields, [](const auto& field) {
        if (field.section != "General") return false;
        const auto colon = field.line.find(':');
        return trim(field.line.substr(0, colon)) == "SpecialStyle";
    });
    fields.push_back({"General", layout.profile_id == base ? "SpecialStyle:0" : "SpecialStyle:1"});
}

template <typename append_line_t>
void append_osu_metadata_sections(const song_document_t& song, const chart_document_t& chart,
                                 const std::vector<std::string>& music_files,
                                 const std::vector<std::int64_t>& bookmarks, std::string_view tags,
                                 std::size_t keys,
                                 const std::vector<passthrough_field_t>& fields,
                                 append_line_t&& append_line) {
    append_osu_section(fields, "General", append_line);
    if (!music_files.empty()) append_line("AudioFilename: " + music_files.front());
    append_line("Mode: 3");
    append_osu_section(fields, "Editor", append_line);
    if (!bookmarks.empty()) append_line(osu_bookmarks_text(bookmarks));
    append_osu_section(fields, "Metadata", append_line);
    append_line("Title:" + song.metadata.title);
    append_line("Artist:" + song.metadata.artist);
    append_line("Creator:" + chart.metadata.author);
    append_line("Version:" + chart.metadata.name);
    if (!tags.empty()) append_line("Tags:" + std::string(tags));
    append_osu_section(fields, "Difficulty", append_line);
    append_line("CircleSize:" + std::to_string(keys));
}

template <typename append_line_t, typename account_t>
void append_osu_event_sections(const std::vector<passthrough_field_t>& fields,
                               std::vector<osu_output_event_t>& event_output,
                               const std::optional<std::string>& storyboard_output,
                               append_line_t&& append_line, account_t&& account_event) {
    for (const auto& field : fields)
        if (field.section == "Events") account_event();
    append_osu_section(fields, "Events", append_line);
    std::ranges::stable_sort(event_output, [](const auto& left, const auto& right) {
        return std::tuple{left.milliseconds, left.row, left.order, left.serial} <
               std::tuple{right.milliseconds, right.row, right.order, right.serial};
    });
    if (storyboard_output) {
        std::size_t offset{};
        while (offset < storyboard_output->size()) {
            const auto newline = storyboard_output->find('\n', offset);
            const auto end = newline == std::string::npos ? storyboard_output->size() : newline;
            append_line(std::string_view{*storyboard_output}.substr(offset, end - offset));
            account_event();
            if (newline == std::string::npos) break;
            offset = newline + 1;
        }
    }
    for (const auto& event : event_output) append_line(event.line);
}

template <typename append_line_t>
void append_osu_unknown_sections(const std::vector<passthrough_field_t>& fields,
                                 append_line_t&& append_line) {
    std::vector<std::string> sections;
    for (const auto& field : fields) {
        const auto known = field.section == "General" || field.section == "Editor" ||
                           field.section == "Metadata" || field.section == "Difficulty" ||
                           field.section == "Events" || field.section == "TimingPoints" ||
                           field.section == "HitObjects" || field.section == "Colours";
        if (known) continue;
        if (std::ranges::find(sections, field.section) == sections.end())
            sections.push_back(field.section);
    }
    if (std::ranges::any_of(fields, [](const auto& field) { return field.section == "Colours"; }))
        append_osu_section(fields, "Colours", append_line);
    for (const auto& section : sections) append_osu_section(fields, section, append_line);
}

template <typename fail_t, typename account_t>
auto assemble_osu_output(const song_document_t& song, const chart_document_t& chart,
                         const osu_mania_write_options_t& options,
                         osu_mania_file_profile_t profile,
                         const std::vector<std::string>& music_files,
                         const std::vector<std::int64_t>& bookmarks, std::string_view tags,
                         std::size_t keys, const std::vector<passthrough_field_t>& fields,
                         std::vector<osu_output_event_t>& events,
                         const std::vector<osu_output_event_t>& timing,
                         const std::vector<osu_output_event_t>& hits,
                         const std::optional<std::string>& storyboard, account_t&& account_event,
                         fail_t&& fail) -> std::string {
    std::string output;
    std::size_t output_lines{};
    const auto append_line = [&](std::string_view line) {
        append_osu_output_line(output, output_lines, options, line, fail);
    };
    append_line(profile == osu_mania_file_profile_t::lazer_v128 ? "osu file format v128"
                                                                : "osu file format v14");
    append_osu_metadata_sections(song, chart, music_files, bookmarks, tags, keys, fields,
                                 append_line);
    append_osu_event_sections(fields, events, storyboard, append_line, account_event);
    append_osu_section(fields, "TimingPoints", append_line);
    for (const auto& event : timing) append_line(event.line);
    append_osu_section(fields, "HitObjects", append_line);
    for (const auto& event : hits) append_line(event.line);
    append_osu_unknown_sections(fields, append_line);
    return output;
}

struct osu_serialized_time_t {
    long double value{};
    std::string text;
};

struct osu_note_sample_t {
    std::int64_t hit_sound{};
    std::array<std::string, 4> values{"0", "0", "0", "0"};
    std::string field;
};

template <typename timestamp_t, typename fail_t>
auto serialize_osu_note_time(std::uint64_t row,
                             std::optional<retained_osu_time_t> retained, bool lazer_profile,
                             bool& rounded_hit_time, timestamp_t&& timestamp_at_row,
                             fail_t&& fail) -> osu_serialized_time_t {
    const auto exact = timestamp_at_row(row);
    if (lazer_profile) {
        if (!std::isfinite(exact) || exact < -86'400'000.0L || exact > 604'800'000.0L)
            fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                 "hit-object time is outside the supported lazer decimal range", "HitObjects.time");
        if (retained && std::fabs(exact - retained->value) <= 1.0e-10L)
            return {exact, retained->spelling};
        auto text = format_double(exact);
        if (text.empty())
            fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                 "hit-object time cannot be serialized as a finite decimal", "HitObjects.time");
        return {exact, std::move(text)};
    }
    const auto rounded = writer_round_ms(exact);
    if (!rounded)
        fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
             "hit-object time is outside the supported integer millisecond range", "HitObjects.time");
    if (std::fabs(exact - static_cast<long double>(*rounded)) > 1.0e-10L)
        rounded_hit_time = true;
    return {static_cast<long double>(*rounded), std::to_string(*rounded)};
}

template <typename fail_t>
auto build_osu_note_sample(const note_t& note, const song_document_t& song, bool hold,
                           fail_t&& fail) -> osu_note_sample_t {
    osu_note_sample_t sample;
    if (const auto raw = split_csv(note.provenance.source_spelling); raw && raw->size() >= 5) {
        std::int64_t parsed{};
        if (parse_integer((*raw)[4], parsed)) sample.hit_sound = parsed;
        if (raw->size() >= 6) {
            const auto original = split((*raw)[5], ':');
            const auto offset = hold ? std::size_t{1} : std::size_t{0};
            for (std::size_t index = 0; index < sample.values.size(); ++index) {
                if (offset + index >= original.size()) break;
                std::int64_t parsed_value{};
                if (parse_integer(original[offset + index], parsed_value))
                    sample.values[index] = std::string(original[offset + index]);
            }
        }
    }
    if (note.sound) {
        const auto name = osu_resource_path(song, *note.sound, true);
        if (!name || name->find(':') != std::string::npos)
            fail(osu_mania_write_diagnostic_code_t::invalid_resource,
                 "note sample must refer to a safe relative audio asset whose path contains no "
                 "colon",
                 "HitObjects.hitSample");
        sample.field = sample.values[0] + ":" + sample.values[1] + ":" + sample.values[2] +
                       ":" + sample.values[3] + ":" + *name;
    } else if (std::ranges::any_of(sample.values, [](const auto& value) { return value != "0"; })) {
        sample.field = sample.values[0] + ":" + sample.values[1] + ":" + sample.values[2] +
                       ":" + sample.values[3] + ":";
    }
    return sample;
}

template <typename timestamp_t, typename fail_t, typename warn_t, typename account_t>
auto serialize_osu_note(const note_t& note, std::uint64_t begin_row, std::size_t lane,
                        std::size_t keys, const song_document_t& song,
                        const measure_index_t& measures,
                        const std::unordered_map<std::uint64_t, retained_osu_note_times_t>& retained,
                        bool lazer_profile, bool& rounded_hit_time, timestamp_t&& timestamp_at_row,
                        std::size_t serial, account_t&& account_event, fail_t&& fail,
                        warn_t&& warn) -> osu_output_event_t {
    const bool hold = note.kind == note_kind_t::hold || note.kind == note_kind_t::roll;
    if (note.kind != note_kind_t::tap && !hold)
        fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
             "osu!mania supports tap and hold hit objects only", "HitObjects.type");
    if (hold != note.end.has_value())
        fail(osu_mania_write_diagnostic_code_t::invalid_document,
             "hold notes require a tail and tap notes cannot have one", "HitObjects.end");
    if (note.kind == note_kind_t::roll)
        warn(osu_mania_write_diagnostic_code_t::lossy_transform,
             "roll was exported as an osu!mania hold", "HitObjects.type");
    const auto source = retained.find(note.id.value);
    const auto head = serialize_osu_note_time(
        begin_row, source == retained.end() ? std::nullopt : source->second.head, lazer_profile,
        rounded_hit_time, timestamp_at_row, fail);
    std::optional<osu_serialized_time_t> tail;
    if (note.end) {
        const auto end_row = absolute_row(measures, *note.end);
        if (!end_row || *end_row < begin_row)
            fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                 "hold tail lies before its head or outside chart geometry", "HitObjects.end");
        tail = serialize_osu_note_time(
            *end_row, source == retained.end() ? std::nullopt : source->second.tail,
            lazer_profile, rounded_hit_time, timestamp_at_row, fail);
        if (tail->value < head.value)
            fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                 lazer_profile ? "decimal serialization would place a hold tail before its head"
                               : "millisecond rounding would place a hold tail before its head",
                 "HitObjects.end");
        if (tail->value > 604'800'000.0L)
            fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                 "hold tail is outside this codec's supported osu! range", "HitObjects.end");
    }
    const auto sample = build_osu_note_sample(note, song, hold, fail);
    const auto x = static_cast<std::uint64_t>(
        ((static_cast<unsigned long long>(lane) * 2ULL + 1ULL) * 256ULL) / keys);
    std::string line = std::to_string(x) + ",192," + head.text + "," +
                       std::to_string(hold ? 128 : 1) + "," +
                       std::to_string(sample.hit_sound) + ",";
    if (tail)
        line += quote_csv(tail->text + ":" + sample.field);
    else if (!sample.field.empty())
        line += quote_csv(sample.field);
    account_event();
    return {head.value, begin_row, serial, serial, std::move(line)};
}

template <typename timestamp_t, typename fail_t>
auto osu_event_time(std::uint64_t row, std::string_view field, bool lazer_profile,
                    timestamp_t&& timestamp_at_row, fail_t&& fail) -> osu_serialized_time_t {
    const auto exact = timestamp_at_row(row);
    if (lazer_profile) {
        auto text = format_double(exact);
        if (!std::isfinite(exact) || text.empty())
            fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                 "event time cannot be serialized as a finite lazer decimal", std::string(field));
        return {exact, std::move(text)};
    }
    const auto rounded = writer_round_ms(exact);
    if (!rounded)
        fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
             "event time is outside the integer millisecond range", std::string(field));
    return {static_cast<long double>(*rounded), std::to_string(*rounded)};
}

template <typename timestamp_t, typename fail_t, typename account_t>
void append_osu_sample_event(std::vector<osu_output_event_t>& output, std::size_t& serial,
                             const measure_index_t& measures, const song_document_t& song,
                             local_position_t position, resource_id_t resource, std::size_t order,
                             std::uint32_t layer, std::int64_t volume, bool lazer_profile,
                             timestamp_t&& timestamp_at_row, account_t&& account_event,
                             fail_t&& fail) {
    const auto row = absolute_row(measures, position);
    if (!row)
        fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
             "sample event lies outside chart geometry", "Events.Sample");
    const auto name = osu_resource_path(song, resource, true);
    if (!name || name->find(':') != std::string::npos)
        fail(osu_mania_write_diagnostic_code_t::invalid_resource,
             "sample event requires a safe relative audio resource without colons", "Events.Sample");
    const auto time = osu_event_time(*row, "Events.Sample", lazer_profile, timestamp_at_row, fail);
    const auto line = "Sample," + time.text + "," + std::to_string(layer) + "," +
                      quote_csv(*name) + "," + std::to_string(volume);
    output.push_back({time.value, *row, order, serial++, line});
    account_event();
}

template <typename timestamp_t, typename account_t, typename fail_t, typename warn_t>
void append_osu_bgm_events(
    std::vector<osu_output_event_t>& output, std::size_t& serial,
    const chart_document_t& chart, const measure_index_t& measures, const song_document_t& song,
    const retained_osu_event_fields_t& retained, bool lazer_profile, timestamp_t&& timestamp_at_row,
    account_t&& account_event, fail_t&& fail, warn_t&& warn) {
    for (const auto& measure : chart.measures) {
        for (const auto& event : measure.bgm_events) {
            if (event.end)
                warn(osu_mania_write_diagnostic_code_t::lossy_transform,
                     "osu! Sample event retains only the BGM head; hold tail is omitted",
                     "bgm_events[].end");
            if (event.sound) {
                const auto [layer, volume] = retained_sample_event_fields(retained, event.id.value);
                append_osu_sample_event(output, serial, measures, song, event.position,
                                        *event.sound, event.provenance.source_order, layer,
                                        volume, lazer_profile, timestamp_at_row, account_event,
                                        fail);
            }
            else
                warn(osu_mania_write_diagnostic_code_t::lossy_transform,
                     "empty background channel event has no osu! sample resource and was omitted",
                     "bgm_events");
        }
    }
}

auto is_osu_sound_event_projected(const chart_document_t& chart, const sound_channel_event_t& event,
                               resource_id_t resource) -> bool {
    return std::ranges::any_of(chart.measures, [&](const auto& measure) {
        return std::ranges::any_of(measure.notes, [&](const auto& note) {
            return note.position == event.position && note.end == event.end &&
                   note.lane == event.lane && note.sound == resource;
        });
    });
}

template <typename timestamp_t, typename account_t, typename fail_t>
void append_osu_sound_channels(
    std::vector<osu_output_event_t>& output, std::size_t& serial,
    const chart_document_t& chart, const measure_index_t& measures, const song_document_t& song,
    bool lazer_profile, timestamp_t&& timestamp_at_row, account_t&& account_event,
    fail_t&& fail) {
    for (const auto& channel : chart.sound_channels) {
        const auto asset = song.find_audio_asset(channel.source_clip.source);
        if (asset == nullptr)
            fail(osu_mania_write_diagnostic_code_t::invalid_resource,
                 "sound channel refers to an unknown audio asset", "sound_channels");
        const auto resource = std::ranges::find_if(song.resources, [&](const auto& item) {
            return item.asset == asset->id;
        });
        if (resource == song.resources.end())
            fail(osu_mania_write_diagnostic_code_t::invalid_resource,
                 "sound channel asset has no resource entry", "sound_channels");
        for (const auto& event : channel.events) {
            if (event.playable) {
                if (!is_osu_sound_event_projected(chart, event, resource->id))
                    fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                         "playable sound-channel event has no equivalent sampled hit object",
                         "sound_channels");
                continue;
            }
            if (event.continuation || event.end)
                fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                     "osu! Sample events cannot represent sustained sound-channel events",
                     "sound_channels");
            append_osu_sample_event(output, serial, measures, song, event.position, resource->id,
                                    event.provenance.source_order, 1, 100, lazer_profile,
                                    timestamp_at_row, account_event, fail);
        }
    }
}

template <typename timestamp_t, typename account_t, typename fail_t>
void append_osu_bga_events(std::vector<osu_output_event_t>& output, std::size_t& serial,
                           const chart_document_t& chart, const measure_index_t& measures,
                           const song_document_t& song,
                           const retained_osu_event_fields_t& retained, bool lazer_profile,
                           timestamp_t&& timestamp_at_row, account_t&& account_event,
                           fail_t&& fail) {
    for (const auto& event : chart.bga_events) {
        const auto row = absolute_row(measures, event.position);
        if (!row)
            fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                 "visual event lies outside chart geometry", "Events");
        if (event.kind != visual_event_kind_t::background &&
            event.kind != visual_event_kind_t::video)
            fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                 "osu! event export supports background images and videos only", "Events");
        const auto name = osu_resource_path(song, event.resource, false);
        if (!name)
            fail(osu_mania_write_diagnostic_code_t::invalid_resource,
                 "visual event refers to an unsafe or missing visual asset", "Events");
        const auto time = osu_event_time(*row, "Events", lazer_profile, timestamp_at_row, fail);
        const auto line = (event.kind == visual_event_kind_t::video ? "Video," : "0,") +
                          time.text + "," + quote_csv(*name) + "," +
                          retained_event_offsets(retained, event.id.value);
        output.push_back({time.value, *row, event.provenance.source_order, serial++, line});
        account_event();
    }
}

template <typename timestamp_t, typename account_t, typename fail_t, typename warn_t>
auto serialize_osu_bookmarks(const chart_document_t& chart, const measure_index_t& measures,
                             timestamp_t&& timestamp_at_row, account_t&& account_event,
                             fail_t&& fail, warn_t&& warn) -> std::vector<std::int64_t> {
    std::vector<std::int64_t> times;
    times.reserve(chart.bookmarks.size());
    for (const auto& bookmark : chart.bookmarks) {
        std::int64_t milliseconds{};
        if (bookmark.anchor == marker_anchor_t::audio_time) {
            if (!bookmark.audio_milliseconds)
                fail(osu_mania_write_diagnostic_code_t::invalid_document,
                     "audio-time bookmark is missing its millisecond value", "Editor.Bookmarks");
            milliseconds = *bookmark.audio_milliseconds;
        } else {
            if (!bookmark.position)
                fail(osu_mania_write_diagnostic_code_t::invalid_document,
                     "beat bookmark is missing its chart position", "Editor.Bookmarks");
            const auto row = absolute_row(measures, *bookmark.position);
            if (!row)
                fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                     "bookmark lies outside chart geometry", "Editor.Bookmarks");
            const auto rounded = writer_round_ms(timestamp_at_row(*row));
            if (!rounded)
                fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                     "bookmark time is outside the integer millisecond range", "Editor.Bookmarks");
            milliseconds = *rounded;
        }
        if (!bookmark.label.empty())
            warn(osu_mania_write_diagnostic_code_t::lossy_transform,
                 "bookmark label was omitted because osu! stores bookmark times only",
                 "Editor.Bookmarks");
        account_event();
        times.push_back(milliseconds);
    }
    std::ranges::sort(times);
    return times;
}

} // namespace

auto osu_mania_write_result_t::has_errors() const -> bool {
    return diagnostic_queries::has_errors(diagnostics);
}

auto osu_mania_write_result_t::from(osu_mania_write_outcome_t&& outcome) -> osu_mania_write_result_t {
    return {.text = std::move(outcome.value), .diagnostics = std::move(outcome.diagnostics)};
}

auto osu_mania_write_result_t::from(const song_document_t& song, osu_mania_write_options_t options)
    -> osu_mania_write_outcome_t {
    osu_mania_write_outcome_t result{};
    const auto fail = [&](osu_mania_write_diagnostic_code_t code, std::string message,
                          std::string field = {}) -> void {
        result.diagnostics.push_back({.severity = diagnostic_severity_t::error,
                                      .code = code,
                                      .message = std::move(message),
                                      .field = std::move(field)});
        throw osu_write_abort_t{};
    };
    const auto warn = [&](osu_mania_write_diagnostic_code_t code, std::string message,
                          std::string field = {}) {
        result.diagnostics.push_back({.severity = diagnostic_severity_t::warning,
                                      .code = code,
                                      .message = std::move(message),
                                      .field = std::move(field)});
    };
    try {
        const auto* chart = select_osu_chart(song, options, fail);
        const auto output_profile = select_osu_profile(song, *chart, options, fail);
        const bool lazer_profile = output_profile == osu_mania_file_profile_t::lazer_v128;

        auto baked_chart = validate_and_bake_osu_chart(song, *chart, fail);
        chart = &*baked_chart.chart;
        auto storyboard_output = prepare_osu_storyboard(song, options, fail);
        validate_osu_chart_features(*chart, options, fail, warn);
        auto geometry = build_osu_export_geometry(*chart, options, fail);
        const auto& measures = geometry.measures;
        const auto& ordered_lane_ids = geometry.ordered_lane_ids;
        const auto keys = ordered_lane_ids.size();
        const auto lane_column = [&](lane_id_t lane) -> std::optional<std::size_t> {
            const auto found = std::ranges::find(ordered_lane_ids, lane);
            if (found == ordered_lane_ids.end())
                return std::nullopt;
            return static_cast<std::size_t>(std::distance(ordered_lane_ids.begin(), found));
        };

        const auto bpm_timing = build_osu_bpm_timing(*chart, measures, fail);
        const auto& bpms = bpm_timing.events;
        const auto relative_milliseconds_at = [&](std::uint64_t row) {
            return osu_relative_milliseconds(bpm_timing, row,
                                             chart->divisions_per_standard_measure, fail);
        };

        const auto retained_note_times = retained_osu_note_times(*chart);
        auto offset_seconds = rational_value(chart->beat_zero_audio_offset_seconds);
        if (!offset_seconds)
            fail(osu_mania_write_diagnostic_code_t::invalid_document,
                 "beat-zero audio offset is invalid", "beat_zero_audio_offset_seconds");
        auto offset_ms = *offset_seconds * 1000.0L;
        if (const auto inferred = infer_osu_offset_ms(*chart, measures, bpms, retained_note_times,
                                                      relative_milliseconds_at))
            offset_ms = *inferred;
        const auto timestamp_at_row = [&](std::uint64_t row) {
            return offset_ms + relative_milliseconds_at(row);
        };

        const auto scrolls = collect_osu_scroll_events(*chart, measures, fail);
        const auto kiais = collect_osu_kiai_events(*chart, measures, fail);
        std::size_t event_count{};
        const auto account_event = [&]() {
            if (event_count >= options.max_events)
                fail(osu_mania_write_diagnostic_code_t::output_limit,
                     "osu! event count exceeds the configured export bound", "events");
            ++event_count;
        };
        std::size_t event_serial{};
        auto timing_output = build_osu_timing_output(
            *chart, measures, bpm_timing, scrolls, kiais, timestamp_at_row, account_event,
            event_serial, fail);

        std::vector<osu_output_event_t> hit_output;
        bool rounded_hit_time{};
        std::size_t note_serial{};
        for (std::size_t measure_index = 0; measure_index < chart->measures.size();
             ++measure_index) {
            const auto& measure = chart->measures[measure_index];
            for (const auto& note : measure.notes) {
                const auto lane = lane_column(note.lane);
                if (!lane)
                    fail(osu_mania_write_diagnostic_code_t::invalid_layout,
                         "note refers to a lane outside the osu!mania layout", "HitObjects.lane");
                const auto begin_row = absolute_row(measures, note.position);
                if (!begin_row)
                    fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                         "note head lies outside chart geometry", "HitObjects.time");
                hit_output.push_back(serialize_osu_note(
                    note, *begin_row, *lane, keys, song, measures, retained_note_times,
                    lazer_profile, rounded_hit_time, timestamp_at_row, note_serial, account_event,
                    fail, warn));
                ++note_serial;
            }
        }
        std::ranges::stable_sort(hit_output, [](const auto& left, const auto& right) {
            return std::tuple{left.milliseconds, left.row, left.order, left.serial} <
                   std::tuple{right.milliseconds, right.row, right.order, right.serial};
        });
        if (rounded_hit_time && !lazer_profile)
            warn(osu_mania_write_diagnostic_code_t::timestamp_quantization,
                 "fractional hit-object milliseconds were rounded to nearest integer; exact "
                 "half-millisecond ties round away from zero",
                 "HitObjects.time");

        std::vector<osu_output_event_t> event_output;
        const auto retained_events = retained_osu_event_fields(*chart);
        append_osu_bgm_events(event_output, event_serial, *chart, measures, song, retained_events,
                              lazer_profile, timestamp_at_row, account_event, fail, warn);
        append_osu_sound_channels(event_output, event_serial, *chart, measures, song,
                                  lazer_profile, timestamp_at_row, account_event, fail);
        append_osu_bga_events(event_output, event_serial, *chart, measures, song,
                              retained_events, lazer_profile, timestamp_at_row, account_event,
                              fail);
        auto bookmark_times = serialize_osu_bookmarks(*chart, measures, timestamp_at_row,
                                                      account_event, fail, warn);

        auto music_files = collect_osu_music_files(song, fail);
        auto tags = collect_osu_tags(song, *chart, fail);
        auto extra_fields = options.preserve_unknown_fields
                                ? passthrough_fields(*chart, song, result.diagnostics)
                                : std::vector<passthrough_field_t>{};
        project_osu_specialstyle_field(*chart, extra_fields);
        if (!result.ok())
            throw osu_write_abort_t{};
        std::size_t metadata_field_count{};
        std::size_t metadata_bytes{};
        const auto account_metadata = [&](std::string_view line) {
            if (metadata_field_count >= options.max_metadata_fields ||
                line.size() > options.max_metadata_bytes ||
                metadata_bytes > options.max_metadata_bytes - line.size()) {
                fail(osu_mania_write_diagnostic_code_t::metadata_limit,
                     "osu! metadata exceeds the configured field-count or byte limit", "metadata");
            }
            ++metadata_field_count;
            metadata_bytes += line.size();
        };
        account_osu_metadata(music_files, bookmark_times, song, *chart, tags, keys, extra_fields,
                             account_metadata);
        result.value = assemble_osu_output(song, *chart, options, output_profile, music_files,
                                          bookmark_times, tags, keys, extra_fields, event_output,
                                          timing_output, hit_output, storyboard_output,
                                          account_event, fail);
    } catch (const osu_write_abort_t&) {
        result.value.reset();
    } catch (const std::exception& error) {
        result.value.reset();
        result.diagnostics.push_back(
            {.severity = diagnostic_severity_t::error,
             .code = osu_mania_write_diagnostic_code_t::invalid_document,
             .message = std::string("osu! export failed atomically: ") + error.what(),
             .field = "document"});
    } catch (...) {
        result.value.reset();
        result.diagnostics.push_back({.severity = diagnostic_severity_t::error,
                                      .code = osu_mania_write_diagnostic_code_t::invalid_document,
                                      .message = "osu! export failed atomically",
                                      .field = "document"});
    }
    return result;
}

auto parse_osu_mania_bytes(std::span<const std::byte> bytes, osu_mania_parse_options_t options) -> osu_mania_parse_result_t {
    return osu_mania_parse_result_t::from(osu_mania_parse_result_t::from(bytes, std::move(options)));
}

auto write_osu_mania(const song_document_t& song, osu_mania_write_options_t options) -> osu_mania_write_result_t {
    return osu_mania_write_result_t::from(osu_mania_write_result_t::from(song, std::move(options)));
}

} // namespace wormedit

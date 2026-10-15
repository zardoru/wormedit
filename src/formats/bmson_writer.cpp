#include <wormedit/core/rational.h>
#include "../core/checked_math.h"
#include "../core/text_util.h"
#include <wormedit/formats/bmson.h>

#include <wormedit/core/sv_recipe.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wormedit {
namespace {

using json_t = nlohmann::ordered_json;

struct write_failure_t {
    bmson_write_diagnostic_code_t code;
    std::string message;
    std::string path;
};

[[noreturn]] auto fail(bmson_write_diagnostic_code_t code, std::string message,
                       std::string path) -> void {
    throw write_failure_t{code, std::move(message), std::move(path)};
}

using checked_math::checked_multiply;

using checked_math::checked_add;

auto absolute_position(const measure_index_t& index, local_position_t position,
                       bool allow_measure_end = false) -> std::optional<std::uint64_t> {
    const auto entry = std::ranges::find(index.entries, position.measure,
                                         &measure_index_entry_t::id);
    if (entry == index.entries.end() ||
        (allow_measure_end ? position.row > entry->row_capacity
                           : position.row >= entry->row_capacity)) {
        return std::nullopt;
    }
    std::uint64_t result{};
    if (!checked_add(entry->absolute_begin_row, position.row, result)) return std::nullopt;
    return result;
}

auto to_pulse(const bmson_grid_t& grid, std::uint64_t row,
              std::string_view path) -> std::uint32_t {
    const auto pulse = bmson_pulse_for_row(grid, row);
    if (!pulse || *pulse > UINT32_MAX) {
        fail(bmson_write_diagnostic_code_t::invalid_geometry,
             "position cannot be represented as an unsigned 32-bit BMSON pulse",
             std::string(path));
    }
    return static_cast<std::uint32_t>(*pulse);
}

auto path_utf8(const std::filesystem::path& path) -> std::string {
    return text_util::path_utf8(path);
}

auto is_safe_relative_path(std::string_view value) -> bool {
    if (value.empty() || value.find('\0') != std::string_view::npos) {
        return false;
    }
    std::string normalized(value);
    std::ranges::replace(normalized, '\\', '/');
    if (normalized.front() == '/' ||
        (normalized.size() >= 2 &&
         ((normalized[0] >= 'A' && normalized[0] <= 'Z') ||
          (normalized[0] >= 'a' && normalized[0] <= 'z')) &&
         normalized[1] == ':')) {
        return false;
    }
    std::size_t begin{};
    while (begin <= normalized.size()) {
        const auto end = normalized.find('/', begin);
        const auto component = normalized.substr(begin, end == std::string::npos
                                                            ? std::string::npos
                                                            : end - begin);
        if (component == "..") return false;
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return true;
}

auto normalized_resource_name(std::string_view value) -> std::string {
    std::string normalized(value);
    std::ranges::replace(normalized, '\\', '/');
    std::vector<std::string_view> components;
    std::size_t begin{};
    while (begin <= normalized.size()) {
        const auto end = normalized.find('/', begin);
        const auto component = std::string_view(normalized).substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!component.empty() && component != ".") components.push_back(component);
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    std::string result;
    for (const auto component : components) {
        if (!result.empty()) result.push_back('/');
        result.append(component);
    }
    return result;
}

auto retained_resource_name(std::string_view candidate, std::string_view asset_name)
    -> std::string {
    if (is_safe_relative_path(candidate) &&
        normalized_resource_name(candidate) == normalized_resource_name(asset_name)) {
        return std::string(candidate);
    }
    return std::string(asset_name);
}

auto asset_path(const song_document_t& song, asset_id_t id, std::string path)
    -> std::string {
    const auto asset = song.find_audio_asset(id);
    if (asset == nullptr) {
        fail(bmson_write_diagnostic_code_t::unresolved_resource,
             "sound channel refers to an unknown audio asset", std::move(path));
    }
    const auto name = path_utf8(asset->source_path);
    if (!is_safe_relative_path(name)) {
        fail(bmson_write_diagnostic_code_t::unresolved_resource,
             "BMSON audio paths must be safe relative paths", std::move(path));
    }
    return name;
}

auto visual_asset_path(const song_document_t& song, asset_id_t id, std::string path)
    -> std::string {
    const auto asset = std::ranges::find(song.visual_assets, id, &visual_asset_t::id);
    if (asset == song.visual_assets.end()) {
        fail(bmson_write_diagnostic_code_t::unresolved_resource,
             "visual resource refers to an unknown asset", std::move(path));
    }
    const auto name = path_utf8(asset->source_path);
    if (!is_safe_relative_path(name)) {
        fail(bmson_write_diagnostic_code_t::unresolved_resource,
             "BMSON visual paths must be safe relative paths", std::move(path));
    }
    return name;
}

auto reduced(std::uint64_t numerator, std::uint64_t denominator)
    -> std::pair<std::uint64_t, std::uint64_t> {
    if (denominator == 0) return {numerator, denominator};
    const auto value = reduce_fraction({numerator, denominator});
    return {value.numerator, value.denominator};
}

auto decimal_number(signed_rational_t value, std::string path) -> std::string {
    if (value.numerator <= 0 || value.denominator == 0) {
        fail(bmson_write_diagnostic_code_t::unrepresentable_number,
             "BMSON BPM values must be positive exact decimals", std::move(path));
    }
    auto [numerator, denominator] = reduced(static_cast<std::uint64_t>(value.numerator),
                                             value.denominator);
    unsigned twos{};
    unsigned fives{};
    while (denominator % 2U == 0U) {
        denominator /= 2U;
        ++twos;
    }
    while (denominator % 5U == 0U) {
        denominator /= 5U;
        ++fives;
    }
    if (denominator != 1U) {
        fail(bmson_write_diagnostic_code_t::unrepresentable_number,
             "BMSON JSON decimal cannot exactly represent this rational BPM", std::move(path));
    }
    const auto scale = std::max(twos, fives);
    std::uint64_t factor = 1;
    for (unsigned index = twos; index < scale; ++index) {
        if (!checked_multiply(factor, 2, factor)) {
            fail(bmson_write_diagnostic_code_t::unrepresentable_number,
                 "exact BPM decimal exceeds the supported numeric range", std::move(path));
        }
    }
    for (unsigned index = fives; index < scale; ++index) {
        if (!checked_multiply(factor, 5, factor)) {
            fail(bmson_write_diagnostic_code_t::unrepresentable_number,
                 "exact BPM decimal exceeds the supported numeric range", std::move(path));
        }
    }
    std::uint64_t scaled{};
    if (!checked_multiply(numerator, factor, scaled)) {
        fail(bmson_write_diagnostic_code_t::unrepresentable_number,
             "exact BPM decimal exceeds the supported numeric range", std::move(path));
    }
    std::uint64_t decimal_scale = 1;
    for (unsigned index = 0; index < scale; ++index) {
        if (!checked_multiply(decimal_scale, 10, decimal_scale)) {
            fail(bmson_write_diagnostic_code_t::unrepresentable_number,
                 "exact BPM decimal exceeds the supported numeric range", std::move(path));
        }
    }
    const auto whole = scaled / decimal_scale;
    if (scale == 0) return std::to_string(whole);
    auto fraction = std::to_string(scaled % decimal_scale);
    if (fraction.size() < scale) fraction.insert(0, scale - fraction.size(), '0');
    while (!fraction.empty() && fraction.back() == '0') fraction.pop_back();
    if (fraction.empty()) return std::to_string(whole);
    return std::to_string(whole) + "." + fraction;
}

auto exact_nonnegative_scaled(signed_rational_t value, std::uint64_t scale,
                              std::string path) -> std::uint64_t {
    if (value.denominator == 0 || value.numerator < 0) {
        fail(bmson_write_diagnostic_code_t::unrepresentable_number,
             "BMSON duration must be a non-negative rational", std::move(path));
    }
    std::uint64_t product{};
    if (!checked_multiply(static_cast<std::uint64_t>(value.numerator), scale, product) ||
        product % value.denominator != 0) {
        fail(bmson_write_diagnostic_code_t::unrepresentable_number,
             "duration cannot be represented as an integer BMSON pulse count", std::move(path));
    }
    return product / value.denominator;
}

auto source_object(const source_provenance_t& provenance) -> json_t {
    if (provenance.source_spelling.empty()) return json_t::object();
    try {
        auto value = json_t::parse(provenance.source_spelling);
        if (value.is_object()) return value;
    } catch (const nlohmann::json::exception&) {
    }
    return json_t::object();
}

auto original_json(const song_document_t& song, const chart_document_t& chart,
                   bool enabled) -> json_t {
    if (!enabled) return json_t::object();
    const auto find_bytes = [](const std::vector<format_metadata_t>& groups)
        -> std::shared_ptr<const std::vector<std::byte>> {
        const auto group = std::ranges::find_if(groups, [](const auto& candidate) {
            return candidate.family == format_family_t::bmson &&
                   candidate.encoding.original_bytes != nullptr;
        });
        return group == groups.end() ? nullptr : group->encoding.original_bytes;
    };
    auto bytes = find_bytes(chart.metadata.format_groups);
    if (!bytes) bytes = find_bytes(song.metadata.format_groups);
    if (!bytes) return json_t::object();
    const auto* chars = reinterpret_cast<const char*>(bytes->data());
    try {
        auto value = json_t::parse(std::string_view(chars, bytes->size()));
        return value.is_object() ? value : json_t::object();
    } catch (const nlohmann::json::exception&) {
        fail(bmson_write_diagnostic_code_t::invalid_document,
             "retained BMSON source bytes are no longer valid JSON", "$source");
    }
}

auto bmson_metadata_group(const std::vector<format_metadata_t>& groups)
    -> const format_metadata_t* {
    const auto found = std::ranges::find(groups, format_family_t::bmson,
                                         &format_metadata_t::family);
    return found == groups.end() ? nullptr : &*found;
}

auto bmson_info_metadata_value(const format_metadata_t* group, const std::string& name)
    -> std::optional<json_t> {
    if (!group) return std::nullopt;
    const auto matches = std::ranges::count(group->ordered_fields, name,
                                            &std::pair<std::string, std::string>::first);
    if (matches > 1)
        fail(bmson_write_diagnostic_code_t::invalid_document,
             "BMSON info field has duplicate metadata entries", "$." + name);
    const auto found = std::ranges::find(group->ordered_fields, name,
                                         &std::pair<std::string, std::string>::first);
    if (found == group->ordered_fields.end()) return std::nullopt;
    auto value = json_t::parse(found->second, nullptr, false);
    if (value.is_discarded())
        fail(bmson_write_diagnostic_code_t::invalid_document,
             "BMSON info metadata is not a valid JSON value", "$." + name);
    return value;
}

auto is_metadata_value_changed(const format_metadata_t* group,
                            const std::optional<json_t>& value,
                            const std::optional<json_t>& source) -> bool {
    return group && value != source;
}

auto is_valid_bmson_info_value(std::string_view key, const json_t& value) -> bool {
    const auto string_field = key == "subtitle" || key == "genre";
    const auto string_array = key == "subartists";
    const auto number_field = key == "judge_rank" || key == "total";
    return (!string_field || value.is_string()) &&
           (!string_array || (value.is_array() && std::ranges::all_of(value, [](const auto& item) {
               return item.is_string();
           }))) &&
           (!number_field || (value.is_number() && value.get<double>() >= 0.0));
}

auto select_bmson_info_value(const std::string& name, const format_metadata_t* song_group,
                             const format_metadata_t* chart_group, const json_t& source_info)
    -> std::optional<json_t> {
    const auto song_value = bmson_info_metadata_value(song_group, name);
    const auto chart_value = bmson_info_metadata_value(chart_group, name);
    const auto key = std::string_view(name).substr(std::string_view("info.").size());
    const auto source_value = source_info.contains(key)
                                  ? std::optional<json_t>{source_info[key]} : std::nullopt;
    const auto song_changed = is_metadata_value_changed(song_group, song_value, source_value);
    const auto chart_changed = is_metadata_value_changed(chart_group, chart_value, source_value);
    if (song_changed && chart_changed && song_value != chart_value)
        fail(bmson_write_diagnostic_code_t::invalid_document,
             "song and chart BMSON metadata disagree on the same edited info field", "$." + name);
    if (chart_changed || !song_group) return chart_value;
    if (song_changed || !chart_group) return song_value;
    return chart_value;
}

void apply_bmson_info_field(const std::string& key, const format_metadata_t* song_group,
                            const format_metadata_t* chart_group, const json_t& source_info,
                            json_t& info) {
    const auto name = std::string("info.") + key;
    const auto selected = select_bmson_info_value(name, song_group, chart_group, source_info);
    if (!selected) {
        info.erase(key);
        return;
    }
    if (!is_valid_bmson_info_value(key, *selected))
        fail(bmson_write_diagnostic_code_t::invalid_document,
             "BMSON info metadata must contain a valid JSON value of the expected type", "$." + name);
    info[key] = *selected;
}

auto apply_bmson_info_metadata(const song_document_t& song, const chart_document_t& chart,
                               json_t& info) -> bool {
    const auto* song_group = bmson_metadata_group(song.metadata.format_groups);
    const auto* chart_group = bmson_metadata_group(chart.metadata.format_groups);
    if (!song_group && !chart_group) return false;
    const auto source = original_json(song, chart, true);
    const auto source_info = source.contains("info") && source["info"].is_object()
                                 ? source["info"] : json_t::object();
    const auto genre_name = std::string("info.genre");
    const auto source_genre = source_info.contains("genre")
                                  ? std::optional<json_t>{source_info["genre"]} : std::nullopt;
    const auto genre_edited =
        is_metadata_value_changed(song_group, bmson_info_metadata_value(song_group, genre_name),
                               source_genre) ||
        is_metadata_value_changed(chart_group, bmson_info_metadata_value(chart_group, genre_name),
                               source_genre);
    for (const auto key : {"subtitle", "subartists", "genre", "judge_rank", "total"})
        apply_bmson_info_field(key, song_group, chart_group, source_info, info);
    return genre_edited;
}

auto optional_u32_identity(const resource_entry_t& resource, std::string_view space)
    -> std::optional<std::uint32_t> {
    const auto identity = std::ranges::find_if(resource.target_identities,
        [&](const auto& item) {
            return item.family == format_family_t::bmson && item.namespace_name == space;
        });
    if (identity == resource.target_identities.end()) return std::nullopt;
    std::uint32_t value{};
    const auto parsed = std::from_chars(identity->identifier.data(),
                                        identity->identifier.data() + identity->identifier.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != identity->identifier.data() + identity->identifier.size()) {
        return std::nullopt;
    }
    return value;
}

auto resource_for_id(const song_document_t& song, resource_id_t id,
                     std::string_view path) -> const resource_entry_t& {
    const auto resource = std::ranges::find(song.resources, id, &resource_entry_t::id);
    if (resource == song.resources.end()) {
        fail(bmson_write_diagnostic_code_t::unresolved_resource,
             "event refers to an unknown resource", std::string(path));
    }
    return *resource;
}

auto fixed_x_positions(std::string_view mode) -> std::vector<std::uint32_t> {
    std::vector<std::uint32_t> result;
    if (mode == "beat-5k") {
        result = {1, 2, 3, 4, 5, 8};
    } else if (mode == "beat-7k") {
        result = {1, 2, 3, 4, 5, 6, 7, 8};
    } else if (mode == "beat-10k") {
        result = {1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 16};
    } else if (mode == "beat-14k") {
        for (std::uint32_t x = 1; x <= 8; ++x) result.push_back(x);
        for (std::uint32_t x = 9; x <= 16; ++x) result.push_back(x);
    } else if (mode == "popn-5k") {
        for (std::uint32_t x = 1; x <= 5; ++x) result.push_back(x);
    } else if (mode == "popn-9k") {
        for (std::uint32_t x = 1; x <= 9; ++x) result.push_back(x);
    } else if (mode == "generic-24keys") {
        for (std::uint32_t x = 1; x <= 24; ++x) result.push_back(x);
    } else if (mode == "generic-48keys") {
        for (std::uint32_t x = 1; x <= 48; ++x) result.push_back(x);
    }
    return result;
}

struct lane_map_t {
    [[nodiscard]] static auto from(const chart_document_t& chart) -> lane_map_t;

    std::string mode{"generic-nkeys"};
    std::unordered_map<std::uint64_t, std::uint32_t> x_by_lane;
};

auto bmson_mode_hint(std::string_view profile) -> std::optional<std::string> {
    constexpr std::string_view prefix{"bmson-"};
    if (!profile.starts_with(prefix)) return std::nullopt;
    const auto candidate = std::string(profile.substr(prefix.size()));
    if (candidate == "beat-5k" || candidate == "beat-7k" || candidate == "beat-10k" ||
        candidate == "beat-14k" || candidate == "popn-5k" || candidate == "popn-9k" ||
        candidate == "generic-nkeys" || candidate == "generic-24keys" ||
        candidate == "generic-48keys") return candidate;
    return std::nullopt;
}

auto is_lane_layout_current(const lane_layout_t& initial, const lane_layout_t& other) -> bool {
    if (other.profile_id != initial.profile_id || other.lanes.size() != initial.lanes.size() ||
        other.split_stage != initial.split_stage ||
        other.split_after_column != initial.split_after_column ||
        other.bmson_generic_keys_only != initial.bmson_generic_keys_only) return false;
    for (std::size_t lane = 0; lane < initial.lanes.size(); ++lane) {
        if (other.lanes[lane].id != initial.lanes[lane].id ||
            other.lanes[lane].role != initial.lanes[lane].role ||
            other.lanes[lane].stage != initial.lanes[lane].stage ||
            other.lanes[lane].display_order != initial.lanes[lane].display_order) return false;
    }
    return true;
}

void validate_chart_lane_layouts(const chart_document_t& chart,
                                 const lane_layout_t& initial) {
    for (std::size_t segment = 1; segment < chart.lane_layouts.size(); ++segment) {
        if (!is_lane_layout_current(initial, chart.lane_layouts[segment].layout))
            fail(bmson_write_diagnostic_code_t::unsupported_feature,
                 "BMSON has one chart-wide key layout; changing layouts are not representable",
                 "$.info.mode_hint");
    }
}

auto lane_x_position(const lane_definition_t& lane, std::size_t index,
                     const std::vector<std::uint32_t>& positions,
                     std::string_view mode) -> std::uint32_t {
    if (lane.id.value >= 100001 && lane.id.value <= 100000ULL + UINT32_MAX)
        return static_cast<std::uint32_t>(lane.id.value - 100000);
    if (mode == "generic-nkeys") {
        if (lane.display_order == UINT32_MAX)
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "generic BMSON key index exceeds the supported range", "$.info.mode_hint");
        return lane.display_order + 1;
    }
    if (index < positions.size()) return positions[index];
    fail(bmson_write_diagnostic_code_t::unsupported_feature,
         "lane count does not fit the selected BMSON mode_hint", "$.info.mode_hint");
}

void map_defined_lanes(const lane_layout_t& initial, lane_map_t& result) {
    auto lanes = initial.lanes;
    std::ranges::stable_sort(lanes, {}, &lane_definition_t::display_order);
    const auto positions = fixed_x_positions(result.mode);
    for (std::size_t index = 0; index < lanes.size(); ++index) {
        const auto x = lane_x_position(lanes[index], index, positions, result.mode);
        if (x == 0 || !result.x_by_lane.emplace(lanes[index].id.value, x).second)
            fail(bmson_write_diagnostic_code_t::invalid_document,
                 "lane layout cannot be mapped to unique BMSON x positions", "$.info.mode_hint");
    }
}

void map_implicit_lanes(const chart_document_t& chart, lane_map_t& result) {
    std::set<std::uint64_t> lane_ids;
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes) lane_ids.insert(note.lane.value);
    std::uint32_t x = 1;
    for (const auto id : lane_ids) result.x_by_lane.emplace(id, x++);
}

auto lane_map_t::from(const chart_document_t& chart) -> lane_map_t {
    lane_map_t result;
    if (!chart.lane_layouts.empty()) {
        const auto& initial = chart.lane_layouts.front().layout;
        if (const auto mode = bmson_mode_hint(initial.profile_id)) result.mode = *mode;
        validate_chart_lane_layouts(chart, initial);
        map_defined_lanes(initial, result);
    } else {
        map_implicit_lanes(chart, result);
    }
    return result;
}

[[maybe_unused]] auto make_lane_map(const chart_document_t& chart) -> lane_map_t {
    return lane_map_t::from(chart);
}

struct emitted_note_t {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t length{};
    bool continuation{};
    source_provenance_t provenance;
};

struct emitted_channel_t {
    asset_id_t asset;
    std::string name;
    const sound_channel_t* source{};
    std::optional<std::size_t> retained_source_index;
    std::vector<emitted_note_t> notes;
};

auto sound_channel_source_index(const sound_channel_t& channel)
    -> std::optional<std::size_t> {
    constexpr std::string_view prefix = "$.sound_channels[";
    std::optional<std::size_t> result;
    for (const auto& event : channel.events) {
        const auto& field = event.provenance.source_field;
        if (!field.starts_with(prefix)) continue;
        const auto close = field.find(']', prefix.size());
        if (close == std::string::npos) continue;
        std::size_t index{};
        const auto* begin = field.data() + prefix.size();
        const auto* end = field.data() + close;
        const auto parsed = std::from_chars(begin, end, index);
        if (parsed.ec != std::errc{} || parsed.ptr != end) continue;
        if (result && *result != index) {
            fail(bmson_write_diagnostic_code_t::invalid_document,
                 "one sound channel contains events retained from multiple BMSON source channels",
                 "$.sound_channels");
        }
        result = index;
    }
    return result;
}

struct channel_builder_t {
    struct retained_projection_t {
        std::size_t channel_index{};
        const sound_channel_event_t* event{};
    };

    const song_document_t& song;
    const chart_document_t& chart;
    const measure_index_t& index;
    const bmson_grid_t& grid;
    const lane_map_t& lanes;
    std::vector<bmson_write_diagnostic_t>& diagnostics;
    std::vector<emitted_channel_t> channels;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::size_t> authored_indexes;
    std::unordered_map<std::uint64_t, retained_projection_t> retained_projections;
    bool projection_warning_emitted{};

    channel_builder_t(const song_document_t& song_value, const chart_document_t& chart_value,
                      const measure_index_t& index_value, const bmson_grid_t& grid_value,
                      const lane_map_t& lane_map,
                      std::vector<bmson_write_diagnostic_t>& output_diagnostics)
        : song(song_value), chart(chart_value), index(index_value), grid(grid_value),
          lanes(lane_map), diagnostics(output_diagnostics) {}

    void warn_empty_stem(const sound_channel_t& channel) {
        if (!channel.events.empty()) return;
        const auto lane = std::ranges::find(chart.bgm_source_lanes,
            std::optional{channel.id}, &bgm_source_lane_t::stem_channel);
        if (lane == chart.bgm_source_lanes.end()) return;
        diagnostics.push_back({.severity = diagnostic_severity_t::warning,
            .code = bmson_write_diagnostic_code_t::lossy_transform,
            .message = "stem " + channel.name + " has no points",
            .json_path = "$.sound_channels"});
    }

    void load_retained_channels() {
        channels.reserve(chart.sound_channels.size());
        for (const auto& source : chart.sound_channels) {
            warn_empty_stem(source);
            if (source.source_clip.begin_frame != 0 || source.source_clip.end_frame)
                fail(bmson_write_diagnostic_code_t::unsupported_feature,
                     "BMSON 1.0 cannot encode a cropped source audio clip", "$.sound_channels");
            const auto path = asset_path(song, source.source_clip.source, "$.sound_channels");
            channels.push_back({source.source_clip.source, retained_resource_name(source.name, path),
                                &source, sound_channel_source_index(source), {}});
        }
    }

    auto channel_index_for_resource(const resource_entry_t& resource)
        -> std::optional<std::size_t> {
        const auto identity = optional_u32_identity(resource, "sound_channel");
        if (!identity) return std::nullopt;
        const auto found = std::ranges::find_if(channels, [&](const auto& channel) {
            return channel.retained_source_index == static_cast<std::size_t>(*identity);
        });
        if (found == channels.end()) return std::nullopt;
        if (found->asset != resource.asset)
            fail(bmson_write_diagnostic_code_t::invalid_document,
                 "sound-channel resource identity points to a channel with a different audio asset",
                 "$.sound_channels");
        return static_cast<std::size_t>(std::distance(channels.begin(), found));
    }

    auto channel_for(const resource_entry_t& resource, std::string path,
                        std::optional<object_id_t> bgm_lane = {}) -> std::size_t {
        if (const auto found = channel_index_for_resource(resource)) return *found;
        const auto name = asset_path(song, resource.asset, std::move(path));
        const auto key = std::pair{resource.id.value, bgm_lane ? bgm_lane->value : std::uint64_t{0}};
        if (const auto found = authored_indexes.find(key); found != authored_indexes.end())
            return found->second;
        channels.push_back({resource.asset, name, nullptr, std::nullopt, {}});
        authored_indexes.emplace(key, channels.size() - 1);
        return channels.size() - 1;
    }

    auto pulse64(local_position_t position, const std::string& path, bool end = false)
        -> std::uint64_t {
        const auto row = absolute_position(index, position, end);
        if (!row)
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "event position is outside its chart measure", path);
        const auto value = bmson_pulse_for_row(grid, *row);
        if (!value)
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "event position cannot be represented as a BMSON pulse", path);
        return *value;
    }

    auto pulse(local_position_t position, const std::string& path) -> std::uint32_t {
        const auto value = pulse64(position, path);
        if (value > UINT32_MAX)
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "event position exceeds the BMSON 32-bit pulse range", path);
        return static_cast<std::uint32_t>(value);
    }

    auto pulse_length(local_position_t begin, local_position_t end, const std::string& path)
        -> std::uint32_t {
        const auto begin_pulse = pulse64(begin, path);
        const auto end_pulse = pulse64(end, path, true);
        if (end_pulse <= begin_pulse || end_pulse - begin_pulse > UINT32_MAX)
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "hold length must be positive and fit the BMSON 32-bit pulse range", path);
        return static_cast<std::uint32_t>(end_pulse - begin_pulse);
    }

    void index_retained_projections() {
        for (std::size_t channel_index = 0; channel_index < chart.sound_channels.size();
             ++channel_index)
            for (const auto& event : chart.sound_channels[channel_index].events)
                retained_projections.emplace(event.id.value,
                    retained_projection_t{channel_index, &event});
    }

    auto find_source_projection(const emitted_channel_t& channel,
                                std::optional<object_id_t> linked_event,
                                const source_provenance_t& provenance, bool playable,
                                local_position_t position, std::optional<local_position_t> end,
                                lane_id_t lane) -> const sound_channel_event_t* {
        if (linked_event) {
            const auto found = retained_projections.find(linked_event->value);
            if (found == retained_projections.end())
                fail(bmson_write_diagnostic_code_t::invalid_document,
                     "semantic sound link refers to an unknown retained source event",
                     "$.sound_channels");
            if (found->second.channel_index != static_cast<std::size_t>(&channel - channels.data()))
                fail(bmson_write_diagnostic_code_t::invalid_document,
                     "semantic sound resource does not match its retained source channel",
                     "$.sound_channels");
            return found->second.event;
        }
        if (!channel.source) return nullptr;
        if (!provenance.source_field.empty()) {
            const auto found = std::ranges::find_if(channel.source->events, [&](const auto& item) {
                return item.playable == playable &&
                       item.provenance.source_field == provenance.source_field &&
                       (provenance.source_order == 0 ||
                        item.provenance.source_order == provenance.source_order);
            });
            return found == channel.source->events.end() ? nullptr : &*found;
        }
        const auto found = std::ranges::find_if(channel.source->events, [&](const auto& item) {
            return item.playable == playable && item.position == position && item.end == end &&
                   (!playable || item.lane == lane);
        });
        return found == channel.source->events.end() ? nullptr : &*found;
    }

    auto reconcile_projection(const emitted_channel_t& channel,
                              std::optional<object_id_t> linked_event,
                              const source_provenance_t& provenance, bool playable,
                              local_position_t position, std::optional<local_position_t> end,
                              lane_id_t lane) -> bool {
        const auto* source = find_source_projection(channel, linked_event, provenance, playable,
                                                    position, end, lane);
        if (!source) return false;
        const auto agrees = source->position == position && source->end == end &&
                            (!playable || source->lane == lane);
        if (!agrees && !projection_warning_emitted) {
            diagnostics.push_back({.severity = diagnostic_severity_t::warning,
                .code = bmson_write_diagnostic_code_t::lossy_transform,
                .message = "BMSON source-channel events replaced stale semantic note/BGM projections",
                .json_path = "$.sound_channels"});
            projection_warning_emitted = true;
        }
        return true;
    }

    auto lane_x(lane_id_t lane) const -> std::uint32_t {
        const auto found = lanes.x_by_lane.find(lane.value);
        if (found == lanes.x_by_lane.end())
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "playable lane is absent from the selected BMSON layout",
                 "$.sound_channels[].notes[].x");
        return found->second;
    }

    void append_retained_events() {
        for (std::size_t channel_index = 0; channel_index < chart.sound_channels.size();
             ++channel_index) {
            const auto& channel = chart.sound_channels[channel_index];
            for (const auto& event : channel.events) {
                std::uint32_t x{};
                if (event.playable) x = lane_x(event.lane);
                else if (event.lane.value != 0)
                    fail(bmson_write_diagnostic_code_t::invalid_document,
                         "non-playable sound-channel events must use lane zero",
                         "$.sound_channels[].notes[].x");
                channels[channel_index].notes.push_back({
                    .x=x, .y=pulse(event.position, "$.sound_channels[].notes[].y"),
                    .length=event.end ? pulse_length(event.position, *event.end,
                        "$.sound_channels[].notes[].l") : 0U,
                    .continuation=event.continuation, .provenance=event.provenance});
            }
        }
    }

    void append_semantic_note(const note_t& note) {
        if (note.kind != note_kind_t::tap && note.kind != note_kind_t::hold)
            fail(bmson_write_diagnostic_code_t::unsupported_feature,
                 "BMSON 1.0 supports taps and holds only", "$.sound_channels");
        if (!note.sound)
            fail(bmson_write_diagnostic_code_t::unresolved_resource,
                 "BMSON playable notes require a source audio resource", "$.sound_channels");
        if (note.label || note.branch)
            fail(bmson_write_diagnostic_code_t::unsupported_feature,
                 "BMSON 1.0 cannot encode note labels or branches", "$.sound_channels");
        const auto& resource = resource_for_id(song, *note.sound, "$.sound_channels");
        const auto target = channel_for(resource, "$.sound_channels");
        if (reconcile_projection(channels[target], note.sound_channel_event, note.provenance, true,
                                 note.position, note.end, note.lane)) return;
        const auto y = pulse(note.position, "$.sound_channels[].notes[].y");
        const auto length = note.end ? pulse_length(note.position, *note.end,
            "$.sound_channels[].notes[].l") : 0U;
        channels[target].notes.push_back({lane_x(note.lane), y, length, false, note.provenance});
    }

    void append_semantic_bgm(const bgm_event_t& event) {
        if (event.branch)
            fail(bmson_write_diagnostic_code_t::unsupported_feature,
                 "BMSON 1.0 cannot encode branch-projected BGM events", "$.sound_channels");
        if (!event.sound)
            fail(bmson_write_diagnostic_code_t::unresolved_resource,
                 "BMSON BGM events require a source audio resource", "$.sound_channels");
        const auto& resource = resource_for_id(song, *event.sound, "$.sound_channels");
        const auto target = channel_for(resource, "$.sound_channels", event.source_lane);
        if (reconcile_projection(channels[target], event.sound_channel_event, event.provenance,
                                 false, event.position, event.end, lane_id_t{})) return;
        channels[target].notes.push_back({0, pulse(event.position, "$.sound_channels[].notes[].y"),
                                          event.end ? pulse_length(event.position, *event.end,
                                              "$.sound_channels[].notes[].l") : 0U,
                                          false, event.provenance});
    }

    void append_semantic_events() {
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) append_semantic_note(note);
            for (const auto& event : measure.bgm_events) append_semantic_bgm(event);
        }
    }

    void normalize_channel(emitted_channel_t& channel) {
        std::ranges::stable_sort(channel.notes, [](const auto& left, const auto& right) {
            if (left.y != right.y) return left.y < right.y;
            if (left.provenance.source_order != right.provenance.source_order)
                return left.provenance.source_order < right.provenance.source_order;
            return left.x < right.x;
        });
        std::map<std::uint32_t, bool> restart_at_pulse;
        for (const auto& note : channel.notes) {
            const auto [found, inserted] = restart_at_pulse.emplace(note.y, !note.continuation);
            if (!inserted && !note.continuation) found->second = true;
        }
        for (auto& note : channel.notes) note.continuation = !restart_at_pulse[note.y];
        validate_channel_notes(channel);
        remove_bgm_overlaps(channel);
    }

    void validate_channel_notes(const emitted_channel_t& channel) {
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> lengths;
        for (const auto& note : channel.notes) {
            if (note.x == 0) continue;
            const auto [found, inserted] = lengths.emplace(std::pair{note.y, note.x}, note.length);
            if (!inserted && found->second != note.length)
                fail(bmson_write_diagnostic_code_t::duplicate_event,
                     "fused notes at one BMSON lane and pulse have unequal hold lengths",
                     "$.sound_channels");
            if (!inserted)
                fail(bmson_write_diagnostic_code_t::duplicate_event,
                     "one sound channel contains duplicate notes at the same lane and pulse",
                     "$.sound_channels");
        }
    }

    void remove_bgm_overlaps(emitted_channel_t& channel) {
        std::set<std::uint32_t> playable_pulses;
        for (const auto& note : channel.notes)
            if (note.x != 0) playable_pulses.insert(note.y);
        std::erase_if(channel.notes, [&](const auto& note) {
            return note.x == 0 && playable_pulses.contains(note.y);
        });
    }

    void validate_chart_note_lengths() {
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> lengths;
        for (const auto& channel : channels) {
            for (const auto& note : channel.notes) {
                if (note.x == 0) continue;
                const auto [found, inserted] = lengths.emplace(std::pair{note.y, note.x}, note.length);
                if (!inserted && found->second != note.length)
                    fail(bmson_write_diagnostic_code_t::duplicate_event,
                         "layered BMSON notes at one lane and pulse have unequal hold lengths",
                         "$.sound_channels");
            }
        }
    }

    auto build() -> std::vector<emitted_channel_t> {
        load_retained_channels();
        index_retained_projections();
        append_retained_events();
        append_semantic_events();
        for (auto& channel : channels) normalize_channel(channel);
        validate_chart_note_lengths();
        return std::move(channels);
    }
};


auto channels_for_chart(const song_document_t& song, const chart_document_t& chart,
                        const measure_index_t& index, const bmson_grid_t& grid,
                        const lane_map_t& lanes,
                        std::vector<bmson_write_diagnostic_t>& diagnostics)
    -> std::vector<emitted_channel_t> {
    return channel_builder_t{song, chart, index, grid, lanes, diagnostics}.build();
}

auto resource_name_for_identity(const song_document_t& song, std::string_view name_space,
                                std::string_view identifier) -> const resource_entry_t* {
    for (const auto& resource : song.resources) {
        const auto identity = std::ranges::find_if(resource.target_identities,
            [&](const auto& candidate) {
                return candidate.family == format_family_t::bmson &&
                       candidate.namespace_name == name_space && candidate.identifier == identifier;
            });
        if (identity != resource.target_identities.end()) return &resource;
    }
    return nullptr;
}

auto selected_chart(const song_document_t& song, bmson_write_options_t options)
    -> const chart_document_t& {
    if (options.chart) {
        const auto found = song.find_chart(*options.chart);
        if (found != nullptr) return *found;
        fail(bmson_write_diagnostic_code_t::missing_chart,
             "requested chart ID does not exist in this song", "$.charts");
    }
    if (song.charts.size() == 1) return song.charts.front();
    fail(bmson_write_diagnostic_code_t::missing_chart,
         "select one chart because BMSON stores a single chart per file", "$.charts");
}

auto bake_selected_chart(const song_document_t& song, const chart_document_t& selected)
    -> chart_document_t {
    song_document_t validation_song = song;
    validation_song.charts = {selected};
    const auto validation = validate_document(validation_song);
    if (!validation.empty())
        fail(bmson_write_diagnostic_code_t::invalid_document,
             "document failed core validation: " + validation.front().message, "$");
    const auto baked = bake_sv_recipes_for_export(selected);
    if (!baked)
        fail(bmson_write_diagnostic_code_t::unrepresentable_number,
             "SV recipe export bake failed: " + baked.diagnostics.front().message,
             "$.scroll_events");
    return *baked.chart;
}

void report_bmson_chart_losses(const chart_document_t& chart,
                               std::vector<bmson_write_diagnostic_t>& diagnostics) {
    const auto has_layered_notes = std::ranges::any_of(chart.measures, [](const auto& measure) {
        return std::ranges::any_of(measure.notes, [](const auto& note) {
            return !note.semantic_layers.empty();
        });
    });
    if (chart.semantic_layers.empty() && !has_layered_notes) return;
    diagnostics.push_back({.severity = diagnostic_severity_t::warning,
        .code = bmson_write_diagnostic_code_t::lossy_transform,
        .message = "semantic note layers were flattened while preserving each note's gameplay type",
        .json_path = "$.sound_channels"});
}

void report_bmson_bookmark_loss(const chart_document_t& chart,
                                std::vector<bmson_write_diagnostic_t>& diagnostics) {
    if (chart.bookmarks.empty()) return;
    diagnostics.push_back({.severity = diagnostic_severity_t::warning,
        .code = bmson_write_diagnostic_code_t::lossy_transform,
        .message = "bookmarks were omitted because BMSON 1.0 has no bookmark feature",
        .json_path = "$"});
}

void validate_bmson_timing_features(const chart_document_t& chart) {
    if (!chart.warps.empty() || !chart.scrolls.empty() || !chart.speeds.empty() ||
        !chart.kiai.empty() || !chart.timing_anchors.empty() || !chart.sections.empty())
        fail(bmson_write_diagnostic_code_t::unsupported_feature,
             "BMSON 1.0 cannot encode warps, scroll/speed, kiai, anchors, or sections", "$");
    for (const auto& pause : chart.pauses)
        if (pause.kind != pause_kind_t::stop || pause.source_unit != pause_unit_t::beats)
            fail(bmson_write_diagnostic_code_t::unsupported_feature,
                 "BMSON 1.0 supports beat-based stops only", "$.stop_events");
}

void validate_bmson_visual_features(const chart_document_t& chart) {
    for (const auto& event : chart.bga_events)
        if (event.kind != visual_event_kind_t::background &&
            event.kind != visual_event_kind_t::layer && event.kind != visual_event_kind_t::poor)
            fail(bmson_write_diagnostic_code_t::unsupported_feature,
                 "BMSON 1.0 supports background, layer, and poor BGA events only", "$.bga");
}

auto writer_geometry(const chart_document_t& chart) -> std::pair<bmson_grid_t, measure_index_t> {
    const auto grid = bmson_grid_t::from(chart.divisions_per_standard_measure);
    if (!grid || grid->resolution == 0 || grid->resolution > UINT32_MAX)
        fail(bmson_write_diagnostic_code_t::invalid_geometry,
             "chart resolution cannot be represented by BMSON 1.0", "$.info.resolution");
    const auto built_index = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(built_index))
        fail(bmson_write_diagnostic_code_t::invalid_geometry,
             "chart measures cannot be mapped to BMSON pulse positions", "$.lines");
    return {*grid, std::get<measure_index_t>(built_index)};
}

struct writer_output_context_t {
    const song_document_t& song;
    const chart_document_t& chart;
    const bmson_grid_t& grid;
    const measure_index_t& index;
    const lane_map_t& lanes;
    const std::vector<emitted_channel_t>& channels;
    bmson_write_options_t options;
    std::vector<bmson_write_diagnostic_t>& diagnostics;
    json_t root;
    std::vector<std::pair<std::string, std::string>> exact_tokens;
    std::vector<std::pair<const bpm_event_t*, std::uint32_t>> bpm_values;

    writer_output_context_t(const song_document_t& source_song,
                            const chart_document_t& selected_chart,
                            const bmson_grid_t& chart_grid, const measure_index_t& measure_index,
                            const lane_map_t& lane_map,
                            const std::vector<emitted_channel_t>& output_channels,
                            bmson_write_options_t write_options,
                            std::vector<bmson_write_diagnostic_t>& output_diagnostics)
        : song(source_song), chart(selected_chart), grid(chart_grid), index(measure_index),
          lanes(lane_map), channels(output_channels), options(write_options),
          diagnostics(output_diagnostics),
          root(original_json(source_song, selected_chart, write_options.preserve_source_fields)) {}

    auto exact_number(signed_rational_t value, std::string path) -> json_t {
        auto token = "__WORMEDIT_EXACT_NUMBER_" + std::to_string(exact_tokens.size()) + "__";
        const auto retained = root.dump();
        while (retained.find(token) != std::string::npos ||
               std::ranges::any_of(exact_tokens, [&](const auto& item) {
                   return item.first == token;
               })) token.push_back('_');
        exact_tokens.emplace_back(token, decimal_number(value, std::move(path)));
        return token;
    }
};

void set_info_level(const chart_document_t& chart, json_t& info) {
    if (!chart.metadata.rating) {
        info.erase("level");
        return;
    }
    const auto rating = *chart.metadata.rating;
    if (rating.denominator != 1 || rating.numerator < 0 ||
        static_cast<std::uint64_t>(rating.numerator) > UINT32_MAX)
        fail(bmson_write_diagnostic_code_t::unrepresentable_number,
             "BMSON level must be an unsigned 32-bit integer", "$.info.level");
    info["level"] = static_cast<std::uint32_t>(rating.numerator);
}

void populate_info_metadata(writer_output_context_t& output, json_t& info) {
    const auto genre_edited = apply_bmson_info_metadata(output.song, output.chart, info);
    if (!output.chart.metadata.author.empty() && !output.song.metadata.artist.empty() &&
        output.chart.metadata.author != output.song.metadata.artist)
        fail(bmson_write_diagnostic_code_t::unsupported_feature,
             "BMSON has one artist field and cannot retain different song and chart authors",
             "$.info.artist");
    info["title"] = output.song.metadata.title;
    info["artist"] = output.chart.metadata.author.empty()
                         ? output.song.metadata.artist : output.chart.metadata.author;
    info["chart_name"] = output.chart.metadata.name;
    info["resolution"] = output.grid.resolution;
    info["mode_hint"] = output.lanes.mode;
    if (output.song.metadata.ordered_search_tags.size() > 1)
        fail(bmson_write_diagnostic_code_t::unsupported_feature,
             "BMSON has one genre field and cannot retain multiple ordered search tags",
             "$.info.genre");
    if (output.song.metadata.ordered_search_tags.size() == 1 && !genre_edited)
        info["genre"] = output.song.metadata.ordered_search_tags.front();
    set_info_level(output.chart, info);
}

void append_info_resource_paths(const song_document_t& song, json_t& info) {
    for (const auto field : {"back_image", "eyecatch_image", "title_image", "banner_image",
                             "preview_music"}) {
        const auto* resource = resource_name_for_identity(song, "info", field);
        if (!resource) continue;
        if (resource->asset.value == 0)
            fail(bmson_write_diagnostic_code_t::unresolved_resource,
                 "BMSON info resource has no asset", std::string("$.info.") + field);
        const auto resource_path = std::string("$.info.") + field;
        if (field == std::string_view("preview_music")) {
            const auto path = asset_path(song, resource->asset, resource_path);
            info[field] = retained_resource_name(resource->name, path);
        } else {
            const auto path = visual_asset_path(song, resource->asset, resource_path);
            info[field] = retained_resource_name(resource->name, path);
        }
    }
}

void collect_bpm_values(writer_output_context_t& output) {
    for (const auto& bpm : output.chart.bpms) {
        const auto position = absolute_position(output.index, bpm.position);
        if (!position)
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "BPM event position is outside its chart measure", "$.bpm_events");
        output.bpm_values.emplace_back(&bpm,
            to_pulse(output.grid, *position, "$.bpm_events[].y"));
    }
}

void append_initial_bpm(writer_output_context_t& output, json_t& info) {
    const auto initial = std::ranges::find(
        output.bpm_values, 0U, &std::pair<const bpm_event_t*, std::uint32_t>::second);
    if (initial == output.bpm_values.end())
        fail(bmson_write_diagnostic_code_t::unsupported_feature,
             "BMSON requires an initial BPM event at pulse zero", "$.info.init_bpm");
    info["init_bpm"] = output.exact_number(initial->first->beats_per_minute, "$.info.init_bpm");
}

void write_info_section(writer_output_context_t& output) {
    output.root["version"] = "1.0.0";
    auto info = output.root.contains("info") && output.root["info"].is_object()
                    ? output.root["info"] : json_t::object();
    populate_info_metadata(output, info);
    collect_bpm_values(output);
    append_initial_bpm(output, info);
    append_info_resource_paths(output.song, info);
    output.root["info"] = std::move(info);
}

void write_lines_section(writer_output_context_t& output) {
    auto lines = json_t::array();
    for (const auto& measure : output.index.entries)
        lines.push_back(json_t{{"y", to_pulse(output.grid, measure.absolute_begin_row,
                                               "$.lines[].y")}});
    output.root["lines"] = std::move(lines);
}

void write_bpm_section(writer_output_context_t& output) {
    auto bpms = json_t::array();
    std::ranges::stable_sort(output.bpm_values, [](const auto& left, const auto& right) {
        if (left.second != right.second) return left.second < right.second;
        return left.first->provenance.source_order < right.first->provenance.source_order;
    });
    for (const auto& [event, y] : output.bpm_values) {
        auto value = source_object(event->provenance);
        value["y"] = y;
        value["bpm"] = output.exact_number(event->beats_per_minute, "$.bpm_events[].bpm");
        bpms.push_back(std::move(value));
    }
    output.root["bpm_events"] = std::move(bpms);
}

void write_stops_section(writer_output_context_t& output) {
    auto stops = json_t::array();
    for (const auto& event : output.chart.pauses) {
        const auto position = absolute_position(output.index, event.position);
        if (!position)
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "stop event position is outside its chart measure", "$.stop_events");
        const auto y = to_pulse(output.grid, *position, "$.stop_events[].y");
        const auto duration = exact_nonnegative_scaled(event.duration, output.grid.resolution,
                                                       "$.stop_events[].duration");
        if (duration > UINT32_MAX)
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "stop duration exceeds the BMSON 32-bit pulse range", "$.stop_events[].duration");
        auto value = source_object(event.provenance);
        value["y"] = y;
        value["duration"] = static_cast<std::uint32_t>(duration);
        stops.push_back(std::move(value));
    }
    output.root["stop_events"] = std::move(stops);
}

auto write_sound_channel(const emitted_channel_t& source, const json_t& old_channels,
                         std::set<std::size_t>& retained_indexes) -> json_t {
    const auto retained_index = source.retained_source_index;
    if (retained_index && !retained_indexes.insert(*retained_index).second)
        fail(bmson_write_diagnostic_code_t::invalid_document,
             "multiple sound channels claim the same retained BMSON source object",
             "$.sound_channels");
    auto channel = retained_index && *retained_index < old_channels.size() &&
                           old_channels[*retained_index].is_object()
                       ? old_channels[*retained_index] : json_t::object();
    channel["name"] = source.name;
    auto notes = json_t::array();
    for (const auto& source_note : source.notes) {
        auto note = source_object(source_note.provenance);
        note["x"] = source_note.x;
        note["y"] = source_note.y;
        if (source_note.length != 0) note["l"] = source_note.length;
        else note.erase("l");
        note["c"] = source_note.continuation;
        notes.push_back(std::move(note));
    }
    channel["notes"] = std::move(notes);
    return channel;
}

void write_sound_channels_section(writer_output_context_t& output) {
    auto sound_channels = json_t::array();
    const auto old_channels = output.root.contains("sound_channels") &&
                              output.root["sound_channels"].is_array()
                                  ? output.root["sound_channels"] : json_t::array();
    std::set<std::size_t> retained_indexes;
    for (const auto& channel : output.channels)
        sound_channels.push_back(write_sound_channel(channel, old_channels, retained_indexes));
    output.root["sound_channels"] = std::move(sound_channels);
}

auto collect_bga_resources(const song_document_t& song, const chart_document_t& chart)
    -> std::vector<const resource_entry_t*> {
    std::vector<const resource_entry_t*> result;
    for (const auto& resource : song.resources) {
        const auto retained = std::ranges::any_of(resource.target_identities, [](const auto& id) {
            return id.family == format_family_t::bmson && id.namespace_name == "bga";
        });
        if (retained) result.push_back(&resource);
    }
    for (const auto& event : chart.bga_events) {
        const auto found = std::ranges::find(result, event.resource,
            [](const resource_entry_t* resource) { return resource->id; });
        if (found == result.end()) result.push_back(&resource_for_id(song, event.resource, "$.bga"));
    }
    return result;
}

auto next_bga_header_id(std::optional<std::uint32_t> candidate,
                        std::set<std::uint32_t>& used, std::uint32_t& next) -> std::uint32_t {
    if (candidate && !used.contains(*candidate)) return *candidate;
    while (used.contains(next) && next != UINT32_MAX) ++next;
    if (used.contains(next))
        fail(bmson_write_diagnostic_code_t::invalid_geometry,
             "no BMSON BGA header IDs remain", "$.bga.bga_header");
    return next++;
}

auto retained_bga_header(const json_t& bga, std::uint32_t id) -> json_t {
    if (!bga.contains("bga_header") || !bga["bga_header"].is_array()) return json_t::object();
    for (const auto& old : bga["bga_header"])
        if (old.is_object() && old.value("id", UINT32_MAX) == id) return old;
    return json_t::object();
}

auto write_bga_headers(const song_document_t& song, const json_t& bga,
                       const std::vector<const resource_entry_t*>& resources,
                       std::map<std::uint64_t, std::uint32_t>& header_ids) -> json_t {
    auto headers = json_t::array();
    std::set<std::uint32_t> used;
    std::uint32_t next{};
    for (const auto* resource : resources) {
        const auto id = next_bga_header_id(optional_u32_identity(*resource, "bga"), used, next);
        used.insert(id);
        header_ids.emplace(resource->id.value, id);
        auto header = retained_bga_header(bga, id);
        const auto path = visual_asset_path(song, resource->asset, "$.bga.bga_header[].name");
        header["id"] = id;
        header["name"] = retained_resource_name(resource->name, path);
        headers.push_back(std::move(header));
    }
    return headers;
}

auto write_bga_event(const bga_event_t& event, const measure_index_t& index,
                     const bmson_grid_t& grid,
                     const std::map<std::uint64_t, std::uint32_t>& header_ids) -> json_t {
    const auto position = absolute_position(index, event.position);
    if (!position)
        fail(bmson_write_diagnostic_code_t::invalid_geometry,
             "BGA event position is outside its chart measure", "$.bga");
    const auto header = header_ids.find(event.resource.value);
    if (header == header_ids.end())
        fail(bmson_write_diagnostic_code_t::unresolved_resource,
             "BGA event has no emitted resource header", "$.bga");
    auto value = source_object(event.provenance);
    value["y"] = to_pulse(grid, *position, "$.bga.*_events[].y");
    value["id"] = header->second;
    return value;
}

void append_bga_event(json_t& background, json_t& layer, json_t& poor,
                      const bga_event_t& event, const measure_index_t& index,
                      const bmson_grid_t& grid,
                      const std::map<std::uint64_t, std::uint32_t>& header_ids) {
    auto value = write_bga_event(event, index, grid, header_ids);
    if (event.kind == visual_event_kind_t::background) background.push_back(std::move(value));
    else if (event.kind == visual_event_kind_t::layer) layer.push_back(std::move(value));
    else if (event.kind == visual_event_kind_t::poor) poor.push_back(std::move(value));
    else fail(bmson_write_diagnostic_code_t::unsupported_feature,
              "BMSON 1.0 cannot encode this visual event kind", "$.bga");
}

auto sorted_bga_events(const chart_document_t& chart, const measure_index_t& index)
    -> std::vector<const bga_event_t*> {
    std::vector<const bga_event_t*> events;
    for (const auto& event : chart.bga_events) events.push_back(&event);
    std::ranges::stable_sort(events, [&](const auto* left, const auto* right) {
        const auto left_row = absolute_position(index, left->position).value_or(UINT64_MAX);
        const auto right_row = absolute_position(index, right->position).value_or(UINT64_MAX);
        if (left_row != right_row) return left_row < right_row;
        return left->provenance.source_order < right->provenance.source_order;
    });
    return events;
}

void write_bga_section(writer_output_context_t& output) {
    auto bga = output.root.contains("bga") && output.root["bga"].is_object()
                   ? output.root["bga"] : json_t::object();
    const auto resources = collect_bga_resources(output.song, output.chart);
    std::map<std::uint64_t, std::uint32_t> header_ids;
    bga["bga_header"] = write_bga_headers(output.song, bga, resources, header_ids);
    auto background = json_t::array();
    auto layer = json_t::array();
    auto poor = json_t::array();
    for (const auto* event : sorted_bga_events(output.chart, output.index))
        append_bga_event(background, layer, poor, *event, output.index, output.grid, header_ids);
    bga["bga_events"] = std::move(background);
    bga["layer_events"] = std::move(layer);
    bga["poor_events"] = std::move(poor);
    output.root["bga"] = std::move(bga);
}

auto serialize_writer_output(writer_output_context_t& output) -> std::string {
    auto serialized = output.root.dump(2, ' ', false, json_t::error_handler_t::strict);
    for (const auto& [token, number] : output.exact_tokens) {
        const auto quoted = json_t(token).dump();
        std::size_t offset{};
        while ((offset = serialized.find(quoted, offset)) != std::string::npos) {
            serialized.replace(offset, quoted.size(), number);
            offset += number.size();
        }
    }
    return serialized + "\n";
}

auto write_impl(const song_document_t& song, bmson_write_options_t options,
                std::vector<bmson_write_diagnostic_t>& diagnostics) -> std::string {
    const auto selected = selected_chart(song, options);
    auto chart = bake_selected_chart(song, selected);
    if (!chart.beat_zero_audio_offset_seconds.denominator ||
        chart.beat_zero_audio_offset_seconds.numerator != 0)
        fail(bmson_write_diagnostic_code_t::unsupported_feature,
             "BMSON 1.0 has no beat-zero audio offset field", "$.info");
    report_bmson_chart_losses(chart, diagnostics);
    report_bmson_bookmark_loss(chart, diagnostics);
    validate_bmson_timing_features(chart);
    validate_bmson_visual_features(chart);
    const auto [grid, index] = writer_geometry(chart);
    const auto lanes = lane_map_t::from(chart);
    const auto channels = channels_for_chart(song, chart, index, grid, lanes, diagnostics);
    writer_output_context_t output(song, chart, grid, index, lanes, channels, options, diagnostics);
    write_info_section(output);
    write_lines_section(output);
    write_bpm_section(output);
    write_stops_section(output);
    write_sound_channels_section(output);
    write_bga_section(output);
    return serialize_writer_output(output);
}

} // namespace

auto bmson_write_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto bmson_write_result_t::from(bmson_write_outcome_t&& outcome) -> bmson_write_result_t {
    return {.json = std::move(outcome.value), .diagnostics = std::move(outcome.diagnostics)};
}

auto bmson_write_result_t::from(const song_document_t& song, bmson_write_options_t options)
    -> bmson_write_outcome_t {
    bmson_write_outcome_t result{};
    try {
        result.value = write_impl(song, std::move(options), result.diagnostics);
    } catch (const write_failure_t& failure) {
        result.diagnostics.push_back({diagnostic_severity_t::error, failure.code,
                                      failure.message, failure.path});
    } catch (const nlohmann::json::exception& error) {
        result.diagnostics.push_back({diagnostic_severity_t::error,
                                      bmson_write_diagnostic_code_t::invalid_document,
                                      error.what(), "$"});
    }
    return result;
}



auto write_bmson(const song_document_t& song, bmson_write_options_t options) -> bmson_write_result_t {
    return bmson_write_result_t::from(bmson_write_result_t::from(song, std::move(options)));
}

} // namespace wormedit

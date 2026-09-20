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

auto checked_multiply(std::uint64_t left, std::uint64_t right,
                      std::uint64_t& result) -> bool {
    if (right != 0 && left > std::numeric_limits<std::uint64_t>::max() / right) {
        return false;
    }
    result = left * right;
    return true;
}

auto checked_add(std::uint64_t left, std::uint64_t right,
                 std::uint64_t& result) -> bool {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) return false;
    result = left + right;
    return true;
}

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
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
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
    const auto asset = std::ranges::find(song.audio_assets, id, &audio_asset_t::id);
    if (asset == song.audio_assets.end()) {
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
    const auto divisor = std::gcd(numerator, denominator);
    return {numerator / divisor, denominator / divisor};
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
                     std::string path) -> const resource_entry_t& {
    const auto resource = std::ranges::find(song.resources, id, &resource_entry_t::id);
    if (resource == song.resources.end()) {
        fail(bmson_write_diagnostic_code_t::unresolved_resource,
             "event refers to an unknown resource", std::move(path));
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
    std::string mode{"generic-nkeys"};
    std::unordered_map<std::uint64_t, std::uint32_t> x_by_lane;
};

auto make_lane_map(const chart_document_t& chart) -> lane_map_t {
    lane_map_t result;
    if (!chart.lane_layouts.empty()) {
        const auto& initial = chart.lane_layouts.front().layout;
        constexpr std::string_view prefix{"bmson-"};
        if (std::string_view(initial.profile_id).starts_with(prefix)) {
            const auto candidate = initial.profile_id.substr(prefix.size());
            if (candidate == "beat-5k" || candidate == "beat-7k" ||
                candidate == "beat-10k" || candidate == "beat-14k" ||
                candidate == "popn-5k" || candidate == "popn-9k" ||
                candidate == "generic-nkeys" || candidate == "generic-24keys" ||
                candidate == "generic-48keys") {
                result.mode = candidate;
            }
        }
        for (std::size_t segment = 1; segment < chart.lane_layouts.size(); ++segment) {
            const auto& other = chart.lane_layouts[segment].layout;
            if (other.profile_id != initial.profile_id ||
                other.lanes.size() != initial.lanes.size() ||
                other.split_stage != initial.split_stage ||
                other.split_after_column != initial.split_after_column ||
                other.bmson_generic_keys_only != initial.bmson_generic_keys_only) {
                fail(bmson_write_diagnostic_code_t::unsupported_feature,
                     "BMSON has one chart-wide key layout; changing layouts are not representable",
                     "$.info.mode_hint");
            }
            for (std::size_t lane = 0; lane < initial.lanes.size(); ++lane) {
                if (other.lanes[lane].id != initial.lanes[lane].id ||
                    other.lanes[lane].role != initial.lanes[lane].role ||
                    other.lanes[lane].stage != initial.lanes[lane].stage ||
                    other.lanes[lane].display_order != initial.lanes[lane].display_order) {
                    fail(bmson_write_diagnostic_code_t::unsupported_feature,
                         "BMSON has one chart-wide key layout; changing lane identities are not representable",
                         "$.info.mode_hint");
                }
            }
        }

        auto lanes = initial.lanes;
        std::ranges::stable_sort(lanes, {}, &lane_definition_t::display_order);
        const auto positions = fixed_x_positions(result.mode);
        for (std::size_t index = 0; index < lanes.size(); ++index) {
            std::uint32_t x{};
            if (lanes[index].id.value >= 100001 && lanes[index].id.value <= 100000 + UINT32_MAX) {
                x = static_cast<std::uint32_t>(lanes[index].id.value - 100000);
            } else if (result.mode == "generic-nkeys") {
                if (lanes[index].display_order == UINT32_MAX) {
                    fail(bmson_write_diagnostic_code_t::invalid_geometry,
                         "generic BMSON key index exceeds the supported range", "$.info.mode_hint");
                }
                x = lanes[index].display_order + 1;
            } else if (index < positions.size()) {
                x = positions[index];
            } else {
                fail(bmson_write_diagnostic_code_t::unsupported_feature,
                     "lane count does not fit the selected BMSON mode_hint", "$.info.mode_hint");
            }
            if (x == 0 || !result.x_by_lane.emplace(lanes[index].id.value, x).second) {
                fail(bmson_write_diagnostic_code_t::invalid_document,
                     "lane layout cannot be mapped to unique BMSON x positions", "$.info.mode_hint");
            }
        }
    } else {
        std::set<std::uint64_t> lane_ids;
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) lane_ids.insert(note.lane.value);
        }
        std::uint32_t x = 1;
        for (const auto id : lane_ids) {
            result.x_by_lane.emplace(id, x++);
        }
    }
    return result;
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

auto channels_for_chart(const song_document_t& song, const chart_document_t& chart,
                        const measure_index_t& index, const bmson_grid_t& grid,
                        const lane_map_t& lanes,
                        std::vector<bmson_write_diagnostic_t>& diagnostics)
    -> std::vector<emitted_channel_t> {
    std::vector<emitted_channel_t> result;
    result.reserve(chart.sound_channels.size());
    for (const auto& source : chart.sound_channels) {
        if (source.source_clip.begin_frame != 0 || source.source_clip.end_frame.has_value()) {
            fail(bmson_write_diagnostic_code_t::unsupported_feature,
                 "BMSON 1.0 cannot encode a cropped source audio clip", "$.sound_channels");
        }
        const auto path = asset_path(song, source.source_clip.source, "$.sound_channels");
        result.push_back({source.source_clip.source,
                          retained_resource_name(source.name, path), &source,
                          sound_channel_source_index(source), {}});
    }

    std::map<std::pair<std::uint64_t, std::uint64_t>, std::size_t> authored_channel_indexes;
    const auto channel_index_for_resource = [&](const resource_entry_t& resource)
        -> std::optional<std::size_t> {
        if (const auto identity = optional_u32_identity(resource, "sound_channel")) {
            const auto found = std::ranges::find_if(result, [&](const auto& channel) {
                return channel.retained_source_index == static_cast<std::size_t>(*identity);
            });
            if (found != result.end()) {
                if (found->asset != resource.asset) {
                    fail(bmson_write_diagnostic_code_t::invalid_document,
                         "sound-channel resource identity points to a channel with a different audio asset",
                         "$.sound_channels");
                }
                return static_cast<std::size_t>(std::distance(result.begin(), found));
            }
            return std::nullopt;
        }
        return std::nullopt;
    };
    const auto ensure_channel = [&](const resource_entry_t& resource,
                                    std::string path,
                                    std::optional<object_id_t> bgm_lane = {}) -> std::size_t {
        if (const auto found = channel_index_for_resource(resource)) return *found;
        const auto name = asset_path(song, resource.asset, std::move(path));
        const auto key = std::pair{resource.id.value, bgm_lane ? bgm_lane->value : std::uint64_t{0}};
        if (const auto found = authored_channel_indexes.find(key);
            found != authored_channel_indexes.end()) {
            return found->second;
        }
        result.push_back({resource.asset, name, nullptr, std::nullopt, {}});
        authored_channel_indexes.emplace(key, result.size() - 1);
        return result.size() - 1;
    };
    const auto pulse64 = [&](local_position_t position, std::string path,
                             bool end = false) -> std::uint64_t {
        const auto row = absolute_position(index, position, end);
        if (!row) {
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "event position is outside its chart measure", std::move(path));
        }
        const auto value = bmson_pulse_for_row(grid, *row);
        if (!value.has_value()) {
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "event position cannot be represented as a BMSON pulse", std::move(path));
        }
        return *value;
    };
    const auto pulse = [&](local_position_t position, std::string path) -> std::uint32_t {
        const auto value = pulse64(position, path);
        if (value > UINT32_MAX) {
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "event position exceeds the BMSON 32-bit pulse range", std::move(path));
        }
        return static_cast<std::uint32_t>(value);
    };
    const auto pulse_length = [&](local_position_t begin, local_position_t end,
                                  std::string path) -> std::uint32_t {
        const auto begin_pulse = pulse64(begin, path);
        const auto end_pulse = pulse64(end, path, true);
        if (end_pulse <= begin_pulse || end_pulse - begin_pulse > UINT32_MAX) {
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "hold length must be positive and fit the BMSON 32-bit pulse range",
                 std::move(path));
        }
        return static_cast<std::uint32_t>(end_pulse - begin_pulse);
    };

    for (std::size_t channel_index = 0; channel_index < chart.sound_channels.size();
         ++channel_index) {
        const auto& channel = chart.sound_channels[channel_index];
        for (const auto& event : channel.events) {
            std::uint32_t x{};
            if (event.playable) {
                const auto lane = lanes.x_by_lane.find(event.lane.value);
                if (lane == lanes.x_by_lane.end()) {
                    fail(bmson_write_diagnostic_code_t::invalid_geometry,
                         "sound-channel event lane is absent from the selected BMSON layout",
                         "$.sound_channels[].notes[].x");
                }
                x = lane->second;
            } else if (event.lane.value != 0) {
                fail(bmson_write_diagnostic_code_t::invalid_document,
                     "non-playable sound-channel events must use lane zero",
                     "$.sound_channels[].notes[].x");
            }
            result[channel_index].notes.push_back({
                .x = x,
                .y = pulse(event.position, "$.sound_channels[].notes[].y"),
                .length = event.end.has_value()
                              ? pulse_length(event.position, *event.end,
                                             "$.sound_channels[].notes[].l")
                              : 0U,
                .continuation = event.continuation,
                .provenance = event.provenance,
            });
        }
    }

    auto projection_warning_emitted = false;
    struct retained_projection_t {
        std::size_t channel_index{};
        const sound_channel_event_t* event{};
    };
    std::unordered_map<std::uint64_t, retained_projection_t> retained_projections;
    for (std::size_t channel_index = 0; channel_index < chart.sound_channels.size();
         ++channel_index) {
        for (const auto& event : chart.sound_channels[channel_index].events) {
            retained_projections.emplace(event.id.value,
                                         retained_projection_t{channel_index, &event});
        }
    }
    const auto source_projection = [&](const emitted_channel_t& channel,
                                       std::optional<object_id_t> linked_event,
                                       const source_provenance_t& provenance,
                                       bool playable, local_position_t position,
                                       std::optional<local_position_t> end,
                                       lane_id_t lane) -> const sound_channel_event_t* {
        if (linked_event.has_value()) {
            const auto found = retained_projections.find(linked_event->value);
            if (found == retained_projections.end()) {
                fail(bmson_write_diagnostic_code_t::invalid_document,
                     "semantic sound link refers to an unknown retained source event",
                     "$.sound_channels");
            }
            const auto target_index = static_cast<std::size_t>(&channel - result.data());
            if (found->second.channel_index != target_index) {
                fail(bmson_write_diagnostic_code_t::invalid_document,
                     "semantic sound resource does not match its retained source channel",
                     "$.sound_channels");
            }
            return found->second.event;
        }
        if (channel.source == nullptr) {
            return nullptr;
        }
        if (!provenance.source_field.empty()) {
            const auto found = std::ranges::find_if(
                channel.source->events, [&](const auto& candidate) {
                    return candidate.playable == playable &&
                           candidate.provenance.source_field == provenance.source_field &&
                           (provenance.source_order == 0 ||
                            candidate.provenance.source_order == provenance.source_order);
                });
            return found == channel.source->events.end() ? nullptr : &*found;
        }
        const auto found = std::ranges::find_if(
            channel.source->events, [&](const auto& candidate) {
                return candidate.playable == playable && candidate.position == position &&
                       candidate.end == end && (!playable || candidate.lane == lane);
            });
        return found == channel.source->events.end() ? nullptr : &*found;
    };
    const auto reconcile_projection = [&](const emitted_channel_t& channel,
                                          std::optional<object_id_t> linked_event,
                                          const source_provenance_t& provenance,
                                          bool playable, local_position_t position,
                                          std::optional<local_position_t> end,
                                          lane_id_t lane) -> bool {
        const auto* source_event = source_projection(
            channel, linked_event, provenance, playable, position, end, lane);
        if (source_event == nullptr) {
            return false;
        }
        const auto agrees = source_event->position == position && source_event->end == end &&
                            (!playable || source_event->lane == lane);
        if (!agrees && !projection_warning_emitted) {
            diagnostics.push_back({
                .severity = diagnostic_severity_t::warning,
                .code = bmson_write_diagnostic_code_t::lossy_transform,
                .message = "BMSON source-channel events replaced stale semantic note/BGM projections",
                .json_path = "$.sound_channels",
            });
            projection_warning_emitted = true;
        }
        return true;
    };

    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.kind != note_kind_t::tap && note.kind != note_kind_t::hold) {
                fail(bmson_write_diagnostic_code_t::unsupported_feature,
                     "BMSON 1.0 supports taps and holds only", "$.sound_channels");
            }
            if (!note.sound) {
                fail(bmson_write_diagnostic_code_t::unresolved_resource,
                     "BMSON playable notes require a source audio resource", "$.sound_channels");
            }
            if (note.label || note.branch) {
                fail(bmson_write_diagnostic_code_t::unsupported_feature,
                     "BMSON 1.0 cannot encode note labels or branches",
                     "$.sound_channels");
            }
            const auto& resource = resource_for_id(song, *note.sound, "$.sound_channels");
            const auto target = ensure_channel(resource, "$.sound_channels");
            if (reconcile_projection(result[target], note.sound_channel_event, note.provenance, true,
                                     note.position, note.end, note.lane)) {
                continue;
            }
            const auto y = pulse(note.position, "$.sound_channels[].notes[].y");
            std::uint32_t length_value{};
            if (note.end) {
                length_value = pulse_length(note.position, *note.end,
                                            "$.sound_channels[].notes[].l");
            }
            const auto lane = lanes.x_by_lane.find(note.lane.value);
            if (lane == lanes.x_by_lane.end()) {
                fail(bmson_write_diagnostic_code_t::invalid_geometry,
                     "playable lane is absent from the selected BMSON layout", "$.sound_channels[].notes[].x");
            }
            result[target].notes.push_back({lane->second, y, length_value, false,
                                            note.provenance});
        }
        for (const auto& event : measure.bgm_events) {
            if (event.branch) {
                fail(bmson_write_diagnostic_code_t::unsupported_feature,
                     "BMSON 1.0 cannot encode branch-projected BGM events", "$.sound_channels");
            }
            if (!event.sound) {
                fail(bmson_write_diagnostic_code_t::unresolved_resource,
                     "BMSON BGM events require a source audio resource", "$.sound_channels");
            }
            const auto& resource = resource_for_id(song, *event.sound, "$.sound_channels");
            const auto target = ensure_channel(resource, "$.sound_channels", event.source_lane);
            if (reconcile_projection(result[target], event.sound_channel_event, event.provenance, false,
                                     event.position, std::nullopt, lane_id_t{})) {
                continue;
            }
            const auto y = pulse(event.position, "$.sound_channels[].notes[].y");
            result[target].notes.push_back({0, y, 0, false, event.provenance});
        }
    }

    // The reader retains BGM source events that coincide with playable events even though
    // the specification discards those BGM slices. Exporting the playable event is enough
    // to recover the same semantic chart, so these source-only slots are intentionally omitted.
    for (auto& channel : result) {
        std::ranges::stable_sort(channel.notes, [](const auto& left, const auto& right) {
            if (left.y != right.y) return left.y < right.y;
            if (left.provenance.source_order != right.provenance.source_order) {
                return left.provenance.source_order < right.provenance.source_order;
            }
            return left.x < right.x;
        });
        std::map<std::uint32_t, bool> restart_at_pulse;
        for (const auto& note : channel.notes) {
            const auto [found, inserted] = restart_at_pulse.emplace(note.y, !note.continuation);
            if (!inserted && !note.continuation) found->second = true;
        }
        for (auto& note : channel.notes) note.continuation = !restart_at_pulse[note.y];

        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> lengths;
        for (const auto& note : channel.notes) {
            if (note.x == 0) continue;
            const auto [found, inserted] = lengths.emplace(std::pair{note.y, note.x}, note.length);
            if (!inserted && found->second != note.length) {
                fail(bmson_write_diagnostic_code_t::duplicate_event,
                     "fused notes at one BMSON lane and pulse have unequal hold lengths",
                     "$.sound_channels");
            }
            if (!inserted) {
                fail(bmson_write_diagnostic_code_t::duplicate_event,
                     "one sound channel contains duplicate notes at the same lane and pulse",
                     "$.sound_channels");
            }
        }
        std::set<std::uint32_t> playable_pulses;
        for (const auto& note : channel.notes) {
            if (note.x != 0) playable_pulses.insert(note.y);
        }
        std::erase_if(channel.notes, [&](const auto& note) {
            return note.x == 0 && playable_pulses.contains(note.y);
        });
    }
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> chart_lengths;
    for (const auto& channel : result) {
        for (const auto& note : channel.notes) {
            if (note.x == 0) {
                continue;
            }
            const auto [found, inserted] = chart_lengths.emplace(
                std::pair{note.y, note.x}, note.length);
            if (!inserted && found->second != note.length) {
                fail(bmson_write_diagnostic_code_t::duplicate_event,
                     "layered BMSON notes at one lane and pulse have unequal hold lengths",
                     "$.sound_channels");
            }
        }
    }
    return result;
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

auto write_impl(const song_document_t& song, bmson_write_options_t options,
                std::vector<bmson_write_diagnostic_t>& diagnostics) -> std::string {
    const chart_document_t* chart{};
    if (options.chart) {
        const auto found = std::ranges::find(song.charts, *options.chart, &chart_document_t::id);
        if (found == song.charts.end()) {
            fail(bmson_write_diagnostic_code_t::missing_chart,
                 "requested chart ID does not exist in this song", "$.charts");
        }
        chart = &*found;
    } else if (song.charts.size() == 1) {
        chart = &song.charts.front();
    } else {
        fail(bmson_write_diagnostic_code_t::missing_chart,
             "select one chart because BMSON stores a single chart per file", "$.charts");
    }

    song_document_t validation_song = song;
    validation_song.charts = {*chart};
    const auto validation = validate_document(validation_song);
    if (!validation.empty()) {
        fail(bmson_write_diagnostic_code_t::invalid_document,
             "document failed core validation: " + validation.front().message, "$");
    }
    auto baked_chart = bake_sv_recipes_for_export(*chart);
    if (!baked_chart) {
        fail(bmson_write_diagnostic_code_t::unrepresentable_number,
             "SV recipe export bake failed: " + baked_chart.diagnostics.front().message,
             "$.scroll_events");
    }
    chart = &*baked_chart.chart;
    if (!chart->beat_zero_audio_offset_seconds.denominator ||
        chart->beat_zero_audio_offset_seconds.numerator != 0) {
        fail(bmson_write_diagnostic_code_t::unsupported_feature,
             "BMSON 1.0 has no beat-zero audio offset field", "$.info");
    }
    const auto has_layered_notes = std::ranges::any_of(
        chart->measures, [](const auto& measure) {
            return std::ranges::any_of(measure.notes, [](const auto& note) {
                return !note.semantic_layers.empty();
            });
        });
    if (!chart->semantic_layers.empty() || has_layered_notes) {
        diagnostics.push_back({
            .severity = diagnostic_severity_t::warning,
            .code = bmson_write_diagnostic_code_t::lossy_transform,
            .message = "semantic note layers were flattened while preserving each note's gameplay type",
            .json_path = "$.sound_channels",
        });
    }
    if (!chart->bookmarks.empty()) {
        diagnostics.push_back({
            .severity = diagnostic_severity_t::warning,
            .code = bmson_write_diagnostic_code_t::lossy_transform,
            .message = "bookmarks were omitted because BMSON 1.0 has no bookmark feature",
            .json_path = "$",
        });
    }
    if (!chart->warps.empty() || !chart->scrolls.empty() || !chart->speeds.empty() ||
        !chart->kiai.empty() || !chart->timing_anchors.empty() || !chart->sections.empty()) {
        fail(bmson_write_diagnostic_code_t::unsupported_feature,
             "BMSON 1.0 cannot encode warps, scroll/speed, kiai, anchors, or sections",
             "$");
    }
    for (const auto& pause : chart->pauses) {
        if (pause.kind != pause_kind_t::stop || pause.source_unit != pause_unit_t::beats) {
            fail(bmson_write_diagnostic_code_t::unsupported_feature,
                 "BMSON 1.0 supports beat-based stops only", "$.stop_events");
        }
    }
    for (const auto& event : chart->bga_events) {
        if (event.kind != visual_event_kind_t::background &&
            event.kind != visual_event_kind_t::layer && event.kind != visual_event_kind_t::poor) {
            fail(bmson_write_diagnostic_code_t::unsupported_feature,
                 "BMSON 1.0 supports background, layer, and poor BGA events only", "$.bga");
        }
    }

    const auto grid = make_default_bmson_grid(chart->divisions_per_standard_measure);
    if (!grid || grid->resolution == 0 || grid->resolution > UINT32_MAX) {
        fail(bmson_write_diagnostic_code_t::invalid_geometry,
             "chart resolution cannot be represented by BMSON 1.0", "$.info.resolution");
    }
    const auto built_index = build_measure_index(*chart);
    if (!std::holds_alternative<measure_index_t>(built_index)) {
        fail(bmson_write_diagnostic_code_t::invalid_geometry,
             "chart measures cannot be mapped to BMSON pulse positions", "$.lines");
    }
    const auto& index = std::get<measure_index_t>(built_index);
    const auto lane_map = make_lane_map(*chart);
    const auto channels = channels_for_chart(song, *chart, index, *grid, lane_map,
                                             diagnostics);

    auto root = original_json(song, *chart, options.preserve_source_fields);
    root["version"] = "1.0.0";
    auto info = root.contains("info") && root["info"].is_object()
                    ? root["info"] : json_t::object();
    if (!chart->metadata.author.empty() && !song.metadata.artist.empty() &&
        chart->metadata.author != song.metadata.artist) {
        fail(bmson_write_diagnostic_code_t::unsupported_feature,
             "BMSON has one artist field and cannot retain different song and chart authors",
             "$.info.artist");
    }
    info["title"] = song.metadata.title;
    info["artist"] = chart->metadata.author.empty() ? song.metadata.artist : chart->metadata.author;
    info["chart_name"] = chart->metadata.name;
    info["resolution"] = grid->resolution;
    info["mode_hint"] = lane_map.mode;
    if (song.metadata.ordered_search_tags.size() > 1) {
        fail(bmson_write_diagnostic_code_t::unsupported_feature,
             "BMSON has one genre field and cannot retain multiple ordered search tags",
             "$.info.genre");
    }
    if (song.metadata.ordered_search_tags.size() == 1) {
        info["genre"] = song.metadata.ordered_search_tags.front();
    }
    if (chart->metadata.rating) {
        if (chart->metadata.rating->denominator != 1 || chart->metadata.rating->numerator < 0 ||
            static_cast<std::uint64_t>(chart->metadata.rating->numerator) > UINT32_MAX) {
            fail(bmson_write_diagnostic_code_t::unrepresentable_number,
                 "BMSON level must be an unsigned 32-bit integer", "$.info.level");
        }
        info["level"] = static_cast<std::uint32_t>(chart->metadata.rating->numerator);
    } else {
        info.erase("level");
    }

    std::vector<std::pair<std::string, std::string>> exact_tokens;
    auto exact_number = [&](signed_rational_t value, std::string path) -> json_t {
        auto token = "__WORMEDIT_EXACT_NUMBER_" + std::to_string(exact_tokens.size()) + "__";
        const auto retained = root.dump();
        while (retained.find(token) != std::string::npos ||
               std::ranges::any_of(exact_tokens, [&](const auto& item) { return item.first == token; })) {
            token.push_back('_');
        }
        exact_tokens.emplace_back(token, decimal_number(value, std::move(path)));
        return token;
    };
    std::vector<std::pair<const bpm_event_t*, std::uint32_t>> bpm_values;
    for (const auto& bpm : chart->bpms) {
        const auto position = absolute_position(index, bpm.position);
        if (!position) {
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "BPM event position is outside its chart measure", "$.bpm_events");
        }
        bpm_values.emplace_back(&bpm, to_pulse(*grid, *position, "$.bpm_events[].y"));
    }
    const auto initial = std::ranges::find(bpm_values, 0U, &std::pair<const bpm_event_t*, std::uint32_t>::second);
    if (initial == bpm_values.end()) {
        fail(bmson_write_diagnostic_code_t::unsupported_feature,
             "BMSON requires an initial BPM event at pulse zero", "$.info.init_bpm");
    }
    info["init_bpm"] = exact_number(initial->first->beats_per_minute, "$.info.init_bpm");

    // Standard BMSON image and preview fields point to resources already retained in the
    // catalog. Existing source values survive when no matching resource was materialized.
    for (const auto field : {"back_image", "eyecatch_image", "title_image", "banner_image", "preview_music"}) {
        const auto* resource = resource_name_for_identity(song, "info", field);
        if (!resource) continue;
        if (resource->asset.value == 0) {
            fail(bmson_write_diagnostic_code_t::unresolved_resource,
                 "BMSON info resource has no asset", std::string("$.info.") + field);
        }
        if (field == std::string_view("preview_music")) {
            const auto path = asset_path(song, resource->asset, std::string("$.info.") + field);
            info[field] = retained_resource_name(resource->name, path);
        } else {
            const auto path = visual_asset_path(song, resource->asset, std::string("$.info.") + field);
            info[field] = retained_resource_name(resource->name, path);
        }
    }
    root["info"] = std::move(info);

    auto lines = json_t::array();
    for (const auto& measure : index.entries) {
        lines.push_back(json_t{{"y", to_pulse(*grid, measure.absolute_begin_row, "$.lines[].y")}});
    }
    root["lines"] = std::move(lines);

    auto bpms = json_t::array();
    std::ranges::stable_sort(bpm_values, [](const auto& left, const auto& right) {
        if (left.second != right.second) return left.second < right.second;
        return left.first->provenance.source_order < right.first->provenance.source_order;
    });
    for (const auto& [event, y] : bpm_values) {
        auto value = source_object(event->provenance);
        value["y"] = y;
        value["bpm"] = exact_number(event->beats_per_minute, "$.bpm_events[].bpm");
        bpms.push_back(std::move(value));
    }
    root["bpm_events"] = std::move(bpms);

    auto stops = json_t::array();
    for (const auto& event : chart->pauses) {
        const auto position = absolute_position(index, event.position);
        if (!position) {
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "stop event position is outside its chart measure", "$.stop_events");
        }
        const auto y = to_pulse(*grid, *position, "$.stop_events[].y");
        const auto duration = exact_nonnegative_scaled(event.duration, grid->resolution,
                                                       "$.stop_events[].duration");
        if (duration > UINT32_MAX) {
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "stop duration exceeds the BMSON 32-bit pulse range", "$.stop_events[].duration");
        }
        auto value = source_object(event.provenance);
        value["y"] = y;
        value["duration"] = static_cast<std::uint32_t>(duration);
        stops.push_back(std::move(value));
    }
    root["stop_events"] = std::move(stops);

    auto sound_channels = json_t::array();
    const auto old_channels = root.contains("sound_channels") && root["sound_channels"].is_array()
                                  ? root["sound_channels"] : json_t::array();
    std::set<std::size_t> retained_channel_indexes;
    for (std::size_t index_channel = 0; index_channel < channels.size(); ++index_channel) {
        const auto retained_index = channels[index_channel].retained_source_index;
        if (retained_index && !retained_channel_indexes.insert(*retained_index).second) {
            fail(bmson_write_diagnostic_code_t::invalid_document,
                 "multiple sound channels claim the same retained BMSON source object",
                 "$.sound_channels");
        }
        json_t channel = retained_index && *retained_index < old_channels.size() &&
                                 old_channels[*retained_index].is_object()
                             ? old_channels[*retained_index]
                             : json_t::object();
        channel["name"] = channels[index_channel].name;
        auto notes = json_t::array();
        for (const auto& source_note : channels[index_channel].notes) {
            auto note = source_object(source_note.provenance);
            note["x"] = source_note.x;
            note["y"] = source_note.y;
            if (source_note.length != 0) note["l"] = source_note.length;
            else note.erase("l");
            note["c"] = source_note.continuation;
            notes.push_back(std::move(note));
        }
        channel["notes"] = std::move(notes);
        sound_channels.push_back(std::move(channel));
    }
    root["sound_channels"] = std::move(sound_channels);

    auto bga = root.contains("bga") && root["bga"].is_object() ? root["bga"] : json_t::object();
    std::map<std::uint64_t, std::uint32_t> header_ids;
    std::set<std::uint32_t> used_header_ids;
    std::uint32_t next_header_id{};
    auto bga_resources = std::vector<const resource_entry_t*>{};
    for (const auto& resource : song.resources) {
        const auto has_header_identity = std::ranges::any_of(resource.target_identities,
            [](const auto& identity) {
                return identity.family == format_family_t::bmson && identity.namespace_name == "bga";
            });
        if (has_header_identity) bga_resources.push_back(&resource);
    }
    for (const auto& event : chart->bga_events) {
        const auto found = std::ranges::find(bga_resources, event.resource,
                                             [](const resource_entry_t* resource) { return resource->id; });
        if (found == bga_resources.end()) {
            const auto& resource = resource_for_id(song, event.resource, "$.bga");
            bga_resources.push_back(&resource);
        }
    }
    auto headers = json_t::array();
    for (const auto* resource : bga_resources) {
        auto candidate = optional_u32_identity(*resource, "bga");
        if (candidate && used_header_ids.contains(*candidate)) candidate.reset();
        if (!candidate) {
            while (used_header_ids.contains(next_header_id) && next_header_id != UINT32_MAX) ++next_header_id;
            if (used_header_ids.contains(next_header_id)) {
                fail(bmson_write_diagnostic_code_t::invalid_geometry,
                     "no BMSON BGA header IDs remain", "$.bga.bga_header");
            }
            candidate = next_header_id++;
        }
        used_header_ids.insert(*candidate);
        header_ids.emplace(resource->id.value, *candidate);
        const auto path = visual_asset_path(song, resource->asset, "$.bga.bga_header[].name");
        const auto name = retained_resource_name(resource->name, path);
        json_t header = json_t::object();
        if (bga.contains("bga_header") && bga["bga_header"].is_array()) {
            for (const auto& old : bga["bga_header"]) {
                if (old.is_object() && old.value("id", UINT32_MAX) == *candidate) {
                    header = old;
                    break;
                }
            }
        }
        header["id"] = *candidate;
        header["name"] = name;
        headers.push_back(std::move(header));
    }
    bga["bga_header"] = std::move(headers);
    auto background_events = json_t::array();
    auto layer_events = json_t::array();
    auto poor_events = json_t::array();
    std::vector<const bga_event_t*> sorted_visual;
    for (const auto& event : chart->bga_events) sorted_visual.push_back(&event);
    std::ranges::stable_sort(sorted_visual, [&](const auto* left, const auto* right) {
        const auto a = absolute_position(index, left->position).value_or(UINT64_MAX);
        const auto b = absolute_position(index, right->position).value_or(UINT64_MAX);
        if (a != b) return a < b;
        return left->provenance.source_order < right->provenance.source_order;
    });
    for (const auto* event : sorted_visual) {
        const auto position = absolute_position(index, event->position);
        if (!position) {
            fail(bmson_write_diagnostic_code_t::invalid_geometry,
                 "BGA event position is outside its chart measure", "$.bga");
        }
        const auto header = header_ids.find(event->resource.value);
        if (header == header_ids.end()) {
            fail(bmson_write_diagnostic_code_t::unresolved_resource,
                 "BGA event has no emitted resource header", "$.bga");
        }
        auto value = source_object(event->provenance);
        value["y"] = to_pulse(*grid, *position, "$.bga.*_events[].y");
        value["id"] = header->second;
        if (event->kind == visual_event_kind_t::background) background_events.push_back(std::move(value));
        else if (event->kind == visual_event_kind_t::layer) layer_events.push_back(std::move(value));
        else if (event->kind == visual_event_kind_t::poor) poor_events.push_back(std::move(value));
        else fail(bmson_write_diagnostic_code_t::unsupported_feature,
                  "BMSON 1.0 cannot encode this visual event kind", "$.bga");
    }
    bga["bga_events"] = std::move(background_events);
    bga["layer_events"] = std::move(layer_events);
    bga["poor_events"] = std::move(poor_events);
    root["bga"] = std::move(bga);

    auto output = root.dump(2, ' ', false, json_t::error_handler_t::strict);
    for (const auto& [token, number] : exact_tokens) {
        const auto quoted = json_t(token).dump();
        std::size_t offset{};
        while ((offset = output.find(quoted, offset)) != std::string::npos) {
            output.replace(offset, quoted.size(), number);
            offset += number.size();
        }
    }
    return output + "\n";
}

} // namespace

auto bmson_write_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto write_bmson(const song_document_t& song, bmson_write_options_t options)
    -> bmson_write_result_t {
    bmson_write_result_t result;
    try {
        result.json = write_impl(song, std::move(options), result.diagnostics);
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

} // namespace wormedit

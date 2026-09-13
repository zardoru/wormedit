#include <wormedit/formats/osu_mania.h>

#include <wormedit/core/rational.h>
#include <wormedit/core/timing.h>

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
#include <utility>

namespace wormedit {
namespace {

struct source_line_t {
    std::string_view text;
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
    std::int64_t time{};
    std::uint64_t type{};
    std::int64_t hit_sound{};
    std::optional<std::int64_t> end_time;
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
};

struct pending_resource_t {
    std::string path;
    asset_kind_t kind{asset_kind_t::audio};
    resource_role_t role{resource_role_t::sample};
    std::size_t order{};
    std::optional<long double> time;
    bool sample_event{};
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
};

struct parse_abort_t {};

inline constexpr long double barline_alignment_tolerance_ms = 1.0L;

auto trim(std::string_view text) -> std::string_view {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

auto valid_utf8(std::string_view source) -> std::optional<std::size_t> {
    std::size_t at{};
    while (at < source.size()) {
        const auto first = static_cast<unsigned char>(source[at]);
        if (first <= 0x7fU) {
            ++at;
            continue;
        }
        std::size_t count{};
        std::uint32_t point{};
        if (first >= 0xc2U && first <= 0xdfU) {
            count = 2;
            point = first & 0x1fU;
        } else if (first >= 0xe0U && first <= 0xefU) {
            count = 3;
            point = first & 0x0fU;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            count = 4;
            point = first & 0x07U;
        } else {
            return at;
        }
        if (count > source.size() - at)
            return at;
        for (std::size_t offset = 1; offset < count; ++offset) {
            const auto next = static_cast<unsigned char>(source[at + offset]);
            if ((next & 0xc0U) != 0x80U)
                return at + offset;
            point = (point << 6U) | (next & 0x3fU);
        }
        if ((count == 3 && point < 0x800U) || (count == 4 && point < 0x10000U) ||
            (point >= 0xd800U && point <= 0xdfffU) || point > 0x10ffffU)
            return at;
        at += count;
    }
    return std::nullopt;
}

template <typename value_t> auto parse_integer(std::string_view text, value_t& value) -> bool {
    text = trim(text);
    if (text.empty())
        return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
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

auto path_from_utf8(std::string_view value) -> std::filesystem::path {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const auto character : value)
        utf8.push_back(static_cast<char8_t>(character));
    return std::filesystem::path(std::move(utf8));
}

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
    return result.value;
}

auto checked_lcm(std::uint64_t left, std::uint64_t right) -> std::optional<std::uint64_t> {
    const auto common = std::gcd(left, right);
    if (left / common > std::numeric_limits<std::uint64_t>::max() / right)
        return std::nullopt;
    return (left / common) * right;
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

} // namespace

auto osu_mania_parse_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto parse_osu_mania_bytes(std::span<const std::byte> bytes, osu_mania_parse_options_t options)
    -> osu_mania_parse_result_t {
    osu_mania_parse_result_t result;
    const auto fail = [&](osu_mania_parse_diagnostic_code_t code, std::string message,
                          std::size_t order = 0,
                          std::optional<std::size_t> offset = std::nullopt) -> void {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, code, std::move(message), order, offset});
        throw parse_abort_t{};
    };
    try {
        if (bytes.size() > options.max_source_bytes) {
            fail(osu_mania_parse_diagnostic_code_t::source_limit,
                 "osu! source exceeds the configured byte limit");
        }
        std::string source;
        source.reserve(bytes.size());
        for (const auto byte : bytes)
            source.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
        std::size_t bom_size{};
        std::string_view text(source);
        if (text.starts_with("\xef\xbb\xbf")) {
            text.remove_prefix(3);
            bom_size = 3;
        }
        if (const auto bad = valid_utf8(text)) {
            fail(osu_mania_parse_diagnostic_code_t::invalid_encoding,
                 "osu! source must be valid UTF-8", 0, *bad + bom_size);
        }

        std::vector<source_line_t> lines;
        std::vector<field_t> fields;
        std::size_t metadata_field_count{};
        std::size_t metadata_bytes{};
        const auto account_metadata = [&](std::size_t bytes, std::size_t field_order,
                                          std::size_t field_offset) {
            if (metadata_field_count >= options.max_metadata_fields ||
                bytes > options.max_metadata_bytes ||
                metadata_bytes > options.max_metadata_bytes - bytes) {
                fail(osu_mania_parse_diagnostic_code_t::metadata_limit,
                     "osu! metadata exceeds the configured field-count or byte limit", field_order,
                     field_offset);
            }
            ++metadata_field_count;
            metadata_bytes += bytes;
        };
        std::string current_section;
        std::size_t offset{};
        std::size_t order{};
        while (offset < text.size()) {
            const auto newline = text.find('\n', offset);
            auto line = text.substr(offset, newline == std::string_view::npos ? text.size() - offset
                                                                              : newline - offset);
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            const auto trimmed = trim(line);
            if (!trimmed.empty() && !trimmed.starts_with("//")) {
                if (trimmed.front() == '[' && trimmed.back() == ']') {
                    if (trimmed.size() <= 2) {
                        fail(osu_mania_parse_diagnostic_code_t::invalid_section,
                             "section name cannot be empty", order, offset + bom_size);
                    }
                    current_section = std::string(trim(trimmed.substr(1, trimmed.size() - 2)));
                } else if (current_section.empty() && trimmed.starts_with("osu file format v")) {
                    const auto version =
                        trim(trimmed.substr(std::string_view("osu file format v").size()));
                    fields.push_back(
                        {"osu", "file_format", std::string(version), std::string(trimmed), order});
                } else {
                    lines.push_back({trimmed, current_section, order, offset + bom_size});
                    if (lines.size() > options.max_lines) {
                        fail(osu_mania_parse_diagnostic_code_t::source_limit,
                             "osu! source exceeds the configured line limit", order,
                             offset + bom_size);
                    }
                }
                ++order;
            }
            if (newline == std::string_view::npos)
                break;
            offset = newline + 1;
        }

        std::vector<source_line_t> event_lines;
        std::vector<source_line_t> timing_lines;
        std::vector<source_line_t> hit_lines;
        for (const auto& line : lines) {
            if (line.section == "Events")
                event_lines.push_back(line);
            else if (line.section == "TimingPoints")
                timing_lines.push_back(line);
            else if (line.section == "HitObjects")
                hit_lines.push_back(line);
            else {
                const auto colon = line.text.find(':');
                if (colon == std::string_view::npos) {
                    if (line.section == "General" || line.section == "Editor" ||
                        line.section == "Metadata" || line.section == "Difficulty" ||
                        line.section == "Colours") {
                        fail(osu_mania_parse_diagnostic_code_t::malformed_field,
                             "known osu! key/value sections require a colon-delimited field",
                             line.order, line.byte_offset);
                    }
                    account_metadata(line.text.size(), line.order, line.byte_offset);
                    fields.push_back({line.section, "[]", std::string(line.text),
                                      std::string(line.text), line.order});
                    continue;
                }
                account_metadata(line.text.size(), line.order, line.byte_offset);
                fields.push_back({line.section, std::string(trim(line.text.substr(0, colon))),
                                  std::string(trim(line.text.substr(colon + 1))),
                                  std::string(line.text), line.order});
            }
        }
        const auto find_value = [&](std::string_view section,
                                    std::string_view key) -> std::optional<std::string_view> {
            for (auto found = fields.rbegin(); found != fields.rend(); ++found) {
                if (found->section == section && found->key == key)
                    return found->value;
            }
            return std::nullopt;
        };
        const auto mode_text = find_value("General", "Mode");
        std::int64_t mode{};
        if (!mode_text || !parse_integer(*mode_text, mode) || mode != 3) {
            fail(osu_mania_parse_diagnostic_code_t::unsupported_mode,
                 "only osu! mode 3 (mania) is supported");
        }
        const auto keys_text = find_value("Difficulty", "CircleSize");
        std::int64_t keys{};
        if (!keys_text || !parse_integer(*keys_text, keys) || keys < 1 || keys > 18) {
            fail(osu_mania_parse_diagnostic_code_t::invalid_difficulty,
                 "CircleSize must be an integer key count from 1 through 18");
        }

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
        if (const auto bookmarks = find_value("Editor", "Bookmarks");
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
        std::vector<timing_point_t> timing_points;
        timing_points.reserve(timing_lines.size());
        for (const auto& line : timing_lines) {
            const auto parts = split(line.text, ',');
            timing_point_t point;
            std::int64_t uninherited{};
            std::uint64_t effects{};
            if (parts.size() < 8 || !parse_number(parts[0], point.time) ||
                !parse_number(parts[1], point.beat_length) ||
                !parse_integer(parts[2], point.meter) ||
                !parse_integer(parts[3], point.sample_set) ||
                !parse_integer(parts[4], point.sample_index) ||
                !parse_integer(parts[5], point.volume) || !parse_integer(parts[6], uninherited) ||
                !parse_integer(parts[7], effects) || (uninherited != 0 && uninherited != 1) ||
                point.meter < 1 || point.meter > 32 || point.beat_length == 0.0L ||
                (uninherited == 1 && point.beat_length < 0.0L) ||
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
            timing_points.push_back(std::move(point));
        }

        std::vector<hit_object_t> hit_objects;
        hit_objects.reserve(hit_lines.size());
        for (const auto& line : hit_lines) {
            const auto parts = split_csv(line.text);
            hit_object_t object;
            if (!parts || parts->size() < 5 || !parse_integer((*parts)[0], object.x) ||
                !parse_integer((*parts)[2], object.time) ||
                !parse_integer((*parts)[3], object.type) ||
                !parse_integer((*parts)[4], object.hit_sound) || object.x < 0 || object.x > 512 ||
                object.time < -86'400'000LL || object.time > 604'800'000LL || object.type == 0) {
                fail(osu_mania_parse_diagnostic_code_t::invalid_hit_object,
                     "malformed osu! hit object or out-of-range lane/time", line.order,
                     line.byte_offset);
            }
            const bool is_hold = (object.type & 128U) != 0;
            const bool is_circle = (object.type & 1U) != 0;
            if ((!is_hold && !is_circle) || (object.type & (2U | 8U)) != 0) {
                fail(osu_mania_parse_diagnostic_code_t::invalid_hit_object,
                     "osu!mania supports tap and hold objects; sliders and spinners are not "
                     "supported",
                     line.order, line.byte_offset);
            }
            if (is_hold) {
                if (parts->size() < 6) {
                    fail(osu_mania_parse_diagnostic_code_t::invalid_hit_object,
                         "hold object is missing its end-time/sample field", line.order,
                         line.byte_offset);
                }
                const auto hold_parts = split((*parts)[5], ':');
                std::int64_t end_time{};
                if (hold_parts.empty() || !parse_integer(hold_parts.front(), end_time) ||
                    end_time < object.time || end_time > 604'800'000LL) {
                    fail(osu_mania_parse_diagnostic_code_t::invalid_hit_object,
                         "hold end time must be an integer at or after its head", line.order,
                         line.byte_offset);
                }
                object.end_time = end_time;
                object.hit_sample = (*parts)[5];
            } else if (parts->size() >= 6) {
                object.hit_sample = (*parts)[5];
            }
            object.order = line.order;
            object.raw_line = std::string(line.text);
            hit_objects.push_back(std::move(object));
        }

        std::vector<pending_visual_t> visuals;
        std::vector<pending_resource_t> event_resources;
        for (const auto& line : event_lines) {
            const auto parts = split_csv(line.text);
            if (!parts || parts->empty()) {
                fail(osu_mania_parse_diagnostic_code_t::malformed_field,
                     "malformed comma-separated osu! event", line.order, line.byte_offset);
            }
            const auto& kind = parts->front();
            const bool background = kind == "0" || kind == "Background";
            const bool video = kind == "Video" || kind == "1";
            if ((background || video) && parts->size() >= 3) {
                long double event_time{};
                if (!parse_number((*parts)[1], event_time)) {
                    fail(osu_mania_parse_diagnostic_code_t::malformed_field,
                         "background/video event time must be finite", line.order,
                         line.byte_offset);
                }
                visuals.push_back(
                    {event_time, (*parts)[2], video ? asset_kind_t::video : asset_kind_t::image,
                     video ? visual_event_kind_t::video : visual_event_kind_t::background,
                     line.order});
            } else if ((kind == "Sprite" || kind == "Animation") && parts->size() >= 4) {
                event_resources.push_back({(*parts)[3], asset_kind_t::storyboard,
                                           resource_role_t::bga, line.order, std::nullopt, false});
            } else if (kind == "Sample" && parts->size() >= 4) {
                long double sample_time{};
                if (!parse_number((*parts)[1], sample_time)) {
                    fail(osu_mania_parse_diagnostic_code_t::malformed_field,
                         "sample event time must be finite", line.order, line.byte_offset);
                }
                event_resources.push_back({(*parts)[3], asset_kind_t::audio,
                                           resource_role_t::sample, line.order, sample_time, true});
            }
        }

        std::vector<timing_point_t> effective_points;
        for (const auto& point : timing_points)
            if (point.uninherited)
                effective_points.push_back(point);
        std::ranges::stable_sort(effective_points, [](const auto& left, const auto& right) {
            if (left.time != right.time)
                return left.time < right.time;
            return left.order < right.order;
        });
        if (effective_points.empty()) {
            fail(osu_mania_parse_diagnostic_code_t::missing_timing_anchor,
                 "chart has no uninherited timing point from which to derive bar phase");
        }

        std::vector<long double> event_times;
        event_times.reserve(hit_objects.size() * 2 + visuals.size() + 1);
        for (const auto& object : hit_objects) {
            event_times.push_back(static_cast<long double>(object.time));
            if (object.end_time)
                event_times.push_back(static_cast<long double>(*object.end_time));
        }
        for (const auto& visual : visuals)
            event_times.push_back(visual.time);
        for (const auto& point : timing_points)
            event_times.push_back(point.time);
        if (event_times.empty())
            event_times.push_back(effective_points.front().time);
        const auto [minimum_event, maximum_event] =
            std::minmax_element(event_times.begin(), event_times.end());

        // Equal-time red points are one bar boundary. The last source-ordered point
        // supplies the phase and tempo for the interval after that boundary.
        std::vector<timing_point_t> anchors;
        for (const auto& point : effective_points) {
            if (!anchors.empty() && anchors.back().time == point.time)
                anchors.back() = point;
            else
                anchors.push_back(point);
        }
        std::vector<timeline_line_t> timeline;
        const auto period_for = [](const timing_point_t& point) {
            return point.beat_length * static_cast<long double>(point.meter);
        };
        const auto add_line = [&](long double time, const timing_point_t& point, bool anchor) {
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
        };
        const auto first_period = period_for(anchors.front());
        if (!std::isfinite(first_period) || first_period <= 0.0L) {
            fail(osu_mania_parse_diagnostic_code_t::invalid_timing_point,
                 "first effective timing point has an invalid bar duration", anchors.front().order);
        }
        std::vector<long double> prefix_lines;
        auto backward = anchors.front().time;
        std::size_t backwards{};
        while (backward > *minimum_event) {
            backward -= first_period;
            prefix_lines.push_back(backward);
            if (++backwards > options.max_measures) {
                fail(osu_mania_parse_diagnostic_code_t::measure_limit,
                     "negative-time objects require more measures than the configured limit");
            }
        }
        if (prefix_lines.empty() && (anchors.front().effects & 8U) != 0) {
            backward -= first_period;
            prefix_lines.push_back(backward);
        }
        for (auto iterator = prefix_lines.rbegin(); iterator != prefix_lines.rend(); ++iterator)
            add_line(*iterator, anchors.front(), false);
        if ((anchors.front().effects & 8U) == 0)
            add_line(anchors.front().time, anchors.front(), true);

        std::vector<std::pair<std::size_t, long double>> suppressed_lines;
        std::size_t measure_count = timeline.size();
        for (std::size_t anchor_index = 0; anchor_index < anchors.size(); ++anchor_index) {
            const auto& anchor = anchors[anchor_index];
            const auto next_anchor =
                anchor_index + 1 < anchors.size()
                    ? std::optional<long double>(anchors[anchor_index + 1].time)
                    : std::nullopt;
            const auto period = period_for(anchor);
            auto candidate = anchor.time + period;
            if (!std::isfinite(candidate) || candidate <= anchor.time) {
                fail(osu_mania_parse_diagnostic_code_t::invalid_timing_point,
                     "timing point produces a non-positive or overflowing measure", anchor.order);
            }
            if (next_anchor) {
                std::size_t generated{};
                const bool old_barline_is_suppressed =
                    candidate >= *next_anchor - 1.0e-12L &&
                    candidate - *next_anchor <= barline_alignment_tolerance_ms;
                if (old_barline_is_suppressed) {
                    suppressed_lines.emplace_back(anchor.order, candidate - *next_anchor);
                } else {
                    while (candidate < *next_anchor - 1.0e-12L) {
                        add_line(candidate, anchor, false);
                        candidate += period;
                        if (!std::isfinite(candidate) || ++generated > options.max_measures) {
                            fail(osu_mania_parse_diagnostic_code_t::measure_limit,
                                 "timing-point span exceeds the configured measure limit",
                                 anchor.order);
                        }
                    }
                }
                if ((anchors[anchor_index + 1].effects & 8U) == 0)
                    add_line(*next_anchor, anchors[anchor_index + 1], true);
            } else {
                std::size_t generated{};
                while (candidate <= *maximum_event + 1.0e-12L) {
                    add_line(candidate, anchor, false);
                    candidate += period;
                    if (!std::isfinite(candidate) || ++generated > options.max_measures) {
                        fail(osu_mania_parse_diagnostic_code_t::measure_limit,
                             "chart tail exceeds the configured measure limit", anchor.order);
                    }
                }
                add_line(candidate, anchor, false);
            }
            measure_count = timeline.size();
            if (measure_count > options.max_measures + 1) {
                fail(osu_mania_parse_diagnostic_code_t::measure_limit,
                     "chart exceeds the configured measure limit", anchor.order);
            }
        }
        if (timeline.size() < 2) {
            const auto& last = anchors.back();
            add_line(last.time + period_for(last), last, false);
        }

        // An omitted-first-barline reset can sit inside a measure boundary span.
        // Integrate against every effective timing anchor so measure lengths and
        // positions share the same piecewise tempo geometry.
        const auto quarter_beats_between = [&](long double begin, long double end) {
            std::size_t active_anchor{};
            std::size_t next_anchor{};
            for (std::size_t index = 0; index < anchors.size(); ++index) {
                if (anchors[index].time <= begin) {
                    active_anchor = index;
                    next_anchor = index + 1;
                } else {
                    next_anchor = index;
                    break;
                }
            }
            auto cursor = begin;
            long double beat_count{};
            for (std::size_t index = next_anchor; index < anchors.size(); ++index) {
                const auto& anchor = anchors[index];
                if (anchor.time >= end)
                    break;
                if (anchor.time > cursor) {
                    beat_count += (anchor.time - cursor) / anchors[active_anchor].beat_length;
                    cursor = anchor.time;
                }
                active_anchor = index;
            }
            if (end > cursor)
                beat_count += (end - cursor) / anchors[active_anchor].beat_length;
            return beat_count;
        };

        std::vector<rational_t> measure_lengths;
        measure_lengths.reserve(timeline.size() - 1);
        std::uint64_t divisions = default_divisions_per_standard_measure;
        for (std::size_t index = 0; index + 1 < timeline.size(); ++index) {
            const auto& begin = timeline[index];
            const auto duration = timeline[index + 1].time - begin.time;
            const auto quarter_beats = quarter_beats_between(begin.time, timeline[index + 1].time);
            if (!std::isfinite(duration) || duration <= 0.0L || !std::isfinite(quarter_beats) ||
                quarter_beats <= 0.0L) {
                fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                     "measure timeline contains a non-positive interval");
            }
            const auto length = rationalize(quarter_beats / 4.0L);
            if (!length) {
                fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                     "measure duration cannot be represented as a bounded exact ratio");
            }
            const auto required = checked_lcm(divisions, length->denominator);
            if (!required || *required > 1'000'000'000'000ULL) {
                fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                     "osu! timing requires an editor division count beyond the supported bound");
            }
            divisions = *required;
            measure_lengths.push_back(*length);
        }

        song_document_t song;
        song.id = song_id_t{1};
        chart_document_t chart;
        chart.id = chart_id_t{1};
        chart.source_family = format_family_t::osu_mania;
        chart.divisions_per_standard_measure = divisions;
        const auto source_bytes =
            std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
        format_metadata_t source_group;
        source_group.family = format_family_t::osu_mania;
        source_group.encoding.detected = text_encoding_t::utf8;
        source_group.encoding.original_bytes = source_bytes;
        source_group.encoding.unchanged_since_import = true;
        const auto retain_field = [&](std::string key, std::string value, std::size_t source_order,
                                      bool has_source_identity = true) {
            source_group.ordered_fields.emplace_back(std::move(key), std::move(value));
            if (has_source_identity)
                source_group.ordered_field_source_orders.push_back(source_order);
        };
        struct retained_source_entry_t {
            std::size_t order{};
            std::string key;
            std::string value;
        };
        std::vector<retained_source_entry_t> retained_entries;
        retained_entries.reserve(fields.size() + event_lines.size() + timing_lines.size() +
                                 hit_lines.size());
        for (const auto& field : fields) {
            retained_entries.push_back({field.order, field.section + "." + field.key, field.value});
        }
        for (const auto& line : event_lines)
            retained_entries.push_back({line.order, "Events[]", std::string(line.text)});
        for (const auto& line : timing_lines)
            retained_entries.push_back({line.order, "TimingPoints[]", std::string(line.text)});
        for (const auto& line : hit_lines)
            retained_entries.push_back({line.order, "HitObjects[]", std::string(line.text)});
        std::ranges::stable_sort(retained_entries, {}, &retained_source_entry_t::order);
        for (auto& entry : retained_entries)
            retain_field(std::move(entry.key), std::move(entry.value), entry.order);

        const auto value_or_empty = [&](std::string_view section, std::string_view key) {
            const auto found = find_value(section, key);
            return found ? std::string(*found) : std::string{};
        };
        song.metadata.title = value_or_empty("Metadata", "Title");
        if (song.metadata.title.empty())
            song.metadata.title = value_or_empty("Metadata", "TitleUnicode");
        song.metadata.artist = value_or_empty("Metadata", "Artist");
        if (song.metadata.artist.empty())
            song.metadata.artist = value_or_empty("Metadata", "ArtistUnicode");
        if (const auto tags = find_value("Metadata", "Tags")) {
            std::string_view remaining = *tags;
            while (!remaining.empty()) {
                remaining = trim(remaining);
                if (remaining.empty())
                    break;
                const auto end = remaining.find_first_of(" \t");
                song.metadata.ordered_search_tags.emplace_back(remaining.substr(0, end));
                if (end == std::string_view::npos)
                    break;
                remaining.remove_prefix(end);
            }
        }
        chart.metadata.name = value_or_empty("Metadata", "Version");
        chart.metadata.author = value_or_empty("Metadata", "Creator");
        chart.metadata.format_groups.push_back(source_group);
        song.metadata.format_groups.push_back(source_group);
        id_allocator_t object_ids;
        const auto take_object_id = [&](std::size_t source_order) -> object_id_t {
            const auto id = allocate_id<object_id_t>(object_ids);
            if (!id)
                fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                     "chart object ID space is exhausted", source_order);
            return *id;
        };
        chart.measures.reserve(measure_lengths.size());
        std::vector<measure_geometry_t> geometries;
        geometries.reserve(measure_lengths.size());
        for (std::size_t index = 0; index < measure_lengths.size(); ++index) {
            const auto capacity = validate_measure(divisions, {.length = measure_lengths[index]});
            if (!capacity) {
                fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                     "derived osu! measure exceeds the editor's exact row capacity");
            }
            measure_chunk_t measure;
            measure.id = measure_id_t{static_cast<std::uint64_t>(index + 1)};
            measure.definition = {.length = measure_lengths[index], .meter = std::nullopt};
            const auto& line = timeline[index];
            const auto expected = static_cast<long double>(line.meter_numerator) /
                                  static_cast<long double>(line.meter_denominator);
            const auto actual = static_cast<long double>(measure_lengths[index].numerator) /
                                static_cast<long double>(measure_lengths[index].denominator);
            const bool crosses_timing_point = std::ranges::any_of(anchors, [&](const auto& anchor) {
                return anchor.time > line.time && anchor.time < timeline[index + 1].time;
            });
            if (!crosses_timing_point && std::fabs(actual - expected) < 1.0e-9L &&
                line.meter_numerator > 0 && line.meter_denominator > 0) {
                measure.definition.meter =
                    meter_t{static_cast<std::uint32_t>(line.meter_numerator),
                            static_cast<std::uint32_t>(line.meter_denominator)};
            }
            chart.measures.push_back(std::move(measure));
            geometries.push_back({line.time, timeline[index + 1].time, *capacity.row_capacity});
        }
        if (chart.measures.empty()) {
            fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                 "osu! timing did not produce an editable measure");
        }

        const auto position_at = [&](long double time) -> std::optional<local_position_t> {
            const auto found =
                std::upper_bound(geometries.begin(), geometries.end(), time,
                                 [](long double value, const measure_geometry_t& measure) {
                                     return value < measure.begin;
                                 });
            if (found == geometries.begin())
                return std::nullopt;
            const auto index =
                static_cast<std::size_t>(std::distance(geometries.begin(), found) - 1);
            if (index >= geometries.size() || time < geometries[index].begin - 1.0e-12L ||
                time >= geometries[index].end + 1.0e-12L)
                return std::nullopt;
            const auto total_beats =
                quarter_beats_between(geometries[index].begin, geometries[index].end);
            auto fraction = quarter_beats_between(geometries[index].begin, time) / total_beats;
            if (fraction < 0.0L && fraction > -1.0e-12L)
                fraction = 0.0L;
            auto row = static_cast<std::uint64_t>(
                std::llround(fraction * static_cast<long double>(geometries[index].capacity)));
            if (row >= geometries[index].capacity)
                row = geometries[index].capacity - 1;
            return local_position_t{chart.measures[index].id, row};
        };

        lane_layout_t layout;
        layout.profile_id = "osu-mania-" + std::to_string(keys) + "k";
        layout.display_name = std::to_string(keys) + "K";
        layout.lanes.reserve(static_cast<std::size_t>(keys));
        for (std::int64_t column = 0; column < keys; ++column) {
            layout.lanes.push_back({.id = lane_id_t{static_cast<std::uint64_t>(column + 1)},
                                    .name = "Key " + std::to_string(column + 1),
                                    .role = lane_role_t::playable,
                                    .display_order = static_cast<std::uint32_t>(column)});
        }
        chart.lane_layouts.push_back({.id = take_object_id(0),
                                      .position = {chart.measures.front().id, 0},
                                      .layout = std::move(layout)});

        std::unordered_map<std::string, resource_id_t> audio_resources;
        std::unordered_map<std::string, resource_id_t> visual_resources;
        id_allocator_t asset_ids;
        id_allocator_t resource_ids;
        const auto resource_is_missing = [&](const std::filesystem::path& path) {
            if (!options.asset_base_directory)
                return true;
            std::error_code error;
            return !std::filesystem::exists(*options.asset_base_directory / path, error) || error;
        };
        const auto add_audio = [&](std::string_view name, resource_role_t role,
                                   std::string source_field,
                                   std::size_t source_order) -> std::optional<resource_id_t> {
            const auto path = safe_relative_path(name);
            if (!path) {
                fail(osu_mania_parse_diagnostic_code_t::invalid_resource_path,
                     "osu! resource path must be a safe relative path", source_order);
            }
            const auto key = path->generic_string();
            if (const auto found = audio_resources.find(key); found != audio_resources.end()) {
                const auto existing =
                    std::ranges::find(song.resources, found->second, &resource_entry_t::id);
                if (existing != song.resources.end() &&
                    std::ranges::find(existing->roles, role) == existing->roles.end()) {
                    existing->roles.push_back(role);
                }
                return found->second;
            }
            const auto asset = allocate_id<asset_id_t>(asset_ids);
            const auto resource = allocate_id<resource_id_t>(resource_ids);
            if (!asset || !resource)
                fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                     "resource ID space is exhausted", source_order);
            song.audio_assets.push_back({.id = *asset, .source_path = *path});
            song.resources.push_back(
                {.id = *resource,
                 .name = std::string(name),
                 .asset = *asset,
                 .roles = {role},
                 .target_identities = {{format_family_t::osu_mania, std::string(name),
                                        "audio-path"}},
                 .missing = resource_is_missing(*path),
                 .provenance = {format_family_t::osu_mania, "osu-mania", std::move(source_field),
                                std::string(name), source_order}});
            audio_resources.emplace(key, *resource);
            return *resource;
        };
        const auto add_visual = [&](std::string_view name, asset_kind_t kind, resource_role_t role,
                                    std::string source_field,
                                    std::size_t source_order) -> std::optional<resource_id_t> {
            const auto path = safe_relative_path(name);
            if (!path) {
                fail(osu_mania_parse_diagnostic_code_t::invalid_resource_path,
                     "osu! visual resource path must be a safe relative path", source_order);
            }
            const auto key = path->generic_string();
            if (const auto found = visual_resources.find(key); found != visual_resources.end())
                return found->second;
            const auto asset = allocate_id<asset_id_t>(asset_ids);
            const auto resource = allocate_id<resource_id_t>(resource_ids);
            if (!asset || !resource)
                fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                     "resource ID space is exhausted", source_order);
            song.visual_assets.push_back({.id = *asset, .kind = kind, .source_path = *path});
            song.resources.push_back(
                {.id = *resource,
                 .name = std::string(name),
                 .asset = *asset,
                 .roles = {role},
                 .target_identities = {{format_family_t::osu_mania, std::string(name),
                                        "visual-path"}},
                 .missing = resource_is_missing(*path),
                 .provenance = {format_family_t::osu_mania, "osu-mania", std::move(source_field),
                                std::string(name), source_order}});
            visual_resources.emplace(key, *resource);
            return *resource;
        };

        if (const auto audio = find_value("General", "AudioFilename"); audio && !audio->empty()) {
            add_audio(*audio, resource_role_t::music, "General.AudioFilename", 0);
        }
        std::vector<std::optional<resource_id_t>> hit_samples;
        hit_samples.reserve(hit_objects.size());
        for (const auto& object : hit_objects) {
            std::optional<resource_id_t> sound;
            if (!object.hit_sample.empty()) {
                const auto sample_parts = split(object.hit_sample, ':');
                const auto filename_index = object.end_time ? std::size_t{5} : std::size_t{4};
                if (sample_parts.size() > filename_index && !sample_parts[filename_index].empty()) {
                    sound = add_audio(sample_parts[filename_index], resource_role_t::sample,
                                      "HitObjects.hitSample", object.order);
                }
            }
            hit_samples.push_back(sound);
        }
        std::vector<std::optional<resource_id_t>> visual_event_resources;
        visual_event_resources.reserve(visuals.size());
        for (const auto& visual : visuals) {
            visual_event_resources.push_back(
                add_visual(visual.path, visual.kind, resource_role_t::bga, "Events", visual.order));
        }
        std::optional<object_id_t> sample_source_lane;
        if (std::ranges::any_of(event_resources,
                                [](const auto& item) { return item.sample_event; })) {
            sample_source_lane = take_object_id(0);
            chart.bgm_source_lanes.push_back(
                {.id = *sample_source_lane, .name = "osu! samples", .source_order = 0});
        }
        for (const auto& event_resource : event_resources) {
            if (event_resource.kind == asset_kind_t::audio) {
                const auto sound = add_audio(event_resource.path, event_resource.role,
                                             "Events.resource", event_resource.order);
                if (event_resource.sample_event && event_resource.time && sound &&
                    sample_source_lane) {
                    const auto position = position_at(*event_resource.time);
                    if (!position) {
                        fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                             "sample event falls outside the derived measure timeline",
                             event_resource.order);
                    }
                    chart.measures[static_cast<std::size_t>(position->measure.value - 1)]
                        .bgm_events.push_back(
                            {.id = take_object_id(event_resource.order),
                             .position = *position,
                             .source_lane = *sample_source_lane,
                             .sound = *sound,
                             .provenance = {format_family_t::osu_mania, "osu-mania", "Events",
                                            event_resource.path, event_resource.order}});
                }
            } else {
                add_visual(event_resource.path, event_resource.kind, event_resource.role,
                           "Events.resource", event_resource.order);
            }
        }

        for (std::size_t index = 0; index < hit_objects.size(); ++index) {
            const auto& source_object = hit_objects[index];
            const auto position = position_at(static_cast<long double>(source_object.time));
            const auto end = source_object.end_time
                                 ? position_at(static_cast<long double>(*source_object.end_time))
                                 : std::nullopt;
            if (!position || (source_object.end_time && !end)) {
                fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                     "hit object falls outside the derived measure timeline", source_object.order);
            }
            const auto id = take_object_id(source_object.order);
            const auto column = std::min(static_cast<std::uint64_t>(source_object.x * keys / 512),
                                         static_cast<std::uint64_t>(keys - 1));
            note_t note;
            note.id = id;
            note.position = *position;
            note.lane = lane_id_t{column + 1};
            note.kind = source_object.end_time ? note_kind_t::hold : note_kind_t::tap;
            note.end = end;
            note.sound = hit_samples[index];
            note.provenance = {format_family_t::osu_mania, "osu-mania", "HitObjects",
                               source_object.raw_line, source_object.order};
            chart.measures[static_cast<std::size_t>(position->measure.value - 1)].notes.push_back(
                note);
            retain_field("osu.hitobject." + std::to_string(id.value) + ".time",
                         std::to_string(source_object.time), source_object.order, false);
            if (source_object.end_time) {
                retain_field("osu.hitobject." + std::to_string(id.value) + ".end_time",
                             std::to_string(*source_object.end_time), source_object.order, false);
            }
        }

        for (const auto& point : timing_points) {
            const auto position = position_at(point.time);
            if (!position) {
                fail(osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                     "timing point falls outside the derived measure timeline", point.order);
            }
            const source_provenance_t provenance{format_family_t::osu_mania, "osu-mania",
                                                 "TimingPoints", point.raw_line, point.order};
            if (point.uninherited) {
                const auto bpm = rationalize(60'000.0L / point.beat_length);
                if (!bpm || bpm->numerator > static_cast<std::uint64_t>(INT64_MAX)) {
                    fail(osu_mania_parse_diagnostic_code_t::invalid_timing_point,
                         "BPM cannot be represented by the exact editor timing model", point.order);
                }
                chart.bpms.push_back(
                    {.id = take_object_id(point.order),
                     .position = *position,
                     .beats_per_minute = {static_cast<std::int64_t>(bpm->numerator),
                                          bpm->denominator},
                     .provenance = provenance});
            } else {
                const auto multiplier = rationalize(100.0L / -point.beat_length);
                if (!multiplier || multiplier->numerator > static_cast<std::uint64_t>(INT64_MAX)) {
                    fail(osu_mania_parse_diagnostic_code_t::invalid_timing_point,
                         "inherited scroll multiplier cannot be represented exactly", point.order);
                }
                chart.scrolls.push_back(
                    {.id = take_object_id(point.order),
                     .position = *position,
                     .multiplier = {static_cast<std::int64_t>(multiplier->numerator),
                                    multiplier->denominator},
                     .provenance = provenance});
            }
        }
        auto ordered_points = timing_points;
        std::ranges::stable_sort(ordered_points, [](const auto& left, const auto& right) {
            if (left.time != right.time)
                return left.time < right.time;
            return left.order < right.order;
        });
        std::optional<std::uint32_t> prior_effects;
        for (const auto& point : ordered_points) {
            if (prior_effects && *prior_effects == point.effects)
                continue;
            const auto position = position_at(point.time);
            if (!position)
                continue;
            chart.kiai.push_back(
                {.id = take_object_id(point.order),
                 .position = *position,
                 .enabled = (point.effects & 1U) != 0,
                 .retained_effect_bits = point.effects,
                 .provenance = {format_family_t::osu_mania, "osu-mania", "TimingPoints.effects",
                                point.raw_line, point.order}});
            prior_effects = point.effects;
        }
        for (const auto& [source_order, displacement] : suppressed_lines) {
            retain_field("osu.suppressed_bar_line_displacement_ms", format_double(displacement),
                         source_order, false);
        }
        for (std::size_t index = 0; index < visuals.size(); ++index) {
            const auto position = position_at(visuals[index].time);
            if (!position || !visual_event_resources[index])
                continue;
            chart.bga_events.push_back(
                {.id = take_object_id(visuals[index].order),
                 .position = *position,
                 .resource = *visual_event_resources[index],
                 .kind = visuals[index].event_kind,
                 .provenance = {format_family_t::osu_mania, "osu-mania", "Events",
                                visuals[index].path, visuals[index].order}});
        }
        if (const auto bookmark_text = find_value("Editor", "Bookmarks")) {
            for (const auto bookmark_value : split(*bookmark_text, ',')) {
                std::int64_t milliseconds{};
                if (!parse_integer(bookmark_value, milliseconds)) {
                    fail(osu_mania_parse_diagnostic_code_t::malformed_field,
                         "Editor Bookmarks must be a comma-separated list of integer milliseconds");
                }
                chart.bookmarks.push_back({.id = take_object_id(0),
                                           .label = {},
                                           .anchor = marker_anchor_t::audio_time,
                                           .audio_milliseconds = milliseconds});
            }
        }

        // Resource paths, storyboard/event rows and every unnormalized metadata
        // field remain addressable in the ordered source group above.
        chart.metadata.format_groups.front().ordered_fields = source_group.ordered_fields;
        chart.metadata.format_groups.front().ordered_field_source_orders =
            source_group.ordered_field_source_orders;
        song.metadata.format_groups.front().ordered_fields = source_group.ordered_fields;
        song.metadata.format_groups.front().ordered_field_source_orders =
            source_group.ordered_field_source_orders;

        song.charts.push_back(std::move(chart));
        result.song = std::move(song);
    } catch (const parse_abort_t&) {
        result.song.reset();
    } catch (const std::exception& error) {
        result.song.reset();
        result.diagnostics.push_back({diagnostic_severity_t::error,
                                      osu_mania_parse_diagnostic_code_t::internal_conversion_error,
                                      std::string("osu! import failed atomically: ") + error.what(),
                                      0, std::nullopt});
    } catch (...) {
        result.song.reset();
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

struct passthrough_field_t {
    std::string section;
    std::string line;
};

auto rational_value(signed_rational_t value) -> std::optional<long double> {
    if (value.denominator == 0)
        return std::nullopt;
    const auto result =
        static_cast<long double>(value.numerator) / static_cast<long double>(value.denominator);
    if (!std::isfinite(result))
        return std::nullopt;
    return result;
}

auto path_utf8(const std::filesystem::path& path) -> std::string {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

auto writer_safe_text(std::string_view value) -> bool {
    return value.find('\0') == std::string_view::npos &&
           value.find('\n') == std::string_view::npos &&
           value.find('\r') == std::string_view::npos && !valid_utf8(value).has_value();
}

auto writer_safe_key(std::string_view value) -> bool {
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
    const auto rounded = std::round(value); // half ties move away from zero
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
            std::ranges::find(song.audio_assets, resource->asset, &audio_asset_t::id);
        if (asset == song.audio_assets.end())
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
    if (!safe || !writer_safe_text(name))
        return std::nullopt;
    return safe->generic_string();
}

struct retained_osu_note_times_t {
    std::optional<std::int64_t> head;
    std::optional<std::int64_t> tail;
};

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
            std::int64_t time{};
            if (parse_integer(id_text, id) && parse_integer(value, time)) {
                auto& retained = values[id];
                if (is_tail)
                    retained.tail = time;
                else
                    retained.head = time;
            }
        }
    }
    return values;
}

auto passthrough_fields(const chart_document_t& chart, const song_document_t& song,
                        std::vector<osu_mania_write_diagnostic_t>& diagnostics)
    -> std::vector<passthrough_field_t> {
    std::vector<passthrough_field_t> result;
    std::vector<const format_metadata_t*> groups;
    for (const auto& group : song.metadata.format_groups)
        if (group.family == format_family_t::osu_mania)
            groups.push_back(&group);
    for (const auto& group : chart.metadata.format_groups)
        if (group.family == format_family_t::osu_mania)
            groups.push_back(&group);
    struct retained_field_t {
        const std::vector<std::byte>* source{};
        std::size_t order{};
        std::pair<std::string, std::string> field;
    };
    std::vector<retained_field_t> retained_fields;
    const auto omit_unsafe = [&](std::string_view field) {
        diagnostics.push_back({.severity = diagnostic_severity_t::warning,
                               .code = osu_mania_write_diagnostic_code_t::invalid_metadata,
                               .message = "unsafe retained osu! metadata was omitted",
                               .field = std::string(field)});
    };
    for (const auto* group : groups)
        for (std::size_t index = 0; index < group->ordered_fields.size(); ++index) {
            const auto& [identity, value] = group->ordered_fields[index];
            if (index < group->ordered_field_source_orders.size() &&
                group->encoding.original_bytes) {
                const auto source_order = group->ordered_field_source_orders[index];
                const auto known = std::ranges::find_if(retained_fields, [&](const auto& field) {
                    return field.source == group->encoding.original_bytes.get() &&
                           field.order == source_order;
                });
                if (known != retained_fields.end()) {
                    if (known->field != group->ordered_fields[index]) {
                        diagnostics.push_back(
                            {.severity = diagnostic_severity_t::error,
                             .code = osu_mania_write_diagnostic_code_t::invalid_metadata,
                             .message = "conflicting scoped copies of one retained osu! field",
                             .field = identity});
                    }
                    continue;
                }
                retained_fields.push_back({group->encoding.original_bytes.get(), source_order,
                                           group->ordered_fields[index]});
            }
            const auto dot = identity.find('.');
            if (dot == std::string::npos || identity.starts_with("osu."))
                continue;
            const auto section = std::string_view{identity}.substr(0, dot);
            const auto key = std::string_view{identity}.substr(dot + 1);
            if (section == "HitObjects" || section == "TimingPoints")
                continue;
            if (section == "Events") {
                if (key != "[]" || !writer_safe_text(value) || value.empty() ||
                    value.front() == '[') {
                    if (key == "[]")
                        omit_unsafe(identity);
                    continue;
                }
                const auto comma = value.find(',');
                const auto kind = std::string_view{value}.substr(0, comma);
                if (kind == "0" || kind == "Background" || kind == "1" || kind == "Video" ||
                    kind == "Sample")
                    continue;
                result.push_back({"Events", value});
                continue;
            }
            if (key == "[]") {
                if (section.empty() || !writer_safe_key(section) || !writer_safe_text(value) ||
                    value.empty() || value.front() == '[') {
                    omit_unsafe(identity);
                    continue;
                }
                result.push_back({std::string(section), value});
                continue;
            }
            if (!writer_safe_key(section) || !writer_safe_key(key) || !writer_safe_text(value)) {
                omit_unsafe(identity);
                continue;
            }
            if ((section == "General" && (key == "Mode" || key == "AudioFilename")) ||
                (section == "Editor" && key == "Bookmarks") ||
                (section == "Difficulty" && key == "CircleSize") ||
                (section == "Metadata" && (key == "Title" || key == "Artist" || key == "Creator" ||
                                           key == "Version" || key == "Tags")))
                continue;
            result.push_back({std::string(section), std::string(key) + ":" + value});
        }
    return result;
}

} // namespace

auto osu_mania_write_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto write_osu_mania(const song_document_t& song, osu_mania_write_options_t options)
    -> osu_mania_write_result_t {
    osu_mania_write_result_t result;
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
        const chart_document_t* chart{};
        if (options.chart) {
            const auto found =
                std::ranges::find(song.charts, *options.chart, &chart_document_t::id);
            if (found == song.charts.end())
                fail(osu_mania_write_diagnostic_code_t::missing_chart,
                     "requested chart ID does not exist in this song", "charts");
            chart = &*found;
        } else if (song.charts.size() == 1) {
            chart = &song.charts.front();
        } else {
            fail(osu_mania_write_diagnostic_code_t::missing_chart,
                 "select one chart because osu! stores one chart per .osu file", "charts");
        }

        const auto validation = validate_document(song);
        if (const auto invalid = std::ranges::find_if(validation,
                                                      [](const auto& diagnostic) {
                                                          return diagnostic.severity ==
                                                                 diagnostic_severity_t::error;
                                                      });
            invalid != validation.end()) {
            fail(osu_mania_write_diagnostic_code_t::invalid_document,
                 "document failed core validation: " + invalid->message, "chart");
        }
        if (chart->measures.empty())
            fail(osu_mania_write_diagnostic_code_t::invalid_document,
                 "osu!mania requires at least one chart measure", "measures");
        if (chart->measures.size() > options.max_measures)
            fail(osu_mania_write_diagnostic_code_t::output_limit,
                 "chart measure count exceeds the configured export bound", "measures");
        if (!chart->pauses.empty() || !chart->warps.empty() || !chart->speeds.empty() ||
            !chart->sections.empty() || !chart->timing_anchors.empty() || chart->bms_control_tree ||
            !chart->active_branch_selection.empty()) {
            fail(
                osu_mania_write_diagnostic_code_t::unsupported_feature,
                "osu!mania cannot represent pauses, warps, speed curves, sections, timing anchors, "
                "or conditional branches",
                "chart");
        }
        if (chart->beat_zero_audio_offset_seconds.denominator == 0) {
            fail(osu_mania_write_diagnostic_code_t::invalid_document,
                 "beat-zero audio offset has a zero denominator", "beat_zero_audio_offset_seconds");
        }
        if (chart->semantic_layers.size() != 0 ||
            std::ranges::any_of(chart->measures, [](const auto& measure) {
                return std::ranges::any_of(
                    measure.notes, [](const auto& note) { return !note.semantic_layers.empty(); });
            })) {
            warn(osu_mania_write_diagnostic_code_t::lossy_transform,
                 "semantic note layers were flattened while preserving each note's gameplay kind",
                 "semantic_layers");
        }

        const auto built_measure_index = build_measure_index(*chart);
        if (!std::holds_alternative<measure_index_t>(built_measure_index))
            fail(osu_mania_write_diagnostic_code_t::invalid_document,
                 "chart measure geometry is invalid", "measures");
        const auto& measures = std::get<measure_index_t>(built_measure_index);
        if (chart->divisions_per_standard_measure == 0 || measures.total_rows == 0)
            fail(osu_mania_write_diagnostic_code_t::invalid_document,
                 "chart must have a positive row resolution", "divisions_per_standard_measure");
        for (const auto& entry : measures.entries)
            if (entry.row_capacity > options.max_rows_per_measure)
                fail(osu_mania_write_diagnostic_code_t::output_limit,
                     "measure row capacity exceeds the configured export bound", "measures");

        if (chart->lane_layouts.empty())
            fail(osu_mania_write_diagnostic_code_t::invalid_layout,
                 "osu!mania export requires an explicit lane layout", "lane_layouts");
        std::vector<lane_id_t> ordered_lane_ids;
        const auto normalize_layout = [&](const lane_layout_t& layout) {
            if (layout.lanes.empty() || layout.lanes.size() > 18)
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
        };
        ordered_lane_ids = normalize_layout(chart->lane_layouts.front().layout);
        for (const auto& segment : chart->lane_layouts)
            if (normalize_layout(segment.layout) != ordered_lane_ids)
                fail(osu_mania_write_diagnostic_code_t::invalid_layout,
                     "osu!mania requires one stable lane order for the entire chart",
                     "lane_layouts");
        const auto keys = ordered_lane_ids.size();
        const auto lane_column = [&](lane_id_t lane) -> std::optional<std::size_t> {
            const auto found = std::ranges::find(ordered_lane_ids, lane);
            if (found == ordered_lane_ids.end())
                return std::nullopt;
            return static_cast<std::size_t>(std::distance(ordered_lane_ids.begin(), found));
        };

        std::vector<positioned_bpm_write_t> bpms;
        bpms.reserve(chart->bpms.size());
        for (std::size_t index = 0; index < chart->bpms.size(); ++index) {
            const auto& event = chart->bpms[index];
            const auto row = absolute_row(measures, event.position);
            if (!row)
                fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                     "BPM event lies outside chart geometry", "bpms");
            if (event.beats_per_minute.numerator <= 0 || event.beats_per_minute.denominator == 0)
                fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                     "osu! timing requires positive BPM values", "bpms");
            bpms.push_back({*row, &event, index});
        }
        std::ranges::stable_sort(bpms, [](const auto& left, const auto& right) {
            return std::tuple{left.row, left.event->provenance.source_order, left.insertion_order} <
                   std::tuple{right.row, right.event->provenance.source_order,
                              right.insertion_order};
        });
        if (bpms.empty())
            fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                 "osu!mania requires at least one BPM event", "bpms");
        std::vector<std::pair<std::uint64_t, signed_rational_t>> bpm_groups;
        for (const auto& bpm : bpms) {
            if (!bpm_groups.empty() && bpm_groups.back().first == bpm.row)
                bpm_groups.back().second = bpm.event->beats_per_minute;
            else
                bpm_groups.emplace_back(bpm.row, bpm.event->beats_per_minute);
        }
        const auto bpm_value = [&](signed_rational_t value) -> long double {
            const auto rational = rational_value(value);
            if (!rational || *rational <= 0.0L)
                fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                     "osu! timing requires finite positive BPM values", "bpms");
            return *rational;
        };
        const auto bpm_index_before_or_at = [&](std::uint64_t row) {
            const auto after = std::ranges::upper_bound(
                bpm_groups, row, {}, &std::pair<std::uint64_t, signed_rational_t>::first);
            return after == bpm_groups.begin()
                       ? std::size_t{0}
                       : static_cast<std::size_t>(std::distance(bpm_groups.begin(), after) - 1);
        };
        const auto active_bpm_at = [&](std::uint64_t row) {
            return bpm_groups[bpm_index_before_or_at(row)].second;
        };
        std::vector<long double> bpm_prefix_ms(bpm_groups.size());
        std::uint64_t prefix_row{};
        auto prefix_ms = 0.0L;
        std::size_t prefix_active{};
        for (std::size_t index = 0; index < bpm_groups.size(); ++index) {
            const auto boundary = bpm_groups[index].first;
            if (boundary < prefix_row)
                fail(osu_mania_write_diagnostic_code_t::invalid_document,
                     "BPM events are not ordered by chart position", "bpms");
            const auto beat_delta = static_cast<long double>(boundary - prefix_row) * 4.0L /
                                    static_cast<long double>(chart->divisions_per_standard_measure);
            prefix_ms += beat_delta * 60'000.0L / bpm_value(bpm_groups[prefix_active].second);
            bpm_prefix_ms[index] = prefix_ms;
            prefix_row = boundary;
            prefix_active = index;
        }
        const auto relative_milliseconds_at = [&](std::uint64_t row) {
            if (row < bpm_groups.front().first) {
                const auto beats = static_cast<long double>(row) * 4.0L /
                                   static_cast<long double>(chart->divisions_per_standard_measure);
                return beats * 60'000.0L / bpm_value(bpm_groups.front().second);
            }
            const auto active = bpm_index_before_or_at(row);
            const auto beat_delta = static_cast<long double>(row - bpm_groups[active].first) *
                                    4.0L /
                                    static_cast<long double>(chart->divisions_per_standard_measure);
            return bpm_prefix_ms[active] +
                   beat_delta * 60'000.0L / bpm_value(bpm_groups[active].second);
        };

        const auto retained_note_times = retained_osu_note_times(*chart);
        auto offset_seconds = rational_value(chart->beat_zero_audio_offset_seconds);
        if (!offset_seconds)
            fail(osu_mania_write_diagnostic_code_t::invalid_document,
                 "beat-zero audio offset is invalid", "beat_zero_audio_offset_seconds");
        auto offset_ms = *offset_seconds * 1000.0L;
        if (chart->source_family == format_family_t::osu_mania &&
            chart->beat_zero_audio_offset_seconds.numerator == 0) {
            std::vector<long double> timing_baselines;
            for (const auto& bpm : bpms) {
                if (bpm.event->provenance.source_field != "TimingPoints")
                    continue;
                const auto parts = split(bpm.event->provenance.source_spelling, ',');
                long double source_ms{};
                if (!parts.empty() && parse_number(parts.front(), source_ms))
                    timing_baselines.push_back(source_ms - relative_milliseconds_at(bpm.row));
            }
            if (!timing_baselines.empty()) {
                std::ranges::sort(timing_baselines);
                offset_ms = timing_baselines[timing_baselines.size() / 2];
            } else {
                std::vector<long double> note_baselines;
                for (const auto& measure : chart->measures) {
                    for (const auto& note : measure.notes) {
                        const auto retained = retained_note_times.find(note.id.value);
                        const auto row = absolute_row(measures, note.position);
                        if (retained != retained_note_times.end() && retained->second.head && row)
                            note_baselines.push_back(
                                static_cast<long double>(*retained->second.head) -
                                relative_milliseconds_at(*row));
                    }
                }
                if (!note_baselines.empty()) {
                    std::ranges::sort(note_baselines);
                    offset_ms = note_baselines[note_baselines.size() / 2];
                }
            }
        }
        const auto timestamp_at_row = [&](std::uint64_t row) {
            return offset_ms + relative_milliseconds_at(row);
        };

        std::vector<positioned_scroll_write_t> scrolls;
        scrolls.reserve(chart->scrolls.size());
        for (std::size_t index = 0; index < chart->scrolls.size(); ++index) {
            const auto& event = chart->scrolls[index];
            const auto row = absolute_row(measures, event.position);
            if (!row)
                fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                     "scroll event lies outside chart geometry", "scrolls");
            if (event.multiplier.numerator <= 0 || event.multiplier.denominator == 0)
                fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                     "osu! inherited timing requires positive scroll multipliers", "scrolls");
            scrolls.push_back({*row, &event, index});
        }
        std::ranges::stable_sort(scrolls, [](const auto& left, const auto& right) {
            return std::tuple{left.row, left.event->provenance.source_order, left.insertion_order} <
                   std::tuple{right.row, right.event->provenance.source_order,
                              right.insertion_order};
        });
        std::vector<positioned_kiai_write_t> kiais;
        kiais.reserve(chart->kiai.size());
        for (std::size_t index = 0; index < chart->kiai.size(); ++index) {
            const auto row = absolute_row(measures, chart->kiai[index].position);
            if (!row)
                fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                     "kiai event lies outside chart geometry", "kiai");
            kiais.push_back({*row, &chart->kiai[index], index});
        }
        std::ranges::stable_sort(kiais, [](const auto& left, const auto& right) {
            return std::tuple{left.row, left.event->provenance.source_order, left.insertion_order} <
                   std::tuple{right.row, right.event->provenance.source_order,
                              right.insertion_order};
        });
        const auto effects_at = [&](std::uint64_t row) {
            const auto after =
                std::ranges::upper_bound(kiais, row, {}, &positioned_kiai_write_t::row);
            if (after == kiais.begin())
                return std::uint32_t{};
            const auto& event = *std::prev(after)->event;
            auto effects = event.retained_effect_bits;
            if (event.enabled)
                effects |= 1U;
            else
                effects &= ~1U;
            return effects;
        };
        const auto scroll_at = [&](std::uint64_t row) {
            const auto after =
                std::ranges::upper_bound(scrolls, row, {}, &positioned_scroll_write_t::row);
            return after == scrolls.begin() ? signed_rational_t{1, 1}
                                            : std::prev(after)->event->multiplier;
        };
        const auto meter_at = [&](std::uint64_t row) -> std::uint32_t {
            const auto after = std::ranges::upper_bound(measures.entries, row, {},
                                                        &measure_index_entry_t::absolute_begin_row);
            const auto index =
                after == measures.entries.begin()
                    ? std::size_t{0}
                    : static_cast<std::size_t>(std::distance(measures.entries.begin(), after) - 1);
            const auto& definition = chart->measures[index].definition;
            if (definition.meter) {
                if (definition.meter->denominator != 4 || definition.meter->numerator < 1 ||
                    definition.meter->numerator > 32)
                    fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                         "osu! timing points support time signatures with a 4 denominator and "
                         "numerators from 1 through 32",
                         "measures.meter");
                return definition.meter->numerator;
            }
            if (definition.length.denominator != 0 &&
                definition.length.numerator <= UINT64_MAX / 4) {
                const auto quarter_beats = definition.length.numerator * 4;
                if (quarter_beats % definition.length.denominator == 0) {
                    const auto inferred = quarter_beats / definition.length.denominator;
                    if (inferred >= 1 && inferred <= 32)
                        return static_cast<std::uint32_t>(inferred);
                }
            }
            return 4;
        };
        const auto timing_sample_fields = [](const source_provenance_t& provenance) {
            std::array<std::int64_t, 3> fields{0, 0, 100};
            const auto source = split(provenance.source_spelling, ',');
            if (source.size() >= 6) {
                for (std::size_t index = 0; index < fields.size(); ++index) {
                    std::int64_t parsed{};
                    if (parse_integer(source[index + 3], parsed))
                        fields[index] = parsed;
                }
            }
            return fields;
        };

        std::size_t event_count{};
        const auto account_event = [&]() {
            if (event_count >= options.max_events)
                fail(osu_mania_write_diagnostic_code_t::output_limit,
                     "osu! event count exceeds the configured export bound", "events");
            ++event_count;
        };
        std::vector<osu_output_event_t> timing_output;
        std::set<std::uint64_t> red_rows;
        std::set<std::uint64_t> timing_rows;
        std::size_t event_serial{};
        const auto is_measure_boundary = [&](std::uint64_t row) {
            const auto entry = std::ranges::lower_bound(measures.entries, row, {},
                                                        &measure_index_entry_t::absolute_begin_row);
            return entry != measures.entries.end() && entry->absolute_begin_row == row;
        };
        const auto add_timing_line = [&](std::uint64_t row, long double beat_length,
                                         std::uint32_t meter, bool uninherited, std::size_t order,
                                         std::array<std::int64_t, 3> sample_fields =
                                             std::array<std::int64_t, 3>{0, 0, 100}) {
            const auto milliseconds = timestamp_at_row(row);
            if (!std::isfinite(milliseconds))
                fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                     "timing event time exceeds finite osu! numeric range", "timing");
            auto effects = effects_at(row);
            if (uninherited) {
                constexpr std::uint32_t omit_first_barline_bit = 1U << 3U;
                if (is_measure_boundary(row))
                    effects &= ~omit_first_barline_bit;
                else
                    effects |= omit_first_barline_bit;
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
                              std::to_string(effects);
            timing_output.push_back({milliseconds, row, order, event_serial++, line});
            account_event();
            timing_rows.insert(row);
            if (uninherited)
                red_rows.insert(row);
        };
        for (const auto& bpm : bpms) {
            add_timing_line(bpm.row, 60'000.0L / bpm_value(bpm.event->beats_per_minute),
                            meter_at(bpm.row), true,
                            bpm.event->provenance.source_order ? bpm.event->provenance.source_order
                                                               : bpm.insertion_order,
                            timing_sample_fields(bpm.event->provenance));
        }
        for (const auto& entry : measures.entries) {
            const auto row = entry.absolute_begin_row;
            if (red_rows.contains(row))
                continue;
            add_timing_line(row, 60'000.0L / bpm_value(active_bpm_at(row)), meter_at(row), true,
                            entry.id.value ? static_cast<std::size_t>(entry.id.value)
                                           : event_serial);
        }
        for (const auto& scroll : scrolls) {
            const auto value = rational_value(
                {scroll.event->multiplier.numerator, scroll.event->multiplier.denominator});
            if (!value || *value <= 0.0L)
                fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                     "osu! inherited timing requires positive finite scroll multipliers",
                     "scrolls");
            add_timing_line(scroll.row, -100.0L / *value, 4, false,
                            scroll.event->provenance.source_order
                                ? scroll.event->provenance.source_order
                                : scroll.insertion_order,
                            timing_sample_fields(scroll.event->provenance));
        }
        for (const auto& kiai : kiais) {
            if (timing_rows.contains(kiai.row))
                continue;
            const auto value = rational_value(scroll_at(kiai.row));
            if (!value || *value <= 0.0L)
                fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                     "osu! inherited timing requires positive finite scroll multipliers",
                     "scrolls");
            add_timing_line(kiai.row, -100.0L / *value, 4, false,
                            kiai.event->provenance.source_order
                                ? kiai.event->provenance.source_order
                                : kiai.insertion_order,
                            timing_sample_fields(kiai.event->provenance));
        }
        std::ranges::stable_sort(timing_output, [](const auto& left, const auto& right) {
            return std::tuple{left.milliseconds, left.row, left.order, left.serial} <
                   std::tuple{right.milliseconds, right.row, right.order, right.serial};
        });

        std::vector<osu_output_event_t> hit_output;
        bool rounded_hit_time{};
        std::size_t note_serial{};
        const auto note_time = [&](std::uint64_t row,
                                   std::optional<std::int64_t> retained) -> std::int64_t {
            const auto exact = timestamp_at_row(row);
            const auto rounded = writer_round_ms(exact);
            if (!rounded)
                fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                     "hit-object time is outside the supported integer millisecond range",
                     "HitObjects.time");
            if (retained && *rounded == *retained)
                return *retained;
            if (std::fabs(exact - static_cast<long double>(*rounded)) > 1.0e-10L)
                rounded_hit_time = true;
            return *rounded;
        };
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
                const bool hold = note.kind == note_kind_t::hold || note.kind == note_kind_t::roll;
                if (note.kind != note_kind_t::tap && !hold)
                    fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                         "osu!mania supports tap and hold hit objects only", "HitObjects.type");
                if (hold != note.end.has_value())
                    fail(osu_mania_write_diagnostic_code_t::invalid_document,
                         "hold notes require a tail and tap notes cannot have one",
                         "HitObjects.end");
                if (note.kind == note_kind_t::roll)
                    warn(osu_mania_write_diagnostic_code_t::lossy_transform,
                         "roll was exported as an osu!mania hold", "HitObjects.type");
                const auto retained = retained_note_times.find(note.id.value);
                const auto head = note_time(*begin_row, retained == retained_note_times.end()
                                                            ? std::nullopt
                                                            : retained->second.head);
                std::optional<std::int64_t> tail;
                if (note.end) {
                    const auto end_row = absolute_row(measures, *note.end);
                    if (!end_row || *end_row < *begin_row)
                        fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                             "hold tail lies before its head or outside chart geometry",
                             "HitObjects.end");
                    tail = note_time(*end_row, retained == retained_note_times.end()
                                                   ? std::nullopt
                                                   : retained->second.tail);
                    if (*tail < head)
                        fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                             "millisecond rounding would place a hold tail before its head",
                             "HitObjects.end");
                    if (*tail > 604'800'000LL)
                        fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                             "hold tail is outside this codec's supported osu! range",
                             "HitObjects.end");
                }
                std::string sample_field;
                std::int64_t hit_sound{};
                std::array<std::string, 4> sample_values{"0", "0", "0", "0"};
                if (const auto raw = split_csv(note.provenance.source_spelling);
                    raw && raw->size() >= 5) {
                    std::int64_t parsed_hit_sound{};
                    if (parse_integer((*raw)[4], parsed_hit_sound))
                        hit_sound = parsed_hit_sound;
                    if (raw->size() >= 6) {
                        const auto original_sample = split((*raw)[5], ':');
                        const auto sample_offset = hold ? std::size_t{1} : std::size_t{0};
                        for (std::size_t index = 0; index < sample_values.size(); ++index) {
                            if (sample_offset + index >= original_sample.size())
                                break;
                            std::int64_t parsed{};
                            if (parse_integer(original_sample[sample_offset + index], parsed))
                                sample_values[index] =
                                    std::string(original_sample[sample_offset + index]);
                        }
                    }
                }
                if (note.sound) {
                    const auto name = osu_resource_path(song, *note.sound, true);
                    if (!name || name->find(':') != std::string::npos)
                        fail(osu_mania_write_diagnostic_code_t::invalid_resource,
                             "note sample must refer to a safe relative audio asset whose path "
                             "contains no colon",
                             "HitObjects.hitSample");
                    sample_field = sample_values[0] + ":" + sample_values[1] + ":" +
                                   sample_values[2] + ":" + sample_values[3] + ":" + *name;
                } else if (std::ranges::any_of(sample_values,
                                               [](const auto& value) { return value != "0"; })) {
                    sample_field = sample_values[0] + ":" + sample_values[1] + ":" +
                                   sample_values[2] + ":" + sample_values[3] + ":";
                }
                const auto x = static_cast<std::uint64_t>(
                    ((static_cast<unsigned long long>(*lane) * 2ULL + 1ULL) * 256ULL) / keys);
                const auto type = hold ? 128 : 1;
                std::string line = std::to_string(x) + ",192," + std::to_string(head) + "," +
                                   std::to_string(type) + "," + std::to_string(hit_sound) + ",";
                if (tail) {
                    line += quote_csv(std::to_string(*tail) + ":" + sample_field);
                } else if (!sample_field.empty()) {
                    line += quote_csv(sample_field);
                }
                hit_output.push_back({static_cast<long double>(head), *begin_row, note_serial,
                                      note_serial++, std::move(line)});
                account_event();
            }
        }
        std::ranges::stable_sort(hit_output, [](const auto& left, const auto& right) {
            return std::tuple{left.milliseconds, left.row, left.order, left.serial} <
                   std::tuple{right.milliseconds, right.row, right.order, right.serial};
        });
        for (const auto& event : hit_output) {
            const auto timestamp = writer_round_ms(event.milliseconds);
            if (!timestamp || *timestamp < -86'400'000LL || *timestamp > 604'800'000LL)
                fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                     "hit-object time is outside this codec's supported osu! range",
                     "HitObjects.time");
        }
        if (rounded_hit_time)
            warn(osu_mania_write_diagnostic_code_t::timestamp_quantization,
                 "fractional hit-object milliseconds were rounded to nearest integer; exact "
                 "half-millisecond ties round away from zero",
                 "HitObjects.time");

        std::vector<osu_output_event_t> event_output;
        const auto add_sample_event = [&](local_position_t position, resource_id_t resource,
                                          std::size_t order, std::uint32_t layer) {
            const auto row = absolute_row(measures, position);
            if (!row)
                fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                     "sample event lies outside chart geometry", "Events.Sample");
            const auto name = osu_resource_path(song, resource, true);
            if (!name || name->find(':') != std::string::npos)
                fail(osu_mania_write_diagnostic_code_t::invalid_resource,
                     "sample event requires a safe relative audio resource without colons",
                     "Events.Sample");
            const auto rounded = writer_round_ms(timestamp_at_row(*row));
            if (!rounded)
                fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                     "sample event time is outside the integer millisecond range", "Events.Sample");
            const auto line = "Sample," + std::to_string(*rounded) + "," + std::to_string(layer) +
                              "," + quote_csv(*name) + ",100";
            event_output.push_back(
                {static_cast<long double>(*rounded), *row, order, event_serial++, line});
            account_event();
        };
        for (std::size_t index = 0; index < chart->measures.size(); ++index) {
            const auto& measure = chart->measures[index];
            for (const auto& event : measure.bgm_events)
                if (event.sound)
                    add_sample_event(event.position, *event.sound, event.provenance.source_order,
                                     1);
                else
                    warn(osu_mania_write_diagnostic_code_t::lossy_transform,
                         "empty background channel event has no osu! sample resource and was "
                         "omitted",
                         "bgm_events");
        }
        for (const auto& channel : chart->sound_channels) {
            const auto asset = std::ranges::find(song.audio_assets, channel.source_clip.source,
                                                 &audio_asset_t::id);
            if (asset == song.audio_assets.end())
                fail(osu_mania_write_diagnostic_code_t::invalid_resource,
                     "sound channel refers to an unknown audio asset", "sound_channels");
            const auto resource = std::ranges::find_if(
                song.resources, [&](const auto& item) { return item.asset == asset->id; });
            if (resource == song.resources.end())
                fail(osu_mania_write_diagnostic_code_t::invalid_resource,
                     "sound channel asset has no resource entry", "sound_channels");
            for (const auto& event : channel.events) {
                if (event.playable) {
                    const auto projected =
                        std::ranges::any_of(chart->measures, [&](const auto& measure) {
                            return std::ranges::any_of(measure.notes, [&](const auto& note) {
                                return note.position == event.position && note.end == event.end &&
                                       note.lane == event.lane && note.sound == resource->id;
                            });
                        });
                    if (!projected)
                        fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                             "playable sound-channel event has no equivalent sampled hit object",
                             "sound_channels");
                    continue;
                }
                if (event.continuation || event.end)
                    fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                         "osu! Sample events cannot represent sustained sound-channel events",
                         "sound_channels");
                add_sample_event(event.position, resource->id, event.provenance.source_order, 1);
            }
        }
        for (const auto& event : chart->bga_events) {
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
            const auto rounded = writer_round_ms(timestamp_at_row(*row));
            if (!rounded)
                fail(osu_mania_write_diagnostic_code_t::unrepresentable_position,
                     "visual event time is outside the integer millisecond range", "Events");
            const auto line = (event.kind == visual_event_kind_t::video ? "Video," : "0,") +
                              std::to_string(*rounded) + "," + quote_csv(*name) + ",0,0";
            event_output.push_back({static_cast<long double>(*rounded), *row,
                                    event.provenance.source_order, event_serial++, line});
            account_event();
        }
        std::vector<std::int64_t> bookmark_times;
        bookmark_times.reserve(chart->bookmarks.size());
        for (const auto& bookmark : chart->bookmarks) {
            std::int64_t milliseconds{};
            if (bookmark.anchor == marker_anchor_t::audio_time) {
                if (!bookmark.audio_milliseconds)
                    fail(osu_mania_write_diagnostic_code_t::invalid_document,
                         "audio-time bookmark is missing its millisecond value",
                         "Editor.Bookmarks");
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
                         "bookmark time is outside the integer millisecond range",
                         "Editor.Bookmarks");
                milliseconds = *rounded;
            }
            if (!bookmark.label.empty())
                warn(osu_mania_write_diagnostic_code_t::lossy_transform,
                     "bookmark label was omitted because osu! stores bookmark times only",
                     "Editor.Bookmarks");
            if (event_count >= options.max_events)
                fail(osu_mania_write_diagnostic_code_t::output_limit,
                     "osu! event count exceeds the configured export bound", "Editor.Bookmarks");
            ++event_count;
            bookmark_times.push_back(milliseconds);
        }
        std::ranges::sort(bookmark_times);

        std::vector<std::string> music_files;
        for (const auto& resource : song.resources) {
            if (std::ranges::find(resource.roles, resource_role_t::music) == resource.roles.end())
                continue;
            const auto name = osu_resource_path(song, resource.id, true);
            if (!name)
                fail(osu_mania_write_diagnostic_code_t::invalid_resource,
                     "music resource refers to an unsafe or missing audio asset",
                     "General.AudioFilename");
            if (std::ranges::find(music_files, *name) == music_files.end())
                music_files.push_back(*name);
        }
        if (music_files.size() > 1)
            fail(osu_mania_write_diagnostic_code_t::unsupported_feature,
                 "osu! General.AudioFilename can represent only one music resource",
                 "General.AudioFilename");

        const auto check_metadata = [&](std::string_view value, std::string_view field) {
            if (!writer_safe_text(value))
                fail(osu_mania_write_diagnostic_code_t::invalid_metadata,
                     "metadata cannot contain newlines or NUL bytes", std::string(field));
        };
        check_metadata(song.metadata.title, "Metadata.Title");
        check_metadata(song.metadata.artist, "Metadata.Artist");
        check_metadata(chart->metadata.name, "Metadata.Version");
        check_metadata(chart->metadata.author, "Metadata.Creator");
        std::string tags;
        for (const auto& tag : song.metadata.ordered_search_tags) {
            check_metadata(tag, "Metadata.Tags");
            if (tag.empty() || std::ranges::any_of(tag, [](unsigned char character) {
                    return character == ' ' || character == '\t';
                }))
                fail(osu_mania_write_diagnostic_code_t::invalid_metadata,
                     "osu! tags must be nonempty whitespace-free tokens", "Metadata.Tags");
            if (!tags.empty())
                tags.push_back(' ');
            tags += tag;
        }

        auto extra_fields = options.preserve_unknown_fields
                                ? passthrough_fields(*chart, song, result.diagnostics)
                                : std::vector<passthrough_field_t>{};
        if (result.has_errors())
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
        if (!music_files.empty())
            account_metadata("AudioFilename: " + music_files.front());
        account_metadata("Mode: 3");
        if (!bookmark_times.empty()) {
            std::string line{"Bookmarks: "};
            for (std::size_t index = 0; index < bookmark_times.size(); ++index) {
                if (index != 0)
                    line.push_back(',');
                line += std::to_string(bookmark_times[index]);
            }
            account_metadata(line);
        }
        account_metadata("Title:" + song.metadata.title);
        account_metadata("Artist:" + song.metadata.artist);
        account_metadata("Creator:" + chart->metadata.author);
        account_metadata("Version:" + chart->metadata.name);
        if (!tags.empty())
            account_metadata("Tags:" + tags);
        account_metadata("CircleSize:" + std::to_string(keys));
        for (const auto& field : extra_fields)
            if (field.section != "Events" && field.section != "TimingPoints" &&
                field.section != "HitObjects")
                account_metadata(field.line);
        std::string output;
        std::size_t output_lines{};
        const auto append_line = [&](std::string_view line) {
            if (output_lines >= options.max_lines || line.size() >= options.max_output_bytes ||
                output.size() > options.max_output_bytes - line.size() - 1) {
                fail(osu_mania_write_diagnostic_code_t::output_limit,
                     "osu! output exceeds the configured byte or line limit", "output");
            }
            output.append(line);
            output.push_back('\n');
            ++output_lines;
        };
        const auto append_section = [&](std::string_view section) {
            append_line("[" + std::string(section) + "]");
            for (const auto& field : extra_fields)
                if (field.section == section)
                    append_line(field.line);
        };
        append_line("osu file format v14");
        append_section("General");
        if (!music_files.empty())
            append_line("AudioFilename: " + music_files.front());
        append_line("Mode: 3");
        append_section("Editor");
        if (!bookmark_times.empty()) {
            std::string line{"Bookmarks: "};
            for (std::size_t index = 0; index < bookmark_times.size(); ++index) {
                if (index != 0)
                    line.push_back(',');
                line += std::to_string(bookmark_times[index]);
            }
            append_line(line);
        }
        append_section("Metadata");
        append_line("Title:" + song.metadata.title);
        append_line("Artist:" + song.metadata.artist);
        append_line("Creator:" + chart->metadata.author);
        append_line("Version:" + chart->metadata.name);
        if (!tags.empty())
            append_line("Tags:" + tags);
        append_section("Difficulty");
        append_line("CircleSize:" + std::to_string(keys));
        append_section("Events");
        std::ranges::stable_sort(event_output, [](const auto& left, const auto& right) {
            return std::tuple{left.milliseconds, left.row, left.order, left.serial} <
                   std::tuple{right.milliseconds, right.row, right.order, right.serial};
        });
        for (const auto& event : event_output)
            append_line(event.line);
        append_section("TimingPoints");
        for (const auto& event : timing_output)
            append_line(event.line);
        append_section("HitObjects");
        for (const auto& event : hit_output)
            append_line(event.line);

        std::vector<std::string> unknown_sections;
        for (const auto& field : extra_fields) {
            if (field.section == "General" || field.section == "Editor" ||
                field.section == "Metadata" || field.section == "Difficulty" ||
                field.section == "Events" || field.section == "TimingPoints" ||
                field.section == "HitObjects" || field.section == "Colours")
                continue;
            if (std::ranges::find(unknown_sections, field.section) == unknown_sections.end())
                unknown_sections.push_back(field.section);
        }
        if (std::ranges::any_of(extra_fields,
                                [](const auto& field) { return field.section == "Colours"; }))
            append_section("Colours");
        for (const auto& section : unknown_sections)
            append_section(section);
        result.text = std::move(output);
    } catch (const osu_write_abort_t&) {
        result.text.reset();
    } catch (const std::exception& error) {
        result.text.reset();
        result.diagnostics.push_back(
            {.severity = diagnostic_severity_t::error,
             .code = osu_mania_write_diagnostic_code_t::invalid_document,
             .message = std::string("osu! export failed atomically: ") + error.what(),
             .field = "document"});
    } catch (...) {
        result.text.reset();
        result.diagnostics.push_back({.severity = diagnostic_severity_t::error,
                                      .code = osu_mania_write_diagnostic_code_t::invalid_document,
                                      .message = "osu! export failed atomically",
                                      .field = "document"});
    }
    return result;
}

} // namespace wormedit

#include <wormedit/formats/osu_mania.h>

#include <wormedit/core/rational.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <ranges>
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
                    fields.push_back({line.section, "[]", std::string(line.text),
                                      std::string(line.text), line.order});
                    continue;
                }
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
                event_resources.push_back(
                    {(*parts)[3], asset_kind_t::storyboard, resource_role_t::bga, line.order});
            } else if (kind == "Sample" && parts->size() >= 4) {
                event_resources.push_back(
                    {(*parts)[3], asset_kind_t::audio, resource_role_t::sample, line.order});
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
        for (const auto& event_resource : event_resources) {
            if (event_resource.kind == asset_kind_t::audio) {
                add_audio(event_resource.path, event_resource.role, "Events.resource",
                          event_resource.order);
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

} // namespace wormedit

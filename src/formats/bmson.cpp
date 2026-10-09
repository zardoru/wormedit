#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/bgm_packing.h>
#include <wormedit/formats/bmson.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>

namespace wormedit {
namespace {

struct json_member_t;

struct json_value_t {
    enum class kind_t { null_value, boolean, number, string, array, object } kind{kind_t::null_value};
    std::size_t begin{};
    std::size_t end{};
    bool boolean{};
    std::string text;
    std::vector<json_value_t> array;
    std::vector<json_member_t> object;
};

struct json_member_t {
    std::string first;
    json_value_t second;
};

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
        if (count > source.size() - at) {
            return at;
        }
        for (std::size_t index = 1; index < count; ++index) {
            const auto next = static_cast<unsigned char>(source[at + index]);
            if ((next & 0xc0U) != 0x80U) {
                return at + index;
            }
            point = (point << 6U) | (next & 0x3fU);
        }
        if ((count == 3 && point < 0x800U) || (count == 4 && point < 0x10000U) ||
            (point >= 0xd800U && point <= 0xdfffU) || point > 0x10ffffU) {
            return at;
        }
        at += count;
    }
    return std::nullopt;
}

struct json_policy_error_t {
    std::string message;
    std::size_t offset{};
};

auto wrap_json(const nlohmann::ordered_json& source_value, std::string_view source)
    -> json_value_t {
    json_value_t value;
    value.begin = source_value.start_pos();
    value.end = source_value.end_pos();
    if (source_value.is_object()) {
        value.kind = json_value_t::kind_t::object;
        value.object.reserve(source_value.size());
        for (auto item = source_value.begin(); item != source_value.end(); ++item) {
            value.object.push_back({item.key(), wrap_json(item.value(), source)});
        }
    } else if (source_value.is_array()) {
        value.kind = json_value_t::kind_t::array;
        value.array.reserve(source_value.size());
        for (const auto& item : source_value) {
            value.array.push_back(wrap_json(item, source));
        }
    } else if (source_value.is_string()) {
        value.kind = json_value_t::kind_t::string;
        // With a parser callback, nlohmann 3.12 reports string starts just after
        // the opening quote while the end remains one-past the closing quote.
        if (value.begin > 0 && source[value.begin - 1] == '"') {
            --value.begin;
        }
        value.text = source_value.get<std::string>();
    } else if (source_value.is_boolean()) {
        value.kind = json_value_t::kind_t::boolean;
        value.boolean = source_value.get<bool>();
    } else if (source_value.is_number()) {
        value.kind = json_value_t::kind_t::number;
        value.text = std::string(source.substr(value.begin, value.end - value.begin));
    }
    return value;
}

auto after_json_string(std::string_view source, std::size_t position) -> std::size_t {
    ++position;
    while (position < source.size()) {
        if (source[position] == '\\') {
            position += std::min<std::size_t>(2, source.size() - position);
        } else if (source[position++] == '"') {
            break;
        }
    }
    return position;
}

auto first_duplicate_key_offset(std::string_view source) -> std::optional<std::size_t> {
    struct scope_t {
        bool object{};
        std::set<std::string> keys;
    };
    std::vector<scope_t> scopes;
    std::size_t position{};
    while (position < source.size()) {
        const auto current = source[position];
        if (current == '{' || current == '[') {
            scopes.push_back({.object = current == '{'});
            ++position;
            continue;
        }
        if (current == '}' || current == ']') {
            if (!scopes.empty()) scopes.pop_back();
            ++position;
            continue;
        }
        if (current != '"') {
            ++position;
            continue;
        }
        const auto begin = position;
        position = after_json_string(source, position);
        const auto end = position;
        while (position < source.size() &&
               (source[position] == ' ' || source[position] == '\t' ||
                source[position] == '\r' || source[position] == '\n')) {
            ++position;
        }
        if (position < source.size() && source[position] == ':' && !scopes.empty() &&
            scopes.back().object) {
            const auto parsed_key = nlohmann::ordered_json::parse(source.substr(begin, end - begin));
            const auto key = parsed_key.get<std::string>();
            if (!scopes.back().keys.insert(key).second) return begin;
        }
    }
    return std::nullopt;
}

auto member(const json_value_t& value, std::string_view key) -> const json_value_t* {
    if (value.kind != json_value_t::kind_t::object) {
        return nullptr;
    }
    const auto found = std::ranges::find_if(value.object, [&](const auto& entry) {
        return entry.first == key;
    });
    return found == value.object.end() ? nullptr : &found->second;
}

auto raw_spelling(std::string_view source, const json_value_t& value) -> std::string {
    return std::string(source.substr(value.begin, value.end - value.begin));
}

auto unsigned_integer(const json_value_t& value) -> std::optional<std::uint64_t> {
    if (value.kind != json_value_t::kind_t::number || value.text.empty() ||
        value.text.front() == '-') {
        return std::nullopt;
    }
    std::uint64_t result{};
    const auto parsed = std::from_chars(value.text.data(), value.text.data() + value.text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.text.data() + value.text.size()) {
        return std::nullopt;
    }
    return result;
}

auto parse_bmson_exponent(std::string_view text) -> std::optional<int> {
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
    }
    int exponent{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), exponent);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || exponent < -18 ||
        exponent > 18) {
        return std::nullopt;
    }
    return exponent;
}

auto exact_decimal(std::string_view text) -> std::optional<signed_rational_t> {
    bool negative{};
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    const auto exponent_at = text.find_first_of("eE");
    auto mantissa = text.substr(0, exponent_at);
    int exponent{};
    if (exponent_at != std::string_view::npos) {
        const auto parsed = parse_bmson_exponent(text.substr(exponent_at + 1));
        if (!parsed) {
            return std::nullopt;
        }
        exponent = *parsed;
    }
    const auto point = mantissa.find('.');
    const auto fractional_count = point == std::string_view::npos ? 0 : mantissa.size() - point - 1;
    std::string digits;
    digits.reserve(mantissa.size());
    for (const auto character : mantissa) {
        if (character != '.') {
            digits.push_back(character);
        }
    }
    std::uint64_t numerator{};
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), numerator);
    if (digits.empty() || parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) {
        return std::nullopt;
    }
    int scale = static_cast<int>(fractional_count) - exponent;
    std::uint64_t denominator = 1;
    const auto multiply_ten = [](std::uint64_t& value, int count) {
        for (int index = 0; index < count; ++index) {
            if (value > std::numeric_limits<std::uint64_t>::max() / 10U) {
                return false;
            }
            value *= 10U;
        }
        return true;
    };
    if (scale < 0) {
        if (!multiply_ten(numerator, -scale)) {
            return std::nullopt;
        }
        scale = 0;
    } else if (scale > 18 || !multiply_ten(denominator, scale)) {
        return std::nullopt;
    }
    const auto divisor = std::gcd(numerator, denominator);
    numerator /= divisor;
    denominator /= divisor;
    if (numerator > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    const auto signed_numerator = static_cast<std::int64_t>(numerator);
    return signed_rational_t{negative ? -signed_numerator : signed_numerator, denominator};
}

struct diagnostic_failure_t {
    bmson_parse_diagnostic_code_t code;
    std::string message;
    std::string path;
    std::optional<std::size_t> offset;
};

[[noreturn]] auto fail(bmson_parse_diagnostic_code_t code, std::string message,
                       std::string path, std::optional<std::size_t> offset = {}) -> void {
    throw diagnostic_failure_t{code, std::move(message), std::move(path), offset};
}

auto required_member(const json_value_t& value, std::string_view key, std::string path)
    -> const json_value_t& {
    const auto* result = member(value, key);
    if (result == nullptr) {
        fail(bmson_parse_diagnostic_code_t::missing_field,
             "required BMSON field is missing", std::move(path));
    }
    return *result;
}

auto required_string(const json_value_t& value, std::string path) -> const std::string& {
    if (value.kind != json_value_t::kind_t::string) {
        fail(bmson_parse_diagnostic_code_t::invalid_type,
             "BMSON field must be a string", std::move(path), value.begin);
    }
    return value.text;
}

auto required_object(const json_value_t& value, std::string path) -> const json_value_t& {
    if (value.kind != json_value_t::kind_t::object) {
        fail(bmson_parse_diagnostic_code_t::invalid_type,
             "BMSON field must be an object", std::move(path), value.begin);
    }
    return value;
}

auto required_array(const json_value_t& value, std::string path) -> const json_value_t& {
    if (value.kind != json_value_t::kind_t::array) {
        fail(bmson_parse_diagnostic_code_t::invalid_type,
             "BMSON field must be an array", std::move(path), value.begin);
    }
    return value;
}

auto required_u32(const json_value_t& value, std::string path, std::uint32_t maximum = UINT32_MAX)
    -> std::uint32_t {
    if (value.kind != json_value_t::kind_t::number) {
        fail(bmson_parse_diagnostic_code_t::invalid_type,
             "BMSON field must be an unsigned integer", std::move(path), value.begin);
    }
    if (value.text.empty() || value.text.front() == '-' ||
        value.text.find_first_of(".eE") != std::string::npos) {
        fail(bmson_parse_diagnostic_code_t::invalid_value,
             "BMSON field must be a non-negative integer without a fraction or exponent",
             std::move(path), value.begin);
    }
    std::uint64_t parsed_value{};
    const auto parsed = std::from_chars(value.text.data(), value.text.data() + value.text.size(),
                                        parsed_value);
    if (parsed.ec == std::errc::result_out_of_range) {
        fail(bmson_parse_diagnostic_code_t::integer_out_of_range,
             "BMSON integer exceeds the supported range", std::move(path), value.begin);
    }
    if (parsed.ec != std::errc{} || parsed.ptr != value.text.data() + value.text.size()) {
        fail(bmson_parse_diagnostic_code_t::invalid_value,
             "BMSON field must be a non-negative integer", std::move(path), value.begin);
    }
    if (parsed_value > maximum) {
        fail(bmson_parse_diagnostic_code_t::integer_out_of_range,
             "BMSON integer exceeds its unsigned 32-bit range", std::move(path), value.begin);
    }
    return static_cast<std::uint32_t>(parsed_value);
}

auto required_bpm(const json_value_t& value, std::string path) -> signed_rational_t {
    if (value.kind != json_value_t::kind_t::number) {
        fail(bmson_parse_diagnostic_code_t::invalid_type,
             "BPM must be a JSON number", std::move(path), value.begin);
    }
    const auto exact = exact_decimal(value.text);
    if (!exact.has_value() || exact->numerator <= 0) {
        fail(bmson_parse_diagnostic_code_t::invalid_value,
             "BPM must be a positive exactly representable decimal", std::move(path), value.begin);
    }
    return *exact;
}

auto path_from_utf8(std::string_view value) -> std::filesystem::path {
    std::u8string text;
    text.reserve(value.size());
    std::ranges::transform(value, std::back_inserter(text), [](char character) {
        return static_cast<char8_t>(character);
    });
    return std::filesystem::path(std::move(text));
}

auto rational_length(std::uint64_t pulses, std::uint64_t divisions) -> rational_t {
    const auto divisor = std::gcd(pulses, divisions);
    return {pulses / divisor, divisions / divisor};
}

struct pulse_position_t {
    std::uint64_t y{};
    std::string path;
    std::size_t order{};
};

struct raw_sound_note_t {
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t length{};
    bool continuation{};
    std::size_t order{};
    std::string path;
    std::string spelling;
    std::size_t offset{};
};

struct raw_sound_channel_t {
    std::string name;
    std::string name_spelling;
    std::size_t order{};
    std::vector<raw_sound_note_t> notes;
};

struct raw_timing_t {
    std::uint32_t y{};
    signed_rational_t value;
    std::size_t order{};
    std::string path;
    std::string spelling;
};

struct raw_stop_t {
    std::uint32_t y{};
    std::uint32_t duration{};
    std::size_t order{};
    std::string path;
    std::string spelling;
};

struct raw_visual_header_t {
    std::uint32_t id{};
    std::string name;
    std::string name_spelling;
    std::size_t order{};
    std::string path;
};

struct raw_visual_event_t {
    std::uint32_t y{};
    std::uint32_t id{};
    visual_event_kind_t kind{visual_event_kind_t::background};
    std::size_t order{};
    std::string path;
    std::string spelling;
};

struct id_source_t {
    std::uint64_t next{1};
    template <typename id_t> auto take() -> id_t { return id_t{next++}; }
};

auto provenance(std::string source_field, std::string spelling, std::size_t order = 0)
    -> source_provenance_t {
    return {format_family_t::bmson, "bmson-1.0", std::move(source_field), std::move(spelling), order};
}

auto add_format_group(std::string_view source, const json_value_t& root,
                      const std::shared_ptr<const std::vector<std::byte>>& bytes)
    -> format_metadata_t {
    format_metadata_t group;
    group.family = format_family_t::bmson;
    group.encoding.detected = text_encoding_t::utf8;
    group.encoding.original_bytes = bytes;
    group.encoding.unchanged_since_import = true;
    if (const auto* version = member(root, "version")) {
        group.ordered_fields.emplace_back("version", raw_spelling(source, *version));
    }
    if (const auto* info = member(root, "info"); info && info->kind == json_value_t::kind_t::object) {
        for (const auto& [key, value] : info->object) {
            group.ordered_fields.emplace_back("info." + key, raw_spelling(source, value));
        }
    }
    if (const auto* lines = member(root, "lines")) {
        group.ordered_fields.emplace_back("lines", raw_spelling(source, *lines));
    } else {
        group.ordered_fields.emplace_back("lines", "<omitted>");
    }
    if (const auto* info = member(root, "info")) {
        const auto* resolution = member(*info, "resolution");
        group.ordered_fields.emplace_back("resolution",
                                          resolution ? raw_spelling(source, *resolution) : "<omitted>");
    }
    // Song and chart scopes share these imported fields. Give each occurrence
    // the same stable identity so a generic metadata edit updates both copies.
    group.ordered_field_source_orders.resize(group.ordered_fields.size());
    std::iota(group.ordered_field_source_orders.begin(),
              group.ordered_field_source_orders.end(), std::size_t{});
    return group;
}

auto add_warning(bmson_parse_result_t& result, bmson_parse_diagnostic_code_t code,
                 std::string message, std::string path) -> void {
    result.diagnostics.push_back({diagnostic_severity_t::warning, code, std::move(message),
                                  std::move(path), std::nullopt});
}

auto add_parse_error(bmson_parse_result_t& result, bmson_parse_diagnostic_code_t code,
                     std::string message, std::string path,
                     std::optional<std::size_t> offset = {}) -> void {
    result.diagnostics.push_back({diagnostic_severity_t::error, code, std::move(message),
                                  std::move(path), offset});
}

struct json_policy_state_t {
    std::string_view source;
    const bmson_parse_options_t& options;
    std::size_t value_count{};
    std::unordered_map<int, std::set<std::string>> object_keys;

    json_policy_state_t(std::string_view source_value, const bmson_parse_options_t& parse_options)
        : source(source_value), options(parse_options) {}

    auto on_event(int depth, nlohmann::json::parse_event_t event,
                  nlohmann::ordered_json& parsed) -> bool {
        if (event == nlohmann::json::parse_event_t::object_start ||
            event == nlohmann::json::parse_event_t::array_start ||
            event == nlohmann::json::parse_event_t::value) {
            if (depth < 0 || static_cast<std::size_t>(depth) > options.max_nesting_depth) {
                const auto offset = parsed.start_pos();
                throw json_policy_error_t{"JSON nesting limit exceeded",
                    offset == std::string::npos ? 0 : offset};
            }
            if (++value_count > options.max_json_values) {
                const auto offset = parsed.start_pos();
                throw json_policy_error_t{"JSON value-count limit exceeded",
                    offset == std::string::npos ? 0 : offset};
            }
        }
        if (event == nlohmann::json::parse_event_t::object_start) {
            object_keys[depth].clear();
        } else if (event == nlohmann::json::parse_event_t::key) {
            const auto key = parsed.get<std::string>();
            const auto object_depth = depth - 1;
            if (!object_keys[object_depth].insert(key).second) {
                const auto offset = first_duplicate_key_offset(source).value_or(0);
                throw json_policy_error_t{"duplicate JSON object key", offset};
            }
        } else if (event == nlohmann::json::parse_event_t::object_end) {
            object_keys.erase(depth);
        }
        return true;
    }
};

auto parse_json_root(std::string_view source, const bmson_parse_options_t& options,
                     bmson_parse_result_t& result) -> std::optional<json_value_t> {
    try {
        json_policy_state_t policy{source, options};
        const auto callback = [&](int depth, nlohmann::json::parse_event_t event,
                                  nlohmann::ordered_json& parsed) {
            return policy.on_event(depth, event, parsed);
        };
        const auto parsed = nlohmann::ordered_json::parse(source, callback, true, false);
        return wrap_json(parsed, source);
    } catch (const json_policy_error_t& error) {
        add_parse_error(result, bmson_parse_diagnostic_code_t::invalid_json, error.message,
                        "$", error.offset);
    } catch (const nlohmann::json::parse_error& error) {
        const auto offset = error.byte == 0 ? 0 : std::min(error.byte - 1, source.size());
        add_parse_error(result, bmson_parse_diagnostic_code_t::invalid_json, error.what(),
                        "$", offset);
    } catch (const nlohmann::json::exception& error) {
        add_parse_error(result, bmson_parse_diagnostic_code_t::invalid_json, error.what(), "$", 0);
    }
    return std::nullopt;
}

auto bmson_resolution(const json_value_t& info) -> std::uint32_t {
    const auto* source_resolution = member(info, "resolution");
    if (!source_resolution || source_resolution->kind == json_value_t::kind_t::null_value) {
        return 240;
    }
    if (source_resolution->kind != json_value_t::kind_t::number) {
        fail(bmson_parse_diagnostic_code_t::invalid_type,
             "resolution must be an integer, null, or omitted", "$.info.resolution",
             source_resolution->begin);
    }
    std::int64_t signed_value{};
    const auto parsed = std::from_chars(source_resolution->text.data(),
                                        source_resolution->text.data() + source_resolution->text.size(),
                                        signed_value);
    if (parsed.ec == std::errc::result_out_of_range) {
        fail(bmson_parse_diagnostic_code_t::integer_out_of_range,
             "resolution exceeds its supported integer range", "$.info.resolution",
             source_resolution->begin);
    }
    if (parsed.ec != std::errc{} ||
        parsed.ptr != source_resolution->text.data() + source_resolution->text.size()) {
        fail(bmson_parse_diagnostic_code_t::invalid_type,
             "resolution must be an integer without a fraction or exponent",
             "$.info.resolution", source_resolution->begin);
    }
    const auto magnitude = signed_value < 0
                               ? static_cast<std::uint64_t>(-(signed_value + 1)) + 1U
                               : static_cast<std::uint64_t>(signed_value);
    if (magnitude > UINT32_MAX) {
        fail(bmson_parse_diagnostic_code_t::integer_out_of_range,
             "resolution magnitude exceeds the unsigned 32-bit range", "$.info.resolution",
             source_resolution->begin);
    }
    return magnitude == 0 ? 240U : static_cast<std::uint32_t>(magnitude);
}

auto bmson_authored_lines(const json_value_t& root, const bmson_parse_options_t& options)
    -> std::pair<std::vector<std::uint64_t>, bool> {
    std::vector<std::uint64_t> authored_lines;
    bool generated_lines = true;
    const auto* lines = member(root, "lines");
    if (!lines || lines->kind == json_value_t::kind_t::null_value) {
        return {authored_lines, generated_lines};
    }
    generated_lines = false;
    required_array(*lines, "$.lines");
    for (std::size_t index = 0; index < lines->array.size(); ++index) {
        const auto path = "$.lines[" + std::to_string(index) + "]";
        const auto& line = required_object(lines->array[index], path);
        const auto y = required_u32(required_member(line, "y", path + ".y"), path + ".y");
        if (!authored_lines.empty() && y <= authored_lines.back()) {
            fail(bmson_parse_diagnostic_code_t::invalid_geometry,
                 "bar-line positions must be strictly increasing", path + ".y",
                 member(line, "y")->begin);
        }
        authored_lines.push_back(y);
        if (authored_lines.size() > options.max_measures) {
            fail(bmson_parse_diagnostic_code_t::invalid_geometry,
                 "bar-line count exceeds the configured measure limit", "$.lines", lines->begin);
        }
    }
    return {authored_lines, generated_lines};
}

struct raw_bmson_events_t {
    const json_value_t& root;
    std::string_view source;
    const bmson_parse_options_t& options;
    const json_value_t* sound_channels_value;
    std::vector<raw_timing_t> bpm_events;
    std::vector<raw_stop_t> stops;
    std::vector<raw_sound_channel_t> raw_channels;
    std::vector<raw_visual_header_t> visual_headers;
    std::vector<raw_visual_event_t> visual_events;
    std::vector<pulse_position_t> all_positions;
    std::size_t source_order{};

    raw_bmson_events_t(const json_value_t& root_value, std::string_view source_value,
                       const bmson_parse_options_t& parse_options,
                       const json_value_t* channels)
        : root(root_value), source(source_value), options(parse_options),
          sound_channels_value(channels) {}

    auto parse_timing() -> void {
        const auto parse_timing_array = [&](std::string_view field, bool is_bpm) {
            const auto* array = member(root, field);
            if (!array || array->kind == json_value_t::kind_t::null_value) return;
            required_array(*array, "$." + std::string(field));
            if (array->array.size() > options.max_events) {
                fail(bmson_parse_diagnostic_code_t::invalid_value,
                     "event count exceeds the configured limit", "$." + std::string(field), array->begin);
            }
            for (std::size_t index = 0; index < array->array.size(); ++index) {
                const auto path = "$." + std::string(field) + "[" + std::to_string(index) + "]";
                const auto& event = required_object(array->array[index], path);
                const auto y = required_u32(required_member(event, "y", path + ".y"), path + ".y");
                const auto order = source_order++;
                all_positions.push_back({y, path + ".y", order});
                if (is_bpm) {
                    const auto value = required_bpm(required_member(event, "bpm", path + ".bpm"),
                                                    path + ".bpm");
                    bpm_events.push_back({y, value, order, path + ".bpm", raw_spelling(source, event)});
                } else {
                    const auto duration = required_u32(
                        required_member(event, "duration", path + ".duration"), path + ".duration");
                    stops.push_back({y, duration, order, path + ".duration", raw_spelling(source, event)});
                }
            }
        };
        parse_timing_array("bpm_events", true);
        parse_timing_array("stop_events", false);

    }

    auto parse_sound() -> void {
        if (sound_channels_value) {
            if (sound_channels_value->array.size() > options.max_events) {
                fail(bmson_parse_diagnostic_code_t::invalid_value,
                     "sound-channel count exceeds the configured limit", "$.sound_channels",
                     sound_channels_value->begin);
            }
            for (std::size_t channel_index = 0; channel_index < sound_channels_value->array.size();
                 ++channel_index) {
                const auto path = "$.sound_channels[" + std::to_string(channel_index) + "]";
                const auto& item = required_object(sound_channels_value->array[channel_index], path);
                raw_sound_channel_t channel;
                const auto& channel_name = required_member(item, "name", path + ".name");
                channel.name = required_string(channel_name, path + ".name");
                channel.name_spelling = raw_spelling(source, channel_name);
                channel.order = channel_index;
                const auto& notes = required_array(required_member(item, "notes", path + ".notes"), path + ".notes");
                if (notes.array.size() > options.max_events - std::min(options.max_events, source_order)) {
                    fail(bmson_parse_diagnostic_code_t::invalid_value,
                         "event count exceeds the configured limit", path + ".notes", notes.begin);
                }
                for (std::size_t note_index = 0; note_index < notes.array.size(); ++note_index) {
                    const auto note_path = path + ".notes[" + std::to_string(note_index) + "]";
                    const auto& note = required_object(notes.array[note_index], note_path);
                    channel.notes.push_back(parse_sound_note(note, note_path));
                }
                raw_channels.push_back(std::move(channel));
            }
        }

    }

    auto parse_sound_note(const json_value_t& note, const std::string& note_path)
        -> raw_sound_note_t {
        raw_sound_note_t parsed;
        const auto& x = required_member(note, "x", note_path + ".x");
        parsed.x = x.kind == json_value_t::kind_t::null_value
                       ? 0U : required_u32(x, note_path + ".x");
        parsed.y = required_u32(required_member(note, "y", note_path + ".y"), note_path + ".y");
        if (const auto* length = member(note, "l")) {
            parsed.length = required_u32(*length, note_path + ".l");
        }
        if (const auto* continuation = member(note, "c")) {
            if (continuation->kind != json_value_t::kind_t::boolean) {
                fail(bmson_parse_diagnostic_code_t::invalid_type,
                     "continuation flag must be a boolean", note_path + ".c",
                     continuation->begin);
            }
            parsed.continuation = continuation->boolean;
        }
        parsed.order = source_order++;
        parsed.path = note_path;
        parsed.spelling = raw_spelling(source, note);
        parsed.offset = note.begin;
        all_positions.push_back({parsed.y, note_path + ".y", parsed.order});
        if (parsed.length > UINT64_MAX - parsed.y) {
            fail(bmson_parse_diagnostic_code_t::integer_out_of_range,
                 "note endpoint overflows the pulse range", note_path + ".l", note.begin);
        }
        const auto endpoint = static_cast<std::uint64_t>(parsed.y) + parsed.length;
        if (parsed.length != 0) {
            all_positions.push_back({endpoint, note_path + ".l", parsed.order});
        }
        return parsed;
    }

    auto parse_visual() -> void {
        std::unordered_map<std::uint32_t, std::size_t> header_index;
        if (const auto* bga = member(root, "bga"); bga && bga->kind != json_value_t::kind_t::null_value) {
            required_object(*bga, "$.bga");
            const auto* headers = member(*bga, "bga_header");
            if (headers && headers->kind != json_value_t::kind_t::null_value) {
                required_array(*headers, "$.bga.bga_header");
                for (std::size_t index = 0; index < headers->array.size(); ++index) {
                    const auto path = "$.bga.bga_header[" + std::to_string(index) + "]";
                    const auto& header = required_object(headers->array[index], path);
                    raw_visual_header_t parsed;
                    parsed.id = required_u32(required_member(header, "id", path + ".id"), path + ".id");
                    const auto& header_name = required_member(header, "name", path + ".name");
                    parsed.name = required_string(header_name, path + ".name");
                    parsed.name_spelling = raw_spelling(source, header_name);
                    parsed.order = source_order++;
                    parsed.path = path;
                    if (!header_index.emplace(parsed.id, visual_headers.size()).second) {
                        fail(bmson_parse_diagnostic_code_t::duplicate_event,
                             "BGA header ID is duplicated", path + ".id", member(header, "id")->begin);
                    }
                    visual_headers.push_back(std::move(parsed));
                }
            }
            for (const auto [field, kind] : {
                     std::pair{std::string_view{"bga_events"}, visual_event_kind_t::background},
                     std::pair{std::string_view{"layer_events"}, visual_event_kind_t::layer},
                     std::pair{std::string_view{"poor_events"}, visual_event_kind_t::poor}}) {
                const auto* array = member(*bga, field);
                if (!array || array->kind == json_value_t::kind_t::null_value) continue;
                required_array(*array, "$.bga." + std::string(field));
                if (array->array.size() > options.max_events) {
                    fail(bmson_parse_diagnostic_code_t::invalid_value,
                         "event count exceeds the configured limit", "$.bga." + std::string(field),
                         array->begin);
                }
                for (std::size_t index = 0; index < array->array.size(); ++index) {
                    const auto path = "$.bga." + std::string(field) + "[" + std::to_string(index) + "]";
                    const auto& event = required_object(array->array[index], path);
                    raw_visual_event_t parsed;
                    parsed.y = required_u32(required_member(event, "y", path + ".y"), path + ".y");
                    parsed.id = required_u32(required_member(event, "id", path + ".id"), path + ".id");
                    parsed.kind = kind;
                    parsed.order = source_order++;
                    parsed.path = path;
                    parsed.spelling = raw_spelling(source, event);
                    all_positions.push_back({parsed.y, path + ".y", parsed.order});
                    visual_events.push_back(std::move(parsed));
                }
            }
        }

    }
};

auto create_bmson_measures(const std::vector<pulse_position_t>& all_positions,
                           const std::vector<std::uint64_t>& authored_lines,
                           bool generated_lines, std::uint64_t divisions,
                           const bmson_parse_options_t& options, chart_document_t& chart)
    -> std::vector<std::uint64_t> {
        std::uint64_t max_pulse{};
        for (const auto& position : all_positions) max_pulse = std::max(max_pulse, position.y);
        std::vector<std::uint64_t> boundaries{0};
        if (!generated_lines) {
            for (const auto line : authored_lines) {
                if (line != 0) boundaries.push_back(line);
            }
        }
        if (generated_lines || boundaries.size() == 1) {
            boundaries.clear();
            boundaries.push_back(0);
        }
        const auto geometry_target = std::max(max_pulse, boundaries.back());
        while (boundaries.back() <= geometry_target) {
            std::uint64_t next{};
            if (boundaries.back() > UINT64_MAX - divisions) {
                fail(bmson_parse_diagnostic_code_t::invalid_geometry,
                     "measure boundaries overflow the supported pulse range", "$.lines");
            }
            next = boundaries.back() + divisions;
            boundaries.push_back(next);
            if (boundaries.size() > options.max_measures + 1) {
                fail(bmson_parse_diagnostic_code_t::invalid_geometry,
                     "measure count exceeds the configured limit", "$.lines");
            }
        }
        if (boundaries.size() < 2) boundaries.push_back(divisions);
        // If authored boundaries include the final event exactly, the loop above creates a
        // following geometry interval so that the event belongs to a real measure.
        for (std::size_t index = 0; index + 1 < boundaries.size(); ++index) {
            const auto delta = boundaries[index + 1] - boundaries[index];
            const auto id = measure_id_t{static_cast<std::uint64_t>(index + 1)};
            chart.measures.push_back({.id = id,
                                      .definition = {rational_length(delta, divisions), std::nullopt}});
        }
        return boundaries;
}

struct bmson_mode_lanes_t {
    std::string mode_hint;
    std::vector<std::uint32_t> lane_positions;
    std::set<std::uint32_t> scratches;
    std::set<std::uint32_t> right_stage;
};

auto select_bmson_beat_lanes(bmson_mode_lanes_t& mode) -> bool {
    const auto& hint = mode.mode_hint;
    if (hint != "beat-5k" && hint != "beat-7k" &&
        hint != "beat-10k" && hint != "beat-14k") {
        return false;
    }
    const auto keys = hint == "beat-5k" || hint == "beat-10k" ? 5U : 7U;
    mode.lane_positions.push_back(8);
    mode.scratches.insert(8);
    for (std::uint32_t x = 1; x <= keys; ++x) mode.lane_positions.push_back(x);
    if (hint == "beat-10k" || hint == "beat-14k") {
        for (std::uint32_t x = 9; x <= 8 + keys; ++x) mode.lane_positions.push_back(x);
        mode.lane_positions.push_back(16);
        mode.scratches.insert(16);
        for (std::uint32_t x = 9; x <= 16; ++x) mode.right_stage.insert(x);
    }
    return true;
}

auto select_bmson_mode_lanes(std::string mode_hint, std::uint32_t max_x,
                             bmson_parse_result_t& result) -> bmson_mode_lanes_t {
        bmson_mode_lanes_t mode;
        mode.mode_hint = std::move(mode_hint);
        if (select_bmson_beat_lanes(mode)) {
            return mode;
        }
        auto& lane_positions = mode.lane_positions;
        auto& right_stage = mode.right_stage;
        const auto& hint = mode.mode_hint;
        if (hint == "popn-5k") {
            for (std::uint32_t x = 1; x <= 5; ++x) lane_positions.push_back(x);
        } else if (hint == "popn-9k") {
            for (std::uint32_t x = 1; x <= 9; ++x) lane_positions.push_back(x);
        } else if (hint == "generic-24keys") {
            for (std::uint32_t x = 1; x <= 24; ++x) lane_positions.push_back(x);
        } else if (hint == "generic-48keys") {
            for (std::uint32_t x = 1; x <= 48; ++x) lane_positions.push_back(x);
            for (std::uint32_t x = 25; x <= 48; ++x) right_stage.insert(x);
        } else if (hint != "generic-nkeys") {
            add_warning(result, bmson_parse_diagnostic_code_t::invalid_value,
                        "unknown mode_hint retained; using generic-nkeys lane layout", "$.info.mode_hint");
            mode.mode_hint = "generic-nkeys";
        }
        if (mode.mode_hint == "generic-nkeys") {
            for (std::uint32_t x = 1; x <= max_x; ++x) lane_positions.push_back(x);
        }
        return mode;
}

auto validate_bmson_resource_path(std::string_view name, std::string path)
    -> std::filesystem::path {
            if (name.find('\0') != std::string_view::npos) {
                fail(bmson_parse_diagnostic_code_t::invalid_value,
                     "resource path may not contain a NUL character", std::move(path));
            }
            std::string normalized_name(name);
            std::ranges::replace(normalized_name, '\\', '/');
            if ((!normalized_name.empty() && normalized_name.front() == '/') ||
                (normalized_name.size() >= 2 &&
                 std::isalpha(static_cast<unsigned char>(normalized_name[0])) &&
                 normalized_name[1] == ':')) {
                fail(bmson_parse_diagnostic_code_t::invalid_value,
                     "resource path must be a non-empty relative path", std::move(path));
            }
            auto source_path = path_from_utf8(normalized_name);
            if (source_path.empty() || source_path.is_absolute() || source_path.has_root_name() ||
                source_path.has_root_directory()) {
                fail(bmson_parse_diagnostic_code_t::invalid_value,
                     "resource path must be a non-empty relative path", std::move(path));
            }
            for (const auto& component : source_path) {
                if (component == "..") {
                    fail(bmson_parse_diagnostic_code_t::invalid_value,
                         "resource path may not escape its asset directory", std::move(path));
                }
            }
            return source_path.lexically_normal();
}

struct bmson_resource_catalog_t {
    const bmson_parse_options_t& options;
    bmson_parse_result_t& result;
    song_document_t& song;
    id_source_t& asset_ids;
    id_source_t& resource_ids;
    std::unordered_map<std::string, asset_id_t> audio_by_path;
    std::unordered_map<std::string, asset_id_t> visual_by_path;

    bmson_resource_catalog_t(const bmson_parse_options_t& parse_options,
                             bmson_parse_result_t& parse_result, song_document_t& document,
                             id_source_t& assets, id_source_t& resources)
        : options(parse_options), result(parse_result), song(document),
          asset_ids(assets), resource_ids(resources) {}

    static auto path_key(const std::filesystem::path& path) -> std::string {
        const auto value = path.generic_u8string();
        return std::string(reinterpret_cast<const char*>(value.data()), value.size());
    }

    auto add_audio_asset(const std::filesystem::path& source_path) -> std::pair<asset_id_t, bool> {
        const auto key = path_key(source_path);
        if (const auto found = audio_by_path.find(key); found != audio_by_path.end()) {
            return {found->second, false};
        }
        const auto id = asset_ids.take<asset_id_t>();
        song.audio_assets.push_back({.id = id, .source_path = source_path});
        audio_by_path.emplace(key, id);
        return {id, true};
    }

    auto add_visual_asset(const std::filesystem::path& source_path) -> std::pair<asset_id_t, bool> {
        const auto key = path_key(source_path);
        if (const auto found = visual_by_path.find(key); found != visual_by_path.end()) {
            return {found->second, false};
        }
        const auto id = asset_ids.take<asset_id_t>();
        song.visual_assets.push_back({.id = id, .kind = asset_kind_t::image, .source_path = source_path});
        visual_by_path.emplace(key, id);
        return {id, true};
    }

    auto resource_missing(const std::filesystem::path& source_path) const -> bool {
        if (!options.asset_base_directory.has_value()) return true;
        std::error_code error;
        return !std::filesystem::exists(*options.asset_base_directory / source_path, error) || error;
    }

    auto add_resource(std::string name, asset_id_t asset, std::vector<resource_role_t> roles,
                      std::string identity_namespace, std::string identity,
                      std::filesystem::path source_path, std::string path,
                      std::string source_spelling) -> resource_id_t {
        const auto id = resource_ids.take<resource_id_t>();
        const bool missing = resource_missing(source_path);
        song.resources.push_back({.id = id,
                                  .name = std::move(name),
                                  .asset = asset,
                                  .roles = std::move(roles),
                                  .target_identities = {{format_family_t::bmson,
                                                         std::move(identity),
                                                         std::move(identity_namespace)}},
                                  .missing = missing,
                                  .provenance = provenance(std::move(path),
                                                           std::move(source_spelling))});
        if (missing) {
            add_warning(result, bmson_parse_diagnostic_code_t::unresolved_resource,
                        options.asset_base_directory.has_value()
                            ? "resource file was not found under the configured asset directory"
                            : "resource path retained but unresolved until an asset directory is selected",
                        song.resources.back().provenance.source_field);
        }
        return id;
    }

    auto catalog_info(const json_value_t& info, std::string_view source) -> void {
        for (const auto [field, kind, role] : {
                 std::tuple{std::string_view{"back_image"}, asset_kind_t::image, resource_role_t::bga},
                 std::tuple{std::string_view{"eyecatch_image"}, asset_kind_t::image, resource_role_t::bga},
                 std::tuple{std::string_view{"title_image"}, asset_kind_t::image, resource_role_t::bga},
                 std::tuple{std::string_view{"banner_image"}, asset_kind_t::image, resource_role_t::bga},
                 std::tuple{std::string_view{"preview_music"}, asset_kind_t::audio, resource_role_t::preview}}) {
            const auto* source_field = member(info, field);
            if (!source_field || source_field->kind == json_value_t::kind_t::null_value) continue;
            const auto& name = required_string(*source_field, "$.info." + std::string(field));
            if (name.empty()) continue;
            const auto source_path = validate_bmson_resource_path(name, "$.info." + std::string(field));
            const auto [asset, is_new] = kind == asset_kind_t::audio
                                             ? add_audio_asset(source_path) : add_visual_asset(source_path);
            (void)is_new;
            add_resource(name, asset, {role}, "info", std::string(field), source_path,
                         "$.info." + std::string(field), raw_spelling(source, *source_field));
        }
    }
};

auto create_bmson_lane_layout(const json_value_t& info,
                              const std::vector<raw_sound_channel_t>& raw_channels,
                              const bmson_parse_options_t& options,
                              bmson_parse_result_t& result, chart_document_t& chart,
                              id_source_t& object_ids) -> std::set<std::uint32_t> {
        std::uint32_t max_x{};
        for (const auto& channel : raw_channels) {
            for (const auto& note : channel.notes) max_x = std::max(max_x, note.x);
        }
        std::string mode_hint = "beat-7k";
        if (const auto* hint = member(info, "mode_hint")) {
            mode_hint = required_string(*hint, "$.info.mode_hint");
        }
        auto mode = select_bmson_mode_lanes(std::move(mode_hint), max_x, result);
        const auto& lane_positions = mode.lane_positions;
        const auto& scratches = mode.scratches;
        const auto& right_stage = mode.right_stage;
        mode_hint = std::move(mode.mode_hint);
        const std::set<std::uint32_t> valid_lane_positions(lane_positions.begin(), lane_positions.end());
        if (lane_positions.size() > options.max_events) {
            fail(bmson_parse_diagnostic_code_t::invalid_value,
                 "lane count exceeds the configured safety limit", "$.info.mode_hint");
        }
        lane_layout_t layout;
        layout.profile_id = "bmson-" + mode_hint;
        layout.display_name = mode_hint;
        layout.split_stage = mode_hint == "generic-48keys";
        layout.split_after_column = layout.split_stage ? 24U : 0U;
        layout.bmson_generic_keys_only = mode_hint == "generic-nkeys" ||
                                         mode_hint == "generic-24keys" ||
                                         mode_hint == "generic-48keys";
        std::uint32_t display_order{};
        for (const auto x : lane_positions) {
            layout.lanes.push_back({.id = lane_id_t{100000ULL + x},
                                    .name = scratches.contains(x) ? "Scratch" : "Key " + std::to_string(x),
                                    .role = scratches.contains(x) ? lane_role_t::scratch
                                                                   : lane_role_t::playable,
                                    .stage = right_stage.contains(x) ? stage_group_t::right
                                                                      : stage_group_t::left,
                                    .display_order = display_order++});
        }
        assign_lane_key_colors(layout);
        chart.input_profile.profile_id = "bmson-unbound";
        chart.input_profile.stage_count = right_stage.empty() ? 1U : 2U;
        chart.lane_layouts.push_back({.id = object_ids.take<object_id_t>(),
                                      .position = {chart.measures.front().id, 0},
                                      .layout = std::move(layout)});

        return valid_lane_positions;
}

struct bmson_materialization_t {
    std::string_view source;
    const json_value_t& info;
    const bmson_parse_options_t& options;
    std::uint32_t resolution;
    chart_document_t& chart;
    id_source_t& object_ids;
    bmson_resource_catalog_t& catalog;
    raw_bmson_events_t& raw;
    const std::vector<std::uint64_t>& boundaries;
    const std::set<std::uint32_t>& valid_lane_positions;
    std::map<object_id_t, bgm_packing_key_t> packed_lane_keys{};

    auto map_position(std::uint64_t pulse, std::string path) const -> local_position_t {
        const auto found = std::upper_bound(boundaries.begin(), boundaries.end(), pulse);
        if (found == boundaries.begin() || found == boundaries.end()) {
            fail(bmson_parse_diagnostic_code_t::invalid_geometry,
                 "event pulse is outside generated measure geometry", std::move(path));
        }
        const auto index = static_cast<std::size_t>(std::distance(boundaries.begin(), found) - 1);
        return {measure_id_t{static_cast<std::uint64_t>(index + 1)}, pulse - boundaries[index]};
    }

    auto measure_for(measure_id_t id) -> measure_chunk_t& {
        return chart.measures.at(static_cast<std::size_t>(id.value - 1));
    }

    auto materialize_timing() -> void {
        // Timing events retain exact numeric spellings and source order. The info BPM is
        // materialized only when no explicit BPM event replaces it at pulse zero.
        bool has_initial_bpm = std::ranges::any_of(raw.bpm_events, [](const auto& event) { return event.y == 0; });
        const auto& initial_bpm_source = required_member(info, "init_bpm", "$.info.init_bpm");
        const auto initial_bpm = required_bpm(initial_bpm_source, "$.info.init_bpm");
        if (!has_initial_bpm) {
            chart.bpms.push_back({.id = object_ids.take<object_id_t>(),
                                  .position = map_position(0, "$.info.init_bpm"),
                                  .beats_per_minute = initial_bpm,
                                  .provenance = provenance("$.info.init_bpm",
                                                           raw_spelling(source, initial_bpm_source))});
        }
        std::ranges::stable_sort(raw.bpm_events, {}, &raw_timing_t::order);
        for (const auto& event : raw.bpm_events) {
            chart.bpms.push_back({.id = object_ids.take<object_id_t>(),
                                  .position = map_position(event.y, event.path),
                                  .beats_per_minute = event.value,
                                  .provenance = provenance(event.path, event.spelling, event.order)});
        }
        std::ranges::stable_sort(raw.stops, {}, &raw_stop_t::order);
        for (const auto& stop : raw.stops) {
            const auto divisor = std::gcd<std::uint64_t>(stop.duration, resolution);
            chart.pauses.push_back({.id = object_ids.take<object_id_t>(),
                                    .position = map_position(stop.y, stop.path),
                                    .kind = pause_kind_t::stop,
                                    .duration = {static_cast<std::int64_t>(stop.duration / divisor),
                                                 resolution / divisor},
                                    .source_unit = pause_unit_t::beats,
                                    .provenance = provenance(stop.path, stop.spelling, stop.order)});
        }

    }

    auto materialize_visual() -> void {
        std::unordered_map<std::uint32_t, resource_id_t> visual_resources;
        for (const auto& header : raw.visual_headers) {
            const auto source_path = validate_bmson_resource_path(header.name, header.path + ".name");
            const auto [asset, is_new] = catalog.add_visual_asset(source_path);
            (void)is_new;
            visual_resources.emplace(header.id,
                                      catalog.add_resource(header.name, asset, {resource_role_t::bga}, "bga",
                                                   std::to_string(header.id), source_path,
                                                   header.path + ".name", header.name_spelling));
        }
        for (const auto& event : raw.visual_events) {
            const auto resource = visual_resources.find(event.id);
            if (resource == visual_resources.end()) {
                fail(bmson_parse_diagnostic_code_t::unresolved_resource,
                     "BGA event refers to an undefined BGA header ID", event.path + ".id");
            }
            chart.bga_events.push_back({.id = object_ids.take<object_id_t>(),
                                        .position = map_position(event.y, event.path + ".y"),
                                        .resource = resource->second,
                                        .kind = event.kind,
                                        .provenance = provenance(event.path, event.spelling, event.order)});
        }

    }

    auto materialize_sound() -> void {
        std::size_t total_sound_events{};
        for (const auto& channel : raw.raw_channels) total_sound_events += channel.notes.size();
        if (total_sound_events > options.max_events) {
            fail(bmson_parse_diagnostic_code_t::invalid_value,
                 "sound event count exceeds the configured limit", "$.sound_channels");
        }
        for (std::size_t channel_index = 0; channel_index < raw.raw_channels.size(); ++channel_index) {
            materialize_sound_channel(channel_index);
        }
        pack_sound_lanes();
    }

    auto materialize_sound_channel(std::size_t channel_index) -> void {
        const auto& raw_channel = raw.raw_channels[channel_index];
        const auto source_path = validate_bmson_resource_path(raw_channel.name,
            "$.sound_channels[" + std::to_string(channel_index) + "].name");
        const auto [asset, is_new] = catalog.add_audio_asset(source_path);
        (void)is_new;
        const auto channel_resource = catalog.add_resource(
            raw_channel.name, asset, {resource_role_t::sample, resource_role_t::bgm},
            "sound_channel", std::to_string(channel_index), source_path,
            "$.sound_channels[" + std::to_string(channel_index) + "].name",
            raw_channel.name_spelling);
        sound_channel_t channel;
        channel.id = object_ids.take<object_id_t>();
        channel.name = raw_channel.name;
        channel.source_clip = {.id = clip_id_t{channel.id.value}, .source = asset};
        std::map<std::uint32_t, bool> restart_at_pulse;
        std::map<std::uint32_t, bool> has_playable_at_pulse;
        for (const auto& note : raw_channel.notes) {
            const auto [found, inserted] = restart_at_pulse.emplace(note.y, !note.continuation);
            if (!inserted && !note.continuation) found->second = true;
            if (note.x != 0) has_playable_at_pulse[note.y] = true;
        }
        const bool is_stem = std::ranges::any_of(raw_channel.notes,
            [](const auto& note) { return note.continuation; });
        object_id_t bgm_source_lane{};
        // Stem lanes remain linked even with only playable taps; packed temporary lanes
        // are replaced atomically after their BGM events have been allocated.
        {
            bgm_source_lane = object_ids.take<object_id_t>();
            chart.bgm_source_lanes.push_back({.id = bgm_source_lane,
                                              .name = raw_channel.name,
                                              .source_order = channel_index,
                                              .stem_channel = is_stem ? std::optional{channel.id} : std::nullopt});
        }
        if (!is_stem) packed_lane_keys.emplace(bgm_source_lane, bgm_packing_key_t{false, channel_resource.value});
        catalog.song.resources.back().sound_kind = is_stem ? sound_kind_t::stem : sound_kind_t::keysound;
        sound_note_context_t context{channel, channel_resource, bgm_source_lane,
                                     restart_at_pulse, has_playable_at_pulse};
        for (const auto& note : raw_channel.notes) append_sound_note(note, context);
        chart.sound_channels.push_back(std::move(channel));
    }

    auto packed_sound_requests(std::vector<bgm_event_t*>& events) -> std::vector<bgm_packing_request_t> {
        std::vector<bgm_packing_request_t> requests;
        for (auto& measure : chart.measures) {
            for (auto& event : measure.bgm_events) {
                const auto found = packed_lane_keys.find(event.source_lane);
                if (found == packed_lane_keys.end()) continue;
                events.push_back(&event);
                requests.push_back({found->second,
                    boundaries.at(static_cast<std::size_t>(event.position.measure.value - 1)) + event.position.row});
            }
        }
        return requests;
    }

    auto pack_sound_lanes() -> void {
        std::vector<bgm_event_t*> events;
        const auto requests = packed_sound_requests(events);
        const auto packed = allocate_bgm_columns(requests, {}, 0, options.max_events);
        if (!packed) {
            add_warning(catalog.result, bmson_parse_diagnostic_code_t::invalid_value,
                        "BGM packing rejected; retaining one lane per channel: " + packed.error,
                        "$.sound_channels");
            return;
        }
        std::erase_if(chart.bgm_source_lanes, [](const auto& lane) { return !lane.stem_channel; });
        std::vector<object_id_t> columns;
        columns.reserve(packed.lane_count);
        for (std::size_t index = 0; index < packed.lane_count; ++index) {
            const auto lane_id = object_ids.take<object_id_t>();
            columns.push_back(lane_id);
            chart.bgm_source_lanes.push_back({.id = lane_id,
                .name = "BGM " + std::to_string(index + 1),
                .source_order = raw.raw_channels.size() + index});
        }
        for (std::size_t index = 0; index < events.size(); ++index) {
            events[index]->source_lane = columns.at(packed.lanes.at(index));
        }
    }

    struct sound_note_context_t {
        sound_channel_t& channel;
        resource_id_t channel_resource;
        object_id_t bgm_source_lane;
        std::map<std::uint32_t, bool>& restart_at_pulse;
        std::map<std::uint32_t, bool>& has_playable_at_pulse;
    };

    auto append_sound_note(const raw_sound_note_t& note, sound_note_context_t& context) -> void {
                const bool playable = note.x != 0;
                const bool suppress_bgm = !playable && context.has_playable_at_pulse[note.y];
                const bool continuation = !context.restart_at_pulse[note.y];
                std::optional<local_position_t> end;
                if (note.length != 0) {
                    end = map_position(static_cast<std::uint64_t>(note.y) + note.length,
                                       note.path + ".l");
                }
                lane_id_t lane{};
                if (playable) {
                    if (!valid_lane_positions.contains(note.x)) {
                        fail(bmson_parse_diagnostic_code_t::invalid_value,
                             "note x is outside the selected mode_hint lane range", note.path + ".x",
                             note.offset);
                    }
                    lane = lane_id_t{100000ULL + note.x};
                }
                const auto position = map_position(note.y, note.path + ".y");
                const auto sound_event_id = object_ids.take<object_id_t>();
                context.channel.events.push_back({.id = sound_event_id,
                                          .position = position,
                                          .end = end,
                                          .lane = lane,
                                          .continuation = continuation,
                                          .playable = playable,
                                          .provenance = provenance(note.path, note.spelling, note.order)});
                if (playable) {
                    auto& measure = measure_for(position.measure);
                    measure.notes.push_back({.id = object_ids.take<object_id_t>(),
                                             .position = position,
                                             .lane = lane,
                                             .kind = note.length == 0 ? note_kind_t::tap : note_kind_t::hold,
                                             .end = end,
                                             .sound = context.channel_resource,
                                             .sound_channel_event = sound_event_id,
                                             .provenance = provenance(note.path, note.spelling, note.order)});
                } else if (!suppress_bgm) {
                    auto& measure = measure_for(position.measure);
                    measure.bgm_events.push_back({.id = object_ids.take<object_id_t>(),
                                                  .position = position,
                                                  .source_lane = context.bgm_source_lane,
                                                  .sound = context.channel_resource,
                                                  .sound_channel_event = sound_event_id,
                                                  .structural_slot = 0,
                                                  .provenance = provenance(note.path, note.spelling, note.order),
                                                  .end = end});
                }
    }
};

} // namespace

auto bmson_parse_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto parse_bmson_bytes(std::span<const std::byte> bytes, bmson_parse_options_t options)
    -> bmson_parse_result_t {
    bmson_parse_result_t result;
    const auto add_error = [&](bmson_parse_diagnostic_code_t code, std::string message,
                               std::string path, std::optional<std::size_t> offset = {}) {
        add_parse_error(result, code, std::move(message), std::move(path), offset);
    };
    if (bytes.size() > options.max_source_bytes) {
        add_error(bmson_parse_diagnostic_code_t::invalid_value,
                  "BMSON source exceeds the configured byte limit", "$", 0);
        return result;
    }
    const auto* chars = reinterpret_cast<const char*>(bytes.data());
    const std::string_view source(bytes.empty() ? "" : chars, bytes.size());
    if (const auto invalid = valid_utf8(source); invalid.has_value()) {
        add_error(bmson_parse_diagnostic_code_t::invalid_json,
                  "BMSON input is not valid UTF-8", "$", *invalid);
        return result;
    }
    auto root_value = parse_json_root(source, options, result);
    if (!root_value.has_value()) {
        if (result.diagnostics.empty()) {
            add_error(bmson_parse_diagnostic_code_t::invalid_json, "JSON root is missing", "$", 0);
        }
        return result;
    }
    const auto& root = *root_value;
    try {
        required_object(root, "$" );
        const auto& version = required_string(required_member(root, "version", "$.version"),
                                              "$.version");
        if (version != "1.0.0") {
            fail(bmson_parse_diagnostic_code_t::unsupported_version,
                 "only BMSON version 1.0.0 is supported", "$.version",
                 member(root, "version")->begin);
        }
        const auto& info = required_object(required_member(root, "info", "$.info"), "$.info");
        const auto* sound_channels_value = member(root, "sound_channels");
        if (sound_channels_value && sound_channels_value->kind != json_value_t::kind_t::array) {
            fail(bmson_parse_diagnostic_code_t::invalid_type,
                 "sound_channels must be an array", "$.sound_channels", sound_channels_value->begin);
        }

        const auto resolution = bmson_resolution(info);
        const std::uint64_t divisions = static_cast<std::uint64_t>(resolution) * 4U;

        const auto source_bytes = std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
        song_document_t song;
        song.id = song_id_t{1};
        const auto* title = member(info, "title");
        const auto* artist = member(info, "artist");
        if (title) song.metadata.title = required_string(*title, "$.info.title");
        if (artist) song.metadata.artist = required_string(*artist, "$.info.artist");
        const auto shared_group = add_format_group(source, root, source_bytes);
        song.metadata.format_groups.push_back(shared_group);

        chart_document_t chart;
        chart.id = chart_id_t{1};
        chart.source_family = format_family_t::bmson;
        chart.divisions_per_standard_measure = divisions;
        chart.metadata.format_groups.push_back(shared_group);
        chart.metadata.name = song.metadata.title;
        if (const auto* chart_name = member(info, "chart_name")) {
            chart.metadata.name = required_string(*chart_name, "$.info.chart_name");
        }
        chart.metadata.author = song.metadata.artist;
        if (artist) chart.metadata.author = required_string(*artist, "$.info.artist");
        if (const auto* level = member(info, "level")) {
            const auto rating = unsigned_integer(*level);
            if (!rating.has_value() || *rating > static_cast<std::uint64_t>(INT64_MAX)) {
                fail(bmson_parse_diagnostic_code_t::invalid_type,
                     "level must be a non-negative integer", "$.info.level", level->begin);
            }
            chart.metadata.rating = signed_rational_t{static_cast<std::int64_t>(*rating), 1};
        }

        id_source_t object_ids;
        id_source_t asset_ids;
        id_source_t resource_ids;
        bmson_resource_catalog_t catalog{options, result, song, asset_ids, resource_ids};
        catalog.catalog_info(info, source);

        // Parse bar-line declaration. Absence and null use generated 4/4 bars;
        // an explicit array is retained and controls authored measure boundaries.
        const auto [authored_lines, generated_lines] = bmson_authored_lines(root, options);

        raw_bmson_events_t raw{root, source, options, sound_channels_value};
        raw.parse_timing();
        raw.parse_sound();
        raw.parse_visual();
        const auto boundaries = create_bmson_measures(raw.all_positions, authored_lines,
                                                       generated_lines, divisions, options, chart);
        // Select source lane layout from mode_hint while preserving BMSON x as stable lane IDs.
        const auto valid_lane_positions = create_bmson_lane_layout(
            info, raw.raw_channels, options, result, chart, object_ids);
        bmson_materialization_t materialization{source, info, options, resolution, chart,
                                                object_ids, catalog, raw, boundaries,
                                                valid_lane_positions};
        materialization.materialize_timing();
        materialization.materialize_visual();
        materialization.materialize_sound();

        song.charts.push_back(std::move(chart));
        auto validation = validate_document(song);
        if (!validation.empty()) {
            const auto& first = validation.front();
            fail(bmson_parse_diagnostic_code_t::invalid_geometry,
                 "materialized BMSON document failed core validation: " + first.message, "$", std::nullopt);
        }
        result.song = std::move(song);
        return result;
    } catch (const diagnostic_failure_t& failure) {
        add_error(failure.code, failure.message, failure.path, failure.offset);
        return result;
    }
}

} // namespace wormedit

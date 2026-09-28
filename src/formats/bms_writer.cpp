#include <wormedit/formats/bms_writer.h>

#include <wormedit/core/bms_branch_projection.h>
#include <wormedit/core/bms_profile_detection.h>
#include <wormedit/core/sv_recipe.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <string_view>
#include <tuple>
#include <utility>

#if defined(WORMEDIT_HAS_ICONV)
#include <iconv.h>
#endif

namespace wormedit {
namespace {

struct write_failure_t {
    bms_write_diagnostic_code_t code;
    std::string message;
    std::string path;
};

struct text_encode_result_t {
    std::optional<std::string> bytes;
    std::size_t error_offset{};
    bool unsupported{};
};

[[noreturn]] auto fail(bms_write_diagnostic_code_t code, std::string message, std::string path)
    -> void {
    throw write_failure_t{code, std::move(message), std::move(path)};
}

auto warn(std::vector<bms_write_diagnostic_t>& diagnostics, bms_write_diagnostic_code_t code,
          std::string message, std::string path) -> void {
    diagnostics.push_back(
        {diagnostic_severity_t::warning, code, std::move(message), std::move(path)});
}

auto encoding_name(text_encoding_t encoding) -> std::string_view {
    switch (encoding) {
    case text_encoding_t::unknown:
        return "unknown";
    case text_encoding_t::ascii:
        return "ASCII";
    case text_encoding_t::utf8:
        return "UTF-8";
    case text_encoding_t::cp932:
        return "CP932";
    case text_encoding_t::euc_kr:
        return "EUC-KR";
    case text_encoding_t::cp949:
        return "CP949";
    case text_encoding_t::windows_1252:
        return "Windows-1252";
    }
    return "unknown";
}

auto iconv_name(text_encoding_t encoding) -> std::optional<std::string_view> {
    switch (encoding) {
    case text_encoding_t::cp932:
        return "CP932";
    case text_encoding_t::euc_kr:
        return "EUC-KR";
    case text_encoding_t::cp949:
        return "CP949";
    case text_encoding_t::windows_1252:
        return "WINDOWS-1252";
    case text_encoding_t::unknown:
    case text_encoding_t::ascii:
    case text_encoding_t::utf8:
        return std::nullopt;
    }
    return std::nullopt;
}

auto encode_normalized_bms(std::string_view utf8_with_bom, text_encoding_t encoding)
    -> text_encode_result_t {
    constexpr std::string_view utf8_bom = "\xef\xbb\xbf";
    if (!utf8_with_bom.starts_with(utf8_bom)) {
        return {.error_offset = 0};
    }
    if (encoding == text_encoding_t::utf8) {
        return {.bytes = std::string(utf8_with_bom)};
    }
    const auto utf8 = utf8_with_bom.substr(utf8_bom.size());
    if (encoding == text_encoding_t::ascii) {
        for (std::size_t index = 0; index < utf8.size(); ++index) {
            if (static_cast<unsigned char>(utf8[index]) > 0x7fU) {
                return {.error_offset = index};
            }
        }
        return {.bytes = std::string(utf8)};
    }

    const auto destination_name = iconv_name(encoding);
    if (!destination_name.has_value()) {
        return {.unsupported = true};
    }
#if defined(WORMEDIT_HAS_ICONV)
    const auto converter = iconv_open(std::string(*destination_name).c_str(), "UTF-8");
    if (converter == reinterpret_cast<iconv_t>(-1)) {
        return {.unsupported = true};
    }
    const auto initial_capacity = utf8.size() > (std::numeric_limits<std::size_t>::max() - 32U) / 2U
                                      ? std::numeric_limits<std::size_t>::max()
                                      : std::max<std::size_t>(utf8.size() * 2U + 32U, 32U);
    if (initial_capacity == std::numeric_limits<std::size_t>::max()) {
        iconv_close(converter);
        return {.error_offset = utf8.size()};
    }
    std::string output(initial_capacity, '\0');
    auto* source = const_cast<char*>(utf8.data());
    auto source_remaining = utf8.size();
    std::size_t written{};
    while (true) {
        auto* destination = output.data() + written;
        auto destination_remaining = output.size() - written;
        errno = 0;
        const auto status =
            iconv(converter, &source, &source_remaining, &destination, &destination_remaining);
        written = output.size() - destination_remaining;
        if (status != static_cast<std::size_t>(-1)) {
            break;
        }
        if (errno != E2BIG || output.size() > std::numeric_limits<std::size_t>::max() / 2U) {
            const auto error_offset = utf8.size() - source_remaining;
            iconv_close(converter);
            return {.error_offset = error_offset};
        }
        output.resize(output.size() * 2U);
    }
    while (true) {
        auto* destination = output.data() + written;
        auto destination_remaining = output.size() - written;
        errno = 0;
        const auto status =
            iconv(converter, nullptr, nullptr, &destination, &destination_remaining);
        written = output.size() - destination_remaining;
        if (status != static_cast<std::size_t>(-1)) {
            break;
        }
        if (errno != E2BIG || output.size() > std::numeric_limits<std::size_t>::max() / 2U) {
            iconv_close(converter);
            return {.error_offset = utf8.size()};
        }
        output.resize(output.size() * 2U);
    }
    iconv_close(converter);
    output.resize(written);
    return {.bytes = std::move(output)};
#else
    return {.unsupported = true};
#endif
}

auto base36_digit(char value) -> bool {
    return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z');
}

auto base36_id(std::size_t value) -> std::string {
    constexpr std::string_view digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string result(2, '0');
    result[0] = digits[(value / 36U) % 36U];
    result[1] = digits[value % 36U];
    return result;
}

auto bms_identity(const resource_entry_t& resource, std::string_view name_space)
    -> std::optional<std::string> {
    const auto identity =
        std::ranges::find_if(resource.target_identities, [&](const auto& candidate) {
            return candidate.family == format_family_t::bms &&
                   candidate.namespace_name == name_space;
        });
    if (identity == resource.target_identities.end() || identity->identifier.size() != 2) {
        return std::nullopt;
    }
    std::string result = identity->identifier;
    for (auto& character : result) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    if (!base36_digit(result[0]) || !base36_digit(result[1]) || result == "00") {
        return std::nullopt;
    }
    return result;
}

auto has_line_break_or_nul(std::string_view value) -> bool {
    return std::ranges::any_of(value, [](char character) {
        return character == '\r' || character == '\n' || character == '\0';
    });
}

auto has_edge_whitespace(std::string_view value) -> bool {
    return !value.empty() && (std::isspace(static_cast<unsigned char>(value.front())) != 0 ||
                              std::isspace(static_cast<unsigned char>(value.back())) != 0);
}

auto safe_metadata_token(std::string_view token) -> bool {
    if (token.empty() || !((token.front() >= 'A' && token.front() <= 'Z') ||
                           (token.front() >= 'a' && token.front() <= 'z'))) {
        return false;
    }
    return std::ranges::all_of(token, [](char character) {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

auto ascii_upper(std::string value) -> std::string {
    for (auto& character : value) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return value;
}

auto trim_ascii_whitespace(std::string_view value) -> std::string_view {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

auto generated_or_channel_token(std::string_view token) -> bool {
    const auto upper = ascii_upper(std::string(token));
    if (upper == "PLAYER" || upper == "BPM" || upper == "STOP" || upper == "WAV" ||
        upper == "BMP" || upper == "RANDOM" || upper == "SETRANDOM" || upper == "SWITCH" ||
        upper == "SETSWITCH" || upper == "IF" || upper == "ELSEIF" || upper == "ELSE" ||
        upper == "ENDIF" || upper == "ENDSW" || upper == "CASE" || upper == "SKIP" ||
        upper == "DEF") {
        return true;
    }
    const auto is_base36 = [](char value) { return base36_digit(value); };
    if ((upper.size() == 5 && upper.starts_with("WAV") && is_base36(upper[3]) &&
         is_base36(upper[4])) ||
        (upper.size() == 5 && upper.starts_with("BMP") && is_base36(upper[3]) &&
         is_base36(upper[4])) ||
        (upper.size() == 5 && upper.starts_with("BPM") && is_base36(upper[3]) &&
         is_base36(upper[4])) ||
        (upper.size() == 6 && upper.starts_with("STOP") && is_base36(upper[4]) &&
         is_base36(upper[5]))) {
        return true;
    }
    return upper.size() == 5 && upper[0] >= '0' && upper[0] <= '9' && upper[1] >= '0' &&
           upper[1] <= '9' && upper[2] >= '0' && upper[2] <= '9' && is_base36(upper[3]) &&
           is_base36(upper[4]);
}

auto valid_utf8(std::string_view text) -> bool {
    std::size_t index{};
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        std::size_t width{};
        std::uint32_t codepoint{};
        if (first >= 0xc2U && first <= 0xdfU) {
            width = 2;
            codepoint = first & 0x1fU;
        } else if (first >= 0xe0U && first <= 0xefU) {
            width = 3;
            codepoint = first & 0x0fU;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            width = 4;
            codepoint = first & 0x07U;
        } else {
            return false;
        }
        if (width > text.size() - index)
            return false;
        for (std::size_t offset = 1; offset < width; ++offset) {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xc0U) != 0x80U)
                return false;
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if ((width == 3 && codepoint < 0x800U) || (width == 4 && codepoint < 0x10000U) ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU) || codepoint > 0x10ffffU) {
            return false;
        }
        index += width;
    }
    return true;
}

auto retained_source_bytes(const song_document_t& song, const chart_document_t& chart)
    -> std::shared_ptr<const std::vector<std::byte>> {
    const auto find = [](const std::vector<format_metadata_t>& groups)
        -> std::shared_ptr<const std::vector<std::byte>> {
        const auto group = std::ranges::find_if(groups, [](const auto& candidate) {
            return candidate.family == format_family_t::bms &&
                   candidate.encoding.unchanged_since_import &&
                   candidate.encoding.original_bytes != nullptr;
        });
        return group == groups.end() ? nullptr : group->encoding.original_bytes;
    };
    if (auto bytes = find(chart.metadata.format_groups); bytes)
        return bytes;
    return find(song.metadata.format_groups);
}

auto source_provenance_bytes(const song_document_t& song, const chart_document_t& chart)
    -> std::shared_ptr<const std::vector<std::byte>> {
    const auto find = [](const std::vector<format_metadata_t>& groups)
        -> std::shared_ptr<const std::vector<std::byte>> {
        const auto group = std::ranges::find_if(groups, [](const auto& candidate) {
            return candidate.family == format_family_t::bms &&
                   candidate.encoding.original_bytes != nullptr;
        });
        return group == groups.end() ? nullptr : group->encoding.original_bytes;
    };
    if (auto bytes = find(chart.metadata.format_groups); bytes)
        return bytes;
    return find(song.metadata.format_groups);
}

auto exact_decimal(std::uint64_t numerator, std::uint64_t denominator, std::string path)
    -> std::string {
    if (numerator == 0 || denominator == 0) {
        fail(bms_write_diagnostic_code_t::unrepresentable_number,
             "BMS numeric values must be positive exact rationals", std::move(path));
    }
    const auto divisor = std::gcd(numerator, denominator);
    numerator /= divisor;
    denominator /= divisor;
    auto remainder_denominator = denominator;
    while (remainder_denominator % 2 == 0)
        remainder_denominator /= 2;
    while (remainder_denominator % 5 == 0)
        remainder_denominator /= 5;
    if (remainder_denominator != 1) {
        fail(bms_write_diagnostic_code_t::unrepresentable_number,
             "BMS text cannot represent this rational as an exact terminating decimal",
             std::move(path));
    }

    auto output = std::to_string(numerator / denominator);
    auto remainder = numerator % denominator;
    if (remainder == 0)
        return output;
    output.push_back('.');
    // Compute (remainder * 10) / denominator without overflowing uint64_t.
    // Each decimal digit needs only ten bounded modular additions.
    while (remainder != 0) {
        unsigned digit{};
        std::uint64_t next_remainder{};
        for (unsigned iteration = 0; iteration < 10; ++iteration) {
            if (next_remainder >= denominator - remainder) {
                next_remainder -= denominator - remainder;
                ++digit;
            } else {
                next_remainder += remainder;
            }
        }
        output.push_back(static_cast<char>('0' + digit));
        remainder = next_remainder;
    }
    return output;
}

auto exact_offset_decimal(signed_rational_t value) -> std::string {
    constexpr std::string_view path = "$.charts[].beat_zero_audio_offset_seconds";
    if (value.denominator == 0) {
        fail(bms_write_diagnostic_code_t::unrepresentable_number,
             "BMS OFFSET has an invalid denominator", std::string(path));
    }
    if (value.numerator == 0) return "0";
    const bool negative = value.numerator < 0;
    const auto magnitude = negative
                               ? static_cast<std::uint64_t>(-(value.numerator + 1)) + 1
                               : static_cast<std::uint64_t>(value.numerator);
    auto decimal = exact_decimal(magnitude, value.denominator, std::string(path));
    if (negative) decimal.insert(decimal.begin(), '-');
    return decimal;
}

auto exact_decimal(signed_rational_t value, std::string path) -> std::string {
    if (value.numerator <= 0) {
        fail(bms_write_diagnostic_code_t::unrepresentable_number,
             "BMS timing values must be positive exact rationals", std::move(path));
    }
    return exact_decimal(static_cast<std::uint64_t>(value.numerator), value.denominator,
                         std::move(path));
}

auto parse_positive_timing_value(std::string_view text) -> std::optional<signed_rational_t> {
    const auto parsed = parse_measure_length(text);
    if (!parsed.value.has_value() ||
        parsed.value->numerator >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return signed_rational_t{static_cast<std::int64_t>(parsed.value->numerator),
                             parsed.value->denominator};
}

auto checked_lcm(std::uint64_t left, std::uint64_t right) -> std::optional<std::uint64_t> {
    if (left == 0 || right == 0)
        return std::nullopt;
    const auto reduced = left / std::gcd(left, right);
    if (right > std::numeric_limits<std::uint64_t>::max() / reduced)
        return std::nullopt;
    return reduced * right;
}

struct note_slot_t {
    std::uint64_t numerator{};
    std::uint64_t denominator{1};
    std::string object_id;
    std::string path;
};

struct channel_line_t {
    std::size_t measure{};
    std::string channel;
    std::vector<note_slot_t> notes;
};

struct source_measure_line_t {
    std::size_t source_order{};
    std::size_t measure_index{};
    std::string text;
    std::string raw_value;
    bms_source_span_t source_span;
    bms_line_ending_t line_ending{bms_line_ending_t::none};
};

auto source_measure_line_matches_bytes(const source_measure_line_t& source,
                                       std::span<const std::byte> original_bytes) -> bool {
    if (source.source_span.byte_end <= source.source_span.byte_begin ||
        source.source_span.byte_end > original_bytes.size() ||
        !std::ranges::all_of(source.text, [](char character) {
            return static_cast<unsigned char>(character) <= 0x7fU;
        })) {
        return false;
    }
    std::string_view ending;
    switch (source.line_ending) {
    case bms_line_ending_t::none:
        ending = "";
        break;
    case bms_line_ending_t::lf:
        ending = "\n";
        break;
    case bms_line_ending_t::crlf:
        ending = "\r\n";
        break;
    case bms_line_ending_t::cr:
        ending = "\r";
        break;
    }
    const auto span_size = source.source_span.byte_end - source.source_span.byte_begin;
    if (span_size != source.text.size() + ending.size()) {
        return false;
    }
    for (std::size_t index = 0; index < source.text.size(); ++index) {
        if (std::to_integer<unsigned char>(original_bytes[source.source_span.byte_begin + index]) !=
            static_cast<unsigned char>(source.text[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < ending.size(); ++index) {
        if (std::to_integer<unsigned char>(
                original_bytes[source.source_span.byte_begin + source.text.size() + index]) !=
            static_cast<unsigned char>(ending[index])) {
            return false;
        }
    }
    return true;
}

struct retained_channel_line_t {
    std::size_t source_order{};
    std::size_t measure_index{};
    std::string channel;
    std::string text;
    std::string raw_value;
    bms_source_span_t source_span;
    bms_line_ending_t line_ending{bms_line_ending_t::none};
};

struct retained_timing_declaration_t {
    std::size_t source_order{};
    std::string token;
    std::string raw_value;
    std::string text;
    bms_source_span_t source_span;
    bms_line_ending_t line_ending{bms_line_ending_t::none};
};

struct channel_slot_field_t {
    std::string channel;
    std::size_t slot{};
    std::string object_id;
};

struct long_note_source_field_t {
    std::string start_channel;
    std::size_t start_slot{};
    std::string start_object_id;
    std::string end_channel;
    std::size_t end_slot{};
    std::string end_object_id;
    std::size_t end_source_order{};
};

auto parse_long_note_source_field(std::string_view field)
    -> std::optional<long_note_source_field_t> {
    std::istringstream stream{std::string(field)};
    std::string label;
    std::string start_channel;
    std::size_t start_slot{};
    std::string start_object;
    std::string end_channel;
    std::size_t end_slot{};
    std::string end_object;
    std::size_t end_line{};
    if (!(stream >> label) || label != "long-note" || !(stream >> label) || label != "start" ||
        !(stream >> start_channel >> label >> start_slot >> label >> start_object) ||
        label != "object" || !(stream >> label) || label != "paired" || !(stream >> label) ||
        label != "with" || !(stream >> end_channel >> label >> end_slot >> label >> end_object) ||
        label != "object" || !(stream >> label) || label != "on" || !(stream >> label) ||
        label != "source" || !(stream >> label) || label != "line" || !(stream >> end_line) ||
        end_line == 0 || start_channel.size() != 2 || end_channel.size() != 2 ||
        start_object.size() != 2 || end_object.size() != 2 ||
        !std::ranges::all_of(start_channel, base36_digit) ||
        !std::ranges::all_of(end_channel, base36_digit) ||
        !std::ranges::all_of(start_object, base36_digit) ||
        !std::ranges::all_of(end_object, base36_digit)) {
        return std::nullopt;
    }
    std::string trailing;
    if (stream >> trailing) {
        return std::nullopt;
    }
    return long_note_source_field_t{ascii_upper(std::move(start_channel)),
                                    start_slot,
                                    ascii_upper(std::move(start_object)),
                                    ascii_upper(std::move(end_channel)),
                                    end_slot,
                                    ascii_upper(std::move(end_object)),
                                    end_line - 1};
}

auto parse_channel_slot_field(std::string_view field) -> std::optional<channel_slot_field_t> {
    // Materialized BMS channel events use a deliberately small, canonical
    // source-field grammar. Reject anything that would make the source identity
    // ambiguous instead of trying to infer an edit target from prose.
    const auto slot_marker = field.find(" slot ");
    const auto object_marker =
        field.find(" object ", slot_marker == std::string_view::npos ? 0 : slot_marker + 6);
    if (slot_marker != 2 || object_marker == std::string_view::npos ||
        field.size() != object_marker + 10) {
        return std::nullopt;
    }
    std::size_t slot{};
    const auto slot_text = field.substr(slot_marker + 6, object_marker - slot_marker - 6);
    const auto parsed_slot =
        std::from_chars(slot_text.data(), slot_text.data() + slot_text.size(), slot);
    const auto object_id = field.substr(object_marker + 8);
    if (parsed_slot.ec != std::errc{} || parsed_slot.ptr != slot_text.data() + slot_text.size() ||
        object_id.size() != 2 || !base36_digit(object_id[0]) || !base36_digit(object_id[1])) {
        return std::nullopt;
    }
    return channel_slot_field_t{std::string(field.substr(0, 2)), slot,
                                ascii_upper(std::string(object_id))};
}

auto branch_channel_slot_field(std::string_view field) -> std::optional<channel_slot_field_t> {
    auto parsed = parse_channel_slot_field(field);
    if (parsed.has_value()) {
        return parsed;
    }
    if (field.starts_with("03 slot ")) {
        const auto value_marker = field.find(" value ", 8);
        if (value_marker == std::string_view::npos || field.size() != value_marker + 9) {
            return std::nullopt;
        }
        std::size_t slot{};
        const auto slot_text = field.substr(8, value_marker - 8);
        const auto parsed_slot =
            std::from_chars(slot_text.data(), slot_text.data() + slot_text.size(), slot);
        const auto object_id = field.substr(value_marker + 7);
        if (parsed_slot.ec != std::errc{} ||
            parsed_slot.ptr != slot_text.data() + slot_text.size() || object_id.size() != 2 ||
            !base36_digit(object_id[0]) || !base36_digit(object_id[1])) {
            return std::nullopt;
        }
        return channel_slot_field_t{"03", slot, ascii_upper(std::string(object_id))};
    }
    if (field.starts_with("08 slot ")) {
        const auto bpm_marker = field.find(" BPM", 8);
        if (bpm_marker == std::string_view::npos || field.size() != bpm_marker + 6) {
            return std::nullopt;
        }
        std::size_t slot{};
        const auto slot_text = field.substr(8, bpm_marker - 8);
        const auto parsed_slot =
            std::from_chars(slot_text.data(), slot_text.data() + slot_text.size(), slot);
        const auto object_id = field.substr(bpm_marker + 4);
        if (parsed_slot.ec != std::errc{} ||
            parsed_slot.ptr != slot_text.data() + slot_text.size() || object_id.size() != 2 ||
            !base36_digit(object_id[0]) || !base36_digit(object_id[1])) {
            return std::nullopt;
        }
        return channel_slot_field_t{"08", slot, ascii_upper(std::string(object_id))};
    }
    if (field.starts_with("09 slot ")) {
        const auto stop_marker = field.find(" STOP", 8);
        if (stop_marker == std::string_view::npos || field.size() != stop_marker + 7) {
            return std::nullopt;
        }
        std::size_t slot{};
        const auto slot_text = field.substr(8, stop_marker - 8);
        const auto parsed_slot =
            std::from_chars(slot_text.data(), slot_text.data() + slot_text.size(), slot);
        const auto object_id = field.substr(stop_marker + 5);
        if (parsed_slot.ec != std::errc{} ||
            parsed_slot.ptr != slot_text.data() + slot_text.size() || object_id.size() != 2 ||
            !base36_digit(object_id[0]) || !base36_digit(object_id[1])) {
            return std::nullopt;
        }
        return channel_slot_field_t{"09", slot, ascii_upper(std::string(object_id))};
    }
    return std::nullopt;
}

auto retained_line_matches_bytes(std::string_view text, bms_source_span_t source_span,
                                 bms_line_ending_t line_ending,
                                 std::span<const std::byte> original_bytes) -> bool {
    source_measure_line_t source{
        .text = std::string(text), .source_span = source_span, .line_ending = line_ending};
    return source_measure_line_matches_bytes(source, original_bytes);
}

struct branch_tree_analysis_t {
    bool has_branches{};
    bool has_branch_timing{};
    bool has_unmodeled_root_statement{};
    std::set<std::size_t> branch_source_orders;
    std::set<std::size_t> root_source_orders;
    std::set<std::size_t> retained_timing_source_orders;
    std::set<std::size_t> branch_measure_indices;
    std::set<std::pair<std::string, std::string>> branch_resource_ids;
    std::map<std::size_t, std::pair<std::string, std::string>> metadata_fields;
    std::map<std::size_t, std::pair<std::string, std::string>> branch_metadata_fields;
    std::map<std::size_t, std::pair<std::string, std::string>> branch_resource_fields;
    std::map<std::size_t, std::string> branch_resource_source_lines;
    std::map<std::size_t, retained_channel_line_t> branch_channel_lines;
    std::map<std::size_t, retained_timing_declaration_t> timing_declarations;
    std::map<std::size_t, source_measure_line_t> branch_measure_ratio_lines;
    std::map<std::size_t, source_measure_line_t> branch_direct_bpm_lines;
    std::vector<source_measure_line_t> root_measure_ratio_lines;
    std::vector<source_measure_line_t> root_direct_bpm_lines;
    std::vector<std::pair<std::size_t, std::string>> root_timing_lines;
    std::vector<std::pair<std::size_t, std::string>> preserved_lines;
    std::optional<std::size_t> branch_source_begin;
    std::optional<std::size_t> branch_source_end;
};

auto replace_retained_header_value(std::string_view line, std::string_view expected_token,
                                   std::string_view expected_value, std::string_view replacement)
    -> std::optional<std::string> {
    if (line.empty() || line.front() != '#') {
        return std::nullopt;
    }
    const auto separator = line.find_first_of(" \t:", 1);
    if (separator == std::string_view::npos ||
        ascii_upper(std::string(line.substr(1, separator - 1))) != expected_token ||
        line.substr(separator + 1) != expected_value || has_line_break_or_nul(replacement) ||
        !valid_utf8(replacement)) {
        return std::nullopt;
    }
    std::string result(line.substr(0, separator + 1));
    result.append(replacement);
    return result;
}

auto is_branch_boundary(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::random || kind == bms_control_kind_t::switch_block ||
           kind == bms_control_kind_t::if_group || kind == bms_control_kind_t::if_branch ||
           kind == bms_control_kind_t::else_if_branch || kind == bms_control_kind_t::else_branch ||
           kind == bms_control_kind_t::case_branch || kind == bms_control_kind_t::default_branch;
}

auto is_measure_length_command(std::string_view token) -> std::optional<std::size_t> {
    if (token.size() != 5 || token.substr(3) != "02" ||
        !std::ranges::all_of(token.substr(0, 3),
                             [](char character) { return character >= '0' && character <= '9'; })) {
        return std::nullopt;
    }
    return static_cast<std::size_t>((token[0] - '0') * 100 + (token[1] - '0') * 10 +
                                    (token[2] - '0'));
}

auto is_timing_command(std::string_view token) -> bool {
    if (token == "BPM" ||
        (token.size() == 5 && token.starts_with("BPM") && base36_digit(token[3]) &&
         base36_digit(token[4])) ||
        (token.size() == 6 && token.starts_with("STOP") && base36_digit(token[4]) &&
         base36_digit(token[5]))) {
        return true;
    }
    return token.size() == 5 && token[0] >= '0' && token[0] <= '9' && token[1] >= '0' &&
           token[1] <= '9' && token[2] >= '0' && token[2] <= '9' &&
           (token.substr(3) == "03" || token.substr(3) == "08" || token.substr(3) == "09");
}

auto analyze_branch_tree(const bms_control_node_t& root) -> branch_tree_analysis_t {
    branch_tree_analysis_t result;
    const auto visit = [&](const auto& self, const bms_control_node_t& node,
                           bool inside_branch) -> void {
        if (node.kind == bms_control_kind_t::random || node.kind == bms_control_kind_t::if_group ||
            node.kind == bms_control_kind_t::if_branch ||
            node.kind == bms_control_kind_t::else_if_branch ||
            node.kind == bms_control_kind_t::else_branch ||
            node.kind == bms_control_kind_t::switch_block ||
            node.kind == bms_control_kind_t::case_branch ||
            node.kind == bms_control_kind_t::default_branch) {
            result.has_branches = true;
        }
        const bool node_is_branch_scoped = inside_branch;
        const bool is_synthetic_group =
            node.kind == bms_control_kind_t::root || node.kind == bms_control_kind_t::if_group;
        const bool is_control_statement = node.kind != bms_control_kind_t::root &&
                                          node.kind != bms_control_kind_t::if_group &&
                                          node.kind != bms_control_kind_t::raw_statement;
        if (!is_synthetic_group && node.source_order < std::numeric_limits<std::size_t>::max()) {
            if (node_is_branch_scoped) {
                result.branch_source_orders.insert(node.source_order);
            }
            if (is_control_statement && !node.original_text.empty()) {
                result.preserved_lines.emplace_back(node.source_order, node.original_text);
            } else if (node.kind == bms_control_kind_t::raw_statement &&
                       !node.original_text.empty()) {
                if (node_is_branch_scoped) {
                    result.branch_source_orders.insert(node.source_order);
                    const auto token = ascii_upper(node.command_token);
                    if (safe_metadata_token(token) &&
                        (!generated_or_channel_token(token) || token == "PLAYER")) {
                        result.metadata_fields.emplace(node.source_order,
                                                       std::pair{token, node.raw_value});
                        result.branch_metadata_fields.emplace(node.source_order,
                                                              std::pair{token, node.raw_value});
                    }
                    if (const auto measure = is_measure_length_command(token);
                        measure.has_value()) {
                        result.branch_measure_indices.insert(*measure);
                        result.branch_measure_ratio_lines.emplace(
                            node.source_order,
                            source_measure_line_t{.source_order = node.source_order,
                                                  .measure_index = *measure,
                                                  .text = node.original_text,
                                                  .raw_value = node.raw_value,
                                                  .source_span = node.source_span,
                                                  .line_ending = node.line_ending});
                    }
                    if (is_timing_command(token)) {
                        result.has_branch_timing = true;
                        result.retained_timing_source_orders.insert(node.source_order);
                        if ((token.size() == 5 && token.starts_with("BPM") &&
                             base36_digit(token[3]) && base36_digit(token[4])) ||
                            (token.size() == 6 && token.starts_with("STOP") &&
                             base36_digit(token[4]) && base36_digit(token[5]))) {
                            result.timing_declarations.emplace(
                                node.source_order,
                                retained_timing_declaration_t{node.source_order, token,
                                                              node.raw_value, node.original_text,
                                                              node.source_span, node.line_ending});
                        }
                        if (token == "BPM") {
                            result.branch_direct_bpm_lines.emplace(
                                node.source_order,
                                source_measure_line_t{.source_order = node.source_order,
                                                      .text = node.original_text,
                                                      .raw_value = node.raw_value,
                                                      .source_span = node.source_span,
                                                      .line_ending = node.line_ending});
                        }
                    }
                    if ((token.starts_with("WAV") || token.starts_with("BMP")) &&
                        token.size() == 5 && base36_digit(token[3]) && base36_digit(token[4])) {
                        result.branch_resource_ids.emplace(token.substr(0, 3), token.substr(3, 2));
                        result.branch_resource_fields.emplace(node.source_order,
                                                              std::pair{token, node.raw_value});
                        result.branch_resource_source_lines.emplace(node.source_order,
                                                                    node.original_text);
                    }
                    if (!is_measure_length_command(token).has_value() && token.size() == 5 &&
                        token[0] >= '0' && token[0] <= '9' && token[1] >= '0' && token[1] <= '9' &&
                        token[2] >= '0' && token[2] <= '9' && base36_digit(token[3]) &&
                        base36_digit(token[4]) && node.original_text.size() >= 7 &&
                        node.original_text[6] == ':') {
                        result.branch_channel_lines.emplace(
                            node.source_order,
                            retained_channel_line_t{
                                node.source_order,
                                static_cast<std::size_t>((token[0] - '0') * 100 +
                                                         (token[1] - '0') * 10 + (token[2] - '0')),
                                token.substr(3, 2), node.original_text, node.raw_value,
                                node.source_span, node.line_ending});
                    }
                    result.preserved_lines.emplace_back(node.source_order, node.original_text);
                } else {
                    const auto token = ascii_upper(node.command_token);
                    if (safe_metadata_token(token)) {
                        result.metadata_fields.emplace(node.source_order,
                                                       std::pair{token, node.raw_value});
                    }
                    if (const auto measure = is_measure_length_command(token);
                        measure.has_value()) {
                        result.root_measure_ratio_lines.push_back(
                            {node.source_order, *measure, node.original_text, node.raw_value,
                             node.source_span, node.line_ending});
                    }
                    if (is_timing_command(token)) {
                        result.root_timing_lines.emplace_back(node.source_order,
                                                              node.original_text);
                        if ((token.size() == 5 && token.starts_with("BPM") &&
                             base36_digit(token[3]) && base36_digit(token[4])) ||
                            (token.size() == 6 && token.starts_with("STOP") &&
                             base36_digit(token[4]) && base36_digit(token[5]))) {
                            result.timing_declarations.emplace(
                                node.source_order,
                                retained_timing_declaration_t{node.source_order, token,
                                                              node.raw_value, node.original_text,
                                                              node.source_span, node.line_ending});
                        }
                        if (token == "BPM") {
                            result.root_direct_bpm_lines.push_back(
                                {.source_order = node.source_order,
                                 .text = node.original_text,
                                 .raw_value = node.raw_value,
                                 .source_span = node.source_span,
                                 .line_ending = node.line_ending});
                        }
                    } else if (safe_metadata_token(token) || generated_or_channel_token(token) ||
                               is_measure_length_command(token).has_value()) {
                        result.root_source_orders.insert(node.source_order);
                    }
                    if (!safe_metadata_token(token) && !generated_or_channel_token(token)) {
                        result.has_unmodeled_root_statement = true;
                    }
                }
            }
        }
        if (node.close_text.has_value() && node.close_source_order.has_value()) {
            result.preserved_lines.emplace_back(*node.close_source_order, *node.close_text);
        }

        const bool descendants_are_branch_scoped =
            node_is_branch_scoped || is_branch_boundary(node.kind);
        for (const auto& child : node.children) {
            self(self, child, descendants_are_branch_scoped);
        }
    };
    visit(visit, root, false);
    if (!result.preserved_lines.empty()) {
        const auto [first, last] = std::minmax_element(
            result.preserved_lines.begin(), result.preserved_lines.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; });
        result.branch_source_begin = first->first;
        result.branch_source_end = last->first;
    }
    if (result.has_branches && result.has_branch_timing) {
        result.preserved_lines.insert(result.preserved_lines.end(),
                                      result.root_timing_lines.begin(),
                                      result.root_timing_lines.end());
        for (const auto& [source_order, line] : result.root_timing_lines) {
            (void)line;
            result.retained_timing_source_orders.insert(source_order);
        }
    } else {
        result.retained_timing_source_orders.clear();
    }
    std::ranges::stable_sort(result.preserved_lines, {},
                             [](const auto& line) { return line.first; });
    return result;
}

auto replace_retained_measure_length(std::string_view line, std::size_t expected_measure,
                                     std::string_view expected_value, std::string_view replacement)
    -> std::optional<std::string> {
    if (line.size() < 7 || line.front() != '#' || line[6] != ':' ||
        is_measure_length_command(ascii_upper(std::string(line.substr(1, 5)))) !=
            std::optional<std::size_t>{expected_measure} ||
        line.substr(7) != expected_value || has_line_break_or_nul(replacement) ||
        !valid_utf8(replacement)) {
        return std::nullopt;
    }
    std::string result(line.substr(0, 7));
    result.append(replacement);
    return result;
}

auto has_bms_source_provenance(const source_provenance_t& provenance) -> bool {
    return provenance.family == format_family_t::bms && !provenance.source_spelling.empty();
}

auto fraction_at(const measure_index_t& index, local_position_t position, std::string path)
    -> std::pair<std::uint64_t, std::uint64_t> {
    const auto entry =
        std::ranges::find(index.entries, position.measure, &measure_index_entry_t::id);
    if (entry == index.entries.end() || position.row >= entry->row_capacity) {
        fail(bms_write_diagnostic_code_t::invalid_geometry,
             "event position is outside the chart's measure geometry", std::move(path));
    }
    const auto divisor = std::gcd(position.row, entry->row_capacity);
    return {position.row / divisor, entry->row_capacity / divisor};
}

auto layout_for_export(const chart_document_t& chart, const bms_write_options_t& options,
                       std::vector<bms_write_diagnostic_t>& diagnostics) -> lane_layout_t {
    if (options.lane_profile_id.has_value()) {
        const auto detection = detect_bms_lane_profile({
            .explicit_profile_id = options.lane_profile_id,
        });
        if (!detection.selected_profile.has_value()) {
            fail(bms_write_diagnostic_code_t::missing_profile,
                 "requested BMS lane profile is unknown", "$.charts[].lane_layouts");
        }
        return detection.selected_profile->layout;
    }
    if (chart.lane_layouts.empty()) {
        fail(bms_write_diagnostic_code_t::missing_profile,
             "normalized BMS export requires an authored lane map or an explicit profile",
             "$.charts[].lane_layouts");
    }
    if (chart.lane_layouts.size() != 1 || chart.measures.empty() ||
        chart.lane_layouts.front().position.measure != chart.measures.front().id ||
        chart.lane_layouts.front().position.row != 0) {
        fail(bms_write_diagnostic_code_t::unsupported_feature,
             "BMS lane-layout changes are not supported by this writer slice",
             "$.charts[].lane_layouts");
    }
    (void)diagnostics;
    return chart.lane_layouts.front().layout;
}

auto normalized_write(const song_document_t& song, const chart_document_t& chart,
                      const bms_write_options_t& options,
                      std::vector<bms_write_diagnostic_t>& diagnostics) -> std::string {
    auto export_scope = song;
    export_scope.charts.clear();
    export_scope.charts.push_back(chart);
    export_scope.output_groups.clear();
    const auto validation = validate_document(export_scope);
    const auto validation_error = std::ranges::find_if(
        validation, [](const auto& item) { return item.severity == diagnostic_severity_t::error; });
    if (validation_error != validation.end()) {
        fail(bms_write_diagnostic_code_t::invalid_document,
             "document validation failed before BMS export: " + validation_error->message, "$");
    }
    if (chart.measures.size() > 1000) {
        fail(bms_write_diagnostic_code_t::resource_limit,
             "BMS measure indices are limited to 000 through 999", "$.charts[].measures");
    }

    const auto retained_branch_tree = chart.bms_control_tree.has_value()
                                          ? analyze_branch_tree(*chart.bms_control_tree)
                                          : branch_tree_analysis_t{};
    if (retained_branch_tree.has_branches &&
        options.branch_policy == bms_branch_policy_t::flatten_active &&
        chart.active_branch_selection.empty()) {
        fail(bms_write_diagnostic_code_t::unsupported_branch_policy,
             "active-projection flattening requires explicit or fixed choices for the retained "
             "branch tree",
             "$.charts[].active_branch_selection");
    }
    if (retained_branch_tree.has_branches &&
        options.branch_policy == bms_branch_policy_t::flatten_active) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "the explicitly selected active BMS projection is flattened and inactive branches "
             "are omitted",
             "$.charts[].bms_control_tree");
    }
    const auto branch_tree = options.branch_policy == bms_branch_policy_t::flatten_active
                                 ? branch_tree_analysis_t{}
                                 : retained_branch_tree;
    const bool flattening_active_projection =
        options.branch_policy == bms_branch_policy_t::flatten_active;
    if (!chart.active_branch_selection.empty() && !retained_branch_tree.has_branches) {
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "active branch selections have no retained source tree to preserve",
             "$.charts[].active_branch_selection");
    }
    std::map<std::size_t, std::string> branch_measure_ratio_edits;
    const auto active_branch_orders = [&]() -> std::optional<std::set<std::size_t>> {
        if (!branch_tree.has_branches ||
            options.branch_policy != bms_branch_policy_t::preserve_tree) {
            return std::nullopt;
        }
        const auto projection =
            project_bms_branch_tree(*chart.bms_control_tree, chart.active_branch_selection);
        if (!projection) {
            return std::nullopt;
        }
        return std::set<std::size_t>(projection.active_statement_source_orders.begin(),
                                     projection.active_statement_source_orders.end());
    }();
    std::map<std::size_t, std::pair<std::size_t, rational_t>> effective_measure_sources;
    const auto retain_effective_measure_source = [&](const source_measure_line_t& source,
                                                     bool active) {
        if (!active) {
            return;
        }
        const auto parsed = parse_measure_length(source.raw_value);
        if (!parsed.value.has_value()) {
            return;
        }
        auto& current = effective_measure_sources[source.measure_index];
        if (current.first <= source.source_order) {
            current = {source.source_order, *parsed.value};
        }
    };
    for (const auto& source : branch_tree.root_measure_ratio_lines) {
        retain_effective_measure_source(source, true);
    }
    if (active_branch_orders.has_value()) {
        for (const auto& [source_order, source] : branch_tree.branch_measure_ratio_lines) {
            retain_effective_measure_source(source, active_branch_orders->contains(source_order));
        }
    }
    const auto branch_source_bytes = source_provenance_bytes(song, chart);
    for (const auto measure_index : branch_tree.branch_measure_indices) {
        if (measure_index >= chart.measures.size()) {
            continue;
        }
        const auto projection =
            std::ranges::find(chart.bms_measure_length_projections, measure_index,
                              &bms_measure_length_projection_t::measure_index);
        if (projection == chart.bms_measure_length_projections.end()) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "a branch-controlled measure length has no unique import baseline",
                 "$.charts[].measures[" + std::to_string(measure_index) + "].definition.length");
        }
        const auto& current_length = chart.measures[measure_index].definition.length;
        if (current_length == projection->imported_length) {
            continue;
        }
        const auto source = branch_tree.branch_measure_ratio_lines.find(projection->source_order);
        const auto effective_source = effective_measure_sources.find(measure_index);
        if (!active_branch_orders.has_value() ||
            source == branch_tree.branch_measure_ratio_lines.end() ||
            source->second.measure_index != measure_index ||
            !active_branch_orders->contains(projection->source_order) ||
            effective_source == effective_measure_sources.end() ||
            effective_source->second.first != projection->source_order ||
            effective_source->second.second != projection->imported_length ||
            !branch_source_bytes ||
            !source_measure_line_matches_bytes(source->second, *branch_source_bytes)) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "an edited branch-controlled measure length does not map to one unchanged "
                 "active source statement",
                 "$.charts[].measures[" + std::to_string(measure_index) + "].definition.length");
        }
        branch_measure_ratio_edits.emplace(
            projection->source_order,
            exact_decimal(current_length.numerator, current_length.denominator,
                          "$.charts[].measures[" + std::to_string(measure_index) +
                              "].definition.length"));
    }
    if (branch_tree.has_branches) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "branch content remains source-backed; only uniquely attributable active event and "
             "timing edits are applied in place",
             "$.charts[].bms_control_tree");
    }
    if (branch_tree.has_unmodeled_root_statement) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "unmodeled root-level raw source lines were omitted from normalized BMS output",
             "$.charts[].bms_control_tree");
    }
    const auto is_branch_provenance = [&](const source_provenance_t& provenance) {
        return has_bms_source_provenance(provenance) &&
               branch_tree.branch_source_orders.contains(provenance.source_order);
    };
    const auto is_retained_timing_provenance = [&](const source_provenance_t& provenance) {
        return has_bms_source_provenance(provenance) && branch_tree.has_branches &&
               branch_tree.retained_timing_source_orders.contains(provenance.source_order);
    };
    const auto is_branch_metadata_field = [&](const format_metadata_t& group,
                                              std::size_t field_index) {
        if (group.family != format_family_t::bms ||
            field_index >= group.ordered_field_source_orders.size()) {
            return false;
        }
        return branch_tree.branch_source_orders.contains(
            group.ordered_field_source_orders[field_index]);
    };

    std::optional<std::pair<std::size_t, signed_rational_t>> effective_direct_bpm_source;
    const auto retain_effective_direct_bpm_source = [&](const source_measure_line_t& source,
                                                        bool active) {
        if (!active) {
            return;
        }
        const auto parsed = parse_measure_length(source.raw_value);
        if (!parsed.value.has_value() ||
            parsed.value->numerator >
                static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return;
        }
        if (!effective_direct_bpm_source.has_value() ||
            effective_direct_bpm_source->first <= source.source_order) {
            effective_direct_bpm_source =
                std::pair{source.source_order,
                          signed_rational_t{static_cast<std::int64_t>(parsed.value->numerator),
                                            parsed.value->denominator}};
        }
    };
    for (const auto& source : branch_tree.root_direct_bpm_lines) {
        retain_effective_direct_bpm_source(source, true);
    }
    if (active_branch_orders.has_value()) {
        for (const auto& [source_order, source] : branch_tree.branch_direct_bpm_lines) {
            retain_effective_direct_bpm_source(source,
                                               active_branch_orders->contains(source_order));
        }
    }
    std::map<std::size_t, std::string> branch_direct_bpm_edits;
    for (const auto& bpm : chart.bpms) {
        if (!is_retained_timing_provenance(bpm.provenance)) {
            continue;
        }
        const auto projection =
            std::ranges::find(chart.bms_bpm_projections, bpm.id, &bms_bpm_projection_t::event);
        if (projection == chart.bms_bpm_projections.end() ||
            projection->source_order != bpm.provenance.source_order) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "source-backed BMS BPM timing was edited structurally or lacks its import "
                 "baseline",
                 "$.charts[].bpms");
        }
        const auto channel_field = branch_channel_slot_field(bpm.provenance.source_field);
        const bool branch_channel_bpm =
            is_branch_provenance(bpm.provenance) && channel_field.has_value() &&
            (channel_field->channel == "03" || channel_field->channel == "08");
        if (branch_channel_bpm) {
            continue;
        }
        if (projection->imported_position != bpm.position) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "a retained direct BPM declaration cannot be moved as a channel event",
                 "$.charts[].bpms");
        }
        if (projection->imported_beats_per_minute == bpm.beats_per_minute) {
            continue;
        }
        const auto source = branch_tree.branch_direct_bpm_lines.find(projection->source_order);
        if (source == branch_tree.branch_direct_bpm_lines.end()) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch BPM does not map to a retained direct-BPM source statement",
                 "$.charts[].bpms");
        }
        if (!active_branch_orders.has_value() ||
            !active_branch_orders->contains(projection->source_order) ||
            !effective_direct_bpm_source.has_value() ||
            effective_direct_bpm_source->first != projection->source_order ||
            effective_direct_bpm_source->second != projection->imported_beats_per_minute) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch BPM is not the unique effective declaration in the active "
                 "projection",
                 "$.charts[].bpms");
        }
        if (bpm.provenance.source_field != "BPM" ||
            bpm.provenance.source_spelling != source->second.text || !branch_source_bytes ||
            !source_measure_line_matches_bytes(source->second, *branch_source_bytes)) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch BPM source provenance does not match the retained source bytes",
                 "$.charts[].bpms");
        }
        branch_direct_bpm_edits.emplace(
            projection->source_order,
            exact_decimal(bpm.beats_per_minute, "$.charts[].bpms[].beats_per_minute"));
    }
    for (const auto& pause : chart.pauses) {
        if (!is_retained_timing_provenance(pause.provenance)) {
            continue;
        }
        const auto projection = std::ranges::find(chart.bms_pause_projections, pause.id,
                                                  &bms_pause_projection_t::event);
        if (projection == chart.bms_pause_projections.end() ||
            projection->source_order != pause.provenance.source_order) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "source-backed BMS pause timing was edited or lacks its import baseline; direct "
                 "branch timing editing requires a retained channel event",
                 "$.charts[].pauses");
        }
        const auto channel_field = branch_channel_slot_field(pause.provenance.source_field);
        if (is_branch_provenance(pause.provenance) && channel_field.has_value() &&
            channel_field->channel == "09") {
            continue;
        }
        if (projection->imported_position != pause.position ||
            projection->imported_kind != pause.kind ||
            projection->imported_duration != pause.duration ||
            projection->imported_source_unit != pause.source_unit) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "source-backed BMS pause timing was edited without an attributable branch "
                 "channel event",
                 "$.charts[].pauses");
        }
    }

    const auto has_unsupported_root_timing = [&](const auto& events) {
        return std::ranges::any_of(events, [&](const auto& event) {
            return !is_branch_provenance(event.provenance) &&
                   !is_retained_timing_provenance(event.provenance);
        });
    };
    if ((!flattening_active_projection && has_unsupported_root_timing(chart.pauses)) ||
        has_unsupported_root_timing(chart.warps) || has_unsupported_root_timing(chart.scrolls) ||
        has_unsupported_root_timing(chart.speeds) || has_unsupported_root_timing(chart.kiai) ||
        !chart.timing_anchors.empty() || !chart.sections.empty() || !chart.sound_channels.empty()) {
        fail(bms_write_diagnostic_code_t::unsupported_feature,
             "chart contains timing or media features outside the normalized writer slice",
             "$.charts[]");
    }
    if (!chart.bookmarks.empty()) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "BMS has no supported bookmark representation; bookmarks are omitted",
             "$.charts[].bookmarks");
    }
    if (!chart.semantic_layers.empty() ||
        std::ranges::any_of(chart.measures, [](const auto& measure) {
            return std::ranges::any_of(
                measure.notes, [](const auto& note) { return !note.semantic_layers.empty(); });
        })) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "semantic note layers are omitted while their underlying note types are retained",
             "$.charts[].semantic_layers");
    }
    const auto title = song.metadata.title.empty() ? chart.metadata.name : song.metadata.title;
    const auto artist = song.metadata.artist.empty() ? chart.metadata.author : song.metadata.artist;
    if (has_line_break_or_nul(title) || has_line_break_or_nul(artist) ||
        has_edge_whitespace(title) || has_edge_whitespace(artist) || !valid_utf8(title) ||
        !valid_utf8(artist)) {
        fail(bms_write_diagnostic_code_t::invalid_document,
             "BMS title and artist must be valid UTF-8 single-line values without edge whitespace",
             "$.metadata");
    }

    const auto layout = layout_for_export(chart, options, diagnostics);
    std::map<std::uint64_t, std::string> lane_channels;
    std::map<std::uint64_t, std::string> long_note_channels;
    std::set<std::string> used_channels;
    for (const auto& lane : layout.lanes) {
        if (lane.bms_channels.visible.has_value()) {
            auto channel = *lane.bms_channels.visible;
            for (auto& character : channel) {
                if (character >= 'a' && character <= 'z') {
                    character = static_cast<char>(character - 'a' + 'A');
                }
            }
            if (channel.size() != 2 || !base36_digit(channel[0]) || !base36_digit(channel[1]) ||
                !used_channels.insert(channel).second) {
                fail(bms_write_diagnostic_code_t::invalid_document,
                     "lane profile has an invalid or duplicate visible BMS channel",
                     "$.charts[].lane_layouts");
            }
            lane_channels.emplace(lane.id.value, std::move(channel));
        }
        if (lane.bms_channels.long_note.has_value()) {
            auto channel = *lane.bms_channels.long_note;
            for (auto& character : channel) {
                if (character >= 'a' && character <= 'z') {
                    character = static_cast<char>(character - 'a' + 'A');
                }
            }
            if (channel.size() != 2 || !base36_digit(channel[0]) || !base36_digit(channel[1]) ||
                (channel[0] != '5' && channel[0] != '6') || !used_channels.insert(channel).second) {
                fail(bms_write_diagnostic_code_t::invalid_document,
                     "lane profile has an invalid or duplicate long-note BMS channel",
                     "$.charts[].lane_layouts");
            }
            long_note_channels.emplace(lane.id.value, std::move(channel));
        }
    }

    std::map<std::uint64_t, std::string> audio_ids;
    std::map<std::uint64_t, std::string> visual_ids;
    std::map<std::string, std::size_t> next_ids{{"WAV", 1}, {"BMP", 1}};
    std::map<std::string, std::set<std::string>> assigned_ids;
    std::set<std::uint64_t> branch_resource_ids;
    std::map<std::size_t, std::string> branch_resource_edits;
    struct resource_declaration_t {
        std::string name_space;
        const resource_entry_t* resource{};
        bool branch_scoped{};
    };
    std::vector<resource_declaration_t> declarations;
    declarations.reserve(song.resources.size());
    for (const auto& resource : song.resources) {
        const auto audio = std::ranges::find(song.audio_assets, resource.asset, &audio_asset_t::id);
        const auto visual =
            std::ranges::find(song.visual_assets, resource.asset, &visual_asset_t::id);
        const bool is_audio = audio != song.audio_assets.end();
        const bool is_visual = visual != song.visual_assets.end();
        if (is_audio == is_visual || resource.missing) {
            fail(bms_write_diagnostic_code_t::unresolved_resource,
                 "resource must refer to exactly one available audio or visual asset",
                 "$.resources[]");
        }
        const auto name_space = is_audio ? std::string("WAV") : std::string("BMP");
        if (resource.name.empty() || has_line_break_or_nul(resource.name) ||
            has_edge_whitespace(resource.name) || !valid_utf8(resource.name)) {
            fail(bms_write_diagnostic_code_t::unresolved_resource,
                 "resource path must be a valid UTF-8 single-line value without edge whitespace",
                 "$.resources[].name");
        }
        const bool branch_scoped = is_branch_provenance(resource.provenance);
        auto identifier = bms_identity(resource, name_space);
        if (branch_scoped) {
            const auto retained =
                branch_tree.branch_resource_fields.find(resource.provenance.source_order);
            const auto retained_line =
                branch_tree.branch_resource_source_lines.find(resource.provenance.source_order);
            const auto expected_token =
                identifier.has_value() ? name_space + *identifier : std::string{};
            if (!identifier.has_value() || retained == branch_tree.branch_resource_fields.end() ||
                retained_line == branch_tree.branch_resource_source_lines.end() ||
                retained->second.first != expected_token ||
                ascii_upper(resource.provenance.source_field) != expected_token ||
                resource.provenance.source_spelling != retained_line->second ||
                !branch_tree.branch_resource_ids.contains({name_space, *identifier}) ||
                assigned_ids[name_space].contains(*identifier)) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "branch resources must retain one matching source statement, namespace, and "
                     "identifier",
                     "$.resources[].target_identities");
            }
            if (resource.name != trim_ascii_whitespace(retained->second.second)) {
                branch_resource_edits.emplace(resource.provenance.source_order, resource.name);
            }
        } else if (!identifier.has_value() || assigned_ids[name_space].contains(*identifier) ||
                   branch_tree.branch_resource_ids.contains({name_space, *identifier})) {
            const auto old = identifier;
            auto& next = next_ids[name_space];
            while (next < 1296 &&
                   (assigned_ids[name_space].contains(base36_id(next)) ||
                    branch_tree.branch_resource_ids.contains({name_space, base36_id(next)}))) {
                ++next;
            }
            if (next >= 1296) {
                fail(bms_write_diagnostic_code_t::resource_limit,
                     "BMS resource namespace exceeds 1295 nonzero two-digit identifiers",
                     "$.resources");
            }
            identifier = base36_id(next++);
            if (old.has_value()) {
                warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                     "colliding BMS resource identifier was reassigned deterministically",
                     "$.resources[].target_identities");
            }
        }
        assigned_ids[name_space].insert(*identifier);
        if (is_audio)
            audio_ids.emplace(resource.id.value, *identifier);
        if (is_visual)
            visual_ids.emplace(resource.id.value, *identifier);
        if (branch_scoped)
            branch_resource_ids.insert(resource.id.value);
        declarations.push_back({name_space, &resource, branch_scoped});
    }

    const auto measure_index_result = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measure_index_result)) {
        fail(bms_write_diagnostic_code_t::invalid_geometry,
             "chart measure geometry cannot be represented safely", "$.charts[].measures");
    }
    const auto& index = std::get<measure_index_t>(measure_index_result);

    struct branch_channel_edit_t {
        const retained_channel_line_t* source{};
        std::string text;
        std::size_t payload_begin{};
        std::string payload;
        std::set<std::size_t> cleared_slots;
        std::map<std::size_t, std::string> written_slots;
        std::map<std::size_t, std::size_t> written_slot_origins;
    };
    std::map<std::size_t, branch_channel_edit_t> branch_channel_edits;
    std::map<std::pair<std::size_t, std::size_t>, std::string> branch_channel_slot_claims;
    const auto claim_branch_channel_slot = [&](const source_provenance_t& provenance,
                                               const channel_slot_field_t& field,
                                               std::string_view path) {
        const auto source = branch_tree.branch_channel_lines.find(provenance.source_order);
        if (source == branch_tree.branch_channel_lines.end() ||
            !branch_tree.branch_source_orders.contains(provenance.source_order) ||
            !active_branch_orders.has_value() ||
            !active_branch_orders->contains(provenance.source_order) ||
            source->second.channel != field.channel ||
            provenance.source_spelling != source->second.text ||
            !has_bms_source_provenance(provenance)) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "branch channel event does not identify one active retained source line",
                 std::string(path));
        }
        const auto expected_measure = (source->second.measure_index < 10    ? std::string("00")
                                       : source->second.measure_index < 100 ? std::string("0")
                                                                            : std::string()) +
                                      std::to_string(source->second.measure_index);
        const auto separator = source->second.text.find(':');
        if (separator != 6 || source->second.text.substr(1, 3) != expected_measure ||
            ascii_upper(source->second.text.substr(4, 2)) != source->second.channel ||
            source->second.text.substr(7) != source->second.raw_value ||
            field.slot >= trim_ascii_whitespace(source->second.raw_value).size() / 2 ||
            trim_ascii_whitespace(source->second.raw_value).size() % 2 != 0 ||
            ascii_upper(std::string(
                trim_ascii_whitespace(source->second.raw_value).substr(field.slot * 2, 2))) !=
                field.object_id) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "branch channel event slot does not match its retained source payload",
                 std::string(path));
        }
        const auto [claim, inserted] = branch_channel_slot_claims.emplace(
            std::pair{provenance.source_order, field.slot}, std::string(path));
        if (!inserted && claim->second != path) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "more than one active semantic event claims a retained channel slot",
                 std::string(path));
        }
        return &source->second;
    };
    const auto edit_branch_channel_slot = [&](std::size_t source_order,
                                              const retained_channel_line_t& source,
                                              std::size_t old_slot, std::size_t new_slot,
                                              std::string_view object_id, std::string_view path) {
        const auto original_bytes = branch_source_bytes;
        if (!original_bytes || !retained_line_matches_bytes(source.text, source.source_span,
                                                            source.line_ending, *original_bytes)) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch channel line does not match unchanged imported source bytes",
                 std::string(path));
        }
        auto [edit, inserted] = branch_channel_edits.try_emplace(source_order);
        if (inserted) {
            const auto colon = source.text.find(':');
            if (colon != 6 || source.text.substr(7) != source.raw_value ||
                trim_ascii_whitespace(source.raw_value).empty() ||
                trim_ascii_whitespace(source.raw_value).size() % 2 != 0 ||
                !std::ranges::all_of(trim_ascii_whitespace(source.raw_value), base36_digit)) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "edited branch channel payload is not a valid retained BMS slot line",
                     std::string(path));
            }
            const auto untrimmed_payload = std::string_view(source.text).substr(7);
            const auto trimmed_payload = trim_ascii_whitespace(untrimmed_payload);
            edit->second.source = &source;
            edit->second.text = source.text;
            edit->second.payload_begin =
                7 + static_cast<std::size_t>(trimmed_payload.data() - untrimmed_payload.data());
            edit->second.payload = std::string(trimmed_payload);
        }
        auto& patch = edit->second;
        const auto slot_count = patch.payload.size() / 2;
        if (old_slot >= slot_count || new_slot >= slot_count || object_id.size() != 2 ||
            !std::ranges::all_of(object_id, base36_digit)) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch event position or identifier does not fit its retained source line",
                 std::string(path));
        }
        if (old_slot != new_slot) {
            patch.cleared_slots.insert(old_slot);
        }
        const auto normalized_object_id = ascii_upper(std::string(object_id));
        const auto [written, write_inserted] =
            patch.written_slots.emplace(new_slot, normalized_object_id);
        if (!write_inserted && written->second != ascii_upper(std::string(object_id))) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch events collide in one retained channel slot", std::string(path));
        }
        const auto [origin, origin_inserted] =
            patch.written_slot_origins.emplace(new_slot, old_slot);
        if (!origin_inserted && origin->second != old_slot) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "multiple edited branch events target one retained channel slot",
                 std::string(path));
        }
    };
    const auto expected_branch_slot_position =
        [&](const retained_channel_line_t& source,
            std::size_t slot) -> std::optional<local_position_t> {
        if (source.measure_index >= chart.measures.size()) {
            return std::nullopt;
        }
        auto measure = chart.measures[source.measure_index];
        const auto measure_projection =
            std::ranges::find(chart.bms_measure_length_projections, source.measure_index,
                              &bms_measure_length_projection_t::measure_index);
        if (measure_projection != chart.bms_measure_length_projections.end()) {
            measure.definition.length = measure_projection->imported_length;
            measure.definition.meter.reset();
        }
        const auto capacity =
            validate_measure(chart.divisions_per_standard_measure, measure.definition);
        const auto slot_count = trim_ascii_whitespace(source.raw_value).size() / 2;
        if (!capacity.row_capacity.has_value() || slot_count == 0 ||
            *capacity.row_capacity % slot_count != 0 || slot >= slot_count) {
            return std::nullopt;
        }
        return local_position_t{measure.id, static_cast<std::uint64_t>(slot) *
                                                (*capacity.row_capacity / slot_count)};
    };
    const auto branch_slot_for_position = [&](const retained_channel_line_t& source,
                                              local_position_t position,
                                              std::string_view path) -> std::size_t {
        if (source.measure_index >= chart.measures.size() ||
            chart.measures[source.measure_index].id != position.measure) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "branch channel event cannot be moved to a different measure", std::string(path));
        }
        const auto [numerator, denominator] = fraction_at(index, position, std::string(path));
        const auto slot_count = trim_ascii_whitespace(source.raw_value).size() / 2;
        if (slot_count == 0 || numerator > std::numeric_limits<std::uint64_t>::max() / slot_count ||
            (numerator * slot_count) % denominator != 0 ||
            numerator * slot_count / denominator >= slot_count) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch event position does not map to a slot on its retained source line",
                 std::string(path));
        }
        return static_cast<std::size_t>(numerator * slot_count / denominator);
    };

    std::map<std::size_t, std::string> branch_timing_declaration_edits;
    const auto effective_timing_declaration =
        [&](std::string_view token, std::size_t event_source_order,
            signed_rational_t imported_value) -> const retained_timing_declaration_t* {
        const retained_timing_declaration_t* effective{};
        for (const auto& [source_order, declaration] : branch_tree.timing_declarations) {
            if (declaration.token != token || source_order >= event_source_order ||
                (branch_tree.branch_source_orders.contains(source_order) &&
                 (!active_branch_orders.has_value() ||
                  !active_branch_orders->contains(source_order)))) {
                continue;
            }
            const auto parsed =
                parse_positive_timing_value(trim_ascii_whitespace(declaration.raw_value));
            if (!parsed.has_value() || *parsed != imported_value) {
                continue;
            }
            if (effective == nullptr || effective->source_order < source_order) {
                effective = &declaration;
            }
        }
        return effective;
    };
    const auto validate_branch_timing_declaration_edit =
        [&](const retained_timing_declaration_t& declaration, std::string_view identifier,
            std::string_view channel, std::string_view path) {
            if (!branch_source_bytes ||
                !retained_line_matches_bytes(declaration.text, declaration.source_span,
                                             declaration.line_ending, *branch_source_bytes)) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "edited branch timing declaration does not match unchanged imported source "
                     "bytes",
                     std::string(path));
            }
            if (branch_tree.branch_source_orders.contains(declaration.source_order)) {
                if (!active_branch_orders.has_value() ||
                    !active_branch_orders->contains(declaration.source_order)) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "timing declaration is not inside the selected active branch",
                         std::string(path));
                }
                return;
            }
            for (const auto& [source_order, line] : branch_tree.branch_channel_lines) {
                if (active_branch_orders.has_value() &&
                    active_branch_orders->contains(source_order)) {
                    continue;
                }
                if (line.channel != channel || source_order <= declaration.source_order) {
                    continue;
                }
                const auto payload = trim_ascii_whitespace(line.raw_value);
                for (std::size_t slot = 0; slot + 1 < payload.size(); slot += 2) {
                    if (payload.substr(slot, 2) == identifier) {
                        fail(bms_write_diagnostic_code_t::branch_provenance,
                             "root timing declaration is also used by an inactive sibling "
                             "branch, so editing it would change that sibling",
                             std::string(path));
                    }
                }
            }
        };
    const auto declaration_referenced_by_bpm = [&](const retained_timing_declaration_t& target,
                                                   std::string_view identifier,
                                                   signed_rational_t replacement,
                                                   std::string_view path) {
        for (const auto& bpm : chart.bpms) {
            const auto field = branch_channel_slot_field(bpm.provenance.source_field);
            if (!field.has_value() || field->channel != "08" || field->object_id != identifier) {
                continue;
            }
            const auto projection =
                std::ranges::find(chart.bms_bpm_projections, bpm.id, &bms_bpm_projection_t::event);
            if (projection == chart.bms_bpm_projections.end()) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "extended BPM event has no unique import baseline", std::string(path));
            }
            const auto* declaration = effective_timing_declaration(
                "BPM" + std::string(identifier), bpm.provenance.source_order,
                projection->imported_beats_per_minute);
            if (declaration != nullptr && declaration->source_order == target.source_order &&
                bpm.beats_per_minute != replacement) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "all active events bound to one extended BPM declaration must agree on its "
                     "replacement value",
                     std::string(path));
            }
        }
    };
    const auto declaration_referenced_by_pause = [&](const retained_timing_declaration_t& target,
                                                     std::string_view identifier,
                                                     signed_rational_t replacement,
                                                     std::string_view path) {
        for (const auto& pause : chart.pauses) {
            const auto field = branch_channel_slot_field(pause.provenance.source_field);
            if (!field.has_value() || field->channel != "09" || field->object_id != identifier) {
                continue;
            }
            const auto projection = std::ranges::find(chart.bms_pause_projections, pause.id,
                                                      &bms_pause_projection_t::event);
            if (projection == chart.bms_pause_projections.end()) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "STOP event has no unique import baseline", std::string(path));
            }
            const auto* declaration = effective_timing_declaration("STOP" + std::string(identifier),
                                                                   pause.provenance.source_order,
                                                                   projection->imported_duration);
            if (declaration != nullptr && declaration->source_order == target.source_order &&
                pause.duration != replacement) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "all active pauses bound to one STOP declaration must agree on its "
                     "replacement value",
                     std::string(path));
            }
        }
    };

    for (const auto& bpm : chart.bpms) {
        if (!is_branch_provenance(bpm.provenance)) {
            continue;
        }
        if (bpm.provenance.source_field == "BPM") {
            continue;
        }
        const auto projection =
            std::ranges::find(chart.bms_bpm_projections, bpm.id, &bms_bpm_projection_t::event);
        const auto field = branch_channel_slot_field(bpm.provenance.source_field);
        if (projection == chart.bms_bpm_projections.end() ||
            projection->source_order != bpm.provenance.source_order || !field.has_value() ||
            (field->channel != "03" && field->channel != "08")) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch BPM does not identify one retained direct or extended BPM slot",
                 "$.charts[].bpms");
        }
        const auto* source = claim_branch_channel_slot(
            bpm.provenance, *field, "$.charts[].bpms[" + std::to_string(bpm.id.value) + "]");
        const auto original_position = expected_branch_slot_position(*source, field->slot);
        if (!original_position.has_value() || *original_position != projection->imported_position) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "branch BPM event slot does not match its imported position", "$.charts[].bpms");
        }
        const auto target_slot =
            bpm.position == projection->imported_position
                ? field->slot
                : branch_slot_for_position(*source, bpm.position, "$.charts[].bpms[].position");
        const bool position_changed = target_slot != field->slot;
        const bool value_changed = bpm.beats_per_minute != projection->imported_beats_per_minute;
        if (!position_changed && !value_changed) {
            continue;
        }
        if (field->channel == "03") {
            if (bpm.beats_per_minute.numerator <= 0 || bpm.beats_per_minute.denominator != 1 ||
                bpm.beats_per_minute.numerator > 255) {
                fail(bms_write_diagnostic_code_t::unrepresentable_number,
                     "direct branch BPM channel events require an integer from 1 through 255",
                     "$.charts[].bpms[].beats_per_minute");
            }
            constexpr std::string_view hex = "0123456789ABCDEF";
            const auto value = static_cast<unsigned>(bpm.beats_per_minute.numerator);
            const std::string object_id{hex[value >> 4U], hex[value & 0x0fU]};
            edit_branch_channel_slot(bpm.provenance.source_order, *source, field->slot, target_slot,
                                     object_id, "$.charts[].bpms");
            continue;
        }
        if (value_changed) {
            const auto* declaration =
                effective_timing_declaration("BPM" + field->object_id, bpm.provenance.source_order,
                                             projection->imported_beats_per_minute);
            if (declaration == nullptr) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "extended BPM event does not resolve to one active retained declaration",
                     "$.charts[].bpms");
            }
            validate_branch_timing_declaration_edit(*declaration, field->object_id, "08",
                                                    "$.charts[].bpms");
            declaration_referenced_by_bpm(*declaration, field->object_id, bpm.beats_per_minute,
                                          "$.charts[].bpms");
            branch_timing_declaration_edits.insert_or_assign(
                declaration->source_order,
                exact_decimal(bpm.beats_per_minute, "$.charts[].bpms[].beats_per_minute"));
        }
        if (position_changed) {
            edit_branch_channel_slot(bpm.provenance.source_order, *source, field->slot, target_slot,
                                     field->object_id, "$.charts[].bpms");
        }
    }

    for (const auto& pause : chart.pauses) {
        if (!is_branch_provenance(pause.provenance)) {
            continue;
        }
        const auto projection = std::ranges::find(chart.bms_pause_projections, pause.id,
                                                  &bms_pause_projection_t::event);
        const auto field = branch_channel_slot_field(pause.provenance.source_field);
        if (projection == chart.bms_pause_projections.end() ||
            projection->source_order != pause.provenance.source_order || !field.has_value() ||
            field->channel != "09" || pause.kind != pause_kind_t::stop ||
            pause.source_unit != pause_unit_t::bms_stop_ticks ||
            projection->imported_kind != pause.kind ||
            projection->imported_source_unit != pause.source_unit) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch pause is not a uniquely attributable BMS STOP channel event",
                 "$.charts[].pauses");
        }
        const auto* source = claim_branch_channel_slot(
            pause.provenance, *field, "$.charts[].pauses[" + std::to_string(pause.id.value) + "]");
        const auto original_position = expected_branch_slot_position(*source, field->slot);
        if (!original_position.has_value() || *original_position != projection->imported_position) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "branch STOP event slot does not match its imported position",
                 "$.charts[].pauses");
        }
        const auto target_slot =
            pause.position == projection->imported_position
                ? field->slot
                : branch_slot_for_position(*source, pause.position, "$.charts[].pauses[].position");
        const bool position_changed = target_slot != field->slot;
        const bool duration_changed = pause.duration != projection->imported_duration;
        if (!position_changed && !duration_changed) {
            continue;
        }
        if (duration_changed) {
            const auto* declaration = effective_timing_declaration("STOP" + field->object_id,
                                                                   pause.provenance.source_order,
                                                                   projection->imported_duration);
            if (declaration == nullptr) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "STOP event does not resolve to one active retained declaration",
                     "$.charts[].pauses");
            }
            validate_branch_timing_declaration_edit(*declaration, field->object_id, "09",
                                                    "$.charts[].pauses");
            declaration_referenced_by_pause(*declaration, field->object_id, pause.duration,
                                            "$.charts[].pauses");
            branch_timing_declaration_edits.insert_or_assign(
                declaration->source_order,
                exact_decimal(pause.duration, "$.charts[].pauses[].duration"));
        }
        if (position_changed) {
            edit_branch_channel_slot(pause.provenance.source_order, *source, field->slot,
                                     target_slot, field->object_id, "$.charts[].pauses");
        }
    }

    std::map<std::pair<std::size_t, std::string>, channel_line_t> channel_lines;
    std::map<std::pair<std::size_t, std::uint64_t>, channel_line_t> bgm_lines;
    std::map<std::pair<std::size_t, std::string>, std::size_t> bga_line_order;
    std::vector<std::string> projected_timing_declarations;
    const bpm_event_t* projected_initial_bpm{};
    if (flattening_active_projection) {
        const auto measure_index_for = [&](local_position_t position,
                                           std::string_view path) -> std::size_t {
            const auto measure =
                std::ranges::find(chart.measures, position.measure, &measure_chunk_t::id);
            if (measure == chart.measures.end()) {
                fail(bms_write_diagnostic_code_t::invalid_geometry,
                     "timing event references a measure outside the chart", std::string(path));
            }
            return static_cast<std::size_t>(std::distance(chart.measures.begin(), measure));
        };
        const auto add_timing_channel_event = [&](local_position_t position,
                                                  std::string_view channel,
                                                  std::string_view object_id, std::string path) {
            const auto measure = measure_index_for(position, path);
            const auto [numerator, denominator] = fraction_at(index, position, path);
            const auto key = std::pair{measure, std::string(channel)};
            auto [line, inserted] = channel_lines.try_emplace(
                key, channel_line_t{.measure = measure, .channel = std::string(channel)});
            (void)inserted;
            line->second.notes.push_back(
                {numerator, denominator, std::string(object_id), std::move(path)});
        };

        std::vector<std::pair<const bpm_event_t*, std::uint64_t>> ordered_bpms;
        ordered_bpms.reserve(chart.bpms.size());
        for (const auto& bpm : chart.bpms) {
            const auto absolute = absolute_row(index, bpm.position);
            if (!absolute.has_value()) {
                fail(bms_write_diagnostic_code_t::invalid_geometry,
                     "BPM event is outside the chart's measure geometry", "$.charts[].bpms");
            }
            (void)exact_decimal(bpm.beats_per_minute, "$.charts[].bpms[].beats_per_minute");
            ordered_bpms.emplace_back(&bpm, *absolute);
        }
        std::ranges::stable_sort(ordered_bpms, [](const auto& left, const auto& right) {
            return std::tie(left.second, left.first->provenance.source_order,
                            left.first->id.value) < std::tie(right.second,
                                                             right.first->provenance.source_order,
                                                             right.first->id.value);
        });

        std::vector<std::pair<signed_rational_t, std::string>> bpm_identifiers;
        std::size_t next_bpm_identifier = 1;
        std::optional<std::uint64_t> previous_bpm_row;
        for (const auto& [bpm, absolute] : ordered_bpms) {
            if (previous_bpm_row == absolute) {
                fail(bms_write_diagnostic_code_t::duplicate_event,
                     "multiple BPM events share one BMS channel position and cannot be flattened "
                     "without changing their ordering",
                     "$.charts[].bpms");
            }
            previous_bpm_row = absolute;
            if (absolute == 0) {
                projected_initial_bpm = bpm;
                continue;
            }
            auto identifier = std::ranges::find_if(bpm_identifiers, [&](const auto& entry) {
                return entry.first == bpm->beats_per_minute;
            });
            if (identifier == bpm_identifiers.end()) {
                if (next_bpm_identifier >= 1296) {
                    fail(bms_write_diagnostic_code_t::resource_limit,
                         "flattened BMS timing exceeds 1295 extended BPM identifiers",
                         "$.charts[].bpms");
                }
                const auto id = base36_id(next_bpm_identifier++);
                projected_timing_declarations.push_back(
                    "#BPM" + id + " " +
                    exact_decimal(bpm->beats_per_minute, "$.charts[].bpms[].beats_per_minute"));
                bpm_identifiers.emplace_back(bpm->beats_per_minute, id);
                identifier = std::prev(bpm_identifiers.end());
            }
            add_timing_channel_event(bpm->position, "08", identifier->second,
                                     "$.charts[].bpms[" + std::to_string(bpm->id.value) + "]");
        }

        std::vector<std::pair<signed_rational_t, std::string>> stop_identifiers;
        std::size_t next_stop_identifier = 1;
        for (const auto& pause : chart.pauses) {
            if (pause.kind != pause_kind_t::stop ||
                pause.source_unit != pause_unit_t::bms_stop_ticks) {
                fail(bms_write_diagnostic_code_t::unsupported_feature,
                     "active-projection flattening supports BMS STOP ticks only; delay and other "
                     "pause units cannot be converted without a timing approximation",
                     "$.charts[].pauses[" + std::to_string(pause.id.value) + "]");
            }
            (void)exact_decimal(pause.duration, "$.charts[].pauses[].duration");
            const auto absolute = absolute_row(index, pause.position);
            if (!absolute.has_value()) {
                fail(bms_write_diagnostic_code_t::invalid_geometry,
                     "STOP event is outside the chart's measure geometry", "$.charts[].pauses");
            }
            auto identifier = std::ranges::find_if(
                stop_identifiers, [&](const auto& entry) { return entry.first == pause.duration; });
            if (identifier == stop_identifiers.end()) {
                if (next_stop_identifier >= 1296) {
                    fail(bms_write_diagnostic_code_t::resource_limit,
                         "flattened BMS timing exceeds 1295 STOP identifiers", "$.charts[].pauses");
                }
                const auto id = base36_id(next_stop_identifier++);
                projected_timing_declarations.push_back(
                    "#STOP" + id + " " +
                    exact_decimal(pause.duration, "$.charts[].pauses[].duration"));
                stop_identifiers.emplace_back(pause.duration, id);
                identifier = std::prev(stop_identifiers.end());
            }
            add_timing_channel_event(pause.position, "09", identifier->second,
                                     "$.charts[].pauses[" + std::to_string(pause.id.value) + "]");
        }
    }
    struct hold_endpoint_t {
        std::uint64_t row{};
        std::uint64_t note_id{};
        bool is_end{};
        std::string path;
    };
    std::map<std::uint64_t, std::vector<hold_endpoint_t>> hold_endpoints;
    for (std::size_t measure = 0; measure < chart.measures.size(); ++measure) {
        for (const auto& note : chart.measures[measure].notes) {
            const auto path = "$.charts[].measures[" + std::to_string(measure) + "].notes[]";
            if (is_branch_provenance(note.provenance)) {
                if (const auto long_field =
                        parse_long_note_source_field(note.provenance.source_field);
                    long_field.has_value()) {
                    const auto start_source =
                        branch_tree.branch_channel_lines.find(note.provenance.source_order);
                    const auto end_source =
                        branch_tree.branch_channel_lines.find(long_field->end_source_order);
                    if (start_source == branch_tree.branch_channel_lines.end() ||
                        end_source == branch_tree.branch_channel_lines.end() ||
                        note.provenance.source_spelling !=
                            start_source->second.text + "\n" + end_source->second.text ||
                        note.kind != note_kind_t::hold || !note.end.has_value()) {
                        fail(bms_write_diagnostic_code_t::branch_provenance,
                             "branch hold does not identify its retained start and end channel "
                             "lines",
                             path);
                    }
                    const channel_slot_field_t start_field{long_field->start_channel,
                                                           long_field->start_slot,
                                                           long_field->start_object_id};
                    const channel_slot_field_t end_field{
                        long_field->end_channel, long_field->end_slot, long_field->end_object_id};
                    auto start_provenance = note.provenance;
                    start_provenance.source_spelling = start_source->second.text;
                    (void)claim_branch_channel_slot(start_provenance, start_field, path);
                    auto end_provenance = note.provenance;
                    end_provenance.source_order = long_field->end_source_order;
                    end_provenance.source_spelling = end_source->second.text;
                    (void)claim_branch_channel_slot(end_provenance, end_field, path + ".end");
                    const auto start_position =
                        expected_branch_slot_position(start_source->second, long_field->start_slot);
                    const auto end_position =
                        expected_branch_slot_position(end_source->second, long_field->end_slot);
                    if (!start_position.has_value() || !end_position.has_value()) {
                        fail(bms_write_diagnostic_code_t::branch_provenance,
                             "branch hold endpoints do not map to valid retained source slots",
                             path);
                    }
                    const auto lane =
                        std::ranges::find(layout.lanes, note.lane, &lane_definition_t::id);
                    if (lane == layout.lanes.end() || !lane->bms_channels.long_note.has_value() ||
                        ascii_upper(*lane->bms_channels.long_note) != long_field->start_channel ||
                        long_field->end_channel != long_field->start_channel) {
                        fail(bms_write_diagnostic_code_t::branch_provenance,
                             "branch hold lane no longer matches its retained BMS channel", path);
                    }
                    std::string object_id = long_field->start_object_id;
                    if (note.sound.has_value()) {
                        const auto wav = audio_ids.find(note.sound->value);
                        if (wav == audio_ids.end()) {
                            fail(bms_write_diagnostic_code_t::unresolved_resource,
                                 "branch hold refers to a resource that is not exported as WAV",
                                 path + ".sound");
                        }
                        object_id = wav->second;
                    }
                    const auto target_start_slot =
                        *start_position == note.position
                            ? long_field->start_slot
                            : branch_slot_for_position(start_source->second, note.position,
                                                       path + ".position");
                    const auto target_end_slot =
                        *end_position == *note.end
                            ? long_field->end_slot
                            : branch_slot_for_position(end_source->second, *note.end,
                                                       path + ".end");
                    if (target_start_slot != long_field->start_slot ||
                        object_id != long_field->start_object_id) {
                        edit_branch_channel_slot(note.provenance.source_order, start_source->second,
                                                 long_field->start_slot, target_start_slot,
                                                 object_id, path);
                    }
                    if (target_end_slot != long_field->end_slot) {
                        edit_branch_channel_slot(long_field->end_source_order, end_source->second,
                                                 long_field->end_slot, target_end_slot,
                                                 long_field->end_object_id, path + ".end");
                    }
                    continue;
                }
                const auto field = parse_channel_slot_field(note.provenance.source_field);
                if (!field.has_value()) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "branch note does not identify one retained channel slot", path);
                }
                const auto* source = claim_branch_channel_slot(note.provenance, *field, path);
                const auto original_position = expected_branch_slot_position(*source, field->slot);
                if (!original_position.has_value()) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "branch note slot does not map to a valid retained source position", path);
                }
                const auto lane =
                    std::ranges::find(layout.lanes, note.lane, &lane_definition_t::id);
                std::optional<std::string> expected_channel;
                if (lane != layout.lanes.end()) {
                    switch (note.kind) {
                    case note_kind_t::tap:
                        expected_channel = lane->bms_channels.visible;
                        break;
                    case note_kind_t::invisible:
                        expected_channel = lane->bms_channels.invisible;
                        break;
                    case note_kind_t::mine:
                        expected_channel = lane->bms_channels.mine;
                        break;
                    default:
                        break;
                    }
                }
                if (!expected_channel.has_value() ||
                    ascii_upper(*expected_channel) != field->channel || note.end.has_value()) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "branch note lane or kind no longer matches its retained BMS channel",
                         path);
                }
                std::string object_id = field->object_id;
                if (note.kind != note_kind_t::mine && note.sound.has_value()) {
                    const auto wav = audio_ids.find(note.sound->value);
                    if (wav == audio_ids.end()) {
                        fail(bms_write_diagnostic_code_t::unresolved_resource,
                             "branch note refers to a resource that is not exported as WAV",
                             path + ".sound");
                    }
                    object_id = wav->second;
                } else if (note.kind != note_kind_t::mine) {
                    const bool source_object_resolves =
                        std::ranges::any_of(song.resources, [&](const auto& resource) {
                            return bms_identity(resource, "WAV") ==
                                   std::optional<std::string>{field->object_id};
                        });
                    if (source_object_resolves || note.position != *original_position) {
                        fail(bms_write_diagnostic_code_t::branch_provenance,
                             "an unbound branch note cannot be safely edited from its raw source "
                             "slot",
                             path);
                    }
                }
                const auto target_slot =
                    note.position == *original_position
                        ? field->slot
                        : branch_slot_for_position(*source, note.position, path + ".position");
                if (target_slot != field->slot || object_id != field->object_id) {
                    edit_branch_channel_slot(note.provenance.source_order, *source, field->slot,
                                             target_slot, object_id, path);
                }
                continue;
            }
            if ((note.kind != note_kind_t::tap && note.kind != note_kind_t::hold) ||
                (note.kind == note_kind_t::tap && note.end.has_value())) {
                fail(bms_write_diagnostic_code_t::unsupported_feature,
                     "normalized BMS writer supports playable taps and holds only", path);
            }
            if (note.branch.has_value()) {
                fail(bms_write_diagnostic_code_t::unsupported_feature,
                     "branch-local notes require an explicit branch export policy", path);
            }
            const auto& selected_channels =
                note.kind == note_kind_t::hold ? long_note_channels : lane_channels;
            const auto channel = selected_channels.find(note.lane.value);
            if (channel == selected_channels.end()) {
                fail(
                    bms_write_diagnostic_code_t::missing_profile,
                    note.kind == note_kind_t::hold
                        ? "hold lane has no authored long-note BMS channel in the selected lane map"
                        : "note lane has no visible BMS channel in the selected lane map",
                    path);
            }
            if (!note.sound.has_value()) {
                fail(bms_write_diagnostic_code_t::unresolved_resource,
                     "BMS playable taps require a WAV resource binding", path + ".sound");
            }
            if (branch_resource_ids.contains(note.sound->value)) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "a root note refers to a resource declared only inside a branch",
                     path + ".sound");
            }
            const auto wav = audio_ids.find(note.sound->value);
            if (wav == audio_ids.end()) {
                fail(bms_write_diagnostic_code_t::unresolved_resource,
                     "tap refers to a resource that is not exported as WAV", path + ".sound");
            }
            auto [numerator, denominator] = fraction_at(index, note.position, path + ".position");
            auto key = std::pair{measure, channel->second};
            auto [line, inserted] = channel_lines.try_emplace(key, channel_line_t{
                                                                       .measure = measure,
                                                                       .channel = channel->second,
                                                                   });
            (void)inserted;
            line->second.notes.push_back({numerator, denominator, wav->second, path});

            const auto absolute_start = absolute_row(index, note.position);
            if (!absolute_start.has_value()) {
                fail(bms_write_diagnostic_code_t::invalid_geometry,
                     "hold endpoint is outside the chart's measure geometry", path);
            }
            if (note.kind == note_kind_t::hold) {
                if (!note.end.has_value()) {
                    fail(bms_write_diagnostic_code_t::invalid_document,
                         "hold note has no tail position", path + ".end");
                }
                const auto [end_numerator, end_denominator] =
                    fraction_at(index, *note.end, path + ".end");
                const auto end_measure =
                    std::ranges::find(chart.measures, note.end->measure, &measure_chunk_t::id);
                const auto end_measure_index =
                    static_cast<std::size_t>(std::distance(chart.measures.begin(), end_measure));
                auto [end_line, end_inserted] = channel_lines.try_emplace(
                    std::pair{end_measure_index, channel->second},
                    channel_line_t{.measure = end_measure_index, .channel = channel->second});
                (void)end_inserted;
                end_line->second.notes.push_back(
                    {end_numerator, end_denominator, wav->second, path + ".end"});
                const auto absolute_end = absolute_row(index, *note.end);
                if (!absolute_end.has_value()) {
                    fail(bms_write_diagnostic_code_t::invalid_geometry,
                         "hold tail is outside the chart's measure geometry", path + ".end");
                }
                hold_endpoints[note.lane.value].push_back(
                    {*absolute_start, note.id.value, false, path});
                hold_endpoints[note.lane.value].push_back(
                    {*absolute_end, note.id.value, true, path + ".end"});
            }
        }

        for (const auto& event : chart.measures[measure].bgm_events) {
            const auto path = "$.charts[].measures[" + std::to_string(measure) + "].bgm_events[]";
            if (is_branch_provenance(event.provenance)) {
                const auto field = parse_channel_slot_field(event.provenance.source_field);
                if (!field.has_value() || field->channel != "01" ||
                    event.structural_slot != field->slot) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "branch BGM event does not identify its retained source slot", path);
                }
                const auto* source = claim_branch_channel_slot(event.provenance, *field, path);
                const auto source_lane = std::ranges::find(
                    chart.bgm_source_lanes, event.source_lane, &bgm_source_lane_t::id);
                if (source_lane == chart.bgm_source_lanes.end() ||
                    source_lane->source_order != event.provenance.source_order ||
                    event.branch.has_value()) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "branch BGM event source lane no longer matches its retained source line",
                         path);
                }
                const auto original_position = expected_branch_slot_position(*source, field->slot);
                if (!original_position.has_value()) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "branch BGM slot does not map to a valid retained source position", path);
                }
                std::string object_id = "00";
                if (event.sound.has_value()) {
                    const auto wav = audio_ids.find(event.sound->value);
                    if (wav == audio_ids.end()) {
                        fail(bms_write_diagnostic_code_t::unresolved_resource,
                             "branch BGM event refers to a resource that is not exported as WAV",
                             path + ".sound");
                    }
                    object_id = wav->second;
                }
                const auto target_slot =
                    event.position == *original_position
                        ? field->slot
                        : branch_slot_for_position(*source, event.position, path + ".position");
                if (target_slot != field->slot || object_id != field->object_id) {
                    edit_branch_channel_slot(event.provenance.source_order, *source, field->slot,
                                             target_slot, object_id, path);
                }
                continue;
            }
            if (event.branch.has_value()) {
                fail(bms_write_diagnostic_code_t::unsupported_feature,
                     "branch-local BGM events require an explicit branch export policy", path);
            }
            const auto source_lane = std::ranges::find(chart.bgm_source_lanes, event.source_lane,
                                                       &bgm_source_lane_t::id);
            if (source_lane == chart.bgm_source_lanes.end()) {
                fail(bms_write_diagnostic_code_t::invalid_document,
                     "BGM event refers to an unknown source lane", path + ".source_lane");
            }
            std::string object_id = "00";
            if (event.sound.has_value()) {
                if (branch_resource_ids.contains(event.sound->value)) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "a root BGM event refers to a resource declared only inside a branch",
                         path + ".sound");
                }
                const auto wav = audio_ids.find(event.sound->value);
                if (wav == audio_ids.end()) {
                    fail(bms_write_diagnostic_code_t::unresolved_resource,
                         "BGM event refers to a resource that is not exported as WAV",
                         path + ".sound");
                }
                object_id = wav->second;
            }
            const auto [numerator, denominator] =
                fraction_at(index, event.position, path + ".position");
            const auto key = std::pair{measure, event.source_lane.value};
            auto [line, inserted] =
                bgm_lines.try_emplace(key, channel_line_t{.measure = measure, .channel = "01"});
            (void)inserted;
            line->second.notes.push_back({numerator, denominator, std::move(object_id), path});
        }
    }

    for (auto& [lane, endpoints] : hold_endpoints) {
        (void)lane;
        std::ranges::sort(endpoints, {}, &hold_endpoint_t::row);
        for (std::size_t endpoint = 0; endpoint < endpoints.size(); endpoint += 2) {
            if (endpoint + 1 >= endpoints.size() || endpoints[endpoint].is_end ||
                !endpoints[endpoint + 1].is_end ||
                endpoints[endpoint].note_id != endpoints[endpoint + 1].note_id) {
                fail(bms_write_diagnostic_code_t::unsupported_feature,
                     "overlapping holds on one lane cannot be paired safely by BMS long-note "
                     "channels",
                     endpoint < endpoints.size() ? endpoints[endpoint].path
                                                 : "$.charts[].measures[].notes[]");
            }
        }
    }

    for (const auto& source_lane : chart.bgm_source_lanes) {
        if (branch_tree.branch_source_orders.contains(source_lane.source_order))
            continue;
        if (std::ranges::none_of(bgm_lines, [&](const auto& entry) {
                return entry.first.second == source_lane.id.value;
            })) {
            warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                 "empty editor BGM source lanes have no BMS representation and are omitted",
                 "$.charts[].bgm_source_lanes");
        }
    }

    std::map<std::pair<std::size_t, std::uint64_t>, std::size_t> layer_counts;
    auto sorted_bga_events = chart.bga_events;
    std::ranges::stable_sort(sorted_bga_events, [](const auto& left, const auto& right) {
        return std::tie(left.position.measure.value, left.position.row,
                        left.provenance.source_order,
                        left.id.value) < std::tie(right.position.measure.value, right.position.row,
                                                  right.provenance.source_order, right.id.value);
    });
    for (const auto& event : sorted_bga_events) {
        if (is_branch_provenance(event.provenance)) {
            const auto field = parse_channel_slot_field(event.provenance.source_field);
            if (!field.has_value() || (field->channel != "04" && field->channel != "06" &&
                                       field->channel != "07" && field->channel != "0A")) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "branch BGA event does not identify one retained visual channel slot",
                     "$.charts[].bga_events[]");
            }
            const auto* source =
                claim_branch_channel_slot(event.provenance, *field, "$.charts[].bga_events[]");
            const bool channel_matches_kind =
                (field->channel == "04" && event.kind == visual_event_kind_t::background) ||
                (field->channel == "06" && event.kind == visual_event_kind_t::poor) ||
                ((field->channel == "07" || field->channel == "0A") &&
                 event.kind == visual_event_kind_t::layer);
            if (!channel_matches_kind) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "branch BGA kind no longer matches its retained BMS channel",
                     "$.charts[].bga_events[]");
            }
            const auto original_position = expected_branch_slot_position(*source, field->slot);
            if (!original_position.has_value()) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "branch BGA slot does not map to a valid retained source position",
                     "$.charts[].bga_events[]");
            }
            const auto bmp = visual_ids.find(event.resource.value);
            if (bmp == visual_ids.end()) {
                fail(bms_write_diagnostic_code_t::unresolved_resource,
                     "branch BGA event refers to a resource that is not exported as BMP",
                     "$.charts[].bga_events[].resource");
            }
            const auto target_slot =
                event.position == *original_position
                    ? field->slot
                    : branch_slot_for_position(*source, event.position,
                                               "$.charts[].bga_events[].position");
            if (target_slot != field->slot || bmp->second != field->object_id) {
                edit_branch_channel_slot(event.provenance.source_order, *source, field->slot,
                                         target_slot, bmp->second, "$.charts[].bga_events[]");
            }
            continue;
        }
        const std::string path = "$.charts[].bga_events[]";
        std::string channel;
        switch (event.kind) {
        case visual_event_kind_t::background:
            channel = "04";
            break;
        case visual_event_kind_t::poor:
            channel = "06";
            break;
        case visual_event_kind_t::layer: {
            const auto absolute = absolute_row(index, event.position);
            if (!absolute.has_value()) {
                fail(bms_write_diagnostic_code_t::invalid_geometry,
                     "BGA event is outside the chart's measure geometry", path + ".position");
            }
            auto& count = layer_counts[{event.position.measure.value, *absolute}];
            if (count >= 2) {
                fail(bms_write_diagnostic_code_t::duplicate_event,
                     "more than two layer events share a position; BMS channels 07 and 0A cannot "
                     "represent the collision",
                     path);
            }
            channel = count++ == 0 ? "07" : "0A";
            break;
        }
        case visual_event_kind_t::video:
        case visual_event_kind_t::storyboard:
            fail(bms_write_diagnostic_code_t::unsupported_feature,
                 "BMS BGA channels cannot represent video or storyboard events", path);
        }
        const auto bmp = visual_ids.find(event.resource.value);
        if (branch_resource_ids.contains(event.resource.value)) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "a root BGA event refers to a resource declared only inside a branch",
                 path + ".resource");
        }
        if (bmp == visual_ids.end()) {
            fail(bms_write_diagnostic_code_t::unresolved_resource,
                 "BGA event refers to a resource that is not exported as BMP", path + ".resource");
        }
        const auto [numerator, denominator] =
            fraction_at(index, event.position, path + ".position");
        const auto key = std::pair{
            static_cast<std::size_t>(std::distance(
                chart.measures.begin(),
                std::ranges::find(chart.measures, event.position.measure, &measure_chunk_t::id))),
            channel};
        auto [line, inserted] = channel_lines.try_emplace(
            key, channel_line_t{.measure = key.first, .channel = channel});
        (void)inserted;
        line->second.notes.push_back({numerator, denominator, bmp->second, path});
        auto [order, order_inserted] =
            bga_line_order.try_emplace(key, event.provenance.source_order);
        if (!order_inserted)
            order->second = std::min(order->second, event.provenance.source_order);
    }

    const auto player_value =
        layout.profile_id.starts_with("pms-") ? std::string("3")
        : (layout.split_stage ||
           std::ranges::any_of(layout.lanes,
                               [](const auto& lane) { return lane.stage == stage_group_t::right; }))
            ? std::string("2")
            : std::string("1");
    std::vector<const format_metadata_t*> bms_metadata_groups;
    const auto append_bms_groups = [&](const auto& groups) {
        for (const auto& group : groups) {
            if (group.family == format_family_t::bms && !group.ordered_fields.empty()) {
                bms_metadata_groups.push_back(&group);
            }
        }
    };
    append_bms_groups(song.metadata.format_groups);
    append_bms_groups(chart.metadata.format_groups);
    const auto offset_decimal = exact_offset_decimal(chart.beat_zero_audio_offset_seconds);
    if (chart.beat_zero_audio_offset_seconds.numerator != 0) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "OFFSET is a Wormedit BMS extension and may be ignored by other players",
             "$.charts[].beat_zero_audio_offset_seconds");
    }
    std::optional<std::uint32_t> playlevel_from_rating;
    if (chart.metadata.rating) {
        const auto rating = *chart.metadata.rating;
        if (rating.denominator != 0 && rating.numerator >= 0 &&
            static_cast<std::uint64_t>(rating.numerator) % rating.denominator == 0 &&
            static_cast<std::uint64_t>(rating.numerator) / rating.denominator <= UINT32_MAX) {
            playlevel_from_rating = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(rating.numerator) / rating.denominator);
        } else {
            warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                 "chart rating cannot be represented as an integral BMS PLAYLEVEL",
                 "$.charts[].metadata.rating");
        }
    }
    bool has_ordered_playlevel = false;
    bool ordered_playlevel_matches_rating = true;
    for (const auto* group : bms_metadata_groups) {
        for (const auto& [token, value] : group->ordered_fields) {
            if (ascii_upper(token) != "PLAYLEVEL") {
                continue;
            }
            has_ordered_playlevel = true;
            if (!playlevel_from_rating) {
                continue;
            }
            const auto trimmed = trim_ascii_whitespace(value);
            std::uint32_t parsed{};
            const auto [end, error] =
                std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), parsed);
            if (error != std::errc{} || end != trimmed.data() + trimmed.size() ||
                parsed != *playlevel_from_rating) {
                ordered_playlevel_matches_rating = false;
            }
        }
    }
    if (has_ordered_playlevel && playlevel_from_rating &&
        !ordered_playlevel_matches_rating) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "chart rating differs from retained PLAYLEVEL; retained source metadata takes "
             "precedence",
             "$.charts[].metadata.rating");
    }
    bool has_branch_player_field = false;
    struct effective_metadata_source_t {
        std::size_t source_order{};
        bool branch_scoped{};
    };
    std::optional<effective_metadata_source_t> last_title_source;
    std::optional<effective_metadata_source_t> last_artist_source;
    std::optional<effective_metadata_source_t> last_offset_source;
    std::map<std::size_t, std::string> branch_metadata_edits;
    std::set<std::size_t> metadata_source_orders;
    for (const auto* group : bms_metadata_groups) {
        std::optional<std::size_t> previous_source_order;
        for (std::size_t index = 0; index < group->ordered_fields.size(); ++index) {
            const auto& [token, value] = group->ordered_fields[index];
            if (!safe_metadata_token(token)) {
                fail(bms_write_diagnostic_code_t::invalid_document,
                     "retained BMS metadata contains an unsafe command token",
                     "$.metadata.format_groups[].ordered_fields[]");
            }
            if (index >= group->ordered_field_source_orders.size()) {
                continue;
            }
            const auto source_order = group->ordered_field_source_orders[index];
            const auto source_field = retained_branch_tree.metadata_fields.find(source_order);
            if ((previous_source_order.has_value() && source_order <= *previous_source_order) ||
                !metadata_source_orders.insert(source_order).second ||
                source_field == retained_branch_tree.metadata_fields.end() ||
                ascii_upper(token) != source_field->second.first) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "retained BMS metadata source order or command token does not identify one "
                     "unique source statement",
                     "$.metadata.format_groups[].ordered_fields[]");
            }
            previous_source_order = source_order;
            const bool branch_scoped = is_branch_metadata_field(*group, index);
            const auto normalized_token = ascii_upper(token);
            if (branch_scoped) {
                const auto source_field = branch_tree.branch_metadata_fields.find(source_order);
                if (source_field == branch_tree.branch_metadata_fields.end() ||
                    normalized_token != source_field->second.first) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "branch metadata source identity does not match its retained source "
                         "statement",
                         "$.metadata.format_groups[].ordered_fields[]");
                }
                if (has_line_break_or_nul(value) || !valid_utf8(value)) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "edited branch metadata must be valid UTF-8 single-line text",
                         "$.metadata.format_groups[].ordered_fields[]");
                }
                if (value != source_field->second.second) {
                    branch_metadata_edits.insert_or_assign(source_order, value);
                }
            }
            if (normalized_token == "PLAYER") {
                has_branch_player_field = has_branch_player_field || branch_scoped;
            } else if (normalized_token == "TITLE" &&
                       (!last_title_source || source_order >= last_title_source->source_order)) {
                last_title_source = {source_order, branch_scoped};
            } else if (normalized_token == "ARTIST" &&
                       (!last_artist_source || source_order >= last_artist_source->source_order)) {
                last_artist_source = {source_order, branch_scoped};
            } else if (normalized_token == "OFFSET" &&
                       (!last_offset_source || source_order >= last_offset_source->source_order)) {
                last_offset_source = {source_order, branch_scoped};
            }
        }
    }

    const auto route_projected_branch_metadata =
        [&](std::string_view token, const std::optional<effective_metadata_source_t>& source,
            std::string_view projected_value) {
            if (!source.has_value() || !source->branch_scoped) {
                return false;
            }
            const auto retained = branch_tree.branch_metadata_fields.find(source->source_order);
            if (retained == branch_tree.branch_metadata_fields.end() ||
                retained->second.first != token) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "projected branch metadata has no unique retained source statement",
                     "$.metadata");
            }
            const auto original_value = trim_ascii_whitespace(retained->second.second);
            if (projected_value != original_value) {
                const auto edit = branch_metadata_edits.find(source->source_order);
                if (edit != branch_metadata_edits.end() &&
                    trim_ascii_whitespace(edit->second) != projected_value) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "normalized and source-field edits disagree for the same branch metadata "
                         "statement",
                         "$.metadata");
                }
                if (edit == branch_metadata_edits.end()) {
                    branch_metadata_edits.emplace(source->source_order,
                                                  std::string(projected_value));
                }
            }
            return true;
        };
    const bool title_is_branch_projection =
        route_projected_branch_metadata("TITLE", last_title_source, title);
    const bool artist_is_branch_projection =
        route_projected_branch_metadata("ARTIST", last_artist_source, artist);
    const bool offset_is_branch_projection =
        route_projected_branch_metadata("OFFSET", last_offset_source, offset_decimal);

    const auto has_player_field = std::ranges::any_of(bms_metadata_groups, [&](const auto* group) {
        for (std::size_t index = 0; index < group->ordered_fields.size(); ++index) {
            if (!is_branch_metadata_field(*group, index) &&
                ascii_upper(group->ordered_fields[index].first) == "PLAYER") {
                return true;
            }
        }
        return false;
    });
    std::vector<std::string> output;
    if (!has_player_field && !has_branch_player_field)
        output.emplace_back("#PLAYER " + player_value);
    bool player_emitted = false;
    bool title_emitted = false;
    bool artist_emitted = false;
    bool offset_emitted = false;
    for (std::size_t group_index = 0; group_index < bms_metadata_groups.size(); ++group_index) {
        const auto& fields = bms_metadata_groups[group_index]->ordered_fields;
        for (std::size_t field_index = 0; field_index < fields.size(); ++field_index) {
            if (is_branch_metadata_field(*bms_metadata_groups[group_index], field_index)) {
                continue;
            }
            const auto& [token, retained_value] = fields[field_index];
            const auto upper_token = ascii_upper(token);
            if (!safe_metadata_token(token) ||
                (upper_token != "PLAYER" && generated_or_channel_token(token))) {
                fail(bms_write_diagnostic_code_t::invalid_document,
                     "retained BMS metadata contains an unsafe or writer-owned command "
                     "token",
                     "$.metadata.format_groups[" + std::to_string(group_index) +
                         "].ordered_fields[" + std::to_string(field_index) + "]");
            }
            std::string value = retained_value;
            if (upper_token == "PLAYER") {
                if (player_emitted) {
                    warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                         "duplicate retained PLAYER metadata was collapsed to one canonical value",
                         "$.metadata.format_groups[" + std::to_string(group_index) +
                             "].ordered_fields[" + std::to_string(field_index) + "]");
                    continue;
                }
                value = " " + player_value;
                player_emitted = true;
            } else if (upper_token == "TITLE") {
                if (title_emitted) {
                    warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                         "duplicate retained TITLE metadata was collapsed to its effective value",
                         "$.metadata.format_groups[" + std::to_string(group_index) +
                             "].ordered_fields[" + std::to_string(field_index) + "]");
                    continue;
                }
                value = title_is_branch_projection ? retained_value : title;
                title_emitted = true;
            } else if (upper_token == "ARTIST") {
                if (artist_emitted) {
                    warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                         "duplicate retained ARTIST metadata was collapsed to its effective value",
                         "$.metadata.format_groups[" + std::to_string(group_index) +
                             "].ordered_fields[" + std::to_string(field_index) + "]");
                    continue;
                }
                value = artist_is_branch_projection ? retained_value : artist;
                artist_emitted = true;
            } else if (upper_token == "OFFSET") {
                if (!offset_is_branch_projection) {
                    value = offset_decimal;
                }
                offset_emitted = true;
            }
            if (has_line_break_or_nul(value) || !valid_utf8(value)) {
                fail(bms_write_diagnostic_code_t::invalid_document,
                     "retained BMS metadata values must be valid UTF-8 single-line text",
                     "$.metadata.format_groups[" + std::to_string(group_index) +
                         "].ordered_fields[" + std::to_string(field_index) + "]");
            }
            std::string line = "#" + token;
            if (value.empty() || (value.front() != ' ' && value.front() != '\t')) {
                line.push_back(' ');
            }
            line += value;
            output.push_back(std::move(line));
        }
    }
    if (!title_emitted && !title.empty() && !title_is_branch_projection)
        output.emplace_back("#TITLE " + title);
    if (!artist_emitted && !artist.empty() && !artist_is_branch_projection)
        output.emplace_back("#ARTIST " + artist);
    if (!offset_emitted && !last_offset_source &&
        chart.beat_zero_audio_offset_seconds.numerator != 0)
        output.emplace_back("#OFFSET " + offset_decimal);
    if (!has_ordered_playlevel && playlevel_from_rating)
        output.emplace_back("#PLAYLEVEL " + std::to_string(*playlevel_from_rating));

    if (flattening_active_projection) {
        if (projected_initial_bpm != nullptr) {
            output.emplace_back("#BPM " + exact_decimal(projected_initial_bpm->beats_per_minute,
                                                        "$.charts[].bpms[].beats_per_minute"));
        }
    } else {
        std::vector<const bpm_event_t*> root_bpms;
        for (const auto& bpm : chart.bpms) {
            if (!is_branch_provenance(bpm.provenance) &&
                !is_retained_timing_provenance(bpm.provenance)) {
                root_bpms.push_back(&bpm);
            }
        }
        if (root_bpms.size() > 1 ||
            (!root_bpms.empty() &&
             absolute_row(index, root_bpms.front()->position) != std::optional<std::uint64_t>{0})) {
            fail(bms_write_diagnostic_code_t::unsupported_feature,
                 "this writer slice can export only one direct BPM at chart position zero",
                 "$.charts[].bpms");
        }
        if (!root_bpms.empty()) {
            output.emplace_back("#BPM " + exact_decimal(root_bpms.front()->beats_per_minute,
                                                        "$.charts[].bpms[0].bpm"));
        }
    }

    for (const auto& declaration : declarations) {
        if (declaration.branch_scoped)
            continue;
        const auto& identifiers = declaration.name_space == "WAV" ? audio_ids : visual_ids;
        const auto identity = identifiers.find(declaration.resource->id.value);
        if (identity == identifiers.end())
            continue;
        output.emplace_back("#" + declaration.name_space + identity->second + " " +
                            declaration.resource->name);
    }
    output.insert(output.end(), projected_timing_declarations.begin(),
                  projected_timing_declarations.end());

    bool warned_meter = false;
    for (std::size_t measure = 0; measure < chart.measures.size(); ++measure) {
        const auto& definition = chart.measures[measure].definition;
        if (branch_tree.branch_measure_indices.contains(measure)) {
            continue;
        }
        if (definition.meter.has_value() && !warned_meter) {
            warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                 "BMS measure ratios preserve duration but not authored beat grouping",
                 "$.charts[].measures[].definition.meter");
            warned_meter = true;
        }
        if (definition.length != rational_t{1, 1}) {
            output.emplace_back("#" +
                                (measure < 10    ? std::string("00")
                                 : measure < 100 ? std::string("0")
                                                 : std::string()) +
                                std::to_string(measure) + "02:" +
                                exact_decimal(definition.length.numerator,
                                              definition.length.denominator,
                                              "$.charts[].measures[].definition.length"));
        }
    }
    for (const auto& line : branch_tree.root_measure_ratio_lines) {
        if (!branch_tree.branch_measure_indices.contains(line.measure_index)) {
            continue;
        }
        if (has_line_break_or_nul(line.text) || !valid_utf8(line.text)) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "retained BMS measure-ratio statements must be valid UTF-8 single lines",
                 "$.charts[].bms_control_tree");
        }
        output.push_back(line.text);
    }

    auto encode_channel_line = [&](const channel_line_t& line) -> std::string {
        std::uint64_t slot_count = 1;
        for (const auto& note : line.notes) {
            const auto slots = checked_lcm(slot_count, note.denominator);
            if (!slots.has_value() || *slots > options.max_slots_per_line || *slots > 500'000) {
                fail(bms_write_diagnostic_code_t::resource_limit,
                     "exact note positions require too many BMS subdivision slots",
                     "$.charts[].measures[].notes");
            }
            slot_count = *slots;
        }
        if (slot_count == 0 || slot_count > options.max_slots_per_line || slot_count > 500'000) {
            fail(bms_write_diagnostic_code_t::resource_limit,
                 "BMS channel line exceeds the configured subdivision limit",
                 "$.charts[].measures[].notes");
        }
        std::vector<std::string> cells(static_cast<std::size_t>(slot_count), "00");
        std::vector<bool> occupied(static_cast<std::size_t>(slot_count), false);
        for (const auto& note : line.notes) {
            const auto multiplier = slot_count / note.denominator;
            if (note.numerator > std::numeric_limits<std::uint64_t>::max() / multiplier) {
                fail(bms_write_diagnostic_code_t::unrepresentable_number,
                     "note subdivision index overflows the BMS channel grid", note.path);
            }
            const auto slot = note.numerator * multiplier;
            if (slot >= slot_count || occupied[static_cast<std::size_t>(slot)]) {
                fail(bms_write_diagnostic_code_t::duplicate_event,
                     "two events occupy the same BMS channel slot", note.path);
            }
            occupied[static_cast<std::size_t>(slot)] = true;
            cells[static_cast<std::size_t>(slot)] = note.object_id;
        }
        std::string payload;
        payload.reserve(cells.size() * 2);
        for (const auto& cell : cells)
            payload += cell;
        const auto prefix = line.measure < 10    ? std::string("00")
                            : line.measure < 100 ? std::string("0")
                                                 : std::string();
        return "#" + prefix + std::to_string(line.measure) + line.channel + ":" +
               std::move(payload);
    };

    std::vector<const channel_line_t*> ordered_channel_lines;
    ordered_channel_lines.reserve(channel_lines.size());
    for (const auto& [key, line] : channel_lines) {
        (void)key;
        ordered_channel_lines.push_back(&line);
    }
    std::ranges::stable_sort(ordered_channel_lines, [&](const auto* left, const auto* right) {
        const auto left_key = std::pair{left->measure, left->channel};
        const auto right_key = std::pair{right->measure, right->channel};
        const auto left_order = bga_line_order.contains(left_key) ? bga_line_order.at(left_key) : 0;
        const auto right_order =
            bga_line_order.contains(right_key) ? bga_line_order.at(right_key) : 0;
        return std::tie(left->measure, left_order, left->channel) <
               std::tie(right->measure, right_order, right->channel);
    });
    for (const auto* line : ordered_channel_lines) {
        output.push_back(encode_channel_line(*line));
    }

    std::vector<std::pair<std::pair<std::size_t, std::uint64_t>, const channel_line_t*>>
        ordered_bgm_lines;
    ordered_bgm_lines.reserve(bgm_lines.size());
    for (const auto& [key, line] : bgm_lines) {
        ordered_bgm_lines.emplace_back(key, &line);
    }
    std::ranges::stable_sort(ordered_bgm_lines, [&](const auto& left, const auto& right) {
        const auto left_lane = std::ranges::find(chart.bgm_source_lanes, left.first.second,
                                                 [](const auto& lane) { return lane.id.value; });
        const auto right_lane = std::ranges::find(chart.bgm_source_lanes, right.first.second,
                                                  [](const auto& lane) { return lane.id.value; });
        const auto left_order = left_lane == chart.bgm_source_lanes.end()
                                    ? std::numeric_limits<std::size_t>::max()
                                    : left_lane->source_order;
        const auto right_order = right_lane == chart.bgm_source_lanes.end()
                                     ? std::numeric_limits<std::size_t>::max()
                                     : right_lane->source_order;
        return std::tie(left.first.first, left_order, left.first.second) <
               std::tie(right.first.first, right_order, right.first.second);
    });
    for (const auto& [key, line] : ordered_bgm_lines) {
        (void)key;
        output.push_back(encode_channel_line(*line));
    }

    std::vector<std::string> preserved_tree_lines;
    preserved_tree_lines.reserve(branch_tree.preserved_lines.size());
    std::set<std::size_t> emitted_branch_metadata_edits;
    std::set<std::size_t> emitted_branch_resource_edits;
    std::set<std::size_t> emitted_branch_measure_ratio_edits;
    std::set<std::size_t> emitted_branch_direct_bpm_edits;
    std::set<std::size_t> emitted_branch_timing_declaration_edits;
    std::set<std::size_t> emitted_branch_channel_edits;
    for (const auto& [source_order, line] : branch_tree.preserved_lines) {
        if (has_line_break_or_nul(line) || !valid_utf8(line)) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "retained BMS control-tree statements must be valid UTF-8 single lines",
                 "$.charts[].bms_control_tree");
        }
        const auto metadata_edit = branch_metadata_edits.find(source_order);
        const auto resource_edit = branch_resource_edits.find(source_order);
        const auto ratio_edit = branch_measure_ratio_edits.find(source_order);
        const auto bpm_edit = branch_direct_bpm_edits.find(source_order);
        const auto timing_declaration_edit = branch_timing_declaration_edits.find(source_order);
        const auto channel_edit = branch_channel_edits.find(source_order);
        if (channel_edit != branch_channel_edits.end()) {
            auto& patch = channel_edit->second;
            if (patch.source == nullptr || patch.text != line ||
                patch.payload_begin > patch.text.size() ||
                patch.payload.size() > patch.text.size() - patch.payload_begin) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "edited branch channel line does not match its retained control-tree line",
                     "$.charts[].bms_control_tree");
            }
            auto payload = patch.payload;
            for (const auto slot : patch.cleared_slots) {
                if (slot >= payload.size() / 2) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "edited branch channel clear operation is outside its retained line",
                         "$.charts[].bms_control_tree");
                }
                payload.replace(slot * 2, 2, "00");
            }
            for (const auto& [slot, object_id] : patch.written_slots) {
                if (slot >= payload.size() / 2) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "edited branch channel write operation is outside its retained line",
                         "$.charts[].bms_control_tree");
                }
                const auto origin = patch.written_slot_origins.find(slot);
                const auto original_object = patch.payload.substr(slot * 2, 2);
                if (origin == patch.written_slot_origins.end() ||
                    (slot != origin->second && !patch.cleared_slots.contains(slot) &&
                     (original_object != "00" ||
                      branch_channel_slot_claims.contains({source_order, slot})))) {
                    fail(bms_write_diagnostic_code_t::branch_provenance,
                         "edited branch channel event would overwrite another retained source "
                         "slot",
                         "$.charts[].bms_control_tree");
                }
                payload.replace(slot * 2, 2, object_id);
            }
            patch.text.replace(patch.payload_begin, patch.payload.size(), payload);
            preserved_tree_lines.push_back(std::move(patch.text));
            if (!emitted_branch_channel_edits.insert(source_order).second) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "edited branch channel line appears more than once in the retained tree",
                     "$.charts[].bms_control_tree");
            }
            continue;
        }
        if (metadata_edit == branch_metadata_edits.end() &&
            resource_edit == branch_resource_edits.end() &&
            ratio_edit == branch_measure_ratio_edits.end() &&
            bpm_edit == branch_direct_bpm_edits.end() &&
            timing_declaration_edit == branch_timing_declaration_edits.end()) {
            preserved_tree_lines.push_back(line);
            continue;
        }
        const auto edit_count =
            static_cast<unsigned>(metadata_edit != branch_metadata_edits.end()) +
            static_cast<unsigned>(resource_edit != branch_resource_edits.end()) +
            static_cast<unsigned>(ratio_edit != branch_measure_ratio_edits.end()) +
            static_cast<unsigned>(bpm_edit != branch_direct_bpm_edits.end()) +
            static_cast<unsigned>(timing_declaration_edit != branch_timing_declaration_edits.end());
        if (edit_count != 1) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "one retained branch statement is ambiguously claimed by multiple edits",
                 "$.charts[].bms_control_tree");
        }
        if (metadata_edit != branch_metadata_edits.end()) {
            const auto source_field = branch_tree.branch_metadata_fields.find(source_order);
            if (source_field == branch_tree.branch_metadata_fields.end()) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "edited branch metadata has no retained source identity",
                     "$.charts[].bms_control_tree");
            }
            const auto replaced =
                replace_retained_header_value(line, source_field->second.first,
                                              source_field->second.second, metadata_edit->second);
            if (!replaced.has_value() ||
                !emitted_branch_metadata_edits.insert(source_order).second) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "edited branch metadata does not identify one unambiguous retained source "
                     "statement",
                     "$.charts[].bms_control_tree");
            }
            preserved_tree_lines.push_back(*replaced);
            continue;
        }
        if (bpm_edit != branch_direct_bpm_edits.end()) {
            const auto source = branch_tree.branch_direct_bpm_lines.find(source_order);
            if (source == branch_tree.branch_direct_bpm_lines.end()) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "edited branch BPM has no retained source identity",
                     "$.charts[].bms_control_tree");
            }
            const auto replaced = replace_retained_header_value(
                line, "BPM", source->second.raw_value, bpm_edit->second);
            if (!replaced.has_value() ||
                !emitted_branch_direct_bpm_edits.insert(source_order).second) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "edited branch BPM does not identify one unambiguous retained source "
                     "statement",
                     "$.charts[].bms_control_tree");
            }
            preserved_tree_lines.push_back(*replaced);
            continue;
        }
        if (timing_declaration_edit != branch_timing_declaration_edits.end()) {
            const auto source = branch_tree.timing_declarations.find(source_order);
            if (source == branch_tree.timing_declarations.end()) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "edited extended BPM or STOP value has no retained source declaration",
                     "$.charts[].bms_control_tree");
            }
            const auto replaced =
                replace_retained_header_value(line, source->second.token, source->second.raw_value,
                                              timing_declaration_edit->second);
            if (!replaced.has_value() ||
                !emitted_branch_timing_declaration_edits.insert(source_order).second) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "edited timing declaration does not identify one unambiguous retained "
                     "source statement",
                     "$.charts[].bms_control_tree");
            }
            preserved_tree_lines.push_back(*replaced);
            continue;
        }
        if (ratio_edit != branch_measure_ratio_edits.end()) {
            const auto source = branch_tree.branch_measure_ratio_lines.find(source_order);
            if (source == branch_tree.branch_measure_ratio_lines.end()) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "edited branch measure length has no retained source identity",
                     "$.charts[].bms_control_tree");
            }
            const auto replaced = replace_retained_measure_length(
                line, source->second.measure_index, source->second.raw_value, ratio_edit->second);
            if (!replaced.has_value() ||
                !emitted_branch_measure_ratio_edits.insert(source_order).second) {
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "edited branch measure length does not identify one unambiguous retained "
                     "source statement",
                     "$.charts[].bms_control_tree");
            }
            preserved_tree_lines.push_back(*replaced);
            continue;
        }
        const auto source_field = branch_tree.branch_resource_fields.find(source_order);
        if (source_field == branch_tree.branch_resource_fields.end()) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch resource has no retained source identity",
                 "$.charts[].bms_control_tree");
        }
        const auto replaced = replace_retained_header_value(
            line, source_field->second.first, source_field->second.second, resource_edit->second);
        if (!replaced.has_value() || !emitted_branch_resource_edits.insert(source_order).second) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch resource does not identify one unambiguous retained source "
                 "statement",
                 "$.charts[].bms_control_tree");
        }
        preserved_tree_lines.push_back(*replaced);
    }
    if (emitted_branch_metadata_edits.size() != branch_metadata_edits.size()) {
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one or more edited branch metadata statements are absent from the retained tree",
             "$.charts[].bms_control_tree");
    }
    if (emitted_branch_resource_edits.size() != branch_resource_edits.size()) {
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one or more edited branch resource statements are absent from the retained tree",
             "$.charts[].bms_control_tree");
    }
    if (emitted_branch_measure_ratio_edits.size() != branch_measure_ratio_edits.size()) {
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one or more edited branch measure lengths are absent from the retained tree",
             "$.charts[].bms_control_tree");
    }
    if (emitted_branch_direct_bpm_edits.size() != branch_direct_bpm_edits.size()) {
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one or more edited branch BPM statements are absent from the retained tree",
             "$.charts[].bms_control_tree");
    }
    if (emitted_branch_timing_declaration_edits.size() != branch_timing_declaration_edits.size()) {
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one or more edited extended BPM or STOP declarations are absent from the retained "
             "tree",
             "$.charts[].bms_control_tree");
    }
    if (emitted_branch_channel_edits.size() != branch_channel_edits.size()) {
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one or more edited branch channel lines are absent from the retained tree",
             "$.charts[].bms_control_tree");
    }
    if (branch_tree.has_branches && !preserved_tree_lines.empty()) {
        if (!branch_tree.branch_source_begin.has_value() ||
            !branch_tree.branch_source_end.has_value()) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "retained BMS branch tree has no source-order span",
                 "$.charts[].bms_control_tree");
        }
        bool root_before_tree = true;
        bool root_after_tree = !branch_tree.root_source_orders.empty();
        for (const auto source_order : branch_tree.root_source_orders) {
            root_before_tree = root_before_tree && source_order < *branch_tree.branch_source_begin;
            root_after_tree = root_after_tree && source_order > *branch_tree.branch_source_end;
        }
        if (!branch_tree.root_source_orders.empty() && !root_before_tree && !root_after_tree) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "root semantic statements occur on both sides of retained branch source; this "
                 "writer cannot interleave normalized lines without changing override order",
                 "$.charts[].bms_control_tree");
        }
        if (root_after_tree) {
            output.insert(output.begin(), preserved_tree_lines.begin(), preserved_tree_lines.end());
        } else {
            output.insert(output.end(), preserved_tree_lines.begin(), preserved_tree_lines.end());
        }
    }

    std::string bytes = "\xef\xbb\xbf";
    for (const auto& line : output) {
        bytes += line;
        bytes.push_back('\n');
    }
    return bytes;
}

} // namespace

auto bms_write_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto write_bms(const song_document_t& song, bms_write_options_t options) -> bms_write_result_t {
    bms_write_result_t result;
    try {
        const chart_document_t* chart{};
        if (options.chart.has_value()) {
            const auto found =
                std::ranges::find(song.charts, *options.chart, &chart_document_t::id);
            if (found == song.charts.end()) {
                fail(bms_write_diagnostic_code_t::missing_chart,
                     "requested chart ID does not exist", "$.charts");
            }
            chart = &*found;
        } else if (song.charts.size() == 1) {
            chart = &song.charts.front();
        } else {
            fail(bms_write_diagnostic_code_t::missing_chart,
                 "select one chart explicitly when the song does not contain exactly one chart",
                 "$.charts");
        }
        const auto had_active_recipes = std::ranges::any_of(
            chart->sv_recipes, [](const auto& recipe) { return recipe.enabled; });
        auto baked_chart = bake_sv_recipes_for_export(*chart);
        if (!baked_chart) {
            fail(bms_write_diagnostic_code_t::unrepresentable_number,
                 "SV recipe export bake failed: " + baked_chart.diagnostics.front().message,
                 "$.charts[].sv_recipes");
        }
        chart = &*baked_chart.chart;

        if (!options.output_encoding.has_value() && options.preserve_unchanged_source &&
            song.charts.size() == 1 && !had_active_recipes &&
            chart->beat_zero_audio_offset_seconds.numerator == 0 &&
            options.branch_policy == bms_branch_policy_t::preserve_tree) {
            if (const auto source = retained_source_bytes(song, *chart); source) {
                result.bytes =
                    std::string(reinterpret_cast<const char*>(source->data()), source->size());
                return result;
            }
        }
        auto normalized = normalized_write(song, *chart, options, result.diagnostics);
        if (!options.output_encoding.has_value()) {
            result.bytes = std::move(normalized);
            return result;
        }
        auto encoded = encode_normalized_bms(normalized, *options.output_encoding);
        if (encoded.unsupported) {
            fail(bms_write_diagnostic_code_t::unsupported_encoding,
                 "requested " + std::string(encoding_name(*options.output_encoding)) +
                     " output encoding is unavailable",
                 "$.output_encoding");
        }
        if (!encoded.bytes.has_value()) {
            fail(bms_write_diagnostic_code_t::unrepresentable_text,
                 "normalized BMS text cannot be represented as " +
                     std::string(encoding_name(*options.output_encoding)) + " near UTF-8 byte " +
                     std::to_string(encoded.error_offset),
                 "$.output_encoding");
        }
        result.bytes = std::move(*encoded.bytes);
    } catch (const write_failure_t& failure) {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, failure.code, failure.message, failure.path});
    }
    return result;
}

} // namespace wormedit

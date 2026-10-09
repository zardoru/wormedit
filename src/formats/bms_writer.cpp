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
#elif defined(_WIN32)
#include "win32_codepage.h"
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
#elif defined(_WIN32)
    const auto code_page = win32_codepage::code_page_for(encoding);
    if (!code_page.has_value()) {
        return {.unsupported = true};
    }
    auto converted = win32_codepage::from_utf8(*code_page, utf8);
    return {.bytes = std::move(converted.text), .error_offset = converted.error_offset};
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
    // Subdivision of the source line the events came from, so `00` padding
    // survives edits instead of collapsing to the coarsest exact grid.
    std::uint64_t minimum_slots{1};
};

struct hold_endpoint_t {
    std::uint64_t row{};
    std::uint64_t note_id{};
    bool is_end{};
    std::string path;
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
    // Root comments and unknown headers (source order, exact source text) that no
    // model field owns; normalized output re-emits them verbatim in source order.
    std::vector<std::pair<std::size_t, std::string>> root_unmodeled_lines;
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

struct branch_write_state_t {
    branch_tree_analysis_t retained_tree;
    branch_tree_analysis_t tree;
    bool flattening_active_projection{};
    std::optional<std::set<std::size_t>> active_orders;
    std::map<std::size_t, std::string> measure_ratio_edits;
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

auto is_timing_declaration(std::string_view token) -> bool {
    return (token.size() == 5 && token.starts_with("BPM") && base36_digit(token[3]) &&
            base36_digit(token[4])) ||
           (token.size() == 6 && token.starts_with("STOP") && base36_digit(token[4]) &&
            base36_digit(token[5]));
}

auto source_line_for_node(const bms_control_node_t& node,
                          std::size_t measure = 0)
    -> source_measure_line_t {
    return {
        .source_order = node.source_order,
        .measure_index = measure,
        .text = node.original_text,
        .raw_value = node.raw_value,
        .source_span = node.source_span,
        .line_ending = node.line_ending,
    };
}

void record_timing_declaration(const bms_control_node_t& node, std::string_view token,
                               branch_tree_analysis_t& result) {
    if (is_timing_declaration(token)) {
        result.timing_declarations.emplace(
            node.source_order,
            retained_timing_declaration_t{node.source_order, std::string(token), node.raw_value,
                                          node.original_text, node.source_span, node.line_ending});
    }
}

auto branch_channel_line(const bms_control_node_t& node, std::string_view token)
    -> std::optional<retained_channel_line_t> {
    if (is_measure_length_command(token) || token.size() != 5 || token[0] < '0' || token[0] > '9' ||
        token[1] < '0' || token[1] > '9' || token[2] < '0' || token[2] > '9' ||
        !base36_digit(token[3]) || !base36_digit(token[4]) || node.original_text.size() < 7 ||
        node.original_text[6] != ':') {
        return std::nullopt;
    }
    return retained_channel_line_t{
        node.source_order,
        static_cast<std::size_t>((token[0] - '0') * 100 + (token[1] - '0') * 10 +
                                 (token[2] - '0')),
        std::string(token.substr(3, 2)), node.original_text, node.raw_value, node.source_span,
        node.line_ending};
}

void analyze_branch_raw_statement(const bms_control_node_t& node, std::string_view token,
                                  branch_tree_analysis_t& result) {
    result.branch_source_orders.insert(node.source_order);
    if (safe_metadata_token(token) &&
        (!generated_or_channel_token(token) || token == "PLAYER")) {
        result.metadata_fields.emplace(node.source_order,
                                       std::pair{std::string(token), node.raw_value});
        result.branch_metadata_fields.emplace(node.source_order,
                                              std::pair{std::string(token), node.raw_value});
    }
    if (const auto measure = is_measure_length_command(token); measure) {
        result.branch_measure_indices.insert(*measure);
        result.branch_measure_ratio_lines.emplace(node.source_order,
                                                  source_line_for_node(node, *measure));
    }
    if (is_timing_command(token)) {
        result.has_branch_timing = true;
        result.retained_timing_source_orders.insert(node.source_order);
        record_timing_declaration(node, token, result);
        if (token == "BPM") {
            result.branch_direct_bpm_lines.emplace(node.source_order, source_line_for_node(node));
        }
    }
    if ((token.starts_with("WAV") || token.starts_with("BMP")) && token.size() == 5 &&
        base36_digit(token[3]) && base36_digit(token[4])) {
        result.branch_resource_ids.emplace(std::string(token.substr(0, 3)),
                                           std::string(token.substr(3, 2)));
        result.branch_resource_fields.emplace(
            node.source_order, std::pair{std::string(token), node.raw_value});
        result.branch_resource_source_lines.emplace(node.source_order, node.original_text);
    }
    if (auto channel = branch_channel_line(node, token); channel) {
        result.branch_channel_lines.emplace(node.source_order, std::move(*channel));
    }
    result.preserved_lines.emplace_back(node.source_order, node.original_text);
}

void analyze_root_raw_statement(const bms_control_node_t& node, std::string_view token,
                                branch_tree_analysis_t& result) {
    if (safe_metadata_token(token)) {
        result.metadata_fields.emplace(node.source_order,
                                       std::pair{std::string(token), node.raw_value});
    }
    if (const auto measure = is_measure_length_command(token); measure) {
        result.root_measure_ratio_lines.push_back(source_line_for_node(node, *measure));
    }
    if (is_timing_command(token)) {
        result.root_timing_lines.emplace_back(node.source_order, node.original_text);
        record_timing_declaration(node, token, result);
        if (token == "BPM") {
            result.root_direct_bpm_lines.push_back(source_line_for_node(node));
        }
    } else if (safe_metadata_token(token) || generated_or_channel_token(token) ||
               is_measure_length_command(token)) {
        result.root_source_orders.insert(node.source_order);
    }
    if (!safe_metadata_token(token) && !generated_or_channel_token(token)) {
        result.has_unmodeled_root_statement = true;
        result.root_unmodeled_lines.emplace_back(node.source_order, node.original_text);
    }
}

void visit_branch_tree(const bms_control_node_t& node, bool inside_branch,
                       branch_tree_analysis_t& result) {
    if (is_branch_boundary(node.kind)) {
        result.has_branches = true;
    }
    const auto synthetic = node.kind == bms_control_kind_t::root ||
                           node.kind == bms_control_kind_t::if_group;
    const auto control_statement = !synthetic && node.kind != bms_control_kind_t::raw_statement;
    if (!synthetic && node.source_order < std::numeric_limits<std::size_t>::max()) {
        if (inside_branch) {
            result.branch_source_orders.insert(node.source_order);
        }
        if (control_statement && !node.original_text.empty()) {
            result.preserved_lines.emplace_back(node.source_order, node.original_text);
        } else if (node.kind == bms_control_kind_t::raw_statement && !node.original_text.empty()) {
            const auto token = ascii_upper(node.command_token);
            if (inside_branch) {
                analyze_branch_raw_statement(node, token, result);
            } else {
                analyze_root_raw_statement(node, token, result);
            }
        }
    }
    if (node.close_text && node.close_source_order) {
        result.preserved_lines.emplace_back(*node.close_source_order, *node.close_text);
    }
    const auto child_scope = inside_branch || is_branch_boundary(node.kind);
    for (const auto& child : node.children) {
        visit_branch_tree(child, child_scope, result);
    }
}

auto analyze_branch_tree(const bms_control_node_t& root) -> branch_tree_analysis_t {
    branch_tree_analysis_t result;
    visit_branch_tree(root, false, result);
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
    std::ranges::stable_sort(result.root_unmodeled_lines, {},
                             [](const auto& line) { return line.first; });
    return result;
}

void validate_normalized_write_input(const song_document_t& song, const chart_document_t& chart) {
    auto scope = song;
    scope.charts.clear();
    scope.charts.push_back(chart);
    scope.output_groups.clear();
    const auto validation = validate_document(scope);
    const auto error = std::ranges::find_if(validation, [](const auto& item) {
        return item.severity == diagnostic_severity_t::error;
    });
    if (error != validation.end()) {
        fail(bms_write_diagnostic_code_t::invalid_document,
             "document validation failed before BMS export: " + error->message, "$");
    }
    if (chart.measures.size() > 1000) {
        fail(bms_write_diagnostic_code_t::resource_limit,
             "BMS measure indices are limited to 000 through 999", "$.charts[].measures");
    }
}

auto active_branch_orders_for_write(const branch_tree_analysis_t& tree,
                                    const chart_document_t& chart,
                                    const bms_write_options_t& options)
    -> std::optional<std::set<std::size_t>> {
    if (!tree.has_branches || options.branch_policy != bms_branch_policy_t::preserve_tree) {
        return std::nullopt;
    }
    const auto projection = project_bms_branch_tree(*chart.bms_control_tree,
                                                    chart.active_branch_selection);
    if (!projection) {
        return std::nullopt;
    }
    return std::set<std::size_t>(projection.active_statement_source_orders.begin(),
                                 projection.active_statement_source_orders.end());
}

auto effective_measure_length_sources(const branch_tree_analysis_t& tree,
                                      const std::optional<std::set<std::size_t>>& active_orders)
    -> std::map<std::size_t, std::pair<std::size_t, rational_t>> {
    auto sources = std::map<std::size_t, std::pair<std::size_t, rational_t>>{};
    const auto retain = [&](const source_measure_line_t& line, bool active) {
        if (!active) {
            return;
        }
        const auto parsed = parse_measure_length(line.raw_value);
        if (!parsed.value) {
            return;
        }
        auto& current = sources[line.measure_index];
        if (current.first <= line.source_order) {
            current = {line.source_order, *parsed.value};
        }
    };
    for (const auto& line : tree.root_measure_ratio_lines) {
        retain(line, true);
    }
    if (active_orders) {
        for (const auto& [order, line] : tree.branch_measure_ratio_lines) {
            retain(line, active_orders->contains(order));
        }
    }
    return sources;
}

auto branch_measure_length_edits(
    const chart_document_t& chart, const branch_tree_analysis_t& tree,
    const std::optional<std::set<std::size_t>>& active_orders,
    const std::shared_ptr<const std::vector<std::byte>>& source_bytes)
    -> std::map<std::size_t, std::string> {
    const auto effective_sources = effective_measure_length_sources(tree, active_orders);
    auto edits = std::map<std::size_t, std::string>{};
    for (const auto measure_index : tree.branch_measure_indices) {
        if (measure_index >= chart.measures.size()) {
            continue;
        }
        const auto projection = std::ranges::find(chart.bms_measure_length_projections,
                                                  measure_index,
                                                  &bms_measure_length_projection_t::measure_index);
        if (projection == chart.bms_measure_length_projections.end()) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "a branch-controlled measure length has no unique import baseline",
                 "$.charts[].measures[" + std::to_string(measure_index) +
                     "].definition.length");
        }
        const auto& current = chart.measures[measure_index].definition.length;
        if (current == projection->imported_length) {
            continue;
        }
        const auto line = tree.branch_measure_ratio_lines.find(projection->source_order);
        const auto effective = effective_sources.find(measure_index);
        if (!active_orders || line == tree.branch_measure_ratio_lines.end() ||
            line->second.measure_index != measure_index ||
            !active_orders->contains(projection->source_order) ||
            effective == effective_sources.end() ||
            effective->second.first != projection->source_order ||
            effective->second.second != projection->imported_length || !source_bytes ||
            !source_measure_line_matches_bytes(line->second, *source_bytes)) {
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "an edited branch-controlled measure length does not map to one unchanged "
                 "active source statement",
                 "$.charts[].measures[" + std::to_string(measure_index) +
                     "].definition.length");
        }
        edits.emplace(projection->source_order,
                      exact_decimal(current.numerator, current.denominator,
                                    "$.charts[].measures[" + std::to_string(measure_index) +
                                        "].definition.length"));
    }
    return edits;
}

// Comments and unknown headers are copied byte for byte, so only single-line
// UTF-8 text is safe to place in the normalized (UTF-8) output.
auto can_emit_unmodeled_root_line(const std::string& text) -> bool {
    return !text.empty() && !has_line_break_or_nul(text) && valid_utf8(text);
}

auto prepare_branch_write_state(const song_document_t& song, const chart_document_t& chart,
                                const bms_write_options_t& options,
                                std::vector<bms_write_diagnostic_t>& diagnostics)
    -> branch_write_state_t {
    auto retained = chart.bms_control_tree ? analyze_branch_tree(*chart.bms_control_tree)
                                          : branch_tree_analysis_t{};
    const auto flatten = options.branch_policy == bms_branch_policy_t::flatten_active;
    if (retained.has_branches && flatten && chart.active_branch_selection.empty()) {
        fail(bms_write_diagnostic_code_t::unsupported_branch_policy,
             "active-projection flattening requires explicit or fixed choices for the retained "
             "branch tree",
             "$.charts[].active_branch_selection");
    }
    if (retained.has_branches && flatten) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "the explicitly selected active BMS projection is flattened and inactive branches "
             "are omitted",
             "$.charts[].bms_control_tree");
    }
    if (!chart.active_branch_selection.empty() && !retained.has_branches) {
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "active branch selections have no retained source tree to preserve",
             "$.charts[].active_branch_selection");
    }
    auto state = branch_write_state_t{};
    state.flattening_active_projection = flatten;
    state.retained_tree = retained;
    state.tree = flatten ? branch_tree_analysis_t{} : std::move(retained);
    state.active_orders = active_branch_orders_for_write(state.tree, chart, options);
    state.measure_ratio_edits = branch_measure_length_edits(
        chart, state.tree, state.active_orders, source_provenance_bytes(song, chart));
    if (state.tree.has_branches) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "branch content remains source-backed; only uniquely attributable active event and "
             "timing edits are applied in place",
             "$.charts[].bms_control_tree");
    }
    if (std::ranges::any_of(state.tree.root_unmodeled_lines, [](const auto& line) {
            return !can_emit_unmodeled_root_line(line.second);
        })) {
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "unmodeled root-level raw source lines that are not single-line UTF-8 were "
             "omitted from normalized BMS output",
             "$.charts[].bms_control_tree");
    }
    return state;
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

auto is_branch_source(const branch_tree_analysis_t& tree,
                     const source_provenance_t& provenance) -> bool {
    return has_bms_source_provenance(provenance) &&
           tree.branch_source_orders.contains(provenance.source_order);
}

auto is_retained_timing_source(const branch_tree_analysis_t& tree,
                               const source_provenance_t& provenance) -> bool {
    return has_bms_source_provenance(provenance) && tree.has_branches &&
           tree.retained_timing_source_orders.contains(provenance.source_order);
}

auto effective_direct_bpm_source(
    const branch_tree_analysis_t& tree,
    const std::optional<std::set<std::size_t>>& active_orders)
    -> std::optional<std::pair<std::size_t, signed_rational_t>> {
    std::optional<std::pair<std::size_t, signed_rational_t>> effective;
    const auto retain = [&](const source_measure_line_t& source, bool active) {
        if (!active) return;
        const auto parsed = parse_measure_length(source.raw_value);
        if (!parsed.value || parsed.value->numerator >
                                 static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return;
        if (!effective || effective->first <= source.source_order) {
            effective = std::pair{source.source_order,
                                  signed_rational_t{static_cast<std::int64_t>(parsed.value->numerator),
                                                    parsed.value->denominator}};
        }
    };
    for (const auto& source : tree.root_direct_bpm_lines) retain(source, true);
    if (active_orders) {
        for (const auto& [order, source] : tree.branch_direct_bpm_lines)
            retain(source, active_orders->contains(order));
    }
    return effective;
}

auto branch_bpm_edit(const bpm_event_t& bpm, const chart_document_t& chart,
                     const branch_tree_analysis_t& tree,
                     const std::optional<std::set<std::size_t>>& active_orders,
                     const std::optional<std::pair<std::size_t, signed_rational_t>>& effective,
                     const std::shared_ptr<const std::vector<std::byte>>& source_bytes)
    -> std::optional<std::pair<std::size_t, std::string>> {
    if (!is_retained_timing_source(tree, bpm.provenance)) return std::nullopt;
    const auto projection = std::ranges::find(chart.bms_bpm_projections, bpm.id,
                                               &bms_bpm_projection_t::event);
    if (projection == chart.bms_bpm_projections.end() ||
        projection->source_order != bpm.provenance.source_order)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "source-backed BMS BPM timing was edited structurally or lacks its import baseline",
             "$.charts[].bpms");
    const auto channel = branch_channel_slot_field(bpm.provenance.source_field);
    if (is_branch_source(tree, bpm.provenance) && channel &&
        (channel->channel == "03" || channel->channel == "08")) return std::nullopt;
    if (projection->imported_position != bpm.position)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "a retained direct BPM declaration cannot be moved as a channel event",
             "$.charts[].bpms");
    if (projection->imported_beats_per_minute == bpm.beats_per_minute) return std::nullopt;
    const auto source = tree.branch_direct_bpm_lines.find(projection->source_order);
    if (source == tree.branch_direct_bpm_lines.end())
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "edited branch BPM does not map to a retained direct-BPM source statement",
             "$.charts[].bpms");
    if (!active_orders || !active_orders->contains(projection->source_order) || !effective ||
        effective->first != projection->source_order ||
        effective->second != projection->imported_beats_per_minute)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "edited branch BPM is not the unique effective declaration in the active projection",
             "$.charts[].bpms");
    if (bpm.provenance.source_field != "BPM" ||
        bpm.provenance.source_spelling != source->second.text || !source_bytes ||
        !source_measure_line_matches_bytes(source->second, *source_bytes))
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "edited branch BPM source provenance does not match the retained source bytes",
             "$.charts[].bpms");
    return std::pair{projection->source_order,
                     exact_decimal(bpm.beats_per_minute,
                                   "$.charts[].bpms[].beats_per_minute")};
}

void validate_retained_pause(const pause_event_t& pause, const chart_document_t& chart,
                             const branch_tree_analysis_t& tree) {
    if (!is_retained_timing_source(tree, pause.provenance)) return;
    const auto projection = std::ranges::find(chart.bms_pause_projections, pause.id,
                                               &bms_pause_projection_t::event);
    if (projection == chart.bms_pause_projections.end() ||
        projection->source_order != pause.provenance.source_order)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "source-backed BMS pause timing was edited or lacks its import baseline; direct branch timing editing requires a retained channel event",
             "$.charts[].pauses");
    const auto channel = branch_channel_slot_field(pause.provenance.source_field);
    if (is_branch_source(tree, pause.provenance) && channel && channel->channel == "09") return;
    if (projection->imported_position != pause.position ||
        projection->imported_kind != pause.kind ||
        projection->imported_duration != pause.duration ||
        projection->imported_source_unit != pause.source_unit)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "source-backed BMS pause timing was edited without an attributable branch channel event",
             "$.charts[].pauses");
}

void warn_bgm_hold_tails(const chart_document_t& chart,
                          std::vector<bms_write_diagnostic_t>& diagnostics) {
    for (std::size_t measure = 0; measure < chart.measures.size(); ++measure)
        for (const auto& event : chart.measures[measure].bgm_events)
            if (event.end)
                warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                     "BMS BGM channel retains only the head; hold tail is omitted",
                     "$.charts[].measures[" + std::to_string(measure) +
                         "].bgm_events[" + std::to_string(event.id.value) + "].end");
}

void validate_unsupported_chart_features(const chart_document_t& chart,
                                         bool flattening_active_projection,
                                         const branch_tree_analysis_t& tree,
                                         std::vector<bms_write_diagnostic_t>& diagnostics) {
    const auto unsupported = [&](const auto& events) {
        return std::ranges::any_of(events, [&](const auto& event) {
            return !is_branch_source(tree, event.provenance) &&
                   !is_retained_timing_source(tree, event.provenance);
        });
    };
    if ((!flattening_active_projection && unsupported(chart.pauses)) || unsupported(chart.warps) ||
        unsupported(chart.scrolls) || unsupported(chart.speeds) || unsupported(chart.kiai) ||
        !chart.timing_anchors.empty() || !chart.sections.empty() || !chart.sound_channels.empty())
        fail(bms_write_diagnostic_code_t::unsupported_feature,
             "chart contains timing or media features outside the normalized writer slice",
             "$.charts[]");
    warn_bgm_hold_tails(chart, diagnostics);
    if (!chart.bookmarks.empty())
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "BMS has no supported bookmark representation; bookmarks are omitted",
             "$.charts[].bookmarks");
    const auto has_semantic_notes = std::ranges::any_of(chart.measures, [](const auto& measure) {
        return std::ranges::any_of(measure.notes, [](const auto& note) {
            return !note.semantic_layers.empty();
        });
    });
    if (!chart.semantic_layers.empty() || has_semantic_notes)
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "semantic note layers are omitted while their underlying note types are retained",
             "$.charts[].semantic_layers");
}

auto export_title_artist(const song_document_t& song, const chart_document_t& chart)
    -> std::pair<std::string, std::string> {
    auto title = song.metadata.title.empty() ? chart.metadata.name : song.metadata.title;
    auto artist = song.metadata.artist.empty() ? chart.metadata.author : song.metadata.artist;
    if (has_line_break_or_nul(title) || has_line_break_or_nul(artist) ||
        has_edge_whitespace(title) || has_edge_whitespace(artist) || !valid_utf8(title) ||
        !valid_utf8(artist))
        fail(bms_write_diagnostic_code_t::invalid_document,
             "BMS title and artist must be valid UTF-8 single-line values without edge whitespace",
             "$.metadata");
    return {std::move(title), std::move(artist)};
}

struct export_lane_channels_t {
    std::map<std::uint64_t, std::string> visible;
    std::map<std::uint64_t, std::string> long_note;
};

auto normalize_lane_channel(std::string channel, bool long_note,
                            std::set<std::string>& used) -> std::string {
    for (auto& character : channel)
        if (character >= 'a' && character <= 'z')
            character = static_cast<char>(character - 'a' + 'A');
    if (channel.size() != 2 || !base36_digit(channel[0]) || !base36_digit(channel[1]) ||
        (long_note && channel[0] != '5' && channel[0] != '6') || !used.insert(channel).second)
        fail(bms_write_diagnostic_code_t::invalid_document,
             long_note ? "lane profile has an invalid or duplicate long-note BMS channel"
                       : "lane profile has an invalid or duplicate visible BMS channel",
             "$.charts[].lane_layouts");
    return channel;
}

auto export_lane_channels(const lane_layout_t& layout) -> export_lane_channels_t {
    export_lane_channels_t result;
    std::set<std::string> used;
    for (const auto& lane : layout.lanes) {
        if (lane.bms_channels.visible)
            result.visible.emplace(lane.id.value,
                                   normalize_lane_channel(*lane.bms_channels.visible, false, used));
        if (lane.bms_channels.long_note)
            result.long_note.emplace(lane.id.value,
                                     normalize_lane_channel(*lane.bms_channels.long_note, true, used));
    }
    return result;
}

struct resource_declaration_t {
    std::string name_space;
    const resource_entry_t* resource{};
    bool branch_scoped{};
};

struct resource_export_state_t {
    std::map<std::uint64_t, std::string> audio_ids;
    std::map<std::uint64_t, std::string> visual_ids;
    std::set<std::uint64_t> branch_resource_ids;
    std::map<std::size_t, std::string> branch_resource_edits;
    std::vector<resource_declaration_t> declarations;
};

auto resource_namespace(const song_document_t& song, const resource_entry_t& resource)
    -> std::pair<std::string, bool> {
    const auto audio = std::ranges::find(song.audio_assets, resource.asset, &audio_asset_t::id);
    const auto visual = std::ranges::find(song.visual_assets, resource.asset, &visual_asset_t::id);
    const bool is_audio = audio != song.audio_assets.end();
    const bool is_visual = visual != song.visual_assets.end();
    if (is_audio == is_visual || resource.missing)
        fail(bms_write_diagnostic_code_t::unresolved_resource,
             "resource must refer to exactly one available audio or visual asset", "$.resources[]");
    if (resource.name.empty() || has_line_break_or_nul(resource.name) ||
        has_edge_whitespace(resource.name) || !valid_utf8(resource.name))
        fail(bms_write_diagnostic_code_t::unresolved_resource,
             "resource path must be a valid UTF-8 single-line value without edge whitespace",
             "$.resources[].name");
    return {is_audio ? "WAV" : "BMP", is_audio};
}

void retain_branch_resource(const resource_entry_t& resource, std::string_view name_space,
                            const std::optional<std::string>& identifier,
                            const branch_tree_analysis_t& tree,
                            std::map<std::string, std::set<std::string>>& assigned_ids,
                            std::map<std::size_t, std::string>& edits) {
    const auto retained = tree.branch_resource_fields.find(resource.provenance.source_order);
    const auto line = tree.branch_resource_source_lines.find(resource.provenance.source_order);
    const auto token = identifier ? std::string(name_space) + *identifier : std::string{};
    if (!identifier || retained == tree.branch_resource_fields.end() ||
        line == tree.branch_resource_source_lines.end() || retained->second.first != token ||
        ascii_upper(resource.provenance.source_field) != token ||
        resource.provenance.source_spelling != line->second ||
        !tree.branch_resource_ids.contains({std::string(name_space), *identifier}) ||
        assigned_ids[std::string(name_space)].contains(*identifier))
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch resources must retain one matching source statement, namespace, and identifier",
             "$.resources[].target_identities");
    if (resource.name != trim_ascii_whitespace(retained->second.second))
        edits.emplace(resource.provenance.source_order, resource.name);
}

auto allocate_resource_identifier(const resource_entry_t& resource, std::string_view name_space,
                                  const branch_tree_analysis_t& tree,
                                  std::map<std::string, std::size_t>& next_ids,
                                  std::map<std::string, std::set<std::string>>& assigned_ids,
                                  std::vector<bms_write_diagnostic_t>& diagnostics)
    -> std::string {
    auto identifier = bms_identity(resource, name_space);
    if (identifier && !assigned_ids[std::string(name_space)].contains(*identifier) &&
        !tree.branch_resource_ids.contains({std::string(name_space), *identifier}))
        return *identifier;
    const bool had_identifier = identifier.has_value();
    auto& next = next_ids[std::string(name_space)];
    while (next < 1296 &&
           (assigned_ids[std::string(name_space)].contains(base36_id(next)) ||
            tree.branch_resource_ids.contains({std::string(name_space), base36_id(next)})))
        ++next;
    if (next >= 1296)
        fail(bms_write_diagnostic_code_t::resource_limit,
             "BMS resource namespace exceeds 1295 nonzero two-digit identifiers", "$.resources");
    auto allocated = base36_id(next++);
    if (had_identifier)
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "colliding BMS resource identifier was reassigned deterministically",
             "$.resources[].target_identities");
    return allocated;
}

auto export_resources(const song_document_t& song, const branch_tree_analysis_t& tree,
                      std::vector<bms_write_diagnostic_t>& diagnostics) -> resource_export_state_t {
    resource_export_state_t state;
    std::map<std::string, std::size_t> next_ids{{"WAV", 1}, {"BMP", 1}};
    std::map<std::string, std::set<std::string>> assigned_ids;
    state.declarations.reserve(song.resources.size());
    for (const auto& resource : song.resources) {
        const auto [name_space, is_audio] = resource_namespace(song, resource);
        const bool branch_scoped = is_branch_source(tree, resource.provenance);
        auto identifier = bms_identity(resource, name_space);
        if (branch_scoped)
            retain_branch_resource(resource, name_space, identifier, tree, assigned_ids,
                                  state.branch_resource_edits);
        else
            identifier = allocate_resource_identifier(resource, name_space, tree, next_ids,
                                                       assigned_ids, diagnostics);
        assigned_ids[name_space].insert(*identifier);
        if (is_audio) state.audio_ids.emplace(resource.id.value, *identifier);
        else state.visual_ids.emplace(resource.id.value, *identifier);
        if (branch_scoped) state.branch_resource_ids.insert(resource.id.value);
        state.declarations.push_back({name_space, &resource, branch_scoped});
    }
    return state;
}

struct branch_channel_edit_t {
    const retained_channel_line_t* source{};
    std::string text;
    std::size_t payload_begin{};
    std::string payload;
    std::set<std::size_t> cleared_slots;
    std::map<std::size_t, std::string> written_slots;
    std::map<std::size_t, std::size_t> written_slot_origins;
};

struct branch_event_edit_state_t {
    const chart_document_t& chart;
    const branch_tree_analysis_t& tree;
    const std::optional<std::set<std::size_t>>& active_orders;
    const std::shared_ptr<const std::vector<std::byte>>& source_bytes;
    const measure_index_t& index;
    std::map<std::size_t, branch_channel_edit_t> channel_edits;
    std::map<std::pair<std::size_t, std::size_t>, std::string> slot_claims;
    std::map<std::size_t, std::string> timing_declaration_edits;
    std::map<std::size_t, std::vector<std::string>> converted_bgm_lines;
};

auto claim_retained_channel_slot(branch_event_edit_state_t& state,
                               const source_provenance_t& provenance,
                               const channel_slot_field_t& field, std::string_view path)
    -> const retained_channel_line_t* {
    const auto source = state.tree.branch_channel_lines.find(provenance.source_order);
    if (source == state.tree.branch_channel_lines.end() ||
        !state.tree.branch_source_orders.contains(provenance.source_order) ||
        !state.active_orders || !state.active_orders->contains(provenance.source_order) ||
        source->second.channel != field.channel || provenance.source_spelling != source->second.text ||
        !has_bms_source_provenance(provenance))
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch channel event does not identify one active retained source line",
             std::string(path));
    const auto payload = trim_ascii_whitespace(source->second.raw_value);
    const auto expected_measure = (source->second.measure_index < 10 ? std::string("00")
                                   : source->second.measure_index < 100 ? std::string("0")
                                                                        : std::string()) +
                                  std::to_string(source->second.measure_index);
    if (source->second.text.find(':') != 6 || source->second.text.substr(1, 3) != expected_measure ||
        ascii_upper(source->second.text.substr(4, 2)) != source->second.channel ||
        source->second.text.substr(7) != source->second.raw_value ||
        field.slot >= payload.size() / 2 || payload.size() % 2 != 0 ||
        ascii_upper(std::string(payload.substr(field.slot * 2, 2))) != field.object_id)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch channel event slot does not match its retained source payload",
             std::string(path));
    const auto [claim, inserted] = state.slot_claims.emplace(
        std::pair{provenance.source_order, field.slot}, std::string(path));
    if (!inserted && claim->second != path)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "more than one active semantic event claims a retained channel slot", std::string(path));
    return &source->second;
}

void apply_branch_channel_slot_edit(branch_event_edit_state_t& state, std::size_t source_order,
                              const retained_channel_line_t& source, std::size_t old_slot,
                              std::size_t new_slot, std::string_view object_id,
                              std::string_view path) {
    if (!state.source_bytes || !retained_line_matches_bytes(source.text, source.source_span,
                                                             source.line_ending, *state.source_bytes))
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "edited branch channel line does not match unchanged imported source bytes",
             std::string(path));
    auto [edit, inserted] = state.channel_edits.try_emplace(source_order);
    if (inserted) {
        const auto colon = source.text.find(':');
        const auto payload = trim_ascii_whitespace(source.raw_value);
        if (colon != 6 || source.text.substr(7) != source.raw_value || payload.empty() ||
            payload.size() % 2 != 0 || !std::ranges::all_of(payload, base36_digit))
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch channel payload is not a valid retained BMS slot line",
                 std::string(path));
        const auto untrimmed = std::string_view(source.text).substr(7);
        const auto trimmed_line_payload = trim_ascii_whitespace(untrimmed);
        edit->second.source = &source;
        edit->second.text = source.text;
        edit->second.payload_begin =
            7 + static_cast<std::size_t>(trimmed_line_payload.data() - untrimmed.data());
        edit->second.payload = std::string(trimmed_line_payload);
    }
    auto& patch = edit->second;
    const auto slots = patch.payload.size() / 2;
    if (old_slot >= slots || new_slot >= slots || object_id.size() != 2 ||
        !std::ranges::all_of(object_id, base36_digit))
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "edited branch event position or identifier does not fit its retained source line",
             std::string(path));
    if (old_slot != new_slot) patch.cleared_slots.insert(old_slot);
    const auto normalized_id = ascii_upper(std::string(object_id));
    const auto [written, write_inserted] = patch.written_slots.emplace(new_slot, normalized_id);
    if (!write_inserted && written->second != normalized_id)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "edited branch events collide in one retained channel slot", std::string(path));
    const auto [origin, origin_inserted] = patch.written_slot_origins.emplace(new_slot, old_slot);
    if (!origin_inserted && origin->second != old_slot)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "multiple edited branch events target one retained channel slot", std::string(path));
}

auto effective_timing_declaration(
    const branch_tree_analysis_t& tree,
    const std::optional<std::set<std::size_t>>& active_orders, std::string_view token,
    std::size_t event_source_order, signed_rational_t imported_value)
    -> const retained_timing_declaration_t* {
    const retained_timing_declaration_t* effective{};
    for (const auto& [order, declaration] : tree.timing_declarations) {
        if (declaration.token != token || order >= event_source_order ||
            (tree.branch_source_orders.contains(order) &&
             (!active_orders || !active_orders->contains(order))))
            continue;
        const auto parsed = parse_positive_timing_value(trim_ascii_whitespace(declaration.raw_value));
        if (!parsed || *parsed != imported_value) continue;
        if (!effective || effective->source_order < order) effective = &declaration;
    }
    return effective;
}

void validate_branch_timing_declaration_edit(
    const branch_tree_analysis_t& tree,
    const std::optional<std::set<std::size_t>>& active_orders,
    const std::shared_ptr<const std::vector<std::byte>>& source_bytes,
    const retained_timing_declaration_t& declaration, std::string_view identifier,
    std::string_view channel, std::string_view path) {
    if (!source_bytes || !retained_line_matches_bytes(declaration.text, declaration.source_span,
                                                      declaration.line_ending, *source_bytes))
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "edited branch timing declaration does not match unchanged imported source bytes",
             std::string(path));
    if (tree.branch_source_orders.contains(declaration.source_order)) {
        if (!active_orders || !active_orders->contains(declaration.source_order))
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "timing declaration is not inside the selected active branch", std::string(path));
        return;
    }
    for (const auto& [order, line] : tree.branch_channel_lines) {
        if ((active_orders && active_orders->contains(order)) || line.channel != channel ||
            order <= declaration.source_order)
            continue;
        const auto payload = trim_ascii_whitespace(line.raw_value);
        for (std::size_t slot = 0; slot + 1 < payload.size(); slot += 2)
            if (payload.substr(slot, 2) == identifier)
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "root timing declaration is also used by an inactive sibling branch, so editing it would change that sibling",
                     std::string(path));
    }
}

void validate_bpm_declaration_references(
    const chart_document_t& chart, const branch_tree_analysis_t& tree,
    const std::optional<std::set<std::size_t>>& active_orders,
    const retained_timing_declaration_t& target, std::string_view identifier,
    signed_rational_t replacement, std::string_view path) {
    for (const auto& bpm : chart.bpms) {
        const auto field = branch_channel_slot_field(bpm.provenance.source_field);
        if (!field || field->channel != "08" || field->object_id != identifier) continue;
        const auto projection = std::ranges::find(chart.bms_bpm_projections, bpm.id,
                                                   &bms_bpm_projection_t::event);
        if (projection == chart.bms_bpm_projections.end())
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "extended BPM event has no unique import baseline", std::string(path));
        const auto* declaration = effective_timing_declaration(
            tree, active_orders, "BPM" + std::string(identifier), bpm.provenance.source_order,
            projection->imported_beats_per_minute);
        if (declaration && declaration->source_order == target.source_order &&
            bpm.beats_per_minute != replacement)
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "all active events bound to one extended BPM declaration must agree on its replacement value",
                 std::string(path));
    }
}

void validate_pause_declaration_references(
    const chart_document_t& chart, const branch_tree_analysis_t& tree,
    const std::optional<std::set<std::size_t>>& active_orders,
    const retained_timing_declaration_t& target, std::string_view identifier,
    signed_rational_t replacement, std::string_view path) {
    for (const auto& pause : chart.pauses) {
        const auto field = branch_channel_slot_field(pause.provenance.source_field);
        if (!field || field->channel != "09" || field->object_id != identifier) continue;
        const auto projection = std::ranges::find(chart.bms_pause_projections, pause.id,
                                                   &bms_pause_projection_t::event);
        if (projection == chart.bms_pause_projections.end())
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "STOP event has no unique import baseline", std::string(path));
        const auto* declaration = effective_timing_declaration(
            tree, active_orders, "STOP" + std::string(identifier), pause.provenance.source_order,
            projection->imported_duration);
        if (declaration && declaration->source_order == target.source_order &&
            pause.duration != replacement)
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "all active pauses bound to one STOP declaration must agree on its replacement value",
                 std::string(path));
    }
}

auto expected_retained_branch_slot_position(const branch_event_edit_state_t& state,
                                   const retained_channel_line_t& source, std::size_t slot)
    -> std::optional<local_position_t> {
    if (source.measure_index >= state.chart.measures.size()) return std::nullopt;
    auto measure = state.chart.measures[source.measure_index];
    const auto projection = std::ranges::find(state.chart.bms_measure_length_projections,
                                               source.measure_index,
                                               &bms_measure_length_projection_t::measure_index);
    if (projection != state.chart.bms_measure_length_projections.end()) {
        measure.definition.length = projection->imported_length;
        measure.definition.meter.reset();
    }
    const auto capacity = validate_measure(state.chart.divisions_per_standard_measure,
                                           measure.definition);
    const auto slots = trim_ascii_whitespace(source.raw_value).size() / 2;
    if (!capacity.row_capacity || slots == 0 || *capacity.row_capacity % slots != 0 || slot >= slots)
        return std::nullopt;
    return local_position_t{measure.id, static_cast<std::uint64_t>(slot) *
                                           (*capacity.row_capacity / slots)};
}

auto map_position_to_branch_slot(const branch_event_edit_state_t& state,
                              const retained_channel_line_t& source,
                              local_position_t position, std::string_view path) -> std::size_t {
    if (source.measure_index >= state.chart.measures.size() ||
        state.chart.measures[source.measure_index].id != position.measure)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch channel event cannot be moved to a different measure", std::string(path));
    const auto [numerator, denominator] = fraction_at(state.index, position, std::string(path));
    const auto slots = trim_ascii_whitespace(source.raw_value).size() / 2;
    if (slots == 0 || numerator > std::numeric_limits<std::uint64_t>::max() / slots ||
        (numerator * slots) % denominator != 0 || numerator * slots / denominator >= slots)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "edited branch event position does not map to a slot on its retained source line",
             std::string(path));
    return static_cast<std::size_t>(numerator * slots / denominator);
}

struct branch_channel_event_edit_t {
    channel_slot_field_t field;
    const retained_channel_line_t* source{};
    local_position_t imported_position;
    std::size_t target_slot{};
    bool position_changed{};
};

auto describe_branch_bpm_edit(const bpm_event_t& bpm, branch_event_edit_state_t& state)
    -> std::optional<branch_channel_event_edit_t> {
    if (!is_branch_source(state.tree, bpm.provenance) || bpm.provenance.source_field == "BPM")
        return std::nullopt;
    const auto projection = std::ranges::find(state.chart.bms_bpm_projections, bpm.id,
                                               &bms_bpm_projection_t::event);
    const auto field = branch_channel_slot_field(bpm.provenance.source_field);
    if (projection == state.chart.bms_bpm_projections.end() ||
        projection->source_order != bpm.provenance.source_order || !field ||
        (field->channel != "03" && field->channel != "08"))
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "edited branch BPM does not identify one retained direct or extended BPM slot",
             "$.charts[].bpms");
    const auto path = "$.charts[].bpms[" + std::to_string(bpm.id.value) + "]";
    const auto* source = claim_retained_channel_slot(state, bpm.provenance, *field, path);
    const auto imported = expected_retained_branch_slot_position(state, *source, field->slot);
    if (!imported || *imported != projection->imported_position)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch BPM event slot does not match its imported position", "$.charts[].bpms");
    const auto target = bpm.position == projection->imported_position
                            ? field->slot
                            : map_position_to_branch_slot(state, *source, bpm.position,
                                                          "$.charts[].bpms[].position");
    return branch_channel_event_edit_t{*field, source, *imported, target, target != field->slot};
}

void apply_branch_bpm_edit(const bpm_event_t& bpm, branch_event_edit_state_t& state,
                           const auto& find_declaration, const auto& validate_declaration,
                           const auto& validate_references) {
    const auto edit = describe_branch_bpm_edit(bpm, state);
    if (!edit) return;
    const auto projection = std::ranges::find(state.chart.bms_bpm_projections, bpm.id,
                                               &bms_bpm_projection_t::event);
    const bool value_changed = bpm.beats_per_minute != projection->imported_beats_per_minute;
    if (!edit->position_changed && !value_changed) return;
    if (edit->field.channel == "03") {
        if (bpm.beats_per_minute.numerator <= 0 || bpm.beats_per_minute.denominator != 1 ||
            bpm.beats_per_minute.numerator > 255)
            fail(bms_write_diagnostic_code_t::unrepresentable_number,
                 "direct branch BPM channel events require an integer from 1 through 255",
                 "$.charts[].bpms[].beats_per_minute");
        constexpr std::string_view hex = "0123456789ABCDEF";
        const auto value = static_cast<unsigned>(bpm.beats_per_minute.numerator);
        const std::string object_id{hex[value >> 4U], hex[value & 0x0fU]};
        apply_branch_channel_slot_edit(state, bpm.provenance.source_order, *edit->source,
                                       edit->field.slot, edit->target_slot, object_id,
                                       "$.charts[].bpms");
        return;
    }
    if (value_changed) {
        const auto* declaration = find_declaration("BPM" + edit->field.object_id,
                                                    bpm.provenance.source_order,
                                                    projection->imported_beats_per_minute);
        if (!declaration)
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "extended BPM event does not resolve to one active retained declaration",
                 "$.charts[].bpms");
        validate_declaration(*declaration, edit->field.object_id, "08", "$.charts[].bpms");
        validate_references(*declaration, edit->field.object_id, bpm.beats_per_minute,
                            "$.charts[].bpms");
        state.timing_declaration_edits.insert_or_assign(
            declaration->source_order,
            exact_decimal(bpm.beats_per_minute, "$.charts[].bpms[].beats_per_minute"));
    }
    if (edit->position_changed)
        apply_branch_channel_slot_edit(state, bpm.provenance.source_order, *edit->source,
                                       edit->field.slot, edit->target_slot, edit->field.object_id,
                                       "$.charts[].bpms");
}

auto collect_branch_direct_bpm_edits(
    const chart_document_t& chart, const branch_tree_analysis_t& tree,
    const std::optional<std::set<std::size_t>>& active_orders,
    const std::shared_ptr<const std::vector<std::byte>>& source_bytes)
    -> std::map<std::size_t, std::string> {
    const auto effective_source = effective_direct_bpm_source(tree, active_orders);
    std::map<std::size_t, std::string> edits;
    for (const auto& bpm : chart.bpms) {
        if (const auto edit = branch_bpm_edit(bpm, chart, tree, active_orders, effective_source,
                                              source_bytes))
            edits.emplace(edit->first, edit->second);
    }
    return edits;
}

void validate_retained_pauses(const chart_document_t& chart,
                              const branch_tree_analysis_t& tree) {
    for (const auto& pause : chart.pauses) validate_retained_pause(pause, chart, tree);
}

void apply_branch_bpm_edits(const chart_document_t& chart,
                            branch_event_edit_state_t& state) {
    const auto find_declaration = [&](std::string_view token, std::size_t order,
                                      signed_rational_t imported) {
        return effective_timing_declaration(state.tree, state.active_orders, token, order,
                                             imported);
    };
    const auto validate_declaration = [&](const retained_timing_declaration_t& declaration,
                                          std::string_view identifier,
                                          std::string_view channel, std::string_view path) {
        validate_branch_timing_declaration_edit(state.tree, state.active_orders,
                                                state.source_bytes, declaration, identifier,
                                                channel, path);
    };
    const auto validate_references = [&](const retained_timing_declaration_t& target,
                                         std::string_view identifier,
                                         signed_rational_t replacement, std::string_view path) {
        validate_bpm_declaration_references(chart, state.tree, state.active_orders, target,
                                            identifier, replacement, path);
    };
    for (const auto& bpm : chart.bpms)
        apply_branch_bpm_edit(bpm, state, find_declaration, validate_declaration,
                              validate_references);
}

void apply_branch_pause_edits(const chart_document_t& chart,
                              branch_event_edit_state_t& state) {
    for (const auto& pause : chart.pauses) {
        if (!is_branch_source(state.tree, pause.provenance)) continue;
        const auto projection = std::ranges::find(chart.bms_pause_projections, pause.id,
                                                  &bms_pause_projection_t::event);
        const auto field = branch_channel_slot_field(pause.provenance.source_field);
        if (projection == chart.bms_pause_projections.end() ||
            projection->source_order != pause.provenance.source_order || !field.has_value() ||
            field->channel != "09" || pause.kind != pause_kind_t::stop ||
            pause.source_unit != pause_unit_t::bms_stop_ticks ||
            projection->imported_kind != pause.kind ||
            projection->imported_source_unit != pause.source_unit)
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch pause is not a uniquely attributable BMS STOP channel event",
                 "$.charts[].pauses");
        const auto* source = claim_retained_channel_slot(
            state, pause.provenance, *field,
            "$.charts[].pauses[" + std::to_string(pause.id.value) + "]");
        const auto original_position =
            expected_retained_branch_slot_position(state, *source, field->slot);
        if (!original_position || *original_position != projection->imported_position)
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "branch STOP event slot does not match its imported position",
                 "$.charts[].pauses");
        const auto target_slot = pause.position == projection->imported_position
                                     ? field->slot
                                     : map_position_to_branch_slot(
                                           state, *source, pause.position,
                                           "$.charts[].pauses[].position");
        const bool position_changed = target_slot != field->slot;
        const bool duration_changed = pause.duration != projection->imported_duration;
        if (!position_changed && !duration_changed) continue;
        if (duration_changed) {
            const auto* declaration = effective_timing_declaration(
                state.tree, state.active_orders, "STOP" + field->object_id,
                pause.provenance.source_order, projection->imported_duration);
            if (!declaration)
                fail(bms_write_diagnostic_code_t::branch_provenance,
                     "STOP event does not resolve to one active retained declaration",
                     "$.charts[].pauses");
            validate_branch_timing_declaration_edit(
                state.tree, state.active_orders, state.source_bytes, *declaration,
                field->object_id, "09", "$.charts[].pauses");
            validate_pause_declaration_references(chart, state.tree, state.active_orders,
                                                  *declaration, field->object_id, pause.duration,
                                                  "$.charts[].pauses");
            state.timing_declaration_edits.insert_or_assign(
                declaration->source_order,
                exact_decimal(pause.duration, "$.charts[].pauses[].duration"));
        }
        if (position_changed)
            apply_branch_channel_slot_edit(state, pause.provenance.source_order, *source,
                                           field->slot, target_slot, field->object_id,
                                           "$.charts[].pauses");
    }
}

struct flattened_timing_projection_t {
    std::vector<std::string> declarations;
    const bpm_event_t* initial_bpm{};
};

struct timing_identifier_registry_t {
    std::vector<std::pair<signed_rational_t, std::string>> identifiers;
    std::size_t next{1};
    std::string directive;
    std::string path;
};

auto timing_identifier(timing_identifier_registry_t& registry, signed_rational_t value,
                       std::vector<std::string>& declarations) -> std::string {
    const auto existing = std::ranges::find_if(registry.identifiers,
                                                [&](const auto& entry) {
                                                    return entry.first == value;
                                                });
    if (existing != registry.identifiers.end()) return existing->second;
    if (registry.next >= 1296)
        fail(bms_write_diagnostic_code_t::resource_limit,
             "flattened BMS timing exceeds 1295 " + registry.path, registry.path);
    const auto id = base36_id(registry.next++);
    declarations.push_back("#" + registry.directive + id + " " +
                           exact_decimal(value, registry.path));
    registry.identifiers.emplace_back(value, id);
    return id;
}

void add_timing_channel_event(const chart_document_t& chart, const measure_index_t& index,
                              std::map<std::pair<std::size_t, std::string>, channel_line_t>& lines,
                              local_position_t position, std::string_view channel,
                              std::string_view object_id, std::string path) {
    const auto measure = std::ranges::find(chart.measures, position.measure, &measure_chunk_t::id);
    if (measure == chart.measures.end())
        fail(bms_write_diagnostic_code_t::invalid_geometry,
             "timing event references a measure outside the chart", path);
    const auto measure_index = static_cast<std::size_t>(std::distance(chart.measures.begin(), measure));
    const auto [numerator, denominator] = fraction_at(index, position, path);
    const auto key = std::pair{measure_index, std::string(channel)};
    auto [line, inserted] = lines.try_emplace(
        key, channel_line_t{.measure = measure_index, .channel = std::string(channel)});
    (void)inserted;
    line->second.notes.push_back({numerator, denominator, std::string(object_id), std::move(path)});
}

auto ordered_bpm_events(const chart_document_t& chart, const measure_index_t& index)
    -> std::vector<std::pair<const bpm_event_t*, std::uint64_t>> {
    std::vector<std::pair<const bpm_event_t*, std::uint64_t>> result;
    result.reserve(chart.bpms.size());
    for (const auto& bpm : chart.bpms) {
        const auto absolute = absolute_row(index, bpm.position);
        if (!absolute)
            fail(bms_write_diagnostic_code_t::invalid_geometry,
                 "BPM event is outside the chart's measure geometry", "$.charts[].bpms");
        (void)exact_decimal(bpm.beats_per_minute, "$.charts[].bpms[].beats_per_minute");
        result.emplace_back(&bpm, *absolute);
    }
    std::ranges::stable_sort(result, [](const auto& left, const auto& right) {
        return std::tie(left.second, left.first->provenance.source_order, left.first->id.value) <
               std::tie(right.second, right.first->provenance.source_order, right.first->id.value);
    });
    return result;
}

void project_flattened_bpms(const chart_document_t& chart, const measure_index_t& index,
                            std::map<std::pair<std::size_t, std::string>, channel_line_t>& lines,
                            flattened_timing_projection_t& result) {
    auto ordered = ordered_bpm_events(chart, index);
    timing_identifier_registry_t ids{.directive = "BPM", .path = "$.charts[].bpms[].beats_per_minute"};
    std::optional<std::uint64_t> previous_row;
    for (const auto& [bpm, absolute] : ordered) {
        if (previous_row == absolute)
            fail(bms_write_diagnostic_code_t::duplicate_event,
                 "multiple BPM events share one BMS channel position and cannot be flattened without changing their ordering",
                 "$.charts[].bpms");
        previous_row = absolute;
        if (absolute == 0) {
            result.initial_bpm = bpm;
            continue;
        }
        const auto identifier = timing_identifier(ids, bpm->beats_per_minute, result.declarations);
        add_timing_channel_event(chart, index, lines, bpm->position, "08", identifier,
                                 "$.charts[].bpms[" + std::to_string(bpm->id.value) + "]");
    }
}

void project_flattened_pauses(const chart_document_t& chart, const measure_index_t& index,
                              std::map<std::pair<std::size_t, std::string>, channel_line_t>& lines,
                              flattened_timing_projection_t& result) {
    timing_identifier_registry_t ids{.directive = "STOP", .path = "$.charts[].pauses[].duration"};
    for (const auto& pause : chart.pauses) {
        const auto path = "$.charts[].pauses[" + std::to_string(pause.id.value) + "]";
        if (pause.kind != pause_kind_t::stop || pause.source_unit != pause_unit_t::bms_stop_ticks)
            fail(bms_write_diagnostic_code_t::unsupported_feature,
                 "active-projection flattening supports BMS STOP ticks only; delay and other pause units cannot be converted without a timing approximation",
                 path);
        (void)exact_decimal(pause.duration, "$.charts[].pauses[].duration");
        if (!absolute_row(index, pause.position))
            fail(bms_write_diagnostic_code_t::invalid_geometry,
                 "STOP event is outside the chart's measure geometry", "$.charts[].pauses");
        const auto identifier = timing_identifier(ids, pause.duration, result.declarations);
        add_timing_channel_event(chart, index, lines, pause.position, "09", identifier, path);
    }
}

auto flattened_timing_projection(
    const chart_document_t& chart, const measure_index_t& index,
    std::map<std::pair<std::size_t, std::string>, channel_line_t>& lines)
    -> flattened_timing_projection_t {
    flattened_timing_projection_t result;
    project_flattened_bpms(chart, index, lines, result);
    project_flattened_pauses(chart, index, lines, result);
    return result;
}

struct note_export_context_t {
    const song_document_t& song;
    const chart_document_t& chart;
    const branch_tree_analysis_t& tree;
    const lane_layout_t& layout;
    const measure_index_t& index;
    branch_event_edit_state_t& branch_edits;
    const std::map<std::uint64_t, std::string>& lane_channels;
    const std::map<std::uint64_t, std::string>& long_note_channels;
    const std::map<std::uint64_t, std::string>& audio_ids;
    const std::set<std::uint64_t>& branch_resource_ids;
    std::map<std::pair<std::size_t, std::string>, channel_line_t>& channel_lines;
    std::map<std::uint64_t, std::vector<hold_endpoint_t>>& hold_endpoints;
};

auto exported_audio_id(const note_export_context_t& context, resource_id_t resource,
                       std::string_view path) -> std::string {
    const auto wav = context.audio_ids.find(resource.value);
    if (wav == context.audio_ids.end())
        fail(bms_write_diagnostic_code_t::unresolved_resource,
             "note refers to a resource that is not exported as WAV", std::string(path));
    return wav->second;
}

void append_note_slot(note_export_context_t& context, std::size_t measure,
                      std::string_view channel, std::uint64_t numerator,
                      std::uint64_t denominator, std::string object_id, std::string path) {
    const auto key = std::pair{measure, std::string(channel)};
    auto [line, inserted] = context.channel_lines.try_emplace(
        key, channel_line_t{.measure = measure, .channel = std::string(channel)});
    (void)inserted;
    line->second.notes.push_back(
        {numerator, denominator, std::move(object_id), std::move(path)});
}

auto root_note_channel(const note_t& note, const note_export_context_t& context,
                       std::string_view path) -> std::string {
    if (note.kind != note_kind_t::tap && note.kind != note_kind_t::hold)
        fail(bms_write_diagnostic_code_t::unsupported_feature,
             "normalized BMS writer supports playable taps and holds only", std::string(path));
    if (note.kind == note_kind_t::tap && note.end)
        fail(bms_write_diagnostic_code_t::unsupported_feature,
             "normalized BMS writer supports playable taps and holds only", std::string(path));
    if (note.branch)
        fail(bms_write_diagnostic_code_t::unsupported_feature,
             "branch-local notes require an explicit branch export policy", std::string(path));
    const auto& channels = note.kind == note_kind_t::hold ? context.long_note_channels
                                                          : context.lane_channels;
    const auto channel = channels.find(note.lane.value);
    if (channel == channels.end())
        fail(bms_write_diagnostic_code_t::missing_profile,
             note.kind == note_kind_t::hold
                 ? "hold lane has no authored long-note BMS channel in the selected lane map"
                 : "note lane has no visible BMS channel in the selected lane map",
             std::string(path));
    return channel->second;
}

void append_root_hold_tail(const note_t& note, note_export_context_t& context,
                           std::string_view channel,
                           std::string_view object_id, std::uint64_t absolute_start,
                           std::string_view path) {
    if (!note.end)
        fail(bms_write_diagnostic_code_t::invalid_document,
             "hold note has no tail position", std::string(path) + ".end");
    const auto [numerator, denominator] = fraction_at(context.index, *note.end,
                                                       std::string(path) + ".end");
    const auto end_measure = std::ranges::find(context.chart.measures, note.end->measure,
                                                &measure_chunk_t::id);
    if (end_measure == context.chart.measures.end())
        fail(bms_write_diagnostic_code_t::invalid_geometry,
             "hold tail is outside the chart's measure geometry", std::string(path) + ".end");
    const auto end_measure_index = static_cast<std::size_t>(
        std::distance(context.chart.measures.begin(), end_measure));
    append_note_slot(context, end_measure_index, channel, numerator, denominator,
                     std::string(object_id), std::string(path) + ".end");
    const auto absolute_end = absolute_row(context.index, *note.end);
    if (!absolute_end)
        fail(bms_write_diagnostic_code_t::invalid_geometry,
             "hold tail is outside the chart's measure geometry", std::string(path) + ".end");
    context.hold_endpoints[note.lane.value].push_back(
        {absolute_start, note.id.value, false, std::string(path)});
    context.hold_endpoints[note.lane.value].push_back(
        {*absolute_end, note.id.value, true, std::string(path) + ".end"});
}

void export_root_note(const note_t& note, note_export_context_t& context,
                      std::size_t measure, std::string_view path) {
    const auto channel = root_note_channel(note, context, path);
    if (!note.sound)
        fail(bms_write_diagnostic_code_t::unresolved_resource,
             "BMS playable taps require a WAV resource binding", std::string(path) + ".sound");
    if (context.branch_resource_ids.contains(note.sound->value))
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "a root note refers to a resource declared only inside a branch",
             std::string(path) + ".sound");
    const auto wav = context.audio_ids.find(note.sound->value);
    if (wav == context.audio_ids.end())
        fail(bms_write_diagnostic_code_t::unresolved_resource,
             "tap refers to a resource that is not exported as WAV", std::string(path) + ".sound");
    const auto [numerator, denominator] = fraction_at(context.index, note.position,
                                                       std::string(path) + ".position");
    append_note_slot(context, measure, channel, numerator, denominator, wav->second,
                     std::string(path));
    const auto absolute_start = absolute_row(context.index, note.position);
    if (!absolute_start)
        fail(bms_write_diagnostic_code_t::invalid_geometry,
             "hold endpoint is outside the chart's measure geometry", std::string(path));
    if (note.kind == note_kind_t::hold)
        append_root_hold_tail(note, context, channel, wav->second, *absolute_start, path);
}

void export_branch_hold_note(const note_t& note, note_export_context_t& context,
                             const long_note_source_field_t& source_field,
                             std::string_view path) {
    const auto start = context.tree.branch_channel_lines.find(note.provenance.source_order);
    const auto end = context.tree.branch_channel_lines.find(source_field.end_source_order);
    if (start == context.tree.branch_channel_lines.end() ||
        end == context.tree.branch_channel_lines.end() ||
        note.provenance.source_spelling != start->second.text + "\n" + end->second.text ||
        note.kind != note_kind_t::hold || !note.end)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch hold does not identify its retained start and end channel lines",
             std::string(path));
    const channel_slot_field_t start_field{source_field.start_channel, source_field.start_slot,
                                           source_field.start_object_id};
    const channel_slot_field_t end_field{source_field.end_channel, source_field.end_slot,
                                         source_field.end_object_id};
    auto start_provenance = note.provenance;
    start_provenance.source_spelling = start->second.text;
    (void)claim_retained_channel_slot(context.branch_edits, start_provenance, start_field, path);
    auto end_provenance = note.provenance;
    end_provenance.source_order = source_field.end_source_order;
    end_provenance.source_spelling = end->second.text;
    (void)claim_retained_channel_slot(context.branch_edits, end_provenance, end_field,
                                      std::string(path) + ".end");
    const auto start_position = expected_retained_branch_slot_position(
        context.branch_edits, start->second, source_field.start_slot);
    const auto end_position = expected_retained_branch_slot_position(
        context.branch_edits, end->second, source_field.end_slot);
    if (!start_position || !end_position)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch hold endpoints do not map to valid retained source slots", std::string(path));
    const auto lane = std::ranges::find(context.layout.lanes, note.lane, &lane_definition_t::id);
    if (lane == context.layout.lanes.end() || !lane->bms_channels.long_note ||
        ascii_upper(*lane->bms_channels.long_note) != source_field.start_channel ||
        source_field.end_channel != source_field.start_channel)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch hold lane no longer matches its retained BMS channel", std::string(path));
    auto object_id = source_field.start_object_id;
    if (note.sound) object_id = exported_audio_id(context, *note.sound,
                                                  std::string(path) + ".sound");
    const auto target_start = *start_position == note.position
                                  ? source_field.start_slot
                                  : map_position_to_branch_slot(context.branch_edits, start->second,
                                                                note.position,
                                                                std::string(path) + ".position");
    const auto target_end = *end_position == *note.end
                                ? source_field.end_slot
                                : map_position_to_branch_slot(context.branch_edits, end->second,
                                                              *note.end,
                                                              std::string(path) + ".end");
    if (target_start != source_field.start_slot || object_id != source_field.start_object_id)
        apply_branch_channel_slot_edit(context.branch_edits, note.provenance.source_order,
                                       start->second, source_field.start_slot, target_start,
                                       object_id, path);
    if (target_end != source_field.end_slot)
        apply_branch_channel_slot_edit(context.branch_edits, source_field.end_source_order,
                                       end->second, source_field.end_slot, target_end,
                                       source_field.end_object_id, std::string(path) + ".end");
}

void export_branch_plain_note(const note_t& note, note_export_context_t& context,
                              std::string_view path) {
    const auto field = parse_channel_slot_field(note.provenance.source_field);
    if (!field)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch note does not identify one retained channel slot", std::string(path));
    const auto* source = claim_retained_channel_slot(context.branch_edits, note.provenance,
                                                      *field, path);
    const auto original_position = expected_retained_branch_slot_position(
        context.branch_edits, *source, field->slot);
    if (!original_position)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch note slot does not map to a valid retained source position",
             std::string(path));
    const auto lane = std::ranges::find(context.layout.lanes, note.lane, &lane_definition_t::id);
    std::optional<std::string> expected_channel;
    if (lane != context.layout.lanes.end()) {
        if (note.kind == note_kind_t::tap) expected_channel = lane->bms_channels.visible;
        if (note.kind == note_kind_t::invisible) expected_channel = lane->bms_channels.invisible;
        if (note.kind == note_kind_t::mine) expected_channel = lane->bms_channels.mine;
    }
    if (!expected_channel || ascii_upper(*expected_channel) != field->channel || note.end)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch note lane or kind no longer matches its retained BMS channel",
             std::string(path));
    auto object_id = field->object_id;
    if (note.kind != note_kind_t::mine && note.sound)
        object_id = exported_audio_id(context, *note.sound, std::string(path) + ".sound");
    else if (note.kind != note_kind_t::mine) {
        const auto source_object_resolves = std::ranges::any_of(
            context.song.resources, [&](const auto& resource) {
                return bms_identity(resource, "WAV") ==
                       std::optional<std::string>{field->object_id};
            });
        if (source_object_resolves || note.position != *original_position)
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "an unbound branch note cannot be safely edited from its raw source slot",
                 std::string(path));
    }
    const auto target = note.position == *original_position
                            ? field->slot
                            : map_position_to_branch_slot(context.branch_edits, *source,
                                                          note.position,
                                                          std::string(path) + ".position");
    if (target != field->slot || object_id != field->object_id)
        apply_branch_channel_slot_edit(context.branch_edits, note.provenance.source_order,
                                       *source, field->slot, target, object_id, path);
}

void export_branch_note(const note_t& note, note_export_context_t& context,
                        std::string_view path) {
    const auto hold = parse_long_note_source_field(note.provenance.source_field);
    if (hold) export_branch_hold_note(note, context, *hold, path);
    else export_branch_plain_note(note, context, path);
}

struct bgm_export_context_t {
    const chart_document_t& chart;
    const branch_tree_analysis_t& tree;
    const measure_index_t& index;
    branch_event_edit_state_t& branch_edits;
    const std::map<std::uint64_t, std::string>& audio_ids;
    const std::set<std::uint64_t>& branch_resource_ids;
    std::map<std::pair<std::size_t, std::uint64_t>, channel_line_t>& lines;
    const bms_write_options_t& options;
};

auto exported_bgm_audio_id(const bgm_export_context_t& context, resource_id_t resource,
                           std::string_view path) -> std::string;

auto encode_bms_channel_line(const channel_line_t& line,
                             const bms_write_options_t& options) -> std::string;

void clear_converted_branch_slot(const source_provenance_t& provenance,
                                 const channel_slot_field_t& field,
                                 bgm_export_context_t& context, std::string_view path) {
    const auto* source = claim_retained_channel_slot(context.branch_edits, provenance, field, path);
    apply_branch_channel_slot_edit(context.branch_edits, provenance.source_order, *source,
                                   field.slot, field.slot, "00", path);
}

void export_converted_branch_bgm(const bgm_event_t& event, bgm_export_context_t& context,
                                  std::string_view path) {
    if (const auto hold = parse_long_note_source_field(event.provenance.source_field)) {
        const auto separator = event.provenance.source_spelling.find('\n');
        if (!event.end || separator == std::string::npos)
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "converted branch hold must retain both source endpoints", std::string(path));
        auto start = event.provenance;
        start.source_spelling = event.provenance.source_spelling.substr(0, separator);
        clear_converted_branch_slot(start,
            {hold->start_channel, hold->start_slot, hold->start_object_id}, context, path);
        auto end = event.provenance;
        end.source_order = hold->end_source_order;
        end.source_spelling = event.provenance.source_spelling.substr(separator + 1);
        clear_converted_branch_slot(end,
            {hold->end_channel, hold->end_slot, hold->end_object_id}, context,
            std::string(path) + ".end");
    } else {
        const auto field = parse_channel_slot_field(event.provenance.source_field);
        if (!field || field->channel == "01" || field->channel.size() != 2 ||
            (field->channel.front() != '1' && field->channel.front() != '2'))
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "converted branch BGM has no retained playable source", std::string(path));
        clear_converted_branch_slot(event.provenance, *field, context, path);
    }
    const auto measure = std::ranges::find(context.index.entries, event.position.measure,
                                           &measure_index_entry_t::id);
    if (measure == context.index.entries.end())
        fail(bms_write_diagnostic_code_t::invalid_geometry,
             "converted branch BGM has an unknown measure", std::string(path));
    const auto [numerator, denominator] = fraction_at(context.index, event.position, std::string(path));
    const auto id = event.sound ? exported_bgm_audio_id(context, *event.sound, path) : "00";
    channel_line_t line{.measure = static_cast<std::size_t>(measure - context.index.entries.begin()),
                        .channel = "01", .notes = {}};
    line.notes.push_back({numerator, denominator, id, std::string(path)});
    context.branch_edits.converted_bgm_lines[event.provenance.source_order].push_back(
        encode_bms_channel_line(line, context.options));
}

void export_branch_bgm_event(const bgm_event_t& event, bgm_export_context_t& context,
                             std::string_view path) {
    const auto field = parse_channel_slot_field(event.provenance.source_field);
    if (!field || field->channel != "01") {
        export_converted_branch_bgm(event, context, path);
        return;
    }
    if (event.structural_slot != field->slot)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch BGM event does not identify its retained source slot", std::string(path));
    const auto* source = claim_retained_channel_slot(context.branch_edits, event.provenance,
                                                      *field, path);
    const auto lane = std::ranges::find(context.chart.bgm_source_lanes, event.source_lane,
                                         &bgm_source_lane_t::id);
    // BGM lanes are per-measure columns shared by many source lines, so the
    // retained line is identified by the claimed slot, not by the lane.
    if (lane == context.chart.bgm_source_lanes.end() || event.branch)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch BGM event source lane no longer matches its retained source line",
             std::string(path));
    const auto original = expected_retained_branch_slot_position(context.branch_edits, *source,
                                                                  field->slot);
    if (!original)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch BGM slot does not map to a valid retained source position", std::string(path));
    auto object_id = event.sound ? exported_bgm_audio_id(context, *event.sound,
                                                         std::string(path) + ".sound")
                                 : std::string("00");
    const auto target = event.position == *original
                            ? field->slot
                            : map_position_to_branch_slot(context.branch_edits, *source,
                                                          event.position,
                                                          std::string(path) + ".position");
    if (target != field->slot || object_id != field->object_id)
        apply_branch_channel_slot_edit(context.branch_edits, event.provenance.source_order,
                                       *source, field->slot, target, object_id, path);
}

auto exported_bgm_audio_id(const bgm_export_context_t& context, resource_id_t resource,
                           std::string_view path) -> std::string {
    const auto wav = context.audio_ids.find(resource.value);
    if (wav == context.audio_ids.end())
        fail(bms_write_diagnostic_code_t::unresolved_resource,
             "BGM event refers to a resource that is not exported as WAV", std::string(path));
    return wav->second;
}

// Slot count of the `#mmmCC:payload` line an imported event came from.
auto source_line_slot_count(std::string_view spelling) -> std::optional<std::uint64_t> {
    const auto colon = spelling.find(':');
    if (colon == std::string_view::npos) return std::nullopt;
    auto payload = spelling.substr(colon + 1);
    const auto end = payload.find_first_of(" \t\r\n");
    if (end != std::string_view::npos) payload = payload.substr(0, end);
    if (payload.empty() || payload.size() % 2 != 0) return std::nullopt;
    return payload.size() / 2;
}

void export_root_bgm_event(const bgm_event_t& event, std::size_t measure,
                           bgm_export_context_t& context, std::string_view path) {
    if (event.branch)
        fail(bms_write_diagnostic_code_t::unsupported_feature,
             "branch-local BGM events require an explicit branch export policy", std::string(path));
    const auto source_lane = std::ranges::find(context.chart.bgm_source_lanes, event.source_lane,
                                                &bgm_source_lane_t::id);
    if (source_lane == context.chart.bgm_source_lanes.end())
        fail(bms_write_diagnostic_code_t::invalid_document,
             "BGM event refers to an unknown source lane", std::string(path) + ".source_lane");
    std::string object_id = "00";
    if (event.sound) {
        if (context.branch_resource_ids.contains(event.sound->value))
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "a root BGM event refers to a resource declared only inside a branch",
                 std::string(path) + ".sound");
        object_id = exported_bgm_audio_id(context, *event.sound, std::string(path) + ".sound");
    }
    const auto [numerator, denominator] = fraction_at(context.index, event.position,
                                                       std::string(path) + ".position");
    const auto key = std::pair{measure, event.source_lane.value};
    auto [line, inserted] = context.lines.try_emplace(
        key, channel_line_t{.measure = measure, .channel = "01"});
    (void)inserted;
    if (const auto slots = source_line_slot_count(event.provenance.source_spelling))
        line->second.minimum_slots =
            checked_lcm(line->second.minimum_slots, *slots).value_or(line->second.minimum_slots);
    line->second.notes.push_back({numerator, denominator, std::move(object_id), std::string(path)});
}

void export_bgm_events_for_measure(const chart_document_t& chart, std::size_t measure,
                                   bgm_export_context_t& context) {
    for (const auto& event : chart.measures[measure].bgm_events) {
        const auto path = "$.charts[].measures[" + std::to_string(measure) + "].bgm_events[]";
        if (is_branch_source(context.tree, event.provenance))
            export_branch_bgm_event(event, context, path);
        else
            export_root_bgm_event(event, measure, context, path);
    }
}

void export_chart_notes_and_bgm(const chart_document_t& chart,
                                const branch_tree_analysis_t& tree,
                                note_export_context_t& notes,
                                bgm_export_context_t& bgm) {
    for (std::size_t measure = 0; measure < chart.measures.size(); ++measure) {
        for (const auto& note : chart.measures[measure].notes) {
            const auto path = "$.charts[].measures[" + std::to_string(measure) + "].notes[]";
            if (is_branch_source(tree, note.provenance))
                export_branch_note(note, notes, path);
            else
                export_root_note(note, notes, measure, path);
        }
        export_bgm_events_for_measure(chart, measure, bgm);
    }
}

void validate_hold_endpoints(
    std::map<std::uint64_t, std::vector<hold_endpoint_t>>& hold_endpoints) {
    for (auto& [lane, endpoints] : hold_endpoints) {
        (void)lane;
        std::ranges::sort(endpoints, {}, &hold_endpoint_t::row);
        for (std::size_t index = 0; index < endpoints.size(); index += 2)
            if (index + 1 >= endpoints.size() || endpoints[index].is_end ||
                !endpoints[index + 1].is_end ||
                endpoints[index].note_id != endpoints[index + 1].note_id)
                fail(bms_write_diagnostic_code_t::unsupported_feature,
                     "overlapping holds on one lane cannot be paired safely by BMS long-note channels",
                     index < endpoints.size() ? endpoints[index].path
                                              : "$.charts[].measures[].notes[]");
    }
}

struct bga_export_context_t {
    const chart_document_t& chart;
    const branch_tree_analysis_t& tree;
    const measure_index_t& index;
    branch_event_edit_state_t& branch_edits;
    const std::map<std::uint64_t, std::string>& visual_ids;
    const std::set<std::uint64_t>& branch_resource_ids;
    std::map<std::pair<std::size_t, std::string>, channel_line_t>& lines;
    std::map<std::pair<std::size_t, std::string>, std::size_t>& line_orders;
    std::map<std::pair<std::size_t, std::uint64_t>, std::size_t> layer_counts;
    std::map<std::pair<std::size_t, std::uint64_t>, std::size_t> layer2_counts;
};

void export_branch_bga_event(const bga_event_t& event, bga_export_context_t& context) {
    const auto field = parse_channel_slot_field(event.provenance.source_field);
    if (!field || (field->channel != "04" && field->channel != "06" &&
                   field->channel != "07" && field->channel != "0A"))
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch BGA event does not identify one retained visual channel slot",
             "$.charts[].bga_events[]");
    const auto* source = claim_retained_channel_slot(context.branch_edits, event.provenance,
                                                      *field, "$.charts[].bga_events[]");
    const bool kind_matches =
        (field->channel == "04" && event.kind == visual_event_kind_t::background) ||
        (field->channel == "06" && event.kind == visual_event_kind_t::poor) ||
        (field->channel == "07" && event.kind == visual_event_kind_t::layer) ||
        (field->channel == "0A" && event.kind == visual_event_kind_t::layer2);
    if (!kind_matches)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch BGA kind no longer matches its retained BMS channel",
             "$.charts[].bga_events[]");
    const auto original = expected_retained_branch_slot_position(context.branch_edits, *source,
                                                                  field->slot);
    if (!original)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "branch BGA slot does not map to a valid retained source position",
             "$.charts[].bga_events[]");
    const auto bmp = context.visual_ids.find(event.resource.value);
    if (bmp == context.visual_ids.end())
        fail(bms_write_diagnostic_code_t::unresolved_resource,
             "branch BGA event refers to a resource that is not exported as BMP",
             "$.charts[].bga_events[].resource");
    const auto target = event.position == *original
                            ? field->slot
                            : map_position_to_branch_slot(context.branch_edits, *source,
                                                          event.position,
                                                          "$.charts[].bga_events[].position");
    if (target != field->slot || bmp->second != field->object_id)
        apply_branch_channel_slot_edit(context.branch_edits, event.provenance.source_order,
                                       *source, field->slot, target, bmp->second,
                                       "$.charts[].bga_events[]");
}

auto root_bga_channel(const bga_event_t& event, bga_export_context_t& context,
                      std::string_view path) -> std::string {
    switch (event.kind) {
    case visual_event_kind_t::background: return "04";
    case visual_event_kind_t::poor: return "06";
    case visual_event_kind_t::layer: {
        const auto absolute = absolute_row(context.index, event.position);
        if (!absolute)
            fail(bms_write_diagnostic_code_t::invalid_geometry,
                 "BGA event is outside the chart's measure geometry", std::string(path) + ".position");
        auto& count = context.layer_counts[{event.position.measure.value, *absolute}];
        const auto key = std::pair{static_cast<std::size_t>(event.position.measure.value), *absolute};
        const auto limit = context.layer2_counts.contains(key) ? 1U : 2U;
        if (count >= limit)
            fail(bms_write_diagnostic_code_t::duplicate_event,
                 "more than two layer events share a position; BMS channels 07 and 0A cannot "
                 "represent the collision",
                 std::string(path));
        return count++ == 0 ? "07" : "0A";
    }
    case visual_event_kind_t::layer2:
        return "0A";
    case visual_event_kind_t::video:
    case visual_event_kind_t::storyboard:
        fail(bms_write_diagnostic_code_t::unsupported_feature,
             "BMS BGA channels cannot represent video or storyboard events", std::string(path));
    }
    return {};
}

void reserve_explicit_layer2_slots(const chart_document_t& chart,
                                   bga_export_context_t& context) {
    std::map<std::pair<std::size_t, std::uint64_t>, std::size_t> legacy_counts;
    for (const auto& event : chart.bga_events) {
        if (is_branch_source(context.tree, event.provenance) ||
            (event.kind != visual_event_kind_t::layer && event.kind != visual_event_kind_t::layer2))
            continue;
        const auto absolute = absolute_row(context.index, event.position);
        if (!absolute)
            fail(bms_write_diagnostic_code_t::invalid_geometry,
                 "BGA event is outside the chart's measure geometry",
                 "$.charts[].bga_events[].position");
        const auto key = std::pair{static_cast<std::size_t>(event.position.measure.value), *absolute};
        if (event.kind == visual_event_kind_t::layer2) {
            if (++context.layer2_counts[key] > 1)
                fail(bms_write_diagnostic_code_t::duplicate_event,
                     "more than one Layer 2 event shares a position; BMS channel 0A cannot "
                     "represent the collision",
                     "$.charts[].bga_events[]");
        } else {
            ++legacy_counts[key];
        }
    }
    for (const auto& [key, count] : legacy_counts) {
        if (count > 1 && context.layer2_counts.contains(key))
            fail(bms_write_diagnostic_code_t::duplicate_event,
                 "two layer events collide with an explicit Layer 2 event at one position",
                 "$.charts[].bga_events[]");
    }
}

void export_root_bga_event(const bga_event_t& event, bga_export_context_t& context) {
    const std::string path = "$.charts[].bga_events[]";
    const auto channel = root_bga_channel(event, context, path);
    const auto bmp = context.visual_ids.find(event.resource.value);
    if (context.branch_resource_ids.contains(event.resource.value))
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "a root BGA event refers to a resource declared only inside a branch", path + ".resource");
    if (bmp == context.visual_ids.end())
        fail(bms_write_diagnostic_code_t::unresolved_resource,
             "BGA event refers to a resource that is not exported as BMP", path + ".resource");
    const auto [numerator, denominator] = fraction_at(context.index, event.position,
                                                       path + ".position");
    const auto measure = std::ranges::find(context.chart.measures, event.position.measure,
                                            &measure_chunk_t::id);
    if (measure == context.chart.measures.end())
        fail(bms_write_diagnostic_code_t::invalid_geometry,
             "BGA event is outside the chart's measure geometry", path + ".position");
    const auto measure_index = static_cast<std::size_t>(
        std::distance(context.chart.measures.begin(), measure));
    const auto key = std::pair{measure_index, channel};
    auto [line, inserted] = context.lines.try_emplace(
        key, channel_line_t{.measure = measure_index, .channel = channel});
    (void)inserted;
    line->second.notes.push_back({numerator, denominator, bmp->second, path});
    auto [order, order_inserted] = context.line_orders.try_emplace(key, event.provenance.source_order);
    if (!order_inserted) order->second = std::min(order->second, event.provenance.source_order);
}

void export_bga_events(const chart_document_t& chart, bga_export_context_t& context) {
    reserve_explicit_layer2_slots(chart, context);
    auto ordered = chart.bga_events;
    std::ranges::stable_sort(ordered, [](const auto& left, const auto& right) {
        return std::tie(left.position.measure.value, left.position.row,
                        left.provenance.source_order, left.id.value) <
               std::tie(right.position.measure.value, right.position.row,
                        right.provenance.source_order, right.id.value);
    });
    for (const auto& event : ordered) {
        if (is_branch_source(context.tree, event.provenance))
            export_branch_bga_event(event, context);
        else
            export_root_bga_event(event, context);
    }
}

struct effective_metadata_source_t {
    std::size_t source_order{};
    bool branch_scoped{};
};

struct metadata_source_state_t {
    bool has_branch_player{};
    std::optional<effective_metadata_source_t> title;
    std::optional<effective_metadata_source_t> artist;
    std::optional<effective_metadata_source_t> offset;
    std::map<std::size_t, std::string> branch_edits;
    std::set<std::size_t> source_orders;
};

auto collect_bms_metadata_groups(const song_document_t& song, const chart_document_t& chart)
    -> std::vector<const format_metadata_t*> {
    std::vector<const format_metadata_t*> groups;
    const auto append = [&](const auto& source_groups) {
        for (const auto& group : source_groups)
            if (group.family == format_family_t::bms && !group.ordered_fields.empty())
                groups.push_back(&group);
    };
    append(song.metadata.format_groups);
    append(chart.metadata.format_groups);
    return groups;
}

auto bms_player_value(const lane_layout_t& layout) -> std::string {
    if (layout.profile_id.starts_with("pms-")) return "3";
    const auto has_right_stage = std::ranges::any_of(
        layout.lanes, [](const auto& lane) { return lane.stage == stage_group_t::right; });
    return layout.split_stage || has_right_stage ? "2" : "1";
}

auto derive_playlevel_from_rating(const chart_document_t& chart,
                                  std::vector<bms_write_diagnostic_t>& diagnostics)
    -> std::optional<std::uint32_t> {
    if (!chart.metadata.rating) return std::nullopt;
    const auto rating = *chart.metadata.rating;
    if (rating.denominator != 0 && rating.numerator >= 0 &&
        static_cast<std::uint64_t>(rating.numerator) % rating.denominator == 0 &&
        static_cast<std::uint64_t>(rating.numerator) / rating.denominator <= UINT32_MAX)
        return static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(rating.numerator) / rating.denominator);
    warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
         "chart rating cannot be represented as an integral BMS PLAYLEVEL",
         "$.charts[].metadata.rating");
    return std::nullopt;
}

auto metadata_field_is_branch(const branch_tree_analysis_t& tree,
                              const format_metadata_t& group, std::size_t field_index) -> bool {
    if (group.family != format_family_t::bms || field_index >= group.ordered_field_source_orders.size())
        return false;
    return tree.branch_source_orders.contains(group.ordered_field_source_orders[field_index]);
}

auto retained_playlevel_matches(const std::vector<const format_metadata_t*>& groups,
                                std::optional<std::uint32_t> projected,
                                bool& found) -> bool {
    bool matches = true;
    for (const auto* group : groups) {
        for (const auto& [token, value] : group->ordered_fields) {
            if (ascii_upper(token) != "PLAYLEVEL") continue;
            found = true;
            if (!projected) continue;
            const auto trimmed = trim_ascii_whitespace(value);
            std::uint32_t parsed{};
            const auto [end, error] =
                std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), parsed);
            if (error != std::errc{} || end != trimmed.data() + trimmed.size() ||
                parsed != *projected)
                matches = false;
        }
    }
    return matches;
}

void record_metadata_source_field(const format_metadata_t& group, std::size_t field_index,
                                  const branch_tree_analysis_t& tree,
                                  const branch_tree_analysis_t& retained_tree,
                                  metadata_source_state_t& state,
                                  std::optional<std::size_t>& previous) {
    const auto& [token, value] = group.ordered_fields[field_index];
    if (!safe_metadata_token(token))
        fail(bms_write_diagnostic_code_t::invalid_document,
             "retained BMS metadata contains an unsafe command token",
             "$.metadata.format_groups[].ordered_fields[]");
    if (field_index >= group.ordered_field_source_orders.size()) return;
    const auto order = group.ordered_field_source_orders[field_index];
    const auto source = retained_tree.metadata_fields.find(order);
    if ((previous && order <= *previous) || !state.source_orders.insert(order).second ||
        source == retained_tree.metadata_fields.end() || ascii_upper(token) != source->second.first)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "retained BMS metadata source order or command token does not identify one unique source statement",
             "$.metadata.format_groups[].ordered_fields[]");
    previous = order;
    const bool branch = metadata_field_is_branch(tree, group, field_index);
    const auto normalized = ascii_upper(token);
    if (branch) {
        const auto branch_field = tree.branch_metadata_fields.find(order);
        if (branch_field == tree.branch_metadata_fields.end() ||
            normalized != branch_field->second.first)
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "branch metadata source identity does not match its retained source statement",
                 "$.metadata.format_groups[].ordered_fields[]");
        if (has_line_break_or_nul(value) || !valid_utf8(value))
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch metadata must be valid UTF-8 single-line text",
                 "$.metadata.format_groups[].ordered_fields[]");
        if (value != branch_field->second.second) state.branch_edits.insert_or_assign(order, value);
    }
    auto retain_last = [&](auto& target) {
        if (!target || order >= target->source_order) target = effective_metadata_source_t{order, branch};
    };
    if (normalized == "PLAYER") state.has_branch_player = state.has_branch_player || branch;
    else if (normalized == "TITLE") retain_last(state.title);
    else if (normalized == "ARTIST") retain_last(state.artist);
    else if (normalized == "OFFSET") retain_last(state.offset);
}

auto collect_metadata_source_state(const std::vector<const format_metadata_t*>& groups,
                                   const branch_tree_analysis_t& tree,
                                   const branch_tree_analysis_t& retained_tree)
    -> metadata_source_state_t {
    metadata_source_state_t state;
    for (const auto* group : groups) {
        std::optional<std::size_t> previous;
        for (std::size_t index = 0; index < group->ordered_fields.size(); ++index)
            record_metadata_source_field(*group, index, tree, retained_tree, state, previous);
    }
    return state;
}

auto route_projected_branch_metadata(
    std::string_view token, const std::optional<effective_metadata_source_t>& source,
    std::string_view projected_value, const branch_tree_analysis_t& tree,
    std::map<std::size_t, std::string>& edits) -> bool {
    if (!source || !source->branch_scoped) return false;
    const auto retained = tree.branch_metadata_fields.find(source->source_order);
    if (retained == tree.branch_metadata_fields.end() || retained->second.first != token)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "projected branch metadata has no unique retained source statement", "$.metadata");
    if (projected_value == trim_ascii_whitespace(retained->second.second)) return true;
    const auto edit = edits.find(source->source_order);
    if (edit != edits.end() && trim_ascii_whitespace(edit->second) != projected_value)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "normalized and source-field edits disagree for the same branch metadata statement",
             "$.metadata");
    if (edit == edits.end()) edits.emplace(source->source_order, std::string(projected_value));
    return true;
}

struct metadata_output_context_t {
    const chart_document_t& chart;
    const branch_tree_analysis_t& tree;
    const std::vector<const format_metadata_t*>& groups;
    std::vector<bms_write_diagnostic_t>& diagnostics;
    std::string_view player;
    std::string_view title;
    std::string_view artist;
    std::string_view offset;
    std::optional<std::uint32_t> playlevel;
    bool has_ordered_playlevel{};
    bool has_branch_player{};
    bool title_is_branch{};
    bool artist_is_branch{};
    bool offset_is_branch{};
    std::optional<effective_metadata_source_t> last_offset;
    std::vector<std::string> output;
    bool player_emitted{};
    bool title_emitted{};
    bool artist_emitted{};
    bool offset_emitted{};
};

auto has_retained_root_player(const metadata_output_context_t& context) -> bool {
    for (const auto* group : context.groups) {
        for (std::size_t index = 0; index < group->ordered_fields.size(); ++index) {
            if (!metadata_field_is_branch(context.tree, *group, index) &&
                ascii_upper(group->ordered_fields[index].first) == "PLAYER")
                return true;
        }
    }
    return false;
}

auto metadata_field_output(metadata_output_context_t& context, std::size_t group_index,
                           std::size_t field_index) -> std::optional<std::string> {
    const auto& fields = context.groups[group_index]->ordered_fields;
    if (metadata_field_is_branch(context.tree, *context.groups[group_index], field_index))
        return std::nullopt;
    const auto& [token, retained_value] = fields[field_index];
    const auto upper_token = ascii_upper(token);
    const auto path = "$.metadata.format_groups[" + std::to_string(group_index) +
                      "].ordered_fields[" + std::to_string(field_index) + "]";
    if (!safe_metadata_token(token) ||
        (upper_token != "PLAYER" && generated_or_channel_token(token)))
        fail(bms_write_diagnostic_code_t::invalid_document,
             "retained BMS metadata contains an unsafe or writer-owned command token", path);
    std::string value = retained_value;
    if (upper_token == "PLAYER") {
        if (context.player_emitted) {
            warn(context.diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                 "duplicate retained PLAYER metadata was collapsed to one canonical value", path);
            return std::nullopt;
        }
        value = " " + std::string(context.player);
        context.player_emitted = true;
    } else if (upper_token == "TITLE") {
        if (context.title_emitted) {
            warn(context.diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                 "duplicate retained TITLE metadata was collapsed to its effective value", path);
            return std::nullopt;
        }
        if (!context.title_is_branch) value = context.title;
        context.title_emitted = true;
    } else if (upper_token == "ARTIST") {
        if (context.artist_emitted) {
            warn(context.diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                 "duplicate retained ARTIST metadata was collapsed to its effective value", path);
            return std::nullopt;
        }
        if (!context.artist_is_branch) value = context.artist;
        context.artist_emitted = true;
    } else if (upper_token == "OFFSET") {
        if (!context.offset_is_branch) value = context.offset;
        context.offset_emitted = true;
    }
    if (has_line_break_or_nul(value) || !valid_utf8(value))
        fail(bms_write_diagnostic_code_t::invalid_document,
             "retained BMS metadata values must be valid UTF-8 single-line text", path);
    std::string line = "#" + token;
    if (value.empty() || (value.front() != ' ' && value.front() != '\t')) line.push_back(' ');
    line += value;
    return line;
}

auto emit_metadata_header(metadata_output_context_t context) -> std::vector<std::string> {
    if (!has_retained_root_player(context) && !context.has_branch_player)
        context.output.emplace_back("#PLAYER " + std::string(context.player));
    std::size_t next_unmodeled = 0;
    const auto& unmodeled = context.tree.root_unmodeled_lines;
    // Copies comments and unknown headers that precede `before` in the source.
    const auto flush_unmodeled = [&](std::size_t before) {
        while (next_unmodeled < unmodeled.size() && unmodeled[next_unmodeled].first < before) {
            if (can_emit_unmodeled_root_line(unmodeled[next_unmodeled].second))
                context.output.push_back(unmodeled[next_unmodeled].second);
            ++next_unmodeled;
        }
    };
    for (std::size_t group = 0; group < context.groups.size(); ++group) {
        const auto& orders = context.groups[group]->ordered_field_source_orders;
        for (std::size_t field = 0; field < context.groups[group]->ordered_fields.size(); ++field) {
            if (field < orders.size()) flush_unmodeled(orders[field]);
            if (auto line = metadata_field_output(context, group, field))
                context.output.push_back(std::move(*line));
        }
    }
    flush_unmodeled(std::numeric_limits<std::size_t>::max());
    if (!context.title_emitted && !context.title.empty() && !context.title_is_branch)
        context.output.emplace_back("#TITLE " + std::string(context.title));
    if (!context.artist_emitted && !context.artist.empty() && !context.artist_is_branch)
        context.output.emplace_back("#ARTIST " + std::string(context.artist));
    if (!context.offset_emitted && !context.last_offset &&
        context.chart.beat_zero_audio_offset_seconds.numerator != 0)
        context.output.emplace_back("#OFFSET " + std::string(context.offset));
    if (!context.has_ordered_playlevel && context.playlevel)
        context.output.emplace_back("#PLAYLEVEL " + std::to_string(*context.playlevel));
    return context.output;
}

auto encode_bms_channel_line(const channel_line_t& line,
                             const bms_write_options_t& options) -> std::string {
    std::uint64_t slot_count = std::max<std::uint64_t>(line.minimum_slots, 1);
    for (const auto& note : line.notes) {
        const auto slots = checked_lcm(slot_count, note.denominator);
        if (!slots || *slots > options.max_slots_per_line || *slots > 500'000)
            fail(bms_write_diagnostic_code_t::resource_limit,
                 "exact note positions require too many BMS subdivision slots",
                 "$.charts[].measures[].notes");
        slot_count = *slots;
    }
    if (slot_count == 0 || slot_count > options.max_slots_per_line || slot_count > 500'000)
        fail(bms_write_diagnostic_code_t::resource_limit,
             "BMS channel line exceeds the configured subdivision limit",
             "$.charts[].measures[].notes");
    std::vector<std::string> cells(static_cast<std::size_t>(slot_count), "00");
    std::vector<bool> occupied(static_cast<std::size_t>(slot_count), false);
    for (const auto& note : line.notes) {
        const auto multiplier = slot_count / note.denominator;
        if (note.numerator > std::numeric_limits<std::uint64_t>::max() / multiplier)
            fail(bms_write_diagnostic_code_t::unrepresentable_number,
                 "note subdivision index overflows the BMS channel grid", note.path);
        const auto slot = note.numerator * multiplier;
        if (slot >= slot_count || occupied[static_cast<std::size_t>(slot)])
            fail(bms_write_diagnostic_code_t::duplicate_event,
                 "two events occupy the same BMS channel slot", note.path);
        occupied[static_cast<std::size_t>(slot)] = true;
        cells[static_cast<std::size_t>(slot)] = note.object_id;
    }
    std::string payload;
    payload.reserve(cells.size() * 2);
    for (const auto& cell : cells) payload += cell;
    const auto prefix = line.measure < 10 ? std::string("00")
                         : line.measure < 100 ? std::string("0")
                                              : std::string();
    return "#" + prefix + std::to_string(line.measure) + line.channel + ":" + payload;
}

void append_bms_bpm_header(const chart_document_t& chart, bool flattening_active_projection,
                           const bpm_event_t* projected_initial_bpm,
                           const branch_tree_analysis_t& tree, const measure_index_t& index,
                           std::vector<std::string>& output) {
    if (flattening_active_projection) {
        if (projected_initial_bpm)
            output.emplace_back("#BPM " + exact_decimal(
                projected_initial_bpm->beats_per_minute, "$.charts[].bpms[].beats_per_minute"));
        return;
    }
    std::vector<const bpm_event_t*> root_bpms;
    for (const auto& bpm : chart.bpms)
        if (!is_branch_source(tree, bpm.provenance) &&
            !is_retained_timing_source(tree, bpm.provenance))
            root_bpms.push_back(&bpm);
    if (root_bpms.size() > 1 ||
        (!root_bpms.empty() &&
         absolute_row(index, root_bpms.front()->position) != std::optional<std::uint64_t>{0}))
        fail(bms_write_diagnostic_code_t::unsupported_feature,
             "this writer slice can export only one direct BPM at chart position zero",
             "$.charts[].bpms");
    if (!root_bpms.empty())
        output.emplace_back("#BPM " + exact_decimal(root_bpms.front()->beats_per_minute,
                                                      "$.charts[].bpms[0].bpm"));
}

void append_resource_declarations(const std::vector<resource_declaration_t>& declarations,
                                  const std::map<std::uint64_t, std::string>& audio_ids,
                                  const std::map<std::uint64_t, std::string>& visual_ids,
                                  std::vector<std::string>& output) {
    for (const auto& declaration : declarations) {
        if (declaration.branch_scoped) continue;
        const auto& ids = declaration.name_space == "WAV" ? audio_ids : visual_ids;
        const auto identity = ids.find(declaration.resource->id.value);
        if (identity != ids.end())
            output.emplace_back("#" + declaration.name_space + identity->second + " " +
                                declaration.resource->name);
    }
}

void append_measure_ratio_lines(const chart_document_t& chart,
                                const branch_tree_analysis_t& tree,
                                std::vector<bms_write_diagnostic_t>& diagnostics,
                                std::vector<std::string>& output) {
    bool warned_meter = false;
    for (std::size_t measure = 0; measure < chart.measures.size(); ++measure) {
        const auto& definition = chart.measures[measure].definition;
        if (tree.branch_measure_indices.contains(measure)) continue;
        if (definition.meter && !warned_meter) {
            warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
                 "BMS measure ratios preserve duration but not authored beat grouping",
                 "$.charts[].measures[].definition.meter");
            warned_meter = true;
        }
        if (definition.length != rational_t{1, 1}) {
            const auto prefix = measure < 10 ? std::string("00")
                               : measure < 100 ? std::string("0")
                                               : std::string();
            output.emplace_back("#" + prefix + std::to_string(measure) + "02:" +
                                exact_decimal(definition.length.numerator,
                                              definition.length.denominator,
                                              "$.charts[].measures[].definition.length"));
        }
    }
    for (const auto& line : tree.root_measure_ratio_lines) {
        if (!tree.branch_measure_indices.contains(line.measure_index)) continue;
        if (has_line_break_or_nul(line.text) || !valid_utf8(line.text))
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "retained BMS measure-ratio statements must be valid UTF-8 single lines",
                 "$.charts[].bms_control_tree");
        output.push_back(line.text);
    }
}

void append_ordered_channel_lines(
    const std::map<std::pair<std::size_t, std::string>, channel_line_t>& channel_lines,
    const std::map<std::pair<std::size_t, std::string>, std::size_t>& bga_line_order,
    const std::map<std::pair<std::size_t, std::uint64_t>, channel_line_t>& bgm_lines,
    const chart_document_t& chart, const bms_write_options_t& options,
    std::vector<std::string>& output) {
    std::vector<const channel_line_t*> ordered_channels;
    ordered_channels.reserve(channel_lines.size());
    for (const auto& [key, line] : channel_lines) {
        (void)key;
        ordered_channels.push_back(&line);
    }
    std::ranges::stable_sort(ordered_channels, [&](const auto* left, const auto* right) {
        const auto left_key = std::pair{left->measure, left->channel};
        const auto right_key = std::pair{right->measure, right->channel};
        const auto left_order = bga_line_order.contains(left_key) ? bga_line_order.at(left_key) : 0;
        const auto right_order = bga_line_order.contains(right_key) ? bga_line_order.at(right_key) : 0;
        return std::tie(left->measure, left_order, left->channel) <
               std::tie(right->measure, right_order, right->channel);
    });
    for (const auto* line : ordered_channels)
        output.push_back(encode_bms_channel_line(*line, options));

    std::vector<std::pair<std::pair<std::size_t, std::uint64_t>, const channel_line_t*>> ordered_bgm;
    ordered_bgm.reserve(bgm_lines.size());
    for (const auto& [key, line] : bgm_lines) ordered_bgm.emplace_back(key, &line);
    std::ranges::stable_sort(ordered_bgm, [&](const auto& left, const auto& right) {
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
    for (const auto& [key, line] : ordered_bgm) {
        (void)key;
        output.push_back(encode_bms_channel_line(*line, options));
    }
}

struct branch_tree_emission_context_t {
    const branch_tree_analysis_t& tree;
    std::map<std::size_t, branch_channel_edit_t>& channel_edits;
    const std::map<std::pair<std::size_t, std::size_t>, std::string>& slot_claims;
    const std::map<std::size_t, std::string>& metadata_edits;
    const std::map<std::size_t, std::string>& resource_edits;
    const std::map<std::size_t, std::string>& ratio_edits;
    const std::map<std::size_t, std::string>& bpm_edits;
    const std::map<std::size_t, std::string>& timing_edits;
    std::vector<std::string> output;
    std::set<std::size_t> emitted_metadata;
    std::set<std::size_t> emitted_resources;
    std::set<std::size_t> emitted_ratios;
    std::set<std::size_t> emitted_bpms;
    std::set<std::size_t> emitted_timing;
    std::set<std::size_t> emitted_channels;
};

void append_branch_channel_patch(std::size_t order, std::string_view line,
                                 branch_tree_emission_context_t& context,
                                 const branch_event_edit_state_t& branch_edits) {
    auto& patch = context.channel_edits.at(order);
    if (!patch.source || patch.text != line || patch.payload_begin > patch.text.size() ||
        patch.payload.size() > patch.text.size() - patch.payload_begin)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "edited branch channel line does not match its retained control-tree line",
             "$.charts[].bms_control_tree");
    auto payload = patch.payload;
    for (const auto slot : patch.cleared_slots) {
        if (slot >= payload.size() / 2)
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch channel clear operation is outside its retained line",
                 "$.charts[].bms_control_tree");
        payload.replace(slot * 2, 2, "00");
    }
    for (const auto& [slot, object_id] : patch.written_slots) {
        if (slot >= payload.size() / 2)
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch channel write operation is outside its retained line",
                 "$.charts[].bms_control_tree");
        const auto origin = patch.written_slot_origins.find(slot);
        const auto original = patch.payload.substr(slot * 2, 2);
        if (origin == patch.written_slot_origins.end() ||
            (slot != origin->second && !patch.cleared_slots.contains(slot) &&
             (original != "00" || branch_edits.slot_claims.contains({order, slot}))))
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch channel event would overwrite another retained source slot",
                 "$.charts[].bms_control_tree");
        payload.replace(slot * 2, 2, object_id);
    }
    patch.text.replace(patch.payload_begin, patch.payload.size(), payload);
    context.output.push_back(std::move(patch.text));
    if (!context.emitted_channels.insert(order).second)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "edited branch channel line appears more than once in the retained tree",
             "$.charts[].bms_control_tree");
}

auto edited_branch_resource_line(std::size_t order, std::string_view line,
                                 std::string_view replacement,
                                 branch_tree_emission_context_t& context) -> std::string {
    const auto source = context.tree.branch_resource_fields.find(order);
    if (source == context.tree.branch_resource_fields.end())
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "edited branch resource has no retained source identity", "$.charts[].bms_control_tree");
    const auto value = replace_retained_header_value(line, source->second.first,
                                                      source->second.second, replacement);
    if (!value || !context.emitted_resources.insert(order).second)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "edited branch resource does not identify one unambiguous retained source statement",
             "$.charts[].bms_control_tree");
    return *value;
}

auto edited_branch_statement(std::size_t order, std::string_view line,
                             branch_tree_emission_context_t& context) -> std::string {
    const auto metadata = context.metadata_edits.find(order);
    const auto resource = context.resource_edits.find(order);
    const auto ratio = context.ratio_edits.find(order);
    const auto bpm = context.bpm_edits.find(order);
    const auto timing = context.timing_edits.find(order);
    const auto count = static_cast<unsigned>(metadata != context.metadata_edits.end()) +
                       static_cast<unsigned>(resource != context.resource_edits.end()) +
                       static_cast<unsigned>(ratio != context.ratio_edits.end()) +
                       static_cast<unsigned>(bpm != context.bpm_edits.end()) +
                       static_cast<unsigned>(timing != context.timing_edits.end());
    if (count != 1)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one retained branch statement is ambiguously claimed by multiple edits",
             "$.charts[].bms_control_tree");
    if (metadata != context.metadata_edits.end()) {
        const auto source = context.tree.branch_metadata_fields.find(order);
        if (source == context.tree.branch_metadata_fields.end())
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch metadata has no retained source identity", "$.charts[].bms_control_tree");
        const auto replaced = replace_retained_header_value(line, source->second.first,
                                                             source->second.second, metadata->second);
        if (!replaced || !context.emitted_metadata.insert(order).second)
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch metadata does not identify one unambiguous retained source statement",
                 "$.charts[].bms_control_tree");
        return *replaced;
    }
    if (bpm != context.bpm_edits.end()) {
        const auto source = context.tree.branch_direct_bpm_lines.find(order);
        if (source == context.tree.branch_direct_bpm_lines.end())
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch BPM has no retained source identity", "$.charts[].bms_control_tree");
        const auto replaced = replace_retained_header_value(line, "BPM", source->second.raw_value,
                                                             bpm->second);
        if (!replaced || !context.emitted_bpms.insert(order).second)
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch BPM does not identify one unambiguous retained source statement",
                 "$.charts[].bms_control_tree");
        return *replaced;
    }
    if (timing != context.timing_edits.end()) {
        const auto source = context.tree.timing_declarations.find(order);
        if (source == context.tree.timing_declarations.end())
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited extended BPM or STOP value has no retained source declaration",
                 "$.charts[].bms_control_tree");
        const auto replaced = replace_retained_header_value(line, source->second.token,
            source->second.raw_value, timing->second);
        if (!replaced || !context.emitted_timing.insert(order).second)
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited timing declaration does not identify one unambiguous retained source statement",
                 "$.charts[].bms_control_tree");
        return *replaced;
    }
    if (ratio != context.ratio_edits.end()) {
        const auto source = context.tree.branch_measure_ratio_lines.find(order);
        if (source == context.tree.branch_measure_ratio_lines.end())
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch measure length has no retained source identity",
                 "$.charts[].bms_control_tree");
        const auto replaced = replace_retained_measure_length(line, source->second.measure_index,
                                                               source->second.raw_value,
                                                               ratio->second);
        if (!replaced || !context.emitted_ratios.insert(order).second)
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "edited branch measure length does not identify one unambiguous retained source statement",
                 "$.charts[].bms_control_tree");
        return *replaced;
    }
    return edited_branch_resource_line(order, line, resource->second, context);
}

auto emit_preserved_branch_lines(const branch_tree_analysis_t& tree,
                                 branch_tree_emission_context_t& context,
                                 const branch_event_edit_state_t& branch_edits)
    -> std::vector<std::string> {
    context.output.reserve(tree.preserved_lines.size());
    for (const auto& [order, line] : tree.preserved_lines) {
        if (has_line_break_or_nul(line) || !valid_utf8(line))
            fail(bms_write_diagnostic_code_t::branch_provenance,
                 "retained BMS control-tree statements must be valid UTF-8 single lines",
                 "$.charts[].bms_control_tree");
        if (context.channel_edits.contains(order)) {
            append_branch_channel_patch(order, line, context, branch_edits);
            if (const auto added = branch_edits.converted_bgm_lines.find(order);
                added != branch_edits.converted_bgm_lines.end())
                context.output.insert(context.output.end(), added->second.begin(), added->second.end());
            continue;
        }
        const auto has_edit = context.metadata_edits.contains(order) ||
                              context.resource_edits.contains(order) ||
                              context.ratio_edits.contains(order) ||
                              context.bpm_edits.contains(order) ||
                              context.timing_edits.contains(order);
        context.output.push_back(has_edit ? edited_branch_statement(order, line, context)
                                          : line);
    }
    return context.output;
}

void validate_branch_tree_edits_emitted(const branch_tree_emission_context_t& context,
                                        const branch_event_edit_state_t& branch_edits) {
    if (context.emitted_metadata.size() != context.metadata_edits.size())
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one or more edited branch metadata statements are absent from the retained tree",
             "$.charts[].bms_control_tree");
    if (context.emitted_resources.size() != context.resource_edits.size())
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one or more edited branch resource statements are absent from the retained tree",
             "$.charts[].bms_control_tree");
    if (context.emitted_ratios.size() != context.ratio_edits.size())
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one or more edited branch measure lengths are absent from the retained tree",
             "$.charts[].bms_control_tree");
    if (context.emitted_bpms.size() != context.bpm_edits.size())
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one or more edited branch BPM statements are absent from the retained tree",
             "$.charts[].bms_control_tree");
    if (context.emitted_timing.size() != context.timing_edits.size())
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one or more edited extended BPM or STOP declarations are absent from the retained tree",
             "$.charts[].bms_control_tree");
    if (context.emitted_channels.size() != branch_edits.channel_edits.size())
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "one or more edited branch channel lines are absent from the retained tree",
             "$.charts[].bms_control_tree");
}

void append_preserved_branch_tree(const branch_tree_analysis_t& tree,
                                 std::vector<std::string>& output,
                                 std::vector<std::string> preserved_lines) {
    if (!tree.has_branches || preserved_lines.empty()) return;
    if (!tree.branch_source_begin || !tree.branch_source_end)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "retained BMS branch tree has no source-order span",
             "$.charts[].bms_control_tree");
    bool root_before_tree = true;
    bool root_after_tree = !tree.root_source_orders.empty();
    for (const auto source_order : tree.root_source_orders) {
        root_before_tree = root_before_tree && source_order < *tree.branch_source_begin;
        root_after_tree = root_after_tree && source_order > *tree.branch_source_end;
    }
    if (!tree.root_source_orders.empty() && !root_before_tree && !root_after_tree)
        fail(bms_write_diagnostic_code_t::branch_provenance,
             "root semantic statements occur on both sides of retained branch source; this writer cannot interleave normalized lines without changing override order",
             "$.charts[].bms_control_tree");
    if (root_after_tree)
        output.insert(output.begin(), preserved_lines.begin(), preserved_lines.end());
    else
        output.insert(output.end(), preserved_lines.begin(), preserved_lines.end());
}

auto normalized_write(const song_document_t& song, const chart_document_t& chart,
                      const bms_write_options_t& options,
                      std::vector<bms_write_diagnostic_t>& diagnostics) -> std::string {
    validate_normalized_write_input(song, chart);
    const auto branch_state = prepare_branch_write_state(song, chart, options, diagnostics);
    const auto& retained_branch_tree = branch_state.retained_tree;
    const auto& branch_tree = branch_state.tree;
    const auto flattening_active_projection = branch_state.flattening_active_projection;
    const auto& active_branch_orders = branch_state.active_orders;
    const auto& branch_measure_ratio_edits = branch_state.measure_ratio_edits;
    const auto branch_source_bytes = source_provenance_bytes(song, chart);
    auto branch_direct_bpm_edits = collect_branch_direct_bpm_edits(
        chart, branch_tree, active_branch_orders, branch_source_bytes);
    validate_retained_pauses(chart, branch_tree);

    validate_unsupported_chart_features(chart, flattening_active_projection, branch_tree,
                                        diagnostics);
    const auto [title, artist] = export_title_artist(song, chart);

    const auto layout = layout_for_export(chart, options, diagnostics);
    auto lane_channels = export_lane_channels(layout);
    auto& long_note_channels = lane_channels.long_note;

    auto resource_state = export_resources(song, branch_tree, diagnostics);
    auto& audio_ids = resource_state.audio_ids;
    auto& visual_ids = resource_state.visual_ids;
    auto& branch_resource_ids = resource_state.branch_resource_ids;
    auto& branch_resource_edits = resource_state.branch_resource_edits;
    auto& declarations = resource_state.declarations;

    const auto measure_index_result = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measure_index_result)) {
        fail(bms_write_diagnostic_code_t::invalid_geometry,
             "chart measure geometry cannot be represented safely", "$.charts[].measures");
    }
    const auto& index = std::get<measure_index_t>(measure_index_result);

    branch_event_edit_state_t branch_edits{chart, branch_tree, active_branch_orders,
                                           branch_source_bytes, index, {}, {}, {}, {}};
    auto& branch_channel_edits = branch_edits.channel_edits;
    auto& branch_timing_declaration_edits = branch_edits.timing_declaration_edits;
    apply_branch_bpm_edits(chart, branch_edits);
    apply_branch_pause_edits(chart, branch_edits);

    std::map<std::pair<std::size_t, std::string>, channel_line_t> channel_lines;
    std::map<std::pair<std::size_t, std::uint64_t>, channel_line_t> bgm_lines;
    std::map<std::pair<std::size_t, std::string>, std::size_t> bga_line_order;
    std::vector<std::string> projected_timing_declarations;
    const bpm_event_t* projected_initial_bpm{};
    if (flattening_active_projection) {
        auto projection = flattened_timing_projection(chart, index, channel_lines);
        projected_timing_declarations = std::move(projection.declarations);
        projected_initial_bpm = projection.initial_bpm;
    }
    std::map<std::uint64_t, std::vector<hold_endpoint_t>> hold_endpoints;
    note_export_context_t note_context{song, chart, branch_tree, layout, index, branch_edits,
                                       lane_channels.visible, long_note_channels, audio_ids,
                                       branch_resource_ids, channel_lines, hold_endpoints};
    bgm_export_context_t bgm_context{chart, branch_tree, index, branch_edits, audio_ids,
                                     branch_resource_ids, bgm_lines, options};
    export_chart_notes_and_bgm(chart, branch_tree, note_context, bgm_context);

    validate_hold_endpoints(hold_endpoints);

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

    bga_export_context_t bga_context{chart, branch_tree, index, branch_edits, visual_ids,
                                     branch_resource_ids, channel_lines, bga_line_order, {}, {}};
    export_bga_events(chart, bga_context);

    const auto player_value = bms_player_value(layout);
    auto bms_metadata_groups = collect_bms_metadata_groups(song, chart);
    const auto offset_decimal = exact_offset_decimal(chart.beat_zero_audio_offset_seconds);
    if (chart.beat_zero_audio_offset_seconds.numerator != 0)
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "OFFSET is a Wormedit BMS extension and may be ignored by other players",
             "$.charts[].beat_zero_audio_offset_seconds");
    const auto playlevel_from_rating = derive_playlevel_from_rating(chart, diagnostics);
    bool has_ordered_playlevel = false;
    const bool ordered_playlevel_matches_rating =
        retained_playlevel_matches(bms_metadata_groups, playlevel_from_rating,
                                   has_ordered_playlevel);
    if (has_ordered_playlevel && playlevel_from_rating && !ordered_playlevel_matches_rating)
        warn(diagnostics, bms_write_diagnostic_code_t::lossy_transform,
             "chart rating differs from retained PLAYLEVEL; retained source metadata takes precedence",
             "$.charts[].metadata.rating");
    auto metadata_sources = collect_metadata_source_state(bms_metadata_groups, branch_tree,
                                                           retained_branch_tree);
    auto& has_branch_player_field = metadata_sources.has_branch_player;
    auto& last_title_source = metadata_sources.title;
    auto& last_artist_source = metadata_sources.artist;
    auto& last_offset_source = metadata_sources.offset;
    auto& branch_metadata_edits = metadata_sources.branch_edits;
    const bool title_is_branch_projection = route_projected_branch_metadata(
        "TITLE", last_title_source, title, branch_tree, branch_metadata_edits);
    const bool artist_is_branch_projection = route_projected_branch_metadata(
        "ARTIST", last_artist_source, artist, branch_tree, branch_metadata_edits);
    const bool offset_is_branch_projection = route_projected_branch_metadata(
        "OFFSET", last_offset_source, offset_decimal, branch_tree, branch_metadata_edits);
    metadata_output_context_t metadata_output{chart,
                                             branch_tree,
                                             bms_metadata_groups,
                                             diagnostics,
                                             player_value,
                                             title,
                                             artist,
                                             offset_decimal,
                                             playlevel_from_rating,
                                             has_ordered_playlevel,
                                             has_branch_player_field,
                                             title_is_branch_projection,
                                             artist_is_branch_projection,
                                             offset_is_branch_projection,
                                             last_offset_source,
                                             {},
                                             false,
                                             false,
                                             false,
                                             false};
    auto output = emit_metadata_header(std::move(metadata_output));

    append_bms_bpm_header(chart, flattening_active_projection, projected_initial_bpm,
                          branch_tree, index, output);

    append_resource_declarations(declarations, audio_ids, visual_ids, output);
    output.insert(output.end(), projected_timing_declarations.begin(),
                  projected_timing_declarations.end());

    append_measure_ratio_lines(chart, branch_tree, diagnostics, output);

    append_ordered_channel_lines(channel_lines, bga_line_order, bgm_lines, chart, options, output);

    branch_tree_emission_context_t tree_emission{branch_tree,
                                                  branch_channel_edits,
                                                  branch_edits.slot_claims,
                                                  branch_metadata_edits,
                                                  branch_resource_edits,
                                                  branch_measure_ratio_edits,
                                                  branch_direct_bpm_edits,
                                                  branch_timing_declaration_edits,
                                                  {}, {}, {}, {}, {}, {}, {}};
    auto preserved_tree_lines = emit_preserved_branch_lines(branch_tree, tree_emission, branch_edits);
    validate_branch_tree_edits_emitted(tree_emission, branch_edits);
    append_preserved_branch_tree(branch_tree, output, std::move(preserved_tree_lines));

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

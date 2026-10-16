#include <wormedit/core/result.h>
#include "../core/text_util.h"
#include "../core/timing_arithmetic.h"
#include <wormedit/core/bms36.h>
#include <wormedit/core/numeric_text.h>
#include <wormedit/formats/bms_clipboard.h>

#include <wormedit/core/rational.h>

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <set>
#include <tuple>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wormedit {
namespace {

enum class grammar_t { bmse3, bmse_legacy, ibmsc, ibmsc_xnt };
enum class object_kind_t { note, bgm, bpm, stop, scroll, speed };

struct record_t {
    object_kind_t kind{object_kind_t::note};
    std::string channel;
    std::size_t lane_index{};
    std::size_t bgm_lane{};
    rational_t offset{};
    rational_t duration{};
    signed_rational_t value{};
    std::optional<resource_id_t> sound;
    std::string value_spelling;
    std::string line;
    std::size_t order{};
    unsigned attribute{};
    bool invisible{};
    bool mine{};
    bool long_endpoint{};
};

using line_result_t = result_t<std::vector<std::string_view>, std::string>;

using record_result_t = result_t<record_t, std::string>;

struct resource_result_t {
    std::optional<resource_id_t> id;
    bool ambiguous{};
};

struct resource_index_t {
    std::unordered_map<std::string, resource_result_t> exact;
    std::unordered_map<std::string, resource_result_t> folded;
};

struct decode_context_t {
    const song_document_t& song;
    const chart_document_t& chart;
    const lane_layout_t& layout;
    const bms_clipboard_decode_options_t& options;
    const resource_index_t& resources;
};

[[nodiscard]] auto detect_grammar(std::string_view header) -> std::optional<grammar_t> {
    if (header == "BMSE3.0 ClipBoard Object Data Format") return grammar_t::bmse3;
    if (header == "BMSE ClipBoard Object Data Format") return grammar_t::bmse_legacy;
    if (header == "iBMSC Clipboard Data") return grammar_t::ibmsc;
    if (header == "iBMSC Clipboard Data xNT") return grammar_t::ibmsc_xnt;
    return std::nullopt;
}

[[nodiscard]] auto is_valid_newlines(std::string_view text) -> bool {
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\n' && (index == 0 || text[index - 1] != '\r')) return false;
        if (text[index] == '\r' && (index + 1 == text.size() || text[index + 1] != '\n'))
            return false;
    }
    return true;
}

[[nodiscard]] auto split_payload(std::string_view text, grammar_t grammar) -> line_result_t {
    if (!is_valid_newlines(text)) return std::unexpected(std::string{"BMS clipboard must use consistent CRLF line endings."});
    const auto header_end = text.find("\r\n");
    if (header_end == std::string_view::npos)
        return std::unexpected(std::string{"BMS clipboard is missing its header or record separator."});
    if (!detect_grammar(text.substr(0, header_end)))
        return std::unexpected(std::string{"BMS clipboard header is not recognized."});
    auto body = text.substr(header_end + 2);
    if (body.empty()) return std::unexpected(std::string{"BMS clipboard contains no records."});
    if ((grammar == grammar_t::bmse3 || grammar == grammar_t::bmse_legacy) &&
        !body.ends_with("\r\n"))
        return std::unexpected(std::string{"BMSE clipboard requires its trailing CRLF."});

    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start < body.size()) {
        const auto end = body.find("\r\n", start);
        const auto line_end = end == std::string_view::npos ? body.size() : end;
        const auto line = body.substr(start, line_end - start);
        if (!line.empty()) lines.push_back(line);
        else if (grammar == grammar_t::bmse3 || grammar == grammar_t::bmse_legacy)
            return std::unexpected(std::string{"BMS clipboard contains an empty record."});
        if (end == std::string_view::npos) break;
        start = end + 2;
    }
    if (lines.empty()) return std::unexpected(std::string{"BMS clipboard contains no records."});
    return lines;
}

[[nodiscard]] auto parse_unsigned(std::string_view text) -> std::optional<std::uint64_t> {
    if (text.empty()) return std::nullopt;
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

[[nodiscard]] auto parse_signed(std::string_view text) -> std::optional<std::int64_t> {
    if (text.empty()) return std::nullopt;
    std::int64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

[[nodiscard]] auto parse_finite_double(std::string_view text) -> std::optional<double> {
    if (text.empty()) return std::nullopt;
    double value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value,
                                        std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(value)) return std::nullopt;
    return value;
}

[[nodiscard]] auto normalized_signed(std::int64_t numerator, std::uint64_t denominator)
    -> std::optional<signed_rational_t> {
    return timing_arithmetic::try_normalize({numerator, denominator});
}

[[nodiscard]] auto position_from_rows(double rows) -> std::optional<rational_t> {
    if (!std::isfinite(rows) || rows < 0.0) return std::nullopt;
    if (static_cast<long double>(rows) / 192.0L <= 1e-9L) return rational_t{0, 1};
    const rationalization_options_t limits{
        .maximum_denominator = 192U * 64U,
        .maximum_absolute_error = 1e-9L,
    };
    const auto result = rationalized_value_t::from(rows / 192.0, limits);
    return result ? std::optional{result->value} : std::nullopt;
}

auto base36_digit(char byte) -> std::optional<std::uint32_t> {
    const auto value = bms36_value(static_cast<unsigned char>(byte), bms36_case_policy_t::uppercase_only);
    return value < 0 ? std::nullopt : std::optional{static_cast<std::uint32_t>(value)};
}

[[nodiscard]] auto parse_base36(std::string_view text) -> std::optional<std::uint32_t> {
    std::uint32_t value = 0;
    if (text.empty()) return std::nullopt;
    for (const auto character : text) {
        const auto digit = base36_digit(character);
        if (!digit || value > (std::numeric_limits<std::uint32_t>::max() - *digit) / 36)
            return std::nullopt;
        value = value * 36 + *digit;
    }
    return value;
}

[[nodiscard]] auto decimal_digits(std::uint64_t number, bms_clipboard_numbering_t numbering)
    -> std::optional<std::string> {
    std::string alphabet;
    switch (numbering) {
    case bms_clipboard_numbering_t::base16: alphabet = "0123456789ABCDEF"; break;
    case bms_clipboard_numbering_t::base36: alphabet = bms36_digits; break;
    case bms_clipboard_numbering_t::base62: alphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"; break;
    }
    const auto radix = alphabet.size();
    if (radix == 0 || number >= radix * radix) return std::nullopt;
    std::string result(2, '0');
    result[1] = alphabet[number % radix];
    result[0] = alphabet[number / radix];
    return result;
}

[[nodiscard]] auto index_resources(const song_document_t& song) -> resource_index_t {
    resource_index_t result;
    for (const auto& resource : song.resources) {
        for (const auto& identity : resource.target_identities) {
            auto name_space = identity.namespace_name;
            name_space = text_util::ascii_upper(std::move(name_space));
            if (identity.family != format_family_t::bms || name_space != "WAV")
                continue;
            const auto add = [&](auto& index, std::string key) {
                auto& entry = index[std::move(key)];
                if (entry.id && *entry.id != resource.id) entry.ambiguous = true;
                else entry.id = resource.id;
            };
            add(result.exact, identity.identifier);
            auto folded = identity.identifier;
            folded = text_util::ascii_upper(std::move(folded));
            add(result.folded, std::move(folded));
        }
    }
    return result;
}

[[nodiscard]] auto resolve_resource(const resource_index_t& resources, std::uint64_t number,
                                    bms_clipboard_numbering_t numbering) -> result_t<resource_id_t, std::string> {
    const auto token = decimal_digits(number, numbering);
    if (!token) return std::unexpected(std::string{"BMS clipboard resource index exceeds its numbering base."});
    const auto& index = numbering == bms_clipboard_numbering_t::base62
        ? resources.exact : resources.folded;
    auto key = *token;
    if (numbering != bms_clipboard_numbering_t::base62)
        key = text_util::ascii_upper(std::move(key));
    const auto found = index.find(key);
    if (found == index.end())
        return std::unexpected(std::string{"BMS clipboard references a WAV identity absent from the target song."});
    if (found->second.ambiguous)
        return std::unexpected(std::string{"BMS WAV identity resolves to more than one target resource."});
    return *found->second.id;
}

[[nodiscard]] auto lane_for_channel(const lane_layout_t& layout, std::string_view channel)
    -> std::optional<std::pair<std::size_t, lane_id_t>> {
    std::optional<std::pair<std::size_t, lane_id_t>> match;
    for (std::size_t index = 0; index < layout.lanes.size(); ++index) {
        const auto& lane = layout.lanes[index];
        if (!lane.bms_channels.visible || *lane.bms_channels.visible != channel) continue;
        if (match) return std::nullopt;
        match = std::pair{index, lane.id};
    }
    return match;
}

[[nodiscard]] auto is_note_channel(std::string_view channel) -> bool {
    if (channel.size() != 2 || (channel[0] != '1' && channel[0] != '2')) return false;
    return channel[1] >= '1' && channel[1] <= '9';
}

[[nodiscard]] auto channel_kind(std::string_view channel) -> std::optional<object_kind_t> {
    if (channel == "08") return object_kind_t::bpm;
    if (channel == "09") return object_kind_t::stop;
    if (channel == "SC") return object_kind_t::scroll;
    if (channel == "SP") return object_kind_t::speed;
    if (is_note_channel(channel)) return object_kind_t::note;
    if (channel == "04" || channel == "06" || channel == "07") return std::nullopt;
    return std::nullopt;
}

[[nodiscard]] auto bmse_channel(std::string_view code, std::size_t& bgm_lane)
    -> std::optional<std::pair<object_kind_t, std::string>> {
    if (code.size() != 3) return std::nullopt;
    const auto numeric = parse_base36(code);
    if (!numeric) return std::nullopt;
    if (code.front() == '0') {
        const auto channel = code.substr(1);
        const auto kind = channel_kind(channel);
        if (!kind) return std::nullopt;
        return std::pair{*kind, std::string{channel}};
    }
    constexpr std::uint32_t first_bgm = 36U * 36U + 1U;
    if (*numeric >= first_bgm && *numeric < first_bgm + 128U) {
        bgm_lane = *numeric - first_bgm;
        return std::pair{object_kind_t::bgm, std::string{}};
    }
    return std::nullopt;
}

[[nodiscard]] auto ibmsc_channel(std::uint64_t column, std::size_t& bgm_lane)
    -> std::optional<std::pair<object_kind_t, std::string>> {
    if (column == 1) return std::pair{object_kind_t::scroll, std::string{"SC"}};
    if (column == 2) return std::pair{object_kind_t::bpm, std::string{"08"}};
    if (column == 3) return std::pair{object_kind_t::stop, std::string{"09"}};
    if (column == 5) return std::pair{object_kind_t::note, std::string{"16"}};
    constexpr std::string_view one_player = "11121314151819";
    constexpr std::string_view two_player = "21222324252829";
    if (column >= 6 && column <= 12)
        return std::pair{object_kind_t::note, std::string{one_player.substr((column - 6) * 2, 2)}};
    if (column >= 14 && column <= 20)
        return std::pair{object_kind_t::note, std::string{two_player.substr((column - 14) * 2, 2)}};
    if (column == 21) return std::pair{object_kind_t::note, std::string{"26"}};
    if (column >= 27 && column < 155) {
        bgm_lane = static_cast<std::size_t>(column - 27);
        return std::pair{object_kind_t::bgm, std::string{}};
    }
    return std::nullopt;
}

[[nodiscard]] auto resource_number(signed_rational_t value) -> std::optional<std::uint64_t> {
    if (value.numerator <= 0 || value.denominator == 0 ||
        static_cast<std::uint64_t>(value.numerator) % value.denominator != 0)
        return std::nullopt;
    return static_cast<std::uint64_t>(value.numerator) / value.denominator;
}

[[nodiscard]] auto finish_record(record_t& record, const decode_context_t& context)
    -> std::string {
    if (record.kind == object_kind_t::note) {
        const auto lane = lane_for_channel(context.layout, record.channel);
        if (!lane) return "BMS note channel has no unique lane in the target profile.";
        record.lane_index = lane->first;
        if (record.mine) {
            if (record.value.numerator < 0 || record.value.denominator != 1 || record.value.numerator > 1295)
                return "BMS mine damage index must be an integer from 0 through 1295.";
            return {};
        }
        const auto number = resource_number(record.value);
        if (!number) return "BMS note resource index must be a positive integer.";
        const auto resource = resolve_resource(context.resources, *number,
                                               context.options.object_numbering);
        if (!resource) return resource.error();
        record.sound = *resource;
        return {};
    }
    if (record.kind == object_kind_t::bgm) {
        if (record.bgm_lane >= context.chart.bgm_source_lanes.size())
            return "BMS BGM column has no matching target source lane.";
        const auto number = resource_number(record.value);
        if (!number) return "BMS BGM resource index must be a positive integer.";
        const auto resource = resolve_resource(context.resources, *number,
                                               context.options.object_numbering);
        if (!resource) return resource.error();
        record.sound = *resource;
        return {};
    }
    if (record.kind == object_kind_t::bpm && record.value.numerator <= 0)
        return "BMS clipboard BPM must be positive.";
    if (record.kind == object_kind_t::stop && record.value.numerator < 0)
        return "BMS clipboard STOP duration cannot be negative.";
    return {};
}

[[nodiscard]] auto bmse_position(std::string_view line, grammar_t grammar)
    -> std::optional<rational_t> {
    double row{};
    if (grammar == grammar_t::bmse3) {
        const auto position = line.substr(4, 16);
        if (position.size() != 16 || !std::ranges::all_of(position, [](char character) {
                return (character >= '0' && character <= '9') ||
                       (character >= 'A' && character <= 'F');
            })) return std::nullopt;
        std::uint64_t bits{};
        const auto converted = std::from_chars(position.data(), position.data() + position.size(),
                                               bits, 16);
        if (converted.ec != std::errc{} || converted.ptr != position.data() + position.size())
            return std::nullopt;
        row = std::bit_cast<double>(bits);
    } else {
        const auto position = line.substr(4, 7);
        if (position.size() != 7 || !std::ranges::all_of(position, [](char character) {
                return character >= '0' && character <= '9';
            })) return std::nullopt;
        const auto parsed = parse_unsigned(position);
        if (!parsed) return std::nullopt;
        row = static_cast<double>(*parsed);
    }
    return position_from_rows(row);
}

[[nodiscard]] auto parse_bmse_record(std::string_view line, grammar_t grammar,
                                    std::size_t order, const decode_context_t& context)
    -> record_result_t {
    const auto header_bytes = grammar == grammar_t::bmse3 ? std::size_t{20} : std::size_t{11};
    if (line.size() <= header_bytes) return std::unexpected(std::string{"BMSE clipboard record is truncated."});
    std::size_t bgm_lane{};
    const auto channel = bmse_channel(line.substr(0, 3), bgm_lane);
    if (!channel) return std::unexpected(std::string{"BMSE clipboard channel is unknown or unsupported."});
    if (line[3] < '0' || line[3] > '3') return std::unexpected(std::string{"BMSE clipboard attribute is invalid."});

    const auto offset = bmse_position(line, grammar);
    if (!offset) return std::unexpected(std::string{"BMSE position bits, digits, or recovered fraction are invalid."});
    const auto value_text = line.substr(header_bytes);
    const auto value = parse_exact_decimal(value_text, 18);
    if (!value) return std::unexpected(std::string{"BMSE clipboard value is not a finite decimal number."});

    record_t record;
    record.kind = channel->first;
    record.channel = channel->second;
    record.bgm_lane = bgm_lane;
    record.offset = *offset;
    record.value = *value;
    record.value_spelling = std::string{value_text};
    record.line = std::string{line};
    record.order = order;
    record.attribute = static_cast<unsigned>(line[3] - '0');
    if (record.kind == object_kind_t::note) {
        record.invisible = record.attribute == 1;
        record.long_endpoint = record.attribute == 2;
        record.mine = record.attribute == 3;
    } else if (record.attribute != 0) {
        return std::unexpected(std::string{"BMSE clipboard attributes apply only to playable note records."});
    }
    const auto error = finish_record(record, context);
    if (!error.empty()) return std::unexpected(error);
    return record;
}

[[nodiscard]] auto split_single_spaces(std::string_view line)
    -> std::optional<std::vector<std::string_view>> {
    if (line.empty() || line.front() == ' ' || line.back() == ' ') return std::nullopt;
    std::vector<std::string_view> fields;
    std::size_t begin = 0;
    while (begin < line.size()) {
        const auto end = line.find(' ', begin);
        const auto stop = end == std::string_view::npos ? line.size() : end;
        if (stop == begin) return std::nullopt;
        fields.push_back(line.substr(begin, stop - begin));
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return fields;
}

[[nodiscard]] auto apply_ibmsc_length(record_t& record, grammar_t grammar,
    signed_rational_t ln, std::string_view length_text) -> std::string {
    if (grammar == grammar_t::ibmsc_xnt) {
            if (ln.numerator < 0) return "iBMSC xNT duration cannot be negative.";
            if (ln.numerator > 0) {
                const auto length = parse_finite_double(length_text);
                const auto duration = length ? position_from_rows(*length) : std::nullopt;
                if (!length || !duration || duration->numerator == 0)
                    return "iBMSC xNT duration is not representable.";
                record.duration = *duration;
            }
        } else {
            if (ln.numerator != 0 && ln.numerator != -1)
                return "iBMSC normal-mode long-note field must be -1 or 0.";
            record.long_endpoint = ln.numerator == -1;
        }
    return {};
}

[[nodiscard]] auto apply_ibmsc_flags(record_t& record, grammar_t grammar,
    signed_rational_t ln, std::int64_t hidden, std::int64_t mine, std::string_view length_text)
    -> std::string {
    if (record.kind == object_kind_t::note) {
        record.invisible = hidden == -1;
        record.mine = mine == -1;
        if (record.invisible && record.mine)
            return "iBMSC cannot mark one clipboard note both hidden and mine.";
        const auto length_error = apply_ibmsc_length(record, grammar, ln, length_text);
        if (!length_error.empty()) return length_error;
        if ((record.invisible || record.mine) && (record.long_endpoint || record.duration.numerator))
            return "iBMSC long notes cannot also be hidden or mine.";
    } else if (ln.numerator != 0 || hidden != 0 || mine != 0) {
        return "iBMSC flags are only valid for playable note columns.";
    }

    return {};
}

[[nodiscard]] auto parse_ibmsc_record(std::string_view line, grammar_t grammar,
                                     std::size_t order, const decode_context_t& context)
    -> record_result_t {
    const auto fields = split_single_spaces(line);
    if (!fields || fields->size() != 6)
        return std::unexpected(std::string{"iBMSC record must have six single-space-separated fields."});
    const auto column = parse_unsigned((*fields)[0]);
    const auto row = parse_finite_double((*fields)[1]);
    const auto scaled_value = parse_signed((*fields)[2]);
    const auto offset = row ? position_from_rows(*row) : std::nullopt;
    if (!column || !row || !offset || !scaled_value)
        return std::unexpected(std::string{"iBMSC record contains an invalid column, position, or value."});
    const auto ln = grammar == grammar_t::ibmsc_xnt
        ? parse_exact_decimal((*fields)[3], 18)
        : normalized_signed(parse_signed((*fields)[3]).value_or(std::numeric_limits<std::int64_t>::min()), 1);
    if (grammar == grammar_t::ibmsc && !parse_signed((*fields)[3]))
        return std::unexpected(std::string{"iBMSC long-note flag is not an integer."});
    const auto hidden = parse_signed((*fields)[4]);
    const auto mine = parse_signed((*fields)[5]);
    if (!ln || !hidden || !mine || (*hidden != 0 && *hidden != -1) ||
        (*mine != 0 && *mine != -1))
        return std::unexpected(std::string{"iBMSC long-note, hidden, or mine field is invalid."});
    std::size_t bgm_lane{};
    const auto channel = ibmsc_channel(*column, bgm_lane);
    if (!channel) return std::unexpected(std::string{"iBMSC column is unknown, a spacer, or unsupported."});
    const auto scaled = normalized_signed(*scaled_value, 10'000);
    if (!scaled) return std::unexpected(std::string{"iBMSC numeric value overflows the native rational range."});

    record_t record;
    record.kind = channel->first;
    record.channel = channel->second;
    record.bgm_lane = bgm_lane;
    record.offset = *offset;
    record.value = *scaled;
    record.value_spelling = std::string{(*fields)[2]};
    record.line = std::string{line};
    record.order = order;
    const auto flag_error = apply_ibmsc_flags(record, grammar, *ln, *hidden, *mine, (*fields)[3]);
    if (!flag_error.empty()) return std::unexpected(flag_error);
    const auto error = finish_record(record, context);
    if (!error.empty()) return std::unexpected(error);
    return record;
}

[[nodiscard]] auto checked_multiply(std::uint64_t left, std::uint64_t right)
    -> std::optional<std::uint64_t> {
    if (right != 0 && left > std::numeric_limits<std::uint64_t>::max() / right)
        return std::nullopt;
    return left * right;
}

[[nodiscard]] auto exact_difference(rational_t end, rational_t begin)
    -> std::optional<rational_t> {
    if (end.denominator == 0 || begin.denominator == 0) return std::nullopt;
    auto whole = end.numerator / end.denominator;
    const auto begin_whole = begin.numerator / begin.denominator;
    if (whole < begin_whole) return std::nullopt;
    whole -= begin_whole;
    const auto common = std::gcd(end.denominator, begin.denominator);
    const auto denominator = checked_multiply(end.denominator, begin.denominator / common);
    const auto end_part = checked_multiply(end.numerator % end.denominator, begin.denominator / common);
    const auto begin_part = checked_multiply(begin.numerator % begin.denominator, end.denominator / common);
    if (!denominator || !end_part || !begin_part) return std::nullopt;
    std::uint64_t remainder{};
    if (*end_part < *begin_part) {
        if (whole == 0) return std::nullopt;
        --whole;
        remainder = *denominator - (*begin_part - *end_part);
    } else {
        remainder = *end_part - *begin_part;
    }
    const auto divisor = std::gcd(remainder, *denominator);
    const auto reduced_denominator = *denominator / divisor;
    remainder /= divisor;
    const auto numerator = checked_multiply(whole, reduced_denominator);
    if (!numerator || *numerator > std::numeric_limits<std::uint64_t>::max() - remainder)
        return std::nullopt;
    if (*numerator + remainder == 0) return std::nullopt;
    return rational_t{*numerator + remainder, reduced_denominator};
}

[[nodiscard]] auto fraction_less(rational_t left, rational_t right) -> bool {
    auto reversed = false;
    while (true) {
        const auto left_whole = left.numerator / left.denominator;
        const auto right_whole = right.numerator / right.denominator;
        if (left_whole != right_whole)
            return reversed ? left_whole > right_whole : left_whole < right_whole;
        const auto left_remainder = left.numerator % left.denominator;
        const auto right_remainder = right.numerator % right.denominator;
        if (left_remainder == 0 || right_remainder == 0) {
            if (left_remainder == 0 && right_remainder == 0) return false;
            return reversed ? right_remainder == 0 : left_remainder == 0;
        }
        left = {left.denominator, left_remainder};
        right = {right.denominator, right_remainder};
        reversed = !reversed;
    }
}

[[nodiscard]] auto profile_name(grammar_t grammar) -> std::string_view {
    switch (grammar) {
    case grammar_t::bmse3: return "BMSE 3.0";
    case grammar_t::bmse_legacy: return "BMSE legacy";
    case grammar_t::ibmsc: return "iBMSC normal";
    case grammar_t::ibmsc_xnt: return "iBMSC xNT";
    }
    return "BMS editor";
}

[[nodiscard]] auto make_provenance(const record_t& record, grammar_t grammar)
    -> source_provenance_t {
    std::string field;
    if (record.kind == object_kind_t::note)
        field = record.mine ? "mine damage" : "playable channel " + record.channel;
    else if (record.kind == object_kind_t::bgm)
        field = "BGM lane " + std::to_string(record.bgm_lane);
    else if (record.kind == object_kind_t::bpm) field = "BPM";
    else if (record.kind == object_kind_t::stop) field = "STOP";
    else if (record.kind == object_kind_t::scroll) field = "SCROLL";
    else field = "SPEED";
    return {.family = format_family_t::bms,
            .profile = std::string{profile_name(grammar)},
            .source_field = std::move(field),
            .source_spelling = record.mine ? record.value_spelling : record.line,
            .source_order = record.order};
}

[[nodiscard]] auto make_note(const record_t& record, grammar_t grammar,
                             const decode_context_t& context, object_id_t id,
                             bool hold) -> note_t {
    auto kind = note_kind_t::tap;
    if (record.mine) kind = note_kind_t::mine;
    else if (record.invisible) kind = note_kind_t::invisible;
    else if (hold) kind = note_kind_t::hold;
    return {.id = id,
            .position = {},
            .lane = context.layout.lanes[record.lane_index].id,
            .kind = kind,
            .end = hold ? std::optional<local_position_t>{local_position_t{}} : std::nullopt,
            .sound = record.sound,
            .provenance = make_provenance(record, grammar)};
}

void append_note_object(chart_clipboard_t& clipboard, const record_t& record,
                        grammar_t grammar, const decode_context_t& context,
                        std::size_t id, rational_t duration = {}) {
    const auto hold = duration.numerator != 0;
    clipboard.objects.push_back({.offset = record.offset,
        .duration = duration,
        .lane_index = record.lane_index,
        .payload = make_note(record, grammar, context, object_id_t{id}, hold)});
}

void append_record_object(chart_clipboard_t& clipboard, const record_t& record,
                          grammar_t grammar, const decode_context_t& context,
                          std::size_t id) {
    const auto provenance = make_provenance(record, grammar);
    switch (record.kind) {
    case object_kind_t::note:
        if (!record.long_endpoint)
            append_note_object(clipboard, record, grammar, context, id, record.duration);
        return;
    case object_kind_t::bgm:
        clipboard.objects.push_back({.offset = record.offset,
            .lane_index = record.bgm_lane,
            .payload = bgm_event_t{.id = object_id_t{id}, .position = {},
                .source_lane = context.chart.bgm_source_lanes[record.bgm_lane].id,
                .sound = record.sound, .provenance = provenance}});
        return;
    case object_kind_t::bpm:
        clipboard.objects.push_back({.offset = record.offset,
            .payload = bpm_event_t{.id = object_id_t{id}, .position = {},
                .beats_per_minute = record.value, .provenance = provenance}});
        return;
    case object_kind_t::stop:
        clipboard.objects.push_back({.offset = record.offset,
            .payload = pause_event_t{.id = object_id_t{id}, .position = {},
                .kind = pause_kind_t::stop, .duration = record.value,
                .source_unit = pause_unit_t::bms_stop_ticks, .provenance = provenance}});
        return;
    case object_kind_t::scroll:
        clipboard.objects.push_back({.offset = record.offset,
            .payload = scroll_event_t{.id = object_id_t{id}, .position = {},
                .multiplier = record.value, .provenance = provenance}});
        return;
    case object_kind_t::speed:
        clipboard.objects.push_back({.offset = record.offset,
            .payload = speed_event_t{.id = object_id_t{id}, .position = {},
                .multiplier = record.value, .provenance = provenance}});
        return;
    }
}

[[nodiscard]] auto long_note_group_equal(const record_t& left, const record_t& right) -> bool {
    return left.lane_index == right.lane_index;
}

[[nodiscard]] auto append_long_note_groups(chart_clipboard_t& clipboard,
    std::vector<const record_t*> endpoints, grammar_t grammar,
    const decode_context_t& context, std::size_t& next_id) -> std::string {
    std::ranges::stable_sort(endpoints, [](const auto* left, const auto* right) {
        if (left->lane_index != right->lane_index) return left->lane_index < right->lane_index;
        return fraction_less(left->offset, right->offset);
    });
    std::size_t group_start = 0;
    while (group_start < endpoints.size()) {
        auto group_end = group_start + 1;
        while (group_end < endpoints.size() &&
               long_note_group_equal(*endpoints[group_start], *endpoints[group_end])) ++group_end;
        if ((group_end - group_start) % 2 != 0)
            return "BMS long-note endpoints do not form complete same-lane, same-resource pairs.";
        for (auto index = group_start; index < group_end; index += 2) {
            const auto& head = *endpoints[index];
            const auto& tail = *endpoints[index + 1];
            if (head.sound != tail.sound)
                return "BMS long-note endpoints use different resources.";
            const auto duration = exact_difference(tail.offset, head.offset);
            if (!duration)
                return "BMS long-note endpoint pair has a zero, reversed, or overflowing duration.";
            append_note_object(clipboard, head, grammar, context, next_id++, *duration);
        }
        group_start = group_end;
    }
    return {};
}

using record_key_t = std::tuple<object_kind_t, std::size_t, std::size_t,
    std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
    std::optional<resource_id_t>, std::int64_t, std::uint64_t, bool, bool, bool>;

[[nodiscard]] auto record_key(const record_t& record) -> record_key_t {
    return {record.kind, record.lane_index, record.bgm_lane,
        record.offset.numerator, record.offset.denominator,
        record.duration.numerator, record.duration.denominator,
        record.sound, record.sound ? 0 : record.value.numerator,
        record.sound ? 1 : record.value.denominator,
        record.invisible, record.mine, record.long_endpoint};
}

[[nodiscard]] auto build_clipboard(const std::vector<record_t>& records, grammar_t grammar,
                                   const decode_context_t& context) -> chart_clipboard_outcome_t {
    chart_clipboard_t clipboard{.source_song = context.song.id, .source_chart = context.chart.id};
    std::vector<const record_t*> endpoints;
    std::size_t next_id = 1;
    for (const auto& record : records) {
        if (record.long_endpoint)
            endpoints.push_back(&record);
        else
            append_record_object(clipboard, record, grammar, context, next_id++);
    }
    const auto error =
        append_long_note_groups(clipboard, std::move(endpoints), grammar, context, next_id);
    if (!error.empty())
        return std::unexpected(error);
    if (clipboard.objects.empty())
        return std::unexpected(std::string{"BMS clipboard contains no supported objects."});
    return clipboard;
}

} // namespace

auto parse_bms_chart_clipboard(std::string_view text, const song_document_t& target_document,
                               const chart_document_t& target_chart,
                               const lane_layout_t& target_layout,
                               bms_clipboard_decode_options_t options)
    -> chart_clipboard_outcome_t {
    if (options.maximum_bytes == 0 || text.size() > options.maximum_bytes)
        return std::unexpected(std::string{"BMS clipboard exceeds the configured byte limit."});
    if (options.maximum_records == 0)
        return std::unexpected(std::string{"BMS clipboard record limit must be positive."});
    const auto separator = text.find("\r\n");
    if (separator == std::string_view::npos)
        return std::unexpected(
            std::string{"BMS clipboard is missing its header or record separator."});
    const auto grammar = detect_grammar(text.substr(0, separator));
    if (!grammar)
        return std::unexpected(std::string{"BMS clipboard header is not recognized."});
    const auto lines = split_payload(text, *grammar);
    if (!lines)
        return std::unexpected(lines.error());
    if (lines->size() > options.maximum_records)
        return std::unexpected(std::string{"BMS clipboard exceeds the configured record limit."});

    const auto resources = index_resources(target_document);
    const decode_context_t context{target_document, target_chart, target_layout, options,
                                   resources};
    std::vector<record_t> records;
    records.reserve(lines->size());
    std::set<record_key_t> seen;
    for (std::size_t order = 0; order < lines->size(); ++order) {
        auto parsed = *grammar == grammar_t::bmse3 || *grammar == grammar_t::bmse_legacy
                          ? parse_bmse_record((*lines)[order], *grammar, order, context)
                          : parse_ibmsc_record((*lines)[order], *grammar, order, context);
        if (!parsed)
            return std::unexpected(parsed.error());
        if (!seen.insert(record_key(*parsed)).second)
            return std::unexpected(std::string{"BMS clipboard contains a duplicate record."});
        records.push_back(std::move(*parsed));
    }
    return build_clipboard(records, *grammar, context);
}

} // namespace wormedit

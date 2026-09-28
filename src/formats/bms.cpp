#include <wormedit/core/bms_profile_detection.h>
#include <wormedit/formats/bms.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

#if defined(WORMEDIT_HAS_ICONV)
#include <iconv.h>
#endif

namespace wormedit {
namespace {

struct source_line_t {
    std::string text;
    bms_source_span_t span;
    bms_line_ending_t ending{bms_line_ending_t::none};
    std::size_t order{};
};

struct decode_result_t {
    std::optional<std::string> text;
    std::size_t error_offset{};
    bool unsupported{};
};

struct parsed_line_t {
    std::string token;
    std::string raw_value;
    std::string semantic_value;
    bool has_separator{};
    char separator{};
};

enum class frame_kind_t {
    random,
    if_group,
    switch_block,
};

struct open_frame_t {
    frame_kind_t kind{frame_kind_t::random};
    std::vector<std::size_t> parent_path;
    std::vector<std::size_t> path;
    std::optional<std::vector<std::size_t>> active_branch_path;
    bool terminal_branch_seen{};
};

auto equal_ascii_case_insensitive(std::string_view left, std::string_view right) -> bool {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto a = static_cast<unsigned char>(left[index]);
        const auto b = static_cast<unsigned char>(right[index]);
        if (std::toupper(a) != std::toupper(b)) {
            return false;
        }
    }
    return true;
}

auto trim_ascii(std::string_view value) -> std::string_view {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

auto path_from_utf8(std::string_view value) -> std::filesystem::path {
    std::u8string utf8;
    utf8.reserve(value.size());
    std::ranges::transform(value, std::back_inserter(utf8),
                           [](char character) { return static_cast<char8_t>(character); });
    return std::filesystem::path(std::move(utf8));
}

auto utf8_error_offset(std::string_view text) -> std::optional<std::size_t> {
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
            return index;
        }
        if (width > text.size() - index) {
            return index;
        }
        for (std::size_t offset = 1; offset < width; ++offset) {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xc0U) != 0x80U) {
                return index + offset;
            }
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if ((width == 3 && codepoint < 0x800U) || (width == 4 && codepoint < 0x10000U) ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU) || codepoint > 0x10ffffU) {
            return index;
        }
        ++index;
        index += width - 1;
    }
    return std::nullopt;
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

auto decode_line(std::string_view input, text_encoding_t encoding) -> decode_result_t {
    if (encoding == text_encoding_t::ascii) {
        for (std::size_t index = 0; index < input.size(); ++index) {
            if (static_cast<unsigned char>(input[index]) > 0x7fU) {
                return {.text = std::nullopt, .error_offset = index};
            }
        }
        return {.text = std::string(input)};
    }
    if (encoding == text_encoding_t::utf8) {
        if (const auto error = utf8_error_offset(input); error.has_value()) {
            return {.text = std::nullopt, .error_offset = *error};
        }
        return {.text = std::string(input)};
    }

    const auto source_name = iconv_name(encoding);
    if (!source_name.has_value()) {
        return {.unsupported = true};
    }
#if defined(WORMEDIT_HAS_ICONV)
    const auto converter = iconv_open("UTF-8", std::string(*source_name).c_str());
    if (converter == reinterpret_cast<iconv_t>(-1)) {
        return {.unsupported = true};
    }

    std::string output(std::max<std::size_t>(input.size() * 4U + 16U, 32U), '\0');
    auto* source = const_cast<char*>(input.data());
    auto source_remaining = input.size();
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
        if (errno != E2BIG) {
            const auto error_offset = input.size() - source_remaining;
            iconv_close(converter);
            return {.text = std::nullopt, .error_offset = error_offset};
        }
        if (output.size() > std::numeric_limits<std::size_t>::max() / 2U) {
            iconv_close(converter);
            return {.text = std::nullopt, .error_offset = input.size()};
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
            return {.text = std::nullopt, .error_offset = input.size()};
        }
        output.resize(output.size() * 2U);
    }
    iconv_close(converter);
    output.resize(written);
    return {.text = std::move(output)};
#else
    return {.unsupported = true};
#endif
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

auto decode_diagnostic(bms_parse_result_t& result, bms_parse_diagnostic_code_t code,
                       std::string message, std::size_t byte_offset, std::size_t byte_end) -> void {
    result.diagnostics.push_back({
        .severity = diagnostic_severity_t::error,
        .code = code,
        .message = std::move(message),
        .line = 1,
        .column = byte_offset + 1,
        .source_span = bms_source_span_t{byte_offset, byte_end},
    });
}

auto add_line_diagnostic(bms_parse_result_t& result, diagnostic_severity_t severity,
                         bms_parse_diagnostic_code_t code, std::string message,
                         const source_line_t& line, std::size_t column = 1) -> void {
    result.diagnostics.push_back({
        .severity = severity,
        .code = code,
        .message = std::move(message),
        .line = line.order + 1,
        .column = column,
        .source_span = line.span,
    });
}

auto split_lines(std::span<const std::byte> bytes, std::size_t start, text_encoding_t encoding,
                 const bms_parse_options_t& options, bms_parse_result_t& result)
    -> std::optional<std::vector<source_line_t>> {
    const auto* data = reinterpret_cast<const char*>(bytes.data());
    std::vector<source_line_t> lines;
    auto cursor = start;
    while (cursor < bytes.size()) {
        if (lines.size() >= options.max_lines) {
            result.diagnostics.push_back({
                .severity = diagnostic_severity_t::error,
                .code = bms_parse_diagnostic_code_t::malformed_line,
                .message = "BMS source exceeds the configured physical line limit",
                .line = lines.size() + 1,
                .column = 1,
                .source_span = bms_source_span_t{cursor, bytes.size()},
            });
            return std::nullopt;
        }
        const auto begin = cursor;
        while (cursor < bytes.size() && data[cursor] != '\n' && data[cursor] != '\r') {
            ++cursor;
        }
        const auto content_end = cursor;
        if (content_end - begin > options.max_line_bytes) {
            result.diagnostics.push_back({
                .severity = diagnostic_severity_t::error,
                .code = bms_parse_diagnostic_code_t::malformed_line,
                .message = "BMS source line exceeds the configured byte limit",
                .line = lines.size() + 1,
                .column = options.max_line_bytes + 1,
                .source_span = bms_source_span_t{begin, cursor},
            });
            return std::nullopt;
        }
        auto ending = bms_line_ending_t::none;
        if (cursor < bytes.size()) {
            if (data[cursor] == '\r' && cursor + 1 < bytes.size() && data[cursor + 1] == '\n') {
                cursor += 2;
                ending = bms_line_ending_t::crlf;
            } else if (data[cursor] == '\r') {
                ++cursor;
                ending = bms_line_ending_t::cr;
            } else {
                ++cursor;
                ending = bms_line_ending_t::lf;
            }
        }

        const std::string_view raw_line(data + begin, content_end - begin);
        auto decoded = decode_line(raw_line, encoding);
        if (decoded.unsupported) {
            add_line_diagnostic(result, diagnostic_severity_t::error,
                                bms_parse_diagnostic_code_t::unsupported_encoding,
                                "requested " + std::string(encoding_name(encoding)) +
                                    " conversion is unavailable in this build",
                                source_line_t{.span = {begin, cursor}, .order = lines.size()});
            return std::nullopt;
        }
        if (!decoded.text.has_value()) {
            const auto error = begin + decoded.error_offset;
            decode_diagnostic(result, bms_parse_diagnostic_code_t::invalid_encoding,
                              "input contains a malformed " + std::string(encoding_name(encoding)) +
                                  " byte sequence",
                              error, std::min(error + 1, cursor));
            result.diagnostics.back().line = lines.size() + 1;
            result.diagnostics.back().column = decoded.error_offset + 1;
            return std::nullopt;
        }
        lines.push_back({
            .text = std::move(*decoded.text),
            .span = {begin, cursor},
            .ending = ending,
            .order = lines.size(),
        });
    }
    return lines;
}

auto parse_line(std::string_view text) -> parsed_line_t {
    if (text.empty() || text.front() != '#') {
        return {};
    }
    text.remove_prefix(1);
    const auto separator = text.find_first_of(" \t:");
    if (separator == std::string_view::npos) {
        return {.token = std::string(text)};
    }
    auto raw_value = text.substr(separator + 1);
    const auto semantic = trim_ascii(raw_value);
    return {
        .token = std::string(text.substr(0, separator)),
        .raw_value = std::string(raw_value),
        .semantic_value = std::string(semantic),
        .has_separator = true,
        .separator = text[separator],
    };
}

template <typename id_t> auto new_id(id_allocator_t& allocator) -> id_t {
    return allocate_id<id_t>(allocator).value_or(id_t{});
}

auto make_node(const source_line_t& line, const parsed_line_t& parsed, bms_control_kind_t kind,
               id_allocator_t& allocator, std::optional<std::int64_t> value = std::nullopt)
    -> bms_control_node_t {
    return {
        .id = new_id<branch_id_t>(allocator),
        .kind = kind,
        .value = value,
        .original_text = line.text,
        .command_token = parsed.token,
        .raw_value = parsed.raw_value,
        .source_span = line.span,
        .source_order = line.order,
        .line_ending = line.ending,
    };
}

auto node_at(bms_control_node_t& root, const std::vector<std::size_t>& path)
    -> bms_control_node_t& {
    auto* node = &root;
    for (const auto index : path) {
        node = &node->children.at(index);
    }
    return *node;
}

auto append_node(bms_control_node_t& root, const std::vector<std::size_t>& parent_path,
                 bms_control_node_t node) -> std::vector<std::size_t> {
    auto& parent = node_at(root, parent_path);
    const auto index = parent.children.size();
    parent.children.push_back(std::move(node));
    auto path = parent_path;
    path.push_back(index);
    return path;
}

auto close_node(bms_control_node_t& root, const std::vector<std::size_t>& path,
                const source_line_t& line) -> void {
    auto& node = node_at(root, path);
    node.close_source_span = line.span;
    node.close_text = line.text;
    node.close_source_order = line.order;
    node.close_line_ending = line.ending;
}

auto parse_integer(std::string_view text) -> std::optional<std::int64_t> {
    std::int64_t value{};
    if (text.empty()) {
        return std::nullopt;
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

auto base36_digit(char character) -> bool {
    const auto value = static_cast<unsigned char>(character);
    return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z');
}

auto upper_ascii(std::string_view value) -> std::string {
    auto result = std::string(value);
    std::ranges::transform(result, result.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return result;
}

auto is_bms_metadata_command(std::string_view token) -> bool {
    constexpr std::array commands{
        std::string_view{"PLAYER"},     std::string_view{"GENRE"},   std::string_view{"TITLE"},
        std::string_view{"SUBTITLE"},   std::string_view{"ARTIST"},  std::string_view{"SUBARTIST"},
        std::string_view{"PLAYLEVEL"},  std::string_view{"OFFSET"},  std::string_view{"RANK"},
        std::string_view{"DEFEXRANK"},
        std::string_view{"TOTAL"},      std::string_view{"VOLWAV"},  std::string_view{"STAGEFILE"},
        std::string_view{"BANNER"},     std::string_view{"BACKBMP"}, std::string_view{"PREVIEW"},
        std::string_view{"DIFFICULTY"}, std::string_view{"COMMENT"}, std::string_view{"URL"},
        std::string_view{"EMAIL"},
    };
    return std::ranges::find(commands, token) != commands.end();
}

auto is_safe_bms_header_token(std::string_view token) -> bool {
    if (token.empty() || !((token.front() >= 'A' && token.front() <= 'Z') ||
                           (token.front() >= 'a' && token.front() <= 'z'))) {
        return false;
    }
    return std::ranges::all_of(token, [](char character) {
        return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_';
    });
}

auto parse_measure_command(std::string_view token)
    -> std::optional<std::pair<std::size_t, std::string>> {
    if (token.size() != 5 ||
        !std::ranges::all_of(token.substr(0, 3),
                             [](unsigned char c) { return c >= '0' && c <= '9'; }) ||
        !base36_digit(token[3]) || !base36_digit(token[4])) {
        return std::nullopt;
    }
    std::size_t measure{};
    const auto converted = std::from_chars(token.data(), token.data() + 3, measure);
    if (converted.ec != std::errc{}) {
        return std::nullopt;
    }
    return std::pair{measure, upper_ascii(token.substr(3, 2))};
}

struct root_channel_line_t {
    const bms_control_node_t* node{};
    std::size_t measure{};
    std::string channel;
    std::string payload;
    std::size_t pair_count{};
    bool materialize{};
};

auto is_playable_channel(std::string_view channel) -> bool {
    if (channel.size() != 2) {
        return false;
    }
    switch (channel.front()) {
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
        return true;
    case 'D':
    case 'E':
        return channel[1] >= '1' && channel[1] <= '9';
    default:
        return false;
    }
}

auto is_materialized_note_family(std::string_view channel) -> bool {
    return channel.size() == 2 &&
           (channel.front() == '1' || channel.front() == '2' || channel.front() == '3' ||
            channel.front() == '4' || channel.front() == '5' || channel.front() == '6' ||
            channel.front() == 'D' || channel.front() == 'E');
}

auto is_long_note_channel(std::string_view channel) -> bool {
    return channel.size() == 2 && (channel.front() == '5' || channel.front() == '6');
}

auto checked_lcm(std::uint64_t left, std::uint64_t right) -> std::optional<std::uint64_t> {
    if (left == 0 || right == 0) {
        return std::nullopt;
    }
    const auto reduced = left / std::gcd(left, right);
    if (reduced > std::numeric_limits<std::uint64_t>::max() / right) {
        return std::nullopt;
    }
    return reduced * right;
}

auto resolution_for_slots(std::uint64_t current_resolution, const measure_definition_t& measure,
                          std::size_t pair_count) -> std::optional<std::uint64_t> {
    if (pair_count == 0 || measure.length.numerator == 0 || measure.length.denominator == 0) {
        return std::nullopt;
    }
    const auto slots = static_cast<std::uint64_t>(pair_count);
    // R * (p / q) / slots must be integral. Since p/q is reduced, the
    // remaining denominator is q * (slots / gcd(p, slots)).
    const auto reduced_slots = slots / std::gcd(measure.length.numerator, slots);
    if (reduced_slots > std::numeric_limits<std::uint64_t>::max() / measure.length.denominator) {
        return std::nullopt;
    }
    const auto required_multiple = reduced_slots * measure.length.denominator;
    return checked_lcm(current_resolution, required_multiple);
}

auto parse_positive_rational(std::string_view text) -> std::optional<signed_rational_t> {
    const auto parsed = parse_measure_length(text);
    if (!parsed.value.has_value() ||
        parsed.value->numerator >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return signed_rational_t{static_cast<std::int64_t>(parsed.value->numerator),
                             parsed.value->denominator};
}

auto parse_signed_decimal_seconds(std::string_view text) -> std::optional<signed_rational_t> {
    if (text.empty()) return std::nullopt;
    const bool negative = text.front() == '-';
    if (negative || text.front() == '+') text.remove_prefix(1);
    if (text.empty()) return std::nullopt;
    std::uint64_t magnitude{};
    std::uint64_t denominator{1};
    bool decimal_point = false;
    bool digit_seen = false;
    const auto limit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) +
                       (negative ? 1U : 0U);
    for (const char character : text) {
        if (character == '.' && !decimal_point) {
            decimal_point = true;
            continue;
        }
        if (character < '0' || character > '9') return std::nullopt;
        digit_seen = true;
        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (magnitude > (limit - digit) / 10 ||
            (decimal_point && denominator > std::numeric_limits<std::uint64_t>::max() / 10)) {
            return std::nullopt;
        }
        magnitude = magnitude * 10 + digit;
        if (decimal_point) denominator *= 10;
    }
    if (!digit_seen) return std::nullopt;
    const auto divisor = std::gcd(magnitude, denominator);
    magnitude /= divisor;
    denominator /= divisor;
    const auto numerator = negative
                               ? (magnitude == limit
                                      ? std::numeric_limits<std::int64_t>::min()
                                      : -static_cast<std::int64_t>(magnitude))
                               : static_cast<std::int64_t>(magnitude);
    return signed_rational_t{numerator, denominator};
}

auto source_provenance(std::string_view field, std::string_view spelling, std::size_t order,
                       std::string_view profile = "bms-byte-parser") -> source_provenance_t {
    return {
        .family = format_family_t::bms,
        .profile = std::string(profile),
        .source_field = std::string(field),
        .source_spelling = std::string(spelling),
        .source_order = order,
    };
}

auto node_source_line(const bms_control_node_t& node) -> source_line_t {
    return {
        .text = node.original_text,
        .span = node.source_span,
        .ending = node.line_ending,
        .order = node.source_order,
    };
}

auto has_errors(const std::vector<bms_parse_diagnostic_t>& diagnostics) -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto push_raw_statement(bms_control_node_t& root, const std::vector<std::size_t>& parent_path,
                        const source_line_t& line, const parsed_line_t& parsed,
                        id_allocator_t& allocator) -> void {
    (void)append_node(root, parent_path,
                      make_node(line, parsed, bms_control_kind_t::raw_statement, allocator));
}

auto is_branch_boundary(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::random || kind == bms_control_kind_t::switch_block ||
           kind == bms_control_kind_t::if_group || kind == bms_control_kind_t::if_branch ||
           kind == bms_control_kind_t::else_if_branch || kind == bms_control_kind_t::else_branch ||
           kind == bms_control_kind_t::case_branch || kind == bms_control_kind_t::default_branch;
}

auto is_control_token(std::string_view token) -> bool {
    constexpr std::array<std::string_view, 13> tokens{
        "RANDOM",    "SETRANDOM", "IF",  "ELSEIF", "ELSE",  "ENDIF",     "SWITCH",
        "SETSWITCH", "CASE",      "DEF", "SKIP",   "ENDSW", "ENDRANDOM",
    };
    return std::ranges::any_of(tokens, [&](std::string_view candidate) {
        return equal_ascii_case_insensitive(token, candidate);
    });
}

auto active_branch_statement_orders(const bms_control_node_t& root,
                                    const std::vector<branch_selection_t>& selections)
    -> std::optional<std::set<std::size_t>> {
    std::map<std::uint64_t, std::int64_t> selected_values;
    for (const auto& selection : selections) {
        if (!selection.branch ||
            !selected_values.emplace(selection.branch.value, selection.selected_value).second) {
            return std::nullopt;
        }
    }

    std::set<std::size_t> active_orders;
    std::set<std::uint64_t> visited_selections;
    std::function<void(const bms_control_node_t&, std::optional<std::int64_t>, bool, bool*)> visit;
    visit = [&](const bms_control_node_t& node, std::optional<std::int64_t> random_value,
                bool inside_active_branch, bool* switch_stopped) -> void {
        if (node.kind == bms_control_kind_t::raw_statement) {
            if (inside_active_branch) {
                active_orders.insert(node.source_order);
            }
            return;
        }
        if (node.kind == bms_control_kind_t::skip) {
            if (switch_stopped != nullptr) {
                *switch_stopped = true;
            }
            return;
        }
        if (node.kind == bms_control_kind_t::random) {
            const auto selected = selected_values.find(node.id.value);
            if (selected == selected_values.end() || !node.value.has_value() ||
                selected->second < 1 || selected->second > *node.value) {
                return;
            }
            visited_selections.insert(node.id.value);
            for (const auto& child : node.children) {
                visit(child, selected->second, true, switch_stopped);
            }
            return;
        }
        if (node.kind == bms_control_kind_t::if_group) {
            if (!random_value.has_value()) {
                return;
            }
            const bms_control_node_t* fallback{};
            const bms_control_node_t* selected{};
            for (const auto& child : node.children) {
                if ((child.kind == bms_control_kind_t::if_branch ||
                     child.kind == bms_control_kind_t::else_if_branch) &&
                    child.value == random_value) {
                    selected = &child;
                    break;
                }
                if (child.kind == bms_control_kind_t::else_branch) {
                    fallback = &child;
                }
            }
            if (selected == nullptr) {
                selected = fallback;
            }
            if (selected != nullptr) {
                visit(*selected, random_value, true, switch_stopped);
            }
            return;
        }
        if (node.kind == bms_control_kind_t::switch_block) {
            const auto choice = selected_values.find(node.id.value);
            if (choice == selected_values.end()) {
                return;
            }
            visited_selections.insert(node.id.value);
            const bms_control_node_t* selected{};
            const bms_control_node_t* fallback{};
            for (const auto& child : node.children) {
                if (child.kind == bms_control_kind_t::case_branch &&
                    child.value == choice->second) {
                    selected = &child;
                    break;
                }
                if (child.kind == bms_control_kind_t::default_branch) {
                    fallback = &child;
                }
            }
            if (selected == nullptr) {
                selected = fallback;
            }
            if (selected == nullptr) {
                return;
            }
            bool stopped{};
            bool entered{};
            for (const auto& child : node.children) {
                if (child.kind == bms_control_kind_t::case_branch ||
                    child.kind == bms_control_kind_t::default_branch) {
                    entered = entered || &child == selected;
                    if (entered && !stopped) {
                        visit(child, random_value, true, &stopped);
                    }
                } else if (entered && !stopped) {
                    visit(child, random_value, true, &stopped);
                }
            }
            return;
        }
        const auto branch_scope = inside_active_branch ||
                                  node.kind == bms_control_kind_t::if_branch ||
                                  node.kind == bms_control_kind_t::else_if_branch ||
                                  node.kind == bms_control_kind_t::else_branch ||
                                  node.kind == bms_control_kind_t::case_branch ||
                                  node.kind == bms_control_kind_t::default_branch;
        for (const auto& child : node.children) {
            visit(child, random_value, branch_scope, switch_stopped);
        }
    };
    visit(root, std::nullopt, false, nullptr);
    if (visited_selections.size() != selected_values.size()) {
        return std::nullopt;
    }
    return active_orders;
}

struct retained_source_t {
    std::shared_ptr<const std::vector<std::byte>> bytes;
    text_encoding_t encoding{text_encoding_t::unknown};
};

auto retained_bms_source(const song_document_t& song, const chart_document_t& chart)
    -> std::optional<retained_source_t> {
    const auto find =
        [](const std::vector<format_metadata_t>& groups) -> std::optional<retained_source_t> {
        const auto group = std::ranges::find_if(groups, [](const auto& candidate) {
            return candidate.family == format_family_t::bms &&
                   candidate.encoding.original_bytes != nullptr;
        });
        if (group == groups.end()) {
            return std::nullopt;
        }
        return retained_source_t{group->encoding.original_bytes, group->encoding.detected};
    };
    if (auto source = find(chart.metadata.format_groups); source.has_value()) {
        return source;
    }
    return find(song.metadata.format_groups);
}

auto ending_bytes(bms_line_ending_t ending) -> std::string_view {
    switch (ending) {
    case bms_line_ending_t::none:
        return {};
    case bms_line_ending_t::lf:
        return "\n";
    case bms_line_ending_t::crlf:
        return "\r\n";
    case bms_line_ending_t::cr:
        return "\r";
    }
    return {};
}

auto retained_line_text(const retained_source_t& source, const bms_control_node_t& node)
    -> std::optional<std::string> {
    if (node.source_span.byte_begin > node.source_span.byte_end ||
        node.source_span.byte_end > source.bytes->size()) {
        return std::nullopt;
    }
    const auto ending = ending_bytes(node.line_ending);
    const auto span_size = node.source_span.byte_end - node.source_span.byte_begin;
    if (ending.size() > span_size) {
        return std::nullopt;
    }
    const auto* bytes = reinterpret_cast<const char*>(source.bytes->data());
    const auto content_size = span_size - ending.size();
    if (std::string_view(bytes + node.source_span.byte_begin + content_size, ending.size()) !=
        ending) {
        return std::nullopt;
    }
    const auto decoded = decode_line(
        std::string_view(bytes + node.source_span.byte_begin, content_size), source.encoding);
    if (!decoded.text.has_value() || decoded.unsupported) {
        return std::nullopt;
    }
    return decoded.text;
}

} // namespace

auto bms_parse_result_t::has_errors() const -> bool {
    return wormedit::has_errors(diagnostics);
}

auto is_bms_text_encoding_supported(text_encoding_t encoding) -> bool {
    if (encoding == text_encoding_t::ascii || encoding == text_encoding_t::utf8) {
        return true;
    }
    const auto source_name = iconv_name(encoding);
    if (!source_name.has_value()) {
        return false;
    }
#if defined(WORMEDIT_HAS_ICONV)
    const auto converter = iconv_open("UTF-8", std::string(*source_name).c_str());
    if (converter == reinterpret_cast<iconv_t>(-1)) {
        return false;
    }
    iconv_close(converter);
    return true;
#else
    return false;
#endif
}

auto parse_bms_bytes(std::span<const std::byte> bytes, bms_parse_options_t options)
    -> bms_parse_result_t {
    bms_parse_result_t result;
    result.encoding.user_override = options.encoding_override;
    if (bytes.size() > options.max_source_bytes) {
        result.diagnostics.push_back({
            .severity = diagnostic_severity_t::error,
            .code = bms_parse_diagnostic_code_t::malformed_line,
            .message = "BMS source exceeds the configured byte limit",
            .line = 1,
            .column = 1,
            .source_span = bms_source_span_t{0, bytes.size()},
        });
        return result;
    }
    if (options.max_chart_resolution < default_divisions_per_standard_measure) {
        result.diagnostics.push_back({
            .severity = diagnostic_severity_t::error,
            .code = bms_parse_diagnostic_code_t::invalid_measure,
            .message = "configured chart-resolution limit must be at least the default 960 rows",
            .line = 1,
            .column = 1,
        });
        return result;
    }
    result.encoding.original_bytes =
        std::make_shared<const std::vector<std::byte>>(bytes.begin(), bytes.end());
    result.encoding.unchanged_since_import = true;

    const auto* data = reinterpret_cast<const char*>(bytes.data());
    const auto has_bom = bytes.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xefU &&
                         static_cast<unsigned char>(data[1]) == 0xbbU &&
                         static_cast<unsigned char>(data[2]) == 0xbfU;
    std::size_t content_start = has_bom ? 3 : 0;
    text_encoding_t encoding = text_encoding_t::unknown;
    if (has_bom) {
        if (options.encoding_override.has_value() &&
            *options.encoding_override != text_encoding_t::utf8) {
            decode_diagnostic(result, bms_parse_diagnostic_code_t::invalid_encoding,
                              "UTF-8 BOM conflicts with the requested " +
                                  std::string(encoding_name(*options.encoding_override)) +
                                  " override",
                              0, 3);
            return result;
        }
        encoding = text_encoding_t::utf8;
    } else if (options.encoding_override.has_value()) {
        encoding = *options.encoding_override;
        if (encoding == text_encoding_t::unknown) {
            decode_diagnostic(result, bms_parse_diagnostic_code_t::unsupported_encoding,
                              "unknown cannot be used as an encoding override", 0, bytes.size());
            return result;
        }
    } else {
        const auto all_ascii = std::ranges::all_of(
            bytes, [](std::byte byte) { return std::to_integer<unsigned char>(byte) <= 0x7fU; });
        if (!all_ascii) {
            result.diagnostics.push_back({
                .severity = diagnostic_severity_t::error,
                .code = bms_parse_diagnostic_code_t::invalid_encoding,
                .message = "non-ASCII BMS input without a UTF-8 BOM is ambiguous; provide an "
                           "explicit encoding override",
                .line = 1,
                .column = 1,
                .source_span = bms_source_span_t{0, bytes.size()},
            });
            return result;
        }
        encoding = text_encoding_t::ascii;
    }
    result.encoding.detected = encoding;

    auto lines = split_lines(bytes, content_start, encoding, options, result);
    if (!lines.has_value()) {
        return result;
    }

    song_document_t song;
    id_allocator_t ids;
    song.id = new_id<song_id_t>(ids);
    chart_document_t chart;
    chart.id = new_id<chart_id_t>(ids);
    chart.source_family = format_family_t::bms;
    chart.bms_control_tree = bms_control_node_t{
        .id = new_id<branch_id_t>(ids),
        .kind = bms_control_kind_t::root,
        .source_span = {0, bytes.size()},
    };

    auto& root = *chart.bms_control_tree;
    std::vector<std::size_t> current_path;
    std::vector<open_frame_t> frames;
    std::map<std::string, resource_id_t, std::less<>> resource_ids;
    std::map<std::size_t, std::size_t> last_measure_length_order;
    std::vector<std::pair<std::string, std::string>> ordered_metadata_fields;
    std::vector<std::size_t> ordered_metadata_source_orders;

    auto ensure_measure = [&](std::size_t index) -> measure_chunk_t& {
        while (chart.measures.size() <= index) {
            chart.measures.push_back({
                .id = new_id<measure_id_t>(ids),
                .definition = {{1, 1}, meter_t{4, 4}},
            });
        }
        return chart.measures[index];
    };

    auto add_resource = [&](const source_line_t& line, std::string_view command,
                            std::string_view identifier, const std::string& name) {
        const auto normalized_id = upper_ascii(identifier);
        if (name.empty()) {
            add_line_diagnostic(result, diagnostic_severity_t::warning,
                                bms_parse_diagnostic_code_t::malformed_line,
                                "resource declaration has an empty path", line);
            return;
        }
        const auto target_key = std::string(command) + normalized_id;
        if (resource_ids.contains(target_key)) {
            add_line_diagnostic(
                result, diagnostic_severity_t::warning,
                bms_parse_diagnostic_code_t::duplicate_resource,
                "duplicate BMS resource identifier; first declaration remains the semantic binding",
                line);
            return;
        }

        const auto asset_id = new_id<asset_id_t>(ids);
        const auto resource_id = new_id<resource_id_t>(ids);
        const auto source_path = path_from_utf8(name);
        if (equal_ascii_case_insensitive(command, "WAV")) {
            song.audio_assets.push_back({.id = asset_id, .source_path = source_path});
            song.resources.push_back({
                .id = resource_id,
                .name = name,
                .asset = asset_id,
                .roles = {resource_role_t::sample, resource_role_t::bgm},
                .target_identities = {{format_family_t::bms, normalized_id, "WAV"}},
                .provenance =
                    {
                        .family = format_family_t::bms,
                        .profile = "bms-byte-parser",
                        .source_field = std::string(command) + normalized_id,
                        .source_spelling = line.text,
                        .source_order = line.order,
                    },
            });
        } else {
            song.visual_assets.push_back({
                .id = asset_id,
                .kind = asset_kind_t::image,
                .source_path = source_path,
            });
            song.resources.push_back({
                .id = resource_id,
                .name = name,
                .asset = asset_id,
                .roles = {resource_role_t::bga},
                .target_identities = {{format_family_t::bms, normalized_id, "BMP"}},
                .provenance =
                    {
                        .family = format_family_t::bms,
                        .profile = "bms-byte-parser",
                        .source_field = std::string(command) + normalized_id,
                        .source_spelling = line.text,
                        .source_order = line.order,
                    },
            });
        }
        resource_ids.emplace(target_key, resource_id);
    };

    auto nesting_limit_reached = [&](const source_line_t& line, const parsed_line_t& parsed) {
        if (frames.size() < options.max_nesting_depth) {
            return false;
        }
        push_raw_statement(root, current_path, line, parsed, ids);
        add_line_diagnostic(
            result, diagnostic_severity_t::warning, bms_parse_diagnostic_code_t::malformed_control,
            "control nesting exceeds the configured depth limit; directive is retained raw", line);
        return true;
    };

    for (const auto& line : *lines) {
        const auto parsed = parse_line(line.text);
        const auto token = upper_ascii(parsed.token);
        if (line.text.empty()) {
            push_raw_statement(root, current_path, line, parsed, ids);
            continue;
        }
        if (line.text.front() != '#') {
            push_raw_statement(root, current_path, line, parsed, ids);
            continue;
        }

        if (token == "TITLE" || token == "ARTIST") {
            push_raw_statement(root, current_path, line, parsed, ids);
            continue;
        }

        if (token.size() == 5 && token.starts_with("WAV") && base36_digit(token[3]) &&
            base36_digit(token[4])) {
            push_raw_statement(root, current_path, line, parsed, ids);
            continue;
        }
        if (token.size() == 5 && token.starts_with("BMP") && base36_digit(token[3]) &&
            base36_digit(token[4])) {
            push_raw_statement(root, current_path, line, parsed, ids);
            continue;
        }

        const auto measure_command = parse_measure_command(token);
        if (measure_command.has_value()) {
            if (parsed.separator != ':') {
                push_raw_statement(root, current_path, line, parsed, ids);
                add_line_diagnostic(result, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::malformed_line,
                                    "BMS measure/channel command is missing its colon delimiter",
                                    line);
                continue;
            }
            const auto& [measure_number, channel] = *measure_command;
            if (channel == "02") {
                push_raw_statement(root, current_path, line, parsed, ids);
                (void)ensure_measure(measure_number);
            } else {
                push_raw_statement(root, current_path, line, parsed, ids);
                (void)ensure_measure(measure_number);
                const auto payload = trim_ascii(parsed.raw_value);
                const auto valid_payload = !payload.empty() && payload.size() % 2 == 0 &&
                                           std::ranges::all_of(payload, [](char character) {
                                               return base36_digit(character);
                                           });
                if (!valid_payload) {
                    add_line_diagnostic(
                        result, diagnostic_severity_t::error,
                        bms_parse_diagnostic_code_t::malformed_line,
                        payload.size() % 2 != 0
                            ? "BMS object payload has an odd number of base-36 digits"
                            : "BMS object payload contains a non-base-36 digit or is empty",
                        line);
                }
            }
            continue;
        }

        const auto add_simple_control = [&](bms_control_kind_t kind,
                                            std::optional<std::int64_t> value) {
            (void)append_node(root, current_path, make_node(line, parsed, kind, ids, value));
        };
        const auto make_control_value = [&]() -> std::optional<std::int64_t> {
            const auto value = parse_integer(parsed.semantic_value);
            if (!value.has_value()) {
                add_line_diagnostic(result, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::invalid_number,
                                    "control directive has a missing or invalid integer value; raw "
                                    "spelling is retained",
                                    line);
            }
            return value;
        };

        if (token == "RANDOM" || token == "SETRANDOM" || token == "SETSWITCH") {
            const auto value = make_control_value();
            const auto kind = token == "RANDOM"      ? bms_control_kind_t::random
                              : token == "SETRANDOM" ? bms_control_kind_t::set_random
                                                     : bms_control_kind_t::set_switch;
            if (token == "RANDOM") {
                if (nesting_limit_reached(line, parsed)) {
                    continue;
                }
                const auto path =
                    append_node(root, current_path, make_node(line, parsed, kind, ids, value));
                frames.push_back(
                    {.kind = frame_kind_t::random, .parent_path = current_path, .path = path});
                current_path = path;
            } else {
                add_simple_control(kind, value);
            }
            continue;
        }

        if (token == "IF") {
            if (nesting_limit_reached(line, parsed)) {
                continue;
            }
            const auto value = make_control_value();
            auto group_node = bms_control_node_t{
                .id = new_id<branch_id_t>(ids),
                .kind = bms_control_kind_t::if_group,
            };
            const auto group = append_node(root, current_path, std::move(group_node));
            const auto branch = append_node(
                root, group, make_node(line, parsed, bms_control_kind_t::if_branch, ids, value));
            frames.push_back({.kind = frame_kind_t::if_group,
                              .parent_path = current_path,
                              .path = group,
                              .active_branch_path = branch});
            current_path = branch;
            continue;
        }

        if (token == "ELSEIF" || token == "ELIF" || token == "ELSE") {
            if (frames.empty() || frames.back().kind != frame_kind_t::if_group ||
                !frames.back().active_branch_path.has_value()) {
                push_raw_statement(root, current_path, line, parsed, ids);
                add_line_diagnostic(result, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::malformed_control,
                                    "orphan conditional branch directive retained as raw text",
                                    line);
                continue;
            }
            auto& frame = frames.back();
            const auto is_else_if = token == "ELSEIF" || token == "ELIF";
            if (frame.terminal_branch_seen) {
                push_raw_statement(root, current_path, line, parsed, ids);
                add_line_diagnostic(result, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::malformed_control,
                                    is_else_if
                                        ? "conditional branch after #ELSE is retained as raw text"
                                        : "repeated #ELSE is retained as raw text",
                                    line);
                continue;
            }
            const auto kind =
                is_else_if ? bms_control_kind_t::else_if_branch : bms_control_kind_t::else_branch;
            const auto value = is_else_if ? make_control_value() : std::nullopt;
            const auto branch =
                append_node(root, frame.path, make_node(line, parsed, kind, ids, value));
            frame.active_branch_path = branch;
            frame.terminal_branch_seen = !is_else_if;
            current_path = branch;
            continue;
        }

        if (token == "ENDIF") {
            if (frames.empty() || frames.back().kind != frame_kind_t::if_group) {
                push_raw_statement(root, current_path, line, parsed, ids);
                add_line_diagnostic(result, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::malformed_control,
                                    "orphan #ENDIF retained as raw text", line);
                continue;
            }
            auto frame = std::move(frames.back());
            frames.pop_back();
            close_node(root, frame.path, line);
            current_path = std::move(frame.parent_path);
            continue;
        }

        if (token == "SWITCH") {
            if (nesting_limit_reached(line, parsed)) {
                continue;
            }
            const auto value = make_control_value();
            const auto path =
                append_node(root, current_path,
                            make_node(line, parsed, bms_control_kind_t::switch_block, ids, value));
            frames.push_back(
                {.kind = frame_kind_t::switch_block, .parent_path = current_path, .path = path});
            current_path = path;
            continue;
        }

        if (token == "CASE" || token == "DEF") {
            if (frames.empty() || frames.back().kind != frame_kind_t::switch_block) {
                push_raw_statement(root, current_path, line, parsed, ids);
                add_line_diagnostic(result, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::malformed_control,
                                    "orphan switch branch directive retained as raw text", line);
                continue;
            }
            auto& frame = frames.back();
            const auto is_default = token == "DEF";
            if (frame.terminal_branch_seen) {
                push_raw_statement(root, current_path, line, parsed, ids);
                add_line_diagnostic(result, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::malformed_control,
                                    is_default ? "repeated #DEF is retained as raw text"
                                               : "#CASE after #DEF is retained as raw text",
                                    line);
                continue;
            }
            const auto value = token == "CASE" ? make_control_value() : std::nullopt;
            const auto kind = token == "CASE" ? bms_control_kind_t::case_branch
                                              : bms_control_kind_t::default_branch;
            const auto branch =
                append_node(root, frame.path, make_node(line, parsed, kind, ids, value));
            frame.active_branch_path = branch;
            frame.terminal_branch_seen = is_default;
            current_path = branch;
            continue;
        }

        if (token == "ENDSW") {
            if (frames.empty() || frames.back().kind != frame_kind_t::switch_block) {
                push_raw_statement(root, current_path, line, parsed, ids);
                add_line_diagnostic(result, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::malformed_control,
                                    "orphan #ENDSW retained as raw text", line);
                continue;
            }
            auto frame = std::move(frames.back());
            frames.pop_back();
            close_node(root, frame.path, line);
            current_path = std::move(frame.parent_path);
            continue;
        }

        if (token == "ENDRANDOM") {
            if (frames.empty() || frames.back().kind != frame_kind_t::random) {
                push_raw_statement(root, current_path, line, parsed, ids);
                add_line_diagnostic(result, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::malformed_control,
                                    "orphan #ENDRANDOM retained as raw text", line);
                continue;
            }
            auto frame = std::move(frames.back());
            frames.pop_back();
            close_node(root, frame.path, line);
            current_path = std::move(frame.parent_path);
            continue;
        }

        if (token == "SKIP") {
            auto in_switch_case = false;
            for (auto frame = frames.rbegin(); frame != frames.rend(); ++frame) {
                if (frame->kind == frame_kind_t::switch_block) {
                    in_switch_case = frame->active_branch_path.has_value();
                    break;
                }
            }
            if (in_switch_case) {
                add_simple_control(bms_control_kind_t::skip, std::nullopt);
            } else {
                push_raw_statement(root, current_path, line, parsed, ids);
                add_line_diagnostic(result, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::malformed_control,
                                    "#SKIP outside an active switch case is retained as raw text",
                                    line);
            }
            continue;
        }

        // All unrecognized headers and channel payloads stay byte-addressable in the tree.
        push_raw_statement(root, current_path, line, parsed, ids);
    }

    for (const auto& frame : frames) {
        const auto& node = node_at(root, frame.path);
        result.diagnostics.push_back({
            .severity = diagnostic_severity_t::warning,
            .code = bms_parse_diagnostic_code_t::malformed_control,
            .message = "control block reaches end of file without its closing directive",
            .line = node.source_order + 1,
            .column = 1,
            .source_span = node.source_span,
        });
    }

    if (chart.measures.empty()) {
        (void)ensure_measure(0);
    }

    auto line_diagnostic = [&](const bms_control_node_t& node, diagnostic_severity_t severity,
                               bms_parse_diagnostic_code_t code, std::string message) {
        add_line_diagnostic(result, severity, code, std::move(message), node_source_line(node));
    };

    std::vector<const bms_control_node_t*> root_statements;
    root_statements.reserve(root.children.size());
    for (const auto& node : root.children) {
        if (node.kind == bms_control_kind_t::raw_statement) {
            root_statements.push_back(&node);
        }
    }

    std::map<std::size_t, const bms_control_node_t*> choice_nodes;
    const auto find_choice_nodes = [&](const auto& self, const bms_control_node_t& node) -> void {
        if (node.kind == bms_control_kind_t::random ||
            node.kind == bms_control_kind_t::switch_block) {
            choice_nodes.emplace(node.source_order, &node);
        }
        for (const auto& child : node.children) {
            self(self, child);
        }
    };
    find_choice_nodes(find_choice_nodes, root);

    std::map<std::size_t, std::int64_t> branch_choices;
    const auto choice_count = std::min(options.branch_choices.size(), options.max_lines);
    if (options.branch_choices.size() > options.max_lines) {
        result.diagnostics.push_back({
            .severity = diagnostic_severity_t::warning,
            .code = bms_parse_diagnostic_code_t::invalid_branch_selection,
            .message = "branch choice count exceeds the configured physical line limit; excess "
                       "choices are ignored",
            .line = 1,
            .column = 1,
        });
    }
    for (std::size_t index = 0; index < choice_count; ++index) {
        const auto& choice = options.branch_choices[index];
        const auto [found, inserted] =
            branch_choices.emplace(choice.control_source_order, choice.selected_value);
        if (!inserted) {
            const auto node = choice_nodes.find(choice.control_source_order);
            if (node != choice_nodes.end()) {
                line_diagnostic(*node->second, diagnostic_severity_t::warning,
                                bms_parse_diagnostic_code_t::invalid_branch_selection,
                                "duplicate choice for a RANDOM/SWITCH block; the first choice is "
                                "used");
            } else {
                result.diagnostics.push_back({
                    .severity = diagnostic_severity_t::warning,
                    .code = bms_parse_diagnostic_code_t::invalid_branch_selection,
                    .message = "duplicate choice for an unknown control source order; the first "
                               "choice is used",
                    .line = choice.control_source_order + 1,
                    .column = 1,
                });
            }
            (void)found;
        }
    }

    for (auto choice = branch_choices.begin(); choice != branch_choices.end();) {
        const auto node = choice_nodes.find(choice->first);
        if (node == choice_nodes.end()) {
            result.diagnostics.push_back({
                .severity = diagnostic_severity_t::warning,
                .code = bms_parse_diagnostic_code_t::invalid_branch_selection,
                .message = "branch choice does not identify a RANDOM or SWITCH opening line",
                .line = choice->first + 1,
                .column = 1,
            });
            choice = branch_choices.erase(choice);
            continue;
        }
        const auto& control = *node->second;
        if (control.kind == bms_control_kind_t::random &&
            (!control.value.has_value() || choice->second < 1 || choice->second > *control.value)) {
            line_diagnostic(control, diagnostic_severity_t::warning,
                            bms_parse_diagnostic_code_t::invalid_branch_selection,
                            "RANDOM choice is outside the block's declared range; its branches "
                            "remain unprojected");
            choice = branch_choices.erase(choice);
            continue;
        }
        if (control.kind == bms_control_kind_t::switch_block) {
            const auto has_case = std::ranges::any_of(control.children, [&](const auto& child) {
                return child.kind == bms_control_kind_t::case_branch && child.value.has_value() &&
                       *child.value == choice->second;
            });
            const auto has_default = std::ranges::any_of(control.children, [](const auto& child) {
                return child.kind == bms_control_kind_t::default_branch;
            });
            if (!has_case && !has_default) {
                line_diagnostic(control, diagnostic_severity_t::warning,
                                bms_parse_diagnostic_code_t::invalid_branch_selection,
                                "SWITCH choice matches no CASE and has no DEFAULT; its branches "
                                "remain unprojected");
                choice = branch_choices.erase(choice);
                continue;
            }
        }
        ++choice;
    }

    auto selected_if_branch =
        [](const bms_control_node_t& group,
           std::optional<std::int64_t> random_value) -> const bms_control_node_t* {
        if (!random_value.has_value()) {
            return nullptr;
        }
        const bms_control_node_t* fallback{};
        for (const auto& child : group.children) {
            if ((child.kind == bms_control_kind_t::if_branch ||
                 child.kind == bms_control_kind_t::else_if_branch) &&
                child.value == random_value) {
                return &child;
            }
            if (child.kind == bms_control_kind_t::else_branch) {
                fallback = &child;
            }
        }
        return fallback;
    };

    auto selected_switch_branch = [](const bms_control_node_t& block,
                                     std::int64_t value) -> const bms_control_node_t* {
        const bms_control_node_t* fallback{};
        for (const auto& child : block.children) {
            if (child.kind == bms_control_kind_t::case_branch && child.value == value) {
                return &child;
            }
            if (child.kind == bms_control_kind_t::default_branch) {
                fallback = &child;
            }
        }
        return fallback;
    };

    std::vector<const bms_control_node_t*> projected_control_statements;
    std::map<std::size_t, bool> visited_choices;
    std::function<void(const bms_control_node_t&, std::optional<std::int64_t>, bool,
                       std::optional<std::int64_t>&, std::optional<std::int64_t>&, bool*)>
        project_control;
    project_control = [&](const bms_control_node_t& node, std::optional<std::int64_t> random_value,
                          bool inside_control, std::optional<std::int64_t>& pending_random,
                          std::optional<std::int64_t>& pending_switch,
                          bool* switch_stopped) -> void {
        if (node.kind == bms_control_kind_t::raw_statement) {
            if (inside_control) {
                projected_control_statements.push_back(&node);
            }
            return;
        }
        if (node.kind == bms_control_kind_t::set_random) {
            pending_random = node.value;
            return;
        }
        if (node.kind == bms_control_kind_t::set_switch) {
            pending_switch = node.value;
            return;
        }
        if (node.kind == bms_control_kind_t::skip) {
            if (switch_stopped != nullptr) {
                *switch_stopped = true;
            }
            return;
        }
        if (node.kind == bms_control_kind_t::random) {
            auto selected_random = std::optional<std::int64_t>{};
            if (const auto choice = branch_choices.find(node.source_order);
                choice != branch_choices.end()) {
                selected_random = choice->second;
                visited_choices[node.source_order] = true;
            } else if (pending_random.has_value()) {
                selected_random = pending_random;
            }
            pending_random.reset();
            if (selected_random.has_value() && (!node.value.has_value() || *selected_random < 1 ||
                                                *selected_random > *node.value)) {
                line_diagnostic(node, diagnostic_severity_t::warning,
                                bms_parse_diagnostic_code_t::invalid_branch_selection,
                                "fixed RANDOM value is outside the block's declared range; its "
                                "branches remain unprojected");
                selected_random.reset();
            }
            if (selected_random.has_value()) {
                chart.active_branch_selection.push_back({node.id, *selected_random});
                for (const auto& child : node.children) {
                    project_control(child, selected_random, true, pending_random, pending_switch,
                                    switch_stopped);
                }
            }
            return;
        }
        if (node.kind == bms_control_kind_t::if_group) {
            if (const auto* selected = selected_if_branch(node, random_value);
                selected != nullptr) {
                project_control(*selected, random_value, true, pending_random, pending_switch,
                                switch_stopped);
            }
            return;
        }
        if (node.kind == bms_control_kind_t::switch_block) {
            const bms_control_node_t* selected{};
            auto selected_value = std::optional<std::int64_t>{};
            if (const auto choice = branch_choices.find(node.source_order);
                choice != branch_choices.end()) {
                selected_value = choice->second;
                visited_choices[node.source_order] = true;
            } else if (pending_switch.has_value()) {
                selected_value = pending_switch;
            }
            pending_switch.reset();
            if (selected_value.has_value()) {
                selected = selected_switch_branch(node, *selected_value);
                if (selected == nullptr) {
                    line_diagnostic(node, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::invalid_branch_selection,
                                    "fixed SWITCH value matches no CASE and has no DEFAULT; its "
                                    "branches remain unprojected");
                    selected_value.reset();
                } else {
                    chart.active_branch_selection.push_back({node.id, *selected_value});
                }
            }
            auto stopped = false;
            auto entered = false;
            for (const auto& child : node.children) {
                if (child.kind == bms_control_kind_t::case_branch ||
                    child.kind == bms_control_kind_t::default_branch) {
                    entered = entered || &child == selected;
                    if (entered && !stopped) {
                        project_control(child, random_value, true, pending_random, pending_switch,
                                        &stopped);
                    }
                } else if (entered && !stopped) {
                    project_control(child, random_value, true, pending_random, pending_switch,
                                    &stopped);
                }
            }
            return;
        }
        for (const auto& child : node.children) {
            project_control(child, random_value,
                            inside_control || node.kind != bms_control_kind_t::root, pending_random,
                            pending_switch, switch_stopped);
        }
    };
    auto pending_random = std::optional<std::int64_t>{};
    auto pending_switch = std::optional<std::int64_t>{};
    project_control(root, std::nullopt, false, pending_random, pending_switch, nullptr);
    for (const auto& [source_order, selected_value] : branch_choices) {
        (void)selected_value;
        if (!visited_choices.contains(source_order)) {
            const auto node = choice_nodes.find(source_order);
            if (node != choice_nodes.end()) {
                line_diagnostic(*node->second, diagnostic_severity_t::warning,
                                bms_parse_diagnostic_code_t::invalid_branch_selection,
                                "choice targets a control block inside an unselected branch and "
                                "is ignored");
            }
        }
    }
    std::ranges::sort(projected_control_statements, {},
                      [](const auto* node) { return node->source_order; });

    std::vector<const bms_control_node_t*> active_statements = root_statements;
    active_statements.insert(active_statements.end(), projected_control_statements.begin(),
                             projected_control_statements.end());
    std::ranges::stable_sort(active_statements, {},
                             [](const auto* node) { return node->source_order; });

    std::map<std::string, signed_rational_t, std::less<>> extended_bpms;
    std::map<std::string, signed_rational_t, std::less<>> stop_definitions;
    std::optional<std::pair<const bms_control_node_t*, signed_rational_t>> initial_bpm;
    std::vector<root_channel_line_t> channel_lines;
    std::vector<std::string> used_playable_channels;
    std::optional<unsigned int> player_header;
    bool octave_fp_header = false;

    for (const auto* node : active_statements) {
        const auto token = upper_ascii(node->command_token);
        const auto value = trim_ascii(node->raw_value);

        if (token == "OCT/FP") {
            octave_fp_header = true;
            continue;
        }
        if (is_bms_metadata_command(token)) {
            if (token == "TITLE") {
                song.metadata.title = std::string(value);
            } else if (token == "ARTIST") {
                song.metadata.artist = std::string(value);
            } else if (token == "PLAYER") {
                const auto parsed_player = parse_integer(value);
                if (parsed_player.has_value() && *parsed_player >= 0 &&
                    static_cast<std::uint64_t>(*parsed_player) <=
                        std::numeric_limits<unsigned int>::max()) {
                    player_header = static_cast<unsigned int>(*parsed_player);
                }
            } else if (token == "OFFSET") {
                const auto parsed_offset = parse_signed_decimal_seconds(value);
                if (parsed_offset) {
                    chart.beat_zero_audio_offset_seconds = *parsed_offset;
                } else {
                    line_diagnostic(*node, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::invalid_number,
                                    "OFFSET is not an exact signed decimal; raw text is retained");
                }
            }
            ordered_metadata_fields.emplace_back(token, node->raw_value);
            ordered_metadata_source_orders.push_back(node->source_order);
            continue;
        }
        if (token.size() == 5 && token.starts_with("WAV") && base36_digit(token[3]) &&
            base36_digit(token[4])) {
            add_resource(node_source_line(*node), "WAV", std::string_view(token).substr(3),
                         std::string(value));
            continue;
        }
        if (token.size() == 5 && token.starts_with("BMP") && base36_digit(token[3]) &&
            base36_digit(token[4])) {
            add_resource(node_source_line(*node), "BMP", std::string_view(token).substr(3),
                         std::string(value));
            continue;
        }

        const auto measure_command = parse_measure_command(token);
        if (measure_command.has_value() && token.size() == 5 && node->original_text.size() >= 7 &&
            node->original_text[0] == '#' && node->original_text[6] == ':') {
            const auto& [measure_number, channel] = *measure_command;
            if (channel == "02") {
                const auto parsed_length = parse_measure_length(value);
                if (parsed_length.value.has_value()) {
                    if (last_measure_length_order.contains(measure_number)) {
                        line_diagnostic(
                            *node, diagnostic_severity_t::warning,
                            bms_parse_diagnostic_code_t::invalid_measure,
                            "repeated measure-length declaration; later value is applied");
                    }
                    auto& measure = ensure_measure(measure_number);
                    measure.definition.length = *parsed_length.value;
                    measure.definition.meter.reset();
                    last_measure_length_order[measure_number] = node->source_order;
                } else {
                    line_diagnostic(*node, diagnostic_severity_t::error,
                                    bms_parse_diagnostic_code_t::invalid_measure,
                                    "invalid exact BMS measure length");
                }
                continue;
            }
        }

        if (token == "BPM" || (token.size() == 5 && token.starts_with("BPM") &&
                               base36_digit(token[3]) && base36_digit(token[4]))) {
            const auto parsed_value = parse_positive_rational(value);
            if (!parsed_value.has_value()) {
                line_diagnostic(
                    *node, diagnostic_severity_t::warning,
                    bms_parse_diagnostic_code_t::invalid_number,
                    "BPM declaration is not a positive exact number; raw text is retained");
                continue;
            }
            if (token == "BPM") {
                if (initial_bpm.has_value()) {
                    line_diagnostic(
                        *node, diagnostic_severity_t::warning,
                        bms_parse_diagnostic_code_t::invalid_number,
                        "duplicate direct BPM declaration; later valid value is applied");
                }
                initial_bpm = std::pair{node, *parsed_value};
            } else {
                const auto identifier = token.substr(3, 2);
                if (extended_bpms.contains(identifier)) {
                    line_diagnostic(
                        *node, diagnostic_severity_t::warning,
                        bms_parse_diagnostic_code_t::invalid_number,
                        "duplicate extended BPM identifier; later valid value is applied");
                }
                extended_bpms.insert_or_assign(identifier, *parsed_value);
            }
            continue;
        }
        if (token.size() == 6 && token.starts_with("STOP") && base36_digit(token[4]) &&
            base36_digit(token[5])) {
            const auto parsed_value = parse_positive_rational(value);
            if (!parsed_value.has_value()) {
                line_diagnostic(
                    *node, diagnostic_severity_t::warning,
                    bms_parse_diagnostic_code_t::invalid_number,
                    "STOP declaration is not a positive exact number; raw text is retained");
                continue;
            }
            const auto identifier = token.substr(4, 2);
            if (stop_definitions.contains(identifier)) {
                line_diagnostic(*node, diagnostic_severity_t::warning,
                                bms_parse_diagnostic_code_t::invalid_number,
                                "duplicate STOP identifier; later valid value is applied");
            }
            stop_definitions.insert_or_assign(identifier, *parsed_value);
            continue;
        }

        if (!measure_command.has_value() && is_safe_bms_header_token(token)) {
            // Retain syntactically safe dialect extensions and custom metadata even
            // before they acquire a normalized core field. Known timing/resource
            // commands have already been consumed above.
            ordered_metadata_fields.emplace_back(token, node->raw_value);
            ordered_metadata_source_orders.push_back(node->source_order);
            continue;
        }

        if (!measure_command.has_value() || node->original_text.size() < 7 ||
            node->original_text[0] != '#' || node->original_text[6] != ':') {
            continue;
        }
        const auto& [measure_number, channel] = *measure_command;
        if (channel == "02") {
            continue;
        }
        const auto payload = value;
        const auto valid_payload =
            !payload.empty() && payload.size() % 2 == 0 &&
            std::ranges::all_of(payload, [](char character) { return base36_digit(character); });
        if (!valid_payload) {
            continue;
        }
        channel_lines.push_back({
            .node = node,
            .measure = measure_number,
            .channel = channel,
            .payload = std::string(payload),
            .pair_count = payload.size() / 2,
        });
        if (is_playable_channel(channel)) {
            used_playable_channels.push_back(channel);
        }
    }
    std::ranges::stable_sort(channel_lines, {},
                             [](const auto& line) { return line.node->source_order; });

    chart.metadata.name = song.metadata.title;
    chart.metadata.author = song.metadata.artist;

    auto measure_length_source = [&](std::size_t measure_index) -> const bms_control_node_t* {
        const auto order = last_measure_length_order.find(measure_index);
        if (order == last_measure_length_order.end()) {
            return nullptr;
        }
        const auto found = std::ranges::find_if(active_statements, [&](const auto* node) {
            return node->source_order == order->second;
        });
        return found == active_statements.end() ? nullptr : *found;
    };
    std::set<std::size_t> inexact_measure_geometry;
    auto fallback_measure_length = [&](std::size_t measure_index, std::string message) {
        inexact_measure_geometry.insert(measure_index);
        auto& measure = chart.measures[measure_index];
        measure.definition = {{1, 1}, meter_t{4, 4}};
        if (const auto* node = measure_length_source(measure_index); node != nullptr) {
            line_diagnostic(*node, diagnostic_severity_t::warning,
                            bms_parse_diagnostic_code_t::invalid_measure, std::move(message));
        } else {
            result.diagnostics.push_back({
                .severity = diagnostic_severity_t::warning,
                .code = bms_parse_diagnostic_code_t::invalid_measure,
                .message = std::move(message),
                .line = 1,
                .column = 1,
            });
        }
    };

    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        const auto required = checked_lcm(chart.divisions_per_standard_measure,
                                          chart.measures[index].definition.length.denominator);
        if (!required.has_value() || *required > options.max_chart_resolution) {
            fallback_measure_length(
                index,
                "measure-length denominator exceeds the configured exact chart-resolution limit; "
                "the measure remains raw and uses its default effective length");
            continue;
        }
        chart.divisions_per_standard_measure = *required;
    }
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        const auto capacity = validate_measure(chart.divisions_per_standard_measure,
                                               chart.measures[index].definition);
        if (!capacity.row_capacity.has_value()) {
            fallback_measure_length(
                index,
                "measure length cannot be represented safely at the selected chart resolution; "
                "the measure remains raw and uses its default effective length");
        }
    }

    std::optional<lane_input_profile_t> selected_profile;
    if (!used_playable_channels.empty() || options.lane_profile_id.has_value()) {
        result.lane_profile_detection = detect_bms_lane_profile({
            .file_extension = options.file_extension,
            .player_header = player_header,
            .octave_fp_header = octave_fp_header,
            .used_channels = used_playable_channels,
            .explicit_profile_id = options.lane_profile_id,
        });
        selected_profile = result.lane_profile_detection->selected_profile;
        if (options.lane_profile_id.has_value() && !selected_profile.has_value()) {
            std::string message = "selected BMS lane profile is unknown or does not map every "
                                  "recognized playable channel; channel payloads remain raw";
            if (!result.lane_profile_detection->evidence.empty()) {
                message += ": ";
                message += result.lane_profile_detection->evidence.front().message;
            }
            const auto first_playable = std::ranges::find_if(
                channel_lines, [](const auto& line) { return is_playable_channel(line.channel); });
            if (first_playable != channel_lines.end()) {
                line_diagnostic(*first_playable->node, diagnostic_severity_t::warning,
                                bms_parse_diagnostic_code_t::unsupported_channel,
                                std::move(message));
            } else {
                result.diagnostics.push_back({
                    .severity = diagnostic_severity_t::warning,
                    .code = bms_parse_diagnostic_code_t::unsupported_channel,
                    .message = std::move(message),
                    .line = 1,
                    .column = 1,
                });
            }
        }
        if (selected_profile.has_value()) {
            chart.input_profile = selected_profile->input;
            chart.lane_layouts.push_back({
                .id = new_id<object_id_t>(ids),
                .position = {chart.measures.front().id, 0},
                .layout = selected_profile->layout,
            });
        }
    }

    std::vector<std::string> unmaterialized_long_note_channels;
    for (auto& channel_line : channel_lines) {
        const auto channel = std::string_view(channel_line.channel);
        const auto is_builtin_channel = channel == "01" || channel == "03" || channel == "08" ||
                                        channel == "09" || channel == "04" || channel == "06" ||
                                        channel == "07" || channel == "0A";
        if (is_builtin_channel) {
            channel_line.materialize = true;
        } else if (is_playable_channel(channel)) {
            if (!selected_profile.has_value()) {
                line_diagnostic(*channel_line.node, diagnostic_severity_t::warning,
                                bms_parse_diagnostic_code_t::unsupported_channel,
                                options.lane_profile_id.has_value()
                                    ? "playable-family payload is incompatible with the selected "
                                      "profile and remains raw"
                                    : "playable-family payload remains raw until a lane profile is "
                                      "selected");
                continue;
            }
            channel_line.materialize = true;
        } else {
            continue;
        }

        const auto required = resolution_for_slots(chart.divisions_per_standard_measure,
                                                   chart.measures[channel_line.measure].definition,
                                                   channel_line.pair_count);
        if (!required.has_value() || *required > options.max_chart_resolution) {
            channel_line.materialize = false;
            if (is_long_note_channel(channel)) {
                unmaterialized_long_note_channels.push_back(channel_line.channel);
            }
            line_diagnostic(*channel_line.node, diagnostic_severity_t::warning,
                            bms_parse_diagnostic_code_t::invalid_measure,
                            "exact channel subdivisions exceed the configured chart-resolution "
                            "limit; payload remains raw");
            continue;
        }
        chart.divisions_per_standard_measure = *required;
    }

    auto lane_for_channel = [&](std::string_view channel) -> const lane_definition_t* {
        if (!selected_profile.has_value()) {
            return nullptr;
        }
        const auto found = std::ranges::find_if(
            selected_profile->layout.lanes, [channel](const lane_definition_t& lane) {
                return (lane.bms_channels.visible.has_value() &&
                        *lane.bms_channels.visible == channel) ||
                       (lane.bms_channels.invisible.has_value() &&
                        *lane.bms_channels.invisible == channel) ||
                       (lane.bms_channels.long_note.has_value() &&
                        *lane.bms_channels.long_note == channel) ||
                       (lane.bms_channels.mine.has_value() && *lane.bms_channels.mine == channel);
            });
        return found == selected_profile->layout.lanes.end() ? nullptr : &*found;
    };

    std::set<std::uint64_t> incomplete_long_note_lanes;
    for (const auto& channel : unmaterialized_long_note_channels) {
        if (const auto* lane = lane_for_channel(channel); lane != nullptr) {
            incomplete_long_note_lanes.insert(lane->id.value);
        }
    }

    auto position_for_slot = [&](const root_channel_line_t& channel_line,
                                 std::size_t slot) -> std::optional<local_position_t> {
        const auto& measure = chart.measures[channel_line.measure];
        const auto capacity =
            validate_measure(chart.divisions_per_standard_measure, measure.definition);
        if (!capacity.row_capacity.has_value() || channel_line.pair_count == 0 ||
            *capacity.row_capacity % channel_line.pair_count != 0) {
            return std::nullopt;
        }
        const auto rows_per_slot = *capacity.row_capacity / channel_line.pair_count;
        return local_position_t{
            .measure = measure.id,
            .row = static_cast<std::uint64_t>(slot) * rows_per_slot,
        };
    };

    auto resource_for_object = [&](const bms_control_node_t& node,
                                   std::string_view object_id) -> std::optional<resource_id_t> {
        const auto key = std::string("WAV") + std::string(object_id);
        const auto found = resource_ids.find(key);
        if (found != resource_ids.end()) {
            return found->second;
        }
        line_diagnostic(
            node, diagnostic_severity_t::warning, bms_parse_diagnostic_code_t::unresolved_reference,
            "channel object references an undeclared " + key + "; semantic event remains unbound");
        return std::nullopt;
    };

    // Unlike sound references, a BGA event cannot be represented without a resource ID.
    // Keep undefined WAV references unbound, but create a missing BMP catalog entry so the
    // visual event remains editable and can be relinked later. The raw channel statement is
    // still retained in the BMS control tree in either case.
    auto bga_resource_for_object = [&](const bms_control_node_t& node,
                                       std::string_view object_id) -> resource_id_t {
        const auto key = std::string("BMP") + std::string(object_id);
        if (const auto found = resource_ids.find(key); found != resource_ids.end()) {
            return found->second;
        }

        const auto asset_id = new_id<asset_id_t>(ids);
        const auto resource_id = new_id<resource_id_t>(ids);
        const auto normalized_id = upper_ascii(object_id);
        const auto missing_name = "Missing BMS BMP " + normalized_id;
        song.visual_assets.push_back({
            .id = asset_id,
            .kind = asset_kind_t::image,
            .source_path = {},
        });
        song.resources.push_back({
            .id = resource_id,
            .name = missing_name,
            .asset = asset_id,
            .roles = {resource_role_t::bga},
            .target_identities = {{format_family_t::bms, normalized_id, "BMP"}},
            .missing = true,
            .provenance = source_provenance(key + " (missing declaration)", node.original_text,
                                            node.source_order),
        });
        resource_ids.emplace(key, resource_id);
        line_diagnostic(node, diagnostic_severity_t::warning,
                        bms_parse_diagnostic_code_t::unresolved_reference,
                        "BGA channel references undeclared " + key +
                            "; a missing visual resource placeholder was created");
        return resource_id;
    };

    if (initial_bpm.has_value()) {
        chart.bpms.push_back({
            .id = new_id<object_id_t>(ids),
            .position = {chart.measures.front().id, 0},
            .beats_per_minute = initial_bpm->second,
            .provenance = source_provenance("BPM", initial_bpm->first->original_text,
                                            initial_bpm->first->source_order),
        });
    }

    struct long_note_endpoint_t {
        const root_channel_line_t* line{};
        local_position_t position;
        std::size_t slot{};
        std::string object_id;
    };
    std::map<std::uint64_t, std::vector<long_note_endpoint_t>> long_note_endpoints;

    for (const auto& channel_line : channel_lines) {
        if (!channel_line.materialize) {
            continue;
        }
        const auto channel = std::string_view(channel_line.channel);
        const auto& node = *channel_line.node;
        std::optional<object_id_t> bgm_source_lane;
        if (channel == "01") {
            const auto lane_id = new_id<object_id_t>(ids);
            bgm_source_lane = lane_id;
            chart.bgm_source_lanes.push_back({
                .id = lane_id,
                .name = "BGM source #" + channel_line.node->command_token + " (line " +
                        std::to_string(node.source_order + 1) + ")",
                .source_order = node.source_order,
            });
        }

        for (std::size_t slot = 0; slot < channel_line.pair_count; ++slot) {
            const auto object_id =
                upper_ascii(std::string_view(channel_line.payload).substr(slot * 2, 2));
            const auto position = position_for_slot(channel_line, slot);
            if (!position.has_value()) {
                line_diagnostic(node, diagnostic_severity_t::warning,
                                bms_parse_diagnostic_code_t::invalid_measure,
                                "channel slot cannot be represented exactly at the selected "
                                "chart resolution; payload remains raw");
                break;
            }

            if (channel == "01") {
                std::optional<resource_id_t> sound;
                if (object_id != "00") {
                    sound = resource_for_object(node, object_id);
                }
                chart.measures[channel_line.measure].bgm_events.push_back({
                    .id = new_id<object_id_t>(ids),
                    .position = *position,
                    .source_lane = *bgm_source_lane,
                    .sound = sound,
                    .structural_slot = slot,
                    .provenance = source_provenance("01 slot " + std::to_string(slot) + " object " +
                                                        object_id,
                                                    node.original_text, node.source_order),
                });
                continue;
            }

            if (channel == "04" || channel == "06" || channel == "07" || channel == "0A") {
                if (object_id == "00") {
                    continue;
                }
                const auto kind = channel == "06" ? visual_event_kind_t::poor
                                  : channel == "07" || channel == "0A"
                                      ? visual_event_kind_t::layer
                                      : visual_event_kind_t::background;
                chart.bga_events.push_back({
                    .id = new_id<object_id_t>(ids),
                    .position = *position,
                    .resource = bga_resource_for_object(node, object_id),
                    .kind = kind,
                    .provenance =
                        source_provenance(std::string(channel) + " slot " + std::to_string(slot) +
                                              " object " + object_id,
                                          node.original_text, node.source_order),
                });
                continue;
            }

            if (channel == "03") {
                if (object_id == "00") {
                    continue;
                }
                unsigned int bpm_value{};
                const auto parsed_bpm = std::from_chars(
                    object_id.data(), object_id.data() + object_id.size(), bpm_value, 16);
                if (parsed_bpm.ec != std::errc{} ||
                    parsed_bpm.ptr != object_id.data() + object_id.size() || bpm_value == 0) {
                    line_diagnostic(node, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::invalid_number,
                                    "direct BPM object is not a positive hexadecimal byte; raw "
                                    "slot is retained");
                    continue;
                }
                chart.bpms.push_back({
                    .id = new_id<object_id_t>(ids),
                    .position = *position,
                    .beats_per_minute = {static_cast<std::int64_t>(bpm_value), 1},
                    .provenance =
                        source_provenance("03 slot " + std::to_string(slot) + " value " + object_id,
                                          node.original_text, node.source_order),
                });
                continue;
            }

            if (channel == "08") {
                if (object_id == "00") {
                    continue;
                }
                const auto found = extended_bpms.find(object_id);
                if (found == extended_bpms.end()) {
                    line_diagnostic(node, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::unresolved_reference,
                                    "extended BPM object references undeclared BPM" + object_id +
                                        "; source slot remains raw");
                    continue;
                }
                chart.bpms.push_back({
                    .id = new_id<object_id_t>(ids),
                    .position = *position,
                    .beats_per_minute = found->second,
                    .provenance =
                        source_provenance("08 slot " + std::to_string(slot) + " BPM" + object_id,
                                          node.original_text, node.source_order),
                });
                continue;
            }

            if (channel == "09") {
                if (object_id == "00") {
                    continue;
                }
                const auto found = stop_definitions.find(object_id);
                if (found == stop_definitions.end()) {
                    line_diagnostic(node, diagnostic_severity_t::warning,
                                    bms_parse_diagnostic_code_t::unresolved_reference,
                                    "STOP object references undeclared STOP" + object_id +
                                        "; source slot remains raw");
                    continue;
                }
                chart.pauses.push_back({
                    .id = new_id<object_id_t>(ids),
                    .position = *position,
                    .kind = pause_kind_t::stop,
                    .duration = found->second,
                    .source_unit = pause_unit_t::bms_stop_ticks,
                    .provenance =
                        source_provenance("09 slot " + std::to_string(slot) + " STOP" + object_id,
                                          node.original_text, node.source_order),
                });
                continue;
            }

            if (!is_materialized_note_family(channel)) {
                continue;
            }
            const auto* lane = lane_for_channel(channel);
            if (lane == nullptr) {
                line_diagnostic(node, diagnostic_severity_t::warning,
                                bms_parse_diagnostic_code_t::unsupported_channel,
                                "selected profile has no semantic lane for this channel; source "
                                "payload remains raw");
                continue;
            }
            if (object_id == "00") {
                continue;
            }

            if (is_long_note_channel(channel)) {
                long_note_endpoints[lane->id.value].push_back({
                    .line = &channel_line,
                    .position = *position,
                    .slot = slot,
                    .object_id = object_id,
                });
                continue;
            }

            auto kind = note_kind_t::tap;
            const auto visible =
                lane->bms_channels.visible.has_value() && *lane->bms_channels.visible == channel;
            const auto invisible = lane->bms_channels.invisible.has_value() &&
                                   *lane->bms_channels.invisible == channel;
            const auto mine =
                lane->bms_channels.mine.has_value() && *lane->bms_channels.mine == channel;
            if (invisible) {
                kind = note_kind_t::invisible;
            } else if (mine) {
                kind = note_kind_t::mine;
            } else if (!visible) {
                line_diagnostic(node, diagnostic_severity_t::warning,
                                bms_parse_diagnostic_code_t::unsupported_channel,
                                "selected profile does not identify the note-family meaning of " +
                                    std::string(channel) + "; source payload remains raw");
                continue;
            }

            std::optional<resource_id_t> sound;
            if (kind != note_kind_t::mine) {
                sound = resource_for_object(node, object_id);
            }
            chart.measures[channel_line.measure].notes.push_back({
                .id = new_id<object_id_t>(ids),
                .position = *position,
                .lane = lane->id,
                .kind = kind,
                .sound = sound,
                .provenance = source_provenance(
                    std::string(channel) + " slot " + std::to_string(slot) + " object " + object_id,
                    node.original_text, node.source_order, selected_profile->layout.profile_id),
            });
        }
    }

    for (auto& [lane_value, endpoints] : long_note_endpoints) {
        if (incomplete_long_note_lanes.contains(lane_value)) {
            continue;
        }
        std::ranges::stable_sort(endpoints, [](const auto& left, const auto& right) {
            return std::tie(left.line->measure, left.position.row, left.line->node->source_order,
                            left.slot) < std::tie(right.line->measure, right.position.row,
                                                  right.line->node->source_order, right.slot);
        });
        for (std::size_t index = 0; index + 1 < endpoints.size(); index += 2) {
            const auto& start = endpoints[index];
            const auto& end = endpoints[index + 1];
            const auto crosses_inexact_measure =
                std::ranges::any_of(inexact_measure_geometry, [&](const auto measure) {
                    return measure >= start.line->measure && measure <= end.line->measure;
                });
            if (crosses_inexact_measure) {
                line_diagnostic(*start.line->node, diagnostic_severity_t::warning,
                                bms_parse_diagnostic_code_t::unpaired_long_note,
                                "long-note endpoints cross inexact fallback measure geometry and "
                                "remain raw");
                continue;
            }
            if (std::tie(end.line->measure, end.position.row) <=
                std::tie(start.line->measure, start.position.row)) {
                line_diagnostic(*end.line->node, diagnostic_severity_t::warning,
                                bms_parse_diagnostic_code_t::unpaired_long_note,
                                "long-note endpoints share a position and remain raw because a "
                                "positive-length hold cannot be represented");
                continue;
            }

            const auto* lane = lane_for_channel(start.line->channel);
            if (lane == nullptr) {
                continue;
            }
            const auto sound = resource_for_object(*start.line->node, start.object_id);
            const auto end_line = end.line->node->source_order + 1;
            const auto source_field = std::string("long-note start ") + start.line->channel +
                                      " slot " + std::to_string(start.slot) + " object " +
                                      start.object_id + " paired with " + end.line->channel +
                                      " slot " + std::to_string(end.slot) + " object " +
                                      end.object_id + " on source line " + std::to_string(end_line);
            const auto source_spelling =
                start.line->node->original_text + "\n" + end.line->node->original_text;
            chart.measures[start.line->measure].notes.push_back({
                .id = new_id<object_id_t>(ids),
                .position = start.position,
                .lane = lane->id,
                .kind = note_kind_t::hold,
                .end = end.position,
                .sound = sound,
                .provenance =
                    source_provenance(source_field, source_spelling, start.line->node->source_order,
                                      selected_profile->layout.profile_id),
            });
        }
        if (endpoints.size() % 2 != 0) {
            const auto& endpoint = endpoints.back();
            line_diagnostic(*endpoint.line->node, diagnostic_severity_t::warning,
                            bms_parse_diagnostic_code_t::unpaired_long_note,
                            "long-note endpoint has no matching partner; its source payload "
                            "remains raw");
        }
    }

    std::ranges::stable_sort(chart.bpms, {},
                             [](const auto& event) { return event.provenance.source_order; });

    chart.bms_bpm_projections.reserve(chart.bpms.size());
    for (const auto& event : chart.bpms) {
        chart.bms_bpm_projections.push_back({
            .event = event.id,
            .source_order = event.provenance.source_order,
            .imported_position = event.position,
            .imported_beats_per_minute = event.beats_per_minute,
        });
    }
    chart.bms_pause_projections.reserve(chart.pauses.size());
    for (const auto& event : chart.pauses) {
        chart.bms_pause_projections.push_back({
            .event = event.id,
            .source_order = event.provenance.source_order,
            .imported_position = event.position,
            .imported_kind = event.kind,
            .imported_duration = event.duration,
            .imported_source_unit = event.source_unit,
        });
    }

    chart.metadata.name = song.metadata.title;
    chart.metadata.author = song.metadata.artist;
    chart.metadata.format_groups.push_back({
        .family = format_family_t::bms,
        .encoding = result.encoding,
    });
    chart.bms_measure_length_projections.reserve(chart.measures.size());
    for (std::size_t measure_index = 0; measure_index < chart.measures.size(); ++measure_index) {
        const auto source = last_measure_length_order.find(measure_index);
        chart.bms_measure_length_projections.push_back({
            .measure_index = measure_index,
            .source_order = source == last_measure_length_order.end()
                                ? std::numeric_limits<std::size_t>::max()
                                : source->second,
            .imported_length = chart.measures[measure_index].definition.length,
        });
    }
    song.metadata.format_groups.push_back({
        .family = format_family_t::bms,
        .ordered_fields = std::move(ordered_metadata_fields),
        .ordered_field_source_orders = std::move(ordered_metadata_source_orders),
        .encoding = result.encoding,
    });
    song.charts.push_back(std::move(chart));
    result.song = std::move(song);
    return result;
}

auto bms_branch_statement_edit_result_t::has_errors() const -> bool {
    return std::ranges::any_of(diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto edit_bms_inactive_branch_statement(const song_document_t& song,
                                        bms_branch_statement_edit_t edit)
    -> bms_branch_statement_edit_result_t {
    bms_branch_statement_edit_result_t result;
    const auto reject = [&](bms_branch_statement_edit_diagnostic_code_t code, std::string message,
                            std::string path) {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, code, std::move(message), std::move(path)});
    };

    const chart_document_t* chart{};
    std::size_t chart_index{};
    if (edit.chart.has_value()) {
        const auto found = std::ranges::find(song.charts, *edit.chart, &chart_document_t::id);
        if (found == song.charts.end()) {
            reject(bms_branch_statement_edit_diagnostic_code_t::missing_chart,
                   "requested chart ID does not exist", "$.charts");
            return result;
        }
        chart_index = static_cast<std::size_t>(found - song.charts.begin());
        chart = &*found;
    } else if (song.charts.size() == 1) {
        chart = &song.charts.front();
    } else {
        reject(bms_branch_statement_edit_diagnostic_code_t::missing_chart,
               "select one chart explicitly when the song does not contain exactly one chart",
               "$.charts");
        return result;
    }
    const auto path = "$.charts[" + std::to_string(chart_index) + "].bms_control_tree";
    if (!chart->bms_control_tree.has_value()) {
        reject(bms_branch_statement_edit_diagnostic_code_t::missing_control_tree,
               "the selected chart has no retained BMS control tree", path);
        return result;
    }
    if (!edit.statement) {
        reject(bms_branch_statement_edit_diagnostic_code_t::target_not_found,
               "the statement ID is invalid", path);
        return result;
    }
    if (edit.replacement_text.size() > edit.max_replacement_bytes) {
        reject(bms_branch_statement_edit_diagnostic_code_t::resource_limit,
               "replacement statement exceeds the configured byte limit", path);
        return result;
    }

    const bms_control_node_t* target{};
    std::size_t target_count{};
    std::size_t source_order_count{};
    bool target_is_branch_scoped{};
    const auto find_target = [&](const auto& self, const bms_control_node_t& node,
                                 bool inside_branch) -> void {
        if (node.id == edit.statement) {
            ++target_count;
            target = &node;
            target_is_branch_scoped = inside_branch;
        }
        if (node.kind != bms_control_kind_t::root && node.kind != bms_control_kind_t::if_group &&
            node.source_order == edit.expected_source_order) {
            ++source_order_count;
        }
        const auto descendants_inside_branch = inside_branch || is_branch_boundary(node.kind);
        for (const auto& child : node.children) {
            self(self, child, descendants_inside_branch);
        }
    };
    find_target(find_target, *chart->bms_control_tree, false);
    if (target_count == 0) {
        reject(bms_branch_statement_edit_diagnostic_code_t::target_not_found,
               "the retained tree does not contain the requested statement ID", path);
        return result;
    }
    if (target_count != 1 || source_order_count != 1) {
        reject(bms_branch_statement_edit_diagnostic_code_t::ambiguous_target,
               "the statement ID or source order is not unique in the retained tree", path);
        return result;
    }
    if (target->kind != bms_control_kind_t::raw_statement || !target_is_branch_scoped) {
        reject(bms_branch_statement_edit_diagnostic_code_t::target_not_branch_statement,
               "only raw statements inside a retained branch body can be replaced", path);
        return result;
    }
    if (target->source_order != edit.expected_source_order ||
        target->source_span.byte_begin != edit.expected_source_span.byte_begin ||
        target->source_span.byte_end != edit.expected_source_span.byte_end ||
        target->original_text != edit.expected_original_text) {
        reject(bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch,
               "the statement no longer matches the caller's retained source identity", path);
        return result;
    }

    const auto active_orders =
        active_branch_statement_orders(*chart->bms_control_tree, chart->active_branch_selection);
    if (!active_orders.has_value()) {
        reject(bms_branch_statement_edit_diagnostic_code_t::invalid_branch_selection,
               "the chart's active branch selections do not resolve against this control tree",
               path);
        return result;
    }
    if (active_orders->contains(target->source_order)) {
        reject(bms_branch_statement_edit_diagnostic_code_t::active_statement,
               "active branch statements must be edited through their materialized semantic "
               "objects",
               path);
        return result;
    }

    const auto source = retained_bms_source(song, *chart);
    const auto retained_text =
        source.has_value() ? retained_line_text(*source, *target) : std::optional<std::string>{};
    const auto current = parse_line(target->original_text);
    const auto retained = retained_text.has_value() ? parse_line(*retained_text) : parsed_line_t{};
    const auto replacement = parse_line(edit.replacement_text);
    if (!source.has_value() || !retained_text.has_value() || current.token.empty() ||
        current.token != target->command_token || current.raw_value != target->raw_value ||
        retained.token.empty() ||
        !equal_ascii_case_insensitive(retained.token, target->command_token)) {
        reject(bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch,
               "the statement cannot be verified against its retained source bytes", path);
        return result;
    }
    if (edit.replacement_text.empty() || edit.replacement_text.front() != '#' ||
        edit.replacement_text.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
        utf8_error_offset(edit.replacement_text).has_value() || replacement.token.empty() ||
        !equal_ascii_case_insensitive(replacement.token, target->command_token) ||
        is_control_token(replacement.token)) {
        reject(bms_branch_statement_edit_diagnostic_code_t::invalid_replacement,
               "replacement must be one valid UTF-8 BMS statement with the same non-control "
               "command token",
               path);
        return result;
    }

    result.song = song;
    auto& edited_chart = result.song->charts[chart_index];
    bms_control_node_t* edited_target{};
    const auto find_mutable = [&](const auto& self, bms_control_node_t& node) -> void {
        if (node.id == edit.statement) {
            edited_target = &node;
        }
        for (auto& child : node.children) {
            self(self, child);
        }
    };
    find_mutable(find_mutable, *edited_chart.bms_control_tree);
    edited_target->original_text = std::move(edit.replacement_text);
    edited_target->command_token = replacement.token;
    edited_target->raw_value = replacement.raw_value;

    const auto mark_changed = [&](std::vector<format_metadata_t>& groups) {
        for (auto& group : groups) {
            if (group.family == format_family_t::bms && group.encoding.original_bytes &&
                group.encoding.original_bytes.get() == source->bytes.get()) {
                group.encoding.unchanged_since_import = false;
            }
        }
    };
    mark_changed(result.song->metadata.format_groups);
    mark_changed(edited_chart.metadata.format_groups);
    return result;
}

auto insert_bms_inactive_branch_statement(const song_document_t& song,
                                          bms_branch_statement_insert_t insertion)
    -> bms_branch_statement_edit_result_t {
    bms_branch_statement_edit_result_t result;
    const auto reject = [&](bms_branch_statement_edit_diagnostic_code_t code, std::string message,
                            std::string path) {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, code, std::move(message), std::move(path)});
    };

    const chart_document_t* chart{};
    std::size_t chart_index{};
    if (song.charts.size() != 1) {
        reject(bms_branch_statement_edit_diagnostic_code_t::unsupported_song_shape,
               "inactive branch insertion currently requires a one-chart BMS source document",
               "$.charts");
        return result;
    }
    if (insertion.chart.has_value()) {
        const auto found = std::ranges::find(song.charts, *insertion.chart, &chart_document_t::id);
        if (found == song.charts.end()) {
            reject(bms_branch_statement_edit_diagnostic_code_t::missing_chart,
                   "requested chart ID does not exist", "$.charts");
            return result;
        }
        chart_index = static_cast<std::size_t>(found - song.charts.begin());
        chart = &*found;
    } else if (song.charts.size() == 1) {
        chart = &song.charts.front();
    } else {
        reject(bms_branch_statement_edit_diagnostic_code_t::missing_chart,
               "select one chart explicitly when the song does not contain exactly one chart",
               "$.charts");
        return result;
    }
    const auto path = "$.charts[" + std::to_string(chart_index) + "].bms_control_tree";
    if (!chart->bms_control_tree.has_value()) {
        reject(bms_branch_statement_edit_diagnostic_code_t::missing_control_tree,
               "the selected chart has no retained BMS control tree", path);
        return result;
    }
    if (!insertion.leaf_branch || !insertion.after_statement) {
        reject(bms_branch_statement_edit_diagnostic_code_t::target_not_found,
               "the leaf branch and insertion anchor IDs must be valid", path);
        return result;
    }
    if (insertion.statement_text.size() > insertion.max_statement_bytes) {
        reject(bms_branch_statement_edit_diagnostic_code_t::resource_limit,
               "inserted statement exceeds the configured byte limit", path);
        return result;
    }

    const auto is_leaf_branch = [](bms_control_kind_t kind) {
        return kind == bms_control_kind_t::if_branch ||
               kind == bms_control_kind_t::else_if_branch ||
               kind == bms_control_kind_t::else_branch || kind == bms_control_kind_t::case_branch ||
               kind == bms_control_kind_t::default_branch;
    };
    const bms_control_node_t* leaf{};
    const bms_control_node_t* anchor{};
    const bms_control_node_t* anchor_parent{};
    std::size_t leaf_id_count{};
    std::size_t anchor_id_count{};
    std::size_t leaf_order_count{};
    std::size_t anchor_order_count{};
    std::uint64_t max_branch_id{};
    std::size_t max_source_order{};
    const auto find_targets = [&](const auto& self, const bms_control_node_t& node) -> void {
        max_branch_id = std::max(max_branch_id, node.id.value);
        max_source_order = std::max(max_source_order, node.source_order);
        if (node.close_source_order.has_value()) {
            max_source_order = std::max(max_source_order, *node.close_source_order);
        }
        if (node.id == insertion.leaf_branch) {
            ++leaf_id_count;
            leaf = &node;
        }
        if (node.id == insertion.after_statement) {
            ++anchor_id_count;
            anchor = &node;
        }
        if (node.kind != bms_control_kind_t::root && node.kind != bms_control_kind_t::if_group) {
            if (node.source_order == insertion.expected_leaf_source_order) {
                ++leaf_order_count;
            }
            if (node.source_order == insertion.expected_statement_source_order) {
                ++anchor_order_count;
            }
        }
        for (const auto& child : node.children) {
            if (child.id == insertion.after_statement) {
                anchor_parent = &node;
            }
            self(self, child);
        }
    };
    find_targets(find_targets, *chart->bms_control_tree);
    if (leaf_id_count == 0 || anchor_id_count == 0) {
        reject(bms_branch_statement_edit_diagnostic_code_t::target_not_found,
               "the retained tree does not contain the requested leaf or insertion anchor", path);
        return result;
    }
    if (leaf_id_count != 1 || anchor_id_count != 1 || leaf_order_count != 1 ||
        anchor_order_count != 1) {
        reject(bms_branch_statement_edit_diagnostic_code_t::ambiguous_target,
               "the leaf, anchor ID, or source order is not unique in the retained tree", path);
        return result;
    }
    if (!is_leaf_branch(leaf->kind) || leaf != anchor_parent ||
        anchor->kind != bms_control_kind_t::raw_statement) {
        reject(bms_branch_statement_edit_diagnostic_code_t::target_not_branch_statement,
               "insertion requires a raw statement directly inside the identified leaf branch",
               path);
        return result;
    }
    if (leaf->source_order != insertion.expected_leaf_source_order ||
        leaf->source_span.byte_begin != insertion.expected_leaf_source_span.byte_begin ||
        leaf->source_span.byte_end != insertion.expected_leaf_source_span.byte_end ||
        leaf->original_text != insertion.expected_leaf_original_text ||
        anchor->source_order != insertion.expected_statement_source_order ||
        anchor->source_span.byte_begin != insertion.expected_statement_source_span.byte_begin ||
        anchor->source_span.byte_end != insertion.expected_statement_source_span.byte_end ||
        anchor->original_text != insertion.expected_statement_original_text) {
        reject(bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch,
               "the leaf or anchor no longer matches the caller's retained source identity", path);
        return result;
    }

    const auto source = retained_bms_source(song, *chart);
    const auto retained_leaf =
        source.has_value() ? retained_line_text(*source, *leaf) : std::optional<std::string>{};
    const auto retained_anchor =
        source.has_value() ? retained_line_text(*source, *anchor) : std::optional<std::string>{};
    const auto parsed_leaf = parse_line(leaf->original_text);
    const auto parsed_anchor = parse_line(anchor->original_text);
    if (!source.has_value() || !retained_leaf.has_value() || !retained_anchor.has_value() ||
        *retained_leaf != leaf->original_text || *retained_anchor != anchor->original_text ||
        parsed_leaf.token.empty() || parsed_anchor.token.empty() ||
        !equal_ascii_case_insensitive(parsed_leaf.token, leaf->command_token) ||
        !equal_ascii_case_insensitive(parsed_anchor.token, anchor->command_token) ||
        parsed_anchor.raw_value != anchor->raw_value) {
        reject(bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch,
               "the leaf and anchor cannot be verified against their retained source bytes", path);
        return result;
    }

    const auto active_orders =
        active_branch_statement_orders(*chart->bms_control_tree, chart->active_branch_selection);
    if (!active_orders.has_value()) {
        reject(bms_branch_statement_edit_diagnostic_code_t::invalid_branch_selection,
               "the chart's active branch selections do not resolve against this control tree",
               path);
        return result;
    }
    if (active_orders->contains(anchor->source_order)) {
        reject(bms_branch_statement_edit_diagnostic_code_t::active_statement,
               "statements cannot be inserted into the currently active branch leaf", path);
        return result;
    }

    const auto parsed_statement = parse_line(insertion.statement_text);
    if (insertion.statement_text.empty() || insertion.statement_text.front() != '#' ||
        insertion.statement_text.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
        utf8_error_offset(insertion.statement_text).has_value() || parsed_statement.token.empty() ||
        is_control_token(parsed_statement.token) ||
        !std::ranges::all_of(insertion.statement_text,
                             [](unsigned char character) { return character < 0x80; })) {
        reject(bms_branch_statement_edit_diagnostic_code_t::invalid_replacement,
               "inserted text must be one ASCII BMS statement, not a control directive", path);
        return result;
    }
    if (max_branch_id == std::numeric_limits<std::uint64_t>::max()) {
        reject(bms_branch_statement_edit_diagnostic_code_t::resource_limit,
               "the retained branch ID space is exhausted", path);
        return result;
    }
    if (anchor->source_order == std::numeric_limits<std::size_t>::max() ||
        max_source_order == std::numeric_limits<std::size_t>::max()) {
        reject(bms_branch_statement_edit_diagnostic_code_t::resource_limit,
               "the retained source-order space is exhausted", path);
        return result;
    }
    const auto line_ending = ending_bytes(anchor->line_ending);
    if (line_ending.empty()) {
        reject(bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch,
               "the insertion anchor has no retained line ending", path);
        return result;
    }
    const auto insertion_offset = anchor->source_span.byte_end;
    if (insertion_offset > source->bytes->size() ||
        insertion.statement_text.size() >
            std::numeric_limits<std::size_t>::max() - line_ending.size() ||
        insertion.statement_text.size() + line_ending.size() >
            std::numeric_limits<std::size_t>::max() - source->bytes->size()) {
        reject(bms_branch_statement_edit_diagnostic_code_t::resource_limit,
               "the inserted statement would exceed the retained source size range", path);
        return result;
    }
    const auto inserted_size = insertion.statement_text.size() + line_ending.size();

    result.song = song;
    auto& edited_chart = result.song->charts[chart_index];
    bms_control_node_t* edited_leaf{};
    const auto find_mutable_leaf = [&](const auto& self, bms_control_node_t& node) -> void {
        if (node.id == insertion.leaf_branch) {
            edited_leaf = &node;
            return;
        }
        for (auto& child : node.children) {
            self(self, child);
            if (edited_leaf != nullptr) {
                return;
            }
        }
    };
    find_mutable_leaf(find_mutable_leaf, *edited_chart.bms_control_tree);
    const auto position = std::ranges::find(edited_leaf->children, insertion.after_statement,
                                            &bms_control_node_t::id);
    auto updated_source = std::make_shared<std::vector<std::byte>>();
    updated_source->reserve(source->bytes->size() + inserted_size);
    updated_source->insert(updated_source->end(), source->bytes->begin(),
                           source->bytes->begin() + static_cast<std::ptrdiff_t>(insertion_offset));
    for (const auto character : insertion.statement_text) {
        updated_source->push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    for (const auto character : line_ending) {
        updated_source->push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    updated_source->insert(updated_source->end(),
                           source->bytes->begin() + static_cast<std::ptrdiff_t>(insertion_offset),
                           source->bytes->end());

    const auto shift_source_order = [&](std::size_t& source_order) {
        if (source_order > anchor->source_order &&
            source_order != std::numeric_limits<std::size_t>::max()) {
            ++source_order;
        }
    };
    const auto shift_span = [&](bms_source_span_t& span) {
        if (span.byte_begin >= insertion_offset) {
            span.byte_begin += inserted_size;
            span.byte_end += inserted_size;
        }
    };
    const auto shift_tree = [&](const auto& self, bms_control_node_t& node) -> void {
        if (node.kind == bms_control_kind_t::root) {
            node.source_span.byte_end += inserted_size;
        } else {
            shift_span(node.source_span);
            shift_source_order(node.source_order);
        }
        if (node.close_source_span.has_value()) {
            shift_span(*node.close_source_span);
        }
        if (node.close_source_order.has_value()) {
            shift_source_order(*node.close_source_order);
        }
        for (auto& child : node.children) {
            self(self, child);
        }
    };
    shift_tree(shift_tree, *edited_chart.bms_control_tree);

    bms_control_node_t inserted{
        .id = branch_id_t{max_branch_id + 1},
        .kind = bms_control_kind_t::raw_statement,
        .original_text = std::move(insertion.statement_text),
        .command_token = parsed_statement.token,
        .raw_value = parsed_statement.raw_value,
        .source_span = {insertion_offset, insertion_offset + inserted_size},
        .source_order = insertion.expected_statement_source_order + 1,
        .line_ending = anchor->line_ending,
    };
    edited_leaf->children.insert(position + 1, std::move(inserted));

    const auto shift_provenance = [&](source_provenance_t& provenance) {
        if (provenance.family == format_family_t::bms) {
            shift_source_order(provenance.source_order);
        }
    };
    const auto shift_group_orders = [&](std::vector<format_metadata_t>& groups) {
        for (auto& group : groups) {
            if (group.family != format_family_t::bms || !group.encoding.original_bytes ||
                group.encoding.original_bytes.get() != source->bytes.get()) {
                continue;
            }
            for (auto& source_order : group.ordered_field_source_orders) {
                shift_source_order(source_order);
            }
            group.encoding.original_bytes = updated_source;
            group.encoding.unchanged_since_import = false;
        }
    };
    shift_group_orders(result.song->metadata.format_groups);
    for (auto& resource : result.song->resources) {
        shift_provenance(resource.provenance);
    }
    shift_group_orders(edited_chart.metadata.format_groups);
    for (auto& measure : edited_chart.measures) {
        for (auto& note : measure.notes) {
            shift_provenance(note.provenance);
        }
        for (auto& event : measure.bgm_events) {
            shift_provenance(event.provenance);
        }
    }
    for (auto& lane : edited_chart.bgm_source_lanes) {
        shift_source_order(lane.source_order);
    }
    for (auto& event : edited_chart.bpms) {
        shift_provenance(event.provenance);
    }
    for (auto& event : edited_chart.pauses) {
        shift_provenance(event.provenance);
    }
    for (auto& event : edited_chart.warps) {
        shift_provenance(event.provenance);
    }
    for (auto& event : edited_chart.scrolls) {
        shift_provenance(event.provenance);
    }
    for (auto& event : edited_chart.speeds) {
        shift_provenance(event.provenance);
    }
    for (auto& event : edited_chart.kiai) {
        shift_provenance(event.provenance);
    }
    for (auto& event : edited_chart.bga_events) {
        shift_provenance(event.provenance);
    }
    for (auto& channel : edited_chart.sound_channels) {
        for (auto& event : channel.events) {
            shift_provenance(event.provenance);
        }
    }
    for (auto& projection : edited_chart.bms_measure_length_projections) {
        shift_source_order(projection.source_order);
    }
    for (auto& projection : edited_chart.bms_bpm_projections) {
        shift_source_order(projection.source_order);
    }
    for (auto& projection : edited_chart.bms_pause_projections) {
        shift_source_order(projection.source_order);
    }

    return result;
}

auto delete_bms_inactive_branch_statement(const song_document_t& song,
                                          bms_branch_statement_delete_t deletion)
    -> bms_branch_statement_edit_result_t {
    bms_branch_statement_edit_result_t result;
    const auto reject = [&](bms_branch_statement_edit_diagnostic_code_t code, std::string message,
                            std::string path) {
        result.diagnostics.push_back(
            {diagnostic_severity_t::error, code, std::move(message), std::move(path)});
    };

    const chart_document_t* chart{};
    std::size_t chart_index{};
    if (song.charts.size() != 1) {
        reject(bms_branch_statement_edit_diagnostic_code_t::unsupported_song_shape,
               "inactive branch deletion currently requires a one-chart BMS source document",
               "$.charts");
        return result;
    }
    if (deletion.chart.has_value()) {
        const auto found = std::ranges::find(song.charts, *deletion.chart, &chart_document_t::id);
        if (found == song.charts.end()) {
            reject(bms_branch_statement_edit_diagnostic_code_t::missing_chart,
                   "requested chart ID does not exist", "$.charts");
            return result;
        }
        chart_index = static_cast<std::size_t>(found - song.charts.begin());
        chart = &*found;
    } else {
        chart = &song.charts.front();
    }
    const auto path = "$.charts[" + std::to_string(chart_index) + "].bms_control_tree";
    if (!chart->bms_control_tree.has_value()) {
        reject(bms_branch_statement_edit_diagnostic_code_t::missing_control_tree,
               "the selected chart has no retained BMS control tree", path);
        return result;
    }
    if (!deletion.leaf_branch || !deletion.statement) {
        reject(bms_branch_statement_edit_diagnostic_code_t::target_not_found,
               "the leaf branch and statement IDs must be valid", path);
        return result;
    }
    if (deletion.expected_original_text.size() > deletion.max_statement_bytes) {
        reject(bms_branch_statement_edit_diagnostic_code_t::resource_limit,
               "deleted statement exceeds the configured byte limit", path);
        return result;
    }

    const auto is_leaf_branch = [](bms_control_kind_t kind) {
        return kind == bms_control_kind_t::if_branch ||
               kind == bms_control_kind_t::else_if_branch ||
               kind == bms_control_kind_t::else_branch || kind == bms_control_kind_t::case_branch ||
               kind == bms_control_kind_t::default_branch;
    };
    const bms_control_node_t* leaf{};
    const bms_control_node_t* leaf_parent{};
    const bms_control_node_t* target{};
    const bms_control_node_t* target_parent{};
    std::vector<const bms_control_node_t*> leaf_ancestors;
    std::vector<const bms_control_node_t*> path_nodes;
    std::size_t leaf_id_count{};
    std::size_t target_id_count{};
    std::size_t leaf_order_count{};
    std::size_t target_order_count{};
    const auto find_targets = [&](const auto& self, const bms_control_node_t& node,
                                  const bms_control_node_t* parent) -> void {
        path_nodes.push_back(&node);
        if (node.id == deletion.leaf_branch) {
            ++leaf_id_count;
            leaf = &node;
            leaf_parent = parent;
            leaf_ancestors = path_nodes;
        }
        if (node.id == deletion.statement) {
            ++target_id_count;
            target = &node;
            target_parent = parent;
        }
        if (node.kind != bms_control_kind_t::root && node.kind != bms_control_kind_t::if_group) {
            if (node.source_order == deletion.expected_leaf_source_order) {
                ++leaf_order_count;
            }
            if (node.source_order == deletion.expected_source_order) {
                ++target_order_count;
            }
        }
        if (node.close_source_order.has_value()) {
            if (*node.close_source_order == deletion.expected_leaf_source_order) {
                ++leaf_order_count;
            }
            if (*node.close_source_order == deletion.expected_source_order) {
                ++target_order_count;
            }
        }
        for (const auto& child : node.children) {
            self(self, child, &node);
        }
        path_nodes.pop_back();
    };
    find_targets(find_targets, *chart->bms_control_tree, nullptr);
    if (leaf_id_count == 0 || target_id_count == 0) {
        reject(bms_branch_statement_edit_diagnostic_code_t::target_not_found,
               "the retained tree does not contain the requested leaf or statement", path);
        return result;
    }
    if (leaf_id_count != 1 || target_id_count != 1 || leaf_order_count != 1 ||
        target_order_count != 1) {
        reject(bms_branch_statement_edit_diagnostic_code_t::ambiguous_target,
               "the leaf, statement ID, or source order is not unique in the retained tree", path);
        return result;
    }
    if (!is_leaf_branch(leaf->kind) || leaf_parent == nullptr ||
        (leaf->kind == bms_control_kind_t::if_branch ||
                 leaf->kind == bms_control_kind_t::else_if_branch ||
                 leaf->kind == bms_control_kind_t::else_branch
             ? leaf_parent->kind != bms_control_kind_t::if_group
             : leaf_parent->kind != bms_control_kind_t::switch_block) ||
        target->kind != bms_control_kind_t::raw_statement || target_parent != leaf) {
        reject(bms_branch_statement_edit_diagnostic_code_t::target_not_branch_statement,
               "deletion requires one raw statement directly inside the identified leaf branch",
               path);
        return result;
    }
    if (leaf->source_order != deletion.expected_leaf_source_order ||
        leaf->source_span.byte_begin != deletion.expected_leaf_source_span.byte_begin ||
        leaf->source_span.byte_end != deletion.expected_leaf_source_span.byte_end ||
        leaf->original_text != deletion.expected_leaf_original_text ||
        target->source_order != deletion.expected_source_order ||
        target->source_span.byte_begin != deletion.expected_source_span.byte_begin ||
        target->source_span.byte_end != deletion.expected_source_span.byte_end ||
        target->original_text != deletion.expected_original_text) {
        reject(bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch,
               "the leaf or statement no longer matches the caller's retained source identity",
               path);
        return result;
    }

    const auto source = retained_bms_source(song, *chart);
    if (!source.has_value()) {
        reject(bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch,
               "the statement has no retained source bytes", path);
        return result;
    }
    const auto& source_bytes = *source->bytes;
    const auto source_size = source_bytes.size();
    const auto target_span = target->source_span;
    if (target_span.byte_begin > target_span.byte_end || target_span.byte_end > source_size ||
        target_span.byte_begin == target_span.byte_end) {
        reject(bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch,
               "the statement has an invalid or empty retained byte span", path);
        return result;
    }
    const auto deleted_size = target_span.byte_end - target_span.byte_begin;
    if (deleted_size > source_size || target_span.byte_begin > source_size - deleted_size) {
        reject(bms_branch_statement_edit_diagnostic_code_t::resource_limit,
               "the statement span or source order cannot be safely compacted", path);
        return result;
    }

    const auto retained_leaf = retained_line_text(*source, *leaf);
    const auto retained_target = retained_line_text(*source, *target);
    const auto parsed_leaf = parse_line(leaf->original_text);
    const auto parsed_target = parse_line(target->original_text);
    if (!retained_leaf.has_value() || !retained_target.has_value() ||
        *retained_leaf != leaf->original_text || *retained_target != target->original_text ||
        parsed_leaf.token.empty() || parsed_target.token.empty() ||
        !equal_ascii_case_insensitive(parsed_leaf.token, leaf->command_token) ||
        !equal_ascii_case_insensitive(parsed_target.token, target->command_token) ||
        parsed_target.raw_value != target->raw_value || is_control_token(parsed_target.token)) {
        reject(bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch,
               "the leaf or statement cannot be verified against its retained source bytes", path);
        return result;
    }
    if (leaf->source_span.byte_end > target_span.byte_begin ||
        leaf->source_order >= target->source_order) {
        reject(bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch,
               "the statement is not ordered after its retained leaf opener", path);
        return result;
    }

    const auto closing_token = [](bms_control_kind_t kind) -> std::string_view {
        switch (kind) {
        case bms_control_kind_t::random:
            return "ENDRANDOM";
        case bms_control_kind_t::if_group:
            return "ENDIF";
        case bms_control_kind_t::switch_block:
            return "ENDSW";
        default:
            return {};
        }
    };
    for (const auto* ancestor : leaf_ancestors) {
        const auto expected_close = closing_token(ancestor->kind);
        if (expected_close.empty()) {
            continue;
        }
        if (!ancestor->close_source_span.has_value() || !ancestor->close_text.has_value() ||
            !ancestor->close_line_ending.has_value() || !ancestor->close_source_order.has_value()) {
            reject(bms_branch_statement_edit_diagnostic_code_t::target_not_branch_statement,
                   "deletion requires every enclosing BMS control block to be closed", path);
            return result;
        }
        auto closing_node = *ancestor;
        closing_node.source_span = *ancestor->close_source_span;
        closing_node.original_text = *ancestor->close_text;
        closing_node.line_ending = *ancestor->close_line_ending;
        const auto retained_close = retained_line_text(*source, closing_node);
        const auto parsed_close = parse_line(*ancestor->close_text);
        if (!retained_close.has_value() || *retained_close != *ancestor->close_text ||
            !equal_ascii_case_insensitive(parsed_close.token, expected_close) ||
            ancestor->close_source_span->byte_begin < target_span.byte_end ||
            *ancestor->close_source_order <= target->source_order) {
            reject(bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch,
                   "an enclosing control block does not match its retained closing statement",
                   path);
            return result;
        }
    }

    const auto active_orders =
        active_branch_statement_orders(*chart->bms_control_tree, chart->active_branch_selection);
    if (!active_orders.has_value()) {
        reject(bms_branch_statement_edit_diagnostic_code_t::invalid_branch_selection,
               "the chart's active branch selections do not resolve against this control tree",
               path);
        return result;
    }
    if (active_orders->contains(target->source_order)) {
        reject(bms_branch_statement_edit_diagnostic_code_t::active_statement,
               "active branch statements must be edited through their materialized semantic "
               "objects",
               path);
        return result;
    }

    const auto claims_deleted_order = [&](const source_provenance_t& provenance) {
        return provenance.family == format_family_t::bms &&
               provenance.source_order == target->source_order;
    };
    const auto claims_order = [&](std::size_t source_order) {
        return source_order == target->source_order;
    };
    bool has_projected_identity = std::ranges::any_of(song.resources, [&](const auto& resource) {
        return claims_deleted_order(resource.provenance);
    });
    const auto inspect_groups = [&](const std::vector<format_metadata_t>& groups) {
        return std::ranges::any_of(groups, [&](const auto& group) {
            return group.family == format_family_t::bms &&
                   std::ranges::find(group.ordered_field_source_orders, target->source_order) !=
                       group.ordered_field_source_orders.end();
        });
    };
    has_projected_identity = has_projected_identity ||
                             inspect_groups(song.metadata.format_groups) ||
                             inspect_groups(chart->metadata.format_groups);
    for (const auto& measure : chart->measures) {
        has_projected_identity =
            has_projected_identity ||
            std::ranges::any_of(
                measure.notes,
                [&](const auto& note) { return claims_deleted_order(note.provenance); }) ||
            std::ranges::any_of(measure.bgm_events, [&](const auto& event) {
                return claims_deleted_order(event.provenance);
            });
    }
    has_projected_identity = has_projected_identity ||
                             std::ranges::any_of(chart->bpms,
                                                 [&](const auto& event) {
                                                     return claims_deleted_order(event.provenance);
                                                 }) ||
                             std::ranges::any_of(chart->pauses,
                                                 [&](const auto& event) {
                                                     return claims_deleted_order(event.provenance);
                                                 }) ||
                             std::ranges::any_of(chart->warps,
                                                 [&](const auto& event) {
                                                     return claims_deleted_order(event.provenance);
                                                 }) ||
                             std::ranges::any_of(chart->scrolls,
                                                 [&](const auto& event) {
                                                     return claims_deleted_order(event.provenance);
                                                 }) ||
                             std::ranges::any_of(chart->speeds,
                                                 [&](const auto& event) {
                                                     return claims_deleted_order(event.provenance);
                                                 }) ||
                             std::ranges::any_of(chart->kiai,
                                                 [&](const auto& event) {
                                                     return claims_deleted_order(event.provenance);
                                                 }) ||
                             std::ranges::any_of(chart->bga_events, [&](const auto& event) {
                                 return claims_deleted_order(event.provenance);
                             });
    for (const auto& channel : chart->sound_channels) {
        has_projected_identity =
            has_projected_identity || std::ranges::any_of(channel.events, [&](const auto& event) {
                return claims_deleted_order(event.provenance);
            });
    }
    has_projected_identity =
        has_projected_identity ||
        std::ranges::any_of(chart->bgm_source_lanes,
                            [&](const auto& lane) { return claims_order(lane.source_order); }) ||
        std::ranges::any_of(chart->bms_measure_length_projections,
                            [&](const auto& projection) {
                                return projection.source_order !=
                                           std::numeric_limits<std::size_t>::max() &&
                                       claims_order(projection.source_order);
                            }) ||
        std::ranges::any_of(
            chart->bms_bpm_projections,
            [&](const auto& projection) { return claims_order(projection.source_order); }) ||
        std::ranges::any_of(chart->bms_pause_projections, [&](const auto& projection) {
            return claims_order(projection.source_order);
        });
    if (has_projected_identity) {
        reject(bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch,
               "the inactive statement is also claimed by a materialized BMS projection", path);
        return result;
    }

    const auto can_shift_span = [&](bms_source_span_t span, bool is_root) {
        if (span.byte_begin > span.byte_end || span.byte_end > source_size) {
            return false;
        }
        if (is_root) {
            return span.byte_begin == 0 && span.byte_end == source_size;
        }
        return span.byte_end <= target_span.byte_begin || span.byte_begin >= target_span.byte_end;
    };
    bool spans_are_safe = true;
    const auto validate_spans = [&](const auto& self, const bms_control_node_t& node) -> void {
        if (node.id != target->id &&
            !can_shift_span(node.source_span, node.kind == bms_control_kind_t::root)) {
            spans_are_safe = false;
        }
        if (node.close_source_span.has_value() && !can_shift_span(*node.close_source_span, false)) {
            spans_are_safe = false;
        }
        for (const auto& child : node.children) {
            self(self, child);
        }
    };
    validate_spans(validate_spans, *chart->bms_control_tree);
    if (!spans_are_safe) {
        reject(bms_branch_statement_edit_diagnostic_code_t::provenance_mismatch,
               "another retained source span overlaps or escapes the deleted statement range",
               path);
        return result;
    }

    auto updated_source = std::make_shared<std::vector<std::byte>>();
    updated_source->reserve(source_size - deleted_size);
    updated_source->insert(updated_source->end(), source_bytes.begin(),
                           source_bytes.begin() +
                               static_cast<std::ptrdiff_t>(target_span.byte_begin));
    updated_source->insert(updated_source->end(),
                           source_bytes.begin() + static_cast<std::ptrdiff_t>(target_span.byte_end),
                           source_bytes.end());

    result.song = song;
    auto& edited_chart = result.song->charts[chart_index];
    auto& edited_tree = *edited_chart.bms_control_tree;
    const auto shift_source_order = [&](std::size_t& source_order) {
        if (source_order > target->source_order) {
            --source_order;
        }
    };
    const auto shift_span = [&](bms_source_span_t& span) {
        if (span.byte_begin >= target_span.byte_end) {
            span.byte_begin -= deleted_size;
            span.byte_end -= deleted_size;
        }
    };
    const auto shift_tree = [&](const auto& self, bms_control_node_t& node) -> void {
        if (node.kind == bms_control_kind_t::root) {
            node.source_span.byte_end -= deleted_size;
        } else {
            shift_span(node.source_span);
            shift_source_order(node.source_order);
        }
        if (node.close_source_span.has_value()) {
            shift_span(*node.close_source_span);
        }
        if (node.close_source_order.has_value()) {
            shift_source_order(*node.close_source_order);
        }
        for (auto& child : node.children) {
            self(self, child);
        }
    };
    shift_tree(shift_tree, edited_tree);
    bms_control_node_t* edited_leaf{};
    const auto find_mutable_leaf = [&](const auto& self, bms_control_node_t& node) -> void {
        if (node.id == deletion.leaf_branch) {
            edited_leaf = &node;
            return;
        }
        for (auto& child : node.children) {
            self(self, child);
            if (edited_leaf != nullptr) {
                return;
            }
        }
    };
    find_mutable_leaf(find_mutable_leaf, edited_tree);
    const auto edited_target =
        std::ranges::find(edited_leaf->children, deletion.statement, &bms_control_node_t::id);
    edited_leaf->children.erase(edited_target);

    const auto shift_provenance = [&](source_provenance_t& provenance) {
        if (provenance.family == format_family_t::bms) {
            shift_source_order(provenance.source_order);
        }
    };
    const auto shift_group_orders = [&](std::vector<format_metadata_t>& groups) {
        for (auto& group : groups) {
            if (group.family != format_family_t::bms) {
                continue;
            }
            if (group.encoding.original_bytes &&
                group.encoding.original_bytes.get() == source->bytes.get()) {
                for (auto& source_order : group.ordered_field_source_orders) {
                    shift_source_order(source_order);
                }
                group.encoding.original_bytes = updated_source;
                group.encoding.unchanged_since_import = false;
            }
        }
    };
    shift_group_orders(result.song->metadata.format_groups);
    for (auto& resource : result.song->resources) {
        shift_provenance(resource.provenance);
    }
    shift_group_orders(edited_chart.metadata.format_groups);
    for (auto& measure : edited_chart.measures) {
        for (auto& note : measure.notes) {
            shift_provenance(note.provenance);
        }
        for (auto& event : measure.bgm_events) {
            shift_provenance(event.provenance);
        }
    }
    for (auto& lane : edited_chart.bgm_source_lanes) {
        shift_source_order(lane.source_order);
    }
    for (auto& event : edited_chart.bpms) {
        shift_provenance(event.provenance);
    }
    for (auto& event : edited_chart.pauses) {
        shift_provenance(event.provenance);
    }
    for (auto& event : edited_chart.warps) {
        shift_provenance(event.provenance);
    }
    for (auto& event : edited_chart.scrolls) {
        shift_provenance(event.provenance);
    }
    for (auto& event : edited_chart.speeds) {
        shift_provenance(event.provenance);
    }
    for (auto& event : edited_chart.kiai) {
        shift_provenance(event.provenance);
    }
    for (auto& event : edited_chart.bga_events) {
        shift_provenance(event.provenance);
    }
    for (auto& channel : edited_chart.sound_channels) {
        for (auto& event : channel.events) {
            shift_provenance(event.provenance);
        }
    }
    for (auto& projection : edited_chart.bms_measure_length_projections) {
        if (projection.source_order != std::numeric_limits<std::size_t>::max()) {
            shift_source_order(projection.source_order);
        }
    }
    for (auto& projection : edited_chart.bms_bpm_projections) {
        shift_source_order(projection.source_order);
    }
    for (auto& projection : edited_chart.bms_pause_projections) {
        shift_source_order(projection.source_order);
    }

    return result;
}

} // namespace wormedit

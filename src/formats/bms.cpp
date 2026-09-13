#include <wormedit/formats/bms.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <map>
#include <string_view>
#include <system_error>
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
                .target_identities = {{format_family_t::bms, normalized_id}},
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
                .target_identities = {{format_family_t::bms, normalized_id}},
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

    auto retain_branch_scoped = [&](const source_line_t& line, const parsed_line_t& parsed,
                                    std::string_view description) {
        if (frames.empty()) {
            return false;
        }
        push_raw_statement(root, current_path, line, parsed, ids);
        add_line_diagnostic(
            result, diagnostic_severity_t::warning, bms_parse_diagnostic_code_t::malformed_control,
            std::string(description) + " occurs inside conditional control flow and remains raw",
            line);
        return true;
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
            if (retain_branch_scoped(line, parsed, "metadata declaration")) {
                continue;
            }
            push_raw_statement(root, current_path, line, parsed, ids);
            if (token == "TITLE") {
                song.metadata.title = parsed.semantic_value;
            } else {
                song.metadata.artist = parsed.semantic_value;
            }
            ordered_metadata_fields.emplace_back(token, parsed.raw_value);
            continue;
        }

        if (token.size() == 5 && token.starts_with("WAV") && base36_digit(token[3]) &&
            base36_digit(token[4])) {
            if (retain_branch_scoped(line, parsed, "resource declaration")) {
                continue;
            }
            push_raw_statement(root, current_path, line, parsed, ids);
            add_resource(line, "WAV", std::string_view(token).substr(3), parsed.semantic_value);
            continue;
        }
        if (token.size() == 5 && token.starts_with("BMP") && base36_digit(token[3]) &&
            base36_digit(token[4])) {
            if (retain_branch_scoped(line, parsed, "resource declaration")) {
                continue;
            }
            push_raw_statement(root, current_path, line, parsed, ids);
            add_resource(line, "BMP", std::string_view(token).substr(3), parsed.semantic_value);
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
                if (retain_branch_scoped(line, parsed, "measure-length declaration")) {
                    continue;
                }
                push_raw_statement(root, current_path, line, parsed, ids);
                (void)ensure_measure(measure_number);
                const auto parsed_length = parse_measure_length(parsed.semantic_value);
                if (parsed_length.value.has_value()) {
                    if (last_measure_length_order.contains(measure_number)) {
                        // Repeated ratio lines remain in the source tree; the later valid value
                        // wins.
                        add_line_diagnostic(
                            result, diagnostic_severity_t::warning,
                            bms_parse_diagnostic_code_t::invalid_measure,
                            "repeated measure-length declaration; later value is applied", line);
                    }
                    auto& measure = ensure_measure(measure_number);
                    measure.definition.length = *parsed_length.value;
                    measure.definition.meter.reset();
                    last_measure_length_order[measure_number] = line.order;
                } else {
                    add_line_diagnostic(
                        result, diagnostic_severity_t::error,
                        bms_parse_diagnostic_code_t::invalid_measure,
                        "invalid exact BMS measure length", line,
                        line.text.find(':') == std::string::npos ? 1 : line.text.find(':') + 2);
                }
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
                add_line_diagnostic(
                    result, diagnostic_severity_t::warning,
                    bms_parse_diagnostic_code_t::malformed_control,
                    is_else_if ? "conditional branch after #ELSE is retained as raw text"
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
                add_line_diagnostic(
                    result, diagnostic_severity_t::warning,
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
    chart.metadata.name = song.metadata.title;
    chart.metadata.author = song.metadata.artist;
    chart.metadata.format_groups.push_back({
        .family = format_family_t::bms,
        .encoding = result.encoding,
    });
    song.metadata.format_groups.push_back({
        .family = format_family_t::bms,
        .ordered_fields = std::move(ordered_metadata_fields),
        .encoding = result.encoding,
    });
    song.charts.push_back(std::move(chart));
    result.song = std::move(song);
    return result;
}

} // namespace wormedit

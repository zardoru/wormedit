#include "sys/audio_project_import.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/decoder.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string_view>
#include <vector>

namespace wormedit::sys {
namespace {

constexpr std::size_t maximum_tag_bytes = 1024U * 1024U;
constexpr std::size_t maximum_field_bytes = 16U * 1024U;

auto byte_at(std::string_view bytes, std::size_t offset) -> std::uint8_t {
    return static_cast<std::uint8_t>(bytes[offset]);
}

auto read_u32_le(std::string_view bytes, std::size_t offset) -> std::uint32_t {
    return std::uint32_t{byte_at(bytes, offset)} |
           (std::uint32_t{byte_at(bytes, offset + 1)} << 8U) |
           (std::uint32_t{byte_at(bytes, offset + 2)} << 16U) |
           (std::uint32_t{byte_at(bytes, offset + 3)} << 24U);
}

auto read_u32_be(std::string_view bytes, std::size_t offset) -> std::uint32_t {
    return (std::uint32_t{byte_at(bytes, offset)} << 24U) |
           (std::uint32_t{byte_at(bytes, offset + 1)} << 16U) |
           (std::uint32_t{byte_at(bytes, offset + 2)} << 8U) |
           std::uint32_t{byte_at(bytes, offset + 3)};
}

auto read_syncsafe(std::string_view bytes, std::size_t offset) -> std::optional<std::uint32_t> {
    if ((byte_at(bytes, offset) | byte_at(bytes, offset + 1) |
         byte_at(bytes, offset + 2) | byte_at(bytes, offset + 3)) & 0x80U)
        return std::nullopt;
    return (std::uint32_t{byte_at(bytes, offset)} << 21U) |
           (std::uint32_t{byte_at(bytes, offset + 1)} << 14U) |
           (std::uint32_t{byte_at(bytes, offset + 2)} << 7U) |
           std::uint32_t{byte_at(bytes, offset + 3)};
}

void append_utf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint > 0x10ffffU || (codepoint >= 0xd800U && codepoint <= 0xdfffU)) return;
    if (codepoint < 0x80U) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800U) {
        output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint < 0x10000U) {
        output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else {
        output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
        output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    }
}

auto utf8_width(std::uint8_t lead) -> std::size_t {
    if (lead < 0x80U) return 1;
    if (lead >= 0xc2U && lead <= 0xdfU) return 2;
    if (lead >= 0xe0U && lead <= 0xefU) return 3;
    if (lead >= 0xf0U && lead <= 0xf4U) return 4;
    return 0;
}

auto valid_utf8_character(std::string_view text, std::size_t index,
                          std::size_t width) -> bool {
    if (width == 0 || width > text.size() - index) return false;
    if (width == 1) return true;
    const auto lead = byte_at(text, index);
    std::uint32_t codepoint = lead & (width == 2 ? 0x1fU : width == 3 ? 0x0fU : 0x07U);
    for (std::size_t part = 1; part < width; ++part) {
        const auto next = byte_at(text, index + part);
        if ((next & 0xc0U) != 0x80U) return false;
        codepoint = (codepoint << 6U) | (next & 0x3fU);
    }
    const auto minimum = width == 2 ? 0x80U : width == 3 ? 0x800U : 0x10000U;
    return codepoint >= minimum && codepoint <= 0x10ffffU &&
           (codepoint < 0xd800U || codepoint > 0xdfffU);
}

auto valid_utf8(std::string_view text) -> bool {
    for (std::size_t index = 0; index < text.size();) {
        const auto width = utf8_width(byte_at(text, index));
        if (!valid_utf8_character(text, index, width)) return false;
        index += width;
    }
    return true;
}

auto trim_text(std::string value) -> std::string {
    const auto first = value.find_first_not_of(" \t\r\n\0", 0, 5);
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n\0", std::string::npos, 5);
    return value.substr(first, last - first + 1);
}

auto decode_utf16(std::string_view source, bool big_endian) -> std::string {
    std::string output;
    output.reserve(source.size());
    for (std::size_t index = 0; index + 1 < source.size(); index += 2) {
        const std::uint32_t unit = big_endian
            ? (std::uint32_t{byte_at(source, index)} << 8U) | byte_at(source, index + 1)
            : (std::uint32_t{byte_at(source, index + 1)} << 8U) | byte_at(source, index);
        if (unit == 0) break;
        if (unit >= 0xd800U && unit <= 0xdbffU) {
            if (index + 3 >= source.size()) break;
            const std::uint32_t next = big_endian
                ? (std::uint32_t{byte_at(source, index + 2)} << 8U) | byte_at(source, index + 3)
                : (std::uint32_t{byte_at(source, index + 3)} << 8U) | byte_at(source, index + 2);
            if (next < 0xdc00U || next > 0xdfffU) break;
            append_utf8(output, 0x10000U + ((unit - 0xd800U) << 10U) + (next - 0xdc00U));
            index += 2;
        } else {
            append_utf8(output, unit);
        }
    }
    return trim_text(std::move(output));
}

auto decode_id3_text(std::string_view value) -> std::string {
    if (value.empty() || value.size() > maximum_field_bytes) return {};
    const auto encoding = byte_at(value, 0);
    value.remove_prefix(1);
    if (encoding == 1 || encoding == 2) {
        bool big_endian = encoding == 2;
        if (encoding == 1 && value.size() >= 2 &&
            ((byte_at(value, 0) == 0xfeU && byte_at(value, 1) == 0xffU) ||
             (byte_at(value, 0) == 0xffU && byte_at(value, 1) == 0xfeU))) {
            big_endian = byte_at(value, 0) == 0xfeU;
            value.remove_prefix(2);
        }
        return decode_utf16(value, big_endian);
    }
    std::string output;
    if (encoding == 3) {
        output = std::string{value.substr(0, value.find('\0'))};
        if (!valid_utf8(output)) return {};
    } else if (encoding == 0) {
        for (const auto character : value.substr(0, value.find('\0')))
            append_utf8(output, static_cast<std::uint8_t>(character));
    }
    return trim_text(std::move(output));
}

void assign_tag(audio_project_import_result_t& result, std::string_view name,
                std::string value) {
    if (value.empty() || value.size() > maximum_field_bytes || !valid_utf8(value)) return;
    if ((name == "TITLE" || name == "TIT2" || name == "TT2") && result.title.empty())
        result.title = std::move(value);
    else if ((name == "ARTIST" || name == "TPE1" || name == "TP1") && result.artist.empty())
        result.artist = std::move(value);
}

auto id3_frame_size(std::string_view bytes, std::size_t offset,
                    std::uint8_t version) -> std::uint32_t {
    if (version == 2)
        return (std::uint32_t{byte_at(bytes, offset + 3)} << 16U) |
               (std::uint32_t{byte_at(bytes, offset + 4)} << 8U) |
               byte_at(bytes, offset + 5);
    if (version == 4) return read_syncsafe(bytes, offset + 4).value_or(0);
    return read_u32_be(bytes, offset + 4);
}

auto id3_frame_next(std::string_view bytes, std::size_t offset, std::size_t end,
                    std::uint8_t version, audio_project_import_result_t& result)
    -> std::optional<std::size_t> {
    const auto header_size = version == 2 ? 6U : 10U;
    if (header_size > end - offset || byte_at(bytes, offset) == 0) return std::nullopt;
    const auto name = bytes.substr(offset, version == 2 ? 3U : 4U);
    const auto size = id3_frame_size(bytes, offset, version);
    if (size == 0 || size > end - offset - header_size) return std::nullopt;
    const auto flags = version == 2 ? 0U :
                       (std::uint32_t{byte_at(bytes, offset + 8)} << 8U) |
                       byte_at(bytes, offset + 9);
    offset += header_size;
    if ((name == "TIT2" || name == "TPE1" || name == "TT2" || name == "TP1") &&
        size <= maximum_field_bytes && !(flags & 0x00c0U))
        assign_tag(result, name, decode_id3_text(bytes.substr(offset, size)));
    return offset + size;
}

auto id3_first_frame(std::string_view bytes, std::size_t end,
                     std::uint8_t version) -> std::optional<std::size_t> {
    std::size_t offset = 10;
    if (!(byte_at(bytes, 5) & 0x40U)) return offset;
    if (version == 2 || offset + 4 > end) return std::nullopt;
    const auto extra = version == 4 ? read_syncsafe(bytes, offset)
                                    : std::optional<std::uint32_t>{read_u32_be(bytes, offset)};
    if (!extra) return std::nullopt;
    offset += version == 4 ? *extra : 4U + *extra;
    if (offset > end) return std::nullopt;
    return offset;
}

void parse_id3(std::string_view bytes, audio_project_import_result_t& result) {
    if (bytes.size() < 10 || bytes.substr(0, 3) != "ID3") return;
    const auto version = byte_at(bytes, 3);
    if (version < 2 || version > 4 || (byte_at(bytes, 5) & 0x80U)) return;
    const auto tag_size = read_syncsafe(bytes, 6);
    if (!tag_size || *tag_size > maximum_tag_bytes - 10 || *tag_size > bytes.size() - 10) return;
    const auto end = std::size_t{10} + *tag_size;
    auto offset = id3_first_frame(bytes, end, version);
    while (offset && *offset < end)
        offset = id3_frame_next(bytes, *offset, end, version, result);
}

void parse_id3v1(const std::filesystem::path& path, audio_project_import_result_t& result) {
    if (!result.title.empty() && !result.artist.empty()) return;
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file || file.tellg() < std::streamoff{128}) return;
    file.seekg(-128, std::ios::end);
    std::array<char, 128> tag{};
    if (!file.read(tag.data(), static_cast<std::streamsize>(tag.size())) ||
        std::string_view{tag.data(), 3} != "TAG") return;
    const auto latin1_field = [&](std::size_t offset) {
        std::string output;
        for (std::size_t index = offset; index < offset + 30 && tag[index]; ++index)
            append_utf8(output, static_cast<std::uint8_t>(tag[index]));
        return trim_text(std::move(output));
    };
    if (result.title.empty()) assign_tag(result, "TITLE", latin1_field(3));
    if (result.artist.empty()) assign_tag(result, "ARTIST", latin1_field(33));
}

void parse_vorbis_comment_packet(std::string_view packet,
                                 audio_project_import_result_t& result) {
    if (packet.size() < 11 || packet.substr(0, 7) != std::string_view{"\x03vorbis", 7}) return;
    std::size_t offset = 7;
    const auto vendor_size = read_u32_le(packet, offset);
    offset += 4;
    if (vendor_size > packet.size() - offset || packet.size() - offset - vendor_size < 4) return;
    offset += vendor_size;
    const auto count = read_u32_le(packet, offset);
    offset += 4;
    if (count > 4096) return;
    for (std::uint32_t index = 0; index < count && packet.size() - offset >= 4; ++index) {
        const auto length = read_u32_le(packet, offset);
        offset += 4;
        if (length > packet.size() - offset) return;
        const auto field = packet.substr(offset, length);
        offset += length;
        const auto equals = field.find('=');
        if (equals == std::string_view::npos || equals > 16) continue;
        std::string name{field.substr(0, equals)};
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        const auto value = field.substr(equals + 1);
        if ((name == "TITLE" || name == "ARTIST") && value.size() <= maximum_field_bytes)
            assign_tag(result, name, trim_text(std::string{value}));
    }
}

void parse_ogg(std::string_view bytes, audio_project_import_result_t& result) {
    std::size_t offset{};
    std::string packet;
    std::size_t packets{};
    while (offset + 27 <= bytes.size() && packets < 3) {
        if (bytes.substr(offset, 4) != "OggS" || byte_at(bytes, offset + 4) != 0) return;
        const auto segments = byte_at(bytes, offset + 26);
        if (offset + 27U + segments > bytes.size()) return;
        auto payload = offset + 27U + segments;
        for (std::size_t index = 0; index < segments; ++index) {
            const auto length = byte_at(bytes, offset + 27U + index);
            if (length > bytes.size() - payload || packet.size() + length > maximum_tag_bytes)
                return;
            packet.append(bytes.substr(payload, length));
            payload += length;
            if (length < 255U) {
                ++packets;
                if (packets == 2) parse_vorbis_comment_packet(packet, result);
                packet.clear();
            }
        }
        offset = payload;
    }
}

auto read_tag_prefix(const std::filesystem::path& path) -> std::string {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) return {};
    std::string bytes(maximum_tag_bytes, '\0');
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(stream.gcount()));
    return bytes;
}

auto normalized_extension(const std::filesystem::path& path) -> std::string {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension;
}

} // namespace

auto inspect_audio_for_project(const std::filesystem::path& source_path)
    -> audio_project_import_result_t {
    audio_project_import_result_t result;
    result.source_path = source_path;
    const auto extension = normalized_extension(source_path);
    if (extension != ".mp3" && extension != ".ogg") {
        result.error = "Select an MP3 or Ogg Vorbis audio file.";
        return result;
    }
    const auto bytes = read_tag_prefix(source_path);
    if (bytes.empty()) {
        result.error = "Could not read the audio file.";
        return result;
    }
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    audio::decoder_t decoder;
    if (!decoder.open(source_path, &result.error)) return result;
    const auto info = decoder.info();
    if (!info || info->source_sample_rate == 0) {
        result.error = "Could not inspect the audio stream.";
        return result;
    }
    result.stream = {.sample_rate = info->source_sample_rate,
                     .channels = info->source_channel_count,
                     .frame_count = info->source_frame_count,
                     .seekable = info->seekable};
    result.duration_seconds = static_cast<double>(info->source_frame_count) /
                              static_cast<double>(info->source_sample_rate);
#else
    if ((extension == ".mp3" && bytes.substr(0, 3) != "ID3" &&
         (static_cast<unsigned char>(bytes[0]) != 0xffU)) ||
        (extension == ".ogg" && bytes.substr(0, 4) != "OggS")) {
        result.error = "The file does not appear to contain supported audio.";
        return result;
    }
#endif
    if (extension == ".mp3") {
        parse_id3(bytes, result);
        parse_id3v1(source_path, result);
    } else {
        parse_ogg(bytes, result);
    }
    if (result.title.empty()) result.title = source_path.stem().string();
    if (result.title.empty() || result.title.size() > maximum_field_bytes ||
        !valid_utf8(result.title)) result.title = "Untitled";
    result.error.clear();
    result.valid = true;
    return result;
}

} // namespace wormedit::sys

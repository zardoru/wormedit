#pragma once

#include <wormedit/core/document.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace wormedit {

// Counts derive from the enum endpoints; catalog assertions also check every key.
inline constexpr std::size_t format_family_count = static_cast<std::size_t>(format_family_t::quaver) + 1;
inline constexpr std::size_t text_encoding_count = static_cast<std::size_t>(text_encoding_t::windows_1252) + 1;
inline constexpr std::size_t note_kind_count = static_cast<std::size_t>(note_kind_t::fake) + 1;
inline constexpr std::size_t sound_kind_count = static_cast<std::size_t>(sound_kind_t::sample) + 1;
inline constexpr std::size_t bms_line_ending_count = static_cast<std::size_t>(bms_line_ending_t::cr) + 1;

template <typename Entry, std::size_t Size>
consteval auto is_catalog_exhaustive(const std::array<Entry, Size>& entries, std::size_t count) -> bool {
    if (Size != count) return false;
    for (std::size_t index = 0; index < Size; ++index)
        if (static_cast<std::size_t>(entries[index].kind) != index) return false;
    return true;
}

template <typename Entry, std::size_t Size, typename Kind>
constexpr auto find_catalog_entry(const std::array<Entry, Size>& entries, Kind kind) -> const Entry* {
    const auto index = static_cast<std::size_t>(kind);
    return index < Size ? &entries[index] : nullptr;
}

struct format_family_descriptor_t {
    format_family_t kind;
    std::string_view label;
    bool offered_as_output;
    bool offered_as_metadata;
};
inline constexpr std::array format_families{
    format_family_descriptor_t{format_family_t::native_project, "Native", true, true},
    format_family_descriptor_t{format_family_t::bms, "BMS", true, true},
    format_family_descriptor_t{format_family_t::stepmania, "StepMania", true, true},
    format_family_descriptor_t{format_family_t::osu_mania, "osu!mania", true, true},
    format_family_descriptor_t{format_family_t::bmson, "BMSON", true, true},
    format_family_descriptor_t{format_family_t::midi, "MIDI", false, true},
    format_family_descriptor_t{format_family_t::quaver, "Quaver", false, true},
};
static_assert(is_catalog_exhaustive(format_families, format_family_count));
constexpr auto format_family_label(format_family_t kind) -> std::string_view {
    const auto* entry = find_catalog_entry(format_families, kind);
    return entry ? entry->label : "Unknown";
}

struct text_encoding_descriptor_t {
    text_encoding_t kind;
    std::string_view label;
    // ASCII and UTF-8 have dedicated paths; no iconv conversion is requested.
    std::optional<std::string_view> iconv_name;
    bool offered;
};
inline constexpr std::array bms_text_encodings{
    text_encoding_descriptor_t{text_encoding_t::unknown, "unknown", std::nullopt, false},
    text_encoding_descriptor_t{text_encoding_t::ascii, "ASCII", std::nullopt, false},
    text_encoding_descriptor_t{text_encoding_t::utf8, "UTF-8", std::nullopt, true},
    text_encoding_descriptor_t{text_encoding_t::cp932, "CP932", "CP932", true},
    text_encoding_descriptor_t{text_encoding_t::euc_kr, "EUC-KR", "EUC-KR", true},
    text_encoding_descriptor_t{text_encoding_t::cp949, "CP949", "CP949", true},
    text_encoding_descriptor_t{text_encoding_t::windows_1252, "Windows-1252", "WINDOWS-1252", true},
};
static_assert(is_catalog_exhaustive(bms_text_encodings, text_encoding_count));
constexpr auto text_encoding_label(text_encoding_t kind) -> std::string_view {
    const auto* entry = find_catalog_entry(bms_text_encodings, kind);
    return entry ? entry->label : "unknown";
}
constexpr auto text_encoding_iconv_name(text_encoding_t kind) -> std::optional<std::string_view> {
    const auto* entry = find_catalog_entry(bms_text_encodings, kind);
    return entry ? entry->iconv_name : std::nullopt;
}

struct note_kind_descriptor_t {
    note_kind_t kind;
    std::string_view label;
    std::string_view short_label;
    std::string_view command_id_suffix;
    // Alt + digit; zero means this kind has no palette shortcut.
    unsigned shortcut_digit;
};
inline constexpr std::array note_kinds{
    note_kind_descriptor_t{note_kind_t::tap, "Tap", "Tap", "tap", 0},
    note_kind_descriptor_t{note_kind_t::hold, "Hold", "Hold", "hold", 0},
    note_kind_descriptor_t{note_kind_t::roll, "Roll", "Roll", "roll", 8},
    note_kind_descriptor_t{note_kind_t::invisible, "Invisible note", "Invisible", "invisible", 5},
    note_kind_descriptor_t{note_kind_t::mine, "Mine", "Mine", "mine", 4},
    note_kind_descriptor_t{note_kind_t::lift, "Lift", "Lift", "lift", 6},
    note_kind_descriptor_t{note_kind_t::fake, "Fake note", "Fake", "fake", 7},
};
static_assert(is_catalog_exhaustive(note_kinds, note_kind_count));
// Presentation order is explicit; enum keys, never raw integers, cross layers.
inline constexpr std::array note_palette_kinds{note_kind_t::tap, note_kind_t::hold, note_kind_t::mine,
    note_kind_t::invisible, note_kind_t::lift, note_kind_t::fake, note_kind_t::roll};
constexpr auto note_kind_label(note_kind_t kind) -> std::string_view {
    const auto* entry = find_catalog_entry(note_kinds, kind);
    return entry ? entry->label : "Note";
}

struct sound_kind_descriptor_t { sound_kind_t kind; std::string_view label; };
inline constexpr std::array sound_kinds{
    sound_kind_descriptor_t{sound_kind_t::unspecified, "Sound"},
    sound_kind_descriptor_t{sound_kind_t::stem, "Stem"},
    sound_kind_descriptor_t{sound_kind_t::keysound, "Keysound"},
    sound_kind_descriptor_t{sound_kind_t::sample, "Sample"},
};
static_assert(is_catalog_exhaustive(sound_kinds, sound_kind_count));
constexpr auto sound_kind_label(sound_kind_t kind) -> std::string_view {
    const auto* entry = find_catalog_entry(sound_kinds, kind);
    return entry ? entry->label : "Sound";
}

struct bms_line_ending_descriptor_t { bms_line_ending_t kind; std::string_view bytes; };
inline constexpr std::array bms_line_endings{
    bms_line_ending_descriptor_t{bms_line_ending_t::none, ""},
    bms_line_ending_descriptor_t{bms_line_ending_t::lf, "\n"},
    bms_line_ending_descriptor_t{bms_line_ending_t::crlf, "\r\n"},
    bms_line_ending_descriptor_t{bms_line_ending_t::cr, "\r"},
};
static_assert(is_catalog_exhaustive(bms_line_endings, bms_line_ending_count));
constexpr auto bms_line_ending_bytes(bms_line_ending_t kind) -> std::string_view {
    const auto* entry = find_catalog_entry(bms_line_endings, kind);
    return entry ? entry->bytes : std::string_view{};
}

inline constexpr std::array<std::string_view, 4> bms_extensions{".bms", ".bme", ".bml", ".pms"};
// Input is a normalized lowercase extension, as produced by lowercase_extension.
constexpr auto is_bms_extension(std::string_view extension) -> bool {
    for (const auto candidate : bms_extensions)
        if (candidate == extension) return true;
    return false;
}

constexpr auto bms_extension_for_profile(std::string_view profile) -> std::string_view {
    if (profile.find("pms") != std::string_view::npos) return bms_extensions[3];
    if (profile.find("bme") != std::string_view::npos) return bms_extensions[1];
    if (profile.find("bml") != std::string_view::npos) return bms_extensions[2];
    return bms_extensions[0];
}

} // namespace wormedit

#pragma once
#include <wormedit/core/document.h>

namespace wormedit {
struct inherited_metadata_field_t_info {
    inherited_metadata_field_t field;
    std::string_view label;
    std::string_view bms_key;
    std::string_view bmson_key;
    std::string_view stepmania_key;
    std::string_view osu_key;
    [[nodiscard]] auto key(format_family_t) const -> std::string_view;
    [[nodiscard]] auto audio_role() const -> std::optional<resource_role_t> {
        if (field == inherited_metadata_field_t::music) return resource_role_t::music;
        if (field == inherited_metadata_field_t::preview) return resource_role_t::preview;
        return {};
    }
    [[nodiscard]] auto is_audio_path() const -> bool { return audio_role().has_value(); }
};
inline constexpr std::array<inherited_metadata_field_t_info, inherited_metadata_field_count> inherited_metadata_fields{{
    {inherited_metadata_field_t::title, "Title", "TITLE", "title", "TITLE", "Metadata.Title"},
    {inherited_metadata_field_t::subtitle, "Subtitle", "SUBTITLE", "subtitle", "SUBTITLE", ""},
    {inherited_metadata_field_t::artist, "Artist", "ARTIST", "artist", "ARTIST", "Metadata.Artist"},
    {inherited_metadata_field_t::genre, "Genre", "GENRE", "genre", "GENRE", ""},
    {inherited_metadata_field_t::music, "Music file", "", "", "MUSIC", "General.AudioFilename"},
    {inherited_metadata_field_t::preview, "Preview audio file", "PREVIEW", "preview_music", "", ""},
}};
inline constexpr bool is_inherited_metadata_catalog_exhaustive = [] {
    for (std::size_t index = 0; index < inherited_metadata_fields.size(); ++index)
        if (static_cast<std::size_t>(inherited_metadata_fields[index].field) != index) return false;
    return true;
}();
static_assert(is_inherited_metadata_catalog_exhaustive);

struct resolved_metadata_t {
    [[nodiscard]] static auto from(const song_document_t&, const chart_document_t&) -> resolved_metadata_t;
    std::array<std::string, inherited_metadata_field_count> inherited;
    std::array<std::string, inherited_metadata_field_count> effective;
    [[nodiscard]] auto value(inherited_metadata_field_t field) const -> const std::string& {
        return effective.at(static_cast<std::size_t>(field));
    }
    auto operator==(const resolved_metadata_t&) const -> bool = default;
};
struct metadata_audio_references_t {
    [[nodiscard]] static auto from(const song_document_t&, const metadata_overrides_t&)
        -> diagnosed_t<metadata_audio_references_t>;
    std::vector<asset_id_t> assets;
    auto operator==(const metadata_audio_references_t&) const -> bool = default;
};
// Target writers retain their existing inherited serialization. This preflight
// validates only explicit overrides; unsupported slots never silently disappear.
[[nodiscard]] auto validate_metadata_overrides(const metadata_overrides_t&, format_family_t)
    -> diagnostic_list_t;
} // namespace wormedit

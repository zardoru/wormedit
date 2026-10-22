#include <wormedit/core/metadata_inheritance.h>
#include "text_util.h"
#include <wormedit/core/metadata_limits.h>
#include <nlohmann/json.hpp>

namespace wormedit {
auto inherited_metadata_field_t_info::key(format_family_t family) const -> std::string_view {
    switch (family) {
    case format_family_t::bms: return bms_key;
    case format_family_t::bmson: return bmson_key;
    case format_family_t::stepmania: return stepmania_key;
    case format_family_t::osu_mania: return osu_key;
    case format_family_t::native_project:
    case format_family_t::midi:
    case format_family_t::quaver: return {};
    }
    return {};
}
namespace {
auto retained_value(const std::vector<format_metadata_t>& groups, const inherited_metadata_field_t_info& field)
    -> std::optional<std::string> {
    std::optional<std::string> result;
    for (const auto& group : groups) {
        auto key = std::string{field.key(group.family)};
        if (key.empty()) continue;
        if (group.family == format_family_t::bmson) key = "info." + key;
        for (const auto& [name, value] : group.ordered_fields)
            if (name == key) {
                if (group.family == format_family_t::bmson) {
                    const auto decoded = nlohmann::json::parse(value, nullptr, false);
                    if (decoded.is_string()) result = decoded.get<std::string>();
                } else result = std::string{text_util::trim(value, text_util::ascii_whitespace)};
            }
    }
    return result;
}
auto is_safe_metadata_file(std::string_view value) -> bool {
    if (value.empty()) return true;
    const auto path = text_util::path_from_utf8(value);
    return !path.is_absolute() && !path.has_root_name() && !path.filename().empty() &&
        path.lexically_normal() == path && std::ranges::none_of(path, [](const auto& part) { return part == ".."; });
}
auto inherited_value(const song_document_t& song, const chart_document_t& chart,
    const inherited_metadata_field_t_info& field) -> std::string {
    if (field.field == inherited_metadata_field_t::title) return song.metadata.title;
    if (field.field == inherited_metadata_field_t::artist) return song.metadata.artist;
    if (auto value = retained_value(chart.metadata.format_groups, field)) return *value;
    if (auto value = retained_value(song.metadata.format_groups, field)) return *value;
    if (field.field == inherited_metadata_field_t::music) {
        for (const auto& resource : song.resources)
            if (resource.has_role(resource_role_t::music))
                if (const auto* asset = song.find_audio_asset(resource.asset)) return asset->source_path.generic_string();
    }
    return {};
}
}
auto resolved_metadata_t::from(const song_document_t& song, const chart_document_t& chart) -> resolved_metadata_t {
    resolved_metadata_t result;
    for (const auto& field : inherited_metadata_fields) {
        const auto index = static_cast<std::size_t>(field.field);
        result.inherited[index] = inherited_value(song, chart, field);
        result.effective[index] = chart.metadata.overrides.value(field.field).value_or(result.inherited[index]);
    }
    return result;
}
auto validate_metadata_overrides(const metadata_overrides_t& overrides, format_family_t family) -> diagnostic_list_t {
    diagnostic_list_t diagnostics;
    for (const auto& field : inherited_metadata_fields) {
        const auto& value = overrides.value(field.field);
        if (!value) continue;
        if (family != format_family_t::native_project && field.key(family).empty()) {
            diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
                std::string{field.label} + " override has no metadata slot in this target format."));
            continue;
        }
        if (!text_util::is_valid_bounded_text(*value, default_metadata_edit_limits.maximum_chart_text_bytes) || value->find_first_of("\r\n") != std::string::npos)
            diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
                std::string{field.label} + " override must be bounded single-line UTF-8 text."));
        else if (field.is_audio_path() && family != format_family_t::native_project && !is_safe_metadata_file(*value))
            diagnostics.push_back(error(diagnostic_code_t::unsupported_feature,
                std::string{field.label} + " override must be a safe relative path."));
    }
    return diagnostics;
}
auto metadata_overrides_t::transform_audio_paths(const std::function<void(std::filesystem::path&)>& transform) -> bool {
    bool changed{};
    for (const auto& field : inherited_metadata_fields) {
        auto& authored = value(field.field);
        if (!field.is_audio_path() || !authored || authored->empty()) continue;
        auto path = text_util::path_from_utf8(*authored);
        transform(path);
        const auto text = text_util::path_utf8(path);
        changed = changed || text != *authored;
        *authored = text;
    }
    return changed;
}
auto metadata_audio_references_t::from(const song_document_t& song, const metadata_overrides_t& overrides)
    -> diagnosed_t<metadata_audio_references_t> {
    diagnosed_t<metadata_audio_references_t> result;
    for (const auto& field : inherited_metadata_fields) {
        const auto& value = overrides.value(field.field);
        if (!field.is_audio_path() || !value || value->empty()) continue;
        if (!text_util::is_valid_bounded_text(*value, default_metadata_edit_limits.maximum_chart_text_bytes)) continue;
        const auto path = text_util::path_from_utf8(*value).lexically_normal();
        const audio_asset_t* matched{};
        for (const auto& asset : song.audio_assets) {
            if (asset.source_path.lexically_normal() != path) continue;
            if (matched) {
                result.diagnostics.push_back(error(diagnostic_code_t::invalid_id, "Override audio path refers to multiple assets."));
                return result;
            }
            matched = &asset;
        }
        if (!matched) result.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
            std::string{field.label} + " must refer to an imported audio asset; import or relink it first."));
        else result.value.assets.push_back(matched->id);
    }
    return result;
}
} // namespace wormedit

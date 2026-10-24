#pragma once

#include "ui/frontend/catalog.h"
#include "ui/frontend/workspace.h"
#include <wormedit/core/catalogs.h>
#include <wormedit/core/timing_lane_catalog.h>
#include <wormedit/formats/bms.h>
#include <array>
#include <wormedit/audio/playback_rate.h>
#include <vector>
#include <wormedit/core/lane_profiles.h>
#include <utility>

namespace wormedit::sys {

static_assert(static_cast<int>(lane_role_t::playable) ==
              static_cast<int>(new_project_lane_role_t::playable));
static_assert(static_cast<int>(lane_role_t::foot_pedal) ==
              static_cast<int>(new_project_lane_role_t::foot_pedal));

consteval auto build_sound_kind_catalog_view()
    -> std::array<wormedit::ui::kind_descriptor_view_t<sound_kind_t>, sound_kind_count> {
    std::array<wormedit::ui::kind_descriptor_view_t<sound_kind_t>, sound_kind_count> result{};
    for (std::size_t index = 0; index < sound_kinds.size(); ++index) {
        const auto& kind = sound_kinds[index];
        result[index] = {.id = kind.kind, .label = kind.label.data()};
    }
    return result;
}
inline constexpr auto sound_kind_catalog_view = build_sound_kind_catalog_view();
inline constexpr auto sound_kind_view = std::span{sound_kind_catalog_view};

// Constructed once during process initialization; frame projection only borrows it.
inline auto build_new_project_layout_catalog_view()
    -> std::vector<new_project_layout_entry_view_t> {
    std::vector<new_project_layout_entry_view_t> result;
    result.reserve(new_project_layouts().size());
    for (const auto& layout : new_project_layouts()) {
        const auto preview = layout.preview_lane_roles();
        std::vector<new_project_lane_role_t> roles;
        roles.reserve(preview.size());
        for (const auto role : preview)
            roles.push_back(static_cast<new_project_lane_role_t>(role));
        result.push_back({.id = layout.id,
                          .label = layout.label,
                          .game = layout.game,
                          .lane_shape = layout.lane_shape,
                          .variant = layout.variant,
                          .preview_lane_roles = std::move(roles)});
    }
    return result;
}
inline const auto new_project_layout_catalog_view = build_new_project_layout_catalog_view();
inline const new_project_layout_catalog_view_t new_project_layout_view{
    .entries = new_project_layout_catalog_view};

consteval auto build_playback_rate_catalog_view()
    -> std::array<wormedit::ui::kind_descriptor_view_t<audio::playback_rate_id_t>, audio::playback_rate_count> {
    std::array<wormedit::ui::kind_descriptor_view_t<audio::playback_rate_id_t>, audio::playback_rate_count> result{};
    for (std::size_t index = 0; index < audio::playback_rate_catalog.size(); ++index) {
        const auto& rate = audio::playback_rate_catalog[index];
        result[index] = {.id = rate.id, .label = rate.label.data()};
    }
    return result;
}
inline constexpr auto playback_rate_catalog_view = build_playback_rate_catalog_view();
inline constexpr auto playback_rate_view = std::span{playback_rate_catalog_view};

inline auto build_bms_lane_profile_catalog_view()
    -> std::vector<ui::kind_descriptor_view_t<std::string_view>> {
    std::vector<ui::kind_descriptor_view_t<std::string_view>> result;
    for (const auto& entry : built_in_bms_lane_profiles())
        result.push_back({.id = entry.profile.layout.profile_id,
                          .label = entry.profile.layout.display_name});
    return result;
}
inline const auto bms_lane_profile_catalog_view = build_bms_lane_profile_catalog_view();

// Capability probing and storage allocation happen once, never during projection.
inline auto build_bms_encoding_catalog_view() -> std::vector<wormedit::ui::kind_descriptor_view_t<text_encoding_t>> {
    std::vector<wormedit::ui::kind_descriptor_view_t<text_encoding_t>> result;
    result.reserve(bms_text_encodings.size());
    for (const auto& encoding : bms_text_encodings) {
        if (encoding.offered && is_bms_text_encoding_supported(encoding.kind))
            result.push_back({.id = encoding.kind,
                              .label = encoding.choice_label.data()});
    }
    return result;
}
inline const auto bms_encoding_catalog_view = build_bms_encoding_catalog_view();
inline const auto bms_encoding_view = std::span{bms_encoding_catalog_view};

inline auto is_bms_encoding_offered(text_encoding_t id) -> bool {
    for (const auto& encoding : bms_encoding_catalog_view)
        if (encoding.id == id) return true;
    return false;
}

consteval auto build_format_family_catalog_view(bool output)
    -> std::array<wormedit::ui::kind_descriptor_view_t<format_family_t>, format_family_count> {
    std::array<wormedit::ui::kind_descriptor_view_t<format_family_t>, format_family_count> result{};
    std::size_t index = 0;
    for (const auto& family : format_families) {
        if (output ? family.offered_as_output : family.offered_as_metadata)
            result[index++] = {.id = family.kind,
                              .label = (output ? family.output_label : family.label).data()};
    }
    return result;
}
consteval auto format_family_offered_count(bool output) -> std::size_t {
    std::size_t count = 0;
    for (const auto& family : format_families)
        if (output ? family.offered_as_output : family.offered_as_metadata) ++count;
    return count;
}
inline constexpr auto output_family_catalog_view = build_format_family_catalog_view(true);
inline constexpr auto metadata_family_catalog_view = build_format_family_catalog_view(false);
inline constexpr auto output_family_view = std::span{output_family_catalog_view}.first(format_family_offered_count(true));
inline constexpr auto metadata_family_view = std::span{metadata_family_catalog_view}.first(format_family_offered_count(false));

consteval auto build_timing_lane_catalog_view(bool captions)
    -> std::array<wormedit::ui::kind_descriptor_view_t<timing_lane_kind_t>, timing_lane_kind_count> {
    std::array<wormedit::ui::kind_descriptor_view_t<timing_lane_kind_t>, timing_lane_kind_count> result{};
    for (std::size_t index = 0; index < timing_lanes.size(); ++index) {
        const auto& lane = timing_lanes[index];
        result[index] = {.id = lane.kind,
                         .label = (captions ? lane.short_label : lane.label).data()};
    }
    return result;
}
inline constexpr auto timing_lane_catalog_view = build_timing_lane_catalog_view(false);
inline constexpr auto timing_caption_catalog_view = build_timing_lane_catalog_view(true);
inline constexpr auto timing_lane_view = std::span{timing_lane_catalog_view};
inline constexpr auto timing_caption_view = std::span{timing_caption_catalog_view};

consteval auto build_note_palette_catalog_view()
    -> std::array<wormedit::ui::kind_descriptor_view_t<note_kind_t>, note_palette_kinds.size()> {
    std::array<wormedit::ui::kind_descriptor_view_t<note_kind_t>, note_palette_kinds.size()> result{};
    for (std::size_t index = 0; index < note_palette_kinds.size(); ++index) {
        const auto kind = note_palette_kinds[index];
        result[index] = {.id = kind,
                         .label = find_catalog_entry(note_kinds, kind)->palette_label.data()};
    }
    return result;
}
inline constexpr auto note_palette_catalog_view = build_note_palette_catalog_view();
inline constexpr auto note_palette_view = std::span{note_palette_catalog_view};

} // namespace wormedit::sys

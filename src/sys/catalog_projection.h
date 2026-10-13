#pragma once

#include "ui/frontend/catalog.h"
#include <wormedit/core/catalogs.h>
#include <array>
#include <vector>
#include <wormedit/core/lane_profiles.h>

namespace wormedit::sys {

consteval auto build_sound_kind_catalog_view()
    -> std::array<WormUiKindDescriptorView, sound_kind_count> {
    std::array<WormUiKindDescriptorView, sound_kind_count> result{};
    for (std::size_t index = 0; index < sound_kinds.size(); ++index) {
        const auto& kind = sound_kinds[index];
        result[index] = {.id = static_cast<std::uint32_t>(kind.kind), .label = kind.label.data()};
    }
    return result;
}
inline constexpr auto sound_kind_catalog_view = build_sound_kind_catalog_view();
inline constexpr WormUiKindCatalogView sound_kind_view{
    .entries = sound_kind_catalog_view.data(), .count = sound_kind_catalog_view.size()};

// Constructed once during process initialization; frame projection only borrows it.
inline auto build_new_project_layout_catalog_view() -> std::vector<WormUiStringDescriptorView> {
    std::vector<WormUiStringDescriptorView> result;
    result.reserve(new_project_layouts().size());
    for (const auto& layout : new_project_layouts())
        result.push_back({.id = layout.id.data(), .label = layout.label.data()});
    return result;
}
inline const auto new_project_layout_catalog_view = build_new_project_layout_catalog_view();
inline auto build_new_project_layout_labels() -> std::vector<const char*> {
    std::vector<const char*> result;
    result.reserve(new_project_layout_catalog_view.size());
    for (const auto& entry : new_project_layout_catalog_view) result.push_back(entry.label);
    return result;
}
inline const auto new_project_layout_labels = build_new_project_layout_labels();
inline const WormUiStringCatalogView new_project_layout_view{
    .entries = new_project_layout_catalog_view.data(), .count = new_project_layout_catalog_view.size(),
    .labels = new_project_layout_labels.data()};

} // namespace wormedit::sys

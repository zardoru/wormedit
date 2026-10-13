#pragma once

#include "ui/frontend/catalog.h"
#include <wormedit/core/catalogs.h>
#include <array>

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

} // namespace wormedit::sys

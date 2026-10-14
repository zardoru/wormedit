#pragma once

#include "ui/frontend/catalog.h"
#include <cstdint>

namespace wormedit::ui {

inline auto kind_label(const WormUiKindCatalogView& catalog, std::uint32_t id,
                       const char* fallback) -> const char* {
    for (std::size_t index = 0; index < catalog.count; ++index)
        if (catalog.entries[index].id == id) return catalog.entries[index].label;
    return fallback;
}

} // namespace wormedit::ui

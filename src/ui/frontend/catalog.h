#pragma once

#include <wormedit/core/vocabulary.h>

#include <span>
#include <string_view>
#include <vector>

namespace wormedit {
enum class format_family_t;
enum class text_encoding_t;
enum class timing_lane_kind_t;
namespace audio {
enum class playback_rate_id_t;
}
}

namespace wormedit::ui {

// Stable vocabulary identities and process-lifetime labels from the core catalogs.
template <typename Kind>
struct kind_descriptor_view_t {
    Kind id{};
    std::string_view label;

    auto operator==(const kind_descriptor_view_t&) const -> bool = default;
};

template <typename Kind>
struct kind_catalog_view_t {
    std::vector<kind_descriptor_view_t<Kind>> entries;

    // Catalogs are immutable for the process lifetime. Populate a persistent view once.
    void refill(std::span<const kind_descriptor_view_t<Kind>> catalog) {
        if (entries.empty()) entries.assign(catalog.begin(), catalog.end());
    }

    [[nodiscard]] auto label(Kind id, const char* fallback) const -> const char* {
        for (const auto& entry : entries)
            if (entry.id == id) return entry.label.data();
        return fallback;
    }
};

} // namespace wormedit::ui

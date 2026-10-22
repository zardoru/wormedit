#pragma once

#include <wormedit/core/vocabulary.h>
#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace wormedit {
enum class timing_category_t { bpm, scroll, speed, kiai, stop, delay, warp, anchor, offset, measures, count };
inline constexpr auto timing_category_count = static_cast<std::size_t>(timing_category_t::count);
template<class Id> struct timing_link_identity_t {
    Id source;
    Id target;
    auto operator==(const timing_link_identity_t&) const -> bool = default;
};
struct timing_category_link_t {
    chart_id_t source;
    // A local write remembers its source; explicit relink discards this override.
    bool is_local_override{};
    std::vector<timing_link_identity_t<object_id_t>> objects;
    std::vector<timing_link_identity_t<recipe_id_t>> recipes;
    std::vector<timing_link_identity_t<measure_id_t>> measures;
    auto operator==(const timing_category_link_t&) const -> bool = default;
};
struct timing_links_t {
    std::array<std::optional<timing_category_link_t>, timing_category_count> categories;
    [[nodiscard]] auto value(timing_category_t category) -> std::optional<timing_category_link_t>& {
        return categories.at(static_cast<std::size_t>(category));
    }
    [[nodiscard]] auto value(timing_category_t category) const -> const std::optional<timing_category_link_t>& {
        return categories.at(static_cast<std::size_t>(category));
    }
    auto operator==(const timing_links_t&) const -> bool = default;
};
} // namespace wormedit

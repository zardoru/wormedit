#pragma once

#include <compare>
#include <cstdint>
#include <optional>

namespace wormedit {

template <typename tag_t> struct stable_id_t {
    std::uint64_t value{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return value != 0;
    }
    auto operator<=>(const stable_id_t&) const = default;
};

struct song_id_tag_t;
struct chart_id_tag_t;
struct measure_id_tag_t;
struct object_id_tag_t;
struct lane_id_tag_t;
struct layer_id_tag_t;
struct sound_group_id_tag_t;
struct resource_id_tag_t;
struct asset_id_tag_t;
struct clip_id_tag_t;
struct branch_id_tag_t;
struct recipe_id_tag_t;
struct storyboard_id_tag_t;

using song_id_t = stable_id_t<song_id_tag_t>;
using chart_id_t = stable_id_t<chart_id_tag_t>;
using measure_id_t = stable_id_t<measure_id_tag_t>;
using object_id_t = stable_id_t<object_id_tag_t>;
using lane_id_t = stable_id_t<lane_id_tag_t>;
using layer_id_t = stable_id_t<layer_id_tag_t>;
using sound_group_id_t = stable_id_t<sound_group_id_tag_t>;
using resource_id_t = stable_id_t<resource_id_tag_t>;
using asset_id_t = stable_id_t<asset_id_tag_t>;
using clip_id_t = stable_id_t<clip_id_tag_t>;
using branch_id_t = stable_id_t<branch_id_tag_t>;
using recipe_id_t = stable_id_t<recipe_id_tag_t>;
using storyboard_id_t = stable_id_t<storyboard_id_tag_t>;

struct id_allocator_t {
    std::uint64_t next_value{1};
};

template <typename id_t>
[[nodiscard]] auto allocate_id(id_allocator_t& allocator) -> std::optional<id_t> {
    if (allocator.next_value == 0) {
        return std::nullopt;
    }
    const auto value = allocator.next_value++;
    return id_t{value};
}

} // namespace wormedit

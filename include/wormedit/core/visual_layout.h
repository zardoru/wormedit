#pragma once

#include <cstdint>
#include <optional>

namespace wormedit {

enum class visual_fit_policy_t : std::uint8_t {
    stretch,
    contain,
    cover,
};

struct visual_destination_rect_t {
    double left{};
    double top{};
    double width{};
    double height{};

    auto operator==(const visual_destination_rect_t&) const -> bool = default;
};

struct visual_source_rect_t {
    std::uint32_t left{};
    std::uint32_t top{};
    std::uint32_t width{};
    std::uint32_t height{};

    auto operator==(const visual_source_rect_t&) const -> bool = default;
};

struct visual_layout_request_t {
    std::uint32_t source_width{};
    std::uint32_t source_height{};
    visual_destination_rect_t destination;
    visual_fit_policy_t fit{visual_fit_policy_t::contain};
};

struct visual_image_layout_t {
    visual_destination_rect_t destination;
    visual_source_rect_t source;

    auto operator==(const visual_image_layout_t&) const -> bool = default;
};

[[nodiscard]] auto valid_visual_fit_policy(visual_fit_policy_t fit) noexcept -> bool;

// Produces deterministic renderer-neutral geometry. Contain letterboxes in
// destination space; cover center-crops whole source pixels; stretch uses both
// rectangles in full. Invalid or non-finite geometry publishes no layout.
[[nodiscard]] auto make_visual_image_layout(const visual_layout_request_t& request)
    -> std::optional<visual_image_layout_t>;

} // namespace wormedit

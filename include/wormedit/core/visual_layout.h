#pragma once

#include <array>
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
    [[nodiscard]] static auto from(const visual_layout_request_t& request)
        -> std::optional<visual_image_layout_t>;

    visual_destination_rect_t destination;
    visual_source_rect_t source;

    auto operator==(const visual_image_layout_t&) const -> bool = default;
};

struct visual_point_t {
    double x{};
    double y{};

    auto operator==(const visual_point_t&) const -> bool = default;
};

struct visual_image_quad_request_t {
    std::uint32_t source_width{};
    std::uint32_t source_height{};
    visual_destination_rect_t destination;
    double logical_width{640.0};
    double logical_height{480.0};
    double x{};
    double y{};
    double origin_x{0.5};
    double origin_y{0.5};
    double scale_x{1.0};
    double scale_y{1.0};
    double rotation_radians{};
};

struct visual_image_quad_t {
    [[nodiscard]] static auto from(const visual_image_quad_request_t& request)
        -> std::optional<visual_image_quad_t>;

    // Top-left, top-right, bottom-right, bottom-left in texture order. Signed
    // scale changes their screen winding and therefore preserves axis flips.
    std::array<visual_point_t, 4> points;
};

[[nodiscard]] auto is_valid_visual_fit_policy(visual_fit_policy_t fit) noexcept -> bool;


// Produces deterministic renderer-neutral geometry. Contain letterboxes in
// destination space; cover center-crops whole source pixels; stretch uses both
// rectangles in full. Invalid or non-finite geometry publishes no layout.
[[nodiscard]] auto make_visual_image_layout(const visual_layout_request_t& request)
    -> std::optional<visual_image_layout_t>;

// Maps a sprite on a logical canvas into a contained destination and rotates
// its four textured corners about the authored origin. This geometry is shared
// by interactive frontends and deterministic offline rendering.
[[nodiscard]] auto make_visual_image_quad(const visual_image_quad_request_t& request)
    -> std::optional<visual_image_quad_t>;

} // namespace wormedit
